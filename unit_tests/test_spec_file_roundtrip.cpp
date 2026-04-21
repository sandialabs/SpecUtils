/* SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.

 Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC
 (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
 Government retains certain rights in this software.
 For questions contact William Johnson via email at wcjohns@sandia.gov, or
 alternative emails of interspec@sandia.gov.

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

#include <set>
#include <string>
#include <vector>
#include <sstream>
#include <cmath>
#include <memory>
#include <functional>
#include <iostream>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/Filesystem.h"

using namespace std;
using namespace SpecUtils;

// Command-line arguments, captured in main() so tests can access --indir=
static vector<string> g_cl_args;

int main( int argc, char **argv )
{
  for( int i = 0; i < argc; ++i )
    g_cl_args.push_back( argv[i] );

  return doctest::Context( argc, argv ).run();
}


static string get_indir()
{
  string indir;
  for( size_t i = 1; i < g_cl_args.size(); ++i )
  {
    if( SpecUtils::istarts_with( g_cl_args[i], "--indir=" ) )
      indir = g_cl_args[i].substr( 8 );
  }

  SpecUtils::ireplace_all( indir, "%20", " " );
  while( indir.size() && (indir[0] == '"' || indir[0] == '\\') )
    indir = indir.substr( 1 );
  while( indir.size() && (indir.back() == '"' || indir.back() == '\\') )
    indir = indir.substr( 0, indir.size() - 1 );

  return indir;
}


struct FormatInfo
{
  SaveSpectrumAsType format;
  const char *name;
  bool is_single_spectrum;     // true = format sums all records into one spectrum on write
  bool preserves_live_time;
  bool preserves_real_time;
  bool preserves_neutron_counts;
  bool preserves_gps;
  bool preserves_num_channels;  // false for Exploranium (may truncate channels)
  bool preserves_exact_counts;  // false for formats that may round/truncate counts
  float count_tolerance;        // relative tolerance for gamma count sum comparison
  function<bool( SpecFile &, istream & )> loader;
};


static vector<FormatInfo> get_formats()
{
  vector<FormatInfo> formats;

  // Multi-record formats
  formats.push_back( {
    SaveSpectrumAsType::N42_2012, "N42_2012",
    false, true, true, true, true, true, true, 0.001f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_N42( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::N42_2006, "N42_2006",
    false, true, true, true, true, true, true, 0.001f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_N42( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::Pcf, "Pcf",
    false, true, true, true, false, true, true, 0.01f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_pcf( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::Txt, "Txt",
    false, true, true, false, false, true, true, 0.001f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_txt_or_csv( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::Csv, "Csv",
    false, false, false, false, false, true, true, 0.001f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_txt_or_csv( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::ExploraniumGr130v0, "ExploraniumGr130v0",
    false, false, false, false, false, false, false, 0.15f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_binary_exploranium( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::ExploraniumGr135v2, "ExploraniumGr135v2",
    false, false, false, false, false, false, false, 0.15f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_binary_exploranium( is ); }
  } );

  // Single-spectrum formats (sum all records on write)
  formats.push_back( {
    SaveSpectrumAsType::Chn, "Chn",
    true, true, true, false, false, true, true, 0.01f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_chn( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::SpcBinaryInt, "SpcBinaryInt",
    true, true, true, false, false, true, true, 0.01f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_binary_spc( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::SpcBinaryFloat, "SpcBinaryFloat",
    true, true, true, false, false, true, true, 0.01f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_binary_spc( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::SpcAscii, "SpcAscii",
    true, true, true, false, false, true, true, 0.01f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_iaea_spc( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::SpeIaea, "SpeIaea",
    true, true, true, false, false, true, true, 0.01f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_iaea( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::Cnf, "Cnf",
    true, true, true, false, false, true, true, 0.01f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_cnf( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::Tka, "Tka",
    true, false, false, false, false, true, true, 0.01f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_tka( is ); }
  } );

#if( SpecUtils_ENABLE_URI_SPECTRA )
  formats.push_back( {
    SaveSpectrumAsType::Uri, "Uri",
    true, true, true, true, false, true, true, 0.01f,
    []( SpecFile &sf, istream &is ) { return sf.load_from_uri( is ); }
  } );
#endif

  return formats;
}//get_formats()


// Helper to compare a reloaded file against the original for a given format.
// For single-spectrum formats with multi-record input, the original's summed measurement
// is used as the reference.
static void compare_roundtrip( const SpecFile &original,
                               const SpecFile &reloaded,
                               const FormatInfo &fmt )
{
  // Basic check: reloaded file has at least one measurement
  CHECK_MESSAGE( reloaded.num_measurements() >= 1,
    fmt.name << ": reloaded file has no measurements" );

  if( reloaded.num_measurements() < 1 )
    return;

  if( fmt.is_single_spectrum )
  {
    // For single-spectrum formats, compare against summed original
    const set<int> &samples = original.sample_numbers();
    const vector<string> &det_names = original.gamma_detector_names();

    shared_ptr<Measurement> summed = original.sum_measurements( samples, det_names, nullptr );
    CHECK_MESSAGE( summed != nullptr, fmt.name << ": failed to sum original measurements" );
    if( !summed )
      return;

    // Reloaded should have exactly 1 measurement
    CHECK_MESSAGE( reloaded.num_measurements() == 1,
      fmt.name << ": expected 1 measurement, got " << reloaded.num_measurements() );

    const shared_ptr<const Measurement> reloaded_meas = reloaded.measurement_at_index( 0 );
    CHECK_MESSAGE( reloaded_meas != nullptr, fmt.name << ": reloaded measurement is null" );
    if( !reloaded_meas )
      return;

    // Compare gamma count sum
    const double orig_counts = summed->gamma_count_sum();
    const double reload_counts = reloaded_meas->gamma_count_sum();

    if( orig_counts > 1.0 )
    {
      const double rel_diff = fabs( orig_counts - reload_counts ) / orig_counts;
      CHECK_MESSAGE( rel_diff < fmt.count_tolerance,
        fmt.name << ": gamma count sum mismatch: original=" << orig_counts
        << " reloaded=" << reload_counts << " rel_diff=" << rel_diff );
    }

    // Compare number of channels
    if( fmt.preserves_num_channels && summed->gamma_counts() && reloaded_meas->gamma_counts() )
    {
      CHECK_MESSAGE( summed->gamma_counts()->size() == reloaded_meas->gamma_counts()->size(),
        fmt.name << ": channel count mismatch: original=" << summed->gamma_counts()->size()
        << " reloaded=" << reloaded_meas->gamma_counts()->size() );
    }

    // Compare live time
    if( fmt.preserves_live_time && summed->live_time() > 0.0f )
    {
      const float orig_lt = summed->live_time();
      const float reload_lt = reloaded_meas->live_time();
      const float rel_diff = fabsf( orig_lt - reload_lt ) / orig_lt;
      CHECK_MESSAGE( rel_diff < 0.01f,
        fmt.name << ": live time mismatch: original=" << orig_lt
        << " reloaded=" << reload_lt );
    }

    // Compare real time
    if( fmt.preserves_real_time && summed->real_time() > 0.0f )
    {
      const float orig_rt = summed->real_time();
      const float reload_rt = reloaded_meas->real_time();
      const float rel_diff = fabsf( orig_rt - reload_rt ) / orig_rt;
      CHECK_MESSAGE( rel_diff < 0.01f,
        fmt.name << ": real time mismatch: original=" << orig_rt
        << " reloaded=" << reload_rt );
    }
  }
  else
  {
    // Multi-record formats: compare aggregate quantities

    // For formats that truly preserve record structure, check measurement count
    if( fmt.format == SaveSpectrumAsType::N42_2012
       || fmt.format == SaveSpectrumAsType::N42_2006
       || fmt.format == SaveSpectrumAsType::Pcf )
    {
      CHECK_MESSAGE( reloaded.num_measurements() == original.num_measurements(),
        fmt.name << ": measurement count mismatch: original=" << original.num_measurements()
        << " reloaded=" << reloaded.num_measurements() );
    }

    // Compare total gamma count sum
    const double orig_counts = original.gamma_count_sum();
    const double reload_counts = reloaded.gamma_count_sum();

    if( orig_counts > 1.0 )
    {
      const double rel_diff = fabs( orig_counts - reload_counts ) / orig_counts;
      CHECK_MESSAGE( rel_diff < fmt.count_tolerance,
        fmt.name << ": gamma count sum mismatch: original=" << orig_counts
        << " reloaded=" << reload_counts << " rel_diff=" << rel_diff );
    }

    // Compare total live time
    if( fmt.preserves_live_time && original.gamma_live_time() > 0.0f )
    {
      const float orig_lt = original.gamma_live_time();
      const float reload_lt = reloaded.gamma_live_time();
      const float rel_diff = fabsf( orig_lt - reload_lt ) / orig_lt;
      CHECK_MESSAGE( rel_diff < 0.01f,
        fmt.name << ": total live time mismatch: original=" << orig_lt
        << " reloaded=" << reload_lt );
    }

    // Compare total real time
    if( fmt.preserves_real_time && original.gamma_real_time() > 0.0f )
    {
      const float orig_rt = original.gamma_real_time();
      const float reload_rt = reloaded.gamma_real_time();
      const float rel_diff = fabsf( orig_rt - reload_rt ) / orig_rt;
      CHECK_MESSAGE( rel_diff < 0.01f,
        fmt.name << ": total real time mismatch: original=" << orig_rt
        << " reloaded=" << reload_rt );
    }

    // Compare neutron counts
    if( fmt.preserves_neutron_counts && original.neutron_counts_sum() > 0.0 )
    {
      const double orig_n = original.neutron_counts_sum();
      const double reload_n = reloaded.neutron_counts_sum();
      const double rel_diff = fabs( orig_n - reload_n ) / orig_n;
      CHECK_MESSAGE( rel_diff < 0.01,
        fmt.name << ": neutron count mismatch: original=" << orig_n
        << " reloaded=" << reload_n );
    }

    // Compare GPS info
    if( fmt.preserves_gps && original.has_gps_info() )
    {
      CHECK_MESSAGE( reloaded.has_gps_info(),
        fmt.name << ": GPS info lost in roundtrip" );

      if( reloaded.has_gps_info() )
      {
        CHECK_MESSAGE( fabs( original.mean_latitude() - reloaded.mean_latitude() ) < 0.001,
          fmt.name << ": latitude mismatch" );
        CHECK_MESSAGE( fabs( original.mean_longitude() - reloaded.mean_longitude() ) < 0.001,
          fmt.name << ": longitude mismatch" );
      }
    }
  }//if single-spectrum / else multi-record
}//compare_roundtrip(...)


static void run_roundtrip_for_file( const string &filepath, const string &filename )
{
  SpecFile original;
  const bool loaded = original.load_file( filepath, ParserType::Auto, filename );
  REQUIRE_MESSAGE( loaded, "Failed to load " << filepath );
  REQUIRE_MESSAGE( original.num_measurements() >= 1, "No measurements in " << filepath );

  // Build sample and detector number sets for write()
  const set<int> &sample_nums = original.sample_numbers();
  const vector<int> &det_nums_vec = original.detector_numbers();
  const set<int> det_nums( det_nums_vec.begin(), det_nums_vec.end() );

  CHECK_MESSAGE( !sample_nums.empty(), "No sample numbers in " << filepath );
  CHECK_MESSAGE( !det_nums.empty(), "No detector numbers in " << filepath );

  const vector<FormatInfo> formats = get_formats();

  for( const FormatInfo &fmt : formats )
  {
    SUBCASE( fmt.name )
    {
      // Step 1: Write to stringstream
      stringstream ss( ios::in | ios::out | ios::binary );

      bool write_ok = false;
      try
      {
        original.write( ss, sample_nums, det_nums, fmt.format );
        write_ok = true;
      }
      catch( const exception &e )
      {
        CHECK_MESSAGE( false, fmt.name << ": write() threw: " << e.what() );
      }

      if( !write_ok )
        continue;

      const string data = ss.str();
      CHECK_MESSAGE( !data.empty(), fmt.name << ": write produced empty output" );
      if( data.empty() )
        continue;

      // Step 2: Re-parse from the written data
      istringstream iss( data, ios::binary );
      SpecFile reloaded;

      bool reload_ok = false;
      try
      {
        reload_ok = fmt.loader( reloaded, iss );
      }
      catch( const exception &e )
      {
        CHECK_MESSAGE( false, fmt.name << ": loader threw: " << e.what() );
      }

      CHECK_MESSAGE( reload_ok, fmt.name << ": failed to re-parse written data" );
      if( !reload_ok )
        continue;

      // Step 3: Compare
      compare_roundtrip( original, reloaded, fmt );
    }//SUBCASE( fmt.name )
  }//for( each format )
}//run_roundtrip_for_file(...)


TEST_CASE( "Roundtrip Mn56_DetX_Shielded.n42" )
{
  const string indir = get_indir();
  REQUIRE_MESSAGE( !indir.empty(), "No --indir specified" );

  const string filepath = SpecUtils::append_path( indir, "spectra/Mn56_DetX_Shielded.n42" );
  REQUIRE_MESSAGE( SpecUtils::is_file( filepath ), "Test file not found: " << filepath );

  run_roundtrip_for_file( filepath, "Mn56_DetX_Shielded.n42" );
}


TEST_CASE( "Roundtrip passthrough.n42" )
{
  const string indir = get_indir();
  REQUIRE_MESSAGE( !indir.empty(), "No --indir specified" );

  // passthrough.n42 is in bindings/python/examples/ relative to the repo root.
  // indir points to unit_tests/test_data, so go up two levels.
  const string repo_root = SpecUtils::parent_path( SpecUtils::parent_path( indir ) );
  const string filepath = SpecUtils::append_path( repo_root,
    "bindings/python/examples/passthrough.n42" );

  REQUIRE_MESSAGE( SpecUtils::is_file( filepath ), "Test file not found: " << filepath );

  run_roundtrip_for_file( filepath, "passthrough.n42" );
}
