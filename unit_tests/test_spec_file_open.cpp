/* SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 
 Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC
 (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
 Government retains certain rights in this software.
 For questions contact William Johnson via email at wcjohns@sandia.gov, or
 alternative emails of interspec@sandia.gov.
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <string>
#include <ostream> 


#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/Filesystem.h"

using namespace std;

// I couldnt quite figure out how to access command line arguments
//  from doctest, so we'll just work around it a bit.
vector<string> g_cl_args;


int main(int argc, char** argv)
{
  for( int i = 0; i < argc; ++i )
    g_cl_args.push_back( argv[i] );
  
  return doctest::Context(argc, argv).run();
}


TEST_CASE( "testFileOpen" )
{
  string indir;
  
  for( size_t i = 1; i < g_cl_args.size(); ++i )
  {
    const string arg = g_cl_args[i];
    
    if( SpecUtils::istarts_with( arg, "--indir=" ) )
      indir = arg.substr( 8 );
  }//for( int arg = 1; arg < argc; ++ arg )
  
  SpecUtils::ireplace_all( indir, "%20", " " );
  
  while( indir.size() && indir[0]=='"' )
    indir = indir.substr( 1 );
  while( indir.size() && indir[indir.size()-1]=='"' )
    indir = indir.substr( 0, indir.size()-1 );
  
  
  CHECK_MESSAGE( !indir.empty(), "No Input directory specified" << indir );
  CHECK_MESSAGE( SpecUtils::is_directory(indir), "Input is not a valid directory: " << indir );
  
  vector<std::string> files = SpecUtils::recursive_ls( indir );
  
  MESSAGE( "Input Directory:" << indir );
  
  for( size_t i = 0; i < files.size(); ++i )
  {
    const string file = files[i];
    
    MESSAGE( "Testing file: '" << file << "'" );
    
    SpecUtils::SpecFile meas;
    const bool loaded = meas.load_file( file, SpecUtils::ParserType::Auto, file );
    CHECK_MESSAGE( loaded, "Failed to load " << file );
    
    if( loaded )
    {
      const double sum_gamma = meas.gamma_count_sum();
      CHECK_MESSAGE( sum_gamma >= 1.0, "No decoded gammas in " << file );
    }
  }//for( size_t i = 0; i < files.size(); ++i )
  
//  CHECK( false );
}
