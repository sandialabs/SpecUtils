/**
 SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 Copyright (C) 2016 William Johnson
 Copyright (C) 2023 Chris Kuethe

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

#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "rapidxml/rapidxml.hpp"

#include "SpecUtils/DateTime.h"
#include "SpecUtils/EnergyCalibration.h"
#include "SpecUtils/RapidXmlUtils.hpp"
#include "SpecUtils/SpecFile.h"
#include "SpecUtils/StringAlgo.h"

using namespace std;

namespace SpecUtils {

bool SpecFile::load_radiacode_file(const std::string& filename) {
#ifdef _WIN32
  ifstream input(convert_from_utf8_to_utf16(filename).c_str(),
                 ios_base::binary | ios_base::in);
#else
  ifstream input(filename.c_str(), ios_base::binary | ios_base::in);
#endif

  if (!input.is_open())
    return false;

  const bool success = load_from_radiacode(input);

  if (success)
    filename_ = filename;

  return success;
}  // bool load_radiacode_file( const std::string &filename );

bool SpecFile::load_from_radiacode(std::istream& input) {
  reset();

  if (!input.good())
    return false;

  std::unique_lock<std::recursive_mutex> scoped_lock(mutex_);

  const istream::pos_type start_pos = input.tellg();
  input.unsetf(ios::skipws);

  // Determine stream size
  input.seekg(0, ios::end);
  const size_t file_size = static_cast<size_t>(input.tellg() - start_pos);
  input.seekg(start_pos);

  // The smallest valid 256 channel RadiaCode file I've been able to construct
  // is about 7KB. Typical 1024-channel foreground RC files are about 27KB going
  // up to 31KB for files with many counts per channel. My largest real file
  // with both foreground and background spectra is 59KB. My largest synthetic
  // dual spectrum file that can load back into the RC app is 68KB.
  //
  // The limits of the numeric formats or the descriptive strings have not been
  // carefully analyzed.
  //
  // Finally, there appears to be space for including a thumbnail of the
  // spectrum plot which might be another 6-7KB, but I have not seen any real
  // world files including a thumbnail.
  //
  // Taken together, 80KB should be sufficient to load a RadiaCode file,
  //  but we'll be a little loose with things, jic

  if( (file_size < (5*1024)) || (file_size > (160*1024)) )
    return false;

  string filedata;
  filedata.resize(file_size + 1);

  input.read(&(filedata[0]), static_cast<streamsize>(file_size));
  filedata[file_size] = 0;  // jic.

  // Look for some distinctive strings early in the file
  // If they exist, this is probably a RadiaCode file.
  int signature_max_offset = 512;
  const auto fmtver_pos = filedata.find("<FormatVersion>");
  if (fmtver_pos == string::npos || fmtver_pos > signature_max_offset)
    return false;

  const auto dcr_pos = filedata.find("<DeviceConfigReference>");
  if (dcr_pos == string::npos || dcr_pos > signature_max_offset)
    return false;

  const auto device_model_pos = filedata.find("RadiaCode-");
  if (device_model_pos == string::npos ||
      device_model_pos > signature_max_offset)
    return false;

  if (device_model_pos < dcr_pos)
    return false;

  try
  {
    rapidxml::xml_document<char> doc;
    doc.parse<rapidxml::parse_trim_whitespace|rapidxml::allow_sloppy_parse>( &(filedata[0]) );


    /*
     The RadiaCode XML format has no published specification. In the example
     below, fixed values such as "120920" or "2" which do not appear to change
     between data files are included verbatim; actual varying quantities are
     indicated by their type, such as (float), (integer), or (string).

    ------------------------------------------------------------------------

     <?xml version="1.0"?>
     <ResultDataFile xmlns:xsd="http://www.w3.org/2001/XMLSchema"
     xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
       <FormatVersion>(int)</FormatVersion>
       <ResultDataList>
         <ResultData>
           <DeviceConfigReference>
             <Name>RadiaCode-(int)</Name>
           </DeviceConfigReference>

         <BackgroundSpectrumFile>(string)</BackgroundSpectrumFile>

         <StartTime>(isotime)</StartTime>
         <EndTime>(isotime)</EndTime>

         <EnergySpectrum>
           <NumberOfChannels>(integer)</NumberOfChannels>
           <ChannelPitch>1</ChannelPitch>
           <SpectrumName>(string)</SpectrumName>
           <SerialNumber>(string)</SerialNumber>
           <EnergyCalibration>
             <PolynomialOrder>2</PolynomialOrder>
             <Coefficients>
               <Coefficient>(float)</Coefficient>
               <Coefficient>(float)</Coefficient>
               <Coefficient>(float)</Coefficient>
             </Coefficients>
           </EnergyCalibration>
           <MeasurementTime>(integer)</MeasurementTime>
           <Spectrum>
             <DataPoint>(integer)</DataPoint>
             ...
             <DataPoint>(integer)</DataPoint>
           </Spectrum>
         </EnergySpectrum>

         <StartTime>(isotime)</StartTime>
         <EndTime>(isotime)</EndTime>

         <BackgroundEnergySpectrum>
         ...
         </BackgroundEnergySpectrum>

         <Visible>true</Visible>
         <PulseCollection>
           <Format>Base64 encoded binary</Format>
           <Pulses />
         </PulseCollection>

         </ResultData>
       </ResultDataList>
     </ResultDataFile>

     */
    
    // We will first define a lambda to parse the <EnergySpectrum> and <BackgroundEnergySpectrum>
    //  elements, as they share a lot of the same stuff (we have to use a lambda since we fill out
    //  protected members of Measurement, so we cant use an anonymous function).
    const auto parse_meas = []( const rapidxml::xml_node<char> * const spectrum_node ) -> shared_ptr<Measurement> {
      shared_ptr<Measurement> meas = make_shared<Measurement>();
      
      const rapidxml::xml_node<char> * const real_time_node = XML_FIRST_INODE( spectrum_node, "MeasurementTime" );
      
      if( !real_time_node || !parse_float(real_time_node->value(), real_time_node->value_size(), meas->real_time_) )
        meas->parse_warnings_.push_back( "Could not parse measurement duration." );
      meas->live_time_ = meas->real_time_; // Only clock-time is given in the file.
      
      
      // Start/End time are sibling nodes of EnergySpectrum when they should
      // be children of that node... the same as MeasurementTime. At least
      // they come before their associated EnergySpectrum.
      const rapidxml::xml_node<char> * const start_time_node = spectrum_node->previous_sibling( "StartTime" );
      const string time_str = xml_value_str(start_time_node);
      if( time_str.size() )
        meas->start_time_ = SpecUtils::time_from_string( time_str, SpecUtils::DateParseEndianType::LittleEndianFirst );
      
      const rapidxml::xml_node<char> * const title_node = XML_FIRST_INODE( spectrum_node, "SpectrumName" );
      meas->title_ = xml_value_str( title_node );
      
      const rapidxml::xml_node<char> * const channel_count_node = XML_FIRST_INODE( spectrum_node, "Spectrum" );
      if( !channel_count_node )
        throw runtime_error( "No Spectrum node under the EnergySpectrum node" );
      
      const rapidxml::xml_node<char> * const nchannel_node = XML_FIRST_INODE( spectrum_node, "NumberOfChannels" );
      size_t num_exp_channels = 0;
      if( nchannel_node )
      {
        int value = 0;
        if( parse_int( nchannel_node->value(), nchannel_node->value_size(), value ) )
          num_exp_channels = static_cast<size_t>( std::max( value, 0 ) );
      }
      
      bool error_with_cc = false; //track if we have any errors parsing channel counts numbers
      auto channel_counts = std::make_shared<vector<float>>();
      
      // Reserve number of channels, to avoid some probably unnecessary allocation and copies
      channel_counts->reserve( (num_exp_channels > 16) ? num_exp_channels : size_t(1024) );
      
      XML_FOREACH_CHILD( x, channel_count_node, "DataPoint" )
      {
        int value = 0;
        error_with_cc |= !parse_int( x->value(), x->value_size(), value );
        channel_counts->push_back( value );
      }//foreach( <DataPoint> ) node)
      
      if( error_with_cc )
        meas->parse_warnings_.push_back( "Some channel counts were not correctly parsed." );
      
      if( (num_exp_channels > 16) && (num_exp_channels != channel_counts->size()) )
        meas->parse_warnings_.push_back( "The number of parsed energy channels ("
                                        + std::to_string(channel_counts->size()) + ") didn't match"
                                        " number of expected (" + std::to_string(num_exp_channels)
                                        + ")." );
      
      const size_t num_channels = channel_counts->size();
      if( num_channels < 16 ) //16 is arbitrary
        throw runtime_error( "Insufficient foreground spectrum channels." );
      
      meas->gamma_counts_ = channel_counts;
      
      const rapidxml::xml_node<char> * const energy_cal_node = XML_FIRST_INODE( spectrum_node, "EnergyCalibration" );
      const rapidxml::xml_node<char> * const coeffs_node = xml_first_inode( energy_cal_node, "Coefficients");
      if( coeffs_node )
      {
        vector<float> cal_coefs;
        XML_FOREACH_CHILD( coef_node, coeffs_node, "Coefficient" )
        {
          float coef_val = 0.0;
          if( parse_float(coef_node->value(), coef_node->value_size(), coef_val ) )
            cal_coefs.push_back( coef_val );
          else
            meas->parse_warnings_.push_back( "Error parsing energy calibration coefficient to float." );
        }//XML_FOREACH_CHILD( coef, energy_cal_node, "Coefficients" )
        
        try
        {
          auto newcal = make_shared<EnergyCalibration>();
          newcal->set_polynomial( num_channels, cal_coefs, {} );
          meas->energy_calibration_ = newcal;
        }catch( std::exception &e )
        {
          meas->parse_warnings_.push_back( "Error interpreting energy calibration: " + string(e.what()) );
        }
      }//if( energy_cal_node )
      
      meas->gamma_count_sum_ = std::accumulate(begin(*channel_counts), end(*channel_counts), 0.0);
      
      meas->detector_name_ = "gamma";
      meas->contained_neutron_ = false;
      
      return meas;
    };// const auto parse_meas lambda
    

    // Drill down to the <ResultData> node
    const rapidxml::xml_node<char> * const base_node = XML_FIRST_INODE( &doc, "ResultDataFile" );
    if( !base_node )
      throw runtime_error( "Missing ResultDataFile node." );
    
    const rapidxml::xml_node<char> * const data_list_node = XML_FIRST_INODE( base_node, "ResultDataList" );
    if( !data_list_node )
      throw runtime_error( "Missing ResultDataList node." );
    
    const rapidxml::xml_node<char>* n_root = XML_FIRST_INODE( data_list_node, "ResultData" );
    if (!n_root)
      throw runtime_error("unable to find ResultData");

    const rapidxml::xml_node<char> * const config_node = XML_FIRST_INODE( n_root, "DeviceConfigReference" );
    if( config_node )
    {
      const rapidxml::xml_node<char> * const name_node = XML_FIRST_INODE( config_node, "Name" );
      instrument_model_ = xml_value_str( name_node );
    }//if( config_node )
    
    const rapidxml::xml_node<char> * const foreground_node = XML_FIRST_INODE( n_root, "EnergySpectrum" );
    if( !foreground_node )
      throw runtime_error( "No EnergySpectrum node." );
    
    if( XML_NEXT_TWIN(foreground_node) )
      throw runtime_error("File contains more than one EnergySpectrum");
    
    const rapidxml::xml_node<char> * const serial_num_node = XML_FIRST_INODE( foreground_node, "SerialNumber" );
    instrument_id_ = xml_value_str( serial_num_node );
    
    //const rapidxml::xml_node<char> * const version_node = XML_FIRST_INODE( base_node, "FormatVersion" );
    //if( version_node && (xml_value_str(version_node) != "120920") )
    //  parse_warnings_.push_back( "This version of radiacode XML file not tested." );
    
    // Now actually parse the foreground <EnergySpectrum> node.
    shared_ptr<Measurement> fg_meas = parse_meas( foreground_node );
    assert( fg_meas && (fg_meas->num_gamma_channels() >= 16) );
    
    fg_meas->source_type_ = SourceType::Foreground;
     
    measurements_.push_back( fg_meas );
    
    // Check for and try to parse background spectrum.
    const rapidxml::xml_node<char> * const background_node = XML_FIRST_INODE( n_root, "BackgroundEnergySpectrum" );
    if( background_node )
    {
      try
      {
        shared_ptr<Measurement> bg_meas = parse_meas( background_node );
        assert( bg_meas && (bg_meas->num_gamma_channels() >= 16) );
        
        bg_meas->source_type_ = SourceType::Background;
        if( !bg_meas->energy_calibration_ || !bg_meas->energy_calibration_->valid() )
        {
          if( bg_meas->gamma_counts_->size() == bg_meas->gamma_counts_->size() )
            bg_meas->energy_calibration_ = fg_meas->energy_calibration_;
        }
        
        const rapidxml::xml_node<char> * const background_file_node = XML_FIRST_INODE( n_root, "BackgroundSpectrumFile" );
        if( bg_meas->title_ != xml_value_str(background_file_node) )
          parse_warnings_.push_back( "Background spectrum name not consistent with expected background." );
        
        measurements_.push_back( bg_meas );
      }catch( std::exception &e )
      {
        parse_warnings_.push_back( "Failed to parse background spectrum in file: " + string(e.what()) );
      }
    }//if( background_node )
    
    instrument_type_ = "Spectroscopic Personal Radiation Detector";
    manufacturer_ = "Scan-Electronics";
    detector_type_ = SpecUtils::DetectorType::RadiaCode;
    
    cleanup_after_load();
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg(start_pos, ios::beg);
    return false;
  }// try / catch

  return true;
}//bool load_from_radiacode( std::istream &input )

}// namespace SpecUtils
