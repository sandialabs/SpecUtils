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
  bool toFloat( const std::string &str, float &f )
  {
    //ToDO: should probably use SpecUtils::parse_float(...) for consistency/speed
    const int nconvert = sscanf( str.c_str(), "%f", &f );
    return (nconvert == 1);
  }
}//namespace


namespace SpecUtils
{
bool SpecFile::load_from_Gr135_txt( std::istream &input )
{
  //See data file for refIED7MP8PYR for an example file
  reset();
  
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  
  try
  {
    string line;
    if( !SpecUtils::safe_get_line( input, line ) )
      return false;
    
    vector<string> headers;
    split( headers, line, "\t" );
    
    if( headers.empty() )
      throw runtime_error( "Not the GR135 header expected" );
    
    //Calibration to use when none is specified
    shared_ptr<EnergyCalibration> default_cal;
    
    //each header will look like:
    // "1899715091 Oct. 09 2013  12:29:38 T counts Live time (s) 279.4 neutron 1 gieger 194"
    std::vector< std::shared_ptr<Measurement> > measurements;
    std::vector< std::shared_ptr<std::vector<float>> > gammacounts;
    
    for( size_t i = 0; i < headers.size(); ++i )
    {
      const string &header = headers[i];
      if( header.empty() )
        continue;
      
      auto meas = make_shared<Measurement>();
      
      string::size_type pos = header.find( ' ' );
      if( pos == string::npos )
        throw runtime_error( "Invalid GR135 measurement header" );
      
      const string measIDStr = header.substr( 0, pos );
      string timestampStr = header.substr( pos+1 );
      pos = timestampStr.find( " T " );
      if( pos == string::npos )
        pos = timestampStr.find( " R " );
      if( pos == string::npos )
        pos = timestampStr.find( " Q " );
      if( pos == string::npos )
        pos = timestampStr.find( " counts " );
      
      if( pos == string::npos )
        throw runtime_error( "Couldnt find end of GR135 timestamp string" );
      timestampStr = timestampStr.substr( 0, pos );  //"Oct. 09 2013  13:08:29"
      
      meas->start_time_ = time_from_string( timestampStr.c_str() );
#if(PERFORM_DEVELOPER_CHECKS)
      if( meas->start_time_.is_special() )
        log_developer_error( __func__, ("Failed to extract measurement start time from: '" + header  + "' timestampStr='" + timestampStr + "'").c_str() );
#endif
      
      pos = header.find( "Live time (s)" );
      if( pos == string::npos )
        throw runtime_error( "Couldnt find Live time" );
      
      string liveTimeStr = header.substr( pos + 13 );
      trim( liveTimeStr );
      pos = liveTimeStr.find( " " );
      if( pos != string::npos )
        liveTimeStr = liveTimeStr.substr( 0, pos );
      
      if( !liveTimeStr.empty() )
      {
        float val;
        if( !toFloat( liveTimeStr, val ) )
          throw runtime_error( "Error converting live time to float" );
        meas->live_time_ = static_cast<float>( val );
      }//if( !liveTimeStr.empty() )
      
      pos = header.find( "neutron" );
      if( pos != string::npos )
      {
        string neutronStr = header.substr( pos + 7 );
        trim( neutronStr );
        
        if( !neutronStr.empty() )
        {
          float val;
          if( !toFloat( neutronStr, val ) )
            throw runtime_error( "Error converting neutron counts to float" );
          meas->neutron_counts_.resize( 1, val );
          meas->neutron_counts_sum_ = val;
          meas->contained_neutron_ = true;
        }//if( !liveTimeStr.empty() )
        //        cerr << "meas->neutron_counts_sum_=" << meas->neutron_counts_sum_ << endl;
      }//if( pos != string::npos )
      
      pos = header.find( "gieger" );
      if( pos != string::npos )
      {
        string remark = header.substr( pos );
        trim( remark );
        meas->remarks_.push_back( remark );
      }
      
      std::shared_ptr<std::vector<float>> channelcounts( new vector<float>() );
      channelcounts->reserve( 1024 );
      meas->gamma_counts_ = channelcounts;
      meas->gamma_count_sum_ = 0.0;
      
      meas->sample_number_ = static_cast<int>(i) + 1;
      
      measurements.push_back( meas );
      gammacounts.push_back( channelcounts );
      measurements_.push_back( meas );
    }//for( size_t i = 0; i < headers.size(); ++i )
    
    if( gammacounts.empty() )
      throw runtime_error( "No GR135 txt file header" );
    
    std::vector<float> counts;
    
    while( SpecUtils::safe_get_line( input, line ) )
    {
      if( line.empty() )
        continue;
      
      SpecUtils::split_to_floats( line.c_str(), line.size(), counts );
      if( counts.size() != measurements.size() )
        throw runtime_error( "Unexpected number of channel counts" );
      
      for( size_t i = 0; i < counts.size(); ++i )
      {
        gammacounts.at(i)->push_back( counts[i] );
        measurements[i]->gamma_count_sum_ += counts[i];
      }
    }//while( SpecUtils::safe_get_line( input, line ) )
    
    const uint32_t len = static_cast<uint32_t>( gammacounts[0]->size() );
    const bool isPowerOfTwo = ((len != 0) && !(len & (len - 1)));
    if( !isPowerOfTwo )
      throw runtime_error( "Invalid number of channels" );
    
    try
    {
      //Some default calibration coefficients that are kinda sorta close
      for( size_t i = 0; i < measurements.size(); ++i )
      {
        if( measurements[i]->energy_calibration_->type() == EnergyCalType::InvalidEquationType )
        {
          //The file doesnt provide energy calibration information, so we will always need to
          //  go to a default energy calibration.
          if( !default_cal )
          {
            default_cal = make_shared<EnergyCalibration>();
            assert( len );
            default_cal->set_default_polynomial(len, {0.0f, 3.0f}, {} );
          }
          measurements[i]->energy_calibration_ = default_cal;
        }
      }
    }catch( std::exception & )
    {
      //we shouldnt ever get here
    }
    
    cleanup_after_load();
    
    return true;
  }catch( std::exception &e )
  {
    cerr << "load_from_Gr135_txt(...) caught: " << e.what() << endl;
  }//try / catch
  
  input.clear();
  input.seekg( orig_pos, ios::beg );
  
  reset();
  
  return false;
}//bool load_from_Gr135_txt( std::istream &istr )

  
  
bool SpecFile::load_binary_exploranium_file( const std::string &filename )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
#ifdef _WIN32
  ifstream file( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream file( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !file.is_open() )
    return false;
  
  const bool loaded = load_from_binary_exploranium( file );
  
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_binary_exploranium_file( const std::string &file_name )
  
  
bool SpecFile::load_from_binary_exploranium( std::istream &input )
{
  //Currently doesnt:
  //  -CHSUM  Checksum not checked
  //  -Dose rate not extracted
  //  -Software version not checked/extracted
  //  -Roi/Line results not checked
  //  -Check for library being used
  //  -Spectrum, Survey, CZT, and Dose not checked for or used
  //  -Channel number hasnt been verified
  //  -GR135 v1 and GR130 not verified
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  reset();
  
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  input.seekg( 0, ios::end );
  const istream::pos_type eof_pos = input.tellg();
  input.seekg( orig_pos, ios::beg );
  
  const size_t size = static_cast<size_t>( 0 + eof_pos - orig_pos );
  
  if( size < 513 )
    return false;
  
  try
  {
    char charbuff[128];
    vector<char> buffer( size );
    if( !input.read( &buffer[0], size) )
      throw runtime_error( "Failed Read" );
    
    const char *zs = "ZZZZ";
    const char *start = &buffer[0];
    const char *end = start + size;
    
    const string asc = "ASC";
    const string asd = "ASD";
    
    //We have to first identify the byte each record starts at before decoding
    //  any of them, since we need to know the record size while decoding the
    //  record (could probably be clever about avoiding this, but whatever).
    vector<size_t> recordstarts;
    for( size_t i = 0; i < (size-1-2*256);
        i = (std::search(start+i+1, end, zs, zs+4) - start) )
    {
      const char *data = start + i;
      
      if( data[0]!='Z' || data[1]!='Z' || data[2]!='Z' || data[3]!='Z' )
        continue;
      
      //Record type Type: A=Spectrum, S=Survey, C=CZT, D=Dose
      const bool is135v2 = (asc.find(data[8])!=string::npos);
      const bool is135v1 = (asc.find(data[4])!=string::npos);
      const bool is130v0 = (asd.find(data[6])!=string::npos);
      
      const bool dtSpectrum = (is135v2 && (data[8] == 'A'))
      || (is135v1 && (data[4] == 'A'))
      || (is130v0 && (data[6] == 'A'));
      const bool dtCzt = (is135v2 && (data[8] == 'C'))
      || (is135v1 && (data[4] == 'C'));
      
      const size_t record_size = recordstarts.size() ? i - recordstarts.back() : 0;
      
      const size_t num_channels = (is130v0 ? 256 : 1024);
      const bool valid = (is135v2 || is135v1 || is130v0)
                         && (dtSpectrum || dtCzt)
                         && ((record_size > num_channels*2) || recordstarts.empty());
      if( valid )
      {
        recordstarts.push_back( i );
        i += 512;
      }//if( valid )
    }//for( loop over file and identify starts of records )
    
    //Map number of channels and polynomial coefficents to a EnergyCalibration object for re-use
    //  for the different Measurements
    map<pair<uint16_t,vector<float>>,shared_ptr<const EnergyCalibration>> energy_cals;
    
    auto set_energy_cal = [&energy_cals]( const uint16_t nchannel,
                                          const vector<float> &coeffs,
                                          shared_ptr<Measurement> meas )
    {
      auto energy_cal_pos = energy_cals.find({nchannel,coeffs});
      if( energy_cal_pos == std::end(energy_cals) )
      {
        auto energy_cal = make_shared<EnergyCalibration>();
        try
        {
          energy_cal->set_polynomial( nchannel, coeffs, {} );
        }catch( std::exception &e )
        {
          //This warning wont make it to subsequent Measurements that re-use this calibration, but
          // oh well for the moment
          if( coeffs != vector<float>{0.0f,0.0f,0.0f} )
            meas->parse_warnings_.push_back( "Provided energy calibration invalid: "
                                              + string(e.what()) );
        }
        energy_cal_pos = energy_cals.insert( {{nchannel, coeffs}, energy_cal} ).first;
      }//if( we have to create the energy calibration )
        
      meas->energy_calibration_ = energy_cal_pos->second;
    };//set_energy_cal lamda
    
    
    
    
    for( size_t j = 0; j < recordstarts.size(); ++j )
    {
      const char *data = start + recordstarts[j];
      
      const bool is135v2 = (asc.find(data[8])!=string::npos);
      const bool is135v1 = (asc.find(data[4])!=string::npos);
      const bool is130v0 = (asd.find(data[6])!=string::npos);
      
      const bool dtCzt = (is135v2 && (data[8] == 'C'))
      || (is135v1 && (data[4] == 'C'));
      
      const bool found1350 = (data[4] == '1') && (data[5] == '3') && (data[6] == '5');
      const bool found1024 = (data[5] == 0x0) && (data[6] == 0x4);
      
      if( is135v2 && !found1350 )
      {
        parse_warnings_.emplace_back( "The header is missing the string \"135\" at offsets 5, 6, and 7" );
//#if(PERFORM_DEVELOPER_CHECKS)
//        log_developer_error( __func__, "The header is missing the string \"135\" at offsets 5, 6, and 7" );
//#endif
      }//if( is135v2 && !found1350 )
        
      if( is135v1 && !found1024 )
      {
        parse_warnings_.emplace_back( "The header is missing the 16-bit integer \"1024\" at offsets 6 and 7'" );
#if(PERFORM_DEVELOPER_CHECKS)
        log_developer_error( __func__, "The header is missing the 16-bit integer \"1024\" at offsets 6 and 7'" );
#endif
      }//if( is135v1 && !found1024 )
      
      const size_t record_size = ( (j==(recordstarts.size()-1))
                                  ? (size - recordstarts[j])
                                  : (recordstarts[j+1] - recordstarts[j]));
      
      std::shared_ptr<Measurement> meas( new Measurement() );
      
      //try to get the date/time
      const char *datepos = data + 7;
      if( is130v0 )
        datepos = data + 7;
      else if( is135v1 )
      {
        if( record_size == 2099 )
          datepos = data + 7;
        else if( record_size == 2124 )
          datepos = data + 13;
        else if( record_size == 2127 )
          datepos = data + 13;
      }else //if( is135v2 )
        datepos = data + 13;
      
      uint8_t timeinfo[6]; //= {year, month, day, hour, minutes, seconds}
      for( size_t i = 0; i < 6; ++i )
        timeinfo[i] = 10*(((datepos[i]) & 0xF0) >> 4) + ((datepos[i]) & 0xF);
      
      try
      {
        const boost::gregorian::date the_date( 2000 + timeinfo[0], timeinfo[1], timeinfo[2]);
        const boost::posix_time::time_duration the_time( timeinfo[3], timeinfo[4], timeinfo[5]);
        meas->start_time_ = boost::posix_time::ptime( the_date, the_time );
        
        //if( is130v0 ){
        //  float battery_voltage = (10*(((datepos[7]) & 0xF0) >> 4) + ((datepos[7]) & 0xF)) / 10.0f;
        //}
      }catch(...)
      {
      }
      
      if( meas->start_time_.is_special() )
      {
        //We're desperate here - lets search the entire header area.
        for( size_t i = 0; i < 75; ++i )
        {
          datepos = data + 4 + i;
          for( size_t i = 0; i < 6; ++i )
            timeinfo[i] = 10*(((datepos[i]) & 0xF0) >> 4) + ((datepos[i]) & 0xF);
          try
          {
            const boost::gregorian::date the_date( 2000 + timeinfo[0], timeinfo[1], timeinfo[2]);
            const boost::posix_time::time_duration the_time( timeinfo[3], timeinfo[4], timeinfo[5]);
            meas->start_time_ = boost::posix_time::ptime( the_date, the_time );
            break;
          }catch(...)
          {
          }
        }//for( size_t i = 0; i < 75; ++i )
      }//if( meas->start_time_.is_special() )
      
      //neutrn info
      meas->contained_neutron_ = is135v2;
      if( is135v2 )
      {
        uint16_t neutsum;
        memcpy( &neutsum, data+36, 2 );
        meas->neutron_counts_sum_ = neutsum;
        meas->neutron_counts_.resize( 1, static_cast<float>(neutsum) );
      }//if( is135v2 )
      
      //Serial number
      uint16_t serialnum = 0, version = 0;
      if( is135v2 )
      {
        memcpy( &serialnum, data+40, 2 );
        memcpy( &version, data+42, 2 );
      }else if( is130v0 )
      {
        memcpy( &serialnum, data+27, 2 );
        memcpy( &version, data+29, 2 );
      }else if( is135v1 )
      {
        memcpy( &serialnum, data+28, 2 );
        memcpy( &version, data+30, 2 );
      }
      
      
      
      if( serialnum != 0 )
      {
        snprintf( charbuff, sizeof(charbuff), "%i", static_cast<int>(serialnum) );
        instrument_id_ = charbuff;
      }//if( serialnum != 0 )
      
      
      size_t calpos = 0;
      if( is130v0 || (is135v1 && record_size==2099) )
        calpos = 31;
      else if( is135v2 || (is135v1 && (record_size==2124)) )
        calpos = 44;
      
      size_t datapos;
      uint16_t nchannels;
      
      if( is130v0 )
      {
        uint16_t real_time;
        uint32_t live_time_thousanths;
        nchannels = 256;
        //        memcpy( &nchannels, data+5, 2 );
        memcpy( &real_time, data+14, 2 );
        memcpy( &live_time_thousanths, data+47, 4 );  //channels 1,2
        datapos = 47;
        
        const float lt = live_time_thousanths/1000.0f;
        const float rt = static_cast<float>( real_time );
        meas->real_time_ = std::max( rt, lt );
        meas->live_time_ = std::min( rt, lt );
        
        meas->contained_neutron_ = false;
      }else if( is135v1 )
      {
        memcpy( &nchannels, data+5, 2 );
        
        uint32_t live_time_thousanths;
        if( record_size == 2099 )
        {
          memcpy( &live_time_thousanths, data+50, 4 );
          datapos = 50;
        }else  // if( record_size == 2124 )
        {
          memcpy( &live_time_thousanths, data+75, 4 );
          datapos = 75;
        }
        
        meas->contained_neutron_ = false;
        meas->live_time_ = meas->real_time_ = live_time_thousanths / 1000.0f;
      }else //if( is135v2 )
      {
        /*
         char dose_unit = data[31];
         uint32_t dose;
         memcpy( &dose, data + 32, sizeof(dose) );
         
         switch( dose_unit )
         {
         case 'R': case 0:   //not sure how works...
         case 'G': case 1:
         case 'S': case 2:
         default:
         break;
         }
         */
        
        //uint16_t pilup_pulses;
        //memcpy( &pilup_pulses, data + 38, sizeof(pilup_pulses) );
        
        uint16_t nneutrons;
        uint32_t real_time_thousanths, live_time_thousanths;
        memcpy( &nchannels, data + 19, sizeof(uint16_t) );
        memcpy( &real_time_thousanths, data+21, sizeof(uint32_t) );
        memcpy( &nneutrons, data+36, sizeof(uint16_t) );
        memcpy( &live_time_thousanths, data+75, sizeof(uint32_t) ); // live time in mSec (channels 1,2)
        datapos = 75;
        
        
        const float lt = live_time_thousanths / 1000.0f;
        const float rt = real_time_thousanths / 1000.0f;
        meas->real_time_ = std::max( rt, lt );
        meas->live_time_ = std::min( rt, lt );
        
        meas->contained_neutron_ = true;
        meas->neutron_counts_sum_ = static_cast<double>( nneutrons );
        meas->neutron_counts_.resize( 1, static_cast<float>(nneutrons) );
      }//if( is130v0 ) / ....
      
      const float month = 30.0f*24.0f*60.0f*60.0f;
      if( meas->live_time_ < 0.1f || meas->live_time_ > month )
        meas->live_time_ = 0.0f;
      if( meas->real_time_ < 0.1f || meas->real_time_ > month )
        meas->real_time_ = 0.0f;
      
      if( is130v0 )
        meas->detector_description_ = "";
      else if( is135v1 )
        meas->detector_description_ = "";
      
      if( is130v0 )
        meas->detector_description_ = "GR-130";
      else if( is135v1 )
        meas->detector_description_ = "GR-135 v1";  // {Serial #'+SerialNumber+', '}  {Fnot invariant for same file}
      else if( is135v2 )
        meas->detector_description_ = "GR-135 v2";
      if( dtCzt )
        meas->detector_description_ += ", CZT";
      
      snprintf( charbuff, sizeof(charbuff),
               ", RecordSize: %i bytes", int(record_size) );
      meas->title_ += meas->detector_description_ + charbuff;
      
      meas->detector_number_ = dtCzt;
      meas->sample_number_ = static_cast<int>( j+1 );
      
      const size_t expected_num_channels = (is130v0 ? 256 : 1024);
      if( expected_num_channels != nchannels )
      {
        if( nchannels != 256 )
          nchannels = expected_num_channels;
        
        string msg = "The expected and read number of channels didnt agree";
#if(PERFORM_DEVELOPER_CHECKS)
        log_developer_error( __func__, msg.c_str() );
#endif
        if( std::find( std::begin(parse_warnings_), std::end(parse_warnings_), msg) == std::end(parse_warnings_) )
          parse_warnings_.emplace_back( std::move(msg) );
      }
      
      vector<float> calcoeffs = {0.0f, 0.0f, 0.0f};
      if( calpos )
        memcpy( &(calcoeffs[0]), data + calpos, 3*4 );
      set_energy_cal( nchannels, calcoeffs, meas );
      
      
      auto gamma_counts = std::make_shared<vector<float> >( nchannels, 0.0f );
      meas->gamma_counts_ = gamma_counts;
      vector<float> &channel_data = *gamma_counts.get();
      channel_data[0] = channel_data[1] = 0.0f;
      channel_data[channel_data.size()-1] = 0.0f;
      
      if( nchannels >= 1 )
      {
        for( uint16_t i = 2; i < (nchannels-1); ++i )
        {
          uint16_t counts = 0;
          const auto chanpos = data + datapos + 2*i;
          if( (chanpos+2) < end )  //probably not needed, but jic
            memcpy( &counts, chanpos, 2 );
          const float val = static_cast<float>( counts );
          channel_data[i] = val;
          meas->gamma_count_sum_ += val;
        }//for( size_t i = 0; i < nchannels; ++i )
      }
      
      if( is135v1 && (meas->energy_calibration_->type() != EnergyCalType::Polynomial) )
      {
        for( size_t i = 0; i < (record_size - 1024*2); ++i )
        {
          vector<float> cal = {0.0f, 0.0f, 0.0f};
          memcpy( &(cal[0]), data+1, 12 );
          
          set_energy_cal( nchannels, cal, meas );
          
          if( meas->energy_calibration_->type() != EnergyCalType::InvalidEquationType )
          {
            string msg = "Irregular GR energy calibration apparently found.";
#if(PERFORM_DEVELOPER_CHECKS)
            log_developer_error( __func__, msg.c_str() );
#endif
            if( std::find( std::begin(parse_warnings_), std::end(parse_warnings_), msg) == std::end(parse_warnings_) )
              parse_warnings_.emplace_back( std::move(msg) );
            break;
          }//if( valid )
        }//for( size_t i = 0; i < (record_size - 1024*2); ++i )
      }//if( is135v1 && (meas->energy_calibration_ != polynomial) )
      
      if( meas->energy_calibration_->type() != EnergyCalType::Polynomial )
      {
        //We didnt find a (valid) energy calibration, so we will use a default one based off the
        //  model
        bool usingGuessedVal = false;
        vector<float> default_coefs = { 0.0f, 0.0f, 0.0f };
        
        if( is135v1 || is135v2 )
        {
          if( dtCzt )
          {
            default_coefs[1] = (122.06f - 14.4f)/(126.0f - 10.0f);
            default_coefs[0] = 14.4f - default_coefs[1]*10.0f;
            default_coefs[2] = 0.0f;
            usingGuessedVal = true;
            
            string msg = "Default GR135 energy calibration for CZT has been assumed.";
#if(PERFORM_DEVELOPER_CHECKS)
            log_developer_error( __func__, msg.c_str() );
#endif
            if( std::find( std::begin(parse_warnings_), std::end(parse_warnings_), msg) == std::end(parse_warnings_) )
              parse_warnings_.emplace_back( std::move(msg) );
          }else
          {
            default_coefs[0] = 0.11533801f;
            default_coefs[1] = 2.8760445f;
            default_coefs[2] = 0.0006023737f;
            usingGuessedVal = true;
            
            string msg = "Default GR135 energy calibration for NaI has been assumed.";
#if(PERFORM_DEVELOPER_CHECKS)
            log_developer_error( __func__, msg.c_str() );
#endif
            if( std::find( std::begin(parse_warnings_), std::end(parse_warnings_), msg) == std::end(parse_warnings_) )
              parse_warnings_.emplace_back( std::move(msg) );
          }
        }else if( is130v0 )
        {
          const float nbin = static_cast<float>( nchannels );
          
          default_coefs[0] = -21.84f;
          default_coefs[1] = 3111.04f/(nbin+1.0f);
          default_coefs[2] = 432.84f/(nbin+1.0f)/(nbin+1.0f);
          usingGuessedVal = true;
          
          string msg = "Default GR130 energy calibration for NaI has been assumed.";
#if(PERFORM_DEVELOPER_CHECKS)
          log_developer_error( __func__, msg.c_str() );
#endif
          if( std::find( std::begin(parse_warnings_), std::end(parse_warnings_), msg) == std::end(parse_warnings_) )
            parse_warnings_.emplace_back( std::move(msg) );
        }
        
        if( usingGuessedVal )
          set_energy_cal( nchannels, default_coefs, meas );
      }//if( meas->energy_calibration_->type() != EnergyCalType::Polynomial )
      
      if( j == 0 )
      {
        manufacturer_ = "Exploranium";
        instrument_model_ = is130v0 ? "GR130" : "GR135";
        instrument_type_ = "Radionuclide Identifier";
        if( !is130v0 )
          detector_type_ = SpecUtils::DetectorType::Exploranium;
      }//if( j == 0 )
      
      measurements_.push_back( meas );
    }//for( size_t j = 0; j < recordstarts.size(); ++j )
    
    cleanup_after_load();
  }catch( std::exception & )
  {
    input.clear();
    input.seekg( orig_pos, ios::beg );
    
    reset();
    return false;
  }//try / catch
  
  const bool success = !measurements_.empty();
  
  if( !success )
    reset();
  
  return success;
}//void load_from_binary_exploranium()
  
  
bool SpecFile::write_binary_exploranium_gr130v0( std::ostream &output ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const size_t nmeas = measurements_.size();
  
  try
  {
    int nwrote = 0;
    
    for( size_t measn = 0; measn < nmeas; ++measn )
    {
      const std::shared_ptr<const Measurement> origmeas = measurements_[measn];
      if( !origmeas || !origmeas->gamma_counts_ || origmeas->gamma_counts_->size() < 5 )
        continue;
      
      Measurement meas( *origmeas );
      
      const size_t noutchannel = 256;
      const size_t ninputchannel = meas.gamma_counts_->size();
      
      if( (ninputchannel > noutchannel) && ((ninputchannel % noutchannel)!=0) )
      {
        const size_t ncombine = ninputchannel / noutchannel;
        meas.combine_gamma_channels( ncombine );
      }else if( ninputchannel > noutchannel )
      {
        const float min_e = meas.gamma_energy_min();
        const float delta_e = meas.gamma_energy_max() - min_e;
        
        try
        {
          auto newcal = make_shared<EnergyCalibration>();
          newcal->set_polynomial( noutchannel, {min_e,delta_e/noutchannel}, {} );
          meas.rebin( newcal );
        }catch(...)
        {
          
        }
      }//
      
      //Lets write to a buffer before writing to the stream to make things a
      //  little easier in terms of keeping track of position.
      char buffer[561];
      memset( buffer, 0, sizeof(buffer) );
      
      memcpy( buffer + 0, "ZZZZ", 4 );
      
      const uint16_t record_length = 560; //total length of the record
      memcpy( buffer + 4, &record_length, 2 );
      buffer[6] = 'A';
      
      if( !meas.start_time_.is_special() && meas.start_time_.date().year() > 2000 )
      {
        buffer[7] = static_cast<uint8_t>(meas.start_time_.date().year() - 2000);
        buffer[8] = static_cast<uint8_t>(meas.start_time_.date().month());
        buffer[9] = static_cast<uint8_t>(meas.start_time_.date().day());
        buffer[10] = static_cast<uint8_t>(meas.start_time_.time_of_day().hours());
        buffer[11] = static_cast<uint8_t>(meas.start_time_.time_of_day().minutes());
        buffer[12] = static_cast<uint8_t>(meas.start_time_.time_of_day().seconds());
        
        for( size_t i = 7; i < 13; ++i )
          buffer[i] = (((buffer[i] / 10) << 4) | (buffer[i] % 10));
      }//if( !meas.start_time_.is_special() )
      
      //battery volt*10 in BCD
      //buffer[13] = uint8_t(3.3*10);
      //buffer[13] = (((buffer[13] / 10) << 4) | (buffer[13] % 10));
      
      const uint16_t real_time = static_cast<uint16_t>( floor(meas.real_time_ + 0.5f) );
      memcpy( buffer + 14, &real_time, 2 );
      buffer[16] = (meas.gamma_energy_max() > 2000.0f ? 1 : 0); //coarse gain (0=1.5MeV, 1=3.0MeV)
      buffer[17] = 0; //Fgain: fine gain (0-255)
      //buffer[18] = buffer[19] = 0; //unsigned int    Peak    stab. peak position in channels *10
      //buffer[20] = buffer[21] = 0; //unsigned int    FW    stab. peak resolution *10 %
      buffer[22] = 'R'; //dose meter mode (R, G, S) {rem, grey, sievert}?
      //buffer[23] = buffer[24] = buffer[25] = buffer[26] = 0;//geiger    GM tube accumulated dose
      
      uint16_t serialint;
      if( !(stringstream(instrument_id_) >> serialint) )
        serialint = 0;
      memcpy( buffer + 27, &serialint, 2 );
      
      uint16_t softwareversion = 301;
      memcpy( buffer + 29, &softwareversion, 2 );
      
      buffer[31] = 'C'; //modification   "C" Customs, "G" Geological
      
      //buffer[32] throughw buffer[46] 15 bytes spare
      
      const uint32_t ltime = static_cast<uint32_t>( floor( 1000.0f * meas.live_time() + 0.5f) );
      memcpy( buffer + 47, &ltime, 4 );
      
      const vector<float> &gammcounts = *meas.gamma_counts_;
      vector<uint16_t> channelcounts( gammcounts.size(), 0 );
      for( size_t i = 0; i < gammcounts.size(); ++i )
        channelcounts[i] = static_cast<uint16_t>( static_cast<uint32_t>(gammcounts[i]) );  //ToDo: check if we want the wrap-around behaviour, or just take uint16_t
      
      if( gammcounts.size() <= 256 )
        memcpy( buffer + 51, &channelcounts[2], (2*gammcounts.size() - 6) );
      else
        memcpy( buffer + 51, &channelcounts[2], 506 );
      
      const uint16_t cosmicchanel = 0; //cosmic    cosmic channel (channel 256)
      memcpy( buffer + 557, &cosmicchanel, 2 );
      
      buffer[559] = 0; //CHSUM    check sum
      
      output.write( buffer, 560 );
      
      ++nwrote;
    }//for( size_t measn = 0; measn < nmeas; ++measn )
    
    if( !nwrote )
      throw runtime_error("Failed to write any spectrums");
  }catch( std::exception & )
  {
    return false;
  }
  
  return true;
}//bool write_binary_exploranium_gr130v0(...)
  
  
  
bool SpecFile::write_binary_exploranium_gr135v2( std::ostream &output ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const size_t nmeas = measurements_.size();
  
  try
  {
    int nwrote = 0;
    
    for( size_t measn = 0; measn < nmeas; ++measn )
    {
      const std::shared_ptr<const Measurement> origmeas = measurements_[measn];
      if( !origmeas || !origmeas->gamma_counts_ || origmeas->gamma_counts_->size() < 5 )
        continue;
      
      Measurement meas( *origmeas );
      
      const uint16_t noutchannel = 1024;
      const size_t ninputchannel = meas.gamma_counts_->size();
      
      if( (ninputchannel > noutchannel) && ((ninputchannel % noutchannel)!=0) )
      {
        const size_t ncombine = meas.gamma_counts_->size() / noutchannel;
        meas.combine_gamma_channels( ncombine );
      }else if( ninputchannel > noutchannel )
      {
        const float min_e = meas.gamma_energy_min();
        const float max_e = meas.gamma_energy_max();
        const float delta_e = max_e - min_e;
        if( max_e <= min_e )
        {
          meas.truncate_gamma_channels( 0, noutchannel, true );
        }else
        {
          auto newcal = make_shared<EnergyCalibration>();
          newcal->set_polynomial( noutchannel, {min_e, delta_e/noutchannel}, {} );
          meas.rebin( newcal );
        }
      }//
      
      
      //Lets write to a buffer before writing to the stream to make things a
      //  little easier in terms of keeping track of position.
      char buffer[76+2*noutchannel];
      memset( buffer, 0, sizeof(buffer) );
      
      memcpy( buffer + 0, "ZZZZ", 4 );
      memcpy( buffer + 4, "1350", 4 );
      
      const bool is_czt = SpecUtils::icontains(meas.detector_name_, "CZT");
      buffer[8] = (is_czt ? 'C' : 'A');
      
      
      //10-13    unsigned long    sequence number  number of spectra ever measured
      //  *this is not true to the input file*
      const uint32_t samplnum = meas.sample_number_;
      memcpy( buffer + 9, &samplnum, sizeof(samplnum) );
      
      if( !meas.start_time_.is_special() && meas.start_time_.date().year() > 2000 )
      {
        buffer[13] = static_cast<uint8_t>(meas.start_time_.date().year() - 2000);
        buffer[14] = static_cast<uint8_t>(meas.start_time_.date().month());
        buffer[15] = static_cast<uint8_t>(meas.start_time_.date().day());
        buffer[16] = static_cast<uint8_t>(meas.start_time_.time_of_day().hours());
        buffer[17] = static_cast<uint8_t>(meas.start_time_.time_of_day().minutes());
        buffer[18] = static_cast<uint8_t>(meas.start_time_.time_of_day().seconds());
        
        for( size_t i = 13; i < 19; ++i )
          buffer[i] = (((buffer[i] / 10) << 4) | (buffer[i] % 10));
      }//if( !meas.start_time_.is_special() )
      
      memcpy( buffer + 19, &noutchannel, sizeof(noutchannel) );
      
      const uint32_t real_time_thousanths = static_cast<uint32_t>( floor(1000*meas.real_time_ + 0.5f) );
      memcpy( buffer + 21, &real_time_thousanths, 4 );
      
      //26,27    unsigned int    gain      gain ( 0 - 1023)
      //28,29    unsigned int    Peak      stab. peak position in channels * 10
      //30,31    unsigned int    FW      stab. peak resolution * 10 %
      //32    unsigned char    unit      dose meter unit ('R', 'G', 'S')
      //33-36    unsigned long    geiger      GM tube accumulated dose
      
      const uint16_t nneutron = static_cast<uint16_t>( floor(meas.neutron_counts_sum() + 0.5) );
      memcpy( buffer + 36, &nneutron, 2 );
      
      //39,40    unsigned int    pileup      pileup pulses
      
      uint16_t serialint;
      if( !(stringstream(instrument_id_) >> serialint) )
        serialint = 0;
      memcpy( buffer + 40, &serialint, 2 );
      
      uint16_t softwareversion = 201;
      memcpy( buffer + 42, &softwareversion, 2 );
      
      vector<float> calcoeffs;
      assert( meas.energy_calibration_ );
      switch( meas.energy_calibration_->type() )
      {
        case EnergyCalType::Polynomial:
        case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
          calcoeffs = meas.energy_calibration_->coefficients();
          break;
        case EnergyCalType::FullRangeFraction:
          calcoeffs = fullrangefraction_coef_to_polynomial( meas.energy_calibration_->coefficients(),
                                                            meas.gamma_counts_->size() );
          break;
        case EnergyCalType::LowerChannelEdge:
        case EnergyCalType::InvalidEquationType:
          break;
      }//switch( meas.energy_calibration_->type() )
      
      if( calcoeffs.size() )
        memcpy( buffer + 44, &(calcoeffs[0]), 4*std::max(calcoeffs.size(),size_t(3)) );
      
      //57    char      temp[0]      display temperature
      //58    char      temp[1]      battery temperature
      //59    char      temp[2]      detector temperature
      //60-75    char      spare      16 bytes
      
      //live time isnt in the spec, but it was found here in the parsing code.
      const uint32_t live_time_thousanths = static_cast<uint32_t>( floor(1000*meas.live_time_ + 0.5f) );
      memcpy( buffer + 75, &live_time_thousanths, sizeof(uint32_t) ); // live time in mSec (channels 1,2)
      
      const vector<float> &counts = *meas.gamma_counts_;
      vector<uint16_t> channelcounts( counts.size(), 0 );
      for( size_t i = 0; i < counts.size(); ++i )
        channelcounts[i] = static_cast<uint16_t>( min( counts[i], 65535.0f ) );
      
      if( counts.size() <= noutchannel )
        memcpy( buffer + 79, &channelcounts[2], 2*counts.size() - 4 );
      else
        memcpy( buffer + 79, &channelcounts[2], 2*(noutchannel-2) );
      
      //2*nch+76  unsigned char    CHSUM    check sum
      
      output.write( buffer, 76+2*noutchannel );
      
      ++nwrote;
    }//for( size_t measn = 0; measn < nmeas; ++measn )
    
    if( !nwrote )
      throw runtime_error("Failed to write any spectrums");
  }catch( std::exception & )
  {
    return false;
  }
  
  return true;
}//bool write_binary_exploranium_gr135v2(...)

}//namespace SpecUtils



