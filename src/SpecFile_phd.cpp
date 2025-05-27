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
#include <sstream>
#include <iostream>
#include <stdexcept>


#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/SpecFile.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;


namespace SpecUtils
{
bool SpecFile::load_phd_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_phd( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_phd_file( const std::string &filename );

  
bool SpecFile::load_from_phd( std::istream &input )
{
  //Note: this function implemented off using only a couple files from a single
  //      source to determine file format; there is likely some assumptions that
  //      could be loosened or tightened up.
  
  reset();
  
  if( !input.good() )
    return false;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
  
  const istream::pos_type orig_pos = input.tellg();
  
  try
  {
    string line;
    size_t linenum = 0;  //for debug purposes only
    bool tested_first_line = false;
    while( input.good() )
    {
      const size_t max_len = 1024*1024;  //all the files I've seen have been less than like 80 characters
      SpecUtils::safe_get_line( input, line, max_len );
      ++linenum;
      
      if( line.size() >= (max_len-1) )
        throw runtime_error( "Line greater than 1MB" );
      
      if( linenum > 32*1024 )  //2048 would probably be plenty
        throw runtime_error( "Too many lines for PHD format" );
      
      trim( line );
      
      if( line.empty() )
        continue;
      
      if( !tested_first_line )
      {
        //First line for all files I've seen is "BEGIN IMS2.0"
        tested_first_line = true;
        if( !SpecUtils::istarts_with( line, "BEGIN" ) )
          throw runtime_error( "First line of PHD file must start with 'BEGIN'" );
        
        continue;
      }//if( !tested_first_line )
      
      if( SpecUtils::istarts_with( line, "#Collection") )
      {
        SpecUtils::safe_get_line( input, line, max_len );
        ++linenum;
        //line is somethign like "2012/10/11 09:34:51.7 2011/10/13 09:32:43.6 14377.2   "
        continue;
      }//if( SpecUtils::istarts_with( line, "#Collection") )
      
      
      if( SpecUtils::istarts_with( line, "#Acquisition") )
      {
        SpecUtils::safe_get_line( input, line, max_len );
        ++linenum;
        trim( line );
        //line is somethign like "2012/09/15 09:52:14.0 3605.0        3600.0"
        vector<string> fields;
        SpecUtils::split( fields, line, " \t");
        if( fields.size() < 4 )
          continue;
        
        //We wont worry about conversion error for now
        stringstream(fields[2]) >> meas->real_time_;
        stringstream(fields[3]) >> meas->live_time_;
        meas->start_time_ = SpecUtils::time_from_string( (fields[0] + " " + fields[1]).c_str() );
        continue;
      }//if( SpecUtils::istarts_with( line, "#Acquisition") )
      
      
      if( SpecUtils::istarts_with( line, "#g_Spectrum") )
      {
        // We should only see this element once, so lets check for an unexpected format; we'll just
        //  add a warning, and not full-blown support since I havent actually seen a file like this.
        if( meas->gamma_counts_ && !meas->gamma_counts_->empty() )
        {
          auto &prevwarn = meas->parse_warnings_;
          string warning = "Multiple spectrum elements found in PHD file; only using last one.";
          const auto warnpos = std::find( begin(prevwarn), end(prevwarn), warning );
          if( warnpos == end(prevwarn) )
            prevwarn.push_back( std::move(warning) );
        }//if( we have already set the gamma counts )
        
        SpecUtils::safe_get_line( input, line, max_len );
        ++linenum;
        trim( line );
        //line is something like "8192  2720.5"
        vector<float> fields;
        SpecUtils::split_to_floats( line, fields );
        
        if( fields.empty() || fields[0]<32 || fields[0]>65536 || floorf(fields[0])!=fields[0] )
          throw runtime_error( "Line after #g_Spectrum not as expected" );
        
        const float upper_energy = (fields.size()>1 && fields[1]>500.0f && fields[1]<13000.0f) ? fields[1] : 0.0f;
        const size_t nchannel = static_cast<size_t>( fields[0] );
        if( (nchannel < 1) || (nchannel > 1024*65536) )
          throw runtime_error( "Invalid number of channels (" + std::to_string(nchannel) + ")" );
        
        auto counts = std::make_shared< vector<float> >(nchannel,0.0f);
        
        size_t last_channel = 0;
        while( SpecUtils::safe_get_line(input, line, max_len) )
        {
          SpecUtils::trim( line );
          if( line.empty() )
            continue;
          
          if( line[0] == '#')
            break;
          
          SpecUtils::split_to_floats( line, fields );
          if( fields.empty() ) //allow blank lines
            continue;
          
          if( fields.size() == 1 )  //you need at least two rows for phd format
            throw runtime_error( "Unexpected spectrum data line-size" );
          
          if( (floorf(fields[0]) != fields[0]) || (fields[0] < 0.0f) || IsNan(fields[0]) || IsInf(fields[0]) )
            throw runtime_error( "First col of spectrum data must be integer >= 0" );
          
          const size_t start_channel = static_cast<size_t>( fields[0] );
          
          if( (last_channel != 0) && ((start_channel <= last_channel) || (start_channel > nchannel)) )
            throw runtime_error( "Channels not ordered as expected" );

          //We'll let the fuzzing test give us out-of-order channels, but otherwise require the file
          //  to give us channels in-order
#if( !SpecUtils_BUILD_FUZZING_TESTS )
          last_channel = start_channel;
#endif
          
          for( size_t i = 1; (i < fields.size()) && (start_channel+i-2)<nchannel; ++i )
            (*counts)[start_channel + i - 2] = fields[i];
        }//while( spectrum data )
        
        meas->gamma_counts_ = counts;
        
        // We could probably sum the counts as we read them in, but we'll just wait to do it here
        //  incase we have multiple #g_Spectrum (of which, we'll take just the last one)
        meas->gamma_count_sum_ = 0.0;
        for( const float &cc : *counts )
          meas->gamma_count_sum_ += cc;
        
        if( upper_energy > 0.0f && nchannel > 0 )
        {
          //There is maybe better energy calibration in the file, but since I
          //  so rarely see this file format, I'm not bothering with parsing it.
          try
          {
            auto newcal = make_shared<EnergyCalibration>();
            newcal->set_full_range_fraction( nchannel, {0.0f,upper_energy}, {} );
            meas->energy_calibration_ = newcal;
          }catch( std::exception & )
          {
            //shouldnt ever get here.
          }//try / catch
        }//if( we have energy range info )
      }//if( SpecUtils::istarts_with( line, "#g_Spectrum") )
      
      if( SpecUtils::istarts_with( line, "#Calibration") )
      {
        //Following line gives datetime of calibration
      }//if( "#Calibration" )
      
      if( SpecUtils::istarts_with( line, "#g_Energy") )
      {
        //Following lines look like:
        //59.540           176.1400         0.02968
        //88.040           260.7800         0.00000
        //122.060          361.7500         0.00000
        //165.860          491.7100         0.02968
        //...
        //1836.060         5448.4400        0.02968
      }//if( "#g_Energy" )
      
      if( SpecUtils::istarts_with( line, "#g_Resolution") )
      {
        //Following lines look like:
        //59.540           0.9400           0.00705
        //88.040           0.9700           0.00669
        //122.060          1.0100           0.00151
        //165.860          1.0600           0.00594
        //...
        //1836.060         2.3100           0.00393
      }//if( "#g_Resolution" )
      
      if( SpecUtils::istarts_with( line, "#g_Efficiency") )
      {
        //Following lines look like:
        //59.540           0.031033         0.0002359
        //88.040           0.083044         0.0023501
        //122.060          0.107080         0.0044224
        //165.860          0.103710         0.0026757
        //...
        //1836.060         0.020817         0.0012261
      }//if( "#g_Efficiency" )
    }//while( input.good() )
    
    if( !meas->gamma_counts_ || meas->gamma_counts_->empty() )
      throw runtime_error( "Didnt find gamma spectrum" );
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
  }catch( std::exception &e )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
    return false;
  }
  
  return true;
}//bool load_from_phd( std::istream &input );

}//namespace SpecUtils





