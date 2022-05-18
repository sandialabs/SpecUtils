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
#include <vector>
#include <memory>
#include <string>
#include <cctype>
#include <limits>
#include <float.h>
#include <numeric>
#include <fstream>
#include <cctype>
#include <float.h>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <functional>


#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;


namespace SpecUtils
{
bool SpecFile::load_txt_or_csv_file( const std::string &filename )
{
  try
  {
#ifdef _WIN32
    std::unique_ptr<ifstream> input( new ifstream( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in ) );
#else
    std::unique_ptr<ifstream> input( new ifstream( filename.c_str(), ios_base::binary|ios_base::in ) );
#endif
    
    if( !input->is_open() )
      return false;
    
    //lets make sure this is an ascii file; we'll just check the first 255 bytes, and call this
    //  good enough.
    //Really we should make sure its a UTF-8 file, so we can export from Excel
    
    uint8_t first_bytes[256] = { 0 };
    input->read( (char *) (&first_bytes), sizeof(first_bytes) - 1 );
    first_bytes[sizeof(first_bytes) - 1] = '\0'; //jic
    input->seekg( 0, ios::beg );
    
    // Even the formats that claim UTF-8, all seem to be ascii
    const bool is_utf8 = ((first_bytes[0] == 0xEF)
                          && (first_bytes[1] == 0xBB)
                          && (first_bytes[2] == 0xBF) );
    
    /*
     //TODO: check if this stream-based UTF-16 to UTF-8 conversion actually works; useful when
     //      people save a txt file on windows to UTF-16.
     #include <locale>
     #include <codecvt>
     
     
     const bool is_utf16_big_endian = ((first_bytes[0] == 0xFE)
                                       && (first_bytes[1] == 0xFF));
     
     const bool is_utf16_little_endian = ((first_bytes[0] == 0xFF)
                                         && (first_bytes[1] == 0xFE));
    
    if( is_utf16_big_endian || is_utf16_little_endian )
    {
      input.reset();
      
      std::unique_ptr<wifstream> winput( new wifstream( filename, ios_base::binary | ios_base::in ) );
      // std::consume_header | std::little_endian;
      using convert_ut16_utf8 = std::wbuffer_convert< std::codecvt_utf8_utf16<char, 0x10ffff,std::consume_header> >;
      convert_ut16_utf8 converter( winput->rdbuf() );
      input.reset( new std::istream( &converter ) );
      
      // I *think* input should now be UTF-8???
    }
    */
   
    
    for( size_t i = (is_utf8 ? 3 : 0); i < (sizeof(first_bytes) - 1); ++i )
    {
      if( first_bytes[i] > 127 )
        return false;
    }
   
    
    //while( input->good() )
    //{
    //  const int c = input->get();
    //  if( input->good() && c>127 )
    //    return false;
    //}//while( input.good() )
    
    
    //we have an ascii file if we've made it here
    input->clear();
    input->seekg( (is_utf8 ? 3 : 0), ios_base::beg );
    
    
    //Check to see if this is a GR135 text file
    string firstline;
    // We'll limit string length read in; 4 kb is arbitrary, but should be way more than enough
    const size_t max_line_len = 4*1024;
    SpecUtils::safe_get_line( *input, firstline, max_line_len );
    
    bool success = false;
    
    const bool isGR135File = contains( firstline, "counts Live time (s)" )
                             && contains( firstline, "gieger" );
    
    
    
    if( isGR135File )
    {
      input->seekg( 0, ios_base::beg );
      success = load_from_Gr135_txt( *input );
    }
    
    
    // We'll allow some
    const bool isD3Raw = (!success
                         && (firstline.size() > ((max_line_len - 128)))
                         && (firstline.find( "Bin Number, 0, 1," ) < 10));
    if( isD3Raw )
    {
      input->seekg( 0, ios_base::beg );
      success = load_from_D3S_raw( *input );
    }
    
    
    const bool isSDF = ((!success && firstline.size() > 3 && firstline[2]==',')
                        && ( SpecUtils::starts_with( firstline, "GB" )
                            || SpecUtils::starts_with( firstline, "NB" )
                            || SpecUtils::starts_with( firstline, "S1" )
                            || SpecUtils::starts_with( firstline, "S2" )
                            || SpecUtils::starts_with( firstline, "GS" )
                            || SpecUtils::starts_with( firstline, "GS" )
                            || SpecUtils::starts_with( firstline, "NS" )
                            || SpecUtils::starts_with( firstline, "ID" )
                            || SpecUtils::starts_with( firstline, "AB" )));
    if( isSDF )
    {
      input->close();
      input.reset();
      
      success = load_spectroscopic_daily_file( filename );
      
      if( success )
        return true;
      
#ifdef _WIN32
      input.reset( new ifstream( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in ) );
#else
      input.reset( new ifstream( filename.c_str(), ios_base::binary|ios_base::in ) );
#endif
    }//if( isSDF )
    
    
    if( !success && (firstline.find("Fields, RSP 1, RSP 2") != string::npos) )
    {
      input->seekg( 0, ios_base::beg );
      success = load_from_srpm210_csv( *input );
    }//if( firstline.find("Fields, RSP 1, RSP 2") != string::npos )
    
    
    if( !success )
    {
      input->clear();
      input->seekg( (is_utf8 ? 3 : 0), ios_base::beg );
      success = load_from_txt_or_csv( *input );
    }
    
    if( success )
      filename_ = filename;
    else
      reset();
    
    return success;
  }catch(...)
  {}//try / catch
  
  reset();
  
  return false;
}//bool load_txt_or_csv_file( const std::string &filename )

  
bool SpecFile::load_from_txt_or_csv( std::istream &istr )
{
  reset();
  
  if( !istr.good() )
    return false;
  
  const std::streampos startpos = istr.tellg();
  
  string firstdata;
  firstdata.resize( 20, '\0' );
  if( !istr.read(&(firstdata[0]), 19) )
    return false;
  
  //Non- exaustive list of formats that we might be able to extract a spectrum
  //  from, but we really shouldnt, because its N42
  const char *not_allowed_txt[] = { "<?xml", "<Event", "<N42InstrumentData" };
  for( const char *txt : not_allowed_txt )
  {
    if( SpecUtils::icontains(firstdata, txt) )
      return false;
  }
  
  istr.seekg( startpos, ios::beg );
  
  double gamma_sum = 0.0, neutron_sum = 0.0;
  while( istr.good() )
  {
    try
    {
      auto m = std::make_shared<Measurement>();
      m->set_info_from_txt_or_csv( istr );
      
      if( m->num_gamma_channels() < 7 && !m->contained_neutron() )
        break;
      
      gamma_sum += m->gamma_count_sum();
      neutron_sum += m->neutron_counts_sum();
      
      measurements_.push_back( m );
    }catch( exception & )
    {
      //cerr << "SpecFile::load_from_txt_or_csv(...)\n\tCaught: " << e.what() << endl;
      break;
    }
  }//while( istr.good() )
  
  
  if( (gamma_sum < FLT_EPSILON) && (neutron_sum < FLT_EPSILON) )
  {
    reset();
    istr.clear();
    istr.seekg( startpos, ios::end );
    return false;
  }
  
  
  if( measurements_.empty() )
  {
    reset();
    istr.clear();
    istr.seekg( startpos, ios::end );
    return false;
  }
  
  try
  {
    cleanup_after_load();
  }catch( std::exception &e )
  {
    cerr << "SpecFile::load_from_txt_or_csv(istream &)\n\tCaught: " << e.what() << endl;
    reset();
    istr.clear();
    istr.seekg( startpos, ios::end );
    return false;
  }//try / catch
  
  if( measurements_.empty() )
  {
    istr.clear();
    istr.seekg( startpos, ios::end );
    reset();
    return false;
  }
  
  return true;
}//bool load_from_txt_or_csv( std::ostream& ostr )


bool SpecFile::load_from_D3S_raw( std::istream &input )
{
  reset();
  
  if( !input.good() )
    return false;
  
  const std::streampos startpos = input.tellg();
  
  try
  {
    
    //Bin Number, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
    //Energy[keV] ,-11.132,-16.503691,-15.844797,-15.185859...
    //10:37:42.261,5/12/2022,NODET,0,NA,NA,31.84942,-55.072339
    //Time,Date,Detection_ID,...Livetime(ms)....Dose Unit, Output(0), Output(1),...Output(71),Bin(0),Bin(1),Bin(2)
    //10:37:43.316,5/12/2022,NODET,0,NA,NA,31.84942,-55.072339,... [corresponding to headers of previous line]
    //[entries like the previous line repeated until end of file]
    
    // We'll limit the line length 512 kB; the example file I have is 512 kb total, with the
    //  longest line being 47 kb, so we'll be overly safe, since I think even 1 MB of memory
    //  is not a concern
    const size_t max_line_len = 512*1024;
    
    shared_ptr<EnergyCalibration> energy_cal;
    shared_ptr<DetectorAnalysis> detectors_analysis;
    
    {// begin code to get energy cal info
      string first_line;
      if( !SpecUtils::safe_get_line( input, first_line, max_line_len ) )
        throw runtime_error( "Failed to get first line." );
      
      size_t pos = first_line.find( ',' );
      if( pos == string::npos )
        throw runtime_error( "No comma in first line." );
      
      if( !icontains(first_line.substr(0,pos), "Bin Number" ) )
        throw runtime_error( "No 'Bin Number' in first line before first comma." );
      
      
      for( pos += 1; (pos < first_line.size()) && std::isspace(first_line[pos]); ++pos )
      {
      }
      
      if( pos >= first_line.size() )
        throw runtime_error( "Nearly empty first line." );
      
      //cout << "First character of first line is: '" << first_line[pos] << "'" << endl;
      first_line = first_line.substr(pos);
      
      vector<int> channel_numbers;
      split_to_ints( first_line.c_str(), first_line.size(), channel_numbers );
      //cout << "First line gives " << channel_numbers.size() << " channels." << endl;
      // TODO: maybe check channel numbers are increasing by one
      
      string second_line;
      if( !SpecUtils::safe_get_line( input, second_line, max_line_len ) )
        throw runtime_error( "Failed to get second line." );
      
      pos = second_line.find( ',' );
      if( pos == string::npos )
        throw runtime_error( "No comma in second line." );
      
      if( !icontains(second_line.substr(0,pos), "Energy[keV]" ) )
        throw runtime_error( "No 'Energy[keV]' in second line before first comma." );
      
      for( pos += 1; (pos < second_line.size()) && std::isspace(second_line[pos]); ++pos )
      {
      }
      
      if( pos >= second_line.size() )
        throw runtime_error( "Nearly empty second line." );
      
      //cout << "First character of second line is: '" << second_line[pos] << "'" << endl;
      second_line = second_line.substr(pos);
      
      std::vector<float> channel_energies;
      split_to_floats( second_line, channel_energies );
      
      //cout << "There are channel_energies.size()=" << channel_energies.size() << endl;
      
      // THe D3S look to have 4096 channels
      if( channel_energies.size() < 128 )
        throw runtime_error( "Too few channel numbers." );
      
      // TODO: check that channel_numbers is same size as channel_energies; even though we dont use them
      
      try
      {
        energy_cal = make_shared<EnergyCalibration>();
        
        // This next call will check that energies are monotonically increasing, and if not throw
        //  an exception.
        energy_cal->set_lower_channel_energy( channel_energies.size(), move(channel_energies) );
      }catch(std::exception &e)
      {
        energy_cal.reset();
        
        throw runtime_error( "Invalid channel energies: " + string(e.what()) );
      }//try / cat to set the energy cal.
      
      //cout << "Have set Energy cal." << endl;
    }// begin code to get energy cal info
    
    assert( energy_cal && energy_cal->valid() );
    
    // I am *guessing* this next line is background,
    string background_line_str;
    if( !SpecUtils::safe_get_line( input, background_line_str, max_line_len ) )
      throw runtime_error( "Failed to get background line." );
    
    string header_line;
    if( istarts_with( background_line_str, "Time," ) )
    {
      // I havent actually seen this; but seems reasonable it *might* happen (e.g., background
      //   left out).
      header_line.swap( background_line_str );
    }else
    {
      if( !SpecUtils::safe_get_line( input, header_line, max_line_len ) )
        throw runtime_error( "Failed to get header line." );
    }
    
    
    // In the example file I have, the headers are:
    //"Time", "Date", "Detection_ID", "Confidence", "UNUSED", "UNUSED", "Latitude", "Longitude",
    //"Processing Time(ms)", "Sensor Temp (degC)", "Battery (%)", "Livetime(ms)", "Neutron Count",
    //"Dose", "Dose Unit", "Output(0)", "Output(1)", ..., "Output(71)","Bin(0)","Bin(1)"..."Bin(4095)"
    int time_index = -1, date_index = -1, riid_id_index = -1, riid_conf_index = -1,
        latitude_index = -1, longitude_index = -1, process_time_ms_index = -1,
        sensor_temp_index = -1, battery_index = -1, live_time_index = -1, neutron_index = -1,
        dose_index = -1, does_unit_index = -1, output_start_index = -1, bin_start_index = -1,
        bin_last_index = -1;
    
    vector<string> headers;
    split_no_delim_compress( headers, header_line, "," );
    
    
    for( int index = 0; index < static_cast<int>(headers.size()); ++index )
    {
      string &hdr = headers[index];
      trim( hdr );
      
      if( istarts_with(hdr, "Time") )
        time_index = index;
      else if( istarts_with(hdr, "Date") )
        date_index = index;
      else if( istarts_with(hdr, "Detection_ID") )
        riid_id_index = index;
      else if( istarts_with(hdr, "Confidence") )
        riid_conf_index = index;
      else if( istarts_with(hdr, "Latitude") )
        latitude_index = index;
      else if( istarts_with(hdr, "Longitude") )
        longitude_index = index;
      else if( istarts_with(hdr, "Processing Tim") )
        process_time_ms_index = index;
      else if( istarts_with(hdr, "Sensor Temp") )
        sensor_temp_index = index;
      else if( istarts_with(hdr, "Battery") )
        battery_index = index;
      else if( istarts_with(hdr, "Livetime") )
        live_time_index = index;
      else if( istarts_with(hdr, "Neutron Count") )
        neutron_index = index;
      else if( iequals_ascii(hdr, "Dose") )
        dose_index = index;
      else if( istarts_with(hdr, "Dose Unit") )
        does_unit_index = index;
      else if( istarts_with(hdr, "Output(") )
      {
        if( output_start_index < 0 )
          output_start_index = index;
      }else if( istarts_with(hdr, "Bin(") )
      {
        if( bin_start_index < 0 )
          bin_start_index = index;
        else
          bin_last_index = index;
      }else if( !iequals_ascii(hdr, "UNUSED") )
      {
        cerr << "Unrecognized header '" << hdr << "' in D3S file - ignoring." << endl;
      }
    }//for( int index = 0; index < static_cast<int>(headers.size()); ++index )
    
    if( (bin_start_index < 0) || (bin_last_index < 0) || (live_time_index < 0) )
      throw runtime_error( "Failed to find necessary CSV headers" );
    
    
    int sample_num = 1;
    
    
    auto parse_spectrum_line = [&]( string &line ) -> shared_ptr<Measurement> {
      vector<string> fields;
      split_no_delim_compress( fields, line, "," );
      
      //cout << "Spectrum line had " << fields.size() << " fields." << endl;
      
      //if( fields.size() < max_index )
      //{
      //  cout << "Spectrum line didnt have enough fields..." << endl;
      //  return nullptr;
      //}
      
      auto get_field_str = [&]( const int index ) -> string {
        if( (index >= 0) && (static_cast<size_t>(index) < fields.size()) )
          return fields[index];
        return "";
      };
    
      
      bool failed_any_parse = false;
      double gamma_sum = 0.0;
      const size_t nchannel = energy_cal->num_channels();
      auto channel_counts = make_shared<vector<float>>( nchannel, 0.0f );
      const size_t end_bin = std::min( static_cast<size_t>(bin_last_index),
                                       std::min( bin_start_index + nchannel, fields.size() ) );
      for( size_t i = bin_start_index; i < fields.size(); ++i )
      {
        const size_t channel_num = i - bin_start_index;
        assert( channel_num < channel_counts->size() );
        float &channel = (*channel_counts)[channel_num];
        if( parse_float( fields[i].c_str(), fields[i].size(), channel ) )
        {
          gamma_sum += channel;
        }else
        {
          failed_any_parse = true;
        }
      }//for( size_t i = bin_start_index; i < fields.size(); ++i )
      
      if( gamma_sum <= 0 )
        return nullptr;
      
      shared_ptr<Measurement> m = make_shared<Measurement>();
      
      // Live time is in milli-seconds.
      const string livetime_str = get_field_str(live_time_index);
      if( parse_float( livetime_str.c_str(), livetime_str.size(), m->live_time_) )
      {
        m->live_time_ /= 1000.0f;
        m->real_time_ = m->live_time_;
      }else
      {
        m->live_time_ = 0.0f;
      }
      
      m->gamma_counts_ = channel_counts;
      m->gamma_count_sum_ = gamma_sum;
      
      m->energy_calibration_ = energy_cal;
      
      const string date_str = get_field_str( date_index ); //ex "5/12/2022"
      const string time_str = get_field_str( time_index ); //ex "10:37:43.316"
      const string date_time = date_str + " " + time_str;
      m->start_time_ = time_from_string_strptime( date_time, DateParseEndianType::MiddleEndianFirst );
      
      const string neut_counts_str = get_field_str(neutron_index);
      float neutron_counts = 0.0;
      if( parse_float( neut_counts_str.c_str(), neut_counts_str.size(), neutron_counts) )
      {
        m->contained_neutron_ = true;
        m->neutron_counts_sum_ = 0.0f;
        m->neutron_counts_.push_back( neutron_counts );
      }
      
      try
      {
        const string lat_str = get_field_str(latitude_index);
        const string lon_str = get_field_str(longitude_index);
        
        const double lat = std::stod( lat_str );
        const double lon = std::stod( lon_str );
        if( valid_latitude(lat) && valid_longitude(lon) && (lat != lon) )
        {
          m->latitude_ = lat;
          m->longitude_ = lon;
        }
      }catch(...)
      {
        m->parse_warnings_.push_back( "Could not interpret lat/lon" );
      }
      
      
      const string temp_str = get_field_str(sensor_temp_index);
      if( temp_str.size() > 1 )
        m->remarks_.push_back( "Sensor temperature: " + temp_str + " C" );
      
      const string battery_str = get_field_str(battery_index);
      if( battery_str.size() > 1 )
        m->remarks_.push_back( "Battery: " + battery_str + " %" );
      
      m->sample_number_ = sample_num++;
      
      /*
      get_field_str(does_unit_index);
      //get_field_str(output_start_index);
       // I think "Processing Time(ms)" is maybe just the CPU processing time; so irrelevant to us.
       //string process_time_str = get_field_str(process_time_ms_index);
      */
      
      string riid_str = get_field_str(riid_id_index);
      if( riid_str == "NODET" )
        riid_str = "";
      
      string riid_conf_str = get_field_str(riid_conf_index);
      string dose_str = get_field_str(dose_index);
      string dose_unit_str = get_field_str(does_unit_index);
      
      // Fix up an encoding issue..
      if( (dose_unit_str.size() == 5)
         && (static_cast<uint8_t>(dose_unit_str[0]) == 181)
         && (dose_unit_str[1] == 'S')
         && (dose_unit_str[2] == 'v')
         && (dose_unit_str[3] == '/')
         && (dose_unit_str[4] == 'h')
         )
      {
        dose_unit_str = "uSv/h";
      }
      
      if( riid_str.size() || dose_str.size() )
      {
        DetectorAnalysisResult result;
        result.nuclide_ = riid_str;
        result.id_confidence_ = riid_conf_str;
        result.remark_ = "For sample " + std::to_string( m->sample_number_ );
        
        string remark;
        if( !riid_str.empty() )
        {
          remark = "RIID result: " + riid_str;
          if( riid_conf_str.size() )
            remark += ", confidence: " + riid_conf_str;
        }//if( !riid_str.empty() )
        
        if( !dose_str.empty() )
        {
          if( remark.size() )
            remark += ", ";
          remark += "Dose: " + dose_str + " " + dose_unit_str;
        }//if( !dose_str.empty() )
        
        if( remark.size() )
          m->remarks_.push_back( remark );
        
        if( parse_float( dose_str.c_str(), dose_str.size(), result.dose_rate_) )
        {
          //convert to micro-sievert per hour ..
          const uint8_t first_char = dose_unit_str.empty() ? uint8_t(0)
                                                           : static_cast<uint8_t>(dose_unit_str[0]);
          
          if( (icontains(dose_unit_str, "sv") || icontains(dose_unit_str, "siev"))
              && (icontains(dose_unit_str, "micro") || (dose_unit_str.size() && first_char==181) || istarts_with(dose_unit_str, "usv/h")) )
          {
            //The 3DS dose_unit_str will look like [181,83,118,47,104] --> [?Sv/h]
            // We are
          }else
          {
           if( !result.remark_.empty() )
             result.remark_ += ", ";
            result.remark_ += "Dose unit not known";
            if( !dose_unit_str.empty() )
              result.remark_ += ": " + dose_unit_str;
          }
        }//if( we can parse dose rate )
        
        
        if( !detectors_analysis )
          detectors_analysis = make_shared<DetectorAnalysis>();
        detectors_analysis->results_.push_back( result );
      }//if( riid_str.size() || dose_str.size() )
      
      if( failed_any_parse )
        m->parse_warnings_.push_back( "Not all gamma channel data was successfully parsed." );
      
      return m;
    };//parse_spectrum_line
    
    
    auto background = parse_spectrum_line( background_line_str );
    if( background && (background->gamma_count_sum() >= 1) )
    {
      background->source_type_ = SourceType::Background;
      measurements_.push_back( background );
    }
    
    
    string line;
    while( SpecUtils::safe_get_line( input, line, max_line_len ) )
    {
      auto m = parse_spectrum_line( line );
      if( m && (m->gamma_count_sum() >= 1) )
      {
        m->source_type_ = SourceType::Foreground;
        measurements_.push_back( m );
      }
    }//while( more spectra to get ... )
    
    if( measurements_.empty() )
      throw runtime_error( "No measurements found" );
    
    parse_warnings_.push_back( "Real time was not provided in the file so has been set to the"
                               " live-time - dead time is unknown." );
    
    if( detectors_analysis )
      detectors_analysis_ = detectors_analysis;
    
    // The Kromek D3S is the only detector model I'm aware of that makes data of this format.
    manufacturer_ = "Kromek";
    instrument_model_ = "D3S";
    detector_type_ = DetectorType::KromekD3S;
    
    cleanup_after_load();
  }catch( std::exception &e )
  {
    input.clear();
    input.seekg( startpos, ios::end );
    reset();
    
    return false;
  }//try / catch
  
  return true;
}//bool load_from_D3S_raw( std::istream &input )

  
void Measurement::set_info_from_txt_or_csv( std::istream& istr )
{
  const istream::pos_type orig_pos = istr.tellg();
  
  errno = 0;
  
  try
  {
    set_info_from_avid_mobile_txt( istr );
    return;
  }catch(...)
  {
    reset();
  }
  
  //I feel as though this function can be improved in terms of being more robust
  //  to reading input, as well as shortened or re-factored
  //Also, I hacked to make this function very quickly, so I'm sure the code is
  //  even more so crap that the rest in this file
  //
  // \TODO: Try parsing every line as SpecUtils::split_to_floats(...), and only if that fails do
  //        all the other trimming, lower casing, splitting, and such - would probably be a lot
  //        faster, and the code would probably be a lot cleaner
  
  const int kChannel = 0, kEnergy = 1, kCounts = 2;//, kSecondRecord = 3;
  float energy_units = 1.0f;
  
  map<size_t,int> column_map;
  vector<string>::const_iterator pos;
  
  //If "poly_calib_coeff" is provided in file, we need to wait until we know number of channels
  //  before initializing energy calibration
  vector<float> poly_calib_coeff;
  
  string line;
  size_t nlines_used = 0, nlines_total = 0;
  const size_t maxlen = 1024*1024; //should be long enough for even the largest spectra
  
  while( SpecUtils::safe_get_line(istr, line, maxlen) )
  {
    if( line.size() > (maxlen-5) )
      throw runtime_error( "Found to long of line" );
    
    trim( line );
    to_lower_ascii( line );
    
    if( line.empty() )
      continue;
    
    ++nlines_total;
    
    vector<string> split_fields, fields;
    
    //Dont allow a space delimiter until we have the columns mapped out to avoid things like
    //  "Energy (keV)" counting as two columns
    const bool has_comma = (line.find(',') != string::npos);
    const bool no_split_space = (column_map.empty() && !isdigit(line[0]) && !SpecUtils::istarts_with(line, "Channel Energy Counts") );
    const char *delim = has_comma ? "," : (no_split_space ? "\t,;" : "\t, ;");
    
    SpecUtils::split( split_fields, line, delim );
     
    fields.reserve( split_fields.size() );
    for( string s : split_fields )
    {
      trim( s );
      if( !s.empty() )
        fields.push_back( s );
    }//for( string s : split_fields )
    
    const size_t nfields = fields.size();
    
    if( !nfields )
      continue;
    
    if( isdigit(fields[0][0]) )
    {
      //Check if we have a valid column map defined yet, either because it is empty, or it has one
      //  entry that is not counts.  This can happen if there was a header that was only partially
      //  recognized, for example "Energy (Mev), Det. 0", since we are currently looking at column
      //  header definitions seen in actual files like "counts", "data", "selection", "signal",
      //  and "detector" (\TODO: figuring out header meaning could probably be generalized more)
      
      const bool no_column_map_yet = (column_map.empty()
                        || ((column_map.size() == 1) && (column_map.begin()->second != kCounts)) );
      if( no_column_map_yet )
      {
        if( nfields==1 )
        {
          column_map[0] = kCounts;
        }if( nfields==2 && isdigit(fields[1][0]) )
        {
          column_map[0] = kEnergy;
          column_map[1] = kCounts;
        }else if( (nfields > 2) && (nfields < 9) && isdigit(fields[1][0]) && isdigit(fields[2][0]) )
        {
          if( fields[0].find('.') != string::npos )
          {
            // If first column has a decimal point in it, assume thats energy (so we don't try to
            //  parse first column as a integer and fail latter on), and then just assume the next
            //  column will be counts (following the only examples I've seen)
            column_map[0] = kEnergy;
            column_map[1] = kCounts;
          }else
          {
            column_map[0] = kChannel;
            column_map[1] = kEnergy;
            column_map[2] = kCounts;
          }
        }else
        {
          throw runtime_error( string("unrecognized line that started with digit '")
                              + fields[0][0] + string("'") );
        }
      }//if( no_column_map_yet )
      
      if( no_column_map_yet && (fields.size() == 4) )
      {
        // \TODO: Move this section of code to its own function
        vector<float> cals;
        if( SpecUtils::split_to_floats( line.c_str(), line.size(), cals ) )
        {
          //refY2EF53S0BD
          vector<float> eqn;
          if( cals.size() )
            eqn.insert( eqn.end(), cals.begin()+1, cals.end() );
          
          if( eqn.size()>=3 && fabs(eqn[0]) < 3000.0f && eqn[1] >= 0.0f )
          {
            //I think cals[0] might be real time
            //            cerr << "cals[0]=" << cals[0] << endl;
            const vector< pair<float,float> > devpairs;
            
            const istream::pos_type current_pos = istr.tellg();
            
            string channeldata;
            if( SpecUtils::safe_get_line(istr, channeldata, maxlen) )
            {
              ++nlines_total;
              const istream::pos_type post_pos = istr.tellg();
              istr.seekg( 0, ios::end );
              const istream::pos_type eof_pos = istr.tellg();
              istr.seekg( post_pos, ios::beg );
              if( post_pos == eof_pos )
                istr.setstate( ios::eofbit );
              
              auto channels = std::make_shared<vector<float>>();
              if( SpecUtils::split_to_floats( channeldata.c_str(), channeldata.size(), *channels ) )
              {
                if( post_pos == eof_pos )
                {
                  ++nlines_used;
                  const size_t nchan = channels->size();
                  if( nchan >= 128 )
                  {
                    try
                    {
                      auto newcal = make_shared<EnergyCalibration>();
                      newcal->set_polynomial( nchan, eqn, {} );
                      energy_calibration_ = newcal;
                      gamma_counts_ = channels;
                      break;  //This breaks out of the primary while loop of this function
                    }catch( std::exception & )
                    {
                      //I guess this wasnt a valid energy calibration, so assume not the spectrum we
                      //  want
                    }
                  }//if( some reasonalbe number of channels )
                }else if( channels->size() == 2 )
                {
                  // refV6GHP7WTWX
                  channels->clear();
                  
                  string str;
                  while( SpecUtils::safe_get_line(istr, str, maxlen) )
                  {
                    ++nlines_total;
                    trim(str);
                    if( str.empty() )
                      continue;
                    
                    vector<float> vals;
                    if( !SpecUtils::split_to_floats( str.c_str(), str.size(), vals ) )
                    {
                      channels.reset();
                      break;
                    }
                    if( vals.size() != 2 )
                    {
                      channels.reset();
                      break;
                    }
                    
                    ++nlines_used;
                    channels->push_back( vals[1] );
                  }//
                  
                  if( !!channels )
                  {
                    const bool validCalib
                    = SpecUtils::calibration_is_valid( SpecUtils::EnergyCalType::Polynomial, eqn, devpairs,
                                                      channels->size() );
                    
                    if( validCalib && channels->size() >= 64 )
                    {
                      ++nlines_used;
                      
                      live_time_ = cals[0];
                      
                      try
                      {
                        auto newcal = make_shared<EnergyCalibration>();
                        newcal->set_polynomial( channels->size(), eqn, devpairs );
                        energy_calibration_ = newcal;
                        gamma_counts_ = channels;
                        break; //This breaks out of the primary while loop of this function
                      }catch( std::exception & )
                      {
                        //I guess energy calibration really wasnt valid - probably shouldnt get here
                      }
                    }//if( some reasonalbe number of channels )
                  }//if( !!channels )
                }//if( there were exactly two lines ) / else
              }//if( could split the second line into floats )
              
            }//if( we could get a second line )
            
            //If we didn't "break" out of the primary while loop above, we weren't successful at
            //  reading the file, so go back and try the other CSV methods
            istr.seekg( current_pos, ios::beg );
          }//if( potentially calibration data )
        }//if( the line was made of 4 numbers )
      }//if( fields.size() == 4 )
      
      auto channels = make_shared<vector<int>>();
      auto counts = make_shared<vector<float>>();
      auto energies = make_shared<vector<float>>();
      
      //After we hit a line that no longer starts with numbers, we actually want
      // to leave istr at the beggining of that line so if another spectrum
      // comes aftwards, we wont lose its first line of information
      istream::pos_type position = istr.tellg();
      
      do
      {
        ++nlines_total;
        
        if( line.size() > (maxlen-5) )
          throw runtime_error( "Found to long of line" );
        
        vector<string> split_fields, fields;
        trim( line );
        split( split_fields, line, "\t, ;" );
        
        fields.reserve( split_fields.size() );
        for( const string &s : split_fields )
          if( !s.empty() )
            fields.push_back( s );
        
        
        if( fields.empty() )
          continue;
        
        if( !isdigit( fields.at(0).at(0) ) )
        {
          istr.seekg( position, ios::beg );
          break;
        }
        
        int channel = 0;
        float energy = 0.0f, count = 0.0f; //, count2 = 0.0f;
        for( size_t col = 0; col < fields.size(); ++col )
        {
          if( column_map.count(col) )
          {
            switch( column_map[col] )
            {
              case kChannel:       channel = atoi(fields[col].c_str()); break;
              case kEnergy:        energy  = static_cast<float>(atof(fields[col].c_str())); break;
              case kCounts:        count   = static_cast<float>(atof(fields[col].c_str())); break;
              //case kCounts + 1:    count2  = static_cast<float>(atof(fields[col].c_str())); break;
              default:
                // \TODO: Ignoring past the first record...
                assert( column_map[col] > kCounts );
                break;
            }//switch( column_map[col] )
          }//if( column_map.count(col) )
        }//for( size_t col = 0; col < fields.size(); ++col )
        
        if( IsNan(energy) || IsInf(energy) )
          continue;
        if( IsNan(count) || IsInf(count) /*|| IsNan(count2) || IsInf(count2)*/ )
          continue;
        
        //        if( errno )
        //          throw runtime_error( "Error converting to float" );
        
        energy *= energy_units;
        
        if( (energies->size() && (energies->back() > energy) )
           || ( channels->size() && (channels->back() > channel) ) )
        {
          throw runtime_error( "Found decreasing energy" );
        }//if( energies->size() && (energies->back() > energy) )
        
        ++nlines_used;
        energies->push_back( energy );
        counts->push_back( count );
        channels->push_back( channel );
        position = istr.tellg();
      }while( SpecUtils::safe_get_line( istr, line, maxlen ) );
      
      if( counts->empty() )
        throw runtime_error( "Didnt find and channel counts" );
      
      gamma_counts_ = counts;
      
      if( (energies->size() >= counts->size()) && (energies->back()!=0.0f) )
      {
        try
        {
          auto newcal = make_shared<EnergyCalibration>();
          newcal->set_lower_channel_energy( counts->size(), std::move(*energies) );
          energy_calibration_ = newcal;
        }catch( std::exception &e )
        {
          parse_warnings_.push_back( "Lower channel energies provided were invalid: "
                                     + string(e.what()) );
        }
      }//if( we have channel lower energies )
      
      break;
    }else if( column_map.empty()
             && (  istarts_with( fields[0], "channel" )
                 || istarts_with( fields[0], "counts" )
                 || istarts_with( fields[0], "data" )
                 || istarts_with( fields[0], "energy" )
                 || istarts_with( fields[0], "Ch" )
                 || fields[0]=="##" ) )
    {
      ++nlines_used;
      
      for( size_t i = 0; i < nfields; ++i )
      {
        if( starts_with( fields[i], "channel" )
           || starts_with( fields[i], "ch" )
           || fields[i]=="##" )
        {
          column_map[i] = kChannel;
        }else if( starts_with( fields[i], "energy" )
                 || starts_with( fields[i], "en" ) )
        {
          column_map[i] = kEnergy;
          if( SpecUtils::contains( fields[i], "mev" ) )
            energy_units = 1000.0f;
          
          const auto kevpos = fields[i].find( "(kev)" );
          if( kevpos != string::npos && ((fields[i].size() - kevpos) > 3) )
          {
            //Theramino produces a header like "Energy(KeV)    Counts    "
            //  but the rest of the lines are CSV
            //  Hopefully accounting for this doesn't erroneously affect other formats
            string restofline = fields[i].substr(kevpos+5);
            SpecUtils::trim(restofline);
            if( istarts_with(restofline, "count")
               || istarts_with(restofline, "data")
               || istarts_with(restofline, "signal")
               || istarts_with(restofline, "detector") )
            {
              column_map[i+1] = kCounts;
            }
          }//if( text after "(kev)" )
          
        }else if( starts_with( fields[i], "counts" )
                 || starts_with( fields[i], "data" )
                 || starts_with( fields[i], "selection" )
                 || starts_with( fields[i], "signal" )
                 || starts_with( fields[i], "detector" )
                 )
        {
          // \TODO: currently, only the first "counts" column is used for channel data, but we could
          //        have multiple columns for multi-detector systems; however, I haven't actually
          //        seen any spectrum files where the additional columns are non-zero, so not
          //        implementing for now
          int numRecords = 0;
          for( const auto &entry : column_map )
            numRecords += (entry.second >= kCounts);
          column_map[i] = kCounts + numRecords;
        }
      }//for( size_t i = 0; i < nfields; ++i )
      
    }else if( starts_with( fields[0], "remark" ) )
    {
      ++nlines_used;
      bool used = false;
      pos = std::find( fields.begin(), fields.end(), "starttime" );
      if( (pos != fields.end()) && ((pos+1) != fields.end()) )
      {
        used = true;
        start_time_ = time_from_string( (pos+1)->c_str() );
      }
      pos = std::find( fields.begin(), fields.end(), "livetime" );
      if( (pos != fields.end()) && ((pos+1) != fields.end()) )
      {
        used = true;
        live_time_ = time_duration_string_to_seconds( *(pos+1) );
      }
      pos = std::find( fields.begin(), fields.end(), "realtime" );
      if( (pos != fields.end()) && ((pos+1) != fields.end()) )
      {
        used = true;
        real_time_ = time_duration_string_to_seconds( *(pos+1) );
      }
      
      try
      {
        if( sample_number_ < 0 )
        {
          sample_number_ = SpecUtils::sample_num_from_remark( line );
          used |= (sample_number_ > -1);
        }
      }catch(...){}
      
      try
      {
        if( speed_ == 0.0 )
        {
          speed_ = SpecUtils::speed_from_remark( line );
          used |= (speed_ != 0.0);
        }
      }catch(...){}
      
      try
      {
        if( detector_name_.empty() )
        {
          detector_name_ = detector_name_from_remark( line );
          used |= !detector_name_.empty();
        }
      }catch(...){}
      
      if( !used )
      {
        string::size_type pos = line.find_first_of( " :\t" );
        if( pos < line.size() )
        {
          string remark = line.substr( pos + 1 );
          pos = remark.find_first_not_of( " :\t" );
          remarks_.push_back( remark.substr( pos ) );
        }
      }
    }else if( istarts_with( fields[0], "starttime" )
             || istarts_with( fields[0], "Measurement start" )
             || istarts_with( fields[0], "Started at" ) )
    {
      ++nlines_used;
      
      string timestr;
      if( nfields > 1 )
        timestr = fields[1];
      if( nfields > 2 )
        timestr += (" " + fields[2]);
      
      if( timestr.size() < 2 )
      {
        //Theramino has lines like: "Started at: 2020/02/12 14:57:39"
        const auto semicolonpos = fields[0].find(":");
        if( semicolonpos != string::npos && (fields[0].size()-semicolonpos) > 2 )
        {
          timestr = fields[0].substr(semicolonpos+1);
          SpecUtils::trim(timestr);
        }
      }//if( timestr.empty() )
      
      start_time_ = time_from_string( timestr.c_str() );
    }else if( starts_with( fields[0], "livetime" ) )
    {
      ++nlines_used;
      
      if( nfields > 1 )
        live_time_ = time_duration_string_to_seconds( fields[1] );
    }else if( istarts_with( fields[0], "realtime" )
             || istarts_with( fields[0], "Real time" )
             || istarts_with( fields[0], "Total time") )
    {
      ++nlines_used;
      
      if( nfields > 1 )
      {
        real_time_ = time_duration_string_to_seconds( fields[1] );
      }else
      {
        const auto semipos = fields[0].find(":");
        if( semipos != string::npos && (semipos+2) < fields[0].size() )
        {
          string restofline = fields[0].substr( semipos+1 );
          SpecUtils::trim(restofline);
          real_time_ = time_duration_string_to_seconds( restofline );
        }
      }
    }else if( starts_with( fields[0], "neutroncount" ) )
    {
      ++nlines_used;
      
      if( nfields > 1 )
      {
        if( !(stringstream(fields[1]) >> neutron_counts_sum_) )
          throw runtime_error( "Invalid neutroncount: " + fields[1] );
        contained_neutron_ = true;
      }
    }else if( starts_with( fields[0], "samplenumber" ) )
    {
      ++nlines_used;
      
      if( nfields > 1 )
      {
        if( !(stringstream(fields[1]) >> sample_number_) )
          throw runtime_error( "Invalid samplenumber: " + fields[1] );
      }
    }else if( starts_with( fields[0], "detectorname" ) )
    {
      ++nlines_used;
      
      if( nfields > 1 )
        detector_name_ = fields[1];
    }else if( starts_with( fields[0], "detectortype" ) )
    {
      ++nlines_used;
      
      if( nfields > 1 )
        detector_description_ = fields[1];
    }else if( starts_with( fields[0], "title" ) )
    {
      ++nlines_used;
      
      string::size_type pos = line.find_first_of( " :\t" );
      if( pos < line.size() )
      {
        title_ = line.substr( pos + 1 );
        pos = title_.find_first_not_of( " :\t" );
        title_ = title_.substr( pos );
      }
    }else if( starts_with( fields[0], "calibcoeff" ) )
    {
      //CalibCoeff   : a=0.000000000E+000 b=0.000000000E+000 c=3.000000000E+000 d=0.000000000E+000
      // \TODO: better figure out meaning of these pars; right now logic based on IAEA SPC files.
      ++nlines_used;
      
      float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
      const size_t apos = line.find( "a=" );
      const size_t bpos = line.find( "b=" );
      const size_t cpos = line.find( "c=" );
      const size_t dpos = line.find( "d=" );
      if( apos < (line.size()-2) )
        a = static_cast<float>( atof( line.c_str() + apos + 2 ) );
      if( bpos < (line.size()-2) )
        b = static_cast<float>( atof( line.c_str() + bpos + 2 ) );
      if( cpos < (line.size()-2) )
        c = static_cast<float>( atof( line.c_str() + cpos + 2 ) );
      if( dpos < (line.size()-2) )
        d = static_cast<float>( atof( line.c_str() + dpos + 2 ) );
      
      if( c > 0 || b > 0 )
        poly_calib_coeff = { d, c, b, a };
    }
    
  }//while( getline( istr, line ) )
  
  if( nlines_total < 10 || nlines_used < static_cast<size_t>( ceil(0.25*nlines_total) ))
  {
    reset();
    istr.seekg( orig_pos, ios::beg );
    istr.clear( ios::failbit );
    throw runtime_error( "Not enough (useful) lines in the file." );
  }//
  
  const size_t nchannel = gamma_counts_ ? gamma_counts_->size() : size_t(0);
  if( nchannel>=2 && !poly_calib_coeff.empty()
      && (energy_calibration_->type() == EnergyCalType::InvalidEquationType) )
  {
    try
    {
      auto newcal = make_shared<EnergyCalibration>();
      newcal->set_polynomial( nchannel, poly_calib_coeff, {} );
      energy_calibration_ = newcal;
    }catch( std::exception &e )
    {
      parse_warnings_.push_back( "Provided energy calibration coefficients apear to be invalid: "
                                 + string(e.what()) );
    }
  }
  
  if( nchannel>=2 && (nchannel < 65540) && column_map.size()
           && (energy_calibration_->type() == EnergyCalType::InvalidEquationType) )
  {
    // We will check if we have a channel column, and NOT an energy column, otherwise if we would
    //  have already set energy calibration
    bool have_channel_col = false, have_energy_col = false;
    for( const auto &vp : column_map )
    {
      have_channel_col |= (vp.second == kCounts);
      have_energy_col |= (vp.second == kEnergy);
    }
    
    if( have_channel_col && !have_energy_col )
    {
      //We have at least two channels, and 64k or less (both 2 and 64k arbitrarily chosen, just to be
      //  reasonable)
      auto newcal = make_shared<EnergyCalibration>();
      newcal->set_default_polynomial( nchannel, {0.0f,3000.0f/nchannel}, {} );
      energy_calibration_ = newcal;
    }
  }//if( poly calibration ) / else (all we had was raw channel numbers)
  
  if( nchannel < 5 || (energy_calibration_->type() == EnergyCalType::InvalidEquationType) )
  {
    reset();
    istr.seekg( orig_pos, ios::beg );
    istr.clear( ios::failbit );
    stringstream msg;
    msg << "Measurement::set_info_from_txt_or_csv(...)\n\tI was unable to load the spectrum, probably"
    << " due to missing data or an invalid line somewhere";
    throw runtime_error( msg.str() );
  }//if( !gamma_counts_ || gamma_counts_->empty() )
  
  if( contained_neutron_ )
  {
    neutron_counts_.resize( 1 );
    neutron_counts_[0] = static_cast<float>( neutron_counts_sum_ );
  }//if( contained_neutron_ )
  
  for( const float f : *gamma_counts_ )
    gamma_count_sum_ += f;
  
  //if( (gamma_count_sum_ < FLT_EPSILON) && !contained_neutron() )
  //{
  //  reset();
  //  istr.seekg( orig_pos, ios::beg );
  //  istr.clear( ios::failbit );
  //  throw runtime_error( "Measurement::set_info_from_txt_or_csv(...)\n\tFailed to find gamma or neutron counts" );
  //}
  
  //Some CSV files only contain live or real time, so just set them equal
  if( real_time_ > FLT_EPSILON && fabs(live_time_) < FLT_EPSILON )
  {
    live_time_ = real_time_;
    parse_warnings_.emplace_back( "Measurement did not contain Live Time, so setting this to Real Time" );
  }else if( live_time_ > FLT_EPSILON && fabs(real_time_) < FLT_EPSILON )
  {
    real_time_ = live_time_;
    parse_warnings_.emplace_back( "Measurement did not contain Real Time, so setting this to Live Time" );
  }
}//void set_info_from_txt_or_csv( std::istream& istr )

  
  
void Measurement::set_info_from_avid_mobile_txt( std::istream &istr )
{
  //There is a variant of refQQZGMTCC93, RSL mobile system ref8T2SZ11TQE
  
  using SpecUtils::safe_get_line;
  using SpecUtils::split_to_floats;
  
  const istream::pos_type orig_pos = istr.tellg();
  
  try
  {
    string line;
    if( !SpecUtils::safe_get_line(istr, line) )
      throw runtime_error(""); //"Failed getting first line"
    
    if( line.size() < 8 || line.size() > 100 )
      throw runtime_error(""); //"First line not reasonable length"
    
    //const size_t first_invalid_char = line.substr(0,8).find_first_not_of( "0123456789 ,\r\n\t+-e." );
    const size_t first_invalid_char = line.find_first_not_of( "0123456789 ,\r\n\t+-e." );
    
    if( first_invalid_char != string::npos )
      throw runtime_error( "" ); //"Invalid character in first 8 characters"
    
    vector<string> flinefields;
    SpecUtils::split( flinefields, line, " ,\t");
    if( flinefields.size() != 4 )
      throw runtime_error( "" ); //"First line not real time then calibration coefs"
    
    vector<float> fline;
    if( !split_to_floats(line, fline) || fline.size()!=4 )
      throw runtime_error( "" ); //We expect the first line to be all numbers
    
    const vector<float> eqn( fline.begin() + 1, fline.end() );
    const float realtime = fline[0];
    
    if( realtime < -FLT_EPSILON )
      throw runtime_error( "" ); //"First coefficient not real time"
    
    if( !safe_get_line(istr, line) )
      throw runtime_error(""); //"Failed getting second line"
    
    if( !split_to_floats(line, fline) )
      throw runtime_error( "" ); //"Second line not floats"
    
    if( fline.size() < 127 && fline.size() != 2 )
      throw runtime_error( "" ); //"Invalid second line"
    
    //If we got here, this is probably a valid file
    auto counts = std::make_shared< vector<float> >();
    
    if( fline.size() >= 127 )
    {
      //Second line is CSV of channel counts
      if( SpecUtils::safe_get_line(istr, line) && line.size() )
        throw runtime_error(""); //"Only expected two lines"
      
      counts->swap( fline );
    }else
    {
      //Rest of file is \t seperated column with two columns per line
      //  "channel\tcounts"
      float channelnum = fline[0];
      const float counts0 = fline[1];
      
      if( fabs(channelnum) > FLT_EPSILON && fabs(channelnum - 1.0) > FLT_EPSILON )
        throw runtime_error( "" ); //"First column doesnt refer to channel number"
      
      if( counts0 < -FLT_EPSILON )
        throw runtime_error( "" ); //"Second column doesnt refer to channel counts"
      
      channelnum = channelnum - 1.0f;
      istr.seekg( orig_pos, ios::beg );
      SpecUtils::safe_get_line( istr, line );
      
      while( safe_get_line( istr, line ) )
      {
        trim( line );
        if( line.empty() ) //Sometimes file will have a newline at the end of the file
          continue;
        
        if( !split_to_floats(line, fline) || fline.size() != 2 )
          throw runtime_error( "" ); //"Unexpected number of fields on a line"
        
        if( fabs(channelnum + 1.0f - fline[0]) > 0.9f /*FLT_EPSILON*/ )
          throw runtime_error( "" ); //"First column is not channel number"
        
        channelnum = fline[0];
        counts->push_back( fline[1] );
      }//while( SpecUtils::safe_get_line( istr, line ) )
    }//if( fline.size() >= 127 )
    
    const size_t nchannel = counts->size();
    if( nchannel < 127 )
      throw runtime_error(""); //"Not enought channels"
    
    auto newcal = make_shared<EnergyCalibration>();
    //Next line will throw exception if invalid calibration; we are requiring valid energy cal
    newcal->set_polynomial( nchannel, eqn, {} );
    energy_calibration_ = newcal;
    
    //    real_time_ = realtime;
    live_time_ = realtime;
    contained_neutron_ = false;
    neutron_counts_.clear();
    gamma_counts_ = counts;
    neutron_counts_sum_ = gamma_count_sum_ = 0.0;
    for( const float f : *counts )
      gamma_count_sum_ += f;
  }catch( std::exception & )
  {
    istr.seekg( orig_pos, ios::beg );
    throw;
  }
}//void set_info_from_avid_mobile_txt( std::istream& istr )

  
bool SpecFile::load_from_srpm210_csv( std::istream &input )
{
  try
  {
    string line;
    if( !SpecUtils::safe_get_line(input, line) )
      return false;
    
    if( line.find("Fields, RSP 1, RSP 2") == string::npos )
      return false;
    
    vector<string> header;
    SpecUtils::split( header, line, "," );
    if( header.size() < 3 )
      return false; //we know this cant happen, but whatever
    header.erase( begin(header) );  //get rid of "Fields"
    
#if(PERFORM_DEVELOPER_CHECKS)
    set<string> header_names_check;
#endif
    for( auto &field : header )
    {
      SpecUtils::trim( field );
      if( field.size() < 2 )
      {
#if(PERFORM_DEVELOPER_CHECKS)
        header_names_check.insert( field );
#endif
        continue; //JIC, shouldnt ever hit though
      }
      
      //Transform "RSP 1" to "RSP 01", so this way when names get sorted
      //  "RSP 11" wont come before "RSP 2"
      if( isdigit( field[field.size()-1] ) && !isdigit( field[field.size()-2] ) )
        field = field.substr(0,field.size()-1) + '0' + field.substr(field.size()-1);
      
#if(PERFORM_DEVELOPER_CHECKS)
      header_names_check.insert( field );
#endif
    }//for( auto &field : header )
    
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
    if( header_names_check.size() != header_names_check.size() )
      log_developer_error( __func__, ("There was a duplicate detector name in SRPM CSV file: '" + line + "' - who knows what will happen").c_str() );
#endif
    
    
    vector<float> real_times, live_times;
    vector<vector<float>> gamma_counts, neutron_counts;
    
    while( SpecUtils::safe_get_line(input, line) )
    {
      SpecUtils::trim( line );
      if( line.empty() )
        continue;
      
      auto commapos = line.find(',');
      if( commapos == string::npos )
        continue;  //shouldnt happen
      
      const string key = line.substr( 0, commapos );
      line = line.substr(commapos+1);
      
      //All columns, other than the first are integral, however for conveince
      //  we will just read them into floats.  The time (in microseconds) would
      //  maybe be the only thing that would lose info, but it will be way to
      //  minor to worry about.
      
      //Meh, I dont think we care about any of the following lines
      const string lines_to_skip[] = { "PLS_CNTR", "GOOD_CNTR", "PU_CNTR",
        "COSM_CNTR", "PMT_COUNTS_1", "PMT_COUNTS_2", "PMT_COUNTS_3",
        "PMT_COUNTS_4", "XRAY_CNTR"
      };
      
      if( std::find(begin(lines_to_skip), end(lines_to_skip), key) != end(lines_to_skip) )
        continue;
      
      vector<float> line_data;
      if( !SpecUtils::split_to_floats(line, line_data) )
      {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        log_developer_error( __func__, ("Failed in parsing line of SRPM file: '" + line + "'").c_str() );
#endif
        continue;
      }
      
      if( line_data.empty() )
        continue;
      
      if( key == "ACC_TIME_us" )
      {
        real_times.swap( line_data );
      }else if( key == "ACC_TIME_LIVE_us" )
      {
        live_times.swap( line_data );
      }else if( SpecUtils::istarts_with( key, "Spectrum_") )
      {
        if( gamma_counts.size() < line_data.size() )
          gamma_counts.resize( line_data.size() );
        for( size_t i = 0; i < line_data.size(); ++i )
          gamma_counts[i].push_back(line_data[i]);
      }else if( SpecUtils::istarts_with( key, "Ntr_") )
      {
        if( SpecUtils::icontains(key, "Total") )
        {
          if( neutron_counts.size() < line_data.size() )
            neutron_counts.resize( line_data.size() );
          for( size_t i = 0; i < line_data.size(); ++i )
            neutron_counts[i].push_back(line_data[i]);
        }else if( SpecUtils::icontains(key, "Low")
                 || SpecUtils::icontains(key, "High")
                 || SpecUtils::icontains(key, "_Neutron") )
        {
          //Meh, ignore this I guess
        }else
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          log_developer_error( __func__, ("Unrecognized neutron type in SRPM file: '" + key + "'").c_str() );
#endif
        }
      }else
      {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        log_developer_error( __func__, ("Unrecognized line type in SRPM file: '" + key + "'").c_str() );
#endif
      }//if( key is specific value ) / else
    }//while( SpecUtils::safe_get_line(input, line) )
    
    if( gamma_counts.empty() )
      return false;
    
    reset();
    
    
    for( size_t i = 0; i < gamma_counts.size(); ++i )
    {
      vector<float> &gammacount = gamma_counts[i];
      if( gammacount.size() < 7 ) //7 is arbitrary
        continue;
      
      float livetime = 0.0f, realtime = 0.0f;
      if( i < live_times.size() )
        livetime = 1.0E-6f * live_times[i];
      if( i < real_times.size() )
        realtime = 1.0E-6f * real_times[i];
      
      //JIC something is whack getting time, hack it! (shouldnt happen that I'm aware of)
      if( livetime==0.0f && realtime!=0.0f )
        realtime = livetime;
      if( realtime==0.0f && livetime!=0.0f )
        livetime = realtime;
      
      auto m = std::make_shared<Measurement>();
      
      if( i < header.size() )
        m->detector_name_ = header[i];
      else
        m->detector_name_ = "Det" + to_string(i);
      m->detector_number_ = static_cast<int>( i );
      m->real_time_ = realtime;
      m->live_time_ = livetime;
      m->detector_description_ = "PVT";
      m->gamma_counts_ = std::make_shared<vector<float>>( gammacount );
      if( i < neutron_counts.size() )
        m->neutron_counts_ = neutron_counts[i];
      for( const float counts : *m->gamma_counts_ )
        m->gamma_count_sum_ += counts;
      for( const float counts : m->neutron_counts_ )
        m->neutron_counts_sum_ += counts;
      m->contained_neutron_ = !m->neutron_counts_.empty();
      m->sample_number_ = 1;
      
      //Further quantities it would be nice to fill out:
      /*
       OccupancyStatus  occupied_;
       float speed_;  //in m/s
       QualityStatus quality_status_;
       SourceType     source_type_;
       shared_ptr<const EnergyCalibration> energy_calibration_
       std::vector<std::string>  remarks_;
       boost::posix_time::ptime  start_time_;
       std::vector<float>        neutron_counts_;
       double latitude_;  //set to -999.9 if not specified
       double longitude_; //set to -999.9 if not specified
       boost::posix_time::ptime position_time_;
       std::string title_;  //Actually used for a number of file formats
       */
      measurements_.push_back( m );
      
    }//for( size_t i = 0; i < gamma_counts.size(); ++i )
    
    detector_type_ = DetectorType::Srpm210;  //This is deduced from the file
    instrument_type_ = "Spectroscopic Portal Monitor";
    manufacturer_ = "Leidos";
    instrument_model_ = "SRPM-210";
    
    //Further information it would be nice to fill out:
    //instrument_id_ = "";
    //remarks_.push_back( "..." );
    //lane_number_ = ;
    //measurement_location_name_ = "";
    //inspection_ = "";
    //measurement_operator_ = "";
    
    cleanup_after_load();
  }catch( std::exception & )
  {
    reset();
    return false;
  }
  
  return true;
}//bool load_from_srpm210_csv( std::istream &input );
  

  
bool Measurement::write_txt( std::ostream& ostr ) const
{
  const char *endline = "\r\n";
  char buffer[128];
  
  ostr << endline << endline;
  
  for( size_t i = 0; i < remarks_.size(); ++i )
  {
    string remark = remarks_[i];
    if( i == 0 )
    {
      if( (remark.find( "Survey" ) == string::npos) && sample_number_>=0 )
      {
        snprintf( buffer, sizeof(buffer), " Survey %i ", sample_number_ );
        remark += buffer;
      }
      
      const string found_name = detector_name_from_remark( remark );
      if( found_name.empty() && !detector_name_.empty() )
        remark += " " + detector_name_ + " ";
      
      if( (remark.find( "Speed" ) == string::npos) && (speed_>0.000000001) )
      {
        snprintf( buffer, sizeof(buffer), " Speed %f m/s", speed_ );
        remark += buffer;
      }
    }//if( i == 0 )
    
    ostr << "Remark: " << remark << endline;
  }//for( size_t i = 0; i < remarks_.size(); ++i )
  
  if( !start_time_.is_special() )
    ostr << "StartTime " << start_time_ << "" << endline;
  ostr << "LiveTime " << live_time_ << " seconds" << endline;
  ostr << "RealTime " << real_time_ << " seconds" << endline;
  ostr << "SampleNumber " << sample_number_ << endline;
  ostr << "DetectorName " << detector_name_ << endline;
  ostr << "DetectorType " << detector_description_ << endline;
  
  if( has_gps_info() )
  {
    ostr << "Latitude: " << latitude_ << endline;
    ostr << "Longitude: " << longitude_ << endline;
    if( !position_time_.is_special() )
      ostr << "Position Time: "
      << SpecUtils::to_iso_string(position_time_) << endline;
  }
  
  ostr << "EquationType ";
  switch( energy_calibration_->type() )
  {
    case SpecUtils::EnergyCalType::Polynomial:
    case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
      ostr << "Polynomial";
      break;
    case SpecUtils::EnergyCalType::FullRangeFraction:   ostr << "FullRangeFraction"; break;
    case SpecUtils::EnergyCalType::LowerChannelEdge:    ostr << "LowerChannelEdge"; break;
    case SpecUtils::EnergyCalType::InvalidEquationType: ostr << "Unknown"; break;
  }//switch( energy_calibration_->type() )
  
  
  ostr << endline << "Coefficients ";
  const vector<float> &cal_coeffs = energy_calibration_->coefficients();
  for( size_t i = 0; i < cal_coeffs.size(); ++i )
    ostr << (i ? " " : "") << cal_coeffs[i];
  ostr << endline;
  
  if( contained_neutron_ )
    ostr << "NeutronCount " << neutron_counts_sum_ << endline;
  
  //  ostr "Channel" << " "
  //       << setw(12) << ios::left << "Energy" << " "
  //       << setw(12) << ios::left << "Counts" << endline;
  
  assert( energy_calibration_ );
  const size_t nChannel = gamma_counts_ ? gamma_counts_->size() : size_t(0);
  const auto channel_energies = energy_calibration_->channel_energies();
  const bool has_energies = channel_energies && (channel_energies->size() >= nChannel);
  
  ostr << "Channel" << " " << (has_energies ? "Energy" : "Channel") << " " << "Counts" << endline;
  
  for( size_t i = 0; i < nChannel; ++i )
  {
    //    ostr << setw(12) << ios::right << i
    //         << setw(12) << ios::right << channel_energies_->at(i)
    //         << setw(12) << ios::right << gamma_counts_->operator[](i)
    //         << endline;
    ostr << i << " " << (has_energies ? (*channel_energies)[i] : static_cast<float>(i))
         << " " << gamma_counts_->operator[](i)
    << endline;
  }//for( size_t i = 0; i < compressed_counts.size(); ++i )
  
  ostr << endline;
  
  return !ostr.bad();
}//bool write_txt( std::ostream& ostr ) const
  
  
bool SpecFile::write_txt( std::ostream& ostr ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const char *endline = "\r\n";
  ostr << "Original File Name: " << filename_ << endline;
  ostr << "TotalGammaLiveTime: " << gamma_live_time_ << " seconds" << endline;
  ostr << "TotalRealTime: " << gamma_real_time_ << " seconds" << endline;
  ostr << "TotalGammaCounts: " << gamma_count_sum_ << " seconds" << endline;
  ostr << "TotalNeutron: " << neutron_counts_sum_ << " seconds" << endline;
  if( instrument_id_.size() )
    ostr << "Serial number " << instrument_id_ << endline;
  
  for( const string &remark : remarks_ )
    ostr << "Remark: " << remark << endline;
  
  for( const std::shared_ptr<const Measurement> m : measurements_ )
    m->write_txt( ostr );
  
  return !ostr.bad();
}//bool write_txt( std::ostream& ostr ) const

  
bool Measurement::write_csv( std::ostream& ostr ) const
{
  const char *endline = "\r\n";
  
  assert( energy_calibration_ );
  const size_t nchannel = gamma_counts_ ? gamma_counts_->size() : size_t(0);
  const auto channel_energies = energy_calibration_->channel_energies();
  const bool has_energies = channel_energies && (channel_energies->size() >= nchannel);
  
  if( has_energies )
  {
    ostr << "Energy, Data" << endline;
    for( size_t i = 0; i < nchannel; ++i )
      ostr << channel_energies->at(i) << "," << gamma_counts_->operator[](i) << endline;
  }else
  {
    ostr << "Channel, Data" << endline;
    for( size_t i = 0; i < nchannel; ++i )
      ostr << i << "," << gamma_counts_->operator[](i) << endline;
  }
  
  ostr << endline;
  
  return !ostr.bad();
}//bool Measurement::write_csv( std::ostream& ostr ) const
  
  
bool SpecFile::write_csv( std::ostream& ostr ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
  for( const std::shared_ptr<const Measurement> meas : measurements_ )
    meas->write_csv( ostr );
    
  return !ostr.bad();
}//bool write_csv( std::ostream& ostr ) const

}//namespace SpecUtils


