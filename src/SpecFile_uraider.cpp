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

#include <string>
#include <vector>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>
#include <iostream>
#include <stdexcept>

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_utils.hpp"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/RapidXmlUtils.hpp"
#include "SpecUtils/EnergyCalibration.h"
#include "SpecUtils/SpecFile_location.h"

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
  
bool SpecFile::load_micro_raider_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  try
  {
    reset();
    rapidxml::file<char> input_file( input );
    const bool success = load_from_micro_raider_from_data( input_file.data() );
    
    if( success )
      filename_ = filename;
    
    return success;
  }catch( std::exception & )
  {
    reset();
    return false;
  }//try/catch
  
  return false;
}//bool load_micro_raider_file(...)
  
  
bool SpecFile::load_from_micro_raider_from_data( const char *data )
{
  try
  {
    typedef char XmlChar;
    rapidxml::xml_document<XmlChar> doc;
    
    //Casting 'data' to non-const, BUT rapidxml::parse_non_destructive
    //  _should_ garuntee the source is no altered.  It's a little bit shaky
    //  but it allows us to potentially use mmap
    doc.parse<rapidxml::parse_non_destructive |rapidxml::allow_sloppy_parse>( (XmlChar *)data );
    const rapidxml::xml_node<XmlChar> *IdResult = XML_FIRST_NODE((&doc),"IdResult");
    
    if( !IdResult )
      throw runtime_error( "Invalid Micro Raider XML document" );
    
    const rapidxml::xml_node<XmlChar> *DeviceId = XML_FIRST_NODE(IdResult,"DeviceId");
    const rapidxml::xml_node<XmlChar> *SurveyId = XML_FIRST_NODE(IdResult,"SurveyId");
    const rapidxml::xml_node<XmlChar> *UUID = XML_FIRST_NODE(IdResult,"UUID");
    const rapidxml::xml_node<XmlChar> *EventNumber = XML_FIRST_NODE(IdResult,"EventNumber");
    const rapidxml::xml_node<XmlChar> *CrystalType = XML_FIRST_NODE(IdResult,"CrystalType");
    const rapidxml::xml_node<XmlChar> *UserMode = XML_FIRST_NODE(IdResult,"UserMode");
    const rapidxml::xml_node<XmlChar> *StartTime = XML_FIRST_NODE(IdResult,"StartTime");
    //    const rapidxml::xml_node<XmlChar> *StopTime = XML_FIRST_NODE(IdResult,"StopTime");
    const rapidxml::xml_node<XmlChar> *GPS = XML_FIRST_NODE(IdResult,"GPS");
    const rapidxml::xml_node<XmlChar> *RealTime = XML_FIRST_NODE(IdResult,"RealTime");
    const rapidxml::xml_node<XmlChar> *LiveTime = XML_FIRST_NODE(IdResult,"LiveTime");
    //    const rapidxml::xml_node<XmlChar> *DeadTime = XML_FIRST_NODE(IdResult,"DeadTime");
    const rapidxml::xml_node<XmlChar> *DoseRate = XML_FIRST_NODE(IdResult,"DoseRate");
    //    const rapidxml::xml_node<XmlChar> *CountRate = XML_FIRST_NODE(IdResult,"CountRate");
    const rapidxml::xml_node<XmlChar> *NeutronCountRate = XML_FIRST_NODE(IdResult,"NeutronCountRate");
    
    
    const rapidxml::xml_node<XmlChar> *Nuclide = XML_FIRST_NODE(IdResult,"Nuclide");
    const rapidxml::xml_node<XmlChar> *Image = XML_FIRST_NODE(IdResult,"Image");
    const rapidxml::xml_node<XmlChar> *VoiceRecording = XML_FIRST_NODE(IdResult,"VoiceRecording");
    const rapidxml::xml_node<XmlChar> *Spectrum = XML_FIRST_NODE(IdResult,"Spectrum");
    
    if( !Spectrum || !Spectrum->value_size() )
      throw runtime_error( "No Spectrum Node" );
    
    std::shared_ptr< vector<float> > channel_counts
    = std::make_shared<vector<float> >();
    
    const bool validchannel
    = SpecUtils::split_to_floats( Spectrum->value(),
                                 Spectrum->value_size(), *channel_counts );
    if( !validchannel || channel_counts->empty() )
      throw runtime_error( "Couldnt parse channel counts" );
    
    std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
    
    meas->gamma_counts_ = channel_counts;
    
    meas->gamma_count_sum_ = 0.0;
    const size_t nchannel = meas->gamma_counts_->size();
    for( size_t i = 0; i < nchannel; ++i )
      meas->gamma_count_sum_ += (*channel_counts)[i];
    
    instrument_id_ = xml_value_str( DeviceId );
    if( SurveyId )
      remarks_.push_back( "Survey ID: " + xml_value_str( SurveyId ) );
    uuid_ = xml_value_str( UUID );
    if( EventNumber )
      remarks_.push_back( "EventNumber: " + xml_value_str( EventNumber ) );
    if( CrystalType )
      remarks_.push_back( "CrystalType: " + xml_value_str( CrystalType ) );
    if( UserMode )
      remarks_.push_back( "CrystalType: " + xml_value_str( UserMode ) );
    
    //Unnecessary allocation to get time.
    const string start_time = xml_value_str(StartTime);
    meas->start_time_ = SpecUtils::time_from_string( start_time.c_str() );
    
    if( GPS && GPS->value_size() )
    {
      rapidxml::xml_attribute<XmlChar> *att = GPS->first_attribute("Valid",5);
      
      if( !att || XML_VALUE_ICOMPARE(att,"True") )
      {
        double latitude, longitude;
        if( parse_deg_min_sec_lat_lon(GPS->value(), GPS->value_size(), latitude, longitude) )
        {
          auto loc = make_shared<LocationState>();
          loc->type_ = LocationState::StateType::Instrument;
          meas->location_ = loc;
          auto geo = make_shared<GeographicPoint>();
          loc->geo_location_ = geo;
          geo->latitude_ = latitude;
          geo->longitude_ = longitude;
        }//if( parse gps coords )
      }//if( !att || XML_VALUE_ICOMPARE(att,"True") )
    }//if( GPS )
    
    if( RealTime && RealTime->value_size() )
      meas->real_time_ = time_duration_string_to_seconds( RealTime->value(), RealTime->value_size() );
    if( LiveTime && LiveTime->value_size() )
      meas->live_time_ = time_duration_string_to_seconds( LiveTime->value(), LiveTime->value_size() );
    
    
    std::shared_ptr<DetectorAnalysis> detana;
    
    if( DoseRate && DoseRate->value_size() )
    {
      try
      {
        const float doseunit = dose_units_usvPerH( DoseRate->value(), DoseRate->value_size() );
        
        float dose_rate;
        if( !parse_float(DoseRate->value(), DoseRate->value_size(), dose_rate) )
          throw runtime_error( "Dose value of '" + xml_value_str(DoseRate) + "' not a valid number.");
        
        meas->dose_rate_ = dose_rate * doseunit;
      }catch( std::exception &e )
      {
        parse_warnings_.push_back( "Error decoding dose: " + string(e.what()) );
      }//try / catch parse dose
    }//if( DoseRate && DoseRate->value_size() )
    
    if( NeutronCountRate && NeutronCountRate->value_size() )
    {
      const string neutroncountstr = xml_value_str(NeutronCountRate);
      meas->neutron_counts_.resize( 1, 0.0f );
      
      float neutrons = 0.0f;
      if( toFloat(neutroncountstr,neutrons) )
      {
        if( meas->real_time_ > 0.0f )
          neutrons *= meas->real_time_;
        else if( meas->live_time_ > 0.0f )
          neutrons *= meas->live_time_;
        else
          meas->remarks_.push_back( "NeutronCountRate: " + neutroncountstr + " (error computing gross count)" ); //meh, should be fine...
        
        meas->neutron_counts_sum_ = neutrons;
        meas->neutron_counts_[0] = neutrons;
        meas->contained_neutron_ = true;
      }else
      {
        meas->remarks_.push_back( "NeutronCountRate: " + neutroncountstr );
        cerr << "Failed to read '" << neutroncountstr << "' as neutroncountstr"
        << endl;
      }
    }//if( NeutronCountRate && NeutronCountRate->value_size() )
    
    while( Nuclide )
    {
      DetectorAnalysisResult res;
      
      const rapidxml::xml_node<XmlChar> *NuclideName = XML_FIRST_NODE(Nuclide,"NuclideName");
      const rapidxml::xml_node<XmlChar> *NuclideType = XML_FIRST_NODE(Nuclide,"NuclideType");
      const rapidxml::xml_node<XmlChar> *NuclideIDConfidenceIndication = XML_FIRST_NODE(Nuclide,"NuclideIDConfidenceIndication");
      const rapidxml::xml_node<XmlChar> *NuclideIDStrengthIndication = XML_FIRST_NODE(Nuclide,"NuclideIDStrengthIndication");
      const rapidxml::xml_node<XmlChar> *NuclideDescription = XML_FIRST_NODE(Nuclide,"NuclideDescription");
      //      const rapidxml::xml_node<XmlChar> *NuclideInstruction = XML_FIRST_NODE(Nuclide,"NuclideInstruction");
      const rapidxml::xml_node<XmlChar> *NuclideHPRDSType = XML_FIRST_NODE(Nuclide,"NuclideHPRDSType");
      
      res.nuclide_ = xml_value_str(NuclideName);
      res.nuclide_type_ = xml_value_str(NuclideType);
      res.id_confidence_ = xml_value_str(NuclideIDConfidenceIndication);
      
      const string strength = xml_value_str(NuclideIDStrengthIndication);
      if( strength.size() )
        res.remark_ += "strength: " + strength;
      if( NuclideHPRDSType && NuclideHPRDSType->value_size() )
        res.remark_ += (res.remark_.size() ? ". " : "") + xml_value_str(NuclideHPRDSType);
      if( NuclideDescription && NuclideDescription->value_size() )
        res.remark_ += (res.remark_.size() ? ". " : "") + xml_value_str(NuclideDescription);
      
      Nuclide = XML_NEXT_TWIN(Nuclide);
      
      if( !detana )
        detana = std::make_shared<DetectorAnalysis>();
      detana->results_.push_back( res );
    }//while( Nuclide )
    
    while( Image && Image->value_size() )
    {
      remarks_.push_back( "Image: " + xml_value_str(Image) );
      Image = XML_NEXT_TWIN(Image);
    }
    
    if( VoiceRecording && VoiceRecording->value_size() )
      remarks_.push_back( "VoiceRecording: " + xml_value_str(VoiceRecording) );
    
    detectors_analysis_ = detana;
    
    //Following values taken from a Micro Raider ICD1 N42 2006 file
    manufacturer_ = "ICx Radiation";
    instrument_model_ = "Raider";
    instrument_type_ = "Radionuclide Identifier";  //or PersonalRadiationDetector
    detector_type_ = DetectorType::MicroRaider;
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
    
    return true;
  }catch( std::exception & )
  {
    return false;
  }
  
  
  //  "<IdResult"
  
  return false;
}//bool load_from_micro_raider_from_data( const char *data )

}//namespace SpecUtils

