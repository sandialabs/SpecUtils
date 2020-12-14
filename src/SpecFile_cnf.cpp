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

    typedef unsigned char byte;

    template< typename T > std::array< byte, sizeof(T) >  to_bytes(const T& object)
    {
        std::array< byte, sizeof(T) > bytes;

        const byte* begin = reinterpret_cast<const byte*>(std::addressof(object));
        const byte* end = begin + sizeof(T);
        std::copy(begin, end, std::begin(bytes));

        return bytes;
    }

    enum class cam_type //name and size of bytes
    {
  
        cam_float,     //any float
        cam_double,    //any double
        cam_byte,      //a byte
        cam_word,      //int16
        cam_longword,  //int
        cam_quadword,  //int64     
        cam_datetime ,   //date time
        cam_duration,   //time duration 
        cam_string,
    };

    //Convert data to CAM data formats

    template <class T>
    //IEEE-754 variables to CAM float (PDP-11)
    std::array< byte, sizeof(int32_t) > convert_to_CAM_float(const T& input)
    {

        //pdp-11 is a wordswaped float/4
        float temp_f = static_cast<float>(input * 4);
        const auto temp = to_bytes(temp_f);
        const size_t word_size = 2;
        std::array< byte, sizeof(int32_t) > output = { 0x00 };
        //perform a word swap
        for (size_t i = 0; i < word_size; i++)
        {
            output[i] = temp[i + word_size];
            output[i + word_size] = temp[i];
        }
        return output;
    }

    template <class T>
    //IEEE variables to CAM double (PDP-11)
    std::array< byte, sizeof(int64_t) > convert_to_CAM_double(const T& input)
    {

        //pdp-11 is a word swaped Double/4
        double temp_d = static_cast<double>(input * 4.0) ;
        const auto temp = to_bytes(temp_d);
        const size_t word_size = 2;
        std::array< byte, sizeof(int64_t) > output = { 0x00 };
        //perform a word swap
        for (size_t i = 0; i < word_size; i++)
        {
            output[i + 3 * word_size] = temp[i];				//IEEE fourth is PDP-11 first
            output[i + 2 * word_size] = temp[i + word_size];  //IEEE third is PDP-11 second
            output[i + word_size] = temp[i + 2 * word_size];//IEEE second is PDP-11 third
            output[i] = temp[i + 3 * word_size];            //IEEE first is PDP-11 fouth
        }
        return output;
    }

    //boost ptime to CAM DateTime
    std::array< byte, sizeof(int64_t) > convert_to_CAM_datetime(const boost::posix_time::ptime& date_time)
    {
        //error checking
        if (date_time.is_not_a_date_time())
            throw std::range_error("The input date time is not a valid date time");

        std::array< byte, sizeof(int64_t) > bytes = { 0x00 };
        //get the total seconds between the input time and the epoch
        boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
        boost::posix_time::time_duration::sec_type sec_from_epoch = (date_time - epoch).total_seconds();

        //covert to modified julian in usec
        uint64_t j_sec = (sec_from_epoch + 3506716800UL) * 10000000UL;
        bytes = to_bytes(j_sec);
        return bytes;
    }

    boost::posix_time::ptime convert_from_CAM_datetime( uint64_t time_raw )
    {
      boost::posix_time::ptime answer;
      if( !time_raw )
        return answer;

      answer = boost::posix_time::ptime( boost::gregorian::date(1970, 1, 1) );

      const int64_t secs = time_raw / 10000000L;
      const int64_t sec_from_epoch = secs - 3506716800L;
      
      answer += boost::posix_time::seconds(sec_from_epoch);
      answer += boost::posix_time::microseconds( secs % 10000000L );
      
      return answer;
    }//convert_from_CAM_datetime(...)


    //float sec to CAM duration
    std::array< byte, sizeof(int64_t) > convert_to_CAM_duration(const float& duration)
    {
        std::array< byte, sizeof(int64_t) > bytes = { 0x00 };
        //duration in usec is larger than a int64: covert to years
        if (duration * 10000000 > INT64_MAX)
        {
            double t_duration = duration / 31557600;
            //duration in years is larger than an int32, divide by a million years
            if (duration / 31557600 > INT32_MAX)
            {
                int32_t y_duration = static_cast<int32_t>(t_duration / 1e6);
                const auto y_bytes = to_bytes(y_duration);
                std::copy(begin(y_bytes), end(y_bytes), begin(bytes));
                //set the flags
                bytes[7] = 0x80;
                bytes[4] = 0x01;
                return bytes;

            }
            //duration can be represented in years
            else
            {
                int32_t y_duration = static_cast<int32_t>(t_duration);
                const auto y_bytes = to_bytes(y_duration);
                std::copy(begin(y_bytes), end(y_bytes), begin(bytes));
                //set the flag
                bytes[7] = 0x80;
                return bytes;
            }
        }
        //duration is able to be represented in usec
        else
        {
            //cam time span is in usec and a negatve int64
            int64_t t_duration = static_cast<int64_t>((double)duration * -10000000);
            bytes = to_bytes(t_duration);
            return bytes;
        }

    }
    //enter the input to the cam desition vector of bytes at the location, with a given datatype
    template<typename T, typename = typename std::enable_if<std::is_arithmetic<T>::value, T>::type>
    void enter_CAM_value(const T& input, vector<byte>& destination, const size_t& location, const cam_type& type) 
    {
        switch (type) {
        case cam_type::cam_float:
        {
            const auto bytes = convert_to_CAM_float(input);
          
          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
            throw std::runtime_error( "enter_CAM_value(cam_float) invalid write location" );
          
            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
        }
        break;
        case cam_type::cam_double:
        {
            const auto bytes = convert_to_CAM_double(input);
          
          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
            throw std::runtime_error( "enter_CAM_value(cam_double) invalid write location" );
          
            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
        }
        break;
        case cam_type::cam_duration:
        {
            const auto bytes = convert_to_CAM_duration(input);
          
          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
            throw std::runtime_error( "enter_CAM_value(cam_duration) invalid write location" );
          
            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
        }
        break;
        case cam_type::cam_quadword:
        {
            int64_t t_quadword = static_cast<int64_t>(input);
            const auto bytes = to_bytes(t_quadword);
          
          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
            throw std::runtime_error( "enter_CAM_value(cam_quadword) invalid write location" );
          
            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
        }
        break;
        case cam_type::cam_longword:
        {
            int32_t t_longword = static_cast<int32_t>(input);
            const auto bytes = to_bytes(t_longword);
          
          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
            throw std::runtime_error( "enter_CAM_value(cam_longword) invalid write location" );
          
            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
        }
        break;
        case cam_type::cam_word:
        {
            int16_t t_word = static_cast<int16_t>(input);
            const auto bytes = to_bytes(t_word);
          
          if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
            throw std::runtime_error( "enter_CAM_value(cam_word) invalid write location" );
          
            std::copy(std::begin(bytes), std::end(bytes), destination.begin() + location);
        }
        break;
        case cam_type::cam_byte:
        {
            byte t_byte = static_cast<byte>(input);
            destination.at(location) = t_byte;
            //const byte* begin = reinterpret_cast<const byte*>(std::addressof(t_byte));
            //const byte* end = begin + sizeof(byte);
            //std::copy(begin, end, destination.begin() + location);
        break;
        }
        default:
            string message = "error - Invalid converstion from: ";
            message.append(typeid(T).name());
            message.append(" to athermetic type");

            throw std::invalid_argument(message);
            break;
        }//end switch
    }
    //enter the input to the cam desition vector of bytes at the location, with a given datatype
    void enter_CAM_value(const boost::posix_time::ptime& input, vector<byte>& destination, const size_t& location, const cam_type& type=cam_type::cam_datetime)
    {
        if (type != cam_type::cam_datetime)
        {
            throw std::invalid_argument("error - Invalid converstion from: boost::posix_time::ptime");
        }

        const auto bytes = convert_to_CAM_datetime(input);
      
      if( (std::begin(destination) + location + (std::end(bytes) - std::begin(bytes))) > std::end(destination) )
        throw std::runtime_error( "enter_CAM_value(ptime) invalid write location" );
       
      std::copy(begin(bytes), end(bytes) , destination.begin() + location);
    }
    //enter the input to the cam desition vector of bytes at the location, with a given datatype
    void enter_CAM_value(const string& input, vector<byte>& destination, const size_t& location, const cam_type& type=cam_type::cam_string)
    {
        if (type != cam_type::cam_string)
        {
            throw std::invalid_argument("error - Invalid converstion from: char*[]");
        }
      
      if( (std::begin(destination) + location + (std::end(input) - std::begin(input))) > std::end(destination) )
        throw std::runtime_error( "enter_CAM_value(string) invalid write location" );
      
        std::copy(input.begin(), input.end(), destination.begin() + location);
    }

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
    
    
    input.seekg( record_offset, std::ios::beg );
    
    uint64_t time_raw;
    read_binary_data( input, time_raw );
    meas->start_time_ = convert_from_CAM_datetime( time_raw );
    
    uint32_t I, J;
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
    if( !isPowerOfTwo || (num_channels < 16) || (num_channels > (65536 + 8)) )
      throw runtime_error( "Invalid number of channels" );
    
    vector<float> calib_params(3);
    input.seekg( energy_calib_offset, std::ios::beg );
    
    vector<float> cal_coefs = { 0.0f, 0.0f, 0.0f };
    cal_coefs[0] = readCnfFloat( input );
    cal_coefs[1] = readCnfFloat( input );
    cal_coefs[2] = readCnfFloat( input );
    
    try
    {
      auto newcal = make_shared<EnergyCalibration>();
      newcal->set_polynomial( num_channels, cal_coefs, {} );
      meas->energy_calibration_ = newcal;
    }catch( std::exception & )
    {
      bool allZeros = true;
      for( const float v : cal_coefs )
        allZeros = allZeros && (v==0.0f);
      
      if( !allZeros )
        throw runtime_error( "Calibration parameters were invalid" );
    }//try /catch set calibration
    
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
    const boost::posix_time::ptime& start_time = summed->start_time().is_special() ?
            SpecUtils::time_from_string("1970-01-01 00:00:00"): summed->start_time();

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

        //create a containter for the file header
        const size_t file_header_length = 0x800;
        const size_t sec_header_length = 0x30;

        //create the aquisition parameters (acqp) header
        size_t acqp_header[] = { 0x0100, 0x0800, 0x0000, 0x0800,              //has common data, size of block, size of header
                                   0x0000, 0x0000,  sec_header_length,        //block locaton is acually a int32 but break it up for this
                                   0x0000, 0x0000, 0x0000, 0x0000, 0x003C,    //always 0x3C
                                   0x0000, 0x0001, 0x0440, 0x02EA, 0x01FB,    //number of records, size of record block, address of records in block, address of common tabular
                                   0x0019, 0x03E6, 0X0009, 0x0000, 0x0000 };  //Always 0x19, address of entries in block,
        acqp_header[21] = acqp_header[6] + acqp_header[14] + acqp_header[15];

        //create the sample header (SAMP)
        size_t samp_header[] = { 0x0500, 0x0A00, 0x0000, 0x1000,              //has common data, size of header
                                   0x0000, 0x0000,   sec_header_length,       //block locaton is acually a int32 but break it up for this
                                   0x0000, 0x0000, 0x0000, 0x0000, 0x003C,    //always 0x3C
                                   0x0000, 0x0000, 0x0000, 0x7FFF, 0x7FFF,    //size of data item, address of the common tabular
                                   0x0000, 0x7FFF, 0x0000, 0x0000, 0x0A00 };  //address of entires in block

        //create the spectrum header (DATA)
        size_t data_header[] = { 0x0500, 0x0000, 0x0000, 0x1A00,              //has common data, size of header
                                   0x0000, 0x0000, sec_header_length,         //block locaton is acually a int32 but break it up for this
                                   0x0000, 0x0000, 0x0000, 0x0000, 0x003C,    //always 0x3C
                                   0x0000, 0x0000, 0x0004, 0x0000, 0x0000,    //size of data item
                                   0x0000, 0x01D0, 0x0000, 0x0000, 0x0001 };  //address of entires in block, always 1

        //compute the number of channels and the size of the block
        //data_header[19] = summed->num_gamma_channels();
        //CAM file is required to hvae one of fixed (0x400 - 0x10000) channel numbers
        size_t num_channels = summed->num_gamma_channels();
        if (num_channels <= 0x200)
        {
            data_header[19] = 0x200;
        }
        if (num_channels > 0x200 && num_channels <= 0x400)
        {
            data_header[19] = 0x400;
        }
        else if (num_channels > 0x400 && num_channels <= 0x800)
        {
            data_header[19] = 0x800;
        }
        else if (num_channels > 0x800 && num_channels <= 0x1000)
        {
            data_header[19] = 0x1000;
        }
        else if (num_channels > 0x1000 && num_channels <= 0x2000)
        {
            data_header[19] = 0x2000;
        }
        else if (num_channels > 0x2000 && num_channels <= 0x4000)
        {
            data_header[19] = 0x4000;
        }
        else if (num_channels > 0x4000 && num_channels <= 0x8000)
        {
            data_header[19] = 0x8000;
        }
        else if (num_channels > 0x8000 && num_channels <= 0x10000)
        {
            data_header[19] = 0x10000;
        }
        else
        {
            data_header[19] = num_channels;
        }

        data_header[1] = data_header[6] + data_header[18] + data_header[19] * data_header[14];
        if (data_header[19] == 0x4000) 
        {
            data_header[2] = 0x01;
        }

        const size_t file_length = file_header_length + acqp_header[1] +samp_header[1] + data_header[1];
        //create a vector to store all the bytes
        std::vector<byte> cnf_file(file_length, 0x00);


        //enter the file header
        enter_CAM_value(0x400, cnf_file, 0x0, cam_type::cam_word);
        //enter_CAM_value(0x4, cnf_file, 0x5, cam_type::cam_byte);
        enter_CAM_value(file_header_length, cnf_file, 0x6, cam_type::cam_word);
        enter_CAM_value(file_length, cnf_file, 0xa, cam_type::cam_longword);
        enter_CAM_value(sec_header_length, cnf_file, 0x10, cam_type::cam_word);
        enter_CAM_value(0x29, cnf_file, 0x12, cam_type::cam_word); //Maximum number of headers

        //enter the acqp header
        size_t acqp_loc = acqp_header[3];
        enter_CAM_value(0x12000, cnf_file, 0x70, cam_type::cam_longword);       //block identifier
        enter_CAM_value(0x12000, cnf_file, acqp_loc, cam_type::cam_longword);   //block identifier in block
        //put in array of data
        for (size_t i = 0; i < 22; i++)
        {
            size_t pos = 0x4 + i * 0x2;
            enter_CAM_value(acqp_header[i], cnf_file, 0x70 + pos, cam_type::cam_word);
            enter_CAM_value(acqp_header[i], cnf_file, acqp_loc + pos, cam_type::cam_word);
        }
        acqp_loc += 0x30;

        //enter the aqcp data
        enter_CAM_value("PHA ", cnf_file, acqp_loc + 0x80, cam_type::cam_string);
        enter_CAM_value(0x04, cnf_file, acqp_loc + 0x88, cam_type::cam_word);         //BITES
        enter_CAM_value(0x01, cnf_file, acqp_loc + 0x8D, cam_type::cam_word);         //ROWS
        enter_CAM_value(0x01, cnf_file, acqp_loc + 0x91, cam_type::cam_word);         //GROUPS
        enter_CAM_value(0x4, cnf_file, acqp_loc + 0x55, cam_type::cam_word);          //BACKGNDCHNS
        enter_CAM_value(data_header[19], cnf_file, acqp_loc + 0x89, cam_type::cam_longword);//Channels
        //enter_CAM_value(8192, cnf_file, acqp_loc + 0x89, cam_type::cam_longword);
        string title = summed->title(); //CTITLE
        if (!title.empty()) 
        {
            std::string expectsString(title.begin(), title.begin() + 0x20);
            enter_CAM_value(expectsString, cnf_file, acqp_loc);
        }
        
 //TODO: implement converted shape calibration information into CNF files 
        //shape calibration, just use the defualt values for NaI detectors if the type cotains any NaI, if not use Ge defaults
        const string& detector_type = summed->detector_type();
        enter_CAM_value("SQRT", cnf_file, acqp_loc + 0x464, cam_type::cam_string);
        if(detector_type.find("NaI") == 0 || detector_type.find("nai") == 0 || detector_type.find("NAI") == 0)
        {
            enter_CAM_value(-7.0, cnf_file, acqp_loc + 0x3C6, cam_type::cam_float); //FWHMOFF
            enter_CAM_value(2.0, cnf_file, acqp_loc + 0x3CA, cam_type::cam_float);  //FWHMSLOPE
        }
        else //use the Ge defualts
        {
            enter_CAM_value(1.0, cnf_file, acqp_loc + 0x3C6, cam_type::cam_float);
            enter_CAM_value(0.3, cnf_file, acqp_loc + 0x3CA, cam_type::cam_float);
        }

        //energy calibration
        enter_CAM_value("POLY", cnf_file, acqp_loc + 0x5E, cam_type::cam_string);
        enter_CAM_value("POLY", cnf_file, acqp_loc + 0xFB, cam_type::cam_string);
        enter_CAM_value("keV", cnf_file, acqp_loc + 0x346, cam_type::cam_string);
        enter_CAM_value(1.0, cnf_file, acqp_loc + 0x312, cam_type::cam_float);
        //check if there is energy calibration infomation
        if (!energy_cal_coeffs.empty()) {
            enter_CAM_value(energy_cal_coeffs.size(), cnf_file, acqp_loc + 0x46C, cam_type::cam_word);
            for (size_t i = 0; i < energy_cal_coeffs.size(); i++)
            {
                enter_CAM_value(energy_cal_coeffs[i], cnf_file, acqp_loc + 0x32E + i * 0x4, cam_type::cam_float);
            }
            enter_CAM_value(03, cnf_file, acqp_loc + 0x32A, cam_type::cam_longword); //ECALFLAGS set to energy and shape calibration
        }
        else 
        {
            enter_CAM_value(02, cnf_file, acqp_loc + 0x32A, cam_type::cam_longword); //ECALFLAGS set to just shape calibration
        }

        //times
        if (!start_time.is_not_a_date_time()) {
            enter_CAM_value(0x01, cnf_file, acqp_loc + acqp_header[16], cam_type::cam_byte);
            enter_CAM_value(start_time, cnf_file, acqp_loc + acqp_header[16] + 0x01, cam_type::cam_datetime);
        }
        enter_CAM_value(real_time, cnf_file, acqp_loc + acqp_header[16] + 0x09, cam_type::cam_duration);
        enter_CAM_value(live_time, cnf_file, acqp_loc + acqp_header[16] + 0x11, cam_type::cam_duration);

        //enter the samp header
        size_t samp_loc = samp_header[3];
        enter_CAM_value(0x12001, cnf_file, 0x70 + 0x30, cam_type::cam_longword); //block identifier
        enter_CAM_value(0x12001, cnf_file, samp_loc, cam_type::cam_longword);   //block identifier in block
        //put in array of data
        for (size_t i = 0; i < 22; i++)
        {
            size_t pos = 0x4 + i * 0x2;
            enter_CAM_value(samp_header[i], cnf_file, 0x70 + 0x30 + pos, cam_type::cam_word);
            enter_CAM_value(samp_header[i], cnf_file, samp_loc + pos, cam_type::cam_word);
        }
        samp_loc += 0x30;

        //if there is a sample ID of some sort
        /*if (!sample_ID.empty()) {
            enter_CAM_value(sample_ID.erase(sample_ID.begin() + 0x10, sample_ID.end()), cnf_file, samp_loc + 0x40, cam_string);
        }*/
        //sample quanity cannot be zero
        enter_CAM_value(1.0, cnf_file, samp_loc + 0x90, cam_type::cam_float);
        //set the sample time to the aqusition start time
        if (!start_time.is_not_a_date_time()) {
            enter_CAM_value(start_time, cnf_file, samp_loc + 0xB4, cam_type::cam_datetime);
        }
        if (summed->has_gps_info()) 
        {
            enter_CAM_value(summed->latitude(), cnf_file, samp_loc + 0x8D0, cam_type::cam_double);
            enter_CAM_value(summed->longitude(), cnf_file, samp_loc + 0x928, cam_type::cam_double);
            enter_CAM_value(summed->speed(), cnf_file, samp_loc + 0x938, cam_type::cam_double);
            if(!summed->position_time().is_not_a_date_time())
                enter_CAM_value(summed->position_time(), cnf_file, samp_loc + 0x940, cam_type::cam_datetime);
        }
        //enter the data header
        size_t data_loc = data_header[3];
        enter_CAM_value(0x12005, cnf_file, 0x70 + 2*0x30, cam_type::cam_longword); //block identifier
        enter_CAM_value(0x12005, cnf_file, data_loc, cam_type::cam_longword);   //block identifier in block
        //put in array of data
        for (size_t i = 0; i < 22; i++)
        {
            size_t pos = 0x4 + i * 0x2;
            enter_CAM_value(data_header[i], cnf_file, 0x70 + 2* 0x30 + pos, cam_type::cam_word);
            enter_CAM_value(data_header[i], cnf_file, data_loc + pos, cam_type::cam_word);
        }
        data_loc += 0x30 + data_header[18];
        //put the data in
        for (size_t i = 0; i < gamma_channel_counts.size(); i++)
        {
            enter_CAM_value(gamma_channel_counts[i], cnf_file, data_loc + 0x4 * i, cam_type::cam_longword);
        }
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



