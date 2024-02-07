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
#include <cctype>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <functional>

#include "3rdparty/date/include/date/date.h"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
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
  
  bool toInt( const std::string &str, int &f )
  {
    const int nconvert = sscanf( str.c_str(), "%i", &f );
    return (nconvert == 1);
  }
}//namespace


namespace SpecUtils
{
  
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
  
  uint8_t first_bytes[4] = { 0, 0, 0, 0 };
  file.read( (char *) (&first_bytes), 4 );
  file.seekg( 0, ios::beg );
  
  // Usually the '$' character is the first character in the file, however, there may be a
  //  Byte Order Mark (BOM) indicating UTF-8, UTF-16 little-endian, or UTF-16 big-endian (We'll ignore
  const bool is_ascii = (first_bytes[0] == '$');
  
  const bool is_utf8 = ((first_bytes[0] == 0xEF)
                        && (first_bytes[1] == 0xBB)
                        && (first_bytes[2] == 0xBF)
                        && (first_bytes[3] == '$'));
  
  const bool is_utf16_big_endian = ((first_bytes[0] == 0xFE)
                                    && (first_bytes[1] == 0xFF)
                                    && (first_bytes[3] == '$'));
  
  const bool is_utf16_little_endian = ((first_bytes[0] == 0xFF)
                                       && (first_bytes[1] == 0xFE)
                                       && (first_bytes[2] == '$'));
  
  // TODO: accept UTF-32 (BE) = {0x00, 0x00, 0xFE, 0xFF }, UTF-32 (LE) = { 0xFF, 0xFE, 0x00, 0x00 }
  
  if( !is_ascii && !is_utf8 && !is_utf16_big_endian && !is_utf16_little_endian )
  {
    //    cerr << "IAEA file '" << filename << "'does not have expected first character"
    //         << " of '$', firstbyte=" << int(first_bytes[0])
    //         << " (" << char(first_bytes[0]) << ")" << endl;
    return false;
  }//if( (first_bytes[0] != '$') && (first_bytes[2] != '$') )
  
  
  bool loaded = false;
  
  if( is_ascii || is_utf8 )
  {
    if( is_utf8 )
      file.seekg( 3, ios::beg );
    
    loaded = load_from_iaea( file );
  }else if( is_utf16_big_endian || is_utf16_little_endian )
  {
    file.seekg( 0, ios::end );
    const istream::pos_type eof_pos = file.tellg();
    file.seekg( 2, ios::beg );
    const size_t filelen = static_cast<size_t>( 0 + eof_pos );
    
    // If larger than 1 MB, this probably isnt an ASCII SPE file
    if( filelen > 1024*1024 || filelen <= 256 )
      return false;
    
    const size_t content_len_bytes = (filelen - 2) + (filelen % 2);
    const size_t content_len_wchar = content_len_bytes / 2;
    
    assert( (content_len_bytes % 2) == 0 );
    assert( 2*content_len_wchar == content_len_bytes );
    
    stringstream file_contents_utf8;
    
    // Read file contents into wide-string, and then convert to UTF-8; we'll do it scoped to release
    //  memory as we can.
    //  TODO: Use a stream-based UTF-16 to UTF-8 converter; there is an untested skeleton for doing
    //        this in #SpecFile::load_txt_or_csv_file
    {//begin read wide and convert to utf-8
      std::wstring wide_contents( content_len_wchar, wchar_t('\0') );
      
      std::vector<char> raw_data;
      file.unsetf(ios::skipws);
      raw_data.resize( content_len_bytes, '\0' );
      
      //
      if( !file.read( &raw_data.front(), static_cast<streamsize>(filelen - 2) ) )
      {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        log_developer_error( __func__, "Error reading UTF-16 file contents into memory" );
#endif
        return false;
      }//if( !file.read( &raw_data.front(), static_cast<streamsize>(filelen - 2) ) )
      
      
      for( size_t i = 0; (i + 1) < raw_data.size(); i += 2 )
      {
        wchar_t lower_byte = is_utf16_little_endian ? raw_data[i] : raw_data[i+1];
        wchar_t upper_byte = is_utf16_little_endian ? raw_data[i+1] : raw_data[i];
        wchar_t &val = wide_contents[i/2];
        val = (lower_byte | (upper_byte << 1));
      }
      
      file_contents_utf8 = stringstream( convert_from_utf16_to_utf8(wide_contents) );
    }//end read wide and convert to utf-8

    
    loaded = load_from_iaea( file_contents_utf8 );
  }//if( is_ascii ) / else
  
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_iaea_file(...)


bool SpecFile::load_from_iaea( std::istream& istr )
{
  //channel data in $DATA:
  //live time, real time in $MEAS_TIM:
  //measurement datetime in $DATE_MEA:
  //Description in $SPEC_ID
  //Ploynomial calibration coefficients in $ENER_FIT: as well as $MCA_CAL:
  
  if( !istr.good() )
    return false;
  
  const istream::pos_type orig_pos = istr.tellg();
  
  reset();
  std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
  std::shared_ptr<LocationState> location;
  
  
  //Each line should be terminated with "line feed" (0Dh)
  // and "carriage return" (0Ah), but for now I'm just using safe_get_line(...),
  // just to be safe.
  bool skip_getline = false;
  
  try
  {
    string line;
    while( SpecUtils::safe_get_line( istr, line ) )
    {
      trim(line);
      if( !line.empty() )
        break;
    }//while( SpecUtils::safe_get_line( istr, line ) )
    
    
    //If someone opened the file up in a Windows text editor, the file may have
    //  gotten converted to UTF16, in which case the first two bytes will be
    //  ByteOrderMarker [0xFF,0xFE] (if using little endian order, or could be
    //  other way around); UTF8 is [0xEF,0xBB,0xBF].
    //In principle could detect and account for this, but whatever for now.
    //  istr.imbue(std::locale(istr.getloc(), new std::codecvt_utf16<wchar_t, 0x10ffff, std::consume_header>));
    //https://en.wikipedia.org/wiki/Byte_order_mark
    
    if( line.empty() || line[0]!='$' )
      throw runtime_error( "IAEA file first line must start with a '$'" );
    
    bool neutrons_were_cps = false;
    std::shared_ptr<DetectorAnalysis> anaresult;
    vector<float> cal_coeffs;
    vector<pair<int,float> > bin_to_energy;
    vector<pair<float,float>> deviation_pairs;
    
    
    //Lambda to set energy calibration and add the current Measurement to the SpecFile.
    //  This is primarily incase there are files with multiple records (delineated by the
    //  "$ENDRECORD:" tag), but I havent actually seen this, so mostly untested.
    auto cleanup_current_meas = [&](){
      if( !meas )
        return;
      
      const size_t nchannel = meas->gamma_counts_ ? meas->gamma_counts_->size() : size_t(0);
      
      if( nchannel > 1 )
      {
        //Sometimes coefficents are all zero, resulting in a parse warning of an invalid
        //  calibratration, so lets avoid this
        while( !cal_coeffs.empty() && (cal_coeffs.back() == 0.0f) )
          cal_coeffs.resize( cal_coeffs.size() - 1 );
        
        if( !cal_coeffs.empty() )
        {
          try
          {
            auto newcal = make_shared<EnergyCalibration>();
            newcal->set_polynomial( nchannel, cal_coeffs, deviation_pairs );
            meas->energy_calibration_ = newcal;
            
            if( bin_to_energy.empty() )
              meas->parse_warnings_.push_back( "A lower channel energy calibration was also specified in file, but not used." );
          }catch( std::exception &e )
          {
            meas->parse_warnings_.push_back( "Energy cal provided invalid: " + string(e.what()) );
          }//try / catch
        }//if( !cal_coeffs.empty() )
        
        
        if( !bin_to_energy.empty()
           && (!meas->energy_calibration_ || !meas->energy_calibration_->valid()) )
        {
          try
          {
            const size_t nlower = bin_to_energy.size();
            if( (nchannel != nlower) && ((nchannel+1) != nlower) )
              throw runtime_error( "Invalid number of lower channel energies ("
                                  + std::to_string(nlower) + ") for "
                                  + std::to_string(nchannel) + " gamma channels." );
            
            int prev_chan_num = bin_to_energy[0].first - 1;
            vector<float> lower_energies( nlower );
            for( size_t i = 0; i < nlower; ++i )
            {
              const int chan_num = bin_to_energy[i].first;
              if( chan_num != (prev_chan_num + 1) )
                throw runtime_error( "Channels not in increasing number ("
                                     + std::to_string(chan_num) + " follows "
                                     + std::to_string(prev_chan_num) + ")" );
              
              prev_chan_num = chan_num;
              lower_energies[i] = bin_to_energy[i].second;
            }//for( size_t i = 0; i < nlower; ++i )
            
            auto newcal = make_shared<EnergyCalibration>();
            newcal->set_lower_channel_energy(nchannel, std::move(lower_energies));
            meas->energy_calibration_ = newcal;
          }catch( std::exception &e )
          {
            meas->parse_warnings_.push_back( "Invalid lower channel energies: " + string(e.what()) );
            
            const size_t num = bin_to_energy.size();
            
            if( num )
            {
              stringstream remarkstrm;
              remarkstrm << "Calibration in file from:";
              for( size_t i = 0; i < num && i < 5; ++i )
                remarkstrm << (i?", ":" ") << "bin " << bin_to_energy[i].first
                           << "->" << bin_to_energy[i].second << " keV";
              
              if( num > 5 )
              {
                remarkstrm << " ... ";
              
                for( size_t i = num - 5; i < num; ++i )
                  remarkstrm << (i?", ":" ") << "bin " << bin_to_energy[i].first
                    << "->" << bin_to_energy[i].second << " keV";
              }//if( num > 5 )
              
              meas->remarks_.push_back( remarkstrm.str() );
            }//if( num )
          }//try / catch
        }//if( we dont have energy cal yet, and we do have bin_to_energy )
      }//if( nchannel > 1 )
      
      cal_coeffs.clear();
      deviation_pairs.clear();
      
      if( neutrons_were_cps )
      {
        if( meas->real_time_ > 0.0f )
        {
          meas->neutron_counts_sum_ *= meas->real_time_;
          for( size_t i = 0; i < meas->neutron_counts_.size(); ++i )
            meas->neutron_counts_[i] *= meas->real_time_;
        }else
        {
          meas->remarks_.push_back( "Neutron counts is in counts per second (real time was zero, so could not determine gross counts)" );
        }
      }//if( neutrons_were_cps )
      
      if( location )
        meas->location_ = location;
      
      if( (measurements_.empty() || measurements_.back()!=meas) && nchannel > 0 )
        measurements_.push_back( meas );
    };//cleanup_current_meas lambda
    
    
    do
    {
      trim(line);
      to_upper_ascii(line);
      skip_getline = false;
      
      if( starts_with(line,"$DATA:") )
      {
        //RadEagle files contains a seemingly duplicate section of DATA: $TRANSFORMED_DATA:
        
        if( !SpecUtils::safe_get_line( istr, line ) )
          throw runtime_error( "Error reading DATA section of IAEA file" );
        
        trim(line);
        vector<string> channelstrs;
        split( channelstrs, line, " \t," );
        
        int firstchannel = 0, lastchannel = 0;
        if( channelstrs.size() == 2 )
        {
          if( !parse_int(channelstrs[0].c_str(), channelstrs[0].size(), firstchannel)
              || !parse_int(channelstrs[1].c_str(), channelstrs[1].size(), lastchannel) )
          {
            firstchannel = lastchannel = 0;
          }
        }else
        {
          parse_warnings_.emplace_back( "Error reading DATA section of IAEA file, "
                                       "unexpected number of fields in first line." );
        }//if( firstlineparts.size() == 2 )
        
        double sum = 0.0;
        std::shared_ptr< vector<float> > channel_data( new vector<float>() );
        if( (firstchannel < lastchannel)
           && (firstchannel >= 0)
           && ((lastchannel - firstchannel) < (65536 + 2)) )
        {
          channel_data->reserve( static_cast<size_t>(lastchannel - firstchannel) + 1 );
        }
        
        //TODO: for some reason I think this next test condition is a little fragile...
        int num_cd_error = 0, num_cd_error_current = 0;
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )

          if( line.empty() )
            continue;
          
          vector<float> linevalues;
          const bool ok = SpecUtils::split_to_floats( line.c_str(), line.size(), linevalues );
          
          if( !ok )
          {
            num_cd_error += 1;
            num_cd_error_current += 1;
            
            char buffer[1024];
            snprintf( buffer, sizeof(buffer),
                     "Error converting channel data to counts for line: '%s'",
                     line.c_str() );
            buffer[sizeof(buffer)-1] = '\0';
            if( num_cd_error < 2 )
            {
              meas->parse_warnings_.emplace_back( buffer );
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
              log_developer_error( __func__, buffer );
#endif
            }//if( num_cd_error < 2 )
            
            // We'll allow for one poorly defined line in a row, then we'll abort the $DATA section.
            //  Note: this condition is not based on any particular data files seen
            if( num_cd_error_current > 1 )
            {
              meas->parse_warnings_.emplace_back( "$DATA section seems to be improperly terminated" );
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
              log_developer_error( __func__, buffer );
#endif
              break;
            }
            
            continue;
          }//if( !ok )
          
          num_cd_error_current = 0;
          
          //          sum += std::accumulate( linevalues.begin(), linevalues.end(), std::plus<float>() );
          for( size_t i = 0; i < linevalues.size(); ++i )
            sum += linevalues[i];
          channel_data->insert( channel_data->end(), linevalues.begin(), linevalues.end() );
        }//while( we havent reached the nex section )
        
        meas->gamma_counts_ = channel_data;
        meas->gamma_count_sum_ = sum;
      }else if( starts_with(line,"$MEAS_TIM:") )
      {
        if( !SpecUtils::safe_get_line( istr, line ) )
          throw runtime_error( "Error reading MEAS_TIM section of IAEA file" );
        vector<string> fields;
        split( fields, line, " \t," );
        if( fields.size() == 2 )
        {
          meas->live_time_ = static_cast<float>( atof( fields[0].c_str() ) );
          meas->real_time_ = static_cast<float>( atof( fields[1].c_str() ) );
          if( meas->real_time_ <= std::numeric_limits<float>::epsilon() )
            meas->real_time_ = meas->live_time_;
        }else
        {
          parse_warnings_.emplace_back( "Error reading MEAS_TIM section of IAEA file, "
                                       "unexpected number of fields." );
        }//if( firstlineparts.size() == 2 )
      }else if( starts_with(line,"$DATE_MEA:") )
      {
        if( !SpecUtils::safe_get_line( istr, line ) )
          throw runtime_error( "Error reading MEAS_TIM section of IAEA file" );
        
        trim(line);
        
        try
        {
          //Nominally formated like: "mm/dd/yyyy hh:mm:ss" (ex "02/29/2016 14:31:47")
          //  which time_from_string should get right...
          meas->start_time_ = time_from_string( line, DateParseEndianType::MiddleEndianFirst );
        }catch(...)
        {
          parse_warnings_.emplace_back( "Unable to convert date/time '" + line +
                                       "' to a valid posix time" );
        }
      }else if( starts_with(line,"$SPEC_ID:") )
      {
        string remark;
        
        // There is some inconsistency with how this "$SPEC_ID:" field is used; if it is a
        //  single line, we will interpret it as the "title" of the record, if we dont detect
        //  some detector specific information.  If it is multiple lines, we will stuff it in
        //  a (file-level) remark.
        size_t num_unlabeled_spec_id_lines = 0;
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          if( line.empty() )
            continue;
          
          if( starts_with(line,"RE ") )
          {
            //Is a ORTEC RADEAGLE
            //See http://www.ortec-online.com/-/media/ametekortec/brochures/radeagle.pdf to decode model number/options
            //Example "RE 3SG-H-GPS"
            //const bool has_gps = SpecUtils::icontains(line,"-GPS");
            //const bool has_neutron = SpecUtils::icontains(line,"-H");
            const bool has_underwater = SpecUtils::icontains(line,"SGA");
            if( has_underwater )
              remarks_.push_back( "Detector has under water option" );
            
            manufacturer_ = "Ortec";
            instrument_model_ = "RadEagle " + line.substr(3);
            instrument_type_ = "RadionuclideIdentifier";
          }else if( starts_with(line,"SN#") )
          {
            instrument_id_ = line.substr(3);
            trim(instrument_id_);
          }else if( starts_with(line,"HW#") )
          { //ex. "HW# HW 2.1 SW 2.34"
            line = line.substr(3);
            string hw, sw;
            const size_t hw_pos = line.find("HW");
            if( hw_pos != string::npos )
            {
              hw = line.substr( hw_pos + 2 );
              const size_t pos = hw.find("SW");
              if( pos != string::npos )
                hw = hw.substr(0,pos);
            }//if( hw_pos != string::npos )
            
            const size_t sw_pos = line.find("SW");
            if( sw_pos != string::npos )
            {
              sw = line.substr( sw_pos + 2 );
              const size_t pos = sw.find("HW");
              if( pos != string::npos )
                sw = sw.substr(0,pos);
            }//if( sw_pos != string::npos )
            
            trim( hw );
            trim( sw );
            
            if( !hw.empty() )
              component_versions_.push_back( pair<string,string>("HardwareVersion",hw) );
            
            if( !sw.empty() )
              component_versions_.push_back( pair<string,string>("SoftwareVersion",sw) );
            
            // }else if( starts_with(line,"AW#") )  //Not sure what AW is
            //{
          }else
          {
            num_unlabeled_spec_id_lines += !line.empty();
            remark += (!remark.empty() ? " " : "") + line;
          }
        }//while( SpecUtils::safe_get_line( istr, line ) )
        
        if( num_unlabeled_spec_id_lines == 1 )
          meas->title_ += remark;
        else if( !remark.empty() )
          remarks_.push_back( remark );
      }else if( starts_with(line,"$ENER_FIT:")
               || starts_with(line,"$GAIN_OFFSET_XIA:") )
      {
        if( !starts_with(line,"$GAIN_OFFSET_XIA:") || cal_coeffs.empty() )
        {
          if( !SpecUtils::safe_get_line( istr, line ) )
            throw runtime_error( "Error reading ENER_FIT section of IAEA file" );
          trim(line);
          if( !SpecUtils::split_to_floats( line.c_str(), line.size(), cal_coeffs ) )
            cal_coeffs.clear();
        }else
        {
          SpecUtils::safe_get_line( istr, line );
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
        }
      }else if( starts_with(line,"$MCA_CAL:") )
      {
        try
        {
          if( !SpecUtils::safe_get_line( istr, line ) )
            throw runtime_error("Error reading MCA_CAL section of IAEA file");
          trim(line);
          
          const int npar = atoi( line.c_str() );
          
          if( line.empty() || npar < 1 )
            throw runtime_error("Invalid number of parameters");
          
          if( !SpecUtils::safe_get_line( istr, line ) )
            throw runtime_error("Error reading MCA_CAL section of IAEA file");
          trim(line);
          
          
          //Often times the line will end with "keV".  Lets get rid of that to
          //  not trip up developers checks
          if( SpecUtils::iends_with(line, "kev") )
          {
            line = line.substr(0,line.size()-3);
            SpecUtils::trim( line );
          }
            
          const bool success = split_to_floats( line.c_str(), line.size(), cal_coeffs );
            
          if( !success )
            cal_coeffs.clear();
            
          //make sure the file didnt just have all zeros
          bool allZeros = true;
          for( const float c : cal_coeffs )
            allZeros = allZeros && (fabs(c) < 1.0E-08);
            
          if( !allZeros && (cal_coeffs.size() != npar) )
          {
            string msg = "Unexpected number of calibration parameters in IAEA file, expected "
                         + to_string(npar) + " found " + to_string(cal_coeffs.size());
            parse_warnings_.emplace_back( std::move(msg) );
          }
        }catch( exception &e )
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          const string msg = "Error in MCA_CAL section of IAEA file\n\t" + string(e.what());
          log_developer_error( __func__, msg.c_str() );
#endif
        }
      }else if( starts_with(line,"$GPS:") )
      {
        float speed = numeric_limits<float>::quiet_NaN();
        double longitude = numeric_limits<double>::quiet_NaN(), latitude = numeric_limits<double>::quiet_NaN();
        
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          string valuestr;
          const string::size_type pos = line.find( "=" );
          if( pos != string::npos && ((pos+1) < line.size()) )
            valuestr = line.substr( pos + 1 );
          trim( valuestr );
          
          if( starts_with( line, "Lon=") )
          {
            if( !parse_double(valuestr.c_str(), valuestr.size(), longitude) )
              longitude = numeric_limits<double>::quiet_NaN();
          }else if( starts_with( line, "Lat=") )
          {
            if( !parse_double(valuestr.c_str(), valuestr.size(), latitude) )
              latitude = numeric_limits<double>::quiet_NaN();
          }else if( starts_with( line, "Speed=") )
          {
            if( !parse_float( valuestr.c_str(), valuestr.size(), speed) )
              speed = numeric_limits<float>::quiet_NaN();
          }else if( !line.empty() )
            remarks_.push_back( line ); //also can be Alt=, Dir=, Valid=
        }//while( SpecUtils::safe_get_line( istr, line ) )
        
        if( !IsNan(speed) || (valid_longitude(longitude) && valid_latitude(latitude)) )
        {
          if( !location )
          {
            location = make_shared<LocationState>();
            location->type_ = LocationState::StateType::Instrument;
          }
          
          location->speed_ = speed;
          if( valid_longitude(longitude) && valid_latitude(latitude) )
          {
            auto geo = make_shared<GeographicPoint>();
            geo->longitude_ = longitude;
            geo->latitude_ = latitude;
            location->geo_location_ = geo;
          }//if( valid GPS coordinates )
        }//if( we read in something that will go in LocationState )
      }else if( starts_with(line,"$GPS_COORDINATES:") )
      {
        if( SpecUtils::safe_get_line( istr, line ) )
          remarks_.push_back( "GPS Coordinates: " + line );
      }else if( starts_with(line,"$NEUTRONS:") )
      { //ex "0.000000  (total)"
        if( !SpecUtils::safe_get_line( istr, line ) )
          throw runtime_error("Error reading NEUTRONS section of IAEA file");
        trim( line );
        float val;
        if( toFloat(line,val) )
        {
          meas->neutron_counts_.push_back( val );
          meas->neutron_counts_sum_ += val;
          meas->contained_neutron_ = true;
        }else
          parse_warnings_.push_back( "Error parsing neutron counts from line: " + line );
      }else if( starts_with(line,"$NEUTRONS_LIVETIME:") )
      { //ex "267706.437500  (sec)"
        if( !SpecUtils::safe_get_line( istr, line ) )
          throw runtime_error("Error reading NEUTRONS_LIVETIME section of IAEA file");
        trim( line );
        meas->remarks_.push_back( "Neutron Live Time: " + line );
      }else if( starts_with(line,"$NEUTRON_CPS:") )
      { //found in RadEagle SPE files
        if( !SpecUtils::safe_get_line( istr, line ) )
          throw runtime_error("Error reading NEUTRON_CPS section of IAEA file");
        trim( line );
        float val;
        if( toFloat(line,val) )
        {
          neutrons_were_cps = true;
          meas->neutron_counts_.push_back( val );
          meas->neutron_counts_sum_ += val;
          meas->contained_neutron_ = true;
        }else
        {
          parse_warnings_.emplace_back( "Error parsing neutron cps from line: " + line );
        }
      }else if( starts_with(line,"$SPEC_REM:") )
      {
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          if( !line.empty() )
            meas->remarks_.push_back( line );
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$ROI:") )
      {
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          //          if( !line.empty() )
          //            meas->remarks_.push_back( line );
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$ROI_INFO:") )
      {
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          if( !line.empty() )
          {
            vector<float> parts;
            SpecUtils::split_to_floats( line.c_str(), line.size(), parts );
            if( parts.size() > 7 )
            {
              const int roinum     = static_cast<int>( parts[0] );
              const int startbin   = static_cast<int>( parts[1] );
              const int endbin     = static_cast<int>( parts[2] );
              const float meanbin  = parts[3];
              const float fwhmbins = parts[4];
              const int roiarea    = static_cast<int>( parts[5] );
              const int peakarea   = static_cast<int>( parts[6] );
              const int areauncert = static_cast<int>( parts[7] );
              
              char buffer[512];
              snprintf( buffer, sizeof(buffer),
                       "ROI in file: { \"roinum\": %i, \"startbin\": %i,"
                       " \"endbin\": %i, \"meanbin\": %.2f, \"fwhmbins\": %.2f,"
                       " \"roiarea\": %i, \"peakarea\": %i, "
                       "\"peakareauncert\": %i }",
                       roinum, startbin, endbin, meanbin, fwhmbins, roiarea,
                       peakarea, areauncert );
              
              meas->remarks_.push_back( buffer );
            }//if( parts.size() > 7 )
          }
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$ENER_DATA:")
                || starts_with(line,"$MCA_CAL_DATA:")
                || starts_with(line,"$ENER_TABLE:") )
      {
        if( SpecUtils::safe_get_line( istr, line ) )
        {
          const size_t num = static_cast<size_t>( atol( line.c_str() ) );
          while( SpecUtils::safe_get_line( istr, line ) )
          {
            trim(line);
            if( starts_with(line,"$") )
            {
              skip_getline = true;
              break;
            }//if( we have overrun the data section )
            
            if( !line.empty() )
            {
              vector<float> parts;
              SpecUtils::split_to_floats( line.c_str(), line.size(), parts );
              if( parts.size() == 2 )
              {
                // Only add this point if the channel and energy are reasonable.
                const int bin = float_to_integral<int>(parts[0]);
                const float energy = parts[1];
                if( (bin >= 0) && (bin < 131072) && (energy >= 0.0f) && (energy < 3000000.0f) )
                  bin_to_energy.emplace_back( bin, parts[1] );
              }
            }
          }//while( SpecUtils::safe_get_line( istr, line ) )
        }//if( file says how many entries to expect )
      }else if( starts_with(line,"$SHAPE_CAL:") )
      {
        //I think this is FWHM calibration parameters - skipping this for now
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
        }//while( SpecUtils::safe_get_line( istr, line ) )
        
        /*
         if( !SpecUtils::safe_get_line( istr, line ) )
         throw runtime_error("Error reading SHAPE_CA section of IAEA file");
         trim(line);
         unsigned int npar;
         if( !(stringstream(line) >> npar ) )
         throw runtime_error( "Invalid number of parameter: " + line );
         
         if( !SpecUtils::safe_get_line( istr, line ) )
         throw runtime_error("Error reading SHAPE_CA section of IAEA file");
         trim(line);
         
         vector<float> coefs;
         vector<string> fields;
         split( fields, line, " \t," );
         try
         {
         for( const string &field : fields )
         {
         float val;
         if( !(stringstream(field) >> val) )
         throw runtime_error( "Invalid calibration parameter: " + field );
         coefs.push_back( val );
         }
         
         if( npar != coefs.size() )
         throw runtime_error( "Unexpected number of parameters in SHAPE_CA block of IAEA file." );
         }catch(...)
         {
         }
         */
      }else if( starts_with(line,"$PEAKLABELS:") )
      {
        //XXX - it would be nice to keep the peak lables, or at least search for
        //      these peaks and assign the isotopes..., but for now we'll ignore
        //      them :(
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$CAMBIO:") )
      {
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$APPLICATION_ID:") )
      {
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          //line something like "identiFINDER 2 NG, Application: 2.37, Operating System: 1.2.040"
          if( line.size() )
            remarks_.push_back( line );
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$DEVICE_ID:") )
      {
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          if( SpecUtils::icontains(line, "identiFINDER") )
          {
            if( SpecUtils::icontains(line, "LG") )
            {
              detector_type_ = DetectorType::IdentiFinderLaBr3;
              instrument_model_ = line;
              manufacturer_ = "FLIR";
              
              if( SpecUtils::icontains( line, "LGH" ) )
                meas->contained_neutron_ = true;
            }else if( SpecUtils::icontains( line, "NG") )
            {
              detector_type_ = DetectorType::IdentiFinderNG;
              instrument_model_ = line;
              manufacturer_ = "FLIR";
              
              if( SpecUtils::icontains( line, "NGH" ) )
                meas->contained_neutron_ = true;
            }else if( SpecUtils::icontains( line, "T1") || SpecUtils::icontains( line, "T2") )
            {
              detector_type_ = DetectorType::IdentiFinderTungsten;
              instrument_model_ = line;
              manufacturer_ = "FLIR";
            }else
            {
              instrument_model_ = line;
              manufacturer_ = "FLIR";
              detector_type_ = DetectorType::IdentiFinderUnknown;
            }
          }else if( SpecUtils::istarts_with( line, "SN#") )
          {
            line = line.substr(3);
            SpecUtils::trim( line );
            if( line.size() )
              instrument_id_ = line;
          }else
            remarks_.push_back( line );
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$FLIR_DATASET_NUMBER:") )
      {
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          //line something like "identiFINDER 2 NG, Application: 2.37, Operating System: 1.2.040"
          if( line.size() )
            remarks_.push_back( "FLIR DATSET NUMBER: " + line );
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$FLIR_GAMMA_DETECTOR_INFORMATION:") )
      {
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          if( line.size() )
            remarks_.push_back( "GAMMA DETECTOR INFORMATION: " + line );
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$FLIR_NEUTRON_DETECTOR_INFORMATION:") )
      {
        meas->contained_neutron_ = true;
        
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          if( line.size() )
            remarks_.push_back( "NEUTRON DETECTOR INFORMATION: " + line );
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$FLIR_SPECTRUM_TYPE:") )
      {
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          if( SpecUtils::icontains( line, "IntrinsicActivity" ) )
            meas->source_type_ = SourceType::IntrinsicActivity;
          else if( SpecUtils::icontains( line, "Measurement" ) )
            meas->source_type_ = SourceType::Foreground;
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$FLIR_REACHBACK:") )
      {
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          if( line.size() )
            remarks_.push_back( "Reachback url: " + line );
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$FLIR_DOSE_RATE_SWMM:") )
      {
        //I dont understand the structure of this... so just putting in remarks.
        string remark;
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          if( line.size() )
            remark += (remark.size()?", ":"") + line;
        }//while( SpecUtils::safe_get_line( istr, line ) )
        
        if( remark.size() )
          remarks_.push_back( "Dose information: " + remark );
      }else if( starts_with(line,"$FLIR_ANALYSIS_RESULTS:") )
      {
        vector<string> analines;
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          if( !line.empty() )
            analines.push_back( line );
        }//while( SpecUtils::safe_get_line( istr, line ) )
        
        if( analines.empty() )
          continue;
        
        int numresults = 0;
        if( !toInt(analines[0], numresults) || (numresults <= 0) )
          continue;
        
        if( (analines.size()-1) != (4*static_cast<size_t>(numresults)) )
        {
          string remark;
          for( size_t i = 0; i < analines.size(); ++i )
            remark += (remark.size()?", ":"") + analines[i];
          remarks_.push_back( "FLIR_ANALYSIS_RESULTS not in expected format: " + remark );
          continue;
        }
        
        if( !anaresult )
          anaresult = std::make_shared<DetectorAnalysis>();
        
        for( int ananum = 0; ananum < numresults; ++ananum )
        {
          DetectorAnalysisResult result;
          result.nuclide_       = analines.at(1+4*ananum); //"Th-232 or U-232"
          result.nuclide_type_  = analines.at(2+4*ananum); //"NORM"
          result.id_confidence_ = analines.at(4+4*ananum); //"5" - not really sure this is actually confidence...
          result.remark_        = analines.at(3+4*ananum); //"Innocent"
          
          anaresult->results_.push_back( result );
        }//for( int ananum = 0; ananum < numresults; ++ananum )
      }else if( starts_with(line,"$DOSE_RATE:") )
      {  //Dose rate in uSv.  Seen in RadEagle
        if( !SpecUtils::safe_get_line( istr, line ) )
          throw runtime_error( "Error reading DOSE_RATE section of IAEA file" );
        
        trim(line);
        skip_getline = starts_with(line,"$");
        
        float dose_rate = 0.0f;
        if( !toFloat(line, dose_rate) )
        {
          parse_warnings_.push_back( "Error reading DOSE_RATE, line: " + line );
        }else
        {
          meas->dose_rate_ = dose_rate;
        }
      }else if( starts_with(line,"$RADIONUCLIDES:") )
      { //Have only seen one file with this , and it only had a single nuclide
        //Cs137*[9.58755]
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          //Super unsure about the formatting so far (20171218)!
          const size_t nuc_end = line.find_first_of("*[");
          if( nuc_end != string::npos )
          {
            DetectorAnalysisResult result;
            result.nuclide_ = line.substr(0,nuc_end);
            
            //result.nuclide_type_  = "";
            const size_t conf_start = line.find('[');
            const size_t conf_end = line.find(']');
            if( conf_end > conf_start && conf_end != string::npos )
              result.id_confidence_ = line.substr(conf_start+1,conf_end-conf_start-1);
            result.remark_        = line;  //20171218: include everything for until I get better confirmation of format...
            
            
            if( !anaresult )
              anaresult = std::make_shared<DetectorAnalysis>();
            anaresult->results_.push_back( result );
          }//if( nuc_end != string::npos )
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$SPEC_INTEGRAL:") )
      {
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          if( line.size() )
            remarks_.push_back( "SPEC_INTEGRAL: " + line );
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$IDENTIFY_PARAMETER:") )
      {
        vector<float> calibcoefs;
        vector< pair<float,float> > fwhms;
        while( !skip_getline && SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          const bool slope = SpecUtils::icontains( line, "Energieeichung_Steigung");
          const bool offset = SpecUtils::icontains( line, "Energieeichung_Offset");
          const bool quad = SpecUtils::icontains( line, "Energieeichung_Quadrat");
          if( slope || offset || quad )
          {
            if( SpecUtils::safe_get_line( istr, line ) )
            {
              trim(line);
              if( starts_with(line,"$") )
              {
                skip_getline = true;
                break;
              }//if( we have overrun the data section )
              
              if( slope )
              {
                if( calibcoefs.size() < 2 )
                  calibcoefs.resize( 2, 0.0f );
                if( !toFloat( line, calibcoefs[1] ) )
                  throw runtime_error( "Couldnt convert to cal slope to flt: " + line );
              }else if( offset )
              {
                if( calibcoefs.size() < 1 )
                  calibcoefs.resize( 1, 0.0f );
                if( !toFloat( line, calibcoefs[0] ) )
                  throw runtime_error( "Couldnt convert to cal offset to flt: " + line );
              }else if( quad )
              {
                if( calibcoefs.size() < 3 )
                  calibcoefs.resize( 3, 0.0f );
                if( !toFloat( line, calibcoefs[2] ) )
                  throw runtime_error( "Couldnt convert to cal quad to flt: " + line );
              }
            }//if( SpecUtils::safe_get_line( istr, line ) )
          }//if( SpecUtils::icontains( line, "Energieeichung_Steigung") )
          
          /*
           FWHM_FWHM1:
           18.5
           FWHM_Energie1:
           122
           FWHM_FWHM2:
           43.05
           FWHM_Energie2:
           662
           Detektortyp:
           2
           */
        }//while( SpecUtils::safe_get_line( istr, line ) )
        
        if( calibcoefs.size() && !cal_coeffs.size() )
          cal_coeffs = calibcoefs;
      }else if( starts_with(line,"$NON_LINEAR_DEVIATIONS:") )
      {
        SpecUtils::safe_get_line( istr, line );
        trim(line);
        
        const size_t npairs = static_cast<size_t>( atoi(line.c_str()) );
        
        if( line.empty() || npairs < 1 )
          continue;
        
        std::vector<std::pair<float,float>> pairs;
        
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          float energy, deviation;
          if( !(stringstream(line) >> energy >> deviation ) )
          {
            pairs.clear();
            break;
          }//if( !(stringstream(line) >> energy >> deviation ) )
          
          pairs.push_back( pair<float,float>(energy,deviation) );
        }//while( SpecUtils::safe_get_line( istr, line ) )
        
        if( pairs.size() == npairs )
        {
          deviation_pairs = pairs;
        }else if( npairs || pairs.size() )
        {
          char buffer[256];
          snprintf( buffer, sizeof(buffer),
                   "Error parsing deviation pairs, expected %i, read in %i; "
                   "not using", int(npairs), int(pairs.size()) );
          parse_warnings_.emplace_back( buffer );
        }//if( pairs.size() == npairs )
      }else if( starts_with(line,"$ENDRECORD:") )
      {
        cleanup_current_meas();
        
        meas = make_shared<Measurement>();
        location.reset();
      }else if( starts_with(line,"$RT:")
               || starts_with(line,"$DT:") )
      {
        //Burn off things we dont care about
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
        }
      }else if( starts_with(line,"$IDENTIFY_NUKLIDE:")
               || starts_with(line,"$IDENTIFY_PEAKS:")
               || starts_with(line,"$PRESETS:")
               || starts_with(line,"$ICD_TYPE:")
               || starts_with(line,"$TEMPERATURE:")
               || starts_with(line,"$CPS:" )
               || starts_with(line,"$PEC_ID:")
               )
      {
        //Burn off things we dont care about
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          if( line == "Live Time" )
          {
            if( SpecUtils::safe_get_line( istr, line ) )
            {
              if( starts_with(line,"$") )
              {
                skip_getline = true;
                break;
              }//if( we have overrun the data section )
              remarks_.push_back( "A preset live time of " + line + " was used" );
            }//if( SpecUtils::safe_get_line( istr, line ) )
          }//if( line == "Live Time" )
          //          if( line.size() )
          //            cout << "Got Something" << endl;
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$FLIR_NEUTRON_SWMM:")
               || starts_with(line,"$TRANSFORMED_DATA:") )
      {
        //we'll just burn through this section of the file since wcjohns doesnt
        //  understand the purpose or structure of these sections
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
        }//while( SpecUtils::safe_get_line( istr, line ) )
      }else if( starts_with(line,"$KROMEK_INFO:") )
      {
        //  "$DATE_MEA:" appears to be the *end* of the measurement, so we'll correct for that
        if( is_special(meas->start_time_)
           || (meas->real_time_ <= std::numeric_limits<float>::epsilon())
           || IsInf(meas->real_time_)
           || IsNan(meas->real_time_) )
        {
          parse_warnings_.emplace_back( "Not correcting Kromek time to be start of measurement" );
        }else
        {
          meas->start_time_ -= chrono::milliseconds( static_cast<int64_t>( std::round(1000.0*meas->real_time_) ) );
        }
        
        
        // These files will have one line like "LLD:" followed by another line with its actual value
        //  We'll grab some of this info, and stuff others in a comment
        vector<string> kromek_lines;
        while( SpecUtils::safe_get_line( istr, line ) )
        {
          trim(line);
          if( starts_with(line,"$") )
          {
            skip_getline = true;
            break;
          }//if( we have overrun the data section )
          
          kromek_lines.push_back( line );
        }//while( SpecUtils::safe_get_line( istr, line ) )
        

        for( size_t index = 0; (index + 1) < kromek_lines.size(); ++index )
        {
          const string &label = kromek_lines[index];
          const string &value = kromek_lines[index+1];
          
          if( label.find(':') == string::npos ) //make sure we actually do label: value right
            continue;
          
          if( (label == "DETECTOR_SERIAL_NO:")
             || (label == "PRODUCT_SERIAL_NO:")
             || (label == "DEVICE_SERIAL_NO:") )
          {
            if( !instrument_id_.empty() )
              instrument_id_ += ", ";
            if( instrument_id_.find(value) == string::npos )
              instrument_id_ += value;
          }else if( (label == "DETECTOR_TYPE:")
                   || (label == "DETECTOR_TYPE_ID:")
                   || (label == "PRODUCT_FAMILY:") )
          {
            if( !instrument_model_.empty() )
              instrument_model_ += ", ";
            if( instrument_model_.find(value) == string::npos )
              instrument_model_ += value;
          }else
          {
            // TODO: this should probably correspond to some parameters or something.
            remarks_.push_back( label + " " + value );
          }
        }//for( size_t index = 0; (index + 1) < kromek_lines.size(); ++index )
  
      }else if( !line.empty() && line != "END" )
      {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        log_developer_error( __func__, ("Unrecognized line '" + line + "'").c_str() );
#endif
      }
      
      /*
       *GADRAS users manual recomends having $SAMPLE_DESCRIPTION:, $DATA_TYPE:, $DETECTOR_TYPE:, $RADIATION_TYPE:, $METHOD_TYPE:, $MCA_CAL_DATA:
       *IAEA pdfs say the following are also possible.
       $ADC:
       $ADD_HV:
       $BT:
       $COUNTS:
       $DT:
       $DTC:
       $ENER_DATA:
       $ENER_DATA_X:
       $ENER_FIT:
       $FAST_DISCR:
       $GAIN_VALUE:
       $HV:
       $INPUT:
       $INSP_INFO:
       $MCA_166_ID:
       $MCA_REPEAT:
       $MEAS_TIM:
       $MCS_ADD_DATA:
       $MCS_AMP_DATA:
       $MCS_AMP_ROI:
       $MCS_AMP_ROI_INFO:
       $MCS_CHANNELS:
       $MCS_INPUT:
       $MCS_SWEEPS:
       $MCS_TIME:
       $MODE:
       $PD_COUNTS:
       $POWER:
       $POWER_STATE:
       $PRESETS:
       $PUR:
       $PZC_VALUE:
       $REC_COUNTER:
       $REC_ERROR_COUNTER:
       $ROI:
       $ROI_INFO:
       $RT:
       $SCANDU:
       $SCANDU_RESULTS:
       $Sensors:
       $SINGLE_POINTS:
       $SLOW_DISCR:
       $SPEC_INTEGRAL:
       $SPEC_REM:
       $STAB:
       $STAB_COUNTER:
       $STAB_OFFSET:
       $STAB_OFFSET_MAX:
       $STAB_OFFSET_MIN:
       $STAB_PARAM:
       $TEMPERATURE:
       $TDF:
       $THR:
       $UF6_INSP_INFO:
       $WATCHMON_ROI_INFO:
       $WINSCAN_INFO:
       $WINSPEC_AUTOMATION:
       $WINSPEC_INFO:
       */
    }while( skip_getline || SpecUtils::safe_get_line( istr, line) );
    //  while( getline( istr, line ) )
    
    cleanup_current_meas();
    
    if( anaresult )
      detectors_analysis_ = anaresult;
    
    cleanup_after_load();
  }catch(...)
  {
    istr.clear();
    istr.seekg( orig_pos, ios::beg );
    reset();
    return false;
  }
  
  return true;
}//bool load_from_iaea( std::istream& ostr )
  

  
bool SpecFile::write_iaea_spe( ostream &output,
                                set<int> sample_nums,
                                const set<int> &det_nums ) const
{
  //www.ortec-online.com/download/ortec-software-file-structure-manual.pdf
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  //Do a sanity check on samples and detectors, event though #sum_measurements would take care of it
  //  (but doing it here indicates source a little better)
  for( const auto sample : sample_nums )
  {
    if( !sample_numbers_.count(sample) )
      throw runtime_error( "write_iaea_spe: invalid sample number (" + to_string(sample) + ")" );
  }
  if( sample_nums.empty() )
    sample_nums = sample_numbers_;
  
  vector<string> det_names;
  for( const int num : det_nums )
  {
    auto pos = std::find( begin(detector_numbers_), end(detector_numbers_), num );
    if( pos == end(detector_numbers_) )
      throw runtime_error( "write_iaea_spe: invalid detector number (" + to_string(num) + ")" );
    det_names.push_back( detector_names_[pos-begin(detector_numbers_)] );
  }
  
  if( det_nums.empty() )
    det_names = detector_names_;
  
  std::shared_ptr<Measurement> summed = sum_measurements( sample_nums, det_names, nullptr );
  
  if( !summed || !summed->gamma_counts() || summed->gamma_counts()->empty() )
    return false;
  
  try
  {
    char buffer[256];
    
    string title = summed->title();
    SpecUtils::ireplace_all( title, "\r\n", " " );
    SpecUtils::ireplace_all( title, "\r", " " );
    SpecUtils::ireplace_all( title, "\n", " " );
    
    if( title.size() )
      output << "$SPEC_ID:\r\n" << title << "\r\n";
    
    vector<string> remarks = remarks_;
    remarks.insert( remarks.end(), summed->remarks_.begin(), summed->remarks_.end() );
    
    if( remarks.size() || title.size() )
    {
      output << "$SPEC_REM:\r\n";
      
      for( string remark : remarks )
      {
        SpecUtils::ireplace_all( remark, "\r\n", " " );
        SpecUtils::ireplace_all( remark, "\r", " " );
        SpecUtils::ireplace_all( remark, "\n", " " );
        if( remark.size() )
          output << remark << "\r\n";
      }
    }//if( remarks.size() )
    
    if( !is_special(summed->start_time_) )
    {
      // mm/dd/yyyy hh:mm:ss "02/29/2016 14:31:47"
      
      const chrono::time_point<chrono::system_clock,date::days> st_as_days = date::floor<date::days>(summed->start_time_);
      const date::year_month_day st_ymd = date::year_month_day{st_as_days};
      const date::hh_mm_ss<time_point_t::duration> time_of_day = date::make_time(summed->start_time_ - st_as_days);
      
      const int year = static_cast<int>( st_ymd.year() );
      const int month = static_cast<int>( static_cast<unsigned>( st_ymd.month() ) );
      const int day = static_cast<int>( static_cast<unsigned>( st_ymd.day() ) );
      const int hour = static_cast<int>( time_of_day.hours().count() );
      const int mins = static_cast<int>( time_of_day.minutes().count() );
      const int secs = static_cast<int>( time_of_day.seconds().count() );
      
      //snprintf( buffer, sizeof(buffer), "%.2i/%.2i/%.4i %.2i:%.2i:%09.6f",
      //        month, day, year, hour, mins, (secs+frac) );
      snprintf( buffer, sizeof(buffer), "%.2i/%.2i/%.4i %.2i:%.2i:%.2i",
               month, day, year, hour, mins, secs );
      
      output << "$DATE_MEA:\r\n" << buffer << "\r\n";
    }
    
    if( summed->real_time_ > 0.0f && summed->live_time_ > 0.0f )
    {
      //output << "$MEAS_TIM:\r\n" << summed->live_time_ << " " << summed->real_time_ << "\r\n";
      snprintf( buffer, sizeof(buffer), "%.5f %.5f", summed->live_time_, summed->real_time_ );
      output << "$MEAS_TIM:\r\n" << buffer << "\r\n";
    }
    vector<float> coefs;
    const vector<float> &counts = *summed->gamma_counts_;
    if( counts.size() )
    {
      output << "$DATA:\r\n0 " << (counts.size()-1) << "\r\n";
      
      for( size_t i = 0; i < counts.size(); ++i )
      {
        const float count = counts[i];
        if( std::floorf(count) == count )
        {
          // If we print as a float, then by default above 1.0E6 will print in scientific
          //  notation, which some other applications do not read in
          output << static_cast<int64_t>(count) << "\r\n";
        }else
        {
          // If its a float, I guess we should print as a float?
          output << count << "\r\n";
        }
      }//for( size_t i = 0; i < counts.size(); ++i )
      
      assert( summed->energy_calibration_ );
      coefs = summed->energy_calibration_->coefficients();
      switch( summed->energy_calibration_->type() )
      {
        case EnergyCalType::Polynomial:
        case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
          coefs = summed->energy_calibration_->coefficients();
          break;
        
        case EnergyCalType::FullRangeFraction:
          coefs = summed->energy_calibration_->coefficients();
          coefs = fullrangefraction_coef_to_polynomial( coefs, counts.size() );
          break;
          
        case EnergyCalType::LowerChannelEdge:
        case EnergyCalType::InvalidEquationType:
          break;
      }//switch( summed->energy_calibration_->type() )
    }//if( counts.size() )
    
    if( coefs.size() )
    {
      output << "$ENER_FIT:\r\n";
      for( size_t i = 0; i < coefs.size(); ++i )
        output << (i?" ":"") << coefs[i];
      output << "\r\n";
      output << "$MCA_CAL:\r\n" << coefs.size() << "\r\n";
      for( size_t i = 0; i < coefs.size(); ++i )
        output << (i?" ":"") << coefs[i];
      output << "\r\n";
    }//if( coefs.size() )
    
    output << "$ENDRECORD:\r\n";
  }catch( std::exception & )
  {
    return false;
  }
  
  return true;
}//write_iaea_spe(...)
}//namespace SpecUtils



