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
    
    //lets make sure this is an ascii file
    //Really we should make sure its a UTF-8 file, so we can export from Excell
    while( input->good() )
    {
      const int c = input->get();
      if( input->good() && c>127 )
        return false;
    }//while( input.good() )
    
    //we have an ascii file if we've made it here
    input->clear();
    input->seekg( 0, ios_base::beg );
    
    
    //Check to see if this is a GR135 text file
    string firstline;
    SpecUtils::safe_get_line( *input, firstline );
    
    bool success = false;
    
    const bool isGR135File = contains( firstline, "counts Live time (s)" )
    && contains( firstline, "gieger" );
    
    if( isGR135File )
    {
      input->seekg( 0, ios_base::beg );
      success = load_from_Gr135_txt( *input );
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
      input->seekg( 0, ios_base::beg );
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
  
  while( istr.good() )
  {
    try
    {
      auto m = std::make_shared<Measurement>();
      m->set_info_from_txt_or_csv( istr );
      
      if( m->num_gamma_channels() < 7 && !m->contained_neutron() )
        break;
      
      measurements_.push_back( m );
    }catch( exception & )
    {
      //cerr << "SpecFile::load_from_txt_or_csv(...)\n\tCaught: " << e.what() << endl;
      break;
    }
  }//while( istr.good() )
  
  
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
  }
  
  //I feel as though this function can be improved in terms of being more robust
  //  to reading input, as well as shortened or re-factored
  //Also, I hacked to make this function very quickly, so I'm sure the code is
  //  even more so crap that the rest in this file
  reset();
  const int kChannel = 0, kEnergy = 1, kCounts = 2;//, kSecondRecord = 3;
  float energy_units = 1.0f;
  
  map<size_t,int> column_map;
  vector<string>::const_iterator pos;
  
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
    const char *delim = (line.find(',') != string::npos) ? "," : ((column_map.empty() && !isdigit(line[0])) ? "\t,;" : "\t, ;");  //Dont allow a space delimiter until we have the columns mapped out to avoid things like "Energy (keV)" counting as two columns
    
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
    
    if( isdigit(fields.at(0).at(0)) )
    {
      if( column_map.empty() )
      {
        if( nfields==1 )
        {
          column_map[0] = kCounts;
        }if( nfields==2 && isdigit(fields.at(1).at(0)) )
        {
          column_map[0] = kEnergy;
          column_map[1] = kCounts;
        }else if( nfields<9
                 && isdigit(fields.at(1).at(0)) && isdigit(fields.at(2).at(0)) )
        {
          column_map[0] = kChannel;
          column_map[1] = kEnergy;
          column_map[2] = kCounts;
        }else
        {
          throw runtime_error( string("unrecognized line that started with digit '")
                              + fields[0][0] + string("'") );
        }
      }//if( column_map.empty() )
      
      if( fields.size() == 4 )
      {
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
              
              std::shared_ptr<vector<float> > channels( new vector<float>() );
              if( SpecUtils::split_to_floats( channeldata.c_str(), channeldata.size(), *channels ) )
              {
                if( post_pos == eof_pos )
                {
                  ++nlines_used;
                  const size_t nchan = channels->size();
                  const bool validCalib
                  = SpecUtils::calibration_is_valid( SpecUtils::EnergyCalType::Polynomial, eqn,
                                                    devpairs, nchan );
                  
                  if( validCalib && nchan >= 128 )
                  {
                    gamma_counts_ = channels;
                    energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
                    calibration_coeffs_ = eqn;
                    channel_energies_ = SpecUtils::polynomial_binning( eqn, nchan, devpairs );
                    
                    break;
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
                      gamma_counts_ = channels;
                      energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
                      const size_t ncalcoef = size_t((cals[3]==0.0f) ? 2 : 3);
                      calibration_coeffs_.resize( ncalcoef );
                      for( size_t i = 0; i < ncalcoef; ++i )
                        calibration_coeffs_[i] = cals[1+i];
                      channel_energies_ = SpecUtils::polynomial_binning( calibration_coeffs_,
                                                                        channels->size(),
                                                                        std::vector<std::pair<float,float>>() );
                      
                      break;
                    }//if( some reasonalbe number of channels )
                  }//if( !!channels )
                }//if( there were exactly two lines ) / else
              }//if( could split the second line into floats )
              
            }//if( we could get a second line )
            
            //If we didnt 'break' above, we werent successful at reading the
            //  file
            istr.seekg( current_pos, ios::beg );
          }//if( potentially calibration data )
        }//if( the line was made of 4 numbers )
      }//if( fields.size() == 4 )
      
      std::shared_ptr<std::vector<float>> energies( new vector<float>() ), counts( new vector<float>());
      std::shared_ptr<vector<int> > channels( new vector<int>() );
      
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
        float energy = 0.0f, count = 0.0f;//, count2 = 0.0f;
        for( size_t col = 0; col < fields.size(); ++col )
        {
          if( column_map.count(col) )
          {
            switch( column_map[col] )
            {
              case kChannel:       channel = atoi(fields[col].c_str()); break;
              case kEnergy:        energy  = static_cast<float>(atof(fields[col].c_str())); break;
              case kCounts:        count   = static_cast<float>(atof(fields[col].c_str())); break;
                //              case kSecondRecord: count2  = static_cast<float>(atof(fields[col].c_str()));  break;
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
      
      if( energies->size() && (energies->back()!=0.0f) )
      {
        energy_calibration_model_ = SpecUtils::EnergyCalType::LowerChannelEdge;
        calibration_coeffs_ = *energies;
        //        channel_energies_ = energies;
      }else //if( channels->size() && (channels->back()!=0) )
      {
        if( (energy_calibration_model_ == SpecUtils::EnergyCalType::Polynomial)
           && (calibration_coeffs_.size() > 1) && (calibration_coeffs_.size() < 10) )
        {
          //nothing to do here
        }else
        {
          energy_calibration_model_ = SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial;
          calibration_coeffs_.resize( 2 );
          calibration_coeffs_[0] = 0.0f;
          calibration_coeffs_[1] = 3000.0f / std::max( counts->size()-1, size_t(1) );
          //        channel_energies_ = SpecUtils::polynomial_binning( calibration_coeffs_, counts->size(),
          //                                                           std::vector<std::pair<float,float>>() );
        }
      }//if( energies ) / else channels
      
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
            //  Hopefully accounting for this doesnt erroneoulsy affect other formats
            string restofline = fields[i].substr(kevpos+5);
            SpecUtils::trim(restofline);
            if( SpecUtils::istarts_with(restofline, "count")
               || SpecUtils::istarts_with(restofline, "data")
               || SpecUtils::istarts_with(restofline, "signal") )
            {
              column_map[i+1] = kCounts;
            }
          }//if( text after "(kev)" )
          
        }else if( starts_with( fields[i], "counts" )
                 || starts_with( fields[i], "data" )
                 || starts_with( fields[i], "selection" )
                 || starts_with( fields[i], "signal" )
                 )
        {
          //          bool hasRecordOne = false;
          //          for( map<size_t,int>::const_iterator iter = column_map.begin();
          //              iter != column_map.end(); ++iter )
          //          {
          //            hasRecordOne |= (iter->second == kCounts);
          //          }
          
          //          if( hasRecordOne )
          //            column_map[i] = kSecondRecord;
          //          else
          column_map[i] = kCounts;
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
      
      if( c > 0 )
      {
        energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
        calibration_coeffs_.resize( 2 );
        calibration_coeffs_[0] = b;
        calibration_coeffs_[1] = c;
      }
    }
    
  }//while( getline( istr, line ) )
  
  if( nlines_total < 10 || nlines_used < static_cast<size_t>( ceil(0.25*nlines_total) ))
  {
    reset();
    istr.seekg( orig_pos, ios::beg );
    istr.clear( ios::failbit );
    throw runtime_error( "Not enough (useful) lines in the file." );
  }//
  
  if( !gamma_counts_ || gamma_counts_->size() < 5 || calibration_coeffs_.empty() )
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
  
  if( (gamma_count_sum_ < FLT_EPSILON) && !contained_neutron() )
  {
    reset();
    istr.seekg( orig_pos, ios::beg );
    istr.clear( ios::failbit );
    throw runtime_error( "Measurement::set_info_from_txt_or_csv(...)\n\tFailed to find gamma or neutron counts" );
  }
  
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
    
    const vector< pair<float,float> > devpairs;
    const bool validcalib
    = SpecUtils::calibration_is_valid( SpecUtils::EnergyCalType::Polynomial, eqn, devpairs,
                                      nchannel );
    if( !validcalib )
      throw runtime_error( "" ); //"Invalid calibration"
    
    //    real_time_ = realtime;
    live_time_ = realtime;
    contained_neutron_ = false;
    deviation_pairs_.clear();
    calibration_coeffs_ = eqn;
    energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
    neutron_counts_.clear();
    gamma_counts_ = counts;
    neutron_counts_sum_ = gamma_count_sum_ = 0.0;
    for( const float f : *counts )
      gamma_count_sum_ += f;
  }catch( std::exception &e )
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
    
#if(PERFORM_DEVELOPER_CHECKS)
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
#if(PERFORM_DEVELOPER_CHECKS)
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
#if(PERFORM_DEVELOPER_CHECKS)
          log_developer_error( __func__, ("Unrecognized neutron type in SRPM file: '" + key + "'").c_str() );
#endif
        }
      }else
      {
#if(PERFORM_DEVELOPER_CHECKS)
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
       SpecUtils::EnergyCalType   energy_calibration_model_;
       std::vector<std::string>  remarks_;
       boost::posix_time::ptime  start_time_;
       std::vector<float>        calibration_coeffs_;  //should consider making a shared pointer (for the case of LowerChannelEdge binning)
       std::vector<std::pair<float,float>>          deviation_pairs_;     //<energy,offset>
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
    //measurment_operator_ = "";
    
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
  switch( energy_calibration_model_ )
  {
    case SpecUtils::EnergyCalType::Polynomial:
    case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
      ostr << "Polynomial";
      break;
    case SpecUtils::EnergyCalType::FullRangeFraction:   ostr << "FullRangeFraction"; break;
    case SpecUtils::EnergyCalType::LowerChannelEdge:    ostr << "LowerChannelEdge"; break;
    case SpecUtils::EnergyCalType::InvalidEquationType: ostr << "Unknown"; break;
  }//switch( energy_calibration_model_ )
  
  
  ostr << endline << "Coefficients ";
  for( size_t i = 0; i < calibration_coeffs_.size(); ++i )
    ostr << (i ? " " : "") << calibration_coeffs_[i];
  
  if( energy_calibration_model_ == SpecUtils::EnergyCalType::LowerChannelEdge
     && calibration_coeffs_.empty()
     && !!channel_energies_
     && channel_energies_->size() )
  {
    for( size_t i = 0; i < channel_energies_->size(); ++i )
      ostr << (i ? " " : "") << (*channel_energies_)[i];
  }
  
  ostr << endline;
  
  if( contained_neutron_ )
    ostr << "NeutronCount " << neutron_counts_sum_ << endline;
  
  //  ostr "Channel" << " "
  //       << setw(12) << ios::left << "Energy" << " "
  //       << setw(12) << ios::left << "Counts" << endline;
  ostr << "Channel" << " "
  "Energy" << " "
  "Counts" << endline;
  const size_t nChannel = gamma_counts_->size();
  
  for( size_t i = 0; i < nChannel; ++i )
  {
    //    ostr << setw(12) << ios::right << i
    //         << setw(12) << ios::right << channel_energies_->at(i)
    //         << setw(12) << ios::right << gamma_counts_->operator[](i)
    //         << endline;
    ostr << i << " " << channel_energies_->at(i)
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
  
  ostr << "Energy, Data" << endline;
  
  for( size_t i = 0; i < gamma_counts_->size(); ++i )
    ostr << channel_energies_->at(i) << "," << gamma_counts_->operator[](i) << endline;
  
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


