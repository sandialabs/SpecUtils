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
#include <fstream>
#include <numeric>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "rapidxml/rapidxml.hpp"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;


namespace SpecUtils
{
  
bool SpecFile::load_multiact_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_multiact( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_multiact_file( const std::string &filename );

  
bool SpecFile::load_from_multiact( std::istream &input )
{
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  
  try
  {
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( orig_pos, ios::beg );
    const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );
    if( filesize > 512*1024 )  //The files I've seen are a few kilobytes
      throw runtime_error( "File to large to be MultiAct" );
    
    if( filesize < (128 + 24 + 48) )
      throw runtime_error( "File to small to be MultiAct" );
    
    string start = "                ";
    
    if( !input.read(&start[0], 8) )
      throw runtime_error( "Failed to read header" );
    
    if( !SpecUtils::istarts_with( start, "MultiAct") )
      throw runtime_error( "File must start with word 'MultiAct'" );
    
    double countssum = 0.0;
    auto channel_counts = make_shared<vector<float>>();
    
    vector<char> data;
    data.resize( filesize - 8, '\0' );
    input.read(&data.front(), static_cast<streamsize>(filesize-8) );
    
    //103: potentially channel counts (int of some sort)
    //107: real time in seconds (int of some sort)
    //111: real time in seconds (int of some sort)
    //115: live time in seconds (int of some sort)
    
    uint32_t numchannels, realtime, livetime;
    memcpy( &numchannels, (&(data[103])), 4 );
    memcpy( &realtime, (&(data[107])), 4 );
    memcpy( &livetime, (&(data[115])), 4 );
    
    if( realtime < livetime || livetime > 3600*24*5 )
    {
#if(PERFORM_DEVELOPER_CHECKS)
      log_developer_error( __func__, ("Got real time (" + std::to_string(realtime)
                                      + ") less than (" + std::to_string(livetime) + ") livetime").c_str() );
#endif
      throw runtime_error( "Invalid live/real time values" );
    }
    
    for( size_t i = 128; i < (data.size()-21); i += 3 )
    {
      //ToDo: make sure channel counts are reasonable...
      uint32_t threebyte = 0;
      memcpy( &threebyte, (&(data[i])), 3 );
      channel_counts->push_back( static_cast<float>(threebyte) );
      countssum += threebyte;
    }
    
    if( channel_counts->size() < 16 )
      throw runtime_error( "Not enough channels" );
    
    auto meas = make_shared<Measurement>();
    
    meas->real_time_ = static_cast<float>( realtime );
    meas->live_time_ = static_cast<float>( livetime );
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
}//bool load_from_multiact( std::istream &input );

}//namespace SpecUtils


