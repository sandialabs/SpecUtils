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
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;


namespace SpecUtils
{
bool SpecFile::load_chn_file( const std::string &filename )
{
  reset();
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
#ifdef _WIN32
  ifstream file( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream file( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  if( !file.is_open() )
    return false;
  
  uint8_t firstbyte;
  file.read( (char *) (&firstbyte), 1 );
  file.seekg( 0, ios::beg );
  
  if( firstbyte != uint8_t(255) )
  {
    //    cerr << "CHN file '" << filename << "'does not have expected first byte of"
    //         << " 255 firstbyte=" << int(firstbyte) << endl;
    return false;
  }//if( wrong first byte )
  
  const bool loaded = load_from_chn( file );
  
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_chn_file( const std::string &filename )

  
bool SpecFile::load_from_chn( std::istream &input )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  input.seekg( 0, ios::end );
  const istream::pos_type eof_pos = input.tellg();
  input.seekg( orig_pos, ios::beg );
  const size_t size = static_cast<size_t>( 0 + eof_pos - orig_pos );
  
  try
  {
    if( size < 548 )  //128 channel, plus 32 byte header
      throw runtime_error( "File to small to be a CHN file." );
    
    const size_t header_size = 32;
    
    vector<char> buffer( header_size );
    if( !input.read( &buffer[0], header_size ) )
      throw runtime_error( "SpecFile::load_from_chn(...): Error reading header from file stream" );
    
    int16_t firstval;
    memcpy( &firstval, &(buffer[0]), sizeof(uint16_t) );
    
    if( firstval != -1 )
      throw runtime_error( "Invalid first value" );
    
    uint16_t firstchannel, numchannels;
    memcpy( &firstchannel, &(buffer[28]), sizeof(uint16_t) );
    memcpy( &numchannels, &(buffer[30]), sizeof(uint16_t) );
    
    if( numchannels == 0 )
    {
      numchannels = (size - header_size - 512) / 4;
      const bool isPowerOfTwo = !(numchannels & (numchannels - 1));
      if( !isPowerOfTwo || numchannels < 128 || numchannels > 32768 )
        throw runtime_error( "Invalid number of channels" );
    }//if( numchannels == 0 )
    
    //    const bool isPowerOfTwo = ((numchannels != 0) && !(numchannels & (numchannels - 1)));
    //    const bool evenNumChnnel = ((numchannels%2)==0) || (numchannels==16383);
    //    if( !evenNumChnnel || numchannels<2 ) // || !isPowerOfTwo
    //      throw runtime_error( "CHN file with unexpected number of data channels: " + std::to_string(numchannels) );
    
#if(PERFORM_DEVELOPER_CHECKS)
    if( firstchannel != 0 )
    {
      //if( firstchannel==1 ), we should shift the bin contents over by 1 to the left
      char buffer[256];
      snprintf( buffer, sizeof(buffer),
               "Found a first channel offset of %i", int(firstchannel) );
      log_developer_error( __func__, buffer );
      if( firstchannel != 1 )
        cerr << "firstchannel==" << firstchannel << endl;  //I've never ran into this, at least in the corpus of files I have
    }
#endif
    
    if( size < (header_size+4*numchannels) )
      throw runtime_error( "CHN Filesize smaller than expected" );
    
    //first non-zero spectrum counts at channel 120, so I assume first channel data
    //is in channel 32, 36, or 40 (PeakEasy has 22 leading zeros, Cambio 21)
    
    const string monthstr(  &(buffer[18]), &(buffer[18]) + 3 );
    const string daystr(    &(buffer[16]), &(buffer[16]) + 2 );
    const string yearstr(   &(buffer[21]), &(buffer[21]) + 2 );
    const bool isNot2k = (buffer[23]=='0');  //I think PeakEasy defaults to putting a '\0' in this spot, so well assume year > 2k unless actually properly noted
    const string hourstr(   &(buffer[24]), &(buffer[24]) + 2 );
    const string minutestr( &(buffer[26]), &(buffer[26]) + 2 );
    const string secondstr( &(buffer[6]), &(buffer[6]) + 2 );
    
    uint32_t realTimeTimesFifty, liveTimeTimesFifty;
    memcpy( &realTimeTimesFifty, &(buffer[8]), sizeof(uint32_t) );
    memcpy( &liveTimeTimesFifty, &(buffer[12]), sizeof(uint32_t) );
    
    const float realTime = realTimeTimesFifty / 50.0f;
    const float liveTime = liveTimeTimesFifty / 50.0f;
    
    double gamma_sum = 0.0;
    auto channel_data = std::make_shared<vector<float>>(numchannels);
    
    vector<float> &channel_data_ref = *channel_data;
    
    if( numchannels > 2 )
    {
      vector<uint32_t> int_channel_data( numchannels );
      if( !input.read( (char *)(&int_channel_data[0]), 4*numchannels ) )
        throw runtime_error( "SpecFile::load_from_chn(...): Error reading numchannels from file stream" );
      
      int_channel_data[0] = 0;
      int_channel_data[1] = 0;
      int_channel_data[numchannels-2] = 0;
      int_channel_data[numchannels-1] = 0;
      
      for( uint16_t i = 0; i < numchannels; ++i )
      {
        const float val = static_cast<float>( int_channel_data[i] );
        gamma_sum += val;
        channel_data_ref[i] = val;
      }//for( size_t i = 0; i < numchannels; ++i )
    }//if( numchannels > 2 )
    
    const istream::pos_type current_pos = input.tellg();
    const size_t bytes_left = static_cast<size_t>( 0 + eof_pos - current_pos );
    
    int16_t chntype = 0;
    if( bytes_left > 1 )
    {
      buffer.resize( bytes_left );
      if( !input.read( &buffer[0], bytes_left ) )
        throw runtime_error( "SpecFile::load_from_chn(...):  Error reading remaining file contents from file stream" );
      
      memcpy( &chntype, &(buffer[0]), sizeof(int16_t) );
      
#if(PERFORM_DEVELOPER_CHECKS)
      //Files such as ref985OS89O82 can have value chntype=-1
      if( chntype != -102 && chntype != -101 && chntype != 1 )
      {
        stringstream msg;
        msg << "Found a chntype with unexpected value: " << chntype;
        log_developer_error( __func__, msg.str().c_str() );
      }
#endif
    }//if( bytes_left > 1 )
    
    vector<float> calibcoefs{ 0.0f, 0.0f, 0.0f };
    if( chntype == -102 && bytes_left >= 16 )
      memcpy( &(calibcoefs[0]), &(buffer[4]), 3*sizeof(float) );
    else if( bytes_left >= 12 )
      memcpy( &(calibcoefs[0]), &(buffer[4]), 2*sizeof(float) );
    //      float FWHM_Zero_32767 = *(float *)(&(buffer[16]));
    //      float FWHM Slope = *(float *)(&(buffer[20]));
    
    //at 256 we have 1 byte for DetDescLength, and then 63 bytes for DetDesc
    string detdesc;
    if( bytes_left >= 257 )
    {
      uint8_t DetDescLength;
      memcpy( &DetDescLength, &(buffer[256]), 1 );
      const size_t det_desc_index = 257;
      const size_t enddet_desc = det_desc_index + DetDescLength;
      if( DetDescLength && enddet_desc < bytes_left && DetDescLength < 64 )
      {
        detdesc = string( &(buffer[det_desc_index]), &(buffer[enddet_desc]) );
        trim( detdesc );
      }//if( title_index < bytes_left )
    }//if( bytes_left >= 257 )
    
    
    string title;
    if( bytes_left >= 322 )
    {
      uint8_t SampleDescLength;
      memcpy( &SampleDescLength, &(buffer[320]), 1 );
      const size_t title_index = 321;
      const size_t endtitle = title_index + SampleDescLength;
      if( SampleDescLength && endtitle < bytes_left && SampleDescLength < 64 )
      {
        title = string( &(buffer[title_index]), &(buffer[endtitle]) );
        trim( title );
      }//if( title_index < bytes_left )
    }//if( bytes_left >= 322 )
    
    std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
    meas->live_time_ = liveTime;
    meas->real_time_ = realTime;
    meas->gamma_count_sum_ = gamma_sum;
    
    if( (fabs(calibcoefs[0])<1.0E-12 && fabs(calibcoefs[1])<1.0E-12)
       || (fabs(calibcoefs[0])<1.0E-12 && fabs(calibcoefs[1]-1.0)<1.0E-8) )
    {
      //
    }else if( calibcoefs[1] > 1000 && calibcoefs[1] < 16000
             && fabs(calibcoefs[0]) < 100 )
    {
      //This is a guess at how to detect when FWHM is specified in the CHN file;
      //  probably will fail to detect it sometimes, and falsely detect others.
      if( fabs(calibcoefs[2]) >= 0.25*calibcoefs[1] )
        calibcoefs[2] = 0.0f;
      
      try
      {
        auto newcal = make_shared<EnergyCalibration>();
        newcal->set_full_range_fraction( channel_data_ref.size(), calibcoefs, {} );
        meas->energy_calibration_ = newcal;
      }catch( std::exception &e )
      {
        meas->parse_warnings_.push_back( "Invalid FRF energy cal: " + string(e.what()) );
      }
    }else if( calibcoefs[1] < 1000 )
    {
      try
      {
        auto newcal = make_shared<EnergyCalibration>();
        newcal->set_polynomial( channel_data_ref.size(), calibcoefs, {} );
        meas->energy_calibration_ = newcal;
      }catch( std::exception &e )
      {
        meas->parse_warnings_.push_back( "Invalid polynomial energy cal: " + string(e.what()) );
      }
    }else
    {
      if( (calibcoefs[0] != 0.0f) || (calibcoefs[1] != 0.0f) )
      {
        meas->parse_warnings_.push_back( "Could not identify CHN energy calibration with pars {"
                                        + to_string(calibcoefs[0]) + ", " + to_string(calibcoefs[1])
                                        + ", " + std::to_string(calibcoefs[2]) + "}." );
      }
    }//if( calibcoefs[0]==0.0 && calibcoefs[1]==1.0 )
    
    
    if( channel_data && channel_data->size() )
      meas->gamma_counts_ = channel_data;
    
    const string datestr = daystr + "-" + monthstr
                           + (isNot2k ? "-19" : "-20") + yearstr
                           + " " + hourstr + ":" + minutestr + ":" + secondstr;
    
    meas->start_time_ = time_from_string( datestr.c_str() );
    //cerr << "DateStr=" << datestr << " --> " << SpecUtils::to_iso_string(meas->start_time_) << endl;
    if( title.size() )
      meas->title_ = title;
    
    if( detdesc.size() )
      meas->remarks_.push_back( "Detector Description: " + detdesc );
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
  }catch( std::runtime_error & )
  {
    input.clear();
    input.seekg( orig_pos, ios::beg );
    //cerr << SRC_LOCATION << "\n\tCaught:" << e.what() << endl;
    reset();
    return false;
  }//try / catch
  
  
  return true;
}//bool load_from_chn( std::istream &input )

  
bool SpecFile::write_integer_chn( ostream &ostr, set<int> sample_nums,
                                   const set<int> &det_nums ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( sample_nums.empty() )
    sample_nums = sample_numbers_;
  
  const size_t ndet = detector_numbers_.size();
  vector<bool> detectors( ndet, true );
  if( !det_nums.empty() )
  {
    for( size_t i = 0; i < ndet; ++i )
      detectors[i] = (det_nums.count(detector_numbers_[i]) != 0);
  }//if( det_nums.empty() )
  
  std::shared_ptr<Measurement> summed = sum_measurements( sample_nums, detectors );
  
  if( !summed || !summed->gamma_counts() )
    return false;
  
  const std::shared_ptr<const std::vector<float>> &fgammacounts = summed->gamma_counts();
  
  int16_t val16 = -1;
  ostr.write( (const char *)&val16, sizeof(int16_t) );
  //index=2
  
  val16 = 0; //McaNumber
  ostr.write( (const char *)&val16, sizeof(int16_t) );
  //index=4
  
  val16 = 1; //Segment, set to 1 in UMCBI
  ostr.write( (const char *)&val16, sizeof(int16_t) );
  //index=6
  
  char buffer[256];
  
  const boost::posix_time::ptime &starttime = summed->start_time_;
  if( starttime.is_special() )
    buffer[0] = buffer[1] = '0';
  else
    snprintf( buffer, sizeof(buffer), "%02d", int(starttime.time_of_day().seconds()) );
  ostr.write( buffer, 2 );
  //index=8
  
  const uint32_t realTimeTimesFifty = static_cast<uint32_t>(50.0f*summed->real_time_);
  const uint32_t liveTimeTimesFifty = static_cast<uint32_t>(50.0f*summed->live_time_);
  ostr.write( (const char *)&realTimeTimesFifty, 4 );
  ostr.write( (const char *)&liveTimeTimesFifty, 4 );
  //index=16
  
  if( starttime.is_special() )
    buffer[0] = buffer[1] = '0';
  else
    snprintf( buffer, sizeof(buffer), "%02d", int(starttime.date().day_number()) );
  ostr.write( buffer, 2 );
  //index=18
  
  const char *monthstr = "   ";
  
  try
  {
    switch( starttime.date().month().as_enum() )
    {
      case boost::gregorian::Jan: monthstr = "Jan"; break;
      case boost::gregorian::Feb: monthstr = "Feb"; break;
      case boost::gregorian::Mar: monthstr = "Mar"; break;
      case boost::gregorian::Apr: monthstr = "Apr"; break;
      case boost::gregorian::May: monthstr = "May"; break;
      case boost::gregorian::Jun: monthstr = "Jun"; break;
      case boost::gregorian::Jul: monthstr = "Jul"; break;
      case boost::gregorian::Aug: monthstr = "Aug"; break;
      case boost::gregorian::Sep: monthstr = "Sep"; break;
      case boost::gregorian::Oct: monthstr = "Oct"; break;
      case boost::gregorian::Nov: monthstr = "Nov"; break;
      case boost::gregorian::Dec: monthstr = "Dec"; break;
      case boost::gregorian::NotAMonth:
      case boost::gregorian::NumMonths:
        break;
    }//switch( starttime.date().month().as_enum() )
  }catch(...)
  {
    //Here when month is invalid...
  }
  ostr.write( monthstr, 3 );
  //index=21
  
  if( starttime.is_special() )
    buffer[0] = buffer[1] = '0';
  else
    snprintf( buffer, sizeof(buffer), "%02d", int(starttime.date().year()%100) );
  ostr.write( buffer, 2 );
  //index=23
  
  if( !starttime.is_special() && starttime.date().year() >= 2000 )
    ostr.write( "1", 1 );
  else
    ostr.write( "0", 1 );
  //index=24
  
  if( starttime.is_special() )
    buffer[0] = buffer[1] = '0';
  else
    snprintf( buffer, sizeof(buffer), "%02d", int(starttime.time_of_day().hours()) );
  ostr.write( buffer, 2 );
  //index=26
  
  
  if( starttime.is_special() )
    buffer[0] = buffer[1] = '0';
  else
    snprintf( buffer, sizeof(buffer), "%02d", int(starttime.time_of_day().minutes()) );
  ostr.write( buffer, 2 );
  //index=28
  
  uint16_t firstchannel = 0;
  ostr.write( (const char *)&firstchannel, 2 );
  //index=30
  
  const uint16_t numchannels = static_cast<uint16_t>( fgammacounts->size() );
  ostr.write( (const char *)&numchannels, 2 );
  //index=32
  
  //Not actually sure if we want to write the channel data at index 32, 34, 36, or 40...
  //  Also, there may be a need to shift the channels by one left or right.
  //  Also, not certain about values in first channel or two...
  vector<uint32_t> intcounts( numchannels );
  for( uint16_t i = 0; i < numchannels; ++i )
    intcounts[i] = static_cast<uint32_t>( (*fgammacounts)[i] + 0.5f );
  ostr.write( (const char *)&intcounts[0], numchannels*4 );
  
  vector<float> calibcoef = summed->calibration_coeffs();
  
  switch( summed->energy_calibration_model() )
  {
    case SpecUtils::EnergyCalType::Polynomial:
    case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
      break;
    case SpecUtils::EnergyCalType::FullRangeFraction:
      calibcoef = SpecUtils::fullrangefraction_coef_to_polynomial( calibcoef, fgammacounts->size() );
      break;
    case SpecUtils::EnergyCalType::LowerChannelEdge:
    case SpecUtils::EnergyCalType::InvalidEquationType:
      calibcoef.clear();
      break;
  }//switch( summed->energy_calibration_model() )
  
  if( calibcoef.size() < 3 )
    calibcoef.resize( 3, 0.0f );
  
  int16_t chntype = -102;
  ostr.write( (const char *)&chntype, 2 );
  
  buffer[0] = buffer[1] = '0';
  ostr.write( buffer, 2 );
  ostr.write( (const char *)&calibcoef[0], 3*sizeof(float) );
  
  float fummyf = 0.0f;
  ostr.write( (const char *)&fummyf, sizeof(float) );
  ostr.write( (const char *)&fummyf, sizeof(float) );
  ostr.write( (const char *)&fummyf, sizeof(float) );
  
  for( size_t i = 0; i < 228; ++i )
    ostr.write( "\0", 1 );
  
  
  
  string detdesc = summed->title_;
  for( const string &remark : remarks_ )
  {
    if( SpecUtils::starts_with( remark, "Detector Description: " ) )
      detdesc = " " + remark.substr( 22 );
  }
  
  trim( detdesc );
  
  if( detdesc.length() > 63 )
    detdesc = detdesc.substr(0,63);
  
  uint8_t len = static_cast<uint8_t>( detdesc.size() );
  ostr.write( (const char *)&len, 1 );
  ostr.write( detdesc.c_str(), len+1 );
  for( size_t i = len+1; i < 63; ++i )
    ostr.write( "\0", 1 );
  
  string titlestr;
  if( measurements_.size()==1 )
    titlestr = measurements_[0]->title_;
  
  if( titlestr.length() > 63 )
    titlestr = titlestr.substr(0,63);
  len = static_cast<uint8_t>( titlestr.size() );
  ostr.write( (const char *)&len, 1 );
  ostr.write( titlestr.c_str(), len+1 );
  for( size_t i = len+1; i < 63; ++i )
    ostr.write( "\0", 1 );
  
  for( size_t i = 0; i < 128; ++i )
    ostr.write( "\0", 1 );
  
  return true;
}//bool write_integer_chn(...)
}//namespace SpecUtils


