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
#include <tuple>
#include <cctype>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <float.h>
#include <fstream>
#include <sstream>
#include <assert.h>
#include <iostream>
#include <stdexcept>
#include <algorithm>


#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/Filesystem.h"
#include "SpecUtils/SpecFile_location.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

namespace
{
  bool toDouble( const std::string &str, double &f )
  {
    const int nconvert = sscanf( str.c_str(), "%lf", &f );
    return (nconvert == 1);
  }
  
  //During parsing we abuse the remarks to hold PCF specific information, so
  //  lets extract that back out now - soryy for this horriblness.
  string findPcfRemark( const char *start, const vector<string> &remarks )
  {
    const size_t start_len = strlen(start);
    for( size_t i = 0; i < remarks.size(); ++i )
      if( SpecUtils::istarts_with(remarks[i], start) )
      {
        string val = remarks[i].substr( start_len );
        const size_t pos = val.find_first_not_of( " :\t\n\r=");
        if( pos != string::npos )
          val = val.substr(pos);
        return val;
      }
    return "";
  }//string findPcfRemark( const char *start, const vector<string> &remarks )
  
  
  string parse_pcf_field( const string &header, size_t offset, size_t len )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    if( offset+len > header.size() )
    {
      log_developer_error( __func__, "Logic error in parse_pcf_field" );
      throw runtime_error( "Logic error in parse_pcf_field" );
    }
#endif
    string field( header.begin() + offset, header.begin() + offset + len );
    const size_t zeropos = field.find_first_of( '\0' );
    if( zeropos != string::npos )
      field = field.substr(0,zeropos);
    SpecUtils::trim( field );
    
    return field;
  };//parse_pcf_field
  
  
  
  //returns negative if invalid name
  int pcf_det_name_to_dev_pair_index( std::string name, int &col, int &panel, int &mca )
  {
    col = panel = mca = -1;
    
    //loop over columns (2 uncompressed, or 4 compressed)  //col 1 is Aa1, col two is Ba1
    //  loop over panels (8) //Aa1, Ab1, Ac1
    //    loop over MCAs (8) //Aa1, Aa2, Aa3, etc
    //      loop over deviation pairs (20)
    //        energy (float uncompressed, or int16_t compressed)
    //        offset (float uncompressed, or int16_t compressed)
    
    if( name.size() < 2 || name.size() > 3
       || name[name.size()-1] < '1' || name[name.size()-1] > '8' )
    {
      return -1;
    }
    
    SpecUtils::to_lower_ascii( name );
    
    const char col_char = ((name.size()==3) ? name[1] : 'a');
    const char panel_char = name[0];
    const char mca_char = name[name.size()-1];
    
    if( col_char < 'a' || col_char > 'd' || panel_char < 'a' || panel_char > 'h' )
      return -1;
    
    col = col_char - 'a';
    panel = panel_char - 'a';
    mca = mca_char - '1';
    
    return col*(8*8*2*20) + panel*(8*2*20) + mca*(2*20);
  }
  
  
  int pcf_det_name_to_dev_pair_index( std::string name )
  {
    int col, panel, mca;
    return pcf_det_name_to_dev_pair_index( name, col, panel, mca );
  };//pcf_det_name_to_dev_pair_index lambda
  
}//namespace


namespace SpecUtils
{
  
/** Gives the maximum number of channels any spectrum in the file will need to write to PCF file (rounded up to the nearest multiple of
 64 channels), as well as a sets a pointer to the lower channel energies to write to the first record, but only if lower channel energy
 calibration should be used (if FRF should be used, then pointer will be reset to nulltr).
 */
void SpecFile::pcf_file_channel_info( size_t &nchannel,
                                       std::shared_ptr<const std::vector<float>> &lower_channel_energies ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  nchannel = 0;
  lower_channel_energies.reset();
  
  bool use_lower_channel = true;
  std::shared_ptr<const std::vector<float>> lower_e_bin;
  
  for( const auto &meas : measurements_ )
  {
    const size_t nmeaschann = meas->num_gamma_channels();
    
    const auto &cal = meas->energy_calibration_;
    if( !cal || (cal->type() == EnergyCalType::InvalidEquationType) || (nmeaschann <= 7) )
      continue;
    
    const size_t ncalchannel = cal->num_channels();
    assert( ncalchannel == nmeaschann );
    
    const auto &these_energies = cal->channel_energies();
    assert( these_energies );
      
    nchannel = std::max( nchannel, nmeaschann );
    
    if( meas->energy_calibration_model() != SpecUtils::EnergyCalType::LowerChannelEdge )
    {
      use_lower_channel = false;
      continue;
    }
    
    //If we have already found a lower_e_bin, check if this current
    //  one is either the same one in memory, or if not, if its reasonably
    //  close in numeric value.
    if( use_lower_channel && lower_e_bin && (lower_e_bin != these_energies) )
    {
      if( lower_e_bin->size() != these_energies->size() )
      {
        use_lower_channel = false;
        lower_e_bin.reset();
        continue;
      }
      
      for( size_t channel = 0; use_lower_channel && channel < lower_e_bin->size(); ++channel )
      {
        //A better float test would be AlmostEquals() from
        //  https://github.com/abseil/googletest/blob/master/googletest/include/gtest/internal/gtest-internal.h
        const float lhs = (*lower_e_bin)[channel];
        const float rhs = (*these_energies)[channel];
        
        if( fabs(lhs-rhs) > std::max(FLT_EPSILON,0.001f*lhs) )
        {
          use_lower_channel = false;
          lower_e_bin.reset();
          continue;
        }
      }//for( size_t channel = 0; use_lower_channel && channel < lower_e_bin->size(); ++channel )
    }else if( use_lower_channel && (lower_e_bin != these_energies) )
    {
      lower_e_bin = these_energies;
    }//if( we already have found lower_e_bin ) / else if( we might us the lower channel energies )
  }//for( const auto &meas : measurements_ )
  
  if( nchannel <= 7 )
  {
    nchannel = 0;
    return;
  }
  
  if( use_lower_channel && lower_e_bin && (lower_e_bin->size() > 7) )
  {
    nchannel += 1;  //GADRAS needs N+1 channels for the lower energy channels record
    if( lower_e_bin->size() == nchannel )
    {
      lower_channel_energies = lower_e_bin;
    }else
    {
      // After the great energy calibration re-factoring, I dont think we will ever get here...
      auto binning = make_shared<vector<float>>(nchannel);
      if( lower_e_bin->size() >= nchannel )
      {
        for( size_t i = 0; i < nchannel; ++i )
          (*binning)[i] = (*lower_e_bin)[i];
      }else
      {
        for( size_t i = 0; i < lower_e_bin->size(); ++i )
          (*binning)[i] = (*lower_e_bin)[i];
        
        const float last_bin_energy = lower_e_bin->back();
        const float last_bin_width = (*lower_e_bin)[lower_e_bin->size()-1] - (*lower_e_bin)[lower_e_bin->size()-2];
        for( size_t i = lower_e_bin->size(); i < nchannel; ++i )
          (*binning)[i] = last_bin_energy + (1+i-lower_e_bin->size())*last_bin_width;
      }
      
      lower_channel_energies = binning;
    }//if( we can re-use lower channel energies ) / else ( we create new ones )
  }//if( we will use lower channel energies )
  
  //We need to round nchannel_file up to the nearest 64 channels since each
  // record must be multiple of 256 bytes
  if( (nchannel % 64) != 0 )
    nchannel += (64 - (nchannel % 64));
}//size_t pcf_file_channel_info(...) const;
  
/*
std::shared_ptr<const std::vector<float>> SpecFile::lower_channel_energies_for_pcf() const
{
 shared_ptr<const vector<float>> lower_channel_energies;
 for( const auto &meas : measurements_ )
 {
 const auto &these_energies = meas->gamma_channel_energies();
 
 //these_energies is currently garunteed to be valid if num_gamma_channels()>0, but lets check JIC
 if( meas->num_gamma_channels() < 7 || !these_energies )
 continue;
 
 if( (these_energies->size()+1)<nchannel_file
 || meas->energy_calibration_model() != SpecUtils::EnergyCalType::LowerChannelEdge )
 {
 return 0;
 }
 
 //If we have already found a lower_channel_energies, check if this current
 //  one is either the same  one in memory, or if not, if its reasonably
 //  close in numeric value.
 if( lower_channel_energies && (lower_channel_energies!=these_energies) )
 {
 if( lower_channel_energies->size() != these_energies->size() )
 return 0;
 
 for( size_t channel = 0; channel < lower_channel_energies->size(); ++channel )
 {
 //A better float test would be AlmostEquals() from
 //  https://github.com/abseil/googletest/blob/master/googletest/include/gtest/internal/gtest-internal.h
 const float lhs = (*lower_channel_energies)[channel];
 const float rhs = (*these_energies)[channel];
 
 if( fabs(lhs-rhs) > std::max(FLT_EPSILON,0.001f*lhs) )
 return 0;
 }
 }//if( we already have found lower_channel_energies )
 
 lower_channel_energies = these_energies;
 }//for( size_t i = 0; all_lower_chanel_energy && (i < measurements_.size()); ++i )
 
 if( !lower_channel_energies )
 return 0;
 
 size_t nchannel_file = num_gamma_channels();
 
 auto channel_counts = make_shared<const vector<float>>();
 vector<float> channel_counts( nchannel_file );
 for( size_t i = 0; i < nchannel_file; ++i )
 {
 if( i < lower_channel_energies->size() )
 channel_counts[i] = (*lower_channel_energies)[i];
 else
 channel_counts[i] = (i-lower_channel_energies->size()+1)*(lower_channel_energies->back()/lower_channel_energies->size());
 }
}//std::shared_ptr<const std::vector<float>> lower_channel_energies_for_pcf() const;
*/
  
  
size_t SpecFile::write_lower_channel_energies_to_pcf( std::ostream &ostr,
                                                       std::shared_ptr<const std::vector<float>> lower_channel_energies,
                                                       const size_t nchannel_file ) const
{
#if(PERFORM_DEVELOPER_CHECKS)
  //write_pcf() will make sure the stream will support tellp() if PERFORM_DEVELOPER_CHECKS is true
  const auto orig_pos = ostr.tellp();
  assert( (nchannel_file % 64)== 0 );
#endif
  
  if( nchannel_file < 7 || !lower_channel_energies || lower_channel_energies->size() < 7 )
    return 0;
  
  string title_source_description = "Energy";
  title_source_description.resize( 180, ' ' );
  ostr.write( &(title_source_description[0]), title_source_description.size() );
  
  string datestr;
  for( const auto &m : measurements_ )
  {
    if( !is_special(m->start_time()) )
    {
      datestr = SpecUtils::to_common_string( m->start_time(), true );
      break;
    }
  }
  
  if( datestr.empty() )
    datestr = "01-Jan-1900 00:00:00.00";
  
  datestr.resize( 23, ' ' );
  datestr += ' ';  //tag char
  ostr.write( (char *)&datestr[0], 24 );
  
  
  const float dummy_lt_rt = 1.0f;
  ostr.write( (char *)&dummy_lt_rt, 4 );
  ostr.write( (char *)&dummy_lt_rt, 4 );
  
  std::fill_n( std::ostreambuf_iterator<char>(ostr), 12, '\0' ); //halflife, molecular_weight, spectrum_multiplier
  
  const float offset = lower_channel_energies->front();
  const float gain = lower_channel_energies->back() - lower_channel_energies->front();
  
  ostr.write( (char *)&offset, 4 );
  ostr.write( (char *)&gain, 4 );
  
  std::fill_n( std::ostreambuf_iterator<char>(ostr), 20, '\0' ); //calibration and such
  
  const int32_t num_channel = static_cast<int32_t>( lower_channel_energies->size() );
  ostr.write( (char *)&num_channel, 4 );
  
  //cout << "Writing record 0 num_channel=" << num_channel << endl;
  
  ostr.write( (char *)&((*lower_channel_energies)[0]), 4*num_channel );
  
  if( nchannel_file > num_channel )
    std::fill_n( std::ostreambuf_iterator<char>(ostr), 4*(nchannel_file-num_channel), '\0' );
  
#if(PERFORM_DEVELOPER_CHECKS)
  const auto final_pos = ostr.tellp();
  const auto nwritten = final_pos - orig_pos;
  if( (nwritten != (256+4*nchannel_file))
     || ( (final_pos % 256) != 0 )
     || ( (nwritten % 256) != 0 ) )
  {
    char buffer[512];
    snprintf( buffer, sizeof(buffer),
             "When writing first channel energy record to PCF file, encountered error:"
             " orig_pos=%i at start of spectrum final_pos=%i at end, with a diff of %i."
             " All those should be multiples of 256",
             int(orig_pos), int(final_pos), int(nwritten) );
    log_developer_error( __func__, buffer );
  }
#endif
  
  return 256 + 4*nchannel_file;
}//size_t write_lower_channel_energies_to_pcf()
  
  
void SpecFile::write_deviation_pairs_to_pcf( std::ostream &ostr ) const
{
  //Find the deviation pairs to use in this file, for each detector.  PCF
  //  format assumes each detector only has one set of deviation pairs in the
  //  file, so we'll take just the first ones we find foreach detector.
  map<std::string, vector<pair<float,float>>> dev_pairs;
  set<string> detnames( begin(detector_names_), end(detector_names_) );  //may have neutron only detector names too
  
  bool has_some_dev_pairs = false, need_compress_pairs = false;
  
  for( size_t i = 0; !detnames.empty() && i < measurements_.size(); ++i )
  {
    const auto &meas = measurements_[i];
    const auto &name = meas->detector_name_;
    
    //Assume measurement with a gamma detector name, will also have gamma counts,
    //  so erase detector name from `detnames` now to make sure to get rid of
    //  neutron only detector names as well.
    detnames.erase( name );
    
    if( dev_pairs.find(name) != end(dev_pairs) ) //only get the first dev pairs we find for the detector.
      continue;
    
    //Make sure its actually a gamma detector
    if( meas->gamma_counts_ && !meas->gamma_counts_->empty())
    {
      has_some_dev_pairs |= (!meas->deviation_pairs().empty());
      if( name.size() >= 3
         && (name[1]=='c' || name[1]=='C' || name[1]=='d' || name[1]=='D')
         && (name[0]>='a' && name[0]<='g')
         && (name[2]>'0' && name[2]<'9') )
        need_compress_pairs = true;
      
      dev_pairs[name] = meas->deviation_pairs();
    }
  }//for( size_t i = 0; !detnames.empty() && i < measurements_.size(); ++i )
  
  //cerr << "Put " << dev_pairs.size() << " dev pairs in, with " << detnames.size()
  //     << " detnames remaining. has_some_dev_pairs=" << has_some_dev_pairs << endl;
  
  if( !has_some_dev_pairs )
    dev_pairs.clear();
  
  //cerr << "DetNames we have pairs for are: {";
  //for( const auto &n : dev_pairs )
  //  cerr << n.first << ", ";
  //cerr << "}" << endl;
  
  
  if( dev_pairs.empty() )
  {
#if(PERFORM_DEVELOPER_CHECKS && !defined(WIN32) )
    assert( ostr.tellp() == 256 );
#endif
    return;
  }
  
  std::string header = need_compress_pairs ? "DeviationPairsInFileCompressed" : "DeviationPairsInFile";
  header.resize( 256, ' ' );
  ostr.write( &header[0], header.size() );
  
  const size_t nDevBytes = 4*8*8*20*2*2;    //20,480 bytes
  const size_t nDevInts = nDevBytes / 2;    //10,240 ints
  const size_t nDevFloats = nDevBytes / 4;  //5,120 floats
  
  uint8_t dev_pair_data[nDevBytes] = { uint8_t(0) };
  
  const size_t valsize = need_compress_pairs ? 2 : 4;
  const size_t maxnvals = need_compress_pairs ? nDevInts : nDevFloats;
  
  
  set<string> unwritten_dets;
  set<int> written_index;
  for( const auto &det_devs : dev_pairs )
  {
    const string &name = det_devs.first;
    const auto &pairs = det_devs.second;
    
    int index = pcf_det_name_to_dev_pair_index( name );
    
    if( index < 0 || (index+39) > maxnvals )
    {
      unwritten_dets.insert( name );
      continue;
    }
    
    //cerr << "DetName=" << name << " gave index " << index << ", (maxnvals="
    //     << maxnvals << "), pairs.size=" << pairs.size() << " vals={";
    //for( size_t i = 0; i < pairs.size() && i < 20; ++i )
    //  cerr << "{" << pairs[i].first << "," << pairs[i].second << "}, ";
    //cerr << "}" << endl;
    
    written_index.insert( index );
    for( size_t i = 0; i < pairs.size() && i < 20; ++i )
    {
      const size_t bytepos = (index + 2*i)*valsize;
      
      if( need_compress_pairs )
      {
#if( defined(_MSC_VER) && _MSC_VER <= 1700 )
        const int16_t energy = static_cast<int16_t>( (pairs[i].first<0.0f) ? pairs[i].first-0.5f : pairs[i].first+0.5f );
        const int16_t offset = static_cast<int16_t>( (pairs[i].second<0.0f) ? pairs[i].second-0.5f : pairs[i].second+0.5f );
#else
        const int16_t energy = static_cast<int16_t>( roundf(pairs[i].first) );
        const int16_t offset = static_cast<int16_t>( roundf(pairs[i].second) );
#endif
        memcpy( &(dev_pair_data[bytepos + 0]), &energy, 2 );
        memcpy( &(dev_pair_data[bytepos + 2]), &offset, 2 );
      }else
      {
        memcpy( &(dev_pair_data[bytepos + 0]), &(pairs[i].first), 4 );
        memcpy( &(dev_pair_data[bytepos + 4]), &(pairs[i].second), 4 );
      }
    }
  }//for( const auto &det_devs : dev_pairs )
  
  //cout << "Didnt write dev pairs for " << unwritten_dets.size()
  //     << " detectors out of dev_pairs.size()=" << dev_pairs.size()
  //     << " detnames.size=" << detector_names_.size() << endl;
  
  //If we havent written some detectors deviation pairs, put them into the
  //  first available spots... This isnt actually correct, but will work
  //  in the case its not an RPM at all.
  if( unwritten_dets.size() )
  {
    if( unwritten_dets.size() != dev_pairs.size() )
    {
#if( !SpecUtils_BUILD_FUZZING_TESTS )
      cerr << "Warning: " << unwritten_dets.size() << " of the "
      << dev_pairs.size() << " gamma detectors didnt have conforming"
      << " names, so they are being written in the first available"
      << " spot in the PCF file." << endl;
#endif
    }
    
    for( const auto &name : unwritten_dets )
    {
#if(PERFORM_DEVELOPER_CHECKS)
      bool found_spot = false;
#endif
      for( int index = 0; index < maxnvals; index += 40 )
      {
        if( !written_index.count(index) )
        {
          const auto &dpairs = dev_pairs[name];
          for( size_t i = 0; i < dpairs.size() && i < 20; ++i )
          {
            const size_t bytepos = (index + 2*i)*valsize;
            
            if( need_compress_pairs )
            {
#if( defined(_MSC_VER) && _MSC_VER <= 1700 )
              const int16_t energy = static_cast<int16_t>( (dpairs[i].first<0.0f) ? dpairs[i].first-0.5f : dpairs[i].first+0.5f );
              const int16_t offset = static_cast<int16_t>( (dpairs[i].second<0.0f) ? dpairs[i].second-0.5f : dpairs[i].second+0.5f );
#else
              const int16_t energy = static_cast<int16_t>( roundf(dpairs[i].first) );
              const int16_t offset = static_cast<int16_t>( roundf(dpairs[i].second) );
#endif
              memcpy( &(dev_pair_data[bytepos + 0]), &energy, 2 );
              memcpy( &(dev_pair_data[bytepos + 2]), &offset, 2 );
            }else
            {
              memcpy( &(dev_pair_data[bytepos + 0]), &(dpairs[i].first), 4 );
              memcpy( &(dev_pair_data[bytepos + 4]), &(dpairs[i].second), 4 );
            }
          }//for( loop over deviation pairs )
          
#if(PERFORM_DEVELOPER_CHECKS)
          found_spot = true;
#endif
          written_index.insert( index );
          break;
        }//if( index hasnt been used )
      }//for( indexs to try )
      
      //In principle we may not have written the deviation pairs if we
      //  couldnt find a spot, but at this point, oh well.
#if(PERFORM_DEVELOPER_CHECKS)
      if( !found_spot )
        log_developer_error( __func__, ("SpecFile::write_deviation_pairs_to_pcf: "
                                        "Couldnt find spot to write deviation pairs for detector " + name + "!!!").c_str() );
#endif
    }//for( const auto &name : detectors_not_written )
  }//if( detectors_not_written.size() )
  
#if(PERFORM_DEVELOPER_CHECKS && !defined(WIN32) )
  assert( ostr.tellp() == 512 );
#endif
  ostr.write( (char *)dev_pair_data, sizeof(dev_pair_data) );
}//void write_deviation_pairs_to_pcf( std::ostream &outputstrm ) const
  
  
  
bool SpecFile::write_pcf( std::ostream &outputstrm ) const
{
#if(PERFORM_DEVELOPER_CHECKS)
  //The input stream may not support tellp(), so for testing to make sure we
  //  have the positions correct, we'll use a stringstream, and then swap
  //  later on.
  stringstream ostr;
#else
  std::ostream &ostr = outputstrm;
#endif
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  double nneutron_written = 0.0;
  try
  {
    size_t nchannel_file = 0;
    std::shared_ptr<const std::vector<float>> lower_channel_energies;
    pcf_file_channel_info( nchannel_file, lower_channel_energies );
    
#if(PERFORM_DEVELOPER_CHECKS)
    assert( (nchannel_file % 64) == 0 );
#endif
    
    if( !nchannel_file )
      throw runtime_error( "No measurements to write to PCF." );
    
    //We want to put the detector name in the "Title" of the PCF, but only if
    //  there is more than one detector.
    //  XXX - this is an expensive operation!
    set<string> gamma_det_names;
    
    for( const auto &m : measurements_ )
    {
      if( m && m->gamma_counts_ && m->gamma_counts_->size() )
        gamma_det_names.insert( m->detector_name_ );
    }
    
    const size_t num_gamma_detectors = gamma_det_names.size();
    
    
    std::basic_string<char> fileid;
    const int16_t NRPS = 1 + static_cast<int16_t>( 4.0*nchannel_file/256.0 );
    //cout << "NRPS=" << int(NRPS) << endl;
    
    fileid.resize( 2, '\0' );
    memcpy( &(fileid[0]), &NRPS, 2 );
    fileid += "DHS       " + uuid();  //The seven spaved is the "File last modified date hash".  UUID should be 36 bytes
    fileid.resize( 48 , ' ' );
    fileid += inspection_;
    fileid.resize( 64, ' ' );
    int16_t lanenum = static_cast<int16_t>( lane_number_ );
    fileid.resize( 66, 0 );
    memcpy( &(fileid[64]), &lanenum, 2 );
    
    
    for( size_t i = 0; i < remarks_.size(); ++i )
    {
      string val = remarks_[i];
      SpecUtils::trim( val );
      if( val.empty()
         || SpecUtils::istarts_with(val, "ItemDescription")
         || SpecUtils::istarts_with(val, "CargoType")
         || SpecUtils::istarts_with(val, "ItemToDetectorDistance")
         || SpecUtils::istarts_with(val, "OccupancyNumber") )
        continue;
      fileid += (i ? "\r\n" : "") + val;
    }
    
    fileid.resize( 92, ' ' );
    
    fileid += instrument_type();
    fileid.resize( 120, ' ' );
    fileid += manufacturer();
    fileid.resize( 148, ' ' );
    fileid += instrument_model();
    fileid.resize( 166, ' ' );
    fileid += instrument_id();
    fileid.resize( 184, ' ' );
    
    string item_description = findPcfRemark("ItemDescription",remarks_);
    if( item_description.size() > 20 )
      item_description = item_description.substr(0,20);
    fileid += item_description;
    fileid.resize( 204, ' ' );
    
    fileid += measurement_location_name_;
    fileid.resize( 220, ' ' );
    
    if( has_gps_info() )
    {
      //we only have 16 bytes, here; we'll try printing to 7 decimal points, and
      //  if too long, print 5, and then 4, etc..
      size_t len = 63;
      char valbuffer[64] = { 0 };
      for( int ndecimals = 7; len > 16 && ndecimals > 2; --ndecimals )
      {
        char frmtstr[32];
        snprintf( frmtstr, sizeof(frmtstr), "%%.%if,%%.%if", ndecimals, ndecimals);
        snprintf( valbuffer, sizeof(valbuffer), frmtstr, mean_latitude(), mean_longitude() );
        len = strlen( valbuffer );
      }
      fileid += valbuffer;
    }//if( has_gps_info() )
    
    fileid.resize( 236, ' ' );
    
    fileid.resize( 238, '\0' ); //2-byte signed integer of Item to detector distance
    string item_dist_str = findPcfRemark("ItemToDetectorDistance",remarks_);
    int16_t itemdistance = static_cast<int16_t>( atoi( item_dist_str.c_str() ) );
    memcpy( &(fileid[236]), &itemdistance, 2 );
    
    fileid.resize( 240, '\0' ); //2-byte signed integer of Occupancy number
    int occnum = occupancy_number_from_remarks();
    if( occnum >= 0 )
    {
      const int16_t occ = static_cast<int16_t>( occnum );
      memcpy( &(fileid[238]), &occ, 2 );
    }
    
    string cargo_type = findPcfRemark("CargoType",remarks_);
    if( cargo_type.size() > 16 )
      cargo_type = cargo_type.substr(0,16);
    fileid += cargo_type;
    
    fileid.resize( 256, ' ' );
    ostr.write( &fileid[0], fileid.size() );
    
    //
    write_deviation_pairs_to_pcf( ostr );
    
    //For files with energy calibration defined by lower channel energies, the
    //  first record in the file will have a title of "Energy" with the channel
    //  counts equal to the channel lower energies
    write_lower_channel_energies_to_pcf( ostr, lower_channel_energies, nchannel_file );
    
    
    //Backgrounds (and calibrations?) dont count toward sample numbers for GADRAS; it also assumes
    //  samples start at 1 (like FORTRAN...).  So we will hack things a bit
    //  for passthrough()s; there is a little bit of checking to make sure
    //  sample numbers are kept in the same order as original, btu its not super
    //  robust or tested.
    vector<int> passthrough_samples;  //will be sorted with at most one of each value
    
    for( size_t i = 0; i < measurements_.size(); ++i )
    {
#if(PERFORM_DEVELOPER_CHECKS)
      istream::pos_type file_pos = ostr.tellp();
      
      if( (file_pos % 256) != 0 )
      {
        char buffer[256];
        snprintf( buffer, sizeof(buffer),
                 "When writing PCF file, at file position %i at start of spectrum %i when should be at a multiple of 256", int(file_pos), int(i) );
        log_developer_error( __func__, buffer );
      }
#endif
      
      std::shared_ptr<const Measurement> meas = measurements_[i];
      
      if( !meas || ((!meas->gamma_counts_ || meas->gamma_counts_->empty()) && !meas->contained_neutron() ) )
      {
        continue;
      }
      
      
      string spectrum_title;  //ex: 'Survey 1 Det=Aa1 Background @250cm'
      string collection_time;  //Formatted like: '2010-02-24T00:08:24.82Z'
      
      char character_tag;
      float live_time, true_time, halflife = 0.0, molecular_weight = 0.0,
      spectrum_multiplier = 0.0, offset, gain, quadratic, cubic, low_energy,
      neutron_counts;
      const int32_t num_channel = static_cast<int32_t>( meas->gamma_counts_ ? meas->gamma_counts_->size() : size_t(0) );
      
      live_time = meas->live_time_;
      true_time = meas->real_time_;
      
      char buffer[128];
      
      int sample_num = meas->sample_number_;
      if( passthrough() && (meas->source_type() != SourceType::Background && (meas->source_type() != SourceType::Calibration)) )
      {
        auto pos = std::lower_bound( begin(passthrough_samples), end(passthrough_samples), meas->sample_number_ );
        sample_num = static_cast<int>(pos - passthrough_samples.begin()) + 1;
        if( pos == end(passthrough_samples) || ((pos != end(passthrough_samples)) && ((*pos) != meas->sample_number_)) )
        {
          //Will almost always be an insertion at the end - so its not the worst possible...
          passthrough_samples.insert( pos, meas->sample_number_ );
        }
      }
      
      if( passthrough()
         && (meas->sample_number_ >= 0)
         && !SpecUtils::icontains( meas->title_, "sample" )
         && !SpecUtils::icontains( meas->title_, "survey" ) )
      {
        if( meas->source_type() == SourceType::Background )
          snprintf( buffer, sizeof(buffer), " Background" );
        else if( meas->source_type() == SourceType::Calibration )
          snprintf( buffer, sizeof(buffer), " Calibration" );
        else
          snprintf( buffer, sizeof(buffer), " Survey %i", sample_num );
        spectrum_title += buffer;
      }
      
      if( num_gamma_detectors > 1 )
      {
        //See refP0Z5UKVMME for why remove DetectorInfo
        string detname = meas->detector_name_;
        if( SpecUtils::istarts_with( detname, "DetectorInfo" ) )
          detname = detname.substr(12);
        
        spectrum_title += (spectrum_title.empty() ? "Det=" : ": Det=") + detname;
      }
      
      if( !passthrough()
         && !SpecUtils::icontains( meas->title_, "Background" )
         && !SpecUtils::icontains( meas->title_, "Calibration" )
         && !SpecUtils::icontains( meas->title_, "Foreground" ) )
      {
        if( meas->source_type_ == SourceType::Background )
          spectrum_title += " Background";
        else if( meas->source_type_ == SourceType::Calibration )
          spectrum_title += " Calibration";
        else
          spectrum_title += " Foreground";
      }//if( not already labeled foreground or background )
      
      
      if( meas->location_
         && !IsNan(meas->location_->speed_)
         && !SpecUtils::icontains( meas->title_, "speed" ) )
      {
        snprintf( buffer, sizeof(buffer), " Speed %f m/s", meas->location_->speed_ );
        spectrum_title += buffer;
      }
      
      //Added next line 20181109 to make sure a round trip from PCF to PCF will
      //  not change title
      if( !meas->title_.empty() )
        spectrum_title = meas->title_;
      
      //Next commented out code is from before 20181109 and left as comment for
      //  future reference incase any tests break
      //Lets keep the title from repeating background/foreground
      //string old_title = meas->title_;
      //const char * const check_paterns[] = { "background", "foreground" };
      //for( const char *p : check_paterns )
      //  if( SpecUtils::icontains(old_title, p) && SpecUtils::icontains(spectrum_title, p) )
      //    SpecUtils::ireplace_all(old_title, p, "" );
      //spectrum_title += " " + old_title;
      
      trim( spectrum_title );
      SpecUtils::ireplace_all( spectrum_title, "  ", " " );
      
      string source_list, spectrum_desc;
      for( const string &remark : meas->remarks() )
      {
        if( SpecUtils::istarts_with(remark, "Description:") )
          spectrum_desc = remark.substr(12);
        else if( SpecUtils::istarts_with(remark, "Source:") )
          source_list = remark.substr(7);
      }//for( const string &remark : meas->remarks() )
      
      SpecUtils::trim( spectrum_title );
      SpecUtils::trim( spectrum_desc );
      SpecUtils::trim( source_list );
      
      //Maximum length for title, description, or source list is 128 characters
      if( spectrum_title.size() > 128 )
        spectrum_title = spectrum_title.substr(0,128);
      if( spectrum_desc.size() > 128 )
        spectrum_desc = spectrum_desc.substr(0,128);
      if( source_list.size() > 128 )
        source_list = source_list.substr(0,128);
      
      string title_source_description;
      if( spectrum_title.size() < 61 && spectrum_desc.size() < 61 && source_list.size() < 61 )
      {
        spectrum_title.resize( 60, ' ' );
        spectrum_desc.resize( 60, ' ' );
        source_list.resize( 60, ' ' );
        title_source_description = spectrum_title + spectrum_desc + source_list;
      }else
      {
        const int title_len = static_cast<int>( spectrum_title.size() );
        const int desc_len = static_cast<int>( spectrum_desc.size() );
        const int source_len = static_cast<int>( source_list.size() );
        
        if( (title_len + desc_len + source_len) < 178 )
        {
          title_source_description = char(0xFF) + spectrum_title + char(0xFF) + spectrum_desc + char(0xFF) + source_list;
        }else //skip spectrum_desc, which is only used for plot file in GADRAS
        {
          if( (title_len + source_len) > 177 )
            spectrum_title = spectrum_title.substr(0,177-source_len);
          title_source_description = char(0xFF) + (spectrum_title + char(0xFF)) + (char(0xFF) + source_list);
        }
      }//if( we can used fixed title/desc/source placement ) else ( use truncation )
      
      if( !is_special(meas->start_time_) )
        collection_time = SpecUtils::to_vax_string( meas->start_time() );
      else
        collection_time = "                       "; //"01-Jan-1900 00:00:00.00";  //23 characters
      
      character_tag = ' ';
      
      //From phone conversation with Dean 20170816:
      //  The meaning of the 'tag' character is highly overloaded, and can mean,
      //  among other usses:
      //    '-' not occupied, and anything else occupied - for RPM data
      //    '-' use a dashed line when plotting
      //    '<' Use filled region style when plotting
      //    'T' Calibration from thorium
      //    'K' Calibration from potasium
      
      if( passthrough() )
      {
        if( (meas->occupied() ==  OccupancyStatus::NotOccupied)
           && (meas->source_type() != SourceType::Background) )
          character_tag = '-';
        else if( meas->occupied() == OccupancyStatus::Occupied )
          character_tag = ' ';
        //else if this is background and we know what isotope we are calibrating
        //  from, then could put 'K' or 'T'
      }
      
      assert( meas->energy_calibration_ );
      vector<float> calib_coef = meas->energy_calibration_->coefficients();
      const auto caltype = meas->energy_calibration_->type();
      
      if( num_channel && (caltype == SpecUtils::EnergyCalType::Polynomial
                          || caltype == SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial) )
      {
        calib_coef = SpecUtils::polynomial_coef_to_fullrangefraction( calib_coef, meas->gamma_counts_->size() );
      }
      
      offset         = (calib_coef.size() > 0) ? calib_coef[0] : 0.0f;
      gain           = (calib_coef.size() > 1) ? calib_coef[1] : 0.0f;
      quadratic      = (calib_coef.size() > 2) ? calib_coef[2] : 0.0f;
      cubic          = (calib_coef.size() > 3) ? calib_coef[3] : 0.0f;
      low_energy     = 0.0f;
      if( caltype == SpecUtils::EnergyCalType::FullRangeFraction )
        low_energy   = (calib_coef.size() > 4) ? calib_coef[4] : 0.0f;
      
      if( lower_channel_energies && lower_channel_energies->size() > 7 )
      {
        offset = lower_channel_energies->front();
        gain = lower_channel_energies->back() - lower_channel_energies->front();
        quadratic = cubic = low_energy = 0.0f;
      }//if( lower_channel_energies && lower_channel_energies->size() > 7 )
      
      const float dummy_float = 0.0f;
      neutron_counts = static_cast<float>( meas->neutron_counts_sum_ );
      
      nneutron_written += neutron_counts;
      
      title_source_description.resize( 180, ' ' ); //JIC
      ostr.write( &(title_source_description[0]), title_source_description.size() );
      
      collection_time.resize( 23, ' ' );
      
      ostr.write( &(collection_time[0]), collection_time.size() );
      
      ostr.write( &character_tag, 1 );
      ostr.write( (char *)&live_time, 4 );
      ostr.write( (char *)&true_time, 4 );
      ostr.write( (char *)&halflife, 4 );
      ostr.write( (char *)&molecular_weight, 4 );
      ostr.write( (char *)&spectrum_multiplier, 4 );
      ostr.write( (char *)&offset, 4 );
      ostr.write( (char *)&gain, 4 );
      ostr.write( (char *)&quadratic, 4 );
      ostr.write( (char *)&cubic, 4 );
      ostr.write( (char *)&low_energy, 4 );
      ostr.write( (char *)&dummy_float, 4 );
      ostr.write( (char *)&neutron_counts, 4 ); //
      ostr.write( (char *)&num_channel, 4 );
      
      if( num_channel )
        ostr.write( (char *)&(meas->gamma_counts_->operator[](0)), 4*num_channel );
      
      //Incase this spectrum has less channels than 'nchannel_file'
      if( nchannel_file != num_channel )
      {
        //        assert( nchannel_file > num_channel );
        char dummies[4] = {'\0','\0','\0','\0'};
        for( size_t i = num_channel; i < nchannel_file; ++i )
          ostr.write( dummies, 4 );
      }//if( nchannel_file != num_channel )
      
    }//for( auto meas, measurements_ )
    
#if(PERFORM_DEVELOPER_CHECKS)
    if( !ostr.bad() )
    {
      outputstrm << ostr.rdbuf();
      return true;
    }
    return false;
#else
    return !ostr.bad();
#endif
  }catch( std::exception & /* e */ )
  {
    //cerr << "SpecFile::write_pcf(): \n\tCaught " << e.what() << endl;
  }
  
  return false;
}//bool write_pcf( std::ostream& ostr ) const

bool SpecFile::load_pcf_file( const std::string &filename )
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
  
  const bool loaded = load_from_pcf( file );
  
  
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_pcf_file( const std::string &filename )
  
  
bool SpecFile::load_from_pcf( std::istream &input )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  
  try
  {
    input.exceptions( ios::failbit | ios::badbit );
    
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( orig_pos, ios::beg );
    const size_t filelen = static_cast<size_t>( 0 + eof_pos - orig_pos );
    
    if( filelen && (filelen < 512) )
      throw runtime_error( "File to small" );
    
    string fileid, header;
    fileid.resize( 256 );
    header.resize( 256 );
    
    input.read( &(fileid[0]), fileid.size() ); //256
    
    uint16_t NRPS;  //number of 256 byte records per spectrum
    memcpy( &NRPS, &(fileid[0]), 2 );
    const size_t bytes_per_record = 256 * static_cast<uint32_t>(NRPS);
    
    if( NRPS == 0 || (filelen && (NRPS*size_t(256)) > filelen) )
    {
      char buff[256];
      snprintf( buff, sizeof(buff), "Invalid number 256 segments per records, NRPS=%i", int(NRPS) );
      throw runtime_error( buff );
    }
    
    //Usually expect fileid[2]=='D', fileid[3]=='H' fileid[4]=='S'
    const bool is_dhs_version = (fileid[2]=='D' || fileid[3]=='H' || fileid[4]=='S');
    bool goodheader = is_dhs_version;
    if( !goodheader )
      goodheader = (fileid.substr(2,3) == "   ");
    
    if( !goodheader )
    {
      //something like '783 - 03/06/15 18:10:28'
      const size_t pos = fileid.find( " - " );
      goodheader = (pos != string::npos && pos < 20
                    && ((fileid[pos+5]=='/' && fileid[pos+7]=='/'
                         && fileid[pos+9]=='/' && fileid[pos+11]==' ')
                        || (fileid[pos+5]=='/' && fileid[pos+8]=='/'
                            && fileid[pos+11]==' ' && fileid[pos+14]==':'))
                    );
    }//if( !goodheader )
    
    if( !goodheader )
      throw runtime_error( "Unexpected fileID: '" + string( &fileid[0], &fileid[3] ) + "'" );
    
    input.read( &(header[0]), header.size() );  //512
    
    //Now read in deviation pairs from the file; we'll add these to the
    //  appropriate records late.
    //loop over columns (2 uncompressed, or 4 compressed)  //col 1 is Aa1, col two is Ba1
    //  loop over panels (8) //Aa1, Ab1, Ac1
    //    loop over MCAs (8) //Aa1, Aa2, Aa3, etc
    //      loop over deviation pairs (20)
    //        energy (float uncompressed, or int16_t compressed)
    //        offset (float uncompressed, or int16_t compressed)
    set<string> detector_names;
    std::vector< std::pair<float,float> > deviation_pairs[4][8][8];
    
    // We should have the string "DeviationPairsInFile" in the header if there are deviation pairs
    //  present.  However, I have seen at one case where the file only had "DeviationPairs",
    //  so we will just test for that (I dont think this should produce any false-positives...).
    bool have_deviation_pairs = (header.find("DeviationPairs") != string::npos);
    const bool compressed_devpair = (header.find("DeviationPairsInFileCompressed") != string::npos);
    
    if( have_deviation_pairs )
    {
      const size_t nDevBytes = 4*8*8*20*2*2;    //20,480 bytes
      uint8_t dev_pair_bytes[nDevBytes];
      
      input.read( (char *)dev_pair_bytes, nDevBytes );
      
      
      const int val_size = compressed_devpair ? 2 : 4;
      
      //A lot of times all of the deviation pairs are zero, so we will check
      //  if this is the case
      have_deviation_pairs = false;
      
      for( int row_index = 0; row_index < (compressed_devpair ? 4 : 2); ++row_index )
      {
        for( int panel_index = 0; panel_index < 8; ++panel_index )
        {
          for( int mca_index = 0; mca_index < 8; ++mca_index )
          {
            const int byte_pos = row_index*8*8*20*2*val_size + panel_index*8*20*2*val_size + mca_index*20*2*val_size;
            bool hasNonZero = false;
            for( int pos = byte_pos; !hasNonZero && pos < (byte_pos + 40*val_size); ++pos )
              hasNonZero = dev_pair_bytes[pos];
            if( !hasNonZero )
              continue;
            
            int last_nonzero = 0;
            auto &devpairs = deviation_pairs[row_index][panel_index][mca_index];
            if( compressed_devpair )
            {
              int16_t vals[40];
              memcpy( vals, &(dev_pair_bytes[byte_pos]), 80 );
              for( int i = 0; i < 20; ++i )
              {
                last_nonzero = (vals[2*i] || vals[2*i+1]) ? i+1 : last_nonzero;
                devpairs.push_back( pair<float,float>(vals[2*i],vals[2*i+1]) );
              }
            }else
            {
              float vals[40];
              memcpy( vals, &(dev_pair_bytes[byte_pos]), 160 );
              for( int i = 0; i < 20; ++i )
              {
                last_nonzero = (vals[2*i] || vals[2*i+1]) ? i+1 : last_nonzero;
                devpairs.push_back( pair<float,float>(vals[2*i],vals[2*i+1]) );
              }
            }//if( compressed ) / else
            
            devpairs.erase( begin(devpairs) + last_nonzero, end(devpairs) );
            
            have_deviation_pairs = (have_deviation_pairs || !devpairs.empty());
          }//for( int mca_index = 0; mca_index < 8; ++mca_index )
        }//for( int panel_index = 0; panel_index < 8; ++panel_index )
      }//for( int row_index = 0; row_index < (compressed ? 4 : 2); ++row_index )
    }else
    {
      //If this is not the "DHS" version of a PCF file with the extended header
      //  information, after the file header contents are:
      //  byte offset, data type, description
      //  0          , int16_t  , Number of records per spectrum (NRPS)
      //  2          , char[3]  , Version
      //  5          , char[4]  , Energy calibration label (unused)
      //  9          , float[5] , Energy calibration
      //  Then I guess a bunch of garbage to get up to 356 bytes.
      //  Note that each spectrum record usually has its own calibration, so
      //  this one in the header can usually be ignored.
      const size_t current_pos = static_cast<size_t>( 0 + input.tellg() );
      input.seekg( current_pos-256, ios::beg );
    }//if( header.find("DeviationPairsInFile") != string::npos ) / else
    
    
    shared_ptr<LocationState> gps_location;
    
    if( is_dhs_version )
    {
      int16_t lanenumber, item_dist, occ_num;
      //const string last_modified = parse_pcf_field(fileid,5,7);
      uuid_ = parse_pcf_field(fileid,12,36);
      inspection_ = parse_pcf_field(fileid,48,16);  //Secondary, Primary
      memcpy( &lanenumber, &(fileid[64]), 2 );
      if( lanenumber > 0 )
        lane_number_ = lanenumber;
      const string measremark = parse_pcf_field(fileid,66,26);
      if(!measremark.empty())
        remarks_.push_back( measremark );
      instrument_type_ = parse_pcf_field(fileid,92,28);
      manufacturer_ = parse_pcf_field(fileid,120,28);
      instrument_model_ = parse_pcf_field(fileid,148,18);
      instrument_id_ = parse_pcf_field(fileid,166,18);
      const string item_description = parse_pcf_field(fileid,184,20);
      if(!item_description.empty())
        remarks_.push_back( "ItemDescription: " + measremark );
      measurement_location_name_ = parse_pcf_field(fileid,204,16);
      const string meas_coords = parse_pcf_field(fileid,220,16);
      vector<string> meas_coords_components;
      SpecUtils::split(meas_coords_components, meas_coords, " ,\t\r\n");
      if( meas_coords_components.size() > 2 )
      {
        //Totally untested as of 20170811 - beacuase I have never seen a PCF file with coordinates...
        //parse_deg_min_sec_lat_lon(...)
        //ortecLatOrLongStrToFlt()
        double latitude = -999.9, longitude = -999.9;
        
        if( !toDouble( meas_coords_components[0], latitude )
           || !toDouble( meas_coords_components[0], longitude )
           || !SpecUtils::valid_latitude(latitude)
           || !SpecUtils::valid_longitude(longitude) )
        {
          latitude = longitude = -999.9;
          
          string warn_msg = "Could not interpret GPS coordinates in file.";
          auto pos = std::find( begin(parse_warnings_), end(parse_warnings_), warn_msg );
          if( pos == end(parse_warnings_) )
            parse_warnings_.push_back( std::move(warn_msg) );
          
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          char buffer[256];
          snprintf( buffer, sizeof(buffer),
                   "PCF file had non empty coordinates string '%s', but didnt return valid coordinates", meas_coords.c_str() );
          log_developer_error( __func__, buffer );
#endif
        }else
        {
          gps_location = make_shared<LocationState>();
          auto geo = make_shared<GeographicPoint>();
          gps_location->geo_location_ = geo;
          geo->latitude_ = latitude;
          geo->longitude_ = longitude;
        }
      }//if( meas_coords_components.size() > 2 )
      
      memcpy( &item_dist, &(fileid[236]), 2 );
      if( item_dist > 0 )
        remarks_.push_back( "ItemToDetectorDistance: " + std::to_string(item_dist) + " cm" );
      
      memcpy( &occ_num, &(fileid[238]), 2 );
      if( occ_num > 0 )
        remarks_.push_back( "OccupancyNumber: " + std::to_string(occ_num) );
      
      const string cargo_type = parse_pcf_field(fileid,240,16);
      if( cargo_type.size() )
        remarks_.push_back( "CargoType: " + cargo_type );
    }//if( is_dhs_version )
    
    
    bool any_contained_neutron = false, all_contained_neutron = true;
    
    bool allSamplesHaveNumbers = true, someSamplesHaveNumbers = false;
    
    //If deviation pairs in file, we are at: 512+(20*256)=5632
    // else we are at 256
    
    size_t record_number = 0;
    
    //If first record of PCF gives the lower channel energies, then we'll fill
    //  out lower_channel_energy_cal.
    shared_ptr<EnergyCalibration> lower_channel_energy_cal;
    
    //In order to re-use energy calibration coefficients (and not bother filling them out until
    //  we've matched up all the non-linear deviation pairs), we will temporarily store the
    //  calibration coefficients until we've ran through all the records, and then go back and set
    //  the energy calbration.
    map<vector<float>,vector<shared_ptr<Measurement>>> energy_coeffs_to_meas;
    
    
    while( input.good() && static_cast<size_t>(input.tellg()) < (filelen-256) )
    {
      ++record_number;
      
      const istream::pos_type specstart = input.tellg();
      
      /*
       Summary from lthard 20181109 wrt the GADRAS code for reading PCF files:
       The source + description + title can be 180 bytes. If it isn’t, it shifts the
       source over the description field, and if the description + source is longer
       than 180, it starts truncating the Title to fit all of the source.
       Also, the maximum length any of the Title, Description (which is only used for
       plotting purposes) and Source is 128 bytes.
       */
      string title_description_source;
      string spectrum_title;  //ex: 'Background Aa1  Distance=250 cm'
      string spectrum_desc; //ex '<T>HPGe 50%</T>' or '<T>Gamma</T>'
      string source_list;
      
      
      title_description_source.resize( 180 );
      input.read( &(title_description_source[0]), 180 );
      
      if( title_description_source[0] == char(0XFF) )
      {
        //I *think* all this string manipulation should be okay, but not well
        //  tested.
        title_description_source = title_description_source.substr(1);
        auto pos = title_description_source.find( char(0xFF) );
        spectrum_title = title_description_source.substr(0,pos);
        if( pos != string::npos )
        {
          title_description_source = title_description_source.substr(pos+1);
          if( !title_description_source.empty() )
          {
            pos = title_description_source.find( char(0xFF) );
            spectrum_desc = title_description_source.substr(0,pos);
            if( pos != string::npos )
              source_list = title_description_source.substr(pos+1);
          }
        }
      }else
      {
        spectrum_title = title_description_source.substr(0,60);
        spectrum_desc = title_description_source.substr(60,60);
        source_list = title_description_source.substr(120,60);
      }//if( we found the GADRAS delimiter ) / else
      
      size_t pos = spectrum_title.find_last_not_of( "\0 " );
      if( pos != string::npos )
        spectrum_title = spectrum_title.substr( 0, pos + 1 );
      
      pos = spectrum_desc.find_last_not_of( "\0 " );
      if( pos != string::npos )
        spectrum_desc = spectrum_desc.substr( 0, pos + 1 );
      
      pos = source_list.find_last_not_of( "\0 " );
      if( pos != string::npos )
        source_list = source_list.substr( 0, pos + 1 );
      
      trim( spectrum_title );
      trim( spectrum_desc );
      trim( source_list );
      
      //      static_assert( sizeof(float) == 4, "Float must be 4 bytes" );
      //      static_assert(std::numeric_limits<float>::digits >= 32);
      
      string collection_time;  //VAX Formatted like: "2014-Sep-19 14:12:01.62"
      collection_time.resize(23);
      input.read( &(collection_time[0]), collection_time.size() ); //203
      
      //cout << "The date/time characters read are:" << endl;
      //for( size_t i = 0; i < collection_time.size(); ++i )
      //  cout << "\t" << i+1 << ": " << collection_time[i] << ", " << int(collection_time[i]) << endl;
      
      
      int32_t num_channel;
      char character_tag;
      vector<float> energy_cal_terms( 5, 0.0f );
      float live_time, true_time, halflife, molecular_weight,
      spectrum_multiplier, unused_float, neutron_counts;
      
      input.read( &character_tag, 1 );                            //204
      input.read( (char *)&live_time, 4 );                        //208
      input.read( (char *)&true_time, 4 );                        //212
      input.read( (char *)&halflife, 4 );                         //216
      input.read( (char *)&molecular_weight, 4 );                 //220
      input.read( (char *)&spectrum_multiplier, 4 );              //224
      input.read( (char *)&energy_cal_terms[0], 4 );              //228
      input.read( (char *)&energy_cal_terms[1], 4 );              //232
      input.read( (char *)&energy_cal_terms[2], 4 );              //236
      input.read( (char *)&energy_cal_terms[3], 4 );              //240
      input.read( (char *)&energy_cal_terms[4], 4 );              //244
      input.read( (char *)&unused_float, 4 );                     //248
      input.read( (char *)&neutron_counts, 4 );                   //252
      input.read( (char *)&num_channel, 4 );                      //256
      
      //we have now read 256 bytes for this record
      
      if( num_channel == 0 )
      {
        //lets advance to the next expected spectrum
        if( input.seekg( 0 + specstart + bytes_per_record, ios::beg ) )
          continue;
        else
          break;
      }//if( num_channel == 0 )
      
      if( num_channel < 0 || num_channel>65536 )
      {
        char buffer[64];
        snprintf( buffer, sizeof(buffer),
                 "Invaid number of channels: %i", int(num_channel) );
        throw runtime_error( buffer );
      }//if( num_channel < 0 || num_channel>65536 )
      
      std::shared_ptr< vector<float> > channel_data = std::make_shared<vector<float> >( num_channel );
      input.read( (char *)&(channel_data->operator[](0)), 4*num_channel );
      
      // We'll do a little sanity check to make sure all teh floats we read are valid-ish and wont
      //  cause problems later on.
      auto ensure_valid_float = []( float &f ){
        if( IsNan(f) || IsInf(f) )
          f = 0.0f;
      };//ensure_valid_pos_float lambda
      
      ensure_valid_float( live_time );
      ensure_valid_float( true_time );
      live_time = (live_time < 0.0f) ? 0.0f : live_time;
      true_time = (true_time < 0.0f) ? 0.0f : true_time;
      
      ensure_valid_float( neutron_counts );
      for( float &f : energy_cal_terms )
        ensure_valid_float( f );
      
      for( float &f : *channel_data )  //This should probably be vectorized or something
        ensure_valid_float( f );
      
      auto meas = std::make_shared<Measurement>();
      
      const istream::pos_type specend = input.tellg();
      const size_t speclen = static_cast<size_t>( 0 + specend - specstart );
      if( speclen != bytes_per_record )
      {
        if( speclen > bytes_per_record )
        {
          const string msg = "SpecFile::load_from_pcf(...):\n\tUnexpected record length, expected "
                             + std::to_string(256*NRPS) + " but got length "
                             + std::to_string(speclen) + ", - am forcing correct position in file";
          meas->parse_warnings_.push_back( msg );
        }//
        
        //For the last spectrum in the file may extend beyond the end of the
        //  file since NRPS may be larger than necessary to capture all the
        //  spectral information, so the extra space is left out of the file
        const size_t nextpos = static_cast<size_t>( 0 + specstart + bytes_per_record );
        if( nextpos > filelen )
          input.seekg( filelen, ios::beg );
        else
          input.seekg( 0 + specstart + bytes_per_record, ios::beg );
      }//if( speclen != (4*NRPS) )
      
      if( spectrum_multiplier > 1.0f && !IsInf(spectrum_multiplier) && !IsNan(spectrum_multiplier) )
      {
        for( float &f : *channel_data )
          f *= spectrum_multiplier;
      }//if( spectrum_multiplier!=0.0 && spectrum_multiplier!=1.0 )
      
      //cout << "For record " << record_number << ", num_channel=" << num_channel << " with bytes_per_record=" << bytes_per_record << endl;
      
      if( (record_number == 1) && SpecUtils::iequals_ascii(spectrum_title,"Energy") )
      {
        bool increasing = true;
        for( int32_t channel = 1; increasing && (channel < num_channel); ++channel )
          increasing = ((*channel_data)[channel] >= (*channel_data)[channel-1]);
        
        if( increasing && num_channel > 2 )
        {
          //It looks like we should also check that live and real times is 1.0f
          try
          {
            lower_channel_energy_cal = make_shared<EnergyCalibration>();
            lower_channel_energy_cal->set_lower_channel_energy( channel_data->size() - 1,
                                                                std::move(*channel_data) );
          }catch( std::exception & )
          {
            //shouldnt ever get here
          }
          
          continue;
        }//if( increasing )
      }//if( record_number == 1 and title=="Energy" )
      
      //If we're here, were keeping meas.
      
      measurements_.push_back( meas );
      meas->live_time_ = live_time;
      meas->real_time_ = true_time;
      
      meas->location_ = gps_location;
      
      const bool has_neutrons = (neutron_counts > 0.00000001);
      meas->contained_neutron_ = has_neutrons;
      any_contained_neutron = (any_contained_neutron || has_neutrons);
      all_contained_neutron = (all_contained_neutron && has_neutrons);
      
      for( const float f : *channel_data )
        meas->gamma_count_sum_ += f;
      meas->neutron_counts_.resize( 1 );
      meas->neutron_counts_[0] = neutron_counts;
      meas->neutron_counts_sum_ = neutron_counts;
      
      float dx = std::numeric_limits<float>::quiet_NaN();
      float dy = dx, dz = dx, speed = dx;
      
      try{ dx = dx_from_remark(spectrum_title); }catch( std::exception & ){ }
      try{ dy = dy_from_remark(spectrum_title); }catch( std::exception & ){ }
      try{ dz = dz_from_remark(spectrum_title); }catch( std::exception & ){ }
      try{ speed = speed_from_remark(spectrum_title); }catch( std::exception & ){ }
      const string distance = distance_from_pcf_title(spectrum_title);
      
      if( !IsNan(speed) || !IsNan(dx) || !IsNan(dy) || !IsNan(dz) || !distance.empty() )
      {
        auto location = make_shared<LocationState>();
        
        if( gps_location && gps_location->geo_location_ )
          location->geo_location_ = gps_location->geo_location_;
        
        location->type_ = LocationState::StateType::Item;
        location->speed_ = speed;
        auto rel_loc = make_shared<RelativeLocation>();
        location->relative_location_ = rel_loc;
        rel_loc->from_cartesian( 10.0f*dx, 10.0f*dy, 10.0f*dz );
        rel_loc->origin_description_ = distance;
        
        meas->location_ = location;
      }//if( !IsNan(speed) || !IsNan(dx) || !IsNan(dy) || !IsNan(dz) )
      
      meas->detector_name_ = detector_name_from_remark( spectrum_title );
      meas->sample_number_ = sample_num_from_remark( spectrum_title );
      
      if( meas->sample_number_ < 0 )
        allSamplesHaveNumbers = false;
      someSamplesHaveNumbers = (someSamplesHaveNumbers || (meas->sample_number_ >= 0));
      
      meas->start_time_ = time_from_string( collection_time.c_str() );
      
      //XXX test for Background below not tested
      if( SpecUtils::icontains( spectrum_title, "Background" ) )
        meas->source_type_ = SourceType::Background;
      else if( SpecUtils::icontains( spectrum_title, "Calib" ) )
        meas->source_type_ = SourceType::Calibration;
      else //if( spectrum_title.find("Foreground") != string::npos )
        meas->source_type_ = SourceType::Foreground;
      //else meas->source_type_ = SourceType::Unknown
      
      meas->title_ = spectrum_title;
      
      if( !spectrum_desc.empty() )
        meas->remarks_.push_back( "Description: " + spectrum_desc );
      
      if( !source_list.empty() )
        meas->remarks_.push_back( "Source: " + source_list );
      
      if( character_tag == '-' )
      {
        meas->occupied_ =  OccupancyStatus::NotOccupied;
      }else if( character_tag == ' ' )
      {
        //If the data isnt portal data, then will change to Unknown
        meas->occupied_ = OccupancyStatus::Occupied;
        
        //Background spectra should not have the tag character be a dash, as the
        //  tag chacter could indicate calibration isotope.
        if( meas->source_type_ == SourceType::Background )
          meas->occupied_ =  OccupancyStatus::NotOccupied;
      }else
      {
        meas->occupied_ = OccupancyStatus::Unknown;
      }
      
      while( energy_cal_terms.size() && (energy_cal_terms.back()==0.0f) )
        energy_cal_terms.erase( energy_cal_terms.begin() + energy_cal_terms.size() - 1 );
      
      if( lower_channel_energy_cal )
      {
        //Note: at least for DB.pcf I checked:
        //  (lower_channel_energy_cal->coefficients()->back()==(energy_cal_terms[0]+energy_cal_terms[1]))
        if( lower_channel_energy_cal->coefficients().size() == (channel_data->size()+1) )
        {
          meas->energy_calibration_ = lower_channel_energy_cal;
        }else
        {
          /// \TODO: if we have less energy channels, could make a new lower channel calibration...
          meas->parse_warnings_.push_back( "PCF specified lower channel energies, but number of"
                                           " channels didnt match up for this record." );
          energy_coeffs_to_meas[energy_cal_terms].push_back( meas );
        }
      }else
      {
        energy_coeffs_to_meas[energy_cal_terms].push_back( meas );
      }
      
      meas->gamma_counts_ = channel_data;
      
      detector_names.insert( meas->detector_name_ );
    }//while( stream.good() && stream.tellg() < size )
    
    if( any_contained_neutron && !all_contained_neutron )
    {
      for( auto &p : measurements_ )
        p->contained_neutron_ = true;
    }else if( !any_contained_neutron )
    {
      for( auto &p : measurements_ )
      {
        p->neutron_counts_.clear();
        p->neutron_counts_sum_ = 0.0;
      }
    }//if( we had some neutrons ) / else
    
    
    if( measurements_.empty() )
      throw runtime_error( "Didnt read in any Measurements" );
    
    
    if( !allSamplesHaveNumbers )
    {
      if( someSamplesHaveNumbers )
      {
        //Find the first sample, and then work back from there decrementing
        size_t first_sample;
        for( first_sample = 0; first_sample < measurements_.size(); ++first_sample )
          if( measurements_[first_sample]->sample_number_ >= 0 )
            break;
        
        if( first_sample >= measurements_.size() )  //SHouldnt ever happen
        {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          log_developer_error( __func__, "Logic error: someSamplesHaveNumbers is true, but could find meas now!" );
#endif //PERFORM_DEVELOPER_CHECKS
          throw runtime_error( "someSamplesHaveNumbers was a lie!" );
        }//if( first_sample >= measurements_.size() )
        
        //Make a hack attempt to assign somewhat reasonable sampel numbers...untested as of 20190109
        map<string,set<int>> sample_nums;
        for( size_t i = first_sample; i < measurements_.size(); ++i )
          if( measurements_[i]->sample_number_ >= 0 )
            sample_nums[measurements_[i]->detector_name_].insert( measurements_[i]->sample_number_ );
        
        int last_assigned = -1;
        for( size_t i = first_sample; i > 0; --i )
        {
          auto &m = measurements_[i];
          int val = -1;
          set<int> &samples = sample_nums[m->detector_name_];
          if( !samples.count(last_assigned) )
            val = last_assigned;
          else if( samples.size() )
            val = (*samples.begin())-1;
          
          last_assigned = val;
          samples.insert( val );
          m->sample_number_ = val;
        }//for( get measurements at start of file )
        
        for( size_t i = first_sample; i < measurements_.size(); ++i )
        {
          auto &m = measurements_[i];
          if( m->sample_number_ >= 0 )
          {
            last_assigned = m->sample_number_;
            continue;
          }
          
          set<int> &samples = sample_nums[m->detector_name_];
          int samplenum = last_assigned;
          while( samples.count(samplenum) )
            ++samplenum;
          
          last_assigned = samplenum;
          samples.insert( samplenum );
          m->sample_number_ = samplenum;
        }//for( size_t i = first_sample; i < measurements_.size(); ++i )
      }else
      {
        int sample_num = 1;
        set<string> detectors_seen;
        for( auto &meas : measurements_ )
        {
          if( detectors_seen.count(meas->detector_name_) )
          {
            ++sample_num;
            detectors_seen.clear();
          }
          meas->sample_number_ = sample_num;
          detectors_seen.insert( meas->detector_name_ );
        }//for( auto &meas : measurements_ )
      }//if( some have sample numbers ) / else ( none have sample numbers )
    }//if( !allSamplesHaveNumbers )
    
    /*
     //Assign sample numbers if the titles all specified the sample numbers
     if( titlesamplenum >= 0 )
     sample_numbers_from_title[meas] = titlesamplenum;
     
     
     map<std::shared_ptr<Measurement>,int> sample_numbers_from_title;
     
     sample_numbers[meas->detector_name_] = std::max( meas->sample_number_,
     sample_numbers[meas->detector_name_] );
     if( sample_numbers_from_title.size() )
     {
     }
     if( sample_numbers_from_title.size() == measurements_.size() )
     {
     for( auto &p : measurements_ )
     p->sample_number_ = sample_numbers_from_title[p];
     }
     */
    
    //Now map from the detector name to deviation pairs it should use.
    map<string,vector<pair<float,float>>> det_name_to_devs;
    
    if( have_deviation_pairs )
    {
      bool used_deviation_pairs[4][8][8] = {}; //iniitalizes tp zero/false
      
      //Assign deviation pairs to detectors with names like "Aa1", "Ab2", etc.
      for( const string &name : detector_names )
      {
        int col, panel, mca;
        pcf_det_name_to_dev_pair_index( name, col, panel, mca );
        if( col < 0 || panel < 0 || mca < 0 || col > (compressed_devpair ? 1 : 3) || panel > 7 || mca > 7 )
          continue;
        
        det_name_to_devs[name] = deviation_pairs[col][panel][mca];
        used_deviation_pairs[col][panel][mca] = true;
      }//for( string name : detector_names )
      
      //Now assign dev pairs to remaining detectors, assuming they were put
      //  in the first available location
      //  TODO: Check with GADRAS team to see how this should actually be handled
      for( const string &name : detector_names )
      {
        if( det_name_to_devs.count(name) )
          continue;
        for( int col = 0; col < (compressed_devpair ? 4 : 2); ++col )
        {
          for( int panel = 0; panel < 8; ++panel )
          {
            for( int mca = 0; mca < 8; ++mca )
            {
              if( !used_deviation_pairs[col][panel][mca] )
              {
                used_deviation_pairs[col][panel][mca] = true;
                det_name_to_devs[name] = deviation_pairs[col][panel][mca];
                col = panel = mca = 10;
              }//if( we found a dev pairs we havent used yet )
            }//for( int panel = 0; panel < 8; ++panel )
          }//for( int panel = 0; panel < 8; ++panel )
        }//for( int col = 0; col < (compressed_devpair ? 4 : 2); ++col )
      }//for( const string &name : detector_names )
      
#if(PERFORM_DEVELOPER_CHECKS&& !SpecUtils_BUILD_FUZZING_TESTS)
      bool unused_dev_pairs = false;
      for( int col = 0; col < (compressed_devpair ? 4 : 2); ++col )
      {
        for( int panel = 0; panel < 8; ++panel )
        {
          for( int mca = 0; mca < 8; ++mca )
          {
            if( !deviation_pairs[col][panel][mca].empty()
               && !used_deviation_pairs[col][panel][mca] )
              unused_dev_pairs = true;
          }
        }
      }//for( int col = 0; col < (compressed_devpair ? 4 : 2); ++col )
      
      if( unused_dev_pairs )
      {
        log_developer_error( __func__, "Read in deviation pairs that did not get assigned to a detector" );
      }//if( unused_dev_pairs )
#endif
    }//if( have_deviation_pairs )
    
    //Finally set the energy calibration for Measurements in energy_coeffs_to_meas, now that we
    //  have all the information we need.
    //We will create a cache of energy calibration information to EnergyCalibration objects so we
    //  can minimize memory usage.
    typedef tuple<size_t,vector<float>,vector<pair<float,float>>> RawCalInfo_t;
    map< RawCalInfo_t, shared_ptr<EnergyCalibration> > prev_cals;
    for( auto &coef_to_measv : energy_coeffs_to_meas )
    {
      //Note: lower_channel_energy_cal may be valid here if for some reason some records have a
      //      different number of channels that the channel energies given by the detector.
      //assert( !lower_channel_energy_cal );
      
      const vector<float> &coefs = coef_to_measv.first;
      const vector<shared_ptr<Measurement>> &meas_for_coefs = coef_to_measv.second;
      
      //If coefs is empty, the calibration coefficients were all zero; a default energy cal will be
      //  assigned in cleanup_after_load.
      if( coefs.size() == 0 )
        continue;
      
      if( coefs.size() == 1 )
      {
        string msg = "PCF FRF calibration only had one coefficient (" + to_string(coefs[0]) + ")";
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        //We probably shouldnt ever run into this, so log it, since it might be an error in this code
        log_developer_error( __func__, msg.c_str() );
#endif
        for( const shared_ptr<Measurement> &meas : meas_for_coefs )
          meas->parse_warnings_.push_back( msg );
        continue;  //Use default calibration
      }//if( coefs.size() == 1 )
      
      for( const shared_ptr<Measurement> &meas : meas_for_coefs )
      {
        const size_t nchannel = meas->num_gamma_channels();
        if( nchannel < 2 )
          continue;
        
        RawCalInfo_t calinfo{ nchannel, coefs, {} };
        if( have_deviation_pairs )
        {
          auto dev_pos = det_name_to_devs.find(meas->detector_name_);
          if( dev_pos != end(det_name_to_devs) )
            std::get<2>(calinfo) = dev_pos->second;
        }//if( have_deviation_pairs )
        
        auto prevpos = prev_cals.find( calinfo );
        if( prevpos != end(prev_cals) )
        {
          meas->energy_calibration_ = prevpos->second;
        }else
        {
          try
          {
            auto newcal = make_shared<EnergyCalibration>();
            newcal->set_full_range_fraction( nchannel, coefs, std::get<2>(calinfo) );
            prev_cals[calinfo] = newcal;
            meas->energy_calibration_ = newcal;
          }catch( std::exception &e )
          {
            meas->parse_warnings_.push_back( "PCF FRF calibration invalid: " + string(e.what()) );
          }
        }
      }//for( loop over Measurements that share these coefficients )
    }//for( auto &coef_to_measv : energy_coeffs_to_meas )
    
    
    cleanup_after_load( DontChangeOrReorderSamples );
    
    
    //We dont want it indicate occupied/not-occupied for non portal data, but
    //  since the tag character is a little ambiguous, we'll try a cleanup here.
    if( !passthrough() )
    {
      for( auto &m : measurements_ )
        m->occupied_ = OccupancyStatus::Unknown;
    }//if( !passthrough() )
  }catch( std::exception & )
  {
    input.clear();
    input.seekg( orig_pos, ios::beg );
    
    //cerr << "SpecFile::load_from_pcf(...)\n\tCaught:" << e.what() << endl;
    
    reset();
    return false;
  }//try / catch
  
  return true;
}//bool load_from_pcf( std::istream& istr )

}//namespace SpecUtils
