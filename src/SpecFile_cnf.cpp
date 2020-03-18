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

#include <cmath>
#include <vector>
#include <memory>
#include <string>
#include <cctype>
#include <limits>
#include <numeric>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <functional>


#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

namespace
{
}//namespace


namespace SpecUtils
{
bool SpecFile::load_cnf_file( const std::string &filename )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  reset();
  
#ifdef _WIN32
  ifstream file( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream file( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  if( !file.is_open() )
    return false;
  const bool loaded = load_from_cnf( file );
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_cnf_file( const std::string &filename );
  
  
bool SpecFile::load_from_cnf( std::istream &input )
{
  //Function to find a specific block (e.g., 512kb) of information in a CNF file.
  auto findCnfBlock = []( const uint8_t B, const size_t SBeg, size_t &pos,
                         std::istream &input, const size_t streamsize ) -> bool {
    for( pos = SBeg; (pos+512) < streamsize; pos += 512 )
    {
      input.seekg( pos, std::ios::beg );
      uint8_t bytes[2];
      input.read( (char *)bytes, 2 );
      if( (bytes[0] == B) && (bytes[1] == 0x20) )
        return true;
    }
    return false;
  };//findCnfBlock(...)
  
  
  //Function to read a 32bit float (i.e., energy calibration coefficient) from a
  //  CNF file
  auto readCnfFloat = []( std::istream &input ) -> float {
    float val;
    uint8_t Buf[4], TmpBuf[4];
    input.read( (char *)Buf, 4 );
    TmpBuf[0] = Buf[2];
    TmpBuf[1] = Buf[3];
    TmpBuf[2] = Buf[0];
    TmpBuf[3] = Buf[1];
    
    memcpy( &val, TmpBuf, sizeof(val) );
    
    return 0.25f*val;
  };//float readCnfFloat( std::istream &input )
  
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  reset();
  
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  
  try
  {
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( 0, ios::beg );
    
    const size_t size = static_cast<size_t>( 0 + eof_pos - orig_pos );
    
    char buff[65];
    size_t SBeg = 0, SPos = 0;
    auto meas = std::make_shared<Measurement>();
    
    if( findCnfBlock(0x1, SBeg, SPos, input, size) )
    {
      input.seekg( SPos + 32 + 16, std::ios::beg );
      input.read( buff, 64 );
      string title( buff, buff+64 );
      input.read( buff, 16 );
      string sampleid( buff, buff+16 );
      trim( title );
      trim( sampleid );
      
      meas->title_ = title;
      
      if( sampleid.size() )
        meas->remarks_.push_back( "Sample ID: " + sampleid );
    }//if( findCnfBlock(0x1, SBeg, SPos, input, size) )
    
    if( !findCnfBlock(0x0, SBeg, SPos, input, size) )
      throw runtime_error( "Couldnt find record data" );
    
    uint16_t w34, w36;
    input.seekg( SPos+34, std::ios::beg );
    read_binary_data( input, w34 );
    read_binary_data( input, w36 );
    
    const size_t record_offset = SPos + w36 + 48 + 1;
    const size_t num_channel_offset = SPos + 48 + 137;
    const size_t energy_calib_offset = SPos + w34 + 48 + 68;
    const size_t mca_offset = SPos+w34+48+156;
    const size_t instrument_offset = SPos+w34+48+1;
    const size_t generic_detector_offset = SPos+w34+48+732;
    const size_t specific_detector_offset = SPos+w34+48+26;
    const size_t serial_num_offset = SPos+w34+48+940;
    
    if( (record_offset+24) > size
       || (energy_calib_offset+12) > size
       || (num_channel_offset+4) > size
       || (mca_offset+8) > size
       || (instrument_offset+31) > size
       || (generic_detector_offset+8) > size
       || (specific_detector_offset+16) > size
       || (serial_num_offset+12) > size
       )
    {
      throw runtime_error( "Invalid record offset" );
    }
    
    uint32_t I, J;
    input.seekg( record_offset, std::ios::beg );
    read_binary_data( input, I );
    read_binary_data( input, J );
    const double seconds = J*429.4967296 + I/1.0E7;
    
    //The Date Time offset is empiraccally found, and only tested with a handful
    //  of files.  Nov 17 1858 is the commonly used "Modified Julian Date"
    meas->start_time_ = boost::posix_time::ptime( boost::gregorian::date(1858,boost::gregorian::Nov,17),
                                                 boost::posix_time::time_duration(0,0,0) );
    
    //Make a feeble attempt to avoid overflow - which was happening on 32 bit
    //  builds at first
    const int64_t days = static_cast<int64_t>( seconds / (24.0*60.0*60.0) );
    const int64_t remainder_secs = static_cast<int64_t>( seconds - days*24.0*60.0*60.0 );
    const int64_t remainder_ms = static_cast<int64_t>( (seconds - (days*24.0*60.0*60.0) - remainder_secs)*1.0E3 );
    
    try
    {
      meas->start_time_ += boost::posix_time::hours( days*24 );
      meas->start_time_ += boost::posix_time::seconds( remainder_secs );
      meas->start_time_ += boost::posix_time::milliseconds( remainder_ms );
    }catch(...)
    {
      meas->start_time_ = boost::posix_time::ptime();
    }
    
    read_binary_data( input, I );
    read_binary_data( input, J );
    I = 0xFFFFFFFF - I;
    J = 0xFFFFFFFF - J;
    meas->real_time_ = static_cast<float>(J*429.4967296 + I/1.0E7);
    
    read_binary_data( input, I );
    read_binary_data( input, J );
    I = 0xFFFFFFFF - I;
    J = 0xFFFFFFFF - J;
    meas->live_time_ = static_cast<float>(J*429.4967296 + I/1.0E7);
    
    uint32_t num_channels;
    input.seekg( num_channel_offset, std::ios::beg );
    read_binary_data( input, num_channels );
    
    const bool isPowerOfTwo = ((num_channels != 0) && !(num_channels & (num_channels - 1)));
    if( !isPowerOfTwo && (num_channels>=64 && num_channels<=65536) )
      throw runtime_error( "Invalid number of channels" );
    
    vector<float> calib_params(3);
    input.seekg( energy_calib_offset, std::ios::beg );
    
    meas->calibration_coeffs_.resize( 3 );
    meas->calibration_coeffs_[0] = readCnfFloat( input );
    meas->calibration_coeffs_[1] = readCnfFloat( input );
    meas->calibration_coeffs_[2] = readCnfFloat( input );
    meas->energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
    
    
    std::vector< std::pair<float,float> > dummy_devpairs;
    const bool validCalib
    = SpecUtils::calibration_is_valid( SpecUtils::EnergyCalType::Polynomial,
                                      meas->calibration_coeffs_,
                                      dummy_devpairs, num_channels );
    if( !validCalib )
    {
      bool allZeros = true;
      for( const float v : meas->calibration_coeffs_ )
        allZeros = allZeros && (v==0.0f);
      
      if( !allZeros )
        throw runtime_error( "Calibration parameters were invalid" );
      
      meas->calibration_coeffs_.clear();
      meas->energy_calibration_model_ = SpecUtils::EnergyCalType::InvalidEquationType;
    }//if( !validCalib )
    
    input.seekg( mca_offset, std::ios::beg );
    input.read( buff, 8 );
    
    string mca_type( buff, buff+8 );
    trim( mca_type );
    if( mca_type.size() )
      remarks_.push_back( "MCA Type: " + mca_type );
    
    input.seekg( instrument_offset, std::ios::beg );
    input.read( buff, 31 );
    string instrument_name( buff, buff+31 );
    trim( instrument_name );
    if( instrument_name.size() )
      meas->detector_name_ = instrument_name;
    //      remarks_.push_back( "Instrument Name: " + instrument_name );
    
    input.seekg( generic_detector_offset, std::ios::beg );
    input.read( buff, 8 );
    string generic_detector( buff, buff+8 );
    trim( generic_detector );
    
    if( mca_type == "I2K" && generic_detector == "Ge" )
    {
      //This assumption is based on inspecting files from a only two
      //  Falcon 5000 detectors
      //  (also instrument_name=="Instrument Name")
      detector_type_ = DetectorType::Falcon5000;
      instrument_type_ = "Spectrometer";
      manufacturer_ = "Canberra";
      instrument_model_ = "Falcon 5000";
    }//if we think this is a Falcon 5000
    
    //    input.seekg( specific_detector_offset, std::ios::beg );
    //    input.read( buff, 16 );
    //    string specific_detector( buff, buff+16 );
    //    trim( specific_detector );
    //    cerr << "specific_detector=" << specific_detector << endl;
    
    //Serial number appeares to be all zeros
    //    input.seekg( serial_num_offset, std::ios::beg );
    //    input.read( buff, 12 );
    //    string serial_num( buff, buff+12 );
    //    trim( serial_num );
    //    cerr << "serial_num=" << serial_num << endl;
    //    for( size_t i = 0; i < serial_num.size(); ++i )
    //       cerr << i << "\t" << serial_num[i] << "\t" << int( *(uint8_t *)&(serial_num[i]) ) << endl;
    
    
    SBeg = 0;
    if( !findCnfBlock(0x5, SBeg, SPos, input, size) )
      throw runtime_error( "Couldnt locate channel data portion of file" );
    
    vector<uint32_t> channeldata( num_channels );
    
    SBeg = SPos + 512;
    if( findCnfBlock(0x5, SBeg, SPos, input, size) )
      input.seekg( SPos+512, std::ios::beg );
    else
      input.seekg( SBeg, std::ios::beg );
    
    if( (SBeg+4*num_channels) > size )
      throw runtime_error( "Invalid file size" );
    input.read( (char *)&channeldata[0], 4*num_channels );
    
    //Get rid of live time and real time that may be in the first two channels
    channeldata[0] = channeldata[1] = 0;
    
    vector<float> *channel_data = new vector<float>(num_channels);
    meas->gamma_counts_.reset( channel_data );
    for( size_t i = 0; i < num_channels; ++i )
    {
      const float val = static_cast<float>( channeldata[i] );
      meas->gamma_count_sum_ += val;
      (*channel_data)[i] = val;
    }//set gamma channel data
    
    //    manufacturer_;
    //    instrument_model_;
    //    instrument_id_;
    //    manufacturer_;
    
    
    measurements_.push_back( meas );
  }catch ( std::exception & )
  {
    input.clear();
    input.seekg( orig_pos, ios::beg );
    
    reset();
    return false;
  }//try / catch to read the file
  
  cleanup_after_load();
  
  return true;
}//bool load_from_cnf( std::istream &input )

}//namespace SpecUtils



