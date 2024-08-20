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

#include <tuple>
#include <string>
#include <time.h>
#include <vector>
#include <stdio.h>
#include <iostream>
#include <stdlib.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "3rdparty/date/include/date/date.h"

#if(PERFORM_DEVELOPER_CHECKS)
// We will do additional checks against what we would expect from boost,
//  only if we have `PERFORM_DEVELOPER_CHECKS` enabled; otherwise we
//  wont be using/linking to boost
#include <boost/date_time/date.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#endif //PERFORM_DEVELOPER_CHECKS

#include "SpecUtils/DateTime.h"

using namespace std;

#if( PERFORM_DEVELOPER_CHECKS )
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
#endif // PERFORM_DEVELOPER_CHECKS


SpecUtils::time_point_t random_time_point()
{
  //unsigned short year = static_cast<unsigned short>( 1401 + (rand() % 700) );
  //const unsigned month = 1 + (rand() % 12);
  //const unsigned day = (rand() % 28);
  
  //const int64_t nmicro = rhs.time_of_day().total_microseconds();
  //date::year_month_day ymd{ date::year(year), date::month(month), date::day(day) };
  
  //date::sys_days days = ymd;
  //SpecUtils::time_point_t tp = days;
  //tp += chrono::microseconds(nmicro);
  
  SpecUtils::time_point_t tp{}; //01-Jan-1970 00:00:00, I think.
  tp += chrono::hours( rand() % (130*364*24) );
  tp += chrono::microseconds( rand() % (60LL*60LL*1000000LL) );
  
  return tp;
}//SpecUtils::time_point_t random_time_point()


TEST_CASE( "isoString" )
{
  srand( (unsigned) time(NULL) );
  
  // Check that we can round-trip a date-time to/from ISO and extended ISO format
  for( size_t i = 0; i < 1000; ++i )
  {
    const SpecUtils::time_point_t random_tp = random_time_point();
    //random_tp.time_since_epoch().count()
    
    string our_extended_str = SpecUtils::to_extended_iso_string(random_tp);
    string our_iso_str = SpecUtils::to_iso_string(random_tp);
    
    //if( our_extended_str.length() < 26 )
    //  our_extended_str.resize( 26, '0' );
    //if( our_iso_str.length() < 22 )
    //  our_iso_str.resize( 22, '0' );
    
    const SpecUtils::time_point_t parse_check_iso = SpecUtils::time_from_string( our_iso_str.c_str() );
    CHECK_MESSAGE( parse_check_iso == random_tp,
                         "Failed to read time from ISO string ('" << our_iso_str
                         + "'), parsed " + SpecUtils::to_iso_string(parse_check_iso) );
    
    const SpecUtils::time_point_t parse_check_ext = SpecUtils::time_from_string( our_extended_str.c_str() );
    
    CHECK_MESSAGE( parse_check_ext == random_tp,
                  "Failed to read time from EXTENDED string ('" << parse_check_ext
                  << "'), parsed " << SpecUtils::to_iso_string(parse_check_ext) );
  }//for( size_t i = 0; i < 1000; ++i )
  
  
#if(PERFORM_DEVELOPER_CHECKS)
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

    //cout << "{" << tp.time_since_epoch().count() << ",\"" << our_iso_str << "\", \"" << our_extended_str << "\"}," << endl;
    
    const boost::posix_time::ptime check = boost::posix_time::from_iso_string( our_iso_str );
    CHECK_MESSAGE( pt == check,
                  "failed to read ISO time back in using boost::from_iso_string( '"
                  << our_iso_str << "' ).");
    CHECK_MESSAGE( our_extended_str == boost_extended_str,
                  "SpecUtils::to_extended_iso_string produced '"
                  << our_extended_str
                  << "' while boost::to_iso_extended_string produced '"
                  << boost_extended_str << "'." );
    CHECK_MESSAGE( our_iso_str == boost_iso_str,
                  "SpecUtils::to_iso_string produced '"
                  << our_iso_str << "' while boost::to_iso_string produced '"
                  << boost_iso_str << "'" );
      
  
    const SpecUtils::time_point_t parse_check_iso = SpecUtils::time_from_string( our_iso_str.c_str() );
    CHECK_MESSAGE( parse_check_iso == tp,
                         "Failed to read time from ISO string ('" << boost_iso_str
                         + "'), parsed " + SpecUtils::to_iso_string(parse_check_iso) );
    
    const SpecUtils::time_point_t parse_check_ext = SpecUtils::time_from_string( our_extended_str.c_str() );
    
    CHECK_MESSAGE( parse_check_ext == tp,
                  "Failed to read time from EXTENDED string ('" << boost_iso_str
                  << "'), parsed " << SpecUtils::to_iso_string(parse_check_ext) );
  }//for( size_t i = 0; i < 1000; ++i )

#endif //#if(PERFORM_DEVELOPER_CHECKS)
  
  // 20240808: this next variable holds  <microseconds since epoch, iso string, extended iso str>
  //   values that were validated against boost (in the code a little above), from 1-Jan-1970 to
  //   2100.
  //  I am really not sure if we should expect these tests to pass on different computers and
  //  operating systems, since maybe different systems handle a leap second different here or there.
  const vector<tuple<int64_t,string,string>> test_iso_counts = {
    {721926293297432,"19921116T150453.297432", "1992-11-16T15:04:53.297432"},
    {2328244886831047,"20431012T064126.831047", "2043-10-12T06:41:26.831047"},
    {2070889253213130,"20350816T150053.213130", "2035-08-16T15:00:53.213130"},
    {2170563132806966,"20381013T061212.806966", "2038-10-13T06:12:12.806966"},
    {2508747014182889,"20490701T101014.182889", "2049-07-01T10:10:14.182889"},
    {3010699211744637,"20650528T012011.744637", "2065-05-28T01:20:11.744637"},
    {2491401464854923,"20481212T155744.854923", "2048-12-12T15:57:44.854923"},
    {1232617244922087,"20090122T094044.922087", "2009-01-22T09:40:44.922087"},
    {1265806379179537,"20100210T125259.179537", "2010-02-10T12:52:59.179537"},
    {2634978431459255,"20530701T102711.459255", "2053-07-01T10:27:11.459255"},
    {3665721182059272,"20860228T075302.059272", "2086-02-28T07:53:02.059272"},
    {3811624763849168,"20901014T003923.849168", "2090-10-14T00:39:23.849168"},
    {3385397426173554,"20770411T201026.173554", "2077-04-11T20:10:26.173554"},
    {2875019790258081,"20610207T163630.258081", "2061-02-07T16:36:30.258081"},
    {3781303064380073,"20891028T015744.380073", "2089-10-28T01:57:44.380073"},
    {2336897940678666,"20440120T101900.678666", "2044-01-20T10:19:00.678666"},
    {2699674178143479,"20550720T052938.143479", "2055-07-20T05:29:38.143479"},
    {2681608365649600,"20541223T031245.649600", "2054-12-23T03:12:45.649600"},
    {4028234800366287,"20970825T020640.366287", "2097-08-25T02:06:40.366287"},
    {2066697357743887,"20350629T023557.743887", "2035-06-29T02:35:57.743887"},
    {983535209312996,"20010302T121329.312996", "2001-03-02T12:13:29.312996"},
    {2435457673658068,"20470306T040113.658068", "2047-03-06T04:01:13.658068"},
    {1809417362440482,"20270504T075602.440482", "2027-05-04T07:56:02.440482"},
    {363016862089711,"19810703T140102.089711", "1981-07-03T14:01:02.089711"},
    {658681707644150,"19901115T150827.644150", "1990-11-15T15:08:27.644150"},
    {1405691679506325,"20140718T135439.506325", "2014-07-18T13:54:39.506325"},
    {1735150068067672,"20241225T180748.067672", "2024-12-25T18:07:48.067672"},
    {3630362681656728,"20850115T020441.656728", "2085-01-15T02:04:41.656728"},
    {1126822215720868,"20050915T221015.720868", "2005-09-15T22:10:15.720868"},
    {1680414038429666,"20230402T054038.429666", "2023-04-02T05:40:38.429666"},
    {141836308886106,"19740630T145828.886106", "1974-06-30T14:58:28.886106"},
    {3807270486177452,"20900824T150806.177452", "2090-08-24T15:08:06.177452"},
    {2020677579532082,"20340112T111939.532082", "2034-01-12T11:19:39.532082"},
    {331047518922011,"19800628T133838.922011", "1980-06-28T13:38:38.922011"},
    {3359790581751322,"20760619T110941.751322", "2076-06-19T11:09:41.751322"},
    {3919335159999810,"20940313T161239.999810", "2094-03-13T16:12:39.999810"},
    {296397025011257,"19790524T123025.011257", "1979-05-24T12:30:25.011257"},
    {673531024573417,"19910506T115704.573417", "1991-05-06T11:57:04.573417"},
    {797231789599904,"19950407T051629.599904", "1995-04-07T05:16:29.599904"},
    {2463985711616069,"20480130T082831.616069", "2048-01-30T08:28:31.616069"},
    {871827613118929,"19970817T142013.118929", "1997-08-17T14:20:13.118929"},
    {3677256191982300,"20860711T200311.982300", "2086-07-11T20:03:11.982300"},
    {2419394752020620,"20460901T060552.020620", "2046-09-01T06:05:52.020620"},
    {4078433840227075,"20990329T021720.227075", "2099-03-29T02:17:20.227075"},
    {225299876309516,"19770220T151756.309516", "1977-02-20T15:17:56.309516"},
    {1046969626378267,"20030306T165346.378267", "2003-03-06T16:53:46.378267"},
    {2357358977240383,"20440913T055617.240383", "2044-09-13T05:56:17.240383"},
    {3920183352191441,"20940323T114912.191441", "2094-03-23T11:49:12.191441"},
    {42708938217307,"19710510T073538.217307", "1971-05-10T07:35:38.217307"},
    {2769049040804430,"20570930T041720.804430", "2057-09-30T04:17:20.804430"},
    {2353617151088180,"20440731T223231.088180", "2044-07-31T22:32:31.088180"},
    {1740747384107886,"20250228T125624.107886", "2025-02-28T12:56:24.107886"},
    {1739960890164614,"20250219T102810.164614", "2025-02-19T10:28:10.164614"},
    {1003186026161521,"20011015T224706.161521", "2001-10-15T22:47:06.161521"},
    {1159453419593477,"20060928T142339.593477", "2006-09-28T14:23:39.593477"},
    {1312806866222737,"20110808T123426.222737", "2011-08-08T12:34:26.222737"},
    {661987097427748,"19901223T211817.427748", "1990-12-23T21:18:17.427748"},
    {3805907308971991,"20900808T202828.971991", "2090-08-08T20:28:28.971991"},
    {1447636933912341,"20151116T012213.912341", "2015-11-16T01:22:13.912341"},
    {156300525637905,"19741215T004845.637905", "1974-12-15T00:48:45.637905"},
    {1664988662795487,"20221005T165102.795487", "2022-10-05T16:51:02.795487"},
    {3995566126350494,"20960811T232846.350494", "2096-08-11T23:28:46.350494"},
    {3749342996768107,"20881023T040956.768107", "2088-10-23T04:09:56.768107"},
    {492505833047213,"19850810T071033.047213", "1985-08-10T07:10:33.047213"},
    {2629685705299965,"20530501T041505.299965", "2053-05-01T04:15:05.299965"},
    {2094798719741358,"20360519T083159.741358", "2036-05-19T08:31:59.741358"},
    {3523228277378895,"20810824T023117.378895", "2081-08-24T02:31:17.378895"},
    {930875963712192,"19990702T003923.712192", "1999-07-02T00:39:23.712192"},
    {1339189276335263,"20120608T210116.335263", "2012-06-08T21:01:16.335263"},
    {1109123973029421,"20050223T015933.029421", "2005-02-23T01:59:33.029421"},
    {647569516369823,"19900710T002516.369823", "1990-07-10T00:25:16.369823"},
    {1704868904468946,"20240110T064144.468946", "2024-01-10T06:41:44.468946"},
    {551871198760748,"19870628T093318.760748", "1987-06-28T09:33:18.760748"},
    {3735443775805757,"20880515T071615.805757", "2088-05-15T07:16:15.805757"},
    {1608901006854212,"20201225T125646.854212", "2020-12-25T12:56:46.854212"},
    {1112775119191307,"20050406T081159.191307", "2005-04-06T08:11:59.191307"},
    {2197166434950833,"20390817T040034.950833", "2039-08-17T04:00:34.950833"},
    {1456030036827048,"20160221T044716.827048", "2016-02-21T04:47:16.827048"},
    {3124035909694026,"20681229T194509.694026", "2068-12-29T19:45:09.694026"},
    {1952254797160151,"20311112T125957.160151", "2031-11-12T12:59:57.160151"},
    {3538948590194089,"20820222T011630.194089", "2082-02-22T01:16:30.194089"},
    {2457669474125714,"20471118T055754.125714", "2047-11-18T05:57:54.125714"},
    {914646789641586,"19981226T043309.641586", "1998-12-26T04:33:09.641586"},
    {971139710871809,"20001010T010150.871809", "2000-10-10T01:01:50.871809"},
    {1009815559493690,"20011231T161919.493690", "2001-12-31T16:19:19.493690"},
    {2790363225143880,"20580603T205345.143880", "2058-06-03T20:53:45.143880"},
    {3356764561046452,"20760515T103601.046452", "2076-05-15T10:36:01.046452"},
    {1590028539303136,"20200521T023539.303136", "2020-05-21T02:35:39.303136"},
    {3137364525448305,"20690602T020845.448305", "2069-06-02T02:08:45.448305"},
    {128390450540059,"19740126T000050.540059", "1974-01-26T00:00:50.540059"},
    {2666278951736898,"20540628T170231.736898", "2054-06-28T17:02:31.736898"},
    {3952031547619188,"20950327T023227.619188", "2095-03-27T02:32:27.619188"},
    {701643794303178,"19920326T210314.303178", "1992-03-26T21:03:14.303178"},
    {2643807881096508,"20531011T150441.096508", "2053-10-11T15:04:41.096508"},
    {384946769888110,"19820314T093929.888110", "1982-03-14T09:39:29.888110"},
    {1188438106398284,"20070830T014146.398284", "2007-08-30T01:41:46.398284"},
    {769000665866490,"19940515T111745.866490", "1994-05-15T11:17:45.866490"},
    {2313681165252892,"20430426T171245.252892", "2043-04-26T17:12:45.252892"},
    {3566824405752092,"20830110T163325.752092", "2083-01-10T16:33:25.752092"},
    {3816373845584069,"20901207T235045.584069", "2090-12-07T23:50:45.584069"},
    {2306980125135084,"20430208T034845.135084", "2043-02-08T03:48:45.135084"},
    {3689262597508437,"20861127T190957.508437", "2086-11-27T19:09:57.508437"},
    {1333853405900039,"20120408T025005.900039", "2012-04-08T02:50:05.900039"},
    {4070506188914152,"20981227T080948.914152", "2098-12-27T08:09:48.914152"},
    {1518221157291138,"20180210T000557.291138", "2018-02-10T00:05:57.291138"},
    {2413459234839010,"20460624T132034.839010", "2046-06-24T13:20:34.839010"},
    {2186275741415080,"20390413T024901.415080", "2039-04-13T02:49:01.415080"},
    {963224639657574,"20000710T102359.657574", "2000-07-10T10:23:59.657574"},
    {3087148799719350,"20671029T211959.719350", "2067-10-29T21:19:59.719350"},
    {2112543662358312,"20361210T174102.358312", "2036-12-10T17:41:02.358312"},
    {3217903952748197,"20711221T061232.748197", "2071-12-21T06:12:32.748197"},
    {2072824692608668,"20350908T003812.608668", "2035-09-08T00:38:12.608668"},
    {905623148087034,"19980912T175908.087034", "1998-09-12T17:59:08.087034"},
    {1527643298894529,"20180530T012138.894529", "2018-05-30T01:21:38.894529"},
    {4008703446597533,"20970111T004406.597533", "2097-01-11T00:44:06.597533"},
    {2798710461204243,"20580908T113421.204243", "2058-09-08T11:34:21.204243"},
    {819150780691613,"19951216T215300.691613", "1995-12-16T21:53:00.691613"},
    {2399504232683514,"20460114T005712.683514", "2046-01-14T00:57:12.683514"},
    {3144375903244278,"20690822T054503.244278", "2069-08-22T05:45:03.244278"},
    {3487073368095605,"20800701T152928.095605", "2080-07-01T15:29:28.095605"},
    {1081063465261966,"20040404T072425.261966", "2004-04-04T07:24:25.261966"},
    {2714542105652932,"20560108T072825.652932", "2056-01-08T07:28:25.652932"},
    {3209628884872706,"20710916T113444.872706", "2071-09-16T11:34:44.872706"},
    {497622252321947,"19851008T122412.321947", "1985-10-08T12:24:12.321947"},
    {2192741769562200,"20390626T225609.562200", "2039-06-26T22:56:09.562200"},
    {1866246368952905,"20290220T014608.952905", "2029-02-20T01:46:08.952905"},
    {3896057936903123,"20930617T061856.903123", "2093-06-17T06:18:56.903123"},
    {2945367687775857,"20630502T214127.775857", "2063-05-02T21:41:27.775857"},
    {235243602157606,"19770615T172642.157606", "1977-06-15T17:26:42.157606"},
    {192322191399831,"19760204T224951.399831", "1976-02-04T22:49:51.399831"},
    {3457972569004964,"20790730T195609.004964", "2079-07-30T19:56:09.004964"},
    {2183224555041304,"20390308T191555.041304", "2039-03-08T19:15:55.041304"},
    {3312750395561569,"20741223T002635.561569", "2074-12-23T00:26:35.561569"},
    {3128798754185391,"20690222T224554.185391", "2069-02-22T22:45:54.185391"},
    {3297137939822631,"20740625T073859.822631", "2074-06-25T07:38:59.822631"},
    {2050979001386546,"20341229T042321.386546", "2034-12-29T04:23:21.386546"},
    {3070654703331467,"20670421T233823.331467", "2067-04-21T23:38:23.331467"},
    {244337658285411,"19770928T233418.285411", "1977-09-28T23:34:18.285411"},
    {1504317739055715,"20170902T020219.055715", "2017-09-02T02:02:19.055715"},
    {2555733816549789,"20501227T060336.549789", "2050-12-27T06:03:36.549789"},
    {1548171729875195,"20190122T154209.875195", "2019-01-22T15:42:09.875195"},
    {2497769661598486,"20490224T085421.598486", "2049-02-24T08:54:21.598486"},
    {3718898770179344,"20871105T192610.179344", "2087-11-05T19:26:10.179344"},
    {3273262994560486,"20730921T234314.560486", "2073-09-21T23:43:14.560486"},
    {3965856343074886,"20950903T024543.074886", "2095-09-03T02:45:43.074886"},
    {3018925263522276,"20650831T062103.522276", "2065-08-31T06:21:03.522276"},
    {1006326360706292,"20011121T070600.706292", "2001-11-21T07:06:00.706292"},
    {3502692735946735,"20801229T101215.946735", "2080-12-29T10:12:15.946735"},
    {2686405212193379,"20550216T154012.193379", "2055-02-16T15:40:12.193379"},
    {2033306974617090,"20340607T152934.617090", "2034-06-07T15:29:34.617090"},
    {602716589271061,"19890205T211629.271061", "1989-02-05T21:16:29.271061"},
    {2725806467945739,"20560517T162747.945739", "2056-05-17T16:27:47.945739"},
    {1337912152293410,"20120525T021552.293410", "2012-05-25T02:15:52.293410"},
    {3725204157372974,"20880117T185557.372974", "2088-01-17T18:55:57.372974"},
    {631870084406009,"19900109T072804.406009", "1990-01-09T07:28:04.406009"},
    {1821958074434017,"20270926T112754.434017", "2027-09-26T11:27:54.434017"},
    {1290871240459802,"20101127T152040.459802", "2010-11-27T15:20:40.459802"},
    {3441334813720126,"20790119T062013.720126", "2079-01-19T06:20:13.720126"},
    {164211874853339,"19750316T142434.853339", "1975-03-16T14:24:34.853339"},
    {1841796565078009,"20280513T020925.078009", "2028-05-13T02:09:25.078009"},
    {3781729319170386,"20891102T002159.170386", "2089-11-02T00:21:59.170386"},
    {3429946586951803,"20780909T105626.951803", "2078-09-09T10:56:26.951803"},
    {3784651008620378,"20891205T195648.620378", "2089-12-05T19:56:48.620378"},
    {3802962647141228,"20900705T183047.141228", "2090-07-05T18:30:47.141228"},
    {3682009005515220,"20860904T201645.515220", "2086-09-04T20:16:45.515220"},
    {3928235001722685,"20940624T162321.722685", "2094-06-24T16:23:21.722685"},
    {2517616359987624,"20491012T015239.987624", "2049-10-12T01:52:39.987624"},
    {478343291817270,"19850227T090811.817270", "1985-02-27T09:08:11.817270"},
    {2260762801976326,"20410822T054001.976326", "2041-08-22T05:40:01.976326"},
    {3911679122563566,"20931215T013202.563566", "2093-12-15T01:32:02.563566"},
    {2603972537870430,"20520707T134217.870430", "2052-07-07T13:42:17.870430"},
    {1897366379820090,"20300215T061259.820090", "2030-02-15T06:12:59.820090"},
    {1903038424822644,"20300421T214704.822644", "2030-04-21T21:47:04.822644"},
    {1703057167071225,"20231220T072607.071225", "2023-12-20T07:26:07.071225"},
    {474867077387585,"19850118T033117.387585", "1985-01-18T03:31:17.387585"},
    {3469765631105199,"20791214T074711.105199", "2079-12-14T07:47:11.105199"},
    {209022953765736,"19760816T055553.765736", "1976-08-16T05:55:53.765736"},
    {2742082182798817,"20561122T012942.798817", "2056-11-22T01:29:42.798817"},
    {644917973939242,"19900609T075253.939242", "1990-06-09T07:52:53.939242"},
    {1740130271571915,"20250221T093111.571915", "2025-02-21T09:31:11.571915"},
    {3616114994988574,"20840803T042314.988574", "2084-08-03T04:23:14.988574"},
    {1698321732076354,"20231026T120212.076354", "2023-10-26T12:02:12.076354"},
    {2502110096021565,"20490415T143456.021565", "2049-04-15T14:34:56.021565"},
    {2312801429807644,"20430416T125029.807644", "2043-04-16T12:50:29.807644"},
    {3921019532682167,"20940402T040532.682167", "2094-04-02T04:05:32.682167"},
    {1243793429380084,"20090531T181029.380084", "2009-05-31T18:10:29.380084"},
    {2244487787839414,"20410214T204947.839414", "2041-02-14T20:49:47.839414"},
    {2781414312039327,"20580220T070512.039327", "2058-02-20T07:05:12.039327"},
    {653960559490695,"19900921T234239.490695", "1990-09-21T23:42:39.490695"},
    {1102024431716348,"20041202T215351.716348", "2004-12-02T21:53:51.716348"},
    {1994312629537223,"20330313T074349.537223", "2033-03-13T07:43:49.537223"},
    {2577497142712339,"20510905T032542.712339", "2051-09-05T03:25:42.712339"},
    {3942909202334738,"20941211T123322.334738", "2094-12-11T12:33:22.334738"},
    {3224031336059923,"20720301T041536.059923", "2072-03-01T04:15:36.059923"},
    {3370220315748059,"20761018T041835.748059", "2076-10-18T04:18:35.748059"},
    {4017962312415930,"20970428T043832.415930", "2097-04-28T04:38:32.415930"},
    {2443026432196435,"20470601T182712.196435", "2047-06-01T18:27:12.196435"},
    {1268645274875306,"20100315T092754.875306", "2010-03-15T09:27:54.875306"},
    {2685773571958794,"20550209T081251.958794", "2055-02-09T08:12:51.958794"},
    {4071891375404160,"20990112T085615.404160", "2099-01-12T08:56:15.404160"},
    {3904326453243266,"20930920T230733.243266", "2093-09-20T23:07:33.243266"},
    {986276046513157,"20010403T053406.513157", "2001-04-03T05:34:06.513157"},
    {1068396418833048,"20031109T164658.833048", "2003-11-09T16:46:58.833048"},
    {924959997559125,"19990424T131957.559125", "1999-04-24T13:19:57.559125"},
    {2901893144010356,"20611215T172544.010356", "2061-12-15T17:25:44.010356"},
    {3919039241028677,"20940310T060041.028677", "2094-03-10T06:00:41.028677"},
    {2079019955377471,"20351118T173235.377471", "2035-11-18T17:32:35.377471"},
    {3679930692452170,"20860811T185812.452170", "2086-08-11T18:58:12.452170"},
    {1354463638975995,"20121202T155358.975995", "2012-12-02T15:53:58.975995"},
    {38394466101134,"19710321T090746.101134", "1971-03-21T09:07:46.101134"},
    {843015937496932,"19960918T030537.496932", "1996-09-18T03:05:37.496932"},
    {3297368664186449,"20740627T234424.186449", "2074-06-27T23:44:24.186449"},
    {262117798209094,"19780422T182958.209094", "1978-04-22T18:29:58.209094"}
  };
  
  for( const auto &v : test_iso_counts )
  {
    SpecUtils::time_point_t tp{}; //initialized to epoch
    
    tp += std::chrono::microseconds( get<0>(v) );
    
    string iso_str = SpecUtils::to_iso_string(tp);
    string extended_str = SpecUtils::to_extended_iso_string(tp);
    
    if( iso_str.length() < 22 )
      iso_str.resize( 22, '0' );
    if( extended_str.length() < 26 )
      extended_str.resize( 26, '0' );
        
    CHECK_EQ( iso_str, get<1>(v) );
    CHECK_EQ( extended_str, get<2>(v) );
  }

  
  //check special time values
  SpecUtils::time_point_t d1 = SpecUtils::time_point_t{} + SpecUtils::time_point_t::duration::min();
  SpecUtils::time_point_t d2 = SpecUtils::time_point_t{} + SpecUtils::time_point_t::duration::max();
  SpecUtils::time_point_t d3{};
  //SpecUtils::time_point_t d4 = std::chrono::system_clock::now();

  CHECK( SpecUtils::to_iso_string(d1) == "not-a-date-time" );
  CHECK( SpecUtils::to_iso_string(d2) == "not-a-date-time" );
  CHECK( SpecUtils::to_iso_string(d3) == "not-a-date-time" );
  //CHECK( SpecUtils::to_iso_string(d4) != "not-a-date-time" );

  //"20140414T141201.621543"
  
  CHECK_EQ( SpecUtils::to_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01.62") ), "20140919T141201.62" );
  CHECK_EQ( SpecUtils::to_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01") ), "20140919T141201" );
  CHECK_EQ( SpecUtils::to_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01.621543") ), "20140919T141201.621543" );
  
  CHECK_EQ( SpecUtils::to_iso_string( SpecUtils::time_from_string("20140101T14:12:01.62") ), "20140101T141201.62" );
  CHECK_EQ( SpecUtils::to_iso_string( SpecUtils::time_from_string("20140101T14:12:01.623") ), "20140101T141201.623" );
  CHECK_EQ( SpecUtils::to_iso_string( SpecUtils::time_from_string("20140101T14:12:01.626") ), "20140101T141201.626" );
  CHECK_EQ( SpecUtils::to_iso_string( SpecUtils::time_from_string("20140101T00:00:00") ), "20140101T000000" );
  
  
  //"2014-04-14T14:12:01.621543"
  CHECK_EQ( SpecUtils::to_extended_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01.62") ), "2014-09-19T14:12:01.62" );
  CHECK_EQ( SpecUtils::to_extended_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01") ), "2014-09-19T14:12:01" );
  CHECK_EQ( SpecUtils::to_extended_iso_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01.621543") ), "2014-09-19T14:12:01.621543" );
  
  CHECK_EQ( SpecUtils::to_vax_string( SpecUtils::time_from_string("2014-Sep-19 14:12:01.62") ), "19-Sep-2014 14:12:01.62" );
  CHECK_EQ( SpecUtils::to_vax_string( SpecUtils::time_from_string("20140101T14:12:01.62") ), "01-Jan-2014 14:12:01.62" );
  CHECK_EQ( SpecUtils::to_vax_string( SpecUtils::time_from_string("20140101T14:12:01.623") ), "01-Jan-2014 14:12:01.62" );
  CHECK_EQ( SpecUtils::to_vax_string( SpecUtils::time_from_string("20140101T14:12:01.626") ), "01-Jan-2014 14:12:01.63" );
  CHECK_EQ( SpecUtils::to_vax_string( SpecUtils::time_from_string("20140101T00:00:00") ), "01-Jan-2014 00:00:00.00" );
  

  /** Converts the input to string in format d-mmm-YYYY HH:MM:SS AM,
   where mmm is 3 char month name; d is day number with no leading zeros.
   Returns "not-a-date-time" if input is not valid.
   Ex. 24hr format: "2014-Sep-9 13:02:15", AMP/PM: "2014-Sep-9 03:02:15 PM"
   */
  //std::string SpecUtils::to_common_string( const boost::posix_time::ptime &t, const bool twenty_four_hour );
  
}//TEST_CASE( "isoString" )
