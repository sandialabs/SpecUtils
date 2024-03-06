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

#include <vector>
#include <string>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>

#if( PERFORM_DEVELOPER_CHECKS )
#include <assert.h>
#endif

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/UriSpectrum.h"

using namespace std;


namespace SpecUtils
{
bool SpecFile::load_uri_file( const std::string &filename )
{
  /** The URI defined format; e.g., from a QR-code.
   The string can either be the URI(s) itself, or point to a file with the URI(s) in them.
   If a multipart URI, source should have all URIs, with each URI starting with "RADDATA://".
   */
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  reset();
  
#ifdef _WIN32
  ifstream file( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream file( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  if( file.is_open() )
  {
    const bool loaded = load_from_uri( file );
    if( loaded )
      filename_ = filename;
    return loaded;
  }//if( file.is_open() )
  
  if( istarts_with(filename, "RADDATA://G0/"))
  {
    stringstream strm( filename );
    return load_from_uri( strm );
  }//if( file.is_open() ) / else if URI
  
  return false;
}//bool load_uri_file( const std::string &filename )
  
  
bool SpecFile::load_from_uri( std::istream &input )
{
  if( !input )
    return false;
  
  const istream::iostate origexceptions = input.exceptions();
  input.exceptions( istream::goodbit );  //make so stream never throws
  const istream::pos_type start_pos = input.tellg();
  input.unsetf(ios::skipws);
  
  // Determine stream size
  input.seekg( 0, ios::end );
  const size_t file_size = static_cast<size_t>( input.tellg() - start_pos );
  input.seekg( start_pos );
  
  // Its unreasonable to have more than a few tens of kilobyte spectra, but we'll allow up to 1 MB
  if( (file_size > (1024 * 1024)) || (file_size < 20) )
  {
    input.exceptions(origexceptions);
    return false;
  }


  try
  {
    // Read file contents into a string.
    string rawdata;
    rawdata.resize(file_size);
    if (!input.read(&(rawdata[0]), file_size))
      throw runtime_error("Failed to read first file contents.");

    //  First lets make the "RADDATA://G0/" part always lowercase.
    ireplace_all( rawdata, "RADDATA://G0/", "raddata://G0/" );
    
    // Incase someone saved a mailto: URI to file, we will remove all the email front-matter,
    //  leaving just "raddata://G0/" and after (for all URIs in rawdata).
    ireplace_all( rawdata, "mailto:", "mailto:" );
    for( size_t mailto_pos = rawdata.find( "mailto:" ); 
        mailto_pos != string::npos;
        mailto_pos = rawdata.find( "mailto:" ) )
    {
      //  (TODO: I guess we could get a false-positive here, if someone saved 'mailto:' in the
      //   spectrum description... I guess we should account for this...)
      const size_t raddata_pos = rawdata.find( "raddata://G0/", mailto_pos );
      if( raddata_pos == string::npos )
      {
        // Only poorly formed input will make it here
#if( PERFORM_DEVELOPER_CHECKS )
        log_developer_error( __func__, "SpecFile::load_from_uri: encountered a 'mailto:'"
                                        " without a trailing 'raddata://G0/' uri" );
        assert( raddata_pos != string::npos );
#endif

        rawdata.erase( begin(rawdata) + mailto_pos, end(rawdata) );
      }else
      {
        rawdata.erase( begin(rawdata) + mailto_pos, begin(rawdata) + raddata_pos );
      }
    }//for( while we need to remove 'mailto:' front matter )
    
    
    // File may contain multiple URIs (think multi-url spectra), so we'll split the URIs up
    // Break up `rawdata` input multiple URIs, splitting at raddata://
    vector<string> uris;
    size_t next_pos = 0;
    do
    {
      const size_t current_pos = next_pos;
      next_pos = rawdata.find( "raddata://G0/", current_pos + 1 );
      
      // From cplusplus.com: len: Number of characters to include in the substring (if the string
      //                          is shorter, as many characters as possible are used).
      const size_t len = next_pos - current_pos;  // So we should be safe if next_pos==string::npos
      string uri = rawdata.substr(current_pos, next_pos - current_pos);
      trim( uri ); //TODO: just copy next_pos, then decrement the result to clear any whitespace.
      
      if( !uri.empty() )
        uris.push_back( std::move(uri) );
    }while( next_pos != string::npos );
    
    if( uris.empty() )
      return false;

    
    vector<UrlSpectrum> url_spectra;
    
    try
    {
      // `decode_spectrum_urls(string)` expects the input URI is URL-decoded already
      url_spectra = decode_spectrum_urls( uris );
    }catch( std::exception &e )
    {
      // Try URL decoding the URIs
      for( string &uri : uris )
        uri = url_decode( uri );
      
      try
      {
        url_spectra = decode_spectrum_urls( uris );
      }catch( std::exception & )
      {
        try
        {
          // Try URL decoding the URIs ONE more time (e.g., if you originally encoded to go
          //  into email, but then didnt URL decode it)
          for( string &uri : uris )
            uri = url_decode( uri );
          url_spectra = decode_spectrum_urls( uris );
        }catch( std::exception & )
        {
          // Really no go - give up.
          throw runtime_error( "Failed to decode URL to spectra: " + string(e.what()) );
        }
      }
    }//try / catch
    
    if( url_spectra.empty() )
      throw runtime_error( "Failed to decode URL spectra." );
    
    
    shared_ptr<SpecFile> specfile = to_spec_file( url_spectra );
    
    if( !specfile )
      throw runtime_error( "Failed to convert UrlSpectrum to SpecFile." );
    
    *this = *specfile;
    
    return true;
  }catch( std::exception &e )
  {
    cerr << "Failed to parse URI spectrum: " << e.what() << endl;
    reset();
    input.seekg( start_pos );
    input.clear();
    input.exceptions(origexceptions);
  }//try / catch
  
  return false;
}//bool load_from_uri( std::istream &input )
  
  
bool SpecFile::write_uri( std::ostream &output, const size_t num_uris, 
                         const uint8_t encode_options ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( (num_uris < 0) || (num_uris > 9) )
    throw runtime_error( "SpecFile::write_uri: invalid number (" + to_string(num_uris)
                        + ") of URIs specified." );
  
  if( measurements_.empty() )
    return false;
  
  string detector_model;
  switch( detector_type_ )
  {
    case DetectorType::Unknown:
      detector_model = instrument_model_;
      break;
    
    default:
      detector_model = detectorTypeToString(detector_type_);
      break;
  }//switch( detector_type_ )
  
  vector<shared_ptr<const SpecUtils::Measurement>> specs;
  
  if( num_uris == 1 || (measurements_.size() == 1) )
  {
    // If 1, then all spectra in the `SpecFile` will be be written into a single URI
    specs.insert( end(specs), begin(measurements_), end(measurements_) );
  }else
  {
    // If greater than one, then all spectra will be summed to a single spectrum, and it
    //  written to multiple URIs, separated by line breaks.
    specs.push_back( sum_measurements( sample_numbers_, detector_names_, nullptr ) );
  }
  
  vector<UrlSpectrum> spectra  = to_url_spectra( specs, detector_model );
  
  vector<string> uris = url_encode_spectra( spectra, encode_options, num_uris );
  
  for( size_t i = 0; i < uris.size(); ++i )
    output << (i ? "\n\r" : "") << uris[i];
  
  return output.good();
}//bool SpecFile::write_uri(...)
  
}//namespace SpecUtils





