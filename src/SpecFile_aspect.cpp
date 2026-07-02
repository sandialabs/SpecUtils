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
#include <mutex>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <istream>
#include <stdexcept>

#include "SpecUtils/DateTime.h"
#include "SpecUtils/SpecFile.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

/*
 ASPECT (NPC "Aspect", Dubna, Russia) binary .spc format.

 The file is a 512-byte packed `tSpectrHeader` (all multi-byte values little-endian)
 followed by `buffer` channel counts stored as little-endian uint32.  
 Layout (from the header "Формат файла спектра.h" - see https://github.com/sandialabs/SpecUtils/issues/47):

   0x00 device[16]  0x10 unit  0x11 section  0x12 sampleTime[6]  0x18 weight[16]
   0x28 volume[16]  0x38 startTime[6]  0x3E liveTime(u32)  0x42 realTime(u32)
   0x46 buffer(u16) 0x48 gain  0x4A offset  0x4C lowerLevel  0x4E upperLevel
   0x50 first(u16)  0x52 last(u16)  0x54 chWidth  0x56 adcCont[10]  0x6A prepar
   0x6B preparTime[6]  0x71 reserve[15]  0x80 energy[4][16]  0xC0 fwhm[4][16]
   0x100 comment[4][64]  0x200 data[buffer]

 The 6-byte date/time fields are raw bytes: day, month, year-2000, hour, minute, second.
 The units of liveTime/realTime are not known for sure, *look* like they are probably milliseconds. 
 Text fields look to be Windows-1251 encoded.

"spectrum_file_format_en.h":
```
#pragma pack(1)

typedef struct
{
	BYTE device[ 16 ];		// device name
	BYTE unit;				// ADC number
	BYTE section;	  		// section number

	BYTE sampleTime[ 6 ];  	// sample collection date/time "DMYHMS"
	BYTE weight[ 16 ];    	// mass, kg
	BYTE volume[ 16 ];		// volume, L

	BYTE startTime[ 6 ];    	// spectrum acquisition start date/time "DMYHMS"
	DWORD liveTime;			// live time
	DWORD realTime;			// real time

	WORD buffer;			// ADC buffer size
	WORD gain;	      		// ADC resolution
	WORD offset;			// ADC offset
	WORD lowerLevel;		// lower discrimination level (LLD)
	WORD upperLevel;		// upper discrimination level (ULD)

	WORD first;	      		// first channel
	WORD last;	      		// last channel

	WORD chWidth;			// channel width
	WORD adcCont[ 10 ];		// additional ADC parameters

	BYTE prepar;			// preparation type: 0 = none, 1 = ashing
	BYTE preparTime[ 6 ];   	// sampling start date/time "DMYHMS"

	BYTE reserve[ 15 ];

	BYTE energy[ 4 ][ 16 ];		// energy calibration coefficients
	BYTE fwhm[ 4 ][ 16 ];		// FWHM calibration coefficients

	BYTE comment[ 4 ][ 64 ];	// description of measurement conditions
} tSpectrHeader, *pSpectrHeader;
```
*/

namespace
{
  const size_t k_aspect_header_size = 512;

  inline uint16_t read_u16le( const unsigned char *p )
  {
    return static_cast<uint16_t>( static_cast<uint16_t>(p[0])
                                | (static_cast<uint16_t>(p[1]) << 8) );
  }

  inline uint32_t read_u32le( const unsigned char *p )
  {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
  }
}//namespace


namespace SpecUtils
{
bool SpecFile::load_aspect_spc_file( const std::string &filename )
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

  const bool loaded = load_from_aspect_spc( file );

  if( loaded )
    filename_ = filename;

  return loaded;
}//bool load_aspect_spc_file( const std::string &filename )


bool SpecFile::load_from_aspect_spc( std::istream &input )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  if( !input.good() )
    return false;

  const istream::pos_type orig_pos = input.tellg();

  try
  {
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( orig_pos );

    const size_t filesize = static_cast<size_t>( eof_pos - orig_pos );

    // Smallest sensible file: header + a handful of channels.
    if( filesize < (k_aspect_header_size + 4*16) )
      throw runtime_error( "File too small to be an ASPECT SPC file." );

    if( ((filesize - k_aspect_header_size) % 4) != 0 )
      throw runtime_error( "File size not consistent with 32-bit channel counts." );

    vector<char> header( k_aspect_header_size );
    if( !input.read( header.data(), static_cast<streamsize>(k_aspect_header_size) ) )
      throw runtime_error( "Failed to read ASPECT SPC header." );

    const unsigned char * const h = reinterpret_cast<const unsigned char *>( header.data() );

    // --- Structural validation: this is the primary guard against mis-accepting a
    //     foreign file, since the format has no magic number. ---
    const uint16_t buffer = read_u16le( h + 0x46 );  // ADC buffer size == number of channels
    const uint16_t first  = read_u16le( h + 0x50 );  // first channel (== 0)
    const uint16_t last   = read_u16le( h + 0x52 );  // last channel  (== buffer - 1)

    if( buffer < 16 )
      throw runtime_error( "ASPECT SPC: too few channels." );

    if( filesize != (k_aspect_header_size + size_t(buffer)*4) )
      throw runtime_error( "ASPECT SPC: file size does not match channel count." );

    if( (first != 0) || (last != static_cast<uint16_t>(buffer - 1)) )
      throw runtime_error( "ASPECT SPC: first/last channel fields inconsistent." );

    // --- Acquisition start time (day, month, year-2000, hour, minute, second).  A genuine
    //     file always has a valid timestamp; reject anything that does not parse. ---
    const auto read_dmyhms = [h]( size_t off ) -> time_point_t {
      const int day = h[off+0], month = h[off+1], yr = h[off+2];
      const int hour = h[off+3], minute = h[off+4], second = h[off+5];
      if( !day && !month && !yr && !hour && !minute && !second )
        return time_point_t{};  // field not set
      if( (month < 1) || (month > 12) || (day < 1) || (day > 31)
          || (hour > 23) || (minute > 59) || (second > 59) )
        return time_point_t{};  // out of range
      char buf[24] = { '\0' };
      snprintf( buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                2000 + yr, month, day, hour, minute, second );
      return time_from_string( buf );
    };//read_dmyhms lambda

    // Require either a valid date, or an all-zero (unset) field.  A non-zero but invalid date
    // means this isn't an ASPECT file (or it is corrupt), so reject it - a genuine file will
    // have a valid acquisition time or leave the field zeroed.
    const time_point_t start_time = read_dmyhms( 0x38 );
    const bool start_time_blank = !(h[0x38] | h[0x39] | h[0x3A] | h[0x3B] | h[0x3C] | h[0x3D]);
    if( (start_time == time_point_t{}) && !start_time_blank )
      throw runtime_error( "ASPECT SPC: invalid acquisition time." );

    // --- Live/real time (milliseconds).  Must be positive, live<=real, and dead-time sane. ---
    const uint32_t live_ms = read_u32le( h + 0x3E );
    const uint32_t real_ms = read_u32le( h + 0x42 );
    if( (live_ms == 0) || (real_ms == 0) || (live_ms > real_ms) )
      throw runtime_error( "ASPECT SPC: implausible live/real time." );
    if( (static_cast<uint64_t>(live_ms) * 100u) < static_cast<uint64_t>(real_ms) )
      throw runtime_error( "ASPECT SPC: dead time exceeds 99%." );  // live < real/100

    // --- Channel data: `buffer` little-endian uint32 counts. ---
    const size_t nbytes = size_t(buffer) * 4;
    vector<char> databuf( nbytes );
    if( !input.read( databuf.data(), static_cast<streamsize>(nbytes) ) )
      throw runtime_error( "ASPECT SPC: failed to read channel data." );

    const unsigned char * const dp = reinterpret_cast<const unsigned char *>( databuf.data() );
    auto channel_data = make_shared<vector<float>>( buffer );
    double gamma_sum = 0.0;
    for( size_t i = 0; i < buffer; ++i )
    {
      const uint32_t counts = read_u32le( dp + 4*i );
      (*channel_data)[i] = static_cast<float>( counts );
      gamma_sum += counts;
    }//for( each channel )

    if( gamma_sum <= 0.0 )
      throw runtime_error( "ASPECT SPC: spectrum contains no counts." );

    // Guard against misread binary whose "counts" sum to an unphysical rate (>10 MHz avg).
    if( gamma_sum > (1.0e4 * static_cast<double>(real_ms)) )
      throw runtime_error( "ASPECT SPC: implausible count rate." );

    // --- File accepted: build the measurement. ---
    auto meas = make_shared<Measurement>();
    meas->live_time_ = live_ms / 1000.0f;
    meas->real_time_ = real_ms / 1000.0f;
    meas->start_time_ = start_time;
    meas->gamma_counts_ = channel_data;
    meas->gamma_count_sum_ = gamma_sum;

    // Reads a fixed-length, possibly NUL-terminated Windows-1251 text field as trimmed UTF-8.
    const auto hdr_str = [h]( size_t off, size_t len ) -> string {
      const char *b = reinterpret_cast<const char *>( h + off );
      size_t n = 0;
      while( (n < len) && b[n] )
        ++n;
      string s = cp1251_to_utf8( string( b, b + n ) );
      trim( s );
      return s;
    };//hdr_str lambda

    // Energy calibration coefficients (a0..a3), stored as ASCII strings.  Empty in every
    // sample seen so far; parsed defensively (ASCII assumption is unverified - no calibrated
    // sample was available).  Only set a calibration if a non-constant term is present.
    {
      vector<float> coefs( 4, 0.0f );
      bool any_text = false, parse_ok = true;
      for( int i = 0; i < 4; ++i )
      {
        const string coef = hdr_str( 0x80 + 16*i, 16 );
        if( coef.empty() )
          continue;
        any_text = true;
        float val = 0.0f;
        if( parse_float( coef.c_str(), coef.size(), val ) && std::isfinite(val) )
          coefs[i] = val;
        else
          parse_ok = false;
      }//for( each energy coefficient )

      const bool usable = (coefs[1] != 0.0f) || (coefs[2] != 0.0f) || (coefs[3] != 0.0f);
      if( any_text && parse_ok && usable )
      {
        while( (coefs.size() > 2) && (coefs.back() == 0.0f) )
          coefs.pop_back();
        try
        {
          auto newcal = make_shared<EnergyCalibration>();
          newcal->set_polynomial( buffer, coefs, {} );
          meas->energy_calibration_ = newcal;
        }catch( std::exception &e )
        {
          if( !coefs.empty() )
            meas->parse_warnings_.push_back( "Invalid ASPECT energy calibration: " + string(e.what()) );
        }
      }else if( any_text && !parse_ok )
      {
        meas->parse_warnings_.push_back( "Could not parse ASPECT energy calibration coefficients." );
      }
    }//energy calibration

    // Remaining metadata - all empty/zero in the available samples, read where present.
    const string device = hdr_str( 0x00, 16 );
    if( !device.empty() )
      instrument_model_ = device;
    manufacturer_ = "Aspect";

    const int unit = h[0x10], section = h[0x11];
    if( unit )
      meas->remarks_.push_back( "ADC number: " + std::to_string(unit) );
    if( section )
      meas->remarks_.push_back( "Section: " + std::to_string(section) );

    const string weight = hdr_str( 0x18, 16 );
    if( !weight.empty() )
      meas->remarks_.push_back( "Sample mass (kg): " + weight );
    const string volume = hdr_str( 0x28, 16 );
    if( !volume.empty() )
      meas->remarks_.push_back( "Sample volume (L): " + volume );

    const time_point_t sample_time = read_dmyhms( 0x12 );
    if( sample_time != time_point_t{} )
      meas->remarks_.push_back( "Sample collection time: " + to_iso_string(sample_time) );

    const uint16_t gain = read_u16le( h + 0x48 );
    const uint16_t offset = read_u16le( h + 0x4A );
    const uint16_t lld = read_u16le( h + 0x4C );
    const uint16_t uld = read_u16le( h + 0x4E );
    const uint16_t chwidth = read_u16le( h + 0x54 );
    if( gain )
      meas->remarks_.push_back( "ADC resolution: " + std::to_string(gain) );
    if( offset )
      meas->remarks_.push_back( "ADC offset: " + std::to_string(offset) );
    if( lld )
      meas->remarks_.push_back( "Lower discriminator level: " + std::to_string(lld) );
    if( uld )
      meas->remarks_.push_back( "Upper discriminator level: " + std::to_string(uld) );
    if( chwidth )
      meas->remarks_.push_back( "Channel width: " + std::to_string(chwidth) );

    const int prepar = h[0x6A];
    if( prepar )
      meas->remarks_.push_back( string("Sample preparation: ") + ((prepar == 1) ? "ashing" : "type " + std::to_string(prepar)) );
    const time_point_t prepar_time = read_dmyhms( 0x6B );
    if( prepar_time != time_point_t{} )
      meas->remarks_.push_back( "Sample preparation time: " + to_iso_string(prepar_time) );

    // FWHM calibration coefficients (ASCII); no dedicated field, so keep as a remark.
    {
      string fwhm;
      for( int i = 0; i < 4; ++i )
      {
        const string coef = hdr_str( 0xC0 + 16*i, 16 );
        if( !coef.empty() )
          fwhm += (fwhm.empty() ? "" : ", ") + coef;
      }
      if( !fwhm.empty() )
        meas->remarks_.push_back( "FWHM calibration coefficients: " + fwhm );
    }//FWHM coefficients

    // Free-text measurement description (four lines).
    for( int i = 0; i < 4; ++i )
    {
      const string comment = hdr_str( 0x100 + 64*i, 64 );
      if( !comment.empty() )
        meas->remarks_.push_back( comment );
    }//for( each comment line )

    measurements_.push_back( meas );

    cleanup_after_load();
  }catch( std::exception & )
  {
    input.clear();
    input.seekg( orig_pos );
    reset();
    return false;
  }//try / catch

  return true;
}//bool load_from_aspect_spc( std::istream &input )

}//namespace SpecUtils
