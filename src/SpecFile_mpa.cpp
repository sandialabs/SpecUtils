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
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

/*
 The MPA format is the ASCII spectrum file written by the MPANT program made by
 FAST ComTec.  It is an INI-style text file: a series of "[Section]" headers,
 each followed by "key=value" lines, and finally one or more channel-count data
 blocks introduced by a "[DATA<n>,<numchannels> ]" header.

 An example (heavily abridged) looks like:

   [MCA4A] 1504 FW 4.12    <- magic first line; text after ']' varies by hardware/firmware
   ...
   enb_ch=3                <- bitmask of which ADCs were enabled
   ...
   [ADC1]                  <- one section of metadata per ADC (detector)
   active=1
   realtime=50614.618
   livetime=50599.266
   TOTALSUM=327061
   range=8192
   caloff=0.174077         <- polynomial energy calibration (keV) coefficients
   calfact=0.405861
   calfact2=0
   calfact3=0
   calunit=keV
   caluse=1                <- whether the calibration should actually be applied
   cmline0=05/14/2026 09:16:39.821 - 23:20:14.434   <- start/stop time
   [ADC2]
   ...
   [DATA0,8192 ]           <- channel counts for ADC1 (DATA<n> pairs with ADC<n+1>)
   0
   1
   ...
   [DATA1,8192 ]           <- channel counts for ADC2
   ...

 Only the "[ADCn]" sections are gamma spectra (calunit=keV); the "[MCSn]" and
 "[CHNn]" sections are multichannel-scaler/timing data (calunit=usec) and are
 ignored.  MPANT supports up to four simultaneous ADCs (detectors); each active
 one produces a "[DATA<n>]" block that we turn into its own Measurement.  Even
 though the ADCs run simultaneously, they collect independent spectra (not
 coincidence data, and not meant to be summed), so each is given its own sample
 number to keep it as a separate, individually-viewable record.
 */

namespace
{
  //Defensive upper bounds; MPANT hardware tops out at 4 ADCs, and channel counts
  //  are limited by what EnergyCalibration will accept.  These guard against
  //  hostile or corrupt files claiming absurd sizes.
  const int    sm_mpa_max_sections    = 64;
  const size_t sm_mpa_max_tot_channels = 4 * 131072;
  const size_t sm_mpa_max_line_len     = 64 * 1024;
  const size_t sm_mpa_max_file_size    = 64 * 1024 * 1024;
  const size_t sm_mpa_min_file_size    = 32;

  /** Metadata parsed from a single "[ADCn]" section. */
  struct MpaAdcInfo
  {
    bool   active   = false;
    bool   caluse   = false;
    int    range    = 0;      //declared number of channels (cross-check only)
    float  realtime = 0.0f;
    float  livetime = 0.0f;
    float  runtime  = 0.0f;
    double totalsum = -1.0;   //<0 means the field was not present
    float  caloff   = 0.0f;
    float  calfact  = 0.0f;
    float  calfact2 = 0.0f;
    float  calfact3 = 0.0f;
    std::string calunit;      //expected to be "keV" for gamma data
    std::string cmline0;      //"MM/DD/YYYY HH:MM:SS.fff - MM/DD/YYYY HH:MM:SS.fff"
    std::string cmline1;      //a short user label
    SpecUtils::time_point_t start_time{};
  };//struct MpaAdcInfo

  /** A single "[DATA<index>,<N> ]" channel-count block. */
  struct MpaDataBlock
  {
    int    index = -1;             //0-based DATA index
    size_t declared_channels = 0;  //value from the "[DATAn,N ]" header
    std::shared_ptr<std::vector<float>> counts;
    double sum = 0.0;
  };//struct MpaDataBlock

  enum class MpaSection{ Global, Adc, Ignore, None };
}//anonymous namespace


namespace SpecUtils
{
bool SpecFile::load_mpa_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif

  if( !input.is_open() )
    return false;

  const bool success = load_from_mpa( input );

  if( success )
    filename_ = filename;

  return success;
}//bool load_mpa_file( const std::string &filename )


bool SpecFile::load_from_mpa( std::istream &input )
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

    if( filesize > sm_mpa_max_file_size )
      throw runtime_error( "File too large to be MPA" );

    if( filesize < sm_mpa_min_file_size )
      throw runtime_error( "File too small to be MPA" );

    string line;

    // The first line must be the "[MCAxx] ..." magic/section header.
    if( !SpecUtils::safe_get_line( input, line, sm_mpa_max_line_len ) )
      throw runtime_error( "Failed to read MPA header" );

    // The file may have a leading UTF-8 byte-order-marker; skip it if present.
    if( (line.size() >= 3) && (static_cast<unsigned char>(line[0]) == 0xEF)
        && (static_cast<unsigned char>(line[1]) == 0xBB)
        && (static_cast<unsigned char>(line[2]) == 0xBF) )
      line.erase( 0, 3 );
    SpecUtils::trim( line );

    // The tag is "[MCA" + a short (<32 char) hardware id + "]"; the text after the
    //  closing bracket (e.g. "1504 FW 4.12") varies with hardware/firmware, so is
    //  only kept as a remark.  Requiring this exact shape keeps us from
    //  accidentally claiming some other bracketed-text format.
    const string::size_type rbrace = line.find( ']' );
    if( (line.size() < 5) || (line[0] != '[') || (rbrace == string::npos)
        || (rbrace > 40) )
      throw runtime_error( "MPA file must start with a '[MCAxx]' tag" );

    const string model = line.substr( 1, rbrace - 1 );
    if( (model.size() >= 32) || !SpecUtils::istarts_with( model, "MCA" ) )
      throw runtime_error( "MPA file must start with a '[MCAxx]' tag" );

    const string hardware_firmware = SpecUtils::trim_copy( line.substr( rbrace + 1 ) );

    // Walk the remaining sections.
    map<int,MpaAdcInfo> adcs;      //keyed by 1-based ADC number
    vector<MpaDataBlock> datablocks;
    bool found_adc = false;
    int enb_ch = 0;
    size_t total_channels = 0;

    MpaSection cursection = MpaSection::Global;
    int cur_adc = -1;

    while( SpecUtils::safe_get_line( input, line, sm_mpa_max_line_len ) )
    {
      SpecUtils::trim( line );
      if( line.empty() )
        continue;

      if( line[0] == '[' )
      {
        const string::size_type rb = line.find( ']' );
        if( rb == string::npos )
          throw runtime_error( "Malformed section header in MPA file" );

        const string token = line.substr( 1, rb - 1 );

        if( SpecUtils::istarts_with( token, "DATA" ) )
        {
          // Header is like "DATA0,8192" (with a possible trailing space): the
          //  number after the comma is how many channel-count lines follow.
          const string::size_type comma = token.find( ',' );
          if( comma == string::npos )
            throw runtime_error( "Malformed DATA header in MPA file" );

          int idx = -1, nchannel = -1;
          const string idxstr = SpecUtils::trim_copy( token.substr( 4, comma - 4 ) );
          const string nstr = SpecUtils::trim_copy( token.substr( comma + 1 ) );
          if( !SpecUtils::parse_int( idxstr.c_str(), idxstr.size(), idx )
              || !SpecUtils::parse_int( nstr.c_str(), nstr.size(), nchannel ) )
            throw runtime_error( "Malformed DATA header in MPA file" );

          if( (idx < 0) || (idx >= sm_mpa_max_sections)
              || (nchannel < 1)
              || (static_cast<size_t>(nchannel) > EnergyCalibration::sm_max_channels) )
            throw runtime_error( "Invalid DATA block size in MPA file" );

          if( datablocks.size() >= static_cast<size_t>(sm_mpa_max_sections) )
            throw runtime_error( "Too many DATA blocks in MPA file" );

          if( (total_channels + static_cast<size_t>(nchannel)) > sm_mpa_max_tot_channels )
            throw runtime_error( "Too many total channels in MPA file" );

          MpaDataBlock db;
          db.index = idx;
          db.declared_channels = static_cast<size_t>( nchannel );
          db.counts = make_shared<vector<float>>();
          db.counts->reserve( static_cast<size_t>(nchannel) );

          for( int i = 0; i < nchannel; ++i )
          {
            string dline;
            if( !SpecUtils::safe_get_line( input, dline, sm_mpa_max_line_len ) )
              throw runtime_error( "MPA DATA block ended prematurely" );
            SpecUtils::trim( dline );

            // A blank line or a new section header before we have read all the
            //  declared channels means the file is truncated/corrupt.
            if( dline.empty() || (dline[0] == '[') )
              throw runtime_error( "MPA DATA block ended prematurely" );

            float val = 0.0f;
            if( !SpecUtils::parse_float( dline.c_str(), dline.size(), val )
                || IsNan(val) || IsInf(val) || (val < 0.0f) )
              throw runtime_error( "Invalid channel count in MPA DATA block" );

            db.counts->push_back( val );
            db.sum += val;
          }//for( each declared channel )

          total_channels += static_cast<size_t>( nchannel );
          datablocks.push_back( std::move(db) );
          cursection = MpaSection::None;
          cur_adc = -1;
          continue;
        }//if( DATA section )

        if( SpecUtils::istarts_with( token, "MCA" ) )
        {
          cursection = MpaSection::Global;
          cur_adc = -1;
        }else if( SpecUtils::istarts_with( token, "ADC" ) )
        {
          const string numstr = SpecUtils::trim_copy( token.substr( 3 ) );
          int adcnum = -1;
          if( SpecUtils::parse_int( numstr.c_str(), numstr.size(), adcnum )
              && (adcnum >= 1) && (adcnum <= sm_mpa_max_sections) )
          {
            found_adc = true;
            adcs[adcnum];  //create the entry if not already present
            cursection = MpaSection::Adc;
            cur_adc = adcnum;
          }else
          {
            cursection = MpaSection::Ignore;
            cur_adc = -1;
          }
        }else
        {
          // [MCSn], [CHNn], [REPORT-FILE...], etc. - not gamma data.
          cursection = MpaSection::Ignore;
          cur_adc = -1;
        }

        continue;
      }//if( line is a section header )

      // A "key=value" line.  Split on the first '=' only: timestamp values
      //  contain ':' and the colon-delimited lines (e.g. "RUNTIME: ...") only
      //  appear inside sections we ignore.
      const string::size_type eq = line.find( '=' );
      if( eq == string::npos )
        continue;

      const string key = SpecUtils::to_lower_ascii_copy( SpecUtils::trim_copy( line.substr( 0, eq ) ) );
      const string value = SpecUtils::trim_copy( line.substr( eq + 1 ) );

      if( cursection == MpaSection::Global )
      {
        if( key == "enb_ch" )
          SpecUtils::parse_int( value.c_str(), value.size(), enb_ch );
      }else if( (cursection == MpaSection::Adc) && (cur_adc >= 1) )
      {
        MpaAdcInfo &adc = adcs[cur_adc];

        if( key == "active" )
        {
          int v = 0;
          adc.active = (SpecUtils::parse_int( value.c_str(), value.size(), v ) && (v != 0));
        }else if( key == "caluse" )
        {
          int v = 0;
          adc.caluse = (SpecUtils::parse_int( value.c_str(), value.size(), v ) && (v != 0));
        }else if( key == "range" )
        {
          SpecUtils::parse_int( value.c_str(), value.size(), adc.range );
        }else if( key == "realtime" )
        {
          SpecUtils::parse_float( value.c_str(), value.size(), adc.realtime );
        }else if( key == "livetime" )
        {
          SpecUtils::parse_float( value.c_str(), value.size(), adc.livetime );
        }else if( key == "runtime" )
        {
          SpecUtils::parse_float( value.c_str(), value.size(), adc.runtime );
        }else if( key == "totalsum" )
        {
          // Keep the "field absent" sentinel (<0) if the value fails to parse,
          //  so a malformed TOTALSUM doesnt trigger a spurious mismatch warning.
          double ts = -1.0;
          if( SpecUtils::parse_double( value.c_str(), value.size(), ts ) )
            adc.totalsum = ts;
        }else if( key == "caloff" )
        {
          SpecUtils::parse_float( value.c_str(), value.size(), adc.caloff );
        }else if( key == "calfact" )
        {
          SpecUtils::parse_float( value.c_str(), value.size(), adc.calfact );
        }else if( key == "calfact2" )
        {
          SpecUtils::parse_float( value.c_str(), value.size(), adc.calfact2 );
        }else if( key == "calfact3" )
        {
          SpecUtils::parse_float( value.c_str(), value.size(), adc.calfact3 );
        }else if( key == "calunit" )
        {
          adc.calunit = value;
        }else if( key == "cmline0" )
        {
          adc.cmline0 = value;
        }else if( key == "cmline1" )
        {
          adc.cmline1 = value;
        }
      }//if( global section ) / else if( ADC section )
    }//while( more lines )

    // If we never saw an ADC section or a data block, this is not an MPA file
    //  (or at least not one we can extract a spectrum from).
    if( !found_adc || datablocks.empty() )
      throw runtime_error( "No [ADCn] section or [DATAn] block found in MPA file" );

    // Parse the start time out of each ADC's cmline0 (the part before " - ").
    for( map<int,MpaAdcInfo>::iterator iter = adcs.begin(); iter != adcs.end(); ++iter )
    {
      MpaAdcInfo &adc = iter->second;
      if( adc.cmline0.empty() )
        continue;

      string startstr = adc.cmline0;
      const string::size_type dash = startstr.find( " - " );
      if( dash != string::npos )
        startstr = startstr.substr( 0, dash );
      SpecUtils::trim( startstr );

      const time_point_t t = SpecUtils::time_from_string( startstr, DateParseEndianType::MiddleEndianFirst );
      if( !SpecUtils::is_special(t) )
        adc.start_time = t;
    }//for( each ADC )

    // Build one Measurement per data block, pairing DATA<n> with ADC<n+1>.  Sort
    //  by index so detector ordering is deterministic.
    std::sort( begin(datablocks), end(datablocks),
              []( const MpaDataBlock &lhs, const MpaDataBlock &rhs ) -> bool {
                return lhs.index < rhs.index;
              } );

    // Each ADC is an independent detector collecting its own spectrum
    //  simultaneously (not coincidence data, and not to be summed together), so
    //  give each data block its own sample number to keep them as separate
    //  records.  A running counter (rather than the DATA index) guarantees the
    //  sample numbers stay distinct even for odd/duplicate DATA indices.
    int sample_num = 1;
    for( const MpaDataBlock &db : datablocks )
    {
      const int adc_num = db.index + 1;

      auto meas = make_shared<Measurement>();
      meas->gamma_counts_ = db.counts;
      meas->gamma_count_sum_ = db.sum;
      meas->sample_number_ = sample_num++;
      meas->detector_name_ = "Adc" + std::to_string( adc_num );
      meas->detector_number_ = db.index;

      const map<int,MpaAdcInfo>::const_iterator adcpos = adcs.find( adc_num );
      if( adcpos == adcs.end() )
      {
        meas->parse_warnings_.push_back( "No [ADC" + std::to_string(adc_num)
                                        + "] section found for data block "
                                        + std::to_string(db.index) );
      }else
      {
        const MpaAdcInfo &adc = adcpos->second;

        meas->real_time_ = adc.realtime;
        meas->live_time_ = adc.livetime;
        meas->start_time_ = adc.start_time;
        if( !adc.cmline1.empty() )
          meas->title_ = adc.cmline1;

        if( (adc.range > 0) && (static_cast<size_t>(adc.range) != db.counts->size()) )
          meas->parse_warnings_.push_back( "Declared channel count (" + std::to_string(adc.range)
                          + ") does not match number of data values ("
                          + std::to_string(db.counts->size()) + ")" );

        if( (adc.totalsum >= 0.0) && (std::llround(adc.totalsum) != std::llround(db.sum)) )
          meas->parse_warnings_.push_back( "TOTALSUM (" + std::to_string(std::llround(adc.totalsum))
                          + ") does not match summed channel counts ("
                          + std::to_string(std::llround(db.sum)) + ")" );

        if( !adc.active )
          meas->parse_warnings_.push_back( "[ADC" + std::to_string(adc_num) + "] was marked inactive" );

        // Only apply the energy calibration when the file says to use it
        //  (caluse=1); a stale/unused polynomial can be badly wrong.
        if( adc.caluse )
        {
          if( !adc.calunit.empty() && !SpecUtils::iequals_ascii(adc.calunit, "keV") )
          {
            meas->parse_warnings_.push_back( "Energy calibration unit was '" + adc.calunit
                                            + "', not keV; calibration not applied" );
          }else
          {
            vector<float> coeffs{ adc.caloff, adc.calfact, adc.calfact2, adc.calfact3 };
            while( !coeffs.empty() && (coeffs.back() == 0.0f) )
              coeffs.resize( coeffs.size() - 1 );

            if( !coeffs.empty() )
            {
              try
              {
                auto cal = make_shared<EnergyCalibration>();
                cal->set_polynomial( db.counts->size(), coeffs, {} );
                meas->energy_calibration_ = cal;
              }catch( std::exception &e )
              {
                meas->parse_warnings_.push_back( "Energy calibration provided was invalid: "
                                                + string(e.what()) );
              }//try / catch
            }//if( !coeffs.empty() )
          }//if( unexpected unit ) / else
        }else if( (adc.caloff != 0.0f) || (adc.calfact != 0.0f)
                 || (adc.calfact2 != 0.0f) || (adc.calfact3 != 0.0f) )
        {
          // caluse=0: the file carries calibration coefficients but explicitly
          //  says not to use them (they may be stale), so leave the spectrum in
          //  channel space and note why.
          meas->parse_warnings_.push_back( "Energy calibration present in file was not applied"
                          " because caluse=0; spectrum left in channel space." );
        }//if( adc.caluse ) / else if( has coefficients )
      }//if( no ADC section ) / else

      measurements_.push_back( meas );
    }//for( each data block )

    // The enb_ch bitmask should have one set bit per data block; note if not.
    if( enb_ch > 0 )
    {
      int nbits = 0;
      for( int v = enb_ch; v; v >>= 1 )
        nbits += (v & 0x1);
      if( static_cast<size_t>(nbits) != datablocks.size() )
        remarks_.push_back( "MPANT enb_ch bitmask is inconsistent with the number of data blocks" );
    }//if( enb_ch > 0 )

    manufacturer_ = "FAST ComTec";
    instrument_model_ = model;
    if( !hardware_firmware.empty() )
      remarks_.push_back( "MPANT hardware/firmware: " + hardware_firmware );

    if( measurements_.empty() )
      throw runtime_error( "No spectra parsed from MPA file" );

    cleanup_after_load();

    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }//try / catch

  return false;
}//bool load_from_mpa( std::istream &input )

}//namespace SpecUtils
