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

#include <vector>
#include <string>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>


#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/Filesystem.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

namespace
{
}//namespace


namespace SpecUtils
{
bool SpecFile::load_tka_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_tka( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_tka_file( const std::string &filename )

  
bool SpecFile::load_from_tka( std::istream &input )
{
  /*
   Simple file with one number on each line with format:
   Live time
   Real time
   counts first channel
   .
   .
   .
   counts last channel
   */
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  
  try
  {
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( orig_pos, ios::beg );
    const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );
    if( filesize > 512*1024 )
      throw runtime_error( "File to large to be TKA" );
    
    //ToDo: check UTF16 ByteOrderMarker [0xFF,0xFE] as first two bytes.
    
    auto get_next_number = [&input]( float &val ) -> int {
      const size_t max_len = 128;
      string line;
      if( !SpecUtils::safe_get_line( input, line, max_len ) )
        return -1;
      
      if( line.length() > 32 )
        throw runtime_error( "Invalid line length" );
      
      SpecUtils::trim(line);
      if( line.empty() )
        return 0;
      
      if( line.find_first_not_of("+-.0123456789") != string::npos )
        throw runtime_error( "Invalid char" );
      
      if( !(stringstream(line) >> val) )
        throw runtime_error( "Failed to convert '" + line + "' into number" );
      
      return 1;
    };//get_next_number lambda
    
    int rval;
    float realtime, livetime, dummy;
    
    while( (rval = get_next_number(livetime)) != 1 )
    {
      if( rval <= -1 )
        throw runtime_error( "unexpected end of file" );
    }
    
    while( (rval = get_next_number(realtime)) != 1 )
    {
      if( rval <= -1 )
        throw runtime_error( "unexpected end of file" );
    }
    
    if( livetime > (realtime+FLT_EPSILON) || livetime<0.0f || realtime<0.0f || livetime>2592000.0f || realtime>2592000.0f )
      throw runtime_error( "Livetime or realtime invalid" );
    
    double countssum = 0.0;
    auto channel_counts = make_shared<vector<float>>();
    while( (rval = get_next_number(dummy)) >= 0 )
    {
      if( rval == 1 )
      {
        countssum += dummy;
        channel_counts->push_back( dummy );
      }
    }
    
    if( channel_counts->size() < 16 )
      throw runtime_error( "Not enough counts" );
    
    auto meas = make_shared<Measurement>();
    
    meas->real_time_ = realtime;
    meas->live_time_ = livetime;
    meas->gamma_count_sum_ = countssum;
    meas->gamma_counts_ = channel_counts;
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
    
    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }//try / catch
  
  
  return false;
}//bool load_from_tka( std::istream &input );

}//namespace SpecUtils





