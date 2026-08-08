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

/** Fuzzes `CAMInputOutput::CAMIO` - the CNF/CAM reader and writer.

 The input is treated as a raw CNF file, so the corpus for this target is just a directory of
 `.cnf` files (the same ones you would give `file_parse_fuzz`).

 Each of `CAMIO`s getters lazily parses its own block(s) of the file, and any of them can throw,
 so every getter is called in its own try/catch; otherwise the first block that fails to parse
 would hide every block parser after it.  This is the main thing `SpecFile::load_from_cnf()`
 (which `file_parse_fuzz` already covers) cannot do for us - it bails on the first exception, and
 it never touches the efficiency, peak, or shape-calibration parsers at all.

 Whatever we manage to parse is then fed back into the *writing* side of `CAMIO` and the
 generated file is re-parsed.  That gets the record/block generators (the code behind
 `SpecFile::write_cnf()`) exercised with hostile values - NaN energies, 2^32 channel counts,
 non-ASCII nuclide names, and so on.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "SpecUtils/CAMIO.h"
#include "SpecUtils/DateTime.h"

using namespace std;

namespace
{
/** Keeps the optimizer from throwing away the parsing work we just did. */
volatile size_t g_sink = 0;

template<class T>
void sink( const T &val )
{
  // Compare, rather than cast, so that NaN/huge floats dont trip UBSans float-cast-overflow
  g_sink += static_cast<size_t>( val != T{} );
}

void sink( const string &val )
{
  g_sink += val.size();
}


template<class Fcn>
void guarded( Fcn &&fcn )
{
  try
  {
    fcn();
  }catch( ... )
  {
  }
}


/** The subset of a parsed file we hand back to the writer. */
struct ParsedFile
{
  vector<CAMInputOutput::Line> lines;
  vector<CAMInputOutput::Nuclide> nuclides;
  vector<uint32_t> spectrum;
  vector<float> energy_cal;
  CAMInputOutput::DetInfo det_info;
  SpecUtils::time_point_t start_time{};
  float live_time = 0.0f, real_time = 0.0f;
  string title;
  double latitude = 0.0, longitude = 0.0, speed = 0.0;
  SpecUtils::time_point_t position_time{};
  bool have_gps = false;
};


/** Calls every read API of `CAMIO`, each independently of the others.

 @param reader A reader that `ReadFile(...)` has already succeeded on.
 @param parsed If non-null, filled out with the values needed to drive the writer.
 */
void exercise_getters( CAMInputOutput::CAMIO &reader, ParsedFile * const parsed )
{
  guarded( [&](){
    const vector<CAMInputOutput::EfficiencyPoint> &pts = reader.GetEfficiencyPoints();
    g_sink += pts.size();
    for( const CAMInputOutput::EfficiencyPoint &p : pts )
    {
      sink( p.Index );
      sink( p.Energy );
      sink( p.Efficiency );
      sink( p.EfficiencyUncertainty );
    }

    // Only valid after GetEfficiencyPoints() has been called
    sink( static_cast<int>( reader.GetEfficiencyModel() ) );

    // Calling again shouldnt double-up, or partially retain, the points
    g_sink += reader.GetEfficiencyPoints().size();
  } );

  guarded( [&](){
    const vector<CAMInputOutput::Peak> &peaks = reader.GetPeaks();
    g_sink += peaks.size();
    for( const CAMInputOutput::Peak &p : peaks )
    {
      sink( p.Energy );
      sink( p.Centroid );
      sink( p.CentroidUncertainty );
      sink( p.FullWidthAtHalfMaximum );
      sink( p.LowTail );
      sink( p.Area );
      sink( p.AreaUncertainty );
      sink( p.Continuum );
      sink( p.CriticalLevel );
      sink( p.CountRate );
      sink( p.CountRateUncertainty );
      sink( p.LeftChannel );
      sink( p.RightChannel );
    }
  } );

  // Note GetNuclides() will call GetLines() itself if no lines have been read yet, so calling
  //  GetLines() first exercises the "lines already read" path of GetNuclides()
  guarded( [&](){
    const vector<CAMInputOutput::Line> &lines = reader.GetLines();
    g_sink += lines.size();
    for( const CAMInputOutput::Line &l : lines )
    {
      sink( l.Energy );
      sink( l.Abundance );
      sink( l.NuclideIndex );
      sink( l.LineActivity );
      sink( l.LineEfficiency );
      sink( l.LineMDA );
    }

    if( parsed )
      parsed->lines = lines;
  } );

  guarded( [&](){
    const vector<CAMInputOutput::Nuclide> &nucs = reader.GetNuclides();
    g_sink += nucs.size();
    for( const CAMInputOutput::Nuclide &n : nucs )
    {
      sink( n.Name );
      sink( n.HalfLife );
      sink( n.HalfLifeUnit );
      sink( n.ElementSymbol );
      sink( n.Metastable );
      sink( n.Activity );
      sink( n.MDA );
    }

    if( parsed )
      parsed->nuclides = nucs;
  } );

  guarded( [&](){
    const vector<uint32_t> &spec = reader.GetSpectrum();
    g_sink += spec.size();
    for( const uint32_t c : spec )
      sink( c );

    if( parsed )
      parsed->spectrum = spec;
  } );

  guarded( [&](){
    const vector<float> &cal = reader.GetEnergyCalibration();
    for( const float c : cal )
      sink( c );

    if( parsed )
      parsed->energy_cal = cal;
  } );

  guarded( [&](){
    const vector<float> &shape = reader.GetShapeCalibration();
    for( const float c : shape )
      sink( c );
  } );

  guarded( [&](){ sink( reader.GetSampleTime() ); } );

  guarded( [&](){
    const SpecUtils::time_point_t t = reader.GetAquisitionTime();
    sink( t );
    if( parsed )
      parsed->start_time = t;
  } );

  guarded( [&](){
    const float lt = reader.GetLiveTime();
    sink( lt );
    if( parsed )
      parsed->live_time = lt;
  } );

  guarded( [&](){
    const float rt = reader.GetRealTime();
    sink( rt );
    if( parsed )
      parsed->real_time = rt;
  } );

  guarded( [&](){
    const string title = reader.GetSampleTitle();
    sink( title );
    if( parsed )
      parsed->title = title;
  } );

  guarded( [&](){
    const CAMInputOutput::DetInfo &info = reader.GetDetectorInfo();
    sink( info.Type );
    sink( info.Name );
    sink( info.SerialNo );
    sink( info.MCAType );

    if( parsed )
      parsed->det_info = info;
  } );

  guarded( [&](){ sink( reader.GetNumChannelsFromAcqp() ); } );

  guarded( [&](){
    string sample_id, sample_type, sample_units, sample_geometry, user_name, sample_desc;
    if( reader.GetSampleStrings( sample_id, sample_type, sample_units, sample_geometry,
                                 user_name, sample_desc ) )
    {
      sink( sample_id );
      sink( sample_type );
      sink( sample_units );
      sink( sample_geometry );
      sink( user_name );
      sink( sample_desc );
    }
  } );

  guarded( [&](){
    const CAMInputOutput::KEdgeInfo kedge = reader.GetKEdgeInfo();
    if( kedge.hasInfo )
    {
      sink( kedge.temperature );
      sink( kedge.pathLength );
      sink( kedge.u235Enrichment );
      sink( kedge.puAtomicWeight );
    }
  } );

  guarded( [&](){
    double latitude, longitude, speed;
    SpecUtils::time_point_t position_time;
    if( reader.GetGPSData( latitude, longitude, speed, position_time ) )
    {
      sink( latitude );
      sink( longitude );
      sink( speed );
      sink( position_time );

      if( parsed )
      {
        parsed->latitude = latitude;
        parsed->longitude = longitude;
        parsed->speed = speed;
        parsed->position_time = position_time;
        parsed->have_gps = true;
      }
    }
  } );
}//void exercise_getters(...)


/** Feeds parsed values back into the writing side of `CAMIO`, and returns the generated file.

 Returns an empty vector if a file couldnt be generated.
 */
vector<byte_type> write_file( const ParsedFile &parsed )
{
  // The amount of memory `CreateFile()` needs scales with these, and the fuzzer has no reason to
  //  care about large-but-similar inputs here, so keep them modest to stay fast and inside the
  //  fuzzers RSS limit.
  const size_t max_channels = 16*1024;
  const size_t max_lines = 256;
  const size_t max_nuclides = 64;

  vector<byte_type> written;

  try
  {
    CAMInputOutput::CAMIO writer;

    if( !parsed.spectrum.empty() )
    {
      const size_t nchannel = min( parsed.spectrum.size(), max_channels );
      const vector<uint32_t> counts( begin(parsed.spectrum), begin(parsed.spectrum) + nchannel );
      writer.AddSpectrum( counts );
    }

    guarded( [&](){ writer.AddEnergyCalibration( parsed.energy_cal ); } );
    guarded( [&](){ writer.AddDetectorType( parsed.det_info.Type ); } );
    guarded( [&](){ writer.AddAcquitionTime( parsed.start_time ); } );
    guarded( [&](){ writer.AddRealTime( parsed.real_time ); } );
    guarded( [&](){ writer.AddLiveTime( parsed.live_time ); } );
    guarded( [&](){ writer.AddSampleTitle( parsed.title ); } );

    if( parsed.have_gps )
      guarded( [&](){ writer.AddGPSData( parsed.latitude, parsed.longitude,
                                         static_cast<float>(parsed.speed), parsed.position_time ); } );

    // Lines have to go in before the nuclides, so `AddNuclide(...)` can find each nuclides lines
    const size_t nline = min( parsed.lines.size(), max_lines );
    for( size_t i = 0; i < nline; ++i )
      guarded( [&](){ writer.AddLine( parsed.lines[i] ); } );

    const size_t nnuc = min( parsed.nuclides.size(), max_nuclides );
    for( size_t i = 0; i < nnuc; ++i )
    {
      CAMInputOutput::Nuclide nuc = parsed.nuclides[i];

      // `AddNuclide(...)` throws for any unit it doesnt recognize, which would mean we almost
      //  never reach the nuclide record generator; map anything unexpected to seconds.
      const string unit = nuc.HalfLifeUnit;
      if( (unit != "y") && (unit != "d") && (unit != "h") && (unit != "m") && (unit != "s")
          && (unit != "Y") && (unit != "D") && (unit != "H") && (unit != "M") && (unit != "S") )
        nuc.HalfLifeUnit = "s";

      guarded( [&](){ writer.AddNuclide( nuc ); } );
    }

    written = writer.CreateFile();
  }catch( ... )
  {
  }

  return written;
}//vector<byte_type> write_file( const ParsedFile & )

}//namespace


extern "C" int LLVMFuzzerTestOneInput( const uint8_t *data, size_t size )
{
  // `SpecFile::load_from_cnf()` refuses anything larger than this, so dont spend time on inputs
  //  the library would never hand to CAMIO anyway.
  if( size > 8*1024*1024 )
    return -1;

  const vector<byte_type> input( data, data + size );

  CAMInputOutput::CAMIO reader;

  try
  {
    reader.ReadFile( input );
  }catch( ... )
  {
    // No readable file header, so there is nothing for any of the getters to do
    return 0;
  }

  ParsedFile parsed;
  exercise_getters( reader, &parsed );

  const vector<byte_type> written = write_file( parsed );

  // Round-trip: re-parse what we just generated.  Anything the writer can produce should be
  //  something the reader can survive.
  if( !written.empty() )
  {
    try
    {
      CAMInputOutput::CAMIO rereader;
      rereader.ReadFile( written );
      exercise_getters( rereader, nullptr );
    }catch( ... )
    {
    }
  }//if( !written.empty() )

  return 0;
}//int LLVMFuzzerTestOneInput( const uint8_t *, size_t )
