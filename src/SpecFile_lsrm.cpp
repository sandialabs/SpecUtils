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
#include <cstring>
#include <iostream>
#include <stdexcept>


#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

namespace
{
  bool toFloat( const std::string &str, float &f )
  {
    //ToDO: should probably use SpecUtils::parse_float(...) for consistency/speed
    const int nconvert = sscanf( str.c_str(), "%f", &f );
    return (nconvert == 1);
  }
}//namespace


namespace SpecUtils
{
bool SpecFile::load_lsrm_spe_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_lsrm_spe( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_lsrm_spe_file( const std::string &filename );

  
bool SpecFile::load_from_lsrm_spe( std::istream &input )
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
    if( filesize > 512*1024 )
      throw runtime_error( "File to large to be LSRM SPE" );
    
    const size_t initial_read = std::min( filesize, size_t(2048) );
    string data( initial_read, '\0' );
    input.read( &(data[0]), initial_read );
    
    const size_t spec_tag_pos = data.find("SPECTR=");
    if( spec_tag_pos == string::npos )
      throw runtime_error( "Couldnt find SPECTR" );
    
    const size_t spec_start_pos = spec_tag_pos + 7;
    const size_t nchannel = (filesize - spec_start_pos) / 4;
    if( nchannel < 128 )
      throw runtime_error( "Not enough channels" );
    
    if( nchannel > 68000 )
      throw runtime_error( "To many channels" );
    
    //We could have the next test, but lets be loose right now.
    //if( ((filesize - spec_start_pos) % 4) != 0 )
    //  throw runtime_error( "Spec size not multiple of 4" );
    
    auto getval = [&data]( const string &tag ) -> string {
      const size_t pos = data.find( tag );
      if( pos == string::npos )
        return "";
      
      const size_t value_start = pos + tag.size();
      const size_t endline = data.find_first_of( "\r\n", value_start );
      if( endline == string::npos )
        return "";
      
      const string value = data.substr( pos+tag.size(), endline - value_start );
      return SpecUtils::trim_copy( value );
    };//getval
    
    auto meas = make_shared<Measurement>();
    
    string startdate = getval( "MEASBEGIN=" );
    if( startdate.empty() )
    {
      startdate = getval( "DATE=" );
      startdate += " " + getval( "TIME=" );
    }
    
    meas->start_time_ = SpecUtils::time_from_string( startdate.c_str() );
    
    if( !toFloat( getval("TLIVE="), meas->live_time_ ) )
      meas->live_time_ = 0.0f;
    
    if( !toFloat( getval("TREAL="), meas->real_time_ ) )
      meas->live_time_ = 0.0f;
    
    instrument_id_ = getval( "DETECTOR=" );
    
    const string energy = getval( "ENERGY=" );
    if( SpecUtils::split_to_floats( energy, meas->calibration_coeffs_ ) )
      meas->energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
    
    const string comment = getval( "COMMENT=" );
    if( !comment.empty() )
      remarks_.push_back( comment );
    
    const string fwhm = getval( "FWHM=" );
    if( !fwhm.empty() )
      remarks_.push_back( "FWHM=" + fwhm );
    
    //Other things we could look for:
    //"SHIFR=", "NOMER=", "CONFIGNAME=", "PREPBEGIN=", "PREPEND=", "OPERATOR=",
    //"GEOMETRY=", "SETTYPE=", "CONTTYPE=", "MATERIAL=", "DISTANCE=", "VOLUME="
    //"WEIGHT=", "R_I_D=", "FILE_SPE="
    
    if( initial_read < filesize )
    {
      data.resize( filesize, '\0' );
      input.read( &(data[initial_read]), filesize-initial_read );
    }
    
    vector<int32_t> spectrumint( nchannel, 0 );
    memcpy( &(spectrumint[0]), &(data[spec_start_pos]), 4*nchannel );
    
    meas->gamma_count_sum_ = 0.0f;
    auto channel_counts = make_shared<vector<float>>(nchannel);
    for( size_t i = 0; i < nchannel; ++i )
    {
      (*channel_counts)[i] = static_cast<float>( spectrumint[i] );
      meas->gamma_count_sum_ += spectrumint[i];
    }
    meas->gamma_counts_ = channel_counts;
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
    
    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }//try / catch to parse
  
  return false;
}//bool load_from_lsrm_spe( std::istream &input );

}//namespace SpecUtils





