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
#include <numeric>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <functional>


#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"
#include "SpecUtils/SerialToDetectorModel.h"

using namespace std;

namespace
{
  bool not_alpha_numeric(char c)
  {
    return !(std::isalnum(c) || c==' ');
  }
  
  /* IAEA block labels that represent items to put into the remarks_ variable of
   SpecFile.
   */
  const char * const ns_iaea_comment_labels[] =
  {
    "Comment", "AcquisitionMode", "CrystalType", "Confidence",
    "MinDoseRate", "MaxDoseRate", "AvgDoseRate", "MinNeutrons", "MaxNeutrons",
    "DetectorLength", "DetectorDiameter", "BuiltInSrcType", "BuiltInSrcActivity",
    "HousingType", "GMType", "He3Pressure", "He3Length", "He3Diameter",
    "ModMaterial", "ModVolume", "ModThickness", "LastSourceStabTime",
    "LastSourceStabFG", "LastCalibTime", "LastCalibSource", "LastCalibFG",
    "LastCalibFWHM", "LastCalibTemp", "StabilType", "StartupStatus",
    "TemperatureBoard", "TemperatureBoardRange", "BatteryVoltage", "Uptime",
    "DoseRate", "DoseRateMax20min", "BackgroundSubtraction",
    "FWHMCCoeff", "ROI", "CalibPoint", "NeutronAlarm",
    "GammaDetector", "NeutronDetector", "SurveyId", "EventNumber",
    "Configuration"
  };//const char * const ns_iaea_comment_labels = {...}
  
  
  /* IAEA block labels that represent information to be put into
   component_versions_ member variable of SpecFile.
   */
  const char * const ns_iaea_version_labels[] =
  {
    "Hardware", "TemplateLibraryVersion", "NativeAlgorithmVersion",
    "ApiVersion", "Firmware", "Operating System", "Application",
    "SoftwareVersion"
  };
  
  string pad_iaea_prefix( string label )
  {
    label.resize( 22, ' ' );
    return label + ": ";
  }
  
  string print_to_iaea_datetime( const boost::posix_time::ptime &t )
  {
    char buffer[256];
    const int day = static_cast<int>( t.date().day() );
    const int month = static_cast<int>( t.date().month() );
    const int year = static_cast<int>( t.date().year() );
    const int hour = static_cast<int>( t.time_of_day().hours() );
    const int minutes = static_cast<int>( t.time_of_day().minutes() );
    const int seconds = static_cast<int>( t.time_of_day().seconds() );
    
    snprintf( buffer, sizeof(buffer), "%02d.%02d.%04d %02d:%02d:%02d",
             day, month, year, hour, minutes, seconds );
    
    return buffer;
  }//print_iaea_datetime(...)
  
}//namespace


namespace SpecUtils
{
  
bool SpecFile::load_spc_file( const std::string &filename )
{
  reset();
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
#ifdef _WIN32
  ifstream file( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream file( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  if( !file.is_open() )
    return false;
  
  uint8_t firstbyte;
  file.read( (char *) (&firstbyte), 1 );
  file.seekg( 0, ios::beg );
  const bool isbinary = (firstbyte == 0x1);
  //  const bool istext = (char(firstbyte) == 'S');
  
  if( !isbinary && /*!istext*/ !isalpha(firstbyte) )
  {
    //    cerr << "SPC file '" << filename << "'is not binary or text firstbyte="
    //         << (char)firstbyte << endl;
    return false;
  }//if( !isbinary && !istext )
  
  bool loaded = false;
  if( isbinary )
    loaded = load_from_binary_spc( file );
  else
    loaded =  load_from_iaea_spc( file );
  
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_spc_file( const std::string &filename )
  
  

bool SpecFile::load_iaea_file( const std::string &filename )
{
  reset();
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
#ifdef _WIN32
  ifstream file( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream file( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  if( !file.is_open() )
    return false;
  
  uint8_t firstbyte;
  file.read( (char *) (&firstbyte), 1 );
  file.seekg( 0, ios::beg );
  
  if( firstbyte != '$' )
  {
    //    cerr << "IAEA file '" << filename << "'does not have expected first chacter"
    //         << " of '$', firstbyte=" << int(firstbyte)
    //         << " (" << char(firstbyte) << ")" << endl;
    return false;
  }//if( wrong first byte )
  
  const bool loaded = load_from_iaea( file );
  
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_iaea_file(...)
  
  
bool SpecFile::load_from_iaea_spc( std::istream &input )
{
  //Function is currently not very robust to line ending changes, or unexpected
  //  whitespaces.  Aslo parsing of channel counts coult be sped up probably.
  
  reset();
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  std::shared_ptr<DetectorAnalysis> analysis;
  
  std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
  
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  
  //There are quite a number of fields that Measurement or SpecFile class
  //  does not yet implement, so for now we will just put them into the remarks
  
  string detector_type = "";
  
  try
  {
    string line;
    int Length = -1;
    
    
    //going through and making sure this is an ASCII file wont work, because
    //  there is often a subscript 3 (ascii code 179) or infinity symbols...
    //  So instead we'll insist the first non-empty line of file must start with
    //  three different alphanumneric characters.  We could probably tighten this
    //  up to apply to all non-empty lines in the file.
    int linenum = 0, nnotrecognized = 0;
    bool tested_first_line = false;
    vector<float> calibcoeff_poly;
    
    while( input.good() )
    {
      const istream::pos_type sol_pos = input.tellg();
      
      //    getline( input, line, '\r' );
      
      const size_t max_len = 1024*1024;  //allows a line length of 64k fields, each of 16 characters, which his more than any spectrum file should get
      SpecUtils::safe_get_line( input, line, max_len );
      
      if( line.size() >= (max_len-1) )
        throw runtime_error( "Line greater than 1MB" );
      
      trim( line );
      
      if( line.empty() )
        continue;
      
      if( !tested_first_line )
      {
        tested_first_line = true;
        if( line.size() < 3
           || !isalnum(line[0]) || !isalnum(line[1]) || !isalnum(line[2])
           || !(line[0]!=line[1] || line[0]!=line[2] || line[2]!=line[3]) )
          throw runtime_error( "File failed constraint that first three charcters"
                              " on the first non-empty line must be alphanumeric"
                              " and not be equal" );
      }//if( !tested_first_line )
      
      
      const size_t colonpos = line.find(':');
      const size_t info_pos = line.find_first_not_of(": ", colonpos);
      
      bool is_remark = false;
      //Check if its a remark field
      for( const char * const label : ns_iaea_comment_labels )
      {
        if( istarts_with( line, label ) )
        {
          is_remark = true;
          if( info_pos != string::npos )
          {
            string remark = label;
            remark += " : " + line.substr(info_pos);
            trim( remark );
            remarks_.push_back( remark );
          }
          break;
        }//if( istarts_with( line, label ) )
      }//for( const char * const label : ns_iaea_comment_labels )
      
      bool is_version = false;
      for( const char * const label : ns_iaea_version_labels )
      {
        if( istarts_with( line, label ) )
        {
          is_version = true;
          if( info_pos != string::npos )
            component_versions_.push_back( make_pair(label,line.substr(info_pos)) );
          break;
        }//if( istarts_with( line, label ) )
      }//for( const char * const label : ns_iaea_comment_labels )
      
      
      if( is_version )
      {
        //nothing to to here
      }else if( is_remark )
      {
        //Go through and look for warning signs...
        if( istarts_with( line, "BackgroundSubtraction" )
           && info_pos != string::npos
           && !icontains( line.substr(info_pos), "No" ) )
        {
#if(PERFORM_DEVELOPER_CHECKS)
          /// @TODO should put this message in a parser error/warning section.
          string msg = "Instrument may have been in background subtract mode.";
          if( !std::count( begin(remarks_), end(remarks_), msg) )
            remarks_.emplace_back( std::move(msg) );
#endif
        }
      }else if( istarts_with( line, "SpectrumName" ) )
      {//SpectrumName        : ident903558-21_2012-07-26_07-10-55-003.spc
        if( info_pos != string::npos )
        {
          if( SpecUtils::icontains( line.substr(info_pos), "ident") )
          {
            detector_type_ = DetectorType::IdentiFinderNG;
            //TODO: IdentiFinderLaBr3
            manufacturer_ = "FLIR";
            instrument_model_ = "identiFINDER";
          }else if( SpecUtils::icontains(line, "Raider") )
          {
            detector_type_ = DetectorType::MicroRaider;
            instrument_model_ = "MicroRaider";
            manufacturer_ = "FLIR";
          }
        }//if( info_pos != string::npos )
      }else if( istarts_with( line, "DetectorType" ) )
      {//DetectorType        : NaI
        if( info_pos != string::npos )
          detector_type = line.substr(info_pos);
      }
      //"GammaDetector" now included in ns_iaea_comment_labels[].
      //else if( istarts_with( line, "GammaDetector" ) )
      //{
      //ex: "GammaDetector           : NaI 35x51"
      //if( info_pos != string::npos )
      //remarks_.push_back( "Gamma Detector: " + line.substr(info_pos) );
      //meas->detector_type_ = line.substr(info_pos);
      //}
      //"NeutronDetector" now included in ns_iaea_comment_labels[].
      //else if( istarts_with( line, "NeutronDetector" ) )
      //{
      //if( info_pos != string::npos )
      //remarks_.push_back( "Neutron Detector: " + line.substr(info_pos) );
      //}
      else if( istarts_with( line, "XUnit" ) )
      {//XUnit        : keV
        if( info_pos != string::npos
           && !istarts_with( line.substr(info_pos), "keV") )
          cerr << "SpecFile::load_from_iaea_spc(istream &)\n\t" << "Unexpected x-unit: "
          << line.substr(info_pos) << endl;
      }else if( istarts_with( line, "YUnit" ) ) //        :
      {
      }else if( istarts_with( line, "Length" ) )
      {//Length       : 1024
        if( info_pos != string::npos )
          Length = atoi( line.c_str() + info_pos );
      }else if( istarts_with( line, "SubSpcNum" ) )
      {//SubSpcNum    : 1
        int SubSpcNum = 1;
        if( info_pos != string::npos )
          SubSpcNum = atoi( line.c_str() + info_pos );
        if( SubSpcNum > 1 )
        {
          const string msg = "SpecFile::load_from_iaea_spc(istream &)\n\tASCII Spc files only support "
          "reading files with one spectrum right now";
          throw std::runtime_error( msg );
        }
      }else if( istarts_with( line, "StartSubSpc" ) )
      {//StartSubSpc  : 0
      }else if( istarts_with( line, "StopSubSpc" ) )
      {//StopSubSpc   : 0
      }else if( istarts_with( line, "Realtime" ) )
      {//Realtime     : 300.000
        if( info_pos != string::npos )
          meas->real_time_ = static_cast<float>( atof( line.c_str() + info_pos ) );
      }else if( istarts_with( line, "Livetime" )
               || istarts_with( line, "Liveime" )
               || istarts_with( line, "Lifetime" ) )
      {//Livetime     : 300.000
        if( info_pos != string::npos )
          meas->live_time_ = static_cast<float>( atof( line.c_str() + info_pos ) );
      }else if( istarts_with( line, "Deadtime" ) )
      {//Deadtime     : 0.000
      }else if( istarts_with( line, "FastChannel" ) )
      {//FastChannel  : 69008
      }else if( istarts_with( line, "Starttime" ) )
      {//Starttime    : '28.08.2012 16:12:26' or '3.14.2006 10:19:36'
        if( info_pos != string::npos )
          meas->start_time_ = time_from_string( line.substr( info_pos ).c_str() );
      }else if( istarts_with( line, "Stoptime" ) )
      {//Stoptime     : 28.08.2012 16:17:25
        //      if( info_pos != string::npos )
        //         meas-> = time_from_string( line.substr( info_pos ).c_str() );
      }else if( istarts_with( line, "NeutronCounts" )
               || istarts_with( line, "SumNeutrons" ) )
      {//NeutronCounts         : 0
        const float num_neut = static_cast<float>( atof( line.substr( info_pos ).c_str() ) );
        if( info_pos != string::npos && meas->neutron_counts_.empty() )
          meas->neutron_counts_.push_back( num_neut );
        else if( info_pos != string::npos )
          meas->neutron_counts_[0] += num_neut;
        meas->neutron_counts_sum_ += num_neut;
        meas->contained_neutron_ = true;
      }
      //    FWHMCCoeff            : a=0.000000000E+000 b=0.000000000E+000 c=0.000000000E+000 d=0.000000000E+000'
      else if( starts_with( line, "CalibCoeff" ) )
      {//CalibCoeff   : a=0.000000000E+000 b=0.000000000E+000 c=3.000000000E+000 d=0.000000000E+000
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        const size_t apos = line.find( "a=" );
        const size_t bpos = line.find( "b=" );
        const size_t cpos = line.find( "c=" );
        const size_t dpos = line.find( "d=" );
        const bool have_a = apos < (line.size()-2);
        const bool have_b = bpos < (line.size()-2);
        const bool have_c = cpos < (line.size()-2);
        const bool have_d = dpos < (line.size()-2);
        if( have_a )
          a = static_cast<float>( atof( line.c_str() + apos + 2 ) );
        if( have_b )
          b = static_cast<float>( atof( line.c_str() + bpos + 2 ) );
        if( have_c )
          c = static_cast<float>( atof( line.c_str() + cpos + 2 ) );
        if( have_d )
          d = static_cast<float>( atof( line.c_str() + dpos + 2 ) );
        
        if( have_a && have_b && have_c && have_d
            && (a!=0.0 || b!=0.0 || c!=0.0 ) )
        {
          calibcoeff_poly = {d,c,b,a};
        }else if( have_b && have_c && c!=0.0 )
        {
          calibcoeff_poly = {b,c};
        }
      }else if( istarts_with( line, "NuclideID1" )
               || istarts_with( line, "NuclideID2" )
               || istarts_with( line, "NuclideID3" )
               || istarts_with( line, "NuclideID4" ) )
      {
        //"8 Annih. Rad."
        //"- Nuc. U-233"
        //"5 NORM K-40"
        //"- Ind.Ir-192s"
        if( info_pos != string::npos )
        {
          if( !analysis )
            analysis = std::make_shared<DetectorAnalysis>();
          
          DetectorAnalysisResult result;
          
          string info = line.substr(info_pos);
          string::size_type delim = info.find_first_of( ' ' );
          if( delim == 1 && (std::isdigit(info[0]) || info[0]=='-') )
          {
            result.id_confidence_ = info.substr(0,delim);
            info = info.substr(delim);
            SpecUtils::trim( info );
            delim = info.find_first_of( " ." );
            
            string nuctype = info.substr( 0, delim );
            
            if( SpecUtils::istarts_with(nuctype, "Ann")
               || SpecUtils::istarts_with(nuctype, "Nuc")
               || SpecUtils::istarts_with(nuctype, "NORM")
               || SpecUtils::istarts_with(nuctype, "Ind")
               || SpecUtils::istarts_with(nuctype, "Cal")
               || SpecUtils::istarts_with(nuctype, "x")
               || SpecUtils::istarts_with(nuctype, "med")
               || SpecUtils::istarts_with(nuctype, "cos")
               || SpecUtils::istarts_with(nuctype, "bac")
               || SpecUtils::istarts_with(nuctype, "TENORM")
               || SpecUtils::istarts_with(nuctype, "bre")
               //any others? this list so far was just winged
               )
            {
              result.nuclide_type_ = nuctype;
              result.nuclide_ = info.substr( delim );
              SpecUtils::trim( result.nuclide_ );
              
              if(!result.nuclide_.empty() && result.nuclide_[0]=='.' )
              {
                result.nuclide_ = result.nuclide_.substr(1);
                SpecUtils::trim( result.nuclide_ );
                result.nuclide_type_ += ".";
              }
              
              result.remark_ = line.substr(info_pos); //just in case
            }else
            {
              //Leaving below line in because I only tested above parsing on a handfull of files (20161010).
              cerr << "Unknown radiation type in ana  result: '" << result.nuclide_type_ << "'" << endl;
              result.nuclide_ = line.substr(info_pos);
            }
          }else
          {
            result.nuclide_ = line.substr(info_pos);
          }
          
          
          analysis->results_.push_back( result );
        }//if( info_pos != string::npos )
      }else if( istarts_with( line, "IDLibrary" ) )
      {
        //Comes in files with "NuclideID1" and "NuclideID2" lines, after all the
        //  nuclides.
        if( !analysis )
          analysis = std::make_shared<DetectorAnalysis>();
        analysis->remarks_.push_back( "Library: " + line.substr(info_pos) );
      }else if( istarts_with( line, "SpectrumText" ) )
      {//SpectrumText : 0
        
      }else if( istarts_with( line, "SerialNumber" ) )
      {
        if( info_pos != string::npos )
          instrument_id_ = line.substr(info_pos);
      }else if( istarts_with( line, "UUID" ) )
      {
        if( info_pos != string::npos )
          uuid_ = line.substr(info_pos);
      }else if( istarts_with( line, "Manufacturer" ) )
      {
        if( info_pos != string::npos )
          manufacturer_ = line.substr(info_pos);
      }else if( istarts_with( line, "ModelNumber" ) )
      {
        if( info_pos != string::npos )
          instrument_model_ = line.substr(info_pos);
      }else if( istarts_with( line, "OperatorInformation" ) )
      {
        measurement_operator_ = line.substr(info_pos);
      }else if( istarts_with( line, "GPSValid" ) )
      {
        if( SpecUtils::icontains(line, "no") )
        {
          meas->latitude_ = -999.9;
          meas->longitude_= -999.9;
        }
      }else if( istarts_with( line, "GPS" ) )
      {
        line = line.substr(info_pos);
        const string::size_type pos = line.find( '/' );
        if( pos != string::npos )
        {
          for( size_t i = 0; i < line.size(); ++i )
            if( !isalnum(line[i]) )
              line[i] = ' ';
          
          string latstr = line.substr(0,pos);
          string lonstr = line.substr(pos+1);
          trim( latstr );
          trim( lonstr );
          
          meas->latitude_ = conventional_lat_or_long_str_to_flt( latstr );
          meas->longitude_ = conventional_lat_or_long_str_to_flt( lonstr );
        }else
        {
          cerr << "SpecFile::load_from_iaea_spc(istream &): couldnt split lat lon" << endl;
        }
      }else if( istarts_with( line, "DeviceId" ) )
      {
        instrument_id_ = line.substr(info_pos);
        trim( instrument_id_ );
      }else if( istarts_with( line, "Nuclide0" )
               || istarts_with( line, "Nuclide1" )
               || istarts_with( line, "Nuclide2" )
               || istarts_with( line, "Nuclide3" ) )
      {
        //some identiFINDER 2 LGH detectors makes it here.
        
        const istream::pos_type currentpos = input.tellg();
        
        //"Nuclide0" line is sometimes followed by "Strength0", "Class0",
        //  and "Confidence0" lines, so lets try and grab them.
        string strengthline, classline, confidenceline;
        try
        {
          SpecUtils::safe_get_line( input, strengthline, max_len );
          SpecUtils::safe_get_line( input, classline, max_len );
          SpecUtils::safe_get_line( input, confidenceline, max_len );
          
          
          const size_t strength_colonpos = strengthline.find(':');
          const size_t strength_info_pos = strengthline.find_first_not_of(": ", strength_colonpos);
          
          const size_t class_colonpos = classline.find(':');
          const size_t class_info_pos = classline.find_first_not_of(": ", class_colonpos);
          
          const size_t conf_colonpos = confidenceline.find(':');
          const size_t conf_info_pos = confidenceline.find_first_not_of(": ", conf_colonpos);
          
          if( !SpecUtils::istarts_with( strengthline, "Strength" )
             || !SpecUtils::istarts_with( classline, "Class" )
             || !SpecUtils::istarts_with( confidenceline, "Confidence" )
             || class_info_pos == string::npos
             || strength_info_pos == string::npos
             || conf_info_pos == string::npos )
            throw runtime_error( "" );
          
          strengthline = strengthline.substr( strength_info_pos );
          classline = classline.substr( class_info_pos );
          confidenceline = confidenceline.substr( conf_info_pos );
        }catch(...)
        {
          input.seekg( currentpos );
          strengthline.clear();
          classline.clear();
          confidenceline.clear();
        }
        
        
        if( !analysis )
          analysis = std::make_shared<DetectorAnalysis>();
        
        DetectorAnalysisResult result;
        result.nuclide_ = line.substr(info_pos);
        result.nuclide_type_ = classline;
        result.id_confidence_ = confidenceline;
        if(!strengthline.empty())
          result.remark_ = "Strength " + strengthline;
        
        analysis->results_.push_back( result );
      }else if( line.length() && isdigit(line[0]) && ((linenum - nnotrecognized) > 1)  )
      {
        auto channel_data = std::make_shared< std::vector<float> >();
        
        input.seekg( sol_pos, ios::beg );
        while( SpecUtils::safe_get_line( input, line ) )
        {
          trim(line);
          
          if( line.empty() && Length>=0 && channel_data->size()==Length )
          {
            //ref8MLQDKLR3E seems to have a bunch of extra zeros at the end of the
            //  file (after a line break), so lets deal with this in a way that we
            //  can still try to enforce Length==channel_data->size() at the end
            if( !input.eof() )
            {
              istream::pos_type pos;
              do
              {
                pos = input.tellg();
                trim( line );
                if(!line.empty() && (line[0]<'0' || line[0]>'9') )
                {
                  input.seekg( pos, ios::beg );
                  break;
                }
              }while( SpecUtils::safe_get_line( input, line ) );
            }//if( not at the end of the file )
            
            break;
          }//if( we hit an empty line, and weve read the expected number of channels )
          
          if(!line.empty() && (line[0]<'0' || line[0]>'9') )
            break;
          
          vector<float> linefloats;
          SpecUtils::split_to_floats( line.c_str(), line.length(), linefloats );
          channel_data->insert( channel_data->end(), linefloats.begin(), linefloats.end() );
        }//while( SpecUtils::safe_get_line( input, line ) )
        
        if( size_t(Length) != channel_data->size() )
        {
          bool isPowerOfTwo = ((Length != 0) && !(Length & (Length - 1)));
          if( isPowerOfTwo && Length >= 1024 && size_t(Length) < channel_data->size() )
          {
            channel_data->resize( Length );
          }else if( Length > 0 )
          {
            stringstream msg;
            msg << "SpecFile::load_from_iaea_spc(istream &)\n\tExpected to read "
                << Length << " channel datas, but instead read " << channel_data->size();
            throw std::runtime_error( msg.str() );
          }//if( Length > 0 && size_t(Length) != channel_data->size() )
        }//if( size_t(Length) != channel_data->size() )
        
        meas->gamma_counts_ = channel_data;
        for( const float a : *channel_data )
          meas->gamma_count_sum_ += a;
      }
      else
      {
        if( !linenum && line.length() )
        {
          for( size_t i = 0; i < line.size(); ++i )
            if( (line[i] & 0x80) )
              throw runtime_error( "Unknown tag and non-ascii character in first non-empty line" );
        }
        
        if( SpecUtils::istarts_with(line, "TSA,") )
          throw runtime_error( "This is probably a TSA file, not a Ascii Spc" );
        
        ++nnotrecognized;
        if( nnotrecognized > 15 && nnotrecognized >= linenum )
          throw runtime_error( "To many unregognized begining lines" );
        
#if(PERFORM_DEVELOPER_CHECKS)
        cerr << "Warning: SpecFile::load_from_iaea_spc(...):  I didnt recognize line: '"
        << line << "'" << endl;
#endif
      }//if / else to figure out what this line cooresponds to
      
      ++linenum;
    }//while( input.good() )
    
    if( meas && meas->gamma_counts_ && (meas->gamma_counts_->size()>2) && !calibcoeff_poly.empty() )
    {
      try
      {
        auto newcal = make_shared<EnergyCalibration>();
        newcal->set_polynomial( meas->gamma_counts_->size(), calibcoeff_poly, {} );
        meas->energy_calibration_ = newcal;
      }catch( std::exception &e )
      {
        meas->parse_warnings_.push_back( "Energy cal provided invalid: " + string(e.what()) );
      }//
    }//if( we have energy calibration )
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
    return false;
  }
  
  
  //identiFINDER 2 NGH spectrum files will have spectrum number as their UUID,
  //  so to create a bit more unique UUID, lets add in the serial number to the
  //  UUID, like in the other identiFINDER formats.
  if(!uuid_.empty() && uuid_.size() < 5 && !instrument_id_.empty())
    uuid_ = instrument_id_ + "/" + uuid_;
  
  if( !meas->gamma_counts_ || meas->gamma_counts_->size() < 9 )
  {
    reset();
    //    cerr << "SpecFile::load_from_iaea_spc(...): did not read any spectrum info"
    //         << endl;
    return false;
  }//if( meas->gamma_counts_->empty() )
  
  measurements_.push_back( meas );
  
  detectors_analysis_ = analysis;
  
  //A temporary message untile I debug detector_type_ a little more
  if( icontains(instrument_model_,"identiFINDER")
     && ( (icontains(instrument_model_,"2") && !icontains(instrument_model_,"LG")) || icontains(instrument_model_,"NG")))
    detector_type_ = DetectorType::IdentiFinderNG;
  else if( icontains(detector_type,"La") && !detector_type.empty())
  {
    cerr << "Has " << detector_type << " is this a LaBr3? Cause I'm assuming it is" << endl;
    //XXX - this doesnt actually catch all LaBr3 detectors
    detector_type_ = DetectorType::IdentiFinderLaBr3;
  }else if( icontains(instrument_model_,"identiFINDER") && icontains(instrument_model_,"LG") )
  {
    cout << "Untested IdentiFinderLaBr3 association!" << endl;
    detector_type_ = DetectorType::IdentiFinderLaBr3;
  }else if( icontains(instrument_model_,"identiFINDER") )
  {
    detector_type_ = DetectorType::IdentiFinder;
  }
  
  //  if( detector_type_ == Unknown )
  //  {
  //    cerr << "I couldnt find detector type for ASCII SPC file" << endl;
  //  }
  
  cleanup_after_load();
  
  return true;
}//bool load_from_iaea_spc( std::istream &input )
  
  
  
bool SpecFile::write_ascii_spc( std::ostream &output,
                                 std::set<int> sample_nums,
                                 const std::set<int> &det_nums ) const
{
  try
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    if( sample_nums.empty() )
      sample_nums = sample_numbers_;
    
    const size_t ndet = detector_numbers_.size();
    vector<bool> detectors( ndet, true );
    if( !det_nums.empty() )
    {
      for( size_t i = 0; i < ndet; ++i )
        detectors[i] = (det_nums.count(detector_numbers_[i]) != 0);
    }//if( det_nums.empty() )
    
    
    std::shared_ptr<Measurement> summed = sum_measurements( sample_nums, detectors );
    
    if( !summed || !summed->gamma_counts() || summed->gamma_counts()->empty() )
      return false;
    
    if(!summed->title().empty())
      output << pad_iaea_prefix( "SpectrumName" ) << summed->title() << "\r\n";
    else
      output << pad_iaea_prefix( "SpectrumName" ) << filename_ << "\r\n";
    
    output << pad_iaea_prefix( "XUnit" ) << "keV\r\n";
    output << pad_iaea_prefix( "YUnit" ) << "\r\n";
    output << pad_iaea_prefix( "Length" ) << summed->gamma_counts_->size() << "\r\n";
    output << pad_iaea_prefix( "SubSpcNum" )   << "1\r\n";
    output << pad_iaea_prefix( "StartSubSpc" ) << "0\r\n";
    output << pad_iaea_prefix( "StopSubSpc" )  << "0\r\n";
    
    int ncomment = 0;
    bool printedFWHMCCoeff = false;
    for( const string &remark : remarks_ )
    {
      bool used = false;
      for( const char * const label : ns_iaea_comment_labels )
      {
        const string prefix = label + string(" : ");
        if( SpecUtils::istarts_with(remark, prefix) )
        {
          output << pad_iaea_prefix(label) << remark.substr( prefix.size() ) << "\r\n";
          printedFWHMCCoeff |= SpecUtils::iequals_ascii(label,"FWHMCCoeff");
          used = true;
          break;
        }
      }//for( const char * const label : ns_iaea_comment_labels )
      
      if( !used )
      {
        ++ncomment;
        output << pad_iaea_prefix("Comment") << remark << "\r\n";
      }
    }//for( const string &remark : remarks_ )
    
    if( !ncomment )
      output << pad_iaea_prefix("Comment") << "\r\n";
    
    char buffer[256];
    if( summed->real_time_ > 0.0f )
    {
      snprintf( buffer, sizeof(buffer), "%.3f", summed->real_time_ );
      output << pad_iaea_prefix( "Realtime" ) << buffer << "\r\n";
    }
    
    if( summed->live_time_ > 0.0f )
    {
      snprintf( buffer, sizeof(buffer), "%.3f", summed->live_time_ );
      output << pad_iaea_prefix( "Livetime" ) << buffer << "\r\n";
    }
    
    if( (summed->real_time_ > 0.0f) && (summed->live_time_ > 0.0f) )
    {
      snprintf( buffer, sizeof(buffer), "%.3f", (summed->real_time_ - summed->live_time_) );
      output << pad_iaea_prefix( "Deadtime" ) << buffer << "\r\n";
    }
    
    //I dont know what FastChannel is
    //output << pad_iaea_prefix( "FastChannel" ) << "3229677" << "\r\n";
    
    for( const pair<std::string,std::string> &cmpnt : component_versions_ )
    {
      for( const char * const label : ns_iaea_version_labels )
      {
        if( cmpnt.first == label )
        {
          output << pad_iaea_prefix(cmpnt.first) << cmpnt.second << "\r\n";
          break;
        }//if( cmpnt.first == label )
      }
    }//for( const pair<std::string,std::string> &cmpnt : component_versions_ )
    
    
    if( !summed->start_time_.is_special() )
    {
      output << pad_iaea_prefix( "Starttime" ) << print_to_iaea_datetime(summed->start_time_) << "\r\n";
      
      //Add stop time if we
      if( (sample_nums.size()==1 && det_nums.size()==1) )
      {
        float intsec, fracsec;
        fracsec = std::modf( summed->real_time_, &intsec );
        
        boost::posix_time::ptime endtime = summed->start_time_
        + boost::posix_time::seconds( static_cast<int>(intsec) )
        + boost::posix_time::microseconds( static_cast<int>( floor((1.0e6f * fracsec) + 0.5f) ) );
        
        output << pad_iaea_prefix( "StopTime" ) << print_to_iaea_datetime( endtime ) << "\r\n";
      }//
    }//if( !summed->start_time_.is_special() )
    
    
    if( summed->contained_neutron_ )
      output << pad_iaea_prefix( "NeutronCounts" ) << static_cast<int>(floor(summed->neutron_counts_sum_ + 0.5)) << "\r\n";
    
    assert( summed->energy_calibration_ );
    
    const size_t nchannel = summed->gamma_counts_ ? summed->gamma_counts_->size() : size_t(0);
    vector<float> calcoefs = summed->energy_calibration_->coefficients();
    switch( summed->energy_calibration_->type() )
    {
      case SpecUtils::EnergyCalType::Polynomial:
      case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
        //coefficnets are already in format we want.
        break;
        
      case EnergyCalType::FullRangeFraction:
        calcoefs = SpecUtils::fullrangefraction_coef_to_polynomial( calcoefs, nchannel );
        break;
        
      case EnergyCalType::LowerChannelEdge:
      case EnergyCalType::InvalidEquationType:
        calcoefs.clear(); //probably isnt necassary
        break;
    }//switch( summed->energy_calibration_->type() )
    
    
    const size_t ncoef = calcoefs.size();
    const float a = (ncoef>3 ? calcoefs[3] : 0.0f);
    const float b = (ncoef>2 ? calcoefs[2] : 0.0f);
    const float c = (ncoef>1 ? calcoefs[1] : 0.0f);
    const float d = (ncoef>0 ? calcoefs[0] : 0.0f);
    
    snprintf( buffer, sizeof(buffer), "a=%.9e b=%.9e c=%.9e d=%.9e", a, b, c, d );
    output << pad_iaea_prefix( "CalibCoeff" ) << buffer << "\r\n";
    
    //Not sure why this line always appears in files, but whatever
    if( !printedFWHMCCoeff )
      output << pad_iaea_prefix( "FWHMCCoeff" ) << "a=0.000000000E+000 b=0.000000000E+000 c=0.000000000E+000 d=0.000000000E+000\r\n";
    
    
    if(!instrument_id_.empty())
    {
      //We see two variants of how the serial number is specified, so lets put
      //  both into the file incase an analysis program only looks for one of them
      output << pad_iaea_prefix( "SerialNumber" ) << instrument_id_ << "\r\n";
      output << pad_iaea_prefix( "DeviceId" ) << instrument_id_ << "\r\n";
    }
    
    string uuid = uuid_;
    if(!instrument_id_.empty() && istarts_with( uuid, (instrument_id_+"/") ) )
      uuid = uuid.substr(instrument_id_.size()+1);
    if(!uuid.empty())
      output << pad_iaea_prefix( "UUID" ) << uuid.substr(instrument_id_.size()+1) << "\r\n";
    
    if(!manufacturer_.empty())
      output << pad_iaea_prefix( "Manufacturer" ) << manufacturer_ << "\r\n";
    
    if(!instrument_model_.empty())
      output << pad_iaea_prefix( "ModelNumber" ) << instrument_model_ << "\r\n";
    
    if(!measurement_operator_.empty())
      output << pad_iaea_prefix( "OperatorInformation" ) << measurement_operator_ << "\r\n";
    
    if( summed->has_gps_info() )
    {
      output << pad_iaea_prefix( "GPSValid" ) << "yes\r\n";
      //Should probably put into degree, minute, second notation, but some other time...
      output << pad_iaea_prefix( "GPS" ) << summed->latitude_ << "," << summed->longitude_ << "\r\n";
    }//if( summed->has_gps_info() )
    
    
    if( detectors_analysis_ && !detectors_analysis_->results_.empty())
    {
      for( size_t i = 0; i < detectors_analysis_->results_.size(); ++i )
      {
        const DetectorAnalysisResult &res = detectors_analysis_->results_[i];
        
        //We see two ways analysis results are conveyed in SPC files; lets make an
        //  attempt at having the output be consistent with the input, in terms
        //  of SPC files.  This of course will be a inconsistent for converting
        //  other file formats to SPC, but such is life
        if(!res.nuclide_.empty() && !res.nuclide_type_.empty())
        {
          const string postfix = string("") + char('0' + i);
          output << pad_iaea_prefix( "Nuclide" + postfix ) << res.nuclide_ << "\r\n";
          if( SpecUtils::istarts_with(res.remark_, "Strength ") )
            output << pad_iaea_prefix( "Strength" + postfix ) << res.nuclide_ << "\r\n";
          else
            output << pad_iaea_prefix( "Strength" + postfix ) << "\r\n";
          output << pad_iaea_prefix( "Class" + postfix ) << res.nuclide_type_ << "\r\n";
          output << pad_iaea_prefix( "Confidence" + postfix ) << res.id_confidence_ << "\r\n";
        }else if(!res.nuclide_.empty())
        {
          const string postfix = string("") + char('1' + i);
          output << pad_iaea_prefix( "NuclideID" + postfix ) << res.nuclide_ << "\r\n";
        }
      }//
    }//if( have analsysi results to output )
    
    
    //I've only seen the SpectrumText line either empty, having only a '0'...
    output << pad_iaea_prefix( "SpectrumText" ) << "\r\n";
    
    const vector<float> &counts = *summed->gamma_counts_;
    output << counts[0];
    for( size_t i = 1; i < counts.size(); ++i )
      output << (((i%8)==0) ? "\r\n" : ",") << counts[i];
    output << "\r\n";
    
    /*
     //Other tags found in identiFINDER SPE files:
     $RT:
     12
     $DT:
     9
     $SPEC_INTEGRAL:
     2769
     $FLIR_DATASET_NUMBER:
     284
     $FLIR_GAMMA_DETECTOR_INFORMATION:
     SodiumIodide
     Cesium137
     NaI 35x51
     
     $FLIR_NEUTRON_DETECTOR_INFORMATION:
     ?He tube 3He3/608/15NS
     $FLIR_SPECTRUM_TYPE:
     Measurement
     UserControlled
     $FLIR_DOSE_RATE_SWMM:
     12
     1.301
     0.062
     0.141
     $FLIR_NEUTRON_SWMM:
     12
     0.000
     0.000
     0.000
     $FLIR_REACHBACK:
     
     */
    
    //Still need to fix up DetectorType, and deal with instrument model coorectly
    /*
     }else if( istarts_with( line, "DetectorType" ) )
     {//DetectorType        : NaI
     if( info_pos != string::npos )
     detector_type = line.substr(info_pos);
     }
     //A temporary message untile I debug detector_type_ a little more
     if( icontains(instrument_model_,"identiFINDER")
     && ( (icontains(instrument_model_,"2") && !icontains(instrument_model_,"LG")) || icontains(instrument_model_,"NG")))
     detector_type_ = DetectorType::IdentiFinderNG;
     else if( icontains(detector_type,"La") && detector_type.size() )
     {
     cerr << "Has " << detector_type << " is this a LaBr3? Cause I'm assuming it is" << endl;
     //XXX - this doesnt actually catch all LaBr3 detectors
     detector_type_ = IdentiFinderLaBr3;
     }else if( icontains(instrument_model_,"identiFINDER") && icontains(instrument_model_,"LG") )
     {
     cout << "Untested IdentiFinderLaBr3 association!" << endl;
     detector_type_ = IdentiFinderLaBr3;
     }else if( icontains(instrument_model_,"identiFINDER") )
     {
     detector_type_ = kIdentiFinderDetector;
     }
     */
  }catch( std::exception & )
  {
    return false;
  }
  
  return true;
}//bool write_ascii_spc(...)
  
  
bool SpecFile::write_binary_spc( std::ostream &output,
                                  const SpecFile::SpcBinaryType type,
                                  std::set<int> sample_nums,
                                  const std::set<int> &det_nums ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( sample_nums.empty() )
    sample_nums = sample_numbers_;
  
  const size_t ndet = detector_numbers_.size();
  vector<bool> detectors( ndet, true );
  if( !det_nums.empty() )
  {
    for( size_t i = 0; i < ndet; ++i )
      detectors[i] = (det_nums.count(detector_numbers_[i]) != 0);
  }//if( det_nums.empty() )
  
  //const size_t initialpos = output.tellp();
  
  std::shared_ptr<Measurement> summed = sum_measurements( sample_nums, detectors );
  
  if( !summed || !summed->gamma_counts() )
    return false;
  
  const size_t ngammachan = summed->gamma_counts()->size();
  
  const uint16_t n_channel = static_cast<uint16_t>( std::min( ngammachan, static_cast<size_t>(std::numeric_limits<uint16_t>::max()) ) );
  size_t pos = 0;
  
  //see http://www.ortec-online.com/download/ortec-software-file-structure-manual.pdf
  const int16_t wINFTYP = 1; // Must be 1
  const int16_t wFILTYP = (type==IntegerSpcType ? 1 : 5);
  const int16_t wSkip1[2] = { 0, 0 } ;
  const int16_t wACQIRP = 3; // Acquisition information record pointer
  const int16_t wSAMDRP = 4; // Sample description record pointer
  const int16_t wDETDRP = 5; // Detector description record pointer
  const int16_t wSKIP2[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  const int16_t wCALDES = 0; //8; // Calibration description record pointer
  const int16_t wCALRP1 = 6; // First calibration data record pointer
  const int16_t wCALRP2 = 0; //7; // Second calibration data record pointer
  const int16_t wEFFPRP = 0; // Efficiency pairs record pointer (first record)
  const int16_t wROIRP1 = 0; // Record number of the first of two ROI recs
  const int16_t wEPRP = 0;   // Energy pairs record pointer
  const int16_t wEPN = 0;    // Number of energy pairs records
  const int16_t wSkip3[6] = { 0, 0, 0, 0, 0, 0 };
  const int16_t wEFFPNM = 0; // Number of efficiency pairs records  256
  int16_t wSPCTRP = 9; //was 25 for example file //could probably be wCALDES // pointer to spectrum
  
  int16_t firstReportPtr = 0;
  if( summed->contained_neutron() || summed->has_gps_info()
     || (detectors_analysis_ && !detectors_analysis_->results_.empty()) )
  {
    firstReportPtr = 9;
    wSPCTRP = 25;  //we will allow a max fo 2048 bytes in expansion header, which is 16*128.  16+9=25
  }
  
  
  //n_channel
  //We can fit 32 floats per 128 byte record
  const int16_t wSPCRCN = (n_channel / 32) + (((n_channel%32)!=0) ? 1 : 0); // Number records for the spectrum
  
  const int16_t wABSTCHN = 0;// Physical start channel for data
  
  float sACQTIM = 0.0; //10616.5 is 2014-Sep-19 12:14:57  // Date and time acquisition start in DECDAY format
  double dACQTI8 = 0.0; //10616.5 // Date and time as double precision DECDAY
  
  if( !summed->start_time().is_special() )
  {
    const boost::posix_time::ptime &startime = summed->start_time();
    const boost::gregorian::date epic_date( 1979, boost::gregorian::Jan, 1 );
    const boost::gregorian::days daydiff = startime.date() - epic_date;
    const double dayfrac = startime.time_of_day().total_microseconds() / (24.0*60.0*60.0*1.0E6);
    dACQTI8 = daydiff.days() + dayfrac;
    sACQTIM = static_cast<float>( dACQTI8 );
  }//if( !summed->start_time().is_special() )
  
  const int16_t wSkip4[4] = { 0, 0, 0, 0 };
  const int16_t wCHNSRT = 0; // Start channel number
  const float sRLTMDT = summed->real_time(); // Real time in seconds
  const float sLVTMDT = summed->live_time(); // Live time in seconds
  const int16_t wSkip50 = 0;
  const int16_t framRecords = 0;//     Pointer to FRAM records
  const int16_t TRIFID = 0; // I*2     Pointer to TRIFID records
  const int16_t NaI = 0; //I*2     Pointer to NaI records
  const int16_t Location = 0;// I*2     Pointer to Location records
  const int16_t MCSdata = 0; //I*2     Number of channels of MCS data appended to the histogram data in this file
  const int16_t expansionHeader = 2;//     Pointer to expansion header record (i.e. second header record)
  const int16_t reserved[5] = { 0, 0, 0, 0, 0 }; // 57-62                 Reserved (must be 0)
  const float RRSFCT = 0.0;//  R*4     Total random summing factor
  const uint8_t zero_byte = 0;
  const int16_t zeroword = 0;
  const uint32_t zero_dword = 0;
  
  //word number
  pos += write_binary_data( output, wINFTYP );            //1
  pos += write_binary_data( output, wFILTYP );            //2
  output.write( (const char *)&wSkip1[0], 2*2 ); //4
  pos += 4;
  pos += write_binary_data( output, wACQIRP );            //5
  pos += write_binary_data( output, wSAMDRP );            //6
  pos += write_binary_data( output, wDETDRP );            //7
  output.write( (const char *)&wSKIP2[0], 2*9 ); //8
  pos += 2*9;
  pos += write_binary_data( output, wCALDES );            //17
  pos += write_binary_data( output, wCALRP1 );            //18
  pos += write_binary_data( output, wCALRP2 );            //19
  pos += write_binary_data( output, wEFFPRP );            //20
  pos += write_binary_data( output, wROIRP1 );            //21
  pos += write_binary_data( output, wEPRP );              //22
  pos += write_binary_data( output, wEPN );               //23
  output.write( (const char *)&wSkip3[0], 2*6 ); //24
  pos += 2*6;
  pos += write_binary_data( output, wEFFPNM );            //30
  pos += write_binary_data( output, wSPCTRP );            //31
  pos += write_binary_data( output, wSPCRCN );            //32
  pos += write_binary_data( output, n_channel );          //33
  pos += write_binary_data( output, wABSTCHN );           //34
  pos += write_binary_data( output, sACQTIM );            //35
  pos += write_binary_data( output, dACQTI8 );            //37
  
  output.write( (const char *)&wSkip4[0], 2*4 ); //41
  pos += 2*4;
  pos += write_binary_data( output, wCHNSRT );            //45
  pos += write_binary_data( output, sRLTMDT );            //46
  pos += write_binary_data( output, sLVTMDT );            //48
  pos += write_binary_data( output, wSkip50 );            //50
  pos += write_binary_data( output, framRecords );        //51
  
  //write_binary_data( output, zeroword );           //52
  
  pos += write_binary_data( output, TRIFID );             //53
  pos += write_binary_data( output, NaI );                //54
  
  pos += write_binary_data( output, Location );           //
  pos += write_binary_data( output, MCSdata );            //
  pos += write_binary_data( output, expansionHeader );    //
  output.write( (const char *)&reserved[0], 2*5 ); //55
  pos += 2*5;
  
  //output.write( (const char *)&reserved[0], 2*3 ); //60
  
  //  output.write( (const char *)&reserved[0], 2*5 ); //
  pos += write_binary_data( output, RRSFCT ); //63
  
  //20160915: we're actually at pos 126 right now, so lets write in
  pos += write_binary_data( output, zeroword );
  
  
  //Write expansion header information
  size_t poswanted = (expansionHeader-1)*128;
  while( pos < poswanted )
    pos += write_binary_data( output, zero_byte );
  
  
  {
    const int16_t recordID = 111;
    const int16_t gpsPointer = 0;  //I havent been able to reliable decode files with a GPS record, so we wont write this record
    
    pos += write_binary_data( output, recordID );
    pos += write_binary_data( output, gpsPointer );
    pos += write_binary_data( output, firstReportPtr );
  }
  
  //write Acquisition information record pointer
  //1      Default spectrum file name (stored as 16 ASCII characters)
  //17     Date in the form DD-MMM-YY* (stored as 12 ASCII characters).
  //       The * character should be ignored if it is not a "1". If it is a "1",
  //       it indicates the data is after the year 2000.
  //29     Time in the form HH:MM:SS (stored as 10 ASCII characters)
  //39     Live Time rounded to nearest second (stored as 10 ASCII characters)
  //49     Real Time rounded to nearest second (stored as 10 ASCII characters)
  //59–90: Reserved
  //91     Start date of sample collection (10 ASCII characters)
  //103    Start time of sample collection (8 ASCII characters)
  //111    Stop date of sample collection (10 ASCII characters
  //121    Stop time of sample collection (8 ASCII characters)
  
  poswanted = (wACQIRP-1)*128;
  assert( (expansionHeader == 0) || (wACQIRP > expansionHeader) );
  while( pos < poswanted )
    pos += write_binary_data( output, zero_byte );
  
  const char *defaultname = 0;
  switch( detector_type_ )
  {
    case DetectorType::Exploranium:    case DetectorType::IdentiFinder:
    case DetectorType::IdentiFinderNG: case DetectorType::IdentiFinderLaBr3:
    case DetectorType::SAIC8:          case DetectorType::Falcon5000:
    case DetectorType::Unknown:        case DetectorType::MicroRaider:
    case DetectorType::Rsi701: case DetectorType::Rsi705:
    case DetectorType::AvidRsi: case DetectorType::Sam940LaBr3:
    case DetectorType::Sam940: case DetectorType::OrtecRadEagleNai:
    case DetectorType::OrtecRadEagleCeBr2Inch:
    case DetectorType::OrtecRadEagleCeBr3Inch:
    case DetectorType::OrtecRadEagleLaBr:
    case DetectorType::Sam945:
    case DetectorType::Srpm210:
    case DetectorType::RadHunterNaI:
    case DetectorType::RadHunterLaBr3:
      defaultname = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
      break;
      
    case DetectorType::DetectiveUnknown:
    case DetectorType::DetectiveEx:
    case DetectorType::DetectiveEx100:
    case DetectorType::DetectiveEx200:
    case DetectorType::MicroDetective:
    case DetectorType::DetectiveX:
      defaultname = "DetectiveEX.SPC";
      break;
  }//switch( detector_type_ )
  
  pos += 16;
  output.write( defaultname, 16 );
  
  string datestr;
  if( summed->start_time().date().is_special() )
  {
    datestr = "01-Jan-001";
  }else
  {
    const int daynum = summed->start_time().date().day();
    if( daynum < 10 )
      datestr += "0";
    datestr += std::to_string(daynum);
    datestr += "-";
    switch( summed->start_time().date().month() )
    {
      case boost::gregorian::Jan: datestr += "Jan"; break;
      case boost::gregorian::Feb: datestr += "Feb"; break;
      case boost::gregorian::Mar: datestr += "Mar"; break;
      case boost::gregorian::Apr: datestr += "Apr"; break;
      case boost::gregorian::May: datestr += "May"; break;
      case boost::gregorian::Jun: datestr += "Jun"; break;
      case boost::gregorian::Jul: datestr += "Jul"; break;
      case boost::gregorian::Aug: datestr += "Aug"; break;
      case boost::gregorian::Sep: datestr += "Sep"; break;
      case boost::gregorian::Oct: datestr += "Oct"; break;
      case boost::gregorian::Nov: datestr += "Nov"; break;
      case boost::gregorian::Dec: datestr += "Dec"; break;
      case boost::gregorian::NotAMonth: case boost::gregorian::NumMonths:
        datestr += "\0\0\0";
        break;
    }//switch( summed->start_time().date().month() )
    
    datestr += "-";
    const int yearnum = summed->start_time().date().year() % 100;
    if( yearnum < 10 )
      datestr += "0";
    datestr += std::to_string(yearnum);
    datestr += (summed->start_time().date().year() > 1999 ? "1" : "0");
  }
  datestr.resize( 13, '\0' );
  
  pos += 12;
  output.write( &datestr[0], 12 );
  
  char timestr[12] = { '\0' };
  if( summed->start_time().is_special() )
  {
    strcpy( timestr, "00:00:00" );
  }else
  {
    const int hournum = static_cast<int>( summed->start_time().time_of_day().hours() );
    const int minutenum = static_cast<int>( summed->start_time().time_of_day().minutes() );
    const int secondnum = static_cast<int>( summed->start_time().time_of_day().seconds() );
    snprintf( timestr, sizeof(timestr)-1, "%02d:%02d:%02d",
             hournum, minutenum, secondnum );
  }
  
  pos += 10;
  output.write( timestr, 10 );
  
  const int rtint = int(floor(summed->live_time()+0.5f));
  const int ltint = int(floor(summed->real_time()+0.5f));
  string ltIntStr = std::to_string(rtint );
  string rtIntStr = std::to_string(ltint);
  ltIntStr.resize( 11, '\0' );
  rtIntStr.resize( 11, '\0' );
  
  pos += 10;
  output.write( &ltIntStr[0], 10 );
  pos += 10;
  output.write( &rtIntStr[0], 10 );
  
  for( int i = 0; i < 32; ++i )
    pos += write_binary_data( output, zero_byte );
  
  //The start date/time and end date/time of sample collection doesnt seem to
  //  be correct in detector generated SPC file, so we'll just put something
  //  here that is not correct
  const char start_date_of_sample_collection[13] = "\0\0\0\0\0\0\0\0\0\0\0\0";
  pos += 12;
  output.write( start_date_of_sample_collection, 12 );
  const char start_time_of_sample_collection[9] = "10:59:03";
  pos += 8;
  output.write( start_time_of_sample_collection, 8 );
  const char stop_date_of_sample_collection[11] = "25-JAN-081";
  pos += 10;
  output.write( stop_date_of_sample_collection, 10 );
  const char stop_time_of_sample_collection[9] = "10:59:03";
  pos += 8;
  output.write( stop_time_of_sample_collection, 8 );
  
  //Write Sample description record (only works if input file format was SPC)
  poswanted = (wSAMDRP-1)*128;
  while( pos < poswanted )
    pos += write_binary_data( output, zero_byte );
  string sampledescrip = summed->title_;
  for( const string &s : remarks_ )
  {
    if( SpecUtils::starts_with(s, "Sample Description: ") )
      sampledescrip = " " + s.substr( 20 );
  }
  
  trim( sampledescrip );
  
  sampledescrip.resize( 128, '\0' );
  pos += 128;
  output.write( &sampledescrip[0], 128 );
  
  //Write Detector description record pointer
  poswanted = (wDETDRP-1)*128;
  assert( wDETDRP > wSAMDRP );
  while( pos < poswanted )
    pos += write_binary_data( output, zero_byte );
  string descrip = instrument_id_;
  descrip.resize( 128, '\0' );
  pos += 128;
  output.write( &descrip[0], 128 );
  
  
  //First calibration data record
  poswanted = (wCALRP1-1)*128;
  assert( wCALRP1 > wDETDRP );
  while( pos < poswanted )
    pos += write_binary_data( output, zero_byte );
  
  {//begin codeblock to write energy calibration information
    const int16_t wAFIT = 0; // 2 Above knee efficiency calibration type
    const int16_t wBFIT = 0; // 4 Below knee efficiency calibration type
    const int16_t wEFFPRS = 0; // 6 Number of efficiency pairs
    const int16_t wNCH = 0; // 8 number of channels in spectrum
    const float sKNEE = 0.0f; // 12 Detector knee (keV)
    const float sASIG = 0.0f; // 16 2-sigma uncertainty above knee
    const float sBSIG = 0.0f; // 20 2-sigma uncertainty below knee
    float sEC1 = 0.0f; // 24 Energy vs. channel coefficient A
    float sEC2 = 0.0f; // 28 Energy vs. channel coefficient B
    float sEC3 = 0.0f; // 32 Energy vs. channel coefficient C
    const float sFC1 = 0.0f; //FWHM vs. channel coefficient A (actually in most file)
    const float sFC2 = 0.0f; //FWHM vs. channel coefficient B (actually in most file)
    const float sFC3 = 0.0f; //FWHM vs. channel coefficient C (actually in most file)
    const float sPE1 = 0.0f; //Above knee eff. vs. energy coeff A or poly coeff 1
    const float sPE2 = 0.0f; //Above knee eff. vs. energy coeff B or poly coeff 2
    const float sPE3 = 0.0f; //Above knee eff. vs. energy coeff C or poly coeff 3
    const float sSE1 = 0.0f; //Below knee eff. vs. energy coeff A or poly coeff 4
    const float sSE2 = 0.0f; //Below knee eff. vs. energy coeff B or poly coeff 5
    const float sSE3 = 0.0f; //Below knee eff. vs. energy coeff C or poly coeff 6
    const int16_t wFWHTYP = 0; // FWHM type
    const int16_t wRES1 = 0; // reserved
    const int16_t wRES2 = 3; // reserved
    const int16_t wENGPRS = 0; // Number of energy pairs
    const int16_t wDETNUM = 0; //Detector Number
    const int16_t wNBKNEE = 0; // Number of calibration points below knee
    const float sENA2 = 0.0f; // Temp energy calibration
    const float sENB2 = 0.0f; // Temp energy calibration
    const float sENC2 = 0.0f; // Temp energy calibration
    const float sCALUNC = 0.0f; //Calibration source uncertainty
    const float sCALDIF = 0.0f; // Energy calibration difference
    const float sR7 = 0.0f; // Polynomial coefficient 7
    const float sR8 = 0.0f; // Polynomial coefficient 8
    const float sR9 = 0.0f; // Polynomial coefficient 9
    const float sR10 = 0.0f; // Polynomial coefficient 10
    
    
    vector<float> calib_coef = summed->calibration_coeffs();
    if( summed->energy_calibration_model() == SpecUtils::EnergyCalType::FullRangeFraction )
      calib_coef = SpecUtils::fullrangefraction_coef_to_polynomial( calib_coef, n_channel );
    else if( summed->energy_calibration_model() != SpecUtils::EnergyCalType::Polynomial
            && summed->energy_calibration_model() != SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial )
      calib_coef.clear();
    
    if( calib_coef.size() > 0 )
      sEC1 = calib_coef[0];
    if( calib_coef.size() > 1 )
      sEC2 = calib_coef[1];
    if( calib_coef.size() > 2 )
      sEC3 = calib_coef[2];
    
    pos += write_binary_data( output, wAFIT );
    pos += write_binary_data( output, wBFIT ); // 4 Below knee efficiency calibration type
    pos += write_binary_data( output, wEFFPRS ); // 6 Number of efficiency pairs
    pos += write_binary_data( output, wNCH ); // 8 number of channels in spectrum
    pos += write_binary_data( output, sKNEE ); // 12 Detector knee (keV)
    pos += write_binary_data( output, sASIG ); // 16 2-sigma uncertainty above knee
    pos += write_binary_data( output, sBSIG ); // 20 2-sigma uncertainty below knee
    pos += write_binary_data( output, sEC1 ); // 24 Energy vs. channel coefficient A
    pos += write_binary_data( output, sEC2 ); // 28 Energy vs. channel coefficient B
    pos += write_binary_data( output, sEC3 ); // 32 Energy vs. channel coefficient C
    pos += write_binary_data( output, sFC1 ); //FWHM vs. channel coefficient A (actually in most file)
    pos += write_binary_data( output, sFC2 ); //FWHM vs. channel coefficient B (actually in most file)
    pos += write_binary_data( output, sFC3 ); //FWHM vs. channel coefficient C (actually in most file)
    pos += write_binary_data( output, sPE1 ); //Above knee eff. vs. energy coeff A or poly coeff 1
    pos += write_binary_data( output, sPE2 ); //Above knee eff. vs. energy coeff B or poly coeff 2
    pos += write_binary_data( output, sPE3 ); //Above knee eff. vs. energy coeff C or poly coeff 3
    pos += write_binary_data( output, sSE1 ); //Below knee eff. vs. energy coeff A or poly coeff 4
    pos += write_binary_data( output, sSE2 ); //Below knee eff. vs. energy coeff B or poly coeff 5
    pos += write_binary_data( output, sSE3 ); //Below knee eff. vs. energy coeff C or poly coeff 6
    pos += write_binary_data( output, wFWHTYP ); // FWHM type
    pos += write_binary_data( output, wRES1 ); // reserved
    pos += write_binary_data( output, wRES2 ); // reserved
    pos += write_binary_data( output, wENGPRS ); // Number of energy pairs
    pos += write_binary_data( output, wDETNUM ); //Detector Number
    pos += write_binary_data( output, wNBKNEE ); // Number of calibration points below knee
    pos += write_binary_data( output, sENA2 ); // Temp energy calibration
    pos += write_binary_data( output, sENB2 ); // Temp energy calibration
    pos += write_binary_data( output, sENC2 ); // Temp energy calibration
    pos += write_binary_data( output, sCALUNC ); //Calibration source uncertainty
    pos += write_binary_data( output, sCALDIF ); // Energy calibration difference
    pos += write_binary_data( output, sR7 ); // Polynomial coefficient 7
    pos += write_binary_data( output, sR8 ); // Polynomial coefficient 8
    pos += write_binary_data( output, sR9 ); // Polynomial coefficient 9
    pos += write_binary_data( output, sR10 ); // Polynomial coefficient 10
  }//end codeblock to write energy calibration information
  
  
  //Second calibration data record
  if( wCALRP2 > 0 )
  {
    //static_assert( wCALRP2 > wCALRP1, "");
    poswanted = (wCALRP2-1)*128;
    while( pos < poswanted )
      pos += write_binary_data( output, zero_byte );
  }//if( wCALRP2 > 0 )
  
  
  //Calibration description record
  if( wCALDES > 0 )
  {
    static_assert( (wCALRP2 == 0) || (wCALDES > wCALRP2), "");
    
    poswanted = (wCALDES-1)*128;
    while( pos < poswanted )
      pos += write_binary_data( output, zero_byte );
  }//if( wCALDES > 0 )
  
  
  if( firstReportPtr > 0 )
  {
    poswanted = 128*(firstReportPtr-1);
    assert( poswanted >= pos );
    while( pos < poswanted )
      pos += write_binary_data( output, zero_byte );
    
    string information;
    
    set<string> nuclides;
    map<string,int> nuclide_types;
    if( detectors_analysis_ && !detectors_analysis_->results_.empty() )
    {
      for( const auto &res : detectors_analysis_->results_ )
      {
        if( !res.nuclide_type_.empty() )
          nuclide_types[res.nuclide_type_] += 1;
        if( !res.nuclide_.empty() )
          nuclides.insert( res.nuclide_ );
      }
    }//if( we have analysis results )
    
    //If we have the nuclide catagories (ex, NORM, SNM, Industrial), we will put
    //  all the info at the begining of information.
    //Else if we only have nuclide types, we will put after neutron info.  This
    // seems to be how, at least the files I have, do it; I'm sure theres some
    // logic I'm missing...
    if( !nuclide_types.empty() )
    {
      information += "Found: ";
      for( auto iter = begin(nuclide_types); iter != end(nuclide_types); ++iter )
      {
        if( iter != begin(nuclide_types) )
          information += '\t';
        information += iter->first + "(" + std::to_string(iter->second) + ")";
      }
      information += "\r\n";
      for( const auto &n : nuclides )
        information += "\t" + n;
      information += "\r\n";
      information += string("\0",1);
    }
    
    if( summed->has_gps_info() )
    {
      int degrees, minutes, seconds;
      double val = summed->latitude();
      degrees = static_cast<int>( floor( fabs(val) ) );
      val = 60.0*(val - degrees);
      minutes = static_cast<int>( floor( val ) );
      val = 60.0*(val - minutes);
      seconds = static_cast<int>( floor( val + 0.5 ) );
      
      char buffer[128] = { '\0' };
      snprintf( buffer, sizeof(buffer)-1, "Latitude %d %d %d %s\n",
               degrees, minutes, seconds, (summed->latitude()>0 ? "N" : "S") );
      information += buffer;
      
      val = summed->longitude();
      degrees = static_cast<int>( floor( fabs(val) ) );
      val = 60.0*(val - degrees);
      minutes = static_cast<int>( floor( val ) );
      val = 60.0*(val - minutes);
      seconds = static_cast<int>( floor( val + 0.5 ) );
      
      snprintf( buffer, sizeof(buffer)-1, "Longitude %d %d %d %s\n",
               degrees, minutes, seconds, (summed->longitude()>0 ? "E" : "W") );
      information += buffer;
    }//if( summed->has_gps_info() )
    
    
    if( summed->contained_neutron() )
    {
      char buffer[256] = { '\0' };
      const int nneut = static_cast<int>( floor(summed->neutron_counts_sum()+0.5) );
      snprintf( buffer, sizeof(buffer)-1, "Total neutron counts = %d\n", nneut );
      information += buffer;
      
      for( const string &s : remarks_ )
      {
        if( s.find("Total neutron count time = ") != string::npos )
          information += s + "\n";
      }
    }//if( summed->contained_neutron() )
    
    //Should consider adding: "Total neutron count time = ..."
    
    if( nuclide_types.empty() && !nuclides.empty() )
    {
      information += string("Found Nuclides\0\r\n",17);
      for( const auto &nuc : nuclides )
        information += nuc + "\r\n";
      information += string("\0",1);
    }//if( nuclide_types.empty() && !nuclides.empty() )
    
    if( information.size() >= 2048 )
      information.resize( 2047 );
    
    const uint16_t ntxtbytes = static_cast<uint16_t>( information.size() + 1 );
    const uint16_t sourcecode = 0;  //I dont actually know what this is for
    pos += write_binary_data( output, ntxtbytes );
    pos += write_binary_data( output, sourcecode );
    
    pos += ntxtbytes;
    output.write( information.c_str(), ntxtbytes );
    
    //Advance the file position to the next record start, to keep file size a
    //  mutliple of 128 bytes (not sure if this is needed)
    while( (pos % 128) )
      pos += write_binary_data( output, zero_byte );
    
    //Should upgrade to include analysis results if they are available
    //    if( !!detectors_analysis_ ){ ... }
    
    //"Found Nuclides"
    //"Suspect Nuclides"
    //"Top Lines"
    //"GPS"  or something like "GPS Location not determined"
    //"Gamma Dose Rate"
    //"Version"
    //"ID Report"
  }//if( firstReportPtr > 0 )
  
  
  //Write spectrum information
  poswanted = (wSPCTRP-1)*128;
  assert( poswanted >= pos );
  while( pos < poswanted )
    pos += write_binary_data( output, zero_byte );
  
  pos += 4*n_channel;
  const vector<float> &channel_data = *summed->gamma_counts();
  if( type == IntegerSpcType )
  {
    vector<uint32_t> int_channel_data( n_channel );
    for( uint16_t i = 0; i < n_channel; ++i )
      int_channel_data[i] = static_cast<uint32_t>( channel_data[i] );  //should we round instead of truncate?
    output.write( (const char *)&int_channel_data[0], 4*n_channel );
  }else
  {
    output.write( (const char *)&channel_data[0], 4*n_channel );
  }//if( file is integer channel data ) / else float data
  
  //If the number of channels was not a multiple of 32, write zeroes to finish
  //  filling out the 128 byte record.
  const uint16_t n_leftover = (n_channel % 32);
  
  for( uint16_t i = 0; i < n_leftover; ++i )
    pos += write_binary_data( output, zero_dword );
  
  return true;
}//bool write_binary_spc(...)
  
  
  
bool SpecFile::load_from_binary_spc( std::istream &input )
{
  /*
   This function was implemented by hand-decoding binary SPC files by wcjohns.
   However, I modified it a little bit when I found
   http://www.ortec-online.com/download/ortec-software-file-structure-manual.pdf
   (which I originally wasnt aware of until 20141107)
   
   //smallint is signed 2-byte integer
   //word is unsigned 32 bit integer
   //singe is a 4 byte float
   
   TSpcHdr = record
   1  1    wINFTYP: smallint; // Must be 1
   2  1    wFILTYP: smallint; // Must be 1 (integer SPC) or 5 (real SPC)
   4  2    wSkip1: array[1..2] of word;
   5  1    wACQIRP: smallint; // Acquisition information record pointer
   6  1    wSAMDRP: smallint; // Sample description record pointer
   7  1    wDETDRP: smallint; // Detector description record pointer
   16 9    wSKIP2: array[1..9] of word;
   17 1    wCALDES: smallint; // Calibration description record pointer
   18 1    wCALRP1: smallint; // First calibration data record pointer
   19 1    wCALRP2: smallint; // Second calibration data record pointer
   20 1    wEFFPRP: smallint; // Efficiency pairs record pointer (first record)
   21 1    wROIRP1: smallint; // Record number of the first of two ROI recs
   22 1    wEPRP: smallint; // Energy pairs record pointer
   23 1    wEPN: smallint; // Number of energy pairs records
   29 6    wSkip3: array[1..6] of word;
   30 1    wEFFPNM: smallint; // Number of efficiency pairs records
   31 1    wSPCTRP: smallint; // pointer to spectrum
   32 1    wSPCRCN: smallint; // number of records in sp
   33 1    wSPCCHN: word; // Number of Channels;
   34 1    wABSTCHN: smallint; // Physical start channel for data
   36 2    sACQTIM: single; // Date and time acquisition start in DECDAY format
   40 4    dACQTI8: double; // Date and time as double precision DECDAY
   44 4    wSkip4: array[1..4] of word;
   45 1    wCHNSRT: smallint; // Start channel number
   47 2    sRLTMDT: single; // Real time in seconds
   49 2    sLVTMDT: single; // Live time in seconds
   50 1    wSkip50: word;
   51 1           I*2     Pointer to FRAM records
   52 1           I*2     Pointer to TRIFID records
   53 1           I*2     Pointer to NaI records
   54 1           I*2     Pointer to Location records
   55 1           I*2     Number of channels of MCS data appended to the histogram data in this file
   56 1           I*2     Pointer to expansion header record (i.e. second header record)
   57-62                 Reserved (must be 0)
   63-64 RRSFCT  R*4     Total random summing factor
   end;
   This data is referenced from the second header record. You first must determine
   whether you have a second header record by reading word 56 of the first header record.
   If this word is non-zero then it points to the second header record which is just
   an extension of the first header record. The current File structures manual has
   an error starting with the description of word 53 of Record 1 in section 4.11.1.
   The current and correct definition for the last few words in record 1 is:
   
   WORD  NAME    Type   Description
   51            I*2     Pointer to FRAM records
   52            I*2     Pointer to TRIFID records
   53            I*2     Pointer to NaI records
   54            I*2     Pointer to Location records
   55            I*2     Number of channels of MCS data appended to the histogram data in this file
   56            I*2     Pointer to expansion header record (i.e. second header record)
   57-62                 Reserved (must be 0)
   63-64 RRSFCT  R*4     Total random summing factor
   
   The following is a description of the expansion header (second header) record
   
   WORD  Type   Description
   1           I*2      Record Identifier (should be decimal 111 for second header record)
   2           I*2      Pointer to GPS data record
   3           I*2      Pointer to 1st record of App specific report text. 1st 16-bits
   are the integer number of text bytes and the 2nd 16 bits are
   the integer report source code (bit 0=Detective-EX). Immediately
   following are the specified number of text bytes. This report
   may span several 128 byte records.
   4-64                Reserved (must be 0)
   
   The App specific report text record(s) are where the Detective-EX stores the
   identification report. The report starts with the fourth byte in the first record
   and continues for as many bytes as specified in the first 16 bit integer in the
   first record. The text is formatted with carriage returns and line feeds so that
   the block could be sent directly to a printer. The last line contains the neutron
   count rate from Detective units that have been updated to add that information
   to their report.
   
   The following is a description of the new entries in the hardware parameters records.
   
   To read the hardware records you must first read the first record to get the total
   number of records (Word 64 of the first hardware parameters record). Next you read
   records 2 through 4 assuming the total number of records is greater than or equal 4.
   If the total number of records is less than 4 then read only the specified number of records.
   
   Next read the 5 Multi-nuclide MDA Preset records if the total number of records
   is greater than or equal 9. The Multi-nuclide MDA Preset records are always complete
   so stop reading records after the first 4 if the total number of records is less
   than 9 (in other words, don't try to read a partial set of MDA Preset records).
   
   Finally, read the Monitors records if the total number of records is greater
   than 9. All remaining bytes in the record block are either stored monitor values
   or padding to round up to the next record. Monitors are stored as pairs of ASCII
   encoded, null terminated strings. The first string in a pair is the monitor label
   and the second string is the monitor value. All bytes after the last string pair
   should be set to zero to indicate zero length string pairs. This prevents garbage
   strings from being read from the file.
   
   The new stuff starts with hardware parameters record 4. The following describes
   parameters record 4:
   
   WORD   Type   Description
   1-4     bit    Validity flags
   5       I*2    Start delay in seconds (DART)
   6       I*2    Conserve delay in seconds (DART)
   7       I*2    Off delay in seconds (DART)
   8       I*2    Power mode (zero=On, nonzero=Conserve)
   9-10    I*4    Hardware status flags from SHOW_STATUS command
   11      I*2    LFR Enabled (zero=disabled, nonzero=enabled)
   12      I*2    ETP Enabled (zero=disabled, nonzero=enabled)
   13-14   R*4    ETP mode protection time in microseconds
   15-16   R*4    Actual HV in volts
   17      I*2    Resolution Enhancer Enabled (zero=disabled, nonzero=enabled)
   18-19   R*4    Manual PZ Setting in microseconds
   20-32          Unused (must be zero)
   33-64   C*16   Data view name strings (4 x 16-character strings, padded with spaces)
   
   If you wanted to include the GV analysis output, then you could read the UFO file.
   It has the same name as the spectrum file with the extension of UFO. The format is
   in the file manual. It isnít complicated, but is another table-driven content file.
   */
  
  reset();
  
  if( !input.good() )
    return false;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const istream::pos_type orig_pos = input.tellg();
  
  try
  {
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( orig_pos, ios::beg );
    
    const size_t size = static_cast<size_t>( 0 + eof_pos - orig_pos );
    
    bool foundNeutronDet = false;
    string latitudeStr, longitudeStr;
    
    //As painful as this is, I will read in each variable below individually
    //  rather than using a struct, because packing may be an issue
    //  see http://stackoverflow.com/questions/7789668/why-would-the-size-of-a-packed-structure-be-different-on-linux-and-windows-when
    int16_t wINFTYP; // Must be 1
    int16_t wFILTYP; // Must be 1 (integer SPC) or 5 (real SPC)
    int16_t wSkip1[2];
    int16_t wACQIRP; // Acquisition information record pointer
    int16_t wSAMDRP; // Sample description record pointer
    int16_t wDETDRP; // Detector description record pointer
    int16_t wSKIP2[9];
    int16_t wCALDES; // Calibration description record pointer
    int16_t wCALRP1; // First calibration data record pointer
    int16_t wCALRP2; // Second calibration data record pointer
    int16_t wEFFPRP; // Efficiency pairs record pointer (first record)
    int16_t wROIRP1; // Record number of the first of two ROI recs
    int16_t wEPRP; // Energy pairs record pointer
    int16_t wEPN; // Number of energy pairs records
    int16_t wSkip3[6];
    int16_t wEFFPNM; // Number of efficiency pairs records
    int16_t wSPCTRP; // pointer to spectrum
    int16_t wSPCRCN; // number of records in sp
    uint16_t n_channel; // Number of Channels;
    int16_t wABSTCHN; // Physical start channel for data
    float sACQTIM; // Date and time acquisition start in DECDAY format
    double dACQTI8; // Date and time as double precision DECDAY
    int16_t wSkip4[4];
    int16_t wCHNSRT; // Start channel number
    float sRLTMDT; // Real time in seconds
    float sLVTMDT; // Live time in seconds
    int16_t wSkip50;
    int16_t framRecords;//     Pointer to FRAM records
    int16_t TRIFID; // I*2     Pointer to TRIFID records
    int16_t NaI; //I*2     Pointer to NaI records
    int16_t Location;// I*2     Pointer to Location records
    int16_t MCSdata; //I*2     Number of channels of MCS data appended to the histogram data in this file
    int16_t expansionHeader;//     Pointer to expansion header record (i.e. second header record)
    int16_t reserved[5]; // 57-62                 Reserved (must be 0)
    int16_t RRSFCT[2];//  R*4     Total random summing factor
    
    read_binary_data( input, wINFTYP ); // Must be 1
    if( wINFTYP != 1 )
      throw runtime_error( "First byte indicates not a binary SPC file" );
    
    read_binary_data( input, wFILTYP ); // Must be 1 (integer SPC) or 5 (real SPC)
    if( wFILTYP != 1 && wFILTYP != 5 )
      throw runtime_error( "Second byte indicates not a binary SPC file" );
    
    input.read( (char *)&wSkip1[0], 2*2 );
    read_binary_data( input, wACQIRP ); // Acquisition information record pointer
    read_binary_data( input, wSAMDRP ); // Sample description record pointer
    read_binary_data( input, wDETDRP ); // Detector description record pointer
    input.read( (char *)&wSKIP2[0], 2*9 );
    read_binary_data( input, wCALDES ); // Calibration description record pointer
    read_binary_data( input, wCALRP1 ); // First calibration data record pointer
    read_binary_data( input, wCALRP2 ); // Second calibration data record pointer
    read_binary_data( input, wEFFPRP ); // Efficiency pairs record pointer (first record)
    read_binary_data( input, wROIRP1 ); // Record number of the first of two ROI recs
    read_binary_data( input, wEPRP ); // Energy pairs record pointer
    read_binary_data( input, wEPN ); // Number of energy pairs records
    input.read( (char *)&wSkip3[0], 2*6 );
    read_binary_data( input, wEFFPNM ); // Number of efficiency pairs records
    read_binary_data( input, wSPCTRP ); // pointer to spectrum
    read_binary_data( input, wSPCRCN ); // number of records in sp
    read_binary_data( input, n_channel ); // Number of Channels;
    
    if( (32u*static_cast<uint32_t>(wSPCRCN)) < n_channel )
      throw runtime_error( "Not enough records for claimed number of channels" );
    
    read_binary_data( input, wABSTCHN ); // Physical start channel for data
    read_binary_data( input, sACQTIM ); // Date and time acquisition start in DECDAY format
    read_binary_data( input, dACQTI8 ); // Date and time as double precision DECDAY
    input.read( (char *)&wSkip4[0], 2*4 );
    read_binary_data( input, wCHNSRT ); // Start channel number
    read_binary_data( input, sRLTMDT ); // Real time in seconds
    read_binary_data( input, sLVTMDT ); // Live time in seconds
    read_binary_data( input, wSkip50 );
    read_binary_data( input, framRecords );//     Pointer to FRAM records
    read_binary_data( input, TRIFID ); // I*2     Pointer to TRIFID records
    read_binary_data( input, NaI ); //I*2     Pointer to NaI records
    read_binary_data( input, Location );// I*2     Pointer to Location records
    read_binary_data( input, MCSdata ); //I*2     Number of channels of MCS data appended to the histogram data in this file
    read_binary_data( input, expansionHeader );//     Pointer to expansion header record (i.e. second header record)
    input.read( (char *)&reserved[0], 2*5 );
    input.read( (char *)&RRSFCT[0], 2*2 );
    
    if( !input.good() )
      throw runtime_error( "Error reading header data" );
    
    /*
     cout << "wINFTYP=" <<wINFTYP << endl;
     cout << "wINFTYP=" <<wINFTYP << endl;
     cout << "wACQIRP=" <<wACQIRP << endl;
     cout << "wSAMDRP=" << wSAMDRP<< endl;
     cout << "wDETDRP=" <<wDETDRP << endl;
     cout << "wCALDES=" << wCALDES<< endl;
     cout << "wCALRP1=" << wCALRP1<< endl;
     cout << "wCALRP2=" <<wCALRP2 << endl;
     cout << "wEFFPRP=" << wEFFPRP<< endl;
     cout << "wROIRP1=" << wROIRP1<< endl;
     cout << "wEPRP=" << wEPRP<< endl;
     cout << "wEPN=" <<wEPN << endl;
     cout << "wEFFPNM=" <<wEFFPNM << endl;
     cout << "wSPCTRP=" <<wSPCTRP << endl;
     cout << "wSPCRCN=" <<wSPCRCN << endl;
     cout << "n_channel=" << n_channel<< endl;
     cout << "wABSTCHN=" << wABSTCHN<< endl;
     cout << "sACQTIM=" << sACQTIM<< endl;
     cout << "dACQTI8=" << dACQTI8<< endl;
     cout << "wCHNSRT=" << wCHNSRT<< endl;
     cout << "sRLTMDT=" <<sRLTMDT << endl;
     cout << "sLVTMDT=" << sLVTMDT<< endl;
     cout << "wSkip50=" << wSkip50<< endl;
     cout << "framRecords=" <<framRecords << endl;
     cout << "TRIFID=" << TRIFID<< endl;
     cout << "NaI=" << NaI<< endl;
     cout << "Location=" << Location<< endl;
     cout << "MCSdata=" <<MCSdata << endl;
     cout << "expansionHeader=" << expansionHeader<< endl;
     cout << "RRSFCT[0]=" << RRSFCT[0] << endl;
     cout << "RRSFCT[1]=" << RRSFCT[1] << endl;
     */
    
    //calibration data
    vector<float> calib_coefs;
    if( wCALRP1 > 0 )
    {
      input.seekg( (wCALRP1-1)*128 + orig_pos, ios::beg );
      int16_t wAFIT; // 2 Above knee efficiency calibration type
      int16_t wBFIT; // 4 Below knee efficiency calibration type
      int16_t wEFFPRS; // 6 Number of efficiency pairs
      int16_t wNCH; // 8 number of channels in spectrum
      float sKNEE; // 12 Detector knee (keV)
      float sASIG; // 16 2-sigma uncertainty above knee
      float sBSIG; // 20 2-sigma uncertainty below knee
      float sEC1; // 24 Energy vs. channel coefficient A
      float sEC2; // 28 Energy vs. channel coefficient B
      float sEC3; // 32 Energy vs. channel coefficient C
      float sFC1; //FWHM vs. channel coefficient A (actually in most file)
      float sFC2; //FWHM vs. channel coefficient B (actually in most file)
      float sFC3; //FWHM vs. channel coefficient C (actually in most file)
      float sPE1; //Above knee eff. vs. energy coeff A or poly coeff 1
      float sPE2; //Above knee eff. vs. energy coeff B or poly coeff 2
      float sPE3; //Above knee eff. vs. energy coeff C or poly coeff 3
      float sSE1; //Below knee eff. vs. energy coeff A or poly coeff 4
      float sSE2; //Below knee eff. vs. energy coeff B or poly coeff 5
      float sSE3; //Below knee eff. vs. energy coeff C or poly coeff 6
      int16_t wFWHTYP; // FWHM type
      int16_t wRES1; // reserved
      int16_t wRES2; // reserved
      int16_t wENGPRS; // Number of energy pairs
      int16_t wDETNUM; //Detector Number
      int16_t wNBKNEE; // Number of calibration points below knee
      float sENA2; // Temp energy calibration
      float sENB2; // Temp energy calibration
      float sENC2; // Temp energy calibration
      float sCALUNC; //Calibration source uncertainty
      float sCALDIF; // Energy calibration difference
      float sR7; // Polynomial coefficient 7
      float sR8; // Polynomial coefficient 8
      float sR9; // Polynomial coefficient 9
      float sR10; // Polynomial coefficient 10
      
      
      read_binary_data( input, wAFIT ); // 2 Above knee efficiency calibration type
      read_binary_data( input, wBFIT ); // 4 Below knee efficiency calibration type
      read_binary_data( input, wEFFPRS ); // 6 Number of efficiency pairs
      read_binary_data( input, wNCH ); // 8 number of channels in spectrum
      read_binary_data( input, sKNEE ); // 12 Detector knee (keV)
      read_binary_data( input, sASIG ); // 16 2-sigma uncertainty above knee
      read_binary_data( input, sBSIG ); // 20 2-sigma uncertainty below knee
      read_binary_data( input, sEC1 ); // 24 Energy vs. channel coefficient A
      read_binary_data( input, sEC2 ); // 28 Energy vs. channel coefficient B
      read_binary_data( input, sEC3 ); // 32 Energy vs. channel coefficient C
      read_binary_data( input, sFC1 ); //FWHM vs. channel coefficient A (actually in most file)
      read_binary_data( input, sFC2 ); //FWHM vs. channel coefficient B (actually in most file)
      read_binary_data( input, sFC3 ); //FWHM vs. channel coefficient C (actually in most file)
      read_binary_data( input, sPE1 ); //Above knee eff. vs. energy coeff A or poly coeff 1
      read_binary_data( input, sPE2 ); //Above knee eff. vs. energy coeff B or poly coeff 2
      read_binary_data( input, sPE3 ); //Above knee eff. vs. energy coeff C or poly coeff 3
      read_binary_data( input, sSE1 ); //Below knee eff. vs. energy coeff A or poly coeff 4
      read_binary_data( input, sSE2 ); //Below knee eff. vs. energy coeff B or poly coeff 5
      read_binary_data( input, sSE3 ); //Below knee eff. vs. energy coeff C or poly coeff 6
      read_binary_data( input, wFWHTYP ); // FWHM type
      read_binary_data( input, wRES1 ); // reserved
      read_binary_data( input, wRES2 ); // reserved
      read_binary_data( input, wENGPRS ); // Number of energy pairs
      read_binary_data( input, wDETNUM ); //Detector Number
      read_binary_data( input, wNBKNEE ); // Number of calibration points below knee
      read_binary_data( input, sENA2 ); // Temp energy calibration
      read_binary_data( input, sENB2 ); // Temp energy calibration
      read_binary_data( input, sENC2 ); // Temp energy calibration
      read_binary_data( input, sCALUNC ); //Calibration source uncertainty
      read_binary_data( input, sCALDIF ); // Energy calibration difference
      read_binary_data( input, sR7 ); // Polynomial coefficient 7
      read_binary_data( input, sR8 ); // Polynomial coefficient 8
      read_binary_data( input, sR9 ); // Polynomial coefficient 9
      read_binary_data( input, sR10 ); // Polynomial coefficient 10
      
      calib_coefs.push_back( sEC1 );
      calib_coefs.push_back( sEC2 );
      calib_coefs.push_back( sEC3 );
      
      /*
       cout << "wAFIT=" <<wAFIT << endl;
       cout << "wBFIT=" <<wBFIT << endl;
       cout << "wEFFPRS=" <<wEFFPRS << endl;
       cout << "wNCH=" << wNCH<< endl;
       cout << "sKNEE=" <<sKNEE << endl;
       cout << "sASIG=" <<sASIG << endl;
       cout << "sBSIG=" <<sBSIG << endl;
       cout << "sEC1=" <<sEC1 << endl;
       cout << "sEC2=" <<sEC2 << endl;
       cout << "sEC3=" <<sEC3 << endl;
       cout << "sFC1=" <<sFC1 << endl;
       cout << "sFC2=" <<sFC2 << endl;
       cout << "sFC3=" <<sFC3 << endl;
       cout << "sPE1=" <<sPE1 << endl;
       cout << "sPE2=" <<sPE2 << endl;
       cout << "sPE3=" <<sPE3 << endl;
       cout << "sSE1=" <<sSE1 << endl;
       cout << "sSE2=" <<sSE2 << endl;
       cout << "sSE3=" <<sSE3 << endl;
       cout << "wFWHTYP=" <<wFWHTYP << endl;
       cout << "wRES1=" <<wRES1 << endl;
       cout << "wRES2=" <<wRES2 << endl;
       cout << "wENGPRS=" <<wENGPRS << endl;
       cout << "wDETNUM=" <<wDETNUM << endl;
       cout << "wNBKNEE=" <<wNBKNEE << endl;
       cout << "sENA2=" <<sENA2 << endl;
       cout << "sENB2=" <<sENB2 << endl;
       cout << "sENC2=" <<sENC2 << endl;
       cout << "sCALUNC=" <<sCALUNC << endl;
       cout << "sCALDIF=" <<sCALDIF << endl;
       cout << "sR7=" <<sR7 << endl;
       cout << "sR8=" <<sR8 << endl;
       cout << "sR9=" <<sR9 << endl;
       cout << "sR10=" << sR10<< endl;
       */
    }//if( wCALRP1 > 0 )
    
    if( !input.good() )
      throw runtime_error( "Error reading calibration data" );
    
    string instrument_id;
    double sum_gamma = 0.0;
    double total_neutrons = 0.0;
    float total_neutron_count_time = 0.0;
    std::shared_ptr<DetectorAnalysis> analysis;
    auto channel_data = make_shared<vector<float>>( n_channel, 0.0f );
    
    
    boost::posix_time::ptime meas_time;
    string manufacturer = "Ortec";
    string inst_model = "Detective";
    string type_instrument = "RadionuclideIdentifier";
    DetectorType type_detector = DetectorType::Unknown;
    
    
    {//begin codeblock to get acquisition information
      char namedata[17], datedata[19];
      datedata[18] = namedata[16] = '\0';
      datedata[9] = ' ';
      
      
      //Not working correctly - f-it for right now
      if( wACQIRP > 0 )
      {
        input.seekg( 128*(wACQIRP-1) + orig_pos, ios::beg );
        input.read( namedata, 16 );
        input.read( datedata, 9 );
        input.read( datedata+10, 3 );  //just burning off 3 bytes
        input.read( datedata+10, 8 );
        
        if( !input.good() )
          throw runtime_error( "Didnt successfully read date data" );
        
        string name( namedata, namedata+16 );
        trim( name );
        
        
        //name seems to always be 'DetectiveEX.SPC'
        if( istarts_with( name, "Detective" ) )
        {
          type_instrument = "Radionuclide Identifier";
          manufacturer = "Ortec";
          type_detector = DetectorType::DetectiveUnknown;
        }//if( istarts_with( name, "DetectiveEX" ) )
        
        try
        {
          meas_time = time_from_string( datedata );
          //cout << "meas_time=" << SpecUtils::to_iso_string( meas_time ) << endl;
        }catch(...)
        {
          cerr << "SpecFile::loadBinarySpcFile(...): invalid date string: "
          << datedata << endl;
        }
        
        /*
         cout << "name=" << name << endl;
         cout << "datedata='" << datedata << "'" << endl;
         */
      }//if( wACQIRP > 0 )
      
      if( wACQIRP > 0 )
      {
        input.seekg( 128*(wACQIRP-1) + orig_pos + 90, ios::beg );
        char start_date_of_sample_collection[11] = { '\0' };
        char start_time_of_sample_collection[9] = { '\0' };
        char stop_date_of_sample_collection[11] = { '\0' };
        char stop_time_of_sample_collection[9] = { '\0' };
        input.read( start_date_of_sample_collection, 10 );
        char dummy[2];
        input.read( dummy, 2 );
        input.read( start_time_of_sample_collection, 8 );
        input.read( stop_date_of_sample_collection, 10 );
        input.read( stop_time_of_sample_collection, 8 );
        
        /*
         cout << "start_date_of_sample_collection='" << start_date_of_sample_collection << "'" << endl;
         cout << "start_time_of_sample_collection='" << start_time_of_sample_collection << "'" << endl;
         cout << "stop_date_of_sample_collection='" << stop_date_of_sample_collection << "'" << endl;
         cout << "stop_time_of_sample_collection='" << stop_time_of_sample_collection << "'" << endl;
         */
      }//if( wACQIRP > 0 )
    }//end codeblock to get acquisition information
    
    //TODO: look for the following strings and include in results
    //State Of Health^@OK
    //Gamma Dose Rate^@0.07 ?Sv/h
    //  [Ge]^@Detector Temperature^@OK
    //  Battery Voltage^@15.37 Volts
    //  Battery Time Remaining^@125 Min
    //  Cooler Body Temperature^@OK
    //  Cooler Drive Voltage^@OK
    //  Cold-Tip Temperature^@OK
    //  HV Bias^@-3509 V
    
    if( expansionHeader )
    {
      input.seekg( 128*(expansionHeader-1) + orig_pos, ios::beg );
      
      if( !input.good() )
      {
        stringstream msg;
        msg << "Unable to read expansion header in file, possible pointer "
        << expansionHeader << " (location " << 128*(expansionHeader-1)
        << " of size=" << size << ")" << endl;
        throw runtime_error( msg.str() );
      }//if( !input.good() )
      
      int16_t recordID, gpsPointer, firstReportPtr;
      read_binary_data( input, recordID );
      read_binary_data( input, gpsPointer );
      read_binary_data( input, firstReportPtr );
      
      if( recordID != 111 )
      {
        gpsPointer = firstReportPtr = 0;
        cerr << "Binary SPC file has invalid expansion header" << endl;
      }//if( recordID != 111 )
      
      if( gpsPointer )
      {
        //See refD3BAVOI7JG
        //        input.seekg( 128*(gpsPointer-1) + orig_pos, ios::beg );
        
        //        if( input.good() )
        //        {
        //          uint16_t ntxtbytes;
        //          read_binary_data( input, ntxtbytes );
        //
        //          cerr << "ntxtbytes=" << ntxtbytes << endl;
        //          ntxtbytes = std::min(ntxtbytes, uint16_t(120));
        //          vector<char> data(ntxtbytes+1);
        //          data[ntxtbytes] = '\0';
        //          input.read( &data[0], ntxtbytes );
        //          for( size_t i = 0; i < ntxtbytes; ++i )
        //            cout << i << ": " << int(data[i]) << ", '" << data[i] << "'" << endl;
        //        }else
        //        {
        //          cerr << "Failed to be able to read GPS REcord" << endl;
        //        }
        cerr << "Binary SPC file has not yet implemented GPS coordinates decoding" << endl;
      }
      
      
      if( firstReportPtr > 0 )
      {
        const auto curr_pos = 128*static_cast<int>(firstReportPtr-1) + orig_pos;
        
        input.seekg( curr_pos, ios::beg );
        if( !input.good() )
        {
          stringstream msg;
          msg << "Unable to read report in file, possible bad report pointer "
          << firstReportPtr << " (location " << 128*(firstReportPtr-1)
          << " of size=" << size << ")" << endl;
          throw runtime_error( msg.str() );
        }//if( !input.good() )
        
        uint16_t ntxtbytes, sourcecode;
        read_binary_data( input, ntxtbytes );
        read_binary_data( input, sourcecode );
        
        if( (static_cast<istream::pos_type>(size) > (curr_pos+4)) && (static_cast<int>(ntxtbytes) > (size - curr_pos - 4)) )
          ntxtbytes = (size - curr_pos - 4);
        
        if( ntxtbytes > 2048 )  //20190604: is 2048 a randomly picked number, or the expansion header max size
          ntxtbytes = 0;
        
        if( ntxtbytes > 0 )
        {
          vector<char> data(ntxtbytes+1);
          data[ntxtbytes] = '\0';
          input.read( &data[0], ntxtbytes );
          
          string term;
          
          {//being codeblock to look for neutrons
            //Apparently capitalization isnt consistent, so will convert to
            //  lowercase; I didnt test yet for longitude/latitude, or nuclide
            //  IDs, so I dont want to convert to lower case for those yet.
            string datastr( data.begin(), data.end() );
            SpecUtils::to_lower_ascii( datastr );
            
            term = "total neutron counts = ";
            string::const_iterator positer, enditer;
            positer = std::search( datastr.begin(), datastr.end(),
                                  term.begin(), term.end() );
            if( positer != datastr.end() )
            {
              term = "total neutron counts = ";
              positer = std::search( datastr.begin(), datastr.end(),
                                    term.begin(), term.end() );
              foundNeutronDet = true;
              total_neutrons = atof( &(*(positer+term.size())) );
            }else
            {
              term = "neutron counts";
              positer = std::search( datastr.begin(), datastr.end(),
                                    term.begin(), term.end() );
              if( positer != datastr.end() )
              {
                foundNeutronDet = true;
                //                positer += 17;
                total_neutrons = atof( &(*(positer+term.size())) ); //atof( &(*positer) );
              }
            }
            
            
            term = "total neutron count time = ";
            positer = std::search( datastr.begin(), datastr.end(),
                                  term.begin(), term.end() );
            if( positer != datastr.end() )
            {
              foundNeutronDet = true;
              total_neutron_count_time = static_cast<float>( atof( &(*positer) + 27 ) );
            }
          }//end codeblock to look for neutrons
          
          
          
          //Other strings to look for:
          //"Found Nuclides"
          //"Suspect Nuclides"
          //"Top Lines"
          //"GPS"  or something like "GPS Location not determined"
          //"Gamma Dose Rate"
          //"Version"
          //"ID Report"
          //"Total neutron count time = "
          //"Average neutron count rate = "
          //
          //Maybye look for "ID Report"
          
          vector<char>::iterator positer, enditer;
          term = "Latitude";
          positer = std::search( data.begin(), data.end(),
                                term.begin(), term.end() );
          if( positer != data.end() )
          {
            positer += 8;
            while( (positer!=data.end()) && !isdigit(*positer) )
              ++positer;
            enditer = positer;
            while( (enditer!=data.end()) && (*enditer)!='\n' )
              ++enditer;
            latitudeStr.insert(latitudeStr.end(), positer, enditer );
            latitudeStr.erase( std::remove_if(latitudeStr.begin(), latitudeStr.end(), not_alpha_numeric), latitudeStr.end());
          }//if( there is latitude info )
          
          term = "Longitude";
          positer = std::search( data.begin(), data.end(),
                                term.begin(), term.end() );
          if( positer != data.end() )
          {
            positer += 9;
            while( (positer!=data.end()) && !isdigit(*positer) )
              ++positer;
            enditer = positer;
            while( (enditer!=data.end()) && (*enditer)!='\n' )
              ++enditer;
            longitudeStr.insert(longitudeStr.end(), positer, enditer );
            longitudeStr.erase(std::remove_if(longitudeStr.begin(), longitudeStr.end(), not_alpha_numeric), longitudeStr.end());
          }//if( there is longitude info )
          
          
          string found_term = "Found Nuclides";
          vector<char>::iterator nucpos = std::search( data.begin(), data.end(),
                                                      found_term.begin(), found_term.end() );
          if( nucpos == data.end() )
          {
            found_term = "Found:";
            nucpos = std::search( data.begin(), data.end(), found_term.begin(), found_term.end() );
            
            //Found: actually looks a little different, ex:
            //  Found: Industrial(2)  NORM(1) Other(2)^M
            //  Co60    Co57    Mn54    K40     Co56
            //XXX TODO: Which right now were putting Industrial/NORM/OTHER as
            //          their own analysis results, in addition to the actual
            //          nuclides. Should fix.
          }
          
          if( nucpos != data.end() )
          {
            if( !analysis )
              analysis = std::make_shared<DetectorAnalysis>();
            
            //Should reformat this list seperated by newlines to csv or somehting
            //  and also I dont know if "Suspect Nuclides" is garunteed to be there
            string suspect_term = "Suspect Nuclides";
            vector<char>::iterator suspectpos = std::search( nucpos, data.end(),
                                                            suspect_term.begin(), suspect_term.end() );
            if( suspectpos == data.end() )
            {
              suspect_term = "Suspect:";
              suspectpos = std::search( nucpos, data.end(), suspect_term.begin(), suspect_term.end() );
            }
            
            
            string found_nucs_str( nucpos+found_term.size(), suspectpos );
            vector<string> found_nucs, suspect_nucs;
            split( found_nucs, found_nucs_str, "\t,\n\r\0");
            
            for( string &nuc : found_nucs )
            {
              nuc.erase(std::remove_if(nuc.begin(), nuc.end(), not_alpha_numeric), nuc.end());
              trim( nuc );
              ireplace_all( nuc, "  ", " " );
              
              if( icontains( nuc, "keep counting" ) )
              {
                remarks_.push_back( nuc );
              }else if( nuc.size() )
              {
                DetectorAnalysisResult result;
                result.remark_ = "Found";
                //result.id_confidence_ = "Found";
                result.nuclide_ = nuc;
                analysis->results_.push_back( result );
              }
            }//for( string &nuc : found_nucs )
            
            const string lines_term = "Top Lines";
            vector<char>::iterator linesiter = std::search( suspectpos, data.end(),
                                                           lines_term.begin(), lines_term.end() );
            
            if( suspectpos != data.end() )
            {
              string suspect_nucs_str = string( suspectpos+suspect_term.size(), linesiter );
              string::size_type endpos = suspect_nucs_str.find_first_of("\0");
              if( endpos != string::npos )
                suspect_nucs_str.substr(0,endpos);
              
              split( suspect_nucs, suspect_nucs_str, "\t,\n\r\0" );
              for( string &nuc : suspect_nucs )
              {
                nuc.erase( std::remove_if(nuc.begin(), nuc.end(), not_alpha_numeric), nuc.end());
                trim( nuc );
                ireplace_all( nuc, "  ", " " );
                
                if( icontains( nuc, "keep counting" ) )
                {
                  remarks_.push_back( nuc );
                }else if( nuc.size() )
                {
                  DetectorAnalysisResult result;
                  result.remark_ = "Suspect";
                  //result.id_confidence_ = "Suspect";
                  result.nuclide_ = nuc;
                  analysis->results_.push_back( result );
                }
              }//for( string &nuc : found_nucs )
            }//if( suspectpos != data.end() )
            
            //               const char *dose_rate = strstr( &data[0], "Gamma Dose Rate" );
            //               if( dose_rate )
            //               cerr << "Found dose rate: " << dose_rate << endl << endl;
            
            if( (data.end() - (linesiter+lines_term.size())) > 0 )
            {
              string toplines( linesiter+10, data.end() );
              vector<string> lines;
              split( lines, toplines, "\r\n" );
              
              for( size_t i = 0; i < lines.size(); ++i )
              {
                //                 lines[i].erase( std::remove_if(lines[i].begin(), lines[i].end(), not_alpha_numeric), lines[i].end());
                trim( lines[i] );
                ireplace_all( lines[i], "  ", " " );
                ireplace_all( lines[i], "\t", "&#009;" );  //replace tab characters with tab character code
                
                if( lines[i].empty()
                   || istarts_with(lines[i], "Longitude")
                   || istarts_with(lines[i], "GPS") )
                  break;
                
                char buffer[256];
                snprintf( buffer, sizeof(buffer), "Top Line %i: %s", int(i), lines[i].c_str() );
                analysis->remarks_.push_back( buffer );
              }//for( size_t i = 0; i < lines.size(); ++i )
            }//if( there are top lines )
          }//if( nucpos != data.end() )
        }//if( ntxtbytes )
        
      }//if( firstReportPtr )
    }//if( expansionHeader )
    
    
    if( wSAMDRP > 0 )
    {
      input.seekg( 128*(wSAMDRP-1) + orig_pos, ios::beg );
      vector<char> data(128);
      input.read( &data[0], 128 );
      
      data.erase(std::remove_if(data.begin(), data.end(), not_alpha_numeric), data.end());
      
      string remark( data.begin(), data.end() );
      trim( remark );
      if( remark.size() )
        remarks_.push_back( "Sample Description: " + remark );
    }//if( wSAMDRP )
    
    if( wDETDRP > 0 && input.seekg(128*(wDETDRP-1) + orig_pos, ios::beg) )
    {
      input.seekg( 128*(wDETDRP-1) + orig_pos, ios::beg );
      vector<char> data( 128, '\0' );
      input.read( &data[0], 128 );
      data.erase(std::remove_if(data.begin(), data.end(), not_alpha_numeric), data.end());
      
      if( data.size() )
      {
        data.push_back( '\0' );
        instrument_id = &data[0];
        size_t len = instrument_id.find_last_not_of( " \0\t" );
        if( len != string::npos )
          instrument_id = instrument_id.substr( 0, len + 1 );
        else
          instrument_id = "";
      }//if( data.size() )
      
      trim( instrument_id );
      ireplace_all( instrument_id, "\n", " " );
      ireplace_all( instrument_id, "\r", " " );
      ireplace_all( instrument_id, "  ", " " );
      
      //TODO: Some instrument_id will have format like: "EX100T 16145691 1614569126 APR 5"
      //  which the actual serial number would be "16145691".  This should be fixed.
      
      
      //Some detective EX100s, like in ref49KB84PGM4, have the serial number in
      //  not in the standard position (but may have the model in the standard
      //  position), so we will attempt to check this out and add the SN.
      vector<int16_t> calibrationpos;
      if( wCALRP1 > 0 )
        calibrationpos.push_back( wCALRP1 );
      if( wCALRP2 > 0 )
        calibrationpos.push_back( wCALRP2 );
      if( wCALDES > 0 )
        calibrationpos.push_back( wCALDES );
      
      for( size_t i = 0; i < calibrationpos.size(); ++i )
      {
        vector<char> data(128);
        
        if( !input.seekg(128*(calibrationpos[i]-1) + orig_pos, ios::beg) )
          continue;
        if( !input.read( &data[0], 128 ) )
          continue;
        
        data.push_back( '\0' );
        string strdata = &data[0];
        size_t pos = strdata.find( "SN:" );
        if( pos != string::npos )
        {
          strdata = strdata.substr( pos+3 );
          pos = strdata.find_last_not_of( " \0\t" );
          if( pos != string::npos )
            strdata = strdata.substr( 0, pos + 1 );
          strdata.erase(std::remove_if(strdata.begin(), strdata.end(), not_alpha_numeric), strdata.end());
          ireplace_all( strdata, "\n", " " );
          ireplace_all( strdata, "\r", " " );
          ireplace_all( strdata, "  ", " " );
          trim( strdata );
          if( strdata.length() && (instrument_id.find(strdata)==string::npos) )
            instrument_id += (instrument_id.size() ? " " : "") + strdata;
        }//if( pos != string::npos )
      }//for( size_t i = 0; i < calibrationpos.size(); ++i )
      
      SerialToDetectorModel::DetectorModel model = SerialToDetectorModel::detective_model_from_serial( instrument_id );
      
      if( model == SerialToDetectorModel::DetectorModel::UnknownSerialNumber
         || model == SerialToDetectorModel::DetectorModel::Unknown
         || model == SerialToDetectorModel::DetectorModel::NotInitialized )
      {
        model = SerialToDetectorModel::guess_detective_model_from_serial( instrument_id );
      }
      
      switch( model )
      {
        case SerialToDetectorModel::DetectorModel::Unknown:
        case SerialToDetectorModel::DetectorModel::NotInitialized:
        case SerialToDetectorModel::DetectorModel::UnknownSerialNumber:
          type_detector = DetectorType::DetectiveUnknown;
          inst_model = "Detective";
          break;
          
        case SerialToDetectorModel::DetectorModel::MicroDetective:
          type_detector = DetectorType::MicroDetective;
          inst_model = "MicroDetective";
          break;
          
        case SerialToDetectorModel::DetectorModel::DetectiveEx:
          type_detector = DetectorType::DetectiveEx;
          inst_model = foundNeutronDet ? "DetectiveEX" : "DetectiveDX";
          break;
          
        case SerialToDetectorModel::DetectorModel::DetectiveEx100:
          type_detector = DetectorType::DetectiveEx100;
          inst_model = foundNeutronDet ? "DetectiveEX100" : "DetectiveDX100";
          break;
          
        case SerialToDetectorModel::DetectorModel::Detective200:
          type_detector = DetectorType::DetectiveEx200;
          inst_model = "Detective200";
          break;
          
        case SerialToDetectorModel::DetectorModel::DetectiveX:
          type_detector = DetectorType::DetectiveX;
          inst_model = "Detective X";
          break;
      }//switch( model )
      
    }//if( wDETDRP > 0 && input.seekg(128*(wDETDRP-1) + orig_pos, ios::beg) )
    
    //    cout << instrument_id << ": " << sFC1 << ", " << sFC2 << ", " << sFC3 << endl;
    
    //
    //    cerr << "instrument_id=" << instrument_id << endl;
    /*
     const char *note = &(data[0])+384;
     const char *end_note = strstr( note, "\r" );
     if( (end_note - note) > 256 )
     end_note = note + 256;
     string remark = string( note, end_note );
     trim( remark );
     */
    
    //read in channel data
    vector<float> &counts_ref = *channel_data;
    input.seekg( 128*(wSPCTRP-1) + orig_pos, ios::beg );
    
    if( !input.good() )
      throw runtime_error( "Unable to read channel data" );
    
    const size_t last_expected = static_cast<size_t>( 4*n_channel + 128*(wSPCTRP-1) + orig_pos );
    if( last_expected > size_t(12+eof_pos) )  //12 is a just in case...
      throw runtime_error( "File not expected size" );
    
    if( wFILTYP == 1 )
    {
      vector<uint32_t> int_channel_data( n_channel );
      input.read( (char *)&int_channel_data[0], 4*n_channel );
      
      for( size_t i = 0; i < n_channel; ++i )
        counts_ref[i] = static_cast<float>( int_channel_data[i] );
    }else //if( wFILTYP == 5 )
    {
      input.read( (char *) &(counts_ref[0]), 4*n_channel );
    }//if( file is integer channel data ) / else float data
    
    counts_ref[0] = 0;
    counts_ref[n_channel-1] = 0;
    
    for( size_t i = 0; i < n_channel; ++i )
      sum_gamma += counts_ref[i];
    
    manufacturer_       = manufacturer;
    instrument_type_    = type_instrument;
    instrument_model_   = inst_model;
    detector_type_      = type_detector;
    detectors_analysis_ = analysis;
    instrument_id_      = instrument_id;
    
    std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
    
    if( sLVTMDT < 0.0 || sRLTMDT < 0.0 )
      throw runtime_error( "Invalid real or live time" );
    
    meas->live_time_ = sLVTMDT;
    meas->real_time_ = sRLTMDT;
    meas->start_time_ = meas_time;
    meas->gamma_counts_ = channel_data;
    meas->gamma_count_sum_ = sum_gamma;
    
    assert( channel_data );
    if( channel_data->size() > 1 )
    {
      try
      {
        auto newcal = make_shared<EnergyCalibration>();
        newcal->set_polynomial( channel_data->size(), calib_coefs, {} );
        meas->energy_calibration_ = newcal;
      }catch( std::exception &e )
      {
        meas->parse_warnings_.push_back( "Invalid SPC energy cal provided: " + string(e.what()) );
      }
    }//if( channel_data->size() > 1 )
    
    //File ref9HTGHJ9SXR has the neutron information in it, but
    //  the serial number claims this is a micro-DX (no neutron detector), and
    //  a found nuclide is neutron on hydrogen, but yet no neutrons are actually
    //  detected.  This implies we should check if the serial number contains
    //  "DX" and no neutrons are detected, then set meas->contained_neutron_
    //  to false.
    //  Not doing this now due to ambiguity.
    meas->contained_neutron_ = foundNeutronDet;
    meas->neutron_counts_sum_ = total_neutrons;
    if( foundNeutronDet || (total_neutrons>0.0) )
      meas->neutron_counts_.push_back( static_cast<float>(total_neutrons) );
    
    if( total_neutron_count_time > 0.0 )
    {
      char buffer[128];
      snprintf( buffer, sizeof(buffer),
               "Total neutron count time = %f seconds", total_neutron_count_time );
      meas->remarks_.push_back( buffer );
    }
    
    
    if( longitudeStr.size() && latitudeStr.size() )
    {
      meas->latitude_ = conventional_lat_or_long_str_to_flt(latitudeStr);
      meas->longitude_ = conventional_lat_or_long_str_to_flt(longitudeStr);
    }//if( longitudeStr.size() && latitudeStr.size() )
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
  }catch( std::exception &e )
  {
#if(PERFORM_DEVELOPER_CHECKS)
    cerr  << "SpecFile::load_from_binary_spc(istream &) caught:\n" << e.what() << endl;
#endif
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
    return false;
  }//try / catch
  
  return true;
}//bool load_from_binary_spc( std::istream &input )
}//namespace SpecUtils



