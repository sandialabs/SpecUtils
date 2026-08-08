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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
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
#include "SpecUtils/CAMIO.h"
#include "SpecUtils/EnergyCalibration.h"
#include "SpecUtils/DateTime.h"
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
  bool preserves_start_time;    // true if format stores a measurement start time
  int start_time_tolerance_sec; // allowed round-trip drift; 1 for whole-second-precision formats
  function<bool( SpecFile &, istream & )> loader;
};


static vector<FormatInfo> get_formats()
{
  vector<FormatInfo> formats;

  // Multi-record formats
  formats.push_back( {
    SaveSpectrumAsType::N42_2012, "N42_2012",
    false, true, true, true, true, true, true, 0.001f,
    true, 0,
    []( SpecFile &sf, istream &is ) { return sf.load_from_N42( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::N42_2006, "N42_2006",
    false, true, true, true, true, true, true, 0.001f,
    true, 0,
    []( SpecFile &sf, istream &is ) { return sf.load_from_N42( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::Pcf, "Pcf",
    false, true, true, true, false, true, true, 0.01f,
    true, 1,
    []( SpecFile &sf, istream &is ) { return sf.load_from_pcf( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::Txt, "Txt",
    false, true, true, false, false, true, true, 0.001f,
    false, 0,
    []( SpecFile &sf, istream &is ) { return sf.load_from_txt_or_csv( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::Csv, "Csv",
    false, false, false, false, false, true, true, 0.001f,
    false, 0,
    []( SpecFile &sf, istream &is ) { return sf.load_from_txt_or_csv( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::ExploraniumGr130v0, "ExploraniumGr130v0",
    false, false, false, false, false, false, false, 0.15f,
    false, 0,
    []( SpecFile &sf, istream &is ) { return sf.load_from_binary_exploranium( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::ExploraniumGr135v2, "ExploraniumGr135v2",
    false, false, false, false, false, false, false, 0.15f,
    false, 0,
    []( SpecFile &sf, istream &is ) { return sf.load_from_binary_exploranium( is ); }
  } );

  // Single-spectrum formats (sum all records on write)
  formats.push_back( {
    SaveSpectrumAsType::Chn, "Chn",
    true, true, true, false, false, true, true, 0.01f,
    true, 1,
    []( SpecFile &sf, istream &is ) { return sf.load_from_chn( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::SpcBinaryInt, "SpcBinaryInt",
    true, true, true, false, false, true, true, 0.01f,
    true, 1,
    []( SpecFile &sf, istream &is ) { return sf.load_from_binary_spc( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::SpcBinaryFloat, "SpcBinaryFloat",
    true, true, true, false, false, true, true, 0.01f,
    true, 1,
    []( SpecFile &sf, istream &is ) { return sf.load_from_binary_spc( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::SpcAscii, "SpcAscii",
    true, true, true, false, false, true, true, 0.01f,
    true, 1,
    []( SpecFile &sf, istream &is ) { return sf.load_from_iaea_spc( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::SpeIaea, "SpeIaea",
    true, true, true, false, false, true, true, 0.01f,
    true, 1,
    []( SpecFile &sf, istream &is ) { return sf.load_from_iaea( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::Cnf, "Cnf",
    true, true, true, false, true, true, true, 0.01f,
    true, 1,
    []( SpecFile &sf, istream &is ) { return sf.load_from_cnf( is ); }
  } );

  formats.push_back( {
    SaveSpectrumAsType::Tka, "Tka",
    true, false, false, false, false, true, true, 0.01f,
    false, 0,
    []( SpecFile &sf, istream &is ) { return sf.load_from_tka( is ); }
  } );

#if( SpecUtils_ENABLE_URI_SPECTRA )
  formats.push_back( {
    SaveSpectrumAsType::Uri, "Uri",
    true, true, true, true, false, true, true, 0.01f,
    true, 1,
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

    // Compare start time
    if( fmt.preserves_start_time
        && !SpecUtils::is_special( summed->start_time() )
        && !SpecUtils::is_special( reloaded_meas->start_time() ) )
    {
      const SpecUtils::time_point_t orig_st = summed->start_time();
      const SpecUtils::time_point_t reload_st = reloaded_meas->start_time();
      const auto signed_diff = orig_st - reload_st;
      const auto diff = (signed_diff < signed_diff.zero()) ? -signed_diff : signed_diff;
      const auto tolerance = std::chrono::seconds( fmt.start_time_tolerance_sec );
      CHECK_MESSAGE( diff <= tolerance,
        fmt.name << ": start time mismatch: original="
        << SpecUtils::to_iso_string( orig_st )
        << " reloaded=" << SpecUtils::to_iso_string( reload_st ) );
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

    // Compare per-measurement start time on formats that preserve record structure.
    if( fmt.preserves_start_time
        && (fmt.format == SaveSpectrumAsType::N42_2012
            || fmt.format == SaveSpectrumAsType::N42_2006
            || fmt.format == SaveSpectrumAsType::Pcf)
        && (reloaded.num_measurements() == original.num_measurements()) )
    {
      const auto tolerance = std::chrono::seconds( fmt.start_time_tolerance_sec );
      for( size_t i = 0; i < original.num_measurements(); ++i )
      {
        const std::shared_ptr<const Measurement> orig_m = original.measurement_at_index( i );
        const std::shared_ptr<const Measurement> reload_m = reloaded.measurement_at_index( i );
        if( !orig_m || !reload_m )
          continue;
        if( SpecUtils::is_special( orig_m->start_time() )
            || SpecUtils::is_special( reload_m->start_time() ) )
          continue;
        const auto signed_diff = orig_m->start_time() - reload_m->start_time();
        const auto diff = (signed_diff < signed_diff.zero()) ? -signed_diff : signed_diff;
        CHECK_MESSAGE( diff <= tolerance,
          fmt.name << " meas " << i << ": start time mismatch: original="
          << SpecUtils::to_iso_string( orig_m->start_time() )
          << " reloaded=" << SpecUtils::to_iso_string( reload_m->start_time() ) );
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
      // For single-spectrum formats with multiple measurements, pre-sum into a single
      //  spectrum since some formats (e.g., URI) can only represent a limited number of spectra.
      const SpecFile *write_src = &original;
      SpecFile summed;
      set<int> write_sample_nums = sample_nums;
      set<int> write_det_nums = det_nums;

      if( fmt.is_single_spectrum && (original.num_measurements() > 1) )
      {
        auto sum_meas = original.sum_measurements( sample_nums, original.detector_names(), nullptr );
        if( !sum_meas )
        {
          CHECK_MESSAGE( false, fmt.name << ": failed to create summed measurement" );
          continue;
        }
        summed.add_measurement( sum_meas, true );
        write_src = &summed;
        write_sample_nums = summed.sample_numbers();
        const vector<int> &sdn = summed.detector_numbers();
        write_det_nums = set<int>( sdn.begin(), sdn.end() );
      }//if( single-spectrum format with multiple measurements )

      // Step 1: Write to stringstream
      stringstream ss( ios::in | ios::out | ios::binary );

      bool write_ok = false;
      try
      {
        write_src->write( ss, write_sample_nums, write_det_nums, fmt.format );
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
      compare_roundtrip( *write_src, reloaded, fmt );
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


TEST_CASE( "IEC declarations are bounded and match retained channel data" )
{
  const auto make_iec_test_input = []( const string &declaration, const size_t count,
                                       const size_t first_channel = 0 ) {
    ostringstream out;
    out << "A004 SYSTEM  SUBSYS  00000000000000\n"
        << "A004 1 1 " << declaration << "\n"
        << "A004 01/01/24 00:00:00 01/01/24 00:00:01\n"
        << "A004 0 1 0 0\n"
        << "A004 1 0 0 0 0.5\n";

    for( size_t offset = 0; offset < count; offset += 5 )
    {
      out << "A004 " << (first_channel + offset);
      const size_t row_count = std::min(size_t(5), count - offset);
      for( size_t i = 0; i < row_count; ++i )
        out << " 1";
      out << "\n";
    }
    return out.str();
  };

  for( const string &declaration : {"9999999", "131073", "64.5", "nan"} )
  {
    SpecFile file;
    istringstream input( make_iec_test_input(declaration, 64) );
    CHECK_FALSE( file.load_from_spectraline_iec(input) );
  }

  // A file that declares more channels than it carries is accepted as a truncated
  // spectrum: the retained counts still start at channel 0 and are contiguous, so
  // they line up with the energy calibration.  The shortfall is reported.
  {
    SpecFile file;
    istringstream input( make_iec_test_input("128", 65) );
    CHECK( file.load_from_spectraline_iec(input) );
    REQUIRE( file.measurements().size() == 1 );
    const auto meas = file.measurements().front();
    CHECK( meas->num_gamma_channels() == 65 );
    bool noted_truncation = false;
    for( const string &warning : meas->parse_warnings() )
      noted_truncation |= (warning.find("truncated") != string::npos);
    CHECK( noted_truncation );
  }

  // Channel data that does not begin at channel 0 is rejected - accepting it would
  // shift every count against the record-4 energy polynomial.
  {
    SpecFile file;
    istringstream input( make_iec_test_input("128", 125, 5) );
    CHECK_FALSE( file.load_from_spectraline_iec(input) );
  }
  {
    SpecFile file;
    istringstream input( make_iec_test_input("64", 64) );
    CHECK( file.load_from_spectraline_iec(input) );
    REQUIRE( file.measurements().size() == 1 );
    CHECK( file.measurements().front()->num_gamma_channels() == 64 );
  }
  {
    SpecFile file;
    const size_t maximum = EnergyCalibration::sm_max_channels;
    istringstream input( make_iec_test_input(std::to_string(maximum), maximum) );
    CHECK( file.load_from_spectraline_iec(input) );
    REQUIRE( file.measurements().size() == 1 );
    CHECK( file.measurements().front()->num_gamma_channels() == maximum );
  }
}


TEST_CASE( "N42-2006 detector descriptions remain XML text" )
{
  const string indir = get_indir();
  REQUIRE_MESSAGE( !indir.empty(), "No --indir specified" );
  const string repo_root = SpecUtils::parent_path( SpecUtils::parent_path(indir) );
  const string path = SpecUtils::append_path(repo_root, "bindings/python/examples/passthrough.n42");
  ifstream input_file( path, ios::binary );
  REQUIRE( input_file.good() );
  string input( (istreambuf_iterator<char>(input_file)), istreambuf_iterator<char>() );

  const string original = "<DetectorType>HPGe 50%</DetectorType>";
  const string replacement =
      "<DetectorType>safe&lt;/DetectorType&gt;&lt;ChannelData&gt;injected"
      "&lt;/ChannelData&gt;&lt;DetectorType&gt;</DetectorType>";
  const size_t position = input.find(original);
  REQUIRE( position != string::npos );
  input.replace(position, original.size(), replacement);

  SpecFile file;
  istringstream input_stream(input);
  REQUIRE( file.load_from_N42(input_stream) );
  ostringstream output;
  REQUIRE( file.write_2006_N42(output) );
  const string xml = output.str();
  CHECK( xml.find("safe&lt;/DetectorType&gt;&lt;ChannelData&gt;injected") != string::npos );
  CHECK( xml.find("<DetectorType>safe</DetectorType><ChannelData>injected") == string::npos );
}


template<typename T>
static void write_cam_test_value( vector<byte_type> &data, const size_t offset, const T value )
{
  REQUIRE( offset + sizeof(T) <= data.size() );
  std::memcpy( data.data() + offset, &value, sizeof(T) );
}


static vector<byte_type> make_cam_geometry_security_test_input(
    const uint16_t entry_size, const uint16_t record_size = 64, const uint16_t records = 1 )
{
  const size_t block_pos = 0x800;
  const size_t data_size = block_pos + 0x30 + static_cast<size_t>(record_size) * records;
  vector<byte_type> data( std::max(size_t(0x600), data_size), 0 );

  write_cam_test_value<uint32_t>( data, 0x70,
      static_cast<uint32_t>(CAMInputOutput::CAMIO::CAMBlock::GEOM) );
  write_cam_test_value<uint32_t>( data, 0x70 + 0x0a, static_cast<uint32_t>(block_pos) );
  write_cam_test_value<uint16_t>( data, block_pos + 0x04, 0x0700 );
  write_cam_test_value<uint16_t>( data, block_pos + 0x10, 0x0030 );
  write_cam_test_value<uint16_t>( data, block_pos + 0x1e, records );
  write_cam_test_value<uint16_t>( data, block_pos + 0x20, record_size );
  write_cam_test_value<uint16_t>( data, block_pos + 0x28, 0 );
  write_cam_test_value<uint16_t>( data, block_pos + 0x2a, entry_size );
  data[block_pos + 0x30] = 1;
  return data;
}


TEST_CASE( "CAM geometry rejects non-advancing and undersized entries" )
{
  for( const uint16_t entry_size : {uint16_t(0), uint16_t(12)} )
  {
    CAMInputOutput::CAMIO reader;
    reader.ReadFile( make_cam_geometry_security_test_input(entry_size) );
    CHECK_THROWS( reader.GetEfficiencyPoints() );
  }

  CAMInputOutput::CAMIO excessive_reader;
  const uint16_t entry_size = 13;
  const uint16_t record_size = numeric_limits<uint16_t>::max();
  const uint16_t records = 27;
  auto excessive_input = make_cam_geometry_security_test_input(entry_size, record_size, records);
  const size_t first_record = 0x800 + 0x30;
  for( size_t record = 0; record < records; ++record )
  {
    const size_t start = first_record + record * static_cast<size_t>(record_size);
    const size_t end = start + record_size;
    for( size_t location = start; location + entry_size <= end; location += entry_size )
      excessive_input[location] = static_cast<byte_type>(record + 1);
  }
  excessive_reader.ReadFile( excessive_input );
  CHECK_THROWS( excessive_reader.GetEfficiencyPoints() );
  CHECK_THROWS( excessive_reader.GetEfficiencyPoints() );
}
