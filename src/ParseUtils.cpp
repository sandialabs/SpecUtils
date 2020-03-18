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
#include <string>
#include <limits>
#include <sstream>
#include <algorithm>

#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"


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
std::istream& safe_get_line(std::istream& is, std::string& t)
{
  return safe_get_line( is, t, 0 );
}
  
  
std::istream &safe_get_line( std::istream &is, std::string &t, const size_t maxlength )
{
  //from  http://stackoverflow.com/questions/6089231/getting-std-ifstream-to-handle-lf-cr-and-crlf
  //  adapted by wcjohns
  t.clear();
  
  // The characters in the stream are read one-by-one using a std::streambuf.
  // That is faster than reading them one-by-one using the std::istream.
  // Code that uses streambuf this way must be guarded by a sentry object.
  // The sentry object performs various tasks,
  // such as thread synchronization and updating the stream state.
  std::istream::sentry se( is, true );
  std::streambuf *sb = is.rdbuf();
  
  for( ; !maxlength || (t.length() < maxlength); )
  {
    int c = sb->sbumpc(); //advances pointer to current location by one
    switch( c )
    {
      case '\r':
        c = sb->sgetc();  //does not advance pointer to current location
        if( c == '\n' )
          sb->sbumpc();   //advances pointer to one current location by one
        return is;
        
      case '\n':
        return is;
        
      case EOF:
        is.setstate( ios::eofbit );
        return is;
        
      default:
        t += (char)c;
        
        if( maxlength && (t.length() == maxlength) )
        {
          c = sb->sgetc();    //does not advance pointers current location
          
          if( c == EOF )
          {
            sb->sbumpc();
            is.setstate( ios::eofbit );
          }else
          {
            if( c == '\r' )
            {
              sb->sbumpc();     //advances pointers current location by one
              c = sb->sgetc();  //does not advance pointer to current location
            }
            
            if( c == '\n')
            {
              sb->sbumpc();     //advances pointer to one current location by one
              c = sb->sgetc();  //does not advance pointer to current location
            }
          }
          
          return is;
        }
    }//switch( c )
  }//for(;;)
  
  return is;
}//safe_get_line(...)

void expand_counted_zeros( const vector<float> &data, vector<float> &return_answer )
{
  vector<float> answer;
  answer.reserve( 1024 );
  vector<float>::const_iterator iter;
  for( iter = data.begin(); iter != data.end(); iter++)
  {
    if( (*iter != 0.0f) || (iter+1==data.end()) || (*(iter+1)==0.0f) )
      answer.push_back(*iter);
    else
    {
      iter++;
      const size_t nZeroes = ((iter==data.end()) ? 0u : static_cast<size_t>(floor(*iter + 0.5f)) );
      
      if( ((*iter) <= 0.5f) || ((answer.size() + nZeroes) > 131072) )
        throw runtime_error( "Invalid counted zeros: too many total elements, or negative number of zeros" );
      
      for( size_t k = 0; k < nZeroes; ++k )
        answer.push_back( 0.0f );
    }//if( at a non-zero value, the last value, or the next value is zero) / else
  }//for( iterate over data, iter )
  
  answer.swap( return_answer );
}//vector<float> expand_counted_zeros( const vector<float> &data )
  
  
void compress_to_counted_zeros( const std::vector<float> &input, std::vector<float> &results )
{
  results.clear();
  
  //Previous to 20181120 1E-8 was used, but this caused problems with PCF files
  //  from GADRAS that were not Poisson varied.  FLT_EPSILON is usually 1.19e-7f
  //  which is still to big!  So chose 10*FLT_MIN (FLT_MIN is something 1E-37)
  //  which worked with a GADRAS Db.pcf I checked.
  const float epsilon = 10.0f * std::numeric_limits<float>::min();
  
  
  const size_t nBin = input.size();
  
  for( size_t bin = 0; bin < nBin; ++bin )
  {
    const bool isZero = (fabs(input[bin]) < epsilon);
    
    if( !isZero ) results.push_back( input[bin] );
    else          results.push_back( 0.0f );
    
    if( isZero )
    {
      size_t nBinZeroes = 0;
      while( ( bin < nBin ) && ( fabs( input[bin] ) < epsilon) )
      {
        ++nBinZeroes;
        ++bin;
      }//while more zero bins
      
      results.push_back( static_cast<float>(nBinZeroes) );
      
      if( bin != nBin )
        --bin;
    }//if( input[bin] == 0.0 )
  }//for( size_t bin = 0; bin < input.size(); ++bin )
}//void compress_to_counted_zeros(...)

  
  
double conventional_lat_or_long_str_to_flt( std::string input )
{
  input.erase( std::remove_if(input.begin(), input.end(), [](char c) -> bool {
    return !(std::isalnum(c) || c==' ');
  } ), input.end() );
  
  trim( input );
  
  char dir;
  float degress, minutes, seconds;
  const size_t npar = sscanf( input.c_str(),"%f %f %f %c",
                             &degress, &minutes, &seconds, &dir );
  
  if( npar == 4 )
  {
    const double sign = ((dir=='N'||dir=='E') ? 1.0 : -1.0);
    return sign * (degress + (minutes/60.0) + (seconds/3600.0));
  }
  
  return -999.9;
};//conventional_lat_or_long_str_to_flt(...)
  
  
bool valid_longitude( const double longitude )
{
  return (fabs(static_cast<double>(longitude))<=180.0 && !IsInf(longitude) );
}

  
bool valid_latitude( const double latitude )
{
  return (fabs(static_cast<double>(latitude))<=90.0 && !IsInf(latitude) );
}

  
bool parse_deg_min_sec_lat_lon( const char *str, const size_t len,
                                 double &lat, double &lon )
{
  //only tested on MicroRaider files
  //    "25°47\"17.820' N / 80°19\"25.500' W"
  lat = lon = -999.9;
  
  if( !str || !len )
    return false;
  
  const char *end = str + len;
  const char *pos = std::find( str, end, '/' );
  if( pos != end )
  {
    string latstr(str,pos), lonstr(pos+1,end);
    for( size_t i = 0; i < latstr.size(); ++i )
      if( !isalnum(latstr[i]) && latstr[i]!='.' )
        latstr[i] = ' ';
    for( size_t i = 0; i < lonstr.size(); ++i )
      if( !isalnum(lonstr[i]) && latstr[i]!='.' )
        lonstr[i] = ' ';
    
    ireplace_all( latstr, "degree", " " );
    ireplace_all( latstr, "minute", " " );
    ireplace_all( latstr, "second", " " );
    ireplace_all( lonstr, "degree", " " );
    ireplace_all( lonstr, "minute", " " );
    ireplace_all( lonstr, "second", " " );
    ireplace_all( latstr, "deg", " " );
    ireplace_all( latstr, "min", " " );
    ireplace_all( latstr, "sec", " " );
    ireplace_all( lonstr, "deg", " " );
    ireplace_all( lonstr, "min", " " );
    ireplace_all( lonstr, "sec", " " );
    ireplace_all( latstr, "  ", " " );
    ireplace_all( lonstr, "  ", " " );
    
    lat = conventional_lat_or_long_str_to_flt( latstr );
    lon = conventional_lat_or_long_str_to_flt( lonstr );
    
    //cerr << "latstr='" << latstr << "'-->" << lat << endl;
    //cerr << "lonstr='" << lonstr << "'-->" << lon << endl;
    
    return (SpecUtils::valid_longitude(lon)
            && SpecUtils::valid_latitude(lat));
  }//if( pos != end )
  
  return false;
}//bool parse_deg_min_sec_lat_lon(
  
  
int sample_num_from_remark( std::string remark )
{
    SpecUtils::to_lower_ascii(remark);
    size_t pos = remark.find( "survey" );
    
    if( pos == string::npos )
      pos = remark.find( "sample" );
    
    if( pos == string::npos )
    {
      //    cerr << "Remark '" << remark << "'' didnt contain a sample num" << endl;
      return -1;
    }
    
    pos = remark.find_first_not_of( " \t=", pos+6 );
    if( pos == string::npos )
    {
#if( PERFORM_DEVELOPER_CHECKS )
      string msg = "Remark '" + remark + "'' didnt have a integer sample num";
      log_developer_error( __func__, msg.c_str() );
#endif
      return -1;
    }
    
    int num = -1;
    if( !(stringstream(remark.c_str()+pos) >> num) )
    {
#if( PERFORM_DEVELOPER_CHECKS )
      string msg = "ample_num_from_remark(...): Error converting '" + string(remark.c_str()+pos) + "' to int";
      log_developer_error( __func__, msg.c_str() );
#endif
      return -1;
    }//if( cant convert result to int )
    
    return num;
}//int sample_num_from_remark( const std::string &remark )
  

  
float speed_from_remark( std::string remark )
{
  to_lower_ascii( remark );
  size_t pos = remark.find( "speed" );
  
  if( pos == string::npos )
    return 0.0;
  
  pos = remark.find_first_not_of( "= \t", pos+5 );
  if( pos == string::npos )
    return 0.0;
  
  const string speedstr = remark.substr( pos );
  
  float speed = 0.0;
  if( !toFloat( speedstr, speed) )
  {
#if( PERFORM_DEVELOPER_CHECKS )
    string msg = "speed_from_remark(...): couldn conver to number: '" + speedstr + "' to float";
    log_developer_error( __func__, msg.c_str() );
#endif
    return 0.0;
  }//if( !(stringstream(speedstr) >> speed) )
  
  
  for( size_t i = 0; i < speedstr.size(); ++i )
  {
    if( (!isdigit(speedstr[i])) && (speedstr[i]!=' ') && (speedstr[i]!='\t') )
    {
      float convertion = 0.0f;
      
      const string unitstr = speedstr.substr( i );
      const size_t unitstrlen = unitstr.size();
      
      if( unitstrlen>=3 && unitstr.substr(0,3) == "m/s" )
        convertion = 1.0f;
      else if( unitstrlen>=3 && unitstr.substr(0,3) == "mph" )
        convertion = 0.44704f;
      else
      {
#if( PERFORM_DEVELOPER_CHECKS )
        string msg = "speed_from_remark(...): Unknown speed unit: '" + unitstr + "'";
        log_developer_error( __func__, msg.c_str() );
#endif
      }
      
      return convertion*speed;
    }//if( we found the start of the units )
  }//for( size_t i = 0; i < speedstr.size(); ++i )
  
  return 0.0;
}//float speed_from_remark( const std::string &remark )
  
  
  
std::string detector_name_from_remark( const std::string &remark )
{
  //Check for the Gadras convention similar to "Det=Aa1"
  if( SpecUtils::icontains(remark, "det") )
  {
    //Could use a regex here... maybe someday I'll upgrade
    string remarkcopy = remark;
    
    string remarkcopylowercase = remarkcopy;
    SpecUtils::to_lower_ascii( remarkcopylowercase );
    
    size_t pos = remarkcopylowercase.find( "det" );
    if( pos != string::npos )
    {
      remarkcopy = remarkcopy.substr(pos);
      pos = remarkcopy.find_first_of( "= " );
      if( pos != string::npos )
      {
        string det_identifier = remarkcopy.substr(0,pos);
        SpecUtils::to_lower_ascii( det_identifier );
        SpecUtils::trim( det_identifier ); //I dont htink this is necassarry
        if( det_identifier=="det" || det_identifier=="detector"
           || (SpecUtils::levenshtein_distance(det_identifier,"detector") < 3) ) //Allow two typos of "detector"; arbitrary
        {
          remarkcopy = remarkcopy.substr(pos);  //get rid of the "det="
          while( remarkcopy.length() && (remarkcopy[0]==' ' || remarkcopy[0]=='=') )
            remarkcopy = remarkcopy.substr(1);
          pos = remarkcopy.find_first_of( ' ' );
          return remarkcopy.substr(0,pos);
        }
      }
    }
    
  }//if( SpecUtils::icontains(remark, "det") )
  
  
  vector<string> split_contents;
  split( split_contents, remark, ", \t\r\n" );
  
  for( const string &field : split_contents )
  {
    if( (field.length() < 3) ||  !isdigit(field[field.size()-1])
       || (field.length() > 4) ||  (field[1] != 'a') )
      continue;
    
    if( field[0]!='A' && field[0]!='B' && field[0]!='C' && field[0]!='D' )
      continue;
    
    return field;
  }//for( size_t i = 0; i < split_contents.size(); ++i )
  
  return "";
}//std::string detector_name_from_remark( const std::string &remark )
  
  
  
float dose_units_usvPerH( const char *str, const size_t str_length )
{
  if( !str )
    return 0.0f;
  
  if( icontains( str, str_length, "uSv", 3 )
     || icontains( str, str_length, "\xc2\xb5Sv", 4) )
    return 1.0f;
  
  //One sievert equals 100 rem.
  if( icontains( str, str_length, "&#xB5;Rem/h", 11 ) ) //micro
    return 0.01f;
  
  return 0.0f;
}//float dose_units_usvPerH( const char *str )

  
const std::string &convert_n42_instrument_type_from_2006_to_2012( const std::string &classcode )
{
  static const string PortalMonitor = "Portal Monitor";
  static const string SpecPortal = "Spectroscopic Portal Monitor";
  static const string RadionuclideIdentifier = "Radionuclide Identifier";
  static const string PersonalRadiationDetector = "Spectroscopic Personal Radiation Detector"; //hmm, prob not best
  static const string SurveyMeter = "Backpack or Personal Radiation Scanner";
  static const string Spectrometer = "Spectroscopic Personal Radiation Detector";
  
  if( iequals_ascii(classcode, "PortalMonitor") || iequals_ascii(classcode, "PVT Portal") )
    return PortalMonitor;
  else if( iequals_ascii(classcode, "SpecPortal") )
    return SpecPortal;
  else if( iequals_ascii(classcode, "RadionuclideIdentifier") )
    return RadionuclideIdentifier;
  else if( iequals_ascii(classcode, "PersonalRadiationDetector") )
    return PersonalRadiationDetector;
  else if( iequals_ascii(classcode, "SurveyMeter") )
    return SurveyMeter;
  else if( iequals_ascii(classcode, "Spectrometer") )
    return Spectrometer;
  
  return classcode;
}//const std::string &convert_n42_instrument_type_2006_to_2012( const std::string &input )
  
}//namespace SpecUtils
