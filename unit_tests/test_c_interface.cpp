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

#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include "SpecUtils/DateTime.h"
#include "SpecUtils/Filesystem.h"

#include "bindings/c/SpecUtils_c.h"


// I couldnt quite figure out how to access command line arguments
//  from doctest, so we'll just work around it a bit.
std::vector<std::string> g_cl_args;


int main(int argc, char** argv)
{
  for( int i = 0; i < argc; ++i )
    g_cl_args.push_back( argv[i] );
  
  return doctest::Context(argc, argv).run();
}



TEST_CASE( "TestCWrapperOpenFile" )
{
  // "datetimes.txt" contains a lot of date/times that could be seen in spectrum files
  std::string indir, input_filename = "";

  for( size_t i = 1; i < g_cl_args.size()-1; ++i )
  {
    if( g_cl_args[i] == std::string("--indir") )
      indir = g_cl_args[i+1];
  }
  
  // We will look for "datetimes.txt", in a not-so-elegant way
  const std::string potential_input_paths[] = {
    indir,
    "",
    "./unit_tests/test_data",
    "../unit_tests/test_data",
    "../../unit_tests/test_data",
    "../../../unit_tests/test_data"
  };
  
  const std::string rel_file_name = "spectra/Example1.pcf";
  
  for( const std::string dir : potential_input_paths )
  {
    const std::string potential = SpecUtils::append_path( dir, rel_file_name );
    if( SpecUtils::is_file(potential) )
      input_filename = potential;
  }
  
  REQUIRE( !input_filename.empty() );
  
  // Create a SpecUtils::SpecFile object, that we will use
  //  to open a spectrum file.
  SpecUtils_SpecFile *specfile = SpecUtils_SpecFile_create();
  
  // The filename to open
  const char * const filename = "unit_tests/test_data/spectra/drf_cal_HPGe_Am241.pcf";
  
  // Parse file into memory
  const bool success = SpecFile_load_file( specfile, input_filename.c_str() );

  if( !success )
    SpecUtils_SpecFile_destroy( specfile );

  REQUIRE( success );
  
  // The file should only have a single record in it.
  const uint32_t num_meas = SpecUtils_SpecFile_number_measurements( specfile );
  if( num_meas == 0 )
    SpecUtils_SpecFile_destroy( specfile );
  
  REQUIRE( num_meas >= 0 );
  CHECK( num_meas == 1 );
  
  // This is just a smoke test that we can open a file.
  //  We wont go into any more detail here.
  
  SpecUtils_SpecFile_destroy( specfile );
}//TEST_CASE( "TestCWrapperOpenFile" )


SpecUtils_Measurement *make_measurement( int id, const char *det_name, char tag )
{
  SpecUtils_Measurement *m = SpecUtils_Measurement_create();
  REQUIRE( m );
  SpecUtils_Measurement_set_detector_name(m, det_name);
  
  const char *setName = SpecUtils_Measurement_detector_name(m);
  CHECK( strcmp(det_name, setName) == 0 );
  
  SpecUtils_Measurement_set_pcf_tag( m, tag );
  CHECK( SpecUtils_Measurement_pcf_tag(m) == tag );
    
  const auto now_sys = std::chrono::system_clock::now();
  const auto now_tp = std::chrono::time_point_cast<std::chrono::microseconds>(now_sys);
  const SpecUtils::time_point_t epoch{};
  const auto total_time = std::chrono::duration_cast<std::chrono::microseconds>(now_tp - epoch);
  
  const int64_t now_usec = static_cast<int64_t>(total_time.count());
  const std::string now_str = SpecUtils::to_extended_iso_string( now_tp );
  
  bool set_time_with_str = SpecUtils_Measurement_set_start_time_str(m, "Invalid str");
  CHECK( !set_time_with_str );
  
  set_time_with_str = SpecUtils_Measurement_set_start_time_str(m, now_str.c_str());
  CHECK( set_time_with_str );
  
  const int64_t read_now_from_str = SpecUtils_Measurement_start_time_usecs(m);
  CHECK_EQ( now_usec, read_now_from_str );
  
  SpecUtils_Measurement_set_start_time_usecs(m, int64_t(0) );
  CHECK_EQ( SpecUtils_Measurement_start_time_usecs(m), 0 );
  
  SpecUtils_Measurement_set_start_time_usecs(m, now_usec );
  const int64_t read_now_usec = SpecUtils_Measurement_start_time_usecs(m);
  CHECK_EQ( now_usec, read_now_usec );

  const std::string title = "Test Measurement " + std::to_string(id) + " Det=" + det_name;
  SpecUtils_Measurement_set_title( m, title.c_str() );
  const char *readTitle = SpecUtils_Measurement_title( m );
  REQUIRE( readTitle );
  CHECK_EQ( title, std::string(readTitle) );

  const std::string descr = "test_descr " + std::to_string(id);
  SpecUtils_Measurement_set_description(m, descr.c_str());
  const char *readDesc = SpecUtils_Measurement_description( m );
  REQUIRE( readDesc );
  CHECK_EQ( descr, std::string(readDesc) );
  
  const std::string source = "source " + std::to_string(id);
  SpecUtils_Measurement_set_source_string(m, source.c_str());
  const char *readSrc = SpecUtils_Measurement_source_string(m);
  REQUIRE( readSrc );
  CHECK_EQ( source, std::string(readSrc) );
  
  const uint32_t num_channel = 128;
  const float gamma_live_time = id + 10.55F;
  const float real_time = id + 11.66F;
  
  double gamma_sum = 0.0;
  std::vector<float> spectrum;
  for( uint32_t i = 0; i < num_channel; i++ )
  {
    const float val = i * 1.0F;
    gamma_sum += val;
    spectrum.push_back(val);
  }
  SpecUtils_Measurement_set_gamma_counts(m, &(spectrum[0]), num_channel, gamma_live_time, real_time );
  
  const double returned_gamma_sum = SpecUtils_Measurement_gamma_count_sum(m);
  CHECK( fabs(gamma_sum - returned_gamma_sum) < 0.001 );
  
  const float returned_real_time = SpecUtils_Measurement_real_time(m);
  CHECK_EQ( returned_real_time, real_time );
  
  const float returned_live_time = SpecUtils_Measurement_live_time(m);
  CHECK_EQ( returned_live_time, gamma_live_time );
  
  CHECK( !SpecUtils_Measurement_contained_neutron(m) );
  
  
  const float neutron_live_time = real_time - 1.2f;
  const float num_neutron_counts[2] = {id + 99.0F, id + 1.0f};
  const size_t num_neut_tubes = sizeof(num_neutron_counts) / sizeof(num_neutron_counts[0]);
  double neut_sum = 0.0;
  for( size_t i = 0; i < num_neut_tubes; ++i )
    neut_sum += num_neutron_counts[i];
  
  SpecUtils_Measurement_set_neutron_counts( m, num_neutron_counts, num_neut_tubes, neutron_live_time );
  
  CHECK( SpecUtils_Measurement_contained_neutron(m) );
  
  const float returned_neut_live_time = SpecUtils_Measurement_neutron_live_time(m);
  CHECK_EQ( returned_neut_live_time, neutron_live_time );
  const double return_neut_sum = SpecUtils_Measurement_neutron_count_sum(m);
  CHECK_EQ( return_neut_sum, neut_sum );
  
  
  {// Begin quick check to make sure invalid energy calibrations are rejected
    SpecUtils_EnergyCal *invalid_cal = SpecUtils_EnergyCal_create();
    const float invalid_cal_coefs[] = {-0.1f, -1.2f };
    const bool valid_cal = SpecUtils_EnergyCal_set_polynomial( invalid_cal, num_channel, invalid_cal_coefs, 2, NULL, 0 );
    CHECK( !valid_cal );
    SpecUtils_EnergyCal_destroy( invalid_cal );
  }// End quick check to make sure invalid energy calibrations are rejected
  
  
  const float cal_coefs[] = {-0.1f, 1.2f, -0.0001f };
  const uint32_t num_cal_coefs = sizeof(cal_coefs) / sizeof(cal_coefs[0]);
  float upper_energy = 0;
  for( uint32_t i = 0; i < num_cal_coefs; ++i )
    upper_energy += cal_coefs[i]*std::pow(1.0f*num_channel,1.0f*i);
  
  const uint32_t number_dev_pairs = 10;
  float dev_pairs[2*number_dev_pairs] = {0.0f};
  for (size_t i = 0; i < number_dev_pairs; i++)
  {
    const float energy = i * upper_energy / number_dev_pairs;
    const float offset = i; //TODO: come up with a better slightly random offset energy
    dev_pairs[i*2] = energy;
    dev_pairs[i*2 + 1] = offset;
  }
  
  
  SpecUtils_EnergyCal *cal = SpecUtils_EnergyCal_create();
  const bool valid_cal = SpecUtils_EnergyCal_set_polynomial( cal, num_channel, cal_coefs, num_cal_coefs, dev_pairs, number_dev_pairs );
  CHECK( valid_cal );

  // This next line will make it so we MUST NOT call destroy on `cal`
  SpecUtils_CountedRef_EnergyCal *cal_ref = SpecUtils_EnergyCal_make_counted_ref(cal);
  CHECK_EQ( SpecUtils_EnergyCal_ptr_from_ref(cal_ref), cal );
  
  SpecUtils_Measurement_set_energy_calibration(m, cal_ref);
  
  //We still need to cleanup the counted ref though
  SpecUtils_CountedRef_EnergyCal_destroy( cal_ref );
  
  //The original `cal` object is still valid, and living in memory; SpecUtils::Measurement now has
  //  a counted ref to the object in memory, that it owns (really a c++ shared_ptr is used).
  
  // Now that we have set the energy calibration, we can sum over an energy range
  const double returned_gamma_integral = SpecUtils_Measurement_gamma_integral(m, -10000.0f, 10000000.0f );
  CHECK( fabs(gamma_sum - returned_gamma_integral) < 0.001 );
  
  return m;
}


TEST_CASE( "TestCWrapperCreateAndModifyFile" )
{
  SpecUtils_SpecFile *specfile = SpecUtils_SpecFile_create();
  REQUIRE( specfile );
  
  const int ids[] = { 1, 4, 5 };
  const char *det_names[] = { "Aa1", "Ba1", "SomeOtherName" };
  char tags[] = { ' ', '\0', 'K' };
  
  double gamma_sum = 0.0, neutron_sum = 0.0;
  
  for( const int meas_id : ids )
  {
    for( const char *det_name : det_names )
    {
      for( const char tag : tags )
      {
        SpecUtils_Measurement *meas = make_measurement( meas_id, det_name, tag );
        REQUIRE( meas );
        
        gamma_sum += SpecUtils_Measurement_gamma_count_sum(meas);
        neutron_sum += SpecUtils_Measurement_neutron_count_sum(meas);
        
        const bool do_cleanup_now = false;
        SpecUtils_SpecFile_add_measurement(specfile, meas, do_cleanup_now);
      }//for( const char tag : tags )
    }//for( const char *det_name : det_names )
  }//for( const int meas_id : ids )
  
  const bool dont_change_sample_numbers = false;
  const bool reorder_by_time = false;
  SpecUtils_SpecFile_cleanup( specfile, dont_change_sample_numbers, reorder_by_time );
  
  uint32_t meas_index = 0;
  for( const int meas_id : ids )
  {
    for( const char *det_name : det_names )
    {
      for( const char tag : tags )
      {
        const SpecUtils_Measurement *m = SpecUtils_SpecFile_get_measurement_by_index(specfile, meas_index);
        REQUIRE( !!m );
        CHECK_EQ( std::string(det_name), std::string(SpecUtils_Measurement_detector_name(m)) );
        CHECK_EQ( tag, SpecUtils_Measurement_pcf_tag(m) );
        
        meas_index += 1;
      }//for( const char tag : tags )
    }//for( const char *det_name : det_names )
  }//for( const int meas_id : ids )
}//TEST_CASE( "TestCWrapperCreateAndModifyFile" )
