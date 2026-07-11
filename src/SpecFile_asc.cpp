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

#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <float.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

/*
 The .asc/.ASC files are GADRAS-lineage ASCII exports, related to the PCF format.
 Three variants are handled:

 (A) "ECAL" header:  "<nchan> ECAL <c0> <c1> <c2> <c3> <c4>"  (5 full-range-fraction
     energy calibration coefficients), optionally followed by a PCF-style
     fixed-width metadata block (each line: value, then "! <FieldName>", with
     empty values NUL-padded), then one or more "! Record #" delimited records.

 (B) bare channel-count header: a single integer on the first line, then one or
     more "! Record #" delimited records (no file-level energy calibration).

 (C) plain single-column: first line "<nchan> <livetime> <realtime> <neutron>"
     (neutron < 0 means none), then exactly <nchan> counts, one per line.  No
     records, no calibration.

 A record (variants A & B) has the layout:
    ! Record # <n>            (marker; also "!\tRecord #<n>" or "! Record #: <n>")
    <title/description>        (e.g. "Background", "Foreground", "Background2 Det: 4")
    <0+ metadata/blank lines>  (e.g. "U1B_ID: 010, FilterID: 01, Temp(deg C): 20")
    <date/time>                (e.g. "28-Mar-2008 09:37:40")
    <livetime> <realtime> <nchannels>
    <10 parameter values>      (usually zeros)
    <nchannels gamma counts>   (integer or scientific, whitespace-separated, wrapped)
*/

namespace
{
  using SpecUtils::trim;
  using SpecUtils::trim_copy;
  using SpecUtils::split;
  using SpecUtils::icontains;
  using SpecUtils::iequals_ascii;
  using SpecUtils::parse_int;
  using SpecUtils::parse_float;

  //Reasonable bounds on channel count so we neither reject real files nor try to
  //  allocate absurd amounts of memory from a corrupt/hostile file.
  const size_t sm_min_channels = 1;
  const size_t sm_max_channels = 131073;

  //Max plausible live/real time.  GADRAS simulations use very long times (e.g.
  //  1e7 seconds), so this is generous while still rejecting obvious garbage.
  const float sm_max_time = 1.0e9f;


  /** Returns true if the (trimmed) line is a record delimiter.  Accepts the
   "! Record # 1", "!\tRecord #1", and "! Record #: 1" variants, as well as a
   bare "!" line (used by some files that put the title on the following line).
   Note that empty metadata fields trim to "! <FieldName>" (e.g. "! UUID"); those
   are not treated as record markers because text other than "Record" follows. */
  bool is_record_marker( const string &line )
  {
    if( line.empty() )
      return false;
    // A '!' in the first column is a record delimiter; this covers "! Record #1",
    //  "!\tRecord #1", "! Record #: 1", a bare "!", and "! <some label>".  Empty
    //  metadata fields trim to "! <FieldName>" but keep their leading padding, so
    //  they are (correctly) not matched here.
    if( line[0] == '!' )
      return true;
    // Also accept an indented "! Record..." just in case a file pads the marker.
    string t = trim_copy( line );
    if( t.empty() || (t[0] != '!') )
      return false;
    string rest = t.substr( 1 );
    trim( rest );
    return SpecUtils::istarts_with( rest, "Record" );
  }//is_record_marker(...)


  /** Detects a "<livetime> <realtime> <nchannels>" timing line.  Returns true and
   fills the outputs only when the line is exactly three whitespace separated
   tokens: two non-negative floats and a positive integer channel count in range. */
  bool parse_timing_line( const string &line, float &livetime, float &realtime, size_t &nchan )
  {
    vector<string> tokens;
    split( tokens, line, " \t" );
    if( tokens.size() != 3 )
      return false;

    float lt = 0.0f, rt = 0.0f;
    int nc = 0;
    if( !parse_float( tokens[0].c_str(), tokens[0].size(), lt )
       || !parse_float( tokens[1].c_str(), tokens[1].size(), rt )
       || !parse_int( tokens[2].c_str(), tokens[2].size(), nc ) )
      return false;

    //Allow nc == 0: some files have "analysis" records (e.g. "GTH: Pk/Avg=...")
    //  with a valid timing line but no spectrum; the caller skips those.
    if( nc < 0 || nc > static_cast<int>(sm_max_channels) )
      return false;
    if( lt < 0.0f || rt < 0.0f || lt > sm_max_time || rt > sm_max_time )
      return false;

    //Reject when the "integer" token was actually a float (e.g. "3.000E+03"), so
    //  we don't mistake a parameter line for a timing line.
    if( tokens[2].find_first_of( ".eE" ) != string::npos )
      return false;

    livetime = lt;
    realtime = rt;
    nchan = static_cast<size_t>( nc );
    return true;
  }//parse_timing_line(...)
}//namespace


namespace SpecUtils
{
bool SpecFile::load_asc_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif

  if( !input.is_open() )
    return false;

  const bool success = load_from_asc( input );

  if( success )
    filename_ = filename;

  return success;
}//bool load_asc_file( const std::string &filename )


bool SpecFile::load_from_asc( std::istream &input )
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

    if( filesize < 8 )
      throw runtime_error( "File too small to be an ASC file" );
    if( filesize > 64*1024*1024 )
      throw runtime_error( "File too large to be an ASC file" );

    // Read all lines up front, stripping any NUL padding (used in the metadata
    //  block).  safe_get_line() already handles \r\n / \r / \n line endings.
    vector<string> lines;
    {
      string line;
      while( SpecUtils::safe_get_line( input, line, 65536 ) )
      {
        // Strip NUL padding (metadata block) and the legacy 0x1A DOS end-of-file
        //  marker that some files trail with.
        line.erase( std::remove_if( line.begin(), line.end(),
                     []( char c ){ return (c == '\0') || (c == '\x1a'); } ), line.end() );
        lines.push_back( line );
        if( lines.size() > (4u*1024u*1024u) )
          throw runtime_error( "Too many lines to be an ASC file" );
      }
    }

    if( lines.empty() )
      throw runtime_error( "Empty file" );

    // Classify the format from the first line.
    vector<string> hdr_tokens;
    split( hdr_tokens, lines[0], " \t" );
    if( hdr_tokens.empty() )
      throw runtime_error( "No header" );

    // Format A calibration header: "<nchan> ECAL ..." (some files use "E").
    const bool is_format_a = (hdr_tokens.size() >= 2)
                             && (iequals_ascii( hdr_tokens[1], "ECAL" )
                                 || iequals_ascii( hdr_tokens[1], "E" ));
    const bool is_format_c = !is_format_a && (hdr_tokens.size() == 4);

    // --- Format C: plain single-column spectrum -----------------------------
    if( !is_format_a && is_format_c )
    {
      int nchan_i = 0;
      float livetime = 0.0f, realtime = 0.0f, neutron = -1.0f;
      if( !parse_int( hdr_tokens[0].c_str(), hdr_tokens[0].size(), nchan_i )
         || !parse_float( hdr_tokens[1].c_str(), hdr_tokens[1].size(), livetime )
         || !parse_float( hdr_tokens[2].c_str(), hdr_tokens[2].size(), realtime )
         || !parse_float( hdr_tokens[3].c_str(), hdr_tokens[3].size(), neutron ) )
        throw runtime_error( "Not a Format-C ASC header" );

      if( nchan_i < static_cast<int>(sm_min_channels) || nchan_i > static_cast<int>(sm_max_channels) )
        throw runtime_error( "Invalid channel count" );
      if( livetime < 0.0f || realtime < 0.0f || livetime > sm_max_time || realtime > sm_max_time )
        throw runtime_error( "Invalid live/real time" );

      const size_t nchan = static_cast<size_t>( nchan_i );
      auto channel_counts = make_shared<vector<float>>();
      channel_counts->reserve( nchan );
      double countssum = 0.0;

      // Counts may be one-per-line or multiple whitespace-separated values per line.
      for( size_t i = 1; (i < lines.size()) && (channel_counts->size() < nchan); ++i )
      {
        const string val = trim_copy( lines[i] );
        if( val.empty() )
          continue;
        vector<float> vals;
        if( !SpecUtils::split_to_floats( val, vals ) )
          throw runtime_error( "Invalid channel value in Format-C ASC file" );
        for( const float f : vals )
        {
          channel_counts->push_back( f );
          countssum += f;
          if( channel_counts->size() >= nchan )
            break;
        }
      }//for( each data line )

      if( channel_counts->size() != nchan )
        throw runtime_error( "Format-C ASC file did not contain the declared number of channels" );

      auto meas = make_shared<Measurement>();
      meas->live_time_ = livetime;
      meas->real_time_ = realtime;
      meas->gamma_count_sum_ = countssum;
      meas->gamma_counts_ = channel_counts;
      if( neutron >= 0.0f )
      {
        meas->contained_neutron_ = true;
        meas->neutron_counts_.push_back( neutron );
        meas->neutron_counts_sum_ = neutron;
      }

      measurements_.push_back( meas );
      cleanup_after_load();
      return true;
    }//if( Format C )

    // --- Formats A & B: "! Record #" delimited records ----------------------

    // File level energy calibration coefficients (Format A only).
    vector<float> ecal_coeffs;
    size_t line_idx = 1;

    if( is_format_a )
    {
      // Header: "<nchan> ECAL <c0> <c1> <c2> <c3> <c4>"
      int hdr_nchan = 0;
      if( !parse_int( hdr_tokens[0].c_str(), hdr_tokens[0].size(), hdr_nchan )
         || hdr_nchan < static_cast<int>(sm_min_channels)
         || hdr_nchan > static_cast<int>(sm_max_channels) )
        throw runtime_error( "Invalid ECAL header channel count" );

      for( size_t i = 2; i < hdr_tokens.size(); ++i )
      {
        float c = 0.0f;
        if( !parse_float( hdr_tokens[i].c_str(), hdr_tokens[i].size(), c ) )
          throw runtime_error( "Invalid ECAL coefficient" );
        ecal_coeffs.push_back( c );
      }

      // Optional PCF-style metadata block: every line between the ECAL header and
      //  the first record marker is of the form "<value>   ! <FieldName>".  We
      //  consume the whole block and best-effort map recognized fields (field
      //  names vary slightly between files, e.g. "DNDO.Inspection" vs
      //  "DNDOInspection", so we match leniently and ignore unknown fields).
      while( line_idx < lines.size() && !is_record_marker( lines[line_idx] ) )
      {
        const string &raw = lines[line_idx];
        const size_t ann_pos = raw.find( "! " );
        if( ann_pos == string::npos )
        {
          ++line_idx;  //not a recognizable metadata line - ignore it
          continue;
        }

        const string field = trim_copy( raw.substr( ann_pos + 2 ) );
        const string value = trim_copy( raw.substr( 0, ann_pos ) );
        if( !value.empty() )
        {
          if( iequals_ascii( field, "UUID" ) )
            uuid_ = value;
          else if( iequals_ascii( field, "Manufacturer" ) )
            manufacturer_ = value;
          else if( iequals_ascii( field, "InstrumentModel" ) )
            instrument_model_ = value;
          else if( iequals_ascii( field, "InstrumentID" ) )
            instrument_id_ = value;
          else if( iequals_ascii( field, "InstrumentType" ) )
            instrument_type_ = value;
          else if( iequals_ascii( field, "MeasurementLocationName" ) )
            measurement_location_name_ = value;
          else if( iequals_ascii( field, "MeasurementRemark" ) )
            remarks_.push_back( value );
          else if( iequals_ascii( field, "LaneNumber" ) )
          {
            int lane = 0;
            if( parse_int( value.c_str(), value.size(), lane ) )
              lane_number_ = lane;
          }else if( value != "0" )
          {
            remarks_.push_back( field + ": " + value );
          }
        }//if( !value.empty() )

        ++line_idx;
      }//while( metadata block )
    }else
    {
      // Format B: the first line must be a bare channel count, and the next
      //  non-blank line must be a record marker (keeps us from grabbing
      //  arbitrary text/CSV files).
      if( hdr_tokens.size() != 1 )
        throw runtime_error( "Not an ASC header" );
      int hdr_nchan = 0;
      if( !parse_int( hdr_tokens[0].c_str(), hdr_tokens[0].size(), hdr_nchan )
         || hdr_nchan < static_cast<int>(sm_min_channels)
         || hdr_nchan > static_cast<int>(sm_max_channels) )
        throw runtime_error( "Not an ASC channel-count header" );

      size_t peek = 1;
      while( peek < lines.size() && trim_copy(lines[peek]).empty() )
        ++peek;
      if( peek >= lines.size() || !is_record_marker( lines[peek] ) )
        throw runtime_error( "Bare-count ASC file not followed by a record marker" );
    }//if( Format A ) / else ( Format B )

    // Parse each record.
    while( line_idx < lines.size() )
    {
      // Advance to the next record marker (skipping any blank lines).
      while( line_idx < lines.size() && !is_record_marker( lines[line_idx] ) )
      {
        if( !trim_copy( lines[line_idx] ).empty() )
          throw runtime_error( "Unexpected content between ASC records" );
        ++line_idx;
      }
      if( line_idx >= lines.size() )
        break;

      ++line_idx;  //consume the record marker
      if( line_idx >= lines.size() )
        throw runtime_error( "Record marker at end of file" );

      auto meas = make_shared<Measurement>();

      // Title / description line (with optional "Det: <name>").
      {
        string title = trim_copy( lines[line_idx] );
        ++line_idx;

        const string lower = to_lower_ascii_copy( title );
        const size_t det_pos = lower.find( "det:" );
        if( det_pos != string::npos )
        {
          string after = title.substr( det_pos + 4 );
          vector<string> det_tokens;
          split( det_tokens, after, " \t" );
          if( !det_tokens.empty() )
            meas->detector_name_ = det_tokens[0];
          title = trim_copy( title.substr( 0, det_pos ) );
        }//if( has "Det:" )

        meas->title_ = title;

        if( icontains( title, "Background" ) )
          meas->source_type_ = SourceType::Background;
        else if( icontains( title, "Calib" ) )
          meas->source_type_ = SourceType::Calibration;
        else if( icontains( title, "Foreground" ) )
          meas->source_type_ = SourceType::Foreground;
        //else leave SourceType::Unknown
      }//title block

      // Read the preamble (date and metadata lines) until we reach the timing
      //  line.  In these formats the date/time is the line immediately before the
      //  timing line, so we buffer the (non-blank) preamble lines and only try to
      //  interpret the last one as a date - this avoids spurious date-parse
      //  attempts (and their log noise) on metadata lines.
      float livetime = 0.0f, realtime = 0.0f;
      size_t nchan = 0;
      bool found_timing = false;
      vector<string> preamble;
      const size_t max_preamble = 64;
      size_t scanned = 0;

      while( line_idx < lines.size() && (scanned++ < max_preamble) )
      {
        const string &line = lines[line_idx];

        if( is_record_marker( line ) )
          throw runtime_error( "Record missing its timing line" );

        if( parse_timing_line( line, livetime, realtime, nchan ) )
        {
          found_timing = true;
          ++line_idx;
          break;
        }

        const string trimmed = trim_copy( line );
        if( !trimmed.empty() )
          preamble.push_back( trimmed );
        ++line_idx;
      }//while( scanning preamble )

      if( !found_timing )
        throw runtime_error( "Could not find record timing line" );

      // The last preamble line is the date/time (if present and parseable).
      if( !preamble.empty() )
      {
        const time_point_t t = time_from_string( preamble.back() );
        if( !is_special(t) )
        {
          meas->start_time_ = t;
          preamble.pop_back();
        }
      }//if( !preamble.empty() )

      // Any remaining preamble lines become remarks; comma separated fields
      //  (e.g. "U1B_ID: 010, FilterID: 01, Temp(deg C): 20") become separate remarks.
      for( const string &pre : preamble )
      {
        if( pre.find(',') != string::npos )
        {
          vector<string> parts;
          split( parts, pre, "," );
          for( string &p : parts )
          {
            trim( p );
            if( !p.empty() )
              meas->remarks_.push_back( p );
          }
        }else
        {
          meas->remarks_.push_back( pre );
        }
      }//for( remaining preamble lines )

      // After the timing line comes a fixed-length parameter/calibration array
      //  (nominally 10 values, but this varies slightly between files) followed by
      //  the <nchan> gamma counts, all whitespace separated and line-wrapped.
      //  Since the spectrum is always the trailing <nchan> values before the next
      //  record marker, we collect every numeric value and take the last <nchan>
      //  as the spectrum - this is robust to the parameter array's exact length.
      vector<float> all_vals;
      all_vals.reserve( nchan + 16 );
      while( line_idx < lines.size() )
      {
        if( is_record_marker( lines[line_idx] ) )
          break;

        const string trimmed = trim_copy( lines[line_idx] );
        ++line_idx;
        if( trimmed.empty() )
          continue;

        vector<float> vals;
        if( !SpecUtils::split_to_floats( trimmed, vals ) )
          throw runtime_error( "Invalid channel data in ASC record" );
        all_vals.insert( all_vals.end(), vals.begin(), vals.end() );

        if( all_vals.size() > (nchan + 4096) )  //guard against runaway records
          throw runtime_error( "ASC record contained too many values" );
      }//while( reading parameter + channel data )

      if( all_vals.size() < nchan )
        throw runtime_error( "ASC record did not contain the declared number of channels" );

      // Skip records that are too small to be a real gamma spectrum: zero-channel
      //  analysis entries (e.g. "GTH: Pk/Avg=..."), and records with too few
      //  channels for SpecUtils to assign a sane default calibration (a linear
      //  0-3000 keV default needs >= 7 channels).  These are analysis fragments,
      //  not spectra.
      if( nchan < 7 )
        continue;

      auto channel_counts = make_shared<vector<float>>( all_vals.end() - static_cast<ptrdiff_t>(nchan), all_vals.end() );
      double countssum = 0.0;
      for( const float v : *channel_counts )
        countssum += v;

      meas->live_time_ = livetime;
      meas->real_time_ = realtime;
      meas->gamma_count_sum_ = countssum;
      meas->gamma_counts_ = channel_counts;

      // Apply the file-level ECAL energy calibration (Format A) if it is meaningful.
      if( !ecal_coeffs.empty() )
      {
        // Trim trailing zero coefficients; a calibration is only useful if it has
        //  a non-zero gain term.
        vector<float> coeffs = ecal_coeffs;
        while( coeffs.size() > 1 && coeffs.back() == 0.0f )
          coeffs.pop_back();

        bool any_nonzero_gain = false;
        for( size_t i = 1; i < coeffs.size(); ++i )
          any_nonzero_gain = any_nonzero_gain || (coeffs[i] != 0.0f);

        if( any_nonzero_gain )
        {
          try
          {
            auto newcal = make_shared<EnergyCalibration>();
            newcal->set_full_range_fraction( nchan, coeffs, {} );
            meas->energy_calibration_ = newcal;
          }catch( std::exception & )
          {
            meas->parse_warnings_.push_back( "Failed to interpret ECAL energy calibration." );
          }
        }//if( any_nonzero_gain )
      }//if( have ECAL coefficients )

      measurements_.push_back( meas );
    }//while( parsing records )

    if( measurements_.empty() )
      throw runtime_error( "No spectra found in ASC file" );

    cleanup_after_load();

    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }//try / catch

  return false;
}//bool load_from_asc( std::istream &input )

}//namespace SpecUtils
