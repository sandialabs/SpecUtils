#ifndef SpecUtils_ParseUtils_h
#define SpecUtils_ParseUtils_h
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

#include <limits>
#include <string>
#include <vector>
#include <istream>
#include <ostream>

/** Some functions and definitions that help to parse and write spectrum files,
 but maybe dont fit in other sections of code.
 */
namespace  SpecUtils
{
  /** \brief Gets a line from the input stream that may be terminated with
   either UNIX or Windows EOL characters.
 
   See code for code source.
   Note that this function is probably very slow, and could be upgraded.
  */
  std::istream &safe_get_line( std::istream &is, std::string &t );


  /** Same as other variant of #safe_get_line, except allows specifying the
   maximum number of bytes to read; specifying zero means no limit.
   */
  std::istream &safe_get_line( std::istream &is, std::string &t, const size_t maxlength );


  /** Expands the N42 counted zeros scheme, i.e., if an entry is zero, then the
   entry after that says how many zeroes that elememnt should be expanded to.
   
   Requires zeros to be identically 0.0f, in order be expanded by the next
   value.  The value following a zero is rounded to nearest integer (no integer
   check is performed).
   */
  void expand_counted_zeros( const std::vector<float> &data,
                            std::vector<float> &results );
  

  /** Performs the counted zero compression.
   Note that contents less than 10.0f*FLT_MIN are assumed to be zeros.
   */
  void compress_to_counted_zeros( const std::vector<float> &data,
                                 std::vector<float> &results );

  
  /** Parses strings similar to "25°47\"17.820' N / 80°19\"25.500' W" into
   latitude and longitude.
   
   Not currently super-well implemented, or incredibly robust, but good enough
   for a few spectrum file formats encountered.
   
   @return Success status of parsing the input string to lat/long
   */
  bool parse_deg_min_sec_lat_lon( const char *str, const size_t len,
                                  double &lat, double &lon );

  
  /** Parses a string like "25°47\"17.820' N" or "80°19\"25.500' W" into the
   apropriate degrees
   
   @return on success returns parsed value, or on failure returns -999.9
   */
  double conventional_lat_or_long_str_to_flt( std::string input );
  
  
  /** Checks if abs(latitude) is less or equal to 90. */
  bool valid_latitude( const double latitude );
  
  
  /** Checks if abs(longitude) is less than or equal to 180. */
  bool valid_longitude( const double longitude );
  

  /** Tries to extract sample number from remark in file - mostly from N42-2006
   files.
   */
  int sample_num_from_remark( std::string remark ); //returns -1 on error
  
  /** Tries to extract speed from a remark string - mostly from N42-2006, and
   returns it in m/s.
   
   Ex., takes a line like "Speed = 5 mph" and returns the speed in m/s.
   Returns 0.0 on failure.
   
   Note: not very generically implemented, just covers cases that have been ran
   into.
   */
  float speed_from_remark( std::string remark );
  

  /** Looks for GADRAS style detector names in remarks, or something from the
      N42 conventions of 'Aa1', 'Aa2', etc.  Returns empty string on failure.
   */
  std::string detector_name_from_remark( const std::string &remark );
  
  /** Looks for x position information in remark
  
  * ex: "Title: FA-SG-LANL-0-0-8{dx=-155.6579,dy=-262.5} @235cm H=262.5cm V=221.1404cm/s : Det=Ba2"
  */
  float dx_from_remark( std::string remark );

  /** Looks for y position information in remark

* ex: "Title: FA-SG-LANL-0-0-8{dx=-155.6579,dy=-262.5} @235cm H=262.5cm V=221.1404cm/s : Det=Ba2"
*/
  float dy_from_remark(std::string remark);

  
  /** Returns the dose units indicated by the string, in units such that 1.0
   micro-sievert per hour is equal to 1.0.
    
   Currently only handles the label "uSv" and "uRem/h",
   E.g. function only barely implemented.
    
   Throws exception on error.
   */
  float dose_units_usvPerH( const char *str, const size_t str_length );

  
  /** Convert from 2006 N42 to 2012 instrument types
   E.x., "PortalMonitor" -> "Portal Monitor", or
  "SpecPortal" -> "Spectroscopic Portal Monitor"
   */
  const std::string &convert_n42_instrument_type_from_2006_to_2012(
                                                    const std::string &input );
  
  
  /** Reads a POD from an istream.  Currently no endianess transform is done. */
  template <class T>
  std::istream &read_binary_data( std::istream &input, T &val );
  
  
  /** Writes a POD from an istream.
   Currently no endianess transform is done.
   
   @returns the number of bytes written.
   */
  template <class T>
  size_t write_binary_data( std::ostream &input, const T &val );


  /** Converts from a float value, to the nearest representable integer value.
   
   Rounds the input val (as a float) and clamps the returned value to the representable integer
   range.
   
   Performing this operation, without invoking undefined behavior, unexpected float<-->int issues,
   and getting correct values near the integer limits turns out to be surprisingly tricky, but
   hopefully this function does everything correctly.  It was tested for uint32_t, but *should*
   work fine for other integer types.
   */
  template<class Integral>
  Integral float_to_integral( float val );
}//namespace  SpecUtils

#include <cmath>
#include <type_traits>

//Implementation
namespace SpecUtils
{
  /** @TODO check if type is int/float/etc and also check if host is big/little
            endian and if need be convert from little to host.
      @TODO add a template check for is_pod, eg std::enable_if_t<std::is_pod<T>{}, bool> =true
   */
  template <class T>
  std::istream &read_binary_data( std::istream &input, T &val )
  {
    input.read( (char *)&val, sizeof(T) );
    return input;
  }
  
  /** @TODO check if type is int/float/etc and also check if host is big/little
            endian and if need be convert from host to little.
      @TODO add a template check for is_pod, eg std::enable_if_t<std::is_pod<T>{}, bool> =true
      @TODO make sure T doest resolve to a reference type or anything
   */
  template <class T>
  size_t write_binary_data( std::ostream &input, const T &val )
  {
    input.write( (const char *)&val, sizeof(T) );
    return sizeof(T);
  }

template<class Integral>
Integral float_to_integral( float d )
{
  /*
    Some tests you can run for this function
    assert( float_to_integral<uint32_t>(0.0f) == 0 );
    assert( float_to_integral<uint32_t>(-0.1f) == 0 );
    assert( float_to_integral<uint32_t>(-1.0f) == 0 );
    assert( float_to_integral<uint32_t>(0.499f) == 0 );
    assert( float_to_integral<uint32_t>(0.5f) == 1 );
    assert( float_to_integral<uint32_t>(1.5f) == 2 );
    assert( float_to_integral<uint32_t>(1.4999f) == 1 );
    assert( float_to_integral<uint32_t>(1024.1f) == 1024 );
    assert( float_to_integral<uint32_t>(1024.8f) == 1025 );
    // 4294967295 is the largest uint32_t, but if you convert to a float, it will have value
    //  4294967296.0f, which is larger.
    assert( float_to_integral<uint32_t>(4294967296.0f) == 4294967295 );
    // The next value representable by a float above 4294967296, is 4294967808
    assert( float_to_integral<uint32_t>(4294967808.0f) == 4294967295 );
    // The next value representable by a float below 4294967296, is 4294967040
    assert( float_to_integral<uint32_t>(4294967040.0f) == 4294967040 );
   
    // Almost all my testing was for uint32_t, but it passes some sanity checks for signed ints
    assert( float_to_integral<int32_t>(1.0f) == 1 );
    assert( float_to_integral<int32_t>(-1.0f) == -1 );
    assert( float_to_integral<int32_t>(-1024.0f) == -1024 );
    assert( float_to_integral<int32_t>(-0.1) == 0 );
    assert( float_to_integral<int32_t>(-0.4999) == 0 );
    assert( float_to_integral<int32_t>(-0.5) == -1 );
    assert( float_to_integral<int32_t>(-0.51) == -1 );
   */
  
    const float orig = d;
    static constexpr int max_exp = std::is_signed<Integral>() ? ((sizeof(float)*8)-1) : (sizeof(float)*8);
  
    if( IsNan(d) || IsInf(d) )
      return 0;
  
    d = std::round(d);
  
    if( !std::is_signed<Integral>() && std::signbit(d) )
      return 0;
  
    int exp;
    std::frexp(d, &exp);
  
    if( exp <= max_exp )
      return static_cast<Integral>( d );
  
    static constexpr Integral min_int_val = std::numeric_limits<Integral>::min();
    static constexpr Integral max_int_val = std::numeric_limits<Integral>::max();
    return std::signbit(d) ? min_int_val : max_int_val;
  }//float_to_integral
}//namespace SpecUtils

#endif //SpecUtils_ParseUtils_h

