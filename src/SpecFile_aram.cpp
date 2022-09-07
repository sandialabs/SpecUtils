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
#include <iostream>
#include <stdexcept>

#include "rapidxml/rapidxml.hpp"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/Filesystem.h"
#include "SpecUtils/RapidXmlUtils.hpp"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

namespace
{
  bool xml_value_to_flt( const rapidxml::xml_base<char> *node, float &val )
  {
    val = 0.0f;
    if( !node )
      return false;
    return SpecUtils::parse_float( node->value(), node->value_size(), val );
  }
}//namespace

namespace SpecUtils
{
bool SpecFile::load_aram_file( const std::string &filename )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  reset();
  
  //Only seend pretty small ones, therefor limit to 25 MB, JIC
  if( SpecUtils::file_size(filename) > 25*1024*1024 )
    return false;
  
#ifdef _WIN32
  ifstream file( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream file( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  if( !file.is_open() )
    return false;
  const bool loaded = load_from_aram( file );
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_aram_file( const std::string &filename )
  
  
bool SpecFile::load_from_aram( std::istream &input )
{
  //This is a wierd TXT and XML format hybrid, so we have to seprate out the
  //  XML from non-XML portions and parse them seperately.
  
  if( !input )
    return false;
  
  const istream::iostate origexceptions = input.exceptions();
  input.exceptions( istream::goodbit );  //make so stream never throws
  const istream::pos_type start_pos = input.tellg();
  input.unsetf(ios::skipws);
  
  // Determine stream size
  input.seekg( 0, ios::end );
  const size_t file_size = static_cast<size_t>( input.tellg() - start_pos );
  input.seekg( start_pos );
  
  //I've only seen pretty small ARAM files, so assume if over 25MB, not a ARAM
  if( file_size > 25*1024*1024 )
    return false;
  
  string filedata;
  filedata.resize(file_size + 1);
  
  input.read(&(filedata[0]), static_cast<streamsize>(file_size));
  filedata[file_size] = 0; //jic.
  
  //Look to find "<event" and "ARAM" in the first kb
  const auto event_tag_pos = filedata.find( "<event" );
  if( event_tag_pos == string::npos || event_tag_pos > 2048 )
    return false;
  
  const auto aram_pos = filedata.find( "ARAM" );
  if( aram_pos == string::npos || aram_pos > 2048 )
    return false;
  
  auto event_tag_clos_pos = filedata.find( "</event", event_tag_pos + 5 );
  if( event_tag_clos_pos == string::npos )
    return false;
  
  //It looks like there can be multiple <event> tags in the file, but in the
  //  file I have with a second <event> tag, it is empty, so wont worry about now
  
  try
  {
    rapidxml::xml_document<char> doc;
    const char orig_close_char = filedata[event_tag_clos_pos];
    filedata[event_tag_clos_pos] = '\0';
    doc.parse<rapidxml::parse_non_destructive|rapidxml::allow_sloppy_parse>( &(filedata[event_tag_pos]) );
    
    const rapidxml::xml_node<char> *event_node = XML_FIRST_NODE((&doc),"event");
    if( !event_node )
      throw runtime_error( "Failed to get event node, even though it really should be there" );
    
    const rapidxml::xml_node<char> *detectors_node = XML_FIRST_NODE(event_node,"detectors");
    if( !detectors_node )
      throw runtime_error( "No detectors node" );
    
    const rapidxml::xml_node<char> *gamma_node = XML_FIRST_NODE(detectors_node,"gamma");
    if( !gamma_node )
      throw runtime_error( "No detectors node" );
    
    const rapidxml::xml_node<char> *sample_node = XML_FIRST_NODE(gamma_node,"sample");
    if( !sample_node )
      throw runtime_error( "No sample node" );
    const rapidxml::xml_node<char> *channels_node = XML_FIRST_NODE(sample_node,"channels");
    if( !channels_node || !channels_node->value_size() )
      throw runtime_error( "No sample channels node" );
    
    const std::string start_iso_str = xml_value_str( XML_FIRST_ATTRIB(event_node,"start_iso8601") );
    const auto start_time = SpecUtils::time_from_string( start_iso_str.c_str() );
    
    //const std::string end_iso_str = xml_value_str( XML_FIRST_ATTRIB(event_node,"end_iso8601") );
    //const auto end_time = SpecUtils::time_from_string( end_iso_str.c_str() );
    
    //Other attributes we could put into the comments or somethign
    //XML_FIRST_ATTRIB(event_node,"monitor_type") //"ARAM"
    //XML_FIRST_ATTRIB(event_node,"version")
    //XML_FIRST_ATTRIB(event_node,"start_timestamp") //"1464191873756"
    //XML_FIRST_ATTRIB(event_node,"monitor_name")  //"IST"
    //XML_FIRST_ATTRIB(event_node,"event_id") //"1464191866846"
    
    
    std::shared_ptr<Measurement> fore_meas, back_meas;
    fore_meas = make_shared<Measurement>();
    auto fore_channels = std::make_shared<vector<float>>();
    fore_channels->reserve( 1024 );
    SpecUtils::split_to_floats( channels_node->value(), channels_node->value_size(), *fore_channels );
    if( fore_channels->size() < 64 ) //64 is arbitrary
      throw runtime_error( "Not enough channels" );
    
    float live_time = 0.0f, real_time = 0.0f;
    xml_value_to_flt( XML_FIRST_ATTRIB(channels_node, "realtime"), real_time );
    xml_value_to_flt( XML_FIRST_ATTRIB(channels_node, "livetime"), live_time );
    fore_meas->set_gamma_counts( fore_channels, live_time/1000.0f, real_time/1000.0f );
    fore_meas->source_type_ = SourceType::Foreground;
    fore_meas->occupied_ = OccupancyStatus::Occupied;
    if( !is_special(start_time) )
      fore_meas->set_start_time( start_time );
    
    //See if neutrons are around
    const rapidxml::xml_node<char> *neutron_node = XML_FIRST_NODE(detectors_node,"neutron");
    const rapidxml::xml_node<char> *neutron_sample = xml_first_node_nso(neutron_node, "sample", "");
    const rapidxml::xml_node<char> *neutron_counts = xml_first_node_nso(neutron_sample, "counts", "");
    if( neutron_counts )
    {
      float total_neutrons = 0.0;
      if( xml_value_to_flt( XML_FIRST_ATTRIB(neutron_counts, "total"), total_neutrons) )
      {
        fore_meas->neutron_counts_.push_back( total_neutrons );
        fore_meas->neutron_counts_sum_ = total_neutrons;
        fore_meas->contained_neutron_ = true;
        
        if( xml_value_to_flt( XML_FIRST_ATTRIB(neutron_counts, "realtime"), real_time ) )
          fore_meas->remarks_.push_back( "Neutron real time: " + std::to_string(real_time/1000.0) + "s" );
        if( xml_value_to_flt( XML_FIRST_ATTRIB(neutron_counts, "livetime"), live_time ) )
          fore_meas->remarks_.push_back( "Neutron live time: " + std::to_string(live_time/1000.0) + "s" );
      }
    }//if( neutron_counts )
    
    const rapidxml::xml_node<char> *background_node = XML_FIRST_NODE(gamma_node,"background");
    channels_node = xml_first_node_nso( background_node, "channels", "" );
    if( channels_node && channels_node->value_size() )
    {
      back_meas = make_shared<Measurement>();
      auto back_channels = std::make_shared<vector<float>>();
      SpecUtils::split_to_floats( channels_node->value(), channels_node->value_size(), *back_channels );
      if( back_channels->size() >= 64 ) //64 is arbitrary
      {
        xml_value_to_flt( XML_FIRST_ATTRIB(channels_node, "realtime"), real_time );
        xml_value_to_flt( XML_FIRST_ATTRIB(channels_node, "livetime"), live_time );
        back_meas->set_gamma_counts( back_channels, live_time/1000.0f, real_time/1000.0f );
        back_meas->set_title( "Background" );
        back_meas->source_type_ = SourceType::Background;
        back_meas->occupied_ = OccupancyStatus::NotOccupied;
        if( !is_special(start_time) )
          back_meas->set_start_time( start_time );
        measurements_.push_back( back_meas );
      }
    }//if( background data )
    
    measurements_.push_back( fore_meas );
    
    // \TODO: Add a time history to SpecFile for when the spectra arent available.
    //This file contains a time history of the gross count data (but only a single summed spectrum)
    const rapidxml::xml_node<char> *gamma_counts_node = XML_FIRST_NODE(gamma_node,"counts");
    if( gamma_counts_node )
      remarks_.push_back( "The ARAM file format has a time history in it that is not decoded" );
    
    filedata[event_tag_clos_pos] = orig_close_char;
    
    //Try and get energy calibration.
    size_t calib_pos = string::npos, coef_start = string::npos, coef_end = string::npos;
    calib_pos = filedata.rfind("<Calibration");
    if( calib_pos != string::npos )
      coef_start = filedata.find( "<Coefficients>", calib_pos );
    if( coef_start != string::npos )
      coef_end = filedata.find( "</Coefficients>", coef_start );
    if( coef_end != string::npos && ((coef_start+14) < coef_end) )
    {
      vector<float> coefs;
      const size_t coef_strlen = coef_end - coef_start - 14;
      SpecUtils::split_to_floats( &filedata[coef_start+14], coef_strlen, coefs );
      
      if( coefs.size() > 1 && coefs.size() < 10 )
      {
        try
        {
          auto newcal = make_shared<EnergyCalibration>();
          newcal->set_polynomial( fore_meas->num_gamma_channels(), coefs, {} );
          fore_meas->energy_calibration_ = newcal;
          
          if( back_meas
              && (back_meas->num_gamma_channels() != fore_meas->num_gamma_channels())
              && (back_meas->num_gamma_channels() > 0) )
          {
            newcal = make_shared<EnergyCalibration>();
            newcal->set_polynomial( back_meas->num_gamma_channels(), coefs, {} );
          }
          
          if( back_meas && back_meas->num_gamma_channels() )
            back_meas->energy_calibration_ = newcal;
        }catch( std::exception & )
        {
          
        }
      }//if( we got the coefficients )
    }//if( we found the coefficients )
    
    vector<string> begin_remarks;
    const string begindata( &(filedata[0]), &(filedata[event_tag_pos]) );
    SpecUtils::split(begin_remarks, begindata, "\r\n");
    string lat_str, lon_str;
    for( const auto &remark : begin_remarks )
    {
      if( SpecUtils::istarts_with(remark, "Site Name:") )
        measurement_location_name_ = SpecUtils::trim_copy( remark.substr(10) );
      else if( SpecUtils::istarts_with(remark, "Site Longitude:") )
        lon_str = remark.substr( 15 ); //ex. "39deg 18min 15.2sec N"
      else if( SpecUtils::istarts_with(remark, "Site Latitude:") )
        lat_str = remark.substr( 14 ); //ex. "124deg 13min 51.8sec W"
      else
        remarks_.push_back( remark );
    }//for( const auto &remark : begin_remarks )
    
    if( lon_str.size() && lat_str.size() )
    {
      double lat, lon;
      const string coord = lon_str + " / " + lat_str;
      if( parse_deg_min_sec_lat_lon( coord.c_str(), coord.size(), lat, lon ) )
      {
        fore_meas->longitude_ = lon;
        fore_meas->latitude_ = lat;
        if( back_meas )
        {
          back_meas->longitude_ = lon;
          back_meas->latitude_ = lat;
        }
      }//if( can parse string )
    }//if( lat / lon str )
    
    //manufacturer_;
    instrument_model_ = "ARAM";
    
    //There is a <trigger...> node under <event> that describes why the event
    //  alarmed, should read that into comments or analysis results, or something
    
    parse_warnings_.emplace_back( "The ARAM file format has a time history in it that is not decoded" );
    
    cleanup_after_load();
    
    return true;
  }catch( std::exception & )
  {
    reset();
    input.seekg( start_pos );
    input.clear();
  }//try / catch
  
  input.exceptions( origexceptions );
  
  
  return false;
}//bool load_from_aram( std::istream &input )
}//namespace SpecUtils





