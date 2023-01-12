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
#include <numeric>
#include <iostream>
#include <stdexcept>

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_utils.hpp"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/SpecUtilsAsync.h"
#include "SpecUtils/RapidXmlUtils.hpp"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

namespace
{
bool is_candidate_scan_data( std::istream &input )
{
  if( !input )
    return false;
  
  char buffer[257] = { '\0' };
  const istream::pos_type start_pos = input.tellg();
  const bool successful_read = !!input.read( buffer, 256 );
  
  input.clear();
  input.seekg( start_pos, ios::beg );
  
  if( !successful_read )
    return false;
  
  //null terminate the buffer (should already be, but jic)
  buffer[256] = '\0';
  
  //Check how many non-null bytes there are
  size_t nlength = 0;
  for( size_t i = 0; i < 256; ++i )
    nlength += (buffer[i] ? 1 : 0);
  
  //Allow for max of 8 zero bytes...
  if( nlength+8 < 256 )
    return false;
  
  if( strstr(buffer, "<scanData>") )
    return true;
  
  // Not a candidate ScanData file
  return false;
}//bool is_candidate_scan_data( const char * data, const char * const data_end )

// Maps from RSP number, to N42 panel number - this is just a guess at the moment.
string rsp_name( const string &name )
{
  const char * const rspm_names[8] = { "Aa1", "Aa2", "Ba1", "Ba2", "Ca1", "Ca2", "Da1", "Da2" };
  
  int rsp_num = 0;
  
  if( SpecUtils::parse_int(name.c_str(), name.size(),rsp_num) && ((rsp_num >= 1) && (rsp_num <= 8)))
    return rspm_names[rsp_num - 1];
  
  return name;
}//string rsp_name(...)


bool is_candidate_gxml(std::istream& input)
{
  if (!input)
    return false;

  char buffer[257] = { '\0' };
  const istream::pos_type start_pos = input.tellg();
  const bool successful_read = !!input.read(buffer, 256);

  input.clear();
  input.seekg(start_pos, ios::beg);

  if (!successful_read)
    return false;

  if (strstr(buffer, "<BGAMMA>") || strstr(buffer, "<bgamma>"))
    return true;

  return false;
}//bool is_candidate_gxml(...)

}//namespace


namespace SpecUtils
{

bool SpecFile::load_xml_scan_data_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_xml_scan_data( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool SpecFile::load_xml_scan_data_file( const std::string &filename )


bool SpecFile::load_from_xml_scan_data( std::istream &input )
{
  reset();
  
  if( !input.good() )
    return false;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const istream::pos_type start_pos = input.tellg();
  
  try
  {
    input.unsetf(ios::skipws);
    
    if( !is_candidate_scan_data(input) )
      throw runtime_error( "Not ScanData XML file candidate." );
    
    rapidxml::file<char> input_file( input );
    
    std::unique_ptr<rapidxml::xml_document<char>> doc( new rapidxml::xml_document<char>() );
    
    char *data = input_file.data();
    char *data_end = data + input_file.size();
    
    doc->parse<rapidxml::parse_trim_whitespace | rapidxml::allow_sloppy_parse>( data, data_end );
    
    const rapidxml::xml_node<char> *scanData = XML_FIRST_NODE(doc.get(), "scanData");
    if( !scanData )
      throw runtime_error( "No scanData element" );
    
    // ScanData doesnt contain energy calibration; we'll use a default calibration when there
    //  are 9 channels; otherwise we'll use a 0 to 3 MeV polynomial calibration.
    //  We'll also share the same energy calibrations for all samples that have same number of
    //  channels, to minimize memory usage.
    //  (note: cleanup_after_load(...) would largely take care of the non 9 channel cases for us,
    //         but we'll do it here to be a little more efficient, and planning for files with
    //         more than 9 channels)
    map<size_t,shared_ptr<EnergyCalibration>> energy_cals;
    auto get_energy_cal = [&energy_cals]( size_t nchannel ) -> shared_ptr<EnergyCalibration> {
      
      const auto pos = energy_cals.find( nchannel );
      
      if( pos == end(energy_cals) )
      {
        auto cal = make_shared<EnergyCalibration>();
        
        if( nchannel == 9 )
        {
          vector<float> edges{ 0.0f, 109.0f, 167.6f, 284.8f, 519.1f, 987.9f,
            1163.7f, 1456.6f, 2862.9f, 3027.0f
          };// edges
          
          assert( edges.size() == 10 );
          
          cal->set_lower_channel_energy( 9, std::move(edges) );
        }else if( nchannel >= EnergyCalibration::sm_min_channels )
        {
          assert( nchannel < EnergyCalibration::sm_max_channels ); // set_default_polynomial will throw if this isnt the case anyway
          cal->set_default_polynomial( nchannel, {0.0f, 3000.0f/nchannel}, {} );
        }else
        {
          // Leave the default energy calibration
        }//if( nchannel == 9 ) / else
        
        energy_cals[nchannel] = cal;
        
        return cal;
      }//if( pos == end(energy_cals) )
      
      return pos->second;
    };//get_energy_cal
    
    
    XML_FOREACH_CHILD( SegmentResults, scanData, "SegmentResults" )
    {
      const rapidxml::xml_node<char> *RspId = XML_FIRST_NODE(SegmentResults,"RspId");
      const string RspId_str = SpecUtils::xml_value_str( RspId );
      if( RspId_str.empty() )
        throw runtime_error( "Empty or missing RspId under SegmentResults." );
      
      if( RspId_str == "17" ) //This is some sum or something; we'll skip it
        continue;
      
      string time_str = xml_value_str( XML_FIRST_NODE(SegmentResults,"GammaLastBackgroundTime") );
      
      vector<float> gamma_counts, neutron_counts;
      XML_FOREACH_CHILD( GammaBackground, SegmentResults, "GammaBackground" )
      {
        float val;
        if( !parse_float( GammaBackground->value(), GammaBackground->value_size(), val ) )
          throw runtime_error( "Failed to parse GammaBackground float" );
        
        gamma_counts.push_back( val );
      }//for( loop over GammaBackground )
      
      // Get <NeutronBackground1>, <NeutronBackground2>,...4; we'll account that maybe
      // NeutronBackground1 is missing, but NeutronBackground2 is there
      for( size_t i = 1; i <= 4; ++i )
      {
        const string name = "NeutronBackground" + std::to_string( i );
        const auto el = SegmentResults->first_node( name.c_str(), name.size() );
        if( el )
        {
          if( neutron_counts.size() < i )
            neutron_counts.resize( i, 0.0f );
          
          if( !parse_float( el->value(), el->value_size(), neutron_counts[i-1] ) )
            throw runtime_error( "Failed to parse NeutronBackground float" );
        }//if( el )
      }//for( loop over potential neutrons )
      
      // Other elements we arent parsing: <GammaAverage>, <GammaMin1>, <GammaMin1Pz>, <GammaMin2>,
      //  <GammaMin2Pz>, <GammaMax>, <GammaMaxPz>, <AlarmFlags>, <GammaErrorCount>,
      //  <GammaSampleAlarming>
      
      if( gamma_counts.empty() && neutron_counts.empty() )
        continue;
      
      // The background gamma counts have 10 channels, but the regular measurements have 9 channels;
      //  if we remove the first channel, then channel counts seem to match up about right
      if( gamma_counts.size() == 10 )
        gamma_counts.erase( begin(gamma_counts) );
      
      // We will make a separate Measurement for gammas and neutrons, although I think
      //  cleanup_after_load will maybe merge them back together - but we'll let that function
      //  do things consistently.
      if( gamma_counts.size() )
      {
        auto meas = make_shared<Measurement>();
        meas->detector_name_ = rsp_name(RspId_str);
        meas->energy_calibration_ = get_energy_cal( gamma_counts.size() );
        meas->contained_neutron_ = false;
        meas->gamma_counts_ = make_shared<vector<float>>( gamma_counts );
        meas->start_time_ = time_from_string( time_str.c_str() );
        meas->source_type_ = SourceType::Background;
        meas->occupied_ = OccupancyStatus::NotOccupied;
        meas->sample_number_ = 0;
        
        // The XML doesnt contain live/real time, but 1 seconds is my best guess based on one file.
        meas->live_time_ = 1.0f;
        meas->real_time_ = 1.0f;
        
        measurements_.push_back( meas );
      }//if( gamma_counts.size() )
      
      
      if( neutron_counts.size() )
      {
        auto meas = make_shared<Measurement>();
        meas->detector_name_ = rsp_name(RspId_str) + "N";
        meas->contained_neutron_ = true;
        meas->neutron_counts_ = neutron_counts;
        meas->start_time_ = time_from_string( time_str.c_str() );
        meas->source_type_ = SourceType::Background;
        meas->occupied_ = OccupancyStatus::NotOccupied;
        meas->sample_number_ = 0;
        
        // The XML doesnt contain live/real time, but 1 seconds is my best guess based on one file.
        meas->live_time_ = 1.0f;
        meas->real_time_ = 1.0f;
        
        measurements_.push_back( meas );
      }//if( neutron_counts.size() )
    }//for( loop over <SegmentResults> elements )
    
    
    // The backgrounds have an explicit RspID number, but the <PanelDataList> do not; so we will
    //  assume the are given in order
    int panel_num = 0;
    XML_FOREACH_CHILD( PanelDataList, scanData, "PanelDataList" )
    {
      panel_num += 1;
      
      XML_FOREACH_CHILD( item, PanelDataList, "item" )
      {
        const rapidxml::xml_node<char> *SampleDateTime = XML_FIRST_NODE(item,"SampleDateTime");
        const rapidxml::xml_node<char> *SampleId = XML_FIRST_NODE(item,"SampleId");

        bool did_contained_neutrons = false;
        vector<float> gamma_counts, neutron_counts;
        XML_FOREACH_CHILD( GammaData, item, "GammaData" )
        {
          float val;
          if( !parse_float( GammaData->value(), GammaData->value_size(), val ) )
            throw runtime_error( "Failed to parse GammaData float" );
          
          gamma_counts.push_back( val );
        }//for( loop over <GammaData> )
        
        XML_FOREACH_CHILD( NeutronData, item, "NeutronData" )
        {
          float val;
          if( !parse_float( NeutronData->value(), NeutronData->value_size(), val ) )
            throw runtime_error( "Failed to parse NeutronData float" );
          
          neutron_counts.push_back( val );
        }//for( loop over <NeutronData> )
        
        // Other elements we arent parsing: <CommStatus>, <Gamma0Rejected>, <OPFlags>,
        //   and multiple <VpsData> elements
        const string RspId_str = std::to_string( panel_num );
        const string time_str = xml_value_str( SampleDateTime );
        int sample_num = -1;
        if( SampleId )
          parse_int( SampleId->value(), SampleId->value_size(), sample_num );
        
        // See notes above on creating a separate Measurement for gamma and neutron.
        if( gamma_counts.size() )
        {
          auto meas = make_shared<Measurement>();
          meas->detector_name_ = rsp_name( RspId_str );
          meas->energy_calibration_ = get_energy_cal( gamma_counts.size() );
          meas->contained_neutron_ = false;
          meas->gamma_counts_ = make_shared<vector<float>>( gamma_counts );
          meas->start_time_ = time_from_string( time_str.c_str() );
          meas->source_type_ = SourceType::Foreground;
          meas->occupied_ = OccupancyStatus::Occupied;
          meas->sample_number_ = sample_num;
          
          // The XML doesnt contain live/real time, but we know its 0.1s real-time
          meas->live_time_ = 0.1f;
          meas->real_time_ = 0.1f;
          
          measurements_.push_back( meas );
        }//if( gamma_counts.size() )
        
        
        if( neutron_counts.size() )
        {
          auto meas = make_shared<Measurement>();
          meas->detector_name_ = rsp_name( RspId_str ) + "N";
          meas->contained_neutron_ = true;
          meas->neutron_counts_ = neutron_counts;
          meas->start_time_ = time_from_string( time_str.c_str() );
          meas->source_type_ = SourceType::Foreground;
          meas->occupied_ = OccupancyStatus::Occupied;
          meas->sample_number_ = sample_num;
          
          // The XML doesnt contain live/real time, but we know its 0.1s real-time
          meas->live_time_ = 0.1f;
          meas->real_time_ = 0.1f;
          
          measurements_.push_back( meas );
        }//if( gamma_counts.size() )
      }//for( loop over <item>
    }//for( loop over <PanelDataList> )
    
    if( measurements_.empty() )
      throw runtime_error( "No measurements" );
    
    // We need to sum all the gamma and neutron counts and stuff -
    SpecUtilsAsync::ThreadPool pool;
    for( size_t i = 0; i < measurements_.size(); ++i )
    {
      auto m = measurements_[i];
      assert( m );
      if( !m )
        continue;
      
      pool.post( [m](){
        if( m->gamma_counts_ )
          m->gamma_count_sum_ = std::accumulate( begin(*m->gamma_counts_),
                                                 end(*m->gamma_counts_), double(0.0) );
        m->neutron_counts_sum_ = std::accumulate( begin(m->neutron_counts_),
                                                  end(m->neutron_counts_), double(0.0) );
      } );
    }//for( size_t i = 0; i < measurements_.size(); ++i )
    pool.join();
    
    
    instrument_type_ = "Portal Monitor";
    
    if( energy_cals.count(9) )
    {
      detector_type_ = DetectorType::SAIC8;
      manufacturer_ = "SAIC";
      instrument_model_ = "RPM8";
    }//if( energy_cals.count(9) )
    
    
    const rapidxml::xml_node<char> *RpmID = XML_FIRST_NODE(scanData,"RpmID");
    const rapidxml::xml_node<char> *ScanId = XML_FIRST_NODE(scanData,"ScanId");
    const rapidxml::xml_node<char> *LaneDescription = XML_FIRST_NODE(scanData,"LaneDescription");
    // The example file I have only has a single <SegmentDescription> element, although there could
    //  be more?
    const rapidxml::xml_node<char> *SegmentDescription = XML_FIRST_NODE(scanData,"SegmentDescription");
    
    if( RpmID && RpmID->value_size() )
      instrument_id_ = xml_value_str( RpmID );
      
    if( ScanId && ScanId->value_size() )
      uuid_ = xml_value_str( ScanId );
    
    if( LaneDescription )
    {
      const rapidxml::xml_node<char> *type = XML_FIRST_NODE(LaneDescription,"type");
      if( type )
        remarks_.push_back( "Lane Type: " + xml_value_str(type) );
      
      const rapidxml::xml_node<char> *vector = XML_FIRST_NODE(LaneDescription,"vector");
      if( vector )
        remarks_.push_back( "Lane Vector: " + xml_value_str(vector) );
      
      const rapidxml::xml_node<char> *conveyance = XML_FIRST_NODE(LaneDescription,"conveyance");
      if( conveyance )
        remarks_.push_back( "Lane Conveyance: " + xml_value_str(conveyance) );
      
      const rapidxml::xml_node<char> *width = XML_FIRST_NODE(LaneDescription,"width");
      if( width )
        remarks_.push_back( "Lane Width: " + xml_value_str(width) );
    }//if( LaneDescription )
    
    if( SegmentDescription )
    {
      const rapidxml::xml_node<char> *DataSourceId = XML_FIRST_NODE(SegmentDescription,"DataSourceId");
      if( DataSourceId )
        remarks_.push_back( "DataSourceId: " + xml_value_str(DataSourceId) );
      
      //const rapidxml::xml_node<char> *RpmId = XML_FIRST_NODE(SegmentDescription,"RpmId");
      
      const rapidxml::xml_node<char> *VehicleId = XML_FIRST_NODE(SegmentDescription,"VehicleId");
      if( VehicleId )
        remarks_.push_back( "VehicleId: " + xml_value_str(VehicleId) );
      
      //const rapidxml::xml_node<char> *RpmDateTime = XML_FIRST_NODE(SegmentDescription,"RpmDateTime");
      //const rapidxml::xml_node<char> *SegmentOffset = XML_FIRST_NODE(SegmentDescription,"SegmentOffset");
      //const rapidxml::xml_node<char> *SegmentLength = XML_FIRST_NODE(SegmentDescription,"SegmentLength");
      //const rapidxml::xml_node<char> *DirectionTraveled = XML_FIRST_NODE(SegmentDescription,"DirectionTraveled");
      const rapidxml::xml_node<char> *AlarmVehicle = XML_FIRST_NODE(SegmentDescription,"AlarmVehicle");
      if( AlarmVehicle )
        remarks_.push_back( "VehicleId: " + xml_value_str(AlarmVehicle) );
    }//if( SegmentDescription )
    
    
    cleanup_after_load();
    
    return true;
  }catch( std::exception & )
  {
    input.clear();
    input.seekg( start_pos, ios::beg );
    reset();
    return false;
  }//try/catch
  
  
  return true;
}//bool SpecFile::load_from_xml_scan_data(...)



bool SpecFile::load_caen_gxml_file(const std::string& filename)
{
#ifdef _WIN32
  ifstream input(convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary | ios_base::in);
#else
  ifstream input(filename.c_str(), ios_base::binary | ios_base::in);
#endif

  if (!input.is_open())
    return false;

  const bool success = load_from_caen_gxml(input);

  if (success)
    filename_ = filename;

  return success;
}//bool load_caen_gxml_file(const std::string& filename)


bool SpecFile::load_from_caen_gxml(std::istream& input)
{
  reset();

  if (!input.good())
    return false;

  std::unique_lock<std::recursive_mutex> scoped_lock(mutex_);

  const istream::pos_type start_pos = input.tellg();

  try
  {
    input.unsetf(ios::skipws);

    if (!is_candidate_gxml(input))
      throw runtime_error("Not GXML file candidate.");

    rapidxml::file<char> input_file(input);

    std::unique_ptr<rapidxml::xml_document<char>> doc( new rapidxml::xml_document<char>() );

    char* data = input_file.data();
    char* data_end = data + input_file.size();

    doc->parse<rapidxml::parse_trim_whitespace | rapidxml::allow_sloppy_parse>(data, data_end);

    const rapidxml::xml_node<char>* const bgamma_node = XML_FIRST_INODE(doc.get(), "BGAMMA");
    if (!bgamma_node)
      throw runtime_error("No BGAMMA element");

    const rapidxml::xml_node<char>* const spectrum_node = XML_FIRST_INODE(bgamma_node, "SPECTRUM");
    if (!spectrum_node)
      throw runtime_error("No SPECTRUM element");

    const rapidxml::xml_node<char>* const data_node = XML_FIRST_INODE(spectrum_node, "DATA");
    if (!data_node || (data_node->value_size() < 16))
      throw runtime_error("No spectrum DATA element");

    vector<string> warnings, remarks;
    auto counts = make_shared<vector<float>>();
    if (!SpecUtils::split_to_floats(data_node->value(), data_node->value_size(), *counts))
      warnings.push_back("May not have read in all channel counts.");

    if (counts->size() < 16)
      throw runtime_error("No channel counts");

    float live_time = 0.0f, real_time = 0.0f;
    const rapidxml::xml_node<char>* const lt_node = XML_FIRST_INODE(spectrum_node, "ELT");
    if (!lt_node 
      || !lt_node->value_size() 
      || !SpecUtils::parse_float(lt_node->value(), lt_node->value_size(), live_time) )
    {
      warnings.push_back("Unable to parse live time.");
    }

    const rapidxml::xml_node<char>* const rt_node = XML_FIRST_INODE(spectrum_node, "ERT");
    if (!rt_node
      || !rt_node->value_size()
      || !SpecUtils::parse_float(rt_node->value(), rt_node->value_size(), real_time))
    {
      warnings.push_back("Unable to parse real time.");
    }

    // From the one example I have, the <DATETIME> node is before START/STOP time, so we'll ignore for now
    //const rapidxml::xml_node<char>* const datetime_node = XML_FIRST_INODE(bgamma_node, "DATETIME");
    
    const rapidxml::xml_node<char>* const meas_node = XML_FIRST_INODE(bgamma_node, "MEASUREMENT");
    
    SpecUtils::time_point_t start_time{};
    if (meas_node)
    {
      const rapidxml::xml_node<char>* const operator_node = XML_FIRST_INODE(meas_node, "OPERATOR");
      const rapidxml::xml_node<char>* const start_node = XML_FIRST_INODE(meas_node, "START");
      const rapidxml::xml_node<char>* const stop_node = XML_FIRST_INODE(meas_node, "STOP");
      const rapidxml::xml_node<char>* const comments_node = XML_FIRST_INODE(meas_node, "COMMENTS");
      const rapidxml::xml_node<char>* const tags_node = XML_FIRST_INODE(meas_node, "TAGS");
      if (start_node && start_node->value_size())
        start_time = SpecUtils::time_from_string(xml_value_str(start_node));
      if (comments_node && comments_node->value_size())
        remarks.push_back(xml_value_str(comments_node));  
      if (tags_node && tags_node->value_size())
        remarks.push_back("TAGS: " + xml_value_str(tags_node));
      if (operator_node && operator_node->value_size())
        remarks.push_back("Operator: " + xml_value_str(operator_node));
    }//if (meas_node)
    

    const rapidxml::xml_node<char>* const ch_start_node = XML_FIRST_INODE(spectrum_node, "CHNNLSTART");
    if (ch_start_node && ch_start_node->value() && !xml_value_compare(ch_start_node, "0"))
      warnings.push_back("File defined a channel start of '" 
                         + xml_value_str(ch_start_node) + "' - which is not handled." );

    const rapidxml::xml_node<char>* const ch_end_node = XML_FIRST_INODE(spectrum_node, "CHNNLEND");
    if (ch_end_node && ch_end_node->value())
    {
      int val = 0;
      parse_int(ch_end_node->value(), ch_end_node->value_size(), val);
      if( val != static_cast<int>(counts->size() - 1) )
        warnings.push_back("File defined a channel end of '"
          + xml_value_str(ch_end_node) + "' - which is not handled.");
    }//if (ch_end_node && ch_end_node->value())


    auto meas = make_shared<Measurement>();
    meas->gamma_counts_ = counts;
    meas->start_time_ = start_time;
    meas->real_time_ = real_time;
    meas->live_time_ = live_time;
    meas->gamma_count_sum_ = 0.0;
    for (const float& v : *counts)
      meas->gamma_count_sum_ += v;
    
    measurements_.push_back(meas);
    parse_warnings_ = warnings;
    remarks_ = remarks;
    manufacturer_ = "CAEN";
    instrument_model_ = "Hexagon";
    instrument_type_ = "";
    detector_type_ = DetectorType::Unknown;

    cleanup_after_load();

    return true;
  }catch (std::exception& e)
  {
    input.clear();
    input.seekg(start_pos, ios::beg);
    reset();
    return false;
  }//try/catch
  
  return true;
}//bool load_from_caen_gxml(std::istream& input)


}//namespace SpecUtils
