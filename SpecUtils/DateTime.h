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

#include <chrono>
#include <string>

namespace  SpecUtils
{
  using time_point_t = std::chrono::time_point<std::chrono::system_clock,std::chrono::microseconds>;

  /** Returns true if the time_point_t is the smallest or largest representable time point,
   or zero (e.g., uninitialized since digital radiation measurements from Jan 01, 1970 dont
   exist anymore), or false for all other values.
   
   This is in analogy with boost::posix_time::ptime, that we upgraded the code from.
   */
  SpecUtils_DLLEXPORT 
  bool is_special( const time_point_t &t );

  //to_iso_string(...) and to_extended_iso_string(...) are implemented here
  //  to avoid having to link to the boost datetime library
  
  /** Converts the input time to an iso formatted string.
   Ex. "20140414T141201.621543"
   */
  SpecUtils_DLLEXPORT
  std::string to_iso_string( const time_point_t &t );
  
  /** Converts the input time to an extended iso formatted string.
   Ex. "2014-04-14T14:12:01.621543"
   */
  SpecUtils_DLLEXPORT
  std::string to_extended_iso_string( const time_point_t &t );
  
  /** Converts the input to string in format d-mmm-YYYY HH:MM:SS AM,
   where mmm is 3 char month name; d is day number with no leading zeros.
   Returns "not-a-date-time" if input is not valid.
   Ex. 24hr format: "9-Sep-2014 15:02:15", AM/PM: "9-Sep-2014 03:02:15 PM"
   */
  SpecUtils_DLLEXPORT
  std::string to_common_string( const time_point_t &t, const bool twenty_four_hour );
  
  /** Converts input to the 23 character VAX format "DD-MMM-YYYY HH:MM:SS.SS".
   Returns empty string if input is not valid.
   Ex. "19-Sep-2014 14:12:01.62"
   */
  SpecUtils_DLLEXPORT
  std::string to_vax_string( time_point_t t );
  
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
  
  /** Converts the input string to a std::chrono::time_point.
   
   Tries a number of common date formats to parse the date.
   
   Since #SpecUtils::time_point_t has a precision of microseconds, any time
   accuracy past microseconds, is truncated.
   
   Any time-zone information is discarded ("2015-05-16T05:50:06-04:00" will
   parse as "2015-05-16T05:50:06").
   
   Date parsing is focused on spectrum file dates, so may not parse
   dates in distant past or future, or non-ascii dates, or other uncommon
   (for spectrum files) situations.
   
   Not tested on iOS, Android, Linux (I think just did on macOS and Windows).
   
   \param time_string Input string
   \param endian How to parse ambiguous dates.
   \returns If successful returns a valid time_point, if `time_string`
            couldn't be parsed returns time_point_t{} (i.e., 0).
   
   Does not throw.
   */
  SpecUtils_DLLEXPORT
  time_point_t time_from_string( std::string time_string,
                                 const DateParseEndianType endian = DateParseEndianType::MiddleEndianFirst );
  
  
  
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
  SpecUtils_DLLEXPORT
  float time_duration_string_to_seconds( const char *duration_str, const size_t length );

  /** Convenience function for getting time duration from a ISO 8601 like
   std::string.
   */
  SpecUtils_DLLEXPORT
  float time_duration_string_to_seconds( const std::string &duration );
  
  /** Converts a string formatted like "[-]h[h][:mm][:ss][.fff]", (ex. "02:15:01.332") to number of
   seconds.
   
   Duration will be negative if first character is '-'.
   Unlike boost::posix_time::duration_from_string (which uses delimiters "-:,."), the only valid
   delimiter is a colon (':').  Leading and trailing whitespaces are ignored.
   
   Some examples:
    - "1:13:1.2" is on hour, thirteen minutes, and 1.2 seconds
    - "01:15" or equivalently "1:15" is on hour and fifteen minutes
    - "-01:00" is negative one hour
    - " \t13:12:1.1   " is thirteen hours, twelve minutes, and 1.1 seconds.
    - Invalid examples: ":", ":32", ":32:16", "--01:32:16", "12:32a", "12::1", "12:01:-2", "12:32:"
                        "12:32 a", "a12:13", "a 12:13"
   
   Throws exception if input is invalid.
   */
  SpecUtils_DLLEXPORT
  double delimited_duration_string_to_seconds( const std::string &duration );

  /** \brief Gives the CPU time in seconds.
   
   Useful for timing things when you dont want to use chrono.
   Does not count CPU time of sub-processes.
   
   \returns The CPU time in seconds, or on error -DBL_MAX.
   */
  SpecUtils_DLLEXPORT
  double get_cpu_time();
  
  
  /** \brief Gives the current wall time in seconds.
   
   Useful for timing things when you dont want to use chrono.
   
   \returns The wall time in seconds, or on error -DBL_MAX.
   
   Note May have an occasional jump of a few seconds on Windows due to a
   hardware issue (fixed on newer windows/hardware?)
   */
  SpecUtils_DLLEXPORT
  double get_wall_time();
  
}//namespace  SpecUtils

#endif //SpecUtils_DateTime_h


