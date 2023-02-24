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
#include <cstring>
#include <iostream>

#if( __cplusplus >= 201703L )
#include <charconv>
#endif

#include "3rdparty/date/include/date/date.h"

#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"

/*
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#define NOMINMAX
#include <windows.h>
#undef NOMINMAX
#endif
*/

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
  // If smallest or largest representable time point, or zero (uninitialized - digital radiation
  //  measurements from Jan 01, 1970 dont exist anymore), then assume "special", in analogy with
  //  boost::posix_time::ptime, that we upgraded the code from.
  bool is_special( const SpecUtils::time_point_t &t )
  {
    const SpecUtils::time_point_t::duration dur = t.time_since_epoch();
    return ((dur == SpecUtils::time_point_t::duration::min())
            || (dur == SpecUtils::time_point_t::duration::max())
            || (dur == SpecUtils::time_point_t::duration::zero())
            );
  }//is_special(...)


  std::string to_common_string( const SpecUtils::time_point_t &t, const bool twenty_four_hour )
  {
    if( is_special(t) )
      return "not-a-date-time";
    
    const chrono::time_point<chrono::system_clock,date::days> t_as_days = date::floor<date::days>(t);
    const date::year_month_day t_ymd = date::year_month_day{t_as_days};
    const date::hh_mm_ss<time_point_t::duration> time_of_day = date::make_time(t - t_as_days);
    
    const int year = static_cast<int>( t_ymd.year() );
    const int day = static_cast<int>( static_cast<unsigned>( t_ymd.day() ) );
    int hour = static_cast<int>( time_of_day.hours().count() );
    const int mins = static_cast<int>( time_of_day.minutes().count() );
    const int secs = static_cast<int>( time_of_day.seconds().count() );
    
    bool is_pm = (hour >= 12);
    
    if( !twenty_four_hour )
    {
      if( is_pm )
        hour -= 12;
      if( hour == 0 )
        hour = 12;
    }
    
    
    const char * const month = month_number_to_Str( static_cast<unsigned>(t_ymd.month()) );
    
    char buffer[64];
    snprintf( buffer, sizeof(buffer), "%i-%s-%04i %02i:%02i:%02i%s",
             day, month, year, hour, mins, secs, (twenty_four_hour ? "" : (is_pm ? " PM" : " AM")) );
    
    // For development, check if we can get the same answer using the date library
    const char *fmt_flgs = twenty_four_hour ? "%d-%b-%Y %H:%M:%S" : "%d-%b-%Y %I:%M:%S %p";
    string answer = date::format(fmt_flgs, date::floor<chrono::seconds>(t));
    if( answer.size() && (answer.front() == '0') )
      answer = answer.substr(1);
    
    assert( answer == buffer );
    
    return buffer;
  }//to_common_string
  
  
  std::string to_vax_string( const SpecUtils::time_point_t &t )
  {
    //Ex. "19-Sep-2014 14:12:01.62"
    if( is_special(t) )
      return "";
    
    const auto t_as_days = date::floor<date::days>(t);
    const date::year_month_day t_ymd = date::year_month_day{t_as_days};
    const date::hh_mm_ss<time_point_t::duration> time_of_day = date::make_time(t - t_as_days);
    
    const int year = static_cast<int>( t_ymd.year() );
    const int day = static_cast<int>( static_cast<unsigned>( t_ymd.day() ) );
    int hour = static_cast<int>( time_of_day.hours().count() );
    const int mins = static_cast<int>( time_of_day.minutes().count() );
    const int secs = static_cast<int>( time_of_day.seconds().count() );
    const char * const month = month_number_to_Str( static_cast<unsigned>(t_ymd.month()) );
    const auto microsecs = date::round<chrono::microseconds>( time_of_day.subseconds() );
    // We'll round to nearest hundredth; not really sure if rounding, or truncating is the proper thing to do.
    const int hundreth = static_cast<int>( std::round( microsecs.count() / 10000.0 ) ); //round to nearest hundredth of a second
    
    char buffer[32];
    snprintf( buffer, sizeof(buffer), "%02i-%s-%04i %02i:%02i:%02i.%02i",
             day, month, year, hour, mins, secs, hundreth );
    
    
    // For development, check if we can get the same answer using the date library
    string answer = date::format("%d-%b-%Y %H:%M:%S", date::floor<chrono::seconds>(t));
    char fractional[32];
    snprintf(fractional, sizeof(fractional), ".%02i", hundreth);
    answer += fractional;
    assert( answer == buffer );
    
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
  }//std::string to_vax_string( const SpecUtils::time_point_t &t );
  
  
  std::string print_to_iso_str( const SpecUtils::time_point_t &t,
                               const bool extended )
  {
    if( is_special(t) )
      return "not-a-date-time";
    
    const auto t_as_days = date::floor<date::days>(t);
    const date::year_month_day t_ymd = date::year_month_day{t_as_days};
    const date::hh_mm_ss<time_point_t::duration> time_of_day = date::make_time(t - t_as_days);
    
    const int year = static_cast<int>( t_ymd.year() );
    const int day = static_cast<int>( static_cast<unsigned>( t_ymd.day() ) );
    const int month = static_cast<int>( static_cast<unsigned>( t_ymd.month() ) );
    const int hour = static_cast<int>( time_of_day.hours().count() );
    const int mins = static_cast<int>( time_of_day.minutes().count() );
    const int secs = static_cast<int>( time_of_day.seconds().count() );
    const auto microsecs = date::round<chrono::microseconds>( time_of_day.subseconds() );
    const double frac = 1.0E-6*microsecs.count();
    
    char buffer[256];
    if( extended ) //"2014-04-14T14:12:01.621543"
      snprintf( buffer, sizeof(buffer),
               "%.4i-%.2i-%.2iT%.2i:%.2i:%09.6f",
               year, month, day, hour, mins, (secs+frac) );
    else           //"20140414T141201.621543"
      snprintf( buffer, sizeof(buffer),
               "%.4i%.2i%.2iT%.2i%.2i%09.6f",
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
    
    
    // For development, check if we can get the same answer using the date library
    //  It looks like we reliably can!
    //  TODO: decide on one implementation or the other
    const char *fmt_flgs = extended ? "%Y-%m-%dT%H:%M:%S" : "%Y%m%dT%H%M%S";
    string answer = date::format(fmt_flgs, t);
    // Trim trailing zeros
    // Note that `point` may not be '.'!!
    const size_t dec_pos = answer.find(point);
    if( (dec_pos != 0) && (dec_pos != string::npos) )
    {
      while( (answer.size() > dec_pos) && ((answer.back() == '0') || (answer.back() == point)))
        answer = answer.substr(0,answer.size() - 1);
    }
     
    assert( answer == buffer );
    
    return buffer;
  }//std::string to_extended_iso_string( const SpecUtils::time_point_t &t )
  
  std::string to_extended_iso_string( const SpecUtils::time_point_t &t )
  {
    return print_to_iso_str( t, true );
  }
  
  std::string to_iso_string( const SpecUtils::time_point_t &t )
  {
    return print_to_iso_str( t, false );
  }//std::string to_iso_string( const SpecUtils::time_point_t &t )
  
  
  SpecUtils::time_point_t time_from_string( std::string time_string,
                                                     const DateParseEndianType endian )
  {
    // A note on the current implementation (20220904):
    //  This function has evolved from ancient compilers (i.e., pre c++11) on all different
    //  platforms, including ones that didnt support strptime, to using boost, to the current
    //  implementation that assumes C++14, and uses HH's date library.   Along the way there
    //  was a lot of platform specific workarounds, for all the wacky spectrum file date
    //  formats, but its possible that a lot of them may no longer be necessary - wcjohns needs
    //  to go through and cleanup the implementation, as this a function that can take up
    //  notable time
     
#if(PERFORM_DEVELOPER_CHECKS)
    const string develop_orig_str = time_string;
#endif
    SpecUtils::to_upper_ascii( time_string );  //make sure strings like "2009-11-10t14:47:12" will work (some file parsers convert to lower case)
    SpecUtils::ireplace_all( time_string, ",", " " );
    SpecUtils::ireplace_all( time_string, "  ", " " );
    SpecUtils::ireplace_all( time_string, "_T", "T" );  //Smiths NaI HPRDS: 2009-11-10_T14:47:12Z
    SpecUtils::trim( time_string );
    
    
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
    //boost::posix_time::time_duration gmtoffset(0,0,0);
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
        //try
        //{
        //  gmtoffset = boost::posix_time::duration_from_string(offset);
        //}catch( std::exception & )
        //{
        //  cerr << "Failed to convert '" << offset << "' to time duration, from string '" << time_string << "'" << endl;
        //}
        
        //Should also make sure gmtoffset is a reasonalbe value.
        
        //      cout << "offset for '" << time_string << "' is '" << offset << "' with normal time '" <<  normal<< "'" << endl;
        time_string = normal;
      }//if( signpos != string::npos )
    }//if( offsetcolon != string::npos )
  
    /*
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
     */
    
    // SpecUtils::time_point_t only keeps to microsecond precision, so strings like
    //  "22-Mar-99 05.06.07.000000888 AM" wont parse; so we'll truncate, for the moment
    size_t fraccolon = time_string.find_last_of( ':' );
    
    //The time may not have ":" characters in it, like ISO times, so find the space
    //  that separates the date and the time.
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
      const size_t period = time_string.rfind( '.' );  //For string strings like "22-MAR-99 05.06.07.888 AM", we will search from the back
      
      //Check period is greater than three places to the right of a space
      bool aftertime = false;
      for( size_t pos = period; pos!=string::npos && period; --pos )
      {
        if( isspace(time_string[pos]) )
        {
          aftertime = ((period-pos) > 5);
          break;
        }
      }
      
      //Check that we found the period, and each side
      if( period > (fraccolon+1)
          && aftertime
          && period != string::npos && period > 0 && (period+1) < time_string.size()
          && isdigit(time_string[period-1]) && isdigit(time_string[period+1]) )
      {
        // Check if we need to truncate the precision down to microseconds
        const size_t last = time_string.find_first_not_of( "0123456789", period+1 );
        if( last == string::npos )
        {
          // Numbers go to end of string, eg "22-Mar-99 5.6.7.0000008888"
          if( (time_string.size() - period - 1) > 6 )
            time_string.erase(begin(time_string) + period + 7, end(time_string));
        }else if( (last - period) > 6 ) //
        {
          // Numbers do not go to end of string, eg "22-Mar-99 5.6.7.0000008888 AM"
          time_string.erase( begin(time_string) + period + 7,  begin(time_string) + last );
        }
      }//if( period != string::npos )
    }//if( fraccolon != string::npos )
    
    // I dont think we should get a false-positive for checking for AM PM (e.g., from a month
    //  spelled out or something)
    const bool has_am_pm = (SpecUtils::icontains(time_string, "AM")
                            || SpecUtils::icontains(time_string, "PM"));
    
    //middle: whether to try a middle endian (date start with month number)
    //  date decoding first, or alternatively little endian (date start with day
    //  number).  Both endians will be tried, this just selects which one first.
    const bool middle = (endian == MiddleEndianFirst);
    const bool only = (endian == MiddleEndianOnly || endian == LittleEndianOnly);
    
    // The `bool` in the std::pair indicates whether the format flag reads in AM/PM or not.
    //  Above, we checked for the "AM" and "PM" strings, so to save a little time, and avoid
    //  missing a PM (a non-AM/PM aware format flag will just ignore the PM, and be
    //  successful), we'll only use format flags that match `has_am_pm`.  I'm not crazy
    //  about this, but seems to work, for now
    static const std::pair<bool, const char *> formats[] =
    {
      {true, "%Y-%m-%d%n%I:%M:%S%n%p"},  // 2010-01-15 06:21:15 PM
      {false, "%Y-%m-%d%n%H:%M:%SZ"},    // 2010-01-15T23:21:15Z    //I think this is the most common format in N42-2012 files, so try it early on.
      {false, "%Y-%m-%d%n%H:%M:%S"},     // 2010-01-15 23:21:15     //  Sometimes the N42 files don't have the Z though, or have some offset (we'll ignore)
      {false, "%Y%m%d%n%H%M%S"},         // 20100115T232115         //ISO format
      {true, "%d-%b-%Y%n%r"},            // 1-Oct-2004 12:34:42 AM  //"%r" ~ "%I:%M:%S %p"
      {true, "%d-%b-%y%n%r"},            // '15-MAY-14 08:30:44 PM'  (disambiguate: May 15 2014)
      {false, "%Y%m%d%n%H:%M:%S"},       // 20100115T23:21:15
      {true, (middle ? "%m/%d/%Y%n%I:%M%n%p" : "%d/%m/%Y%n%I:%M%n%p")},  //1/18/2008 2:54 PM
      {true, (only ? "" : (middle ? "%d/%m/%Y%n%I:%M%n%p" : "%m/%d/%Y%n%I:%M%n%p"))},
      {true, (middle ? "%m/%d/%Y%n%r" : "%d/%m/%Y%n%r")},                //1/18/2008 2:54:44 PM
      {true, (only ? "" : (middle ? "%d/%m/%Y%n%r" : "%m/%d/%Y%n%r"))},
      {false, (middle ? "%m/%d/%Y%n%H:%M:%S" : "%d/%m/%Y%n%H:%M:%S")},   //08/05/2014 14:51:09
      {false, (only ? "" : (middle ? "%d/%m/%Y%n%H:%M:%S" : "%m/%d/%Y%n%H:%M:%S"))},
      {false, (middle ? "%m/%d/%Y%n%H:%M" : "%d/%m/%Y%n%H:%M")},         //08/05/2014 14:51
      {false, (only ? "" : (middle ? "%d/%m/%Y%n%H:%M" : "%m/%d/%Y%n%H:%M"))},
      {true, (middle ? "%m-%d-%Y%n%I:%M%n%p" : "%d-%m-%Y%n%I:%M%n%p")},  //14-10-2014 6:15 PM
      {true, (only ? "" : (middle ? "%d-%m-%Y%n%I:%M%n%p" : "%m-%d-%Y%n%I:%M%n%p" ))},
      {true, (middle ? "%m-%d-%Y%n%I:%M:%S%n%p" : "%d-%m-%Y%n%I:%M:%S%n%p")}, //14-10-2014 06:15:52 PM
      {true, (only ? "" : (middle ? "%d-%m-%Y%n%I:%M:%S%n%p" : "%m-%d-%Y%n%I:%M:%S%n%p" ))},
      {false, (middle ? "%m-%d-%Y%n%H:%M:%S" : "%d-%m-%Y%n%H:%M:%S")}, //14-10-2014 16:15:52
      {false, (only ? "" : (middle ? "%d-%m-%Y%n%H:%M:%S" : "%m-%d-%Y%n%H:%M:%S" ))},
      {false, (middle ? "%m-%d-%Y%n%H:%M" : "%d-%m-%Y%n%H:%M")}, //14-10-2014 16:15
      {false, (only ? "" : (middle ? "%d-%m-%Y%n%H:%M" : "%m-%d-%Y%n%H:%M" ))},
//#if( defined(_WIN32) && defined(_MSC_VER) )
//      (middle ? "%m %d %Y %H:%M:%S" : "%d %m %Y %H:%M:%S"), //14 10 2014 16:15:52
//      (only ? "" : (middle ? "%d %m %Y %H:%M:%S" : "%m %d %Y %H:%M:%S")),
//#else
      {false, (middle ? "%m%n%d%n%Y%n%H:%M:%S" : "%d%n%m%n%Y%n%H:%M:%S")}, //14 10 2014 16:15:52
      {false, (only ? "" : (middle ? "%d%n%m%n%Y%n%H:%M:%S" : "%m%n%d%n%Y%n%H:%M:%S" ))},
//#endif
      {false, "%d-%b-%y%n%H:%M:%S"}, //16-MAR-06 13:31:02, or "12-SEP-12 11:23:30"
      {true, "%d-%b-%Y%n%I:%M:%S%n%p"}, //31-Aug-2005 6:38:04 PM,
      {true, "%d %b %Y%n%I:%M:%S%n%p"}, //31 Aug 2005 6:38:04 PM
      {true, "%b %d %Y%n%I:%M:%S%n%p"}, //Mar 22, 1999 5:06:07 AM
      {false, "%d-%b-%Y%n%H:%M:%S"}, //31-Aug-2005 12:38:04,
      {false, "%d %b %Y%n%H:%M:%S"}, //31 Aug 2005 12:38:04
      {false, "%d-%b-%Y%n%H:%M:%S%nZ"},//9-Sep-2014T20:29:21 Z
      {false, (middle ? "%m-%m-%Y%n%H:%M:%S" : "%d-%m-%Y%n%H:%M:%S" )}, //"10-21-2015 17:20:04" or "21-10-2015 17:20:04"
      {false, (only ? "" : (middle ? "%d-%m-%Y%n%H:%M:%S" : "%m-%m-%Y%n%H:%M:%S" ))},
      {false, "%d.%m.%y%n%H:%M:%S"},  //28.02.13 13:42:47      //We need to check '%y' before '%Y'
      {false, "%d.%m.%Y%n%H:%M:%S"},  //28.02.2013 13:42:47
      //      (middle ? "%m.%d.%Y%n%H:%M:%S" : "%d.%m.%Y%n%H:%M:%S"), //26.05.2010 02:53:49
      //      (only ? "" : (middle ? "%d.%m.%Y%n%H:%M:%S" : "%m.%d.%Y%n%H:%M:%S")),
      {false, "%b. %d %Y%n%H:%M:%S"},//May. 21 2013  07:06:42
      //      (middle ? "%m.%d.%y%n%H:%M:%S" : "%d.%m.%y%n%H:%M:%S"),  //'28.02.13 13:42:47'
      {false, (middle ? "%m.%d.%y%n%H:%M:%S" : "%d.%m.%y%n%H:%M:%S")}, //'3.14.06 10:19:36' or '28.02.13 13:42:47'
      {false, (only ? "" : (middle ? "%d.%m.%y%n%H:%M:%S" : "%m.%d.%y%n%H:%M:%S" ))},
      {false, (middle ? "%m.%d.%Y%n%H:%M:%S" : "%d.%m.%Y%n%H:%M:%S")}, //'3.14.2006 10:19:36' or '28.02.2013 13:42:47'
      {false, (only ? "" : (middle ? "%d.%m.%Y%n%H:%M:%S" : "%m.%d.%Y%n%H:%M:%S" ))},
      {false, "%d.%m.%y%n%H:%M:%S"},
      //      (only ? "" : (middle ? "%d.%m.%y%n%H:%M:%S" : "%m.%d.%y%n%H:%M:%S")),
      {false, "%Y.%m.%d%n%H:%M:%S"}, //2012.07.28 16:48:02
      {false, "%d.%b.%Y%n%H:%M:%S"},//01.Nov.2010 21:43:35
      {false, "%Y%m%d%n%H:%M:%S"},  //20100115 23:21:15
      {false, "%Y-%b-%d%n%H:%M:%S"}, //2017-Jul-07 09:16:37
      {false, "%Y/%m/%d%n%H:%M:%S"}, //"2020/02/12 14:57:39"
      {false, "%Y/%m/%d%n%H:%M"}, //"2020/02/12 14:57"
      {false, "%Y-%m-%d%n%H-%M-%S"}, //2018-10-09T19-34-31_27 (not sure what the "_27" exactly means)
      {false, "%Y%m%d%n%H%M"},  //20100115T2321
      {true, (middle ? "%m/%d/%y%n%r" : "%d/%m/%y%n%r")}, //'6/15/09 11:12:30 PM' or '15/6/09 11:12:30 PM'
      {true, (only ? "" : (middle ? "%d/%m/%y%n%r" : "%m/%d/%y%n%r" ))},
      {false, (middle ? "%m/%d/%y%n%H:%M:%S" : "%d/%m/%y%n%H:%M:%S")}, //'6/15/09 13:12:30' or '15/6/09 13:12:30'
      {false, (only ? "" : (middle ? "%d/%m/%y%n%H:%M:%S" : "%m/%d/%y%n%H:%M:%S" ))},
      {true, (middle ? "%m-%d-%y%n%I:%M%n%p" : "%d-%m-%y%n%I:%M%n%p")}, //'03-22-99 5:06 AM' or '22-03-99 5:06 AM'
      {true, (only ? "" : (middle ? "%d-%m-%y%n%I:%M%n%p" : "%m-%d-%y%n%I:%M%n%p" ))},
      
      {true, (middle ? "%m/%d/%y%n%I:%M%n%p" : "%d/%m/%y%n%I:%M%n%p")}, //'6/15/09 05:12 PM' or '15/6/09 05:12 PM'
      {true, (only ? "" : (middle ? "%d/%m/%y%n%I:%M%n%p" : "%m/%d/%y%n%I:%M%n%p" ))},
      
      {false, (middle ? "%m/%d/%y%n%H:%M" : "%d/%m/%y%n%H:%M")}, //'6/15/09 13:12' or '15/6/09 13:12'
      {false, (only ? "" : (middle ? "%d/%m/%y%n%H:%M" : "%m/%d/%y%n%H:%M" ))},
      {true, "%b %d %Y%n%I:%M%n%p"}, //Mar 22, 1999 5:06 AM
      {false, "%b %d %Y%n%H:%M:%S"}, //Mar 22, 1999 5:06:07
      {false, "%b %d %Y%n%H:%M"}, //Mar 22, 1999 5:06:07
      {true, "%d-%b-%Y%n%I.%M.%S%n%p"}, //'22-MAR-2010 05.06.07.888 AM
      {true, "%d-%b-%y%n%I.%M.%S%n%p"}, //'22-MAR-99 05.06.07.888 AM
      {false, "%d-%b-%Y%n%H.%M.%S"}, //'22-MAR-1999 05.06'
      {true, (middle ? "%m-%d-%y%n%I:%M:%S%n%p" : "%d-%m-%y%n%I:%M:%S%n%p")}, //'03-22-99 5:06:07 AM' or '22-03-99 5:06:07 AM'
      {true, (only ? "" : (middle ? "%d-%m-%y%n%I:%M:%S%n%p" : "%m-%d-%y%n%I:%M:%S%n%p" ))},
      {true, "%Y-%m-%d%n%I:%M%n%p"}, //'1999-03-22 5:06 AM'
      {false, "%Y-%m-%d%n%H:%M"}, //'1999-03-22 5:06'
      {true, "%d/%b/%Y%n%I:%M:%S%n%p"}, //'22/Mar/1999 5:06:07 PM'
      {true, "%d/%b/%Y%n%I:%M%n%p"}, //'22/Mar/1999 5:06 PM'
      {false, "%d/%b/%Y%n%H:%M:%S"}, //'22/Mar/1999 5:06:07'
      {true, "%d/%b/%y%n%I:%M%n%p"}, //'22/Mar/99 5:06 PM'
      {false, "%d/%b/%y%n%H:%M:%S"}, //'22/Mar/99 5:06:07'
      {false, "%d.%b.%Y%n%H%M%S"},    // '01.Nov.2010 214335'
      {false, "%b.%n%d%n%Y%n%H%M%S"}, //'May. 21 2013 070642'
      {false, "%d.%m.%y%n%H:%ML%s"}, //'22.3.99 5:06:01'
      {false, "%d.%m.%y%n%H:%M"}, //'22.3.99 5:06'
      {false, "%m-%d-%y%n%H:%M:%S"}, //'03-22-99 05:06:01'
      {false, "%m-%d-%y%n%H:%M"}, //'03-22-99 05:06'
      {false, "%y-%m-%d%n%H:%M"}, //'99-03-22 05:06'
      {false, "%d/%b/%y%n%H.%M.%S"}, //'22/Mar/99 5.06.07'
      {false, "%d/%m/%y%n%H.%M"}, //'22/03/99 5.06'
      
      //Below here are dates only, with no times
      {false, (middle ? "%m/%d/%Y" : "%d/%m/%Y")}, //'6/15/2009' or '15/6/2009'
      {false, (only ? "" : (middle ? "%d/%m/%Y" : "%m/%d/%Y" ))},
      {false, "%d-%b-%Y"},           //"00-Jan-2000 "
      {false, "%Y/%m/%d"}, //"2010/01/18"
      {false, "%Y-%m-%d"}, //"2010-01-18"
      {false, "%Y%m%d"}, //"20100118"
      {false, "%b%n%d%n%Y"}, //March 22, 1999
      {false, "%b%n%d%n%y"}, //March 22, 99
      {false, "%b.%d.%Y"}, //March.22.1999
      {false, "%b.%d.%y"}, //March.22.99
      {false, "%d/%m/%y"}, //'22/03/99'
    };
    
    
    const size_t nformats = sizeof(formats) / sizeof(formats[0]);
    const char *timestr = time_string.c_str();
    
#if( PERFORM_DEVELOPER_CHECKS )
    for( size_t i = 0; i < nformats; ++i )
    {
      const bool fmt_is_am_pm = formats[i].first;
      const string fmt = formats[i].second;
      size_t pos = fmt.find("%r");
      if( pos == string::npos )
        pos = fmt.find("%p");
      assert( fmt_is_am_pm == (pos != string::npos) );
    }
#endif //#if( PERFORM_DEVELOPER_CHECKS )
    
    for( size_t i = 0; i < nformats; ++i )
    {
      const bool fmt_is_am_pm = formats[i].first;
      if( has_am_pm != fmt_is_am_pm )
        continue;
      
      const char * const fmt = formats[i].second;
      
      if( !fmt[0] )
        continue;
      
      std::chrono::time_point<std::chrono::system_clock,std::chrono::microseconds> tp{};
      std::istringstream input(timestr);

      try
      {
        //From fuzz testing, date::parse can throw an exception of type
        //  "type std::invalid_argument: stold: no conversion", so we'll use a try catch, in
        //  addition to the actual test.
        //  We want to return an invalid date/time, rather than throw an exception from this
        //  function.
        //  (note: this was found using the version of the Date library downloaded 20200502 with
        //         git hash e12095f, so can be re-evaluated when the library is updated.)
        if( (input >> date::parse(fmt, tp)) )
        {
          // Dates such as '6/15/09 13:12:30' will parse as '15-Jun-0009 13:12:30' for cases where
          // "%Y" was tried before "%y".  We could change the ordering of format strings, or do some
          // string manipulation, but for the moment we'll just check to see if the year is a
          // reasonable value.
          const chrono::time_point<chrono::system_clock,date::days> tp_as_days = date::floor<date::days>(tp);
          const date::year_month_day ymd = date::year_month_day{tp_as_days};
          const int year = static_cast<int>( ymd.year() );
          if( year < 1000 )
            continue;
            
          return tp;
        }//if( we parsed the string )
      }catch( std::exception & )
      {
      }
    }//for( size_t i = 0; i < nformats; ++i )
    
    
#define CREATE_datetimes_TEST_FILE 0
    
#if( CREATE_datetimes_TEST_FILE )
    static std::mutex datetimes_file_mutex;
    static int ntimes_called = 0;
    string inputstr = time_string;
    SpecUtils::ireplace_all(inputstr, ",", "");
    if( inputstr.size() && inputstr[0]=='#' )
      inputstr = inputstr.substr(1);
    
    auto result = time_from_string( time_string, MiddleEndianFirst );
    
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
#endif
    
    
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
    if( develop_orig_str.size() > 5 //5 is arbitrary
       && develop_orig_str.find("NA")==string::npos
       && std::count( begin(develop_orig_str), end(develop_orig_str), '0') < 8 )
      log_developer_error( __func__, ("Failed to parse date/time from: '" + develop_orig_str  + "' which was massaged into '" + time_string + "'").c_str() );
#endif
    
    return SpecUtils::time_point_t{};
  }//SpecUtils::time_point_t time_from_string( std::string time_string )
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

  
  double delimited_duration_string_to_seconds( const std::string &input )
  {
    //cout << "Input string: '" << input << "'" << endl;
    double answer = 0.0;
    
    //Trim string end
    size_t str_len = input.size();
    while( str_len > 0 && isspace(input[str_len-1]) )
      --str_len;
    
    //Trim string begining
    size_t field_start = 0;
    while( field_start < input.size() && isspace(input[field_start]) )
      ++field_start;
    
    const bool is_neg = (field_start < str_len) && (input[field_start] == '-');
    const bool has_plus = (field_start < str_len) && (input[field_start] == '+');
    if( is_neg || has_plus )
    {
      ++field_start;
      while( field_start < input.size() && isspace( input[field_start] ) )
        ++field_start;
    }

    if( field_start >= str_len )
      throw runtime_error( "empty input" );
    
    const std::string delims = ":";  //"-:,."
    //for( auto c : delims ){ assert( !isspace(c) ); }
    
    size_t fieldnum = 0;
    
    do
    {
      size_t next_delim = input.find_first_of( delims, field_start );
      if( next_delim == string::npos )
        next_delim = str_len;
      
      const char *str_start = input.c_str() + field_start;
      const char *str_end = input.c_str() + next_delim;
      
      //cout << "\tField " << fieldnum << ": '" << string(str_start,str_end) << "'" << endl;
      
      switch( fieldnum )
      {
        case 0:
        case 1:
        {   
          for( const char *ch = str_start; ch != str_end; ++ch )
          {
            if( !isdigit(*ch) && ((*ch) != '-') && ((*ch) != '+') )
              throw runtime_error( string("Invalid character ('") + (*ch) + string("')") );
          }
#if( __cplusplus >= 201703L )
          unsigned long value;
          const auto result = std::from_chars(str_start, str_end, value);
          if( (bool)result.ec || (result.ptr != str_end) )
            throw runtime_error( "Invalid hours or minutes field: '" + std::string(str_start,str_end) + "'" );
#else
          const unsigned long value = std::stoul( std::string(str_start,str_end), nullptr, 10 );
#endif
          if( fieldnum == 1 && value >= 60 )
            throw runtime_error( "Hours or Minutes is larger than 60 (" + std::to_string(value) + ")" );
          
          answer += value * (fieldnum==0 ? 3600.0 : 60.0);
          break;
        }//
        
        case 2:
        {
          size_t end_idx;
//#if( __cplusplus >= 201703L )
//My compiler doesnt support from_chars from doubles, so this hasnt been tested.
//          double value;
//          const auto result = std::from_chars(str_start, str_end, value, std::chars_format::general );
//          if( (bool)result.ec || (result.ptr != str_end) )
//            throw runtime_error( "Invalid second field: '" + std::string(str_start,str_end) + "'" );
//#else
          const double value = std::stod( std::string(str_start,str_end), &end_idx );
          if( str_start+end_idx != str_end )
            throw runtime_error( "Invalid second field: '" + std::string(str_start,str_end) + "'" );
//#endif
          if( value >= 60.0 )
            throw runtime_error( "Seconds is larger than 60 (" + std::to_string(value) + ")" );
          if( value < 0.0 )
            throw runtime_error( "Seconds value is negative (" + std::to_string(value) + ")" );
          
          answer += value;
        }
          break;
          
        default:
          throw std::runtime_error( "to many fields" );
      }//switch( fieldnum )
      
      ++fieldnum;
      field_start = next_delim + 1;
      
      //Check if the string finished with a delimiter
      if( field_start == str_len )
        throw runtime_error( "trailing delimiter" );
    }while( field_start < str_len );
    
    if( fieldnum < 2 )
      throw std::runtime_error( "no delimiters found" );
    
    if( is_neg )
      answer *= -1.0;
    
    //cout << "\tvalue=" << answer << endl << endl;
    
    return answer;
  }//double delimited_duration_string_to_seconds( std::string duration )


/*
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
*/
  double get_wall_time()
  {
    //struct timeval time;
    //if( gettimeofday(&time,NULL) )
    //  return -std::numeric_limits<double>::max();
    //return static_cast<double>(time.tv_sec) + (0.000001 * time.tv_usec);
    
    const auto now = chrono::steady_clock::now();
    return 1.0E-6 * chrono::time_point_cast<chrono::microseconds>( now ).time_since_epoch().count();
  }
//#endif
  
  double get_cpu_time()
  {
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
  }
}

