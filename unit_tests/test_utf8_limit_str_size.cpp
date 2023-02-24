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
#include <vector>
#include <fstream>
#include <cmath>
#include <climits>
#include <iostream>
#include <boost/algorithm/string.hpp>

#define BOOST_TEST_MODULE test_utf8_limit_str_size
#include <boost/test/unit_test.hpp>

//#define BOOST_TEST_DYN_LINK
// To use boost unit_test as header only (no link to boost unit test library):
//#include <boost/test/included/unit_test.hpp>

#include "SpecUtils/StringAlgo.h"


using namespace std;
using namespace boost::unit_test;


BOOST_AUTO_TEST_CASE( test_utf8_limit_str_size )
{
  string teststr = "";
  SpecUtils::utf8_limit_str_size( teststr, 0 );
  BOOST_CHECK_EQUAL( teststr, "" );
  
  teststr = "";
  SpecUtils::utf8_limit_str_size( teststr, 1 );
  BOOST_CHECK_EQUAL( teststr, "" );
  
  teststr = "";
  SpecUtils::utf8_limit_str_size( teststr, 5 );
  BOOST_CHECK_EQUAL( teststr, "" );
  
  teststr = "AAAA";
  SpecUtils::utf8_limit_str_size( teststr, 0 );
  BOOST_CHECK_EQUAL( teststr, "" );
  
  teststr = "AAAA";
  SpecUtils::utf8_limit_str_size( teststr, 1 );
  BOOST_CHECK_EQUAL( teststr, "A" );
  
  teststr = "AAAA";
  SpecUtils::utf8_limit_str_size( teststr, 4 );
  BOOST_CHECK_EQUAL( teststr, "AAAA" );
  
  teststr = "AAA";
  SpecUtils::utf8_limit_str_size( teststr, 3 );
  BOOST_CHECK_EQUAL( teststr, "AAA" );
  
  teststr = "AAA";
  SpecUtils::utf8_limit_str_size( teststr, 4 );
  BOOST_CHECK_EQUAL( teststr, "AAA" );
  
  teststr = u8"ⓧ";
  SpecUtils::utf8_limit_str_size( teststr, 0 );
  BOOST_CHECK_EQUAL( teststr, "" );
  
  teststr = u8"ⓧ";
  SpecUtils::utf8_limit_str_size( teststr, 1 );
  BOOST_CHECK_EQUAL( teststr, "" );
  
  teststr = u8"ⓧ";
  SpecUtils::utf8_limit_str_size( teststr, 2 );
  BOOST_CHECK_EQUAL( teststr, "" );
  
  teststr = u8"ⓧ";
  SpecUtils::utf8_limit_str_size( teststr, 3 );
  BOOST_CHECK_EQUAL( teststr, u8"ⓧ" );
  
  teststr = u8"aⓧ";
  SpecUtils::utf8_limit_str_size( teststr, 1 );
  BOOST_CHECK_EQUAL( teststr, "a" );
  
  teststr = u8"ⓧⓧ";
  SpecUtils::utf8_limit_str_size( teststr, 3 );
  BOOST_CHECK_EQUAL( teststr, u8"ⓧ" );
  
  teststr = u8"ⓧⓧ";
  SpecUtils::utf8_limit_str_size( teststr, 6 );
  BOOST_CHECK_EQUAL( teststr, u8"ⓧⓧ" );
  
  teststr = u8"ⓧⓧaaa";
  SpecUtils::utf8_limit_str_size( teststr, 6 );
  BOOST_CHECK_EQUAL( teststr, u8"ⓧⓧ" );
  
  teststr = u8"ⓧⓧaaa";
  SpecUtils::utf8_limit_str_size( teststr, 7 );
  BOOST_CHECK_EQUAL( teststr, u8"ⓧⓧa" );
  
  
  teststr = u8"aaⓧⓧaaa";
  SpecUtils::utf8_limit_str_size( teststr, 5 );
  BOOST_CHECK_EQUAL( teststr, u8"aaⓧ" );
  
  teststr = u8"aaⓧⓧaaa";
  SpecUtils::utf8_limit_str_size( teststr, 6 );
  BOOST_CHECK_EQUAL( teststr, u8"aaⓧ" );
  
  teststr = u8"aaⓧⓧaaa";
  SpecUtils::utf8_limit_str_size( teststr, 7 );
  BOOST_CHECK_EQUAL( teststr, u8"aaⓧ" );
  
  
  teststr = u8"aõ";
  SpecUtils::utf8_limit_str_size( teststr, 3 );
  BOOST_CHECK_EQUAL( teststr, u8"aõ" );
  
  teststr = u8"õa";
  SpecUtils::utf8_limit_str_size( teststr, 3 );
  BOOST_CHECK_EQUAL( teststr, u8"õa" );
  
  teststr = u8"õ";
  SpecUtils::utf8_limit_str_size( teststr, 3 );
  BOOST_CHECK_EQUAL( teststr, u8"õ" );
  
  teststr = u8"õ";
  SpecUtils::utf8_limit_str_size( teststr, 2 );
  BOOST_CHECK_EQUAL( teststr, u8"õ" );
  
  teststr = u8"õ";
  SpecUtils::utf8_limit_str_size( teststr, 1 );
  BOOST_CHECK_EQUAL( teststr, u8"" );
  
  teststr = u8"÷õ";
  SpecUtils::utf8_limit_str_size( teststr, 2 );
  BOOST_CHECK_EQUAL( teststr, u8"÷" );
  
  teststr = u8"÷õ";
  SpecUtils::utf8_limit_str_size( teststr, 3 );
  BOOST_CHECK_EQUAL( teststr, u8"÷" );
  
  teststr = u8"÷õ";
  SpecUtils::utf8_limit_str_size( teststr, 4 );
  BOOST_CHECK_EQUAL( teststr, u8"÷õ" );
  
  teststr = u8"÷õ";
  SpecUtils::utf8_limit_str_size( teststr, 5 );
  BOOST_CHECK_EQUAL( teststr, u8"÷õ" );
  
  teststr = u8"÷aõ";
  SpecUtils::utf8_limit_str_size( teststr, 5 );
  BOOST_CHECK_EQUAL( teststr, u8"÷aõ" );
  
  teststr = u8"÷aõa";
  SpecUtils::utf8_limit_str_size( teststr, 5 );
  BOOST_CHECK_EQUAL( teststr, u8"÷aõ" );
  
  teststr = u8"÷aõa";
  SpecUtils::utf8_limit_str_size( teststr, 3 );
  BOOST_CHECK_EQUAL( teststr, u8"÷a" );
  
  teststr = u8"÷aõa";
  SpecUtils::utf8_limit_str_size( teststr, 2 );
  BOOST_CHECK_EQUAL( teststr, u8"÷" );
  
  
  teststr = u8"÷aõa";
  SpecUtils::utf8_limit_str_size( teststr, 1 );
  BOOST_CHECK_EQUAL( teststr, u8"" );
  
  teststr = u8"a÷aõa";
  SpecUtils::utf8_limit_str_size( teststr, 1 );
  BOOST_CHECK_EQUAL( teststr, u8"a" );
  
  
  /*
   //Below is for testing that lines read in from two files match after limiting their
   //  size to 45 characters.  But not currently being used.
  string indir;
  const int argc = framework::master_test_suite().argc;
  for( int i = 1; i < argc-1; ++i )
  {
    if( framework::master_test_suite().argv[i] == string("--indir") )
      indir = framework::master_test_suite().argv[i+1];
  }
  
  string test_in_file, test_out_file;
  const string potential_input_paths[] = { ".", indir, "../../testing/", "../../../testing/" };
  for( const string dir : potential_input_paths )
  {
    const string potential = SpecUtils::append_path( dir, "test_data/txt/utf8_limit_str_size_INPUT.txt" );
    if( SpecUtils::is_file(potential) )
    {
      test_in_file = potential;
      test_out_file = SpecUtils::append_path( dir, "test_data/txt/utf8_limit_str_size_OUTPUT.txt" );
    }
  }
  
  if( test_in_file.empty() )
    throw runtime_error( "Could not find 'utf8_limit_str_size_INPUT.txt'"
                        " - you may need to specify the '--indir' command line argument" );
  
  
	ifstream input_check, output_check;
	vector<string> input_vector,output_vector;
  input_check.open( test_in_file.c_str(), ios::in | ios::binary );
	if( !input_check.is_open() )
		throw runtime_error( "Failed to open input file " + test_in_file );
  
  output_check.open ( test_out_file.c_str(), ios::in | ios::binary );
  if( !output_check.is_open() )
    throw runtime_error( "Failed to open output file " + test_out_file );
  
  string line;
  while( SpecUtils::safe_get_line(input_check, line) )
  {
    SpecUtils::utf8_limit_str_size( line , 45 );
    input_vector.push_back( line );
  }
  
  while( SpecUtils::safe_get_line(output_check, line) )
    output_vector.push_back( line );
  
  BOOST_REQUIRE_EQUAL( input_vector.size(), output_vector.size() );
  
	for( size_t i = 0; i < input_vector.size(); ++i )
	{
		BOOST_CHECK_EQUAL( input_vector[i], output_vector[i] );
	}
   */
}
