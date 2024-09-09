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

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include "bindings/c/SpecUtils_c.h"

/** This demonstrates a minimal program for opening a spectrum file, and retrieving
 its data in C.
 
 For full usage of API, see `unit_tests/test_c_interface.cpp`.
 */


int main( int argc, char **argv )
{
  // Create a SpecUtils::SpecFile object, that we will use
  //  to open a spectrum file.
  SpecUtils_SpecFile *specfile = SpecUtils_SpecFile_create();
  
  // The filename to open
  const char * const filename = "unit_tests/test_data/spectra/drf_cal_HPGe_Am241.pcf";
  
  // Parse file into memory
  bool success = SpecFile_load_file( specfile, filename );
  
  if( !success )
  {
    fprintf( stderr, "Failed to open input file '%s'.\n", filename );
    SpecUtils_SpecFile_destroy( specfile );
    return EXIT_FAILURE;
  }
  
  // Print out how many spectrum records there are
  const uint32_t num_meas = SpecUtils_SpecFile_number_measurements( specfile );
  fprintf( stdout, "There are %i measurements in the file.\n", (int)num_meas );
  
  if( num_meas == 0 )
  {
    SpecUtils_SpecFile_destroy( specfile );
    return EXIT_SUCCESS;
  }
  
  // Print out how many sample numbers (e.g., how many different time periods of
  //  measurement this file contained)
  const uint32_t num_sample_nums = SpecUtils_SpecFile_number_samples( specfile );
  fprintf( stdout, "There are %i sample numbers in the file: ", (int)num_sample_nums );
  for( uint32_t index = 0; index < num_sample_nums; ++index )
  {
    // Sample numbers likely do not start at zero, and do not have to be contigous
    const int sample_num = SpecUtils_SpecFile_sample_number( specfile, index );
    fprintf( stdout, "%s%d", (index ? ", " : ""), sample_num );
  }
  fprintf( stdout, "\n" );
  
  
  fprintf( stdout, "The detectors are named:" );
  const uint32_t num_detectors = SpecUtils_SpecFile_number_detectors( specfile );
  for( uint32_t index = 0; index < num_detectors; ++index )
  {
    const char * const det_name = SpecUtils_SpecFile_detector_name( specfile, index );
    fprintf( stdout, "%s'%s'", (index ? ", " : ""), det_name );
  }
  fprintf( stdout, "\n" );
  
  for( uint32_t meas_num = 0; meas_num < num_meas; ++meas_num )
  {
    const SpecUtils_Measurement *meas
                              = SpecUtils_SpecFile_get_measurement_by_index( specfile, meas_num );
    
    const uint32_t num_gamma_channel = SpecUtils_Measurement_number_gamma_channels(meas);
    const float * const channel_counts = SpecUtils_Measurement_gamma_channel_counts(meas);
    
    const int sample_num = SpecUtils_Measurement_sample_number(meas);
    const char * const detector_name = SpecUtils_Measurement_detector_name(meas);
    const float live_time = SpecUtils_Measurement_live_time(meas);
    const float real_time = SpecUtils_Measurement_real_time(meas);
    
    fprintf( stdout, "Measurement %d is sample number %d, and detector '%s', "
            "with live time %f s, and real time %f s.\n",
            (int)meas_num, sample_num, detector_name,
            live_time, real_time );
    
    // Print out the channel counts for the first 10 channels
    const uint32_t num_chan_printout = (num_gamma_channel > 10) ? (uint32_t)10 : num_gamma_channel;
    
    fprintf( stdout, "Measurement %d channel counts: ", (int)meas_num );
    for( uint32_t i = 0; i < num_chan_printout; ++i )
      fprintf( stdout, "%s%f", (i ? ", " : ""), channel_counts[i] );
    fprintf( stdout, "...\n" );
    
    // Print out channel energies, if defined (if defined, will have one more entry than number
    //  of gamma channels).
    const float * const energy_bounds = SpecUtils_Measurement_energy_bounds(meas);
    if( energy_bounds )
    {
      fprintf( stdout, "Measurement %d channel energies: ", (int)meas_num );
      for( uint32_t i = 0; i < num_chan_printout; ++i )
        fprintf( stdout, "%s%f", (i ? ", " : ""), energy_bounds[i] );
      fprintf( stdout, "...\n" );
    }

    // We access more information about the energy cal:
    const SpecUtils_EnergyCal *energy_cal = SpecUtils_Measurement_energy_calibration(meas);
    const enum SpecUtils_EnergyCalType cal_type = SpecUtils_EnergyCal_type(energy_cal);
    const uint32_t num_energy_cal_coeffs = SpecUtils_EnergyCal_number_coefficients(energy_cal);
    const float * const energy_cal_coeffs = SpecUtils_EnergyCal_coefficients(energy_cal);
    const uint32_t num_dev_pairs = SpecUtils_EnergyCal_number_deviation_pairs(energy_cal);
    for( uint32_t dev_pair_num = 0; dev_pair_num < num_dev_pairs; ++dev_pair_num )
    {
      const float energy = SpecUtils_EnergyCal_deviation_energy( energy_cal, dev_pair_num );
      const float offset = SpecUtils_EnergyCal_deviation_offset( energy_cal, dev_pair_num );
    }
    
    const bool had_neutrons = SpecUtils_Measurement_contained_neutron(meas);
    if( had_neutrons )
    {
      const double num_neutrons = SpecUtils_Measurement_neutron_count_sum(meas);
      const float neut_live_time = SpecUtils_Measurement_neutron_live_time(meas);
      fprintf( stdout, "Measurement %d has %f neutrons, with live time %f seconds\n",
              (int)meas_num, num_neutrons, neut_live_time );
    }
    
    if( SpecUtils_Measurement_has_gps_info(meas) )
    {
      const double latitude = SpecUtils_Measurement_latitude(meas);
      const double longitude = SpecUtils_Measurement_longitude(meas);
      fprintf( stdout, "Measurement was at lat,lon=%f,%f\n", latitude, longitude );
      const int64_t num_micro_after_epoch = SpecUtils_Measurement_position_time_microsec(meas);
      if( num_micro_after_epoch )
      {
        const int64_t seconds_after_epoch = num_micro_after_epoch / 1000000;
        time_t time_point = seconds_after_epoch;
        char *time_string = ctime(&time_point);
        if( time_string != NULL )
          fprintf( stdout, "GPS time: %s", time_string );
      }
    }
    
    // If we have a gamma spectrum, lets change its energy calibration.
    //  (we'll use an arbitrary calibration here)
    if( num_gamma_channel > 5 )
    {
      SpecUtils_EnergyCal *new_cal = SpecUtils_EnergyCal_create();
     
      // We'll change the energy calibration so spectrum goes from 0 to 3000 keV,
      //  using a full-range-fraction energy calibration.
      const float coefficients[2] = { 0.0f, 3000.0f };
      const size_t num_coeffs = sizeof(coefficients) / sizeof(coefficients[0]);
      
      // We'll also define 3 deviation pairs: {59,0}, {661,-10}, {2614,0}
      const float dev_pairs[6] = { 59.0f, 0.0f, 661.0f, -10.0f, 2614.0f, 0.0f };
      const size_t num_dev_pair_entries = sizeof(dev_pairs) / sizeof(dev_pairs[0]);
      const size_t num_dev_pairs = num_dev_pair_entries / 2;
      
      const bool valid_cal = SpecUtils_EnergyCal_set_full_range_fraction( new_cal,
                                                                         num_gamma_channel,
                                                                         coefficients,
                                                                         num_coeffs,
                                                                         dev_pairs,
                                                                         num_dev_pairs );
      if( valid_cal )
      {
        // We can find the (fractional) channel corresponding to a specific energy
        const double example_energy = 661.66;
        const double cs137_channel 
                                = SpecUtils_EnergyCal_channel_for_energy( new_cal, example_energy );
        fprintf( stdout, "With the new energy calibration, %f keV corresponds to channel %f.\n",
                example_energy, cs137_channel );
        
        // Or get the energy of a specific (possibly fractional) channel
        const double example_channel = 0.5*num_gamma_channel;
        const double midpoint_energy 
                              = SpecUtils_EnergyCal_energy_for_channel( new_cal, example_channel );
        fprintf( stdout, "With the new energy calibration, channel %f corresponds to energy %f keV.\n",
                example_channel, midpoint_energy );
        
        // We can also directly access lower channel energies:
        //const float * const new_cal_lower_energies = SpecUtils_EnergyCal_channel_energies( new_cal );
        
        // Internally, the potentially many `SpecUtils_Measurement` objects may share a single
        //  energy calibration object (to save memory), and when we set the energy calibration
        //  of a measurement, the lifetime of it needs to be managed, so here we need to promote
        //  the energy cal pointer to a pointer of an object that tracks its counted references.
        //  (this is really a pointer to a `std::shared_ptr<const SpecUtils::EnergyCalibration>`
        //   object - so kinda double indirection).
        //  After this call, we must no longer explicitly de-allocate `new_cal`, as it is "owned"
        //    by the object pointed to by the result.
        SpecUtils_CountedRef_EnergyCal *cal_cntrref = SpecUtils_EnergyCal_make_counted_ref( new_cal );
        
        // Set the energy calibration for the spectrum.
        //  Note: this changes the energy of peaks/features, but does not change the channel
        //        contents of the spectrum (i.e., the 10th channel will have the same number of
        //        counts)
        SpecUtils_SpecFile_set_measurement_energy_calibration( specfile, cal_cntrref, meas );
        
        // Note: `meas` is a `const SpecUtils_Measurement *`, so because its const, we shouldnt
        //  set its energy calibration directly, with a call like:
        //
        //SpecUtils_Measurement_set_energy_calibration( meas, cal_cntrref );
        //
        // (which we could do if the `SpecUtils_Measurement` object didnt belong to a
        //  `SpecUtils_SpecFile`), but instead we have to ask the (non-const)`SpecUtils_SpecFile`
        //  to change the energy calibration, since it "owns" the `SpecUtils_Measurement`.
        
        // We could have instead re-binned the spectrum; this would leave all the peaks/features
        //  at the same energies, but change the channel counts
        //SpecUtils_Measurement_rebin( meas, cal_cntrref );
        
        // Now cleanup the counted reference object.  
        //  Note: the `SpecUtils_Measurement` holds a counted ref to the `SpecUtils_EnergyCal`
        //        object, so it wont be deleted by this next call, just our reference counter.
        SpecUtils_CountedRef_EnergyCal_destroy( cal_cntrref );
      }else
      {
        // We wont get here since we know the energy calibration is valid - but you may not know
        //  this.  e.g., if you are letting a user input energy calibration coefficients.
        SpecUtils_EnergyCal_destroy( new_cal ); // clean-up memory, since we wont be assigning
        fprintf( stderr, "Unexpected invalid full-range-fraction energy calibration\n" );
      }
    }//if( num_gamma_channel > 5 )
    
    // For demonstration, lets edit a few more quantities of the measurement
    //
    //  We _could) make a copy of this measurement, and set its values directly, ex:
    //  SpecUtils_Measurement *meas_copy = SpecUtils_Measurement_clone(meas);
    //  SpecUtils_Measurement_set_position( meas_copy, -121.758858, 37.675911, 0 );
    //  SpecUtils_Measurement_set_title( meas_copy, "Some spectrum title" );
    //  ...
    //
    // But instead, we'll edit the SpecFile currently in memory.
    //  However, `meas` is a const pointer (because it is owned by `specfile`), so
    //  we'll edit it indirectly through calls using `specfile`.
    //
    // Set some GPS coordinates
    SpecUtils_SpecFile_set_measurement_position( specfile, -121.758858, 37.675911, 0, meas );
    
    // Set the measurement title
    SpecUtils_SpecFile_set_measurement_title( specfile, "Some spectrum title", meas );
    
    // Set the neutron counts
    SpecUtils_SpecFile_set_measurement_contained_neutrons( specfile, true, 100.0f, real_time, meas );
    
    // Set that we know this is a item of interest
    SpecUtils_SpecFile_set_measurement_source_type( specfile, SpecUtils_SourceType_Foreground, meas );
     
  }//for( uint32_t meas_num = 0; meas_num < num_meas; ++meas_num )
  
  fprintf( stdout, "Done.\n" );
  return EXIT_SUCCESS;
}//int main()
