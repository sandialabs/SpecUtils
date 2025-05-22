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

#include <array>
#include <cmath>
#include <cctype>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <functional>

#include "3rdparty/date/include/date/date.h"

#include "SpecUtils/DateTime.h"
#include "SpecUtils/SpecFile.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"
#include "SpecUtils/CAMIO.h"

using namespace std;

//namespace
//{
//
//    typedef unsigned char byte_type;
//
//    template< typename T > std::array< byte_type, sizeof(T) >  to_bytes(const T& object)
//    {
//        std::array< byte_type, sizeof(T) > bytes;
//
//        const byte_type* begin = reinterpret_cast<const byte_type*>(std::addressof(object));
//        const byte_type* end = begin + sizeof(T);
//        std::copy(begin, end, std::begin(bytes));
//
//        return bytes;
//    }
//
///** Datatypes for the CAM format.
// */
//    enum class cam_type
//    {
//        cam_float,     //any float
//        cam_double,    //any double
//        cam_byte,      //a byte
//        cam_word,      //int16
//        cam_longword,  //int
//        cam_quadword,  //int64
//        cam_datetime ,   //date time
//        cam_duration,   //time duration 
//        cam_string,
//    };
//
//    //Convert data to CAM data formats
//
//    template <class T>
//    //IEEE-754 variables to CAM float (PDP-11)
//    std::array< byte_type, sizeof(int32_t) > convert_to_CAM_float(const T& input)
//    {
//
//        //pdp-11 is a wordswaped float/4
//        float temp_f = static_cast<float>(input * 4);
//        const auto temp = to_bytes(temp_f);
//        const size_t word_size = 2;
//        std::array< byte_type, sizeof(int32_t) > output = { 0x00 };
//        //perform a word swap
//        for (size_t i = 0; i < word_size; i++)
//        {
//            output[i] = temp[i + word_size];
//            output[i + word_size] = temp[i];
//        }
//        return output;
//    }
//
//    template <class T>
//    //IEEE variables to CAM double (PDP-11)
//    std::array< byte_type, sizeof(int64_t) > convert_to_CAM_double(const T& input)
//    {
//
//        //pdp-11 is a word swaped Double/4
//        double temp_d = static_cast<double>(input * 4.0) ;
//        const auto temp = to_bytes(temp_d);
//        const size_t word_size = 2;
//        std::array< byte_type, sizeof(int64_t) > output = { 0x00 };
//        //perform a word swap
//        for (size_t i = 0; i < word_size; i++)
//        {
//            output[i + 3 * word_size] = temp[i];				//IEEE fourth is PDP-11 first
//            output[i + 2 * word_size] = temp[i + word_size];  //IEEE third is PDP-11 second
//            output[i + word_size] = temp[i + 2 * word_size];//IEEE second is PDP-11 third
//            output[i] = temp[i + 3 * word_size];            //IEEE first is PDP-11 fouth
//        }
//        return output;
//    }
//
//    //time_point to CAM DateTime
//    std::array< byte_type, sizeof(int64_t) > convert_to_CAM_datetime(const SpecUtils::time_point_t& date_time)
//    {
//        //error checking
//        if( SpecUtils::is_special(date_time) )
//            throw std::range_error("The input date time is not a valid date time");
//
//        std::array< byte_type, sizeof(int64_t) > bytes = { 0x00 };
//        //get the total seconds between the input time and the epoch
//        const date::year_month_day epoch( date::year(1970), date::month(1u), date::day(1u) );
//        const date::sys_days epoch_days = epoch;
//        assert( epoch_days.time_since_epoch().count() == 0 ); //true if using unix epoch, lets see on the various systems
//      
//        const auto time_from_epoch = date::floor<std::chrono::seconds>(date_time - epoch_days);
//        const int64_t sec_from_epoch = time_from_epoch.count();
//
//        //covert to modified julian in usec
//        uint64_t j_sec = (sec_from_epoch + 3506716800UL) * 10000000UL;
//        bytes = to_bytes(j_sec);
//        return bytes;
//    }
//
//    //float sec to CAM duration
//    std::array< byte_type, sizeof(int64_t) > convert_to_CAM_duration(const float& duration)
//    {
//        std::array< byte_type, sizeof(int64_t) > bytes = { 0x00 };
//        //duration in usec is larger than a int64: covert to years
//        if ( (static_cast<double>(duration) * 10000000.0) > static_cast<double>(INT64_MAX) )
//        {
//            double t_duration = duration / 31557600;
//            //duration in years is larger than an int32, divide by a million years
//            if ( (duration / 31557600.0) > static_cast<double>(INT32_MAX) )
//            {
//                int32_t y_duration = SpecUtils::float_to_integral<int32_t>(t_duration / 1e6);
//                const auto y_bytes = to_bytes(y_duration);
//                std::copy(begin(y_bytes), end(y_bytes), begin(bytes));
//                //set the flags
//                bytes[7] = 0x80;
//                bytes[4] = 0x01;
//                return bytes;
//
//            }
//            //duration can be represented in years
//            else
//            {
//                int32_t y_duration = static_cast<int32_t>(t_duration);
//                const auto y_bytes = to_bytes(y_duration);
//                std::copy(begin(y_bytes), end(y_bytes), begin(bytes));
//                //set the flag
//                bytes[7] = 0x80;
//                return bytes;
//            }
//        }
//        //duration is able to be represented in usec
//        else
//        {
//            //cam time span is in usec and a negatve int64
//            int64_t t_duration = static_cast<int64_t>((double)duration * -10000000);
//            bytes = to_bytes(t_duration);
//            return bytes;
//        }
//
//    }
//
//    // CAM double to double
//    double convert_from_CAM_double(const std::vector<uint8_t>& data, size_t pos)
//    {
//        const size_t word_size = 2;
//        std::array<uint8_t, sizeof(double_t)> temp = { 0x00 };
//        //std::memcpy(&temp, &data[pos], sizeof(double_t));
//
//        // Perform word swap
//        for (size_t i = 0; i < word_size; i++)
//        {
//            size_t j = i + pos;
//            temp[i] = data[j + 3 * word_size];             // PDP-11 fourth is IEEE first
//            temp[i + word_size] = data[j + 2 * word_size]; // PDP-11 third is IEEE second
//            temp[i + 2 * word_size] = data[j + word_size]; // PDP-11 second is IEEE third
//            temp[i + 3 * word_size] = data[j];             // PDP-11 first is IEEE forth
//        }
//
//        // Convert bytes back to double
//        double temp_d;
//        std::memcpy(&temp_d, temp.data(), sizeof(double_t)); // Safely copy the bytes into a double
//
//        // scale to the double value
//        return temp_d / 4.0;
//    }
//
//    // CAM float to float
//    float convert_from_CAM_float(const std::vector<uint8_t>& data, size_t pos) {
//        if (data.size() < 4) {
//            throw std::invalid_argument("The data input array is not long enough");
//        }
//        if (data.size() < pos + 4) {
//            throw std::out_of_range("The provided index exceeds the length of the array");
//        }
//        uint8_t word1[2], word2[2];
//
//        std::memcpy(word1, &data[pos + 0x2], sizeof(word1));
//        std::memcpy(word2, &data[pos], sizeof(word2));
//
//        uint8_t bytearr[4];
//        // Copy the words from the data array
//        // Assuming the input data is in a format that needs to be swapped
//        std::memcpy(bytearr, word1, sizeof(word1)); // Copy word1 to the beginning
//        std::memcpy(bytearr + 2, word2, sizeof(word2)); // Copy word2 to the end
//
//        float val = *reinterpret_cast<float*>(bytearr);
//
//        return val / 4;
//    }
//    
//    //CAM DateTime to time_point
//    SpecUtils::time_point_t convert_from_CAM_datetime( uint64_t time_raw )
//    {
//      if( !time_raw )
//        return SpecUtils::time_point_t{};
//      
//      const date::sys_days epoch_days = date::year_month_day( date::year(1970), date::month(1u), date::day(1u) );
//      SpecUtils::time_point_t answer{epoch_days};
//
//      const int64_t secs = time_raw / 10000000L;
//      const int64_t sec_from_epoch = secs - 3506716800L;
//      
//      answer += std::chrono::seconds(sec_from_epoch);
//      answer += std::chrono::microseconds( secs % 10000000L );
//      
//      return answer;
//    }//convert_from_CAM_datetime(...)
//
//
//    //enter the input to the cam desition vector of bytes at the location, with a given datatype
//    template<typename T, typename = typename std::enable_if<std::is_arithmetic<T>::value, T>::type>
//    void enter_CAM_value(const T& input, vector<byte_type>& destination, const size_t& location, const cam_type& type)
//    {
//        switch (type) {
//        case cam_type::cam_float:
//        {
//            const auto bytes = convert_to_CAM_float(input);
//          
//          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
//            throw std::runtime_error( "enter_CAM_value(cam_float) invalid write location" );
//          
//            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
//        }
//        break;
//        case cam_type::cam_double:
//        {
//            const auto bytes = convert_to_CAM_double(input);
//          
//          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
//            throw std::runtime_error( "enter_CAM_value(cam_double) invalid write location" );
//          
//            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
//        }
//        break;
//        case cam_type::cam_duration:
//        {
//            const auto bytes = convert_to_CAM_duration(input);
//          
//          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
//            throw std::runtime_error( "enter_CAM_value(cam_duration) invalid write location" );
//          
//            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
//        }
//        break;
//        case cam_type::cam_quadword:
//        {
//            int64_t t_quadword = static_cast<int64_t>(input);
//            const auto bytes = to_bytes(t_quadword);
//          
//          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
//            throw std::runtime_error( "enter_CAM_value(cam_quadword) invalid write location" );
//          
//            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
//        }
//        break;
//        case cam_type::cam_longword:
//        {
//          // TODO: it appears we actually want to use a uint32_t here, and not a int32_t, but because of the static_cast here, things work out, but if we are to "clamp" values, then we need to switch to using unsigned integers
//          int32_t t_longword = static_cast<int32_t>(input);
//          
//            const auto bytes = to_bytes(t_longword);
//          
//          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
//            throw std::runtime_error( "enter_CAM_value(cam_longword) invalid write location" );
//          
//            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
//        }
//        break;
//        case cam_type::cam_word:
//        {
//            int16_t t_word = static_cast<int16_t>(input);
//            const auto bytes = to_bytes(t_word);
//          
//          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
//            throw std::runtime_error( "enter_CAM_value(cam_word) invalid write location" );
//          
//            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
//        }
//        break;
//        case cam_type::cam_byte:
//        {
//          byte_type t_byte = static_cast<byte_type>(input);
//            destination.at(location) = t_byte;
//            //const byte_type* begin = reinterpret_cast<const byte_type*>(std::addressof(t_byte));
//            //const byte_type* end = begin + sizeof(byte_type);
//            //std::copy(begin, end, destination.begin() + location);
//        break;
//        }
//        default:
//            string message = "error - Invalid converstion from: ";
//            message.append(typeid(T).name());
//            message.append(" to athermetic type");
//
//            throw std::invalid_argument(message);
//            break;
//        }//end switch
//    }
//    //enter the input to the cam desition vector of bytes at the location, with a given datatype
//    void enter_CAM_value(const SpecUtils::time_point_t& input, vector<byte_type>& destination, const size_t& location, const cam_type& type=cam_type::cam_datetime)
//    {
//        if (type != cam_type::cam_datetime)
//        {
//            throw std::invalid_argument("error - Invalid conversion from time_point");
//        }
//
//        const auto bytes = convert_to_CAM_datetime(input);
//      
//      if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
//        throw std::runtime_error( "enter_CAM_value(ptime) invalid write location" );
//       
//      std::copy(begin(bytes), end(bytes) , destination.begin() + location);
//    }
//    //enter the input to the cam desition vector of bytes at the location, with a given datatype
//    void enter_CAM_value(const string& input, vector<byte_type>& destination, const size_t& location, const cam_type& type=cam_type::cam_string)
//    {
//        if (type != cam_type::cam_string)
//        {
//            throw std::invalid_argument("error - Invalid converstion from: char*[]");
//        }
//      
//      if( (std::begin(destination) + location + (std::end(input) - std::begin(input))) > std::end(destination) )
//        throw std::runtime_error( "enter_CAM_value(string) invalid write location" );
//      
//        std::copy(input.begin(), input.end(), destination.begin() + location);
//    }
//
//}//namespace
//

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
  //auto findCnfSegment = []( const uint8_t B, const size_t SBeg, size_t &pos,
  //                       std::istream &input, const size_t streamsize ) -> bool {
  //  for( pos = SBeg; (pos+512) < streamsize; pos += 512 )
  //  {
  //    input.seekg( pos, std::ios::beg );
  //    uint8_t bytes[2];
  //    input.read( (char *)bytes, 2 );
  //    if( (bytes[0] == B) && (bytes[1] == 0x20) )
  //      return true;
  //  }
  //  return false;
  //};//findCnfSegment(...)
  
  
  //Function to read a 32bit float (i.e., energy calibration coefficient) from a
  //  CNF file in PDP-11 format to IEEE
  //auto readCnfFloat = []( std::istream &input ) -> float {
  //  float val;
  //  uint8_t Buf[4], TmpBuf[4];
  //  input.read( (char *)Buf, 4 );
  //  TmpBuf[0] = Buf[2];
  //  TmpBuf[1] = Buf[3];
  //  TmpBuf[2] = Buf[0];
  //  TmpBuf[3] = Buf[1];
  //  
  //  memcpy( &val, TmpBuf, sizeof(val) );
  //  
  //  return 0.25f*val;
  //};//float readCnfFloat( std::istream &input )
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  reset();

  if( !input.good() )
    return false;

  // read the file
  try
  {
    const istream::pos_type orig_pos = input.tellg();
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( 0, ios::beg );
    
    const size_t size = static_cast<size_t>( 0 + eof_pos - orig_pos );
    std::vector<char> file_bits(size);
    input.read(reinterpret_cast<char*>(file_bits.data()), size);

    auto cam = std::make_shared<CAMInputOutput::CAMIO>();
    cam->ReadFile(reinterpret_cast<const std::vector<byte_type>&>(file_bits));
    //if( size < 512*4 )
    //  throw runtime_error( "Too small to be CNF" );
    
    //input.seekg( 0, ios::beg );
    
    // A buffer we will use to read textual information from the file into.
    //char buff[65];
    
    // Currently (20231025) we locate information by looking for section IDs; however
    //  we could use the listed offsets in the file header.  For development purposes,
    //  we'll grab these listed offsets, and use some asserts to check they match what
    //  we would otherwise get; we may upgrade things in the future.
    //uint32_t pars_offset = 0, sample_info_offset = 0;
    //uint32_t eff_offset = 0, channel_data_offset = 0, nuc_id_offset = 0;
    
    // Instead of starting at 0 + 112, we could look for section ID 0x00, but this looks
    //  to always be the start of the file
    //for( size_t listing_pos = 112; listing_pos < 512; listing_pos += 48 )
    //{
    //  if( !input.seekg( listing_pos, std::ios::beg ) )
    //    throw runtime_error( "Error seeking" );
    //  
    //  uint32_t header_val;
    //  read_binary_data( input, header_val );
    //  
    //  input.read( buff, 6 );// Advance 6 bytes
    //  
    //  uint32_t offset_val;
    //  read_binary_data( input, offset_val );
    //  
    //  if( header_val == 0x0 )
    //    break;
    //  
    //  switch( header_val )
    //  {
    //    case 0x00012000:  // Times, energy cal, number of channels
    //      pars_offset = offset_val;
    //      break;
    //      
    //    case 0x12001:     // Title and sample ID
    //      sample_info_offset = offset_val;
    //      break;
    //      
    //    case 0x00012002:  // Efficiencies
    //      eff_offset = offset_val;
    //      break;
    //      
    //    case 0x00012005: // Channel data
    //      channel_data_offset = offset_val;
    //      break;
    //    
    //    case 0x00012013: // Energy cal
    //    case 0x0001200D:
    //      // Havent explored at all yet
    //      break;
    //      
    //    case 0x0001202C: //Nuclide ID
    //      nuc_id_offset = offset_val;
    //      break;
    //      
    //    case 0x12010:
    //    case 0x12003:
    //    case 0x12004:
    //    default:
    //      //cout << "Found unidentified header type "
    //      //     << hex << header_val << " at offset " << hex << offset_val << endl;
    //      break;
    //  }//switch( header_val )
    //}//switch( header_val )
    //
    //// We will use "segment_search_start" to mark _where_ to start searching for a particular
    ////  segment from.  We will pass this value to the lambda findCnfSegment(...) defined above to
    ////  tell it where to start looking.
    //size_t segment_search_start = 0;
    //
    //// We will use "segment_position" to mark start of the segment we are currently extracting
    ////  information from; this value will be set from the lambda findCnfSegment(...) defined above.
    //size_t segment_position = 0;
    
    // The Measurement we will place all information into.
    auto meas = std::make_shared<Measurement>();
    
    string sampleid= cam->GetSampleTitle();
    meas->title_ = sampleid;
    if( sampleid.size() )
        meas->remarks_.push_back( "Sample ID: " + sampleid );

 /*   if( findCnfSegment(0x1, segment_search_start, segment_position, input, size) )
    {*/
    //  assert( !sample_info_offset || (sample_info_offset == segment_position) );
    //  
    //  input.seekg( segment_position + 32 + 16, std::ios::beg );
    //  input.read( buff, 64 );
    //  string title( buff, buff+64 );
    //  input.read( buff, 16 );
    //  string sampleid( buff, buff+16 );
    //  trim( title );
    //  trim( sampleid );
    //  

    //  
    //}//if( findCnfSegment(0x1, segment_search_start, segment_position, input, size) )
    //
    //if( !findCnfSegment(0x0, segment_search_start, segment_position, input, size) )
    //  throw runtime_error( "Couldnt find record data" );
    //
    //// segment_position will be a multiple of 512, ex 2048
    //assert( !pars_offset || (pars_offset == segment_position) );
    //
    //uint16_t w34, w36;
    //if( !input.seekg( segment_position+34, std::ios::beg ) )
    //  throw runtime_error( "Failed seek to record offsets" );
    //
    //read_binary_data( input, w34 );
    //read_binary_data( input, w36 );
    //
    //const size_t num_channel_offset       = segment_position + 48 + 137;
    //const size_t record_offset            = segment_position + w36 + 48 + 1;
    //const size_t energy_type_offset       = segment_position + 48 + 251;
    //const size_t energy_calib_offset      = segment_position + w34 + 48 + 68;
    //const size_t fwhm_calib_offset        = segment_position + w34 + 48 + 220;
    //const size_t mca_type_offset          = segment_position + w34 + 48 + 156;
    //const size_t instrument_offset        = segment_position + w34 + 48 + 1;
    //const size_t generic_detector_offset  = segment_position + w34 + 48 + 732;
    //const size_t specific_detector_offset = segment_position + w34 + 48 + 26;
    //const size_t serial_num_offset        = segment_position + w34 + 48 + 940;
    //
    //
    //if( (record_offset+24) > size
    //   || (energy_calib_offset+12) > size
    //   || (num_channel_offset+4) > size
    //   || (mca_type_offset+8) > size
    //   || (instrument_offset+31) > size
    //   || (generic_detector_offset+8) > size
    //   || (specific_detector_offset+16) > size
    //   || (serial_num_offset+12) > size
    //   )
    //{
    //  throw runtime_error( "Invalid record offset" );
    //}
    
    //if( !input.seekg( record_offset, std::ios::beg ) )
    //  throw std::runtime_error( "Failed seek CNF record start" );
    //
    //uint64_t time_raw;
    //read_binary_data( input, time_raw );
    meas->start_time_ = cam->GetAquisitionTime();
    //meas->start_time_ = convert_from_CAM_datetime( time_raw );
    
    //uint32_t I, J;
    //read_binary_data( input, I );
    //read_binary_data( input, J );
    //I = 0xFFFFFFFF - I;
    //J = 0xFFFFFFFF - J;
    //meas->real_time_ = static_cast<float>(J*429.4967296 + I/1.0E7);
    meas->real_time_ = cam->GetRealTime();

    //read_binary_data( input, I );
    //read_binary_data( input, J );
    //I = 0xFFFFFFFF - I;
    //J = 0xFFFFFFFF - J;
    //meas->live_time_ = static_cast<float>(J*429.4967296 + I/1.0E7);
    meas->live_time_ = cam->GetLiveTime();

    //if( !input.seekg( num_channel_offset, std::ios::beg ) )
    //  throw std::runtime_error( "Failed seek for num channels" );
    
    //uint32_t num_channels;
    //read_binary_data( input, num_channels );
    
    //const bool isPowerOfTwo = ((num_channels != 0) && !(num_channels & (num_channels - 1)));
    //if( !isPowerOfTwo || (num_channels < 16) || (num_channels > (65536 + 8)) )
    //  throw runtime_error( "Invalid number of channels" );
    
    //vector<float> calib_params(3);
    //if( !input.seekg( energy_calib_offset, std::ios::beg ) )
    //  throw std::runtime_error( "Failed seek for energy calibration" );
    
    //vector<float> cal_coefs = { 0.0f, 0.0f, 0.0f, 0.0f };
    //cal_coefs[0] = readCnfFloat( input );
    //cal_coefs[1] = readCnfFloat( input );
    //cal_coefs[2] = readCnfFloat( input );
    //cal_coefs[3] = readCnfFloat( input );
    
    vector<float> cal_coefs = cam->GetEnergyCalibration();
    

    // TODO get Energy Cal units
    //input.read( buff, 8 );// Advance 8 bytes
    //input.read( buff, 3 );
    //string energy_cal_units( buff, buff+3 );
    //trim( energy_cal_units );
    //assert( energy_cal_units.empty() || (energy_cal_units == "keV") || (energy_cal_units == "MeV") ); // but doesnt appear to matter?
    
    //if( input.seekg(energy_type_offset, std::ios::beg) )
    //{
    //  input.read( buff, 8 );
    //  string cal_type( buff, buff+8 );
    //  trim( cal_type );
    //  assert( cal_type.empty() || (cal_type == "POLY") );
    //}//if( input.seekg( energy_type_offset, std::ios::beg ) )
    auto & spec = cam->GetSpectrum();
    size_t num_chnanels = spec.size();
    try
    {
      auto newcal = make_shared<EnergyCalibration>();
      newcal->set_polynomial( num_chnanels, cal_coefs, {} );
      meas->energy_calibration_ = newcal;
    }
    catch( std::exception & )
    {
      bool allZeros = true;
      for( const float v : cal_coefs )
        allZeros = allZeros && (v == 0.0f);
      
      // We could check if this is a alpha spectra or not...
      /*
      bool is_alpha_spec = false;
      if( !allZeros )
      {
        //From only a single file, Alpha spec files have: "Alpha Efcor", "Alpha Encal".  Segment 11, has just "Alpha"
        const uint8_t headers_with_alpha[] = { 2, 6, 11, 13, 19 };
        string buffer( 513, '\0' );
        for( uint8_t i : headers_with_alpha )
        {
          size_t segment_position = 0;
          if( findCnfSegment(i, 0, segment_position, input, size) )
          {
            input.seekg( segment_position, std::ios::beg );
            if( input.read( &(buffer[0]), 512 ) && (buffer.find("Alpha") != string::npos) )
            {
              is_alpha_spec = true;
              break;
            }//if( we found the segment, and it had "Alpha" in it )
          }//if( find segment )
        }//for( loop over potential segments that might have "Alpha" in them )
      }//if( !allZeros )
      */
      
      if( !allZeros )
        throw runtime_error( "Calibration parameters were invalid" );
    }//try /catch set calibration
    
    /*
    // It seems like we can read FWHM and Eff info okay-ish, but since we dont
    //  store anywhere, so we wont read in, atm.
     
    if( fwhm_calib_offset && input.seekg(fwhm_calib_offset, std::ios::beg) )
    {
      vector<float> fwhf_coefs( 4 );  //last two look to always be zero
      for( size_t i = 0; i < 4; ++i )
        fwhf_coefs[i] = readCnfFloat( input );
      // FWHM = fwhf_coefs[0] + fwhf_coefs[1]*sqrt(energy)
      // TODO: add to file somehow...
    }//if( input.seekg(fwhm_calib_offset, std::ios::beg) )
    
    if( eff_offset && input.seekg(eff_offset, std::ios::beg) )
    {
      input.seekg( eff_offset + 0x30 + 0x2A, std::ios::beg );
      input.read( buff, 16 );
      string geom_id( buff, buff + 16 ); //'temp'
      trim( geom_id );
      
      input.seekg( eff_offset + 0x30 + 0xCE, std::ios::beg );
      input.read( buff, 32 );
      string eff_type( buff, buff + 32 );// 'DUAL', 'INTERPOL'
      trim( eff_type );
      
      input.seekg( eff_offset + 0x30 + 0x19F, std::ios::beg );
      input.read( buff, 16 );
      string name( buff, buff + 16 ); //'Gamma Efcal v2.1'
      trim( name );
      
      size_t eff_par_offset = eff_offset + 48 + 250;
      input.seekg( eff_par_offset, std::ios::beg );
      
      vector<float> eff_coefs( 6, 0.0f );
      for( size_t i = 0; i < eff_coefs.size(); ++i )
        eff_coefs[i] = readCnfFloat( input );
     
      if( eff_type == "DUAL" )
      {
        cout << "ln(Eff) = ";
        for( size_t i = 0; i < eff_coefs.size(); ++i )
          cout << (i ? " + " : "") << eff_coefs[i] << "*ln(E)^" << i;
        cout << endl;
      }else if( eff_type == "INTERPOL" )
      {
        // I dont know how to interpret these - but I think we should look for peaks and use those values...
        cout << "INTERPOL Eff pars: ";
        for( size_t i = 0; i < eff_coefs.size(); ++i )
          cout << (i ? ", " : "") << eff_coefs[i];
        cout << endl;
      }else
      {
        //Empirical and Linear will probably get here, but I havent checked
        cout << "Eff pars (type '" << eff_type << "'): ";
        for( size_t i = 0; i < eff_coefs.size(); ++i )
          cout << (i ? ", " : "") << eff_coefs[i];
        cout << endl;
      }
    }//if( eff_offset )
    */
    
    input.seekg( mca_type_offset, std::ios::beg );
    input.read( buff, 8 );
    
    string mca_type( buff, buff+8 );
    trim( mca_type );
    if( mca_type.size() )
      remarks_.push_back( "MCA Type: " + mca_type );
    
    input.seekg( instrument_offset, std::ios::beg );
    input.read( buff, 31 );
    string instrument_name( buff, buff + 31 );
    trim( instrument_name );
    if( instrument_name.size() )
      meas->detector_name_ = instrument_name;
    
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
    
    //Serial number appears to be all zeros
    //    input.seekg( serial_num_offset, std::ios::beg );
    //    input.read( buff, 12 );
    //    string serial_num( buff, buff+12 );
    //    trim( serial_num );
    //    cerr << "serial_num=" << serial_num << endl;
    //    for( size_t i = 0; i < serial_num.size(); ++i )
    //       cerr << i << "\t" << serial_num[i] << "\t" << int( *(uint8_t *)&(serial_num[i]) ) << endl;
    
    
    //segment_search_start = 0;
    //if( !findCnfSegment(0x5, segment_search_start, segment_position, input, size) )
    //  throw runtime_error( "Couldnt locate channel data portion of file" );
    //
    //assert( !channel_data_offset || (channel_data_offset == segment_position) );
    //
    //segment_position += 512;
    // NOTE: Previous to 20210315 I looked for a second 0x5 segment, and if found, used it.
    //  However, none of the CNF files I have around need this, and at least one file will be
    //  messed up if this search is done.
    //if( findCnfSegment(0x5, segment_search_start+, segment_position, input, size) )
    //{
    //  if( (segment_position + 512 + 4*num_channels) <= size )
    //    segment_position += 512;
    //  else
    //   segment_position = segment_search_start;
    //}else
    //{
    //  segment_position = segment_search_start;
    //}
    
    
    //input.seekg( segment_position, std::ios::beg );
    
    //if( (segment_position + 4*num_channels) > size )
    //  throw runtime_error( "Invalid file size for num channels" );
    //
    //vector<uint32_t> channeldata( num_channels );
    //if( !input.read( (char *)&channeldata[0], 4*num_channels ) )
    //  throw std::runtime_error( "Error reading channel data" );
    
    //Get rid of live time and real time that may be in the first two channels
    //channeldata[0] = channeldata[1] = 0;

    auto channel_data = make_shared<vector<float>>(num_chnanels);
    for( size_t i = 0; i < num_chnanels; ++i )
    {
      
      const float val = static_cast<float>( spec[i] );
      meas->gamma_count_sum_ += val;
      spec[i] = val;
    }//set gamma channel data
    
    meas->gamma_counts_ = channel_data;
    
    //    manufacturer_;
    //    instrument_model_;
    //    instrument_id_;
    //    manufacturer_;
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
  }catch ( std::exception & )
  {
    input.clear();
    input.seekg( orig_pos, ios::beg );
    
    reset();
    return false;
  }//try / catch to read the file
  
  return true;
}//bool load_from_cnf( std::istream &input )


bool SpecFile::write_cnf( std::ostream &output, std::set<int> sample_nums,
                          const std::set<int> &det_nums ) const
{
  //First, lets take care of some boilerplate code.
  std::unique_lock<std::recursive_mutex> scoped_lock(mutex_);

  for( const auto sample : sample_nums )
  {
    if( !sample_numbers_.count(sample) )
      throw runtime_error( "write_cnf: invalid sample number (" + to_string(sample) + ")" );
  }
  
  if( sample_nums.empty() )
    sample_nums = sample_numbers_;
  
  vector<string> det_names;
  for( const int num : det_nums )
  {
    auto pos = std::find( begin(detector_numbers_), end(detector_numbers_), num );
    if( pos == end(detector_numbers_) )
      throw runtime_error( "write_cnf: invalid detector number (" + to_string(num) + ")" );
    det_names.push_back( detector_names_[pos-begin(detector_numbers_)] );
  }
  
  if( det_nums.empty() )
    det_names = detector_names_;
  
  try
  {
    std::shared_ptr<Measurement> summed = sum_measurements(sample_nums, det_names, nullptr);

    if( !summed || !summed->gamma_counts() || summed->gamma_counts()->empty() )
      return false;

    //call CAMIO here
    CAMInputOutput::CAMIO* cam = new CAMInputOutput::CAMIO();

    //At this point we have the one spectrum (called summed) that we will write
    //  to the CNF file.  If the input file only had a single spectrum, this is
    //  now held in 'summed', otherwise the specified samples and detectors have
    //  all been summed together/

    //Gamma information
    const float real_time = summed->real_time();
    const float live_time = summed->live_time();
    const vector<float> gamma_channel_counts = *summed->gamma_counts();

    //CNF files use polynomial energy calibration, so if the input didnt also
    //  use polynomial, we will convert to polynomial, or in the case of
    //  lower channel or invalid, just clear the coefficients.
    vector<float> energy_cal_coeffs = summed->calibration_coeffs();
    switch (summed->energy_calibration_model())
    {
      case EnergyCalType::Polynomial:
      case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
          //Energy calibration already polynomial, no need to do anything
      break;

      case EnergyCalType::FullRangeFraction:
        //Convert to polynomial
        energy_cal_coeffs = fullrangefraction_coef_to_polynomial(energy_cal_coeffs, gamma_channel_counts.size());
      break;

      case EnergyCalType::LowerChannelEdge:
      case EnergyCalType::InvalidEquationType:
        //No hope of converting energy calibration to the polynomial needed by CNF files.
        energy_cal_coeffs.clear();
      break;
    }//switch( energy cal type coefficients are in )

  
    /// \TODO: Check if neutron counts are supported in CNF files.
    //Neutron information:
    //const double sum_neutrons = summed->neutron_counts_sum();
        
    //With short measurements or handheld detectors we may not have had any
    //  neutron counts, but there was a detector, so lets check if the input
    //  file had information about neutrons.
    //const bool had_neutrons = summed->contained_neutron();


    //Measurement start time.
    //The start time may not be valid (e.g., if input file didnt have times),
    // but if we're here we time is valid, just the unix epoch
    const time_point_t start_time = SpecUtils::is_special(summed->start_time())
                                    ? time_point_t{}
                                    : summed->start_time();

    //Check if we have RIID analysis results we could write to the output file.
    /** \TODO: implement writing RIID analysis resukts to output file.
         
    if (detectors_analysis_ && !detectors_analysis_->is_empty())
    {
      //See DetectorAnalysis class for details; its a little iffy what
      //  information from the original file makes it into the DetectorAnalysis.

      const DetectorAnalysis& ana = *detectors_analysis_;
      //ana.algorithm_result_description_
      //ana.remarks_
      //...

      //Loop over individual results, usually different nuclides or sources.
      for (const DetectorAnalysisResult& nucres : ana.results_)
      {
      }//for( loop over nuclides identified )
     
    }//if( we have riid results from input file )
    */
      
    //We should have most of the information we need identified by here, so now
    //  just need to write to the output stream.
    //  ex., output.write( (const char *)my_data, 10 );

        ////create a containter for the file header
        //const size_t file_header_length = 0x800;
        //const size_t sec_header_length = 0x30;

        ////create the aquisition parameters (acqp) header
        //size_t acqp_header[] = { 0x0100, 0x0800, 0x0000, 0x0800,              //has common data, size of block, size of header
        //                           0x0000, 0x0000,  sec_header_length,        //block locaton is acually a int32 but break it up for this
        //                           0x0000, 0x0000, 0x0000, 0x0000, 0x003C,    //always 0x3C
        //                           0x0000, 0x0001, 0x0440, 0x02EA, 0x01FB,    //number of records, size of record block, address of records in block, address of common tabular
        //                           0x0019, 0x03E6, 0X0009, 0x0000, 0x0000 };  //Always 0x19, address of entries in block,
        //acqp_header[21] = acqp_header[6] + acqp_header[14] + acqp_header[15];

        ////create the sample header (SAMP)
        //size_t samp_header[] = { 0x0500, 0x0A00, 0x0000, 0x1000,              //has common data, size of header
        //                           0x0000, 0x0000,   sec_header_length,       //block locaton is acually a int32 but break it up for this
        //                           0x0000, 0x0000, 0x0000, 0x0000, 0x003C,    //always 0x3C
        //                           0x0000, 0x0000, 0x0000, 0x7FFF, 0x7FFF,    //size of data item, address of the common tabular
        //                           0x0000, 0x7FFF, 0x0000, 0x0000, 0x0A00 };  //address of entires in block

        ////create the spectrum header (DATA)
        //size_t data_header[] = { 0x0500, 0x0000, 0x0000, 0x1A00,              //has common data, size of header
        //                           0x0000, 0x0000, sec_header_length,         //block locaton is acually a int32 but break it up for this
        //                           0x0000, 0x0000, 0x0000, 0x0000, 0x003C,    //always 0x3C
        //                           0x0000, 0x0000, 0x0004, 0x0000, 0x0000,    //size of data item
        //                           0x0000, 0x01D0, 0x0000, 0x0000, 0x0001 };  //address of entires in block, always 1

        ////compute the number of channels and the size of the block
        ////data_header[19] = summed->num_gamma_channels();
        ////CAM file is required to hvae one of fixed (0x400 - 0x10000) channel numbers
        //size_t num_channels = summed->num_gamma_channels();
        //if (num_channels <= 0x200)
        //{
        //    data_header[19] = 0x200;
        //}
        //if (num_channels > 0x200 && num_channels <= 0x400)
        //{
        //    data_header[19] = 0x400;
        //}
        //else if (num_channels > 0x400 && num_channels <= 0x800)
        //{
        //    data_header[19] = 0x800;
        //}
        //else if (num_channels > 0x800 && num_channels <= 0x1000)
        //{
        //    data_header[19] = 0x1000;
        //}
        //else if (num_channels > 0x1000 && num_channels <= 0x2000)
        //{
        //    data_header[19] = 0x2000;
        //}
        //else if (num_channels > 0x2000 && num_channels <= 0x4000)
        //{
        //    data_header[19] = 0x4000;
        //}
        //else if (num_channels > 0x4000 && num_channels <= 0x8000)
        //{
        //    data_header[19] = 0x8000;
        //}
        //else if (num_channels > 0x8000 && num_channels <= 0x10000)
        //{
        //    data_header[19] = 0x10000;
        //}
        //else
        //{
        //    data_header[19] = num_channels;
        //}

        //data_header[1] = data_header[6] + data_header[18] + data_header[19] * data_header[14];
        //if (data_header[19] == 0x4000) 
        //{
        //    data_header[2] = 0x01;
        //}

        //const size_t file_length = file_header_length + acqp_header[1] +samp_header[1] + data_header[1];
        //create a vector to store all the bytes
        //std::vector<byte_type> cnf_file(file_length, 0x00);


        //enter the file header
        //enter_CAM_value(0x400, cnf_file, 0x0, cam_type::cam_word);
        //enter_CAM_value(0x4, cnf_file, 0x5, cam_type::cam_byte);
        //enter_CAM_value(file_header_length, cnf_file, 0x6, cam_type::cam_word);
        //enter_CAM_value(file_length, cnf_file, 0xa, cam_type::cam_longword);
        //enter_CAM_value(sec_header_length, cnf_file, 0x10, cam_type::cam_word);
       // enter_CAM_value(0x29, cnf_file, 0x12, cam_type::cam_word); //Maximum number of headers

        //enter the acqp header
        //size_t acqp_loc = acqp_header[3];
        //enter_CAM_value(0x12000, cnf_file, 0x70, cam_type::cam_longword);       //block identifier
        //enter_CAM_value(0x12000, cnf_file, acqp_loc, cam_type::cam_longword);   //block identifier in block
        //put in array of data
        //for (size_t i = 0; i < 22; i++)
        //{
        //    size_t pos = 0x4 + i * 0x2;
        //    enter_CAM_value(acqp_header[i], cnf_file, 0x70 + pos, cam_type::cam_word);
        //    enter_CAM_value(acqp_header[i], cnf_file, acqp_loc + pos, cam_type::cam_word);
        //}
        //acqp_loc += 0x30;

        //enter the aqcp data
        //enter_CAM_value("PHA ", cnf_file, acqp_loc + 0x80, cam_type::cam_string);
        //enter_CAM_value(0x04, cnf_file, acqp_loc + 0x88, cam_type::cam_word);         //BITES
        //enter_CAM_value(0x01, cnf_file, acqp_loc + 0x8D, cam_type::cam_word);         //ROWS
        //enter_CAM_value(0x01, cnf_file, acqp_loc + 0x91, cam_type::cam_word);         //GROUPS
        //enter_CAM_value(0x4, cnf_file, acqp_loc + 0x55, cam_type::cam_word);          //BACKGNDCHNS
        //enter_CAM_value(data_header[19], cnf_file, acqp_loc + 0x89, cam_type::cam_longword);//Channels
        //enter_CAM_value(8192, cnf_file, acqp_loc + 0x89, cam_type::cam_longword);
        string title = summed->title(); //CTITLE
        if (!title.empty()) 
        {
            std::string expectsString(title.begin(), title.end());
            expectsString.resize( 0x20, '\0');
            //enter_CAM_value(expectsString, cnf_file, acqp_loc);
            cam->AddSampleTitle(expectsString);
        }
        
 //TODO: implement converted shape calibration information into CNF files 
        //shape calibration, just use the default values for NaI detectors if the type cotains any NaI, if not use Ge defaults
        const string& detector_type = summed->detector_type();
        cam->AddDetectorType(detector_type);
        //enter_CAM_value("SQRT", cnf_file, acqp_loc + 0x464, cam_type::cam_string);
        //if(detector_type.find("NaI") == 0 || detector_type.find("nai") == 0 || detector_type.find("NAI") == 0)
        //{
        //    enter_CAM_value(-7.0, cnf_file, acqp_loc + 0x3C6, cam_type::cam_float); //FWHMOFF
        //    enter_CAM_value(2.0, cnf_file, acqp_loc + 0x3CA, cam_type::cam_float);  //FWHMSLOPE
        //}
        //else //use the Ge defualts
        //{
        //    enter_CAM_value(1.0, cnf_file, acqp_loc + 0x3C6, cam_type::cam_float);
        //    enter_CAM_value(0.035, cnf_file, acqp_loc + 0x3CA, cam_type::cam_float);
        //}

        //energy calibration
        cam->AddEnergyCalibration(energy_cal_coeffs);
        //enter_CAM_value("POLY", cnf_file, acqp_loc + 0x5E, cam_type::cam_string);
        //enter_CAM_value("POLY", cnf_file, acqp_loc + 0xFB, cam_type::cam_string);
        //enter_CAM_value("keV", cnf_file, acqp_loc + 0x346, cam_type::cam_string);
        //enter_CAM_value(1.0, cnf_file, acqp_loc + 0x312, cam_type::cam_float);
        ////check if there is energy calibration infomation
        //if (!energy_cal_coeffs.empty()) {
        //    enter_CAM_value(energy_cal_coeffs.size(), cnf_file, acqp_loc + 0x46C, cam_type::cam_word);
        //    for (size_t i = 0; i < energy_cal_coeffs.size(); i++)
        //    {
        //        enter_CAM_value(energy_cal_coeffs[i], cnf_file, acqp_loc + 0x32E + i * 0x4, cam_type::cam_float);
        //    }
        //    enter_CAM_value(03, cnf_file, acqp_loc + 0x32A, cam_type::cam_longword); //ECALFLAGS set to energy and shape calibration
        //}
        //else 
        //{
        //    enter_CAM_value(02, cnf_file, acqp_loc + 0x32A, cam_type::cam_longword); //ECALFLAGS set to just shape calibration
        //}

        //times
        if (!SpecUtils::is_special(start_time)) {
            cam->AddAcquitionTime(start_time);
            /*enter_CAM_value(0x01, cnf_file, acqp_loc + acqp_header[16], cam_type::cam_byte);
            enter_CAM_value(start_time, cnf_file, acqp_loc + acqp_header[16] + 0x01, cam_type::cam_datetime);*/
        }
        cam->AddLiveTime(live_time);
        cam->AddRealTime(real_time);
        //enter_CAM_value(real_time, cnf_file, acqp_loc + acqp_header[16] + 0x09, cam_type::cam_duration);
        //enter_CAM_value(live_time, cnf_file, acqp_loc + acqp_header[16] + 0x11, cam_type::cam_duration);

        //enter the samp header
        //size_t samp_loc = samp_header[3];
        //enter_CAM_value(0x12001, cnf_file, 0x70 + 0x30, cam_type::cam_longword); //block identifier
        //enter_CAM_value(0x12001, cnf_file, samp_loc, cam_type::cam_longword);   //block identifier in block
        ////put in array of data
        //for (size_t i = 0; i < 22; i++)
        //{
        //    size_t pos = 0x4 + i * 0x2;
        //    enter_CAM_value(samp_header[i], cnf_file, 0x70 + 0x30 + pos, cam_type::cam_word);
        //    enter_CAM_value(samp_header[i], cnf_file, samp_loc + pos, cam_type::cam_word);
        //}
        //samp_loc += 0x30;

        //if there is a sample ID of some sort
        /*if (!sample_ID.empty()) {
            enter_CAM_value(sample_ID.erase(sample_ID.begin() + 0x10, sample_ID.end()), cnf_file, samp_loc + 0x40, cam_string);
        }*/
        //sample quanity cannot be zero
        //enter_CAM_value(1.0, cnf_file, samp_loc + 0x90, cam_type::cam_float);
        //set the sample time to the aqusition start time
        //if (!SpecUtils::is_special(start_time)) {
        //    enter_CAM_value(start_time, cnf_file, samp_loc + 0xB4, cam_type::cam_datetime);
        //}
        if (summed->has_gps_info()) 
        {
            //cam->AddGPSData(summed->latitude(), summed->longitude(), summed->speed(), summed->position_time());
            //enter_CAM_value(summed->latitude(), cnf_file, samp_loc + 0x8D0, cam_type::cam_double);
            //enter_CAM_value(summed->longitude(), cnf_file, samp_loc + 0x928, cam_type::cam_double);
            //enter_CAM_value(summed->speed(), cnf_file, samp_loc + 0x938, cam_type::cam_double);
            
            if(!SpecUtils::is_special(summed->position_time()))
                cam->AddGPSData(summed->latitude(), summed->longitude(), summed->speed(), summed->position_time());
            else
                cam->AddGPSData(summed->latitude(), summed->longitude(), summed->speed());
            //    enter_CAM_value(summed->position_time(), cnf_file, samp_loc + 0x940, cam_type::cam_datetime);
        }
        //enter the data header
        cam->AddSpectrum(gamma_channel_counts);
        //size_t data_loc = data_header[3];
        //enter_CAM_value(0x12005, cnf_file, 0x70 + 2*0x30, cam_type::cam_longword); //block identifier
        //enter_CAM_value(0x12005, cnf_file, data_loc, cam_type::cam_longword);   //block identifier in block
        ////put in array of data
        //for (size_t i = 0; i < 22; i++)
        //{
        //    size_t pos = 0x4 + i * 0x2;
        //    enter_CAM_value(data_header[i], cnf_file, 0x70 + 2* 0x30 + pos, cam_type::cam_word);
        //    enter_CAM_value(data_header[i], cnf_file, data_loc + pos, cam_type::cam_word);
        //}
        //data_loc += 0x30 + data_header[18];
        ////put the data in
        //for (size_t i = 0; i < gamma_channel_counts.size(); i++)
        //{
        //  //enter_CAM_value(gamma_channel_counts[i], cnf_file, data_loc + 0x4 * i, cam_type::cam_longword);
        //  
        //  const uint32_t counts = float_to_integral<uint32_t>( gamma_channel_counts[i] );
        //  enter_CAM_value(counts, cnf_file, data_loc + 0x4 * i, cam_type::cam_longword);
        //}
        // 
        auto cnf_file = cam->CreateFile();
        //write the file
        output.write((char* )cnf_file.data(), cnf_file.size());
    }catch( std::exception &e )
    {
#if( PERFORM_DEVELOPER_CHECKS )
      //Print out why we failed for debug purposes.
      log_developer_error( __func__, ("Failed to write CNF file: " + string(e.what())).c_str() );
#endif
      return false;
    }//try / catch
  
    return true;
}//write_cnf

}//namespace SpecUtils



