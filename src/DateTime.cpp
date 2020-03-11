/**
 SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 Copyright (C) 2016 William Johnson
 
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
#include <cctype>

#include <boost/date_time/posix_time/posix_time.hpp>

#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"


#if( USE_HH_DATE_LIB )
#include "3rdparty/date_9a0ee25/include/date/date.h"
#else

//#if( defined(_MSC_VER) && _MSC_VER < 1800 )
//#if( defined(_MSC_VER) )  //Doesnt look like MSVS 2017 has strptime.
#if( defined(_WIN32) )
#define HAS_NATIVE_STRPTIME 0
#else
#define HAS_NATIVE_STRPTIME 1
#endif

#if( HAS_NATIVE_STRPTIME )
#include <time.h>
#endif

#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#endif

/** Checking ISO time string against boost requires linking to boost, so for now
 we'll just rely on the unit-tests to make sure we're getting things okay.
*/
#define CHECK_TIME_STR_AGAINST_BOOST 0

using namespace std;

namespace
{
  bool toFloat( const std::string &str, float &f )
  {
    //ToDO: should probably use SpecUtils::parse_float(...) for consistency/speed
    const int nconvert = sscanf( str.c_str(), "%f", &f );
    return (nconvert == 1);
  }
  
  const char *month_number_to_Str( const int month )
  {
    switch( month )
    {
      case 1: return "Jan";
      case 2: return "Feb";
      case 3: return "Mar";
      case 4: return "Apr";
      case 5: return "May";
      case 6: return "Jun";
      case 7: return "Jul";
      case 8: return "Aug";
      case 9: return "Sep";
      case 10: return "Oct";
      case 11: return "Nov";
      case 12: return "Dec";
    }
    return "";
  }//const char *month_number_to_Str( const int month )
}//namespace


namespace SpecUtils
{
  
  std::string to_common_string( const boost::posix_time::ptime &t, const bool twenty_four_hour )
  {
    if( t.is_special() )
      return "not-a-date-time";
    
    const int year = static_cast<int>( t.date().year() );
    const int day = static_cast<int>( t.date().day() );
    int hour = static_cast<int>( t.time_of_day().hours() );
    const int mins = static_cast<int>( t.time_of_day().minutes() );
    const int secs = static_cast<int>( t.time_of_day().seconds() );
    
    bool is_pm = (hour >= 12);
    
    if( !twenty_four_hour )
    {
      if( is_pm )
        hour -= 12;
      if( hour == 0 )
        hour = 12;
    }
    
    const char * const month = month_number_to_Str( t.date().month() );
    
    char buffer[64];
    snprintf( buffer, sizeof(buffer), "%i-%s-%04i %02i:%02i:%02i%s",
             day, month, year, hour, mins, secs, (twenty_four_hour ? "" : (is_pm ? " PM" : " AM")) );
    
    return buffer;
  }//to_common_string
  
  
  std::string to_vax_string( const boost::posix_time::ptime &t )
  {
    //Ex. "2014-Sep-19 14:12:01.62"
    if( t.is_special() )
      return "";
    
    const int year = static_cast<int>( t.date().year() );
    const char * const month = month_number_to_Str( t.date().month() );
    const int day = static_cast<int>( t.date().day() );
    const int hour = static_cast<int>( t.time_of_day().hours() );
    const int mins = static_cast<int>( t.time_of_day().minutes() );
    const int secs = static_cast<int>( t.time_of_day().seconds() );
    const int hundreth = static_cast<int>( 0.5 + ((t.time_of_day().total_milliseconds() % 1000) / 10.0) ); //round to neares hundreth of a second
    
    char buffer[32];
    snprintf( buffer, sizeof(buffer), "%02i-%s-%04i %02i:%02i:%02i.%02i",
             day, month, year, hour, mins, secs, hundreth );
    
#if(PERFORM_DEVELOPER_CHECKS)
    if( strlen(buffer) != 23 )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Vax format of '%s' is '%s' which is not the expected length of 23, but %i",
               to_extended_iso_string(t).c_str(), buffer, static_cast<int>(strlen(buffer)) );
      log_developer_error( __func__, errormsg );
    }
#endif
    
    return buffer;
  }//std::string to_vax_string( const boost::posix_time::ptime &t );
  
  
  std::string print_to_iso_str( const boost::posix_time::ptime &t,
                               const bool extended )
  {
    if( t.is_special() )
      return "not-a-date-time";
    //should try +-inf as well
    
    const int year = static_cast<int>( t.date().year() );
    const int month = static_cast<int>( t.date().month() );
    const int day = static_cast<int>( t.date().day() );
    const int hour = static_cast<int>( t.time_of_day().hours() );
    const int mins = static_cast<int>( t.time_of_day().minutes() );
    const int secs = static_cast<int>( t.time_of_day().seconds() );
    double frac = t.time_of_day().fractional_seconds()
    / double(boost::posix_time::time_duration::ticks_per_second());
    
    char buffer[256];
    if( extended ) //"2014-04-14T14:12:01.621543"
      snprintf( buffer, sizeof(buffer),
               "%i-%.2i-%.2iT%.2i:%.2i:%09.6f",
               year, month, day, hour, mins, (secs+frac) );
    else           //"20140414T141201.621543"
      snprintf( buffer, sizeof(buffer),
               "%i%.2i%.2iT%.2i%.2i%09.6f",
               year, month, day, hour, mins, (secs+frac) );
    
#if(PERFORM_DEVELOPER_CHECKS)
    string tmpval = buffer;
    if( tmpval.find(".") == string::npos )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Expected there to be a '.' in iso date time: '%s'", buffer );
      log_developer_error( __func__, errormsg );
    }
#endif
    
    const char point = '.';
    //    const char point = std::use_facet< std::numpunct<char> >(std::cout.getloc()).decimal_point();
    
    //Get rid of trailing zeros
    size_t result_len = strlen(buffer) - 1;
    while( result_len > 1 && buffer[result_len]=='0' )
      buffer[result_len--] = '\0';
    
    if( result_len > 1 && buffer[result_len]==point )
      buffer[result_len--] = '\0';
    
#if(PERFORM_DEVELOPER_CHECKS && CHECK_TIME_STR_AGAINST_BOOST )
    string correctAnswer = extended
    ? boost::posix_time::to_iso_extended_string( t )
    : boost::posix_time::to_iso_string( t );
    
    if( correctAnswer.find(".") != string::npos )
    {
      result_len = correctAnswer.size() - 1;
      while( result_len > 1 && correctAnswer[result_len]=='0' )
        correctAnswer = correctAnswer.substr( 0, result_len-- );
      
      if( result_len > 1 && buffer[result_len]==point )
        correctAnswer = correctAnswer.substr( 0, result_len-- );
    }//if( correctAnswer.find(".") != string::npos )
    
    if( correctAnswer != buffer )
    {
      char errormsg[1024];
      snprintf( errormsg, sizeof(errormsg),
               "Failed to format date correctly for %sextended iso format. Expected: '%s', got: '%s'",
               (extended ? "" : "non-"),
               correctAnswer.c_str(), buffer );
      log_developer_error( __func__, errormsg );
    }
#endif
    
    
    return buffer;
  }//std::string to_extended_iso_string( const boost::posix_time::ptime &t )
  
  std::string to_extended_iso_string( const boost::posix_time::ptime &t )
  {
    return print_to_iso_str( t, true );
  }
  
  std::string to_iso_string( const boost::posix_time::ptime &t )
  {
    return print_to_iso_str( t, false );
  }//std::string to_iso_string( const boost::posix_time::ptime &t )
  
  
  boost::posix_time::ptime time_from_string( const char *time_string )
  {
#define CREATE_datetimes_TEST_FILE 0
    
#if( CREATE_datetimes_TEST_FILE )
    static std::mutex datetimes_file_mutex;
    static int ntimes_called = 0;
    string inputstr = time_string;
    SpecUtils::ireplace_all(inputstr, ",", "");
    if( inputstr.size() && inputstr[0]=='#' )
      inputstr = inputstr.substr(1);
    
    //#ifndef _WIN32
    auto result = time_from_string_strptime( time_string, MiddleEndianFirst );
    //#else
    //    auto result = time_from_string_boost( time_string );
    //#endif
    
    if( inputstr.empty() )
      return result;
    
    std::lock_guard<std::mutex> lock( datetimes_file_mutex );
    ++ntimes_called;
    if( (ntimes_called % 1000) == 0 )
      cerr << "Warning, time_from_string() is creating datetimes.txt test file" << endl;
    
    ofstream output( "datetimes.txt", ios::out | ios::app );
    
    if( output )
      output << inputstr << "," << SpecUtils::to_extended_iso_string(result) << "\r\n";
    else
      cerr << "Failed to open datetimes.txt for appending" << endl;
    
    return result;
#else  //CREATE_datetimes_TEST_FILE
    
    return time_from_string_strptime( time_string, MiddleEndianFirst );
#endif
  }//boost::posix_time::ptime time_from_string( const char *time_string )
  
  
  
#if( !USE_HH_DATE_LIB )
#  if( defined(_WIN32) )
#    define timegm _mkgmtime
#  endif
  
  bool strptime_wrapper( const char *s, const char *f, struct tm *t )
  {
    //For the testTimeFromString unit test on my mac, the native take strptime
    //  takes 302188us to run, vs the c++11 version taking 4113835us.
    //  ~10 times slower, so preffer native strptime where available.
    
#if( HAS_NATIVE_STRPTIME )
    return (strptime(s,f,t) != nullptr);
#else
    //*t = std::tm();  //should already be done
    //memset( t, 0, sizeof(*t) );  //Without this some tm fields dont make sense for some formats...
    
    
#if(defined(_WIN32))
    //see https://developercommunity.visualstudio.com/content/problem/18311/stdget-time-asserts-with-istreambuf-iterator-is-no.html
    //if( strlen( f ) < strlen( s ) )
    //  return false;
#endif
    
    //Arg! Windows VS2017 get_time(...) doesnt say it fails when it should!
    
    std::istringstream input( s );
    input.imbue( std::locale( setlocale( LC_ALL, nullptr ) ) );
    input >> std::get_time( t, f );
    if( input.fail() )
      return false;
    
    //cout << "Format '" << f << "' parsed '" << s << "' to " << std::put_time(t, "%c") << endl;
    /*
     cout
     << "seconds after the minute [0-60]: " << t.tm_sec << endl
     << "minutes after the hour [0-59]: " << t.tm_min << endl
     << "hours since midnight [0-23]: " << t.tm_hour << endl
     << "day of the month [1-31]: " << t.tm_mday << endl
     << "months since January [0-11]: " << t.tm_mon << endl
     << "years since 1900: " << t.tm_year << endl
     << "days since Sunday [0-6]: " << t.tm_wday << endl
     << "days since January 1 [0-365]: " << t.tm_yday << endl
     << "Daylight Savings Time flag: " << t.tm_isdst << endl
     << "offset from UTC in seconds: " << t.tm_gmtoff << endl
     //<< "timezone abbreviation: " << (t.tm_zone ? (const char *)t.tm_zone : "null")
     << endl;
     */
    
    return true;
#endif
  }//char *strptime_wrapper(...)
#endif //USE_HH_DATE_LIB
  
  boost::posix_time::ptime time_from_string_strptime( std::string time_string,
                                                     const DateParseEndianType endian )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    const string develop_orig_str = time_string;
#endif
    SpecUtils::to_upper_ascii( time_string );  //make sure strings like "2009-11-10t14:47:12" will work (some file parsers convert to lower case)
    SpecUtils::ireplace_all( time_string, ",", " " );
    SpecUtils::ireplace_all( time_string, "  ", " " );
    SpecUtils::ireplace_all( time_string, "_T", "T" );  //Smiths NaI HPRDS: 2009-11-10_T14:47:12Z
    SpecUtils::trim( time_string );
    
#ifdef _WIN32
    // On MinGW
    //  - it seems we need "Apr" and not "APR".
    //  - AM/PM doesnt seem to work
    //  - lots of other errors
#endif
    
    
    //Replace 'T' with a space to almost cut the number of formats to try in
    //  half.  We could probably do a straight replacement of 'T', but we'll be
    //  conservative and make it have a number on both sides of it (e.g.,
    //  separating the date from the time).
    auto tpos = time_string.find( 'T' );
    while( tpos != string::npos )
    {
      if( (tpos > 0) && ((tpos + 1) < time_string.size())
         && isdigit( time_string[tpos - 1] ) && isdigit( time_string[tpos + 1] ) )
        time_string[tpos] = ' ';
      tpos = time_string.find( 'T', tpos + 1 );
    }
    
    
    //strptime(....) cant handle GMT offset (ex '2014-10-24T08:05:43-04:00')
    //  so we will do this manually.
    boost::posix_time::time_duration gmtoffset(0,0,0);
    const size_t offsetcolon = time_string.find_first_of( ':' );
    if( offsetcolon != string::npos )
    {
      const size_t signpos = time_string.find_first_of( "-+", offsetcolon+1 );
      if( signpos != string::npos )
      {
        //Note: the + and - symbols are in the below for dates like:
        //  '3-07-31T14:25:30-03:-59'
        const size_t endoffset = time_string.find_first_not_of( ":0123456789+-", signpos+1 );
        const string offset = endoffset==string::npos ? time_string.substr(signpos)
        : time_string.substr(signpos,endoffset-signpos-1);
        string normal = time_string.substr(0,signpos);
        if( endoffset != string::npos )
          normal += time_string.substr( endoffset );
        
        //normal will look like "2014-10-24T08:05:43"
        //offset will look like "-04:00"
        try
        {
          gmtoffset = boost::posix_time::duration_from_string(offset);
        }catch( std::exception & )
        {
          cerr << "Failed to convert '" << offset << "' to time duration, from string '" << time_string << "'" << endl;
        }
        
        
        //Should also make sure gmtoffset is a reasonalbe value.
        
        //      cout << "offset for '" << time_string << "' is '" << offset << "' with normal time '" <<  normal<< "'" << endl;
        time_string = normal;
      }//if( signpos != string::npos )
    }//if( offsetcolon != string::npos )
    
#if( defined(_WIN32) && defined(_MSC_VER) )
    //Right now everything is uppercase; for at least MSVC 2012, abreviated months, such as
    //  "Jan", "Feb", etc must start with a capital, and be followed by lowercase
    //  We could probably use some nice regex, but whatever for now
    auto letter_start = time_string.find_first_of( "JFMASOND" );
    if( (letter_start != string::npos) && ((letter_start+2) < time_string.length()) )
    {
      if( time_string[letter_start+1] != 'M' )
      {
        for( size_t i = letter_start+1; i < time_string.length() && isalpha(time_string[i]); ++i )
          time_string[i] = static_cast<char>( tolower(time_string[i]) );
      }
    }
#endif
    
    //strptime(....) cant handle fractions of second (because tm doesnt either)
    //  so we will manually convert fractions of seconds.
    boost::posix_time::time_duration fraction(0,0,0);
    //int fraction_nano_sec = 0;
    
    size_t fraccolon = time_string.find_last_of( ':' );
    
    //The time may not have ":" characters in it, like ISO times, so find the space
    //  that seperates the date and the time.
    if( fraccolon == string::npos )
    {
      //Be careful of dates like "01.Nov.2010 214335" or "May. 21 2013 070642"
      //  or "3.14.2006 10:19:36"
      //There is definitely a better way to do this!
      const size_t seppos = time_string.find_first_of( ' ' );
      fraccolon = ((seppos > 6) ? seppos : fraccolon);
    }
      
    if( fraccolon != string::npos )
    {
      /// \TODO: This munging around messes up on strings like "22/03/99 5.06", or "22-mar-1999 5.06.07"
      const size_t period = time_string.rfind( '.' );  //For string strings like "22-MAR-99 05.06.07.888 AM", we will search from the back
      
      //Check period is greater than three places to the right of a space
      bool aftertime = false;
      for( size_t pos = period; period!=string::npos && period; --pos )
      {
        if( isspace(time_string[pos]) )
        {
          aftertime = ((period-pos) > 5);
          break;
        }
      }
      
      //const size_t period = time_string.find( '.', fraccolon+1 );
      //Check that we found the period, and each side
      if( period > (fraccolon+1)
          && aftertime
          && period != string::npos && period > 0 && (period+1) < time_string.size()
          && isdigit(time_string[period-1]) && isdigit(time_string[period+1]) )
      {
        const size_t last = time_string.find_first_not_of( "0123456789", period+1 );
        string fracstr = ((last!=string::npos)
                          ? time_string.substr(period+1,last-period-1)
                          : time_string.substr(period+1));
        
        //Assume microsecond resolution at the best (note
        //  boost::posix_time::nanosecond isnt available on my OS X install)
        const size_t ndigits = 9;
        //        const uint64_t invfrac = static_cast<uint64_t>(1E9);
        const auto nticks = boost::posix_time::time_duration::ticks_per_second();
        
        if( fracstr.size() < ndigits )
          fracstr.resize( ndigits, '0' );
        else if( fracstr.size() > ndigits )
          fracstr.insert( ndigits, "." );
        
        
        double numres = 0.0;
        if( (stringstream(fracstr) >> numres) )
        {
          const double frac = std::round( numres * nticks * 1.0E-9 );
          const auto fractions = static_cast<boost::posix_time::time_duration::fractional_seconds_type>( frac );
          fraction = boost::posix_time::time_duration(0,0,0,fractions);
        }else
          cerr << "Failed to convert fraction '" << fracstr << "' to double" << endl;
        
        //int64_t numres = 0;  //using int will get rounding wrong
        //if( (stringstream(fracstr) >> numres) )
        //        {
        //          //fraction_nano_sec = numres;
        //          boost::posix_time::time_duration::fractional_seconds_type fractions = numres*nticks/invfrac;
        //          fraction = boost::posix_time::time_duration(0,0,0,fractions);
        //        }else
        //          cerr << "Failed to convert fraction '" << fracstr << "' to double" << endl;
        
        string normal = time_string.substr(0,period);
        if( last != string::npos )
          normal += time_string.substr(last);
        time_string = normal;
      }//if( period != string::npos )
    }//if( fraccolon != string::npos )
    
    //With years like 2070, 2096, and such, strptime seems to fail badly, so we
    //  will fix them up a bit.
    //Assumes the first time you'll get four numbers in a row, it will be the year
    bool add100Years = false;
    if( time_string.size() > 5 )
    {
      for( size_t i = 0; i < time_string.size()-4; ++i )
      {
        if( isdigit(time_string[i]) && isdigit(time_string[i+1])
           && isdigit(time_string[i+2]) && isdigit(time_string[i+3]) )
        {
          int value;
          if( stringstream(time_string.substr(i,4)) >> value )
          {
            //XXX - I havent yet determined what year this issue starts at
            if( value > 2030 && value < 2100 )
            {
              char buffer[8];
              snprintf( buffer, sizeof(buffer), "%i", value - 100 );
              time_string[i] = buffer[0];
              time_string[i+1] = buffer[1];
              time_string[i+2] = buffer[2];
              time_string[i+3] = buffer[3];
              add100Years = true;
            }
          }//if( stringstream(time_string.substr(i,4)) >> value )
          
          break;
        }//if( four numbers in a row )
      }//for( size_t i = 0; i < time_string.size(); ++i )
    }//if( time_string.size() > 5 )
    
    //middle: whether to try a middle endian (date start with month number)
    //  date decoding first, or alternetavely little endian (date start with day
    //  number).  Both endians will be tried, this just selects which one first.
    const bool middle = (endian == MiddleEndianFirst);
    const bool only = (endian == MiddleEndianOnly || endian == LittleEndianOnly);
    
    const char * const formats[] =
    {
      "%Y-%m-%d%n%I:%M:%S%n%p", //2010-01-15 06:21:15 PM
      "%Y-%m-%d%n%H:%M:%SZ", //2010-01-15T23:21:15Z  //I think this is the most common format in N42-2012 files, so try it first.
      "%Y-%m-%d%n%H:%M:%S", //2010-01-15 23:21:15    //  Sometimes the N42 files dont have the Z though, or have some offset (we'll ignore)
      "%Y%m%d%n%H%M%S",  //20100115T232115           //ISO format
      "%d-%b-%Y%n%r",       //1-Oct-2004 12:34:42 AM  //"%r" ~ "%I:%M:%S %p"
      "%d-%b-%y%n%r", //'15-MAY-14 08:30:44 PM'  (disambiguous: May 15 2014)
      "%Y%m%d%n%H:%M:%S",  //20100115T23:21:15
      (middle ? "%m/%d/%Y%n%I:%M%n%p" : "%d/%m/%Y%n%I:%M%n%p"), //1/18/2008 2:54 PM
      (only ? "" : (middle ? "%d/%m/%Y%n%I:%M%n%p" : "%m/%d/%Y%n%I:%M%n%p")),
      (middle ? "%m/%d/%Y%n%r" : "%d/%m/%Y%n%r"), //1/18/2008 2:54:44 PM
      (only ? "" : (middle ? "%d/%m/%Y%n%r" : "%m/%d/%Y%n%r")),
      (middle ? "%m/%d/%Y%n%H:%M:%S" : "%d/%m/%Y%n%H:%M:%S"), //08/05/2014 14:51:09
      (only ? "" : (middle ? "%d/%m/%Y%n%H:%M:%S" : "%m/%d/%Y%n%H:%M:%S")),
      (middle ? "%m/%d/%Y%n%H:%M" : "%d/%m/%Y%n%H:%M"), //08/05/2014 14:51
      (only ? "" : (middle ? "%d/%m/%Y%n%H:%M" : "%m/%d/%Y%n%H:%M")),
      (middle ? "%m-%d-%Y%n%I:%M%n%p" : "%d-%m-%Y%n%I:%M%n%p"), //14-10-2014 6:15 PM
      (only ? "" : (middle ? "%d-%m-%Y%n%I:%M%n%p" : "%m-%d-%Y%n%I:%M%n%p" )),
      (middle ? "%m-%d-%Y%n%I:%M:%S%n%p" : "%d-%m-%Y%n%I:%M:%S%n%p"), //14-10-2014 06:15:52 PM
      (only ? "" : (middle ? "%d-%m-%Y%n%I:%M:%S%n%p" : "%m-%d-%Y%n%I:%M:%S%n%p" )),
      (middle ? "%m-%d-%Y%n%H:%M:%S" : "%d-%m-%Y%n%H:%M:%S"), //14-10-2014 16:15:52
      (only ? "" : (middle ? "%d-%m-%Y%n%H:%M:%S" : "%m-%d-%Y%n%H:%M:%S" )),
      (middle ? "%m-%d-%Y%n%H:%M" : "%d-%m-%Y%n%H:%M"), //14-10-2014 16:15
      (only ? "" : (middle ? "%d-%m-%Y%n%H:%M" : "%m-%d-%Y%n%H:%M" )),
#if( defined(_WIN32) && defined(_MSC_VER) )
      (middle ? "%m %d %Y %H:%M:%S" : "%d %m %Y %H:%M:%S"), //14 10 2014 16:15:52
      (only ? "" : (middle ? "%d %m %Y %H:%M:%S" : "%m %d %Y %H:%M:%S")),
#else
      (middle ? "%m%n%d%n%Y%n%H:%M:%S" : "%d%n%m%n%Y%n%H:%M:%S"), //14 10 2014 16:15:52
      (only ? "" : (middle ? "%d%n%m%n%Y%n%H:%M:%S" : "%m%n%d%n%Y%n%H:%M:%S" )),
#endif
      "%d-%b-%y%n%H:%M:%S", //16-MAR-06 13:31:02, or "12-SEP-12 11:23:30"
      "%d-%b-%Y%n%I:%M:%S%n%p", //31-Aug-2005 6:38:04 PM,
      "%d %b %Y%n%I:%M:%S%n%p", //31 Aug 2005 6:38:04 PM
      "%b %d %Y%n%I:%M:%S%n%p", //Mar 22, 1999 5:06:07 AM
      "%d-%b-%Y%n%H:%M:%S", //31-Aug-2005 12:38:04,
      "%d %b %Y%n%H:%M:%S", //31 Aug 2005 12:38:04
      "%d-%b-%Y%n%H:%M:%S%nZ",//9-Sep-2014T20:29:21 Z
      (middle ? "%m-%m-%Y%n%H:%M:%S" : "%d-%m-%Y%n%H:%M:%S" ), //"10-21-2015 17:20:04" or "21-10-2015 17:20:04"
      (only ? "" : (middle ? "%d-%m-%Y%n%H:%M:%S" : "%m-%m-%Y%n%H:%M:%S" )),
      "%d.%m.%y%n%H:%M:%S",  //28.02.13 13:42:47      //We need to check '%y' before '%Y'
      "%d.%m.%Y%n%H:%M:%S",  //28.02.2013 13:42:47
      //      (middle ? "%m.%d.%Y%n%H:%M:%S" : "%d.%m.%Y%n%H:%M:%S"), //26.05.2010 02:53:49
      //      (only ? "" : (middle ? "%d.%m.%Y%n%H:%M:%S" : "%m.%d.%Y%n%H:%M:%S")),
      "%b. %d %Y%n%H:%M:%S",//May. 21 2013  07:06:42
      //      (middle ? "%m.%d.%y%n%H:%M:%S" : "%d.%m.%y%n%H:%M:%S"),  //'28.02.13 13:42:47'
      (middle ? "%m.%d.%y%n%H:%M:%S" : "%d.%m.%y%n%H:%M:%S"), //'3.14.06 10:19:36' or '28.02.13 13:42:47'
      (only ? "" : (middle ? "%d.%m.%y%n%H:%M:%S" : "%m.%d.%y%n%H:%M:%S" )),
      (middle ? "%m.%d.%Y%n%H:%M:%S" : "%d.%m.%Y%n%H:%M:%S"), //'3.14.2006 10:19:36' or '28.02.2013 13:42:47'
      (only ? "" : (middle ? "%d.%m.%Y%n%H:%M:%S" : "%m.%d.%Y%n%H:%M:%S" )),
      "%d.%m.%y%n%H:%M:%S",
      //      (only ? "" : (middle ? "%d.%m.%y%n%H:%M:%S" : "%m.%d.%y%n%H:%M:%S")),
      "%Y.%m.%d%n%H:%M:%S", //2012.07.28 16:48:02
      "%d.%b.%Y%n%H:%M:%S",//01.Nov.2010 21:43:35
      "%Y%m%d%n%H:%M:%S",  //20100115 23:21:15
      "%Y-%b-%d%n%H:%M:%S", //2017-Jul-07 09:16:37
      "%Y/%m/%d%n%H:%M:%S", //"2020/02/12 14:57:39"
      "%Y/%m/%d%n%H:%M", //"2020/02/12 14:57"
      "%Y-%m-%d%n%H-%M-%S", //2018-10-09T19-34-31_27 (not sure what the "_27" exactly means)
      "%Y%m%d%n%H%M",  //20100115T2321
      (middle ? "%m/%d/%y%n%r" : "%d/%m/%y%n%r"), //'6/15/09 11:12:30 PM' or '15/6/09 11:12:30 PM'
      (only ? "" : (middle ? "%d/%m/%y%n%r" : "%m/%d/%y%n%r" )),
      (middle ? "%m/%d/%y%n%H:%M:%S" : "%d/%m/%y%n%H:%M:%S"), //'6/15/09 13:12:30' or '15/6/09 13:12:30'
      (only ? "" : (middle ? "%d/%m/%y%n%H:%M:%S" : "%m/%d/%y%n%H:%M:%S" )),
      (middle ? "%m-%d-%y%n%I:%M%n%p" : "%d-%m-%y%n%I:%M%n%p"), //'03-22-99 5:06 AM' or '22-03-99 5:06 AM'
      (only ? "" : (middle ? "%d-%m-%y%n%I:%M%n%p" : "%m-%d-%y%n%I:%M%n%p" )),
      
      (middle ? "%m/%d/%y%n%I:%M%n%p" : "%d/%m/%y%n%I:%M%n%p"), //'6/15/09 05:12 PM' or '15/6/09 05:12 PM'
      (only ? "" : (middle ? "%d/%m/%y%n%I:%M%n%p" : "%m/%d/%y%n%I:%M%n%p" )),
      
      (middle ? "%m/%d/%y%n%H:%M" : "%d/%m/%y%n%H:%M"), //'6/15/09 13:12' or '15/6/09 13:12'
      (only ? "" : (middle ? "%d/%m/%y%n%H:%M" : "%m/%d/%y%n%H:%M" )),
      "%b %d %Y%n%I:%M%n%p", //Mar 22, 1999 5:06 AM
      "%b %d %Y%n%H:%M:%S", //Mar 22, 1999 5:06:07
      "%b %d %Y%n%H:%M", //Mar 22, 1999 5:06:07
      "%d-%b-%y%n%I.%M.%S%n%p", //'22-MAR-99 05.06.07.888 AM
      "%d-%b-%Y%n%H.%M.%S", //'22-MAR-1999 05.06'
      (middle ? "%m-%d-%y%n%I:%M:%S%n%p" : "%d-%m-%y%n%I:%M:%S%n%p"), //'03-22-99 5:06:07 AM' or '22-03-99 5:06:07 AM'
      (only ? "" : (middle ? "%d-%m-%y%n%I:%M:%S%n%p" : "%m-%d-%y%n%I:%M:%S%n%p" )),
      "%Y-%m-%d%n%I:%M%n%p", //'1999-03-22 5:06 AM'
      "%Y-%m-%d%n%H:%M", //'1999-03-22 5:06'
      "%d/%b/%Y%n%I:%M:%S%n%p", //'22/Mar/1999 5:06:07 PM'
      "%d/%b/%Y%n%I:%M%n%p", //'22/Mar/1999 5:06 PM'
      "%d/%b/%Y%n%H:%M:%S", //'22/Mar/1999 5:06:07'
      "%d/%b/%y%n%I:%M%n%p", //'22/Mar/99 5:06 PM'
      "%d/%b/%y%n%H:%M:%S", //'22/Mar/99 5:06:07'
      
      "%d.%m.%y%n%H:%ML%s", //'22.3.99 5:06:01'
      "%d.%m.%y%n%H:%M", //'22.3.99 5:06'
      "%m-%d-%y%n%H:%M:%S", //'03-22-99 05:06:01'
      "%m-%d-%y%n%H:%M", //'03-22-99 05:06'
      "%y-%m-%d%n%H:%M", //'99-03-22 05:06'
      "%d/%b/%y%n%H.%M.%S", //'22/Mar/99 5.06.07'
      "%d/%m/%y%n%H.%M", //'22/03/99 5.06'
      
      //Below here are dates only, with no times
      (middle ? "%m/%d/%Y" : "%d/%m/%Y"), //'6/15/2009' or '15/6/2009'
      (only ? "" : (middle ? "%d/%m/%Y" : "%m/%d/%Y" )),
      "%d-%b-%Y",           //"00-Jan-2000 "
      "%Y/%m/%d", //"2010/01/18"
      "%Y-%m-%d", //"2010-01-18"
      "%Y%m%d", //"20100118"
      "%b%n%d%n%Y", //March 22, 1999
      "%b%n%d%n%y", //March 22, 99
      "%b.%d.%Y", //March.22.1999
      "%b.%d.%y", //March.22.99
      "%d/%m/%y", //'22/03/99'
    };
    
    
    const size_t nformats = sizeof(formats) / sizeof(formats[0]);
    const char *timestr = time_string.c_str();
    
    for( size_t i = 0; i < nformats; ++i )
    {
#if( USE_HH_DATE_LIB )
      if( !formats[i][0] )
        continue;
      
      using ClockType = std::chrono::system_clock;
      std::chrono::time_point<ClockType> tp{};
      std::istringstream input(timestr);

      if( (input >> date::parse(formats[i], tp)) )
      {
        //time_t tt = ClockType::to_time_t( tp );
        //return boost::posix_time::from_time_t( tt ) + fraction + boost::gregorian::years( add100Years ? 100 : 0 );
        
        date::sys_days dp = date::floor<date::days>(tp);
        auto ymd = date::year_month_day{dp};
        
        if( add100Years  )
          ymd += date::years(100);
        
        auto time = date::make_time( std::chrono::duration_cast<std::chrono::microseconds>(tp-dp) );
        if( static_cast<int>(ymd.year()) < 1400 || static_cast<int>(ymd.year()) >= 10000 )  //protect against boost throwing an exception
          continue;
        
        const boost::gregorian::date bdate( static_cast<int>(ymd.year()),
                                            static_cast<unsigned int>(ymd.month()),
                                            static_cast<unsigned int>(ymd.day()));
        const boost::posix_time::time_duration btime( time.hours().count(), time.minutes().count(), time.seconds().count() );
        
        return boost::posix_time::ptime(bdate, btime) + fraction;
      }
#else
      struct tm t = std::tm();
      
      if( formats[i][0] && strptime_wrapper( timestr, formats[i], &t ) )
      {
        
        if( t.tm_year <= -500 || t.tm_year > 8000 ) //protect against boost throwing an exception
          continue;
        
        //cout << "Format='" << formats[i] << "' worked to give: "
        //  << print_to_iso_str( boost::posix_time::from_time_t(timegm( &t )) + fraction + boost::gregorian::years( add100Years ? 100 : 0 ), false )
        //  << " time_t=" << timegm(&t)
        //  << endl;
        
        
        //if( add100Years )
        //t.tm_year += 100;
        //std::chrono::time_point tp = system_clock::from_time_t( std::mktime(&t) ) + std::chrono::nanoseconds(fraction_nano_sec);
        
        return boost::posix_time::from_time_t( timegm(&t) )
               + fraction
               + boost::gregorian::years( add100Years ? 100 : 0 )
        /*+ gmtoffset*/;  //ignore offset since we want time in local persons zone
      }
      //      return boost::posix_time::from_time_t( mktime(&tm) - timezone + 3600*daylight ) + fraction + gmtoffset;
#endif //USE_HH_DATE_LIB
    }//for( size_t i = 0; i < nformats; ++i )
    
    //cout << "Couldnt parse" << endl;
    
#if(PERFORM_DEVELOPER_CHECKS)
    if( develop_orig_str.size() > 5 //5 is arbitrary
       && develop_orig_str.find("NA")==string::npos
       && std::count( begin(develop_orig_str), end(develop_orig_str), '0') < 8 )
      log_developer_error( __func__, ("Failed to parse date/time from: '" + develop_orig_str  + "' which was massaged into '" + time_string + "'").c_str() );
#endif
    return boost::posix_time::ptime();
  }//boost::posix_time::ptime time_from_string_strptime( std::string time_string )
  //#endif  //#ifndef _WIN32
  
  float time_duration_string_to_seconds( const std::string &duration )
  {
    return time_duration_string_to_seconds( duration.c_str(), duration.size() );
  }
  
  
  float time_duration_string_to_seconds( const char *duration_str, const size_t len )
  {
    float durration = 0.0f;
    
    if( !duration_str || !len )
      return durration;
    
    const char *orig = duration_str;
    const char *end = duration_str + len;
    
    while( duration_str < end )
    {
      while( (duration_str < end) && !isdigit(*duration_str) )
        ++duration_str;
      
      if( duration_str >= end )
        break;
      
      const char *num_start = duration_str;
      while( (duration_str < end) && (isdigit(*duration_str) || duration_str[0]=='.') )
        ++duration_str;
      const char *num_end = duration_str;
      
      float unit = 1.0;
      char unitchar = (num_end<end) ? (*num_end) : 's';
      
      if( unitchar=='s' || unitchar=='S' )
        unit = 1.0;
      else if( unitchar=='m' || unitchar=='M' )
        unit = 60.0;
      else if( unitchar=='h' || unitchar=='H' )
        unit = 3600.0;
      
      float num_val = 0.0;
      const string numberstr( num_start, num_end );
      if( toFloat( numberstr, num_val ) )
      {
        durration += num_val * unit;
      }else
      {
        cerr << "Error parsing time from '" << string(orig,orig+len) << "'" << endl;
        return durration;
      }
    }//while( *duration_str )
    
    return durration;
  }//float time_duration_string_to_seconds( const char *duration_str )

  
  //  Windows
#ifdef _WIN32
  double get_wall_time()
  {
    LARGE_INTEGER time,freq;
    if( !QueryPerformanceFrequency(&freq) )
      return -std::numeric_limits<double>::max();
    
    if( !QueryPerformanceCounter(&time) )
      return -std::numeric_limits<double>::max();
    
    return static_cast<double>(time.QuadPart) / freq.QuadPart;
  }//double get_wall_time()
#else //  Posix/Linux
  double get_wall_time()
  {
    //\todo Test std::chrono implementation and then get rid of Windows specialization
    //return std::chrono::time_point_cast<std::chrono::microseconds>( std::chrono::system_clock::now() ).time_since_epoch().count() / 1.0E6;
    struct timeval time;
    if( gettimeofday(&time,NULL) )
      return -std::numeric_limits<double>::max();
    return static_cast<double>(time.tv_sec) + (0.000001 * time.tv_usec);
  }
#endif
  
  double get_cpu_time()
  {
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
  }
  
}

