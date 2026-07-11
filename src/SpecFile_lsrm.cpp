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

#include <map>
#include <array>
#include <cmath>
#include <vector>
#include <string>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <numeric>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <algorithm>


#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

// cp1251_to_utf8() lives in SpecUtils::StringAlgo (shared with the ASPECT parser).
using SpecUtils::cp1251_to_utf8;

namespace
{
  // Endian-independent little-endian readers.  Compile to plain unaligned loads on
  // x86/ARM and to a load + bswap on big-endian hosts.
  inline uint16_t read_u16le( const char *p )
  {
    return static_cast<uint16_t>( static_cast<uint8_t>(p[0]) )
         | ( static_cast<uint16_t>( static_cast<uint8_t>(p[1]) ) << 8 );
  }

  inline int32_t read_i32le( const char *p )
  {
    const uint32_t u =  static_cast<uint32_t>( static_cast<uint8_t>(p[0]) )
                     | (static_cast<uint32_t>( static_cast<uint8_t>(p[1]) ) << 8)
                     | (static_cast<uint32_t>( static_cast<uint8_t>(p[2]) ) << 16)
                     | (static_cast<uint32_t>( static_cast<uint8_t>(p[3]) ) << 24);
    return static_cast<int32_t>( u );
  }

  // Encode a Unicode code point into UTF-8, appending to `out`.
  void utf8_append( std::string &out, uint32_t cp )
  {
    if( cp < 0x80 )
    {
      out.push_back( static_cast<char>(cp) );
    }else if( cp < 0x800 )
    {
      out.push_back( static_cast<char>( 0xC0 | (cp >> 6) ) );
      out.push_back( static_cast<char>( 0x80 | (cp & 0x3F) ) );
    }else if( cp < 0x10000 )
    {
      out.push_back( static_cast<char>( 0xE0 | (cp >> 12) ) );
      out.push_back( static_cast<char>( 0x80 | ((cp >> 6) & 0x3F) ) );
      out.push_back( static_cast<char>( 0x80 | (cp & 0x3F) ) );
    }else
    {
      out.push_back( static_cast<char>( 0xF0 | (cp >> 18) ) );
      out.push_back( static_cast<char>( 0x80 | ((cp >> 12) & 0x3F) ) );
      out.push_back( static_cast<char>( 0x80 | ((cp >> 6) & 0x3F) ) );
      out.push_back( static_cast<char>( 0x80 | (cp & 0x3F) ) );
    }
  }

  // Convert a buffer of UTF-16LE bytes (NOT terminated) to UTF-8.  Surrogate pairs
  // are joined; lone surrogates are replaced with U+FFFD.
  std::string utf16le_bytes_to_utf8( const char *bytes, size_t nbytes )
  {
    std::string out;
    out.reserve( nbytes );
    size_t i = 0;
    while( (i + 2) <= nbytes )
    {
      const uint16_t u = read_u16le( bytes + i );
      i += 2;
      uint32_t cp = u;
      if( u >= 0xD800 && u <= 0xDBFF )
      {
        // High surrogate; pair with a following low surrogate.  Lone or
        // unpaired surrogates (including a high surrogate at end-of-buffer
        // with no room for its low surrogate) become U+FFFD so we never
        // emit invalid UTF-8.
        if( (i + 2) <= nbytes )
        {
          const uint16_t low = read_u16le( bytes + i );
          if( low >= 0xDC00 && low <= 0xDFFF )
          {
            cp = 0x10000 + ((static_cast<uint32_t>(u - 0xD800) << 10) | (low - 0xDC00));
            i += 2;
          }else
          {
            cp = 0xFFFD;
          }
        }else
        {
          cp = 0xFFFD;
        }
      }else if( u >= 0xDC00 && u <= 0xDFFF )
      {
        cp = 0xFFFD;
      }
      utf8_append( out, cp );
    }
    return out;
  }

  // Parse a "KEY=value" header text block into a map.  Last-write-wins.  Lines
  // without '=' are ignored.  Trailing CR/LF and surrounding whitespace are
  // stripped from the value.  PEAKS=N and ZONES=N markers cause the next N
  // lines (which carry per-peak / per-zone data) to be skipped.
  void parse_kv_block( const std::string &block,
                       std::map<std::string,std::string> &out )
  {
    // Cap total processed lines to bound memory growth on a hostile input
    // that fills the size cap with millions of short KEY=V lines.  100k is
    // far above any plausible legitimate SpectraLine header.
    constexpr int kMaxLines = 100000;
    int line_count = 0;
    std::istringstream is( block );
    std::string line;
    int skip_lines = 0;
    while( SpecUtils::safe_get_line( is, line, 16384 ) )
    {
      if( ++line_count > kMaxLines )
        break;
      if( skip_lines > 0 )
      {
        --skip_lines;
        continue;
      }

      // Trim trailing CR
      while( !line.empty() && (line.back() == '\r' || line.back() == '\n') )
        line.pop_back();
      if( line.empty() )
        continue;

      const size_t eq = line.find( '=' );
      if( eq == std::string::npos )
        continue;
      std::string key = line.substr( 0, eq );
      std::string val = line.substr( eq + 1 );
      SpecUtils::trim( key );
      SpecUtils::trim( val );
      if( key.empty() )
        continue;

      // Recognize PEAKS / ZONES counts and skip the corresponding number of
      // detail lines in the header so the scanner stays clean.
      if( key == "PEAKS" || key == "ZONES" )
      {
        int n = 0;
        if( SpecUtils::parse_int( val.c_str(), val.size(), n ) && n > 0 && n < 100000 )
          skip_lines = n;
        out[key] = val;
        continue;
      }

      out[key] = val;
    }
  }

  // Append `text` to `target`, preceded by a space if `target` is non-empty.
  void append_remark( std::vector<std::string> &remarks, const std::string &text )
  {
    if( !text.empty() )
      remarks.push_back( text );
  }

  // If `kv` has the given key and the value is non-empty, push "label: value"
  // (cp1251-decoded) onto `remarks`.
  void remark_if_present( std::vector<std::string> &remarks,
                          const std::map<std::string,std::string> &kv,
                          const std::string &key,
                          const std::string &label )
  {
    auto it = kv.find( key );
    if( it == kv.end() || it->second.empty() )
      return;
    remarks.push_back( label + ": " + cp1251_to_utf8(it->second) );
  }

  // Result of parsing the KV header.  The actual writes to Measurement /
  // SpecFile fields happen inside SpecFile member functions (which are
  // friends of Measurement) so this struct exists to bridge the helper.
  struct ParsedSpectraLineHeader
  {
    SpecUtils::time_point_t start_time = {};
    bool has_start_time = false;
    bool has_live_time = false;
    bool has_real_time = false;
    float live_time = 0.0f;
    float real_time = 0.0f;
    std::string title;                   // SHIFR (cp1251 -> utf8)
    std::string detector_id;             // DETECTOR
    std::string instrument_model;        // CONFIGNAME
    std::vector<float> energy_poly_coeffs; // ENERGY (after potentually stripping leading order)
    bool has_dose_rate = false;
    float dose_rate = 0.0f;
    int spectrsize = -1;                 // SPECTRSIZE if declared
    std::vector<std::string> meas_remarks;
    std::vector<std::string> meas_parse_warnings;
    std::vector<std::string> file_remarks;
  };

  // Parse the KV header into a struct.  No writes to Measurement; the caller
  // applies the result.  See PDF §7.5.2.1 and the field map in the plan.
  void parse_spectraline_header_kv( const std::map<std::string,std::string> &kv,
                                    ParsedSpectraLineHeader &p )
  {
    auto get = [&kv]( const char *key ) -> std::string {
      auto it = kv.find( key );
      return (it == kv.end()) ? std::string() : it->second;
    };

    // Times.  SpectraLine LSRM SPE writes dates as `DD-MM-YY HH:MM:SS[.fff]`
    // (per the PDF examples and confirmed against the BeqMoni reference XML
    // twin).  `time_from_string` parses `DD/MM/YY` correctly with the
    // LittleEndianFirst hint but interprets dash-separated `DD-MM-YY` as
    // `YY-MM-DD` and reorders the components.  Normalize dashes-in-the-date
    // to slashes so the parser sees the unambiguous DD/MM/YY layout.
    auto normalize_lsrm_date = []( std::string s ) -> std::string {
      const size_t space = s.find( ' ' );
      const size_t scan_end = (space == std::string::npos) ? s.size() : space;
      for( size_t i = 0; i < scan_end; ++i )
      {
        if( s[i] == '-' )
          s[i] = '/';
      }
      return s;
    };

    {
      const std::string startdate = get("MEASBEGIN");
      if( !startdate.empty() )
      {
        p.start_time = SpecUtils::time_from_string(
            normalize_lsrm_date(startdate),
            SpecUtils::DateParseEndianType::LittleEndianFirst );
        p.has_start_time = !SpecUtils::is_special( p.start_time );
      }

      const std::string tlive = get("TLIVE");
      float v = 0.0f;
      if( !tlive.empty() && SpecUtils::parse_float( tlive.c_str(), tlive.size(), v ) )
      {
        p.live_time = v;
        p.has_live_time = true;
      }
      const std::string treal = get("TREAL");
      v = 0.0f;
      if( !treal.empty() && SpecUtils::parse_float( treal.c_str(), treal.size(), v ) )
      {
        p.real_time = v;
        p.has_real_time = true;
      }
    }

    // Title (sample identifier) and instrument metadata.
    {
      const std::string shifr = get("SHIFR");
      if( !shifr.empty() )
        p.title = cp1251_to_utf8(shifr);
      const std::string detector = get("DETECTOR");
      if( !detector.empty() )
        p.detector_id = cp1251_to_utf8(detector);
      const std::string config = get("CONFIGNAME");
      if( !config.empty() )
        p.instrument_model = cp1251_to_utf8(config);
    }

    // Energy calibration: ENERGY=order,c0,c1,...
    {
      const std::string energy = get("ENERGY");
      if( !energy.empty() )
      {
        std::vector<float> coeffs;
        if( SpecUtils::split_to_floats( energy, coeffs ) && (coeffs.size() >= 2) )
        {
          // First value is polynomial order; remaining values are coefficients.
          if( (std::floor(coeffs[0]) == coeffs[0])
             && (coeffs.size() >= 3)
             && (coeffs[0] > 0.0f)
             && (coeffs[2] > 0.0f) //positive gain
             && ((static_cast<size_t>(coeffs[0]) + 2) == coeffs.size()) )
          {
            p.energy_poly_coeffs.assign( coeffs.begin() + 1, coeffs.end() );
          }else
          {
            if( coeffs[1] > 0.0f )
              p.energy_poly_coeffs.assign( coeffs.begin(), coeffs.end() );
            else
              p.energy_poly_coeffs.assign( coeffs.begin() + 1, coeffs.end() );
          }
        }
      }
    }

    // SPECTRSIZE consistency hint.
    {
      auto it = kv.find( "SPECTRSIZE" );
      if( it != kv.end() )
      {
        int n = 0;
        if( SpecUtils::parse_int( it->second.c_str(), it->second.size(), n ) && n > 0 )
          p.spectrsize = n;
      }
    }

    // DOSERATE -> dose_rate
    {
      const std::string dr = get("DOSERATE");
      if( !dr.empty() )
      {
        float v = 0.0f;
        if( SpecUtils::parse_float( dr.c_str(), dr.size(), v ) )
        {
          p.dose_rate = v;
          p.has_dose_rate = true;
        }else
        {
          p.meas_remarks.push_back( "DOSERATE=" + dr );
        }
      }
    }

    // Remarks (everything else, cp1251-decoded where applicable).
    auto &m_remarks = p.meas_remarks;
    remark_if_present( m_remarks, kv, "MEASEND",         "Measurement end time" );
    remark_if_present( m_remarks, kv, "PREPBEGIN",       "Sample preparation begin" );
    remark_if_present( m_remarks, kv, "PREPEND",         "Sample preparation end" );
    remark_if_present( m_remarks, kv, "OPERATOR",        "Operator" );
    remark_if_present( m_remarks, kv, "NOMER",           "Sample number" );
    remark_if_present( m_remarks, kv, "TYPE",            "Measurement type" );
    remark_if_present( m_remarks, kv, "GEOMETRY",        "Geometry" );
    remark_if_present( m_remarks, kv, "SETTYPE",         "Container set" );
    remark_if_present( m_remarks, kv, "CONTTYPE",        "Container type" );
    remark_if_present( m_remarks, kv, "MATERIAL",        "Sample material" );
    remark_if_present( m_remarks, kv, "DISTANCE",        "Source-to-detector distance (cm)" );
    remark_if_present( m_remarks, kv, "DETRADIUS",       "Detector radius (cm)" );
    remark_if_present( m_remarks, kv, "AMPLIFICATION",   "Amplification" );
    remark_if_present( m_remarks, kv, "RAWMASS",         "Raw mass (g; value;uncertainty)" );
    remark_if_present( m_remarks, kv, "PROBEMASS",       "Probe mass (g; value;uncertainty)" );
    remark_if_present( m_remarks, kv, "SAMPLEMASS",      "Sample mass (g; value;uncertainty)" );
    remark_if_present( m_remarks, kv, "RAWVOLUME",       "Raw volume (mL; value;uncertainty)" );
    remark_if_present( m_remarks, kv, "PROBEVOLUME",     "Probe volume (mL; value;uncertainty)" );
    remark_if_present( m_remarks, kv, "SAMPLEVOLUME",    "Sample volume (mL; value;uncertainty)" );
    remark_if_present( m_remarks, kv, "ENERGY_QUALITY",  "Energy calibration quality (chi-squared, integral, nonlinearity)" );
    remark_if_present( m_remarks, kv, "FWHM",            "FWHM polynomial" );
    remark_if_present( m_remarks, kv, "COMMENT",         "Comment" );

    // PROGVERSION -> file-level remark, once.
    {
      const std::string pv = get("PROGVERSION");
      if( !pv.empty() )
        p.file_remarks.push_back( "SpectraLine version: " + pv );
    }
  }

  // Read `nchannels` int32-LE values starting at `bytes` into a float vector,
  // returning the sum.  Uses read_i32le so the result is host-endian-independent.
  std::shared_ptr<std::vector<float>> read_int32le_spectrum( const char *bytes,
                                                             size_t nchannels,
                                                             double &sum )
  {
    auto out = std::make_shared<std::vector<float>>( nchannels, 0.0f );
    sum = 0.0;
    for( size_t i = 0; i < nchannels; ++i )
    {
      const int32_t v = read_i32le( bytes + 4 * i );
      const float f = static_cast<float>( v );
      (*out)[i] = f;
      if( v > 0 )
        sum += static_cast<double>( v );
    }
    return out;
  }

  // Find the literal "SPECTR=" tag in raw bytes starting at `pos`.  Returns
  // std::string::npos on failure.
  size_t find_spectr_marker( const std::string &raw, size_t pos = 0 )
  {
    return raw.find( "SPECTR=", pos );
  }

  // Find the UTF-16LE-encoded "SPECTR=" byte pattern.
  size_t find_spectr_marker_utf16le( const std::string &raw, size_t pos = 0 )
  {
    static const char kPattern[] = "S\0P\0E\0C\0T\0R\0=\0";
    return raw.find( std::string(kPattern, sizeof(kPattern) - 1), pos );
  }

  // Sub-parse a SpectraLine .spe-style block (KV header up to SPECTR=, then raw
  // int32-LE channels) into the given Measurement.  `block_start` and `block_end`
  // bound the bytes to consider within `raw`.  Returns true on success.
  // Result of parsing a single .spe-style block (header + binary spectrum).
  struct ParsedSpeBlock
  {
    std::shared_ptr<std::vector<float>> channel_counts;
    double sum = 0.0;
    ParsedSpectraLineHeader header;
  };

  // Apply a parsed block to `meas` using ONLY public Measurement setters.
  // Returns the file-level remarks and the SpecFile-level instrument metadata
  // through the out-parameters so the caller (a SpecFile member, which has
  // friend access to its own protected fields) can apply those.
  void apply_parsed_to_measurement( SpecUtils::Measurement &meas,
                                    const ParsedSpeBlock &p,
                                    std::vector<std::string> &out_file_remarks,
                                    std::string &out_instrument_id,
                                    std::string &out_instrument_model )
  {
    out_file_remarks.insert( out_file_remarks.end(),
                             p.header.file_remarks.begin(),
                             p.header.file_remarks.end() );
    if( !p.header.detector_id.empty() )
      out_instrument_id = p.header.detector_id;
    if( !p.header.instrument_model.empty() )
      out_instrument_model = p.header.instrument_model;

    if( !p.header.title.empty() )
      meas.set_title( p.header.title );

    if( p.header.has_start_time )
      meas.set_start_time( p.header.start_time );

    // Setting gamma counts first ensures live/real time and the count sum are
    // applied atomically and the channel count is fixed before energy cal.
    if( p.channel_counts )
    {
      const float lt = p.header.has_live_time ? p.header.live_time : 0.0f;
      const float rt = p.header.has_real_time ? p.header.real_time : 0.0f;
      // Assign a placeholder calibration first because set_gamma_counts asserts
      // energy_calibration_ is non-null on entry.
      auto placeholder = std::make_shared<const SpecUtils::EnergyCalibration>();
      meas.set_energy_calibration( placeholder );
      meas.set_gamma_counts( p.channel_counts, lt, rt );
    }

    // Build remarks: meas-level remarks + dose-rate-as-remark fallback.
    std::vector<std::string> remarks = p.header.meas_remarks;
    if( p.header.has_dose_rate )
    {
      // Dose rate has no public Measurement setter that doesn't also touch
      // exposure rate; fall back to a remark so the value isn't lost.
      remarks.push_back( "DOSERATE: " + std::to_string(p.header.dose_rate) );
    }
    if( !remarks.empty() )
      meas.set_remarks( remarks );

    // Build the full parse-warnings list once and apply at the end so we
    // don't `set_parse_warnings` twice (which replaces, not appends).
    std::vector<std::string> pw = p.header.meas_parse_warnings;
    if( p.header.spectrsize > 0 && p.channel_counts
        && static_cast<size_t>(p.header.spectrsize) != p.channel_counts->size() )
    {
      pw.push_back( "SPECTRSIZE=" + std::to_string(p.header.spectrsize)
        + " disagrees with computed channel count "
        + std::to_string(p.channel_counts->size()) );
    }

    // Energy calibration (after set_gamma_counts so channel count is final).
    if( !p.header.energy_poly_coeffs.empty() && p.channel_counts && !p.channel_counts->empty() )
    {
      try
      {
        auto cal = std::make_shared<SpecUtils::EnergyCalibration>();
        cal->set_polynomial( p.channel_counts->size(), p.header.energy_poly_coeffs, {} );
        meas.set_energy_calibration( cal );
      }catch( std::exception &e )
      {
        pw.push_back( "Energy calibration invalid: " + std::string(e.what()) );
      }
    }

    if( !pw.empty() )
      meas.set_parse_warnings( pw );
  }

  // Parse the block, populating `out`.  No writes to Measurement; the caller
  // (a SpecFile member that's a friend of Measurement) applies the result.
  bool parse_spectraline_spe_block( const std::string &raw,
                                    size_t block_start,
                                    size_t block_end,
                                    ParsedSpeBlock &out )
  {
    // Defensive: callers today always pass block_end <= raw.size(), but the
    // invariant is load-bearing for the substr / pointer arithmetic below.
    if( block_end > raw.size() ) block_end = raw.size();
    if( block_end <= block_start )
      return false;

    const size_t spectr_pos = find_spectr_marker( raw, block_start );
    if( spectr_pos == std::string::npos || spectr_pos >= block_end )
      return false;

    const size_t spec_start = spectr_pos + 7;
    if( spec_start >= block_end )
      return false;

    const size_t spec_bytes = block_end - spec_start;
    const size_t nchannels = spec_bytes / 4;
    if( nchannels < 64 )
      return false;
    if( nchannels > SpecUtils::EnergyCalibration::sm_max_channels )
      return false;

    const std::string header_cp1251 = raw.substr( block_start, spectr_pos - block_start );
    const std::string header_utf8 = cp1251_to_utf8( header_cp1251 );

    std::map<std::string,std::string> kv;
    parse_kv_block( header_utf8, kv );
    parse_spectraline_header_kv( kv, out.header );

    out.channel_counts = read_int32le_spectrum( raw.data() + spec_start, nchannels, out.sum );

    return true;
  }

  // Scan a .spef byte buffer for all <START NAME>...<END NAME> sections.  The
  // section name is whatever appears between "<START " and ">".  Body bytes are
  // bracketed by the byte just past the opening tag's '\n' (or '>') and the byte
  // just before the closing tag's leading '<'.
  struct SpefSection {
    std::string name;
    size_t body_start;
    size_t body_end;
  };

  std::vector<SpefSection> scan_spef_sections( const std::string &raw )
  {
    // The legitimate SpectraLine .spef format has ~9 well-known sections.
    // Cap section count and per-section name length so a hostile file with
    // millions of unmatched <START X> openings (each forcing a full-file
    // close-tag find) cannot turn the scanner into an O(n^2) sink.
    constexpr size_t kMaxSections = 256;
    constexpr size_t kMaxNameLen  = 128;

    std::vector<SpefSection> out;
    size_t pos = 0;
    while( pos < raw.size() && out.size() < kMaxSections )
    {
      const size_t open_pos = raw.find( "<START ", pos );
      if( open_pos == std::string::npos )
        break;
      const size_t open_end = raw.find( '>', open_pos );
      if( open_end == std::string::npos )
        break;
      const size_t name_len = open_end - (open_pos + 7);
      if( name_len > kMaxNameLen )
      {
        // Skip this implausible <START tag and move past it.
        pos = open_end + 1;
        continue;
      }
      std::string name = raw.substr( open_pos + 7, name_len );
      SpecUtils::trim( name );

      // Body starts after the line break following the opening tag (skip \r\n).
      size_t body_start = open_end + 1;
      if( body_start < raw.size() && raw[body_start] == '\r' )
        ++body_start;
      if( body_start < raw.size() && raw[body_start] == '\n' )
        ++body_start;

      // Closing tag for this section.  Allow either "<END NAME>" or generic
      // "<END " followed by the same name.
      const std::string close_tag = "<END " + name + ">";
      size_t close_pos = raw.find( close_tag, body_start );

      SpefSection s;
      s.name = name;
      s.body_start = body_start;
      if( close_pos == std::string::npos )
      {
        // No matching close tag.  Tolerate this for the LAST section in the
        // file (e.g. SPECTRUM DATA in our reference sample): treat the body as
        // running to end-of-file.  The next loop iteration will not find any
        // more <START tags, so this is naturally terminal.
        s.body_end = raw.size();
        out.push_back( s );
        break;
      }

      s.body_end = close_pos;
      out.push_back( s );

      pos = close_pos + close_tag.size();
    }
    return out;
  }

  // Find longest run of printable ASCII (32..126) of length >= min_len in `bytes`.
  std::string extract_longest_ascii( const char *bytes, size_t n, size_t min_len )
  {
    size_t best_pos = 0, best_len = 0, cur_pos = 0, cur_len = 0;
    for( size_t i = 0; i < n; ++i )
    {
      const unsigned char b = static_cast<unsigned char>( bytes[i] );
      if( b >= 32 && b <= 126 )
      {
        if( cur_len == 0 )
          cur_pos = i;
        ++cur_len;
        if( cur_len > best_len )
        {
          best_len = cur_len;
          best_pos = cur_pos;
        }
      }else
      {
        cur_len = 0;
      }
    }
    if( best_len < min_len )
      return std::string();
    return std::string( bytes + best_pos, best_len );
  }

  // Decode 6 little-endian uint16 values at `p` as Y,M,D,h,m,s.  Returns true
  // and fills `tp` if the values look like a sensible date in [1990, 2100].
  bool parse_sps_dt6( const char *p, SpecUtils::time_point_t &tp )
  {
    const uint16_t Y = read_u16le( p + 0 );
    const uint16_t M = read_u16le( p + 2 );
    const uint16_t D = read_u16le( p + 4 );
    const uint16_t h = read_u16le( p + 6 );
    const uint16_t m = read_u16le( p + 8 );
    const uint16_t s = read_u16le( p + 10 );
    if( Y < 1990 || Y > 2100 ) return false;
    if( M < 1   || M > 12 )    return false;
    if( D < 1   || D > 31 )    return false;
    if( h > 23 )               return false;
    if( m > 59 )               return false;
    if( s > 60 )               return false;

    char buf[40];
    std::snprintf( buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", Y, M, D, h, m, s );
    tp = SpecUtils::time_from_string( buf );
    return !SpecUtils::is_special( tp );
  }
}//namespace


namespace SpecUtils
{
bool SpecFile::load_lsrm_spe_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_lsrm_spe( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_lsrm_spe_file( const std::string &filename );

  
bool SpecFile::load_from_lsrm_spe( std::istream &input )
{
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  
  try
  {
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( orig_pos, ios::beg );
    const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );
    if( filesize > 512*1024 )
      throw runtime_error( "File to large to be LSRM SPE" );
    
    // Read the whole file up front: the KEY=VALUE header can exceed a couple kB
    // (large CALC_* / declared-nuclide blocks push the "SPECTR=" marker well past
    // any fixed initial window), and the file is size-capped above regardless.
    string data( filesize, '\0' );
    input.read( &(data[0]), filesize );
    if( static_cast<size_t>(input.gcount()) != filesize )
      throw runtime_error( "Failed to read LSRM SPE" );

    const size_t spec_tag_pos = data.find("SPECTR=");
    if( spec_tag_pos == string::npos )
      throw runtime_error( "Couldnt find SPECTR" );
    
    const size_t spec_start_pos = spec_tag_pos + 7;
    const size_t nchannel = (filesize - spec_start_pos) / 4;
    if( nchannel < 128 )
      throw runtime_error( "Not enough channels" );
    
    if( nchannel > 68000 )
      throw runtime_error( "To many channels" );
    
    //We could have the next test, but lets be loose right now.
    //if( ((filesize - spec_start_pos) % 4) != 0 )
    //  throw runtime_error( "Spec size not multiple of 4" );

    // Parse KEY=VALUE fields only from the text header preceding "SPECTR=" so raw
    // int32 spectrum bytes can never masquerade as a header line.
    const string header = data.substr( 0, spec_tag_pos );
    auto getval = [&header]( const string &tag ) -> string {
      const size_t pos = header.find( tag );
      if( pos == string::npos )
        return "";
      
      const size_t value_start = pos + tag.size();
      const size_t endline = header.find_first_of( "\r\n", value_start );
      if( endline == string::npos )
        return "";

      const string value = header.substr( pos+tag.size(), endline - value_start );
      return SpecUtils::trim_copy( value );
    };//getval
    
    auto meas = make_shared<Measurement>();
    
    string startdate = getval( "MEASBEGIN=" );
    if( startdate.empty() )
    {
      startdate = getval( "DATE=" );
      startdate += " " + getval( "TIME=" );
    }
    
    meas->start_time_ = SpecUtils::time_from_string( startdate.c_str() );
    
    {
      const string tlive = getval("TLIVE=");
      if( !SpecUtils::parse_float( tlive.c_str(), tlive.size(), meas->live_time_ ) )
        meas->live_time_ = 0.0f;
      const string treal = getval("TREAL=");
      if( !SpecUtils::parse_float( treal.c_str(), treal.size(), meas->real_time_ ) )
        meas->real_time_ = 0.0f;
    }
    
    instrument_id_ = getval( "DETECTOR=" );
    
    // ENERGY=order,c0,c1,...  The leading value is the polynomial order, not a
    // coefficient (matches load_from_spectraline_spe); drop it before use.
    // Trailing zero coefficients (padding) are harmless higher-order terms.
    const string energy = getval( "ENERGY=" );
    vector<float> cal_coeffs;
    if( SpecUtils::split_to_floats( energy, cal_coeffs ) && (cal_coeffs.size() >= 2) )
    {
      cal_coeffs.erase( cal_coeffs.begin() );
      try
      {
        auto newcal = make_shared<EnergyCalibration>();
        newcal->set_polynomial( nchannel, cal_coeffs, {} );
        meas->energy_calibration_ = newcal;
      }catch( std::exception &e )
      {
        meas->parse_warnings_.push_back( "Energy calibration invalid: " + string(e.what()) );
      }
    }//if( parsed energy cal coefficients )
    
    const string comment = getval( "COMMENT=" );
    if( !comment.empty() )
      remarks_.push_back( comment );
    
    const string fwhm = getval( "FWHM=" );
    if( !fwhm.empty() )
      remarks_.push_back( "FWHM=" + fwhm );
    
    //Other things we could look for:
    //"SHIFR=", "NOMER=", "CONFIGNAME=", "PREPBEGIN=", "PREPEND=", "OPERATOR=",
    //"GEOMETRY=", "SETTYPE=", "CONTTYPE=", "MATERIAL=", "DISTANCE=", "VOLUME="
    //"WEIGHT=", "R_I_D=", "FILE_SPE="
    
    vector<int32_t> spectrumint( nchannel, 0 );
    memcpy( &(spectrumint[0]), &(data[spec_start_pos]), 4*nchannel );
    
    meas->gamma_count_sum_ = 0.0f;
    auto channel_counts = make_shared<vector<float>>(nchannel);
    for( size_t i = 0; i < nchannel; ++i )
    {
      (*channel_counts)[i] = static_cast<float>( spectrumint[i] );
      meas->gamma_count_sum_ += (*channel_counts)[i];
    }
    meas->gamma_counts_ = channel_counts;
    
    measurements_.push_back( meas );

    cleanup_after_load();

    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }//try / catch to parse

  return false;
}//bool load_from_lsrm_spe( std::istream &input );


// =============================================================================
//  SpectraLine (LSRM, Russia) family parsers.
//
//  Files written by SpectraLineXX (LSRM) come in five flavours that share a
//  common conceptual structure but differ in container format:
//
//    .spe  - Windows-1251 ASCII KEY=VALUE header followed by a marker
//            "SPECTR=" and raw little-endian int32 channel counts.  Documented
//            in PDF section 7.5.2.1.
//    .spex - identical to .spe but the header text is encoded as UTF-16LE
//            (BOM FF FE).  The binary spectrum after the UTF-16-encoded
//            "SPECTR=" tag is *raw* int32-LE bytes (NOT UTF-16 wrapped).
//    .spef - multi-section processing file with <START NAME>...<END NAME>
//            blocks, including a foreground spectrum, an optional background
//            spectrum, and a number of auxiliary configuration sections.
//            Documented in PDF section 7.5.2.3.
//    .sps  - GreenStar binary: 1024-byte header followed by int32-LE channels.
//            Header layout is not documented in the available PDF spec; we
//            decode the spectrum and known date / detector-name fields and
//            mark the rest with a TODO.
//    .iec  - SpectraLine emission of the IEC 61455 international format.
//
//  All readers are endian-independent: multi-byte values go through the
//  read_u16le / read_i32le helpers above.
// =============================================================================

bool SpecFile::load_spectraline_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif

  if( !input.is_open() )
    return false;

  // Decide a primary parser to attempt based on the filename extension, falling
  // back to content sniffing.  Each load_from_* function rewinds the stream on
  // failure, so trying multiple in sequence is safe.
  std::string ext;
  const size_t dot = filename.find_last_of( '.' );
  if( dot != std::string::npos )
  {
    ext = filename.substr( dot + 1 );
    SpecUtils::to_lower_ascii( ext );
  }

  auto try_load = [&input,this]( bool (SpecFile::*fn)(std::istream&) ) -> bool
  {
    input.clear();
    input.seekg( 0, ios::beg );
    return (this->*fn)( input );
  };

  // Sniff the first 16 bytes once.
  char head[16] = {0};
  input.read( head, sizeof(head) );
  const std::streamsize head_n = input.gcount();
  input.clear();
  input.seekg( 0, ios::beg );

  enum class Kind { Spe, Spex, Spef, Sps, Iec, Unknown };
  Kind primary = Kind::Unknown;

  if( ext == "spex" )                     primary = Kind::Spex;
  else if( ext == "spef" )                primary = Kind::Spef;
  else if( ext == "sps" )                 primary = Kind::Sps;
  else if( ext == "iec" )                 primary = Kind::Iec;
  else if( ext == "spe" || ext == "txt" ) primary = Kind::Spe;
  else if( head_n >= 2 && static_cast<unsigned char>(head[0]) == 0xFF
                       && static_cast<unsigned char>(head[1]) == 0xFE )
    primary = Kind::Spex;
  else if( head_n >= 16 && std::memcmp(head, "<START SPECTRUM ", 16) == 0 )
    primary = Kind::Spef;
  else if( head_n >= 4 && head[0] == 0x00 && head[1] == 0x04
                       && head[2] == 0x00 && head[3] == 0x00 )
    primary = Kind::Sps;
  else if( head_n >= 4 && std::memcmp(head, "A004", 4) == 0 )
    primary = Kind::Iec;

  bool success = false;
  switch( primary )
  {
    case Kind::Spex:
      success = try_load( &SpecFile::load_from_spectraline_spex );
      if( !success ) success = try_load( &SpecFile::load_from_spectraline_spe );
      if( !success ) success = try_load( &SpecFile::load_from_spectraline_spef );
      break;
    case Kind::Spef:
      success = try_load( &SpecFile::load_from_spectraline_spef );
      if( !success ) success = try_load( &SpecFile::load_from_spectraline_spe );
      if( !success ) success = try_load( &SpecFile::load_from_spectraline_spex );
      break;
    case Kind::Sps:
      success = try_load( &SpecFile::load_from_spectraline_sps );
      break;
    case Kind::Iec:
      success = try_load( &SpecFile::load_from_spectraline_iec );
      break;
    case Kind::Spe:
    case Kind::Unknown:
    default:
      success = try_load( &SpecFile::load_from_spectraline_spe );
      if( !success ) success = try_load( &SpecFile::load_from_spectraline_spex );
      if( !success ) success = try_load( &SpecFile::load_from_spectraline_spef );
      if( !success ) success = try_load( &SpecFile::load_from_spectraline_sps );
      if( !success ) success = try_load( &SpecFile::load_from_spectraline_iec );
      break;
  }

  if( success )
    filename_ = filename;

  return success;
}//bool load_spectraline_file( const std::string &filename );


bool SpecFile::load_from_spectraline_spe( std::istream &input )
{
  if( !input.good() )
    return false;

  const istream::pos_type orig_pos = input.tellg();

  try
  {
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( orig_pos, ios::beg );
    const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );

    if( filesize < 64 )
      throw runtime_error( "Too small to be SpectraLine SPE" );
    if( filesize > 16ul * 1024ul * 1024ul )
      throw runtime_error( "Too large to be SpectraLine SPE" );

    std::string raw( filesize, '\0' );
    input.read( &raw[0], filesize );
    if( !input )
      throw runtime_error( "Read error" );

    // Reject obvious non-LSRM files quickly.  The PDF says any field can be
    // missing, but every SpectraLine SPE in the wild starts with one of a
    // small set of LSRM keys *at the start of a line*, so we require a
    // line-anchored match rather than a substring match (the latter would
    // accept any text file that happens to contain "TLIVE=" or "SHIFR=").
    {
      const size_t kPeek = std::min<size_t>( filesize, 4096 );
      auto line_starts_with = [&raw,kPeek]( const char *needle, size_t needle_len ) -> bool {
        if( kPeek >= needle_len && std::memcmp(raw.data(), needle, needle_len) == 0 )
          return true;
        size_t p = 0;
        while( p < kPeek )
        {
          const size_t nl = raw.find( '\n', p );
          if( nl == std::string::npos || nl + 1 >= kPeek ) return false;
          p = nl + 1;
          if( kPeek - p >= needle_len && std::memcmp(raw.data() + p, needle, needle_len) == 0 )
            return true;
        }
        return false;
      };
      if( !line_starts_with("PROGVERSION=", 12)
          && !line_starts_with("SHIFR=",      6)
          && !line_starts_with("MEASBEGIN=", 10)
          && !line_starts_with("TLIVE=",      6) )
      {
        throw runtime_error( "Not an LSRM/SpectraLine SPE header" );
      }
    }

    ParsedSpeBlock pb;
    if( !parse_spectraline_spe_block( raw, 0, raw.size(), pb ) )
      throw runtime_error( "Failed to parse SpectraLine SPE block" );

    auto meas = std::make_shared<Measurement>();
    std::vector<std::string> file_remarks_to_add;
    std::string instr_id, instr_model;
    apply_parsed_to_measurement( *meas, pb, file_remarks_to_add, instr_id, instr_model );

    if( !instr_id.empty() && instrument_id_.empty() )
      set_instrument_id( instr_id );
    if( !instr_model.empty() && instrument_model_.empty() )
      set_instrument_model( instr_model );
    for( const auto &r : file_remarks_to_add )
      add_remark( r );

    if( manufacturer_.empty() )
      set_manufacturer( "LSRM" );
    if( instrument_model_.empty() )
      set_instrument_model( "SpectraLine" );

    measurements_.push_back( meas );
    cleanup_after_load();
    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }

  return false;
}//bool load_from_spectraline_spe( std::istream &input );


bool SpecFile::load_from_spectraline_spex( std::istream &input )
{
  if( !input.good() )
    return false;

  const istream::pos_type orig_pos = input.tellg();

  try
  {
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( orig_pos, ios::beg );
    const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );

    if( filesize < 64 )
      throw runtime_error( "Too small to be SpectraLine SPEX" );
    if( filesize > 32ul * 1024ul * 1024ul )
      throw runtime_error( "Too large to be SpectraLine SPEX" );

    std::string raw( filesize, '\0' );
    input.read( &raw[0], filesize );
    if( !input )
      throw runtime_error( "Read error" );

    // BOM check
    if( raw.size() < 2 || static_cast<unsigned char>(raw[0]) != 0xFF
                       || static_cast<unsigned char>(raw[1]) != 0xFE )
    {
      throw runtime_error( "Missing UTF-16LE BOM" );
    }

    // Locate the UTF-16LE-encoded "SPECTR=" tag (14 bytes).
    const size_t tag_pos = find_spectr_marker_utf16le( raw, 2 );
    if( tag_pos == std::string::npos )
      throw runtime_error( "Couldn't find UTF-16LE SPECTR=" );

    size_t spec_start = tag_pos + 14; // size of UTF-16LE "SPECTR="
    // Skip optional UTF-16LE CR/LF following the tag.
    if( spec_start + 2 <= raw.size() && raw[spec_start] == '\r' && raw[spec_start+1] == '\0' )
      spec_start += 2;
    if( spec_start + 2 <= raw.size() && raw[spec_start] == '\n' && raw[spec_start+1] == '\0' )
      spec_start += 2;

    if( spec_start >= raw.size() )
      throw runtime_error( "No spectrum bytes after SPECTR=" );

    const size_t spec_bytes = raw.size() - spec_start;
    const size_t nchannels = spec_bytes / 4;
    if( nchannels < 64 || nchannels > EnergyCalibration::sm_max_channels )
      throw runtime_error( "Implausible channel count" );

    // Decode the UTF-16LE header (after BOM, before SPECTR= tag) to UTF-8.
    const std::string header_utf8 = utf16le_bytes_to_utf8( raw.data() + 2, tag_pos - 2 );

    std::map<std::string,std::string> kv;
    parse_kv_block( header_utf8, kv );

    ParsedSpeBlock pb;
    parse_spectraline_header_kv( kv, pb.header );
    pb.channel_counts = read_int32le_spectrum( raw.data() + spec_start, nchannels, pb.sum );

    auto meas = std::make_shared<Measurement>();
    std::vector<std::string> file_remarks_to_add;
    std::string instr_id, instr_model;
    apply_parsed_to_measurement( *meas, pb, file_remarks_to_add, instr_id, instr_model );

    if( !instr_id.empty() && instrument_id_.empty() )
      set_instrument_id( instr_id );
    if( !instr_model.empty() && instrument_model_.empty() )
      set_instrument_model( instr_model );
    for( const auto &r : file_remarks_to_add )
      add_remark( r );

    if( manufacturer_.empty() )
      set_manufacturer( "LSRM" );
    if( instrument_model_.empty() )
      set_instrument_model( "SpectraLine" );

    measurements_.push_back( meas );
    cleanup_after_load();
    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }

  return false;
}//bool load_from_spectraline_spex( std::istream &input );


bool SpecFile::load_from_spectraline_spef( std::istream &input )
{
  if( !input.good() )
    return false;

  const istream::pos_type orig_pos = input.tellg();

  try
  {
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( orig_pos, ios::beg );
    const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );

    if( filesize < 64 )
      throw runtime_error( "Too small to be SpectraLine SPEF" );
    if( filesize > 32ul * 1024ul * 1024ul )
      throw runtime_error( "Too large to be SpectraLine SPEF" );

    std::string raw( filesize, '\0' );
    input.read( &raw[0], filesize );
    if( !input )
      throw runtime_error( "Read error" );

    // Quick sanity test: the file must begin with "<START " near the top.
    if( raw.compare( 0, 7, "<START " ) != 0 )
      throw runtime_error( "Not a SpectraLine SPEF (missing leading <START >)" );

    auto sections = scan_spef_sections( raw );
    if( sections.empty() )
      throw runtime_error( "No sections found" );

    // Find the foreground spectrum block.  Per PDF 7.5.2.3 it is in the final
    // section of type SPECTRUM DATA.  We also need to overlay the metadata from
    // SPECTRUM HEADER, which is metadata-only (no SPECTR=).
    const SpefSection *header_sec = nullptr;
    const SpefSection *data_sec   = nullptr;
    const SpefSection *bg_sec     = nullptr;
    bool aux_present = false;
    for( const auto &s : sections )
    {
      if( s.name == "SPECTRUM HEADER" )      header_sec = &s;
      else if( s.name == "SPECTRUM DATA" )   data_sec   = &s;
      else if( s.name == "BACKGROUND" )      bg_sec     = &s;
      else                                   aux_present = true;
    }

    ParsedSpeBlock fg_pb;
    bool fg_ok = false;

    if( data_sec )
    {
      // Parse the data block (expects to find SPECTR= and the binary tail).
      // Then merge metadata from the header section if present.
      fg_ok = parse_spectraline_spe_block( raw, data_sec->body_start, data_sec->body_end, fg_pb );
      if( fg_ok && header_sec )
      {
        const std::string hdr_cp1251 = raw.substr( header_sec->body_start,
                                                   header_sec->body_end - header_sec->body_start );
        const std::string hdr_utf8 = cp1251_to_utf8( hdr_cp1251 );
        std::map<std::string,std::string> kv;
        parse_kv_block( hdr_utf8, kv );
        // Merge metadata from the SPECTRUM HEADER section onto fg_pb.header.
        ParsedSpectraLineHeader meta;
        parse_spectraline_header_kv( kv, meta );
        // Prefer SPECTRUM-HEADER values if SPECTRUM-DATA didn't supply them.
        if( !fg_pb.header.has_start_time && meta.has_start_time )
        {
          fg_pb.header.start_time = meta.start_time;
          fg_pb.header.has_start_time = true;
        }
        if( !fg_pb.header.has_live_time && meta.has_live_time )
        {
          fg_pb.header.live_time = meta.live_time;
          fg_pb.header.has_live_time = true;
        }
        if( !fg_pb.header.has_real_time && meta.has_real_time )
        {
          fg_pb.header.real_time = meta.real_time;
          fg_pb.header.has_real_time = true;
        }
        if( fg_pb.header.title.empty() )           fg_pb.header.title = meta.title;
        if( fg_pb.header.detector_id.empty() )     fg_pb.header.detector_id = meta.detector_id;
        if( fg_pb.header.instrument_model.empty() )fg_pb.header.instrument_model = meta.instrument_model;
        if( fg_pb.header.energy_poly_coeffs.empty() ) fg_pb.header.energy_poly_coeffs = meta.energy_poly_coeffs;
        if( fg_pb.header.spectrsize <= 0 )         fg_pb.header.spectrsize = meta.spectrsize;
        if( !fg_pb.header.has_dose_rate && meta.has_dose_rate )
        {
          fg_pb.header.dose_rate = meta.dose_rate;
          fg_pb.header.has_dose_rate = true;
        }
        fg_pb.header.meas_remarks.insert( fg_pb.header.meas_remarks.end(),
                                          meta.meas_remarks.begin(), meta.meas_remarks.end() );
        fg_pb.header.meas_parse_warnings.insert( fg_pb.header.meas_parse_warnings.end(),
                                                 meta.meas_parse_warnings.begin(),
                                                 meta.meas_parse_warnings.end() );
        fg_pb.header.file_remarks.insert( fg_pb.header.file_remarks.end(),
                                          meta.file_remarks.begin(), meta.file_remarks.end() );
      }
    }

    if( !fg_ok && header_sec )
    {
      // Fall back: some older variants put SPECTR= in the SPECTRUM HEADER section.
      fg_ok = parse_spectraline_spe_block( raw, header_sec->body_start, header_sec->body_end, fg_pb );
    }

    if( !fg_ok )
      throw runtime_error( "Could not parse SpectraLine SPEF foreground spectrum" );

    auto fg_meas = std::make_shared<Measurement>();
    {
      std::vector<std::string> file_remarks_to_add;
      std::string instr_id, instr_model;
      apply_parsed_to_measurement( *fg_meas, fg_pb, file_remarks_to_add, instr_id, instr_model );
      if( !instr_id.empty() && instrument_id_.empty() )
        set_instrument_id( instr_id );
      if( !instr_model.empty() && instrument_model_.empty() )
        set_instrument_model( instr_model );
      for( const auto &r : file_remarks_to_add )
        add_remark( r );
    }

    fg_meas->set_source_type( SourceType::Foreground );

    if( manufacturer_.empty() )
      set_manufacturer( "LSRM" );
    if( instrument_model_.empty() )
      set_instrument_model( "SpectraLine" );

    measurements_.push_back( fg_meas );

    // Optional background.  Failure here is non-fatal.
    if( bg_sec )
    {
      try
      {
        ParsedSpeBlock bg_pb;
        if( parse_spectraline_spe_block( raw, bg_sec->body_start, bg_sec->body_end, bg_pb ) )
        {
          auto bg_meas = std::make_shared<Measurement>();
          std::vector<std::string> file_rems_unused;
          std::string id_unused, model_unused;
          apply_parsed_to_measurement( *bg_meas, bg_pb, file_rems_unused, id_unused, model_unused );
          // Share foreground's calibration if the background didn't carry
          // its own and the channel counts match.
          if( (!bg_meas->energy_calibration() || !bg_meas->energy_calibration()->valid())
              && fg_meas->energy_calibration() && fg_meas->energy_calibration()->valid()
              && bg_meas->num_gamma_channels() == fg_meas->num_gamma_channels() )
          {
            bg_meas->set_energy_calibration( fg_meas->energy_calibration() );
          }
          bg_meas->set_source_type( SourceType::Background );
          // Push *last* so any setter exception above leaves measurements_
          // with only the foreground.
          measurements_.push_back( bg_meas );
        }
      }catch( std::exception & )
      {
        // Swallow; foreground already loaded.
      }
    }

    if( aux_present )
    {
      add_remark(
        "SpectraLine .spef: auxiliary sections (CNF / CPT / ZONES / LIBRARY / "
        "SRC / EFA) were present but are not parsed; only the foreground"
        + std::string( bg_sec ? " and background" : "" )
        + " spectra were extracted." );
    }

    cleanup_after_load();
    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }

  return false;
}//bool load_from_spectraline_spef( std::istream &input );


bool SpecFile::load_from_spectraline_sps( std::istream &input )
{
  if( !input.good() )
    return false;

  const istream::pos_type orig_pos = input.tellg();

  try
  {
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( orig_pos, ios::beg );
    const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );

    constexpr size_t kHeaderBytes = 1024;

    if( filesize <= kHeaderBytes )
      throw runtime_error( "Too small to be SpectraLine GreenStar SPS" );
    if( ((filesize - kHeaderBytes) % 4u) != 0 )
      throw runtime_error( "Spectrum tail not multiple of 4 bytes" );

    const size_t nchannels = (filesize - kHeaderBytes) / 4u;
    if( nchannels < 64 || nchannels > EnergyCalibration::sm_max_channels )
      throw runtime_error( "Implausible channel count" );

    std::string raw( filesize, '\0' );
    input.read( &raw[0], filesize );
    if( !input )
      throw runtime_error( "Read error" );

    // The first 4 bytes encode 0x00000400 = 1024 little-endian.  Interpret as
    // the channel count or header length and reject if not.
    const uint32_t magic = static_cast<uint32_t>( read_i32le( raw.data() ) );
    if( magic != 0x400 )
      throw runtime_error( "Missing GreenStar SPS magic 0x00000400" );

    auto meas = std::make_shared<Measurement>();

    // Dates: at offsets 0x100 and 0x10C (each 12 bytes = 6 uint16 LE: Y/M/D/h/m/s).
    SpecUtils::time_point_t t_prep = {}, t_meas = {};
    const bool prep_ok = (filesize >= 0x100 + 12)
                       && parse_sps_dt6( raw.data() + 0x100, t_prep );
    const bool meas_ok = (filesize >= 0x10C + 12)
                       && parse_sps_dt6( raw.data() + 0x10C, t_meas );
    if( meas_ok )
      meas->set_start_time( t_meas );

    std::vector<std::string> remarks;
    if( prep_ok && !SpecUtils::is_special(t_prep) )
    {
      remarks.push_back( "Sample preparation end: "
        + SpecUtils::to_extended_iso_string( t_prep ) );
    }
    if( !remarks.empty() )
      meas->set_remarks( remarks );

    // Detector model: longest ASCII run in [0x180, 0x200).
    if( filesize >= 0x200 )
    {
      const std::string det = extract_longest_ascii( raw.data() + 0x180, 0x200 - 0x180, 8 );
      if( !det.empty() && instrument_id_.empty() )
        set_instrument_id( det );
    }

    // Spectrum at offset 0x400 (kHeaderBytes).
    double sum = 0.0;
    auto channel_counts = read_int32le_spectrum( raw.data() + kHeaderBytes, nchannels, sum );
    {
      // Placeholder calibration to satisfy set_gamma_counts assertion.
      auto placeholder = std::make_shared<const SpecUtils::EnergyCalibration>();
      meas->set_energy_calibration( placeholder );
    }
    meas->set_gamma_counts( channel_counts, 0.0f, 0.0f );

    if( manufacturer_.empty() )
      set_manufacturer( "LSRM" );
    if( instrument_model_.empty() )
      set_instrument_model( "SpectraLine GreenStar" );

    // TODO(SpectraLine .sps): The 1024-byte header layout is not documented in
    // the SpectraLine PDF (spectralinexx_2.0_basic_functions_rus.pdf only lists
    // GreenStar as a supported open-format).  We currently decode the magic
    // word at 0x000, the 6-uint16 dates at 0x100 and 0x10C, and the longest
    // printable ASCII run in [0x180, 0x200) as an instrument id.  Bytes
    // 0x004-0x0FF, 0x118-0x17F, and 0x1A0-0x3FF likely contain additional
    // float fields (suspected: live_time, real_time, mass / volume, distance);
    // these will need to be reverse-engineered against more sample files or by
    // consulting LSRM directly.
    {
      std::vector<std::string> warnings = {
        "SpectraLine GreenStar .sps: only the spectrum, two date fields, and the"
        " detector model string are decoded.  Header bytes 0x004-0x0FF, "
        "0x118-0x17F, and 0x1A0-0x3FF are not yet documented." };
      meas->set_parse_warnings( warnings );
    }

    measurements_.push_back( meas );
    cleanup_after_load();
    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }

  return false;
}//bool load_from_spectraline_sps( std::istream &input );


bool SpecFile::load_from_spectraline_iec( std::istream &input )
{
  if( !input.good() )
    return false;

  const istream::pos_type orig_pos = input.tellg();

  try
  {
    // File-size cap: bound memory growth on a hostile multi-GB file of
    // 5-byte "A004\n" lines (each producing a std::string in `lines`).
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( orig_pos, ios::beg );
    const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );
    if( filesize < 16 )
      throw runtime_error( "Too small to be SpectraLine IEC" );
    if( filesize > 32ul * 1024ul * 1024ul )
      throw runtime_error( "Too large to be SpectraLine IEC" );

    // Read first line to verify the A004 prefix before doing anything else.
    std::string first;
    if( !SpecUtils::safe_get_line( input, first, 256 ) )
      throw runtime_error( "Empty IEC file" );
    if( first.size() < 4 || first.compare(0, 4, "A004") != 0 )
      throw runtime_error( "Not a SpectraLine IEC 1455 file" );

    // Rewind and read the entire file so we can index lines positionally.
    input.clear();
    input.seekg( orig_pos, ios::beg );

    // Cap line count at 5x the maximum supported channel count: each spectrum
    // row carries 5 channels, plus a generous allowance for header / SPARE.
    const size_t kMaxLines = 5 * EnergyCalibration::sm_max_channels + 1024;

    std::vector<std::string> lines;
    {
      std::string line;
      while( SpecUtils::safe_get_line( input, line, 256 ) )
      {
        // EOF reads can return an empty `line` with eof set: skip silently.
        if( line.empty() )
        {
          if( input.eof() )
            break;
          continue;
        }
        if( lines.size() >= kMaxLines )
          throw runtime_error( "IEC file has too many lines" );
        if( line.size() < 4 || line.compare(0, 4, "A004") != 0 )
          throw runtime_error( "Stray non-A004 line in IEC file" );
        // Strip "A004" + a single space (if present).
        std::string payload = line.substr( 4 );
        if( !payload.empty() && payload.front() == ' ' )
          payload.erase( 0, 1 );
        // Strip trailing CR.
        while( !payload.empty() && (payload.back() == '\r' || payload.back() == '\n') )
          payload.pop_back();
        // Strip trailing spaces (the format right-pads to column 80).
        while( !payload.empty() && payload.back() == ' ' )
          payload.pop_back();
        lines.push_back( std::move(payload) );
      }
    }

    if( lines.size() < 6 )
      throw runtime_error( "Too few records in IEC file" );

    auto meas = std::make_shared<Measurement>();
    std::vector<std::string> meas_remarks;
    std::vector<std::string> meas_parse_warnings;
    std::string instrument_id_local, instrument_model_local;
    int digital_offset = 0;

    auto rstrip = []( std::string s ) -> std::string {
      while( !s.empty() && s.back() == ' ' ) s.pop_back();
      return s;
    };

    // Record 1 (IEC 1455 §4.1-4.5) is fixed-width:
    //   bytes  0-7 : System identification (8 chars; leading spaces NOT zeros)
    //   bytes  8-15: Sub-system identification (8 chars; leading spaces NOT zeros)
    //   bytes 16-19: ADC number (4 chars; leading spaces ARE zeros)
    //   bytes 20-23: Segment number (4 chars; leading spaces ARE zeros)
    //   bytes 24-29: Digital offset (6 chars; leading spaces ARE zeros)
    {
      const std::string &r1 = lines[0];
      auto field = [&r1]( size_t off, size_t len ) -> std::string {
        if( off >= r1.size() ) return "";
        return r1.substr( off, std::min(len, r1.size() - off) );
      };
      const std::string sys_id    = rstrip( cp1251_to_utf8( field(0, 8) ) );
      const std::string subsys_id = rstrip( cp1251_to_utf8( field(8, 8) ) );
      // SpecUtils::trim works on the numeric fields (they may be all spaces).
      std::string adc_str = field(16, 4);  SpecUtils::trim( adc_str );
      std::string seg_str = field(20, 4);  SpecUtils::trim( seg_str );
      std::string off_str = field(24, 6);  SpecUtils::trim( off_str );

      if( !sys_id.empty() )
        instrument_model_local = sys_id;
      if( !subsys_id.empty() )
        instrument_id_local = subsys_id;
      if( !sys_id.empty() || !subsys_id.empty() )
      {
        const std::string title = sys_id
          + (subsys_id.empty() ? "" : (sys_id.empty() ? "" : " ") + subsys_id);
        meas->set_title( title );
      }

      int adc = 0, seg = 0;
      if( !adc_str.empty()
          && SpecUtils::parse_int( adc_str.c_str(), adc_str.size(), adc ) && adc != 0 )
        meas_remarks.push_back( "ADC number: " + std::to_string(adc) );
      if( !seg_str.empty()
          && SpecUtils::parse_int( seg_str.c_str(), seg_str.size(), seg ) && seg != 0 )
        meas_remarks.push_back( "Segment number: " + std::to_string(seg) );
      if( !off_str.empty() )
        SpecUtils::parse_int( off_str.c_str(), off_str.size(), digital_offset );
    }

    // Record 2 (IEC 1455 §4.6-4.8): live time (14 chars), real time (14 chars),
    // number of channels (6 chars).  The fields are fixed-width but
    // whitespace-separable, so split_to_floats is sufficient.
    float live_time_val = 0.0f, real_time_val = 0.0f;
    size_t declared_nchannels = 0;
    {
      std::vector<float> nums;
      if( !SpecUtils::split_to_floats( lines[1], nums ) || nums.size() < 3 )
        throw runtime_error( "Could not parse live/real/nchannels line" );
      live_time_val = nums[0];
      real_time_val = nums[1];
      if( nums[2] > 0.0f && nums[2] < 1e7f )
        declared_nchannels = static_cast<size_t>( nums[2] );
    }

    // Line 3: two datetimes "DD/MM/YY HH:MM:SS" each.
    {
      std::istringstream is( lines[2] );
      std::vector<std::string> tok;
      std::string t;
      while( is >> t ) tok.push_back( t );
      if( tok.size() >= 2 )
      {
        const std::string s = tok[0] + " " + tok[1];
        const auto start = SpecUtils::time_from_string(
            s, SpecUtils::DateParseEndianType::LittleEndianFirst );
        if( !SpecUtils::is_special(start) )
          meas->set_start_time( start );
      }
      if( tok.size() >= 4 )
      {
        const std::string s2 = tok[2] + " " + tok[3];
        const auto t2 = SpecUtils::time_from_string(
            s2, SpecUtils::DateParseEndianType::LittleEndianFirst );
        if( !SpecUtils::is_special(t2) )
        {
          meas_remarks.push_back( "Sample preparation end: "
            + SpecUtils::to_extended_iso_string( t2 ) );
        }
      }
    }

    // Line 4: 4 floats - energy polynomial coefficients.
    std::vector<float> energy_coeffs;
    if( SpecUtils::split_to_floats( lines[3], energy_coeffs ) && !energy_coeffs.empty() )
    {
      // The line carries coefficients directly (c0, c1, c2, c3).
    }

    // Record 5 (IEC 1455 §4.12): FWHM polynomial coefficients P,Q,R,W and
    // the exponent I (typically 0.5 for sqrt or 1.0 for linear dependence).
    if( lines.size() >= 5 && !lines[4].empty() )
      meas_remarks.push_back( "FWHM polynomial coefficients: " + lines[4] );

    // Records 6-9 (IEC 1455 §4.13): four 64-char sample description lines.
    // Spec says they are filled with spaces if not used; capture non-empty
    // ones as remarks (cp1251-decoded for parity with record 1).
    for( size_t rec = 6; rec <= 9 && rec - 1 < lines.size(); ++rec )
    {
      const std::string desc = rstrip( cp1251_to_utf8( lines[rec - 1] ) );
      if( !desc.empty() )
        meas_remarks.push_back( "Sample description " + std::to_string(rec - 5) + ": " + desc );
    }

    // Records 47-58 (IEC 1455 §4.18): twelve 64-char user-defined text lines.
    for( size_t rec = 47; rec <= 58 && rec - 1 < lines.size(); ++rec )
    {
      const std::string ud = rstrip( cp1251_to_utf8( lines[rec - 1] ) );
      // Skip the spec's filler text "USER RECORDS" (or "ENREGISTREMENTS POUR
      // UTILISATEURS") which is not actual data.
      if( ud.empty() || ud == "USER RECORDS" || ud == "ENREGISTREMENTS POUR UTILISATEURS" )
        continue;
      meas_remarks.push_back( "User record " + std::to_string(rec - 46) + ": " + ud );
    }

    if( digital_offset != 0 )
    {
      // Per spec §4.5, the digital offset is added to each stored channel
      // index to obtain the ADC channel number, so the energy polynomial in
      // record 4 is in terms of (stored_ch + digital_offset).  We don't
      // currently transform the polynomial; flag this for the caller.
      meas_parse_warnings.push_back(
        "IEC 1455 digital_offset=" + std::to_string(digital_offset)
        + " is non-zero; energy calibration polynomial coefficients are in"
        " the ADC channel frame and may not match stored channel indices." );
    }

    // Records 11-46 are calibration pairs (energy/channel, energy/resolution,
    // energy/efficiency) per IEC 1455 §4.15-4.17.  We don't decode these
    // (they're typically zero-filled in SpectraLine output, and the polynomial
    // calibration in record 4 takes precedence anyway).

    // Records 59 onward (IEC 1455 §4.19): spectrum data, 5 counts per record
    // preceded by the starting channel number (6 numeric tokens per row).
    // Per spec the *last* record may have blank fields for channels in excess
    // of `declared_nchannels`, so accept partial last lines (2-6 tokens).
    auto channel_counts = std::make_shared<std::vector<float>>();
    if( declared_nchannels > 0 )
      channel_counts->reserve( declared_nchannels );
    int last_seen_channel = -1;
    for( size_t i = 5; i < lines.size(); ++i )
    {
      const std::string &ln = lines[i];
      if( ln.empty() ) continue;
      std::vector<float> vals;
      if( !SpecUtils::split_to_floats( ln, vals ) )
        continue;
      if( vals.size() < 2 || vals.size() > 6 ) continue;
      // Validate the leading channel index: non-negative integer, bounded
      // before the cast so out-of-range floats can't trigger UB.
      if( !std::isfinite(vals[0]) || vals[0] < 0.0f
          || vals[0] >= static_cast<float>(EnergyCalibration::sm_max_channels) )
        continue;
      const int ch = static_cast<int>( vals[0] );
      const size_t ncounts = vals.size() - 1;
      const bool full_row    = (vals.size() == 6);
      // Partial rows (1-4 counts) are only legal at the end of the spectrum
      // (last record per IEC 1455 §4.8).  Otherwise reject — this also
      // filters out the calibration-pair records (records 11-46) which have
      // 4 numeric tokens and would otherwise spoof a channel-0 partial row.
      const bool reaches_end = (declared_nchannels > 0)
        && (static_cast<size_t>(ch) + ncounts >= declared_nchannels);
      if( !full_row && !reaches_end ) continue;
      if( last_seen_channel >= 0 && ch != last_seen_channel + 5 ) continue;
      last_seen_channel = ch;
      for( size_t j = 1; j < vals.size(); ++j )
        channel_counts->push_back( vals[j] );
      if( reaches_end )
        break;
    }

    if( channel_counts->size() < 64 )
      throw runtime_error( "Too few channels parsed" );

    // Trim any trailing channel rows beyond the declared channel count.
    if( declared_nchannels > 0 && channel_counts->size() > declared_nchannels )
      channel_counts->resize( declared_nchannels );

    {
      // Placeholder cal so set_gamma_counts asserts pass; replaced below.
      auto placeholder = std::make_shared<const SpecUtils::EnergyCalibration>();
      meas->set_energy_calibration( placeholder );
    }
    meas->set_gamma_counts( channel_counts, live_time_val, real_time_val );

    if( !energy_coeffs.empty() )
    {
      try
      {
        auto newcal = std::make_shared<EnergyCalibration>();
        newcal->set_polynomial( channel_counts->size(), energy_coeffs, {} );
        meas->set_energy_calibration( newcal );
      }catch( std::exception &e )
      {
        meas_parse_warnings.push_back( "Energy calibration invalid: " + std::string(e.what()) );
      }
    }

    if( !meas_remarks.empty() )
      meas->set_remarks( meas_remarks );
    if( !meas_parse_warnings.empty() )
      meas->set_parse_warnings( meas_parse_warnings );

    if( manufacturer_.empty() )
      set_manufacturer( "LSRM" );
    // Prefer record-1 SYS_ID / SUBSYS_ID over the generic "SpectraLine"
    // fallback when they were present.
    if( instrument_id_.empty() && !instrument_id_local.empty() )
      set_instrument_id( instrument_id_local );
    if( instrument_model_.empty() )
      set_instrument_model( instrument_model_local.empty() ? "SpectraLine"
                                                           : instrument_model_local );

    measurements_.push_back( meas );
    cleanup_after_load();
    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }

  return false;
}//bool load_from_spectraline_iec( std::istream &input );

}//namespace SpecUtils





