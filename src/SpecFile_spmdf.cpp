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

#include <tuple>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <assert.h>
#include <iostream>
#include <stdexcept>


#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"
#include "SpecUtils/SpecFile_location.h"

using namespace std;


/**
 @TODO A lot of the places were #log_developer_error is called, could be put
       into parse_wanrings_ as well.
 */

namespace
{
  struct DailyFileS1Info
  {
    bool success;
    std::string detTypeStr;
    std::string appTypeStr;
    int nchannels;
    std::vector<float> calibcoefs;
    bool isDefualtCoefs;
    std::string algorithmVersion;
    //caputures max 1 attribute...
    struct params{ std::string name, value, attname, attval; };
    vector<DailyFileS1Info::params> parameters;
  };//struct DailyFileS1Info
  
  
  struct DailyFileEndRecord
  {
    bool success;
    std::string alarmColor; //Red, Yellow, Gree
    int occupancyNumber;
    SpecUtils::time_point_t lastStartTime;
    std::string icd1FileName;
    float entrySpeed, exitSpeed;
  };//struct DailyFileEndRecord
  
  
  struct DailyFileAnalyzedBackground
  {
    enum BackgroundType{ Gamma, Neutrons };
    
    bool success;
    BackgroundType type;
    float realTime;
    std::shared_ptr< std::vector<float> > spectrum;
  };//struct DailyFileAnalyzedBackground
  
  struct DailyFileNeutronSignal
  {
    bool success;
    int numTimeSlicesAgregated;
    int timeChunkNumber;  //identical to one used for gamma
    vector<float> counts;  //Aa1, Aa2, Aa3, Aa4, Ba1, Ba2, Ba3, Ba4, Ca1, Ca2, Ca3, Ca4, Da1, Da2, Da3, Da4
  };//enum DailyFileNeutronSignal
  
  struct DailyFileGammaSignal
  {
    bool success;
    std::string detectorName;
    int timeChunkNumber;
    std::shared_ptr< std::vector<float> > spectrum;
  };//struct DailyFileGammaSignal
  
  
  struct DailyFileGammaBackground
  {
    bool success;
    std::string detectorName;
    std::shared_ptr< std::vector<float> > spectrum;
  };//struct DailyFileGammaBackground
  
  struct DailyFileNeutronBackground
  {
    bool success;
    float realTime;
    vector<float> counts;
  };//struct DailyFileNeutronBackground
  
  
  void parse_s1_info( const char * const data, const size_t datalen, DailyFileS1Info &info )
  {
    const string s1str( data, datalen );
    
    info.calibcoefs.clear();
    info.isDefualtCoefs = false;
    
    vector<string> s1fields;
    SpecUtils::split( s1fields, s1str, "," );
    if( s1fields.size() < 5 )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "parse_s1_info(): Invalid S1 line" );
#endif
      info.success = false;
      return;
    }
    
    info.detTypeStr = s1fields[1];  //NaI or HPGe
    info.appTypeStr = s1fields[2];  //SPM, RDSC, MRDIS
    info.nchannels = atoi( s1fields[3].c_str() );  //typically 512 or 4096
    info.algorithmVersion = s1fields[4];
    
    if( info.nchannels <= 0 )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "parse_s1_info(): Invalid claimed number of channels" );
#endif
      info.nchannels = 512;
    }
    
    for( size_t i = 5; i < (s1fields.size()-1); i += 2 )
    {
      DailyFileS1Info::params p;
      
      p.name = s1fields[i];
      p.value = s1fields[i+1];
      
      const string::size_type spacepos = p.name.find(' ');
      if( spacepos != string::npos )
      {
        const string::size_type equalpos = p.name.find('=',spacepos);
        if( equalpos != string::npos )
        {
          p.attval = p.name.substr( equalpos+1 );
          p.attname = p.name.substr( spacepos + 1, equalpos - spacepos - 1 );
          p.name = p.name.substr( 0, spacepos );
        }//if( equalpos != string::npos )
      }//if( spacepos != string::npos )
    }//for( size_t i = 5; i < (s1fields.size()-1); i += 2 )
    
    // It appears energy calibration parameters are not provided by the file.  It does however
    //  provide deviation pairs... spectrum files just seem to be a land where sanity is optional.
    if( info.calibcoefs.empty() )
    {
      //We will put some default energy calibration parameters here so we can preserve the deviation
      //  pair information.
      info.calibcoefs = { 0.0f, 3225.0f/std::max(info.nchannels-1,1) };
      info.isDefualtCoefs = true;
    }
    
    info.success = true;
  }//bool parse_s1_info( const std::string &s1str, DailyFileS1Info &info )
  
  void parse_s2_info( const char * const data, const size_t datalen,
                     map<string,vector< pair<float,float> > > &answer )
  {
    const std::string s2str( data, datalen );
    answer.clear();
    
    vector<string> s2fields;
    SpecUtils::split( s2fields, s2str, "," );
    string detname;
    for( size_t i = 1; i < (s2fields.size()-1); )
    {
      const string &field = s2fields[i];
      const string &nextfield = s2fields[i+1];
      
      if( field.empty() || nextfield.empty() )
      {
        i += 2;
        continue;
      }
      
      if( isdigit(field[0]) )
      {
        const float energy = static_cast<float>( atof( field.c_str() ) );
        const float offset = static_cast<float>( atof( nextfield.c_str() ) );
        answer[detname].push_back( pair<float,float>(energy,offset) );
        i += 2;
      }else
      {
        detname = s2fields[i];
        ++i;
      }
    }//for( size_t i = 1; i < (s2fields.size()-1); )
  }//void parse_s2_info(...)
  
  
  void parse_end_record( const char * const data, const size_t datalen, DailyFileEndRecord &info )
  {
    const std::string str( data, datalen );
    
    vector<string> fields;
    SpecUtils::split( fields, str, "," );
    
    if( fields.size() < 5 )
    {
      info.success = false;
      return;
    }
    
    info.alarmColor = fields[1];
    info.occupancyNumber = atoi( fields[2].c_str() );
    info.lastStartTime = SpecUtils::time_from_string( fields[3].c_str() );
    
    //    cout << "'" << fields[3] << "'--->" << info.lastStartTime << endl;
    info.icd1FileName = fields[4];
    info.entrySpeed = (fields.size()>5) ? static_cast<float>(atof(fields[5].c_str())) : 0.0f;
    info.exitSpeed = (fields.size()>6) ? static_cast<float>(atof(fields[6].c_str())) : info.entrySpeed;
    
    info.success = true;
  }//bool parse_end_record(...)
  
  
  void parse_analyzed_background( const char * const data, const size_t datalen,
                                 DailyFileAnalyzedBackground &info )
  {
    const string line( data, datalen );
    std::string::size_type pos1 = line.find( ',' );
    if( pos1 == string::npos )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "parse_analyzed_background: unexpected EOL 0" );
#endif
      info.success = false;
      return;
    }//if( pos1 == string::npos )
    
    assert( line.substr(0,std::min(pos1,std::string::size_type(2))) == "AB" );
    std::string::size_type pos2 = line.find( ',', pos1+1 );
    if( pos2 == string::npos )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "parse_analyzed_background: unexpected EOL 1" );
#endif
      info.success = false;
      return;
    }
    
    string type = line.substr( pos1+1, pos2-pos1-1 );
    if( SpecUtils::iequals_ascii( type, "Gamma" ) )
      info.type = DailyFileAnalyzedBackground::Gamma;
    else if( SpecUtils::iequals_ascii( type, "Neutron" ) )
      info.type = DailyFileAnalyzedBackground::Neutrons;
    else
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      string msg = "parse_analyzed_background: invalid type '" + type + "'";
      log_developer_error( __func__, msg.c_str() );
#endif
      info.success = false;
      return;
    }
    
    pos1 = pos2;
    info.realTime = static_cast<float>( atof( line.c_str() + pos1 + 1 ) );
    pos1 = line.find( ',', pos1+1 );
    
    if( pos1 == string::npos )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "parse_analyzed_background: unexpected EOL 2" );
#endif
      info.success = false;
      return;
    }
    
    info.spectrum.reset( new vector<float>() );
    
    if( info.type == DailyFileAnalyzedBackground::Neutrons )
    {
      const float nneut = static_cast<float>( atof( line.c_str() + pos1 + 1 ) );
      info.spectrum->resize( 1, nneut );
    }else
    {
      const char *start = line.c_str() + pos1 + 1;
      const size_t len = line.size() - pos1 - 2;
      const bool success
      = SpecUtils::split_to_floats( start, len, *info.spectrum );
      
      if( !success )
      {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        log_developer_error( __func__, "parse_analyzed_background: did not decode spectrum" );
#endif
        info.success = false;
        return;
      }
    }//if( neutron ) / ( gamma )
    
    info.success = true;
  }//void parse_analyzed_background(...)
  
  void parse_neutron_signal( const char * const data, const size_t datalen,
                            DailyFileNeutronSignal &info )
  {
    const string line( data, datalen );
    std::string::size_type pos = line.find( ',' );
    if( pos == string::npos )
    {
      info.success = false;
      return;
    }
    
    const char *start = line.c_str() + pos + 1;
    const size_t len = line.size() - pos - 1;
    
    vector<float> vals;
    const bool success = SpecUtils::split_to_floats( start, len, vals );
    if( !success || vals.size() < 2 || IsInf(vals[0])
        || IsNan(vals[0]) || IsInf(vals[1]) || IsNan(vals[1]) )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "parse_neutron_signal: did not decode spectrum" );
#endif
      info.success = false;
      return;
    }
    
    info.numTimeSlicesAgregated = static_cast<int>( vals[0] );
    info.timeChunkNumber = static_cast<int>( vals[1] );
    info.counts.clear();
    info.counts.insert( info.counts.end(), vals.begin()+2, vals.end() );
    
    info.success = true;
  }//bool parse_analyzed_background
  
  void parse_gamma_signal( const char * const data, const size_t datalen,
                          DailyFileGammaSignal &info )
  {
    const string line( data, datalen );
    std::string::size_type pos1 = line.find( ',' );
    if( pos1 == string::npos )
    {
      info.success = false;
      return;
    }
    
    std::string::size_type pos2 = line.find( ',', pos1+1 );
    if( pos2 == string::npos )
    {
      info.success = false;
      return;
    }
    
    info.detectorName = line.substr( pos1+1, pos2-pos1-1 );
    pos1 = pos2;
    pos2 = line.find( ',', pos1+1 );
    if( pos2 == string::npos )
    {
      info.success = false;
      return;
    }
    
    info.timeChunkNumber = static_cast<int>( atoi( line.c_str() + pos1 + 1 ) );
    info.spectrum.reset( new vector<float>() );
    
    vector<float> vals;
    const char *start = line.c_str() + pos2 + 1;
    const size_t len = line.size() - pos2 - 1;
    const bool success = SpecUtils::split_to_floats( start, len, *info.spectrum );
    if( !success || info.spectrum->size() < 2 )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "parse_gamma_signal: did not decode spectrum" );
#endif
      info.success = false;
      return;
    }
    
    info.success = true;
    return;
  }//void parse_gamma_signal()
  
  
  void parse_gamma_background( const char * const data, const size_t datalen,
                              DailyFileGammaBackground &info )
  {
    const string line( data, datalen );
    std::string::size_type pos1 = line.find( ',' );
    if( pos1 == string::npos )
    {
      info.success = false;
      return;
    }
    
    std::string::size_type pos2 = line.find( ',', pos1+1 );
    if( pos2 == string::npos )
    {
      info.success = false;
      return;
    }
    
    info.detectorName = line.substr( pos1+1, pos2-pos1-1 );
    info.spectrum.reset( new vector<float>() );
    
    const char *start = line.c_str() + pos2 + 1;
    const size_t len = line.size() - pos2 - 1;
    const bool success = SpecUtils::split_to_floats( start, len, *info.spectrum );
    if( !success || info.spectrum->size() < 2 )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "parse_gamma_background: did not decode spectrum" );
#endif
      info.success = false;
      return;
    }
    
    info.success = true;
  }//bool parse_gamma_background(...)
  
  
  void parse_neutron_background( const char * const data, const size_t datalen,
                                DailyFileNeutronBackground &info )
  {
    const string line( data, datalen );
    std::string::size_type pos1 = line.find( ',' );
    if( pos1 == string::npos )
    {
      info.success = false;
      return;
    }
    
    std::string::size_type pos2 = line.find( ',', pos1+1 );
    if( pos2 == string::npos )
    {
      info.success = false;
      return;
    }
    
    info.realTime = static_cast<float>( atof( line.c_str() + pos1 + 1 ) );
    
    const char *start = line.c_str() + pos2 + 1;
    const size_t len = line.size() - pos2 - 1;
    
    
    //Files like fail: ref1E5GQ2SW76
    const bool success = SpecUtils::split_to_floats( start, len, info.counts );
    if( !success || info.counts.size() < 2 )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "parse_neutron_background: did not decode counts" );
#endif
      info.success = false;
      return;
    }
    
    info.success = true;
    return;
  }//bool parse_neutron_background(...)
  
}//namespace SpectroscopicDailyFile

namespace SpecUtils
{
bool SpecFile::load_spectroscopic_daily_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  char buffer[8];
  input.get( buffer, sizeof(buffer)-1 );
  buffer[sizeof(buffer)-1] = '\0'; //JIC
  
  string bufferstr = buffer;
  const bool isSDF = ((bufferstr.size() > 3 && bufferstr[2]==',')
                      && ( SpecUtils::starts_with( bufferstr, "GB" )
                          || SpecUtils::starts_with( bufferstr, "NB" )
                          || SpecUtils::starts_with( bufferstr, "S1" )
                          || SpecUtils::starts_with( bufferstr, "S2" )
                          || SpecUtils::starts_with( bufferstr, "GS" )
                          || SpecUtils::starts_with( bufferstr, "GS" )
                          || SpecUtils::starts_with( bufferstr, "NS" )
                          || SpecUtils::starts_with( bufferstr, "ID" )
                          || SpecUtils::starts_with( bufferstr, "AB" )));
  if( !isSDF )
    return false;
  
  input.seekg( 0, ios_base::beg );
  
  const bool success = load_from_spectroscopic_daily_file( input );
  
  if( !success )
    return false;
  
  filename_ = filename;
  //      Field 4, the equipment specifier, is as follows:
  //        -SPM-T for a Thermo ASP-C
  //        -SPM-C for a Canberra ASP-C
  //        -RDSC1 for the Radiation Detector Straddle Carrier in primary
  //        -RDSC2 for the Radiation Detector Straddle Carrier in secondary
  //        -MRDIS2 for the Mobile Radiation Detection and Identification System in secondary
  //        ex. refG8JBF6M229
  vector<string> fields;
  SpecUtils::split( fields, filename, "_" );
  if( fields.size() > 3 )
  {
    if( fields[3] == "SPM-T" )
    {
      manufacturer_ = "Thermo";
      instrument_model_ = "ASP";
    }else if( fields[3] == "SPM-C" )
    {
      manufacturer_ = "Canberra";
      instrument_model_ = "ASP";
    }else if( fields[3] == "RDSC1" )
    {
      inspection_ = "Primary";
      instrument_model_ = "Radiation Detector Straddle Carrier";
    }else if( fields[3] == "RDSC2" )
    {
      inspection_ = "Secondary";
      instrument_model_ = "Radiation Detector Straddle Carrier";
    }else if( fields[3] == "MRDIS2" )
    {
      inspection_ = "Secondary";
      instrument_model_ = "Mobile Radiation Detection and Identification System";
    }
  }//if( fields.size() > 3 )
  
  return true;
}//bool load_spectroscopic_daily_file( const std::string &filename )

  
  
bool SpecFile::load_from_spectroscopic_daily_file( std::istream &input )
{
  /* The daily file is a comma separated value file, with a carriage return and
   line feed denoting the end of each line.  The file is saved as a text (.txt)
   file.  Spaces are not necessary after each comma, in an effort to minimize
   the overall size of the file.
   In the future, all data provided in the Daily File will be energy calibrated,
   according to the calibration parameters provided in the S1 and/or S2 lines.
   This is to ensure that calibration is being done correctly and consistently
   between various institutions, laboratories, and individuals.  The calibration
   parameters will be provided in case an individual wants to “unwrap” the data
   back to the source information provided in the ICD-1 file.  This is not done
   for GS line data as of Revision 3 of this data.
   */
  
  //This is a rough hack in; it would be nice to mmap things and read in that
  //  way, as well as handle potential errors better
  
#define DO_SDF_MULTITHREAD 0
  //Intitial GetLineSafe() for "dailyFile6.txt"
  //  To parse SDF took:  1.027030s wall, 1.000000s user + 0.020000s system = 1.020000s CPU (99.3%)
  //  Total File openeing time was:  1.164266s wall, 1.300000s user + 0.110000s system = 1.410000s CPU (121.1%)
  //
  //Adding an extra indirection of creating a copy of a string
  //  To parse SDF took:  1.162067s wall, 1.120000s user + 0.030000s system = 1.150000s CPU (99.0%)
  //  Total File openeing time was:  1.313293s wall, 1.420000s user + 0.130000s system = 1.550000s CPU (118.0%)
  //
  //Reading the file all at once
  //  To parse SDF took:  1.023828s wall, 0.980000s user + 0.030000s system = 1.010000s CPU (98.6%)
  //  Total File openeing time was:  1.191765s wall, 1.330000s user + 0.160000s system = 1.490000s CPU (125.0%)
  //
  //Making things niavly multithreaded
  //  To parse SDF took:  0.864120s wall, 1.140000s user + 0.110000s system = 1.250000s CPU (144.7%)
  //  Total File openeing time was:  0.995905s wall, 1.410000s user + 0.190000s system = 1.600000s CPU (160.7%)
  //
  //With error checking
  //  To parse SDF took:  0.855769s wall, 1.140000s user + 0.110000s system = 1.250000s CPU (146.1%)
  //
  //With current multithreaded implementation:
  //  To parse SDF took:  0.971223s wall, 0.950000s user + 0.020000s system = 0.970000s CPU (99.9%)
  //  Total File openeing time was:  1.102778s wall, 1.230000s user + 0.110000s system = 1.340000s CPU (121.5%)
  //
  //So I think I'll just stick to single threaded parsing for now since it only
  //  increases speed by ~15% to do mutliithreaded, at the cost of memory.
  //
  //TODO: Check whether creating the Measurement objects in a multithreaded
  //      fashion significantly helps things.
  //      Make it so the worker functions in the SpectroscopicDailyFile
  //      namespace dont re-copy all the strings passed in.
  
  reset();
  const istream::pos_type orig_pos = input.tellg();
  
  try
  {
  int occupancy_num = 0, background_num = 0, s1_num = 0, s2_num = 0;
  vector<DailyFileS1Info> s1infos;
  vector< map<string,vector< pair<float,float> > > > detname_to_devpairs;
  
  map<int,int> background_to_s1_num, background_to_s2_num;
  map<int,int> occupancy_num_to_s1_num, occupancy_num_to_s2_num;
  map<int,vector<std::shared_ptr<DailyFileGammaBackground> > > gamma_backgrounds;
  map<int,std::shared_ptr<DailyFileNeutronBackground> > neutron_backgrounds;  //*should* only have one per background number
  map<int, SpecUtils::time_point_t > end_background;
  
  map<int,vector<std::shared_ptr<DailyFileGammaSignal> > > gamma_signal;
  map<int,vector<std::shared_ptr<DailyFileNeutronSignal> > > neutron_signal;
  
  map<int,DailyFileEndRecord> end_occupancy;
  
  //We *should* only have one analyzed background per occupancy, so we'll go
  //  with this assumption
  map<int,DailyFileAnalyzedBackground> analyzed_gamma_backgrounds;
  map<int,DailyFileAnalyzedBackground> analyzed_neutron_backgrounds;
  
  
  set<string> detectorNames;

  
  //TODO 20180817 - only niavely addressed below:
  //Files like refRA2PVFVA5I look a lot like these types of files because they
  //  are text and start with GB or NB, but instead have formats of
  //NB,000002,000002,000002,000002,00-00-04.841
  //GB,000822,000750,000770,000757,00-00-04.919
  //  with no other line types.  so here we will test for this.
  {//begin test for wierd format
    string line;
    if( !SpecUtils::safe_get_line( input, line, 2048 ) )
      throw runtime_error( "" );
    
    int ndash = 0;
    auto pos = line.find_last_of( ',' );
    for( ; pos < line.size(); ++pos )
      ndash += (line[pos] == '-');
    
    if( ndash > 1 || pos == string::npos )
      throw runtime_error( "" );
    input.seekg( orig_pos, ios::beg );
  }//end test for wierd format
  
  
  
#if( DO_SDF_MULTITHREAD )
  if( !input.good() )
    throw runtime_error( "" );
  
  vector<char> filedata;
  input.seekg( 0, ios::end );
  const istream::pos_type eof_pos = input.tellg();
  input.seekg( orig_pos, ios::beg );
  
  const size_t filelength = 0 + eof_pos - orig_pos;
  filedata.resize( filelength + 1 );
  input.read( &filedata[0], filelength );
  if( !input.good() )
    throw runtime_error( "" );
  
  filedata[filelength] = '\0';
  
  size_t pos = 0;
  const char * const data = &filedata[0];
  
  SpecUtilsAsync::ThreadPool pool;
#else
  string line;
#endif
  
  int nUnrecognizedLines = 0, nLines = 0, nGammaLines = 0;
  
#if( DO_SDF_MULTITHREAD )
  while( pos < filelength )
#else
    while( SpecUtils::safe_get_line( input, line ) )
#endif
    {
      
#if( DO_SDF_MULTITHREAD )
      const char * const linestart = data + pos;
      while( pos < filelength && data[pos] != '\r' && data[pos] != '\n' )
        ++pos;
      
      const char * const lineend = data + pos;
      const size_t linelen = lineend - linestart;
      
      ++pos;
      if( pos < filelength && data[pos]=='\n' )
        ++pos;
#else
      const size_t linelen = line.length();
      const char * const linestart = line.c_str();
#endif
      
      ++nLines;
      if( linelen < 4 )
        continue;
      
      // We expect the first two characters to be like "NB" or "GB" or something, and the third
      //  character to be ','.  We'll be sloppy here and allow a spaces or tabs before the comma,
      //  although I dont think we need to.
      bool next_is_comma = false;
      for( size_t i = 2; !next_is_comma && (i < linelen); ++i )
      {
        next_is_comma = (linestart[i] == ',');
        if( (linestart[i] != ',') && (linestart[i] != ' ') && (linestart[i] != '\t') )
          break;
      }
      
      if( !next_is_comma )
        continue;
      
      const string linetype(linestart,2);
      
      //dates are written as yyyy-mm-ddThh:mm:ss.000Z-hh:mm
      
      if( linetype == "S1" )
      {
        /*     First line of setup parameters
         “S1” is the first line of Setup Parameters.
         The second element on the S1 line is the type of detector, either “NaI”
         or “HPGe”.
         The third element on the S1 line is the application, either “SPM”, “RDSC”
         , or “MRDIS”.
         The fourth element on the S1 line is the number of channels used per
         detector.  This is typically 512 for NaI systems or 4096 for HPGe
         systems.
         The fifth element on the S1 line is the algorithm version number.  This
         is taken from the <AlgorithmVersion> tag in the ICD-2 file.
         The next series of elements are variable, and are additional setup
         parameters taken from the ICD-2 file.  They are the children of the
         <AlgorithmParameter> tag in the ICD-2 file, and alternate <ParameterName>
         , <ParameterValue>.
         For example, if a ParameterName was “NSigma” and the ParameterValue was
         “8”, and the next ParameterName was “Width” and the ParameterValue was
         “14”, the S1 line would include the text:
         NSigma, 8, Width, 14
         This process continues through all <ParameterName> and <ParameterValue>
         elements in the ICD-2 file.
         */
        DailyFileS1Info info;
        parse_s1_info( linestart, linelen, info );
        if( !info.success )
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          log_developer_error( __func__, "load_from_spectroscopic_daily_file(): S1 line invalid" );
#endif
          throw runtime_error( "load_from_spectroscopic_daily_file(): S1 line invalid" );
        }
        
        s1infos.push_back( info );
        
        s1_num = static_cast<int>( s1infos.size() - 1 );
      }else if( linetype == "S2" )
      {
        /*     “S2” is the second line of Setup Parameters.
         This line provides any detector-specific calibration information that is
         included in the ICD-1 file as a <NonlinearityCorrection> tag.
         The <NonlinearityCorrection> tags are listed in detector order.  So the
         S2 line should read:
         S2, Aa1, 81, -5, 122, -6, …, Aa2, 81, -4, 122, -6, …, Aa3, 81, -5, …
         These elements are important for properly calibrating the detectors when
         plotting spectra.
         */
        map<string,vector< pair<float,float> > > devpairs;
        parse_s2_info( linestart, linelen, devpairs );
        detname_to_devpairs.push_back( devpairs );
        
        s2_num = static_cast<int>( detname_to_devpairs.size() - 1 );
      }else if( linetype == "GB" )
      {
        /*
         The monitors will produce a background on a regular, periodic interval.
         The period is currently every 30 minutes; however, it is conceivable that
         this could change in the future.  These periodic backgrounds are crucial
         to evaluating the long-term health of the system as well for detecting and
         troubleshooting intermittent failures.
         */
        /*
         Each gamma detector has its own GB line.
         The specific gamma detector is denoted in the second element of the GB
         line (e.g., Aa1, Ca3).
         The remaining elements are the channel counts in each channel, in order.
         These are taken from the <ChannelData> child of the same
         <BackgroundSpectrum> that the timestamp was derived from.  If
         “zeros compression” is used, the data must be expanded such that each line
         has 512 (or 4096) channels of data.  The data must also be energy calibrated.
         */
        if( nGammaLines == 0 )
        {
          //See "TODO 20180817" above
          if( (line.size()<50) && (line.find("00-00-")!=string::npos) )
          {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
            log_developer_error( __func__, "load_from_spectroscopic_daily_file(): Not a daily file we can decode (probably - giving up)" );
#endif
            throw runtime_error( "Not a daily file we can decode (probably - giving up)" );
          }
        }//if( nGammaLines == 0 )
        
        auto info = make_shared<DailyFileGammaBackground>();
        
#if( DO_SDF_MULTITHREAD )
        pool.post( std::bind( &parse_gamma_background, linestart, linelen, std::ref(*info) ) );
#else
        parse_gamma_background( linestart, linelen, *info );
        
        if( !info->success )  //See "TODO 20180817" above
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          log_developer_error( __func__, "load_from_spectroscopic_daily_file(): Error Parsing gamma background" );
#endif
          throw runtime_error( "Error Parsing gamma background" );
        }//if( !info->success )
        
        detectorNames.insert( info->detectorName );
#endif
        ++nGammaLines;
        gamma_backgrounds[background_num].push_back( info );
      }else if( linetype == "NB" )
      {
        /*    All neutron detectors are combined on a single NB line.
         The second element of the NB line is the duration of the background, in
         seconds.  To help align columns, the duration in seconds should always be
         a three-digit number (e.g., 030 for a 30-second background).  The daily
         file assumes that the duration of the neutron background is identical to
         the duration of the gamma background, which is supported by operational
         observations.
         The remaining elements of the NB line are the counts for each neutron
         detector recorded over the background period.  The detectors are listed in
         order – Aa1N, then Aa2N, then Ba1N, etc.
         */
        auto info = make_shared<DailyFileNeutronBackground>();
        
#if( DO_SDF_MULTITHREAD )
        pool.post( std::bind(&parse_neutron_background, linestart, linelen, std::ref(*info) ) );
#else
        parse_neutron_background( linestart, linelen, *info );
        
        if( !info->success )
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          log_developer_error( __func__, "load_from_spectroscopic_daily_file(): Error Parsing neutron background" );
#endif
          throw runtime_error( "Error Parsing neutron background" );
        }
#endif
        
        neutron_backgrounds[background_num] = info;
      }else if( linetype == "BX" )
      {
        /*    Then end of the background is denoted by a BX line.
         The second element of the BX line is the timestamp of the background.  The
         timestamp is the <StartTime> child to the <BackgroundSpectrum> tag in the
         periodic background files.
         */
        if( linelen < 4 )
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          log_developer_error( __func__, "load_from_spectroscopic_daily_file(): invalid BX line lenght" );
#endif
          throw runtime_error( "invalid BX line lenght" );
        }
        
        const string line( linestart, linelen );
        end_background[background_num] = time_from_string( line.c_str() + 3 );
        background_to_s1_num[background_num] = s1_num;
        background_to_s2_num[background_num] = s2_num;
        
        ++background_num;
      }else if( linetype == "GS" )
      {
        /*    Signals – GS and NS
         ICD-1 file contains large amounts of pre- and post-occupancy data, all
         recorded in 0.1 second intervals.  These high-frequency, long-duration
         measurements significantly increase the size of the ICD-1 file.  This
         level of data fidelity is useful for detailed analysis; however, the cost
         would be too great to use these as daily files, and is not warranted for
         daily file analysis.  The signal lines in the daily file make two
         important concessions to reduce overall file size:
         -The only data included in the daily file is occupied data.  Pre- and
         post-occupancy data are discarded.
         -The data is only recorded in 1 second intervals, not 0.1 second
         intervals.
         Future versions of this document may relax some of these concessions to
         include data other than occupied, or at a higher frequency.
         */
        /*    The second element of the GS line is the gamma detector name, for example
         Aa1 or Da3.
         The third element of the GS line is the time chunk number, starting from
         001.  In this version of the specification, the first ten time slices will
         be aggregated into the first time chunk (001); the next ten time slices
         will be aggregated into the second time chunk (002); and so on, resulting
         in 1 second time chunks.  In the future, it is conceivable that a
         different time chunk size (0.5 second, 0.2 second, or 2 seconds) may be
         utilized.  The time chunk number serves as a timestamp within the occupancy.
         The remaining elements of the GS line are the counts in each channel for
         that detector, aggregated over one second and energy calibrated per the
         parameters provided in the S1 and S2 lines (and any other source as
         necessary).  These are taken directly from the <ChannelData> elements.
         Unfortunately, since these are taken directly from the ICD-1 file, GS line
         data is not energy calibrated as of this version.
         */
        std::shared_ptr<DailyFileGammaSignal> info( new DailyFileGammaSignal );
        
#if( DO_SDF_MULTITHREAD )
        pool.post( std::bind(&parse_gamma_signal, linestart, linelen, std::ref(*info) ) );
#else
        parse_gamma_signal( linestart, linelen, *info );
        
        if( !info->success )
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          log_developer_error( __func__, "load_from_spectroscopic_daily_file(): Error Parsing gamma signal" );
#endif
          
          throw runtime_error( "Error Parsing gamma signal" );
        }//if( !info->success )
        
        detectorNames.insert( info->detectorName );
#endif
        ++nGammaLines;
        gamma_signal[occupancy_num].push_back( info );
      }else if( linetype == "NS" )
      {
        /*    Neutron Signal
         The second element of the NS line is the number of time slices used to
         form one time chunk, represented as a 3 digit number to help align
         columns.  In this case, since ten time slices contribute to each chunk,
         the second element of the NS line should read, “010”.  (Again, future
         versions could change this to 005 or 002 or 020.)
         The third element of the NS line is the time chunk number.  This should be
         identical to the time chunk number used in the gamma signal.
         The remaining elements of the NS line are the counts from each detector
         for the one second interval.  These are taken directly from the
         <ChannelData> elements.  The signals are listed in order of the detectors:
         Aa1N, Aa2N, Ba1N, and so forth.
         */
        std::shared_ptr<DailyFileNeutronSignal> info( new DailyFileNeutronSignal() );
        
#if( DO_SDF_MULTITHREAD )
        pool.post( std::bind(&parse_neutron_signal,linestart, linelen, std::ref(*info) ) );
#else
        parse_neutron_signal( linestart, linelen, *info );
        
        if( !info->success )
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          log_developer_error( __func__, "Error Parsing neutron signal" );
#endif
          throw runtime_error( "Error Parsing neutron signal" );
        }//if( !info->success )
#endif
        
        neutron_signal[occupancy_num].push_back( info );
      }else if( linetype == "ID" )
      {
        /*    One line is provided for each radionuclide identification.  Even if
         multiple identifications are made within the same detector subset and time
         slices, a new line should be provided for each radionuclide identified.
         If a radionuclide is identified multiple times within the same occupancy
         (based on different time slices or different detector subsets), a separate
         line should be provided for each ID.
         The second element of the ID line is the radionuclide identified.  The
         nuclide ID comes from the <NuclideName> tag in the ICD-2 file.
         The next elements of the ID line are the detectors that were used to make
         the identification.  These stem from the <SubsetSampleList> element in the
         ICD-2 file.
         The next elements of the ID line are the time slices that were used in
         making the identification.  These are taken directly from the
         <SubsetSampleList> tag name in the ICD-2 file.
         The ID field will state “NONE” if no radionuclide was identified.  This is
         made clear if the <AlarmDescription> tag reads “No Alarm”.
         */
        //ToDo, implement this
      }else if( linetype == "AB" )
      {
        /*    When evaluating an alarm, the background used by the algorithm is
         extremely important to capture.  This is provided in the ICD-2 file as a
         common background aggregated over all gamma detectors, over a long period
         of time – typically 300 seconds.
         */
        /*    The second element of the AB line is “Neutron” for the neutron background.
         The third element of the AB line is the duration of the background, in
         seconds.  This is taken from the <RealTime> child of the
         <BackgroundSpectrum> element in the ICD-2 file, the same as the gamma
         background.
         The fourth element of the AB line is the <BackgroundCounts> child of the
         <GrossCountSummed> element.  This is the sum of the counts from all
         neutron detectors over the background period.
         */
        DailyFileAnalyzedBackground info;
        parse_analyzed_background( linestart, linelen, info );
        
        if( !info.success )
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          log_developer_error( __func__, "Error Parsing analyzed background" );
#endif
          throw runtime_error( "Error Parsing analyzed background" );
        }
        
        if( info.type == DailyFileAnalyzedBackground::Gamma )
          analyzed_gamma_backgrounds[occupancy_num] = info;
        else
          analyzed_neutron_backgrounds[occupancy_num] = info;
      }else if( linetype == "GX" )
      {
        /*    The end of the occupancy is denoted by a GX line.
         The second element on the GX line is the alarm light color, either Red,
         Yellow, or Green, taken from the <AlarmLightColor> tag in the ICD-2 file.
         This is useful for categorizing alarms in follow-up analysis.
         The third element on the GX line is the occupancy number, taken from the
         <occupancyNumber> tag in the ICD-2 file.
         The fourth element on the GX line is the timestamp, taken from the last
         time stamp in the ICD-1 file.  This is the <StartTime> child of the last
         <DetectorData> element in the file.  This methodology should also work for
         the RDSC, which does not record pre- and post-occupancy data.
         The fifth element of the GX line is the filename of the ICD-1 file that
         provided data for this occupancy.  This information may be useful in case
         the actual ICD-1 file is needed for additional analysis.
         The sixth element on the GX line is the entry speed, taken from the
         <vehicleEntrySpeed> tag in the ICD-2 file.
         The seventh element on the GX line is the exit speed, taken from the
         <vehicleExitSpeed> tag in the ICD-2 file.  This sixth element (exit speed)
         may or may not exist, if the monitor records it.
         */
        
        DailyFileEndRecord info;
        parse_end_record( linestart, linelen, info );
        
        if( !info.success )
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          log_developer_error( __func__, "Error Parsing end of record line" );
#endif
          throw runtime_error( "Error Parsing end of record line" );
        }
        
        end_occupancy[occupancy_num] = info;
        
        occupancy_num_to_s1_num[occupancy_num] = s1_num;
        occupancy_num_to_s2_num[occupancy_num] = s2_num;
        
        ++occupancy_num;
      }else
      {
        string line(linestart, linelen);
        SpecUtils::trim( line );
        
        if( !line.empty() )
        {
          ++nUnrecognizedLines;
          const double fracBad = double(nUnrecognizedLines) / nLines;
          if( (nUnrecognizedLines > 10) && (fracBad > 0.1) )
            throw runtime_error( "To many bad lines" );
          
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          string msg = "unrecognized line begining: " + linetype;
          log_developer_error( __func__, msg.c_str() );
#endif
        }//if( !line.empty() )
      }//if / else (determine what this line means)
    }//while( SpecUtils::safe_get_line( input, line ) )
  
#if( DO_SDF_MULTITHREAD )
  pool.join();
#endif
  
  //Probably not necassary, but JIC
  background_to_s1_num[background_num] = s1_num;
  background_to_s2_num[background_num] = s2_num;
  occupancy_num_to_s1_num[occupancy_num] = s1_num;
  occupancy_num_to_s2_num[occupancy_num] = s2_num;
  
  //TODO: convert so that we are sure we are using the correct setup, incase
  //      there are multiple setup lines
  //Probably just create a struct to hold the information, and parse all the
  //      setups.
  
  
  //Heres what we have to work with:
  if( s1infos.empty() )
  {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
    log_developer_error( __func__, "Either S1 line missing" );
#endif
    throw runtime_error( "Either S1 line missing" );
  }//
  
  
#if( DO_SDF_MULTITHREAD )
  //The below two loops are probably quite wasteful, and not necassary
  for( map<int,vector<std::shared_ptr<DailyFileGammaBackground> > >::const_iterator i = gamma_backgrounds.begin();
      i != gamma_backgrounds.end(); ++i )
  {
    for( size_t j = 0; j < i->second.size(); ++j )
      detectorNames.insert( i->second[j]->detectorName );
  }
  
  for( map<int,vector<std::shared_ptr<DailyFileGammaSignal> > >::const_iterator i = gamma_signal.begin();
      i != gamma_signal.end(); ++i )
  {
    for( size_t j = 0; j < i->second.size(); ++j )
      detectorNames.insert( i->second[j]->detectorName );
  }
#endif  //#if( DO_SDF_MULTITHREAD )
  
  
  map<string,int> detNameToNum;
  int detnum = 0;
  for( const string &name : detectorNames )
    detNameToNum[name] = detnum++;
  
  vector< std::shared_ptr<Measurement> > backgroundMeasurements, signalMeasurements;
  
  int max_occupancie_num = 0;
  
  //Lets re-use energy calibrations were we can
  typedef tuple<size_t,vector<float>,vector<pair<float,float>>> EnergyCalInfo_t;
  map<EnergyCalInfo_t,shared_ptr<EnergyCalibration>> previous_cals;
  
  for( int occnum = 0; occnum < occupancy_num; ++occnum )
  {
    const map<int,int>::const_iterator s1pos = occupancy_num_to_s1_num.find(occnum);
    const map<int,int>::const_iterator s2pos = occupancy_num_to_s2_num.find(occnum);
    
    if( s1pos == occupancy_num_to_s1_num.end() || s1pos->second >= int(s1infos.size()) )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "Serious programing logic error in 0" );
#endif
      throw runtime_error( "Serious programing logic error in 0" );
    }
    
    const DailyFileS1Info &sinfo = s1infos[s1pos->second];
    const map<string,vector< pair<float,float> > > *devpairs = 0;
    if( s2pos != occupancy_num_to_s2_num.end() && s2pos->second<int(detname_to_devpairs.size()))
      devpairs = &(detname_to_devpairs[s2pos->second]);
    
    const map<int,vector<std::shared_ptr<DailyFileGammaSignal> > >::const_iterator gammaiter
    = gamma_signal.find( occnum );
    if( gammaiter == gamma_signal.end() )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "Serious programing logic error in 1" );
#endif
      
      throw runtime_error( "Serious programing logic error in 1" );
    }//if( gammaiter == gamma_signal.end() )
    
    const map<int,vector<std::shared_ptr<DailyFileNeutronSignal> > >::const_iterator neutiter
    = neutron_signal.find( occnum );
    
    const map<int,DailyFileEndRecord>::const_iterator endrecorditer
    = end_occupancy.find( occnum );
    if( endrecorditer == end_occupancy.end() )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "Serious programing logic error in 2" );
#endif
      
      throw runtime_error( "Serious programing logic error in 2" );
    }//if( endrecorditer == end_occupancy.end() )
    
    const DailyFileEndRecord &endrecord = endrecorditer->second;
    const vector<std::shared_ptr<DailyFileGammaSignal> > &gammas = gammaiter->second;
    const vector<std::shared_ptr<DailyFileNeutronSignal> > *nutsignal = 0;
    if( neutiter != neutron_signal.end() )
      nutsignal = &neutiter->second;
    
    const DailyFileAnalyzedBackground *gammaback = 0, *neutback = 0;
    map<int,DailyFileAnalyzedBackground>::const_iterator backiter;
    backiter = analyzed_gamma_backgrounds.find( occnum );
    if( backiter != analyzed_gamma_backgrounds.end() )
      gammaback = &backiter->second;
    backiter = analyzed_neutron_backgrounds.find( occnum );
    if( backiter != analyzed_neutron_backgrounds.end() )
      neutback = &backiter->second;
    
    //Place the analyzed background into signalMeasurements
    if( gammaback )
    {
      std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();

      // \TODO: I'm not sure how/if DeviationPairs are applied before the summing is done; should
      //        check this out a little.
      
      meas->detector_number_    = static_cast<int>( detNameToNum.size() );
      meas->detector_name_      = "sum";
      meas->gamma_counts_       = gammaback->spectrum;
      meas->sample_number_      = 1000*endrecord.occupancyNumber;
      meas->source_type_        = SourceType::Background;
      meas->occupied_           = OccupancyStatus::NotOccupied;
      
      const size_t nchannel = meas->gamma_counts_ ? meas->gamma_counts_->size() : size_t(0);
      if( !sinfo.calibcoefs.empty() && (nchannel > 1) )
      {
        vector<pair<float,float>> thesedevpairs;
        if( devpairs )
        {
          // \TODO: We actually dont have deviation pairs for "sum"; should see if how this should
          //        actually be handled can be infered from the data.
          auto pos = devpairs->find(meas->detector_name_);
          if( pos != end(*devpairs) )
            thesedevpairs = pos->second;
        }
        
        const EnergyCalInfo_t key{ nchannel, sinfo.calibcoefs, thesedevpairs };
        auto pos = previous_cals.find( key );
        if( pos != end(previous_cals) )
        {
          assert( pos->second );
          meas->energy_calibration_ = pos->second;
        }else
        {
          try
          {
            auto newcal = make_shared<EnergyCalibration>();
            if( sinfo.isDefualtCoefs )
              newcal->set_default_polynomial( nchannel, sinfo.calibcoefs, thesedevpairs );
            else
              newcal->set_polynomial( nchannel, sinfo.calibcoefs, thesedevpairs );
            meas->energy_calibration_ = newcal;
            previous_cals[key] = newcal;
          }catch( std::exception &e )
          {
            meas->parse_warnings_.push_back( "Invalid energy cal found: " + string(e.what()) );
          }
        }//if( we can re-use calibration ) / else
      }//if( we have calibration coeffcicents )
      
      meas->remarks_.push_back( "Analyzed Background (sum over all detectors" );
      meas->real_time_ = meas->live_time_ = 0.1f*detNameToNum.size()*gammaback->realTime;
      
      /*
       meas->start_time_         = endrecord.lastStartTime;
       if( gammas.size() )
       {
       //This is a bit of a hack; I want the analyzed backgrounds to appear
       //  just before the analyzed spectra, so to keep this being the case
       //  we have to falsify the time a bit, because the measurements will get
       //  sorted later on according to start time
       const int totalChunks = gammas[gammas.size()-1].timeChunkNumber;
       
       const DailyFileNeutronSignal *neut = 0;
       if( nutsignal && nutsignal->size() )
       neut = &(*nutsignal)[0];
       
       const float realTime = neut ? 0.1f*neut->numTimeSlicesAgregated : 1.0f;
       const float timecor = realTime * (totalChunks - 0.5);
       const chrono::seconds wholesec( static_cast<int>(floor(timecor)) );
       const chrono::microseconds fracsec( static_cast<int>(1.0E6 * (timecor-floor(timecor))) );
       meas->start_time_ -= wholesec;
       meas->start_time_ -= fracsec;
       
       cout << "Background meas->sample_number_=" << meas->sample_number_ << " has time " << meas->start_time_ << endl;
       }//if( gammas.size() )
       */
      
      if( neutback && !neutback->spectrum )
      {
        meas->neutron_counts_ = *neutback->spectrum;
        meas->neutron_counts_sum_ = 0.0;
        for( const float f : meas->neutron_counts_ )
          meas->neutron_counts_sum_ += f;
        meas->contained_neutron_ = true;
      }//if( neutback )
      
      meas->gamma_count_sum_ = 0.0;
      if( !!meas->gamma_counts_ )
      {
        for( const float f : *meas->gamma_counts_ )
          meas->gamma_count_sum_ += f;
      }//if( !!meas->gamma_counts_ )
      
      signalMeasurements.push_back( meas );
      //      backgroundMeasurements.push_back( meas );
    }//if( gammaback )
    
    for( size_t i = 0; i < gammas.size(); ++i )
    {
      const DailyFileNeutronSignal *neut = 0;
      const DailyFileGammaSignal &gamma = *gammas[i];
      
#if( DO_SDF_MULTITHREAD )
      if( !gamma.success )
      {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        log_developer_error( __func__, "Error Parsing gamma signal" );
#endif
        
        throw runtime_error( "Error Parsing gamma signal" );
      }//if( !gamma.success )
#endif
      
      if( nutsignal )
      {
        for( size_t j = 0; j < nutsignal->size(); ++j )
        {
          if( (*nutsignal)[j]->timeChunkNumber == gamma.timeChunkNumber )
          {
            neut = ((*nutsignal)[j]).get();
            break;
          }
        }//for( size_t j = 0; j < nutsignal->size(); ++j )
      }//if( nutsignal )
      
#if( DO_SDF_MULTITHREAD )
      if( neut && !neut->success )
      {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        log_developer_error( __func__, "Error Parsing neutron signal" );
#endif
        
        throw runtime_error( "Error Parsing neutron signal" );
      }//if( neut && !neut->success )
#endif
      
      std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
      
      meas->detector_number_    = detNameToNum[gamma.detectorName];
      meas->detector_name_      = gamma.detectorName;
      meas->gamma_counts_       = gamma.spectrum;
      meas->sample_number_      = 1000*endrecord.occupancyNumber + gamma.timeChunkNumber;
      meas->source_type_        = SourceType::Foreground;
      meas->occupied_           = OccupancyStatus::Occupied;
      const size_t nchannel = meas->gamma_counts_ ? meas->gamma_counts_->size() : size_t(0);
      
      if( !sinfo.calibcoefs.empty() && nchannel > 1 )
      {
        vector<pair<float,float>> thesedevpairs;
        if( devpairs )
        {
          /// \TODO: I am totally not sure about these deviation pairs - need to check logic of
          ///        getting them
          
          auto pos = devpairs->find(meas->detector_name_);
          if( pos != end(*devpairs) )
            thesedevpairs = pos->second;
        }
        
        const EnergyCalInfo_t key{ nchannel, sinfo.calibcoefs, thesedevpairs };
        auto pos = previous_cals.find( key );
        if( pos != end(previous_cals) )
        {
          assert( pos->second );
          meas->energy_calibration_ = pos->second;
        }else
        {
          try
          {
            auto newcal = make_shared<EnergyCalibration>();
            if( sinfo.isDefualtCoefs )
              newcal->set_default_polynomial( nchannel, sinfo.calibcoefs, thesedevpairs );
            else
              newcal->set_polynomial( nchannel, sinfo.calibcoefs, thesedevpairs );
            meas->energy_calibration_ = newcal;
            previous_cals[key] = newcal;
          }catch( std::exception &e )
          {
            meas->parse_warnings_.push_back( "Invalid energy cal found: " + string(e.what()) );
          }
        }//if( we can re-use calibration ) / else
      }//if( we have energy cal info )
      
      auto loc = make_shared<LocationState>();
      loc->type_ = LocationState::StateType::Instrument;
      loc->speed_ = 0.5f*(endrecord.entrySpeed + endrecord.exitSpeed);
      meas->location_ = loc;
      meas->start_time_         = endrecord.lastStartTime;
      meas->remarks_.push_back( "ICD1 Filename: " + endrecord.icd1FileName );
      meas->remarks_.push_back( "Alarm Color: " + endrecord.alarmColor );
      meas->remarks_.push_back( "Occupancy Number: " + std::to_string(endrecord.occupancyNumber) );
      
      max_occupancie_num = std::max( endrecord.occupancyNumber, max_occupancie_num );
      
      meas->gamma_count_sum_ = 0.0;
      if( !!meas->gamma_counts_ )
      {
        for( const float f : *meas->gamma_counts_ )
          meas->gamma_count_sum_ += f;
      }
      
      meas->contained_neutron_ = false;
      meas->live_time_ = meas->real_time_ = 1.0f;
      if( neut )
      {
        meas->live_time_ = meas->real_time_ = 0.1f*neut->numTimeSlicesAgregated;
        
        if( meas->detector_number_ < static_cast<int>(neut->counts.size()) )
        {
          meas->neutron_counts_sum_ = neut->counts[meas->detector_number_];
          meas->neutron_counts_.resize( 1 );
          meas->neutron_counts_[0] = static_cast<float>( meas->neutron_counts_sum_ );
          meas->contained_neutron_ = true;
        }else
        {
          meas->neutron_counts_sum_ = 0.0;
        }
      }//if( neut )
      
      const int totalChunks = gammas[gammas.size()-1]->timeChunkNumber;
      const float dtMeasStart = meas->real_time_ * (totalChunks - 1);
      const float timecor = dtMeasStart * float(totalChunks-gamma.timeChunkNumber)/float(totalChunks);
      if( !IsNan(timecor) && !IsInf(timecor) ) //protect against UB
      {
        const chrono::seconds wholesec( static_cast<int>(floor(timecor)) );
        const chrono::microseconds fracsec( static_cast<int>(1.0E6 * (timecor-floor(timecor))) );
        meas->start_time_ -= wholesec;
        meas->start_time_ -= fracsec;
      }//if( !IsNan(timecor) && !IsInf(timecor) )
      
      signalMeasurements.push_back( meas );
    }//for( size_t i = 0; i < gammas.size(); ++i )
  }//for( int occnum = 0; occnum < occupancy_num; ++occnum )
  
  
  
  for( int backnum = 0; backnum < background_num; ++backnum )
  {
    const map<int,int>::const_iterator s1pos = background_to_s1_num.find(backnum);
    const map<int,int>::const_iterator s2pos = background_to_s2_num.find(backnum);
    
    if( s1pos == background_to_s1_num.end() || s1pos->second >= int(s1infos.size()) )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "Serious programing logic error in 1" );
#endif
      
      throw runtime_error( "Serious programing logic error in 1" );
    }
    
    const DailyFileS1Info &sinfo = s1infos[s1pos->second];
    const map<string,vector< pair<float,float> > > *devpairs = 0;
    const int ndets = static_cast<int>( detname_to_devpairs.size() );
    if( s2pos != background_to_s2_num.end() && s2pos->second < ndets )
      devpairs = &(detname_to_devpairs[s2pos->second]);
    
    const auto gammaback = gamma_backgrounds.find(backnum);
    const auto neutback = neutron_backgrounds.find(backnum);
    const auto backtimestamp = end_background.find(backnum);
    
    if( gammaback == gamma_backgrounds.end() )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "Serious programing logic error in 1.1" );
#endif
      
      throw runtime_error( "Serious programing logic error in 1.1" );
    }
    
    if( backtimestamp == end_background.end() )
    {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "Serious programing logic error in 1.2" );
#endif
      
      throw runtime_error( "Serious programing logic error in 1.2" );
    }
    
    const vector<std::shared_ptr<DailyFileGammaBackground> > &backgrounds = gammaback->second;
    const SpecUtils::time_point_t &timestamp = backtimestamp->second;
    
    for( size_t i = 0; i < backgrounds.size(); ++i )
    {
      const DailyFileGammaBackground &back = *backgrounds[i];
      
#if( DO_SDF_MULTITHREAD )
      if( !back.success )
      {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        log_developer_error( __func__, "Error Parsing gamma background" );
#endif
        
        throw runtime_error( "Error Parsing gamma background" );
      }//if( !back.success )
#endif
      
      std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
      
      meas->source_type_        = SourceType::Background;
      meas->detector_name_      = back.detectorName;
      meas->detector_number_    = detNameToNum[back.detectorName];
      meas->gamma_counts_       = back.spectrum;
      meas->start_time_         = timestamp;
      
      meas->occupied_           =  OccupancyStatus::NotOccupied;
      
      meas->sample_number_ = 1000*(max_occupancie_num+1) + backnum;
      
      if( !meas->gamma_counts_ )
      {
        meas->parse_warnings_.emplace_back( "Warning, invalid gamma counts" );
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        log_developer_error( __func__, meas->parse_warnings_.back().c_str() );
#endif
      }else if( static_cast<int>(meas->gamma_counts_->size()) != sinfo.nchannels )
      {
        meas->parse_warnings_.emplace_back( "Warning, mismatch in spectrum size, got "
                                        + std::to_string(meas->gamma_counts_->size())
                                        + " expected " + std::to_string(sinfo.nchannels) );;
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        log_developer_error( __func__, meas->parse_warnings_.back().c_str() );
#endif
      }//if( invalid gamma counts )...
      
      
      const size_t nchannel = meas->gamma_counts_ ? meas->gamma_counts_->size() : size_t(0);
      if( !sinfo.calibcoefs.empty() && (nchannel > 1) )
      {
        vector<pair<float,float>> thesedevpairs;
        if( devpairs )
        {
          /// \TODO: I am totally not sure about these deviation pairs - need to check logic of
          ///        getting them
          
          auto pos = devpairs->find(meas->detector_name_);
          if( pos != end(*devpairs) )
            thesedevpairs = pos->second;
        }
        
        const EnergyCalInfo_t key{ nchannel, sinfo.calibcoefs, thesedevpairs };
        auto pos = previous_cals.find( key );
        if( pos != end(previous_cals) )
        {
          assert( pos->second );
          meas->energy_calibration_ = pos->second;
        }else
        {
          try
          {
            auto newcal = make_shared<EnergyCalibration>();
            newcal->set_polynomial( nchannel, sinfo.calibcoefs, {} );
            meas->energy_calibration_ = newcal;
            previous_cals[key] = newcal;
          }catch( std::exception &e )
          {
            meas->parse_warnings_.push_back( "Invalid energy cal found: " + string(e.what()) );
          }
        }//if( we can re-use calibration ) / else
      }//if( we have energy calibration information )
      
      meas->gamma_count_sum_ = 0.0;
      if( meas->gamma_counts_ )
      {
        for( const float f : *meas->gamma_counts_ )
          meas->gamma_count_sum_ += f;
      }//if( !!meas->gamma_counts_ )
      
      meas->contained_neutron_ = false;
      if( neutback != neutron_backgrounds.end() )
      {
        const DailyFileNeutronBackground &neutbackground = *neutback->second;
        
#if( DO_SDF_MULTITHREAD )
        if( !neutbackground.success )
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          log_developer_error( __func__, "Error Parsing neutron background" );
#endif
          
          throw runtime_error( "Error Parsing neutron background" );
        }
#endif
        
        meas->live_time_ = meas->real_time_ = neutbackground.realTime;
        const int nneutdet = static_cast<int>( neutbackground.counts.size() );
        if( meas->detector_number_ < nneutdet )
        {
          const float counts = neutbackground.counts[meas->detector_number_];
          meas->neutron_counts_.resize( 1 );
          meas->neutron_counts_[0] = counts;
          meas->neutron_counts_sum_ = counts;
          meas->contained_neutron_ = true;
        }//if( meas->detector_number_ < neutbackground.counts.size() )
      }//if( neutback != neutron_backgrounds.end() )
      
      backgroundMeasurements.push_back( meas );
    }//for( size_t i = 0; i < backgrounds.size(); ++i )
  }//for( int backnum = 0; backnum < background_num; ++backnum )
  
  
  for( std::shared_ptr<Measurement> &m : signalMeasurements )
    measurements_.push_back( m );
  
  for( std::shared_ptr<Measurement> &m : backgroundMeasurements )
    measurements_.push_back( m );
  
  
  for( size_t i = 0; i < s1infos.size(); ++i )
  {
    const DailyFileS1Info &sinfo = s1infos[i];
    remarks_.push_back( "Algorithm Version: " + sinfo.algorithmVersion );
    remarks_.push_back( "Portal Type: " + sinfo.appTypeStr );  //SPM, RDSC, MRDIS
    instrument_type_ = sinfo.detTypeStr;
    
    if( sinfo.appTypeStr == "SPM" )
      instrument_model_ = "ASP";
    else if( sinfo.appTypeStr == "RDSC" )
      instrument_model_ = "Radiation Detector Straddle Carrier";
    else if( sinfo.appTypeStr == "MRDIS" )
      instrument_model_ = "Mobile Radiation Detection and Identification System";
    
    for( const DailyFileS1Info::params &p : sinfo.parameters )
    {
      string remark = p.name + " = " + p.value;
      if( p.attname.size() && p.attval.size() )
        remark += ", " + p.attname + " = " +p.attval;
      remarks_.push_back( remark );
    }//for( const DailyFileS1Info::params &p : sinfo.parameters )
  }//for( size_t i = 0; i < s1infos.size(); ++i )
  
  
#if( SpecUtils_REBIN_FILES_TO_SINGLE_BINNING )
  cleanup_after_load( StandardCleanup | DontChangeOrReorderSamples );
#else
  cleanup_after_load();
#endif
  }catch( std::exception & )
  {
    reset();
    
    input.clear();
    input.seekg( orig_pos, ios::beg );
    
    return false;
  }//try / catch
  
  //if( properties_flags_ & kNotSampleDetectorTimeSorted )
  //  cerr << "load_from_spectroscopic_daily_file: kNotSampleDetectorTimeSorted is set" << endl;
  //
  //if( properties_flags_ & kNotTimeSortedOrder )
  //  cerr << "load_from_spectroscopic_daily_file: kNotTimeSortedOrder is set" << endl;
  //
  //if( properties_flags_ & kNotUniqueSampleDetectorNumbers )
  //  cerr << "load_from_spectroscopic_daily_file: kNotUniqueSampleDetectorNumbers is set" << endl;
  //
  //if( properties_flags_ & kAllSpectraSameNumberChannels )
  //  cerr << "load_from_spectroscopic_daily_file: kAllSpectraSameNumberChannels is set" << endl;
  //
  //if( properties_flags_ & kHasCommonBinning )
  //  cerr << "load_from_spectroscopic_daily_file: kHasCommonBinning is set" << endl;
  
  
  return true;
}//bool load_from_spectroscopic_daily_file( std::istream &input )

}//namespace SpecUtils





