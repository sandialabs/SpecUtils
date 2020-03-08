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
#include <boost/date_time/date.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <boost/algorithm/string.hpp>

//#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE testIsoTimeString
//#include <boost/test/unit_test.hpp>
#include <boost/test/included/unit_test.hpp>
#include "SpecUtils/DateTime.h"


using namespace std;
using namespace boost::unit_test;
using namespace boost::posix_time;
using namespace boost::gregorian;

boost::posix_time::ptime random_ptime()
{
  const auto mon = boost::date_time::months_of_year( (rand()%12) + 1 );
  
  const int yr = 1401 + (rand()%700);
  int day;
  
  switch( mon )
  {
    case boost::date_time::Feb:
      day = 1 + (rand() % 28);
      break;
      
    case boost::date_time::Sep:
    case boost::date_time::Apr:
    case boost::date_time::Jun:
    case boost::date_time::Nov:
      day = 1 + (rand() % 30);
      break;
    
    case boost::date_time::Aug:
    case boost::date_time::Dec:
    case boost::date_time::Jan:
    case boost::date_time::Jul:
    case boost::date_time::Mar:
    case boost::date_time::May:
    case boost::date_time::Oct:
      day = rand()%31+1;
      break;
      
    case boost::date_time::NotAMonth:
    case boost::date_time::NumMonths:
    default:
      assert(0);
  }//switch( mon )
  
  
  const boost::gregorian::date d(yr, mon, day);

  const int hr = rand() % 24;
  const int min = rand() % 60;
  const int sec = rand() % 60;
  const int frac = rand() % 1000000;
  boost::posix_time::time_duration td( hr, min, sec, frac);

  return boost::posix_time::ptime(d, td);
}//random_ptime()



BOOST_AUTO_TEST_CASE( isoString )
{
  srand( time(NULL) );
  
  //compare to original to_iso_extended_string and to_iso_string function
  //which gives the same thing except with trailing 0's
  for( size_t i = 0; i < 1000; ++i )
  {
    const boost::posix_time::ptime pt = random_ptime();
    
    const string boost_extended_str = boost::posix_time::to_iso_extended_string(pt);
    const string boost_iso_str = boost::posix_time::to_iso_string(pt);
    string our_extended_str = SpecUtils::to_extended_iso_string(pt);
    string our_iso_str = SpecUtils::to_iso_string(pt);

    if( our_extended_str.length() < 26 )
      our_extended_str.resize( 26, '0' );
        
    if( our_iso_str.length() < 22 )
      our_iso_str.resize( 22, '0' );
        
    const boost::posix_time::ptime check = boost::posix_time::from_iso_string( our_iso_str );
    BOOST_CHECK_MESSAGE( pt == check,
                         "failed to read ISO time back in using boost::from_iso_string( '"
                         + our_iso_str + "' ).");
    BOOST_CHECK_MESSAGE( our_extended_str == boost_extended_str,
                         "SpecUtils::to_extended_iso_string produced '"
                         + our_extended_str
                         + "' while boost::to_iso_extended_string produced '"
                         + boost_extended_str + "'." );
    BOOST_CHECK_MESSAGE( our_iso_str == boost_iso_str,
                         "SpecUtils::to_iso_string produced '"
                         + our_iso_str + "' while boost::to_iso_string produced '"
                         + boost_iso_str + "'" );
      
  
    const boost::posix_time::ptime parse_check_iso = SpecUtils::time_from_string( boost_iso_str.c_str() );
    BOOST_CHECK_MESSAGE( parse_check_iso == pt,
                         "Failed to read time from ISO string ('" + boost_iso_str
                         + "'), parsed " + SpecUtils::to_iso_string(parse_check_iso) );
    
    const boost::posix_time::ptime parse_check_ext = SpecUtils::time_from_string( boost_extended_str.c_str() );
    
    BOOST_CHECK_MESSAGE( parse_check_ext == pt,
                         "Failed to read time from EXTENDED string ('" + boost_iso_str
                         + "'), parsed " + SpecUtils::to_iso_string(parse_check_ext) );
  }//for( size_t i = 0; i < 1000; ++i )
    
    //check special time values
  boost::posix_time::ptime d1(neg_infin);
  boost::posix_time::ptime d2(pos_infin);
  boost::posix_time::ptime d3(not_a_date_time);
  boost::posix_time::ptime d4(max_date_time);
  boost::posix_time::ptime d5(min_date_time);
  boost::posix_time::ptime d6(date(2015,Jul,1));

  bool passneg1 = to_iso_extended_string(d1)==SpecUtils::to_extended_iso_string(d1);
  bool passneg2 = to_iso_string(d1)==SpecUtils::to_iso_string(d1);
  BOOST_CHECK(!passneg1);
  BOOST_CHECK(!passneg2);

  bool passpos1 = to_iso_extended_string(d2)==SpecUtils::to_extended_iso_string(d2);
  bool passpos2 = to_iso_string(d2)==SpecUtils::to_iso_string(d2);
  BOOST_CHECK(!passpos1);
  BOOST_CHECK(!passpos2);
  
  bool not1 = to_iso_extended_string(d3)==SpecUtils::to_extended_iso_string(d3);
  bool not2 = to_iso_string(d3)==SpecUtils::to_iso_string(d3);
  BOOST_CHECK(not1);
  BOOST_CHECK(not2);
  
  bool max1 = to_iso_extended_string(d4)==SpecUtils::to_extended_iso_string(d4);
  bool max2 = to_iso_string(d4)==SpecUtils::to_iso_string(d4);
  BOOST_CHECK(max1);
  BOOST_CHECK(max2);
  
  bool min1 = to_iso_extended_string(d5)==SpecUtils::to_extended_iso_string(d5);
  bool min2 = to_iso_string(d5)==SpecUtils::to_iso_string(d5);
  BOOST_CHECK(min1);
  BOOST_CHECK(min2);
  
  bool mid1 = to_iso_extended_string(d6)==SpecUtils::to_extended_iso_string(d6);
  bool mid2 = to_iso_string(d6)==SpecUtils::to_iso_string(d6);
  BOOST_CHECK(mid1);
  BOOST_CHECK(mid2);
}
