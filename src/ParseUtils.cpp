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

#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"


using namespace std;


namespace SpecUtils
{
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
}//namespace SpecUtils
