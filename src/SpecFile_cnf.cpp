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

#include <array>
#include <cmath>
#include <cctype>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <functional>

#include "3rdparty/date/include/date/date.h"

#include "SpecUtils/DateTime.h"
#include "SpecUtils/SpecFile.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/EnergyCalibration.h"
#include "SpecUtils/CAMIO.h"

using namespace std;

namespace SpecUtils
{
bool SpecFile::load_cnf_file( const std::string &filename )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  reset();
  
#ifdef _WIN32
  ifstream file( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream file( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  if( !file.is_open() )
    return false;
  const bool loaded = load_from_cnf( file );
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_cnf_file( const std::string &filename );
  
  
void SpecFile::load_cnf_using_reader( CAMInputOutput::CAMIO &reader )
{
  // The Measurement we will place all information into.
  auto meas = std::make_shared<Measurement>();
  
  // get the sample ID
  try
  {
    string sampleid= reader.GetSampleTitle();
    meas->title_ = sampleid;
    if( sampleid.size() )
      meas->remarks_.push_back( "Sample ID: " + sampleid );
  }catch( std::exception &e )
  {
    // Will get here if no sample title
  }

  // get the times
  meas->start_time_ = reader.GetAquisitionTime();
  float real_time = reader.GetRealTime();
  meas->real_time_ = real_time;
  meas->live_time_ = reader.GetLiveTime();
  meas->sample_number_ = 1;

  // set the energy calibration
  vector<float> cal_coefs = reader.GetEnergyCalibration();
  
  // get the spectrum
  std::vector<uint32_t> &spec = reader.GetSpectrum(); //Will throw exception if no data
  size_t num_chnanels = spec.size();
  
  // set the energy calibration
  try
  {
    auto newcal = make_shared<EnergyCalibration>();
    newcal->set_polynomial( num_chnanels, cal_coefs, {} );
    meas->energy_calibration_ = newcal;
  }catch( std::exception & )
  {
    bool allZeros = true;
    for( const float v : cal_coefs )
      allZeros = allZeros && (v == 0.0f);
    
    // We could check if this is a alpha spectra or not...
    /*
     bool is_alpha_spec = false;
     if( !allZeros )
     {
     //From only a single file, Alpha spec files have: "Alpha Efcor", "Alpha Encal".  Segment 11, has just "Alpha"
     const uint8_t headers_with_alpha[] = { 2, 6, 11, 13, 19 };
     string buffer( 513, '\0' );
     for( uint8_t i : headers_with_alpha )
     {
     size_t segment_position = 0;
     if( findCnfSegment(i, 0, segment_position, input, size) )
     {
     input.seekg( segment_position, std::ios::beg );
     if( input.read( &(buffer[0]), 512 ) && (buffer.find("Alpha") != string::npos) )
     {
     is_alpha_spec = true;
     break;
     }//if( we found the segment, and it had "Alpha" in it )
     }//if( find segment )
     }//for( loop over potential segments that might have "Alpha" in them )
     }//if( !allZeros )
     */
    
    if( !allZeros )
      throw runtime_error( "Calibration parameters were invalid" );
  }//try /catch set calibration
  
  // Try to get the detector info
  string det_name;
  try
  {
    const CAMInputOutput::DetInfo &det_info = reader.GetDetectorInfo();

    if( det_info.MCAType.size() )
      remarks_.push_back( "MCA Type: " + det_info.MCAType);

    if( det_info.Type.size() )
      remarks_.push_back( "Detector Type: " + det_info.MCAType);

    if( !det_info.SerialNo.empty() )
      instrument_id_ = det_info.SerialNo;

    det_name = det_info.Name;
    if( !det_name.empty() )
      meas->detector_name_ = det_name;
  }catch( std::exception & )
  {
    //Will get here if no detector info
  }//try/catch to get detector info


  // convert the int32 counts to floats
  auto channel_data = make_shared<vector<float>>(num_chnanels);
  for( size_t i = 0; i < num_chnanels; ++i )
  {
    
    const float val = static_cast<float>( spec[i] );
    meas->gamma_count_sum_ += val;
    (*channel_data)[i] = val;
  }//set gamma channel data
  
  meas->gamma_counts_ = channel_data;
  
  // fill in any results if they exist
  try
  {
    const vector<CAMInputOutput::Nuclide> &cam_results = reader.GetNuclides();
    if( !cam_results.empty() )
    {
      auto new_det_ana = make_shared<DetectorAnalysis>();
      for (size_t i = 0; i < cam_results.size(); i++)
      {
        DetectorAnalysisResult result;
        float activity = cam_results[i].Activity * 37000; // convert from uCi the CNF default

        //skip over non-detects genie stores the hole nuclide library
        if (activity > 1e-6) {
          result.activity_ = activity;
          result.nuclide_ = cam_results[i].Name;
          result.real_time_ = real_time;
          result.detector_ = det_name;

          new_det_ana->results_.push_back(result);
        }
      }//

      detectors_analysis_ = new_det_ana;
    }//if( !cam_results.empty() )
  }catch( std::exception & )
  {

  }//try / catch to fill in any results if they exist

  measurements_.push_back( meas );
}//void load_cnf_using_reader( CAMInputOutput::CAMIO &reader )
  
  
bool SpecFile::load_from_cnf( std::istream &input )
{
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  reset();

  if( !input.good() )
    return false;

  // read the file
  try
  {
    const istream::pos_type orig_pos = input.tellg();
    input.seekg( 0, ios::end );
    const istream::pos_type eof_pos = input.tellg();
    input.seekg( 0, ios::beg );
    
    const size_t size = static_cast<size_t>( 0 + eof_pos - orig_pos );
    std::vector<char> file_bits(size);
    input.read(reinterpret_cast<char*>(file_bits.data()), size);

    //create tge camio object and send it the bits
    CAMInputOutput::CAMIO cam;
    cam.ReadFile(reinterpret_cast<const std::vector<byte_type>&>(file_bits));
    
    load_cnf_using_reader( cam );
    
    cleanup_after_load();
  }catch ( std::exception &e )
  {
    cerr << "Failed CNF: " << e.what() << endl;
    input.clear();
    //input.seekg( orig_pos, ios::beg );
    
    reset();
    return false;
  }//try / catch to read the file
  
  return true;
}//bool load_from_cnf( std::istream &input )


bool SpecFile::write_cnf( std::ostream &output, std::set<int> sample_nums,
                          const std::set<int> &det_nums ) const
{
  //First, lets take care of some boilerplate code.
  std::unique_lock<std::recursive_mutex> scoped_lock(mutex_);

  for( const auto sample : sample_nums )
  {
    if( !sample_numbers_.count(sample) )
      throw runtime_error( "write_cnf: invalid sample number (" + to_string(sample) + ")" );
  }
  
  if( sample_nums.empty() )
    sample_nums = sample_numbers_;
  
  vector<string> det_names;
  for( const int num : det_nums )
  {
    auto pos = std::find( begin(detector_numbers_), end(detector_numbers_), num );
    if( pos == end(detector_numbers_) )
      throw runtime_error( "write_cnf: invalid detector number (" + to_string(num) + ")" );
    det_names.push_back( detector_names_[pos-begin(detector_numbers_)] );
  }
  
  if( det_nums.empty() )
    det_names = detector_names_;
  
  try
  {
    std::shared_ptr<Measurement> summed = sum_measurements(sample_nums, det_names, nullptr);

    if( !summed || !summed->gamma_counts() || summed->gamma_counts()->empty() )
      return false;

    //call CAMIO here
    CAMInputOutput::CAMIO* cam = new CAMInputOutput::CAMIO();

    //At this point we have the one spectrum (called summed) that we will write
    //  to the CNF file.  If the input file only had a single spectrum, this is
    //  now held in 'summed', otherwise the specified samples and detectors have
    //  all been summed together/

    //Gamma information
    const float real_time = summed->real_time();
    const float live_time = summed->live_time();
    const vector<float> gamma_channel_counts = *summed->gamma_counts();

    //CNF files use polynomial energy calibration, so if the input didnt also
    //  use polynomial, we will convert to polynomial, or in the case of
    //  lower channel or invalid, just clear the coefficients.
    vector<float> energy_cal_coeffs = summed->calibration_coeffs();
    switch (summed->energy_calibration_model())
    {
      case EnergyCalType::Polynomial:
      case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
          //Energy calibration already polynomial, no need to do anything
      break;

      case EnergyCalType::FullRangeFraction:
        //Convert to polynomial
        energy_cal_coeffs = fullrangefraction_coef_to_polynomial(energy_cal_coeffs, gamma_channel_counts.size());
      break;

      case EnergyCalType::LowerChannelEdge:
      case EnergyCalType::InvalidEquationType:
        //No hope of converting energy calibration to the polynomial needed by CNF files.
        energy_cal_coeffs.clear();
      break;
    }//switch( energy cal type coefficients are in )

  
    /// \TODO: Check if neutron counts are supported in CNF files.
    //Neutron information:
    //const double sum_neutrons = summed->neutron_counts_sum();
        
    //With short measurements or handheld detectors we may not have had any
    //  neutron counts, but there was a detector, so lets check if the input
    //  file had information about neutrons.
    //const bool had_neutrons = summed->contained_neutron();


    //Measurement start time.
    //The start time may not be valid (e.g., if input file didnt have times),
    // but if we're here we time is valid, just the unix epoch
    const time_point_t start_time = SpecUtils::is_special(summed->start_time())
                                    ? time_point_t{}
                                    : summed->start_time();

    // Check if we have RIID analysis results we could write to the output file.
    // TODO: implement writing RIID analysis resukts to output file.
         
    //if (detectors_analysis_ && !detectors_analysis_->is_empty())
    //{
    //  // See DetectorAnalysis class for details; its a little iffy what
    //  //  information from the original file makes it into the DetectorAnalysis.

    //  const DetectorAnalysis& ana = *detectors_analysis_;
    //  //ana.algorithm_result_description_
    //  //ana.remarks_
    //  //...

    //  //Loop over individual results, usually different nuclides or sources.
    //  for (const DetectorAnalysisResult& nucres : ana.results_)
    //  {
    //      // This requires a lot of changes to camio and it really doesn't
    //      // buy a whole lot. 
    //      CAMInputOutput::Nuclide nuc;
    //      nuc.Name = nucres.nuclide_;
    //      nuc.Activity = nucres.activity_ * 1 / 37000; //convert to uCi
    //  
    //      cam->AddNuclide(nuc);
    //  }//for( loop over nuclides identified )
    // 
    //}//if( we have riid results from input file )
    
      
    
        string title = summed->title(); //CTITLE
        if (!title.empty()) 
        {
            std::string expectsString(title.begin(), title.end());
            expectsString.resize( 0x20, '\0');
            //enter_CAM_value(expectsString, cnf_file, acqp_loc);
            cam->AddSampleTitle(expectsString);
        }
        
 //TODO: implement converted shape calibration information into CNF files 
        //shape calibration, just use the default values for NaI detectors if the type cotains any NaI, if not use Ge defaults
        const string& detector_type = summed->detector_type();
        cam->AddDetectorType(detector_type);

        //energy calibration
        cam->AddEnergyCalibration(energy_cal_coeffs);

        //times
        if (!SpecUtils::is_special(start_time)) {
            cam->AddAcquitionTime(start_time);

        }
        cam->AddLiveTime(live_time);
        cam->AddRealTime(real_time);

        // add the gps info
        if (summed->has_gps_info()) 
        {
            if(!SpecUtils::is_special(summed->position_time()))
                cam->AddGPSData(summed->latitude(), summed->longitude(), summed->speed(), summed->position_time());
            else
                cam->AddGPSData(summed->latitude(), summed->longitude(), summed->speed()); // if there is no position time stamp
        }
        //enter the data 
        cam->AddSpectrum(gamma_channel_counts);

        auto& cnf_file = cam->CreateFile();
        //write the file
         output.write((char* )cnf_file.data(), cnf_file.size());
    }catch( std::exception &e )
    {
#if( PERFORM_DEVELOPER_CHECKS )
      //Print out why we failed for debug purposes.
      log_developer_error( __func__, ("Failed to write CNF file: " + string(e.what())).c_str() );
#endif
      return false;
    }//try / catch
  
    return true;
}//write_cnf

}//namespace SpecUtils



