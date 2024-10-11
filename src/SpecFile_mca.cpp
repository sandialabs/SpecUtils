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
  
  
  string getAmptekMcaLineInfo( const string &data, const string &heading )
  {
    const size_t pos = data.find( heading );
    if( pos == string::npos )
      return "";
    const size_t end = data.find_first_of( "\r\n", pos );
    if( end == string::npos )
      return "";
    return data.substr( pos + heading.size(), end - pos - heading.size() );
  }//getAmptekMcaLineInfo(...)
  
}//namespace


namespace SpecUtils
{
  
bool SpecFile::load_amptek_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_amptek_mca( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_amptek_file( const std::string &filename )

  
bool SpecFile::load_from_amptek_mca( std::istream &input )
{
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  
  char firstline[18];
  input.read( firstline, 17 );
  firstline[17] = '\0';
  
  if( strcmp(firstline, "<<PMCA SPECTRUM>>") != 0 )
    return false;
  
  input.seekg( 0, ios::end );
  const istream::pos_type eof_pos = input.tellg();
  input.seekg( orig_pos, ios::beg );
  
  const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );
  
  
  //Assume maximum file size of 2.5 MB - this is _way_ more than expected for
  //  even a 16k channel spectrum
  if( filesize > 2.5*1024*1024 )
    return false;
  
  try
  {
    auto meas = std::make_shared<Measurement>();
    
    string filedata;
    filedata.resize( filesize );
    input.read( &filedata[0], filesize );
    
    string lineinfo = getAmptekMcaLineInfo( filedata, "TAG - " );
    if( !lineinfo.empty() )
      remarks_.push_back( "Tag: " + lineinfo );
    
    lineinfo = getAmptekMcaLineInfo( filedata, "DESCRIPTION - " );
    if( !lineinfo.empty() )
      meas->measurement_description_ = lineinfo;
    
    float energy_gain = 0.0f;
    lineinfo = getAmptekMcaLineInfo( filedata, "GAIN - " );
    if( !lineinfo.empty() )
    {
      if( !toFloat(lineinfo,energy_gain) )
        energy_gain = 0.0f;
    }//if( !lineinfo.empty() )
    
    lineinfo = getAmptekMcaLineInfo( filedata, "LIVE_TIME - " );
    if( !lineinfo.empty() )
      meas->live_time_ = static_cast<float>( atof( lineinfo.c_str() ) );
    
    lineinfo = getAmptekMcaLineInfo( filedata, "REAL_TIME - " );
    if( !lineinfo.empty() )
      meas->real_time_ = static_cast<float>( atof( lineinfo.c_str() ) );
    
    lineinfo = getAmptekMcaLineInfo( filedata, "START_TIME - " );
    if( !lineinfo.empty() )
      meas->start_time_ = time_from_string( lineinfo.c_str() );
    
    lineinfo = getAmptekMcaLineInfo( filedata, "SERIAL_NUMBER - " );
    if( !lineinfo.empty() )
      instrument_id_ = lineinfo;
    
    size_t datastart = filedata.find( "<<DATA>>" );
    if( datastart == string::npos )
      throw runtime_error( "File doesnt contain <<DATA>> section" );
    
    datastart += 8;
    while( datastart < filedata.size() && !isdigit(filedata[datastart]) )
      ++datastart;
    
    const size_t dataend = filedata.find( "<<END>>", datastart );
    if( dataend == string::npos )
      throw runtime_error( "File doesnt contain <<END>> for data section" );
    
    const size_t datalen = dataend - datastart - 1;
    
    std::shared_ptr<vector<float> > counts( new vector<float>() );
    meas->gamma_counts_ = counts;
    
    const bool success = SpecUtils::split_to_floats(
                                                    filedata.c_str() + datastart, datalen, *counts );
    if( !success || (counts->size() < 2) )
      throw runtime_error( "Couldnt parse channel data" );
    
    if( energy_gain > 0.0f && energy_gain < 100.0 )
    {
      try
      {
        auto newcal = make_shared<EnergyCalibration>();
        newcal->set_polynomial( counts->size(), {0.0f,energy_gain}, {} );
        meas->energy_calibration_ = newcal;
      }catch( std::exception & )
      {
        //probably wont ever make it here
      }
    }//if( parsed gain )
    
    meas->gamma_count_sum_ = 0.0;
    for( const float f : *counts )
      meas->gamma_count_sum_ += f;
    
    const size_t dp5start = filedata.find( "<<DP5 CONFIGURATION>>" );
    if( dp5start != string::npos )
    {
      const size_t dp5end = filedata.find( "<<DP5 CONFIGURATION END>>", dp5start );
      
      if( dp5end != string::npos )
      {
        vector<string> lines;
        const string data = filedata.substr( dp5start, dp5end - dp5start );
        SpecUtils::split( lines, data, "\r\n" );
        for( size_t i = 1; i < lines.size(); ++i )
          meas->remarks_.push_back( lines[i] );
      }//if( dp5end != string::npos )
    }//if( dp5start == string::npos )
    
    
    const size_t dppstart = filedata.find( "<<DPP STATUS>>" );
    if( dppstart != string::npos )
    {
      const size_t dppend = filedata.find( "<<DPP STATUS END>>", dppstart );
      
      if( dppend != string::npos )
      {
        vector<string> lines;
        const string data = filedata.substr( dppstart, dppend - dppstart );
        SpecUtils::split( lines, data, "\r\n" );
        for( size_t i = 1; i < lines.size(); ++i )
        {
          if( SpecUtils::starts_with( lines[i], "Serial Number: " )
             && instrument_id_.size() < 3 )
            instrument_id_ = lines[i].substr( 15 );
          else if( SpecUtils::starts_with( lines[i], "Device Type: " ) )
            instrument_model_ = lines[i].substr( 13 );
          else
            remarks_.push_back( lines[i] );
        }
      }//if( dppend != string::npos )
    }//if( dp5start == string::npos )
    
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }
  
  return false;
}//bool SpecFile::load_from_amptek_mca( std::istream &input )
  

}//namespace SpecUtils





