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

#include <string>
#include <vector>
#include <istream>
#include <ostream>

/** Some functions and definitions that help to parse and write spectrum files,
 but maybe dont fit in other sections of code.
 */
namespace  SpecUtils
{
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
  
  
  /** Returns the dose units indicated by the string, in units such that ia
   micro-sievert per hour is equal to 1.0.
    Currently only handles the label "uSv" and "uRem/h", e.g. fnctn not really
    impleneted.
    Returns 0.0 on error.
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
}//namespace  SpecUtils



//Implementation
namespace SpecUtils
{
  /** @TODO check if type is int/float/etc and alsoo check if host is big/little
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
}

#endif //SpecUtils_ParseUtils_h

