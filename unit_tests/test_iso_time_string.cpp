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

#define BOOST_TEST_MODULE testIsoTimeString
#include <boost/test/unit_test.hpp>

//#define BOOST_TEST_DYN_LINK
// To use boost unit_test as header only (no link to boost unit test library):
//#include <boost/test/included/unit_test.hpp>

#include "3rdparty/date/include/date/date.h"

#include <boost/date_time/date.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>


#include "SpecUtils/DateTime.h"

using namespace std;
using namespace boost::unit_test;


boost::posix_time::ptime to_ptime( const SpecUtils::time_point_t &rhs )
{
  auto dp = ::date::floor<::date::days>(rhs);
  auto ymd = ::date::year_month_day{dp};
  auto time = ::date::make_time(rhs - dp);
  
  boost::posix_time::time_duration td( time.hours().count(),
                                      time.minutes().count(),
                                      time.seconds().count(),
                                      date::floor<chrono::microseconds>(time.subseconds()).count()
                                      );
  
  boost::gregorian::greg_month month{boost::gregorian::Jan};
  switch( static_cast<unsigned>(ymd.month()) )
  {
    case 1: month = boost::gregorian::Jan; break;
    case 2: month = boost::gregorian::Feb; break;
    case 3: month = boost::gregorian::Mar; break;
    case 4: month = boost::gregorian::Apr; break;
    case 5: month = boost::gregorian::May; break;
    case 6: month = boost::gregorian::Jun; break;
    case 7: month = boost::gregorian::Jul; break;
    case 8: month = boost::gregorian::Aug; break;
    case 9: month = boost::gregorian::Sep; break;
    case 10: month = boost::gregorian::Oct; break;
    case 11: month = boost::gregorian::Nov; break;
    case 12: month = boost::gregorian::Dec; break;
    default: assert( 0 ); throw runtime_error( "wth: invalid month?" ); break;
  }
  
  const boost::gregorian::date d( static_cast<int>(ymd.year()), month, (unsigned)ymd.day() );
  
  return boost::posix_time::ptime( d, td );
}//to_ptime(...)


SpecUtils::time_point_t to_time_point( const boost::posix_time::ptime &rhs )
{
  if( rhs.is_special() )
    return {};
  
  unsigned short year = static_cast<unsigned short>( rhs.date().year() );
  const unsigned month = rhs.date().month().as_number();
  const unsigned day = rhs.date().day().as_number();
  
  const int64_t nmicro = rhs.time_of_day().total_microseconds();
  date::year_month_day ymd{ date::year(year), date::month(month), date::day(day) };
  
  date::sys_days days = ymd;
  SpecUtils::time_point_t tp = days;
  tp += chrono::microseconds(nmicro);
  
  return tp;
}

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
    const boost::posix_time::ptime rand_pt = random_ptime();
    
    SpecUtils::time_point_t tp = to_time_point( rand_pt );
    boost::posix_time::ptime pt = to_ptime( tp );
    assert( pt == rand_pt );
    
    const string boost_extended_str = boost::posix_time::to_iso_extended_string(pt);
    const string boost_iso_str = boost::posix_time::to_iso_string(pt);
    string our_extended_str = SpecUtils::to_extended_iso_string(tp);
    string our_iso_str = SpecUtils::to_iso_string(tp);

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
      
  
    const SpecUtils::time_point_t parse_check_iso = SpecUtils::time_from_string( our_iso_str.c_str() );
    BOOST_CHECK_MESSAGE( parse_check_iso == tp,
                         "Failed to read time from ISO string ('" + boost_iso_str
                         + "'), parsed " + SpecUtils::to_iso_string(parse_check_iso) );
    
    const SpecUtils::time_point_t parse_check_ext = SpecUtils::time_from_string( our_extended_str.c_str() );
    
    BOOST_CHECK_MESSAGE( parse_check_ext == tp,
                         "Failed to read time from EXTENDED string ('" + boost_iso_str
                         + "'), parsed " + SpecUtils::to_iso_string(parse_check_ext) );
  }//for( size_t i = 0; i < 1000; ++i )
    
  //check special time values
  SpecUtils::time_point_t d1 = SpecUtils::time_point_t{} + SpecUtils::time_point_t::duration::min();
  SpecUtils::time_point_t d2 = SpecUtils::time_point_t{} + SpecUtils::time_point_t::duration::max();
  SpecUtils::time_point_t d3{};
  SpecUtils::time_point_t d4 = chrono::system_clock::now();

  BOOST_CHECK( SpecUtils::to_iso_string(d1) == "not-a-date-time" );
  BOOST_CHECK( SpecUtils::to_iso_string(d2) == "not-a-date-time" );
  BOOST_CHECK( SpecUtils::to_iso_string(d3) == "not-a-date-time" );
  BOOST_CHECK( SpecUtils::to_iso_string(d4) != "not-a-date-time" );

  //"20140414T141201.621543"
  
  BOOST_CHECK_EQUAL( SpecUtils::to_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01.62") ), "20140919T141201.62" );
  BOOST_CHECK_EQUAL( SpecUtils::to_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01") ), "20140919T141201" );
  BOOST_CHECK_EQUAL( SpecUtils::to_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01.621543") ), "20140919T141201.621543" );
  
  BOOST_CHECK_EQUAL( SpecUtils::to_iso_string( SpecUtils::time_from_string("20140101T14:12:01.62") ), "20140101T141201.62" );
  BOOST_CHECK_EQUAL( SpecUtils::to_iso_string( SpecUtils::time_from_string("20140101T14:12:01.623") ), "20140101T141201.623" );
  BOOST_CHECK_EQUAL( SpecUtils::to_iso_string( SpecUtils::time_from_string("20140101T14:12:01.626") ), "20140101T141201.626" );
  BOOST_CHECK_EQUAL( SpecUtils::to_iso_string( SpecUtils::time_from_string("20140101T00:00:00") ), "20140101T000000" );
  
  
  //"2014-04-14T14:12:01.621543"
  BOOST_CHECK_EQUAL( SpecUtils::to_extended_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01.62") ), "2014-09-19T14:12:01.62" );
  BOOST_CHECK_EQUAL( SpecUtils::to_extended_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01") ), "2014-09-19T14:12:01" );
  BOOST_CHECK_EQUAL( SpecUtils::to_extended_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01.621543") ), "2014-09-19T14:12:01.621543" );
  
  BOOST_CHECK_EQUAL( SpecUtils::to_vax_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01.62") ), "19-Sep-2014 14:12:01.62" );
  BOOST_CHECK_EQUAL( SpecUtils::to_vax_string( SpecUtils::time_from_string("20140101T14:12:01.62") ), "01-Jan-2014 14:12:01.62" );
  BOOST_CHECK_EQUAL( SpecUtils::to_vax_string( SpecUtils::time_from_string("20140101T14:12:01.623") ), "01-Jan-2014 14:12:01.62" );
  BOOST_CHECK_EQUAL( SpecUtils::to_vax_string( SpecUtils::time_from_string("20140101T14:12:01.626") ), "01-Jan-2014 14:12:01.63" );
  BOOST_CHECK_EQUAL( SpecUtils::to_vax_string( SpecUtils::time_from_string("20140101T00:00:00") ), "01-Jan-2014 00:00:00.00" );
  

  /** Converts the input to string in format d-mmm-YYYY HH:MM:SS AM,
   where mmm is 3 char month name; d is day number with no leading zeros.
   Returns "not-a-date-time" if input is not valid.
   Ex. 24hr format: "2014-Sep-9 13:02:15", AMP/PM: "2014-Sep-9 03:02:15 PM"
   */
  //std::string SpecUtils::to_common_string( const boost::posix_time::ptime &t, const bool twenty_four_hour );
  
  
  
  
}
