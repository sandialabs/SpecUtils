#ifndef SpecUtils_DateTime_h
#define SpecUtils_DateTime_h
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


namespace boost
{
  namespace posix_time
  {
    class ptime;
  }
}

namespace  SpecUtils
{
  //to_iso_string(...) and to_extended_iso_string(...) are implemented here
  //  to avoid having to link to the boost datetime library
  
  /** Converts the input time to a iso formated string.
   Ex. "20140414T141201.621543"
   */
  std::string to_iso_string( const boost::posix_time::ptime &t );
  
  /** Converts the input time to a extended iso formated string.
   Ex. "2014-04-14T14:12:01.621543"
   */
  std::string to_extended_iso_string( const boost::posix_time::ptime &t );
  
  /** Converts the input to string in format d-mmm-YYYY HH:MM:SS AM,
   where mmm is 3 char month name; d is day number with no leading zeros.
   Returns "not-a-date-time" if input is not valid.
   Ex. 24hr format: "2014-Sep-9 13:02:15", AMP/PM: "2014-Sep-9 03:02:15 PM"
   */
  std::string to_common_string( const boost::posix_time::ptime &t, const bool twenty_four_hour );
  
  /** Converts input to the 23 character VAX format "DD-MMM-YYYY HH:MM:SS.SS".
   Returns empty string if input is not valid.
   Ex. "2014-Sep-19 14:12:01.62"
   */
  std::string to_vax_string( const boost::posix_time::ptime &t );
  
  /* Using Howard Hinnant's date library https://github.com/HowardHinnant/date
   is nearly a drop-in, header only solution for cross platform date parsing
   that appears to work well (all date/time unit tests pass on macOS at
   least).  If we use, we could probably totally get rid a ton of the
   "pre-proccessing" string wrangling complexity too!  ...next release...,
   when we can also switch to using std::chrono::time_point instead of
   boost::posix_time::ptime
   */
#define USE_HH_DATE_LIB 1
  
  //time_from_string(...):  Currently is a convience function for
  //  time_from_string_strptime(str,MiddleEndianFirst)
  boost::posix_time::ptime time_from_string( const char *str );
  
  
  /** \brief Describes how to attempt to parse date/times when it is ambigous,
   and you might have some prior information based on the source.
   */
  enum DateParseEndianType
  {
    /** Parse date time trying middle endian (month first) before trying little
     endian, for ambiguous formats.  */
    MiddleEndianFirst,
    
    /** Parse date time trying little endian (day first) before trying middle
     endian, for ambiguous formats. */
    LittleEndianFirst,
    
    /** Only try middle endian parsing on ambiguous formats. */
    MiddleEndianOnly,
    
    /* Only try little endian parsing on ambiguous formats. */
    LittleEndianOnly
  };//enum DateParseEndianType
  
  /** Converts the input string to a ptime.
   
   Modifies 'time_string' to be in a compatible format with strptime, and then
   tries a number of common date formats to parse the date.
   Does not throw.
   
   Not tested on iOS, Android, Linux (I think just did on macOS and Windows).
   
   \param time_string Input string
   \param endian How to parse abigous dates.
   \returns If successful returns a valid ptime, if datetime couldnt be parsed
   returns invalid datetime.
   */
  boost::posix_time::ptime time_from_string_strptime( std::string time_string,
                                                     const DateParseEndianType endian );
  
  
  
  /** Reads times like ISO 8601 period formats similar to "PT16M44S" or
   "13H82M49.33S" and returns their duration in seconds.  Returns partial answer
    upon failure (and thus 0.0 on complete failure), that is "PT16M44AS" would
    return 16 minutes, 0 seconds.
   
   Note implementation is anything but complete - only implements what is
   commonly seen for real/live times in spectrum files. Instead of handling
   "PnYnMnDTnHnMnS" formats, this function only does something like "PTnHnMnS".
   
   Note: InterSpec/PhysicalUnits.h/.cpp::stringToTimeDuration(...) is a much
   more complete implementation, but hasnt been tested against parsing spectrum
   files.
   */
  float time_duration_string_to_seconds( const char *duration_str, const size_t length );

  /** Convenience function for getting time duration from a ISO 8601 like
   std::string.
   */
  float time_duration_string_to_seconds( const std::string &duration );
  
  
  /** \brief Gives the CPU time in seconds.
   
   Useful for timing things when you dont want to use chrono.
   Does not count CPU time of sub-proccesses.
   
   \returns The CPU time in seconds, or on error -DBL_MAX.
   */
  double get_cpu_time();
  
  
  /** \brief Gives the current wall time in seconds.
   
   Useful for timing things when you dont want to use chrono.
   
   \returns The wall time in seconds, or on error -DBL_MAX.
   
   Note May have an occational jump of a few seconds on Windows due to a
   hardware issue (fixed on newer windows/hardware?)
   */
  double get_wall_time();
  
}//namespace  SpecUtils

#endif //SpecUtils_DateTime_h


