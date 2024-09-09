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
#include "SpecUtils_config.h"

#include <string>
#include <vector>
#include <numeric>
#include <iostream>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <boost/version.hpp>
#include <boost/functional/hash.hpp>
#include "code_from_boost/hash/hash.hpp"

static_assert( PERFORM_DEVELOPER_CHECKS, "PERFORM_DEVELOPER_CHECKS must be set to on to test hashing (because we need to link to boost)" );

//Untested, but it looks like
// It looks like the version of the hash code we are using was extracted from boost 108500,
//  and from https://www.boost.org/users/history/ , it looks like container_hash was added
//  in 1.81, so I'm guessing this is where the hash code changed.
//  It looks like boost 1.78 doesnt give same hash values as boost 1.85.
static_assert( BOOST_VERSION >= 108100, "Our hashing should only be compared against boost 1.85" );


using namespace std;


// This file tests that the types of variables we hash in `SpecFile::generate_psuedo_uuid()`
//  get the same values for the code lifted from boost, as from boost itself.
TEST_CASE( "Test our boost_hash is same as boost::hash" )
{
  const vector<float> test_floats{ -1.0f, 1.0f,
    std::numeric_limits<float>::min(), -std::numeric_limits<float>::min(),
    std::numeric_limits<float>::epsilon(), -std::numeric_limits<float>::epsilon(),
    std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
    1.1231E-3f, 3.14f, 1.0E-6f, -1.231511E-5f,
    0.0f, -0.0f,
    0.1f*std::numeric_limits<float>::min(), -0.1f*std::numeric_limits<float>::min(),
    std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::quiet_NaN()
  };
  
  for( const float test_val : test_floats )
  {
    CHECK_EQ( boost_hash::hash_value(test_val), boost::hash<float>()(test_val) );
    
    std::size_t our_seed = 0, boost_seed = 0;
    boost_hash::hash_combine( our_seed, test_val );
    boost::hash_combine( boost_seed, test_val );
    CHECK_EQ( our_seed, boost_seed );
  }
  
  {
    std::size_t our_seed = 0, boost_seed = 0;
    boost_hash::hash_combine( our_seed, test_floats );
    boost::hash_combine( boost_seed, test_floats );
    CHECK_EQ( our_seed, boost_seed );
  }
  
  const vector<double> test_doubles{ -1.0, 1.0,
    std::numeric_limits<double>::min(), -std::numeric_limits<double>::min(),
    std::numeric_limits<double>::epsilon(), -std::numeric_limits<double>::epsilon(),
    std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
    1.1231E-3, 3.14, 1.0E-6, -1.231511E-5,
    0.0, -0.0,
    0.1f*std::numeric_limits<double>::min(), -0.1f*std::numeric_limits<double>::min(),
    std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
    std::numeric_limits<double>::quiet_NaN(), -std::numeric_limits<double>::quiet_NaN()
  };
  
  for( const double test_val : test_doubles )
  {
    CHECK_EQ( boost_hash::hash_value(test_val), boost::hash<double>()(test_val) );
    
    std::size_t our_seed = 0, boost_seed = 0;
    boost_hash::hash_combine( our_seed, test_val );
    boost::hash_combine( boost_seed, test_val );
    CHECK_EQ( our_seed, boost_seed );
  }
  
  {
    std::size_t our_seed = 0, boost_seed = 0;
    boost_hash::hash_combine( our_seed, test_doubles );
    boost::hash_combine( boost_seed, test_doubles );
    CHECK_EQ( our_seed, boost_seed );
  }
  
  const vector<size_t> test_sizes{
    size_t(0), size_t(1), size_t(1001), size_t(10), std::numeric_limits<size_t>::max()
  };
  
  for( const size_t test_val : test_sizes )
  {
    CHECK_EQ( boost_hash::hash_value(test_val), boost::hash<size_t>()(test_val) );
    
    std::size_t our_seed = 0, boost_seed = 0;
    boost_hash::hash_combine( our_seed, test_val );
    boost::hash_combine( boost_seed, test_val );
    CHECK_EQ( our_seed, boost_seed );
  }
  
  {
    std::size_t our_seed = 0, boost_seed = 0;
    boost_hash::hash_combine( our_seed, test_sizes );
    boost::hash_combine( boost_seed, test_sizes );
    CHECK_EQ( our_seed, boost_seed );
  }
  
  const vector<int> test_ints{
    -1, 0, 1, 1001, 10, std::numeric_limits<int>::max(), std::numeric_limits<int>::min()
  };
  
  for( const int test_val : test_ints )
  {
    CHECK_EQ( boost_hash::hash_value(test_val), boost::hash<int>()(test_val) );
    
    std::size_t our_seed = 0, boost_seed = 0;
    boost_hash::hash_combine( our_seed, test_val );
    boost::hash_combine( boost_seed, test_val );
    CHECK_EQ( our_seed, boost_seed );
  }
  
  {
    std::size_t our_seed = 0, boost_seed = 0;
    boost_hash::hash_combine( our_seed, test_ints );
    boost::hash_combine( boost_seed, test_ints );
    CHECK_EQ( our_seed, boost_seed );
  }
  
  const vector<string> test_strs{
    "NonEmpty", "", " ", string(" \0 ss\0 ", 7), "Hello", " SomeOtherTest",
    "Aa\xFF", "A", "00", "000", "0000", "00000", "0000\xFF",
    "\xFF\xFF\xFF\xFF\xFF\xFF",
    "\xFF\x00\xFF\x00"
  };
  
  for( const string &test_val : test_strs )
  {
    const std::size_t our_hash = boost_hash::hash_value(test_val);
    const std::size_t boost_hash = boost::hash<std::string>()(test_val);
    CHECK_EQ( our_hash, boost_hash );
    
    std::size_t our_seed = 0, boost_seed = 0;
    boost_hash::hash_combine( our_seed, test_val );
    boost::hash_combine( boost_seed, test_val );
    CHECK_EQ( our_seed, boost_seed );
  }
  
  {
    std::size_t our_seed = 0, boost_seed = 0;
    boost_hash::hash_combine( our_seed, test_strs );
    boost::hash_combine( boost_seed, test_strs );
    CHECK_EQ( our_seed, boost_seed );
  }
}//TEST_CASE( "Test our boost_hash is same as boost::hash" )
