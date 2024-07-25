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

#if( USE_D3_EXPORTING )
#include "D3SupportFiles.h"
#endif //#if( USE_D3_EXPORTING )

#include <cmath>
#include <ctime>
#include <cctype>
#include <chrono>
#include <limits>
#include <locale>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <iterator>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <sys/stat.h>

#include <boost/functional/hash.hpp>

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_utils.hpp"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/Filesystem.h"
#include "SpecUtils/SpecUtilsAsync.h"
#include "SpecUtils/RapidXmlUtils.hpp"
#include "SpecUtils/SpecFile_location.h"
#include "SpecUtils/EnergyCalibration.h"
#include "SpecUtils/SerialToDetectorModel.h"

#if( SpecUtils_ENABLE_D3_CHART )
#include "SpecUtils/D3SpectrumExport.h"
#endif

#if( SpecUtils_ENABLE_URI_SPECTRA )
#include "SpecUtils/UriSpectrum.h"
#endif


#if( SpecUtils_USE_SIMD )
// Should move to using a library for SIMD operations; some potential candidate libraries include:
//   https://github.com/p12tic/libsimdpp (boost license) #include <simdpp/simd.h>
//   https://github.com/VcDevel/Vc (BSD 3 Clause)
//   https://github.com/xtensor-stack/xsimd (BSD 3 Clause)
//   https://github.com/simd-everywhere/simde (MIT)
//
#if( defined(__x86_64__) || defined(__i386__) )
#include <immintrin.h>
#else
static_assert( 0, "SpecUtils_USE_SIMD is currently only enabled for i386/x86" );
#endif
#endif //SpecUtils_USE_SIMD


static_assert( RAPIDXML_USE_SIZED_INPUT_WCJOHNS == 1,
              "The modified version of RapidXml is somehow not being used to compile SpecFile" );


#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#if( defined(_MSC_VER) && _MSC_VER <= 1700 )
#define SRC_LOCATION (std::string("File " TOSTRING(__FILE__) ": Function '") \
        +  std::string(__FUNCTION__) \
        + std::string( "': Line " TOSTRING(__LINE__)))
#else
#define SRC_LOCATION (std::string("File " TOSTRING(__FILE__) ": Function '") \
        + std::string(__func__) \
        + std::string( "': Line " TOSTRING(__LINE__)))
#endif

#if( defined(WIN32) )
#undef min
#undef max
#endif 

using namespace std;


using SpecUtils::trim;
using SpecUtils::split;
using SpecUtils::iequals_ascii;
using SpecUtils::to_lower_ascii;
using SpecUtils::to_upper_ascii;
using SpecUtils::contains;
using SpecUtils::icontains;
using SpecUtils::starts_with;
using SpecUtils::istarts_with;
using SpecUtils::ireplace_all;
using SpecUtils::convert_from_utf8_to_utf16;
using SpecUtils::time_duration_string_to_seconds;

using SpecUtils::time_from_string;

//For references in comments similar to 'refDXMA3HSRA6', see
//  documentation/comment_refernce_to_ouo_source.txt for source file information

//If the SpecFile and Measurement equal_enough functions should require remarks
//  and parse warnings to match - useful to disable when parsing significantly
//  changes so everything besides remarks and parser comments can be validated;
//  Differences will still be printed out, so can be manually inspected to make
//  sure they are as expected.
#define REQUIRE_REMARKS_COMPARE 1

#if( !REQUIRE_REMARKS_COMPARE )
#warning "Not requiring remarks and parse warnings to compare - this should be only for temporary development only."
#endif


namespace
{
  bool toInt( const std::string &str, int &f )
  {
    const int nconvert = sscanf( str.c_str(), "%i", &f );
    return (nconvert == 1);
  }
  
  
  /** Adds the vector of 'input' float vectors, to results.
      The result will be resized to larges input float vector size.
   */
  void add_to( vector<float> &results,
              const vector< std::shared_ptr<const vector<float> > > &input )
  {
    results.clear();
    if( input.empty() )
      return;
    
    size_t max_size = 0;
    for( const auto &currarayptr : input )
      max_size = std::max( max_size, currarayptr->size() );
    
    results.resize( max_size, 0.0f );
    
    for( const auto &currarayptr : input )
    {
      const vector<float> &curraray = (*currarayptr);
      
      const size_t size = curraray.size();
      if( size > results.size() )  //should never evaluate to true, but JIC
        results.resize( size, 0.0f );
      
#if( SpecUtils_USE_SIMD )
      const size_t aligendN = size - (size % 4);
      for( size_t i = 0; i < aligendN; i += 4 )
      {
        _mm_storeu_ps(&results[i],
                      _mm_add_ps(_mm_loadu_ps(&results[i]),
                                 _mm_loadu_ps(&curraray[i])));
      }
      
      for( size_t i = aligendN; i < size; ++i )
        results[i] += curraray[i];
#else
      for( size_t i = 0; i < size; ++i )
        results[i] += curraray[i];
#endif
    }
  }//add_to(...)
  
  
  void sum_with_rebin( vector<float> &results,
                      const std::shared_ptr<const SpecUtils::Measurement> &binning,
                      const vector<std::shared_ptr<const SpecUtils::Measurement> > &datas )
  {
    assert( !!binning );
    
    const shared_ptr<const SpecUtils::EnergyCalibration> wantedcal = binning->energy_calibration();
    assert( wantedcal );
    const shared_ptr<const std::vector<float>> &wantedenergies = wantedcal->channel_energies();
    
    const size_t nbin = wantedcal->num_channels();
    if( results.size() < nbin )
      results.resize( nbin, 0.0f );
    
    assert( !!wantedenergies );
    
    for( size_t i = 0; i < datas.size(); ++i )
    {
      const std::shared_ptr<const SpecUtils::Measurement> &d = datas[i];
      const shared_ptr<const SpecUtils::EnergyCalibration> datacal = d->energy_calibration();
      assert( datacal );
      const std::shared_ptr<const std::vector<float>> &dataenergies = datacal->channel_energies();
      const std::shared_ptr<const std::vector<float>> &channel_counts = d->gamma_counts();
      
      if( !dataenergies || !channel_counts )
      {
        cerr << "sum_with_rebin(...): found spectrum with no bin" << endl;
        continue;
      }//if( !dataenergies )
      
      if( datacal == wantedcal )  //|| (*datacal) == (*wantedcal) )
      {
        assert( results.size() == channel_counts->size() );
        
        for( size_t j = 0; j < nbin; ++j )
          results[j] += (*channel_counts)[j];
      }else if( channel_counts->size() > 3 )
      {
        vector<float> resulting_counts;
        SpecUtils::rebin_by_lower_edge( *dataenergies, *channel_counts,
                            *wantedenergies, resulting_counts );
        
        assert( ((nbin+1) == wantedenergies->size()) || (nbin == wantedenergies->size()) );
        assert( ((resulting_counts.size()+1) == wantedenergies->size())
                 || (resulting_counts.size() == wantedenergies->size()) );
        
        for( size_t j = 0; j < nbin; ++j )
          results[j] += resulting_counts[j];
        
        if( (nbin+1) == resulting_counts.size() )
          results.back() += resulting_counts.back();
      }//if( dataenergies == wantedenergies )
      
    }//for( size_t i = 0; i < datas.size(); ++i )
  }//void sum_with_rebin(...)
  
//Analogous to compare_by_derived_sample_det_time; compares by
// is_derived, sample_number, and then detector_number_, but NOT by start_time_
struct SpecFileLessThan
{
  const int m_sample_number, m_detector_number;
  
  SpecFileLessThan( int sample_num, int det_num )
  : m_sample_number( sample_num ), m_detector_number( det_num )
  {}
  
  bool operator()( const std::shared_ptr<SpecUtils::Measurement>& lhs,
                   const std::shared_ptr<SpecUtils::Measurement> &dummy )
  {
    assert( !dummy );
    if( !lhs )
      return false;
    
    if( lhs->sample_number() == m_sample_number )
      return (lhs->detector_number() < m_detector_number);
    return (lhs->sample_number() < m_sample_number);
  }//operator()
};//struct SpecFileLessThan

//compare_by_derived_sample_det_time: compares by is_derived, sample_number_, and then
//  detector_number_, then by start_time_, then source_type_
bool compare_by_derived_sample_det_time( const std::shared_ptr<const SpecUtils::Measurement> &lhs,
                           const std::shared_ptr<const SpecUtils::Measurement> &rhs )
{
  if( !lhs || !rhs )
    return false;

  const bool lhsNotDerived = !lhs->derived_data_properties();
  const bool rhsNotDerived = !rhs->derived_data_properties();
  if( lhsNotDerived != rhsNotDerived )
    return (lhsNotDerived > rhsNotDerived);
  
  if( lhs->sample_number() != rhs->sample_number() )
    return (lhs->sample_number() < rhs->sample_number());

  if( lhs->detector_number() != rhs->detector_number() )
    return (lhs->detector_number() < rhs->detector_number());

  if( lhs->start_time() != rhs->start_time() )
    return (lhs->start_time() < rhs->start_time());
  
  return (lhs->source_type() < rhs->source_type());
}//compare_by_derived_sample_det_time(...)

}//anonymous namespace






#if(PERFORM_DEVELOPER_CHECKS)
void log_developer_error( const char *location, const char *error )
{
  static std::recursive_mutex s_dev_error_log_mutex;
  static ofstream s_dev_error_log( "developer_errors.log", ios::app | ios::out );
  
  std::unique_lock<std::recursive_mutex> loc( s_dev_error_log_mutex );
  
  const SpecUtils::time_point_t time = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now());
  
  const string timestr = SpecUtils::to_iso_string(time);
//  const string timestr = SpecUtils::to_iso_string( time );
  
  s_dev_error_log << timestr << ": " << location << endl << error << "\n\n" << endl;
  cerr << timestr << ": " << location << endl << error << "\n\n" << endl;
}//void log_developer_error( const char *location, const char *error )
#endif //#if(PERFORM_DEVELOPER_CHECKS)

namespace SpecUtils
{
    
  //implementation of inlined functions
bool SpecFile::modified() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return modified_;
}
  
  
void SpecFile::reset_modified()
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  modified_ = false;
}

void SpecFile::reset_modified_since_decode()
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  modifiedSinceDecode_ = false;
}

bool SpecFile::modified_since_decode() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return modifiedSinceDecode_;
}

float SpecFile::gamma_live_time() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return gamma_live_time_;
}

float SpecFile::gamma_real_time() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return gamma_real_time_;
}

double SpecFile::gamma_count_sum() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return gamma_count_sum_;
}

double SpecFile::neutron_counts_sum() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return neutron_counts_sum_;
}

const std::string &SpecFile::filename() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return filename_;
}

const std::vector<std::string> &SpecFile::detector_names() const
{
  return detector_names_;
}

const std::vector<int> &SpecFile::detector_numbers() const
{
  return detector_numbers_;
}

const std::vector<std::string> &SpecFile::gamma_detector_names() const
{
  return gamma_detector_names_;
}

const std::vector<std::string> &SpecFile::neutron_detector_names() const
{
  return neutron_detector_names_;
}

const std::string &SpecFile::uuid() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return uuid_;
}

const std::vector<std::string> &SpecFile::remarks() const
{
  return remarks_;
}

const std::vector<std::string> &SpecFile::parse_warnings() const
{
  return parse_warnings_;
}

int SpecFile::lane_number() const
{
  return lane_number_;
}

const std::string &SpecFile::measurement_location_name() const
{
  return measurement_location_name_;
}

const std::string &SpecFile::inspection() const
{
  return inspection_;
}

double Measurement::latitude() const
{
  if( !location_ || !(location_->geo_location_) || IsNan(location_->geo_location_->latitude_) )
    return -999.9;
  return location_->geo_location_->latitude_;
}

double Measurement::longitude() const
{
  if( !location_ || !(location_->geo_location_) || IsNan(location_->geo_location_->longitude_) )
    return -999.9;
  return location_->geo_location_->longitude_;
}


const time_point_t Measurement::position_time() const
{
  if( !location_ || !(location_->geo_location_) )
    return time_point_t{};
  
  return location_->geo_location_->position_time_;
}

const std::string &SpecFile::measurement_operator() const
{
  return measurement_operator_;
}

const std::set<int> &SpecFile::sample_numbers() const
{
  //  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  //  set<int> answer;
  //  fore( const std::shared_ptr<Measurement> &meas : measurements_ )
  //    answer.insert( meas->sample_number_ );
  //  return answer;
  return sample_numbers_;
}//set<int> sample_numbers() const


size_t SpecFile::num_measurements() const
{
  size_t n;
  
  {
    std::unique_lock<std::recursive_mutex> lock( mutex_ );
    n = measurements_.size();
  }
  
  return n;
}//size_t num_measurements() const


std::shared_ptr<const Measurement> SpecFile::measurement(
                                                         size_t num ) const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  const size_t n = measurements_.size();
  
  if( num >= n )
    throw std::runtime_error( "SpecFile::measurement(size_t): invalid index" );
  
  return measurements_[num];
}


DetectorType SpecFile::detector_type() const
{
  return detector_type_;
}

const std::string &SpecFile::instrument_type() const
{
  return instrument_type_;
}

const std::string &SpecFile::manufacturer() const
{
  return manufacturer_;
}

const std::string &SpecFile::instrument_model() const
{
  return instrument_model_;
}

const std::string &SpecFile::instrument_id() const
{
  return instrument_id_;
}

std::vector< std::shared_ptr<const Measurement> > SpecFile::measurements() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  
  std::vector< std::shared_ptr<const Measurement> > answer;
  for( size_t i = 0; i < measurements_.size(); ++i )
    answer.push_back( measurements_[i] );
  return answer;
}//std::vector< std::shared_ptr<const Measurement> > measurements() const


std::shared_ptr<const DetectorAnalysis> SpecFile::detectors_analysis() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return detectors_analysis_;
}


const vector<shared_ptr<const MultimediaData>> &SpecFile::multimedia_data() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return multimedia_data_;
}


double SpecFile::mean_latitude() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return mean_latitude_;
}

double SpecFile::mean_longitude() const
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  return mean_longitude_;
}

void SpecFile::set_filename( const std::string &n )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  filename_ = n;
  modified_ = modifiedSinceDecode_ = true;
}

void SpecFile::set_remarks( const std::vector<std::string> &n )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  remarks_ = n;
  modified_ = modifiedSinceDecode_ = true;
}


void SpecFile::set_parse_warnings( const std::vector<std::string> &warnings )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  parse_warnings_ = warnings;
  modified_ = modifiedSinceDecode_ = true;
}


void SpecFile::set_uuid( const std::string &n )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  uuid_ = n;
  modified_ = modifiedSinceDecode_ = true;
}

void SpecFile::set_lane_number( const int num )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  lane_number_ = num;
  modified_ = modifiedSinceDecode_ = true;
}

void SpecFile::set_measurement_location_name( const std::string &n )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  measurement_location_name_ = n;
  modified_ = modifiedSinceDecode_ = true;
}

void SpecFile::set_inspection( const std::string &n )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  inspection_ = n;
  modified_ = modifiedSinceDecode_ = true;
}

void SpecFile::set_instrument_type( const std::string &n )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  instrument_type_ = n;
  modified_ = modifiedSinceDecode_ = true;
}

void SpecFile::set_detector_type( const DetectorType type )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  detector_type_ = type;
  modified_ = modifiedSinceDecode_ = true;
}

void SpecFile::set_manufacturer( const std::string &n )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  manufacturer_ = n;
  modified_ = modifiedSinceDecode_ = true;
}

void SpecFile::set_instrument_model( const std::string &n )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  instrument_model_ = n;
  modified_ = modifiedSinceDecode_ = true;
}

void SpecFile::set_instrument_id( const std::string &n )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  instrument_id_ = n;
  modified_ = modifiedSinceDecode_ = true;
}


//implementation of inline Measurement functions
float Measurement::live_time() const
{
  return live_time_;
}

float Measurement::real_time() const
{
  return real_time_;
}

bool Measurement::contained_neutron() const
{
  return contained_neutron_;
}

int Measurement::sample_number() const
{
  return sample_number_;
}

OccupancyStatus Measurement::occupied() const
{
  return occupied_;
}

double Measurement::gamma_count_sum() const
{
  return gamma_count_sum_;
}
  
float Measurement::neutron_live_time() const
{
  if( !contained_neutron_ )
    return 0.0f;
  
  if( (neutron_live_time_ > 0.0f) && !IsNan(neutron_live_time_) && !IsInf(neutron_live_time_) )
    return neutron_live_time_;
  
  if( (real_time_ <= 0.0f) && (live_time_ > 0.0f) )
    return live_time_;
  
  return real_time_;
}

double Measurement::neutron_counts_sum() const
{
  return neutron_counts_sum_;
}

float Measurement::speed() const
{
  return (!location_ || IsNan(location_->speed_)) ? 0.0f : location_->speed_;
}

float Measurement::dx() const
{
  return (!location_ || !location_->relative_location_ ) ? 0.0f : location_->relative_location_->dx();
}

float Measurement::dy() const
{
  return (!location_ || !location_->relative_location_ ) ? 0.0f : location_->relative_location_->dy();
}

const std::string &Measurement::detector_name() const
{
  return detector_name_;
}

int Measurement::detector_number() const
{
  return detector_number_;
}

const std::string &Measurement::detector_type() const
{
  return detector_description_;
}

SpecUtils::QualityStatus Measurement::quality_status() const
{
  return quality_status_;
}

SourceType Measurement::source_type() const
{
  return source_type_;
}


SpecUtils::EnergyCalType Measurement::energy_calibration_model() const
{
  assert( energy_calibration_ );
  return energy_calibration_->type();
}


const std::vector<std::string> &Measurement::remarks() const
{
  return remarks_;
}

const std::vector<std::string> &Measurement::parse_warnings() const
{
  return parse_warnings_;
}

const time_point_t &Measurement::start_time() const
{
  return start_time_;
}

const time_point_t Measurement::start_time_copy() const
{
  return start_time_;
}

  
const std::vector<float> &Measurement::calibration_coeffs() const
{
  assert( energy_calibration_ );
  return energy_calibration_->coefficients();
}

  
const std::vector<std::pair<float,float>> &Measurement::deviation_pairs() const
{
  assert( energy_calibration_ );
  return energy_calibration_->deviation_pairs();
}
 
std::shared_ptr<const EnergyCalibration> Measurement::energy_calibration() const
{
  assert( energy_calibration_ );
  return energy_calibration_;
}
  
const std::shared_ptr< const std::vector<float> > &Measurement::channel_energies() const
{
  assert( energy_calibration_ );
  return energy_calibration_->channel_energies();
}

  
const std::shared_ptr< const std::vector<float> > &Measurement::gamma_counts() const
{
  return gamma_counts_;
}
  
  
void Measurement::set_start_time( const time_point_t &time )
{
  start_time_ = time;
}

  
void Measurement::set_remarks( const std::vector<std::string> &remar )
{
  remarks_ = remar;
}
  

void SpecFile::add_remark( const std::string &remark )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  remarks_.push_back( remark );
  modified_ = modifiedSinceDecode_ = true;
}

void Measurement::set_parse_warnings(const std::vector<std::string>& warnings)
{
  parse_warnings_ = warnings;
}
  

void Measurement::set_source_type( const SourceType type )
{
  source_type_ = type;
}


void Measurement::set_position( double longitude, double latitude, time_point_t pos_time )
{
  if( !valid_latitude(latitude) && !valid_longitude(longitude) )
  {
    if( !location_ || !(location_->geo_location_) )
      return;
    
    auto loc = make_shared<LocationState>( *location_ );
    loc->geo_location_.reset();
    
    // We could maybe make the position state type default to be position of the instrument
    //loc->type_ = LocationState::StateType::Instrument;
    
    location_ = loc;
    
    // Actually just get rid of location_ if nothing left is valid
    if( IsNan(loc->speed_) && !loc->geo_location_
       && !loc->relative_location_ && !loc->orientation_ )
      location_.reset();

    return;
  }
  
  shared_ptr<LocationState> loc;
  if( location_ )
  {
    loc = make_shared<LocationState>( *location_ );
  }else
  {
    loc = make_shared<LocationState>();
    
    // We will make the position state type default to be position of the instrument
    loc->type_ = LocationState::StateType::Instrument;
  }
  location_ = loc;
  
  shared_ptr<GeographicPoint> geo;
  if( loc->geo_location_ )
    geo = make_shared<GeographicPoint>( *(loc->geo_location_) );
  else
    geo = make_shared<GeographicPoint>();
  
  loc->geo_location_ = geo;
  
  geo->latitude_ = latitude;
  geo->longitude_ = longitude;
  geo->position_time_ = pos_time;
}//set_position(...)

  
void Measurement::set_sample_number( const int samplenum )
{
  sample_number_ = samplenum;
}
  

void Measurement::set_occupancy_status( const OccupancyStatus status )
{
  occupied_ = status;
}


void Measurement::set_detector_name( const std::string &name )
{
  detector_name_ = name;
}


void Measurement::set_detector_number( const int detnum )
{
  detector_number_ = detnum;
}

  
void Measurement::set_gamma_counts( std::shared_ptr<const std::vector<float>> counts,
                                     const float livetime, const float realtime )
{
  live_time_ = livetime;
  real_time_ = realtime;
  gamma_count_sum_ = 0.0;
  
  //const size_t oldnchan = gamma_counts_ ? gamma_counts_->size() : 0u;
  
  if( !counts )
    counts = std::make_shared<std::vector<float>>();
  gamma_counts_ = counts;
  for( const float val : *counts )
    gamma_count_sum_ += val;
  
  assert( energy_calibration_ );
  
  const auto &cal = *energy_calibration_;
  const size_t newnchan = gamma_counts_->size();
  const size_t calnchan = cal.num_channels();
  
  if( (newnchan != calnchan) && (cal.type() != EnergyCalType::LowerChannelEdge) )
  {
    //We could preserve the old coefficients for Polynomial and FRF, and just create a new
    //  calibration... it isnt clear if we should do that, or just clear out the calibration...
    energy_calibration_ = std::make_shared<const SpecUtils::EnergyCalibration>();
  }
}//set_gamma_counts
  
  
void Measurement::set_neutron_counts( const std::vector<float> &counts, const float live_time )
{
  neutron_counts_ = counts;
  neutron_counts_sum_ = 0.0;
  contained_neutron_ = !counts.empty();
  
  if( counts.empty() || (live_time < 0.0f) || IsInf(live_time) || IsNan(live_time) )
    neutron_live_time_ = 0.0f;
  else
    neutron_live_time_ = live_time;
  
  const size_t size = counts.size();
  for( size_t i = 0; i < size; ++i )
    neutron_counts_sum_ += counts[i];
}//void set_neutron_counts( const std::vector<float> &counts )

  
const std::vector<float> &Measurement::neutron_counts() const
{
  return neutron_counts_;
}
  

const std::shared_ptr<const SpecUtils::LocationState> &Measurement::location_state() const
{
  return location_;
}


size_t Measurement::num_gamma_channels() const
{
  if( !gamma_counts_ )
    return 0;
  return gamma_counts_->size();
}
  
  
size_t Measurement::find_gamma_channel( const float x ) const
{
  assert( energy_calibration_ );
  const shared_ptr<const vector<float>> &energies = energy_calibration_->channel_energies();
  if( !energies || (energies->size() < 2) || !gamma_counts_ )
    throw std::runtime_error( "find_gamma_channel: channel energies not defined" );
  
  assert( (gamma_counts_->size()+1) == energies->size() );
  
  //Using upper_bound instead of lower_bound to properly handle the case
  //  where x == bin lower energy.
  const auto pos_iter = std::upper_bound( energies->begin(), energies->end(), x );
  if( pos_iter == begin(*energies) )
    return 0;
  
  const size_t last_channel = gamma_counts_->size() - 1;
  const size_t pos_index = (pos_iter - begin(*energies)) - 1;
  
  return std::min( pos_index, last_channel );
}//size_t find_gamma_channel( const float energy ) const
  
  
float Measurement::gamma_channel_content( const size_t channel ) const
{
  if( !gamma_counts_ || channel >= gamma_counts_->size() )
    return 0.0f;
    
  return gamma_counts_->operator[]( channel );
}//float gamma_channel_content( const size_t channel ) const
  
  
float Measurement::gamma_channel_lower( const size_t channel ) const
{
  assert( energy_calibration_ );
  const shared_ptr<const vector<float>> &energies = energy_calibration_->channel_energies();
  
  if( !energies )
    throw std::runtime_error( "gamma_channel_lower: channel energies not defined" );
    
  if( channel >= energies->size() )
    throw std::runtime_error( "gamma_channel_lower: requesting larger channel ("
                             + std::to_string(channel) + ") than available ("
                             + std::to_string(energies->size()) + ")" );
  
  return energies->operator[]( channel );
}//float gamma_channel_lower( const size_t channel ) const
  
  
float Measurement::gamma_channel_center( const size_t channel ) const
{
  return gamma_channel_lower( channel ) + 0.5f*gamma_channel_width( channel );
}//float gamma_channel_center( const size_t channel ) const
  
  
float Measurement::gamma_channel_upper( const size_t channel ) const
{
  assert( energy_calibration_ );
  const shared_ptr<const vector<float>> &energies = energy_calibration_->channel_energies();
  
  if( !energies || (energies->size() < 2) || ((channel+1) >= energies->size()) )
    throw std::runtime_error( "gamma_channel_upper: channel energies not defined" );
  
  if( (channel+1) >= energies->size() )
    throw std::runtime_error( "gamma_channel_upper: requesting larger channel ("
                             + std::to_string(channel) + ") than available ("
                             + std::to_string(energies->size()) + ")" );
  
  return (*energies)[channel+1];
}//float gamma_channel_upper( const size_t channel ) const
  
  
const std::shared_ptr< const std::vector<float> > &Measurement::gamma_channel_energies() const
{
  assert( energy_calibration_ );
  return energy_calibration_->channel_energies();
}
  
  
const std::shared_ptr< const std::vector<float> > &Measurement::gamma_channel_contents() const
{
  return gamma_counts_;
}
  
  
float Measurement::gamma_channel_width( const size_t channel ) const
{
  assert( energy_calibration_ );
  const shared_ptr<const vector<float>> &energies = energy_calibration_->channel_energies();
  
  if( !energies || (energies->size() < 2) || ((channel+1) >= energies->size()) )
    throw std::runtime_error( "gamma_channel_width: channel energies not defined" );
  
  return (*energies)[channel+1] - (*energies)[channel];
}//float gamma_channel_width( const size_t channel ) const
  
  
const std::string &Measurement::title() const
{
  return title_;
}
  
  
void Measurement::set_title( const std::string &title )
{
  title_ = title;
}
  
  
float Measurement::gamma_energy_min() const
{
  assert( energy_calibration_ );
  const shared_ptr<const vector<float>> &energies = energy_calibration_->channel_energies();
  
  if( !energies || energies->empty() )
    return 0.0f;
  return energies->front();
}

  
float Measurement::gamma_energy_max() const
{
  assert( energy_calibration_ );
  const shared_ptr<const vector<float>> &energies = energy_calibration_->channel_energies();
  
  if( !energies || energies->empty() )
    return 0.0f;
  
  return energies->back();
}


uint32_t Measurement::derived_data_properties() const
{
  return derived_data_properties_;
}


double gamma_integral( const std::shared_ptr<const Measurement> &hist,
                 const float minEnergy, const float maxEnergy )
{
  if( !hist )
    return 0.0;
  
  const double gamma_sum = hist->gamma_integral( minEnergy, maxEnergy );

#if( PERFORM_DEVELOPER_CHECKS )
  /*
  double check_sum = 0.0;
  
  const int lowBin = hist->FindFixBin( minEnergy );
  int highBin = hist->FindFixBin( maxEnergy );
  if( highBin > hist->GetNbinsX() )
    highBin = hist->GetNbinsX();
  
  if( lowBin == highBin )
  {
    const float binWidth = hist->GetBinWidth( lowBin );
    const double frac = (maxEnergy - minEnergy) / binWidth;
    check_sum = frac * hist->GetBinContent( lowBin );
  }else
  {
    if( lowBin > 0 )
    {
      const float lowerLowEdge = hist->GetBinLowEdge( lowBin );
      const float lowerBinWidth = hist->GetBinWidth( lowBin );
      const float lowerUpEdge = lowerLowEdge + lowerBinWidth;
    
      double fracLowBin = 1.0;
      if( minEnergy > lowerLowEdge )
        fracLowBin = (lowerUpEdge - minEnergy) / lowerBinWidth;
    
      check_sum += fracLowBin * hist->GetBinContent( lowBin );
    }//if( lowBin > 0 )
  
    if( highBin > 0 )
    {
      const float upperLowEdge = hist->GetBinLowEdge( highBin );
      const float upperBinWidth = hist->GetBinWidth( highBin );
      const float upperUpEdge = upperLowEdge + upperBinWidth;
    
      double fracUpBin = 1.0;
      if( maxEnergy < upperUpEdge )
        fracUpBin  = (maxEnergy - upperLowEdge) / upperBinWidth;
    
      check_sum += fracUpBin * hist->GetBinContent( highBin );
    }//if( highBin > 0 && lowBin!=highBin )
  
    for( int bin = lowBin + 1; bin < highBin; ++bin )
      check_sum += hist->GetBinContent( bin );
  }//if( lowBin == highBin ) / else
  
  
  if( check_sum != gamma_sum && !IsNan(check_sum) && !IsInf(check_sum)
      && lowBin != highBin )
  {
    char buffer[512];
    snprintf( buffer, sizeof(buffer),
              "Gamma Integral using new method varied from old methodology;"
              " %f (old) vs %f (new); lowBin=%i, highBin=%i" ,
              check_sum, gamma_sum, lowBin, highBin );
    log_developer_error( __func__, buffer );
  }//if( check_sum != gamma_sum )
   */
#endif  //#if( PERFORM_DEVELOPER_CHECKS )

  return gamma_sum;
}//float integral(...)



double Measurement::gamma_integral( float lowerx, float upperx ) const
{
  const auto &channel_energies = energy_calibration_->channel_energies();
      
  if( !channel_energies || !gamma_counts_
      || (channel_energies->size() < 2)
      || (gamma_counts_->size() < 2) )
  {
    return 0.0;
  }

  assert( (gamma_counts_->size() + 1) == channel_energies->size() );
  
  double sum = 0.0;
  const vector<float> &x = *channel_energies;
  const vector<float> &y = *gamma_counts_;
  
  const size_t nchannel = x.size();
  const float maxX = x[nchannel-1];
  
  lowerx = std::min( maxX, std::max( lowerx, x[0] ) );
  upperx = std::max( x[0], std::min( upperx, maxX ) );
  
  if( lowerx == upperx )
    return sum;
  
  if( lowerx > upperx )
    std::swap( lowerx, upperx );
 
  //need to account for edgecase of incase x.size() != y.size()
  const size_t maxchannel = gamma_counts_->size() - 1;
  const size_t lowerChannel = min( find_gamma_channel( lowerx ), maxchannel );
  const size_t upperChannel = min( find_gamma_channel( upperx ), maxchannel );
  
  const float lowerLowEdge = x[lowerChannel];
  const float lowerBinWidth = (lowerChannel < (nchannel-1))
                                      ? x[lowerChannel+1] - x[lowerChannel]
                                      : x[lowerChannel]   - x[lowerChannel-1];
  const float lowerUpEdge = lowerLowEdge + lowerBinWidth;
  
  if( lowerChannel == upperChannel )
  {
    const double frac = (upperx - lowerx) / lowerBinWidth;
    return frac * y[lowerChannel];
  }
  
  const double fracLowBin = (lowerUpEdge - lowerx) / lowerBinWidth;
  sum += fracLowBin * y[lowerChannel];
  
  const float upperLowEdge = x[upperChannel];
  const float upperBinWidth = (upperChannel < (nchannel-1))
                                      ? x[upperChannel+1] - x[upperChannel]
                                      : x[upperChannel]   - x[upperChannel-1];
//  const float upperUpEdge = upperLowEdge + upperBinWidth;
  const double fracUpBin  = (upperx - upperLowEdge) / upperBinWidth;
  sum += fracUpBin * y[upperChannel];

  for( size_t channel = lowerChannel + 1; channel < upperChannel; ++channel )
    sum += y[channel];
  
  return sum;
}//double gamma_integral( const float lowEnergy, const float upEnergy ) const;


double Measurement::gamma_channels_sum( size_t startbin, size_t endbin ) const
{
  double sum = 0.0;
  if( !gamma_counts_ )
    return sum;
  
  const size_t nchannels = gamma_counts_->size();
  
  if( startbin > endbin )
    std::swap( startbin, endbin );
  
  if( startbin >= nchannels )
    return sum;
  
  endbin = std::min( endbin, nchannels-1 );
  
  for( size_t channel = startbin; channel <= endbin; ++channel )
    sum += (*gamma_counts_)[channel];
  
  return sum;
}//double gamma_channels_sum( size_t startbin, size_t endbin ) const;



const char *descriptionText( const SpecUtils::SpectrumType type )
{
  switch( type )
  {
    case SpecUtils::SpectrumType::Foreground:       return "Foreground";
    case SpecUtils::SpectrumType::SecondForeground: return "Secondary";
    case SpecUtils::SpectrumType::Background:       return "Background";
  }//switch( type )
  
  return "";
}//const char *descriptionText( const SpecUtils::SpectrumType type )


const char *suggestedNameEnding( const SaveSpectrumAsType type )
{
  switch( type )
  {
    case SaveSpectrumAsType::Txt:                return "txt";
    case SaveSpectrumAsType::Csv:                return "csv";
    case SaveSpectrumAsType::Pcf:                return "pcf";
    case SaveSpectrumAsType::N42_2006:           return "n42";
    case SaveSpectrumAsType::N42_2012:           return "n42";
    case SaveSpectrumAsType::Chn:                return "chn";
    case SaveSpectrumAsType::SpcBinaryInt:       return "spc";
    case SaveSpectrumAsType::SpcBinaryFloat:     return "spc";
    case SaveSpectrumAsType::SpcAscii:           return "spc";
    case SaveSpectrumAsType::ExploraniumGr130v0: return "dat";
    case SaveSpectrumAsType::ExploraniumGr135v2: return "dat";
    case SaveSpectrumAsType::SpeIaea:            return "spe";
    case SaveSpectrumAsType::Cnf:                return "cnf";
    case SaveSpectrumAsType::Tka:                return "tka";
#if( SpecUtils_ENABLE_D3_CHART )
    case SaveSpectrumAsType::HtmlD3:             return "html";
#endif
#if( SpecUtils_INJA_TEMPLATES )
    case SaveSpectrumAsType::Template:           return "tmplt";
#endif
#if( SpecUtils_ENABLE_URI_SPECTRA )
    case SaveSpectrumAsType::Uri:                return "uri";
#endif
    case SaveSpectrumAsType::NumTypes:          break;
  }//switch( m_format )
  
  return "";
}//const char *suggestedNameEnding( const SaveSpectrumAsType type )


SpectrumType spectrumTypeFromDescription( const char *descrip )
{
  if( strcmp(descrip,descriptionText(SpecUtils::SpectrumType::Foreground)) == 0 )
    return SpecUtils::SpectrumType::Foreground;
  if( strcmp(descrip,descriptionText(SpecUtils::SpectrumType::SecondForeground)) == 0 )
    return SpecUtils::SpectrumType::SecondForeground;
  if( strcmp(descrip,descriptionText(SpecUtils::SpectrumType::Background)) == 0 )
    return SpecUtils::SpectrumType::Background;

  throw runtime_error( "spectrumTypeFromDescription(...): invalid descrip: "
                        + string(descrip) );
  
  return SpecUtils::SpectrumType::Foreground;
}//SpectrumType spectrumTypeFromDescription( const char *descrip )


const char *descriptionText( const SaveSpectrumAsType type )
{
  switch( type )
  {
    case SaveSpectrumAsType::Txt:                return "TXT";
    case SaveSpectrumAsType::Csv:                return "CSV";
    case SaveSpectrumAsType::Pcf:                return "PCF";
    case SaveSpectrumAsType::N42_2006:           return "2006 N42";
    case SaveSpectrumAsType::N42_2012:           return "2012 N42";
    case SaveSpectrumAsType::Chn:                return "CHN";
    case SaveSpectrumAsType::SpcBinaryInt:       return "Integer SPC";
    case SaveSpectrumAsType::SpcBinaryFloat:     return "Float SPC";
    case SaveSpectrumAsType::SpcAscii:           return "ASCII SPC";
    case SaveSpectrumAsType::ExploraniumGr130v0: return "GR130 DAT";
    case SaveSpectrumAsType::ExploraniumGr135v2: return "GR135v2 DAT";
    case SaveSpectrumAsType::SpeIaea:            return "IAEA SPE";
    case SaveSpectrumAsType::Cnf:                return "CNF";
    case SaveSpectrumAsType::Tka:                return "TKA";
#if( SpecUtils_ENABLE_D3_CHART )
    case SaveSpectrumAsType::HtmlD3:             return "HTML";
#endif
#if( SpecUtils_INJA_TEMPLATES )
    case SaveSpectrumAsType::Template:           return "tmplt";
#endif
#if( SpecUtils_ENABLE_URI_SPECTRA )
    case SaveSpectrumAsType::Uri:                return "URI";
#endif
      
    case SaveSpectrumAsType::NumTypes:          return "";
  }
  return "";
}//const char *descriptionText( const SaveSpectrumAsType type )




const std::string &detectorTypeToString( const DetectorType type )
{
  static const string sm_GR135DetectorStr             = "GR135";
  static const string sm_IdentiFinderDetectorStr      = "IdentiFINDER";
  static const string sm_IdentiFinderNGDetectorStr    = "IdentiFINDER-NG";
  static const string sm_IdentiFinderLaBr3DetectorStr = "IdentiFINDER-LaBr3";
  static const string sm_IdentiFinderTungstenStr      = "IdentiFINDER-T";
  static const string sm_IdentiFinderR425NaI          = "IdentiFinder-R425-NaI";
  static const string sm_IdentiFinderR425LaBr         = "IdentiFinder-R425-LaBr";
  static const string sm_IdentiFinderR500NaIStr       = "IdentiFINDER-R500-NaI";
  static const string sm_IdentiFinderR500LaBrStr      = "IdentiFINDER-R500-LaBr3";
  static const string sm_IdentiFinderUnknownStr       = "IdentiFINDER-Unknown";
  static const string sm_DetectiveDetectorStr         = "Detective";
  static const string sm_DetectiveExDetectorStr       = "Detective-EX";
  static const string sm_DetectiveEx100DetectorStr    = "Detective-EX100";
  static const string sm_OrtecIDMPortalDetectorStr    = "Detective-EX200";
  static const string sm_OrtecDetectiveXStr           = "Detective X";
  static const string sm_SAIC8DetectorStr             = "SAIC8";
  static const string sm_Falcon5kDetectorStr          = "Falcon 5000";
  static const string sm_UnknownDetectorStr           = "Unknown";
  static const string sm_MicroDetectiveDetectorStr    = "MicroDetective";
  static const string sm_MicroRaiderDetectorStr       = "MicroRaider";
  static const string sm_RadiaCodeDetectorStr         = "RadiaCode-102";
  static const string sm_InterceptorStr               = "Interceptor";
  static const string sm_Sam940DetectorStr            = "SAM940";
  static const string sm_Sam940Labr3DetectorStr       = "SAM940LaBr3";
  static const string sm_Sam945DetectorStr            = "SAM945";
  static const string sm_Srpm210DetectorStr           = "SRPM-210";
  static const string sm_RIIDEyeNaIStr                = "RIIDEye-NaI";
  static const string sm_RIIDEyeLaBrStr               = "RIIDEye-LaBr3";
  static const string sm_Rsi701DetectorStr            = "RS-701";
  static const string sm_Rsi705DetectorStr            = "RS-705";
  static const string sm_RadHunterNaIDetectorStr      = "RadHunterNaI";
  static const string sm_RadHunterLaBr3DetectorStr    = "RadHunterLaBr3";
  static const string sm_AvidRsiDetectorStr           = "RSI-Unspecified";
  static const string sm_RadEagleNaiDetectorStr       = "RadEagle NaI 3x1";
  static const string sm_RadEagleCeBr2InDetectorStr   = "RadEagle CeBr3 2x1";
  static const string sm_RadEagleCeBr3InDetectorStr   = "RadEagle CeBr3 3x0.8";
  static const string sm_RadEagleLaBrDetectorStr      = "RadEagle LaBr3 2x1";
  static const string sm_RadSeekerNaIStr              = "RadSeeker-CS";
  static const string sm_RadSeekerLaBrStr             = "RadSeeker-CL";
  static const string sm_VerifinderNaI                = "Verifinder-NaI";
  static const string sm_VerifinderLaBr               = "Verifinder-LaBr";
  static const string sm_KromekD3S                    = "Kromek D3S";
  static const string sm_Fulcrum                      = "Fulcrum";
  static const string sm_Fulcrum40h                   = "Fulcrum-40h";
  static const string sm_Sam950                       = "Sam-950";
  
  
//  GN3, InSpector 1000 LaBr3, Pager-S, SAM-Eagle-LaBr, GR130, SAM-Eagle-NaI-3x3
//  InSpector 1000 NaI, RadPack, SpiR-ID LaBr3, Interceptor, Radseeker, SpiR-ID NaI
//  GR135Plus, LRM, Raider, HRM, LaBr3PNNL, Transpec, Falcon 5000, Ranger, RadiaCode-102
//  MicroDetective, FieldSpec, IdentiFINDER-NG, SAM-935, NaI 3x3, SAM-Eagle-LaBr3

  switch( type )
  {
    case DetectorType::Exploranium:
      return sm_GR135DetectorStr;
    case DetectorType::IdentiFinderNG:
      return sm_IdentiFinderNGDetectorStr;
//  IdentiFinderNG,   //I dont have any examples of this
    case DetectorType::IdentiFinder:
      return sm_IdentiFinderDetectorStr;
    case DetectorType::IdentiFinderLaBr3:
      return sm_IdentiFinderLaBr3DetectorStr;
    case DetectorType::IdentiFinderTungsten:
      return sm_IdentiFinderTungstenStr;
    case DetectorType::IdentiFinderR500NaI:
      return sm_IdentiFinderR500NaIStr;
    case DetectorType::IdentiFinderR425NaI:
      return sm_IdentiFinderR425NaI;
    case DetectorType::IdentiFinderR425LaBr:
      return sm_IdentiFinderR425LaBr;
    case DetectorType::IdentiFinderR500LaBr:
      return sm_IdentiFinderR500LaBrStr;
    case DetectorType::IdentiFinderUnknown:
      return sm_IdentiFinderUnknownStr;
    case DetectorType::DetectiveUnknown:
      return sm_DetectiveDetectorStr;
    case DetectorType::DetectiveEx:
      return sm_DetectiveExDetectorStr;
    case DetectorType::DetectiveEx100:
      return sm_DetectiveEx100DetectorStr;
    case DetectorType::DetectiveEx200:
      return sm_OrtecIDMPortalDetectorStr;
    case DetectorType::DetectiveX:
      return sm_OrtecDetectiveXStr;
    case DetectorType::SAIC8:
      return sm_SAIC8DetectorStr;
    case DetectorType::Falcon5000:
      return sm_Falcon5kDetectorStr;
    case DetectorType::Unknown:
      return sm_UnknownDetectorStr;
    case DetectorType::MicroDetective:
      return sm_MicroDetectiveDetectorStr;
    case DetectorType::MicroRaider:
      return sm_MicroRaiderDetectorStr;
    case DetectorType::RadiaCode:
      return sm_RadiaCodeDetectorStr;
    case DetectorType::Interceptor:
      return sm_InterceptorStr;
    case DetectorType::Sam940:
      return sm_Sam940DetectorStr;
    case DetectorType::Sam945:
      return sm_Sam945DetectorStr;
    case DetectorType::Srpm210:
      return sm_Srpm210DetectorStr;
    case DetectorType::RIIDEyeNaI:
      return sm_RIIDEyeNaIStr;
    case DetectorType::RIIDEyeLaBr:
      return sm_RIIDEyeLaBrStr;
    case DetectorType::Sam940LaBr3:
      return sm_Sam940Labr3DetectorStr;
    case DetectorType::Rsi701:
      return sm_Rsi701DetectorStr;
    case DetectorType::RadHunterNaI:
      return sm_RadHunterNaIDetectorStr;
    case DetectorType::RadHunterLaBr3:
      return sm_RadHunterLaBr3DetectorStr;
    case DetectorType::Rsi705:
      return sm_Rsi705DetectorStr;
    case DetectorType::AvidRsi:
      return sm_AvidRsiDetectorStr;
    case DetectorType::OrtecRadEagleNai:
      return sm_RadEagleNaiDetectorStr;
    case DetectorType::OrtecRadEagleCeBr2Inch:
      return sm_RadEagleCeBr2InDetectorStr;
    case DetectorType::OrtecRadEagleCeBr3Inch:
      return sm_RadEagleCeBr3InDetectorStr;
    case DetectorType::OrtecRadEagleLaBr:
      return sm_RadEagleLaBrDetectorStr;
    case DetectorType::RadSeekerNaI:
      return sm_RadSeekerNaIStr;
    case DetectorType::RadSeekerLaBr:
      return sm_RadSeekerLaBrStr;
    case DetectorType::VerifinderNaI:
      return sm_VerifinderNaI;
    case DetectorType::VerifinderLaBr:
      return sm_VerifinderLaBr;
    case DetectorType::KromekD3S:
      return sm_KromekD3S;
    case DetectorType::Fulcrum:
      return sm_Fulcrum;
    case DetectorType::Fulcrum40h:
      return sm_Fulcrum40h;
    case DetectorType::Sam950:
      return sm_Sam950;
  }//switch( type )

  return sm_UnknownDetectorStr;
}//const std::string &detectorTypeToString( const DetectorType type )


Measurement::Measurement()
{
  reset();
}//Measurement()


size_t Measurement::memmorysize() const
{
  size_t size = sizeof(*this);

  //now we need to take care of all the non-simple objects
  size += detector_name_.capacity()*sizeof(string::value_type);
  size += detector_description_.capacity()*sizeof(string::value_type);

  for( const string &r : remarks_ )
    size += r.capacity()*sizeof(string::value_type);

  size += title_.capacity()*sizeof(string::value_type);

  if( gamma_counts_ )
    size += sizeof(*(gamma_counts_.get())) + gamma_counts_->capacity()*sizeof(float);
  size += neutron_counts_.capacity()*sizeof(float);
      
  size += energy_calibration_->memmorysize();
  
  if( location_ )
    size += location_->memmorysize();
  
  return size;
}//size_t Measurement::memmorysize() const


void Measurement::reset()
{
  live_time_ = 0.0f;
  real_time_ = 0.0f;

  sample_number_ = 1;
  occupied_ = OccupancyStatus::Unknown;
  gamma_count_sum_ = 0.0;
  neutron_live_time_ = 0.0f;
  neutron_counts_sum_ = 0.0;
  
  detector_name_.clear();
  detector_number_ = -1;
  detector_description_.clear();
  quality_status_ = QualityStatus::Missing;

  source_type_       = SourceType::Unknown;

  contained_neutron_ = false;

  remarks_.clear();
  parse_warnings_.clear();
  
  start_time_ = time_point_t{};
  
  energy_calibration_ = std::make_shared<EnergyCalibration>();
  gamma_counts_ = std::make_shared<vector<float> >();  // \TODO: I should test not bothering to place an empty vector in this pointer
  neutron_counts_.clear();
  
  title_.clear();
  
  derived_data_properties_ = 0;
  
  dose_rate_ = exposure_rate_ = -1.0f;
  
  location_.reset();
}//void reset()

  
bool Measurement::has_gps_info() const
{
  return location_
         && location_->geo_location_
         && SpecUtils::valid_longitude(location_->geo_location_->longitude_)
         && SpecUtils::valid_latitude(location_->geo_location_->latitude_);
}
  
  
bool SpecFile::has_gps_info() const
{
  return (SpecUtils::valid_longitude(mean_longitude_)
            && SpecUtils::valid_latitude(mean_latitude_));
}


float Measurement::dose_rate() const
{
  return dose_rate_;
}


float Measurement::exposure_rate() const
{
  return exposure_rate_;
}

  
void Measurement::combine_gamma_channels( const size_t ncombine )
{
  const size_t nchannelorig = gamma_counts_ ? gamma_counts_->size() : size_t(0);
  
  if( (nchannelorig < ncombine) || (ncombine < 2) )
    return;

  assert( energy_calibration_ );
  const auto oldcal = energy_calibration_;
  shared_ptr<EnergyCalibration> newcal;
  if( oldcal && oldcal->valid() && oldcal->num_channels() )
    newcal = energy_cal_combine_channels( *oldcal, ncombine );
  
  if( !newcal )
    newcal = make_shared<EnergyCalibration>();
  
  // We wont just take `nnewchann = newcal->num_channels()` as the calibration could be empty, but
  // the gamma data is not empty.
  const bool evenDivide = !(nchannelorig % ncombine);
  const size_t nnewchann = (nchannelorig / ncombine) + (evenDivide ? 0 : 1);
  
  assert( !newcal->valid() || (newcal->num_channels() == nnewchann) );
  
  // We shouldnt ever encounter the number of channels not matching up; but we'll check, jic, since
  //  its cheap.
  if( newcal->valid() && (newcal->num_channels() != nnewchann) )
  {
#if( PERFORM_DEVELOPER_CHECKS )
    log_developer_error( __func__, "Totally unexpected number of channels logic error" );
#endif
    throw logic_error( "Measurement::combine_gamma_channels: num channels logic error." );
  }
  
#if( PERFORM_DEVELOPER_CHECKS )
  const double pre_gammasum = accumulate( begin(*gamma_counts_), end(*gamma_counts_), double(0.0) );
  const float pre_lower_e = gamma_energy_min();
  const float pre_upper_e = gamma_energy_max();  //(*channel_energies_)[nchannelorig - ncombine];
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
  
  auto newchanneldata = std::make_shared<vector<float> >( nnewchann, 0.0f );
  
  for( size_t i = 0; i < nchannelorig; ++i )
    (*newchanneldata)[i/ncombine] += (*gamma_counts_)[i];
  
  //nchannelorig is std::min( gamma_counts_->size(), channel_energies_->size() )
  //  which practically gamma_counts_->size()==channel_energies_->size(), but
  //  jic
  //for( size_t i = nchannelorig; i < gamma_counts_->size(); ++i )
    //(*newchanneldata)[nnewchann-1] += (*gamma_counts_)[i];
  
  gamma_counts_ = newchanneldata;
  energy_calibration_ = newcal;
  
#if( PERFORM_DEVELOPER_CHECKS )
  const double post_gammasum = accumulate( begin(*gamma_counts_),end(*gamma_counts_), double(0.0) );
  const float post_lower_e = gamma_energy_min();
  const float post_upper_e = gamma_energy_max();  //energy_calibration_->channel_energies()->back()
  
  if( fabs(post_gammasum - pre_gammasum) > (0.00001*std::max(fabs(post_gammasum),fabs(pre_gammasum))) )
  {
    char buffer[512];
    snprintf( buffer, sizeof(buffer),
             "Gamma sum changed from %f to %f while combining channels.",
              pre_gammasum, post_gammasum );
    log_developer_error( __func__, buffer );
  }//if( gamma sum changed )
  
  if( fabs(post_lower_e - pre_lower_e) > 0.0001 )
  {
    char buffer[512];
    snprintf( buffer, sizeof(buffer), "Lower energy of spectrum changed from "
              "%f to %f while combining channels.", pre_lower_e, post_lower_e );
    log_developer_error( __func__, buffer );
  }//if( lower energy changed )
  
  if( fabs(post_upper_e - pre_upper_e) > 0.0001 )
  {
    char buffer[512];
    snprintf( buffer, sizeof(buffer), "Upper energy of spectrum changed from %f"
             " to %f while combining channels.", pre_upper_e, post_upper_e );
    log_developer_error( __func__, buffer );
  }//if( lower energy changed )
  
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
}//void combine_gamma_channels( const size_t nchann )

  
void SpecFile::clear_multimedia_data()
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  multimedia_data_.clear();
}//void clear_multimedia_data()

  
void SpecFile::add_multimedia_data( const MultimediaData &data )
{
  shared_ptr<const MultimediaData> new_data = make_shared<MultimediaData>( data );
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  multimedia_data_.push_back( new_data );
}//void add_multimedia_data( const MultimediaData &data )


void SpecFile::set_multimedia_data( vector<shared_ptr<const MultimediaData>> &data )
{
  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  multimedia_data_ = data;
  modified_ = modifiedSinceDecode_ = true;
}//void set_multimedia_data( vector<shared_ptr<const MultimediaData>> &data )
  
  
size_t SpecFile::do_channel_data_xform( const size_t nchannels,
                std::function< void(std::shared_ptr<Measurement>) > xform )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  size_t nchanged = 0;
  std::shared_ptr<Measurement> firstcombined;
  
  set<size_t> nchannelset, othernchannel;
  set<EnergyCalibration> othercalibs;
  map<EnergyCalibration, vector<std::shared_ptr<Measurement> > > calibs;
  
  for( size_t i = 0; i < measurements_.size(); ++i )
  {
    std::shared_ptr<Measurement> &m = measurements_[i];
    //    const size_t nchannel = m->num_gamma_channels();
    
    if( !m->gamma_channel_contents()
       || m->gamma_channel_contents()->size() != nchannels )
    {
      if( !!m->gamma_channel_contents() && !m->gamma_channel_contents()->empty())
      {
        othernchannel.insert( m->gamma_channel_contents()->size() );
        assert( m->energy_calibration_ );
        othercalibs.insert( *m->energy_calibration_ );
      }
      
      continue;
    }//if( not a gamma measurement )
    
    xform( m );
    
    assert( m->energy_calibration_ );
    
    auto &same_cals = calibs[*m->energy_calibration_];
    if( same_cals.size() )
      m->set_energy_calibration( same_cals[0]->energy_calibration_ );
    same_cals.push_back( m );
    
    if( m->energy_calibration_->channel_energies() )
      nchannelset.insert( m->energy_calibration_->num_channels() );
    
    ++nchanged;
  }//for( size_t i = 0; i < measurements_.size(); ++i )
  
  //ToDo: better test this function.
  //cerr << "There are calibs.size()=" << calibs.size() << endl;
  //cerr << "There are othercalibs.size()=" << othercalibs.size() << endl;
  
  if( nchanged )
  {
    if( calibs.size() > 1 || othercalibs.size() > 1
       || (calibs.size()==1 && othercalibs.size()==1
           && !((calibs.begin()->first) == (*othercalibs.begin()))) )
    {
      //cerr << "Un-setting properties_flags_ kHasCommonBinning bit" << endl;
      properties_flags_ &= (~kHasCommonBinning);
    }else
    {
      //cerr << "Setting properties_flags_ kHasCommonBinning bit" << endl;
      properties_flags_ |= kHasCommonBinning;
    }
  
    if( (nchannelset.size() > 1) || (othernchannel.size() > 1)
       || (nchannelset.size()==1 && othernchannel.size()==1
           && (*nchannelset.begin())!=(*othernchannel.begin())) )
    {
      //cerr << "Un-setting properties_flags_ kAllSpectraSameNumberChannels bit" << endl;
      properties_flags_ &= (~kAllSpectraSameNumberChannels);
    }else
    {
      //cerr << "Setting properties_flags_ kAllSpectraSameNumberChannels bit" << endl;
      properties_flags_ |= kAllSpectraSameNumberChannels;
    }
  
    modifiedSinceDecode_ = modified_ = true;
  }//if( nchanged )
  
  return nchanged;
}//size_t do_channel_data_xform( const size_t nchannels, const std::function<std::shared_ptr<Measurement> > &xform )


size_t SpecFile::combine_gamma_channels( const size_t ncombine,
                                                const size_t nchannels )
{
  if( ((nchannels % ncombine) != 0) || !nchannels || !ncombine )
    throw runtime_error( "SpecFile::combine_gamma_channels(): invalid input" );
  
  try
  {
    auto xform = [ncombine]( std::shared_ptr<Measurement> m ){ m->combine_gamma_channels(ncombine); };
    return do_channel_data_xform( nchannels, xform );
  }catch( std::exception &e )
  {
    throw runtime_error( "SpecFile::combine_gamma_channels():" + string(e.what()) );
  }
  
  return 0;
}//size_t combine_gamma_channels( const size_t, const size_t )



void SpecFile::combine_gamma_channels( const size_t ncombine,
                             const std::shared_ptr<const Measurement> &meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  std::shared_ptr<Measurement> m = measurement( meas );
  if( !m )
    throw runtime_error( "SpecFile::combine_gamma_channels(): measurement"
                         " passed in is not owned by this SpecFile." );
  
  m->combine_gamma_channels( ncombine );
  
  //Could actually check for a common binning, or try to share channel_energies_
  //  here, but whatever for now.
  if( measurements_.size() > 1 )
  {
    properties_flags_ &= (~kHasCommonBinning);
    properties_flags_ &= (~kAllSpectraSameNumberChannels);
  }//if( measurements_.size() > 1 )
  
  modifiedSinceDecode_ = modified_ = true;
}//void combine_gamma_channels( const int nchann, &meas )


void Measurement::truncate_gamma_channels( const size_t keep_first_channel,
                                          const size_t keep_last_channel,
                                          const bool keep_under_over_flow )
{
  if( !gamma_counts_ || gamma_counts_->empty() )
    return;
  
  const size_t nprevchannel = gamma_counts_->size();
  
  if( keep_last_channel >= nprevchannel )
    throw runtime_error( "truncate_gamma_channels: invalid upper channel." );
  
  if( keep_first_channel > keep_last_channel )
    throw runtime_error( "truncate_gamma_channels: invalid channel range." );
  
  double underflow = 0.0, overflow = 0.0;
  if( keep_under_over_flow )
  {
    for( size_t i = 0; i < keep_first_channel; ++i )
      underflow += (*gamma_counts_)[i];
    for( size_t i = keep_last_channel + 1; i < nprevchannel; ++i )
      overflow += (*gamma_counts_)[i];
  }//if( keep_under_over_flow )
  
  const size_t nnewchannel = 1 + keep_last_channel - keep_first_channel;
  
  std::shared_ptr<vector<float> > newchannelcounts
                            = std::make_shared<vector<float> >(nnewchannel);
  
  for( size_t i = keep_first_channel; i <= keep_last_channel; ++i )
    (*newchannelcounts)[i-keep_first_channel] = (*gamma_counts_)[i];
  
  newchannelcounts->front() += static_cast<float>( underflow );
  newchannelcounts->back()  += static_cast<float>( overflow );

  
#if( PERFORM_DEVELOPER_CHECKS )
  if( keep_under_over_flow )
  {
    const double newsum = std::accumulate( newchannelcounts->begin(),
                                        newchannelcounts->end(), double(0.0) );
    if( fabs(newsum - gamma_count_sum_) > 0.001 )
    {
      char buffer[512];
      snprintf( buffer, sizeof(buffer),
               "Cropping channel counts resulted gamma sum disagreement, "
                "expected new sum to equal old sum, but instead got %f (new) vs"
                " %f (old).", newsum, gamma_count_sum_ );
      log_developer_error( __func__, buffer );
    }
  }//if( keep_under_over_flow )
#endif //#if( PERFORM_DEVELOPER_CHECKS )
  
  
  assert( energy_calibration_ );
  const auto old_cal = energy_calibration_;
  auto newcal = make_shared<EnergyCalibration>();
    
  const int n_remove_front = static_cast<int>( keep_first_channel );
  const auto &old_coefs = old_cal->coefficients();
  const auto &old_dev = old_cal->deviation_pairs();
      
  switch( old_cal->type() )
  {
    case SpecUtils::EnergyCalType::Polynomial:
    case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
    {
      const auto new_coefs = polynomial_cal_remove_first_channels( n_remove_front, old_coefs );
      newcal->set_polynomial( nnewchannel, new_coefs, old_dev );
      break;
    }//case Polynomial:
      
    case SpecUtils::EnergyCalType::FullRangeFraction:
    {
      const auto oldpoly = fullrangefraction_coef_to_polynomial( old_coefs, nprevchannel );
      const auto newpoly = polynomial_cal_remove_first_channels( n_remove_front, oldpoly );
      auto newfwf = polynomial_coef_to_fullrangefraction( newpoly, nnewchannel );
      
      if( old_coefs.size() > 4 )
      {
//        const float x = static_cast<float>(i)/static_cast<float>(nbin);
//        const float low_e_coef = (a.size() > 4) ? a[4] : 0.0f;
//        val += low_e_coef / (1.0f+60.0f*x);

        // \TODO: - fix up here, if doable.  I dont think this can be exactly done,
        //  but maybye make it match up at the bottom binning, since this term
        //  only effects low energy.
      }//if( a.size() > 4 )
      
      newcal->set_full_range_fraction( nnewchannel, newfwf, old_dev );
      break;
    }//case FullRangeFraction:
      
    case SpecUtils::EnergyCalType::LowerChannelEdge:
    {
      vector<float> new_energies( nnewchannel + 1, 0.0f );
      for( size_t i = keep_first_channel; i <= keep_last_channel; ++i )
        new_energies.at(i-keep_first_channel) = old_coefs.at(i);
      new_energies[nnewchannel] = old_coefs.at(keep_last_channel+1);
      
      newcal->set_lower_channel_energy( nnewchannel, std::move(new_energies) );
      break;
    }
      
    case SpecUtils::EnergyCalType::InvalidEquationType:
    break;
  }//switch( old_cal->type() )
      
  energy_calibration_ = newcal;
  gamma_counts_ = newchannelcounts;
  
  if( !keep_under_over_flow )
    gamma_count_sum_ = std::accumulate( begin(*gamma_counts_), end(*gamma_counts_), double(0.0) );
  
#if( PERFORM_DEVELOPER_CHECKS )
  if( old_cal->channel_energies() )
  {
    if( !newcal->channel_energies() )
    {
      log_developer_error( __func__, "Old energy calibration had channel energies,"
                                     " but new one doesnt" );
    }else
    {
      for( size_t i = keep_first_channel; i <= keep_last_channel; ++i )
      {
        const float newval = newcal->channel_energies()->at(i-keep_first_channel);
        const float oldval = old_cal->channel_energies()->at(i);
        if( fabs(newval-oldval) > 0.001f )
        {
          char buffer[512];
          snprintf( buffer, sizeof(buffer),
                    "Cropping channel counts resulted in disagreement of channel"
                  " energies by old channel %i which had energy %f but now has"
                    " energy %f (new channel number %i)",
                    int(i), oldval, newval, int(i-keep_first_channel) );
          log_developer_error( __func__, buffer );
          break;
        }//if( fabs(newval-oldval) > 0.001f )
      }//for( size_t i = keep_first_channel; i <= keep_last_channel; ++i )
    }//if( new vcal doesnt have binnig ) / else
  }//if( old_cal->channel_energies() )
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
}//size_t Measurement::truncate_gamma_channels(...)


size_t SpecFile::truncate_gamma_channels( const size_t keep_first_channel,
                               const size_t keep_last_channel,
                               const size_t nchannels,
                               const bool keep_under_over_flow )
{
  try
  {
    auto xform = [=]( std::shared_ptr<Measurement> m ){
      m->truncate_gamma_channels(keep_first_channel, keep_last_channel, keep_under_over_flow);
    };
    
    return do_channel_data_xform( nchannels, xform );
  }catch( std::exception &e )
  {
    throw runtime_error( "SpecFile::truncate_gamma_channels():"
                          + string(e.what()) );
  }
  
  return 0;
}//size_t truncate_gamma_channels(...)


void SpecFile::truncate_gamma_channels( const size_t keep_first_channel,
                             const size_t keep_last_channel,
                             const bool keep_under_over_flow,
                             const std::shared_ptr<const Measurement> &meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  
  std::shared_ptr<Measurement> m = measurement( meas );
  if( !m )
    throw runtime_error( "SpecFile::truncate_gamma_channels(): measurement"
                        " passed in is not owned by this SpecFile." );
  
  m->truncate_gamma_channels( keep_first_channel, keep_last_channel,
                              keep_under_over_flow );
  
  //Could actually check for a common binning, or try to share channel_energies_
  //  here, but whatever for now.
  if( measurements_.size() > 1 )
  {
    properties_flags_ &= (~kHasCommonBinning);
    properties_flags_ &= (~kAllSpectraSameNumberChannels);
  }//if( measurements_.size() > 1 )
  
  modifiedSinceDecode_ = modified_ = true;
}//void SpecFile::truncate_gamma_channels(...)



std::shared_ptr<Measurement> SpecFile::measurement( std::shared_ptr<const Measurement> meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( !meas )
    return nullptr;
  
  // TODO: use something like `measurement( meas->sample_number_, meas->detector_number_ )` to speed this lookup up
  
  for( const auto &m : measurements_ )
  {
    if( m == meas )
      return m;
  }//for( const auto &m : measurements_ )
  
  return nullptr;
}//measurement(...)

//set_live_time(...) and set_real_time(...) update both the measurement
//  you pass in, as well as *this.  If measurement is invalid, or not in
//  measurements_, than an exception is thrown.
void SpecFile::set_live_time( const float lt,
                                     std::shared_ptr<const Measurement> meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  std::shared_ptr<Measurement> ptr = measurement( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_live_time(...): measurement"
                         " passed in didnt belong to this SpecFile" );

  const float oldLifeTime = meas->live_time();
  ptr->live_time_ = lt;
  gamma_live_time_ += (lt-oldLifeTime);
  modified_ = modifiedSinceDecode_ = true;
}//set_live_time(...)

      
void SpecFile::set_real_time( const float rt, std::shared_ptr<const Measurement> meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  std::shared_ptr<Measurement> ptr = measurement( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_real_time(...): measurement"
                         " passed in didnt belong to this SpecFile" );

  const float oldRealTime = ptr->live_time();
  ptr->real_time_ = rt;
  gamma_real_time_ += (rt - oldRealTime);
  modified_ = modifiedSinceDecode_ = true;
}//set_real_time(...)


void SpecFile::add_measurement( std::shared_ptr<Measurement> meas,
                                      const bool doCleanup )
{
  if( !meas )
    return;
  
  vector< std::shared_ptr<Measurement> >::iterator meas_pos;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  meas_pos = lower_bound( measurements_.begin(), measurements_.end(),
                          meas, &compare_by_derived_sample_det_time );
 
  if( (meas_pos!=measurements_.end()) && ((*meas_pos)==meas) )
    throw runtime_error( "SpecFile::add_measurement: duplicate meas" );
  
  //Making sure detector names/numbers are kept track of here instead of in
  //  cleanup_after_load() makes sure to preserve sample and detector numbers
  //  of the Measurements already in this SpecFile object
  const string &detname = meas->detector_name_;
  vector<std::string>::const_iterator namepos
         = std::find( detector_names_.begin(), detector_names_.end(), detname );
  
  if( namepos == detector_names_.end() )
  {
    detector_names_.push_back( detname );
    int detnum = -1;
    for( const int d : detector_numbers_ )
      detnum = std::max( d, detnum );
    detnum += 1;
    meas->detector_number_ = detnum;
    detector_numbers_.push_back( detnum );
  }else
  {
    const size_t index = namepos - detector_names_.begin();
    meas->detector_number_ = detector_numbers_.at(index);
  }//if( we dont already have a detector with this name ) / else
  
  
  if( meas->contained_neutron_ )
  {
    const auto neutpos
               = std::find( begin(neutron_detector_names_), end(neutron_detector_names_), detname );
    if( neutpos == end(neutron_detector_names_) )
    {
      neutron_detector_names_.push_back( detname );
      std::sort( begin(neutron_detector_names_), end(neutron_detector_names_) );
    }
  }//if( measurement contained neutrons )
  
  if( meas->gamma_counts_ && !meas->gamma_counts_->empty() )
  {
    const auto gammapos
                = std::find( begin(gamma_detector_names_), end(gamma_detector_names_), detname );
    if( gammapos == end(gamma_detector_names_) )
    {
      gamma_detector_names_.push_back( detname );
      std::sort( begin(gamma_detector_names_), end(gamma_detector_names_) );
    }
  }//if( measurement contained gammas )
  
  const int detnum = meas->detector_number_;
  int samplenum = meas->sample_number();
  
  meas_pos = lower_bound( measurements_.begin(), measurements_.end(),
                         std::shared_ptr<Measurement>(),
                         SpecFileLessThan(samplenum, detnum) );
  
  if( meas_pos != measurements_.end()
     && (*meas_pos)->sample_number()==samplenum
     && (*meas_pos)->detector_number()==detnum )
  {
    const int last_sample = (*sample_numbers_.rbegin());
    meas_pos = lower_bound( measurements_.begin(), measurements_.end(),
                            std::shared_ptr<Measurement>(),
                            SpecFileLessThan(last_sample, detnum) );
    if( meas_pos == measurements_.end()
       || (*meas_pos)->sample_number()!=last_sample
       || (*meas_pos)->detector_number()!=detnum  )
    {
      samplenum = last_sample;
    }else
    {
      samplenum = last_sample + 1;
    }
    
    meas->sample_number_ = samplenum;
  }//if( we need to modify the sample number )
  
  sample_numbers_.insert( meas->sample_number_ );
  
  meas_pos = upper_bound( measurements_.begin(), measurements_.end(),
                          meas, &compare_by_derived_sample_det_time );
  
  measurements_.insert( meas_pos, meas );
  
  if( doCleanup )
  {
    cleanup_after_load();
  }else
  {
    gamma_count_sum_    += meas->gamma_count_sum_;
    neutron_counts_sum_ += meas->neutron_counts_sum_;
    gamma_live_time_    += meas->live_time_;
    gamma_real_time_    += meas->real_time_;
    
    sample_to_measurements_.clear();
    for( size_t measn = 0; measn < measurements_.size(); ++measn )
    {
      std::shared_ptr<Measurement> &meas = measurements_[measn];
      sample_to_measurements_[meas->sample_number_].push_back( measn );
    }
  }//if( doCleanup ) / else
  
  modified_ = modifiedSinceDecode_ = true;
}//void add_measurement( std::shared_ptr<Measurement> meas, bool doCleanup )


void SpecFile::remove_measurements( const vector<std::shared_ptr<const Measurement>> &meas )
{
  if( meas.empty() )
    return;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const size_t norigmeas = measurements_.size();
  if( meas.size() > norigmeas )
    throw runtime_error( "SpecFile::remove_measurements:"
                        " to many input measurements to remove" );
  
  //This below implementation is targeted for SpecFile's with lots of
  //  measurements_, and empiracally is much faster than commented out
  //  implementation below (which is left in because its a bit 'cleaner')
  vector<bool> keep( norigmeas, true );
  
  for( size_t i = 0; i < meas.size(); ++i )
  {
    const std::shared_ptr<const Measurement> &m = meas[i];
    
    map<int, std::vector<size_t> >::const_iterator iter
    = sample_to_measurements_.find( m->sample_number_ );
    
    if( iter != sample_to_measurements_.end() )
    {
      const vector<size_t> &indexs = iter->second;
      size_t i;
      for( i = 0; i < indexs.size(); ++i )
      {
        if( measurements_[indexs[i]] == m )
        {
          keep[indexs[i]] = false;
          break;
        }
      }//for( size_t i = 0; i < indexs.size(); ++i )
      
      if( i == indexs.size() )
        throw runtime_error( "SpecFile::remove_measurements: invalid meas" );
    }//if( iter != sample_to_measurements_.end() )
  }//for( size_t i = 0; i < meas.size(); ++i )
  
  vector< std::shared_ptr<Measurement> > surviving;
  surviving.reserve( norigmeas - meas.size() );
  for( size_t i = 0; i < norigmeas; ++i )
  {
    if( keep[i] )
      surviving.push_back( measurements_[i] );
  }
  
  measurements_.swap( surviving );
  
  /*
   for( size_t i = 0; i < meas.size(); ++i )
   {
   const std::shared_ptr<const Measurement> &m = meas[i];
   vector< std::shared_ptr<Measurement> >::iterator pos;
   if( measurements_.size() > 100 )
   {
   pos = std::lower_bound( measurements_.begin(), measurements_.end(),
   m, &compare_by_derived_sample_det_time );
   }else
   {
   pos = std::find( measurements_.begin(), measurements_.end(), m );
   }
   
   if( pos == measurements_.end() || ((*pos)!=m) )
   throw runtime_error( "SpecFile::remove_measurements: invalid meas" );
   
   measurements_.erase( pos );
   }
   */
  
  cleanup_after_load( DontChangeOrReorderSamples );
  modified_ = modifiedSinceDecode_ = true;
}//void remove_measurements( const vector<std::shared_ptr<const Measurement>> meas )


void SpecFile::remove_measurement( std::shared_ptr<const Measurement> meas,
                                         const bool doCleanup )
{
  if( !meas )
    return;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  vector< std::shared_ptr<Measurement> >::iterator pos
                = std::find( measurements_.begin(), measurements_.end(), meas );
  
  if( pos == measurements_.end() )
    throw runtime_error( "SpecFile::remove_measurement: invalid meas" );
  
  measurements_.erase( pos );
  
  //Could actually fix up detector_names_, detector_numbers_,
  //  neutron_detector_names_, and gamma_detector_names_
  
  if( doCleanup )
  {
    cleanup_after_load();
  }else
  {
    gamma_count_sum_    -= meas->gamma_count_sum_;
    neutron_counts_sum_ -= meas->neutron_counts_sum_;
    gamma_live_time_    -= meas->live_time_;
    gamma_real_time_    -= meas->real_time_;
    
    sample_numbers_.clear();
    sample_to_measurements_.clear();
    
    //should update detector names and numbers too
    set<string> detectornames;
    
    const size_t nmeas = measurements_.size();
    for( size_t measn = 0; measn < nmeas; ++measn )
    {
      const int samplenum = measurements_[measn]->sample_number_;
      sample_numbers_.insert( samplenum );
      sample_to_measurements_[samplenum].push_back( measn );
      detectornames.insert( measurements_[measn]->detector_name_ );
    }
    
    //check that detectornames matches
    for( size_t i = 0; i < detector_names_.size();  )
    {
      if( !detectornames.count(detector_names_[i]) )
      {
        detector_names_.erase( detector_names_.begin() + i );
        detector_numbers_.erase( detector_numbers_.begin() + i );
        continue;
      }
      
      ++i;
    }//for( size_t i = 0; i < detector_names_.size();  )
  }//if( doCleanup ) / else
  
  modified_ = modifiedSinceDecode_ = true;
}//void remove_measurement( std::shared_ptr<Measurement> meas, bool doCleanup );


void SpecFile::set_start_time( const time_point_t &timestamp,
                    const std::shared_ptr<const Measurement> meas  )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  std::shared_ptr<Measurement> ptr = measurement( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_start_time(...): measurement"
                        " passed in didnt belong to this SpecFile" );
  
  ptr->set_start_time( timestamp );
  modified_ = modifiedSinceDecode_ = true;
}//set_start_time(...)

void SpecFile::set_remarks( const std::vector<std::string> &remarks,
                 const std::shared_ptr<const Measurement> meas  )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  std::shared_ptr<Measurement> ptr = measurement( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_remarks(...): measurement"
                        " passed in didnt belong to this SpecFile" );
  
  ptr->set_remarks( remarks );
  modified_ = modifiedSinceDecode_ = true;
}//set_remarks(...)

void SpecFile::set_source_type( const SourceType type,
                                    const std::shared_ptr<const Measurement> meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  std::shared_ptr<Measurement> ptr = measurement( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_source_type(...): measurement"
                        " passed in didnt belong to this SpecFile" );
  
  ptr->set_source_type( type );
  modified_ = modifiedSinceDecode_ = true;
}//set_source_type(...)


void SpecFile::set_position( double longitude, double latitude,
                            time_point_t position_time,
                            const std::shared_ptr<const Measurement> meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  std::shared_ptr<Measurement> ptr = measurement( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_position(...): measurement"
                        " passed in didnt belong to this SpecFile" );
  
  ptr->set_position( longitude, latitude, position_time );
  
  int nGpsCoords = 0;
  mean_latitude_ = mean_longitude_ = 0.0;
  for( auto &meas : measurements_ )
  {
    if( meas->has_gps_info() )
    {
      ++nGpsCoords;
      mean_latitude_ += meas->latitude();
      mean_longitude_ += meas->longitude();
    }
  }//for( auto &meas : measurements_ )
  
  if( nGpsCoords == 0 )
  {
    mean_latitude_ = mean_longitude_ = -999.9;
  }else
  {
    mean_latitude_ /= nGpsCoords;
    mean_longitude_ /= nGpsCoords;
  }//if( !nGpsCoords ) / else
  
  
  modified_ = modifiedSinceDecode_ = true;
}//set_position(...)_


void SpecFile::set_title( const std::string &title,
                                 const std::shared_ptr<const Measurement> meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  std::shared_ptr<Measurement> ptr = measurement( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_title(...): measurement"
                        " passed in didnt belong to this SpecFile" );
  
  ptr->set_title( title );
  
  modified_ = modifiedSinceDecode_ = true;
}//void SpecFile::set_title(...)


void SpecFile::set_contained_neutrons( const bool contained,
                                      const float counts,
                                      const std::shared_ptr<const Measurement> meas,
                                      const float neutron_live_time )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  std::shared_ptr<Measurement> ptr = measurement( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_containtained_neutrons(...): "
                        "measurement passed in didnt belong to this "
                        "SpecFile" );
  
  ptr->contained_neutron_ = contained;
  if( contained )
  {
    ptr->neutron_counts_.resize( 1 );
    ptr->neutron_counts_[0] = counts;
    ptr->neutron_counts_sum_ = counts;
    if( (neutron_live_time > 0.0f) && !IsInf(neutron_live_time) && !IsNan(neutron_live_time) )
      ptr->neutron_live_time_ = neutron_live_time;
    else
      ptr->neutron_live_time_ = 0.0f;
  }else
  {
    ptr->neutron_counts_.resize( 0 );
    ptr->neutron_counts_sum_ = 0.0;
    ptr->neutron_live_time_ = 0.0f;
  }
  
  modified_ = modifiedSinceDecode_ = true;
}//void set_containtained_neutrons(...)


void SpecFile::set_detectors_analysis( const DetectorAnalysis &ana )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const bool newIsEmpty = ana.is_empty();
  
  if( newIsEmpty && !detectors_analysis_ )
    return;
  
  if( newIsEmpty )
    detectors_analysis_.reset();
  else
    detectors_analysis_ = std::make_shared<DetectorAnalysis>( ana );
  
  modified_ = modifiedSinceDecode_ = true;
}//void set_detectors_analysis( const DetectorAnalysis &ana )

void SpecFile::change_detector_name( const string &origname,
                                            const string &newname )
{
  if( origname == newname )
    return;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  auto pos = find( begin(detector_names_), end(detector_names_), origname );
  if( pos == end(detector_names_) )
    throw runtime_error( "change_detector_name: '" + origname + "'"
                         " not a valid detector name" );
  
  const auto newnamepos = find( begin(detector_names_), end(detector_names_), newname );
  if( newnamepos != end(detector_names_) )
    throw runtime_error( "change_detector_name: '" + newname + "'"
                        " is already a detector name" );
  
  const auto oldindex = pos - begin(detector_names_);
  assert( oldindex >= 0 && oldindex < detector_numbers_.size() );
  const int detnum = detector_numbers_[oldindex];
  
  //Lets keep detector_names_ sorted
  detector_names_.erase( pos );
  detector_numbers_.erase( begin(detector_numbers_) + oldindex );
  
  auto newpos = std::lower_bound( begin(detector_names_), end(detector_names_),  newname );
  assert( newpos==end(detector_names_) || (*newpos != newname) );
  detector_names_.insert( newpos, newname );
  const auto newindex = newpos - begin(detector_names_);
  detector_numbers_.insert( begin(detector_numbers_) + newindex, detnum );
  
  auto gammapos = find( begin(gamma_detector_names_), end(gamma_detector_names_), origname );
  if( gammapos != end(gamma_detector_names_) )
  {
    *gammapos = newname;
    std::sort( begin(gamma_detector_names_), end(gamma_detector_names_) );
  }
  
  auto neutpos = find( begin(neutron_detector_names_), end(neutron_detector_names_), origname );
  if( neutpos != end(neutron_detector_names_) )
  {
    *neutpos = newname;
    std::sort( begin(neutron_detector_names_), end(neutron_detector_names_) );
  }
  
  for( auto &m : measurements_ )
  {
    if( m && (m->detector_name_ == origname) )
      m->detector_name_ = newname;
  }
  
  modified_ = modifiedSinceDecode_ = true;
}//change_detector_name(...)

  
void SpecFile::change_sample_numbers( const vector<pair<int,int>> &from_to_sample_nums )
{
  if( from_to_sample_nums.empty() )
    return;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  // Go through and change sample numbers to the requested values
  //  Note that this is very inefficient - we could use `sample_to_measurements_`
  //  to greatly reduce the look-ups and searches, but the logic is a little more
  //  nuanced, so we'll just do it the dumb-simple way for right now.
  for( shared_ptr<Measurement> &meas : measurements_ )
  {
    const int sample = meas->sample_number_;
    auto pos = std::find_if( begin(from_to_sample_nums), end(from_to_sample_nums),
                            [sample]( const pair<int,int> &val ){
      return val.first == sample;
    } );
    
    if( pos != end(from_to_sample_nums) )
      meas->sample_number_ = pos->second;
  }//for( loop over measurements )
  
  sample_numbers_.clear();
  sample_to_measurements_.clear();
  
  for( size_t i = 0; i < measurements_.size(); ++i )
  {
    shared_ptr<Measurement> &meas = measurements_[i];
    sample_numbers_.insert( meas->sample_number_ );
    sample_to_measurements_[meas->sample_number_].push_back( i );
  }
  
  // We will only look to set `properties_flags_` that we may have changed values of
  //  (again, we could be a little more intelligent about this, but we'll be simple
  //  for the moment)
  //
  // Clear relevant bits, incase they aren't already
  properties_flags_ &= ~MeasurementProperties::kNotTimeSortedOrder;
  properties_flags_ &= ~MeasurementProperties::kNotSampleDetectorTimeSorted;
  properties_flags_ &= ~MeasurementProperties::kNotUniqueSampleDetectorNumbers;
  
  for( size_t i = 1; i < measurements_.size(); ++i )
  {
    if( is_special(measurements_[i-1]->start_time_)
       || is_special(measurements_[i]->start_time_) )
      continue;
    
    if( measurements_[i-1]->start_time_ > measurements_[i]->start_time_ )
      properties_flags_ |= kNotTimeSortedOrder;
    
    if( !compare_by_derived_sample_det_time(measurements_[i-1],measurements_[i]) )
      properties_flags_ |= kNotSampleDetectorTimeSorted;
    
    if( (properties_flags_ & kNotTimeSortedOrder)
       && (properties_flags_ & kNotSampleDetectorTimeSorted) )
      break;
  }//for( size_t i = 1; i < measurements_.size(); ++i )
  
  if( !has_unique_sample_and_detector_numbers() )
    properties_flags_ |= kNotUniqueSampleDetectorNumbers;
  
  modified_ = modifiedSinceDecode_ = true;
}//void change_sample_numbers( const std::vector<std::pair<int,int>> &from_to_sample_nums )
  

int SpecFile::occupancy_number_from_remarks() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  for( const string &str : remarks_ )
  {
    if( istarts_with( str, "Occupancy number = " ) )
    {
      const string valstr = str.substr( 19 );
      int val;
      if( toInt(valstr,val) )
        return val;
    }else if( istarts_with( str, "OccupancyNumber" ) )
    {
      string valstr = str.substr( 15 );
      size_t pos = valstr.find_first_not_of( " :\t\n\r=" );
      if( pos != string::npos )
      {
        valstr = valstr.substr( pos );
        int val;
        if( toInt(valstr,val) )
          return val;
      }
    }
    
    //if( has "Occupancy number = " )
    
  }//for( const string &str : remarks_ )

  return -1;
}//int occupancy_number_from_remarks() const


std::shared_ptr<const Measurement> SpecFile::measurement( const int sample_number,
                                             const std::string &det_name ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  vector<string>::const_iterator det_name_iter;
  det_name_iter = find( detector_names_.begin(), detector_names_.end(), det_name );

  if( det_name_iter == detector_names_.end() )
  {
    cerr << "Didnt find detector named '" << det_name
         << "' in detector_names_" << endl;
    return std::shared_ptr<const Measurement>();
  }//if( det_name_iter == detector_names_.end() )

  const size_t det_index = det_name_iter - detector_names_.begin();
  const int detector_number = detector_numbers_[det_index];

  return measurement( sample_number, detector_number );
}//std::shared_ptr<const Measurement> measurement( const int, const string & )



std::shared_ptr<const Measurement> SpecFile::measurement( const int sample_number,
                                               const int detector_number ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  if( properties_flags_ & kNotSampleDetectorTimeSorted )
  {
    std::map<int, std::vector<size_t> >::const_iterator pos;
    pos = sample_to_measurements_.find( sample_number );
    
    if( pos != sample_to_measurements_.end() )
    {
      const vector<size_t> &indices = pos->second;
      for( const size_t ind : indices )
        if( measurements_[ind]->detector_number_ == detector_number )
          return measurements_[ind];
    }//if( pos != sample_to_measurements_.end() )
    
    return std::shared_ptr<const Measurement>();
  }//if( properties_flags_ & kNotSampleDetectorTimeSorted )
  
  
  std::vector< std::shared_ptr<Measurement> >::const_iterator meas_pos;
  meas_pos = lower_bound( measurements_.begin(), measurements_.end(),
                          std::shared_ptr<Measurement>(),
                          SpecFileLessThan(sample_number, detector_number) );
  if( meas_pos == measurements_.end()
     || (*meas_pos)->sample_number()!=sample_number
     || (*meas_pos)->detector_number()!=detector_number )
  {
    return std::shared_ptr<const Measurement>();
  }//if( meas_pos == measurements_.end() )

  return *meas_pos;

  //Below kept around just incase we want to check the above
//
  //could rely on measurements_ being sorted here
//  for( const auto &meas : measurements_ )
//    if( meas->sample_number_==sample_number && meas->detector_number==detector_number )
//    {
//      assert( meas_pos != measurements_.end() );
//      std::shared_ptr<Measurement> lb = *meas_pos;
//      if( lb.get() != meas.get() )
//      {
//        for( const auto &meas : measurements_ )
//          cout << "\t" << meas->sample_number_ << "\t" << meas->detector_number << endl;
//      }//if( lb.get() != meas.get() )
//      assert( lb.get() == meas.get() );
//      return meas;
//    }
//  cerr << "Didnt find detector " << detector_number << " for sample number "
//       << sample_number_ << endl;
//  return std::shared_ptr<const Measurement>();
}//std::shared_ptr<const Measurement> measurement( const int , onst int )



vector<std::shared_ptr<const Measurement>> SpecFile::sample_measurements( const int sample ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  vector< std::shared_ptr<const Measurement> > answer;

  std::map<int, std::vector<size_t> >::const_iterator pos;
  pos = sample_to_measurements_.find( sample );

  if( pos != sample_to_measurements_.end() )
  {
    const vector<size_t> &indices = pos->second;
    for( const size_t ind : indices )
      answer.push_back( measurements_.at(ind) );
  }//if( pos != sample_to_measurements_.end() )

  return answer;

  /*
  //get all measurements_ cooresponding to 'sample_number', where
  //  sample_number may not be a zero based index (eg may start at one)
  vector< std::shared_ptr<const Measurement> > answer;

  if( measurements_.empty() )
    return answer;

  //we could relies on measurements being sorted and use lower_bound/upper_bound
  for( const auto &meas : measurements_ )
    if( meas->sample_number_ == sample )
      answer.push_back( meas );

  return answer;
  */
}//vector<const std::shared_ptr<const Measurement>> measurements( const int sample ) const



void Measurement::set_energy_calibration( const std::shared_ptr<const EnergyCalibration> &cal )
{
  if( !cal )
    throw runtime_error( "set_energy_calibration: called with null input" );
      
  if( !gamma_counts_ && (cal->type() != EnergyCalType::InvalidEquationType) )
    throw runtime_error( "set_energy_calibration: Measurement does not contain gamma counts" );
      
  switch( cal->type() )
  {
    case EnergyCalType::Polynomial:
    case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
    case EnergyCalType::FullRangeFraction:
    case EnergyCalType::LowerChannelEdge:
    {
      const bool same_channel = (gamma_counts_->size() == cal->num_channels());
      if( !same_channel )
        throw runtime_error( "set_energy_calibration: calibration has "
                              + std::to_string(cal->num_channels()) + " but there are "
                              + std::to_string(gamma_counts_->size()) + " gamma channels." );
      break;
    }//
    
    case EnergyCalType::InvalidEquationType:
      break;
  }//switch( cal->type() )
                            
                            
  energy_calibration_ = cal;
}//void set_energy_calibration(...)





void Measurement::rebin( const std::shared_ptr<const EnergyCalibration> &cal )
{
  assert( energy_calibration_ );
  if( energy_calibration_->num_channels() < 4 )
    throw std::runtime_error( "Measurement::rebin(): invalid previous energy calibration" );
  
  if( !cal || (cal->num_channels() < 4) )
    throw std::runtime_error( "Measurement::rebin(): invalid new energy calibration" );
      
  const size_t new_nbin = cal->num_channels();
  auto rebinned_gamma_counts = make_shared<vector<float>>(new_nbin);
  
  SpecUtils::rebin_by_lower_edge( *energy_calibration_->channel_energies(),
                                  *gamma_counts_,
                                  *cal->channel_energies(),
                                  *rebinned_gamma_counts );
    
  gamma_counts_ = rebinned_gamma_counts;
  energy_calibration_ = cal;
}//rebin( )


#if( SpecUtils_ENABLE_EQUALITY_CHECKS )
namespace
{
#define compare_field(l,r,f) if( (l.f) != (r.f) ) return (l.f) < (r.f);
  bool compare_DetectorAnalysisResult( const DetectorAnalysisResult &lhs, const DetectorAnalysisResult &rhs )
  {
    compare_field(lhs,rhs,remark_);
    compare_field(lhs,rhs,nuclide_);
    compare_field(lhs,rhs,activity_);
    compare_field(lhs,rhs,nuclide_type_);
    compare_field(lhs,rhs,id_confidence_);
    compare_field(lhs,rhs,distance_);
    compare_field(lhs,rhs,dose_rate_);
    compare_field(lhs,rhs,real_time_);
    //compare_field(lhs,rhs,start_time_);
    compare_field(lhs,rhs,detector_);
    return false;
  };
}//namespace

void DetectorAnalysisResult::equal_enough( const DetectorAnalysisResult &lhs,
                                          const DetectorAnalysisResult &rhs )
{
  vector<string> issues;
  if( lhs.remark_ != rhs.remark_ )
    issues.push_back( "DetectorAnalysisResult remark for LHS ('"
                         + lhs.remark_ + "') doesnt match RHS ('"
                         + rhs.remark_ + "')" );
  if( lhs.nuclide_ != rhs.nuclide_ )
    issues.push_back( "DetectorAnalysisResult nuclide for LHS ('"
                         + lhs.nuclide_ + "') doesnt match RHS ('"
                         + rhs.nuclide_ + "')" );
  if( lhs.nuclide_type_ != rhs.nuclide_type_ )
    issues.push_back( "DetectorAnalysisResult nuclide type for LHS ('"
                         + lhs.nuclide_type_ + "') doesnt match RHS ('"
                         + rhs.nuclide_type_ + "')" );
  if( lhs.id_confidence_ != rhs.id_confidence_ )
    issues.push_back( "DetectorAnalysisResult ID confidence for LHS ('"
                         + lhs.id_confidence_ + "') doesnt match RHS ('"
                         + rhs.id_confidence_ + "')" );
  if( lhs.detector_ != rhs.detector_ )
    issues.push_back( "DetectorAnalysisResult detector for LHS ('"
                         + lhs.detector_ + "') doesnt match RHS ('"
                         + rhs.detector_ + "')" );
  //if( lhs.start_time_ != rhs.start_time_ )
  //  issues.push_back( "DetectorAnalysisResult start time for LHS ("
  //                      + SpecUtils::to_iso_string(lhs.start_time_) + ") doesnt match RHS ("
  //                    + SpecUtils::to_iso_string(rhs.start_time_) + ")" );
  
  char buffer[1024];
  if( fabs(lhs.activity_ - rhs.activity_) > (0.0001*std::max(fabs(lhs.activity_),fabs(rhs.activity_))) )
  {
    snprintf( buffer, sizeof(buffer),
             "DetectorAnalysisResult activity of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.activity_, rhs.activity_ );
    issues.push_back( buffer );
  }
  
  const double dist_diff = fabs(lhs.distance_ - rhs.distance_);
  if( (dist_diff > 0.001) && (dist_diff > 1.0E-5*std::max(fabs(lhs.distance_),fabs(rhs.distance_))) )
  {
    snprintf( buffer, sizeof(buffer),
             "DetectorAnalysisResult distance of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.distance_, rhs.distance_ );
    issues.push_back( buffer );
  }
  
  if( fabs(lhs.dose_rate_ - rhs.dose_rate_) > 0.001 )
  {
    snprintf( buffer, sizeof(buffer),
             "DetectorAnalysisResult dose rate of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.dose_rate_, rhs.dose_rate_ );
    issues.push_back( buffer );
  }
  
  if( fabs(lhs.real_time_ - rhs.real_time_) > 0.001 )
  {
    snprintf( buffer, sizeof(buffer),
             "DetectorAnalysisResult dose rate of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.real_time_, rhs.real_time_ );
    issues.push_back(  buffer );
  }
  
  string error_msg;
  for( const string &msg : issues )
    error_msg += (error_msg.empty() ? "" : "\n") + msg;
  if( !error_msg.empty() )
    throw runtime_error( error_msg );
}//void DetectorAnalysisResult::equal_enough(...)


void DetectorAnalysis::equal_enough( const DetectorAnalysis &lhs,
                                    const DetectorAnalysis &rhs )
{
  char buffer[1024];
  
  vector<string> issues;
  vector<string> lhsremarks, rhsremarks;
  
  for( string remark : lhs.remarks_ )
  {
//    remark.erase( std::remove_if(remark.begin(), remark.end(), [](char c) -> bool { return !(std::isalnum(c) || c==' '); }), remark.end());
    trim( remark );
    ireplace_all( remark, "  ", " " );
    if( remark.size() )
      lhsremarks.push_back( remark );
  }
  
  for( string remark : rhs.remarks_ )
  {
    //    remark.erase( std::remove_if(remark.begin(), remark.end(), [](char c) -> bool { return !(std::isalnum(c) || c==' '); }), remark.end());
    trim( remark );
    ireplace_all( remark, "  ", " " );
    if( remark.size() )
      rhsremarks.push_back( remark );
  }
  
  stable_sort( lhsremarks.begin(), lhsremarks.end() );
  stable_sort( rhsremarks.begin(), rhsremarks.end() );
  
  
  if( lhsremarks.size() != rhsremarks.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "Number of Analysis remarks for LHS (%i) doesnt match RHS %i",
             int(lhsremarks.size()), int(rhsremarks.size()) );
    issues.push_back( buffer );
  }
  
  for( size_t i = 0; i < rhsremarks.size(); ++i )
  {
    const string lhsr = (i < lhsremarks.size()) ? lhsremarks[i] : string();
    const string rhsr = (i < rhsremarks.size()) ? rhsremarks[i] : string();
    
    if( lhsr != rhsr )
    {
      snprintf( buffer, sizeof(buffer),
               "Analysis remark %i for LHS ('%s') doesnt match RHS ('%s')",
               int(i), lhsr.c_str(), rhsr.c_str() );
      issues.push_back( buffer );
    }
  }//for( size_t i = 0; i < rhsremarks.size(); ++i )
  
  auto lhsap = lhs.algorithm_component_versions_;
  auto rhsap = rhs.algorithm_component_versions_;
  
  if( lhsap.size() != rhsap.size() )
  {
    issues.push_back( "Number of analysis algorithm versions for LHS ('"
                        + std::to_string(lhsap.size()) + "') doesnt match RHS ('"
                        + std::to_string(rhsap.size()) + "')" );
  }else
  {
    std::sort( begin(lhsap), end(lhsap) );
    std::sort( begin(rhsap), end(rhsap) );
    for( size_t i = 0; i < lhsap.size(); ++i )
    {
      const pair<string,string> &lhsr = lhsap[i];
      const pair<string,string> &rhssr = rhsap[i];
      
      if( lhsr.first != rhssr.first )
        issues.push_back( "Analysis algorithm version component name for LHS ('"
                         + lhsr.first + "') doesnt match RHS ('"
                         + rhssr.first + "')" );
      if( lhsr.second != rhssr.second )
        issues.push_back( "Analysis algorithm version component version for LHS ('"
                         + lhsr.second + "') doesnt match RHS ('"
                         + rhssr.second + "')" );
    }//for( size_t i = 0; i < rhs.lhsap.size(); ++i )
  }//if( lhsap.size() != rhsap.size() ) / else
  
  if( lhs.algorithm_name_ != rhs.algorithm_name_ )
    issues.push_back( "Analysis algorithm name for LHS ('"
                        + lhs.algorithm_name_ + "') doesnt match RHS ('"
                        + rhs.algorithm_name_ + "')" );
  
  if( lhs.algorithm_creator_ != rhs.algorithm_creator_ )
    issues.push_back( "Analysis algorithm creator for LHS ('"
                        + lhs.algorithm_creator_ + "') doesnt match RHS ('"
                        + rhs.algorithm_creator_ + "')" );
  
  if( lhs.algorithm_description_ != rhs.algorithm_description_ )
    issues.push_back( "Analysis algorithm description for LHS ('"
                        + lhs.algorithm_description_ + "') doesnt match RHS ('"
                        + rhs.algorithm_description_ + "')" );

  if( lhs.analysis_start_time_ != rhs.analysis_start_time_ )
    issues.push_back( "Analysis start time for LHS ('"
                      + SpecUtils::to_iso_string(lhs.analysis_start_time_)
                      + "') doesnt match RHS ('"
                      + SpecUtils::to_iso_string(rhs.analysis_start_time_)
                      + "')" );
  
  
  if( fabs(lhs.analysis_computation_duration_ - rhs.analysis_computation_duration_) > std::numeric_limits<float>::epsilon() )
    issues.push_back( "Analysis duration time for LHS ('"
      + std::to_string(lhs.analysis_computation_duration_)
      + "') doesnt match RHS ('"
      + std::to_string(rhs.analysis_computation_duration_)
      + "')" );
  
  if( lhs.algorithm_result_description_ != rhs.algorithm_result_description_ )
    issues.push_back( "Analysis algorithm result description for LHS ('"
                        + lhs.algorithm_result_description_ + "') doesnt match RHS ('"
                        + rhs.algorithm_result_description_ + "')" );
  
  
  if( lhs.results_.size() != rhs.results_.size() )
  {
    stringstream msg;
    msg << "Different number of analysis results for LHS ("
    << lhs.results_.size() << ") vs RHS (" << rhs.results_.size() << "):"
    << endl;
    
    for( DetectorAnalysisResult l : lhs.results_ )
    {
      msg << "\tLHS Ana Res: rem='" << l.remark_ << "', nuc='"
           << l.nuclide_ << "', dos="
           << l.dose_rate_ << ", act=" << l.activity_
           << ", conf='" << l.id_confidence_ << "'"
           << ", dist=" << l.distance_
           << ", det='" << l.detector_ << "'"
           << endl;
    }
    
    for( DetectorAnalysisResult l : rhs.results_ )
    {
      msg << "\tRHS Ana Res: rem='" << l.remark_ << "', nuc='"
           << l.nuclide_ << "', dos="
           << l.dose_rate_ << ", act=" << l.activity_
           << ", conf='" << l.id_confidence_ << "'"
           << ", dist=" << l.distance_
           << ", det='" << l.detector_ << "'"
           << endl;
    }
    
    issues.push_back( msg.str() );
  }else
  {
    //Ordering of the results is not guaranteed in a round trip, since we use a
    //  single analysis result type, but N42 2012 defines a few different ones.
    vector<DetectorAnalysisResult> rhsres = rhs.results_;
    vector<DetectorAnalysisResult> lhsres = lhs.results_;
    std::sort( lhsres.begin(), lhsres.end(), &compare_DetectorAnalysisResult );
    std::sort( rhsres.begin(), rhsres.end(), &compare_DetectorAnalysisResult );
    
    for( size_t i = 0; i < rhsres.size(); ++i )
    {
      try
      {
        DetectorAnalysisResult::equal_enough( lhsres[i], rhsres[i] );
      }catch( std::exception &e )
      {
        issues.push_back( e.what() );
      }
    }//for( size_t i = 0; i < rhsres.size(); ++i )
  }//if( lhs.results_.size() != rhs.results_.size() ) / else
  
  if( issues.size() )
  {
    string msg;
    for( const string &err : issues )
      msg += (msg.size() ? "\n" : "") + err;
    throw runtime_error( msg );
  }//if( issues.size() )
}//void DetectorAnalysis::equal_enough(...)



void Measurement::equal_enough( const Measurement &lhs, const Measurement &rhs )
{
  char buffer[1024];
  
  vector<string> issues;
  
  //Make sure live time within something reasonable
  const double live_time_diff = fabs( double(lhs.live_time_) - double(rhs.live_time_));
  if( (live_time_diff > (std::max(lhs.live_time_,rhs.live_time_)*1.0E-5)) && (live_time_diff > 1.0E-3) )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: Live time of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.live_time_, rhs.live_time_ );
    issues.push_back( buffer );
  }
  
  //Make sure real time within something reasonable
  const double real_time_diff = fabs( double(lhs.real_time_) - double(rhs.real_time_));
  if( (real_time_diff > (std::max(lhs.real_time_,rhs.real_time_)*1.0E-5)) && (real_time_diff > 1.0E-3) )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: Real time of LHS (%1.8E) doesnt match RHS (%1.8E); diff=%f",
             lhs.real_time_, rhs.real_time_, real_time_diff );
    issues.push_back( buffer );
  }
  
  if( lhs.contained_neutron_ != rhs.contained_neutron_ )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: LHS %s contain neutrons while RHS %s",
             (lhs.contained_neutron_?"did":"did not"),
             (rhs.contained_neutron_?"did":"did not") );
    issues.push_back( buffer );
  }

  if( lhs.sample_number_ != rhs.sample_number_ )
    issues.push_back( "Measurement: LHS sample number some how didnt equal RHS sample number" );
  
  if( lhs.occupied_ != rhs.occupied_ )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: Occupied of LHS (%i) different form RHS (%i)",
             int(lhs.occupied_), int(rhs.occupied_) );
    issues.push_back( buffer );
  }
  
  if( fabs(lhs.gamma_count_sum_ - rhs.gamma_count_sum_) > (0.00001*std::max(fabs(lhs.gamma_count_sum_),fabs(rhs.gamma_count_sum_))) )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: Gamma count sum of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.gamma_count_sum_, rhs.gamma_count_sum_ );
    issues.push_back( buffer );
  }
  
  const float lhs_neut_lt = lhs.neutron_live_time();
  const float rhs_neut_lt = rhs.neutron_live_time();
  if( fabs(lhs_neut_lt - rhs_neut_lt) > (0.001f*std::max(fabs(lhs_neut_lt),fabs(rhs_neut_lt))) )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: Neutron live time of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs_neut_lt, rhs_neut_lt );
    issues.push_back( buffer );
  }
  
  if( fabs(lhs.neutron_counts_sum_ - rhs.neutron_counts_sum_) > (0.00001*std::max(fabs(lhs.neutron_counts_sum_),fabs(rhs.neutron_counts_sum_))) )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: Neutron count sum of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.neutron_counts_sum_, rhs.neutron_counts_sum_ );
    issues.push_back( buffer );
  }


  if( lhs.detector_name_ != rhs.detector_name_ )
    issues.push_back( "Measurement: Detector name for LHS ('" + lhs.detector_name_
                        + "') doesnt match RHS ('" + rhs.detector_name_ + "')" );

  /*
  if( lhs.detector_number_ != rhs.detector_number_ )
  {
    snprintf( buffer, sizeof(buffer),
             "Detector number of LHS (%i) doesnt match RHS (%i)",
             lhs.detector_number_, rhs.detector_number_ );
   issues.push_back( buffer );
  }
   */

  if( trim_copy(lhs.detector_description_) != trim_copy(rhs.detector_description_)
     && rhs.detector_description_!="Gamma and Neutron"
     && ("NaI, " + trim_copy(lhs.detector_description_)) != trim_copy(rhs.detector_description_)
     && ("LaBr3, " + trim_copy(lhs.detector_description_)) != trim_copy(rhs.detector_description_)
     && ("unknown, " + trim_copy(lhs.detector_description_)) != trim_copy(rhs.detector_description_)
     )
  {
    //static std::atomic<int> ntimesprint(0);
    //if( ntimesprint++ < 10 )
    //{
      const string msg = "Measurement: Detector description for LHS ('" + lhs.detector_description_
                      + "') doesnt match RHS ('" + rhs.detector_description_ + "')";
    //  cerr << "Warning check for detector description falure not being enforced: " << msg << endl;
    //}
    issues.push_back( msg );
  }

  if( lhs.quality_status_ != rhs.quality_status_ )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: Quality status of LHS (%i) different from RHS (%i)",
             int(lhs.quality_status_), int(rhs.quality_status_) );
    issues.push_back( buffer );
  }
  
  if( lhs.source_type_ != rhs.source_type_ )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: Source type of LHS (%i) different from RHS (%i)",
             int(lhs.source_type_), int(rhs.source_type_) );
    issues.push_back( buffer );
  }

  assert( lhs.energy_calibration_ );
  assert( rhs.energy_calibration_ );
  EnergyCalibration::equal_enough( *lhs.energy_calibration_, *rhs.energy_calibration_ );
  
  const set<string> nlhsremarkss( lhs.remarks_.begin(), lhs.remarks_.end() );
  const set<string> nrhsremarkss( rhs.remarks_.begin(), rhs.remarks_.end() );
  
  const vector<string> nlhsremarks( nlhsremarkss.begin(), nlhsremarkss.end() );
  const vector<string> nrhsremarks( nrhsremarkss.begin(), nrhsremarkss.end() );
  
  if( nlhsremarks.size() != nrhsremarks.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: Number of remarks in LHS (%i) doesnt match RHS (%i) for sample %i, det '%s'",
             int(nlhsremarks.size()), int(nrhsremarks.size()),
             lhs.sample_number_, lhs.detector_name_.c_str() );

#if( !REQUIRE_REMARKS_COMPARE )
    cerr << buffer << endl;
#endif
    //for( size_t i = 0; i < nlhsremarks.size(); ++i )
    //  cerr << "\tLHS: '" << nlhsremarks[i] << "'" << endl;
    //for( size_t i = 0; i < nrhsremarks.size(); ++i )
    //  cerr << "\tRHS: '" << nrhsremarks[i] << "'" << endl;
    
#if( REQUIRE_REMARKS_COMPARE )
    issues.push_back( buffer );
#else
    cerr << endl;
#endif
  }//if( nlhsremarks.size() != nrhsremarks.size() )
    
  
  for( const string &rhsr : nrhsremarks )
  {
    if( find( begin(nlhsremarks), end(nlhsremarks), rhsr ) == end(nlhsremarks) )
    {
      const string msg = "Measurement: Remark in RHS, but not LHS: " + rhsr;
#if( REQUIRE_REMARKS_COMPARE )
      issues.push_back( msg );
#else
      cerr << msg << endl;
#endif
    }//if( find( begin(nlhsremarks), end(nlhsremarks), rhsr ) == end(nlhsremarks) )
  }//for( const string &rhsr : nrhsremarks )
  
  for( const string &lhsr : nlhsremarks )
  {
    if( find( begin(nrhsremarks), end(nrhsremarks), lhsr ) == end(nrhsremarks) )
    {
      const string msg = "Measurement: Remark in LHS, but not RHS: remark='" + lhsr + "'";
#if( REQUIRE_REMARKS_COMPARE )
      issues.push_back( msg );
#else
      cerr << msg << endl;
#endif
    }
  }//for( const string &lhsr : nlhsremarks )
  
  /*
  for( size_t i = 0; i < std::max(nlhsremarks.size(),nrhsremarks.size()); ++i )
  {
    const string lhsr = (i < nlhsremarks.size()) ? nlhsremarks[i] : string();
    const string rhsr = (i < nrhsremarks.size()) ? nrhsremarks[i] : string();
    
    if( lhsr != rhsr )
    {
      snprintf( buffer, sizeof(buffer),
               "Remark %i in LHS ('%s') doesnt match RHS ('%s')",
               int(i), lhsr.c_str(), rhsr.c_str() );
#if( REQUIRE_REMARKS_COMPARE )
      issues.push_back( buffer );
#else
      cerr << buffer << endl;
#endif
    }//if( lhsr != rhsr )
  }//for( size_t i = 0; i < nlhsremarks.size(); ++i )
  */
  
  const set<string> lhsparsewarns( lhs.parse_warnings_.begin(), lhs.parse_warnings_.end() );
  const set<string> rhsparsewarns( rhs.parse_warnings_.begin(), rhs.parse_warnings_.end() );
  
  const vector<string> lhsparsewarn( lhsparsewarns.begin(), lhsparsewarns.end() );
  const vector<string> rhsparsewarn( rhsparsewarns.begin(), rhsparsewarns.end() );
  
  if( lhsparsewarn.size() != rhsparsewarn.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: Number of parse warnings in LHS (%i) doesnt match RHS (%i)",
             int(lhsparsewarn.size()), int(rhsparsewarn.size()) );
    
#if( !REQUIRE_REMARKS_COMPARE )
    cerr << buffer << endl;
#endif
    for( size_t i = 0; i < lhsparsewarn.size(); ++i )
      cerr << "\tLHS: '" << lhsparsewarn[i] << "'" << endl;
    for( size_t i = 0; i < rhsparsewarn.size(); ++i )
      cerr << "\tRHS: '" << rhsparsewarn[i] << "'" << endl;
    
#if( REQUIRE_REMARKS_COMPARE )
    issues.push_back( buffer );
#else
    cerr << buffer << endl;
#endif
  }//if( lhsparsewarn.size() != rhsparsewarn.size() )
  
  
  for( const string &warn : lhsparsewarn )
  {
    if( !rhsparsewarns.count(warn) )
    {
      const string msg = "Measurement: Parse warning in LHS, but not RHS: " + warn;
#if( REQUIRE_REMARKS_COMPARE )
      issues.push_back( msg );
#else
      cerr << msg << endl;
#endif
    }//if( !rhsparsewarns.count(warn) )
  }//for( const string &warn : lhsparsewarn )
  
  
  for( const string &warn : rhsparsewarn )
  {
    if( !lhsparsewarns.count(warn) )
    {
      const string msg = "Measurement: Parse warning in RHS, but not LHS: " + warn;
#if( REQUIRE_REMARKS_COMPARE )
      issues.push_back( msg );
#else
      cerr << msg << endl;
#endif
    }//if( !lhsparsewarns.count(warn) )
  }//for( const string &warn : rhsparsewarn )
  
  /*
  for( size_t i = 0; i < std::max(lhsparsewarn.size(),rhsparsewarn.size()); ++i )
  {
    const string lhsr = (i < lhsparsewarn.size()) ? lhsparsewarn[i] : string();
    const string rhsr = (i < rhsparsewarn.size()) ? rhsparsewarn[i] : string();
    
    if( lhsr != rhsr )
    {
      snprintf( buffer, sizeof(buffer),
               "Parse warning %i in LHS ('%s') doesnt match RHS ('%s')",
               int(i), lhsr.c_str(), rhsr.c_str() );
#if( REQUIRE_REMARKS_COMPARE )
      throw runtime_error( buffer );
#else
      cerr << buffer << endl;
#endif
    }
  }//for( size_t i = 0; i < nlhsremarks.size(); ++i )
  */
  
  
  if( lhs.start_time_ != rhs.start_time_ )
  {
	//For some reason the fractional second differ for some timestamps (including CNF files)
	// on Windows vs *NIX.  I guess let this slide for the moment.
	auto tdiff = lhs.start_time_ - rhs.start_time_;
	if( tdiff < time_point_t::duration::zero() )
		tdiff = -tdiff;
	if( tdiff >  std::chrono::seconds(1) )
    issues.push_back( "Measurement: Start time for LHS ("
                        + SpecUtils::to_iso_string(lhs.start_time_) + ") doesnt match RHS ("
                        + SpecUtils::to_iso_string(rhs.start_time_) + ")" );
  }

  
  if( (!lhs.gamma_counts_) != (!rhs.gamma_counts_) )
  {
    snprintf( buffer, sizeof(buffer), "Measurement: Gamma counts availability for LHS (%s)"
             " doesnt match RHS (%s)",
             (!lhs.gamma_counts_?"missing":"available"),
             (!rhs.gamma_counts_?"missing":"available") );
    issues.push_back( buffer );
  }
  
  if( !!lhs.gamma_counts_ )
  {
    if( lhs.gamma_counts_->size() != rhs.gamma_counts_->size() )
    {
      snprintf( buffer, sizeof(buffer),
               "Measurement: Number of gamma channels of LHS (%i) doesnt match RHS (%i)",
               int(lhs.gamma_counts_->size()),
               int(rhs.gamma_counts_->size()) );
      issues.push_back( buffer );
    }else
    {
      size_t num_channel_warnings = 0;
      for( size_t i = 0; i < lhs.gamma_counts_->size(); ++i )
      {
        const float a = lhs.gamma_counts_->at(i);
        const float b = rhs.gamma_counts_->at(i);
        if( fabs(a-b) > 1.0E-6*std::max( fabs(a), fabs(b) ) )
        {
          cerr << "LHS:";
          for( size_t j = (i>4) ? i-4: 0; j < lhs.gamma_counts_->size() && j < (i+5); ++j )
          {
            if( i==j ) cerr << "__";
            cerr << lhs.gamma_counts_->at(j);
            if( i==j ) cerr << "__, ";
            else cerr << ", ";
          }
          cerr << endl;
          
          cerr << "RHS:";
          for( size_t j = (i>4) ? i-4: 0; j < rhs.gamma_counts_->size() && j < (i+5); ++j )
          {
            if( i==j ) cerr << "__";
            cerr << rhs.gamma_counts_->at(j);
            if( i==j ) cerr << "__, ";
            else cerr << ", ";
          }
          cerr << endl;
          
          num_channel_warnings += 1;
          if( num_channel_warnings == 5 )
          {
            issues.push_back( "Measurement: There are additional channel that doesnt match, but error messages will be suppressed." );
            break;
          }else if( num_channel_warnings < 5 )
          {
            snprintf( buffer, sizeof(buffer),
                     "Measurement: Counts in gamma channel %i of LHS (%1.8E) doesnt match RHS (%1.8E)",
                     int(i), lhs.gamma_counts_->at(i),
                     rhs.gamma_counts_->at(i) );
            issues.push_back( buffer );
          }
        }//if( fabs(a-b) > 1.0E-6*std::max( fabs(a), fabs(b) ) )
      }//for( size_t i = 0; i < lhs.gamma_counts_->size(); ++i )
    }//if( lhs.gamma_counts_->size() != rhs.gamma_counts_->size() ) / else
  }//if( !!channel_energies_ )
  
  if( lhs.neutron_counts_.size() != rhs.neutron_counts_.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "Measurement: Number of neutron channels of LHS (%i) doesnt match RHS (%i)",
             int(lhs.neutron_counts_.size()),
             int(rhs.neutron_counts_.size()) );
    issues.push_back( buffer );
  }else
  {
    for( size_t i = 0; i < lhs.neutron_counts_.size(); ++i )
    {
      if( fabs(lhs.neutron_counts_[i] - rhs.neutron_counts_[i] ) > (0.0001*std::max(fabs(lhs.neutron_counts_[i]),fabs(rhs.neutron_counts_[i]))) )
      {
        snprintf( buffer, sizeof(buffer),
                 "Measurement: Counts in neutron channel %i of LHS (%1.8E) doesnt match RHS (%1.8E)",
                 int(i), lhs.neutron_counts_[i],
                 rhs.neutron_counts_[i] );
        issues.push_back( buffer );
      }
    }//for( size_t i = 0; i < lhs.neutron_counts_.size(); ++i )
  }//if( lhs.neutron_counts_.size() != rhs.neutron_counts_.size() ) / else
  
  if( trim_copy(lhs.title_) != trim_copy(rhs.title_) )
    issues.push_back( "Measurement: Title for LHS ('" + lhs.title_
                        + "') doesnt match RHS ('" + rhs.title_ + "')" );
  
  uint32_t lhs_deriv_props = lhs.derived_data_properties_;
  uint32_t rhs_deriv_props = rhs.derived_data_properties_;
  if( lhs_deriv_props != rhs_deriv_props )
  {
    if( rhs.source_type_ == SourceType::Background )
      rhs_deriv_props |= static_cast<uint32_t>(DerivedDataProperties::IsBackground);
    
    if( lhs.source_type_ == SourceType::Background )
      lhs_deriv_props |= static_cast<uint32_t>(DerivedDataProperties::IsBackground);
  }//if( lhs_deriv_props != rhs_deriv_props )
  
  if( lhs_deriv_props != rhs_deriv_props )
  {
    auto list_of_deriv_props = []( const uint32_t val ) -> string {
      const vector<pair<DerivedDataProperties,string>> props{
        {DerivedDataProperties::IsDerived,"IsDerived"},
        {DerivedDataProperties::ItemOfInterestSum,"ItemOfInterestSum"},
        {DerivedDataProperties::UsedForAnalysis,"UsedForAnalysis"},
        {DerivedDataProperties::ProcessedFurther,"ProcessedFurther"},
        {DerivedDataProperties::BackgroundSubtracted,"BackgroundSubtracted"},
        {DerivedDataProperties::IsBackground,"IsBackground"}
      };
      
      if( val == 0 )
        return "{none}";
      
      string answer = "{";
      for( const auto i : props )
      {
        if( val & static_cast<uint32_t>(i.first) )
          answer += ((answer.size() > 3) ? ", " : "") + i.second;
      }
      answer += "}";
      
      return answer;
    };//enum class DerivedDataProperties
    
    issues.push_back( "Measurement: Derived data flags for LHS ('"
                        + list_of_deriv_props(lhs.derived_data_properties_)
                        + "') doesnt match RHS ('"
                        + list_of_deriv_props(rhs.derived_data_properties_) + "')" );
  }//if( lhs.derived_data_properties_ != rhs.derived_data_properties_ )
  
  const float max_dose_rate = std::max( lhs.dose_rate_, rhs.dose_rate_ );
  if( ((lhs.dose_rate_ >= 0) != (rhs.dose_rate_ >= 0))
     || ((lhs.dose_rate_ >= 0)
        && (fabs(lhs.dose_rate_ - rhs.dose_rate_ ) > (0.000001*max_dose_rate)) ) )
  {
    issues.push_back( "Measurement: The dose rate of LHS is " + std::to_string(lhs.dose_rate_)
                        + " while RHS is " + std::to_string(rhs.dose_rate_) );
  }
  
  if( ((lhs.exposure_rate_ >= 0) != (rhs.exposure_rate_ >= 0))
     || ((lhs.exposure_rate_ >= 0) && (fabs(lhs.exposure_rate_ - rhs.exposure_rate_) > 0.00001)))
  {
    issues.push_back( "Measurement: The exposure rate of LHS is " + std::to_string(lhs.exposure_rate_)
                     + " while RHS is " + std::to_string(rhs.exposure_rate_) );
  }
  
  if( (!lhs.location_) != (!rhs.location_) )
  {
    issues.push_back( "Measurement: The "
                     + string(lhs.location_ ? "LHS" : "RHS")
                     + " LocationState info, but "
                     + string(lhs.location_ ? "RHS" : "LHS")
                     + " does not." );
  }else if( lhs.location_ )
  {
    assert( rhs.location_ );
    try
    {
      LocationState::equal_enough( *lhs.location_, *rhs.location_ );
    }catch( std::exception &e )
    {
      issues.push_back( e.what() );
    }
  }//if( (!lhs.location_) != (!rhs.location_) ) / else
  
  string error_msg;
  for( const string &m : issues )
    error_msg += (error_msg.empty() ? "" : "\n") + m;
  
  if( !error_msg.empty() )
    throw runtime_error( error_msg );
}//void equal_enough( const Measurement &lhs, const Measurement &rhs )


void SpecFile::equal_enough( const SpecFile &lhs, const SpecFile &rhs )
{
  std::lock( lhs.mutex_, rhs.mutex_ );
  std::unique_lock<std::recursive_mutex> lhs_lock( lhs.mutex_, std::adopt_lock_t() );
  std::unique_lock<std::recursive_mutex> rhs_lock( rhs.mutex_, std::adopt_lock_t() );

  char buffer[8*1024];
  vector<string> issues;
  
  const double live_time_diff = fabs( double(lhs.gamma_live_time_) - double(rhs.gamma_live_time_));
  if( (live_time_diff > (std::max(lhs.gamma_live_time_,rhs.gamma_live_time_)*1.0E-5)) && (live_time_diff > 1.0E-3) )
  {
    snprintf( buffer, sizeof(buffer),
              "SpecFile: Live time of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.gamma_live_time_, rhs.gamma_live_time_ );
    issues.push_back( buffer );
  }
  
  
  const double real_time_diff = fabs( double(lhs.gamma_real_time_) - double(rhs.gamma_real_time_));
  if( (real_time_diff > (std::max(lhs.gamma_real_time_,rhs.gamma_real_time_)*1.0E-5)) && (real_time_diff > 1.0E-3) )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Real time of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.gamma_real_time_, rhs.gamma_real_time_ );
    issues.push_back( buffer );
  }
  
  const double gamma_sum_diff = fabs(lhs.gamma_count_sum_ - rhs.gamma_count_sum_);
  const double max_gamma_sum = std::max(fabs(lhs.gamma_count_sum_), fabs(rhs.gamma_count_sum_));
  if( gamma_sum_diff > 0.1 || gamma_sum_diff > 1.0E-6*max_gamma_sum )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Gamma sum of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.gamma_count_sum_, rhs.gamma_count_sum_ );
    issues.push_back( buffer );
  }
  
  if( fabs(lhs.neutron_counts_sum_ - rhs.neutron_counts_sum_) > 0.01 )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Neutron sum of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.neutron_counts_sum_, rhs.neutron_counts_sum_ );
    issues.push_back( buffer );
  }
  
  if( lhs.filename_ != rhs.filename_ )
    issues.push_back( "SpecFile: Filename of LHS (" + lhs.filename_
                         + ") doenst match RHS (" + rhs.filename_ + ")" );
  
  if( lhs.detector_names_.size() != rhs.detector_names_.size() )
  {
    string message = "SpecFile: Number of detector names of LHS ("
                     + to_string(lhs.detector_names_.size()) + ": ";
    for( size_t i = 0; i < lhs.detector_names_.size(); ++i )
      message += (i ? ", '" : "'") + lhs.detector_names_[i] + "'";
    message += ") doesnt match RHS (" + to_string(rhs.detector_names_.size()) + ": ";
    for( size_t i = 0; i < rhs.detector_names_.size(); ++i )
      message += (i ? ", '" : "'") + rhs.detector_names_[i] + "'";
    message += ")";
    
    issues.push_back( message );
  }else
  {
    
    const set<string> lhsnames( lhs.detector_names_.begin(),
                               lhs.detector_names_.end() );
    const set<string> rhsnames( rhs.detector_names_.begin(),
                               rhs.detector_names_.end() );
    
    if( lhsnames != rhsnames )
    {
      string lhsnamesstr, rhsnamesstr;
      for( size_t i = 0; i < lhs.detector_names_.size(); ++i )
        lhsnamesstr += (i ? ", " : "") + lhs.detector_names_[i];
      for( size_t i = 0; i < rhs.detector_names_.size(); ++i )
        rhsnamesstr += (i ? ", " : "") + rhs.detector_names_[i];
      
      issues.push_back( "SpecFile: Detector names do not match for LHS ({"
                          + lhsnamesstr + "}) and RHS ({" + rhsnamesstr + "})" );
    }
  }//if( lhs.detector_names_.size() != rhs.detector_names_.size() ) / else
 
  
  if( lhs.detector_numbers_.size() != rhs.detector_numbers_.size()
      || lhs.detector_numbers_.size() != lhs.detector_names_.size() )
    issues.push_back( "SpecFile: Improper number of detector numbers - wtf" );
  
  //Ehh, I guess since detector numbers are an internal only thing (and we
  //  should get rid of them anyway), lets not test this anymore.
  /*
  for( size_t i = 0; i < lhs.detector_names_.size(); ++i )
  {
    const size_t pos = std::find( rhs.detector_names_.begin(),
                                  rhs.detector_names_.end(),
                                  lhs.detector_names_[i] )
                       - rhs.detector_names_.begin();
    if( lhs.detector_numbers_[i] != rhs.detector_numbers_[pos] )
    {
      stringstream msg;
      msg << "SpecFile: Detector number for detector '" << lhs.detector_names_[i] << " dont match.\n\tLHS->[";
      for( size_t i = 0; i < lhs.detector_names_.size() && i < lhs.detector_numbers_.size(); ++i )
        msg << "{" << lhs.detector_names_[i] << "=" << lhs.detector_numbers_[i] << "}, ";
      msg << "]\n\tRHS->[";
      for( size_t i = 0; i < rhs.detector_names_.size() && i < rhs.detector_numbers_.size(); ++i )
        msg << "{" << rhs.detector_names_[i] << "=" << rhs.detector_numbers_[i] << "}, ";
      msg << "]";
      
      throw runtime_error( msg.str() );
    }
  }//for( size_t i = 0; i < lhs.detector_names_.size(); ++i )
  */
  
  
  if( lhs.gamma_detector_names_.size() != rhs.gamma_detector_names_.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Number of gamma detector names of LHS (%i) doesnt match RHS (%i)",
             int(lhs.gamma_detector_names_.size()),
             int(rhs.gamma_detector_names_.size()) );
    issues.push_back( buffer );
  }else
  {
    
    const set<string> glhsnames( begin(lhs.gamma_detector_names_), end(lhs.gamma_detector_names_) );
    const set<string> grhsnames( begin(rhs.gamma_detector_names_), end(rhs.gamma_detector_names_) );
    
    if( glhsnames != grhsnames )
      issues.push_back( "SpecFile: Gamma detector names dont match for LHS and RHS" );
  }//if( lhs.gamma_detector_names_.size() != rhs.gamma_detector_names_.size() ) / else
  
  
  if( lhs.neutron_detector_names_.size() != rhs.neutron_detector_names_.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Number of neutron detector names of LHS (%i) doesnt match RHS (%i)",
             int(lhs.neutron_detector_names_.size()),
             int(rhs.neutron_detector_names_.size()) );
    issues.push_back( buffer );
  }else
  {
    
    const set<string> nlhsnames( begin(lhs.neutron_detector_names_),
                                end(lhs.neutron_detector_names_) );
    const set<string> nrhsnames( begin(rhs.neutron_detector_names_),
                                end(rhs.neutron_detector_names_) );
    
    if( nlhsnames != nrhsnames )
      issues.push_back( "SpecFile: Neutron detector names dont match for LHS and RHS" );
  }//if( lhs.neutron_detector_names_.size() != rhs.neutron_detector_names_.size() ) / else
  
  if( lhs.lane_number_ != rhs.lane_number_ )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Lane number of LHS (%i) doesnt match RHS (%i)",
             lhs.lane_number_, rhs.lane_number_ );
    issues.push_back( buffer );
  }
  
  if( lhs.measurement_location_name_ != rhs.measurement_location_name_ )
    issues.push_back( "SpecFile: Measurement location name of LHS ('"
                         + lhs.measurement_location_name_
                         + "') doesnt match RHS ('"
                         + rhs.measurement_location_name_ + "')" );
  
  if( lhs.inspection_ != rhs.inspection_ )
    issues.push_back( "SpecFile: Inspection of LHS ('" + lhs.inspection_
                        + "') doesnt match RHS ('" + rhs.inspection_ + "')" );

  string leftoperator = lhs.measurement_operator_;
  string rightoperator = rhs.measurement_operator_;
  ireplace_all( leftoperator, "\t", " " );
  ireplace_all( leftoperator, "  ", " " );
  trim( leftoperator );
  ireplace_all( rightoperator, "\t", " " );
  ireplace_all( rightoperator, "  ", " " );
  trim( rightoperator );
  
  if( leftoperator != rightoperator )
    issues.push_back( "SpecFile: Measurement operator of LHS ('"
                         + lhs.measurement_operator_ + "') doesnt match RHS ('"
                         + rhs.measurement_operator_ + ")" );

  if( lhs.sample_numbers_.size() != rhs.sample_numbers_.size() )
  {
    /*
    cout << "lhs.measurements_.size()=" << lhs.measurements_.size() << endl;
    cout << "rhs.measurements_.size()=" << rhs.measurements_.size() << endl;
    
    for( size_t i = 0; i < lhs.measurements_.size(); ++i )
    {
      cout << "LHS: DetName=" << lhs.measurements_[i]->detector_name_
          << " {" << lhs.measurements_[i]->sample_number_
          << "," << lhs.measurements_[i]->detector_number_ << "}"
           << ", SumGamma=" << lhs.measurements_[i]->gamma_count_sum_
           << ", SumNeutron=" << lhs.measurements_[i]->neutron_counts_sum_
           << ", LiveTime=" << lhs.measurements_[i]->live_time_
           << ", RealTime=" << lhs.measurements_[i]->real_time_
           << ", StarTime=" << lhs.measurements_[i]->start_time_
      << endl;
    }
    
    for( size_t i = 0; i < rhs.measurements_.size(); ++i )
    {
      cout << "RHS: DetName=" << rhs.measurements_[i]->detector_name_
      << " {" << rhs.measurements_[i]->sample_number_
               << "," << rhs.measurements_[i]->detector_number_ << "}"
      << ", SumGamma=" << rhs.measurements_[i]->gamma_count_sum_
      << ", SumNeutron=" << rhs.measurements_[i]->neutron_counts_sum_
      << ", LiveTime=" << rhs.measurements_[i]->live_time_
      << ", RealTime=" << rhs.measurements_[i]->real_time_
      << ", StarTime=" << rhs.measurements_[i]->start_time_
      << endl;
    }
     */
    
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Number of sample numbers in LHS (%i) doesnt match RHS (%i)",
             int(lhs.sample_numbers_.size()), int(rhs.sample_numbers_.size()) );
    issues.push_back( buffer );
  }else
  {
    if( lhs.sample_numbers_ != rhs.sample_numbers_ )
    {
      stringstream lhssamples, rhssamples;
      for( auto sample : lhs.sample_numbers_ )
        lhssamples << (sample==(*lhs.sample_numbers_.begin()) ? "":",") << sample;
      for( auto sample : rhs.sample_numbers_ )
        rhssamples << (sample== (*rhs.sample_numbers_.begin()) ? "":",") << sample;
      issues.push_back( "SpecFile: Sample numbers of RHS (" + rhssamples.str()
                          + ") and LHS (" + lhssamples.str() + ") doesnt match" );
    }
  }//if( lhs.sample_numbers_.size() != rhs.sample_numbers_.size() )
  
  if( lhs.detector_type_ != rhs.detector_type_ )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: LHS detector type (%i - %s) doesnt match RHS (%i - %s)",
             int(lhs.detector_type_), detectorTypeToString(lhs.detector_type_).c_str(),
             int(rhs.detector_type_), detectorTypeToString(rhs.detector_type_).c_str() );
    issues.push_back( buffer );
  }
  
  string lhsinst = convert_n42_instrument_type_from_2006_to_2012( lhs.instrument_type_ );
  string rhsinst = convert_n42_instrument_type_from_2006_to_2012( rhs.instrument_type_ );
  if( lhsinst != rhsinst )
  {
    issues.push_back( "SpecFile: Instrument type of LHS ('" + lhs.instrument_type_
                    + "') doesnt match RHS ('" + rhs.instrument_type_ + "')" );
  }
  
  if( lhs.manufacturer_ != rhs.manufacturer_ )
    issues.push_back( "SpecFile: Manufacturer of LHS ('" + lhs.manufacturer_
                        + "') doesnt match RHS ('" + rhs.manufacturer_ + "')" );
  
  if( lhs.instrument_model_ != rhs.instrument_model_ )
    issues.push_back( "SpecFile: Instrument model of LHS ('" + lhs.instrument_model_
                    + "') doesnt match RHS ('" + rhs.instrument_model_ + "')" );
  
  if( lhs.instrument_id_ != rhs.instrument_id_ )
    issues.push_back( "SpecFile: Instrument ID model of LHS ('" + lhs.instrument_id_
                       + "') doesnt match RHS ('" + rhs.instrument_id_ + "')" );
  
  
  if( fabs(lhs.mean_latitude_ - rhs.mean_latitude_) > 0.000001 )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Mean latitude of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.mean_latitude_, rhs.mean_latitude_ );
    issues.push_back( buffer );
  }

  if( fabs(lhs.mean_longitude_ - rhs.mean_longitude_) > 0.000001 )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Mean longitude of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.mean_longitude_, rhs.mean_longitude_ );
    issues.push_back( buffer );
  }

  if( lhs.properties_flags_ != rhs.properties_flags_ )
  {
    string failingBits;
    auto testBitEqual = [&lhs,&rhs,&failingBits]( MeasurementProperties p, const string label ) {
      if( (lhs.properties_flags_ & p) != (rhs.properties_flags_ & p) ) {
        failingBits += failingBits.empty() ? "": ", ";
        failingBits += (lhs.properties_flags_ & p) ? "LHS" : "RHS";
        failingBits += " has " + label;
      }
    };
    
    testBitEqual( kPassthroughOrSearchMode, "kPassthroughOrSearchMode");
    testBitEqual( kHasCommonBinning, "kHasCommonBinning");
    testBitEqual( kRebinnedToCommonBinning, "kRebinnedToCommonBinning");
    testBitEqual( kAllSpectraSameNumberChannels, "kAllSpectraSameNumberChannels");
    testBitEqual( kNotTimeSortedOrder, "kNotTimeSortedOrder");
    testBitEqual( kNotSampleDetectorTimeSorted, "kNotSampleDetectorTimeSorted");
    testBitEqual( kNotUniqueSampleDetectorNumbers, "kNotUniqueSampleDetectorNumbers" );
    
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Properties flags of LHS (%x) doesnt match RHS (%x) (Failing bits: %s)",
             static_cast<unsigned int>(lhs.properties_flags_),
             static_cast<unsigned int>(rhs.properties_flags_),
             failingBits.c_str() );
    
    issues.push_back( buffer );
  }
  
  for( const int sample : lhs.sample_numbers_ )
  {
    for( const string detname : lhs.detector_names_ )
    {
      std::shared_ptr<const Measurement> lhsptr = lhs.measurement( sample, detname );
      std::shared_ptr<const Measurement> rhsptr = rhs.measurement( sample, detname );
      
      if( (!lhsptr) != (!rhsptr) )
      {
        snprintf( buffer, sizeof(buffer), "SpecFile: Measurement availability for LHS (%s)"
                 " doesnt match RHS (%s) for sample %i and detector name %s",
              (!lhsptr?"missing":"available"), (!rhsptr?"missing":"available"),
              sample, detname.c_str() );
        issues.push_back( buffer );
      }
      
      if( !lhsptr || !rhsptr )
        continue;
      
      try
      {
        Measurement::equal_enough( *lhsptr, *rhsptr );
      }catch( std::exception &e )
      {
        vector<string> error_msgs;
        split( error_msgs, e.what(), "\n" );
        const string msg_prefix = "SpecFile: Sample " + to_string(sample) + ", Detector name " + detname + ": ";
        for( const string &msg : error_msgs )
          issues.push_back( msg_prefix + msg );
      }
    }//for( const int detnum : lhs.detector_numbers_ )
  }//for( const int sample : lhs.sample_numbers_ )
  
  if( (!lhs.detectors_analysis_) != (!rhs.detectors_analysis_) )
  {
    snprintf( buffer, sizeof(buffer), "SpecFile: Detector analysis avaialblity for LHS (%s)"
             " doesnt match RHS (%s)",
             (!lhs.detectors_analysis_?"missing":"available"),
             (!rhs.detectors_analysis_?"missing":"available") );
    issues.push_back( buffer );
  }
  
  vector<string> nlhsremarkss, nrhsremarkss;
  for( string r : lhs.remarks_ )
  {
    while( r.find("  ") != string::npos )
      ireplace_all( r, "  ", " " );
    
    if( !starts_with(r,"N42 file created by") )
      nlhsremarkss.push_back( SpecUtils::trim_copy( r ) );
  }
  
  for( string r : rhs.remarks_ )
  {
    while( r.find("  ") != string::npos )
      ireplace_all( r, "  ", " " );
    
    if( !starts_with(r,"N42 file created by") )
      nrhsremarkss.push_back( SpecUtils::trim_copy( r ) );
  }
  
  
  for( const string &rem : nrhsremarkss )
  {
    if( find(begin(nlhsremarkss), end(nlhsremarkss), rem) == end(nlhsremarkss) )
    {
#if( REQUIRE_REMARKS_COMPARE )
      issues.push_back( "SpecFile: RHS contains remark LHS doesnt: '" + rem + "'" );
#else
      cerr << "SpecFile: RHS contains remark LHS doesnt: '" + rem + "'" << endl;
#endif
    }//if( rem not in nlhsremarkss )
  }//for( const string &rem : nrhsremarkss )
  
  for( const string &rem : nlhsremarkss )
  {
    if( find(begin(nrhsremarkss), end(nrhsremarkss), rem) == end(nrhsremarkss) )
    {
#if( REQUIRE_REMARKS_COMPARE )
      issues.push_back( "SpecFile: LHS contains remark RHS doesnt: '" + rem + "'" );
#else
      cerr << "SpecFile: LHS contains remark RHS doesnt: '" + rem + "'" << endl;
#endif
    }//if( rem not in nrhsremarkss )
  }//for( const string &rem : nlhsremarkss )
  
  
  /*
  if( nlhsremarkss.size() != nrhsremarkss.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Number of remarks in LHS (%i) doesnt match RHS (%i)",
             int(nlhsremarkss.size()), int(nrhsremarkss.size()) );
    
    for( string a : nlhsremarkss )
    cout << "\tLHS: " << a << endl;
    for( string a : nrhsremarkss )
    cout << "\tRHS: " << a << endl;
  
#if( REQUIRE_REMARKS_COMPARE )
    issues.push_back( buffer );
#endif
  }else
  {
    stable_sort( nlhsremarkss.begin(), nlhsremarkss.end() );
    stable_sort( nrhsremarkss.begin(), nrhsremarkss.end() );
    
    const set<string> lhsrems( begin(nlhsremarkss), end(nlhsremarkss) );
    const set<string> rhsrems( begin(nrhsremarkss), end(nrhsremarkss) );
    
    for( size_t i = 0; i < std::max(nlhsremarkss.size(),nrhsremarkss.size()); ++i )
    {
      string lhsremark = i < nlhsremarkss.size() ? nlhsremarkss[i] : string();
      string rhsremark = i < nrhsremarkss.size() ? nrhsremarkss[i] : string();
      SpecUtils::trim( lhsremark );
      SpecUtils::trim( rhsremark );
      
      if( lhsremark != rhsremark )
      {
        snprintf( buffer, sizeof(buffer),
                 "SpecFile: Remark %i in LHS ('%s') doesnt match RHS ('%s')",
                 int(i), lhsremark.c_str(), rhsremark.c_str() );
#if( REQUIRE_REMARKS_COMPARE )
        throw runtime_error( buffer );
#endif
      }
    }
     
  }//if( nlhsremarkss.size() != nrhsremarkss.size() )
   */
  
  
  vector<string> nlhs_parse_warnings, nrhs_parse_warnings;
  for( string r : lhs.parse_warnings_ )
  {
    while( r.find("  ") != string::npos )
      ireplace_all( r, "  ", " " );
    nlhs_parse_warnings.push_back( r );
  }
  
  for( string r : rhs.parse_warnings_ )
  {
    while( r.find("  ") != string::npos )
      ireplace_all( r, "  ", " " );
    nrhs_parse_warnings.push_back( r );
  }
  
  for( const string &rem : nrhs_parse_warnings )
  {
    if( find(begin(nlhs_parse_warnings), end(nlhs_parse_warnings), rem) == end(nlhs_parse_warnings) )
    {
#if( REQUIRE_REMARKS_COMPARE )
      issues.push_back( "SpecFile: RHS contains parse warning LHS doesnt: " + rem );
#else
      cerr << "SpecFile: RHS contains parse warning LHS doesnt: " + rem << endl;
#endif
    }//if( rem not in nlhs_parse_warnings )
  }//for( const string &rem : nrhs_parse_warnings )
  
  for( const string &rem : nlhs_parse_warnings )
  {
    if( find(begin(nrhs_parse_warnings), end(nrhs_parse_warnings), rem) == end(nrhs_parse_warnings) )
    {
#if( REQUIRE_REMARKS_COMPARE )
      issues.push_back( "SpecFile: LHS contains parse warning RHS doesnt: " + rem );
#else
      cerr << "SpecFile: LHS contains parse warning RHS doesnt: " + rem << endl;
#endif
    }//if( rem not in nrhs_parse_warnings )
  }//for( const string &rem : nlhs_parse_warnings )
  
  
  /*
  stable_sort( nlhs_parse_warnings.begin(), nlhs_parse_warnings.end() );
  stable_sort( nrhs_parse_warnings.begin(), nrhs_parse_warnings.end() );
  
  if( nlhs_parse_warnings.size() != nrhs_parse_warnings.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Number of parse warnings in LHS (%i) doesnt match RHS (%i)",
             int(nlhs_parse_warnings.size()), int(nrhs_parse_warnings.size()) );
    
    for( string a : nlhs_parse_warnings )
      cout << "\tLHS: " << a << endl;
    for( string a : nrhs_parse_warnings )
      cout << "\tRHS: " << a << endl;
#if( REQUIRE_REMARKS_COMPARE )
    issues.push_back( buffer );
#endif
  }
  
  for( size_t i = 0; i < std::max(nlhs_parse_warnings.size(),nrhs_parse_warnings.size()); ++i )
  {
    string lhsremark = (i < nlhs_parse_warnings.size()) ? nlhs_parse_warnings[i] : string();
    string rhsremark = (i < nrhs_parse_warnings.size()) ? nrhs_parse_warnings[i] : string();
    SpecUtils::trim( lhsremark );
    SpecUtils::trim( rhsremark );
    
    if( lhsremark != rhsremark )
    {
      snprintf( buffer, sizeof(buffer),
               "SpecFile: Parse Warning %i in LHS ('%s') doesnt match RHS ('%s')",
               int(i), lhsremark.c_str(), rhsremark.c_str() );
#if( REQUIRE_REMARKS_COMPARE )
      throw runtime_error( buffer );
#endif
    }
  }//for( size_t i = 0; i < std::max(nlhs_parse_warnings.size(),nrhs_parse_warnings.size()); ++i )
  */
  
  
  vector<pair<string,string> > lhscompvsn, rhscompvsn;
  
  
  for( size_t i = 0; i < lhs.component_versions_.size(); ++i )
  {
    const string &n = lhs.component_versions_[i].first;
    const string &v = lhs.component_versions_[i].second;
    if( n != "InterSpec" && n!= "InterSpecN42Serialization" && n != "Software"
        && !SpecUtils::istarts_with(n, "Original Software") )
      lhscompvsn.push_back( {trim_copy(n), trim_copy(v)} );
  }
  
  for( size_t i = 0; i < rhs.component_versions_.size(); ++i )
  {
    const string &n = rhs.component_versions_[i].first;
    const string &v = rhs.component_versions_[i].second;
    if( n != "InterSpec" && n!= "InterSpecN42Serialization" && n != "Software"
        && !SpecUtils::istarts_with(n, "Original Software") )
      rhscompvsn.push_back( {trim_copy(n), trim_copy(v)} );
  }
  
  for( const pair<string,string> &rhs : rhscompvsn )
  {
    if( find(begin(lhscompvsn), end(lhscompvsn), rhs) == end(lhscompvsn) )
    {
      issues.push_back( "SpecFile: RHS contains component LHS doesnt: {'"
                       + rhs.first + "', '" + rhs.second + "'}" );
    }
  }//for( const pair<string,string> &rhs : rhscompvsn )
  
  
  for( const pair<string,string> &lhs : lhscompvsn )
  {
    if( find(begin(rhscompvsn), end(rhscompvsn), lhs) == end(rhscompvsn) )
    {
      issues.push_back( "SpecFile: LHS contains component RHS doesnt: {'"
                       + lhs.first + "', '" + lhs.second + "'}" );
    }
  }//for( const pair<string,string> &lhs : lhscompvsn )
  
  /*
  if( lhscompvsn.size() != rhscompvsn.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Number of component versions in LHS (%i) doesnt match RHS (%i)",
             int(lhscompvsn.size()), int(rhscompvsn.size()) );
    
    for( size_t i = 0; i < lhscompvsn.size(); ++i )
      cout << "\tLHS: " << lhscompvsn[i].first << ": " << lhscompvsn[i].second << endl;
    for( size_t i = 0; i < rhscompvsn.size(); ++i )
      cout << "\tRHS: " << rhscompvsn[i].first << ": " << rhscompvsn[i].second << endl;
    throw runtime_error( buffer );
  }
  
  stable_sort( lhscompvsn.begin(), lhscompvsn.end() );
  stable_sort( rhscompvsn.begin(), rhscompvsn.end() );
  
  for( size_t i = 0; i < lhscompvsn.size(); ++i )
  {
    pair<string,string> lhsp = lhscompvsn[i];
    pair<string,string> rhsp = rhscompvsn[i];
    
    SpecUtils::trim( lhsp.first );
    SpecUtils::trim( lhsp.second );
    SpecUtils::trim( rhsp.first );
    SpecUtils::trim( rhsp.second );
    
    if( lhsp.first != rhsp.first )
    {
      snprintf( buffer, sizeof(buffer),
               "SpecFile: Component Version %i name in LHS ('%s') doesnt match RHS ('%s')",
               int(i), lhsp.first.c_str(), rhsp.first.c_str() );
      throw runtime_error( buffer );
    }
    
    if( lhsp.second != rhsp.second )
    {
      snprintf( buffer, sizeof(buffer),
               "SpecFile: Component Version %i valiue in LHS ('%s') doesnt match RHS ('%s')",
               int(i), lhsp.second.c_str(), rhsp.second.c_str() );
      throw runtime_error( buffer );
    }
  }//for( size_t i = 0; i < lhscompvsn.size(); ++i )
  */
  
  //Make UUID last, since its sensitive to the other variables changing, and we
  //  want to report on those first to make fixing easier.
  if( lhs.detectors_analysis_ && rhs.detectors_analysis_ )
  {
    try
    {
      DetectorAnalysis::equal_enough( *lhs.detectors_analysis_,
                                     *rhs.detectors_analysis_ );
    }catch( std::exception &e )
    {
      issues.push_back( e.what() );
    }
  }//if( lhs.detectors_analysis_ && rhs.detectors_analysis_ )
  
  
  if( lhs.multimedia_data_.size() != rhs.multimedia_data_.size() )
  {
    issues.push_back( "LHS has " + std::to_string(lhs.multimedia_data_.size()) +
                     " multimedia entries, but RHS has "
                     + std::to_string(rhs.multimedia_data_.size()) );
  }else
  {
    for( size_t i = 0; i < lhs.multimedia_data_.size(); ++i )
    {
      assert( lhs.multimedia_data_[i] && rhs.multimedia_data_[i] );
      if( !lhs.multimedia_data_[i] || !rhs.multimedia_data_[i] )
      {
        issues.push_back( "Unexpected nullptr in multimedia_data_" );
        continue;
      }

      try
      {
        MultimediaData::equal_enough( *lhs.multimedia_data_[i],
                                     *rhs.multimedia_data_[i] );
      }catch( std::exception &e )
      {
        issues.push_back( "Mulimedia entry " + std::to_string(i)
                         + " does not match between LHS and RHS: "
                         + string(e.what()) );
      }// try / catch
    }//for( loop over multimedias )
  }//if( not same num images ) / else
  
  
  /*
#if( !defined(WIN32) )
  if( lhs.uuid_ != rhs.uuid_ )
   issues.push_back( "SpecFile: UUID of LHS (" + lhs.uuid_
                        + ") doesnt match RHS (" + rhs.uuid_ + ")" );
#endif
  */
  
//  bool modified_;
//  bool modifiedSinceDecode_;
  
  string error_msg;
  for( const string &msg : issues )
    error_msg += (error_msg.empty() ? "" : "\n") + msg;
  
  if( !error_msg.empty() )
    throw runtime_error( error_msg );
}//void equal_enough( const Measurement &lhs, const Measurement &rhs )
  
  
void MultimediaData::equal_enough( const MultimediaData &lhs, const MultimediaData &rhs )
{
  if( lhs.remark_ != rhs.remark_ )
    throw runtime_error( "MultimediaData: LHS remark doesnt match RHS" );
  
  if( lhs.descriptions_ != rhs.descriptions_ )
    throw runtime_error( "MultimediaData: LHS description doesnt match RHS" );
  
  if( lhs.data_ != rhs.data_ )
    throw runtime_error( "MultimediaData: LHS data doesnt match RHS" );
  
  if( lhs.data_encoding_ != rhs.data_encoding_ )
    throw runtime_error( "MultimediaData: LHS EncodingType doesnt match RHS" );
  
  if( lhs.capture_start_time_ != rhs.capture_start_time_ )
    throw runtime_error( "MultimediaData: LHS Capture Start Time doesnt match RHS" );
  
  if( lhs.file_uri_ != rhs.file_uri_ )
    throw runtime_error( "MultimediaData: LHS File URI doesnt match RHS" );

  if( lhs.mime_type_ != rhs.mime_type_ )
    throw runtime_error( "MultimediaData: LHS Myme Type doesnt match RHS" );
  
  //<MultimediaFileSizeValue>,
  //<MultimediaDataMIMEKind>,
}//void MultimediaData::equal_enough(...)

#endif //#if( SpecUtils_ENABLE_EQUALITY_CHECKS )


SpecFile::SpecFile()
{
  reset();
}


SpecFile::~SpecFile()
{
}


SpecFile::SpecFile( const SpecFile &rhs )
{
  *this = rhs;
}


const SpecFile &SpecFile::operator=( const SpecFile &rhs )
{
  if( this == &rhs )
    return *this;
//  std::unique_lock<std::recursive_mutex> lhs_lock( mutex_, std::defer_lock );
//  std::unique_lock<std::recursive_mutex> rhs_lock( rhs.mutex_, std::defer_lock );

  //XXX - this next section of code seems to really cause troubles!
  //      need to investigate it, and whatever code is calling it
  std::lock<std::recursive_mutex,std::recursive_mutex>( mutex_, rhs.mutex_ );
  std::unique_lock<std::recursive_mutex> lhs_lock( mutex_, std::adopt_lock_t() );
  std::unique_lock<std::recursive_mutex> rhs_lock( rhs.mutex_, std::adopt_lock_t() );

  reset();
  
  gamma_live_time_        = rhs.gamma_live_time_;
  gamma_real_time_        = rhs.gamma_real_time_;
  gamma_count_sum_        = rhs.gamma_count_sum_;
  neutron_counts_sum_     = rhs.neutron_counts_sum_;

  filename_               = rhs.filename_;
  detector_names_         = rhs.detector_names_;
  detector_numbers_       = rhs.detector_numbers_;
  gamma_detector_names_   = rhs.gamma_detector_names_;
  neutron_detector_names_ = rhs.neutron_detector_names_;

  uuid_                   = rhs.uuid_;
  remarks_                = rhs.remarks_;
  parse_warnings_         = rhs.parse_warnings_;
  lane_number_             = rhs.lane_number_;
  measurement_location_name_ = rhs.measurement_location_name_;
  inspection_             = rhs.inspection_;
  measurement_operator_    = rhs.measurement_operator_;
  sample_numbers_         = rhs.sample_numbers_;
  sample_to_measurements_  = rhs.sample_to_measurements_;
  detector_type_          = rhs.detector_type_;
  instrument_type_        = rhs.instrument_type_;
  manufacturer_           = rhs.manufacturer_;
  instrument_model_       = rhs.instrument_model_;
  instrument_id_          = rhs.instrument_id_;
  component_versions_     = rhs.component_versions_;
  mean_latitude_          = rhs.mean_latitude_;
  mean_longitude_         = rhs.mean_longitude_;
  detectors_analysis_     = rhs.detectors_analysis_;
  multimedia_data_        = rhs.multimedia_data_;
  properties_flags_       = rhs.properties_flags_;
  
  modified_               = rhs.modified_;
  modifiedSinceDecode_    = rhs.modifiedSinceDecode_;
  
  measurements_.clear();
  for( size_t i = 0; i < rhs.measurements_.size(); ++i )
    measurements_.push_back( make_shared<Measurement>( *rhs.measurements_[i] ) );

/*
  //As long as we've done everything right above, we shouldnt need to call
  //  either of the following, right?
  cleanup_after_load();
*/

  return *this;
}//operator=





const Measurement &Measurement::operator=( const Measurement &rhs )
{
  if( this == &rhs )
    return *this;

  live_time_ = rhs.live_time_;
  real_time_ = rhs.real_time_;

  contained_neutron_ = rhs.contained_neutron_;

  sample_number_ = rhs.sample_number_;
  occupied_ = rhs.occupied_;
  gamma_count_sum_ = rhs.gamma_count_sum_;
  neutron_live_time_ = rhs.neutron_live_time_;
  neutron_counts_sum_ = rhs.neutron_counts_sum_;
  
  detector_name_ = rhs.detector_name_;
  detector_number_ = rhs.detector_number_;
  detector_description_ = rhs.detector_description_;
  quality_status_ = rhs.quality_status_;

  source_type_ = rhs.source_type_;

  remarks_ = rhs.remarks_;
  start_time_ = rhs.start_time_;

  energy_calibration_ = rhs.energy_calibration_;
  gamma_counts_ = rhs.gamma_counts_;

//  if( rhs.gamma_counts_ )
//    gamma_counts_.reset( new vector<float>( *rhs.gamma_counts_ ) );
//  else
//    gamma_counts_ = std::make_shared<vector<float>>();

  neutron_counts_ = rhs.neutron_counts_;

  title_ = rhs.title_;

  derived_data_properties_ = rhs.derived_data_properties_;
  
  dose_rate_ = rhs.dose_rate_;
  exposure_rate_ = rhs.exposure_rate_;
  
  location_ = rhs.location_;
  
  return *this;
}//const Measurement &operator=( const Measurement &rhs )





bool SpecFile::load_file( const std::string &filename,
               ParserType parser_type,
               std::string orig_file_ending )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  bool success = false;
  switch( parser_type )
  {
    case ParserType::N42_2006:
    case ParserType::N42_2012:
      success = load_N42_file( filename );
    break;

    case ParserType::Spc:
      success = load_spc_file( filename );
    break;

    case ParserType::Exploranium:
      success = load_binary_exploranium_file( filename );
    break;

    case ParserType::Pcf:
      success = load_pcf_file( filename );
    break;

    case ParserType::Chn:
      success = load_chn_file( filename );
    break;

    case ParserType::SpeIaea:
      success = load_iaea_file( filename );
    break;

    case ParserType::TxtOrCsv:
      success = load_txt_or_csv_file( filename );
    break;

    case ParserType::Cnf:
      success = load_cnf_file( filename );
    break;
      
    case ParserType::TracsMps:
      success = load_tracs_mps_file( filename );
    break;
      
    case ParserType::Aram:
      success = load_aram_file( filename );
    break;
      
    case ParserType::SPMDailyFile:
      success = load_spectroscopic_daily_file( filename );
    break;
      
    case ParserType::AmptekMca:
      success = load_amptek_file( filename );
    break;
      
    case ParserType::OrtecListMode:
      success = load_ortec_listmode_file( filename );
    break;
      
    case ParserType::LsrmSpe:
      success = load_lsrm_spe_file( filename );
      break;
      
    case ParserType::Tka:
      success = load_tka_file( filename );
      break;
      
    case ParserType::MultiAct:
      success = load_multiact_file( filename );
      break;
      
    case ParserType::Phd:
      success = load_phd_file( filename );
      break;
      
    case ParserType::Lzs:
      success = load_lzs_file( filename );
      break;
      
    case ParserType::ScanDataXml:
      success = load_xml_scan_data_file( filename );
      break;
      
    case ParserType::Json:
      success = load_xml_scan_data_file(filename);
      break;

    case ParserType::CaenHexagonGXml:
      success = load_caen_gxml_file( filename );
      break;
      
#if( SpecUtils_ENABLE_URI_SPECTRA )
    case ParserType::Uri:
      success = load_uri_file( filename );
      break;
#endif
      
    case ParserType::MicroRaider:
      success = load_json_file( filename );
    break;
      
    case ParserType::RadiaCode:
      success = load_radiacode_file( filename );
    break;

    case ParserType::Auto:
    {
      bool triedPcf = false, triedSpc = false,
          triedNativeIcd1 = false, triedTxt = false, triedGR135 = false,
          triedChn = false, triedIaea = false, triedLsrmSpe = false,
          triedCnf = false, triedMps = false, triedSPM = false, triedMCA = false,
          triedOrtecLM = false, triedMicroRaider = false, triedAram = false,
          triedTka = false, triedMultiAct = false, triedPhd = false,
          triedLzs = false, triedXmlScanData = false, triedJson = false,
          tried_gxml = false, triedRadiaCode = false, tried_uri = false;
      
      if( orig_file_ending.empty() )
        orig_file_ending = filename;

      if( !orig_file_ending.empty() )
      {
        const size_t period_pos = orig_file_ending.find_last_of( '.' );
        if( period_pos != string::npos )
          orig_file_ending = orig_file_ending.substr( period_pos+1 );
        to_lower_ascii( orig_file_ending );

        if( orig_file_ending=="pcf")
        {
          triedPcf = true;
          success = load_pcf_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="pcf")

        if( orig_file_ending=="dat")
        {
          triedGR135 = true;
          success = load_binary_exploranium_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="dat")

        if( orig_file_ending=="spc")
        {
          triedSpc = true;
          success = load_spc_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="dat")

        if( orig_file_ending=="n42" || orig_file_ending=="xml"
            || orig_file_ending=="icd1" || orig_file_ending=="icd")
        {
          triedNativeIcd1 = true;
          success = load_N42_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="n42")
        
        if( orig_file_ending=="chn" )
        {
          triedChn = true;
          success = load_chn_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="chn" )

        if( orig_file_ending=="spe" )
        {
          triedIaea = true;
          success = load_iaea_file( filename );
          if( success ) break;
          
          triedLsrmSpe = true;
          success = load_lsrm_spe_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="chn" )
        
        if( orig_file_ending=="tka" || orig_file_ending=="jac" )
        {
          triedTka = true;
          success = load_tka_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="chn" )
        
        if( orig_file_ending=="spm" )
        {
          triedMultiAct = true;
          success = load_multiact_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="chn" )
        
        if( orig_file_ending=="txt" )
        {
          triedSPM = true;
          success = load_spectroscopic_daily_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="txt" )
        
        if( orig_file_ending=="txt" || orig_file_ending=="csv" )
        {
          triedTxt = true;
          success = load_txt_or_csv_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="txt" || orig_file_ending=="csv" )

        if( orig_file_ending=="cnf" )
        {
          triedCnf = true;
          success = load_cnf_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="txt" || orig_file_ending=="csv" )
        
        if( orig_file_ending=="mps" )
        {
          triedMps = true;
          success = load_tracs_mps_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="txt" || orig_file_ending=="csv" )
        
        if( orig_file_ending=="gam" /* || orig_file_ending=="neu" */ )
        {
          triedAram = true;
          success = load_aram_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="txt" || orig_file_ending=="csv" )
        
        if( orig_file_ending=="mca" )
        {
          triedMCA = true;
          success = load_amptek_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="txt" || orig_file_ending=="csv" )

        if( orig_file_ending=="lis" )
        {
          triedOrtecLM = true;
          success = load_ortec_listmode_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="txt" || orig_file_ending=="csv" )
        
        if( orig_file_ending=="phd" )
        {
          triedPhd = true;
          success = load_phd_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="xml" )
        
        if( orig_file_ending=="lzs" )
        {
          triedLzs = true;
          success = load_lzs_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="xml" )
        
        
        if( orig_file_ending=="xml" )
        {
          triedMicroRaider = true;
          success = load_micro_raider_file( filename );
          if( success ) break;
          
          triedXmlScanData = true;
          success = load_xml_scan_data_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="xml" )


        if( orig_file_ending=="xml" || orig_file_ending=="rco")
        {
          triedRadiaCode = true;
          success = load_radiacode_file( filename );
          if( success ) break;
        }//if( orig_file_ending=="xml" || orig_file_ending=="rco")


        if( orig_file_ending == "json" )
        {
          triedJson = true;
          success = load_json_file(filename);
          if( success ) break;
        }//if( orig_file_ending=="xml" )


        if( orig_file_ending == "gxml" )
        {
          tried_gxml = true;
          success = load_caen_gxml_file(filename);
          if( success ) break;
        }//if( orig_file_ending=="gxml" )

        
#if( SpecUtils_ENABLE_URI_SPECTRA )
        if( istarts_with(filename, "raddata://") || (orig_file_ending == "uri") )
        {
          tried_uri = true;
          success = load_uri_file(filename);
          if( success ) break;
        }//if( orig_file_ending=="gxml" )
#endif
      }//if( !orig_file_ending.empty() ) / else

      if( !success && !triedSpc )
        success = load_spc_file( filename );

      if( !success && !triedGR135 )
        success = load_binary_exploranium_file( filename );

      if( !success && !triedNativeIcd1 )
        success = load_N42_file( filename );

      if( !success && !triedPcf )
        success = load_pcf_file( filename );

      if( !success && !triedChn )
        success = load_chn_file( filename );

      if( !success && !triedIaea )
        success = load_iaea_file( filename );

      if( !success && !triedSPM )
        success = load_spectroscopic_daily_file( filename );
      
      if( !success && !triedTxt )
        success = load_txt_or_csv_file( filename );

      if( !success && !triedCnf )
        success = load_cnf_file( filename );
      
      if( !success && !triedMps )
        success = load_tracs_mps_file( filename );
      
      if( !success && !triedMCA )
        success = load_amptek_file( filename );
      
      if( !success && !triedMicroRaider )
        success = load_micro_raider_file( filename );
      
      if( !success && !triedAram )
        success = load_aram_file( filename );
      
      if( !success && !triedLsrmSpe )
        success = load_lsrm_spe_file( filename );
      
      if( !success && !triedTka )
        success = load_tka_file( filename );
      
      if( !success && !triedMultiAct )
        success = load_multiact_file( filename );
      
      if( !success && !triedPhd )
        success = load_phd_file( filename );
      
      if( !success && !triedLzs )
        success = load_lzs_file( filename );
      
      if( !success && !triedOrtecLM )
        success = load_ortec_listmode_file( filename );
      
      if( !success && !triedXmlScanData )
        success = load_xml_scan_data_file( filename );
      
      if( !success && !triedJson )
        success = load_json_file( filename );

      if( !success && !tried_gxml )
        success = load_caen_gxml_file( filename );
      
#if( SpecUtils_ENABLE_URI_SPECTRA )
      if( !success && !tried_uri )
        success = load_uri_file( filename );
#endif
      
      if( !success && !triedRadiaCode )
        success = load_radiacode_file( filename );
      
       break;
    }//case Auto
  };//switch( parser_type )

  set_filename( filename );

  if( num_measurements() == 0 )
    reset();

  return (success && num_measurements());
}//bool load_file(...)



bool comp_by_start_time_source( const std::shared_ptr<Measurement> &lhs,
                           const std::shared_ptr<Measurement> &rhs )
{
  if( !lhs || !rhs)
    return (!rhs) < (!lhs);
  
  const time_point_t &left = lhs->start_time();
  const time_point_t &right = rhs->start_time();
  
  if( left == right )
    return (lhs->source_type() < rhs->source_type());
  
  if( is_special(left) && !is_special(right) )
    return true;
  
  return (left < right);
}

void SpecFile::set_sample_numbers_by_time_stamp( const bool sort_meas_types_separately,
                                                const bool force_unique )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  if( measurements_.empty() )
    return;

  //If we're here, we need to (re) assign some sample numbers
  
  //This function can be really slow, so I'm experimenting with a faster way
  //  For a certain large file in debug mode, took from 9.058687s to 0.232338s
  //  (in release mode this was from 1.808691s to 0.053467s)
  //  Right now this faster method is only enabled for really large files with
  //  more than 500 measurements - this is because the faster method does not
  //  preserve existing sample numbers

  if( (measurements_.size() > 500) || force_unique )
  {
    vector<std::shared_ptr<Measurement>> sorted_meas, sorted_foreground, sorted_calibration, sorted_background, sorted_derived;

    sorted_meas.reserve( measurements_.size() );

    for( auto &m : measurements_ )
    {
      if( !m )
        continue;
      
      if( !sort_meas_types_separately )
      {
        sorted_foreground.push_back( m );
        continue;
      }
      
      if( m->derived_data_properties() )
      {
        sorted_derived.push_back( m );
        continue;
      }
      
      switch( m->source_type_ )
      {
        case SourceType::IntrinsicActivity:
        case SourceType::Calibration:
          sorted_calibration.push_back( m );
          break;
            
        case SourceType::Background:
          sorted_background.push_back( m );
          break;
            
        case SourceType::Foreground:
        case SourceType::Unknown:
        default:
          sorted_foreground.push_back( m );
      }//switch( m->source_type_ )
    }//for( auto &m : measurements_ )

    stable_sort( sorted_calibration.begin(), sorted_calibration.end(), &comp_by_start_time_source );
    stable_sort( sorted_background.begin(), sorted_background.end(), &comp_by_start_time_source );
    stable_sort( sorted_foreground.begin(), sorted_foreground.end(), &comp_by_start_time_source );
    stable_sort( sorted_derived.begin(), sorted_derived.end(), &comp_by_start_time_source );
    
    sorted_meas.insert( end(sorted_meas), sorted_calibration.begin(), sorted_calibration.end() );
    sorted_meas.insert( end(sorted_meas), sorted_background.begin(), sorted_background.end() );
    sorted_meas.insert( end(sorted_meas), sorted_foreground.begin(), sorted_foreground.end() );
    sorted_meas.insert( end(sorted_meas), sorted_derived.begin(), sorted_derived.end() );
  
    
    int sample_num = 1;
    vector<std::shared_ptr<Measurement>>::iterator start, end, iter;
    for( start = end = sorted_meas.begin(); start != sorted_meas.end(); start = end )
    {
      //Incremement sample numbers for each new start time.
      //  Also, some files (see refMIONLOV4PS) will mix occupied/non-occupied
      //  samples, so increment on this as well.
      
      while( (end != sorted_meas.end())
             && ((*end)->start_time_ == (*start)->start_time_)
             && ((*end)->occupied_ == (*start)->occupied_) )
        ++end;

      typedef map<string,int> StrIntMap;
      StrIntMap detectors;

      for( iter = start; iter != end; ++iter )
      {
        std::shared_ptr<Measurement> &meas = *iter;

        if( detectors.count(meas->detector_name_) == 0 )
          detectors[meas->detector_name_] = 0;
        else
          ++detectors[meas->detector_name_];

        meas->sample_number_ = sample_num + detectors[meas->detector_name_];
      }//for( iter = start; iter != end; ++iter )

      int largest_delta = 0;
      for( const StrIntMap::value_type &val : detectors )
        largest_delta = max( largest_delta, val.second );

      sample_num = sample_num + largest_delta + 1;
    }//for( loop over time ranges )

  }else
  {
    typedef std::map<int, vector<std::shared_ptr<Measurement> > > SampleToMeasMap;
    typedef map<time_point_t, SampleToMeasMap > TimeToSamplesMeasMap;
    
    //If derived data, we'll put it after non-derived data
    //If the time is invalid, we'll put this measurement after all the others, but before derived
    //If its an IntrinsicActivity, we'll put it before any of the others.
    TimeToSamplesMeasMap time_meas_map;
    SampleToMeasMap intrinsics, derived_data, invalid_times;

    for( const auto &m : measurements_ )
    {
      if( !m )
        continue;
      
      const int detnum = m->detector_number_;
      
      if( !sort_meas_types_separately )
      {
        if( is_special(m->start_time_) )
          invalid_times[detnum].push_back( m );
        else
          time_meas_map[m->start_time_][detnum].push_back( m );
      }else if( m->derived_data_properties() )
      {
        derived_data[detnum].push_back( m );
      }else if( m->source_type() == SourceType::IntrinsicActivity )
      {
        intrinsics[detnum].push_back( m );
      }else if( is_special(m->start_time_) )
      {
        invalid_times[detnum].push_back( m );
      }else
      {
        time_meas_map[m->start_time_][detnum].push_back( m );
      }
    }//for( auto &m : measurements_ )
    
    //Now for simpleness, lets toss intrinsics, derived_data, and invalid_times into time_meas_map
    const auto year = std::chrono::seconds(365*24*60*60);
    const time_point_t intrinsics_time = (time_meas_map.empty()
                                          ? time_point_t{}
                                          : time_meas_map.begin()->first)
                                         - year;
    const time_point_t invalids_time = (time_meas_map.empty()
                                        ? time_point_t{}
                                        : time_meas_map.rbegin()->first)
                                         + year;
    const time_point_t derived_time = (time_meas_map.empty()
                                       ? time_point_t{}
                                       : time_meas_map.rbegin()->first)
                                       + 2*year;
    
    for( const auto &sample_meass : intrinsics )
      for( const auto &m : sample_meass.second )
        time_meas_map[intrinsics_time][sample_meass.first].push_back( m );
    
    for( const auto &sample_meass : invalid_times )
      for( const auto &m : sample_meass.second )
        time_meas_map[invalids_time][sample_meass.first].push_back( m );
    
    for( const auto &sample_meass : derived_data )
      for( const auto &m : sample_meass.second )
        time_meas_map[derived_time][sample_meass.first].push_back( m );
    
    
    int sample = 1;
    
    for( TimeToSamplesMeasMap::value_type &t : time_meas_map )
    {
      SampleToMeasMap &measmap = t.second;
      
      //Make an attempt to sort the measurements in a reproducible, unique way
      //  because measurements wont be in the same order due to the decoding
      //  being multithreaded for some of the parsers.
//20150609: I think the multithreaded parsing has been fixed to yield a
//  deterministic ordering always.  This only really matters for spectra where
//  the same start time is given for all records, or no start time at all is
//  given.  I think in these cases we want to assume the order that the
//  measurements were taken is the same as the order in the file.

      
      size_t nsamples = 0;
      for( const SampleToMeasMap::value_type &s : measmap )
        nsamples = std::max( nsamples, s.second.size() );
      
      for( size_t i = 0; i < nsamples; ++i )
      {
        for( SampleToMeasMap::value_type &s : measmap )
        {
          if( i < s.second.size() )
            s.second[i]->sample_number_ = sample;
        }
        
        ++sample;
      }//for( size_t i = 0; i < nsamples; ++i )
    }//for( const TimeToSamplesMeasMap::value_type &t : time_meas_map )
    
  }//if( measurements_.size() > 500 ) / else
  
  stable_sort( begin(measurements_), end(measurements_), &compare_by_derived_sample_det_time );
}//void  set_sample_numbers_by_time_stamp()


bool SpecFile::has_unique_sample_and_detector_numbers() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  //First we will check that the comnination of sample numbers and detector
  //  numbers is unique, and also that all measurements with the same sample
  //  number have the same time stamp.  If we pass both of thers conditions,
  //  then we'll return since there is no need re-assign sample numbers.
  std::map<int, vector<int> > sampleNumsToSamples;
  std::map<int,std::set<time_point_t> > sampleToTimes;
  const size_t nmeas = measurements_.size();
  for( size_t i = 0; i < nmeas; ++i )
  {
    const std::shared_ptr<Measurement> &m = measurements_[i];
    vector<int> &meass = sampleNumsToSamples[m->sample_number_];
    
    if( std::find(meass.begin(),meass.end(),m->detector_number_) != meass.end() )
    {
      //cerr << "Unique check failed for sample with Start time "
      //     << SpecUtils::to_iso_string(m->start_time()) << " detname=" << m->detector_name()
      //     << " sample " << m->sample_number() << endl;
      return false;
    }
    
    meass.push_back( m->detector_number_ );
    
    std::set<time_point_t> &timesSet = sampleToTimes[m->sample_number_];
    if( !is_special(m->start_time_) )
      timesSet.insert( m->start_time_ );
    if( timesSet.size() > 1 )
      return false;
  }//for( std::shared_ptr<Measurement> &m : measurements_ )
  
  return true;
}//bool has_unique_sample_and_detector_numbers() const


void SpecFile::ensure_unique_sample_numbers()
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( has_unique_sample_and_detector_numbers() )
  {
    stable_sort( measurements_.begin(), measurements_.end(), &compare_by_derived_sample_det_time );
  }else
  {
    set_sample_numbers_by_time_stamp( true, false );
  }
  
  //XXX - TODO should validate this a little further and check performance
  //      impact!  Also, should be proactive about not needing to hit the
  //      expensive "fix" below.
  //
  //Here we will check the first two sample number, and if they are '1' and '2'
  //  respectively, we will not do anything.  If first sample is not 1, but
  //  second sample is 2, we will change first sample to 1.  Otherwise if
  //  first two sample numbers isnt {1,2}, we will change all sample numbers to
  //  start at 1 and increase continuously by 1.
  //  (note: this mess of logic is "inspired" by heuristics, and that actually
  //   looping through all measurements of a large file is expensive)
  set<int> sample_numbers;
  for( size_t i = 0; (sample_numbers.size() < 3) && (i < measurements_.size()); ++i )
    sample_numbers.insert( measurements_[i]->sample_number_ );
  
  if( sample_numbers.empty() )
    return;
  
  if( sample_numbers.size() == 1 )
  {
    for( auto &m : measurements_ )
    {
      //Some files will give SampleNumber an attribute of the <Spectrum> tag, but those dont always
      //  have the same meaning as how we want it to.
      //if( m->sample_number_ <= 0 )
        m->sample_number_ = 1;
    }
    return;
  }
  
  const auto first_val = sample_numbers.begin();
  const auto second_val = next(first_val);
  int first_sample_val = *first_val;
  const int second_sample_val = *second_val;
  if( (first_sample_val + 1) != second_sample_val )
    first_sample_val = second_sample_val - 1;  //garunteed first_sample_val will be unique
  
  if( second_sample_val != 2 )
  {
    for( auto &m : measurements_ )
      sample_numbers.insert( m->sample_number_ );
    
    const vector<int> sample_numbers_vec( sample_numbers.begin(), sample_numbers.end() );
    const auto sn_begin = begin(sample_numbers_vec);
    const auto sn_end = end(sample_numbers_vec);
    
    for( auto &m : measurements_ )
    {
      auto pos = lower_bound( sn_begin, sn_end, m->sample_number_ );
      m->sample_number_ = static_cast<int>( (pos - sn_begin) + 1 );
    }
    
    return;
  }//if( (*first_val) != 1 )
  
  if( first_sample_val != (*first_val) )
  {
    const int old_first_sample = *first_val;
    for( size_t i = 0; (measurements_[i]->sample_number_!=second_sample_val) && (i < measurements_.size()); ++i )
      if( measurements_[i]->sample_number_ == old_first_sample )
        measurements_[i]->sample_number_ = first_sample_val;
  }
}//void ensure_unique_sample_numbers()


std::set<std::string> SpecFile::find_detector_names() const
{
  set<string> det_names;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  for( const auto &meas : measurements_ )
    det_names.insert( meas->detector_name_ );

  return det_names;
}//set<string> find_detector_names() const


void SpecFile::cleanup_after_load( const unsigned int flags )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const bool rebinToCommonBinning = (flags & RebinToCommonBinning);
  
  
  const bool dont_reorder = (flags & DontChangeOrReorderSamples);
  const bool reorder_by_time = (flags & ReorderSamplesByTime);
  
  assert( (dont_reorder + reorder_by_time) <= 1 );
  if( (dont_reorder + reorder_by_time) > 1 )
    throw std::logic_error( "SpecFile::cleanup_after_load: you may only specify one ordering; "
                           "options received=" + std::to_string(flags) );
  
  //When loading the example passthrough N42 file, this function
  //  take about 60% of the parse time - due almost entirely to
  //  SpecFile::rebin_by_polunomial_eqn
  try
  {
    map<size_t,shared_ptr<EnergyCalibration>> default_energy_cal;
    set<string> gamma_detector_names; //can be gamma+nutron
    const set<string> det_names = find_detector_names();
    
    int nGpsCoords = 0;
    mean_latitude_ = mean_longitude_ = 0.0;
    
    properties_flags_ = 0x0;
    
    vector<string> names( det_names.begin(), det_names.end() );
    typedef map<int,string> IntStrMap;
    IntStrMap num_to_name_map;
    set<string> gamma_det_names, neut_det_names;  //If a measurement contains neutrons at all, will be added to this.
    map<string,map<size_t,shared_ptr<const EnergyCalibration>>> missing_cal_fixs;
    
    
    
    map<EnergyCalibration,shared_ptr<const EnergyCalibration>> unique_cals;
    vector<string>::const_iterator namepos;
    
    int numNeutronAndGamma = 0, numWithGammas = 0, numWithNeutrons = 0;
    bool neutronMeasDoNotHaveGamma = true, haveNeutrons = false, haveGammas = false;
    
    SpecUtils::trim( instrument_id_ );
    
    for( size_t meas_index = 0; meas_index < measurements_.size(); ++meas_index )
    {
      std::shared_ptr<Measurement> &meas = measurements_[meas_index];
      
      namepos = find( names.begin(), names.end(), meas->detector_name_ );
      if( namepos != names.end() )
      {
        meas->detector_number_ = static_cast<int>( namepos - names.begin() );
        num_to_name_map[meas->detector_number_] = meas->detector_name_;
      }else
      {
#if( PERFORM_DEVELOPER_CHECKS )
        char buffer[1024];
        snprintf( buffer, sizeof(buffer),
                  "Couldnt find detector '%s' in names - probably shouldnt ever happen",
                  meas->detector_name_.c_str() );
        log_developer_error( __func__, buffer );
#endif
      }
      
      if( meas->gamma_counts_ && meas->gamma_counts_->size() )
        gamma_detector_names.insert( meas->detector_name_ );
      
#if( PERFORM_DEVELOPER_CHECKS )
      if( meas->neutron_counts_sum_ > 0.00001 && !meas->contained_neutron_ )
      {
        char buffer[1024];
        snprintf( buffer, sizeof(buffer),
                 "Spectrum contained %f neutrons, but neutron_counts_sum_ was not set. File=\"%s\"",
                 meas->neutron_counts_sum_, filename_.c_str() );
        log_developer_error( __func__, buffer );
      }
#endif
      
      if( meas->gamma_counts_ && !meas->gamma_counts_->empty() )
        gamma_det_names.insert( meas->detector_name_ );
      
      if( meas->contained_neutron_ )
        neut_det_names.insert( meas->detector_name_ );
      
      const bool thisMeasHasGamma = (meas->gamma_counts_ && !meas->gamma_counts_->empty());
      haveGammas = (haveGammas || thisMeasHasGamma);
      haveNeutrons = (haveNeutrons || meas->contained_neutron_);
      
      numWithGammas += thisMeasHasGamma;
      numWithNeutrons += meas->contained_neutron_;
      numNeutronAndGamma += (meas->contained_neutron_ && thisMeasHasGamma);
      
      if( meas->contained_neutron_ && thisMeasHasGamma )
        neutronMeasDoNotHaveGamma = false;
      
      // Check if 'meas' is N42-2012 DerivedData or not
      const bool nonDerived = !meas->derived_data_properties_;
      properties_flags_ |= (nonDerived ? MeasurementProperties::kNonDerivedDataMeasurements
                                       : MeasurementProperties::kDerivedDataMeasurements);
      
      //Do a basic sanity check of is the calibration is reasonable.
      const size_t nchannel = (meas->gamma_counts_ ? meas->gamma_counts_->size() : size_t(0));
      if( nchannel >= 2 )
      {
        auto cal = meas->energy_calibration_;
        
        /// \TODO: This whole switch could be made to be done if PERFORM_DEVELOPER_CHECKS ...
        switch( cal->type() )
        {
          case EnergyCalType::Polynomial:
          case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
          case EnergyCalType::FullRangeFraction:
          case EnergyCalType::LowerChannelEdge:
            if( !cal->num_channels() )
            {
#if( PERFORM_DEVELOPER_CHECKS )
              log_developer_error( __func__, "Found a energy calibration with with missing"
                                  " channel energies " );
#endif //#if( PERFORM_DEVELOPER_CHECKS )
              meas->energy_calibration_ = make_shared<EnergyCalibration>();
              cal = meas->energy_calibration_;
            }
            
            if( cal->num_channels() != meas->gamma_counts_->size() )
            {
#if( PERFORM_DEVELOPER_CHECKS )
              assert( cal->num_channels() == meas->gamma_counts_->size() );
              log_developer_error( __func__, "Found a energy calibration with different number"
                                   " of channels than gamma spectrum" );
#endif //#if( PERFORM_DEVELOPER_CHECKS )
              meas->energy_calibration_ = make_shared<EnergyCalibration>();
              cal = meas->energy_calibration_;
            }
            break;
            
          case EnergyCalType::InvalidEquationType:
            //Nothing to check here.
            break;
        }//switch( cal->type() )
        
        //If we dont have a energy calibration, but do have a gamma spectrum, lets look for a
        //  calibration from the same detector, but other Measurement.  If we cant do that then lets
        //  at least have all the EnergyCalType::InvalidEquationType point to the same object.
        if( meas->energy_calibration_->type() == EnergyCalType::InvalidEquationType )
        {
          shared_ptr<const EnergyCalibration> fix_cal = missing_cal_fixs[meas->detector_name_][nchannel];
          
          for( size_t otherindex = 0; !fix_cal && (otherindex < measurements_.size()); ++otherindex )
          {
            if( otherindex == meas_index )
              continue;
            
            const auto &othermeas = measurements_[otherindex];
            if( othermeas->energy_calibration_ == meas->energy_calibration_ )
              continue;
            
            if( othermeas->energy_calibration_->type() == EnergyCalType::InvalidEquationType )
            {
              meas->energy_calibration_ = othermeas->energy_calibration_;
              continue;
            }
            
            if( othermeas->detector_name_ != meas->detector_name_ )
              continue;
            
            if( othermeas->energy_calibration_->num_channels() != nchannel )
            {
              try
              {
                auto newcal = make_shared<EnergyCalibration>();
                
                const auto &othercal = othermeas->energy_calibration_;
                switch( othercal->type() )
                {
                  case EnergyCalType::Polynomial:
                    newcal->set_polynomial( nchannel, othercal->coefficients(), othercal->deviation_pairs() );
                    break;
                    
                  case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
                    newcal->set_default_polynomial( nchannel, othercal->coefficients(), othercal->deviation_pairs() );
                    break;
                    
                  case EnergyCalType::FullRangeFraction:
                    newcal->set_full_range_fraction( nchannel, othercal->coefficients(), othercal->deviation_pairs() );
                    break;
                    
                  case EnergyCalType::LowerChannelEdge:
                    if( othercal->num_channels() < nchannel )
                    {
                      newcal.reset();
                    }else
                    {
                      assert( othercal->channel_energies() );
                      newcal->set_lower_channel_energy( nchannel, *othercal->channel_energies() );
                    }
                    break;
                    
                  case EnergyCalType::InvalidEquationType:
                    assert(0);
                    break;
                }//switch( othercal->type() )
                
                fix_cal = newcal;
              }catch( std::exception & )
              {
                fix_cal.reset();
              }//try / catch
            }else
            {
              fix_cal = othermeas->energy_calibration_;
            }
          }//for( const auto &othermeas : measurements_ )
          
          if( fix_cal )
            missing_cal_fixs[meas->detector_name_][nchannel] = fix_cal;
          else
            missing_cal_fixs[meas->detector_name_][nchannel] = meas->energy_calibration_;
          
          if( fix_cal )
          {
            meas->energy_calibration_ = fix_cal;
          
            if( (fix_cal->type() != EnergyCalType::InvalidEquationType)
                && (fix_cal->type() != EnergyCalType::UnspecifiedUsingDefaultPolynomial) )
            meas->parse_warnings_.push_back( "Energy calibration was not specified for this record,"
                                   " so using calibration from another record for this detector" );
          }//if( fix_cal )
        }//if( invalid equation type )
        
        //If the energy calibration is still invalid by here - we're not going to find one for it,
        //  so we will assign a default one.
        if( meas->energy_calibration_->type() == EnergyCalType::InvalidEquationType )
        {
          shared_ptr<EnergyCalibration> &def_cal = default_energy_cal[meas->gamma_counts_->size()];
          if( !def_cal )
          {
            const float nbinf = ((nchannel > 1) ? static_cast<float>(nchannel - 1): 1.0f);
            def_cal = make_shared<EnergyCalibration>();
            def_cal->set_default_polynomial( nchannel, {0.0f, 3000.0f/nbinf}, {} );
          }
          
          meas->energy_calibration_ = def_cal;
          missing_cal_fixs[meas->detector_name_][nchannel] = def_cal;
        }//if( invalid equation type )
        
        //Check if an equivalent calibration has been seen, and if so, use that
        shared_ptr<const EnergyCalibration> &equivcal = unique_cals[*meas->energy_calibration_];
        if( !equivcal )
          equivcal = meas->energy_calibration_;
        else
          meas->energy_calibration_ = equivcal;
      }//if( this is a gamma measurment )
      
      
      if( meas->has_gps_info() )
      {
        //  a lat long of (0 0) probably isnt a valid GPS coordinate
        if( meas->location_
           && meas->location_->geo_location_
           && (IsNan(meas->location_->geo_location_->latitude_) || (fabs(meas->location_->geo_location_->latitude_) < 1.0E-6))
           && (IsNan(meas->location_->geo_location_->longitude_) || (fabs(meas->location_->geo_location_->longitude_) < 1.0E-6))
           )
        {
          auto loc = make_shared<LocationState>( *meas->location_ );
          loc->geo_location_.reset();
          if( IsNan(loc->speed_) && !loc->relative_location_ && !loc->orientation_ )
            loc.reset();
          meas->location_ = loc;
        }else
        {
          ++nGpsCoords;
          mean_latitude_ += meas->latitude();
          mean_longitude_ += meas->longitude();
        }
      }
      
      meas->contained_neutron_ |= ( (meas->neutron_counts_sum_ > 0.0)
                                     || !meas->neutron_counts_.empty() );
      
      assert( meas->contained_neutron_ || (meas->neutron_live_time_ == 0.0f) );
      if( !meas->contained_neutron_ && (meas->neutron_live_time_ > 0.0f) )
      {
#if( PERFORM_DEVELOPER_CHECKS )
        log_developer_error( __func__, "Found a case where neutron live time was specified, but no neutrons parsed" );
#endif
        meas->neutron_live_time_ = 0.0f;
      }//
    }//for( auto &meas : measurements_ )
    
    
    if( (nGpsCoords == 0)
       || (fabs(mean_latitude_)<1.0E-6 && fabs(mean_longitude_)<1.0E-6) )
    {
      mean_latitude_ = mean_longitude_ = -999.9;
    }else
    {
      mean_latitude_ /= nGpsCoords;
      mean_longitude_ /= nGpsCoords;
    }//if( !nGpsCoords ) / else
    
    if( !SpecUtils::valid_longitude(mean_longitude_)
       || !SpecUtils::valid_latitude(mean_latitude_) )
      mean_latitude_ = mean_longitude_ = -999.9;
    
    if( reorder_by_time )
    {
      set_sample_numbers_by_time_stamp( false, true );
    }else if( dont_reorder )
    {
      if( !has_unique_sample_and_detector_numbers() )
        properties_flags_ |= kNotUniqueSampleDetectorNumbers;
      
      for( size_t i = 1; i < measurements_.size(); ++i )
      {
        if( is_special(measurements_[i-1]->start_time_)
           || is_special(measurements_[i]->start_time_) )
          continue;
        
        if( measurements_[i-1]->start_time_ > measurements_[i]->start_time_ )
          properties_flags_ |= kNotTimeSortedOrder;
        
        if( !compare_by_derived_sample_det_time(measurements_[i-1],measurements_[i]) )
          properties_flags_ |= kNotSampleDetectorTimeSorted;
      }//for( size_t i = 1; i < measurements_.size(); ++i )
    }else
    {
      ensure_unique_sample_numbers();
      
      /// @TODO: we can probably add this next bit of logic to another loop so it isnt so expensive.
      for( size_t i = 1; i < measurements_.size(); ++i )
      {
        if( !is_special(measurements_[i-1]->start_time_)
           && !is_special(measurements_[i]->start_time_)
           && (measurements_[i-1]->start_time_ > measurements_[i]->start_time_) )
        {
          properties_flags_ |= kNotTimeSortedOrder;
          break;
        }
      }//for( size_t i = 1; i < measurements_.size(); ++i )
      
    }//if( flags & DontChangeOrReorderSamples ) / else
    
    
#if( PERFORM_DEVELOPER_CHECKS )
    if( !(properties_flags_ & kNotSampleDetectorTimeSorted) )
    {
      bool sampleNumSorted = true;
      for( size_t i = 1; sampleNumSorted && (i < measurements_.size()); ++i )
        sampleNumSorted = (measurements_[i-1]->sample_number_ <= measurements_[i]->sample_number_);
      if( !sampleNumSorted )
        log_developer_error( __func__, "Found a case where sample numbers weren't sorted!" );
      assert( sampleNumSorted );
    }
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
    
    detector_numbers_.clear();
    detector_names_.clear();
    gamma_detector_names_.clear();
    neutron_detector_names_.clear();
    
    for( const IntStrMap::value_type &t : num_to_name_map )
    {
      detector_numbers_.push_back( t.first );
      detector_names_.push_back( t.second );
    }//for( const IntStrMap::value_type &t : num_to_name_map )
    
    gamma_detector_names_.insert( end(gamma_detector_names_), begin(gamma_det_names), end(gamma_det_names) );
    neutron_detector_names_.insert( end(neutron_detector_names_), begin(neut_det_names), end(neut_det_names) );
    
    
    //If none of the Measurements that have neutrons have gammas, then lets see
    //  if it makes sense to add the neutrons to the gamma Measurement
    // TODO: get rid of merge_neutron_meas_into_gamma_meas(), and just keep neutrons how specified
    //       in the spectrum file; either with the gamma, or separate.  Also remove the similar
    //       functioning code from decode_2012_N42_rad_measurement_node(...)
    if( haveNeutrons && haveGammas && neutronMeasDoNotHaveGamma
       && !(flags & DontChangeOrReorderSamples) )
    {
      merge_neutron_meas_into_gamma_meas();
    }
    
    size_t nbins = 0;
    bool all_same_num_bins = true;
    
    
    //In order to figure out if a measurement is passthrough, we'll use these
    //  two variables.
    int pt_num_items = 0;
    float pt_averageRealTime = 0;
    
    //ensure_unique_sample_numbers(); has already been called a
    sample_numbers_.clear();
    sample_to_measurements_.clear();
    
    typedef std::pair<time_point_t,float> StartAndRealTime;
    typedef map<int, StartAndRealTime > SampleToTimeInfoMap;
    SampleToTimeInfoMap samplenum_to_starttime; //used to determine if passthrough or not
    
    const size_t nmeas = measurements_.size();
    size_t ngamma_meas = 0;
    
    /// @TODO: we could probably move this next loop into one of the above to be more efficient
    for( size_t measn = 0; measn < nmeas; ++measn )
    {
      std::shared_ptr<Measurement> &meas = measurements_[measn];
      sample_numbers_.insert( meas->sample_number_ );
      sample_to_measurements_[meas->sample_number_].push_back( measn );
      
      if( !meas->gamma_counts_ || meas->gamma_counts_->empty() )
        continue;
      
      ++ngamma_meas;
      if( nbins==0 )
        nbins = meas->gamma_counts_->size();
      if( nbins != meas->gamma_counts_->size() )
        all_same_num_bins = false;
      
      //20180221: Removed check on measurement type because some ROSA portal
      //  occupancies have less than 4 non-background samples.
      if( //meas->source_type_ != SourceType::Background
         //&& meas->source_type_ != SourceType::Calibration &&
        meas->source_type_ != SourceType::IntrinsicActivity
         && meas->sample_number() >= 0
         && meas->live_time() > 0.00000001
         && meas->real_time() > 0.00000001
         && !meas->derived_data_properties()  //make sure not derived data
         && ((meas->real_time() < 15.0)   //20181108: some search systems will take one spectra every like ~10 seconds
             || ((detector_type_ == SpecUtils::DetectorType::RadiaCode) // Radiacode detectors can take like 30 second spectra
                 && (meas->real_time() < 125.0)) ) )
      {
        ++pt_num_items;
        pt_averageRealTime += meas->real_time_;
        
        if( !is_special(meas->start_time()) )
        {
          const int samplenum = meas->sample_number();
          const time_point_t &st = meas->start_time();
          const float rt = meas->real_time();
          
          SampleToTimeInfoMap::iterator pos = samplenum_to_starttime.find( samplenum );
          if( pos == samplenum_to_starttime.end() )
            pos = samplenum_to_starttime.insert( make_pair(samplenum, make_pair(st,rt)) ).first;
          pos->second.second = max( rt, pos->second.second );
        }
      }//if( a candidate for a passthrough spectrum )
    
    }//for( auto &meas : measurements_ )
    

    bool is_passthrough = true;
    
    if( sample_numbers_.size() < 5 || detector_numbers_.empty() )
      is_passthrough = false;
    
    if( pt_averageRealTime <= 0.00000001 )
      is_passthrough = false;
    
    //In principle should check that measurements were taken sequentially as well
    //pt_averageRealTime /= (pt_num_items ? pt_num_items : 1);
    //is_passthrough = is_passthrough && ( (pt_num_items>5) && (pt_averageRealTime < 2.5) );
    is_passthrough = is_passthrough && ( (pt_num_items>5) && pt_num_items > static_cast<size_t>(0.75*ngamma_meas) );
    //is_passthrough = true;
    
    //Go through and verify the measurements are mostly sequential (one
    //  measurement right after the next), for at least the occupied/item/unknown
    //  portions of the file.  The background portion, as well as the transition
    //  between background/foreground/etc is a little harder to deal with since
    //  background may have gaps, or may have been taken a while before the item
    if( !is_passthrough && (samplenum_to_starttime.size() > 20) )
    {
      int nnotadjacent = 0, nadjacent = 0;
      
      SampleToTimeInfoMap::const_iterator next = samplenum_to_starttime.begin(), iter;
      for( iter = next++; next != samplenum_to_starttime.end(); ++iter, ++next )
      {
        const time_point_t &st = iter->second.first;
        const time_point_t &next_st = next->second.first;
        
        const float rt = iter->second.second;
        const time_point_t::duration duration = chrono::microseconds( static_cast<int64_t>(rt*1.0E6) );
        
        time_point_t::duration diff = ((st + duration) - next_st);
        if( diff < time_point_t::duration::zero() )
          diff = -diff;
        
        if( diff < (duration/100) )
          ++nadjacent;
        else
          ++nnotadjacent;
      }
      
      is_passthrough = (10*nnotadjacent < nadjacent);
    }//if( !is_passthrough && pt_num_items>20 )
    
  
    if( all_same_num_bins )
      properties_flags_ |= kAllSpectraSameNumberChannels;
    
    if( is_passthrough )
      properties_flags_ |= kPassthroughOrSearchMode;
    
    
    if( rebinToCommonBinning && all_same_num_bins && !measurements_.empty()
       && ((gamma_detector_names.size() > 1) || is_passthrough) )
    {
      if( unique_cals.size() <= 1 )
      {
        properties_flags_ |= kHasCommonBinning;
      }else
      {
        //If we have more than one gamma detector, than we have to move them
        //  to have a common energy binning, to display properly
        size_t nbin = 0;
        float min_energy = 99999.9f, max_energy = -99999.9f;
        for( const auto &meas : measurements_ )
        {
          nbin = max( nbin , (meas->gamma_counts_ ? meas->gamma_counts_->size() : size_t(0)) );
          if( meas->energy_calibration_->type() != EnergyCalType::InvalidEquationType )
          {
            min_energy = min( min_energy, meas->gamma_energy_min() );
            max_energy = max( max_energy, meas->gamma_energy_max() );
          }//if( meas->channel_energies_.size() )
        }//for( const auto &meas : measurements_ )
        
        try
        {
          const size_t nbinShift = nbin - 1;
          const float channel_width = (max_energy - min_energy) / nbinShift;
          auto new_cal = make_shared<EnergyCalibration>();
          new_cal->set_polynomial( nbin, {min_energy,channel_width}, {} );
          
          for( const auto &meas : measurements_ )
          {
            if( meas->gamma_counts_->size() > 4 )
            {
              rebin_all_measurements( new_cal );
              properties_flags_ |= kHasCommonBinning;
              properties_flags_ |= kRebinnedToCommonBinning;
              break;
            }//if( meas->gamma_counts_->size() > 16 )
          }//for( const auto &meas : measurements_ )
        }catch( std::exception &e )
        {
          char buffer[1024];
          snprintf( buffer, sizeof(buffer), "Error rebining measurements to a common binning: %s",
                   e.what() );
          
          parse_warnings_.push_back( buffer );
#if( PERFORM_DEVELOPER_CHECKS )
          log_developer_error( __func__, buffer );
#endif
        }//try ? catch
        
        
      }//if( calib_infos_set.size() > 1 )
    }else if( all_same_num_bins && !measurements_.empty() && unique_cals.size()<=1 )
    {
      properties_flags_ |= kHasCommonBinning;
    }else if( !all_same_num_bins )
    {
    }//if( !measurements_.empty() )
    
    if( uuid_.empty() )
      uuid_ = generate_psuedo_uuid();
    
    set_detector_type_from_other_info();

    //Lets get rid of duplicate component_versions
    set< pair<string,string> > component_versions_s;
    std::vector<std::pair<std::string,std::string> > component_versions_nondup;
    for( size_t i = 0; i < component_versions_.size(); ++i )
    {
      if( component_versions_s.count( component_versions_[i] ) )
        continue;
      component_versions_s.insert( component_versions_[i] );
      component_versions_nondup.push_back( component_versions_[i] );
    }
    component_versions_.swap( component_versions_nondup );
    
    
    // TODO: DetectorAnalysisResult should be split into NuclideAnalysisResults and DoseAnalysisResults, but for here we will split them into seperate DetectorAnalysis objects
    //  This is done so we can write out to N42-2012, and read back in consistently.
    if( detectors_analysis_ )
    {
      std::shared_ptr<DetectorAnalysis> new_det_ana = make_shared<DetectorAnalysis>( *detectors_analysis_ );
      new_det_ana->results_.clear();
      vector<DetectorAnalysisResult> &new_results = new_det_ana->results_;
      
      for( DetectorAnalysisResult a : detectors_analysis_->results_ )
      {
        bool duplicate_nuc = false, duplicate_dose = false;
        for( size_t i = 0; (!duplicate_nuc || !duplicate_dose) && (i < new_results.size()); ++i )
        {
          const auto &already_added = new_results[i];
          duplicate_nuc |= ( (a.nuclide_ == already_added.nuclide_)
                            && (a.detector_ == already_added.detector_)
                            && (a.id_confidence_ == already_added.id_confidence_)
                            && (a.activity_ == already_added.activity_) );
          
          duplicate_dose |= ( (a.dose_rate_ == already_added.dose_rate_)
                             && (a.detector_ == already_added.detector_) );
        }//for( size_t i = 0; (add_nuc || add_dose) && (i < new_results.size()); ++i )
        
        
        if( ((a.dose_rate_ >= 0.0) && !a.nuclide_.empty()) )
        {
          if( !duplicate_nuc )
          {
            DetectorAnalysisResult a_nuc = a;
            a_nuc.dose_rate_ = -1.0f;
            new_results.push_back( std::move(a_nuc) );
          }
          
          if( !duplicate_dose )
          {
            a.activity_ = -1.0f;
            a.nuclide_.clear();
            a.nuclide_type_.clear();
            a.id_confidence_.clear();
            a.real_time_ = -1.0f;
            
            new_results.push_back( std::move(a) );
          }
        }else
        {
          if( ((a.dose_rate_ >= 0.0) && !duplicate_dose)
             || (!a.nuclide_.empty() && !duplicate_nuc) )
          {
            // Arbitrarily clean up some fields
            if( a.nuclide_.empty() )
            {
              a.activity_ = -1.0f;
              a.id_confidence_.clear();
              a.real_time_ = -1.0f;
            }//if( no nuclide results )
            
            new_results.push_back( std::move(a) );
          }//if( we have nuclide or dose informaition; i.e., actually worth adding )
        }//if( we have both nuclide and dose info ) / else( we have just one type of info )
      }//for( loop over original analysis results )
      
      // Remove duplicate algorithm_component_versions_
      vector<pair<string,string>> &components = new_det_ana->algorithm_component_versions_;
      auto comp_en = end(components);
      for( auto it = begin(components); it != comp_en; ++it )
        comp_en = std::remove(it + 1, comp_en, *it);
      components.erase( comp_en, end(components) );
      
      detectors_analysis_ = new_det_ana;
    }//if( detectors_analysis_ )
    
    
    recalc_total_counts();
    
#if( PERFORM_DEVELOPER_CHECKS )
    // Make sure energy calibrations have same number of channels as spectrums
    {
      for( const auto &m : measurements_ )
      {
        assert( m );
        const size_t ngammachan = m->gamma_counts_ ? m->gamma_counts_->size() : size_t(0);
        const auto &cal = m->energy_calibration_;
        if( cal && cal->valid() )
        {
          if( cal->num_channels() != ngammachan )
            cerr << "cal->num_channels()=" << cal->num_channels() << ", ngammachan=" << ngammachan << endl;
          assert( cal->num_channels() == ngammachan );
        }
      }//for( const auto &m : measurements_ )
    }
#endif
    
#if( PERFORM_DEVELOPER_CHECKS )
    //Check to make sure all gamma and neutron detector names can be found in detector names
    {
      const vector<string>::const_iterator begindet = detector_names_.begin();
      const vector<string>::const_iterator enddet = detector_names_.end();
      
      for( const std::string &gdet : gamma_detector_names_ )
      {
        if( std::find(begindet,enddet,gdet) == enddet )
        {
          char buffer[1024];
          snprintf( buffer, sizeof(buffer),
                   "Found a gamma detector name not in the list of all detector names: %s\n",
                   gdet.c_str() );
          log_developer_error( __func__, buffer );
        }
      }//for( loop over gamma detector names )
      
      for( const std::string &ndet : neutron_detector_names_ )
      {
        if( std::find(begindet,enddet,ndet) == enddet )
        {
          char buffer[1024];
          snprintf( buffer, sizeof(buffer),
                   "Found a neutron detector name not in the list of all detector names: %s\n",
                   ndet.c_str() );
          log_developer_error( __func__, buffer );
        }
      }//for( loop over neutron detector names )
    }
#endif
    
#if( PERFORM_DEVELOPER_CHECKS )
    //static int ntest = 0;
    //if( ntest++ < 10 )
    //  cerr << "Warning, testing rebingin ish" << endl;
    
    const double prev_gamma_count_sum_ = gamma_count_sum_;
    const double prev_neutron_counts_sum_ = neutron_counts_sum_;
    
    recalc_total_counts();
    
    if( fabs(gamma_count_sum_ - prev_gamma_count_sum_) > 0.01 )
    {
      char buffer[1024];
      snprintf( buffer, sizeof(buffer),
               "Before rebinning and gamma count sum=%10f and afterwards its %10f\n",
               prev_gamma_count_sum_, gamma_count_sum_ );
      log_developer_error( __func__, buffer );
    }
    
    if( fabs(neutron_counts_sum_ - prev_neutron_counts_sum_) > 0.01 )
    {
      char buffer[1024];
      snprintf( buffer, sizeof(buffer),
               "Before rebinning and neutron count sum=%10f and afterwards its %10f\n",
               prev_neutron_counts_sum_, neutron_counts_sum_ );
      log_developer_error( __func__, buffer );
    }
#endif //#if( PERFORM_DEVELOPER_CHECKS )
  }catch( std::exception &e )
  {
    string msg = "From " + string(SRC_LOCATION) + " caught error:\n\t" + string(e.what());
    throw runtime_error( msg );
  }//try / catch
  
  modified_ = modifiedSinceDecode_ = false;
}//void cleanup_after_load()

    
    
// TODO: get rid of this function, and also similarly for the code in
//      decode_2012_N42_rad_measurement_node(...) that does similar
void SpecFile::merge_neutron_meas_into_gamma_meas()
{
  //Check to make sure sample numbers arent whack (they havent been finally
  //  assigned yet) before doing the correction - all the files I've seen
  //  that need this correction already have sample numbers properly
  //  assigned
  std::map<int, std::vector<std::shared_ptr<Measurement>> > sample_to_meass;
  for( const auto &m : measurements_ )
  {
    std::vector<std::shared_ptr<Measurement>> &mv = sample_to_meass[m->sample_number_];
    
    // Check to make sure that gamma and neutron have the same measurement time
    for( auto &prev : mv )
    {
      if( ((m->contained_neutron() == prev->contained_neutron())
         && ((!m->num_gamma_channels()) == (!m->num_gamma_channels())))
         || (m->contained_neutron() && m->num_gamma_channels()) )
      {
        continue;
      }
      
      const auto &neut_meas = m->contained_neutron() ? m : prev;
      const auto &gamma_meas = m->contained_neutron() ? prev : m;
      
      // Some detectors will report a neutron live time that is much less than the sample
      //  numbers real time
      float nuet_time = std::min( neut_meas->live_time(), neut_meas->real_time() );
      if( nuet_time <= 0.1f )
        nuet_time = std::max( neut_meas->live_time(), neut_meas->real_time() );
      const float gamma_time = std::max( gamma_meas->live_time(), gamma_meas->real_time() );
      const float diff = fabs( gamma_time - nuet_time );
      
      // If neutron measurement time is off from gamma by over 1%, then dont combine.
      //  This is arbitrary, and a hack, until we totally remove this function.
      if( diff > 0.01f*std::max(gamma_time, nuet_time) )
      {
        return;
      }
    }//for( size_t i = 0; i < mv.size(); ++i )
    
    mv.push_back( m );
    if( mv.size() > detector_names_.size() )
    {
#if( PERFORM_DEVELOPER_CHECKS )
      log_developer_error( __func__, "Found a file where neutron"
                          " and gammas are seperate measurements, but sample numbers not assigned." );
#endif //#if( PERFORM_DEVELOPER_CHECKS )
      
      //If sample numbers arent already properly assigned, bail
      return;
    }
  }//for( const auto &m : measurements_ )
  
  
  vector<string> gamma_only_dets = gamma_detector_names_;
  vector<string> neutron_only_dets = neutron_detector_names_;
  for( const auto &n : neutron_only_dets )
  {
    const auto pos = std::find( begin(gamma_only_dets), end(gamma_only_dets), n );
    if( pos != end(gamma_only_dets) )
      gamma_only_dets.erase(pos);
  }//for( const auto &n : neutron_detector_names_ )
  
  const size_t ngammadet = gamma_only_dets.size();
  const size_t nneutdet = neutron_only_dets.size();
  
  //If we dont have both gamma and neutron only detectors, bail
  if( !nneutdet || !ngammadet )
    return;
  
  
  //We may need to copy the neutron data into multiple gamma measurements if
  //  the gamma data is given mutliple times with different calibrations
  //  See file in refDUEL9G1II9 for an example of when this happens.
  map<string,vector<string>> neutron_to_gamma_names;
  
  std::sort( begin(gamma_only_dets), end(gamma_only_dets) );
  std::sort( begin(neutron_only_dets), end(neutron_only_dets) );
  
  if( ngammadet == nneutdet )
  {
    for( size_t i = 0; i < ngammadet; ++i )
      neutron_to_gamma_names[neutron_only_dets[i]].push_back( gamma_only_dets[i] );
  }else if( ngammadet && (nneutdet > ngammadet) && ((nneutdet % ngammadet) == 0) )
  {
    const size_t mult = nneutdet / ngammadet;
    for( size_t i = 0; i < nneutdet; ++i )
      neutron_to_gamma_names[neutron_only_dets[i]].push_back( gamma_only_dets[i/mult] );
  }else
  {
    //Use the edit distance of detector names to pair up neutron to
    //  gamma detectors.  Only do the pairing if the minimum levenstein
    //  distance between a neutron detector and a gamma detector is
    //  unique (i.e., there are not two gamma detectors that both have
    //  the minimum distance to a neutron detector).
    
    //If the gamma data is reported in multiple energy calibrations, the
    //  string "_intercal_$<calid>" is appended to the detector name.  We
    //  do not want to include this in the test, however, we also dont want
    //  to spoil the intent of 'uniquelyAssigned'
    vector<pair<string,vector<string>>> tested_gamma_to_actual;
    for( size_t i = 0; i < gamma_only_dets.size(); ++i )
    {
      string gamname = gamma_only_dets[i];
      const auto pos = gamname.find( "_intercal_" );
      if( pos != string::npos )
        gamname = gamname.substr(0,pos);
      
      bool added = false;
      for( auto &t : tested_gamma_to_actual )
      {
        if( t.first == gamname )
        {
          t.second.push_back( gamma_only_dets[i] );
          added = true;
          break;
        }
      }//for( check if we already have a detector with name 'gamname' )
      
      if( !added )
      {
#if( defined(_MSC_VER) && _MSC_VER <= 1700 )
        tested_gamma_to_actual.emplace_back( gamname, vector<string>(1,gamma_only_dets[i]) );
#else
        tested_gamma_to_actual.emplace_back( gamname, vector<string>{gamma_only_dets[i]} );
#endif
      }//if( !added )
    }//for( size_t i = 0; i < gamma_only_dets.size(); ++i )
    
    
    bool uniquelyAssigned = true;
    for( size_t neutindex = 0; uniquelyAssigned && (neutindex < nneutdet); ++neutindex )
    {
      const string &neutname = neutron_only_dets[neutindex];
      
      vector<unsigned int> distances( tested_gamma_to_actual.size() );
      
      for( size_t detnameindex = 0; detnameindex < tested_gamma_to_actual.size(); ++detnameindex )
      {
        const string &gammaname = tested_gamma_to_actual[detnameindex].first;
        distances[detnameindex] = SpecUtils::levenshtein_distance( neutname, gammaname );
        if( distances[detnameindex] > 3 )
        {
          if( SpecUtils::icontains( neutname, "Neutron") )
          {
            string neutnamelowercase = neutname;
            SpecUtils::ireplace_all(neutnamelowercase, "Neutron", "Gamma");
            if( SpecUtils::iequals_ascii( neutnamelowercase, gammaname ) )
            {
              distances[detnameindex] = 0;
            }else
            {
              neutnamelowercase = neutname;
              SpecUtils::ireplace_all(neutnamelowercase, "Neutron", "");
              if( SpecUtils::iequals_ascii( neutnamelowercase, gammaname ) )
                distances[detnameindex] = 0;
            }
          }else if( SpecUtils::iends_with( neutname, "Ntr") )
          {
            string neutnamelowercase = neutname.substr( 0, neutname.size() - 3 );
            if( SpecUtils::iequals_ascii( neutnamelowercase, gammaname ) )
              distances[detnameindex] = 0;
          }else if( SpecUtils::iends_with( neutname, "N") )
          {
            string neutnamelowercase = neutname.substr( 0, neutname.size() - 1 );
            if( SpecUtils::iequals_ascii( neutnamelowercase, gammaname ) )
              distances[detnameindex] = 0;
          }
        }//if( distances[i] > 3 )
      }//for( size_t i = 0; uniquelyAssigned && i < ngammadet; ++i )
      
      auto positer = std::min_element(begin(distances), end(distances) );
      auto index = positer - begin(distances);
      assert( index >= 0 && index < distances.size() );
      unsigned int mindist = distances[index];
      uniquelyAssigned = (1 == std::count(begin(distances),end(distances),mindist));
      
      if( uniquelyAssigned )
      {
        neutron_to_gamma_names[neutname] = tested_gamma_to_actual[index].second;
        //cout << "Assigning neut(" << neutname << ") -> {";
        //for( size_t i = 0; i < neutron_to_gamma_names[neutname].size(); ++i )
        //  cout << (i ? ", " : "") << neutron_to_gamma_names[neutname][i];
        //cout << "}" << endl;
      }
    }//for( size_t neutindex = 0; uniquelyAssigned && (neutindex < nneutdet); ++neutindex )
    
    //ToDo: uniquelyAssigned doesnt catch the case where multiple neutron detectors
    //  are mapped to a gamma detector, but then some gamma detectors dont have
    //  any assigned neutron detectors that should
    
    if( !uniquelyAssigned )
    {
      neutron_to_gamma_names.clear();
      
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
      stringstream devmsg;
      devmsg << "Unable to uniquly map neutron to gamma detector names; neutron"
                " and gammas are seperate measurements, but mapping between"
                " detectors not not unique: gamma_dets={";
      for( size_t i = 0; i < gamma_only_dets.size(); ++i )
        devmsg << (i ? ", ": "") << "'" << gamma_only_dets[i] << "'";
      devmsg << "}, neut_dets={";
      for( size_t i = 0; i < neutron_only_dets.size(); ++i )
        devmsg << (i ? ", ": "") << "'" << neutron_only_dets[i] << "'";
      devmsg << "}";
      
      log_developer_error( __func__, devmsg.str().c_str() );
#endif //#if( PERFORM_DEVELOPER_CHECKS )
    }//if( uniquelyAssigned )
  }//if( figure out how to assign neutron to gamma detectors ) / else
  
  
  //If we cant map the neutron detectors to gamma detectors, lets bail.
  if( neutron_to_gamma_names.empty() /* || neutron_to_gamma_names.size() != nneutdet*/ )
    return;
  
#if( PERFORM_DEVELOPER_CHECKS )
  set<size_t> gammas_we_added_neutron_to;
#endif
  set<string> new_neut_det_names, new_gamma_det_names, new_all_det_names;
  vector<std::shared_ptr<Measurement>> meas_to_delete;
  
  for( size_t measindex = 0; measindex < measurements_.size(); ++measindex )
  {
    std::shared_ptr<Measurement> meas = measurements_[measindex];
    
    
    if( !meas->contained_neutron_ )
    {
      new_all_det_names.insert( meas->detector_name_ );
      if( meas->gamma_counts_ && !meas->gamma_counts_->empty() )
        new_gamma_det_names.insert( meas->detector_name_ );
      
      continue;
    }
    
    if( meas->gamma_counts_ && !meas->gamma_counts_->empty() )
    {
#if( PERFORM_DEVELOPER_CHECKS )
      if( !gammas_we_added_neutron_to.count(measindex) )
        log_developer_error( __func__, "Found a nuetron detector Measurement that had gamma data - shouldnt have happened here." );
#endif  //PERFORM_DEVELOPER_CHECKS
      new_all_det_names.insert( meas->detector_name_ );
      new_gamma_det_names.insert( meas->detector_name_ );
      new_neut_det_names.insert( meas->detector_name_ );
      continue;
    }
    
    //IF we are hear, this `meas` only contains neutron data.
    
    auto namepos = neutron_to_gamma_names.find(meas->detector_name_);
    if( namepos == end(neutron_to_gamma_names) )
    {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
      log_developer_error( __func__, "Found a nuetron detector Measurement I couldnt map to a gamma meas - should investigate." );
#endif  //PERFORM_DEVELOPER_CHECKS
      new_all_det_names.insert( meas->detector_name_ );
      new_neut_det_names.insert( meas->detector_name_ );
      continue;
    }//
    
    const vector<string> &gamma_names = namepos->second;
    
    const size_t nmeas = measurements_.size();
    const size_t max_search_dist = 2*gamma_names.size()*(nneutdet+ngammadet);
    
    //We may actually have multiple gamma detectors that we want to assign this
    //  neutron information to (ex, neutron detector is 'VD1N', and related
    //  gamma detectors are 'VD1', 'VD1_intercal_CmpEnCal', and
    //  'VD1_intercal_LinEnCal' - this can possible result in duplication of
    //  neutron counts, but I havent seen any cases where this happens in an
    //  unintended way (e.g., other than multiple calibrations).
    for( const string &gamma_name : gamma_names )
    {
      std::shared_ptr<Measurement> gamma_meas;
      for( size_t i = measindex; !gamma_meas && (i > 0) && ((measindex-i) < max_search_dist); --i )
      {
        if( measurements_[i-1]->detector_name_ == gamma_name
           && measurements_[i-1]->sample_number_ == meas->sample_number_)
        {
#if( PERFORM_DEVELOPER_CHECKS )
          gammas_we_added_neutron_to.insert( i-1 );
#endif
          gamma_meas = measurements_[i-1];
        }
      }
      
      for( size_t i = measindex+1; !gamma_meas && (i < nmeas) && ((i-measindex) < max_search_dist); ++i )
      {
        if( measurements_[i]->detector_name_ == gamma_name
           && measurements_[i]->sample_number_ == meas->sample_number_ )
        {
#if( PERFORM_DEVELOPER_CHECKS )
          gammas_we_added_neutron_to.insert( i );
#endif
          gamma_meas = measurements_[i];
        }
      }
      
      if( !gamma_meas )
      {
        //TODO: if we are here we should in principle create a measurement for
        //  each of the entries in 'gamma_names' - however for now I guess
        //  we'll just assign the neutron data to a single gamma detector; I
        //  think this will only cause issues for files that had multiple
        //  calibrations for the same data, of which we'll make the neutron data
        //  belong to just the non calibration variant detector (if it exists).
        
        bool aDetNotInterCal = false;
        for( size_t i = 0; !aDetNotInterCal && i < gamma_names.size(); ++i )
          aDetNotInterCal = (gamma_names[i].find("_intercal_") != string::npos);
        
        if( aDetNotInterCal && (gamma_name.find("_intercal_") == string::npos) )
          continue;
        
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
        if( gamma_names.size() != 1 && (meas->detector_name_ != gamma_name) )
        {
          string errmsg = "Found a nuetron detector Measurement (DetName='" + meas->detector_name_
                 + "', SampleNumber=" + std::to_string(meas->sample_number_)
                 + ", StartTime=" + SpecUtils::to_iso_string(meas->start_time_)
                 + ") I couldnt find a gamma w/ DetName='"
                 + gamma_name + "' and SampleNumber=" + std::to_string(meas->sample_number_) + ".";
          log_developer_error( __func__, errmsg.c_str() );
        }
#endif  //PERFORM_DEVELOPER_CHECKS
        
        
        meas->detector_name_ = gamma_name;

        //ToDo: Uhhg, later on when we write things to XML we need to make sure
        //  the detector description will be consistently written out and read
        //  back in, so we have to make sure to update this info too in 'meas'.
        //  We *should* make detector information be an object shared among
        //  measurements, but oh well for now.
        std::shared_ptr<Measurement> same_gamma_meas;
        for( size_t i = 0; !same_gamma_meas && i < measurements_.size(); ++i )
        {
          if( measurements_[i]->detector_name_ == meas->detector_name_ )
            same_gamma_meas = measurements_[i];
        }
        
        if( same_gamma_meas )
        {
          meas->detector_description_ = same_gamma_meas->detector_description_;
          meas->detector_number_ = same_gamma_meas->detector_number_;
        }else
        {
          auto numpos = std::find( begin(detector_names_), end(detector_names_), gamma_name );
          auto numindex = static_cast<size_t>(numpos - begin(detector_names_));
          if( (numpos != end(detector_names_)) && (numindex < detector_numbers_.size()) )
          {
            meas->detector_number_ = detector_numbers_[numindex];
          }else
          {
#if( PERFORM_DEVELOPER_CHECKS )
            log_developer_error( __func__,
                                ("Failed to be able to find detector number for DetName=" + gamma_name).c_str() );
#endif
          }
        }//if( same_gamma_meas ) / else
        
        new_all_det_names.insert( meas->detector_name_ );
        new_neut_det_names.insert( meas->detector_name_ );
        continue;
      }//if( !gamma_meas )
      
      
      //TODO: should check and handle real time being different, as well
      //      as start time.  Should also look into propagating detector
      //      information and other quantities over.
      
      assert( gamma_meas->contained_neutron_ || gamma_meas->neutron_live_time_ == 0.0f );
      if( !gamma_meas->contained_neutron_ || (gamma_meas->neutron_live_time_ < 0.0f) )
        gamma_meas->neutron_live_time_ = 0.0f;
      
      gamma_meas->contained_neutron_ = true;
      gamma_meas->neutron_counts_.insert( end(gamma_meas->neutron_counts_),
                                         begin(meas->neutron_counts_), end(meas->neutron_counts_) );
      gamma_meas->neutron_counts_sum_ += meas->neutron_counts_sum_;
      
      if( meas->neutron_live_time_ > 0.0f )
        gamma_meas->neutron_live_time_ += meas->neutron_live_time_;
      else
        gamma_meas->neutron_live_time_ += meas->real_time_;
      
      gamma_meas->remarks_.insert( end(gamma_meas->remarks_),
                                  begin(meas->remarks_), end(meas->remarks_) );
      
      //Do not add meas->detector_name_ to new_all_det_names - we are getting rid of meas
      new_neut_det_names.insert( gamma_meas->detector_name_ );
      
      meas_to_delete.push_back( meas );
    }//for( size_t gamma_name_index = 0; gamma_name_index < gamma_names.size(); gamma_name_index )
  }//for( size_t measindex = 0; measindex < measurements_.size(); ++measindex )
  
  size_t nremoved = 0;
  for( size_t i = 0; i < meas_to_delete.size(); ++i )
  {
    auto pos = std::find( begin(measurements_), end(measurements_), meas_to_delete[i] );
    if( pos != end(measurements_) )
    {
      ++nremoved;
      measurements_.erase( pos );
    }else
    {
      //if there are multiple energy calibrations, meas_to_delete may
    }
  }//for( size_t i = 0; i < meas_to_delete.size(); ++i )
  
  //Before this function is called, detector_names_ and detector_numbers_
  // should have been filled out, and we havent modified them, so lets
  // preserve detector number to name mapping, but remove detectors that
  // no longer exist
  
  try
  {
    if( detector_names_.size() != detector_numbers_.size() )
      throw runtime_error( "Unequal number of detector names and numbers" );
    
    for( const auto &name : new_all_det_names )
      if( std::find(begin(detector_names_),end(detector_names_),name) == end(detector_names_) )
        throw runtime_error( "Is there a new detector name?" );
    
    //If we are here we wont loop through measurements_ and change the
    //  detector numbers of the measurements and keep the same mapping
    //  of names to numbers
    
    map<string,int> detnames_to_number;
    for( size_t i = 0; i < detector_names_.size(); ++i )
      detnames_to_number[detector_names_[i]] = detector_numbers_[i];
    
    detector_numbers_.clear();
    for( const auto &s : new_all_det_names )
      detector_numbers_.push_back( detnames_to_number[s] );
  }catch( std::exception & )
  {
    //If we are here we will go through and force everyrthing to be consistent
    detector_numbers_.clear();
    for( size_t i = 0; i < new_all_det_names.size(); ++i )
      detector_numbers_.push_back( static_cast<int>(i) );
    const vector<string> new_all_det_names_vec( begin(new_all_det_names), end(new_all_det_names) );
    for( auto &meas : measurements_ )
    {
      const auto iter = std::find( begin(new_all_det_names_vec), end(new_all_det_names_vec), meas->detector_name_ );
      if( iter != end(new_all_det_names_vec) )
      {
        const auto index = iter - begin(new_all_det_names_vec);
        meas->detector_number_ = detector_numbers_[index];
      }else
      {
#if( PERFORM_DEVELOPER_CHECKS )
        log_developer_error( __func__, "Unexpected Detector name found!" );
#endif
        //hope for the best....
      }
    }//
  }//try / catch update
  
  detector_names_.clear();
  detector_names_.insert( end(detector_names_), begin(new_all_det_names), end(new_all_det_names) );
  
  gamma_detector_names_.clear();
  gamma_detector_names_.insert( end(gamma_detector_names_), begin(new_gamma_det_names), end(new_gamma_det_names) );
  
  neutron_detector_names_.clear();
  neutron_detector_names_.insert( end(neutron_detector_names_), begin(new_neut_det_names), end(new_neut_det_names) );
  
  //Could map the detector names to Aa1, etc. here.
}//void merge_neutron_meas_into_gamma_meas()



void SpecFile::set_detector_type_from_other_info()
{
  using SpecUtils::contains;
  using SpecUtils::icontains;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( (detector_type_ != DetectorType::Unknown)
     && (detector_type_ != DetectorType::IdentiFinderUnknown) )
    return;
  
  const string &model = instrument_model_;
//  const string &id = instrument_id_;
  
  if( icontains(model,"SAM") && (contains(model,"940") || icontains(model,"Eagle+"))
     && !icontains(manufacturer_,"Princeton") )
  {
    if( icontains(model,"LaBr") )
      detector_type_ = DetectorType::Sam940LaBr3;
    else
      detector_type_ = DetectorType::Sam940;
    
    //cerr << "ASAm940 model=" << model << endl;
    
    return;
  }
  
  if( icontains(model,"SAM") && contains(model,"945") 
     && !icontains(manufacturer_,"Princeton") )
  {
    //if( icontains(model,"LaBr") )
      //detector_type_ = Sam945LaBr3;
    //else
    detector_type_ = DetectorType::Sam945;
    return;
  }
  
  if( icontains(model,"RS-701") )
  {
    detector_type_ = DetectorType::Rsi701;
    return;
  }
  
  if( icontains(model,"RS-705") )
  {
    detector_type_ = DetectorType::Rsi705;
    return;
  }
  
  if( icontains(model,"RS???") /*&& icontains(id,"Avid")*/ )
  {
    detector_type_ = DetectorType::AvidRsi;
    return;
  }
  
  if( icontains(model,"radHUNTER") )
  {
    if( icontains(model,"UL-LGH") )
      detector_type_ = DetectorType::RadHunterLaBr3;
    else
      detector_type_ = DetectorType::RadHunterNaI;
    return;
  }
  
  
  if( icontains(manufacturer_,"innoRIID")
     && !icontains(manufacturer_,"ortec")
     && (istarts_with(model, "RE ") || icontains(model, "RadEagle")) )
  {
      manufacturer_ += " (Ortec)";
  }
    
  if( (icontains(model,"Rad") && icontains(model,"Eagle"))
      || istarts_with(model, "RE-") || istarts_with(model, "RE ") )
  {
    if( SpecUtils::icontains(model,"3SG") ) //RADEAGLE NaI(Tl) 3x1, GM Handheld RIID
    {
      detector_type_ = DetectorType::OrtecRadEagleNai;
    }else if( SpecUtils::icontains(model,"2CG") ) //RADEAGLE CeBr3 2x1, GM Handheld RIID.
    {
      detector_type_ = DetectorType::OrtecRadEagleCeBr2Inch;
    }else if( SpecUtils::icontains(model,"3CG") ) //RADEAGLE CeBr3 3x0.8, GM Handheld RIID
    {
      detector_type_ = DetectorType::OrtecRadEagleCeBr3Inch;
    }else if( SpecUtils::icontains(model,"2LG") ) //RADEAGLE LaBr3(Ce) 2x1, GM Handheld RIID
    {
      detector_type_ = DetectorType::OrtecRadEagleLaBr;
    }else
    {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
      log_developer_error( __func__, ("Unrecognized RadEagle Model: " + model).c_str() );
#endif
    }
    
    //Make the modle human readable
    if( istarts_with(model, "RE ") )
      instrument_model_ = "RadEagle " + instrument_model_.substr(3);
    
    
    return;
  }//if( a rad eagle )
  
  
  
  //Lets try to figure out if we can fill out detector_type_
  if( iequals_ascii( manufacturer_,"ORTEC" ) )
  {
    if( iequals_ascii(instrument_model_,"OSASP") )
      detector_type_ = DetectorType::DetectiveEx200;
    else if( icontains(instrument_model_,"Detective") && contains(instrument_model_,"200") )
      detector_type_ = DetectorType::DetectiveEx200;
    else if( icontains(instrument_model_,"100") )
      detector_type_ = DetectorType::DetectiveEx100;
    else if( icontains(instrument_model_,"Detective-EX") )
      detector_type_ = DetectorType::DetectiveEx;
    else if( icontains(instrument_model_,"Detective") && contains(instrument_model_,"100") )
      detector_type_ = DetectorType::DetectiveEx100;
    else if( icontains(instrument_model_,"uDetective")
            || (icontains(instrument_model_,"Detective") && icontains(instrument_model_,"micro")) )
      detector_type_ = DetectorType::MicroDetective;
    else if( iequals_ascii(instrument_model_,"Detective X") )
      detector_type_ = DetectorType::DetectiveX;
    else if( icontains(instrument_model_,"Detective") )
      detector_type_ = DetectorType::DetectiveUnknown;
    
    return;
  }//if( iequals_ascii( manufacturer_,"ORTEC" ) )
  
  
  if( icontains( manufacturer_,"Kromek" ) )
  {
    // I've seen manufacturer name be either "Kromek" or "Kromek Ltd"
    
    if( istarts_with(instrument_model_,"D3") || iequals_ascii(instrument_model_,"D3S") )
    {
      //Have seen model strings: "D3", "D3S", "D3S 2.02"
      detector_type_ = DetectorType::KromekD3S;
      return;
    }
    
    //Other Kromek instrument models I've seem: "MultiSpect/KSpect"
  }//if( icontains( manufacturer_,"Kromek" ) )
  
  
  if( iequals_ascii(instrument_type_,"PVT Portal") && iequals_ascii(manufacturer_,"SAIC") )
  {
    detector_type_ = DetectorType::SAIC8;
    
    return;
  }
  
  
  if( icontains(instrument_model_,"identiFINDER") || icontains(model,"R400") || icontains(model,"R500") )
  {
    //From: https://www.flir.com/r400/ 20201218
    // - R400: three variants: NaI with seed, LaBr, NaI with LED
    // - R400 LG: LaBr, 30 x 30 mm, GM, LED
    // - R400 NGH: NaI 35 x 51, GM, He-3
    // - R400 T1: NaI 23 x 21mm - Tungsten shielded, GM
    // - R400 T2: NaI 23 x 21mm - Tungsten shielded, GM
    // - R400 UCLS-NGH: NaI 35 x 51mm, GM, He-3
    // - R400 ULK-NG: NaI 35 x 51mm, GM
    // - R400 ULK-NGH: NaI 35 x 51mm, GM, He-3
    // - R400 UW-NG: NaI 35 x 51mm, GM
    // - R400 UW-NGH: NaI 35 x 51mm, GM, He-3
    // - R400 UW-ULCS-NG: NaI 35 x 51mm, GM, LED
    // - R400 UW-ULCS-NG: NaI 35 x 51mm, GM, He-3, LED
    //
    // - R500 ULCS-NGH: NaI 102 x 19mm, GM, LED
    // - R500 UL-LG: 38 x 38mm, LaBr, GM, LED
    // - R500 UL-LG: 102 x 19mm, NaI, GM, LED //Same model number?
    //    - An actual file with these dimensions gives model as R500 ULCS-NG, so will continue
    //      assuming "LG" is LaBr
    // - R500 UL-LGH: 38 x 38mm, LaBr, GM, LED, He-3
    
    if( icontains(model,"R500") )
    {
      if( icontains(model,"ULCS") || icontains(model,"NaI") )
      {
        detector_type_ = DetectorType::IdentiFinderR500NaI;
        return;
      }
      
      if( icontains(model,"LG") || icontains(model,"LaBr") )
      {
        detector_type_ = DetectorType::IdentiFinderR500LaBr;
        return;
      }
      
      return;
    }//if( icontains(model,"R500") )
    
    if( icontains(model,"R425") )
    {
      if( icontains(model,"LG") || icontains(model,"LNG") || icontains(model,"LaBr")  )
        detector_type_ = DetectorType::IdentiFinderR425LaBr;
      else
        detector_type_ = DetectorType::IdentiFinderR425NaI;
      return;
    }//if( icontains(model,"R425") )
    
    if( icontains(model,"NG") || (icontains(model,"2") && !icontains(model,"LG")) )
    {
      detector_type_ = DetectorType::IdentiFinderNG;
      return;
    }
    
    if( icontains(model,"LG") )
    {
      detector_type_ = DetectorType::IdentiFinderLaBr3;
      return;
    }
    
    if( icontains(model,"T1") || icontains(model,"T2") )
    {
      detector_type_ = DetectorType::IdentiFinderTungsten;
      return;
    }
    
    for( const auto &remark : remarks_ )
    {
      if( (SpecUtils::contains(remark, "35 mm") && SpecUtils::contains(remark, "51 mm"))
          || (SpecUtils::contains(remark, "35mm") && SpecUtils::contains(remark, "51mm"))
          || SpecUtils::contains(remark, "35x51")
         || SpecUtils::contains(remark, "35 x 51"))
      {
        detector_type_ = DetectorType::IdentiFinderNG;
        return;
      }
       
      if( SpecUtils::contains(remark, "30x30")
         || SpecUtils::contains(remark, "30 x 30")
         || SpecUtils::contains(remark, "30mm")
         || SpecUtils::contains(remark, "LaBr") )
      {
        detector_type_ = DetectorType::IdentiFinderLaBr3;
        return;
      }
    }//for( const auto &remark : remarks_ )
    
    
    //cout << "identiFINDER: instrument_model_='" << instrument_model_ << "', manufacturer_='" << manufacturer_ << "'" << endl;
    if( detector_type_ == DetectorType::Unknown )
      detector_type_ = DetectorType::IdentiFinderUnknown;
    
    return;
  }//if( icontains(instrument_model_,"identiFINDER") )
  
  
  if( icontains(manufacturer_,"FLIR") || icontains(instrument_model_,"Interceptor") )
  {
    detector_type_ = DetectorType::Interceptor;
    return;
  }
  
  
  //Thermo Fisher Scientific, ins_model=RIIDEye, LaBr15x15, or ins_model=RIIDEye, NaI2x2
  if( icontains(model,"RIIDEye") )
  {
    if( icontains(model,"LaBr") )
    {
      detector_type_ = DetectorType::RIIDEyeLaBr;
      return;
    }
    
    if( icontains(model,"NaI") )
    {
      detector_type_ = DetectorType::RIIDEyeNaI;
      return;
    }
  }//if( icontains(model,"RIIDEye") )
  
  
  if( (icontains(model,"SAM940") || icontains(model,"SAM 940") || icontains(model,"SAM Eagle"))
     && !icontains(manufacturer_,"Princeton") )
  {
    if( icontains(model,"LaBr") )
      detector_type_ = DetectorType::Sam940LaBr3;
    else
      detector_type_ = DetectorType::Sam940;
    
    return;
  }
  
  
  if( icontains(model,"SAM") && icontains(model,"945") )
  {
    detector_type_ = DetectorType::Sam945;
    return;
  }
  
  
  if( icontains(model,"SAM") && icontains(model,"950") )
  {
    detector_type_ = DetectorType::Sam950;
    return;
  }
  
  if( (icontains(manufacturer_,"ICx Radiation") || icontains(manufacturer_,"FLIR"))
           && icontains(model,"Raider") )
  {
    detector_type_ = DetectorType::MicroRaider;
    return;
  }
  
  
  if( icontains(model,"SRPM") && icontains(model,"210") )
  {
    if( manufacturer_.size() < 2 )
      manufacturer_ = "Leidos";  //"EXPLORANIUM" would be the other option
    detector_type_ = DetectorType::Srpm210;
    
    return;
  }//if( SRPM 210 )
  
  
  if( icontains(model,"RadSeeker") /* && icontains(manufacturer_, "Smiths") */ )
  {
    if( icontains(model,"CS") )
      detector_type_ = DetectorType::RadSeekerNaI;
    else if( icontains(model,"CL") || icontains(model,"DL") || icontains(model, "LaBr") )
      detector_type_ = DetectorType::RadSeekerLaBr;
    
    return;
  }//if( icontains(model,"RadSeeker") )
  
  
  if( icontains(manufacturer_, "Symetrica") 
     || SpecUtils::icontains(model, "VeriFinder")
     || SpecUtils::icontains(model, "Symetrica") )
  {
    // The potential models are:
    //  - SN20:   Handheld RIID, NaI
    //  - SN20-N: Handheld RIID, NaI, non-He3
    //  - SN23-N: Handheld RIID, NaI, non-He3; GPS, Wi-Fi, cellular & Bluetooth
    //  - SL23-N: Handheld RIID, LaBr, non-He3; GPS, Wi-Fi, cellular & Bluetooth
    
    // The <RadInstrumentModelName> tag seems to be "SN20", "SN23-N", etc, but sample size is small.
    // \TODO: verify general form Verifinders will have in this element
    const bool isVerifinder = (SpecUtils::icontains(model, "SN2")
                               || SpecUtils::icontains(model, "VeriFinder")
                               || SpecUtils::icontains(model, "SL2") );
    
    
    // model of "SN11" is the "Compact VeriFinder"
    // model of "SN33" is also possible - looks to be 3x3x3 NaI, so probably the backpack system
    
    if( isVerifinder )
    {
      // Checking the model name to start with "SL2" should catch LaBr, but just to be sure
      //  (in case there is a future SL3x model or something), we'll see
      //  if any of the <RadDetectorInformation> -> <RadDetectorKindCode> elements had LaBr in them.
      bool isLaBr = SpecUtils::icontains(model, "SL2");
      
      for( size_t i = 0; !isLaBr && (i < 12) && i < measurements_.size(); ++i )
      {
        std::shared_ptr<Measurement> &meas = measurements_[i];
        const auto &desc = meas->detector_description_;
        
        const char *key = "Kind: ";
        const auto pos = desc.find( key );
        if( pos != string::npos )
        {
          const size_t keylen = strlen(key);
          const char * const str_begin = desc.c_str() + pos + keylen;
          assert( desc.size() >= (pos + keylen) );
          const string::size_type max_len = desc.size() - pos - keylen;
          
          string value( str_begin, std::min( max_len, size_t(8) ) );
          if( icontains(value, "LaBr") || icontains(value, "La-Br") || icontains(value, "Br3") )
          {
            isLaBr = true;
            break;
          }
          
          if( icontains(value, "NaI") )
          {
            isLaBr = false;
            break;
          }
        }//if( pos != string::npos )
      }//for( size_t i = 0; i < measurements_.size(); ++i )
      
      detector_type_ = isLaBr ? DetectorType::VerifinderLaBr : DetectorType::VerifinderNaI;
      return;
    }//if( isVerifinder )
  }//if( icontains(manufacturer_, "Symetrica") )
    
  
  if( icontains(manufacturer_,"Canberra Industries, Inc.") )
  {
    //Check to see if detectors like "Aa1N+Aa2N", or "Aa1N+Aa2N+Ba1N+Ba2N+Ca1N+Ca2N+Da1N+Da2N"
    //  exist and if its made up of other detectors, get rid of those spectra
  }//
  
  if( icontains(instrument_model_, "Fulcrum") )
  {
    if( icontains(instrument_model_, "40") )
      detector_type_ = DetectorType::Fulcrum40h;
    else
      detector_type_ = DetectorType::Fulcrum;
    return;
  }//if( icontains(instrument_model_, "Fulcrum") )
  
  
  if( manufacturer_.size() || instrument_model_.size() )
  {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
    //In principle we should some of the following detectors to the DetectorType enum
    if( !(manufacturer_=="Princeton Gamma-Tech Instruments, Inc." && instrument_model_=="RIIDEye")
       && !(manufacturer_=="ICx Technologies" && instrument_model_=="")
       && !(manufacturer_=="Radiation Solutions Inc." /* && instrument_model_=="RS-701"*/)
       && !(manufacturer_=="Raytheon" && instrument_model_=="Variant L" )
       && !(manufacturer_=="Avid Annotated Spectrum" /* && instrument_model_==""*/)
       && !(manufacturer_=="Mirion Technologies" && instrument_model_=="model Pedestrian G")
       && !(manufacturer_=="Princeton Gamma-Tech Instruments, Inc." && instrument_model_=="")
       && !(manufacturer_=="Nucsafe" && instrument_model_=="G4_Predator")
       && !(manufacturer_=="Princeton Gamma-Tech Instruments, Inc." && instrument_model_=="Model 135")
       && !(manufacturer_=="" && instrument_model_=="Self-Occuluding Quad NaI Configuration")
       && !(manufacturer_=="" && instrument_model_=="3x3x12 inch NaI Side Ortec Digibase MCA")
       && !(manufacturer_=="Canberra Industries, Inc." && instrument_model_=="ASP EDM")
       && !(manufacturer_=="Raytheon" && instrument_model_=="Variant C")
       && !(manufacturer_=="Scan-Electronics" && instrument_model_=="RadiaCode-102")
       && !(manufacturer_=="Unknown" && instrument_model_=="Unknown")
       && !icontains( manufacturer_, "RIDs R Us")
       && !icontains( manufacturer_, "SRPMs R Us")
       && !icontains( manufacturer_, "RIIDs R Us")
       && !icontains( manufacturer_, "Mobiles R Us")
       && !icontains( manufacturer_, "SPRDs R Us")
       && !(manufacturer_=="BTI" && instrument_model_=="FlexSpec")
       && !(manufacturer_=="labZY")
       && !icontains( manufacturer_, "SSC Pacific")
       && !icontains( instrument_model_, "ASP-")
       && !(icontains( manufacturer_, "Kromek") && instrument_model_=="MultiSpect/KSpect")
       && !icontains( instrument_model_, "LANL")
       && !icontains( instrument_model_, "LLNL")
       && !icontains( instrument_model_, "SNL")
       && !(manufacturer_=="" && instrument_model_=="")
       )
    {
      string msg = "Unknown detector type: manufacturer=" + manufacturer_ + ", ins_model=" + instrument_model_;
      log_developer_error( __func__, msg.c_str() );
    }
#endif
    //Unknown detector type: maufacturer=Smiths Detection, ins_model=RadSeeker_CS
    //Unknown detector type: maufacturer=Smiths Detection, ins_model=RadSeeker_DL
    //Unknown detector type: maufacturer=Princeton Gamma-Tech Instruments, Inc., ins_model=RIIDEye, Ext2x2
}
   
  
  
  
  
  
}//void set_detector_type_from_other_info()


#if( PERFORM_DEVELOPER_CHECKS )
double SpecFile::deep_gamma_count_sum() const
{
  double deep_gamma_sum = 0.0;
  for( const auto &meas : measurements_ )
  {
    if( !meas )
      continue;
    if( !!meas->gamma_counts_ )
    {
      for( const float f : *(meas->gamma_counts_) )
        deep_gamma_sum += f;
    }
  }//for( const auto &meas : measurements_ )
  
  return deep_gamma_sum;
}//double deep_gamma_count_sum() const

double SpecFile::deep_neutron_count_sum() const
{
  double deep_sum = 0.0;
  for( const auto &meas : measurements_ )
  {
    if( !meas )
      continue;
    for( const float f : meas->neutron_counts_ )
      deep_sum += f;
  }//for( const auto &meas : measurements_ )
  
  return deep_sum;
}//double deep_neutron_count_sum() const;

#endif //#if( PERFORM_DEVELOPER_CHECKS )


void SpecFile::recalc_total_counts()
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  gamma_live_time_ = 0.0f;
  gamma_real_time_ = 0.0f;
  gamma_count_sum_ = 0.0;
  neutron_counts_sum_ = 0.0;
  
  //We have an issue that the same data can be repeated, due to multiple energy
  //  calibrations, doubling neutron or gamma counts...  Right now we are just
  //  ignoring this.
  
  for( const auto &meas : measurements_ )
  {
    if( meas )
    {
      if( meas->gamma_counts_ && !meas->gamma_counts_->empty())
      {
        gamma_live_time_ += meas->live_time_;
        gamma_real_time_ += meas->real_time_;
      }

      gamma_count_sum_    += meas->gamma_count_sum_;
      neutron_counts_sum_ += meas->neutron_counts_sum_;
    }//if( meas )
  }//for( const auto &meas : measurements_ )
  
#if( PERFORM_DEVELOPER_CHECKS )
  const double deep_gamma_sum = deep_gamma_count_sum();
  const double deep_neutron_sum = deep_neutron_count_sum();
  
  if( fabs(deep_gamma_sum - gamma_count_sum_) > 0.1
      && fabs(deep_gamma_sum - gamma_count_sum_) > 1.0E-7*max(fabs(deep_gamma_sum), fabs(gamma_count_sum_)) )
  {
    char buffer[1024];
    snprintf( buffer, sizeof(buffer),
             "recalc_total_counts() found a discrepance for sum gammas depending"
            " on if a shallow or deep count was done: %9f for shallow, %9f for"
            " deep\n", gamma_count_sum_, deep_gamma_sum );
    log_developer_error( __func__, buffer );
    assert( 0 );
  }
  
  if( fabs(deep_neutron_sum - neutron_counts_sum_) > 0.1 )
  {
    char buffer[1024];
    snprintf( buffer, sizeof(buffer),
              "recalc_total_counts() found a discrepance for sum nuetrons depending"
              " on if a shallow or deep count was done: %9f for shallow, %9f for"
              " deep\n", neutron_counts_sum_, deep_neutron_sum );
    log_developer_error( __func__, buffer );
    assert( 0 );
  }
  
#endif //#if( PERFORM_DEVELOPER_CHECKS )
}//void recalc_total_counts()




std::string SpecFile::generate_psuedo_uuid() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  std::size_t seed = 0;
  
  
  boost::hash_combine( seed, gamma_live_time_ );
  boost::hash_combine( seed, gamma_real_time_ );
  
  boost::hash_combine( seed, gamma_count_sum_ );
  boost::hash_combine( seed, neutron_counts_sum_ );
//  boost::hash_combine( seed, filename_ );
  boost::hash_combine( seed, detector_names_ );
//  boost::hash_combine( seed, detector_numbers_ );
  boost::hash_combine( seed, neutron_detector_names_ );

// Wont use gamma_detector_names_ for compatibility pre 20190813 when field was added
//  boost::hash_combine( seed, gamma_detector_names_ );
  
  if( !remarks_.empty() )
    boost::hash_combine( seed, remarks_ );
  //parse_warnings_
  boost::hash_combine( seed, lane_number_ );
  if( !measurement_location_name_.empty() )
  boost::hash_combine( seed, measurement_location_name_ );
  if( !inspection_.empty() )
    boost::hash_combine( seed, inspection_ );
  
  boost::hash_combine( seed, instrument_type_ );
  boost::hash_combine( seed, manufacturer_ );
  boost::hash_combine( seed, instrument_model_ );
  
  if( SpecUtils::valid_latitude(mean_latitude_)
     && SpecUtils::valid_longitude(mean_longitude_) )
  {
    boost::hash_combine( seed, mean_latitude_ );
    boost::hash_combine( seed, mean_longitude_ );
  }
  
  //Note, not including properties_flags_
  
  boost::hash_combine( seed, instrument_id_ );
  boost::hash_combine( seed, measurements_.size() );
//  boost::hash_combine( seed, detectors_analysis_ );
  boost::hash_combine( seed, int(detector_type_) );
  boost::hash_combine( seed, measurement_operator_ );
  
  for( const std::shared_ptr<const Measurement> meas : measurements_ )
  {
    boost::hash_combine( seed, meas->live_time() );
    boost::hash_combine( seed, meas->real_time() );
    boost::hash_combine( seed, meas->gamma_count_sum() );
    boost::hash_combine( seed, meas->neutron_counts_sum() );
    
    if( SpecUtils::valid_latitude(meas->latitude()) )
      boost::hash_combine( seed, meas->latitude() );
    if( SpecUtils::valid_longitude(meas->longitude()) )
      boost::hash_combine( seed, meas->longitude() );
    //  boost::hash_combine( seed, position_time_ );
  }//for( const std::shared_ptr<const Measurement> meas : measurements_ )

  
//  std::set<int> sample_numbers_;
//  std::shared_ptr<const DetectorAnalysis> detectors_analysis_;
  
  string uuid;
  
  if(!measurements_.empty() && measurements_[0]
      && !is_special(measurements_[0]->start_time()) )
    uuid = SpecUtils::to_iso_string( measurements_[0]->start_time() );
  else
    uuid = SpecUtils::to_iso_string( time_from_string( "1982-07-28 23:59:59:000" ) );
  
  //uuid something like: "20020131T100001,123456789"
  if( uuid.size() >= 15 )
    uuid = uuid.substr(2,6) + uuid.substr(9,2) + "-" + uuid.substr(11,4) + "-4"
           + ((uuid.size()>=18) ? uuid.substr(16,2) : string("00"));
  
  const uint64_t seed64 = seed;
//  char buffer[64];
//  snprintf( buffer, sizeof(buffer), "%.16llu", seed64 ); //PRIu64
//  const string seedstr = buffer;
  stringstream seedstrm;
  seedstrm  << setw(16) << setfill('0') << seed64;
  const string seedstr = seedstrm.str();
  
  if( seedstr.size() >= 16 )
    uuid += seedstr.substr(0,1) + "-a" + seedstr.substr(1,3) + "-"
            + seedstr.substr(4,12);
  
  //Now in form (using 1982 data above) of 82072823-5959-4xxx-a-xxxxxxxxxxxx
  //  and where the x are hexadecimal digits
  
  return uuid;
}//std::string generate_psuedo_uuid() const






#if( SpecUtils_ENABLE_D3_CHART )
bool SpecFile::write_d3_html( ostream &ostr,
                              const D3SpectrumExport::D3SpectrumChartOptions &options,
                              std::set<int> sample_nums,
                              std::vector<std::string> det_names ) const
{
  try
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
    if( sample_nums.empty() )
      sample_nums = sample_numbers_;
  
    if( det_names.empty() )
      det_names = detector_names_;
  
    std::shared_ptr<Measurement> summed = sum_measurements( sample_nums, det_names, nullptr );
  
    if( !summed || !summed->gamma_counts() || summed->gamma_counts()->empty() )
      return false;
  
    
    vector< pair<const Measurement *,D3SpectrumExport::D3SpectrumOptions> > measurements;
    D3SpectrumExport::D3SpectrumOptions spec_options;
    measurements.push_back( pair<const Measurement *,::D3SpectrumExport::D3SpectrumOptions>(summed.get(),spec_options) );
    
    return D3SpectrumExport::write_d3_html( ostr, measurements, options );
  }catch( std::exception & )
  {
     return false;
  }
  
  return true;
}
#endif

  
  
  
void SpecFile::rebin_measurement( const std::shared_ptr<const EnergyCalibration> &cal,
                                  const std::shared_ptr<const Measurement> &measurement )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( !cal || (cal->num_channels() < 4) )
    throw runtime_error( "rebin_measurement: invalid calibration passed in" );
  
  std::shared_ptr<Measurement> meas;
  for( size_t i = 0; !meas && (i < measurements_.size()); ++i )
  {
    if( measurement == measurements_[i] )
      meas = measurements_[i];
  }
  
  if( !meas )
    throw runtime_error( "rebin_measurement: invalid passed in measurement" );
  
  if( cal == meas->energy_calibration_ )
    return;
  
  meas->rebin( cal );
  
  if( (properties_flags_ & kHasCommonBinning) && (measurements_.size() > 1) )
  {
    //only unset kHasCommonBinning bit if there is more than on gamma measurement (could tighten up
    //  and also check if all calibrations now match, and if so set flag)
    bool other_gamma_meas = false;
    for( size_t i = 0; !other_gamma_meas && (i < measurements_.size()); ++i )
    {
      const auto &m = measurements_[i];
      other_gamma_meas = (m && m->gamma_counts_ && !m->gamma_counts_->empty()
                          && (m->energy_calibration_ != cal));
    }
    if( other_gamma_meas )
      properties_flags_ &= ~kHasCommonBinning;
  }//if( unset kHasCommonBinning flag )
  
  modified_ = modifiedSinceDecode_ = true;
}//rebin_measurement(...)
                            
                            
void SpecFile::rebin_all_measurements( const std::shared_ptr<const EnergyCalibration> &cal )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( !cal || (cal->num_channels() < 4) )
    throw runtime_error( "rebin_measurement: invalid calibration passed in" );
  
  SpecUtilsAsync::ThreadPool threadpool;
  
  for( auto &m : measurements_ )
  {
    assert( m );
    assert( m->energy_calibration_ );
    
    if( !m->gamma_counts_
       || (m->gamma_counts_->size() < 4)
       || (m->energy_calibration_->num_channels() < 4) )
    {
      continue;
    }
    
    threadpool.post( [m,&cal](){ m->rebin(cal); } );
  }//for( auto &m : measurements_ )
  
  threadpool.join();
  
  properties_flags_ |= kHasCommonBinning;
  modified_ = modifiedSinceDecode_ = true;
}//rebin_all_measurements(...)



void SpecFile::set_energy_calibration( const std::shared_ptr<const EnergyCalibration> &cal,
                                const std::shared_ptr<const Measurement> &constmeas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
          
  if( !cal )
    throw runtime_error( "set_calibration: invalid calibration passed in" );
          
  std::shared_ptr<Measurement> meas = measurement(constmeas);
          
  if( !meas )
    throw runtime_error( "set_calibration: invalid passed in measurement" );
          
  if( cal == meas->energy_calibration_ )
    return;
    
  meas->set_energy_calibration( cal );

  if( (properties_flags_ & kHasCommonBinning) && (measurements_.size() > 1) )
  {
    //only unset kHasCommonBinning bit if there is more than on gamma measurement (could tighten up
    //  and also check if all calibrations now match, and if so set flag)
    bool other_gamma_meas = false;
    for( size_t i = 0; !other_gamma_meas && (i < measurements_.size()); ++i )
    {
      const auto &m = measurements_[i];
      other_gamma_meas = (m && m->gamma_counts_ && !m->gamma_counts_->empty()
                          && (m->energy_calibration_ != cal));
    }
    if( other_gamma_meas )
      properties_flags_ &= ~kHasCommonBinning;
  }//if( unset kHasCommonBinning flag )
  
  modified_ = modifiedSinceDecode_ = true;
}//set_energy_calibration(...)
                            

size_t SpecFile::set_energy_calibration( const std::shared_ptr<const EnergyCalibration> &cal,
                                       std::set<int> sample_numbers,
                                       std::vector<std::string> detectors )
{
  if( !cal )
    throw runtime_error( "set_energy_calibration: null calibration passed in" );
  
  if( sample_numbers.empty() )
    sample_numbers = sample_numbers_;
  if( detectors.empty() )
    detectors = detector_names_;
  
  std::sort( begin(detectors), end(detectors) );
  auto is_wanted_det = [&detectors]( const std::string &name ) -> bool {
    const auto pos = lower_bound( begin(detectors), end(detectors), name );
    return ((pos != end(detectors)) && (name == (*pos)));
  };//is_wanted_det(...)
  
  auto is_wanted_sample = [&sample_numbers]( const int sample ) -> bool {
    return (sample_numbers.count(sample) != 0);
  };//is_wanted_sample(...)
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  //Grab the matching measurements, and also check they are compatible with passed in binning before
  //  making any changes.
  vector<shared_ptr<Measurement>> matching_meas;
  
  for( const auto &m : measurements_ )
  {
    const size_t nchannel = ((m && m->gamma_counts_) ? m->gamma_counts_->size() : 0u);
    const size_t ncalchannel = cal->num_channels();
    
    if( nchannel && is_wanted_sample(m->sample_number_) && is_wanted_det(m->detector_name_) )
    {
      //Check that binning is compatible so wont throw an excpetion later when we actually set the
      //  calibration
      switch( cal->type() )
      {
        case EnergyCalType::Polynomial:
        case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
        case EnergyCalType::FullRangeFraction:
        case EnergyCalType::LowerChannelEdge:
          assert( cal->channel_energies() );
          //For LowerChannelEdge we'll let the number of calibration channels be larger than
          //  spectrum channels since some formats specify an upper energy.  (although we should
          //  probably limit the size to one more, but we dont otherplaces yet, so wont here yet).
          if( (ncalchannel != nchannel)
              && ((cal->type()!=EnergyCalType::LowerChannelEdge) || (ncalchannel<nchannel)) )
            throw runtime_error( "set_energy_calibration: incomatible number of channels ("
                                 + std::to_string(nchannel) + " vs the calibrations "
                                 + std::to_string(ncalchannel) + ")" );
        break;
          
        case EnergyCalType::InvalidEquationType:
        break;
      }//switch( cal->type() )
      
      matching_meas.push_back( m );
    }//if( is this a wanted Measurement )
  }//for( const auto &m : measurements_ )
  
  for( auto m : matching_meas )
    m->set_energy_calibration( cal );
  
  //Check if we can have common binning set -
  //  \TODO: this will miss the case the user specified only the gamma measurements, so should fix
  bool has_common = (matching_meas.size() == measurements_.size());
  if( !has_common
      && (sample_numbers==sample_numbers_)
      && (detectors.size()==detector_names_.size()) )
  {
    auto sorted_dets = detector_names_;
    std::sort( begin(sorted_dets), end(sorted_dets) );
    has_common = (sorted_dets == detectors);
  }
  
  if( has_common )
    properties_flags_ |= kHasCommonBinning;
  else
    properties_flags_ &= ~kHasCommonBinning;
    
  modified_ = modifiedSinceDecode_ = true;
  
  return matching_meas.size();
}//set_energy_calibration(...)

  
void SpecFile::set_energy_calibration_from_CALp_file( std::istream &infile )
{
  const std::streampos start_pos = infile.tellg();
 
  try
  {
    // TODO: currently if we have detectors/measurements with different number of channels, then we
    //       may run into an error if the CALp file gives lower channel energies, but
    //       `num_gamma_channels()` returned a different number of channels than is required for any
    //       of the detectors
    size_t fewest_num_channel = 65537, max_num_channels = 0;
    
    // Make a mapping of {detector name, number channels} to energy calibrations we
    //  will read from the CALp file
    map<pair<string,size_t>,shared_ptr<EnergyCalibration>> det_to_cal;
    
    for( const shared_ptr<Measurement> &m : measurements_ )
    {
      const size_t nchan = m->num_gamma_channels();
      if( nchan > 3 )
      {
        fewest_num_channel = std::min( fewest_num_channel, nchan );
        max_num_channels = std::max( max_num_channels, nchan );
        det_to_cal[make_pair(m->detector_name_,nchan)] = nullptr;
      }
    }//for( const auto &m : measurements_ )
    
    if( max_num_channels < 3 )
      throw runtime_error( "No gamma spectra to apply CALp to." );
    
    assert( !det_to_cal.empty() );
    
    while( infile.good() )
    {
      const std::streampos calp_pos = infile.tellg();
      
      string name;
      shared_ptr<EnergyCalibration> cal;
      
      try
      {
        cal = energy_cal_from_CALp_file( infile, fewest_num_channel, name );
      }catch( exception &e )
      {
        if( det_to_cal.empty() )
          throw runtime_error( "CALp file was invalid, or incompatible" );
        
        break; //We just reached the end of the file probably, no biggy
      }//try / catch
      
      assert( cal && cal->valid() );
      assert( cal->type() != SpecUtils::EnergyCalType::InvalidEquationType );
      
      // The calibration may not be for the correct number of channels, for files that have detectors
      //  with different num channels; we'll check for this here and fix the calibration up for this
      //  case.
      for( auto &name_nchan_cal : det_to_cal )
      {
        const string &meas_det_name = name_nchan_cal.first.first;
        const size_t nchan = name_nchan_cal.first.second;
        
        if( !name.empty() && (meas_det_name != name) )
          continue;
        
        // If we already have the right number of channels, we dont need to do anything
        //  and can set the energy calibration here
        if( nchan != fewest_num_channel )
        {
          try
          {
            switch( cal->type() )
            {
              case SpecUtils::EnergyCalType::Polynomial:
              case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
                cal->set_polynomial( nchan, cal->coefficients(), cal->deviation_pairs() );
                break;
                
              case SpecUtils::EnergyCalType::FullRangeFraction:
                cal->set_full_range_fraction( nchan, cal->coefficients(), cal->deviation_pairs() );
                break;
                
              case SpecUtils::EnergyCalType::LowerChannelEdge:
              {
                // We will need to re-read the calibration, with the updated number of channels.
                if( !infile.seekg( calp_pos, ios::beg ) )
                  throw runtime_error( "Failed to seek the input CALp file stream." );
                
                cal = SpecUtils::energy_cal_from_CALp_file( infile, nchan, name );
                if( !cal )
                  throw runtime_error( "CALp file is incompatible with detector '" + name + "'"
                                      " having " + std::to_string(nchan) + " channels." );
                break;
              }//case SpecUtils::EnergyCalType::LowerChannelEdge:
                
              case SpecUtils::EnergyCalType::InvalidEquationType:
                throw std::logic_error( "Invalid calibration type???" );
                break;
            }//switch( cal->type() )
          }catch( std::exception &e )
          {
            throw runtime_error( "Failed to adjust the number of channels in the CALp file"
                                " to what detector '" + name + "' required: " + string(e.what()) );
          }//try / catch
        }//if( nchan != fewest_num_channel )
        
        assert( cal && cal->valid() && (cal->num_channels() == nchan) );
        
        name_nchan_cal.second = cal;
      }//for( const auto &name_nchan_cal : det_to_cal )
    }//while( infile.good() )
    
    
    // Now lets make sure we have energy calibrations for all measurements
    for( auto &name_nchan_cal : det_to_cal )
    {
      const string &det_name = name_nchan_cal.first.first;
      const size_t nchan = name_nchan_cal.first.second;
      const shared_ptr<EnergyCalibration> &cal = name_nchan_cal.second;
      
      assert( !cal || cal->valid() );
      
      if( !cal || !cal->valid() )
        throw runtime_error( "Failed to get an energy calibration for detector '" + det_name + "'"
                            " with " + std::to_string(nchan) + " channels, from the CALp file.");
    }//for( auto &name_nchan_cal : det_to_cal )
    
    // If we are here, we should be good to go
    for( const auto &m : measurements_ )
    {
      const size_t nchan = m->num_gamma_channels();
      if( nchan <= 3 )
        continue;
      
      const auto pos = det_to_cal.find( make_pair(m->detector_name_, nchan) );
      assert( pos != end(det_to_cal) );
      if( pos == end(det_to_cal) )
        throw std::logic_error( "Logic error retrieving energy calibration - this shouldnt have happened." );
      
      const shared_ptr<EnergyCalibration> cal = pos->second;
      assert( cal && cal->valid() && (cal->num_channels() == nchan) );
      if( !cal || !cal->valid() || (cal->num_channels() != nchan) )
        throw std::logic_error( "Logic error with CALp energy calibration - this shouldnt have happened." );
      
      m->set_energy_calibration( cal );
    }//for( const auto &m : measurements_ )
  }catch( std::exception &e )
  {
    // Set the input file back to the original position
    infile.seekg( start_pos, ios::beg );
    
    throw; //re-throw the exception
  }//try / catch
}//void set_energy_calibration_from_CALp_file( std::istream &input )
  

size_t SpecFile::memmorysize() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  size_t size = sizeof(*this);

  size += filename_.capacity()*sizeof(string::value_type);
  for( const string &s : detector_names_ )
    size += s.capacity()*sizeof(string::value_type);
  size += detector_numbers_.capacity()*sizeof(int);
  for( const string &s : gamma_detector_names_ )
    size += s.capacity()*sizeof(string::value_type);
  for( const string &s : neutron_detector_names_ )
    size += s.capacity()*sizeof(string::value_type);

  size += uuid_.capacity()*sizeof(string::value_type);
  for( const string &str : remarks_ )
    size += str.capacity()*sizeof(string::value_type);
  size += measurement_location_name_.capacity()*sizeof(string::value_type);
  size += inspection_.capacity()*sizeof(string::value_type);
  size += sample_numbers_.size()*sizeof(int);

  size += sample_to_measurements_.size() * sizeof(vector<size_t>);
  typedef std::map<int, std::vector<size_t> > InIndVecMap;
  for( const InIndVecMap::value_type &t : sample_to_measurements_ )
    size += t.second.capacity() * sizeof(size_t);

  size += instrument_type_.capacity()*sizeof(string::value_type);
  size += manufacturer_.capacity()*sizeof(string::value_type);
  size += instrument_model_.capacity()*sizeof(string::value_type);
  size += instrument_id_.capacity()*sizeof(string::value_type);

  size += measurements_.capacity() * sizeof( std::shared_ptr<Measurement> );

  //keep from double counting energy calibrations shared between Measurement objects.
  set<const EnergyCalibration *> calibrations_seen;
  set<const SpecUtils::LocationState *> locations_seen;
  
  for( const auto &m : measurements_ )
  {
    size += m->memmorysize();
    assert( m->energy_calibration_ );
    if( calibrations_seen.count( m->energy_calibration_.get() ) )
      size -= m->energy_calibration_->memmorysize();
    calibrations_seen.insert( m->energy_calibration_.get() );
    
    if( m->location_ )
    {
      if( locations_seen.count( m->location_.get() ) )
        size -= m->location_->memmorysize();
      locations_seen.insert( m->location_.get() );
    }//if( m->location_ )
  }//for( const auto &m, measurements_ )

  return size;
}//size_t memmorysize() const


bool SpecFile::passthrough() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  return (properties_flags_ & kPassthroughOrSearchMode);
}//bool passthrough() const


bool SpecFile::contains_derived_data() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  return (properties_flags_ & kDerivedDataMeasurements);
}

bool SpecFile::contains_non_derived_data() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  return (properties_flags_ & kNonDerivedDataMeasurements);
}//

std::shared_ptr<const EnergyCalibration> SpecFile::suggested_sum_energy_calibration(
                                                        const set<int> &sample_numbers,
                                                        const vector<string> &detector_names ) const
{
  if( sample_numbers.empty() || detector_names.empty() )
    return nullptr;
  
  for( const int sample : sample_numbers )
  {
    if( !sample_numbers_.count(sample) )
      throw runtime_error( "suggested_sum_energy_calibration: invalid sample number "
                           + to_string(sample) );
  }//for( check all sample numbers were valid )
  
  for( const string &name : detector_names )
  {
    const auto pos = std::find( begin(detector_names_), end(detector_names_), name );
    if( pos == end(detector_names_) )
      throw runtime_error( "suggested_sum_energy_calibration: invalid detector name '"
                           + name + "'" );
  }//for( check all detector names were valid )
  
  auto energy_range = []( const std::shared_ptr<const EnergyCalibration> &cal ) -> float {
    return !cal ? 0.0f : (cal->upper_energy() - cal->lower_energy());
  };
  
  size_t energy_cal_index = 0;
  std::shared_ptr<const EnergyCalibration> energy_cal;
  
  const bool has_common = ((properties_flags_ & kHasCommonBinning) != 0);
  const bool same_nchannel = ((properties_flags_ & kAllSpectraSameNumberChannels) != 0);
  
  for( size_t i = 0; i < measurements_.size(); ++i )
  {
    const std::shared_ptr<Measurement> &meas = measurements_[i];
      
    if( !sample_numbers.count(meas->sample_number_) )
      continue;
    
    const auto pos = std::find( begin(detector_names), end(detector_names), meas->detector_name_);
    if( pos == end(detector_names) )
      continue;
    
    const auto &this_cal = meas->energy_calibration();
      
    if( this_cal && (this_cal->type() != EnergyCalType::InvalidEquationType) )
    {
  #if(!PERFORM_DEVELOPER_CHECKS)
      if( has_common )
        return this_cal;
      
      if( this_cal == energy_cal )
        continue;
      
      const size_t this_nchannel = this_cal->num_channels();
      const size_t prev_nchannel = energy_cal ? energy_cal->num_channels() : size_t(0);
      const float prev_range = energy_range(energy_cal);
      const float this_range = energy_range(this_cal);
      
      if( !energy_cal
         || (prev_nchannel < this_nchannel)
         || ((prev_nchannel == this_nchannel) && (prev_range < this_range)) )
      {
        energy_cal_index = i;
        energy_cal = this_cal;
      }//if( !binning_ptr || (binning_ptr->size() < thisbinning->size()) )
  #else
      if( has_common && energy_cal && (energy_cal != this_cal) && ((*energy_cal) == (*this_cal)) )
      {
#if( SpecUtils_ENABLE_EQUALITY_CHECKS )
        string errmsg = "EnergyCalibration::equal_enough didnt find any differences";
        try
        {
          EnergyCalibration::equal_enough( *this_cal, *energy_cal );
        }catch( std::exception &e )
        {
          errmsg = e.what();
        }
#else
        string errmsg = "EnergyCalibration::equal_enough not called to check equality.";
#endif
        char buffer[512];
        snprintf( buffer, sizeof(buffer), "Found case where expected common energy calibration"
                 " but didnt actually have all the same binning, issue found: %s", errmsg.c_str() );
        log_developer_error( __func__, buffer );
      }//if( has_common, but energy calibration wasnt the same )
      
      const size_t this_nchannel = this_cal->num_channels();
      const size_t prev_nchannel = energy_cal ? energy_cal->num_channels() : size_t(0);
      const float prev_range = energy_range(energy_cal);
      const float this_range = energy_range(this_cal);
      
      if( !energy_cal
         || (prev_nchannel < this_nchannel)
         || ((prev_nchannel == this_nchannel) && (prev_range < this_range)) )
      {
        if( same_nchannel && energy_cal
            && (energy_cal->num_channels() != this_cal->num_channels()) )
        {
          char buffer[512];
          snprintf( buffer, sizeof(buffer),
                    "Found instance of differening number of gamma channels,"
                    " when I shouldnt have; measurement %i had %i channels,"
                    " while measurement %i had %i channels.",
                    static_cast<int>(energy_cal_index),
                    static_cast<int>(energy_cal->num_channels()),
                    static_cast<int>(i),
                    static_cast<int>(this_cal->num_channels()) );
          log_developer_error( __func__, buffer );
        }
          
        energy_cal_index = i;
        energy_cal = this_cal;
      }//if( !binning_ptr || (binning_ptr->size() < thisbinning->size()) )
  #endif
    }//if( this binning is valid )
  }//for( size_t i = 0; i < measurements_.size(); ++i )
    
  return energy_cal;
}//suggested_sum_energy_calibration(...)



std::shared_ptr<Measurement> SpecFile::sum_measurements( const std::set<int> &sample_numbers,
                                      const std::vector<std::string> &det_names,
                                      std::shared_ptr<const EnergyCalibration> ene_cal ) const
{
  if( det_names.empty() || sample_numbers.empty() )
    return nullptr;
    
  std::shared_ptr<Measurement> dataH = std::make_shared<Measurement>();
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  //Check all provided sample numbers are valid
  for( const int sample : sample_numbers )
  {
    if( !sample_numbers_.count(sample) )
      throw runtime_error( "sum_measurements: invalid sample number passed in ('"
                           + to_string(sample) + "')" );
  }
  
  //Check all provided detector names are valid
  for( const string &name : det_names )
  {
    auto pos = std::find( begin(detector_names_), end(detector_names_), name );
    if( pos == end(detector_names_) )
      throw runtime_error( "sum_measurements: invalid detector name passed in ('" + name + "')" );
  }
  
  if( !ene_cal )
    ene_cal = suggested_sum_energy_calibration( sample_numbers, det_names );
  
  if( !ene_cal )
    return nullptr;
  
  if( ene_cal->type() == EnergyCalType::InvalidEquationType )
    throw runtime_error( "sum_measurements: called with InvalidEquationType energy calibration" );
  
  dataH->energy_calibration_ = ene_cal;
  
  if( measurements_.size() == 1 )
    dataH->set_title( measurements_[0]->title_ );
  else
    dataH->set_title( filename_ );
  
  dataH->contained_neutron_ = false;
  dataH->real_time_ = 0.0f;
  dataH->live_time_ = 0.0f;
  dataH->gamma_count_sum_ = 0.0;
  dataH->neutron_counts_sum_ = 0.0;
  dataH->neutron_live_time_ = 0.0f;
  dataH->sample_number_ = -1;
  if( sample_numbers.size() == 1 )
    dataH->sample_number_ = *begin(sample_numbers);
  dataH->start_time_ = time_point_t{ time_point_t::duration::max() };
  
  const size_t ndet_to_use = det_names.size();
  if( ndet_to_use == 1 )
  {
    const auto &name = det_names.front();
    dataH->detector_name_ = name;
    const auto pos = std::find( begin(detector_names_), end(detector_names_), name );
    assert( pos != end(detector_names_) );
    dataH->detector_number_ = detector_numbers_[pos-begin(detector_names_)];
  }else
  {
    dataH->detector_name_ = "Summed";
    dataH->detector_number_ = -1;
  }
  
  const bool allBinningIsSame = ((properties_flags_ & kHasCommonBinning) != 0);
  
  
  //any less than 'min_per_thread' than the additional memorry allocation isnt
  //  worth it - briefly determined on my newer mbp using both example
  //  example passthrough (16k channel) and a 512 channel NaI portal passthrough
  const size_t min_per_thread = 8;

  size_t num_thread = static_cast<size_t>( SpecUtilsAsync::num_physical_cpu_cores() );
  const size_t num_potential_specta = ndet_to_use * sample_numbers.size();
  num_thread = min( num_thread, num_potential_specta/min_per_thread );
  num_thread = max( size_t(1), num_thread );
  
  vector< vector< shared_ptr<const Measurement> > > meas_per_thread( num_thread );
  vector< vector< shared_ptr<const vector<float> > > > spectrum_per_thread( num_thread );
  
  // If we do the previous thing of just use num_thread to call sum_with_rebin(...) for every
  //  measurement (which calls rebin_by_lower_edge for every measurement), for a couple cases took:
  //    wall=38.8735s, cpu=309.827s (pathological?)
  //    wall=0.2413590s, cpu=1.63878s (example passthrough)
  //    wall=0.0069361s, cpu=0.048806 (Verifinder)
  //    wall=0.0795112s, cpu=0.629063 (portal p1)
  //    wall=0.0240741s, cpu=0.164865s (portal range 2)
  //    wall=0.0482988s, cpu=0.39505s (portal range 3)
  //
  //  But if we group by the same energy calibration, and just bin-by-bin sum all spectra for each
  //  energy calibration, then call rebin_by_lower_edge(...) to combine the sum for each energy
  //  calibration, that same respective cases took "just":
  //    wall=0.242603s, cpu=2.95812s
  //    wall=0.026771s, cpu=0.09360s
  //    wall=0.009272s, cpu=0.04039s
  //    wall=0.004495s, cpu=0.04022s
  //    wall=0.002311s, cpu=0.01358s
  //    wall=0.002978s, cpu=0.03313s
  //
  //  E.g., about a ~10x to 100x speed increase usually
  //
  //  However, using 'GROUP_BY_ENE_CAL_BEFORE_SUM' method has not been extensively tested yet
  //  TODO: we could also remove inserting into 'meas_per_thread' and 'spectrum_per_thread', or
  //        anything related to 'num_thread' for the case (allBinningIsSame == false)
#define GROUP_BY_ENE_CAL_BEFORE_SUM 1
  
#if( GROUP_BY_ENE_CAL_BEFORE_SUM )
  map< shared_ptr<const EnergyCalibration>, vector<shared_ptr<const vector<float>>> > cal_to_meas;
#endif
  
  size_t total_num_gamma_spec = 0;
  set<SourceType> source_types;
  set<string> remarks;
  for( const int sample_number : sample_numbers )
  {
    for( const string &det : det_names )
    {
      std::shared_ptr<const Measurement> meas = measurement( sample_number, det );
      if( !meas )
        continue;
      
      assert( meas->sample_number_ == sample_number );
      
      std::shared_ptr<const vector<float> > spec = meas->gamma_counts();
      const size_t spec_size = (spec ? spec->size() : (size_t)0);
      
      dataH->start_time_ = std::min( dataH->start_time_, meas->start_time_ );
      dataH->neutron_counts_sum_ += meas->neutron_counts_sum();
      dataH->contained_neutron_ |= meas->contained_neutron_;
        
      if( dataH->neutron_counts_.size() < meas->neutron_counts_.size() )
        dataH->neutron_counts_.resize( meas->neutron_counts_.size(), 0.0f );
      const size_t nneutchannel = meas->neutron_counts_.size();
      for( size_t i = 0; i < nneutchannel; ++i )
        dataH->neutron_counts_[i] += meas->neutron_counts_[i];
        
      if( meas->contained_neutron() )
        dataH->neutron_live_time_ += meas->neutron_live_time();
      
      for( const std::string &remark : meas->remarks_ )
        remarks.insert( remark );
        
      source_types.insert( meas->source_type() );
      
      // Sum dose rate, if valid
      if( meas->dose_rate_ >= 0.0f )
      {
        if( dataH->dose_rate_ >= 0.0f )
          dataH->dose_rate_ += meas->dose_rate_;
        else
          dataH->dose_rate_ += meas->dose_rate_;
      }//if( meas->dose_rate_ >= 0.0f )
      
      // Sum exposure rate, if valid
      if( meas->exposure_rate_ >= 0.0f )
      {
        if( dataH->exposure_rate_ >= 0.0f )
          dataH->exposure_rate_ += meas->exposure_rate_;
        else
          dataH->exposure_rate_ += meas->exposure_rate_;
      }//if( meas->dose_rate_ >= 0.0f )
      
      
      if( spec_size > 3 && meas->energy_calibration() && meas->energy_calibration()->valid() )
      {
        dataH->live_time_ += meas->live_time();
        dataH->real_time_ += meas->real_time();
        dataH->gamma_count_sum_ += meas->gamma_count_sum();
        const size_t thread_num = total_num_gamma_spec % num_thread;
        meas_per_thread[thread_num].push_back( meas );
        spectrum_per_thread[thread_num].push_back( meas->gamma_counts() );
        total_num_gamma_spec += 1;
        
#if( GROUP_BY_ENE_CAL_BEFORE_SUM )
        if( !allBinningIsSame )
        {
          const auto cal = meas->energy_calibration();
          const auto meascounts = meas->gamma_counts();
          auto iter = cal_to_meas.find(cal);
          if( iter != end(cal_to_meas) )
            iter->second.push_back( meascounts );
          else
            cal_to_meas[cal].push_back( meascounts );
        }//if( !allBinningIsSame )
#endif //GROUP_BY_ENE_CAL_BEFORE_SUM
      }
    }//for( size_t index = 0; index < detector_names_.size(); ++index )
  }//for( const int sample_number : sample_numbers )
  
  if( !total_num_gamma_spec )
    return nullptr;

  if( source_types.size() == 1 )
    dataH->source_type_ = *begin(source_types);
  
  //If we are only summing one sample, we can preserve some additional
  //  information
  if( total_num_gamma_spec == 1 )
  {
    dataH->sample_number_        = meas_per_thread[0][0]->sample_number_;
    dataH->occupied_             = meas_per_thread[0][0]->occupied_;
    dataH->detector_name_        = meas_per_thread[0][0]->detector_name_;
    dataH->detector_number_      = meas_per_thread[0][0]->detector_number_;
    dataH->detector_description_ = meas_per_thread[0][0]->detector_description_;
    dataH->quality_status_       = meas_per_thread[0][0]->quality_status_;
    dataH->location_             = meas_per_thread[0][0]->location_;
  }//if( total_num_gamma_spec == 1 )
  
  
  if( allBinningIsSame )
  {
#if( PERFORM_DEVELOPER_CHECKS )
    shared_ptr<const EnergyCalibration> commoncal;
    for( const auto &m : measurements_ )
    {
      assert( m );
      assert( m->energy_calibration_ );
      const auto &cal = m->energy_calibration_;
      const bool valid_cal = (cal->type() != EnergyCalType::InvalidEquationType);
      const bool has_cal = (m->gamma_counts_ && !m->gamma_counts_->empty() && valid_cal);
      
      if( !commoncal && has_cal  )
        commoncal = cal;
      
      if( valid_cal && (!m->gamma_counts_ || m->gamma_counts_->empty()) )
        log_developer_error( __func__, "Have valid energy calibration but no channel counts" );
        
      if( has_cal && (commoncal != cal) && (*commoncal != *cal) )
      {
        log_developer_error( __func__, "Found case where kHasCommonBinning bit is eroneously set" );
        break;
      }
    }//for( auto m : measurements_ )
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
    
    if( spectrum_per_thread.size()<1 || spectrum_per_thread[0].empty() )
      throw runtime_error( string(SRC_LOCATION) + "\n\tSerious programming logic error" );
    
    const size_t spec_size = spectrum_per_thread[0][0]->size();
    auto result_vec = make_shared<vector<float>>( spec_size, 0.0f );
    vector<float> &result_vec_ref = *result_vec;
    dataH->gamma_counts_ = result_vec;
    
    if( num_thread > 1 )
    {
      vector< vector<float> > results( num_thread );
    
      SpecUtilsAsync::ThreadPool threadpool;
      for( size_t i = 0; i < num_thread; ++i )
      {
        vector<float> *dest = &(results[i]);
        const vector< std::shared_ptr<const vector<float> > > *spec = &(spectrum_per_thread[i]);
        threadpool.post( [dest,spec](){ add_to(*dest, *spec); } );
      }
      threadpool.join();
    
      //Note: in principle for a multicore machine (>16 physical cores), we
      //      could combine results using a few different threads down to less
      //      than 'min_per_thread'
      for( size_t i = 0; i < num_thread; ++i )
      {
        if( results[i].size() )
        {
          const float *spec_array = &(results[i][0]);
          
#if( SpecUtils_USE_SIMD )
          const size_t aligendN = spec_size - (spec_size % 4);
          for( size_t i = 0; i < aligendN; i += 4 )
          {
            _mm_storeu_ps( &(result_vec_ref[i]),
                          _mm_add_ps( _mm_loadu_ps( &(result_vec_ref[i]) ),
                                     _mm_loadu_ps( &(spec_array[i]) )));
          }
          
          for( size_t i = aligendN; i < spec_size; ++i )
            result_vec_ref[i] += spec_array[i];
#else
          for( size_t bin = 0; bin < spec_size; ++bin )
            result_vec_ref[bin] += spec_array[bin];
#endif
        }
      }//for( size_t i = 0; i < num_thread; ++i )
    }else
    {
      const vector< std::shared_ptr<const vector<float> > > &spectra = spectrum_per_thread[0];
      const size_t num_spectra = spectra.size();
    
      for( size_t i = 0; i < num_spectra; ++i )
      {
        const size_t len = spectra[i]->size();
        const float *spec_array = &(spectra[i]->operator[](0));
        for( size_t bin = 0; bin < spec_size && bin < len; ++bin )
          result_vec_ref[bin] += spec_array[bin];
      }//for( size_t i = 0; i < num_thread; ++i )
    }//if( num_thread > 1 ) / else
  
    //If we are here, we know all the original Measurements had the same binning, but they may not
    //  have the same binning as 'ene_cal'.
    shared_ptr<const EnergyCalibration> orig_bin;
    for( const auto &m : measurements_)
    {
      const auto &cal = m->energy_calibration();
      if( cal->type() != EnergyCalType::InvalidEquationType )
      {
        if( (cal != ene_cal) && ((*cal) != (*ene_cal)) )
          orig_bin = cal;
        break;
      }
    }//for( check if new cal matches all Measurements cals )
    
    
    if( orig_bin )
    {
      auto resulting_counts = make_shared<vector<float>>();
      SpecUtils::rebin_by_lower_edge( *orig_bin->channel_energies(), result_vec_ref,
                                      *ene_cal->channel_energies(), *resulting_counts );
      dataH->gamma_counts_ = resulting_counts;
    }
  }else //if( allBinningIsSame )
  {
#if( GROUP_BY_ENE_CAL_BEFORE_SUM )
    assert( !cal_to_meas.empty() );
    
#if( PERFORM_DEVELOPER_CHECKS )
    const double start_new_time = get_wall_time();
    const double start_new_cpu = get_cpu_time();
#endif
    
    // results and calibrations will be one-to-one; we may have multiple entries for a
    //  EnergyCalibration to keep the worker threads more balanced
    vector< unique_ptr<vector<float>> > results;
    vector< shared_ptr<const EnergyCalibration>> calibrations;
    
    const size_t nominal_per_thread = std::max( size_t(1), total_num_gamma_spec / std::max( num_thread, size_t(1) ) );
    
    SpecUtilsAsync::ThreadPool threadpool;
    
    for( const auto &iter : cal_to_meas )
    {
      const shared_ptr<const EnergyCalibration> cal = iter.first;
      assert( cal );
      assert( cal->num_channels() > 2 );
      assert( cal->channel_energies() );
      assert( cal->channel_energies()->size() > 2 );
      
      const vector< std::shared_ptr<const vector<float> > > &this_cals_specs = iter.second;
      const size_t nspec = this_cals_specs.size();
      
      size_t npec_per_result = nspec;
      if( nspec >= 2*nominal_per_thread )
      {
        const size_t div = nspec / nominal_per_thread;
        //round up so we might have a little more than nominal_per_thread to avoid a thread with only a few
        npec_per_result = (nspec / div) + ((nspec % div) ? 1 : 0);
      }
      
      for( size_t specn = 0; specn < nspec; specn += npec_per_result )
      {
        calibrations.push_back( cal );
        // Note: we will copy in the first spectrum, and later start summing with second spectrum
        results.emplace_back( new vector<float>( begin(*this_cals_specs[specn]), end(*this_cals_specs[specn]) ) );
        
        if( (nspec == 1) || (npec_per_result == 1) )
          continue;
        
        vector<float> * const result_ptr = results.back().get();
        
        const shared_ptr<const vector<float> > * const start_spec = &(this_cals_specs[specn]);
        
        const size_t num_spectra_sum = ((specn + npec_per_result) <= nspec) ? npec_per_result : (nspec - specn);
        const shared_ptr<const vector<float> > * const end_spec = start_spec + num_spectra_sum;
        
        threadpool.post( [start_spec,end_spec,result_ptr](){
          vector<float> &result = *result_ptr;
          // Note we are summing in starting with the second spectrum (we created result with the first one)
          for( const shared_ptr<const vector<float> > *iter = start_spec + 1; iter != end_spec; ++iter )
          {
            const vector<float> &curraray = *(*iter);
            const size_t size = curraray.size();
            assert( size == result.size() );
            
#if( SpecUtils_USE_SIMD )
            const size_t aligendN = size - (size % 4);
            for( size_t i = 0; i < aligendN; i += 4 )
            {
              // TODO: check performance of using intermediate variables
              //__m128 xmmA = _mm_loadu_ps(&result[i]);
              //__m128 xmmB = _mm_loadu_ps(&curraray[i]);
              //__m128 xmmC = _mm_add_ps( xmmA, xmmB );
              //_mm_storeu_ps( &result[i], xmmC );
              
              _mm_storeu_ps(&result[i],
                            _mm_add_ps(_mm_loadu_ps(&result[i]),
                                       _mm_loadu_ps(&curraray[i])));
            }
            
            for( size_t i = aligendN; i < size; ++i )
              result[i] += curraray[i];
#else
            for( size_t i = 0; i < size; ++i )
              result[i] += curraray[i];
#endif

          }
        } );
      }//for( size_t specn = 0; specn < nspec; specn += npec_per_result )
    }//for( const auto &iter : cal_to_meas )
    
    
    const size_t spec_size = ene_cal->num_channels();
    auto result_vec = make_shared<vector<float>>( spec_size, 0.0f );
    dataH->gamma_counts_ = result_vec;
    
    vector<float> &summed_counts = *result_vec;
    const vector<float> &out_energies = *ene_cal->channel_energies();
    
    assert( (spec_size + 1) == out_energies.size() );
    assert( calibrations.size() == results.size() );
    
    threadpool.join();
    
    // We could do a pass here and do a simple sum for every results/calibrations that have the
    //  same calibrations.., but probably not with the bother for now.
    
    // Multithreading this next loop doesnt seem to help much in practice, so wont bother for now
    assert( results.size() == calibrations.size() );
    for( size_t index = 0; index < results.size(); ++index )
    {
      assert( results[index] );
      assert( calibrations[index] );
      
      const shared_ptr<const EnergyCalibration> &cal = calibrations[index];
      
      const vector<float> &in_counts = *results[index];
      const vector<float> &in_energies = *cal->channel_energies();
      
      vector<float> out_counts( spec_size, 0.0f );
      SpecUtils::rebin_by_lower_edge( in_energies, in_counts, out_energies, out_counts );
      
      assert( summed_counts.size() == out_counts.size() );
      
#if( SpecUtils_USE_SIMD )
      const size_t aligendN = spec_size - (spec_size % 4);
      for( size_t i = 0; i < aligendN; i += 4 )
      {
        __m128 xmmA = _mm_loadu_ps(&summed_counts[i]);
        __m128 xmmB = _mm_loadu_ps(&out_counts[i]);
        __m128 xmmC = _mm_add_ps( xmmA, xmmB );
        _mm_storeu_ps( &summed_counts[i], xmmC );
      }
      
      for( size_t i = aligendN; i < spec_size; ++i )
        summed_counts[i] += out_counts[i];
#else
      for( size_t i = 0; i < spec_size; ++i )
        summed_counts[i] += out_counts[i];
#endif
    }//for( const auto &iter : cal_to_meas )
  
    
#if( PERFORM_DEVELOPER_CHECKS )
    const double finish_new_time = get_wall_time();
    const double finish_new_cpu = get_cpu_time();
    
    double finish_old_time, finish_old_cpu;
    // With limited testing, it doesnt look like we need this...
    {// begin check of other implementation
      vector< vector<float> > check_results( num_thread );
      
      for( size_t i = 0; i < num_thread; ++i )
      {
        vector<float> *dest = &(check_results[i]);
        const vector< std::shared_ptr<const Measurement> > *measvec = &(meas_per_thread[i]);
        threadpool.post( [dest,&dataH,measvec](){ sum_with_rebin( *dest, dataH, *measvec ); } );
      }
      
      threadpool.join();
      
      const size_t spec_size = check_results[0].size();
      vector<float> check_result_vec = check_results[0];
      
      for( size_t i = 1; i < num_thread; ++i )
      {
        const size_t len = check_results[i].size();
        assert( len == check_result_vec.size() );
        const float *spec_array = &(check_results[i][0]);
        for( size_t bin = 0; bin < spec_size && bin < len; ++bin )
          check_result_vec[bin] += spec_array[bin];
      }//for( size_t i = 0; i < num_thread; ++i )
      
      finish_old_time = get_wall_time();
      finish_old_cpu = get_cpu_time();
      
      assert( summed_counts.size() == check_result_vec.size() );
      for( size_t i = 0; i < check_result_vec.size(); ++i )
      {
        const float diff = fabs(summed_counts[i] - check_result_vec[i]);
        
        if( (diff > 1.0E-12) && (diff > (1.0E-5*max(summed_counts[i],check_result_vec[i]))) )
        {
          cerr << "Found diff! diff=" << diff
               << ", summed_counts[" << i << "]=" << summed_counts[i]
               << ", check_result_vec[" << i << "]=" << check_result_vec[i] << endl;
        }
      }//
    }// begin check of other implementation
    
    cerr << "New way: wall=" << (finish_new_time - start_new_time) << " s, cpu=" << (finish_new_cpu - start_new_cpu) << endl;
    cerr << "Old way: wall=" << (finish_old_time - finish_new_time) << " s, cpu=" << (finish_old_cpu - finish_new_cpu) << endl;
#endif //PERFORM_DEVELOPER_CHECKS
    
#else // GROUP_BY_ENE_CAL_BEFORE_SUM
    SpecUtilsAsync::ThreadPool threadpool;
    
    vector< vector<float> > results( num_thread );
    
    for( size_t i = 0; i < num_thread; ++i )
    {
      vector<float> *dest = &(results[i]);
      const vector< std::shared_ptr<const Measurement> > *measvec = &(meas_per_thread[i]);
      threadpool.post( [dest,&dataH,measvec](){ sum_with_rebin( *dest, dataH, *measvec ); } );
    }
    
    threadpool.join();
    
    const size_t spec_size = results[0].size();
    auto result_vec = make_shared<vector<float>>( results[0] );
    dataH->gamma_counts_ = result_vec;
    float *result_vec_raw = &((*result_vec)[0]);
    
    for( size_t i = 1; i < num_thread; ++i )
    {
      const size_t len = results[i].size();
      const float *spec_array = &(results[i][0]);
      for( size_t bin = 0; bin < spec_size && bin < len; ++bin )
      result_vec_raw[bin] += spec_array[bin];
    }//for( size_t i = 0; i < num_thread; ++i )
#endif // GROUP_BY_ENE_CAL_BEFORE_SUM
  }//if( allBinningIsSame ) / else
  
  if( is_special(dataH->start_time_) )
    dataH->start_time_ = time_point_t{};
  
  for( const std::string &remark : remarks )
    dataH->remarks_.push_back( remark );

#if( PERFORM_DEVELOPER_CHECKS )
  const size_t ngammchan = dataH->gamma_counts_ ? dataH->gamma_counts_->size() : size_t(0);
  const size_t nenechan = ene_cal->num_channels();
  if( ngammchan != nenechan )
  {
    string msg = "sum_measurements: final number of gamma channels doesnt match energy calibration"
                " number of channels (" + to_string(ngammchan) + " vs " + to_string(nenechan) + ")";
    log_developer_error( __func__, msg.c_str() );
    assert( 0 );
  }
  
  /*
   // This next test will probably fail often as gamma counts may get truncated
  double pre_sum = 0.0, post_sum = 0.0;
  for( const vector< shared_ptr<const vector<float> > > specs : spectrum_per_thread )
  {
    for( shared_ptr<const vector<float> > s : specs )
    {
      for( const float f : *s )
        pre_sum += f;
    }
  }
  
  assert( dataH->gamma_counts_ );
  for( const float f : dataH->gamma_counts_ )
    post_sum += f;
  
  if( fabs(post_sum - pre_sum) > 0.000001*std::max(pre_sum,post_sum) )
  {
    string msg = "sum_measurements: final number of gamma doesnt match input num gammas: "
                 + to_string(pre_sum) + " (input) vs " + to_string(post_sum) + " (output)";
    log_developer_error( __func__, msg.c_str() );
    assert( 0 );
  }
   */
#endif
  
  return dataH;
}//std::shared_ptr<Measurement> sum_measurements( int &, int &, const SpecMeas & )



set<size_t> SpecFile::gamma_channel_counts() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  set<size_t> answer;

  for( const auto &meas : measurements_ )
  {
    const size_t nchannel = meas->num_gamma_channels();
    if( nchannel )
      answer.insert( meas->num_gamma_channels() );
  }
  
  return answer;
}//std::set<size_t> gamma_channel_counts() const


size_t SpecFile::num_gamma_channels() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  size_t max_num_channel = 0;
  for( const auto &meas : measurements_ )
  {
    const size_t nchannel = meas->num_gamma_channels();
    if( nchannel > 6 )
      return nchannel;
    max_num_channel = std::max( max_num_channel, nchannel );
  }

  return max_num_channel;
}//size_t SpecFile::num_gamma_channels() const


struct KeepNBinSpectraStruct
{
  typedef std::vector< std::shared_ptr<Measurement> >::const_iterator MeasVecIter;

  size_t m_nbin;
  MeasVecIter m_start, m_end;
  std::vector< std::shared_ptr<Measurement> > *m_keepers;

  KeepNBinSpectraStruct( size_t nbin,
                         MeasVecIter start, MeasVecIter end,
                         std::vector< std::shared_ptr<Measurement> > *keepers )
    : m_nbin( nbin ), m_start( start ), m_end( end ), m_keepers( keepers )
  {
  }

  void operator()()
  {
    m_keepers->reserve( m_end - m_start );
    for( MeasVecIter iter = m_start; iter != m_end; ++iter )
    {
      const std::shared_ptr<Measurement> &m = *iter;
      if( !m )
        continue;  //shouldnt ever happen, but JIC
      
      const size_t num_bin = m->gamma_counts() ? m->gamma_counts()->size() : size_t(0);

      //Keep if a neutrons only, or number of bins match wanted
      if( (!num_bin && m->contained_neutron()) || (num_bin==m_nbin) )
        m_keepers->push_back( m );
    }//for( MeasVecIter iter = m_start; iter != m_end; ++iter )
  }//void operator()()
};//struct KeepNBinSpectraStruct


//return number of removed spectra
size_t SpecFile::keep_n_bin_spectra_only( size_t nbin )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  size_t nremoved = 0;
  const size_t nstart = measurements_.size();

  std::vector< std::shared_ptr<Measurement> > newmeass;
  newmeass.reserve( nstart );

  if( nstart < 100 )
  {
    KeepNBinSpectraStruct worker( nbin, measurements_.begin(),
                                  measurements_.end(), &newmeass );
    worker();
    nremoved = nstart - newmeass.size();
  }else
  {
    const int nthread = SpecUtilsAsync::num_logical_cpu_cores();
    const size_t meas_per_thread = nstart / nthread;

    SpecUtilsAsync::ThreadPool threadpool;
//    vector<KeepNBinSpectraStruct> workers;
    const KeepNBinSpectraStruct::MeasVecIter begin = measurements_.begin();

    size_t nsections = nstart / meas_per_thread;
    if( (nstart % meas_per_thread) != 0 )
      nsections += 1;

    vector< vector< std::shared_ptr<Measurement> > > answers( nsections );

    size_t sec_num = 0;
    for( size_t pos = 0; pos < nstart; pos += meas_per_thread )
    {
      KeepNBinSpectraStruct::MeasVecIter start, end;
      start = begin + pos;
      if( (pos + meas_per_thread) <= nstart )
        end = start + meas_per_thread;
      else
        end = begin + nstart;

      threadpool.post( KeepNBinSpectraStruct(nbin, start, end, &(answers[sec_num]) ) );
      ++sec_num;
    }//for( size_t pos = 0; pos < nstart; pos += meas_per_thread )

    if( nsections != sec_num )
      throw runtime_error( SRC_LOCATION + "\n\tSerious logic error here!" );

    threadpool.join();

    for( size_t i = 0; i < nsections; ++i )
    {
      const vector< std::shared_ptr<Measurement> > &thismeas = answers[i];
      newmeass.insert( newmeass.end(), thismeas.begin(), thismeas.end() );
    }//for( size_t i = 0; i < nsections; ++i )
    nremoved = nstart - newmeass.size();
  }//if( nstart < 100 )

  if( nremoved )
  {
    measurements_.swap( newmeass );
    cleanup_after_load();
  }//if( nremoved )

  return nremoved;
}//size_t keep_n_bin_spectra_only( size_t nbin )


bool SpecFile::contained_neutron() const
{
  for( const auto &m : measurements_ )
    if( m && m->contained_neutron() )
      return true;
  
  return false;
}//


size_t SpecFile::remove_neutron_measurements()
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  size_t nremoved = 0;

  for( size_t i = 0; i < measurements_.size();  )
  {
    std::shared_ptr<Measurement> &m = measurements_[i];
    if( m->contained_neutron_
        && (!m->gamma_counts_ || m->gamma_counts_->empty()) )
    {
      ++nremoved;
      measurements_.erase( measurements_.begin()+i );
    }else ++i;
  }//for( size_t i = 0; i < norig; ++i )

  if( nremoved )
  {
    cleanup_after_load();
  }//if( nremoved )

  if( nremoved )
    modified_ = modifiedSinceDecode_ = true;
  
  return nremoved;
}//size_t remove_neutron_measurements();


set<string> SpecFile::energy_cal_variants() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  set<string> answer;
  
  for( const std::string &detnam : detector_names_ )
  {
    const size_t pos = detnam.find( "_intercal_" );
    if( pos != string::npos )
      answer.insert( detnam.substr(pos + 10) );
  }

  return answer;
}//set<string> energy_cal_variants() const


size_t SpecFile::keep_energy_cal_variants( const std::set<std::string> &variants )
{
  std::vector< std::shared_ptr<Measurement> > keepers;
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  const set<string> origvaraints = energy_cal_variants();
  
  for( const string &variant : variants )
  {
    if( !origvaraints.count(variant) )
    {
      string msg = "SpecFile::keep_energy_cal_variants():"
      " measurement did not contain an energy variant named '"
      + variant + "', only contained:";
      for( auto iter = begin(origvaraints); iter != end(origvaraints); ++iter )
        msg += " '" + (*iter) + "',";
      if( origvaraints.empty() )
        msg += " none";
      
      throw runtime_error( msg );
    }//if( !origvaraints.count(variant) )
  }//for( const string &variant : variants )
  
  keepers.reserve( measurements_.size() );
  
  for( auto &ptr : measurements_ )
  {
    const std::string &detname = ptr->detector_name_;
    const size_t pos = detname.find( "_intercal_" );
    if( pos == string::npos )
    {
      keepers.push_back( ptr );
    }else
    {
      for( const string &variant : variants )
      {
        const size_t ending_size = variant.size() + 10; // 10 == ("_intercal_" + variant).size()
        assert( ending_size == ("_intercal_" + variant).size() );
        
        if( ((pos + ending_size) == detname.size())
           && (strcmp(detname.c_str()+pos+10,variant.c_str())==0) )
        {
          ptr->detector_name_ = detname.substr( 0, pos );
          keepers.push_back( ptr );
        }
      }//for( const string &variant : variants )
    }//if( not an energy cal variant ) / else
  }//for( auto &ptr : measurements_ )
  
  measurements_.swap( keepers );
  cleanup_after_load();
  
  modified_ = modifiedSinceDecode_ = true;
  
  return (keepers.size() - measurements_.size());
}//void keep_energy_cal_variants( const std::string variant )


size_t SpecFile::keep_derived_data_variant( const SpecFile::DerivedVariantToKeep tokeep )
{
  std::vector< std::shared_ptr<Measurement> > keepers;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
 
  switch( tokeep )
  {
    case DerivedVariantToKeep::NonDerived:
      if( !(properties_flags_ & MeasurementProperties::kDerivedDataMeasurements) )
        return 0;
      break;
      
    case DerivedVariantToKeep::Derived:
      if( !(properties_flags_ & MeasurementProperties::kNonDerivedDataMeasurements) )
        return 0;
      break;
  }//switch( tokeep )

  keepers.reserve( measurements_.size() );
  
  for( auto &ptr : measurements_ )
  {
    switch( tokeep )
    {
      case DerivedVariantToKeep::NonDerived:
        if( !ptr->derived_data_properties_ )
          keepers.push_back( ptr );
        break;
        
      case DerivedVariantToKeep::Derived:
        if( ptr->derived_data_properties_ )
          keepers.push_back( ptr );
        break;
    }//switch( tokeep )
  }//for( auto &ptr : measurements_ )
  
  measurements_.swap( keepers );
  uuid_.clear();
  cleanup_after_load();
  
  modified_ = modifiedSinceDecode_ = true;
  
  return (keepers.size() - measurements_.size());
}//size_t SpecFile::keep_derived_data_variant( const DerivedVariantToKeep tokeep )

  
size_t SpecFile::remove_detectors_data( const set<string> &dets_to_remove )
{
  if( dets_to_remove.empty() )
    return 0;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
 
  for( const string &det : dets_to_remove )
  {
    if( std::find(begin(detector_names_), end(detector_names_), det) == end(detector_names_) )
      throw runtime_error( "SpecFile::remove_detectors_data: invalid detector name '" + det + "'" );
  }//for( const string &det : detectors )
  
  const size_t norig = measurements_.size();
  
  measurements_.erase( std::remove_if( begin(measurements_), end(measurements_),
    [&dets_to_remove]( const shared_ptr<Measurement> &m ) -> bool {
      return (dets_to_remove.count(m->detector_name_) > 0);
    } ), end(measurements_) );
  
  cleanup_after_load();
  
  modified_ = modifiedSinceDecode_ = true;
  
  return (norig - measurements_.size());
}//size_t remove_detectors_data( const std::set<std::string> detectors )

  
int SpecFile::background_sample_number() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  //XXX - maybe could be sped up by using sample_numbers()
  //      and/or sample_measurements(..)
  for( const auto &meas : measurements_ )
    if( meas->source_type_ == SourceType::Background )
      return meas->sample_number_;

  return numeric_limits<int>::min();
}//int background_sample_number() const


void SpecFile::reset()
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  gamma_live_time_ = 0.0f;
  gamma_real_time_ = 0.0f;
  gamma_count_sum_ = 0.0;
  neutron_counts_sum_ = 0.0;
  mean_latitude_ = mean_longitude_ = -999.9;
  properties_flags_ = 0x0;
  filename_ =  "";
  detector_names_.clear();
  neutron_detector_names_.clear();
  gamma_detector_names_.clear();
  uuid_.clear();
  remarks_.clear();
  parse_warnings_.clear();
  lane_number_ = -1;
  measurement_location_name_.clear();
  inspection_.clear();
  measurement_operator_.clear();
  sample_numbers_.clear();
  sample_to_measurements_.clear();
  detector_type_ = DetectorType::Unknown;
  instrument_type_.clear();
  manufacturer_.clear();
  instrument_model_.clear();
  instrument_id_.clear();
  measurements_.clear();
  detector_numbers_.clear();
  modified_ = modifiedSinceDecode_ = false;
  component_versions_.clear();
  detectors_analysis_.reset();
}//void SpecFile::reset()

  
DetectorAnalysisResult::DetectorAnalysisResult()
{
  reset();
}


void DetectorAnalysisResult::reset()
{
  remark_.clear();
  nuclide_.clear();
  activity_ = -1.0;            //in units of becquerel (eg: 1.0 == 1 bq)
  nuclide_type_.clear();
  id_confidence_.clear();
  distance_ = -1.0f;
  dose_rate_ = -1.0f;
  real_time_ = -1.0f;           //in units of secons (eg: 1.0 = 1 s)
  //start_time_ = time_point_t{};
  detector_.clear();
}//void DetectorAnalysisResult::reset()

bool DetectorAnalysisResult::isEmpty() const
{
  return remark_.empty() && nuclide_.empty() && nuclide_type_.empty()
         && id_confidence_.empty() && dose_rate_ <= 0.0f
         && activity_ <= 0.0f && distance_ <= 0.0f;
}//bool isEmpty() const


DetectorAnalysis::DetectorAnalysis()
{
  reset();
}

void DetectorAnalysis::reset()
{
  remarks_.clear();
  algorithm_name_.clear();
  algorithm_component_versions_.clear();
  algorithm_creator_.clear();
  algorithm_description_.clear();
  analysis_start_time_ = time_point_t{};
  analysis_computation_duration_ = 0.0f;
  algorithm_result_description_.clear();
  
  results_.clear();
}//void DetectorAnalysis::reset()


bool DetectorAnalysis::is_empty() const
{
  return (remarks_.empty()
   && algorithm_name_.empty()
   && algorithm_component_versions_.empty()
   && algorithm_creator_.empty()
   && algorithm_description_.empty()
   //&& is_special(analysis_start_time_)
   //&& analysis_computation_duration_ < FLT_EPSILON
   && algorithm_result_description_.empty()
   && results_.empty());
}


void SpecFile::write_to_file( const std::string filename,
                                     const SaveSpectrumAsType format ) const
{
  set<int> samples, detectors;
  
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    samples = sample_numbers_;
    detectors = set<int>( detector_numbers_.begin(), detector_numbers_.end() );
  }
  
  write_to_file( filename, samples, detectors, format );
}//void write_to_file(...)



void SpecFile::write_to_file( const std::string name,
                   const std::set<int> sample_nums,
                   const std::set<int> det_nums,
                   const SaveSpectrumAsType format ) const
{
  if( SpecUtils::is_file(name) || SpecUtils::is_directory(name) )
    throw runtime_error( "File (" + name + ") already exists, not overwriting" );
  
#ifdef _WIN32
  std::ofstream output( convert_from_utf8_to_utf16(name).c_str(), ios::out | ios::binary );
#else
  std::ofstream output( name.c_str(), ios::out | ios::binary );
#endif

  if( !output )
    throw runtime_error( "Failed to open file (" + name + ") for writing" );
  
  write( output, sample_nums, det_nums, format );
}//void write_to_file(...)


void SpecFile::write_to_file( const std::string name,
                   const std::vector<int> sample_nums_vector,
                   const std::vector<int> det_nums_vector,
                   const SaveSpectrumAsType format ) const
{
  //copy vectors into sets
  const std::set<int> sample_nums_set( sample_nums_vector.begin(),
                                        sample_nums_vector.end() );
  const std::set<int> det_nums_set( det_nums_vector.begin(),
                                     det_nums_vector.end() );
  //write the file
  write_to_file( name, sample_nums_set, det_nums_set, format);
}//write_to_file(...)


std::set<int> SpecFile::detector_names_to_numbers( const std::vector<std::string> &det_names ) const
{
  set<int> det_nums_set;
  
  {//begin lock on mutex_
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    for( const std::string &name : det_names )
    {
      const auto pos = std::find( begin(detector_names_), end(detector_names_), name );
      if( pos == end(detector_names_) )
        throw runtime_error( "Invalid detector name ('" + name + "') in the input" );
      
      const size_t index = pos - detector_names_.begin();
      det_nums_set.insert( detector_numbers_[index] );
    }//for( const int num : det_nums )
  }//end lock on mutex_
  
  return det_nums_set;
}//detector_names_to_numbers(...)

void SpecFile::write_to_file( const std::string &filename,
                   const std::set<int> &sample_nums,
                   const std::vector<std::string> &det_names,
                   const SaveSpectrumAsType format ) const
{
  const set<int> det_nums_set = detector_names_to_numbers( det_names );
  
  write_to_file( filename, sample_nums, det_nums_set, format);
}//void write_to_file(...)


void SpecFile::write( std::ostream &strm,
           std::set<int> sample_nums,
           const std::set<int> det_nums,
           const SaveSpectrumAsType format ) const
{ 
  //Its a little heavy handed to lock mutex_ for this entire function, but
  //  techncically necassry since operator= operations are a shallow copy
  //  of the Measurements, meaning there could be an issue if the user modifies
  //  a Measurement in another thread.
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( sample_nums.empty() )
    throw runtime_error( "No sample numbers were specified to write out" );
  
  if( det_nums.empty() )
    throw runtime_error( "No detector numbers were specified to write out" );
  
  for( const int sample : sample_nums )
  {
    if( !sample_numbers_.count(sample ) )
      throw runtime_error( "Specified invalid sample number to write out" );
  }
  
  vector<string> det_names;
  for( const int detnum : det_nums )
  {
    const auto pos = std::find( begin(detector_numbers_), end(detector_numbers_), detnum );
    if( pos == end(detector_numbers_) )
      throw runtime_error( "Specified invalid detector number to write out" );
    
    det_names.push_back( detector_names_[pos - begin(detector_numbers_)] );
  }
  
  SpecFile info = *this;
  
  if( (sample_nums != sample_numbers_)
      || (det_nums.size() != detector_numbers_.size()) )
  {
    vector< std::shared_ptr<const Measurement> > toremove;
    for( std::shared_ptr<const Measurement> oldm : info.measurements() )
    {
      if( !sample_nums.count(oldm->sample_number())
         || !det_nums.count(oldm->detector_number()) )
      {
        toremove.push_back( oldm );
      }
    }//for( oldm : info.measurements() )
    
    info.remove_measurements( toremove );
  }//if( we dont want all the measurements )
  
  if( info.measurements_.empty() )
    throw runtime_error( "No Measurements to write out" );
  
  const std::set<int> &samples = info.sample_numbers_;
  const set<int> detectors( info.detector_numbers_.begin(), info.detector_numbers_.end() );
  
  bool success = false;
  switch( format )
  {
    case SaveSpectrumAsType::Txt:
      success = info.write_txt( strm );
      break;
      
    case SaveSpectrumAsType::Csv:
      success = info.write_csv( strm );
      break;
      
    case SaveSpectrumAsType::Pcf:
      success = info.write_pcf( strm );
      break;
      
    case SaveSpectrumAsType::N42_2006:
      success = info.write_2006_N42( strm );
      break;
      
    case SaveSpectrumAsType::N42_2012:
      success = info.write_2012_N42( strm );
      break;
      
    case SaveSpectrumAsType::Chn:
      success = info.write_integer_chn( strm, samples, detectors );
      break;
      
    case SaveSpectrumAsType::SpcBinaryInt:
      success = info.write_binary_spc( strm, IntegerSpcType, samples, detectors );
      break;
      
    case SaveSpectrumAsType::SpcBinaryFloat:
      success = info.write_binary_spc( strm, FloatSpcType, samples, detectors );
      break;
      
    case SaveSpectrumAsType::SpcAscii:
      success = info.write_ascii_spc( strm, samples, detectors );
      break;
      
    case SaveSpectrumAsType::ExploraniumGr130v0:
      success = info.write_binary_exploranium_gr130v0( strm );
      break;
            
    case SaveSpectrumAsType::ExploraniumGr135v2:
      success = info.write_binary_exploranium_gr135v2( strm );
      break;
      
    case SaveSpectrumAsType::SpeIaea:
      success = info.write_iaea_spe( strm, samples, detectors );
      break;

    case SaveSpectrumAsType::Cnf:
      success = info.write_cnf( strm, samples, detectors );
      break;
      
    case SaveSpectrumAsType::Tka:
      success = info.write_tka( strm, samples, detectors );
      break;
      
#if( SpecUtils_ENABLE_D3_CHART )
    case SaveSpectrumAsType::HtmlD3:
    {
      D3SpectrumExport::D3SpectrumChartOptions options;
      success = info.write_d3_html( strm, options, samples, info.detector_names_ );
      break;
    }
#endif
      
#if( SpecUtils_INJA_TEMPLATES )
    case SaveSpectrumAsType::Template:  
    {
      // BDE_TODO: figure out what is going on here, to write templates we would need to 
      // get the template file argument into here somehow.
      throw runtime_error("Templates not supported from this method");
      break;
    }
#endif
      
#if( SpecUtils_ENABLE_URI_SPECTRA )
    case SaveSpectrumAsType::Uri:
    {
      const size_t num_uri = 1;
      success = info.write_uri( strm, num_uri, SpecUtils::EncodeOptions::UseUrlSafeBase64 );
      break;
    }
#endif
      
    case SaveSpectrumAsType::NumTypes:
      throw runtime_error( "Invalid output format specified" );
      break;
  }//switch( format )
  
  if( !success )
    throw runtime_error( "Failed to write to output" );
}//write_to_file(...)


void SpecFile::write( std::ostream &strm,
           std::set<int> sample_nums,
           const std::vector<std::string> &det_names,
           const SaveSpectrumAsType format ) const
{
  const set<int> det_nums_set = detector_names_to_numbers( det_names );
  
  write( strm, sample_nums, det_nums_set, format );
}//write(...)

}//namespace SpecUtils

