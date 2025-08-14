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

#include <string>
#include <vector>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <regex>

#include "rapidxml/rapidxml.hpp"

#include "SpecUtils/DateTime.h"
#include "SpecUtils/SpecFile.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/RapidXmlUtils.hpp"
#include "SpecUtils/EnergyCalibration.h"


using namespace std;

namespace
{
  // Up through at least Sep 2024, the Radiacode XML files dont contain dead-time,
  //  However, we can estimate this based on dead-time per pulse.
  //  From https://github.com/sandialabs/SpecUtils/issues/35 , we expect dead
  //  time to be 5 us per pulse (although a previous preliminary measurement yielded 54 us)
  float estimate_radiacode102_live_time( const float real_time, const double total_counts )
  {
    if( (real_time <= 0.0) || (total_counts <= 0.0) )
      return real_time;
    
    const double dead_time_pp = 5.0E-06;
    const double detected_cps = total_counts / real_time;
    if( (detected_cps > (1.0 / dead_time_pp)) || IsNan(detected_cps) || IsInf(detected_cps) )
      return real_time;
    
    const double estimated_true_cps = (detected_cps)/(1 - detected_cps * dead_time_pp);
    const double live_time = real_time * (detected_cps / estimated_true_cps);
    
    return static_cast<float>(live_time);
  };//float estimate_radiacode102_live_time( const float real_time, const double total_counts )
}//namespace

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

  bool success = load_from_radiacode(input);

  if( !success )
  {
    input.seekg(0);
    success = load_from_radiacode_spectrogram( input );
  }
  
  if (success)
    filename_ = filename;

  return success;
}// bool load_radiacode_file( const std::string &filename );


bool SpecFile::load_from_radiacode(std::istream& input) {
  reset();

  if( !input.good() )
    return false;

  std::unique_lock<std::recursive_mutex> scoped_lock(mutex_);

  const istream::pos_type start_pos = input.tellg();
  input.unsetf(ios::skipws);

  // Determine stream size
  input.seekg(0, ios::end);
  size_t file_size = static_cast<size_t>(input.tellg() - start_pos);
  input.seekg(start_pos);

  // The smallest valid 256 channel RadiaCode XML file I've been able to construct
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
  // We'll require the file to be at least 5 kb, but smaller than 10 MB, although
  //  a couple MB would probably be plenty.

  if( (file_size < (5*1024)) || (file_size > (10*1024*1024)) )
    return false;

  string filedata;
  filedata.resize(file_size + 1);

  input.read(&(filedata[0]), static_cast<streamsize>(file_size));
  filedata[file_size] = 0;  // jic.

  // Look for some distinctive strings early in the file
  // If they exist, this is probably a RadiaCode or BecqMoni file.
  const size_t signature_max_offset = 512;
  const string::size_type fmtver_pos = filedata.find("<FormatVersion>");
  if( (fmtver_pos == string::npos) || (fmtver_pos > signature_max_offset) )
    return false;

  const string::size_type dcr_pos = filedata.find("<DeviceConfigReference>");
  if( dcr_pos == string::npos )
    return false;

  const string::size_type energy_spectrum_pos = filedata.find("<EnergySpectrum");
  if( energy_spectrum_pos == string::npos )
    return false;

  if( energy_spectrum_pos < dcr_pos )
    return false;

  try
  {
    rapidxml::xml_document<char> doc;
    doc.parse<rapidxml::parse_trim_whitespace|rapidxml::allow_sloppy_parse>( &(filedata[0]) );


    /*
     The BecqMoni/RadiaCode XML format has no published specification. In the example
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
    const auto parse_meas = []( const rapidxml::xml_node<char> * const spectrum_node, const bool is_radiacode ) -> shared_ptr<Measurement> {
      shared_ptr<Measurement> meas = make_shared<Measurement>();
      
      const rapidxml::xml_node<char> * const real_time_node = XML_FIRST_INODE( spectrum_node, "MeasurementTime" );
      const rapidxml::xml_node<char> * const live_time_node = XML_FIRST_INODE( spectrum_node, "LiveTime" );
      
      if( !real_time_node || !parse_float(real_time_node->value(), real_time_node->value_size(), meas->real_time_) )
        meas->parse_warnings_.push_back( "Could not parse measurement duration." );
      meas->live_time_ = meas->real_time_; // Only clock-time is given in the file.
      
      if( live_time_node && live_time_node->value_size() )
      {
        if( !parse_float(live_time_node->value(), live_time_node->value_size(), meas->live_time_) )
          meas->parse_warnings_.push_back( "Could not parse live-time." );
      }
      
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
          num_exp_channels = static_cast<size_t>( std::min(std::max(value, 0), 4096) );
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
      
      // Make up for estimated dead-time
      if( (!live_time_node || !live_time_node->value_size()) && is_radiacode )
      {
        meas->live_time_ = estimate_radiacode102_live_time( meas->real_time_, meas->gamma_count_sum_ );
        // If this correction makes a difference, warn users that it may not be super great,
        //  since I didnt extensively check how applicable the estimate is.
        if( fabs(meas->live_time_ - meas->real_time_) > (0.001*meas->real_time_) )
          meas->parse_warnings_.push_back( "An estimated dead-time correction has been used"
                                          " to correct spectrum live-time." );
      }//if( is_radiacode )
      
      return meas;
    };// const auto parse_meas lambda
    

    bool is_radiacode = false;
    
    // Drill down to the <ResultData> node
    const rapidxml::xml_node<char> * const base_node = XML_FIRST_INODE( &doc, "ResultDataFile" );
    if( !base_node )
      throw runtime_error( "Missing ResultDataFile node." );
    
    const rapidxml::xml_node<char> * const data_list_node = XML_FIRST_INODE( base_node, "ResultDataList" );
    if( !data_list_node )
      throw runtime_error( "Missing ResultDataList node." );
    
    XML_FOREACH_CHILD( n_root, data_list_node, "ResultData" )
    {
      const rapidxml::xml_node<char> * const config_node = XML_FIRST_INODE( n_root, "DeviceConfigReference" );
      if( config_node )
      {
        const rapidxml::xml_node<char> * const name_node = XML_FIRST_INODE( config_node, "Name" );
        if( name_node && name_node->value_size() )
          instrument_model_ = xml_value_str( name_node );
      }//if( config_node )
      
      const rapidxml::xml_node<char> * const foreground_node = XML_FIRST_INODE( n_root, "EnergySpectrum" );
      if( !foreground_node )
        throw runtime_error( "No EnergySpectrum node." );
      
      if( XML_NEXT_TWIN(foreground_node) )
        throw runtime_error("File contains more than one EnergySpectrum");
      
      const rapidxml::xml_node<char> * const serial_num_node = XML_FIRST_INODE( foreground_node, "SerialNumber" );
      if( serial_num_node && serial_num_node->value_size() )
      {
        instrument_id_ = xml_value_str( serial_num_node );
        // RadiaCode now has a variant of the RC103 with a different
        // scintillator. Instead of perhaps calling it a 104, they
        // called it a 103G. For the moment, let's extend the regex
        // to allow the G variant. We can revisit this if and when
        // the proliferation of versions becomes a problem.
        std::regex rc_sn_regex("^RC-(\\d{3}G?)-\\d{6}$");
		std::smatch match_result;

		if ( std::regex_search(instrument_id_, match_result, rc_sn_regex) )
        {
          // Under some circumstances, the radiacode app will mis-identify the source device.
          // I have several data files produced by RC-102-XXXXXX and RC-103-YYYYYY where the
          // DeviceConfigReference/Name node is "RadiaCode-101".
          // Test for this discrepancy and patch the instrument model field if necessary.
          string model_from_sn = "RadiaCode-" + static_cast<string>(match_result[1]);
          if( instrument_model_.find( model_from_sn ) == string::npos )
          {
#if(PERFORM_DEVELOPER_CHECKS)
            parse_warnings_.push_back(
              "DeviceConfigModel " + instrument_model_ +
              " is not consistent with SerialNumber " + instrument_id_ +
              ". Patching to " + model_from_sn );
#endif
            instrument_model_ = model_from_sn;
          }
#if(PERFORM_DEVELOPER_CHECKS)
        } else {
          parse_warnings_.push_back( "SerialNumber " + instrument_id_ + " does not match expected format" );
#endif
        }
      }
      
      //const rapidxml::xml_node<char> * const version_node = XML_FIRST_INODE( base_node, "FormatVersion" );
      //if( version_node && (xml_value_str(version_node) != "120920") )
      //  parse_warnings_.push_back( "This version of radiacode XML file not tested." );
      
      is_radiacode = (is_radiacode || icontains(instrument_model_, "RadiaCode-") );
      
      // Now actually parse the foreground <EnergySpectrum> node.
      shared_ptr<Measurement> fg_meas = parse_meas( foreground_node, is_radiacode );
      assert( fg_meas && (fg_meas->num_gamma_channels() >= 16) );
      
      fg_meas->source_type_ = SourceType::Foreground;
      
      measurements_.push_back( fg_meas );
      
      // Check for and try to parse background spectrum.
      const rapidxml::xml_node<char> * const background_node = XML_FIRST_INODE( n_root, "BackgroundEnergySpectrum" );
      if( background_node )
      {
        try
        {
          shared_ptr<Measurement> bg_meas = parse_meas( background_node, is_radiacode );
          assert( bg_meas && (bg_meas->num_gamma_channels() >= 16) );
          
          bg_meas->source_type_ = SourceType::Background;
          if( !bg_meas->energy_calibration_ || !bg_meas->energy_calibration_->valid() )
          {
            if( bg_meas->gamma_counts_->size() == bg_meas->gamma_counts_->size() )
              bg_meas->energy_calibration_ = fg_meas->energy_calibration_;
          }
          
          measurements_.push_back( bg_meas );
        }catch( std::exception &e )
        {
          parse_warnings_.push_back( "Failed to parse background spectrum in file: " + string(e.what()) );
        }
      }//if( background_node )
    }//XML_FOREACH_CHILD( n_root, data_list_node, "ResultData" )
    
    if( icontains( instrument_model_, "RadiaCode-" ) )
    {
      instrument_type_ = "Spectroscopic Personal Radiation Detector";
      manufacturer_ = "Scan-Electronics";
      detector_type_ = SpecUtils::DetectorType::RadiaCodeCsI10;
    }else
    {
      // File probably made with BecqMoni
    }
    
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


bool SpecFile::load_from_radiacode_spectrogram( std::istream& input )
{
  reset();

  if( !input.good() )
    return false;

  std::unique_lock<std::recursive_mutex> scoped_lock(mutex_);

  const istream::pos_type start_pos = input.tellg();
  input.unsetf(ios::skipws);

  // Determine stream size
  input.seekg(0, ios::end);
  size_t file_size = static_cast<size_t>(input.tellg() - start_pos);
  input.seekg(start_pos);

  // If under a kb, definitely not a valid file
  if( file_size < (1*1024) )
    return false;

  const size_t max_header_len = 512;
  assert( max_header_len < file_size );
  
  // First we'll check the beginning of the file to make sure it looks like
  //  its probably a spectrogram file
  string headerdata;
  headerdata.resize(max_header_len + 1);

  input.read(&(headerdata[0]), static_cast<streamsize>(max_header_len));
  headerdata[max_header_len] = 0;  // jic.
  input.clear();
  input.seekg(start_pos, ios::beg);
  
  // Look for some distinctive strings early in the file - for the moment we'll be
  //  pretty restrictive in what we require to be there.
  if( (headerdata.find("Spectrogram:") == string::npos)
     || (headerdata.find("Accumulation time:") == string::npos)
     || (headerdata.find("Timestamp:") == string::npos)
     || (headerdata.find("Time:") == string::npos)
     || (headerdata.find("Channels:") == string::npos) )
  {
    return false;
  }

  try
  {
    // We'll start back over at the beginning
    //  The fields of the header are tab-separated - we will rely on this
    string header;
    while( safe_get_line(input, header, 10*1024) && header.empty() )
    {
    }
    
    auto get_header_field_str = [&headerdata]( const string &field, const bool required ) -> string {
      const size_t pos = headerdata.find( field + ":" );
      if( (pos == string::npos) && required )
        throw logic_error( "radiacode expected header field, '" + field + "', not found" );
      if( pos == string::npos )
        return "";
      
      string value = headerdata.substr(pos + field.size() + 1);
      const size_t tab_pos = value.find('\t');
      if( tab_pos != string::npos )
        value = value.substr(0,tab_pos);
      trim(value);
      return value;
    };//get_header_field_str lambda
    
    const string name = get_header_field_str("Spectrogram", true);
    const string time_str = get_header_field_str("Time", true);
    const string timestamp_str = get_header_field_str("Timestamp", true);
    //const string acc_time_str = get_header_field_str("Accumulation time", true);
    const string channels_str = get_header_field_str("Channels", true);
    const string serial_num = get_header_field_str("Device serial", false);
    const string flags = get_header_field_str("Flags", false);
    const string comment = get_header_field_str("Comment", false);
    
    const time_point_t start_time = time_from_string( time_str );
    
    uint64_t timestamp = 0;
    if( !(stringstream(timestamp_str) >> timestamp) )
      throw runtime_error( "Unexpected timestamp format" );
    
    size_t num_channels = 0;
    if( !(stringstream(channels_str) >> num_channels) || (num_channels < 16) || (num_channels > 4096) )
      throw runtime_error( "Invalid 'Channels' field" );

    vector<string> warnings;
    vector<shared_ptr<Measurement>> meass;
    auto energy_cal = make_shared<EnergyCalibration>();

    int sample_num = 0;
    uint64_t last_timestamp = timestamp;
    size_t skipped_lines = 0, total_lines = 0;
    string line;
	bool line_warning = true;
    while( safe_get_line(input, line, 64*1024) )
    {
      total_lines += 1;
      
      // We'll be pretty generous about allowing invalid lines
      if( line_warning && (skipped_lines > 5) && (total_lines > 10) && (skipped_lines > (total_lines/10)) ) {
        warnings.push_back("Many invalid lines detected");
        line_warning = false;
      }

      trim( line );
      if( line.empty() )
      {
        skipped_lines += 1;
        continue;
      }

      // The second line in the file - "Spectrum:" - is a hex-encoded
      // representation of the total recorded dose since the last device reset.
      // 0-3: spectrum accumulation time in seconds (uint32)
      // 4-15: calibration factors a0, a1, a2 (float[3])
      // 16 .. : counts per channel (uint32[1024])

      string pfx = "Spectrum: ";  // the space is intentional
      if ((string::npos != line.find(pfx)) && (1 == total_lines) && (line.length() >= 57)) {
        try {
          uint8_t raw_bytes[16];
          vector<float> cal_coefs;
          const size_t ds = pfx.length();

          // "unhexlify"
          for (size_t i = 0; i < sizeof(raw_bytes); i++) {
            string tmp = line.substr(ds + i * 3, 2);
            raw_bytes[i] = strtoul(tmp.c_str(), NULL, 16);
          }

          // convert relevant bytes to a float and stash in calibration vector
          for (int i = 0; i < 3; i++) {
            float tmp;
            memcpy(&tmp, &raw_bytes[4 * i + 4], sizeof(tmp));
            cal_coefs.push_back(tmp);
          }

          energy_cal->set_polynomial(num_channels, cal_coefs, {});
        } catch (std::exception& e) {
          warnings.push_back("Error interpreting energy calibration: " +
                             string(e.what()));
        }
        continue;
      }

      if( !isdigit( (int)line.front() ) )
      {
        skipped_lines += 1;
        continue;
      }
      
      const size_t end_timestamp_pos = line.find('\t');
      if( end_timestamp_pos == string::npos )
      {
        skipped_lines += 1;
        continue;
      }
      
      const string this_timestamp_str = line.substr(0,end_timestamp_pos);
      uint64_t this_timestamp = 0;
      
      if( !(stringstream(this_timestamp_str) >> this_timestamp) )
      {
        skipped_lines += 1;
        continue;
      }
      
      const size_t end_nsecond = line.find('\t', end_timestamp_pos + 1);
      if( (end_nsecond == string::npos) || ((end_nsecond + 4) > line.size()) )
      {
        skipped_lines += 1;
        continue;
      }
      
      const string num_seconds_str = line.substr(end_timestamp_pos + 1, end_nsecond - end_timestamp_pos);
      float num_seconds = 0.0f;
      if( !(stringstream(num_seconds_str) >> num_seconds) )
      {
        skipped_lines += 1;
        continue;
      }

      const char * const counts_start = line.c_str() + end_nsecond + 1;
      const size_t counts_str_len = (line.c_str() + line.size()) - counts_start;
      
      auto channel_counts = make_shared<vector<float>>();
      if( !split_to_floats( counts_start, counts_str_len, *channel_counts ) )
        warnings.push_back( "All channel counts may not have been read." );
      
      if( channel_counts->size() < 2 )
      {
        skipped_lines += 1;
        continue;
      }
      
      if( channel_counts->size() > num_channels )
        throw runtime_error( "More channel counts than expected" );

      // For the benefit of future developers looking at this file to understand
      // this format: each spectrum line is truncated when all subsequent values
      // are 0. A single event int channel 0 would be recorded as "0 0 1".
      // Expand that out to the full <num_channels> members.
      channel_counts->resize( num_channels, 0.0f );
      
      float real_time = 0.0f;
      if( last_timestamp <= this_timestamp )
      {
        real_time = num_seconds;
      }else
      {
        real_time = 1.0E-8 * (this_timestamp - last_timestamp);
        if( fabs(real_time - num_seconds) > 1.5 )
        {
          warnings.push_back( "Indeterminant real-time: timestamp implied " + to_string(real_time) + " seconds" );
          real_time = num_seconds;
        }
      }
      if( (real_time < 0.0f) || IsInf(real_time) || IsNan(real_time) )
      {
        warnings.push_back( "Real-time was negative, setting to zero." );
        real_time = 0.0f;
      }
      
      last_timestamp = this_timestamp;
      const double gamma_sum = std::accumulate( begin(*channel_counts), end(*channel_counts), 0.0 );
        
      auto meas = make_shared<Measurement>();
      meas->real_time_ = real_time;
      meas->live_time_ = estimate_radiacode102_live_time( real_time, gamma_sum );
      meas->gamma_counts_ = channel_counts;
      meas->gamma_count_sum_ = gamma_sum;
      meas->parse_warnings_ = warnings;
      meas->energy_calibration_ = energy_cal;
      meas->sample_number_ = sample_num;
      meas->detector_name_ = "gamma";
      
      if( !is_special(start_time) && (this_timestamp > timestamp) )
      {
        const uint64_t nticks = (this_timestamp - timestamp);
        meas->start_time_ = start_time + chrono::milliseconds(nticks/10000);
      }//if( !is_special(start_time) )
      
      
      meass.push_back( meas );
      
      sample_num += 1;
    }  // while( safe_get_line(input, line, 64*1024) )

    if( meass.empty() )
      throw runtime_error( "No measurements" );
    
    measurements_ = meass;
    instrument_id_ = serial_num;
    
    if( !name.empty() )
      remarks_.push_back( "Name: " + name );
    if( !comment.empty() )
      remarks_.push_back( "Comment: " + comment );
    
    instrument_type_ = "Spectroscopic Personal Radiation Detector";
    manufacturer_ = "Scan-Electronics";
    detector_type_ = SpecUtils::DetectorType::RadiaCodeCsI10;
    
    cleanup_after_load();
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg(start_pos, ios::beg);
    
    return false;
  }// try / catch

  return true;
}//bool parse_radiacode_spectrogram( std::istream& input )
    
  
}// namespace SpecUtils
