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

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/date.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>

#include <boost/algorithm/string.hpp>

//#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE testTimeFromString
//#include <boost/test/unit_test.hpp>
#include <boost/test/included/unit_test.hpp>
#include "SpecUtils/UtilityFunctions.h"


using namespace std;
using namespace boost::unit_test;
using namespace boost::posix_time;
using namespace boost::gregorian;


//time_from_string_strptime (doesn't work on Windows)
//time_from_string_boost ?
BOOST_AUTO_TEST_CASE(timeFromString) 
{
  string indir, input_filename = "";

  const int argc = framework::master_test_suite().argc;
  for( int i = 1; i < argc-1; ++i )
  {
    if( framework::master_test_suite().argv[i] == string("--indir") )
      indir = framework::master_test_suite().argv[i+1];
  }
  
  const string potential_input_paths[] = { ".", indir, "../../testing/", "../../../testing/", "" };
  for( const string dir : potential_input_paths )
  {
    const string potential = UtilityFunctions::append_path( dir, "datetimes.txt" );
    if( UtilityFunctions::is_file(potential) )
      input_filename = potential;
  }
  
  BOOST_REQUIRE_MESSAGE( !input_filename.empty(), "Failed to find input text test file datetimes.txt - you may need to specify the '--indir' command line argument" );

  vector<string> original_string, iso_string;
  ifstream file( input_filename.c_str() );

  BOOST_REQUIRE_MESSAGE( file.is_open(), "Failed to open input text test file '" <<  input_filename << "'" );

  string line;
  while( UtilityFunctions::safe_get_line(file, line) ) 
  {
    if( line.empty() || line[0]=='#' ) 
      continue;
      
    std::vector<std::string> fields;
    UtilityFunctions::split( fields, line, "," );

    if( fields.size() != 2 )
    {
      cerr << "Input line invalid: '" << line << "' should have to fields seprated by a comma" << endl;
      continue; 
    }

    original_string.push_back( fields[0] );
    iso_string.push_back( fields[1] );
  }//while( getline in file )

  assert( original_string.size() == iso_string.size() );
  //BOOST_TEST_MESSAGE( "Will test formats: " << iso_string.size() );

  BOOST_REQUIRE( original_string.size() > 100 );
  
  //convert string to ptime object then convert back to string
  for( size_t i = 0; i < original_string.size(); ++i ) 
  {
    const string orig_fmt_str = original_string[i];
    const string iso_frmt_str = iso_string[i];
    const ptime orig_fmt_answ = UtilityFunctions::time_from_string( orig_fmt_str.c_str() );
    const ptime iso_fmt_answ = UtilityFunctions::time_from_string( iso_frmt_str.c_str() );
        
    const string orig_fmt_answ_str = UtilityFunctions::to_common_string(orig_fmt_answ,true);
    const string iso_fmt_answ_str = UtilityFunctions::to_common_string(iso_fmt_answ,true);
    //bool pass = "not-a-date-time"==s;
      

    BOOST_CHECK_MESSAGE( orig_fmt_answ==iso_fmt_answ, "failed line " << i << " '" 
    << (orig_fmt_str+","+iso_frmt_str) << "' which gave '" << orig_fmt_answ_str << "' and '" << iso_fmt_answ_str << "'" );
  }

  BOOST_TEST_MESSAGE( "Tested " << original_string.size() << " input strings" );
}
