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
#include "SpecUtils/RapidXmlUtils.hpp"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;


namespace SpecUtils
{
  
bool SpecFile::load_lzs_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_lzs( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_lzs_file( const std::string &filename );
  

  
bool SpecFile::load_from_lzs( std::istream &input )
{
  //Note: this function implemented off using a few files to determine
  //      file format; there is likely some assumptions that could be loosened
  //      or tightened up, or additional information to add.
  
  reset();
  
  if( !input.good() )
    return false;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const istream::pos_type start_pos = input.tellg();
  input.unsetf(ios::skipws);
  
  // Determine stream size
  input.seekg( 0, ios::end );
  const size_t file_size = static_cast<size_t>( input.tellg() - start_pos );
  input.seekg( start_pos );
  
  //I've only seen pretty small lzs files (40k), so assume if over 1MB, not a lzs
  if( file_size < 512 || file_size > 1*1024*1024 )
    return false;
  
  string filedata;
  filedata.resize(file_size + 1);
  
  input.read(&(filedata[0]), static_cast<streamsize>(file_size));
  filedata[file_size] = 0; //jic.
  
  //Look to find "<nanoMCA" and "spectrum" in the first kb
  const auto nano_mca_pos = filedata.find( "spectrum" );
  if( nano_mca_pos == string::npos || nano_mca_pos > 2048 )
    return false;
  
  const auto spectrum_pos = filedata.find( "data" );
  if( spectrum_pos == string::npos || spectrum_pos > 2048 )
    return false;
  
  try
  {
    rapidxml::xml_document<char> doc;
    doc.parse<rapidxml::parse_non_destructive|rapidxml::allow_sloppy_parse>( &(filedata[0]) );
    
    //<nanoMCA>  //But some files dont have this tag, but start with <spectrum>
    //  <serialnumber>28001</serialnumber>
    //  <spectrum>
    //    <tag>nanoMCA with Ortec HPGE-TRP, Model GEM-10195-PLUS, SN 24-P-12RA, 3000V-PLUS</tag>
    //    <hardsize>16384</hardsize>
    //    <softsize>16384</hardsize>
    //    <data>...this is spectrum...</data>
    //  </spectrum>
    //  <time>
    //    <real>  613.232</real>
    //    <live>  601.0000</live>
    //    <dead>    4.1769</dead>
    //    <date>11/06/2019  20:19:15</date>
    //  </time>
    //  <registers><size>128</size><data>...not syre what this is...</data></registers>
    //  <calibration>
    //    <enabled>YES</enabled>
    //    <units>2</units>
    //    <channelA>    0.0000</channelA>
    //    <energyA>    0.0000</energyA>
    //    <channelB>10436.2100</channelB>
    //    <energyB> 1332.0000</energyB>
    //  </calibration>
    //  <volatile>
    //    <firmware>30.20</firmware>
    //    <intemp>44</intemp>
    //    <slowadvc> 0.82</slowadc>
    //  </volatile>
    //</nanoMCA>
    
    const rapidxml::xml_node<char> *nano_mca_node = XML_FIRST_NODE((&doc),"nanoMCA");
    if( !nano_mca_node )  //Account for files that start with <spectrum> tag instead of nesting it under <nanoMCA>
      nano_mca_node = &doc;
    
    const rapidxml::xml_node<char> *spectrum_node = XML_FIRST_NODE(nano_mca_node,"spectrum");
    if( !spectrum_node )
      throw runtime_error( "Failed to get spectrum node" );
    
    const rapidxml::xml_node<char> *spec_data_node = XML_FIRST_NODE(spectrum_node,"data");
    if( !spec_data_node )
      throw runtime_error( "Failed to get spectrum/data node" );
    
    auto spec = std::make_shared<vector<float>>();
    const string spec_data_str = xml_value_str(spec_data_node);
    SpecUtils::split_to_floats( spec_data_str.c_str(), *spec, " \t\n\r", false );
    
    if( spec->empty() )
      throw runtime_error( "Failed to parse spectrum to floats" );
    
    auto meas = std::make_shared<Measurement>();
    meas->contained_neutron_ = false;
    meas->gamma_counts_ = spec;
    meas->gamma_count_sum_ = std::accumulate(begin(*spec), end(*spec), 0.0 );
    
    const rapidxml::xml_node<char> *time_node = xml_first_node(nano_mca_node,"time");
    
    const rapidxml::xml_node<char> *real_time_node = xml_first_node(time_node,"real");
    if( real_time_node )
      SpecUtils::parse_float(real_time_node->value(), real_time_node->value_size(), meas->real_time_ );
    
    const rapidxml::xml_node<char> *live_time_node = xml_first_node(time_node,"live");
    if( live_time_node )
      SpecUtils::parse_float(live_time_node->value(), live_time_node->value_size(), meas->live_time_ );
    
    //const rapidxml::xml_node<char> *dead_time_node = xml_first_node(time_node,"dead");
    
    const rapidxml::xml_node<char> *date_node = xml_first_node(time_node,"date");
    if( date_node )
    {
      string datestr = xml_value_str(date_node);
      SpecUtils::ireplace_all(datestr, "@", " ");
      SpecUtils::ireplace_all(datestr, "  ", " ");
      meas->start_time_ = SpecUtils::time_from_string( datestr, SpecUtils::DateParseEndianType::LittleEndianFirst );
    }
    
    const rapidxml::xml_node<char> *calibration_node = XML_FIRST_NODE(nano_mca_node,"calibration");
    //const rapidxml::xml_node<char> *enabled_node = xml_first_node(calibration_node, "enabled");
    //const rapidxml::xml_node<char> *units_node = xml_first_node(calibration_node, "units");
    const rapidxml::xml_node<char> *channelA_node = xml_first_node(calibration_node, "channelA");
    const rapidxml::xml_node<char> *energyA_node = xml_first_node(calibration_node, "energyA");
    const rapidxml::xml_node<char> *channelB_node = xml_first_node(calibration_node, "channelB");
    const rapidxml::xml_node<char> *energyB_node = xml_first_node(calibration_node, "energyB");
    if( channelA_node && energyA_node && channelB_node && energyB_node )
    {
      float channelA, energyA, channelB, energyB;
      if( SpecUtils::parse_float(channelA_node->value(), channelA_node->value_size(), channelA )
         && SpecUtils::parse_float(energyA_node->value(), energyA_node->value_size(), energyA )
         && SpecUtils::parse_float(channelB_node->value(), channelB_node->value_size(), channelB )
         && SpecUtils::parse_float(energyB_node->value(), energyB_node->value_size(), energyB ) )
      {
        const float gain = (energyB - energyA) / (channelB - channelA);
        const float offset = energyA - channelA*gain;
        if( !IsNan(gain) && !IsInf(gain) && !IsNan(offset) && !IsInf(offset)
           && gain > 0.0f && fabs(offset) < 350.0f )
        {
          try
          {
            const size_t nchannel = spec ? spec->size() : size_t(0);
            auto newcal = make_shared<EnergyCalibration>();
            newcal->set_polynomial(nchannel, {offset, gain}, {} );
            meas->energy_calibration_ = newcal;
          }catch( std::exception &e )
          {
            meas->parse_warnings_.push_back( "Invalid energy calibration: " + string(e.what()) );
          }
        }//if( maybe calibration is valid )
      }//if( parsed all float values okay )
    }//if( got at least calibration points )
    
    const rapidxml::xml_node<char> *volatile_node = XML_FIRST_NODE(nano_mca_node,"volatile");
    
    const rapidxml::xml_node<char> *firmware_node = xml_first_node(volatile_node,"firmware");
    if( firmware_node )
      component_versions_.push_back( pair<string,string>("firmware",xml_value_str(firmware_node)) );
    
    const rapidxml::xml_node<char> *intemp_node = xml_first_node(volatile_node,"intemp");
    if( intemp_node && intemp_node->value_size() )
      meas->remarks_.push_back( "Internal Temperature: " + xml_value_str(intemp_node) );
    const rapidxml::xml_node<char> *adctemp_node = xml_first_node(volatile_node,"adctemp");
    if( adctemp_node && adctemp_node->value_size() )
      meas->remarks_.push_back( "ADC Temperature: " + xml_value_str(adctemp_node) );
    
    //const rapidxml::xml_node<char> *slowadvc_node = xml_first_node(volatile_node,"slowadvc");
    
    const rapidxml::xml_node<char> *serialnum_node = XML_FIRST_NODE(nano_mca_node,"serialnumber");
    if( serialnum_node && serialnum_node->value_size() )
      instrument_id_ = xml_value_str(serialnum_node);
    
    const rapidxml::xml_node<char> *tag_node = XML_FIRST_NODE(spectrum_node,"tag");
    if( !tag_node )
      tag_node = XML_FIRST_NODE(nano_mca_node,"tag");
    
    if( tag_node && tag_node->value_size() )
    {
      const string value = xml_value_str(tag_node);
      
      remarks_.push_back( value );
      
      //nanoMCA with Ortec HPGE-TRP, Model GEM-10195-PLUS, SN 24-P-12RA, 3000V-PLUS
      //I'm not sure how reliable it is to assume comma-seperated
      vector<string> fields;
      SpecUtils::split(fields, value, ",");
      for( auto field : fields )
      {
        SpecUtils::trim(field);
        if( SpecUtils::istarts_with(field, "SN") )
          instrument_id_ = SpecUtils::trim_copy(field.substr(2));
        else if( SpecUtils::istarts_with(field, "model") )
          instrument_model_ = SpecUtils::trim_copy(field.substr(5));
      }//for( auto field : fields )
    }//if( tag_node )
    
    manufacturer_ = "labZY";
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( start_pos, ios::beg );
    return false;
  }//try / catch
  
  return true;
}//bool load_from_lzs( std::istream &input )
  
  
}//namespace SpecUtils





