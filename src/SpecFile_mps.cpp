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
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/EnergyCalibration.h"
#include "SpecUtils/SpecFile_location.h"

using namespace std;


namespace SpecUtils
{
bool SpecFile::load_tracs_mps_file( const std::string &filename )
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
  const bool loaded = load_from_tracs_mps( file );
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_tracs_mps_file( const std::string &filename )
  
  
bool SpecFile::load_from_tracs_mps( std::istream &input )
{
  /*
   Cabin Data
   Byte offset  Size  Description
   0  8  Memory address
   8  4  Memory address
   12  4  Connect Status
   16  4  Event
   20  4  Neutron AlarmLevel
   24  4  Gamma Alarm Level
   28  4  Ratio Alarm Level
   32  8  Latitude
   40  8  Longitude
   48  4  GPS Time of Day
   52  4  #1 pod status
   56  4  #2 pod status
   60  4  #1 det status
   64  4  #2 det status
   68  4  #3 det status
   72  4  #4 det status
   76  4  Index Number
   80  4  Neutron GC
   84  4  Gamma GC
   88  2048  Sum Spectra
   2136  4  Pod1 Index Number
   2140  4  Pod1 deltaTau
   
   2144  4  Pod1 Det1 Neutron GC
   2148  4  Pod1 Det2 Neutron GC
   
   2152  4  Pod1 Det1 Gamma GC
   2156  4  Pod1 Det2 Gamma GC
   2160  4  Pod1 Det1 DAC
   2164  4  Pod1 Det2 DAC
   2168  4  Pod1 Det1 calibration Peak
   2172  4  Pod1 Det2 calibration Peak
   2176  4  Pod1 Det1 calibration peak found
   2180  4  Pod1 Det2 calibration peak found
   
   2184  2048  Pod1 Det1 spectra
   4232  2  Pod1 Det1 clock time
   4234  2  Pod1 Det1 dead time
   4236  2  Pod1 Det1 live time
   
   4238  2048  Pod1 Det2 spectra
   6286  2  Pod1 Det2 clock time
   6288  2  Pod1 Det2 dead time
   6290  2  Pod1 Det2 live time
   
   6292  4  Pod2 Index Number
   6296  4  Pod2 deltaTau
   
   6300  4  Pod2 Det1 Neutron GC
   6304  4  Pod2 Det2 Neutron GC
   
   6308  4  Pod2 Det1 Gamma GC
   6312  4  Pod2 Det2 Gamma GC
   6316  4  Pod2 Det1 DAC
   6320  4  Pod2 Det2 DAC
   6324  4  Pod2 Det1 calibration Peak
   6328  4  Pod2 Det2 calibration Peak
   6332  4  Pod2 Det1 calibration peak found
   6336  4  Pod2 Det2 calibration peak found
   
   6340  2048  Pod2 Det1 spectra
   8388  2  Pod2 Det1 clock time
   8390  2  Pod2 Det1 dead time
   8392  2  Pod2 Det1 live time
   
   8394  2048  Pod2 Det2 spectra
   10442  2  Pod2 Det2 clock time
   10444  2  Pod2 Det2 dead time
   10446  2  Pod2 Det2 live time
   
   10448  4  Radar Altimeter
   10452  128  GPS String
   10580  8  GPS Source
   10588  6  GPS Age
   10594  3  GPS Num SV
   10597
   */
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  reset();
  
  const size_t samplesize = 10597;
  
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  input.seekg( 0, ios::end );
  const istream::pos_type eof_pos = input.tellg();
  input.seekg( orig_pos, ios::beg );
  
  const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );
  const size_t numsamples = filesize / samplesize;
  const size_t leftoverbytes = (filesize % samplesize);
  
  if( leftoverbytes )
    return false;
  
  try
  {
    for( size_t sample = 0; sample < numsamples; ++sample )
    {
      double lat, lon;
      const size_t startpos = sample * samplesize;
      uint32_t gpsTOD, indexNum, overallGammaGC, overallNeutronGC, radarAltimiter;
      
      if( !input.seekg(startpos+32, ios::beg) )
        throw runtime_error( "Failed seek 1" );
      if( !input.read( (char *)&lat, sizeof(lat) ) )
        throw runtime_error( "Failed read 2" );
      if( !input.read( (char *)&lon, sizeof(lon) ) )
        throw runtime_error( "Failed read 3" );
      if( !input.read( (char *)&gpsTOD, sizeof(gpsTOD) ) )
        throw runtime_error( "Failed read 4" );
      if( !input.seekg(startpos+76, ios::beg) )
        throw runtime_error( "Failed read 5" );
      if( !input.read( (char *)&indexNum, sizeof(indexNum) ) )
        throw runtime_error( "Failed read 6" );
      if( !input.read( (char *)&overallNeutronGC, sizeof(overallNeutronGC) ) )
        throw runtime_error( "Failed read 7" );
      if( !input.read( (char *)&overallGammaGC, sizeof(overallGammaGC) ) )
        throw runtime_error( "Failed read 8" );
      
      char gpsstr[129];
      gpsstr[128] = '\0';
      if( !input.seekg(startpos+10448, ios::beg) )
        throw runtime_error( "Failed seek 9" );
      if( !input.read( (char *)&radarAltimiter, sizeof(radarAltimiter) ) )
        throw runtime_error( "Failed read 10" );
      if( !input.read( gpsstr, 128 ) )
        throw runtime_error( "Failed read 11" );
      
      for( size_t i = 0; i < 4; ++i )
      {
        const char *title;
        size_t datastart, neutrongc, gammagc, detstatus;
        
        switch( i )
        {
          case 0:
            detstatus = 60;
            datastart = 2184;
            gammagc   = 2152;
            neutrongc = 2144;
            title = "Pod 1, Det 1";
            break;
            
          case 1:
            detstatus = 64;
            datastart = 4238;
            gammagc   = 2156;
            neutrongc = 2148;
            title = "Pod 1, Det 2";
            break;
            
          case 2:
            detstatus = 68;
            datastart = 6340;
            gammagc   = 6308;
            neutrongc = 6300;
            title = "Pod 2, Det 1";
            break;
            
          case 3:
            detstatus = 72;
            datastart = 8394;
            gammagc   = 6312;
            neutrongc = 6304;
            title = "Pod 2, Det 2";
            break;
        }//switch( i )
        
        uint32_t neutroncount;
        uint16_t channeldata[1024];
        uint16_t realtime, livetime, deadtime;
        uint32_t gammaGC, detDAC, calPeak, calPeakFound, status, dummy;
        
        if( !input.seekg(startpos+detstatus, ios::beg) )
          throw runtime_error( "Failed seek 12" );
        if( !input.read( (char *)&status, sizeof(status) ) )
          throw runtime_error( "Failed read 13" );
        
        if( !input.seekg(startpos+gammagc, ios::beg) )
          throw runtime_error( "Failed seek 14" );
        
        if( !input.read( (char *)&gammaGC, sizeof(gammaGC) ) )
          throw runtime_error( "Failed read 15" );
        if( !input.read( (char *)&dummy, sizeof(dummy) ) )
          throw runtime_error( "Failed read 16" );
        
        if( !input.read( (char *)&detDAC, sizeof(detDAC) ) )
          throw runtime_error( "Failed read 17" );
        if( !input.read( (char *)&dummy, sizeof(dummy) ) )
          throw runtime_error( "Failed read 18" );
        
        if( !input.read( (char *)&calPeak, sizeof(calPeak) ) )
          throw runtime_error( "Failed read 19" );
        if( !input.read( (char *)&dummy, sizeof(dummy) ) )
          throw runtime_error( "Failed read 20" );
        
        if( !input.read( (char *)&calPeakFound, sizeof(calPeakFound) ) )
          throw runtime_error( "Failed read 21" );
        if( !input.read( (char *)&dummy, sizeof(dummy) ) )
          throw runtime_error( "Failed read 22" );
        
        if( !input.seekg(startpos+datastart, ios::beg) )
          throw runtime_error( "Failed seek 23" );
        
        if( !input.read( (char *)channeldata, sizeof(channeldata) ) )
          throw runtime_error( "Failed read 24" );
        
        if( !input.read( (char *)&realtime, sizeof(realtime) ) )
          throw runtime_error( "Failed read 25" );
        
        //if realtime == 6250, then its about 1 second... - I have no idea what these units means (25000/4?)
        
        if( !input.read( (char *)&deadtime, sizeof(deadtime) ) )
          throw runtime_error( "Failed read 26" );
        
        if( !input.read( (char *)&livetime, sizeof(livetime) ) )
          throw runtime_error( "Failed read 27" );
        
        if( !input.seekg(startpos+neutrongc, ios::beg) )
          throw runtime_error( "Failed seek 28" );
        
        if( !input.read( (char *)&neutroncount, sizeof(neutroncount) ) )
          throw runtime_error( "Failed read 29" );
        
        auto m = std::make_shared<Measurement>();
        m->live_time_ = livetime / 6250.0f;
        m->real_time_ = realtime / 6250.0f;
        m->contained_neutron_ = (((i%2)!=1) || neutroncount);
        m->sample_number_ = static_cast<int>( sample + 1 );
        m->occupied_ = OccupancyStatus::Unknown;
        m->gamma_count_sum_ = 0.0;
        // Cast to neutron counts to float to match what we are putting in m->neutron_counts_, so
        //  if we check answer in SpecFile::recalc_total_counts(), we are consistent (although now
        //  less precise).
        m->neutron_counts_sum_ = static_cast<float>(neutroncount);
        //        m->speed_ = ;
        m->detector_name_ = title;
        m->detector_number_ = static_cast<int>( i );
        //        m->detector_type_ = "";
        m->quality_status_ = (status==0 ? SpecUtils::QualityStatus::Good : SpecUtils::QualityStatus::Suspect);
        m->source_type_  = SourceType::Unknown;
        
        if( calPeakFound != 0 )
        {
          try
          {
            auto newcal = make_shared<EnergyCalibration>();
            newcal->set_polynomial(1024, {0.0f, (1460.0f/calPeakFound)}, {} );
            m->energy_calibration_ = newcal;
          }catch( std::exception & )
          {
            //probably wont ever get here.
          }
        }//if( calPeakFound != 0 ) / else
        
        vector<float> *gammacounts = new vector<float>( 1024 );
        m->gamma_counts_.reset( gammacounts );
        for( size_t i = 0; i < 1024; ++i )
        {
          const float val = static_cast<float>( channeldata[i] );
          m->gamma_count_sum_ += val;
          (*gammacounts)[i] = val;
        }
        
        if( m->contained_neutron_ )
          m->neutron_counts_.resize( 1, static_cast<float>(neutroncount) );
        
        if( valid_longitude(lon) && valid_latitude(lat) )
        {
          auto loc = make_shared<LocationState>();
          m->location_ = loc;
          auto geo = make_shared<GeographicPoint>();
          loc->geo_location_ = geo;
          geo->latitude_ = lat;
          geo->longitude_ = lon;
        }
        
        m->title_ = title;
        
        measurements_.push_back( m );
      }//for( size_t i = 0; i < 4; ++i )
    }//for( size_t sample = 0; sample < numsamples; ++sample )
    
    cleanup_after_load();
    
    if( measurements_.empty() )
      throw std::runtime_error( "no measurements" );
  }catch( std::exception & )
  {
    //cerr << SRC_LOCATION << "\n\tCaught: " << e.what() << endl;
    input.clear();
    input.seekg( orig_pos, ios::beg );
    reset();
    return false;
  }//try / catch
  
  return true;
}//bool load_from_tracs_mps( std::istream &input )
  

}//namespace SpecUtils

