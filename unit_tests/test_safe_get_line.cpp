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
#include <vector>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>

#include <boost/algorithm/string.hpp>

#define BOOST_TEST_MODULE testsafeGetLine
#include <boost/test/unit_test.hpp>

//#define BOOST_TEST_DYN_LINK
// To use boost unit_test as header only (no link to boost unit test library):
//#include <boost/test/included/unit_test.hpp>

#include "SpecUtils/ParseUtils.h"


using namespace std;

void test_num_lines( const char *str, const size_t num_expected_lines )
{
  std::stringstream strm(str);
  
  size_t num_read_lines = 0;
  string line;
  while( SpecUtils::safe_get_line(strm, line) )
    ++num_read_lines;
  
  BOOST_CHECK_MESSAGE( num_read_lines==num_expected_lines, "Failed on \n'" + string(str) + "'\n Got "
                      + std::to_string(num_read_lines) + " lines but expected " + std::to_string(num_expected_lines) + "\n" );
  //BOOST_CHECK_EQUAL( num_read_lines, num_expected_lines );
}


BOOST_AUTO_TEST_CASE(safeGetLine) {
  
  const char *str = "1 Hello";
  test_num_lines( str, 1 );
  
  str = "1 Hello\n";
  test_num_lines( str, 2 );
  
  str = "2 Hello\r";
  test_num_lines( str, 2 );
  
  str = "3 Hello\n\r";
  test_num_lines( str, 3 );
  
  str = "3.5 Hello\r\n";
  test_num_lines( str, 2 );
  
  str = "4 Hello\ra";
  test_num_lines( str, 2 );
  
  str = "5 Hello\ra\n";
  test_num_lines( str, 3 );
  
  str = "6 Hello\ra\na";
  test_num_lines( str, 3 );
  
  str = "7 Hello\r\n";
  test_num_lines( str, 2 );
  
  str = "8 Hello\r\na";
  test_num_lines( str, 2 );
  
  str = "9 Hello\n\n";
  test_num_lines( str, 3 );
  
  str = "10 Hello\naaa\n";
  test_num_lines( str, 3 );
  
  str = "11 Hello\naaa\na";
  test_num_lines( str, 3 );
}


//Returns final line for testing
void test_num_lines_len_limit( const char *str, const size_t strsizelimit, const size_t num_expected_lines, const string lastline )
{
  std::stringstream strm(str);
  
  size_t num_read_lines = 0;
  string line;
  vector<string> lines;
  while( SpecUtils::safe_get_line(strm, line, strsizelimit) )
  {
    lines.push_back( line );
    ++num_read_lines;
  }
  
  string msg = "Failed (with line len limit " + std::to_string(strsizelimit)
               + ") on \n'" + string(str) + "'\n Got "
               + std::to_string(num_read_lines) + " lines but expected " + std::to_string(num_expected_lines) + "\n";
  for( const auto l : lines )
    msg += "'" + l + "'\n";
  msg += "___________________\n\n";
  
  BOOST_CHECK_MESSAGE( num_read_lines==num_expected_lines, msg );
  //BOOST_CHECK_EQUAL( num_read_lines, num_expected_lines );
  
  if( !lines.empty() )
  {
    string msg = "Ending check failed ('" + lines.back() + "' != '" + lastline + "') for:\n";
    for( const auto l : lines )
      msg += "'" + l + "'\n";
    msg += "___________________\n\n";
    
    BOOST_CHECK_MESSAGE( lines.back() == lastline, msg );
  }
}//test_num_lines_len_limit


BOOST_AUTO_TEST_CASE(safeGetLineLenLimited) {
  
  const char *str = "1 Hello";
  test_num_lines_len_limit( str, 100, 1, str );
  test_num_lines_len_limit( str, 5, 2, "lo" );
  test_num_lines_len_limit( str, 1, 7, "o" );
  
  str = "1.1 Hello\n";
  test_num_lines_len_limit( str, 1, 10, "" );
  
  str = "2\nHello\r";
  test_num_lines_len_limit( str, 1, 7, "" );
  
  str = "3 Hello\n\r";
  test_num_lines_len_limit( str, 3, 5, "" );
  
  str = "3.5 Hello\r\n";
  test_num_lines_len_limit( str, 3, 4, "" );
  
  str = "4 Hello\ra";
  test_num_lines_len_limit( str, 4, 3, "a" );
  
  str = "5 Hello\ra\n";
  test_num_lines_len_limit( str, 4, 4, "" );
  
  str = "6 Hello\ra\na";
  test_num_lines_len_limit( str, 100, 3, "a" );
  
  str = "7 Hello\r\n";
  test_num_lines_len_limit( str, 100, 2, "" );
  
  str = "\n7.5 Hello\r\n";
  test_num_lines_len_limit( str, 100, 3, "" );
  
  str = "7.6 Hello";
  test_num_lines_len_limit( str, 6, 2, "llo" );
  
  str = "\r\n8 Hello\r\na";
  test_num_lines_len_limit( str, 3, 5, "a" );
  
  str = "9 Hello\n\n";
  test_num_lines_len_limit( str, 10, 3, "" );
  
  str = "10 Hello\naaa\n";
  test_num_lines_len_limit( str, 100, 3, "" );
  
  str = "11 Hello\naaa\na";
  test_num_lines_len_limit( str, 100, 3, "a" );
}





