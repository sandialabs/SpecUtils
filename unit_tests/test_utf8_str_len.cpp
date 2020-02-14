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
//#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE test_utf8_str_len
//#include <boost/test/unit_test.hpp>
#include <boost/test/included/unit_test.hpp>

#include "SpecUtils/StringAlgo.h"

using namespace std;
using namespace boost::unit_test;

void check_str( const std::string str, const size_t ncharacters )
{
  const size_t ncounted = SpecUtils::utf8_str_len( str.c_str(), str.size() );
  BOOST_CHECK_MESSAGE( ncharacters == ncounted, "Failed on string '" << str << "' with getting " << ncounted << " but expected " << ncharacters );
}

BOOST_AUTO_TEST_CASE( test_utf8_str_len )
{
  check_str( "", 0 );
  check_str( "A", 1 );
  check_str( "AAA", 3 );
  check_str( u8"÷aõa", 4 );
  check_str( u8"ⓧⓧaaa", 5 );
  check_str( u8"aⓧⓧaaa", 6 );
  check_str( u8"aⓧaⓧaaa", 7 );
  check_str( u8"aⓧ\nⓧaaa", 7 );
  check_str( u8"aⓧ\nⓧaaa÷", 8 );
  check_str( u8"aⓧ\nⓧaaa÷\n", 9 );
}
