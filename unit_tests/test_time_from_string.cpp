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

#include <boost/date_time/date.hpp>
#include <boost/algorithm/string.hpp>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/Filesystem.h"
#include "SpecUtils/ParseUtils.h"


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

void compare_delim_duration_from_str( const string test, const double truth )
{
  const double dur = SpecUtils::delimited_duration_string_to_seconds( test );
  
  CHECK_MESSAGE( fabs(truth - dur) < 1.0E-7*fabs(truth),
    "Delimited duration formatted '" << test << "' gave " << dur
     << " seconds, while we expected " << truth << " seconds (diff of "
     << fabs(truth - dur) << ")." );
}//compare_delim_duration_from_str(...)


TEST_CASE( "Duration From String" )
{
  const double minute = 60;
  const double hour = 3600;
  
  compare_delim_duration_from_str("-03:15:12.12", -(3*hour + 15*minute + 12.12) );
  compare_delim_duration_from_str("03:15:12.12", (3*hour + 15*minute + 12.12) );
  compare_delim_duration_from_str("3:15:12.12", (3*hour + 15*minute + 12.12) );
  compare_delim_duration_from_str("3:15:12", (3*hour + 15*minute + 12) );
  compare_delim_duration_from_str("3:05:12", (3*hour + 05*minute + 12) );
  compare_delim_duration_from_str("03:05:12", (3*hour + 05*minute + 12) );
  compare_delim_duration_from_str("03:05:01", (3*hour + 05*minute + 1) );
  compare_delim_duration_from_str("03:05:01.12345", (3*hour + 05*minute + 1.12345) );
  compare_delim_duration_from_str("5:00:00", 5*hour );
  compare_delim_duration_from_str("00:01:00", 1*minute );
  compare_delim_duration_from_str("-00:01:00", -1*minute );
  compare_delim_duration_from_str("-00:01:00", -1*minute );
  compare_delim_duration_from_str("5:00", 5*hour );
  compare_delim_duration_from_str("5:0", 5*hour );
  compare_delim_duration_from_str("5:0:1", 5*hour + 1 );
  compare_delim_duration_from_str("5:0:1.10000", 5*hour + 1.1 );
  compare_delim_duration_from_str("   5:0:1.10000", 5*hour + 1.1 );
  compare_delim_duration_from_str("\t5:0:1.10000", 5*hour + 1.1 );
  compare_delim_duration_from_str("\t5:0:1. ", 5*hour + 1 );
  compare_delim_duration_from_str("\t5:0:1.", 5*hour + 1 );
  compare_delim_duration_from_str("\t5:0:1. ", 5*hour + 1 );
  compare_delim_duration_from_str("\t5:0:1.\t", 5*hour + 1 );
  compare_delim_duration_from_str("5:0:1.\t", 5*hour + 1 );
  compare_delim_duration_from_str("5:0:1. ", 5*hour + 1 );
  compare_delim_duration_from_str("5:0:1.      ", 5*hour + 1 );
  compare_delim_duration_from_str("5:0:1      ", 5*hour + 1 );
  compare_delim_duration_from_str("  5:0:1.0      ", 5*hour + 1 );
  compare_delim_duration_from_str("  -1:0:0      ", -1*hour );
  
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds(":"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds(":32"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds(":32:16"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("--01:32:16"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("12:32a"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("a 12:32"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds(" a 12:32"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds(" a12:32"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("12::1"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("12::1:"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("12:01:-2"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("12:01:2a"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("12:32:"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("12:-32:15.121"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds(":"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("123:60"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("123:61"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("12:"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("a12:01"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("12:01a"), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("12:01a "), std::exception );
  CHECK_THROWS_AS( SpecUtils::delimited_duration_string_to_seconds("12:01 a"), std::exception );
}//TEST_CASE(durationFromString)

void compare_from_str( const string test, const string truth )
{
  const SpecUtils::time_point_t testptime = SpecUtils::time_from_string( test.c_str() );
  const SpecUtils::time_point_t truthptime = SpecUtils::time_from_string( truth.c_str() );
      
  const string test_fmt_str = SpecUtils::to_iso_string(testptime);
  const string truth_fmt_str = SpecUtils::to_iso_string(truthptime);
  
  REQUIRE_MESSAGE( !SpecUtils::is_special(truthptime), "Truth datetime ('" << truth << "') is invalid" );
  
  CHECK_MESSAGE( test_fmt_str==truth_fmt_str,
                       "Date formatted '" << test << "' gave datetime '" << test_fmt_str
                      << "' while we expected '" << truth_fmt_str << "' from ('" << truth << "')" );
}//void compare_from_str( const string test, const string truth )


void minimalTestFormats()
{
  // datetimes.txt contains an extensive collection of formats and variants, but
  //  for concreteness, here is a minimal collection of formats
  //compare_from_str( "15-May-14 08:30:44 pm",  "20140515T203044" );
  //compare_from_str( "15-May-14 08:30:44 Pm",  "20140515T203044" );
  //cerr << "Only ran a couple tests for MinGW" << endl;
  //return;
  
  compare_from_str( "15-May-14 08:30:44 PM",  "20140515T203044" );
  compare_from_str( "2010-01-15T23:21:15Z",   "20100115T232115" );
  compare_from_str( "2010-01-15 23:21:15",    "20100115T232115" );
  compare_from_str( "1-Oct-2004 12:34:42 AM", "20041001T003442" );
  compare_from_str( "1/18/2008 2:54:44 PM",   "20080118T145444" );
  compare_from_str( "08/05/2014 14:51:09",    "20140805T145109" );
  compare_from_str( "14-10-2014 16:15:52",    "20141014T161552" );
  compare_from_str( "14 10 2014 16:15:52",    "20141014T161552" );
  compare_from_str( "16-MAR-06 13:31:02",     "20060316T133102" );
  compare_from_str( "12-SEP-12 11:23:30",     "20120912T112330" );
  compare_from_str( "31-Aug-2005 12:38:04",   "20050831T123804" );
  compare_from_str( "9-Sep-2014T20:29:21 Z",  "20140909T202921" );
  compare_from_str( "10-21-2015 17:20:04",    "20151021T172004" );
  compare_from_str( "21-10-2015 17:20:04",    "20151021T172004" );
  compare_from_str( "26.05.2010 02:53:49",    "20100526T025349" );
  compare_from_str( "04.05.2010 02:53:49",    "20100504T025349" );
  compare_from_str( "May. 21 2013  07:06:42", "20130521T070642" );
  compare_from_str( "28.02.13 13:42:47",      "20130228T134247" );
  compare_from_str( "28.02.2013 13:42:47",    "20130228T134247" );
  compare_from_str( "3.14.06 10:19:36",       "20060314T101936" );
  compare_from_str( "28.02.13 13:42:47",      "20130228T134247" );
  compare_from_str( "3.14.2006 10:19:36",     "20060314T101936" );
  compare_from_str( "28.02.2013 13:42:47",    "20130228T134247" );
  compare_from_str( "2012.07.28 16:48:02",    "20120728T164802" );
  compare_from_str( "01.Nov.2010 21:43:35",   "20101101T214335" );
  compare_from_str( "20100115 23:21:15",      "20100115T232115" );
  compare_from_str( "2017-Jul-07 09:16:37",   "20170707T091637" );
  compare_from_str( "20100115T232115",        "20100115T232115" );
  compare_from_str( "11/18/2018 10:04 AM",    "20181118T100400" );
  compare_from_str( "11/18/2018 10:04 PM",    "20181118T220400" );
  compare_from_str( "11/18/2018 22:04",       "20181118T220400" );
  compare_from_str( "2020/02/12 14:57:39",    "20200212T145739" );
  compare_from_str( "2018-10-09T19-34-31_27", "20181009T193431" );//not sure what the "_27" exactly means)
  compare_from_str( "31-Aug-2005 6:38:04 PM", "20050831T183804" );
  compare_from_str( "31 Aug 2005 6:38:04 pm", "20050831T183804" );
  compare_from_str( "31-Aug-2005 6:38:04 AM", "20050831T063804" );
  compare_from_str( "31 Aug 2005 6:38:04 AM", "20050831T063804" );
  compare_from_str( "01-Jan-2000",            "20000101T000000" );
  compare_from_str( "2010/01/18",             "20100118T000000" );
  compare_from_str( "2010-01-18",             "20100118T000000" );
  compare_from_str( "2015-05-16T05:50:06.7199222-04:00", "20150516T055006.7199222" ); // Time zone will be discarded
  compare_from_str( "2015-05-16T05:50:06.7199228-04:00", "20150516T055006.7199222" ); // Time zone will be discarded, accuracy truncated to microseconds
  
  compare_from_str( "01.Nov.2010 214335", "20101101T214335" );
  compare_from_str( "May. 21 2013 070642", "20130521T070642" );
  compare_from_str( "3.14.2006 10:19:36", "20060314T101936" );
  
  //"Fri, 16 May 2015 05:50:06 GMT"
  compare_from_str( "1997-07-16T19:20:30+01:00", "19970716T192030" );
  compare_from_str( "2070-07-16T19:20:30+01:00", "20700716T192030" );
  //Some examples from https://docs.microsoft.com/en-us/dotnet/standard/base-types/standard-date-and-time-format-strings
  compare_from_str( "6/15/2009 1:45 PM",  "20090615T134500" );
  compare_from_str( "15/06/2009 13:45",  "20090615T134500" );
  compare_from_str( "2009/6/15 13:45",  "20090615T134500" );
  //compare_from_str( "Monday, June 15, 2009 1:45:30 PM",  "2009-06-15T13:45:30" )
  //compare_from_str( "Monday, June 15, 2009 1:45 PM",  "2009-06-15T13:45:30" )
  //compare_from_str( "Monday, June 15, 2009",  "2009-06-15T13:45:30" )
  compare_from_str( "6/15/2009",  "20090615T000000" );
  compare_from_str( "15/06/2009",  "20090615T000000" );
  compare_from_str( "2009/06/15",  "20090615T000000" );
  compare_from_str( "2009-06-15T13:45:30.0000000-07:00",  "20090615T134530" );
  compare_from_str( "2009-06-15T13:45:30.123-07:00",  "20090615T134530.123" );
  compare_from_str( "2009-06-15T13:45:30.0000000Z",  "20090615T134530" );
  compare_from_str( "2009-06-15T13:45:30.0000000",  "20090615T134530" );
  //2009-06-15T13:45:30-07:00 --> 2009-06-15T13:45:30.0000000-07:00",  "2009-06-15T13:45:30" )
  //2009-06-15T13:45:30 -> Mon, 15 Jun 2009 20:45:30 GMT",  "2009-06-15T13:45:30" )  //RFC1123
  compare_from_str( "2009-06-15T13:45:30",  "20090615T134530" );  //Sortable date/time pattern.
  compare_from_str( "2009-06-15T13:45:30",  "20090615T134530" );
  compare_from_str( "06/10/11 15:24:16 +00:00",  "20110610T15:24:16" );
  compare_from_str( "6/15/09 13:12:30", "20090615T131230" );
  compare_from_str( "6/15/09 13:12", "20090615T131200" );
  compare_from_str( "6/15/09 11:12:30 PM", "20090615T231230" );
  //Following formats from https://help.talend.com/reader/3zI67zZ9kaoTVCjNoXuEyw/YHc8JcQYJ7mWCehcQRTEIw
  //ISO 8601 patterns
  compare_from_str( "1999-03-22T05:06:07.000", "19990322T050607" );
  //1999-03-22 AD
  compare_from_str( "1999-03-22+01:00", "19990322T000000" );
  compare_from_str( "19990322", "19990322T000000" );
  compare_from_str( "1999-03-22T05:06:07.000", "19990322T050607" );
  compare_from_str( "1999-03-22T05:06:07.000", "19990322T050607" );
  compare_from_str( "1999-03-22T05:06:07", "19990322T050607" );
  compare_from_str( "1999-03-22T05:06:07.000Z", "19990322T050607" );
  compare_from_str( "1999-03-22T05:06:07.000+01:00", "19990322T050607" );
  compare_from_str( "1999-03-22T05:06:07+01:00", "19990322T050607" );
  //"1999-081+01:00
  compare_from_str( "1999-03-22T05:06:07.000+01:00", "19990322T050607" );
  compare_from_str( "1999-03-22T05:06:07+01:00", "19990322T050607" );
  //Locale en_CA: English, Canada
  compare_from_str( "22/03/99 5:06 AM", "19990322T050600" );
  compare_from_str( "22/03/99 5:06 PM", "19990322T170600" );
  //Monday, March 22, 1999 5:06:07 o'clock AM CET
  compare_from_str( "22-Mar-1999 5:06:07 AM", "19990322T050607" );
  compare_from_str( "22-Mar-1999 5:06:07 PM", "19990322T170607" );
  //Locale en_GB: English, United Kingdom
  //Monday, 22 March 1999
  compare_from_str( "22 March 1999 05:06:07 CET", "19990322T050607" );
  //Monday, 22 March 1999 05:06:07 o'clock CET
  compare_from_str( "22-Mar-1999 05:06:07", "19990322T050607" );
  compare_from_str( "22-Mar-99 05.06.07.000000888 AM", "19990322T050607" );
  compare_from_str( "22-Mar-99 05.06.07.000000888 PM", "19990322T170607" );
  compare_from_str( "22-Mar-99 05.06.07.00000888 AM", "19990322T050607.000008" );
  compare_from_str( "22-Mar-99 05.06.07.00000888 PM", "19990322T170607.000008" );
  compare_from_str( "22-Mar-1999 05.06.07.00000888 AM", "19990322T050607.000008" );
  compare_from_str( "22-Mar-2010 05.06.07.00000888 PM", "20100322T170607.000008" );
  compare_from_str( "22-Mar-1999 05.06.07.000008 AM", "19990322T050607.000008" );
  compare_from_str( "22-Mar-2010 05.06.07.000008 PM", "20100322T170607.000008" );
  compare_from_str( "22-Mar-1999 05.06.07.0000080 AM", "19990322T050607.000008" );
  compare_from_str( "22-Mar-2010 05.06.07.0000080 PM", "20100322T170607.000008" );
  compare_from_str( "22-Mar-1999 05.06.07.0000088 AM", "19990322T050607.000008" );
  compare_from_str( "22-Mar-2010 05.06.07.0000088 PM", "20100322T170607.000008" );
  compare_from_str( "22-Mar-1999 05.06.07.0000008", "19990322T050607" );
  compare_from_str( "22-Mar-1999 05.06.07.000008", "19990322T050607.000008" );
  compare_from_str( "22-Mar-1999 05.06.07.0000088", "19990322T050607.000008" );
  compare_from_str( "22-Mar-1999 05.06.07.00000888 PM", "19990322T170607.000008" );
  
  
  //Locale en_US : English, United States
  compare_from_str( "March 22, 1999", "19990322T000000" );
  //Monday, March 22, 1999
  compare_from_str( "1999/3/22", "19990322T000000" );
  compare_from_str( "3/22/1999", "19990322T000000" );
  compare_from_str( "03/22/1999", "19990322T000000" );
  //22/3/1999
  //1999-03-22+01:00
  compare_from_str( "22/03/1999", "19990322T000000" );
  compare_from_str( "03-22-99 5:06 AM", "19990322T050600" );
  compare_from_str( "03-22-99 5:06 PM", "19990322T170600" );
  compare_from_str( "03/22/99 5:06 AM", "19990322T050600" );
  compare_from_str( "03/22/99 5:06 PM", "19990322T170600" );
  compare_from_str( "3/22/99 5:06 AM", "19990322T050600" );
  compare_from_str( "3/22/99 5:06 PM", "19990322T170600" );
  compare_from_str( "3-22-99 5:06 AM", "19990322T050600" );
  compare_from_str( "3-22-99 5:06 PM", "19990322T170600" );
  compare_from_str( "Mar 22, 1999 5:06:07 AM", "19990322T050607" );
  compare_from_str( "Mar 22, 1999 5:06:07 PM", "19990322T170607" );
  //Monday, March 22, 1999 5:06:07 AM CET
  //Mon Mar 22 05:06:07 CET 1999
  compare_from_str( "22 Mar 1999 05:06:07 +0100", "19990322T050607" );
  compare_from_str( "03-22-1999 5:06:07 AM", "19990322T050607" );
  compare_from_str( "03-22-1999 5:06:07 PM", "19990322T170607" );
  compare_from_str( "3-22-1999 5:06:07 AM", "19990322T050607" );
  compare_from_str( "3-22-1999 5:06:07 PM", "19990322T170607" );
  compare_from_str( "1999-03-22 5:06:07 AM", "19990322T050607" );
  compare_from_str( "1999-03-22 5:06:07 PM", "19990322T170607" );
  compare_from_str( "1999-3-22 5:06:07 AM", "19990322T050607" );
  compare_from_str( "1999-3-22 5:06:07 PM", "19990322T170607" );
  compare_from_str( "1999-03-22 05:06:07.0", "19990322T050607" );
  compare_from_str( "22/03/1999 5:06:07 AM", "19990322T050607" );
  compare_from_str( "22/03/1999 5:06:07 PM", "19990322T170607" );
  compare_from_str( "22/3/1999 5:06:07 AM", "19990322T050607" );
  compare_from_str( "22/3/1999 5:06:07 PM", "19990322T170607" );
  compare_from_str( "03/22/1999 5:06:07 AM", "19990322T050607" );
  compare_from_str( "03/22/1999 5:06:07 PM", "19990322T170607" );
  compare_from_str( "3/22/1999 5:06:07 AM", "19990322T050607" );
  compare_from_str( "3/22/1999 5:06:07 PM", "19990322T170607" );
  compare_from_str( "03/22/99 5:06:07 AM", "19990322T050607" );
  compare_from_str( "03/22/99 5:06:07 PM", "19990322T170607" );
  compare_from_str( "03/22/99 5:06:07", "19990322T050607" );
  compare_from_str( "3/22/99 5:06:07", "19990322T050607" );
  compare_from_str( "22/03/1999 5:06 AM", "19990322T050600" );
  compare_from_str( "22/03/1999 5:06 PM", "19990322T170600" );
  compare_from_str( "22/3/1999 5:06 AM", "19990322T050600" );
  compare_from_str( "22/3/1999 5:06 PM", "19990322T170600" );
  compare_from_str( "03/22/1999 5:06 AM", "19990322T050600" );
  compare_from_str( "03/22/1999 5:06 PM", "19990322T170600" );
  compare_from_str( "3/22/1999 5:06 AM", "19990322T050600" );
  compare_from_str( "3/22/1999 5:06 PM", "19990322T170600" );
  
  compare_from_str( "03-22-99 5:06:07 AM", "19990322T050607" );
  compare_from_str( "03-22-99 5:06:07 PM", "19990322T170607" );
  compare_from_str( "3-22-99 5:06:07 AM", "19990322T050607" );
  compare_from_str( "3-22-99 5:06:07 PM", "19990322T170607" );
  compare_from_str( "03-22-1999 5:06 AM", "19990322T050600" );
  compare_from_str( "03-22-1999 5:06 PM", "19990322T170600" );
  compare_from_str( "3-22-1999 5:06 AM", "19990322T050600" );
  compare_from_str( "3-22-1999 5:06 PM", "19990322T170600" );
  compare_from_str( "1999-03-22 5:06 AM", "19990322T050600" );
  compare_from_str( "1999-03-22 5:06 PM", "19990322T170600" );
  compare_from_str( "1999-3-22 5:06 AM", "19990322T050600" );
  compare_from_str( "1999-3-22 5:06 PM", "19990322T170600" );
  compare_from_str( "Mar.22.1999", "19990322T000000" );
  compare_from_str( "22/Mar/1999 5:06:07 +0100", "19990322T050607" );
  compare_from_str( "22/Mar/99 5:06 AM", "19990322T050600" );
  compare_from_str( "22/Mar/99 5:06 PM", "19990322T170600" );
  //Locale es: Spanish
  compare_from_str( "22.3.99 5:06", "19990322T050600" );
  compare_from_str( "22/03/99 5:06", "19990322T050600" );
  compare_from_str( "22/03/99", "19990322T000000" );
  compare_from_str( "22.03.1999 5:06:07", "19990322T050607" );
  compare_from_str( "22.03.99 5:06", "19990322T050600" );
  //Locale fr_FR: French, France
  //22/03/99
  //22 mars 1999
  compare_from_str( "22/03/99 05:06", "19990322T050600" );
  compare_from_str( "03/22/99 05:06", "19990322T050600" );
  compare_from_str( "3/22/99 05:06", "19990322T050600" );
  compare_from_str( "03-22-99 05:06", "19990322T050600" );
  compare_from_str( "3-22-99 05:06", "19990322T050600" );
  compare_from_str( "03-22-1999 05:06:07", "19990322T050607" );
  compare_from_str( "3-22-1999 05:06:07", "19990322T050607" );
  compare_from_str( "1999-3-22 05:06:07", "19990322T050607" );
  compare_from_str( "22/03/1999 05:06:07", "19990322T050607" );
  compare_from_str( "22/3/1999 05:06:07", "19990322T050607" );
  compare_from_str( "03/22/1999 05:06:07", "19990322T050607" );
  compare_from_str( "3/22/1999 05:06:07", "19990322T050607" );
  compare_from_str( "22/03/99 05:06:07", "19990322T050607" );
  compare_from_str( "03/22/99 05:06:07", "19990322T050607" );
  compare_from_str( "3/22/99 05:06:07", "19990322T050607" );
  compare_from_str( "22/03/1999 05:06", "19990322T050600" );
  compare_from_str( "22/3/1999 05:06", "19990322T050600" );
  compare_from_str( "03/22/1999 05:06", "19990322T050600" );
  compare_from_str( "3/22/1999 05:06", "19990322T050600" );
  compare_from_str( "03-22-99 05:06:07", "19990322T050607" );
  compare_from_str( "3-22-99 05:06:07", "19990322T050607" );
  compare_from_str( "03-22-1999 05:06", "19990322T050600" );
  compare_from_str( "3-22-1999 05:06", "19990322T050600" );
  compare_from_str( "1999-3-22 05:06", "19990322T050600" );
  //Locale it_IT: Italian, Italy
  compare_from_str( "22-mar-1999", "19990322T000000" );
  compare_from_str( "22/03/99 5.06", "19990322T050600" );
  compare_from_str( "99-03-22 05:06", "19990322T050600" );
  compare_from_str( "22-mar-1999 5.06.07", "19990322T050607" );
  //Locale iw: Hebrew
  //05:06 22/03/99
  //05:06:07 22/03/1999
  //Locale ja_JP: Japanese, Japan
  //99/03/22
  //1999/03/22
  //99/03/22 5:06
  //03/22/99 5:06
  //3/22/99 5:06
  //03-22-99 5:06
  //3-22-99 5:06 AM
  //03-22-1999 5:06:07
  //3-22-1999 5:06:07
  //1999-03-22 5:06:07
  //99/03/22 5:06:07
  //3/22/99 5:06:07 AM
  //1999/03/22 5:06
  //22/03/1999 5:06
  //22/3/1999 5:06
  //03/22/1999 5:06
  //3/22/1999 5:06
  //03-22-99 5:06:07
  //3-22-99 5:06:07
  //03-22-1999 5:06
  //3-22-1999 5:06
  //1999-03-22 5:06
  //1999-3-22 5:06
  //
  //http://www.partow.net/programming/datetime/index.html
  //20060314 13:27:54
  //2006/03/14 13:27:54
  //14/03/2006 13:27:54
  //2006-03-14 13:27:54.123  //YYYY-MM-DD HH:MM:SS.mss
  //14-03-2006 13:27:54.123  //DD-MM-YYYY HH:MM:SS.mss
  //2006-03-14 13:27:54
  //14-03-2006 13:27:54
  //2006-03-14T13:27:54
  //2006-03-14T13:27:54.123
  //20060314T13:27:54
  //20060314T13:27:54.123
  //14-03-2006T13:27:54.123
  //14-03-2006T13:27:54
  //20060314T1327
  //20060314T132754
  //20060314T132754123
  //2006-03-04T13:27:54+03:45
  //2006-03-04T13:27:54-01:28
  //2006-03-04T13:27+03:45
  //2006-03-04T13:27-01:28
  //04/Mar/2006:13:27:54 -0537
  //17/Sep/2006:18:12:45 +1142
  //Sat, 04 Mar 2006 13:27:54 GMT
  //Sat, 04 Mar 2006 13:27:54 -0234

}//void minimalTestFormats()


TEST_CASE( "Time From String" )
{
  minimalTestFormats();
  
  // "datetimes.txt" contains a lot of date/times that could be seen in spectrum files
  string indir, input_filename = "";

  for( size_t i = 1; i < g_cl_args.size()-1; ++i )
  {
    if( g_cl_args[i] == string("--indir") )
      indir = g_cl_args[i+1];
  }
  
  // We will look for "datetimes.txt", in a not-so-elegant way
  const string potential_input_paths[] = {
    indir,
    "",
    ".",
    "./test_data/",
    "./unit_tests/test_data/",
    "../unit_tests/test_data/",
    "../../unit_tests/test_data/",
    "../../../unit_tests/test_data/",
    "../../testing/",
    "../../../testing/",
    "../../../../testing/",
    "../../../../../testing/"
  };
  
  for( const string dir : potential_input_paths )
  {
    const string potential = SpecUtils::append_path( dir, "datetimes.txt" );
    if( SpecUtils::is_file(potential) )
      input_filename = potential;
  }
  
  REQUIRE_MESSAGE( !input_filename.empty(),
                        "Failed to find input text test file datetimes.txt - you may need to specify the '--indir' command line argument" );

  vector<string> original_string, iso_string;
  ifstream file( input_filename.c_str() );

  REQUIRE_MESSAGE( file.is_open(), "Failed to open input text test file '" <<  input_filename << "'" );

  string line;
  while( SpecUtils::safe_get_line(file, line) ) 
  {
    if( line.empty() || line[0]=='#' ) 
      continue;
      
    std::vector<std::string> fields;
    SpecUtils::split( fields, line, "," );

    if( fields.size() != 2 )
    {
      cerr << "Input line invalid: '" << line << "' should have to fields separated by a comma" << endl;
      continue;
    }

    original_string.push_back( fields[0] );
    iso_string.push_back( fields[1] );
  }//while( getline in file )

  assert( original_string.size() == iso_string.size() );
  //BOOST_TEST_MESSAGE( "Will test formats: " << iso_string.size() );

  REQUIRE( original_string.size() > 100 );
  
  //convert string to ptime object then convert back to string
  for( size_t i = 0; i < original_string.size(); ++i ) 
  {
    const string orig_fmt_str = original_string[i];
    const string iso_frmt_str = iso_string[i];
    const SpecUtils::time_point_t orig_fmt_answ = SpecUtils::time_from_string( orig_fmt_str.c_str() );
    const SpecUtils::time_point_t iso_fmt_answ = SpecUtils::time_from_string( iso_frmt_str.c_str() );
        
    const string orig_fmt_answ_str = SpecUtils::to_common_string(orig_fmt_answ,true);
    const string iso_fmt_answ_str = SpecUtils::to_common_string(iso_fmt_answ,true);
    //bool pass = "not-a-date-time"==s;
      

    CHECK_MESSAGE( orig_fmt_answ==iso_fmt_answ, "failed line " << i << " '"
    << (orig_fmt_str+","+iso_frmt_str) << "' which gave '" << orig_fmt_answ_str << "' and '" << iso_fmt_answ_str << "'" );
  }

  MESSAGE( "Tested " << original_string.size() << " input strings" );
}
