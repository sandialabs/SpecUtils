/* SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 
 Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC
 (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
 Government retains certain rights in this software.
 For questions contact William Johnson via email at wcjohns@sandia.gov, or
 alternative emails of interspec@sandia.gov.
 
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

#include <mutex>
#include <ctime>
#include <vector>
#include <string>
#include <memory>
#include <fstream>
#include <assert.h>
#include <iostream>
#include <algorithm>
#include <functional>

#include "SpecUtils/UtilityFunctions.h"
#include "SpecUtils/SerialToDetectorModel.h"

using namespace std;


namespace SerialToDetectorModel
{
  static std::mutex sm_data_mutex;  //protects sm_data_filename and sm_data
  static std::string sm_data_filename = "data/OUO_detective_serial_to_type.csv";

  /** For initial implementation will use a shared_ptr and protect accessing it
      with a mutex.  This is a bit heavy handed, but wont even hit the profiler
      relative to the other parts of parsing a file, so whatever.
   */
#if( PERFORM_DEVELOPER_CHECKS )
  static std::shared_ptr<std::vector<DetectorModelInfo>> sm_data( nullptr );
#else
  static std::shared_ptr<const std::vector<DetectorModelInfo>> sm_data( nullptr );
#endif
  
  
  const std::string &to_str( const DetectorModel model )
  {
    static const std::string invalid_str( "InvalidDetectorModel" );
    static const std::string NotInitialized_str( "NotInitialized" );
    static const std::string UnknownSerialNumber_str( "UnknownSerialNumber" );
    static const std::string Unknown_str( "Unknown" );
    static const std::string DetectiveEx_str( "DetectiveEx" );
    static const std::string MicroDetective_str( "MicroDetective" );
    static const std::string DetectiveEx100_str( "DetectiveEx100" );
    static const std::string Detective200_str( "Detective200" );
    
    switch( model )
    {
      case DetectorModel::NotInitialized: return NotInitialized_str;
      case DetectorModel::UnknownSerialNumber: return UnknownSerialNumber_str;
      case DetectorModel::Unknown: return Unknown_str;
      case DetectorModel::DetectiveEx: return DetectiveEx_str;
      case DetectorModel::MicroDetective: return MicroDetective_str;
      case DetectorModel::DetectiveEx100: return DetectiveEx100_str;
      case DetectorModel::Detective200: return Detective200_str;
    }//switch( model )
    
    return invalid_str;
  }//const std::string &to_str( const DetectorModel model )
  
  
  
  void set_detector_model_input_csv( const std::string &filename )
  {
    std::lock_guard<std::mutex> lock( sm_data_mutex );
    if( filename == sm_data_filename )
      return;
    
    if( sm_data )
      sm_data.reset();
    
    sm_data_filename = filename;
  }//void set_detector_model_input_csv( const std::string &filename )
  
  
  /** Parses detector serial to model number CSV file, returning results sorted
      by serial number
   */
  std::shared_ptr<std::vector<DetectorModelInfo>> parse_detective_model_csv( const std::string &filename )
  {
    auto newdata = std::make_shared<std::vector<DetectorModelInfo>>();
    
    if( filename.empty() )
      return nullptr;
    
#ifdef _WIN32
    const std::wstring wfilename = UtilityFunctions::convert_from_utf8_to_utf16(filename);
    ifstream input( filename.c_str() );
#else
    ifstream input( filename.c_str() );
#endif
    
    if( !input.is_open() )
    {
      cerr << "Error: Unable to open detector serial number to model file '" << sm_data_filename << "'" << endl;
      return nullptr;
    }//if( !input.is_open() )
    
    string line;
    int line_num = 0;
    while( UtilityFunctions::safe_get_line(input, line, 8192) )
    {
      if( line.size() > 8190 )
      {
        std::cerr << "Error: line " << line_num << " is longer than max allowed length of 8190 characters; not reading in file" << std::endl;
        return nullptr;
      }
      
      ++line_num;
      
      UtilityFunctions::trim( line );
      if( line.empty() || line[0]=='#' )
        continue;
      
      vector<string> fields;
      UtilityFunctions::split_no_delim_compress( fields, line, "," );
      
      //Allow the file to be comma or tab delimited, but if an individual field
      //  contains a comma or tab, then the field must be quoted by a double
      //  quote.  Note that if you just copy cells from Microsoft Excel, that
      //  contain a comma, and then past into a text editor, fields with a comma
      //  are not quoted.
      
      //TODO: benchmark allowing quoted fields, e.g.:
      //#include <boost/tokenizer.hpp>
      //...
      //typedef boost::tokenizer<boost::escaped_list_separator<char> > Tokeniser;
      //boost::escaped_list_separator<char> separator("\\",",\t", "\"");
      //Tokeniser t( line, separator );
      //for( Tokeniser::iterator it = t.begin(); it != t.end(); ++it )
      //  fields.push_back(*it);
      
      if( fields.size() < 2 )
        continue;
      
      UtilityFunctions::trim( fields[0] );
      UtilityFunctions::trim( fields[1] );
      
      if( newdata->empty() && fields[0]=="SerialToDetectorModelVersion" )
      {
        if( fields.size()<3 || fields[1]!="0"  || fields[2]!="0" )
          cerr << "Warning: parse_detective_model_csv(...) hasnt implemented versioning yet" << endl;
        continue;
      }
      
      DetectorModelInfo info;
      
      try
      {
        info.serial = stoul( fields[0], nullptr, 10 );
      }catch(...)
      {
        //If the string contained a UTF-8, non-ascii character, then we will
        //  take the value to be
        bool contains_utf8 = false;
        for( const char c : fields[0] )
          contains_utf8 = (contains_utf8 || (c & 0xC0));  //0xC0 indicates start of a UTF-8 character
        
        if( contains_utf8 )
        {
          const size_t hashval = std::hash<std::string>()(fields[0]);
          info.serial = (std::numeric_limits<uint32_t>::max() & static_cast<uint32_t>(hashval));
        }else
        {
          cerr << "Warning: Detector serial number '" << fields[0]
               << "' on line '" << line << "' of file '" << sm_data_filename
               << "' was not a valid unsigned integer" << endl;
          
          continue;
        }//if( contains_utf8 ) / else
      }//try / catch to make a serial number
      
      //NotInitialized,
      //UnknownSerialNumber,
      if( fields[1] == "Unknown" )
        info.model = DetectorModel::Unknown;
      else if( fields[1] == "DetectiveEx" )
        info.model = DetectorModel::DetectiveEx;
      else if( fields[1] == "MicroDetective" )
        info.model = DetectorModel::MicroDetective;
      else if( fields[1] == "DetectiveEx100" )
        info.model = DetectorModel::DetectiveEx100;
      else if( fields[1] == "Detective200" )
        info.model = DetectorModel::Detective200;
      else
      {
        std::cerr << "Error: invalid detector model: '" << fields[1] << "'" << std::endl;
        continue;
      }
        
#if( PERFORM_DEVELOPER_CHECKS )
      info.serial_str = fields[0];
      info.model_str = fields[1];
      if( fields.size() > 2 )
        info.description = fields[2];
      if( fields.size() > 3 )
        info.file_locations = fields[3];
#endif
      newdata->push_back( info );
    }//while( UtilityFunctions::safe_get_line(input, line) )
    
    if( newdata->size() < 2 )  //2 is arbitrary, kindaof
      return nullptr;
    
    std::sort( std::begin(*newdata), std::end(*newdata), [](const DetectorModelInfo &lhs, const DetectorModelInfo &rhs){
      return lhs.serial < rhs.serial;
    } );
    
    return newdata;
  }//std::shared_ptr<std::vector<DetectorModelInfo>> parse_detective_model_csv( const std::string filename )
  
  
  /** Grabbing the serial numbers from binary Ortec files may result is getting
      a string like "Detective EX S06143438  1924", where it isnt clear which
      run of numbers is the actual serial number (I could probably improve
      grabbing this string from the binary file, but havent had time), so we
      will try each run of numbers.
   */
  std::vector<uint32_t> candidate_serial_nums_from_str( const std::string &instrument_id )
  {
    std::vector<uint32_t> answer;
    
    for( size_t i = 0; i < instrument_id.size(); ++i )
    {
      if( !isdigit(instrument_id[i]) || (instrument_id[i]=='0') )  //get rid of leading zeros on serial numbers, so like "S023143" will become "23143"
        continue;
      
      size_t j = 1;
      while( isdigit(instrument_id[i+j]) && (i+j)< instrument_id.size() )
        ++j;
      const std::string strval = instrument_id.substr(i,j);
      i += (j-1);
      if( j <= 2 || strval == "100" )  //100 is probably related to EX100, so skip it
        continue;
      
      uint32_t val = 0;
      try
      {
        val = stoul( strval, nullptr, 10 );
        if( strval.size() >= 2 && val < 100 )
          continue;
      }catch(...)
      {
        bool contains_utf8 = false;
        for( const char c : strval )
          contains_utf8 = (contains_utf8 || (c & 0xC0));  //0xC0 indicates start of a UTF-8 character
        
        if( !contains_utf8 )
          continue;
        
        const size_t hashval = std::hash<std::string>()(strval);
        val = (std::numeric_limits<uint32_t>::max() & static_cast<uint32_t>(hashval));
      }// try / catch
      
      answer.push_back( val );
    }//for( size_t i = 0; i < instrument_id.size(); ++i )
    
    return answer;
  }//std::vector<uint32_t> candidate_serial_nums_from_str( const std::string &instrument_id )
  
  
  DetectorModel detective_model_from_serial( const std::string &instrument_id )
  {
    std::shared_ptr<const vector<DetectorModelInfo>> data;
    
    {//begin lock on sm_data_mutex
      std::lock_guard<std::mutex> lock( sm_data_mutex );
      data = sm_data;
    
      if( !data )
      {
        auto parseddata = parse_detective_model_csv( sm_data_filename );
        if( !parseddata )
          return DetectorModel::NotInitialized;
        
        data = parseddata;
        sm_data = parseddata;
      }//if( !data )
    }//end lock on sm_data_mutex
    
    assert( data );
    
    const auto candidates = candidate_serial_nums_from_str( instrument_id );
    
    
    for( const auto serial : candidates )
    {
      //The lower_bound method should work, but not yet tested.
      //auto lb = std::lower_bound( std::begin(*data), std::end(*data), serial, []( const DetectorModelInfo &info, const uint32_t val ) -> bool {
      //  return info.serial < val;
      //} );
      //if( lb != std::end(*data) && lb->serial == serial )
      //  return lb->model;
      
      for( const auto &info : *data )
        if( info.serial == serial )
          return info.model;
    }//for( const auto serial : candidates )
    
    return DetectorModel::UnknownSerialNumber;
  }//DetectorModel detective_model_from_serial( const std::string &instrument_id );
  
  
  DetectorModel guess_detective_model_from_serial( const std::string &instrument_id )
  {
    if( UtilityFunctions::icontains( instrument_id, "Micro" )
       || UtilityFunctions::icontains( instrument_id, "uDet" )
       || UtilityFunctions::icontains( instrument_id, "HX" )
       || UtilityFunctions::icontains( instrument_id, "uDX")
       || UtilityFunctions::icontains( instrument_id, "\xCE\xBC" )  //Equiv to u8"μ", mu="0xCE0x9C", "&#x39C;", "U+039C", micro="0xC20xB5", "&#xB5;"
       || UtilityFunctions::icontains( instrument_id, "\xCE\x9C")  //lower-caser mu
       || UtilityFunctions::icontains( instrument_id, "\xc2\xb5") ) //micro (apparently a distint code-point)
    {
      return DetectorModel::MicroDetective;
    }
    
    
    //The case independance does not appear to be necassarry, but JIC
    // if( UtilityFunctions::icontains( instrument_id, "EX100" ) || UtilityFunctions::icontains( instrument_id, "EX 100" ) )
    if( UtilityFunctions::icontains( instrument_id, "100" ) )
      return DetectorModel::DetectiveEx100;
    
    if( UtilityFunctions::icontains( instrument_id, "200" ) )
      return DetectorModel::Detective200;
    
    const auto serials = candidate_serial_nums_from_str( instrument_id );
    for( const auto val : serials )
    {
      if( val >= 500 && val < 4000 )
        return DetectorModel::DetectiveEx;
      
      if( val >= 4000 && val < 5000 )
        return DetectorModel::DetectiveEx100;
    }//for( const string &serialstr : serialdigits )
    
    
    return DetectorModel::UnknownSerialNumber;
  }//DetectorModel guess_detective_model_from_serial( const std::string &instrument_id );
  
#if( PERFORM_DEVELOPER_CHECKS )
  void write_csv_file( std::ostream &strm )
  {
    std::shared_ptr<std::vector<DetectorModelInfo>> data;
    
    {//begin lock on sm_data_mutex
      std::lock_guard<std::mutex> lock( sm_data_mutex );
      data = sm_data;
    }//end lock on sm_data_mutex
    
    if( !data || data->empty() )
      throw std::runtime_error( "SerialToDetectorModel::write_csv_file(): you have not initialized detector mapping." );
    
    const char *eol_char = "\r\n";
    
#ifdef _WIN32
    if( !(strm.flags() & ios::binary) )
      eol_char = "\n";
#endif
    
    //Add when this file was written as a comment in the file
    time_t rawtime;
    struct tm *timeinfo;
    char datebuffer[80];
    time( &rawtime );
    timeinfo = localtime( &rawtime );
    strftime( datebuffer, sizeof(datebuffer), "%Y%m%d %H:%M:%S", timeinfo);
    
    strm << "#Serialized " << datebuffer << " using binary compiled on " << COMPILE_DATE_AS_INT << eol_char;
    strm << "SerialToDetectorModelVersion,"
         << SerialToDetectorModel_CURRENT_MAJOR_VERSION
         << "," << SerialToDetectorModel_CURRENT_MINOR_VERSION << eol_char;
    strm << "#Valid Model Strings: 'Unknown', 'DetectiveEx', 'MicroDetective', 'DetectiveEx100', 'Detective200'" << eol_char;
    strm << "#Serial number _must_ either be a unsigned 32bit int, or have at least one non-ASCII unicode character. " << eol_char;
    strm << "#Fields must not contain quotes or commas" << eol_char;
    strm << "#" << eol_char;
    strm << "#SerialNumber,Model,HowModelWasDetermined,OtherDataFromDetectorLocations" << eol_char;
    
    for( const auto &row : *data )
      strm << row.serial_str << "," << row.model_str << "," << row.description
           << "," << row.file_locations << eol_char;
  }//void write_csv_file( std::ostream &strm )
  
  
  std::shared_ptr<std::vector<DetectorModelInfo>> serial_informations()
  {
    std::shared_ptr<std::vector<DetectorModelInfo>> data;
    
    {//begin lock on sm_data_mutex
      std::lock_guard<std::mutex> lock( sm_data_mutex );
      data = sm_data;
      
      if( !data )
      {
        data = parse_detective_model_csv( sm_data_filename );
        if( !data || data->empty() )
          throw std::runtime_error( "SerialToDetectorModel::serial_informations():"
                                    " Could not initialize SerialToDetectorModel data source '" + sm_data_filename + "'" );
        sm_data = data;
      }//if( !data )
    }//end lock on sm_data_mutex
    
    return data;
  }//std::vector<DetectorModelInfo> *serial_informations()
#endif

  
}//namespace SerialToDetectorModel
