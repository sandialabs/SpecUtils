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

#include <ctime>
#include <cmath>
#include <vector>
#include <memory>
#include <string>
#include <cctype>
#include <locale>
#include <limits>
#include <numeric>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <algorithm>
#include <functional>
//#define __STDC_FORMAT_MACROS
//#include <inttypes.h>
#include <sys/stat.h>

#include <boost/functional/hash.hpp>

#if( BUILD_AS_UNIT_TEST_SUITE )
#include <boost/test/unit_test.hpp>
#endif

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_utils.hpp"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/Filesystem.h"
#include "SpecUtils/SpecUtilsAsync.h"
#include "SpecUtils/RapidXmlUtils.hpp"
#include "SpecUtils/EnergyCalibration.h"
#include "SpecUtils/SerialToDetectorModel.h"

#if( SpecUtils_ENABLE_D3_CHART )
#include "SpecUtils/D3SpectrumExport.h"
#endif


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

//If the SpecFile and Measurement equalEnough functions should require remarks
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
  
  bool toFloat( const std::string &str, float &f )
  {
    //ToDO: should probably use SpecUtils::parse_float(...) for consistency/speed
    const int nconvert = sscanf( str.c_str(), "%f", &f );
    return (nconvert == 1);
  }
  
  bool xml_value_to_flt( const rapidxml::xml_base<char> *node, float &val )
  {
    val = 0.0f;
    if( !node )
      return false;
    return SpecUtils::parse_float( node->value(), node->value_size(), val );
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
      
      for( size_t i = 0; i < size; ++i )
        results[i] += curraray[i];
    }
  }//add_to(...)
  
  
  void sum_with_rebin( vector<float> &results,
                      const std::shared_ptr<const SpecUtils::Measurement> &binning,
                      const vector<std::shared_ptr<const SpecUtils::Measurement> > &datas )
  {
    assert( !!binning );
    
    const std::shared_ptr<const std::vector<float>> &wantedenergies = binning->channel_energies();
    
    const size_t nbin = wantedenergies->size();
    if( results.size() < nbin )
      results.resize( nbin, 0.0f );
    
    assert( !!wantedenergies );
    
    for( size_t i = 0; i < datas.size(); ++i )
    {
      const std::shared_ptr<const SpecUtils::Measurement> &d = datas[i];
      const std::shared_ptr<const std::vector<float>> &dataenergies = d->channel_energies();
      const std::shared_ptr<const std::vector<float>> &channel_counts = d->gamma_counts();
      
      if( !dataenergies || !channel_counts )
      {
        cerr << "sum_with_rebin(...): found spectrum with no bin" << endl;
        continue;
      }//if( !dataenergies )
      
      if( dataenergies == wantedenergies )
      {
        for( size_t j = 0; j < nbin; ++j )
          results[j] += (*channel_counts)[j];
      }else if( channel_counts->size() > 3 )
      {
        vector<float> resulting_counts;
        SpecUtils::rebin_by_lower_edge( *dataenergies, *channel_counts,
                            *wantedenergies, resulting_counts );
        
        assert( resulting_counts.size() == nbin );
        
        for( size_t j = 0; j < nbin; ++j )
          results[j] += resulting_counts[j];
      }//if( dataenergies == wantedenergies )
      
    }//for( size_t i = 0; i < datas.size(); ++i )
  }//void sum_with_rebin(...)

  
  
  
  

  
  
  
  boost::posix_time::ptime datetime_ole_to_posix(double ole_dt)
  {
    static const boost::gregorian::date ole_zero(1899,12,30);
    
    boost::gregorian::days d( static_cast<long>(ole_dt) );
    boost::posix_time::ptime pt(ole_zero + d);
    
    ole_dt -= d.days();
    ole_dt *= 24 * 60 * 60 * 1000;
    
    return pt + boost::posix_time::milliseconds( std::abs( static_cast<int64_t>(ole_dt) ) );
    
    /*
     typedef typename time_type::date_type date_type;
     typedef typename time_type::date_duration_type date_duration_type;
     typedef typename time_type::time_duration_type time_duration_type;
     using boost::math::modf;
     static const date_type base_date(1899, Dec, 30);
     static const time_type base_time(base_date, time_duration_type(0,0,0));
     int dayOffset, hourOffset, minuteOffset, secondOffset;
     double fraction = fabs(modf(oa_date, &dayOffset)) * 24; // fraction = hours
     fraction = modf(fraction, &hourOffset) * 60; // fraction = minutes
     fraction = modf(fraction, &minuteOffset) * 60; // fraction = seconds
     modf(fraction, &secondOffset);
     time_type t(base_time);
     t += time_duration_type(hourOffset, minuteOffset, secondOffset);
     t += date_duration_type(dayOffset);
     return t;
     */
  }
  
  
 
  
  
}//anaomous namespace






#if(PERFORM_DEVELOPER_CHECKS)
void log_developer_error( const char *location, const char *error )
{
  static std::recursive_mutex s_dev_error_log_mutex;
  static ofstream s_dev_error_log( "developer_errors.log", ios::app | ios::out );
  
  std::unique_lock<std::recursive_mutex> loc( s_dev_error_log_mutex );
  
  boost::posix_time::ptime time = boost::posix_time::second_clock::local_time();
  
  const string timestr = boost::posix_time::to_iso_extended_string( time );
//  const string timestr = SpecUtils::to_iso_string( time );
  
  s_dev_error_log << timestr << ": " << location << endl << error << "\n\n" << endl;
  cerr << timestr << ": " << location << endl << error << "\n\n" << endl;
}//void log_developer_error( const char *location, const char *error )
#endif //#if(PERFORM_DEVELOPER_CHECKS)

namespace SpecUtils
{
  
//Analogous to Measurement::compare_by_sample_det_time; compares by
// sample_number, and then detector_number_, but NOT by start_time_
struct SpecFileLessThan
{
  const int sample_number, detector_number;
  
  SpecFileLessThan( int sample_num, int det_num )
  : sample_number( sample_num ), detector_number( det_num ) {}
  bool operator()( const std::shared_ptr<Measurement>& lhs, const std::shared_ptr<Measurement> &dummy )
  {
    assert( !dummy );
    if( !lhs )
      return false;
    
    if( lhs->sample_number() == sample_number )
      return (lhs->detector_number() < detector_number);
    return (lhs->sample_number() < sample_number);
  }//operator()
};//struct SpecFileLessThan


  
MeasurementCalibInfo::MeasurementCalibInfo( std::shared_ptr<Measurement> meas )
{
  equation_type = meas->energy_calibration_model();
  nbin = meas->gamma_counts()->size();
  coefficients = meas->calibration_coeffs();
  
  deviation_pairs_ = meas->deviation_pairs();
  original_binning = meas->channel_energies();
  if( meas->channel_energies() && !meas->channel_energies()->empty())
    binning = meas->channel_energies();
  
  if( equation_type == SpecUtils::EnergyCalType::InvalidEquationType
     && !coefficients.empty() )
  {
#if( PERFORM_DEVELOPER_CHECKS )
    log_developer_error( __func__, "Found case where equation_type!=Invalid, but there are coefficients - shouldnt happen!" );
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
    coefficients.clear();
    binning.reset();
  }//
}//MeasurementCalibInfo constructor


MeasurementCalibInfo::MeasurementCalibInfo()
{
  nbin = 0;
  equation_type = SpecUtils::EnergyCalType::InvalidEquationType;
}//MeasurementCalibInfo constructor

void MeasurementCalibInfo::strip_end_zero_coeff()
{
  for( int i = static_cast<int>(coefficients.size()) - 1;
      (i>=0) && (fabs(coefficients[i]) < 0.00000000001); --i )
    coefficients.pop_back();
}//void strip_end_zero_coeff()


void MeasurementCalibInfo::fill_binning()
{
  if( binning && !binning->empty())
    return;
  
  try
  {
    switch( equation_type )
    {
      case SpecUtils::EnergyCalType::Polynomial:
      case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
        binning = SpecUtils::polynomial_binning( coefficients, nbin, deviation_pairs_ );
        break;
        
      case SpecUtils::EnergyCalType::FullRangeFraction:
        binning = SpecUtils::fullrangefraction_binning( coefficients, nbin, deviation_pairs_ );
        break;
        
      case SpecUtils::EnergyCalType::LowerChannelEdge:
      {
        std::shared_ptr< vector<float> > energies
        = std::make_shared< vector<float> >();
        
        *energies  = coefficients;
        
        if( (nbin > 2) && (energies->size() == nbin) )
        {
          energies->push_back(2.0f*((*energies)[nbin-1]) - (*energies)[nbin-2]);
        }else if( energies->size() < nbin )
        {
          //Deal with error
          cerr << "fill_binning(): warning, not enough cahnnel energies to be"
          " valid; expected at least " << nbin << ", but only got "
          << energies->size() << endl;
          energies.reset();
        }else if( energies->size() > (nbin+1) )
        {
          //Make it so
          cerr << "fill_binning(): warning, removing channel energy values!" << endl;
          energies->resize( nbin + 1 );
        }
        
        
        binning = energies;
        
        break;
      }//case SpecUtils::EnergyCalType::LowerChannelEdge:
        
      case SpecUtils::EnergyCalType::InvalidEquationType:
        break;
    }//switch( meas->energy_calibration_model_ )
  }catch( std::exception &e )
  {
    cerr << "An invalid binning was specified, goign to default binning" << endl;
    if( nbin > 0 )
    {
      equation_type = SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial;
      deviation_pairs_.clear();
      coefficients.resize(2);
      coefficients[0] = 0.0f;
      coefficients[1] = 3000.0f / std::max(nbin-1, size_t(1));
    }
  }//try / catch
  
  if( !binning )
    cerr << SRC_LOCATION << "\n\tWarning, failed to set binning" << endl;
}//void fill_binning()

  
bool MeasurementCalibInfo::operator<( const MeasurementCalibInfo &rhs ) const
{
  if( nbin != rhs.nbin )
    return (nbin < rhs.nbin);
  
  if( equation_type != rhs.equation_type )
    return (equation_type < rhs.equation_type);
  
  if( coefficients.size() != rhs.coefficients.size() )
    return (coefficients.size() < rhs.coefficients.size());
  
  
  for( size_t i = 0; i < coefficients.size(); ++i )
  {
    const float leftcoef = coefficients[i];
    const float rightcoef = rhs.coefficients[i];
    const float maxcoef = std::max( fabs(leftcoef), fabs(rightcoef) );
    if( fabs(leftcoef - rightcoef) > (1.0E-5 * maxcoef) )
      return coefficients[i] < rhs.coefficients[i];
  }//for( size_t i = 0; i < coefficients.size(); ++i )
  
  if( deviation_pairs_.size() != rhs.deviation_pairs_.size() )
    return (deviation_pairs_.size() < rhs.deviation_pairs_.size());
  
  for( size_t i = 0; i < deviation_pairs_.size(); ++i )
  {
    const pair<float,float> &l = deviation_pairs_[i];
    const pair<float,float> &r = rhs.deviation_pairs_[i];
    const float maxenergy = std::max( fabs(l.first), fabs(r.first) );
    
    if( fabs(l.first - r.first) > (1.0E-5 * maxenergy) )
      return l.first < r.first;
    
    const float maxdeviation = std::max( fabs(l.second), fabs(r.second) );
    
    if( fabs(l.second - r.second) > (1.0E-5 * maxdeviation) )
      return l.second < r.second;
  }//for( size_t i = 0; i < deviation_pairs_.size(); ++i )
  
  
  if( original_binning && !original_binning->empty() )
  {
    if( !rhs.original_binning || rhs.original_binning->empty() )
      return false;
    
    //a full lexicographical_compare is too much if the binnings are same, but
    //  different objects, so well just look at the objects address for all
    //  cases
    return (original_binning.get() < rhs.original_binning.get());
    
    //      if( binning == rhs.binning )
    //        return false;
    //      return lexicographical_compare( binning->begin(), binning->end(),
    //                                      rhs.binning->begin(), rhs.binning->end(), std::less<float>() );
  }else if( rhs.original_binning && !rhs.original_binning->empty() )
    return true;
  
  return false;
}//bool operatr<(...)


bool MeasurementCalibInfo::operator==( const MeasurementCalibInfo &rhs ) const
{
  const bool rhsLt = operator<(rhs);
  const bool lhsLt = rhs.operator<(*this);
  return !lhsLt && !rhsLt;
}

  


double gamma_integral( const std::shared_ptr<const Measurement> &hist,
                 const float minEnergy, const float maxEnergy )
{
  if( !hist )
    return 0.0;
  
  const double gamma_sum = hist->gamma_integral( minEnergy, maxEnergy );

#if( PERFORM_DEVELOPER_CHECKS )
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
#endif  //#if( PERFORM_DEVELOPER_CHECKS )

  return gamma_sum;
}//float integral(...)



double Measurement::gamma_integral( float lowerx, float upperx ) const
{
  double sum = 0.0;
  
  if( !channel_energies_ || !gamma_counts_ || channel_energies_->size() < 2
      || gamma_counts_->size() < 2 )
    return sum;

  const vector<float> &x = *channel_energies_;
  const vector<float> &y = *gamma_counts_;
  
  const size_t nchannel = x.size();
  const float maxX = 2.0f*x[nchannel-1] - x[nchannel-2];
  
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
  
  if( startbin >= nchannels )
    return sum;
  
  endbin = std::min( endbin, nchannels-1 );
  
  if( startbin > endbin )
    std::swap( startbin, endbin );
  
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
    case SaveSpectrumAsType::N42_2006:                return "n42";
    case SaveSpectrumAsType::N42_2012:            return "n42";
    case SaveSpectrumAsType::Chn:                return "chn";
    case SaveSpectrumAsType::SpcBinaryInt:       return "spc";
    case SaveSpectrumAsType::SpcBinaryFloat:     return "spc";
    case SaveSpectrumAsType::SpcAscii:           return "spc";
    case SaveSpectrumAsType::ExploraniumGr130v0: return "dat";
    case SaveSpectrumAsType::ExploraniumGr135v2: return "dat";
    case SaveSpectrumAsType::SpeIaea:            return "spe";
#if( SpecUtils_ENABLE_D3_CHART )
    case SaveSpectrumAsType::HtmlD3:             return "html";
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
    case SaveSpectrumAsType::N42_2006:                return "2006 N42";
    case SaveSpectrumAsType::N42_2012:            return "2012 N42";
    case SaveSpectrumAsType::Chn:                return "CHN";
    case SaveSpectrumAsType::SpcBinaryInt:       return "Integer SPC";
    case SaveSpectrumAsType::SpcBinaryFloat:     return "Float SPC";
    case SaveSpectrumAsType::SpcAscii:           return "ASCII SPC";
    case SaveSpectrumAsType::ExploraniumGr130v0: return "GR130 DAT";
    case SaveSpectrumAsType::ExploraniumGr135v2: return "GR135v2 DAT";
    case SaveSpectrumAsType::SpeIaea:            return "IAEA SPE";
#if( SpecUtils_ENABLE_D3_CHART )
    case SaveSpectrumAsType::HtmlD3:             return "HTML";
#endif
    case SaveSpectrumAsType::NumTypes:          return "";
  }
  return "";
}//const char *descriptionText( const SaveSpectrumAsType type )


int sample_num_from_remark( std::string remark )
{
  SpecUtils::to_lower_ascii(remark);
  size_t pos = remark.find( "survey" );

  if( pos == string::npos )
    pos = remark.find( "sample" );
  
  if( pos == string::npos )
  {
//    cerr << "Remark '" << remark << "'' didnt contain a sample num" << endl;
    return -1;
  }

  pos = remark.find_first_not_of( " \t=", pos+6 );
  if( pos == string::npos )
  {
    cerr << "Remark '" << remark << "'' didnt have a integer sample num" << endl;
    return -1;
  }

  int num = -1;
  if( !(stringstream(remark.c_str()+pos) >> num) )
  {
     cerr << "sample_num_from_remark(...): Error converting '"
          << remark.c_str()+pos << "' to int" << endl;
    return -1;
  }//if( cant convert result to int )

  return num;
}//int sample_num_from_remark( const std::string &remark )

//Takes a line like "Speed = 5 mph" and returns the speed in m/s.
//  Returns 0 upon failure.
float speed_from_remark( std::string remark )
{
  to_lower_ascii( remark );
  size_t pos = remark.find( "speed" );

  if( pos == string::npos )
    return 0.0;

  pos = remark.find_first_not_of( "= \t", pos+5 );
  if( pos == string::npos )
    return 0.0;

  const string speedstr = remark.substr( pos );

  float speed = 0.0;
  if( !toFloat( speedstr, speed) )
  {
    cerr << "speed_from_remark(...): couldn conver to number: '"
         << speedstr << "'" << endl;
    return 0.0;
  }//if( !(stringstream(speedstr) >> speed) )


  for( size_t i = 0; i < speedstr.size(); ++i )
  {
    if( (!isdigit(speedstr[i])) && (speedstr[i]!=' ') && (speedstr[i]!='\t') )
    {
      float convertion = 0.0f;

      const string unitstr = speedstr.substr( i );
      const size_t unitstrlen = unitstr.size();

      if( unitstrlen>=3 && unitstr.substr(0,3) == "m/s" )
        convertion = 1.0f;
      else if( unitstrlen>=3 && unitstr.substr(0,3) == "mph" )
        convertion = 0.44704f;
      else
        cerr << "speed_from_remark(...): Unknown speed unit: '"
             << unitstrlen << "'" << endl;

      return convertion*speed;
    }//if( we found the start of the units )
  }//for( size_t i = 0; i < speedstr.size(); ++i )

  return 0.0;
}//float speed_from_remark( const std::string &remark )


//Looks for GADRAS style detector names in remarks, or something from the N42
//  conventions of 'Aa1', 'Aa2', etc.
//  Returns empty string on failure.
std::string detector_name_from_remark( const std::string &remark )
{
  //Check for the Gadras convention similar to "Det=Aa1"
  if( SpecUtils::icontains(remark, "det") )
  {
    //Could use a regex here... maybe someday I'll upgrade
    string remarkcopy = remark;
    
    string remarkcopylowercase = remarkcopy;
    SpecUtils::to_lower_ascii( remarkcopylowercase );
    
    size_t pos = remarkcopylowercase.find( "det" );
    if( pos != string::npos )
    {
      remarkcopy = remarkcopy.substr(pos);
      pos = remarkcopy.find_first_of( "= " );
      if( pos != string::npos )
      {
        string det_identifier = remarkcopy.substr(0,pos);
        SpecUtils::to_lower_ascii( det_identifier );
        SpecUtils::trim( det_identifier ); //I dont htink this is necassarry
        if( det_identifier=="det" || det_identifier=="detector"
           || (SpecUtils::levenshtein_distance(det_identifier,"detector") < 3) ) //Allow two typos of "detector"; arbitrary
        {
          remarkcopy = remarkcopy.substr(pos);  //get rid of the "det="
          while( remarkcopy.length() && (remarkcopy[0]==' ' || remarkcopy[0]=='=') )
            remarkcopy = remarkcopy.substr(1);
          pos = remarkcopy.find_first_of( ' ' );
          return remarkcopy.substr(0,pos);
        }
      }
    }
    
  }//if( SpecUtils::icontains(remark, "det") )
  
  
  vector<string> split_contents;
  split( split_contents, remark, ", \t\r\n" );

  for( const string &field : split_contents )
  {
    if( (field.length() < 3) ||  !isdigit(field[field.size()-1])
        || (field.length() > 4) ||  (field[1] != 'a') )
      continue;

    if( field[0]!='A' && field[0]!='B' && field[0]!='C' && field[0]!='D' )
      continue;

    return field;
  }//for( size_t i = 0; i < split_contents.size(); ++i )

  return "";
}//std::string detector_name_from_remark( const std::string &remark )



float dose_units_usvPerH( const char *str, const size_t str_length )
{
  if( !str )
    return 0.0f;
  
  if( icontains( str, str_length, "uSv", 3 )
     || icontains( str, str_length, "\xc2\xb5Sv", 4) )
    return 1.0f;
  
  //One sievert equals 100 rem.
  if( icontains( str, str_length, "&#xB5;Rem/h", 11 ) ) //micro
    return 0.01f;

  return 0.0f;
}//float dose_units_usvPerH( const char *str )




bool is_candidate_n42_file( const char * data )
{
  if( !data )
    return false;
  
  //If smaller than 512 bytes, or doesnt contain a magic_strs string, bail
  const char *magic_strs[] = { "N42", "RadInstrumentData", "Measurement",
    "N42InstrumentData", "ICD1", "HPRDS",
  };
  
  size_t nlength = 0;
  while( nlength < 512 && data[nlength] )
    ++nlength;

  if( nlength < 512 )
    return false;
  
  const string filebegining( data, data+nlength );
  
  for( const char *substr : magic_strs )
  {
    if( SpecUtils::icontains( filebegining, substr ) )
      return true;
  }
  
  return false;
}//bool is_candidate_n42_file( const char * data )


char *convert_n42_utf16_xml_to_utf8( char * data, char * const data_end )
{
  if( !data || data_end <= data )
    return data_end;
  
  const size_t datalen = data_end - data;
  if( datalen < 512 )
    return data_end;
  
  //Look to see how often we alternate between a zero-byte, and non-zero byte
  size_t num_zero_alternations = 0;
  
  //First do a quick check of just the first 64 bytes, being a little loose.
  // (note, we could increment i by two each time, but I was too lazy to test
  //  for now)
  for( size_t i = 1; i < 64; ++i )
    num_zero_alternations += (((!data[i-1]) == (!data[i])) ? 0 : 1);
  
  //For nearly all N42 files num_zero_alternations will still be zero, so we
  //  can return here
  if( num_zero_alternations < 32 )
    return data_end;
  
  //This might be UTF16, lets continue looking at the first 512 bytes.
  for( size_t i = 64; i < 512; ++i )
    num_zero_alternations += (((!data[i-1]) == (!data[i])) ? 0 : 1);
  
  //Arbitrarily allow 16 non-ascii characters in the first 256 characters
  if( num_zero_alternations < 480 )
    return data_end;
  
  //Check that the '<' symbol is in the first ~128 bytes, and skip to it.
  //  The one file I've seen that was UTF16 (but claimed to be UTF8) had the
  //  '<' character at byte three
  char *new_data_start = data;
  while( ((*new_data_start) != '<') && (new_data_start != (data+128)) )
    ++new_data_start;
  
  if( (*new_data_start) != '<' )
    return data_end;
  
  //This is horrible and totally incorrect, but seems to work well enough for
  //  the files I've seen this in... sorry you have to see this, but since
  //  N42 is probably all ASCII, just remove all the zero bytes
  char *new_data_end = data;
  for( char *iter = new_data_start; iter != data_end; ++iter )
  {
    if( *iter )
    {
      *new_data_end = *iter;
      ++new_data_end;
    }
  }
  memset(new_data_end, 0, (data_end - new_data_end) );
  
  return new_data_end;
}//convert_n42_utf16_xml_to_utf8


bool is_candidate_n42_file( const char * const data, const char * const data_end )
{
  if( !data || data_end <= data )
    return false;
 
  const size_t datalen = data_end - data;
  
  if( datalen < 512 )
    return false;
  
  //If smaller than 512 bytes, or doesnt contain a magic_strs string, bail
  const char *magic_strs[] = { "N42", "RadInstrumentData", "Measurement",
    "N42InstrumentData", "ICD1", "HPRDS",
  };
  
  //Check how many non-null bytes there are
  size_t nlength = 0;
  for( size_t i = 0; i < 512; ++i )
    nlength += (data[i] ? 1 : 0);
  
  //Allow for max of 8 zero bytes...
  
  if( nlength+8 < 512 )
    return false;
  
  const string filebegining( data, data+512 );
  
  for( const char *substr : magic_strs )
  {
    if( SpecUtils::icontains( filebegining, substr ) )
      return true;
  }
  
  return false;
}//bool is_candidate_n42_file( const char * data, const char * const data_end )





const std::string &detectorTypeToString( const DetectorType type )
{
  static const string sm_GR135DetectorStr             = "GR135";
  static const string sm_IdentiFinderDetectorStr      = "IdentiFINDER";
  static const string sm_IdentiFinderNGDetectorStr    = "IdentiFINDER-NG";
  static const string sm_IdentiFinderLaBr3DetectorStr = "IdentiFINDER-LaBr3";
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
  static const string sm_Sam940DetectorStr            = "SAM940";
  static const string sm_Sam940Labr3DetectorStr       = "SAM940LaBr3";
  static const string sm_Sam945DetectorStr            = "SAM945";
  static const string sm_Srpm210DetectorStr           = "SRPM-210";
  static const string sm_Rsi701DetectorStr            = "RS-701";
  static const string sm_Rsi705DetectorStr            = "RS-705";
  static const string sm_RadHunterNaIDetectorStr      = "RadHunterNaI";
  static const string sm_RadHunterLaBr3DetectorStr    = "RadHunterLaBr3";
  static const string sm_AvidRsiDetectorStr           = "RSI-Unspecified";
  static const string sm_RadEagleNaiDetectorStr       = "RadEagle NaI 3x1";
  static const string sm_RadEagleCeBr2InDetectorStr   = "RadEagle CeBr3 2x1";
  static const string sm_RadEagleCeBr3InDetectorStr   = "RadEagle CeBr3 3x0.8";
  static const string sm_RadEagleLaBrDetectorStr      = "RadEagle LaBr3 2x1";
  
//  GN3, InSpector 1000 LaBr3, Pager-S, SAM-Eagle-LaBr, GR130, SAM-Eagle-NaI-3x3
//  InSpector 1000 NaI, RadPack, SpiR-ID LaBr3, Interceptor, Radseeker, SpiR-ID NaI
//  GR135Plus, LRM, Raider, HRM, LaBr3PNNL, Transpec, Falcon 5000, Ranger
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
    case DetectorType::Sam940:
      return sm_Sam940DetectorStr;
    case DetectorType::Sam945:
      return sm_Sam945DetectorStr;
    case DetectorType::Srpm210:
      return sm_Srpm210DetectorStr;
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
  if( channel_energies_ )
    size += sizeof(*(channel_energies_.get())) + channel_energies_->capacity()*sizeof(float);

  size += calibration_coeffs_.capacity()*sizeof(float);
  size += neutron_counts_.capacity()*sizeof(float);

  size += deviation_pairs_.capacity()*sizeof(std::pair<float,float>);

  return size;
}//size_t Measurement::memmorysize() const


bool Measurement::compare_by_sample_det_time( const std::shared_ptr<const Measurement> &lhs,
                           const std::shared_ptr<const Measurement> &rhs )
{
  if( !lhs || !rhs )
    return false;

  if( lhs->sample_number_ != rhs->sample_number_ )
    return (lhs->sample_number_ < rhs->sample_number_);

  if( lhs->detector_number_ != rhs->detector_number_ )
    return (lhs->detector_number_ < rhs->detector_number_);

  if( lhs->start_time_ != rhs->start_time_ )
    return (lhs->start_time_ < rhs->start_time_);
  
  return (lhs->source_type() < rhs->source_type());
}//lesThan(...)


void Measurement::reset()
{
  live_time_ = 0.0f;
  real_time_ = 0.0f;

  sample_number_ = 1;
  occupied_ = OccupancyStatus::Unknown;
  gamma_count_sum_ = 0.0;
  neutron_counts_sum_ = 0.0;
  speed_ = 0.0;
  detector_name_ = "";
  detector_number_ = -1;
  detector_description_ = "";
  quality_status_ = QualityStatus::Missing;

  source_type_       = SourceType::Unknown;
  energy_calibration_model_ = SpecUtils::EnergyCalType::InvalidEquationType;

  contained_neutron_ = false;

  latitude_ = longitude_ = -999.9;
  position_time_ = boost::posix_time::not_a_date_time;

  remarks_.clear();
  
  start_time_ = boost::posix_time::not_a_date_time;
  calibration_coeffs_.clear();
  deviation_pairs_.clear();
  channel_energies_ = std::make_shared<vector<float> >();//XXX I should test not bothering to place an empty vector in this pointer
  gamma_counts_ = std::make_shared<vector<float> >();  //XXX I should test not bothering to place an empty vector in this pointer
  neutron_counts_.clear();
}//void reset()

  
bool Measurement::has_gps_info() const
{
  return (SpecUtils::valid_longitude(longitude_) && SpecUtils::valid_latitude(latitude_));
}
  
  
bool SpecFile::has_gps_info() const
{
  return (SpecUtils::valid_longitude(mean_longitude_)
            && SpecUtils::valid_latitude(mean_latitude_));
}

  
void Measurement::combine_gamma_channels( const size_t ncombine )
{
  if( !gamma_counts_ )
    return;
  
  const size_t nchannelorig = gamma_counts_->size();
  const size_t nnewchann = nchannelorig / ncombine;
  
  if( !nchannelorig || ncombine==1 )
    return;
  
  if( !ncombine || ((nchannelorig % ncombine) != 0) || ncombine > nchannelorig )
    throw runtime_error( "combine_gamma_channels(): invalid input." );

#if( PERFORM_DEVELOPER_CHECKS )
  const double pre_gammasum = std::accumulate( gamma_counts_->begin(),
                                            gamma_counts_->end(), double(0.0) );
  const float pre_lower_e = (*channel_energies_)[0];
  const float pre_upper_e = (*channel_energies_)[nchannelorig - ncombine];
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
  
  std::shared_ptr< vector<float> > newchanneldata
                        = std::make_shared<vector<float> >( nnewchann, 0.0f );
  
  for( size_t i = 0; i < nchannelorig; ++i )
    (*newchanneldata)[i/ncombine] += (*gamma_counts_)[i];
  
  //nchannelorig is std::min( gamma_counts_->size(), channel_energies_->size() )
  //  which practically gamma_counts_->size()==channel_energies_->size(), but
  //  jic
  //for( size_t i = nchannelorig; i < gamma_counts_->size(); ++i )
    //(*newchanneldata)[nnewchann-1] += (*gamma_counts_)[i];
  
  gamma_counts_ = newchanneldata;
  
  switch( energy_calibration_model_ )
  {
    case SpecUtils::EnergyCalType::Polynomial:
    case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
      for( size_t i = 1; i < calibration_coeffs_.size(); ++i )
        calibration_coeffs_[i] *= std::pow( float(ncombine), float(i) );
      channel_energies_ = SpecUtils::polynomial_binning( calibration_coeffs_, nnewchann,
                                              deviation_pairs_ );
      break;
      
    case SpecUtils::EnergyCalType::FullRangeFraction:
      channel_energies_ = SpecUtils::fullrangefraction_binning( calibration_coeffs_,
                                                   nnewchann, deviation_pairs_ );
      break;
      
    case SpecUtils::EnergyCalType::LowerChannelEdge:
    case SpecUtils::EnergyCalType::InvalidEquationType:
      if( !!channel_energies_ )
      {
        std::shared_ptr<vector<float> > newbinning
                               = std::make_shared<vector<float> >(nnewchann);
        
        for( size_t i = 0; i < nchannelorig; i += ncombine )
          (*newbinning)[i/ncombine] = (*channel_energies_)[i];
        
        channel_energies_ = newbinning;
      }//if( !!channel_energies_ )
      break;
  }//switch( energy_calibration_model_ )
  
  
#if( PERFORM_DEVELOPER_CHECKS )
  const double post_gammasum = std::accumulate( gamma_counts_->begin(),
                                            gamma_counts_->end(), double(0.0) );
  const float post_lower_e = channel_energies_->front();
  const float post_upper_e = channel_energies_->back();
  
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


size_t SpecFile::do_channel_data_xform( const size_t nchannels,
                std::function< void(std::shared_ptr<Measurement>) > xform )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  size_t nchanged = 0;
  std::shared_ptr<Measurement> firstcombined;
  
  set<size_t> nchannelset, othernchannel;
  set<MeasurementCalibInfo> othercalibs;
  map<MeasurementCalibInfo, vector<std::shared_ptr<Measurement> > > calibs;
  
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
        MeasurementCalibInfo info(m);
        info.binning.reset();
        info.original_binning.reset();
        othercalibs.insert( info );
      }
      
      continue;
    }//if( not a gamma measurment )
    
    xform( m );
    
    MeasurementCalibInfo info( m );
    info.binning.reset();
    info.original_binning.reset();
    
    if(!calibs[info].empty())
    {
      cerr << "Making it so measurment shared channel_energies_ with " << calibs[info][0].get() << endl;
      m->channel_energies_ = calibs[info][0]->channel_energies_;
    }
    
    calibs[info].push_back( m );
    if( !!m->channel_energies_ )
      nchannelset.insert( m->channel_energies_->size() );
    
    ++nchanged;
  }//for( size_t i = 0; i < measurements_.size(); ++i )
  
  //ToDo: better test this function.
  cerr << "There are calibs.size()=" << calibs.size() << endl;
  cerr << "There are othercalibs.size()=" << othercalibs.size() << endl;
  
  if( nchanged )
  {
    if( calibs.size() > 1 || othercalibs.size() > 1
       || (calibs.size()==1 && othercalibs.size()==1
           && !((calibs.begin()->first) == (*othercalibs.begin()))) )
    {
      cerr << "Un-setting properties_flags_ kHasCommonBinning bit" << endl;
      properties_flags_ &= (~kHasCommonBinning);
    }else
    {
      cerr << "Setting properties_flags_ kHasCommonBinning bit" << endl;
      properties_flags_ |= kHasCommonBinning;
    }
  
    if( (nchannelset.size() > 1) || (othernchannel.size() > 1)
       || (nchannelset.size()==1 && othernchannel.size()==1
           && (*nchannelset.begin())!=(*othernchannel.begin())) )
    {
      cerr << "Un-setting properties_flags_ kAllSpectraSameNumberChannels bit" << endl;
      properties_flags_ &= (~kAllSpectraSameNumberChannels);
    }else
    {
      cerr << "Setting properties_flags_ kAllSpectraSameNumberChannels bit" << endl;
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
  
  std::shared_ptr<Measurement> m = measurment( meas );
  if( !m )
    throw runtime_error( "SpecFile::combine_gamma_channels(): measurment"
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
    throw runtime_error( "Measurement::truncate_gamma_channels(): invalid upper channel." );
  
  if( keep_first_channel > keep_last_channel )
    throw runtime_error( "Measurement::truncate_gamma_channels(): invalid channel range." );
  
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
  newchannelcounts->back()  += static_cast<float> ( overflow );

  
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
  
  gamma_counts_ = newchannelcounts;
  
  if( !keep_under_over_flow )
  {
    gamma_count_sum_ = 0.0;
    for( const float f : *gamma_counts_ )
      gamma_count_sum_ += f;
  }//if( !keep_under_over_flow )

  
#if( PERFORM_DEVELOPER_CHECKS )
  std::shared_ptr<const std::vector<float>> old_binning = channel_energies_;
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
  
  const int n = static_cast<int>( keep_first_channel );
  const vector<float> a = calibration_coeffs_;

  
  switch( energy_calibration_model_ )
  {
    case SpecUtils::EnergyCalType::Polynomial:
    case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
    {
      calibration_coeffs_ = SpecUtils::polynomial_cal_remove_first_channels( n, a );      
      channel_energies_ = SpecUtils::polynomial_binning( calibration_coeffs_, nnewchannel,
                                              deviation_pairs_ );
      break;
    }//case Polynomial:
      
    case SpecUtils::EnergyCalType::FullRangeFraction:
    {
      const vector<float> oldpoly = SpecUtils::fullrangefraction_coef_to_polynomial( a,
                                                              nprevchannel );
      
      const vector<float> newpoly = SpecUtils::polynomial_cal_remove_first_channels( n,
                                                                    oldpoly );
      
      vector<float> newfwf = SpecUtils::polynomial_coef_to_fullrangefraction(
                                                        newpoly, nnewchannel );
      
      if( a.size() > 4 )
      {
//        const float x = static_cast<float>(i)/static_cast<float>(nbin);
//        const float low_e_coef = (a.size() > 4) ? a[4] : 0.0f;
//        val += low_e_coef / (1.0f+60.0f*x);

        //XXX - fix up here, if doable.  I dont think this can be exactly done,
        //  but maybye make it match up at the bottom binning, since this term
        //  only effects low energy.
      }//if( a.size() > 4 )
      
      calibration_coeffs_ = newfwf;
      channel_energies_ = SpecUtils::fullrangefraction_binning( calibration_coeffs_,
                                            nnewchannel, deviation_pairs_ );
      break;
    }//case FullRangeFraction:
      
    case SpecUtils::EnergyCalType::LowerChannelEdge:
    case SpecUtils::EnergyCalType::InvalidEquationType:
      if( !!channel_energies_ )
      {
        std::shared_ptr< vector<float> > newbinning
                             = std::make_shared<vector<float> >(nnewchannel);
        
        for( size_t i = keep_first_channel; i <= keep_last_channel; ++i )
          (*newbinning)[i-keep_first_channel] = (*channel_energies_)[i];
        
        channel_energies_ = newbinning;
      }//if( !!channel_energies_ )
      break;
  }//switch( energy_calibration_model_ )
  
  
#if( PERFORM_DEVELOPER_CHECKS )
  if( !!old_binning && !!channel_energies_ )
  {
    for( size_t i = keep_first_channel; i <= keep_last_channel; ++i )
    {
      const float newval = (*channel_energies_)[i-keep_first_channel];
      const float oldval = (*old_binning)[i];
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
  }//if( !!old_binning && !!channel_energies_ )
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
  
  
  std::shared_ptr<Measurement> m = measurment( meas );
  if( !m )
    throw runtime_error( "SpecFile::truncate_gamma_channels(): measurment"
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



std::shared_ptr<Measurement> SpecFile::measurment( std::shared_ptr<const Measurement> meas )
{
  for( const auto &m : measurements_ )
  {
    if( m == meas )
      return m;
  }//for( const auto &m : measurements_ )
  
  return std::shared_ptr<Measurement>();
}//measurment(...)

//set_live_time(...) and set_real_time(...) update both the measurment
//  you pass in, as well as *this.  If measurment is invalid, or not in
//  measurments_, than an exception is thrown.
void SpecFile::set_live_time( const float lt,
                                     std::shared_ptr<const Measurement> meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  std::shared_ptr<Measurement> ptr = measurment( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_live_time(...): measurment"
                         " passed in didnt belong to this SpecFile" );

  const float oldLifeTime = meas->live_time();
  ptr->live_time_ = lt;
  gamma_live_time_ += (lt-oldLifeTime);
  modified_ = modifiedSinceDecode_ = true;
}//set_live_time(...)

void SpecFile::set_real_time( const float rt, std::shared_ptr<const Measurement> meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  std::shared_ptr<Measurement> ptr = measurment( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_real_time(...): measurment"
                         " passed in didnt belong to this SpecFile" );

  const float oldRealTime = ptr->live_time();
  ptr->real_time_ = rt;
  gamma_real_time_ += (rt - oldRealTime);
  modified_ = modifiedSinceDecode_ = true;
}//set_real_time(...)


void SpecFile::add_measurment( std::shared_ptr<Measurement> meas,
                                      const bool doCleanup )
{
  if( !meas )
    return;
  
  vector< std::shared_ptr<Measurement> >::iterator meas_pos;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  meas_pos = lower_bound( measurements_.begin(), measurements_.end(),
                          meas, &Measurement::compare_by_sample_det_time );
 
  if( (meas_pos!=measurements_.end()) && ((*meas_pos)==meas) )
    throw runtime_error( "SpecFile::add_measurment: duplicate meas" );
  
  //Making sure detector names/numbers are kept track of here instead of in
  //  cleanup_after_load() makes sure to preserve sample and detector numbers
  //  of the Measurments already in this SpecFile object
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
    if( meas->contained_neutron_ )
      neutron_detector_names_.push_back( detname );
  }else
  {
    const size_t index = namepos - detector_names_.begin();
    meas->detector_number_ = detector_numbers_.at(index);
  }//if( we dont already have a detector with this name ) / else
  
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
                          meas, &Measurement::compare_by_sample_det_time );
  
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
    
    sample_to_measurments_.clear();
    for( size_t measn = 0; measn < measurements_.size(); ++measn )
    {
      std::shared_ptr<Measurement> &meas = measurements_[measn];
      sample_to_measurments_[meas->sample_number_].push_back( measn );
    }
  }//if( doCleanup ) / else
  
  modified_ = modifiedSinceDecode_ = true;
}//void add_measurment( std::shared_ptr<Measurement> meas, bool doCleanup )


void SpecFile::remove_measurments(
                                         const vector<std::shared_ptr<const Measurement>> &meas )
{
  if( meas.empty() )
    return;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const size_t norigmeas = measurements_.size();
  if( meas.size() > norigmeas )
    throw runtime_error( "SpecFile::remove_measurments:"
                        " to many input measurments to remove" );
  
  //This below implementation is targeted for SpecFile's with lots of
  //  measurements_, and empiracally is much faster than commented out
  //  implementation below (which is left in because its a bit 'cleaner')
  vector<bool> keep( norigmeas, true );
  
  for( size_t i = 0; i < meas.size(); ++i )
  {
    const std::shared_ptr<const Measurement> &m = meas[i];
    
    map<int, std::vector<size_t> >::const_iterator iter
    = sample_to_measurments_.find( m->sample_number_ );
    
    if( iter != sample_to_measurments_.end() )
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
        throw runtime_error( "SpecFile::remove_measurments: invalid meas" );
    }//if( iter != sample_to_measurments_.end() )
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
   m, &Measurement::compare_by_sample_det_time );
   }else
   {
   pos = std::find( measurements_.begin(), measurements_.end(), m );
   }
   
   if( pos == measurements_.end() || ((*pos)!=m) )
   throw runtime_error( "SpecFile::remove_measurments: invalid meas" );
   
   measurements_.erase( pos );
   }
   */
  
  cleanup_after_load();
  modified_ = modifiedSinceDecode_ = true;
}//void remove_measurments( const vector<std::shared_ptr<const Measurement>> meas )


void SpecFile::remove_measurment( std::shared_ptr<const Measurement> meas,
                                         const bool doCleanup )
{
  if( !meas )
    return;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  vector< std::shared_ptr<Measurement> >::iterator pos
                = std::find( measurements_.begin(), measurements_.end(), meas );
  
  if( pos == measurements_.end() )
    throw runtime_error( "SpecFile::remove_measurment: invalid meas" );
  
  measurements_.erase( pos );
  
  //Could actually fix up detector_names_, detector_numbers_,
  //  neutron_detector_names_,
  
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
    sample_to_measurments_.clear();
    
    //should update detector names and numbers too
    set<string> detectornames;
    
    const size_t nmeas = measurements_.size();
    for( size_t measn = 0; measn < nmeas; ++measn )
    {
      const int samplenum = measurements_[measn]->sample_number_;
      sample_numbers_.insert( samplenum );
      sample_to_measurments_[samplenum].push_back( measn );
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
}//void remove_measurment( std::shared_ptr<Measurement> meas, bool doCleanup );


void SpecFile::set_start_time( const boost::posix_time::ptime &timestamp,
                    const std::shared_ptr<const Measurement> meas  )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  std::shared_ptr<Measurement> ptr = measurment( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_start_time(...): measurment"
                        " passed in didnt belong to this SpecFile" );
  
  ptr->set_start_time( timestamp );
  modified_ = modifiedSinceDecode_ = true;
}//set_start_time(...)

void SpecFile::set_remarks( const std::vector<std::string> &remarks,
                 const std::shared_ptr<const Measurement> meas  )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  std::shared_ptr<Measurement> ptr = measurment( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_remarks(...): measurment"
                        " passed in didnt belong to this SpecFile" );
  
  ptr->set_remarks( remarks );
  modified_ = modifiedSinceDecode_ = true;
}//set_remarks(...)

void SpecFile::set_source_type( const SourceType type,
                                    const std::shared_ptr<const Measurement> meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  std::shared_ptr<Measurement> ptr = measurment( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_source_type(...): measurment"
                        " passed in didnt belong to this SpecFile" );
  
  ptr->set_source_type( type );
  modified_ = modifiedSinceDecode_ = true;
}//set_source_type(...)


void SpecFile::set_position( double longitude, double latitude,
                                    boost::posix_time::ptime position_time,
                                    const std::shared_ptr<const Measurement> meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  std::shared_ptr<Measurement> ptr = measurment( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_position(...): measurment"
                        " passed in didnt belong to this SpecFile" );
  
  ptr->longitude_ = longitude;
  ptr->latitude_ = latitude;
  ptr->position_time_ = position_time;
  
  
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
  std::shared_ptr<Measurement> ptr = measurment( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_title(...): measurment"
                        " passed in didnt belong to this SpecFile" );
  
  ptr->set_title( title );
  
  modified_ = modifiedSinceDecode_ = true;
}//void SpecFile::set_title(...)


void SpecFile::set_contained_neutrons( const bool contained,
                                             const float counts,
                                             const std::shared_ptr<const Measurement> meas )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  std::shared_ptr<Measurement> ptr = measurment( meas );
  if( !ptr )
    throw runtime_error( "SpecFile::set_containtained_neutrons(...): "
                        "measurment passed in didnt belong to this "
                        "SpecFile" );
  
  ptr->contained_neutron_ = contained;
  if( contained )
  {
    ptr->neutron_counts_.resize( 1 );
    ptr->neutron_counts_[0] = counts;
    ptr->neutron_counts_sum_ = counts;
  }else
  {
    ptr->neutron_counts_.resize( 0 );
    ptr->neutron_counts_sum_ = 0.0;
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
  
  *pos = newname;
  
  auto neutpos = find( begin(neutron_detector_names_), end(neutron_detector_names_), origname );
  if( neutpos != end(neutron_detector_names_) )
    *neutpos = newname;
  
  
  for( auto &m : measurements_ )
  {
    if( m && (m->detector_name_ == origname) )
      m->detector_name_ = newname;
  }
  
  modified_ = modifiedSinceDecode_ = true;
}//change_detector_name(...)


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
    pos = sample_to_measurments_.find( sample_number );
    
    if( pos != sample_to_measurments_.end() )
    {
      const vector<size_t> &indices = pos->second;
      for( const size_t ind : indices )
        if( measurements_[ind]->detector_number_ == detector_number )
          return measurements_[ind];
    }//if( pos != sample_to_measurments_.end() )
    
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
  pos = sample_to_measurments_.find( sample );

  if( pos != sample_to_measurments_.end() )
  {
    const vector<size_t> &indices = pos->second;
    for( const size_t ind : indices )
      answer.push_back( measurements_.at(ind) );
  }//if( pos != sample_to_measurments_.end() )

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







void Measurement::recalibrate_by_eqn( const std::vector<float> &eqn,
                                      const std::vector<std::pair<float,float>> &dev_pairs,
                                      SpecUtils::EnergyCalType type,
                                      std::shared_ptr<const std::vector<float>> binning )
{
  // Note: this will run multiple times per calibration

  if( !binning && gamma_counts_ && gamma_counts_->size() )
  {
    switch( type )
    {
      case SpecUtils::EnergyCalType::Polynomial:
      case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
        binning = SpecUtils::polynomial_binning( eqn, gamma_counts_->size(), dev_pairs );
      break;

      case SpecUtils::EnergyCalType::FullRangeFraction:
        binning = SpecUtils::fullrangefraction_binning( eqn, gamma_counts_->size(), dev_pairs );
      break;

      case SpecUtils::EnergyCalType::LowerChannelEdge:
        cerr << SRC_LOCATION << "\n\tWarning, you probabhouldnt be calling "
             << "this function for EquationType==LowerChannelEdge, as its probably "
             << "not as efficient as the other version of this function"
             << endl;
        binning.reset( new vector<float>(eqn) );
      break;

      case SpecUtils::EnergyCalType::InvalidEquationType:
        throw runtime_error( "Measurement::recalibrate_by_eqn(...): Can not call with EquationType==InvalidEquationType" );
    }//switch( type )
  }//if( !binning )


  if( !binning || ((binning->size() != gamma_counts_->size())
      && (type!=SpecUtils::EnergyCalType::LowerChannelEdge || gamma_counts_->size()>binning->size())) )
  {
    stringstream msg;
    msg << "Measurement::recalibrate_by_eqn(...): "
           "You can not use this funtion to change the number"
           " of bins. The spectrum has " << gamma_counts_->size()
        << " bins, you tried to give it " << binning->size() << " bins."
        << endl;
    throw runtime_error( msg.str() );
  }//if( channel_energies_->size() != gamma_counts_->size() )


  channel_energies_   = binning;
  if( type != SpecUtils::EnergyCalType::LowerChannelEdge )
    calibration_coeffs_ = eqn;
  else
    calibration_coeffs_.clear();
  energy_calibration_model_  = type;
  deviation_pairs_ = dev_pairs;
  
  if( type == SpecUtils::EnergyCalType::LowerChannelEdge )
    deviation_pairs_.clear();
  
}//void recalibrate_by_eqn( const std::vector<float> &eqn )


void Measurement::rebin_by_eqn( const std::vector<float> &eqn,
                                const std::vector<std::pair<float,float>> &dev_pairs,
                                SpecUtils::EnergyCalType type )
{
  if( !gamma_counts_ || gamma_counts_->empty() )
    throw std::runtime_error( "Measurement::rebin_by_eqn(...): "
                              "gamma_counts_ must be defined");

  std::shared_ptr<const std::vector<float>> binning;
  const size_t nbin = gamma_counts_->size(); //channel_energies_->size();

  switch( type )
  {
    case SpecUtils::EnergyCalType::Polynomial:
    case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
      binning = SpecUtils::polynomial_binning( eqn, nbin, dev_pairs );
    break;

    case SpecUtils::EnergyCalType::FullRangeFraction:
      binning = SpecUtils::fullrangefraction_binning( eqn, nbin, dev_pairs );
    break;

    case SpecUtils::EnergyCalType::LowerChannelEdge:
      binning.reset( new vector<float>(eqn) );
    break;

    case SpecUtils::EnergyCalType::InvalidEquationType:
      throw std::runtime_error( "Measurement::rebin_by_eqn(...): "
                                "can not be called with type==InvalidEquationType");
    break;
  }//switch( type )

  rebin_by_eqn( eqn, dev_pairs, binning, type );
}//void rebin_by_eqn( const std::vector<float> eqn )


void Measurement::rebin_by_eqn( const std::vector<float> &eqn,
                                const std::vector<std::pair<float,float>> &dev_pairs,
                                std::shared_ptr<const std::vector<float>> binning,
                                SpecUtils::EnergyCalType type )
{
  rebin_by_lower_edge( binning );

  if( type == SpecUtils::EnergyCalType::InvalidEquationType )
    throw std::runtime_error( "Measurement::rebin_by_eqn(...): "
                              "can not be called with type==InvalidEquationType");

  if( type != SpecUtils::EnergyCalType::LowerChannelEdge )
    calibration_coeffs_ = eqn;
  else
    calibration_coeffs_.clear();

  deviation_pairs_    = dev_pairs;
  energy_calibration_model_  = type;
}//void rebin_by_eqn(...)





void Measurement::rebin_by_lower_edge( std::shared_ptr<const std::vector<float>> new_binning_shrd )
{
  if( new_binning_shrd.get() == channel_energies_.get() )
    return;

  if( !gamma_counts_ )
    return;
  
  {//being codeblock to do work
    if( !new_binning_shrd )
      throw runtime_error( "rebin_by_lower_edge: invalid new binning" );
    
    if( !channel_energies_ || channel_energies_->empty() )
      throw runtime_error( "rebin_by_lower_edge: no initial binning" );
    
    const size_t new_nbin = new_binning_shrd->size();
    std::shared_ptr<std::vector<float>> rebinned_gamma_counts( new vector<float>(new_nbin) );
    
    SpecUtils::rebin_by_lower_edge( *channel_energies_, *gamma_counts_,
                            *new_binning_shrd, *rebinned_gamma_counts );
    
    channel_energies_ = new_binning_shrd;
    gamma_counts_ = rebinned_gamma_counts;
    
    deviation_pairs_.clear();
    calibration_coeffs_.clear();
    energy_calibration_model_ = SpecUtils::EnergyCalType::LowerChannelEdge;
  }//end codeblock to do works
}//void rebin_by_lower_edge(...)






void expand_counted_zeros( const vector<float> &data, vector<float> &return_answer )
{
  vector<float> answer;
  answer.reserve( 1024 );
  vector<float>::const_iterator iter;
  for( iter = data.begin(); iter != data.end(); iter++)
  {
    if( (*iter != 0.0f) || (iter+1==data.end()) || (*(iter+1)==0.0f) )
      answer.push_back(*iter);
    else
    {
      iter++;
      const size_t nZeroes = ((iter==data.end()) ? 0u : static_cast<size_t>(floor(*iter + 0.5f)) );

      if( ((*iter) <= 0.5f) || ((answer.size() + nZeroes) > 131072) )
        throw runtime_error( "Invalid counted zeros: too many total elements, or negative number of zeros" );
      
      for( size_t k = 0; k < nZeroes; ++k )
        answer.push_back( 0.0f );
    }//if( at a non-zero value, the last value, or the next value is zero) / else
  }//for( iterate over data, iter )

  answer.swap( return_answer );
}//vector<float> expand_counted_zeros( const vector<float> &data )


void compress_to_counted_zeros( const std::vector<float> &input, std::vector<float> &results )
{
  results.clear();

  //Previous to 20181120 1E-8 was used, but this caused problems with PCF files
  //  from GADRAS that were not Poisson varied.  FLT_EPSILON is usually 1.19e-7f
  //  which is still to big!  So chose 10*FLT_MIN (FLT_MIN is something 1E-37)
  //  which worked with a GADRAS Db.pcf I checked.
  const float epsilon = 10.0f*FLT_MIN;
  
  
  
  const size_t nBin = input.size();

  for( size_t bin = 0; bin < nBin; ++bin )
  {
      const bool isZero = (fabs(input[bin]) < epsilon);

      if( !isZero ) results.push_back( input[bin] );
      else          results.push_back( 0.0f );

      if( isZero )
      {
          size_t nBinZeroes = 0;
          while( ( bin < nBin ) && ( fabs( input[bin] ) < epsilon) )
          {
            ++nBinZeroes;
            ++bin;
          }//while more zero bins

          results.push_back( static_cast<float>(nBinZeroes) );

          if( bin != nBin )
            --bin;
      }//if( input[bin] == 0.0 )
  }//for( size_t bin = 0; bin < input.size(); ++bin )
}//void compress_to_counted_zeros(...)

#if( PERFORM_DEVELOPER_CHECKS )

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

void DetectorAnalysisResult::equalEnough( const DetectorAnalysisResult &lhs,
                                          const DetectorAnalysisResult &rhs )
{
  if( lhs.remark_ != rhs.remark_ )
    throw runtime_error( "DetectorAnalysisResult remark for LHS ('"
                         + lhs.remark_ + "') doesnt match RHS ('"
                         + rhs.remark_ + "')" );
  if( lhs.nuclide_ != rhs.nuclide_ )
    throw runtime_error( "DetectorAnalysisResult nuclide for LHS ('"
                         + lhs.nuclide_ + "') doesnt match RHS ('"
                         + rhs.nuclide_ + "')" );
  if( lhs.nuclide_type_ != rhs.nuclide_type_ )
    throw runtime_error( "DetectorAnalysisResult nuclide type for LHS ('"
                         + lhs.nuclide_type_ + "') doesnt match RHS ('"
                         + rhs.nuclide_type_ + "')" );
  if( lhs.id_confidence_ != rhs.id_confidence_ )
    throw runtime_error( "DetectorAnalysisResult ID confifence for LHS ('"
                         + lhs.id_confidence_ + "') doesnt match RHS ('"
                         + rhs.id_confidence_ + "')" );
  if( lhs.detector_ != rhs.detector_ )
    throw runtime_error( "DetectorAnalysisResult detector for LHS ('"
                         + lhs.detector_ + "') doesnt match RHS ('"
                         + rhs.detector_ + "')" );
  //if( lhs.start_time_ != rhs.start_time_ )
  //  throw runtime_error( "DetectorAnalysisResult start time for LHS ("
  //                      + SpecUtils::to_iso_string(lhs.start_time_) + ") doesnt match RHS ("
  //                    + SpecUtils::to_iso_string(rhs.start_time_) + ")" );
  
  char buffer[1024];
  if( fabs(lhs.activity_ - rhs.activity_) > (0.0001*std::max(fabs(lhs.activity_),fabs(rhs.activity_))) )
  {
    snprintf( buffer, sizeof(buffer),
             "DetectorAnalysisResult activity of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.activity_, rhs.activity_ );
    throw runtime_error( buffer );
  }
  
  if( fabs(lhs.distance_ - rhs.distance_) > 0.001 )
  {
    snprintf( buffer, sizeof(buffer),
             "DetectorAnalysisResult distance of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.activity_, rhs.activity_ );
    throw runtime_error( buffer );
  }
  
  if( fabs(lhs.dose_rate_ - rhs.dose_rate_) > 0.001 )
  {
    snprintf( buffer, sizeof(buffer),
             "DetectorAnalysisResult dose rate of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.dose_rate_, rhs.dose_rate_ );
    throw runtime_error( buffer );
  }
  
  if( fabs(lhs.real_time_ - rhs.real_time_) > 0.001 )
  {
    snprintf( buffer, sizeof(buffer),
             "DetectorAnalysisResult dose rate of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.real_time_, rhs.real_time_ );
    throw runtime_error( buffer );
  }
}//void DetectorAnalysisResult::equalEnough(...)


void DetectorAnalysis::equalEnough( const DetectorAnalysis &lhs,
                                    const DetectorAnalysis &rhs )
{
  char buffer[1024];
  
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
    throw runtime_error( buffer );
  }
  
  for( size_t i = 0; i < rhsremarks.size(); ++i )
  {
    if( lhsremarks[i] != rhsremarks[i] )
    {
      snprintf( buffer, sizeof(buffer),
               "Analysis remark %i for LHS ('%s') doesnt match RHS ('%s')",
               int(i), lhsremarks[i].c_str(), rhsremarks[i].c_str() );
      throw runtime_error( buffer );
    }
  }//for( size_t i = 0; i < rhsremarks.size(); ++i )
  
  auto lhsap = lhs.algorithm_component_versions_;
  auto rhsap = rhs.algorithm_component_versions_;
  
  if( lhsap.size() != rhsap.size() )
  {
    throw runtime_error( "Number of analysis algorithm versions for LHS ('"
                        + std::to_string(lhsap.size()) + "') doesnt match RHS ('"
                        + std::to_string(rhsap.size()) + "')" );
  }
  
  std::sort( begin(lhsap), end(lhsap) );
  std::sort( begin(rhsap), end(rhsap) );
  for( size_t i = 0; i < lhsap.size(); ++i )
  {
    if( lhsap[i].first != rhsap[i].first )
      throw runtime_error( "Analysis algorithm version compnent name for LHS ('"
                        + lhsap[i].first + "') doesnt match RHS ('"
                        + lhsap[i].first + "')" );
    if( lhsap[i].second != rhsap[i].second )
      throw runtime_error( "Analysis algorithm version compnent version for LHS ('"
                          + lhsap[i].second + "') doesnt match RHS ('"
                          + lhsap[i].second + "')" );
  }//for( size_t i = 0; i < rhs.lhsap.size(); ++i )
    
  
  if( lhs.algorithm_name_ != rhs.algorithm_name_ )
    throw runtime_error( "Analysis algorithm name for LHS ('"
                        + lhs.algorithm_name_ + "') doesnt match RHS ('"
                        + rhs.algorithm_name_ + "')" );
  
  if( lhs.algorithm_creator_ != rhs.algorithm_creator_ )
    throw runtime_error( "Analysis algorithm creator for LHS ('"
                        + lhs.algorithm_creator_ + "') doesnt match RHS ('"
                        + rhs.algorithm_creator_ + "')" );
  
  if( lhs.algorithm_description_ != rhs.algorithm_description_ )
    throw runtime_error( "Analysis algorithm description for LHS ('"
                        + lhs.algorithm_description_ + "') doesnt match RHS ('"
                        + rhs.algorithm_description_ + "')" );

  if( lhs.algorithm_result_description_ != rhs.algorithm_result_description_ )
    throw runtime_error( "Analysis algorithm result description for LHS ('"
                        + lhs.algorithm_result_description_ + "') doesnt match RHS ('"
                        + rhs.algorithm_result_description_ + "')" );
  
  
  
  if( lhs.results_.size() != rhs.results_.size() )
  {
    stringstream msg;
    msg << "Differnt number of analysis results for LHS ("
    << lhs.results_.size() << ") vs RHS (" << rhs.results_.size() << "):"
    << endl;
    
    for( DetectorAnalysisResult l : lhs.results_ )
    {
      msg << "\tLHS: remark='" << l.remark_ << "', nuclide='"
           << l.nuclide_ << "', doserate="
           << l.dose_rate_ << ", activity=" << l.activity_
           << ", id confidence='" << l.id_confidence_ << "'"
           << ", distance=" << l.distance_ << endl;
    }
    
    for( DetectorAnalysisResult l : rhs.results_ )
    {
      msg << "\t RHS: remark='" << l.remark_ << "', nuclide='"
          << l.nuclide_ << "', doserate="
          << l.dose_rate_ << ", activity=" << l.activity_
          << ", id confidence='" << l.id_confidence_ << "'"
          << ", distance=" << l.distance_ << endl;
    }
    
    throw runtime_error( msg.str() );
  }//if( lhs.results_.size() != rhs.results_.size() )

  //Ordering of the results is not garunteed in a round trip, since we use a
  //  single analysis result type, but N42 2012 defines a few different ones.
  vector<DetectorAnalysisResult> rhsres = rhs.results_;
  vector<DetectorAnalysisResult> lhsres = lhs.results_;
  std::sort( lhsres.begin(), lhsres.end(), &compare_DetectorAnalysisResult );
  std::sort( rhsres.begin(), rhsres.end(), &compare_DetectorAnalysisResult );
  
  for( size_t i = 0; i < rhsres.size(); ++i )
    DetectorAnalysisResult::equalEnough( lhsres[i], rhsres[i] );
}//void DetectorAnalysis::equalEnough(...)



void Measurement::equalEnough( const Measurement &lhs, const Measurement &rhs )
{
  char buffer[1024];
  
  //Make sure live time within something reasonable
  const double live_time_diff = fabs( double(lhs.live_time_) - double(rhs.live_time_));
  if( (live_time_diff > (std::max(lhs.live_time_,rhs.live_time_)*1.0E-5)) && (live_time_diff > 1.0E-3) )
  {
    snprintf( buffer, sizeof(buffer),
             "Live time of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.live_time_, rhs.live_time_ );
    throw runtime_error( buffer );
  }
  
  //Make sure real time within something reasonable
  const double real_time_diff = fabs( double(lhs.real_time_) - double(rhs.real_time_));
  if( (real_time_diff > (std::max(lhs.real_time_,rhs.real_time_)*1.0E-5)) && (real_time_diff > 1.0E-3) )
  {
    snprintf( buffer, sizeof(buffer),
             "Real time of LHS (%1.8E) doesnt match RHS (%1.8E); diff=%f",
             lhs.real_time_, rhs.real_time_, real_time_diff );
    throw runtime_error( buffer );
  }
  
  if( lhs.contained_neutron_ != rhs.contained_neutron_ )
  {
    snprintf( buffer, sizeof(buffer),
             "LHS %s contain neutrons while RHS %s",
             (lhs.contained_neutron_?"did":"did not"),
             (rhs.contained_neutron_?"did":"did not") );
    throw runtime_error( buffer );
  }

  if( lhs.sample_number_ != rhs.sample_number_ )
    throw runtime_error( "LHS sample number some how didnt equal RHS sample number" );
  
  if( lhs.occupied_ != rhs.occupied_ )
  {
    snprintf( buffer, sizeof(buffer),
             "Ocupied of LHS (%i) differend form RHS (%i)",
             int(lhs.occupied_), int(rhs.occupied_) );
    throw runtime_error( buffer );
  }
  
  if( fabs(lhs.gamma_count_sum_ - rhs.gamma_count_sum_) > (0.00001*std::max(fabs(lhs.gamma_count_sum_),fabs(rhs.gamma_count_sum_))) )
  {
    snprintf( buffer, sizeof(buffer),
             "Gamma count sum of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.gamma_count_sum_, rhs.gamma_count_sum_ );
    throw runtime_error( buffer );
  }
  
  if( fabs(lhs.neutron_counts_sum_ - rhs.neutron_counts_sum_) > (0.00001*std::max(fabs(lhs.neutron_counts_sum_),fabs(rhs.neutron_counts_sum_))) )
  {
    snprintf( buffer, sizeof(buffer),
             "Neutron count sum of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.neutron_counts_sum_, rhs.neutron_counts_sum_ );
    throw runtime_error( buffer );
  }

  if( fabs(lhs.speed_ - rhs.speed_) > 0.01 )
  {
    snprintf( buffer, sizeof(buffer),
             "Speed of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.speed_, rhs.speed_ );
    throw runtime_error( buffer );
  }

  if( lhs.detector_name_ != rhs.detector_name_ )
    throw runtime_error( "Detector name for LHS ('" + lhs.detector_name_
                        + "') doesnt match RHS ('" + rhs.detector_name_ + "')" );

  /*
  if( lhs.detector_number_ != rhs.detector_number_ )
  {
    snprintf( buffer, sizeof(buffer),
             "Detector number of LHS (%i) doesnt match RHS (%i)",
             lhs.detector_number_, rhs.detector_number_ );
    throw runtime_error( buffer );
  }
   */

  if( lhs.detector_description_ != rhs.detector_description_
     && rhs.detector_description_!="Gamma and Neutron"
     && ("NaI, " + lhs.detector_description_) != rhs.detector_description_
     && ("LaBr3, " + lhs.detector_description_) != rhs.detector_description_
     && ("unknown, " + lhs.detector_description_) != rhs.detector_description_
     )
    throw runtime_error( "Detector description for LHS ('" + lhs.detector_description_
                        + "') doesnt match RHS ('" + rhs.detector_description_ + "')" );

  if( lhs.quality_status_ != rhs.quality_status_ )
  {
    snprintf( buffer, sizeof(buffer),
             "Quality status of LHS (%i) different from RHS (%i)",
             int(lhs.quality_status_), int(rhs.quality_status_) );
    throw runtime_error( buffer );
  }
  
  if( lhs.source_type_ != rhs.source_type_ )
  {
    snprintf( buffer, sizeof(buffer),
             "Source type of LHS (%i) different from RHS (%i)",
             int(lhs.source_type_), int(rhs.source_type_) );
    throw runtime_error( buffer );
  }

  auto lhs_cal_model = lhs.energy_calibration_model_;
  auto rhs_cal_model = rhs.energy_calibration_model_;
  
  if( lhs_cal_model == SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial )
    lhs_cal_model = SpecUtils::EnergyCalType::Polynomial;
  if( rhs_cal_model == SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial )
    rhs_cal_model = SpecUtils::EnergyCalType::Polynomial;

  if( lhs_cal_model != rhs_cal_model && !!lhs.channel_energies_ )
  {
    if( (lhs_cal_model == SpecUtils::EnergyCalType::Polynomial
         && rhs_cal_model == SpecUtils::EnergyCalType::FullRangeFraction)
       || (lhs_cal_model == SpecUtils::EnergyCalType::FullRangeFraction
           && rhs_cal_model == SpecUtils::EnergyCalType::Polynomial) )
    {
      const size_t nbin = lhs.channel_energies_->size();
      vector<float> lhscoefs = lhs.calibration_coeffs_;
      vector<float> rhscoefs = rhs.calibration_coeffs_;
      
      if( rhs.energy_calibration_model_ == SpecUtils::EnergyCalType::Polynomial )
        rhscoefs = SpecUtils::polynomial_coef_to_fullrangefraction( rhscoefs, nbin );
      if( lhs.energy_calibration_model_ == SpecUtils::EnergyCalType::Polynomial )
        lhscoefs = SpecUtils::polynomial_coef_to_fullrangefraction( lhscoefs, nbin );
      
      if( rhscoefs.size() != lhscoefs.size() )
        throw runtime_error( "Calibration coefficients LHS and RHS do not match size after converting to be same type" );
      
      for( size_t i = 0; i < rhscoefs.size(); ++i )
      {
        const float a = lhscoefs[i];
        const float b = rhscoefs[i];
        
        if( fabs(a - b) > (1.0E-5*std::max(fabs(a),fabs(b))) )
        {
          snprintf( buffer, sizeof(buffer),
                   "Calibration coefficient %i of LHS (%1.8E) doesnt match RHS (%1.8E) (note, concerted calib type)",
                   int(i), a, b );
          throw runtime_error( buffer );
        }
      }
    }else if( lhs.gamma_count_sum_>0.0 || rhs.gamma_count_sum_>0.0 )
    {
      snprintf( buffer, sizeof(buffer),
               "Calibration model of LHS (%i) different from RHS (%i)",
               int(lhs.energy_calibration_model_),
               int(rhs.energy_calibration_model_) );
      throw runtime_error( buffer );
    }
  }
  
  
  const set<string> nlhsremarkss( lhs.remarks_.begin(), lhs.remarks_.end() );
  const set<string> nrhsremarkss( rhs.remarks_.begin(), rhs.remarks_.end() );
  
  const vector<string> nlhsremarks( nlhsremarkss.begin(), nlhsremarkss.end() );
  const vector<string> nrhsremarks( nrhsremarkss.begin(), nrhsremarkss.end() );
  
  if( nlhsremarks.size() != nrhsremarks.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "Number of remarks in LHS (%i) doesnt match RHS (%i) for sample %i, det '%s'",
             int(nlhsremarks.size()), int(nrhsremarks.size()),
             lhs.sample_number_, lhs.detector_name_.c_str() );

#if( !REQUIRE_REMARKS_COMPARE )
    cerr << buffer << endl;
#endif
    for( size_t i = 0; i < nlhsremarks.size(); ++i )
      cerr << "\tLHS: '" << nlhsremarks[i] << "'" << endl;
    for( size_t i = 0; i < nrhsremarks.size(); ++i )
      cerr << "\tRHS: '" << nrhsremarks[i] << "'" << endl;
    
#if( REQUIRE_REMARKS_COMPARE )
    throw runtime_error( buffer );
#else
    cerr << endl;
#endif
  }//if( nlhsremarks.size() != nrhsremarks.size() )
  
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
      throw runtime_error( buffer );
#else
      cerr << buffer << endl;
#endif
    }
  }//for( size_t i = 0; i < nlhsremarks.size(); ++i )

  
  const set<string> lhsparsewarns( lhs.parse_warnings_.begin(), lhs.parse_warnings_.end() );
  const set<string> rhsparsewarns( rhs.parse_warnings_.begin(), rhs.parse_warnings_.end() );
  
  const vector<string> lhsparsewarn( lhsparsewarns.begin(), lhsparsewarns.end() );
  const vector<string> rhsparsewarn( rhsparsewarns.begin(), rhsparsewarns.end() );
  
  if( lhsparsewarn.size() != rhsparsewarn.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "Number of parse warnings in LHS (%i) doesnt match RHS (%i)",
             int(lhsparsewarn.size()), int(rhsparsewarn.size()) );
    
#if( !REQUIRE_REMARKS_COMPARE )
    cerr << buffer << endl;
#endif
    for( size_t i = 0; i < lhsparsewarn.size(); ++i )
      cerr << "\tLHS: '" << lhsparsewarn[i] << "'" << endl;
    for( size_t i = 0; i < rhsparsewarn.size(); ++i )
      cerr << "\tRHS: '" << rhsparsewarn[i] << "'" << endl;
#if( REQUIRE_REMARKS_COMPARE )
    throw runtime_error( buffer );
#else
    cerr << buffer << endl;
#endif
  }//if( lhsparsewarn.size() != rhsparsewarn.size() )
  
  
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
  
  
  if( lhs.start_time_ != rhs.start_time_ )
  {
	//For some reason the fractional second differ for some timestamps (including CNF files)
	// on Windows vs *NIX.  I guess let this slide for the moment.
	auto tdiff = lhs.start_time_ - rhs.start_time_;
	if( tdiff.is_negative() )
		tdiff = -tdiff;
	if( tdiff >  boost::posix_time::seconds(1) )
      throw runtime_error( "Start time for LHS ("
                        + SpecUtils::to_iso_string(lhs.start_time_) + ") doesnt match RHS ("
                        + SpecUtils::to_iso_string(rhs.start_time_) + ")" );
  }

  if( lhs.energy_calibration_model_ == rhs.energy_calibration_model_ )
  {
    vector<float> lhscalcoef = lhs.calibration_coeffs_;
    vector<float> rhscalcoef = rhs.calibration_coeffs_;
    
    while( lhscalcoef.size() && lhscalcoef.back() == 0.0f )
      lhscalcoef.erase( lhscalcoef.end() - 1 );
    while( rhscalcoef.size() && rhscalcoef.back() == 0.0f )
      rhscalcoef.erase( rhscalcoef.end() - 1 );

    if( lhscalcoef.size() != rhscalcoef.size() )
    {
      snprintf( buffer, sizeof(buffer),
               "Number of calibration coefficients of LHS (%i) doesnt match RHS (%i)",
               int(lhscalcoef.size()),
               int(rhscalcoef.size()) );
      throw runtime_error( buffer );
    }
  
    for( size_t i = 0; i < rhscalcoef.size(); ++i )
    {
      const float a = lhscalcoef[i];
      const float b = rhscalcoef[i];
    
      if( fabs(a - b) > (1.0E-5*std::max(fabs(a),fabs(b))) )
      {
        snprintf( buffer, sizeof(buffer),
                 "Calibration coefficient %i of LHS (%1.8E) doesnt match RHS (%1.8E)",
                 int(i), a, b );
        throw runtime_error( buffer );
      }
    }
  }//if( energy_calibration_model_ == rhs.energy_calibration_model_ )
  
  if( lhs.deviation_pairs_.size() != rhs.deviation_pairs_.size() )
    throw runtime_error( "Number of deviation pairs of LHS and RHS dont match" );
  
  for( size_t i = 0; i < lhs.deviation_pairs_.size(); ++i )
  {
    if( fabs(lhs.deviation_pairs_[i].first - rhs.deviation_pairs_[i].first) > 0.001 )
    {
      snprintf( buffer, sizeof(buffer),
               "Energy of deviation pair %i of LHS (%1.8E) doesnt match RHS (%1.8E)",
               int(i), lhs.deviation_pairs_[i].first,
               rhs.deviation_pairs_[i].first );
      throw runtime_error( buffer );
    }
    
    if( fabs(lhs.deviation_pairs_[i].second - rhs.deviation_pairs_[i].second) > 0.001 )
    {
      snprintf( buffer, sizeof(buffer),
               "Offset of deviation pair %i of LHS (%1.8E) doesnt match RHS (%1.8E)",
               int(i), lhs.deviation_pairs_[i].second,
               rhs.deviation_pairs_[i].second );
      throw runtime_error( buffer );
    }
  }//for( size_t i = 0; i < lhs.deviation_pairs_.size(); ++i )
  
  if( (!lhs.channel_energies_) != (!rhs.channel_energies_) )
  {
    snprintf( buffer, sizeof(buffer), "Channel energies avaialblity for LHS (%s)"
             " doesnt match RHS (%s)",
             (!lhs.channel_energies_?"missing":"available"),
             (!rhs.channel_energies_?"missing":"available") );
    throw runtime_error(buffer);
  }
  
  if( !!lhs.channel_energies_ )
  {
    if( lhs.channel_energies_->size() != rhs.channel_energies_->size() )
    {
      snprintf( buffer, sizeof(buffer),
               "Number of channel energies of LHS (%i) doesnt match RHS (%i)",
               int(lhs.channel_energies_->size()),
               int(rhs.channel_energies_->size()) );
      throw runtime_error( buffer );
    }
    
    for( size_t i = 0; i < lhs.channel_energies_->size(); ++i )
    {
      const float lhsenergy = lhs.channel_energies_->at(i);
      const float rhsenergy = rhs.channel_energies_->at(i);
      const float diff = fabs(lhsenergy - rhsenergy );
      if( diff > 0.00001*std::max(fabs(lhsenergy), fabs(rhsenergy)) && diff > 0.001 )
      {
        snprintf( buffer, sizeof(buffer),
                 "Energy of channel %i of LHS (%1.8E) doesnt match RHS (%1.8E)",
                 int(i), lhs.channel_energies_->at(i),
                 rhs.channel_energies_->at(i) );
        throw runtime_error( buffer );
      }
    }
  }//if( !!channel_energies_ )
  
  if( (!lhs.gamma_counts_) != (!rhs.gamma_counts_) )
  {
    snprintf( buffer, sizeof(buffer), "Gamma counts avaialblity for LHS (%s)"
             " doesnt match RHS (%s)",
             (!lhs.gamma_counts_?"missing":"available"),
             (!rhs.gamma_counts_?"missing":"available") );
    throw runtime_error(buffer);
  }
  
  if( !!lhs.gamma_counts_ )
  {
    if( lhs.gamma_counts_->size() != rhs.gamma_counts_->size() )
    {
      snprintf( buffer, sizeof(buffer),
               "Number of gamma channels of LHS (%i) doesnt match RHS (%i)",
               int(lhs.gamma_counts_->size()),
               int(rhs.gamma_counts_->size()) );
      throw runtime_error( buffer );
    }
    
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
        
        snprintf( buffer, sizeof(buffer),
                 "Counts in gamma channel %i of LHS (%1.8E) doesnt match RHS (%1.8E)",
                 int(i), lhs.gamma_counts_->at(i),
                 rhs.gamma_counts_->at(i) );
        throw runtime_error( buffer );
      }
    }
  }//if( !!channel_energies_ )
  
  if( lhs.neutron_counts_.size() != rhs.neutron_counts_.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "Number of neutron channels of LHS (%i) doesnt match RHS (%i)",
             int(lhs.neutron_counts_.size()),
             int(rhs.neutron_counts_.size()) );
    throw runtime_error( buffer );
  }
  
  for( size_t i = 0; i < lhs.neutron_counts_.size(); ++i )
  {
    if( fabs(lhs.neutron_counts_[i] - rhs.neutron_counts_[i] ) > (0.0001*std::max(fabs(lhs.neutron_counts_[i]),fabs(rhs.neutron_counts_[i]))) )
    {
      snprintf( buffer, sizeof(buffer),
               "Counts in neutron channel %i of LHS (%1.8E) doesnt match RHS (%1.8E)",
               int(i), lhs.neutron_counts_[i],
               rhs.neutron_counts_[i] );
      throw runtime_error( buffer );
    }
  }
  
  if( fabs(lhs.latitude_ - rhs.latitude_) > 0.00001 )
  {
    snprintf( buffer, sizeof(buffer),
             "Latitude of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.latitude_, rhs.latitude_ );
    throw runtime_error( buffer );
  }
  
  if( fabs(lhs.longitude_ - rhs.longitude_) > 0.00001 )
  {
    snprintf( buffer, sizeof(buffer),
             "Longitude of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.longitude_, rhs.longitude_ );
    throw runtime_error( buffer );
  }

  if( lhs.position_time_ != rhs.position_time_ )
    throw runtime_error( "Position time for LHS ("
                        + SpecUtils::to_iso_string(lhs.position_time_) + ") doesnt match RHS ("
                        + SpecUtils::to_iso_string(rhs.position_time_) + ")" );

  if( lhs.title_ != rhs.title_ )
    throw runtime_error( "Title for LHS ('" + lhs.title_
                        + "') doesnt match RHS ('" + rhs.title_ + "')" );
}//void equalEnough( const Measurement &lhs, const Measurement &rhs )


void SpecFile::equalEnough( const SpecFile &lhs,
                                   const SpecFile &rhs )
{
  std::lock( lhs.mutex_, rhs.mutex_ );
  std::unique_lock<std::recursive_mutex> lhs_lock( lhs.mutex_, std::adopt_lock_t() );
  std::unique_lock<std::recursive_mutex> rhs_lock( rhs.mutex_, std::adopt_lock_t() );

  char buffer[8*1024];
  
  const double live_time_diff = fabs( double(lhs.gamma_live_time_) - double(rhs.gamma_live_time_));
  if( (live_time_diff > (std::max(lhs.gamma_live_time_,rhs.gamma_live_time_)*1.0E-5)) && (live_time_diff > 1.0E-3) )
  {
    snprintf( buffer, sizeof(buffer),
              "SpecFile: Live time of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.gamma_live_time_, rhs.gamma_live_time_ );
    throw runtime_error( buffer );
  }
  
  
  const double real_time_diff = fabs( double(lhs.gamma_real_time_) - double(rhs.gamma_real_time_));
  if( (real_time_diff > (std::max(lhs.gamma_real_time_,rhs.gamma_real_time_)*1.0E-5)) && (real_time_diff > 1.0E-3) )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Real time of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.gamma_real_time_, rhs.gamma_real_time_ );
    throw runtime_error( buffer );
  }
  
  const double gamma_sum_diff = fabs(lhs.gamma_count_sum_ - rhs.gamma_count_sum_);
  const double max_gamma_sum = std::max(fabs(lhs.gamma_count_sum_), fabs(rhs.gamma_count_sum_));
  if( gamma_sum_diff > 0.1 || gamma_sum_diff > 1.0E-6*max_gamma_sum )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Gamma sum of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.gamma_count_sum_, rhs.gamma_count_sum_ );
    throw runtime_error( buffer );
  }
  
  if( fabs(lhs.neutron_counts_sum_ - rhs.neutron_counts_sum_) > 0.01 )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Neutron sum of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.neutron_counts_sum_, rhs.neutron_counts_sum_ );
    throw runtime_error( buffer );
  }
  
  if( lhs.filename_ != rhs.filename_ )
    throw runtime_error( "SpecFile: Filename of LHS (" + lhs.filename_
                         + ") doenst match RHS (" + rhs.filename_ + ")" );
  
  if( lhs.detector_names_.size() != rhs.detector_names_.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Number of detector names of LHS (%i) doesnt match RHS (%i)",
             int(lhs.detector_names_.size()), int(rhs.detector_names_.size()) );
    throw runtime_error( buffer );
  }
  
  const set<string> lhsnames( lhs.detector_names_.begin(),
                              lhs.detector_names_.end() );
  const set<string> rhsnames( rhs.detector_names_.begin(),
                              rhs.detector_names_.end() );
  
  if( lhsnames != rhsnames )
    throw runtime_error( "SpecFile: Detector names do not match for LHS and RHS" );
 
  if( lhs.detector_numbers_.size() != rhs.detector_numbers_.size()
      || lhs.detector_numbers_.size() != lhs.detector_names_.size() )
    throw runtime_error( "SpecFile: Inproper number of detector numbers - wtf" );
  
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
  
  
  if( lhs.neutron_detector_names_.size() != rhs.neutron_detector_names_.size() )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Number of neutron detector names of LHS (%i) doesnt match RHS (%i)",
             int(lhs.neutron_detector_names_.size()),
             int(rhs.neutron_detector_names_.size()) );
    throw runtime_error( buffer );
  }
  
  const set<string> nlhsnames( lhs.neutron_detector_names_.begin(),
                               lhs.neutron_detector_names_.end() );
  const set<string> nrhsnames( rhs.neutron_detector_names_.begin(),
                               rhs.neutron_detector_names_.end() );
  
  if( nlhsnames != nrhsnames )
    throw runtime_error( "SpecFile: Neutron detector names dont match for LHS and RHS" );

  
  if( lhs.lane_number_ != rhs.lane_number_ )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Lane number of LHS (%i) doesnt match RHS (%i)",
             lhs.lane_number_, rhs.lane_number_ );
    throw runtime_error( buffer );
  }
  
  if( lhs.measurement_location_name_ != rhs.measurement_location_name_ )
    throw runtime_error( "SpecFile: Measurment location name of LHS ('"
                         + lhs.measurement_location_name_
                         + "') doesnt match RHS ('"
                         + rhs.measurement_location_name_ + "')" );
  
  if( lhs.inspection_ != rhs.inspection_ )
    throw runtime_error( "SpecFile: Inspection of LHS ('" + lhs.inspection_
                        + "') doesnt match RHS ('" + rhs.inspection_ + "')" );

  string leftoperator = lhs.measurment_operator_;
  string rightoperator = rhs.measurment_operator_;
  ireplace_all( leftoperator, "\t", " " );
  ireplace_all( leftoperator, "  ", " " );
  trim( leftoperator );
  ireplace_all( rightoperator, "\t", " " );
  ireplace_all( rightoperator, "  ", " " );
  trim( rightoperator );
  
  if( leftoperator != rightoperator )
    throw runtime_error( "SpecFile: Measurment operator of LHS ('"
                         + lhs.measurment_operator_ + "') doesnt match RHS ('"
                         + rhs.measurment_operator_ + ")" );

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
    throw runtime_error( buffer );
  }
  
  if( lhs.sample_numbers_ != rhs.sample_numbers_ )
  {
    stringstream lhssamples, rhssamples;
    for( auto sample : lhs.sample_numbers_ )
      lhssamples << (sample==(*lhs.sample_numbers_.begin()) ? "":",") << sample;
    for( auto sample : rhs.sample_numbers_ )
      rhssamples << (sample== (*rhs.sample_numbers_.begin()) ? "":",") << sample;
    throw runtime_error( "SpecFile: Sample numbers of RHS (" + rhssamples.str()
                         + ") and LHS (" + lhssamples.str() + ") doent match" );
  }
  
  if( lhs.detector_type_ != rhs.detector_type_ )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: LHS detector type (%i) doesnt match RHS (%i)",
             int(lhs.detector_type_), int(rhs.detector_type_) );
    throw runtime_error( buffer );
  }
  
  string lhsinst = convert_n42_instrument_type_from_2006_to_2012( lhs.instrument_type_ );
  string rhsinst = convert_n42_instrument_type_from_2006_to_2012( rhs.instrument_type_ );
  if( lhsinst != rhsinst )
  {
    throw runtime_error( "SpecFile: Instrument type of LHS ('" + lhs.instrument_type_
                    + "') doesnt match RHS ('" + rhs.instrument_type_ + "')" );
  }
  
  if( lhs.manufacturer_ != rhs.manufacturer_ )
    throw runtime_error( "SpecFile: Manufacturer of LHS ('" + lhs.manufacturer_
                        + "') doesnt match RHS ('" + rhs.manufacturer_ + "')" );
  
  if( lhs.instrument_model_ != rhs.instrument_model_ )
    throw runtime_error( "SpecFile: Instrument model of LHS ('" + lhs.instrument_model_
                    + "') doesnt match RHS ('" + rhs.instrument_model_ + "')" );
  
  if( lhs.instrument_id_ != rhs.instrument_id_ )
    throw runtime_error( "SpecFile: Instrument ID model of LHS ('" + lhs.instrument_id_
                       + "') doesnt match RHS ('" + rhs.instrument_id_ + "')" );
  
  
  if( fabs(lhs.mean_latitude_ - rhs.mean_latitude_) > 0.000001 )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Mean latitude of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.mean_latitude_, rhs.mean_latitude_ );
    throw runtime_error( buffer );
  }

  if( fabs(lhs.mean_longitude_ - rhs.mean_longitude_) > 0.000001 )
  {
    snprintf( buffer, sizeof(buffer),
             "SpecFile: Mean longitude of LHS (%1.8E) doesnt match RHS (%1.8E)",
             lhs.mean_longitude_, rhs.mean_longitude_ );
    throw runtime_error( buffer );
  }

  if( lhs.properties_flags_ != rhs.properties_flags_ )
  {
    string failingBits;
    auto testBitEqual = [&lhs,&rhs,&failingBits]( MeasurmentPorperties p, const string label ) {
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
    throw runtime_error( buffer );
  }
  
  for( const int sample : lhs.sample_numbers_ )
  {
    for( const string detname : lhs.detector_names_ )
    {
      std::shared_ptr<const Measurement> lhsptr = lhs.measurement( sample, detname );
      std::shared_ptr<const Measurement> rhsptr = rhs.measurement( sample, detname );
      
      if( (!lhsptr) != (!rhsptr) )
      {
        snprintf( buffer, sizeof(buffer), "SpecFile: Measurment avaialblity for LHS (%s)"
                 " doesnt match RHS (%s) for sample %i and detector name %s",
              (!lhsptr?"missing":"available"), (!rhsptr?"missing":"available"),
              sample, detname.c_str() );
        throw runtime_error(buffer);
      }
      
      if( !lhsptr )
        continue;
      
      try
      {
        Measurement::equalEnough( *lhsptr, *rhsptr );
      }catch( std::exception &e )
      {
        snprintf( buffer, sizeof(buffer), "SpecFile: Sample %i, Detector name %s: %s",
                  sample, detname.c_str(), e.what() );
        throw runtime_error( buffer );
      }
    }//for( const int detnum : lhs.detector_numbers_ )
  }//for( const int sample : lhs.sample_numbers_ )
  
  if( (!lhs.detectors_analysis_) != (!rhs.detectors_analysis_) )
  {
    snprintf( buffer, sizeof(buffer), "SpecFile: Detector analysis avaialblity for LHS (%s)"
             " doesnt match RHS (%s)",
             (!lhs.detectors_analysis_?"missing":"available"),
             (!rhs.detectors_analysis_?"missing":"available") );
    throw runtime_error(buffer);
  }
  
  vector<string> nlhsremarkss, nrhsremarkss;
  for( string r : lhs.remarks_ )
  {
    while( r.find("  ") != string::npos )
      ireplace_all( r, "  ", " " );
    
    if( !starts_with(r,"N42 file created by")
        && !starts_with(r,"N42 file created by") )
      nlhsremarkss.push_back( r );
  }
  
  for( string r : rhs.remarks_ )
  {
    while( r.find("  ") != string::npos )
      ireplace_all( r, "  ", " " );
    
    if( !starts_with(r,"N42 file created by") )
      nrhsremarkss.push_back( r );
  }
  
  stable_sort( nlhsremarkss.begin(), nlhsremarkss.end() );
  stable_sort( nrhsremarkss.begin(), nrhsremarkss.end() );
  
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
    throw runtime_error( buffer );
#endif
  }
  
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
               int(i), nlhsremarkss[i].c_str(), nrhsremarkss[i].c_str() );
#if( REQUIRE_REMARKS_COMPARE )
      throw runtime_error( buffer );
#endif
    }
  }

  
  
  
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
    throw runtime_error( buffer );
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
  
  
  
  vector<pair<string,string> > lhscompvsn, rhscompvsn;
  
  
  for( size_t i = 0; i < lhs.component_versions_.size(); ++i )
  {
    const string &n = lhs.component_versions_[i].first;
    if( n != "InterSpec" && n!= "InterSpecN42Serialization" && n != "Software"
        && !SpecUtils::istarts_with(n, "Original Software") )
      lhscompvsn.push_back( lhs.component_versions_[i] );
  }
  
  for( size_t i = 0; i < rhs.component_versions_.size(); ++i )
  {
    const string &n = rhs.component_versions_[i].first;
    if( n != "InterSpec" && n!= "InterSpecN42Serialization" && n != "Software"
        && !SpecUtils::istarts_with(n, "Original Software") )
      rhscompvsn.push_back( rhs.component_versions_[i] );
  }
  
  
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
  
  
  //Make UUID last, since its sensitive to the other variables changing, and we
  //  want to report on those first to make fixing easier.
  if( !!lhs.detectors_analysis_ )
    DetectorAnalysis::equalEnough( *lhs.detectors_analysis_,
                                  *rhs.detectors_analysis_ );
  
  /*
#if( !defined(WIN32) )
  if( lhs.uuid_ != rhs.uuid_ )
    throw runtime_error( "SpecFile: UUID of LHS (" + lhs.uuid_
                        + ") doesnt match RHS (" + rhs.uuid_ + ")" );
#endif
  */
  
//  bool modified_;
//  bool modifiedSinceDecode_;
}//void equalEnough( const Measurement &lhs, const Measurement &rhs )
#endif //#if( PERFORM_DEVELOPER_CHECKS )


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
  neutron_detector_names_ = rhs.neutron_detector_names_;

  uuid_                   = rhs.uuid_;
  remarks_                = rhs.remarks_;
  parse_warnings_         = rhs.parse_warnings_;
  lane_number_             = rhs.lane_number_;
  measurement_location_name_ = rhs.measurement_location_name_;
  inspection_             = rhs.inspection_;
  measurment_operator_    = rhs.measurment_operator_;
  sample_numbers_         = rhs.sample_numbers_;
  sample_to_measurments_  = rhs.sample_to_measurments_;
  detector_type_          = rhs.detector_type_;
  instrument_type_        = rhs.instrument_type_;
  manufacturer_           = rhs.manufacturer_;
  instrument_model_       = rhs.instrument_model_;
  instrument_id_          = rhs.instrument_id_;
  component_versions_     = rhs.component_versions_;
  mean_latitude_          = rhs.mean_latitude_;
  mean_longitude_         = rhs.mean_longitude_;
  properties_flags_       = rhs.properties_flags_;
  
  modified_               = rhs.modified_;
  modifiedSinceDecode_    = rhs.modifiedSinceDecode_;
  
  measurements_.clear();
  for( size_t i = 0; i < rhs.measurements_.size(); ++i )
  {
    std::shared_ptr<Measurement> ptr( new Measurement( *rhs.measurements_[i] ) );
    measurements_.push_back( ptr );
  }

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
  neutron_counts_sum_ = rhs.neutron_counts_sum_;
  speed_ = rhs.speed_;
  detector_name_ = rhs.detector_name_;
  detector_number_ = rhs.detector_number_;
  detector_description_ = rhs.detector_description_;
  quality_status_ = rhs.quality_status_;

  source_type_ = rhs.source_type_;
  energy_calibration_model_ = rhs.energy_calibration_model_;

  remarks_ = rhs.remarks_;
  start_time_ = rhs.start_time_;

  calibration_coeffs_ = rhs.calibration_coeffs_;
  deviation_pairs_ = rhs.deviation_pairs_;

  channel_energies_ = rhs.channel_energies_;
  gamma_counts_ = rhs.gamma_counts_;

//  if( rhs.gamma_counts_ )
//    gamma_counts_.reset( new vector<float>( *rhs.gamma_counts_ ) );
//  else
//    gamma_counts_ = std::make_shared<vector<float>>();

  neutron_counts_ = rhs.neutron_counts_;
  
  latitude_       = rhs.latitude_;
  longitude_      = rhs.longitude_;
  position_time_  = rhs.position_time_;

  title_ = rhs.title_;

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
      
    case ParserType::MicroRaider:
      success = load_micro_raider_file( filename );
    break;
      
    case ParserType::Auto:
    {
      bool triedPcf = false, triedSpc = false,
          triedNativeIcd1 = false, triedTxt = false, triedGR135 = false,
          triedChn = false, triedIaea = false, triedLsrmSpe = false,
          triedCnf = false, triedMps = false, triedSPM = false, triedMCA = false,
          triedOrtecLM = false, triedMicroRaider = false, triedAram = false,
          triedTka = false, triedMultiAct = false, triedPhd = false,
          triedLzs = false;
      
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
        
        if( orig_file_ending=="tka" )
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
        }//if( orig_file_ending=="xml" )
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
  
  const boost::posix_time::ptime &left = lhs->start_time();
  const boost::posix_time::ptime &right = rhs->start_time();
  
  if( left == right )
    return (lhs->source_type() < rhs->source_type());
  
  if( left.is_special() && !right.is_special() )
    return true;
  
  return (left < right);
}

void  SpecFile::set_sample_numbers_by_time_stamp()
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  if( measurements_.empty() )
    return;

  //If we're here, we need to (re) assign some sample numbers
  
  //This function can be really slow, so I'm experimenting with a faster way
  //  For a certain large file in debug mode, took from 9.058687s to 0.232338s
  //  (in release mode this was from 1.808691s to 0.053467s)
  //  Right now this faster method is only enabled for really large files with
  //  more than 500 measurments - this is because the faster method does not
  //  preserve existing sample numbers

  if( measurements_.size() > 500 )
  {
    vector<std::shared_ptr<Measurement>> sorted_meas, sorted_foreground, sorted_calibration, sorted_background;

    sorted_meas.reserve( measurements_.size() );

    for( auto &m : measurements_ )
    {
      if( !m )
        continue;
      
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

    sorted_meas.insert( end(sorted_meas), sorted_calibration.begin(), sorted_calibration.end() );
    sorted_meas.insert( end(sorted_meas), sorted_background.begin(), sorted_background.end() );
    sorted_meas.insert( end(sorted_meas), sorted_foreground.begin(), sorted_foreground.end() );
  
    
    int sample_num = 1;
    vector<std::shared_ptr<Measurement>>::iterator start, end, iter;
    for( start=end=sorted_meas.begin(); start != sorted_meas.end(); start=end )
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
    typedef map<boost::posix_time::ptime, SampleToMeasMap > TimeToSamplesMeasMap;
    
    TimeToSamplesMeasMap time_meas_map;

    for( const auto &m : measurements_ )
    {
      if( !m )
        continue;
      
      const int detnum = m->detector_number_;
      
      //If the time is invalid, we'll put this measurment after all the others.
      //If its an IntrinsicActivity, we'll put it before any of the others.
      if( m->source_type() == SourceType::IntrinsicActivity )
        time_meas_map[boost::posix_time::neg_infin][detnum].push_back( m );
      else if( m->start_time_.is_special() )
        time_meas_map[boost::posix_time::pos_infin][detnum].push_back( m );
      else
        time_meas_map[m->start_time_][detnum].push_back( m );
    }//for( auto &m : measurements_ )
    
    int sample = 1;
    
    for( TimeToSamplesMeasMap::value_type &t : time_meas_map )
    {
      SampleToMeasMap &measmap = t.second;
      
      //Make an attempt to sort the measurments in a reproducable, unique way
      //  because measurments wont be in the same order due to the decoding
      //  being multithreaded for some of the parsers.
//20150609: I think the multithreaded parsing has been fixed to yeild a
//  deterministic ordering always.  This only really matters for spectra where
//  the same start time is given for all records, or no start time at all is
//  given.  I think in these cases we want to assume the order that the
//  measurments were taken is the same as the order in the file.

      
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
  
  stable_sort( measurements_.begin(), measurements_.end(), &Measurement::compare_by_sample_det_time );
}//void  set_sample_numbers_by_time_stamp()


bool SpecFile::has_unique_sample_and_detector_numbers() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  //First we will check that the comnination of sample numbers and detector
  //  numbers is unique, and also that all measurments with the same sample
  //  number have the same time stamp.  If we pass both of thers conditions,
  //  then we'll return since there is no need re-assign sample numbers.
  std::map<int, vector<int> > sampleNumsToSamples;
  std::map<int,std::set<boost::posix_time::ptime> > sampleToTimes;
  const size_t nmeas = measurements_.size();
  for( size_t i = 0; i < nmeas; ++i )
  {
    const std::shared_ptr<Measurement> &m = measurements_[i];
    vector<int> &meass = sampleNumsToSamples[m->sample_number_];
    
    if( std::find(meass.begin(),meass.end(),m->detector_number_) != meass.end() )
      return false;
    
    meass.push_back( m->detector_number_ );
    
    std::set<boost::posix_time::ptime> &timesSet = sampleToTimes[m->sample_number_];
    if( !m->start_time_.is_special() )
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
    stable_sort( measurements_.begin(), measurements_.end(), &Measurement::compare_by_sample_det_time );
  }else
  {
    set_sample_numbers_by_time_stamp();
  }
  
  //XXX - TODO should validate this a little further and check performance
  //      impact!  Also, should be proactive about not needing to hit the
  //      expensive "fix" below.
  //
  //Here we will check the first two sample number, and if they are '1' and '2'
  //  repectively, we will not do anything.  If first sample is not 1, but
  //  second sample is 2, we will change first sample to 1.  Otherwise if
  //  first two sample numbers isnt {1,2}, we will change all sample numbers to
  //  start at 1 and increase continuosly by 1.
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
      if( m->sample_number_ <= 0 )
        m->sample_number_ = 1;
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
//  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  set<string> det_names;

  for( const auto &meas : measurements_ )
    det_names.insert( meas->detector_name_ );

  return det_names;
}//set<string> find_detector_names() const


void SpecFile::cleanup_after_load( const unsigned int flags )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const bool rebinToCommonBinning = (flags & RebinToCommonBinning);
  
  
  //When loading the example passthrough N42 file, this function
  //  take about 60% of the parse time - due almost entirely to
  //  SpecFile::rebin_by_polunomial_eqn
  try
  {
    set<string> gamma_detector_names; //can be gamma+nutron
    const set<string> det_names = find_detector_names();
    
    int nGpsCoords = 0;
    mean_latitude_ = mean_longitude_ = 0.0;
    
    properties_flags_ = 0x0;
    
    vector<string> names( det_names.begin(), det_names.end() );
    typedef map<int,string> IntStrMap;
    IntStrMap num_to_name_map;
    set<string> neut_det_names;  //If a measurement contains neutrons at all, will be added to this.
    vector<string>::const_iterator namepos;
    
    for( size_t meas_index = 0; meas_index < measurements_.size(); ++meas_index )
    {
      std::shared_ptr<Measurement> &meas = measurements_[meas_index];
      
      namepos = find( names.begin(), names.end(), meas->detector_name_ );
      if( namepos != names.end() )
      {
        meas->detector_number_ = static_cast<int>( namepos - names.begin() );
        num_to_name_map[meas->detector_number_] = meas->detector_name_;
      }else
        cerr << "Couldnt find detector '" << meas->detector_name_ << "'!" << endl;
      
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
      
      if( meas->contained_neutron_ )
        neut_det_names.insert( meas->detector_name_ );
      
      //make sure the file didnt just have all zeros for the linear and
      //  higher calibration coefficients
      bool allZeros = true, invalid_calib = false;
      for( size_t i = 1; i < meas->calibration_coeffs_.size(); ++i )
        allZeros &= (fabs(meas->calibration_coeffs_[i])<1.0E-14);
      
      //Do a basic sanity check of is the calibration is reasonable.
      if( !allZeros && meas->gamma_counts_ && meas->gamma_counts_->size() )
      {
        switch( meas->energy_calibration_model_ )
        {
          case SpecUtils::EnergyCalType::Polynomial:
          case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
            if( meas->calibration_coeffs_.size() < 2
                || fabs(meas->calibration_coeffs_[0])>300.0  //300 is arbitrary, but I have seen -160 in data.
                || fabs(meas->calibration_coeffs_[1])>450.0  //450 is arbitrary, lets 7 channels span 3000 keV
                || (meas->calibration_coeffs_.size()==2 && meas->calibration_coeffs_[1]<=FLT_EPSILON)
                || (meas->calibration_coeffs_.size()>=3 && meas->calibration_coeffs_[1]<=FLT_EPSILON
                      && meas->calibration_coeffs_[2]<=FLT_EPSILON)
               //TODO: Could aso check that the derivative of energy polynomial does not go negative by meas->gamma_counts_->size()
               //      Could call calibration_is_valid(...)
               )
            {
              allZeros = true;
              invalid_calib = true;
            }
            break;
            
          case SpecUtils::EnergyCalType::FullRangeFraction:
            if( meas->calibration_coeffs_.size() < 2
               || fabs(meas->calibration_coeffs_[0]) > 1000.0f
               || meas->calibration_coeffs_[1] < (FLT_EPSILON*meas->gamma_counts_->size()) )
            {
              allZeros = true;
              invalid_calib = true;
            }
            break;
            
          case SpecUtils::EnergyCalType::LowerChannelEdge:
            //should check that at least as many energies where provided as
            //  meas->gamma_counts_->size()
            break;
          
          case SpecUtils::EnergyCalType::InvalidEquationType:
            //Nothing to check here.
            break;
        }//switch( meas->energy_calibration_model_ )
      }//if( !allZeros && meas->gamma_counts_ && meas->gamma_counts_->size() )
      
      if( allZeros && (meas->energy_calibration_model_==SpecUtils::EnergyCalType::Polynomial
           || meas->energy_calibration_model_==SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial
           || meas->energy_calibration_model_==SpecUtils::EnergyCalType::FullRangeFraction)
          && meas->gamma_counts_ && meas->gamma_counts_->size() )
      {
        if( invalid_calib )
        {
          stringstream remark;
          remark << "Energy calibration provided by detector was invalid {";
          for( size_t i = 0; i < meas->calibration_coeffs_.size(); ++i )
            remark << (i?", ":"") << meas->calibration_coeffs_[i];
          remark << "}";
          meas->remarks_.push_back( remark.str() );
        }//if( invalid_calib )
        
        meas->energy_calibration_model_ = SpecUtils::EnergyCalType::InvalidEquationType;
        meas->calibration_coeffs_.clear();
        meas->deviation_pairs_.clear();
        meas->channel_energies_.reset();
      }//if( we dont have a calibration...)
      
      if( allZeros && meas->gamma_counts_ && meas->gamma_counts_->size() )
      {
        //TODO: look to see if we can grab a calibration for this same detector from another sample.
        //std::shared_ptr<Measurement> &meas = measurements_[meas_index];
        //invalid_calib
      }//if( allZeros )
      
      if( meas->has_gps_info() )
      {
        //        a lat long of (0 0) probably isnt a valid GPS coordinate
        if( (fabs(meas->latitude_) < 1.0E-6)
           && (fabs(meas->longitude_) < 1.0E-6) )
        {
          meas->latitude_ = meas->longitude_ = -999.9;
          meas->position_time_ = boost::posix_time::not_a_date_time;
        }else
        {
          ++nGpsCoords;
          mean_latitude_ += meas->latitude();
          mean_longitude_ += meas->longitude();
        }
      }else if( !meas->position_time_.is_special() )
      {
        meas->position_time_ = boost::posix_time::not_a_date_time;
      }
      
      meas->contained_neutron_ |= (meas->neutron_counts_sum_>0.0
                                   || !meas->neutron_counts_.empty());
    }//for( auto &meas : measurements_ )
    
    
    if( nGpsCoords == 0
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
    
    if( flags & DontChangeOrReorderSamples )
    {
      if( !has_unique_sample_and_detector_numbers() )
        properties_flags_ |= kNotUniqueSampleDetectorNumbers;
      
      for( size_t i = 1; i < measurements_.size(); ++i )
      {
        if( measurements_[i-1]->start_time_.is_special()
           || measurements_[i]->start_time_.is_special() )
          continue;
        
        if( measurements_[i-1]->start_time_ > measurements_[i]->start_time_ )
          properties_flags_ |= kNotTimeSortedOrder;
        
        if( !Measurement::compare_by_sample_det_time(measurements_[i-1],measurements_[i]) )
          properties_flags_ |= kNotSampleDetectorTimeSorted;
      }//for( size_t i = 1; i < measurements_.size(); ++i )
    }else
    {
      ensure_unique_sample_numbers();
      
      //ToDo: we can probably add this next bit of logic to another loop so it
      //      isnt so expensive.
      for( size_t i = 1; i < measurements_.size(); ++i )
      {
        if( !measurements_[i-1]->start_time_.is_special()
           && !measurements_[i]->start_time_.is_special()
           && (measurements_[i-1]->start_time_ > measurements_[i]->start_time_) )
        {
          properties_flags_ |= kNotTimeSortedOrder;
          break;
        }
      }//for( size_t i = 1; i < measurements_.size(); ++i )
      
    }//if( flags & DontChangeOrReorderSamples ) / else
    
    
    detector_numbers_.clear();
    detector_names_.clear();
    neutron_detector_names_.clear();
    
    for( const IntStrMap::value_type &t : num_to_name_map )
    {
      detector_numbers_.push_back( t.first );
      detector_names_.push_back( t.second );
    }//for( const IntStrMap::value_type &t : num_to_name_map )
    
    neutron_detector_names_.insert( neutron_detector_names_.end(), neut_det_names.begin(), neut_det_names.end() );
    
    //Make a set of all unique calibrations, so we can later allow Measurements
    //  with the same calibration share a vector<float> of bin energies.
    set<MeasurementCalibInfo> calib_infos_set;
    
    //Build up a mapping from detector names, to _a_ calibration for it, so if
    //  any spectra for that detector doesnt have a calibration associated with
    //  it, we can assign it this one.  Note: the MeasurementCalibInfo wont be
    //  used to assign calibration from, it will be used to index
    //  calib_infos_set.
    map<string,MeasurementCalibInfo> detname_to_calib_map;
    
    int numNeutronAndGamma = 0, numWithGammas = 0, numWithNeutrons = 0;
    bool neutronMeasDoNotHaveGamma = true, haveNeutrons = false, haveGammas = false;
    
    //Need to go through and set binning on spectrums here
    for( auto &meas : measurements_ )
    {
      const bool thisMeasHasGamma = (meas->gamma_counts_ && !meas->gamma_counts_->empty());
      haveGammas = (haveGammas || thisMeasHasGamma);
      haveNeutrons = (haveNeutrons || meas->contained_neutron_);
      
      numWithGammas += thisMeasHasGamma;
      numWithNeutrons += meas->contained_neutron_;
      numNeutronAndGamma += (meas->contained_neutron_ && thisMeasHasGamma);
      
      if( meas->contained_neutron_ && thisMeasHasGamma )
        neutronMeasDoNotHaveGamma = false;
      
      if( !meas->gamma_counts_ || meas->gamma_counts_->empty() )
        continue;
      
      MeasurementCalibInfo info( meas );
      
      if( info.coefficients.size()
         || (info.binning && info.binning->size()) )
      {
        info.fill_binning();
        calib_infos_set.insert( info );
        
        const string &name = meas->detector_name_;
        //        if( detname_to_calib_map.find(name) != detname_to_calib_map.end()
        //            && !(detname_to_calib_map[name] == info) )
        //          cerr << SRC_LOCATION << "\n\tWarning same detector " << name
        //               << " has multiple calibrations" << endl;
        detname_to_calib_map[name] = info;
      }//if( a non-empty info ) / else
    }//for( auto &meas : measurements_ )
    
    
    //If none of the Measurements that have neutrons have gammas, then lets see
    //  if it makes sense to add the neutrons to the gamma Measurement
    //if( numNeutronAndGamma < (numWithGammas/10) )
    if( haveNeutrons && haveGammas && neutronMeasDoNotHaveGamma )
    {
      merge_neutron_meas_into_gamma_meas();
    }
    
    int n_times_guess_cal = 0;
    size_t nbins = 0;
    bool all_same_num_bins = true;
    
    
    //In order to figure out if a measurment is passthrough, we'll use these
    //  two variables.
    int pt_num_items = 0;
    float pt_averageRealTime = 0;
    
    //ensure_unique_sample_numbers(); has already been called a
    sample_numbers_.clear();
    sample_to_measurments_.clear();
    
    typedef std::pair<boost::posix_time::ptime,float> StartAndRealTime;
    typedef map<int, StartAndRealTime > SampleToTimeInfoMap;
    SampleToTimeInfoMap samplenum_to_starttime;
    
    const size_t nmeas = measurements_.size();
    size_t ngamma_meas = 0;
    
    for( size_t measn = 0; measn < nmeas; ++measn )
    {
      std::shared_ptr<Measurement> &meas = measurements_[measn];
      sample_numbers_.insert( meas->sample_number_ );
      sample_to_measurments_[meas->sample_number_].push_back( measn );
      
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
         && meas->real_time() < 15.0 )  //20181108: some search systems will take one spectra every like ~10 seconds
      {
        ++pt_num_items;
        pt_averageRealTime += meas->real_time_;
        
        if( !meas->start_time().is_special() )
        {
          const int samplenum = meas->sample_number();
          const boost::posix_time::ptime &st = meas->start_time();
          const float rt = meas->real_time();
          
          SampleToTimeInfoMap::iterator pos = samplenum_to_starttime.find( samplenum );
          if( pos == samplenum_to_starttime.end() )
            pos = samplenum_to_starttime.insert( make_pair(samplenum, make_pair(st,rt)) ).first;
          pos->second.second = max( rt, pos->second.second );
        }
        
        
      }//if( a candidate for a passthrough spectrum )
      
      MeasurementCalibInfo thisinfo( meas );
      set<MeasurementCalibInfo>::const_iterator pos = calib_infos_set.find(thisinfo); //
      
      //If this Measurement doesnt have a calibration, see if another one for
      //  the same detector exists.
      if( pos == calib_infos_set.end() )
        pos = calib_infos_set.find(detname_to_calib_map[meas->detector_name_]);
      
      try
      {
        if( pos == calib_infos_set.end() )
          throw runtime_error( "" );
      
        //pos->fill_binning();
        meas->channel_energies_ = pos->binning;
        meas->calibration_coeffs_ = pos->coefficients;
        meas->deviation_pairs_ = pos->deviation_pairs_;
        meas->energy_calibration_model_ = pos->equation_type;
        
        if( !meas->channel_energies_ )
          throw runtime_error( "" );
          
        //Force calibration_coeffs_ to free the memory for LowerChannelEdge
        if( pos->equation_type == SpecUtils::EnergyCalType::LowerChannelEdge )
          vector<float>().swap( meas->calibration_coeffs_ );
      }catch( std::exception & )
      {
        if( meas->gamma_counts_ && !meas->gamma_counts_->empty())
        {
          ++n_times_guess_cal;
          meas->energy_calibration_model_ = SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial;
          meas->calibration_coeffs_.resize( 2 );
          meas->calibration_coeffs_[0] = 0.0f;
          meas->calibration_coeffs_[1] = 3000.0f / std::max(meas->gamma_counts_->size()-1, size_t(1));
          MeasurementCalibInfo info( meas );
        
          pos = calib_infos_set.find(info);
        
          if( pos == calib_infos_set.end() )
          {
            info.fill_binning();
            calib_infos_set.insert( info );
            pos = calib_infos_set.find(info);
          }//if( pos == calib_infos_set.end() )
        
          if( pos != calib_infos_set.end() )
          {
            meas->channel_energies_ = pos->binning;
          }else
          {
            //shouldnt ever get here! - but JIC
            meas->channel_energies_ = info.binning;
          }//if( pos != calib_infos_set.end() )  e;se
        }//if( meas->gamma_counts_ && meas->gamma_counts_->size() )
      }//try( to fill binning ) / catch
    }//for( auto &meas : measurements_ )
    

    bool is_passthrough = true;
    
    if( sample_numbers_.size() < 5 || detector_numbers_.empty() )
      is_passthrough = false;
    
    if( pt_averageRealTime <= 0.00000001 )
      is_passthrough = false;
    
    pt_averageRealTime /= (pt_num_items ? pt_num_items : 1);
    
    //In principle should check that measurements were taken sequentially as well
    //is_passthrough = is_passthrough && ( (pt_num_items>5) && (pt_averageRealTime < 2.5) );
    is_passthrough = is_passthrough && ( (pt_num_items>5) && pt_num_items > static_cast<size_t>(0.75*ngamma_meas) );
    //is_passthrough = true;
    
    //Go through and verify the measurments are mostly sequential (one
    //  measurement right after the next), for at least the occupied/item/unknown
    //  portions of the file.  The background portion, as well as the transition
    //  between background/foreground/etc is a little harder to deal with since
    //  background may have gaps, or may have been taken a while before the item
    if( !is_passthrough && samplenum_to_starttime.size() > 20 )
    {
      int nnotadjacent = 0, nadjacent = 0;
      
      SampleToTimeInfoMap::const_iterator next = samplenum_to_starttime.begin(), iter;
      for( iter = next++; next != samplenum_to_starttime.end(); ++iter, ++next )
      {
        const boost::posix_time::ptime &st = iter->second.first;
        const boost::posix_time::ptime &next_st = next->second.first;
        
        const float rt = iter->second.second;
        const boost::posix_time::time_duration duration = boost::posix_time::microseconds( static_cast<int64_t>(rt*1.0E6) );
        
        boost::posix_time::time_duration diff = ((st + duration) - next_st);
        if( diff.is_negative() )
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
    
    //XXX - as of 20121130, the below code which makes sure all samples
    //      of passthrough data has bining information (as apposed to just first
    //      detector that some of the detector manufactureers do), has not been
    //      well tested - just kinda seems to work for SAIC detectors.
    if( is_passthrough )
    {
      const int first_sample_number = *sample_numbers_.begin();
      map<string,std::shared_ptr<Measurement>> det_meas_map;
      
      for( const auto &meas : measurements_ )
      {
        if( meas->sample_number() == first_sample_number
           && meas->gamma_counts_ && !meas->gamma_counts_->empty()
                                     && (!meas->calibration_coeffs_.empty()
                                         || (meas->channel_energies_ && !meas->channel_energies_->empty())) )
          det_meas_map[meas->detector_name_] = meas;
      }//for( const auto &meas : measurements_ )
      
      for( const auto &meas : measurements_ )
      {
        if( meas->sample_number() == first_sample_number )
          continue;
        
        const string &name = meas->detector_name_;
        if( meas->gamma_counts_ && !meas->gamma_counts_->empty()
            && det_meas_map.count(name) && meas->calibration_coeffs_.empty()
            && ( !meas->channel_energies_ || meas->channel_energies_->empty()
                 || (det_meas_map[name]->energy_calibration_model_ == SpecUtils::EnergyCalType::LowerChannelEdge
                     && meas->energy_calibration_model_==SpecUtils::EnergyCalType::InvalidEquationType )
               )
           )
        {
          if( !meas->channel_energies_ || meas->channel_energies_->empty())
          {
            meas->channel_energies_   = det_meas_map[name]->channel_energies_;
            meas->deviation_pairs_    = det_meas_map[name]->deviation_pairs_;
          }//if( !meas->channel_energies_ || !meas->channel_energies_->size() )
          
          meas->energy_calibration_model_  = det_meas_map[name]->energy_calibration_model_;
          meas->calibration_coeffs_ = det_meas_map[name]->calibration_coeffs_;
        }
      }//for( const std::shared_ptr<Measurement> &meas : measurements_ )
    }//if( is_passthrough )
    
    
    if( rebinToCommonBinning && all_same_num_bins && !measurements_.empty()
       && ((gamma_detector_names.size() > 1) || is_passthrough) )
    {
      properties_flags_ |= kHasCommonBinning;
      
      if( (calib_infos_set.size() > 1) )
      {
        properties_flags_ |= kRebinnedToCommonBinning;
        
        //If we have more than one gamma detector, than we have to move them
        //  to have a common energy binning, to display properly
        size_t nbin = 0;
        float min_energy = 99999.9f, max_energy = -99999.9f;
        for( const auto &meas : measurements_ )
        {
          nbin = max( nbin , meas->channel_energies_->size() );
          if(!meas->channel_energies_->empty())
          {
            min_energy = min( min_energy, meas->channel_energies_->front() );
            max_energy = max( max_energy, meas->channel_energies_->back() );
          }//if( meas->channel_energies_.size() )
        }//for( const auto &meas : measurements_ )
        
        size_t nbinShift = nbin - 1;
        const float channel_width = (max_energy - min_energy) /
        static_cast<float>( nbinShift );
        vector<float> poly_eqn( 2, 0.0 );
        poly_eqn[0] = min_energy;  // - 0.5*channel_width; // min_energy;
        poly_eqn[1] = channel_width;
        
        for( const auto &meas : measurements_ )
        {
          if( meas->gamma_counts_->size() > 16 )//dont rebin SAIC or LUDLUMS
          {
            rebin_by_eqn( poly_eqn, std::vector<std::pair<float,float>>(), SpecUtils::EnergyCalType::Polynomial );
            break;
          }//if( meas->gamma_counts_->size() > 16 )
        }//for( const auto &meas : measurements_ )
      }//if( calib_infos_set.size() > 1 )
    }else if( all_same_num_bins && !measurements_.empty() && calib_infos_set.size()==1 )
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
    
    recalc_total_counts();
    
#if( PERFORM_DEVELOPER_CHECKS )
    //Check to make sure all neutron detector names can be found in detector names
    {
      const vector<string>::const_iterator begindet = detector_names_.begin();
      const vector<string>::const_iterator enddet = detector_names_.end();
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
      }
    }
#endif
    
#if( PERFORM_DEVELOPER_CHECKS )
    static int ntest = 0;
    if( ntest++ < 10 )
      cerr << "Warning, testing rebingin ish" << endl;
    
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
    stringstream msg;
    msg << "From " << SRC_LOCATION << " caught error:\n\t" << e.what();
    throw runtime_error( msg.str() );
  }//try / catch
  
  modified_ = modifiedSinceDecode_ = false;
}//void cleanup_after_load()

    
    
    
void SpecFile::merge_neutron_meas_into_gamma_meas()
{
  //Check to make sure sample numbers arent whack (they havent been finally
  //  assigned yet) before doing the correction - all the files I've seen
  //  that need this correction already have sample numbers properly
  //  assigned
  bool bogusSampleNumbers = false;
  std::map<int, std::vector<std::shared_ptr<Measurement>> > sample_to_meass;
  for( const auto &m : measurements_ )
  {
    std::vector<std::shared_ptr<Measurement>> &mv = sample_to_meass[m->sample_number_];
    mv.push_back( m );
    if( mv.size() > detector_names_.size() )
    {
      bogusSampleNumbers = true;
#if( PERFORM_DEVELOPER_CHECKS )
      log_developer_error( __func__, "Found a file where neutron"
                          " and gammas are sperate measurements, but sample numbers not assigned." );
#endif //#if( PERFORM_DEVELOPER_CHECKS )
      break;
    }
  }//for( const auto &m : measurements_ )
  
  //If smaple numbers arent already properly assigned, bail
  if( bogusSampleNumbers )
    return;
  
  
  vector<string> gamma_only_dets = detector_names_;
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
      
#if( PERFORM_DEVELOPER_CHECKS )
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
  set<string> new_neut_det_names;
  set<string> new_all_det_names;
  vector<std::shared_ptr<Measurement>> meas_to_delete;
  
  for( size_t measindex = 0; measindex < measurements_.size(); ++measindex )
  {
    std::shared_ptr<Measurement> meas = measurements_[measindex];
    
    if( !meas->contained_neutron_ )
    {
      new_all_det_names.insert( meas->detector_name_ );
      continue;
    }
    
    
    if( meas->gamma_counts_ && meas->gamma_counts_->size() )
    {
#if( PERFORM_DEVELOPER_CHECKS )
      if( !gammas_we_added_neutron_to.count(measindex) )
        log_developer_error( __func__, "Found a nuetron detector Measurement that had gamma data - shouldnt have happened here." );
#endif  //PERFORM_DEVELOPER_CHECKS
      new_all_det_names.insert( meas->detector_name_ );
      new_neut_det_names.insert( meas->detector_name_ );
      continue;
    }
    
    //IF we are hear, this `meas` only contains neutron data.
    
    auto namepos = neutron_to_gamma_names.find(meas->detector_name_);
    if( namepos == end(neutron_to_gamma_names) )
    {
#if( PERFORM_DEVELOPER_CHECKS )
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
        
#if( PERFORM_DEVELOPER_CHECKS )
        if( gamma_names.size() != 1 && (meas->detector_name_ != gamma_name) )
        {
          stringstream errmsg;
          errmsg << "Found a nuetron detector Measurement (DetName='" << meas->detector_name_
                 << "', SampleNumber=" << meas->sample_number_
                 << ", StartTime=" << SpecUtils::to_iso_string(meas->start_time_)
                 << ") I couldnt find a gamma w/ DetName='"
                 << gamma_name << "' and SampleNumber=" << meas->sample_number_ << ".";
          log_developer_error( __func__, errmsg.str().c_str() );
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
      //      as start time.  Should also look into propogating detecor
      //      information and other quantities over.
      
      gamma_meas->contained_neutron_ = true;
      gamma_meas->neutron_counts_.insert( end(gamma_meas->neutron_counts_),
                                         begin(meas->neutron_counts_), end(meas->neutron_counts_) );
      gamma_meas->neutron_counts_sum_ += meas->neutron_counts_sum_;
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
  
  neutron_detector_names_.clear();
  neutron_detector_names_.insert( end(neutron_detector_names_), begin(new_neut_det_names), end(new_neut_det_names) );
  
  //Could map the detector names to Aa1, etc. here.
}//void merge_neutron_meas_into_gamma_meas()



void SpecFile::set_detector_type_from_other_info()
{
  using SpecUtils::contains;
  using SpecUtils::icontains;
  
  
  if( detector_type_ != DetectorType::Unknown )
    return;
  
  const string &model = instrument_model_;
//  const string &id = instrument_id_;
  
  if( icontains(model,"SAM")
      && (contains(model,"940") || icontains(model,"Eagle+")) )
  {
    if( icontains(model,"LaBr") )
      detector_type_ = DetectorType::Sam940LaBr3;
    else
      detector_type_ = DetectorType::Sam940;
    
    cerr << "ASAm940 model=" << model << endl;
    
    return;
  }
  
  if( icontains(model,"SAM") && contains(model,"945") )
  {
    //if( icontains(model,"LaBr") )
      //detector_type_ = Sam945LaBr3;
    //else
    detector_type_ = DetectorType::Sam945;
    return;
  }
  
  //Dont know what the 'ULCS' that some models have in their name is
  if( icontains(model,"identiFINDER") && icontains(model,"NG") )
  {
    detector_type_ = DetectorType::IdentiFinderNG;
    return;
  }
  
  if( icontains(model,"identiFINDER") && icontains(model,"LG") )
  {
    detector_type_ = DetectorType::IdentiFinderLaBr3;
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
#if(PERFORM_DEVELOPER_CHECKS)
      log_developer_error( __func__, ("Unrecognized RadEagle Model: " + model).c_str() );
#endif
    }
    
    //Make the modle human readable
    if( istarts_with(model, "RE ") )
      instrument_model_ = "RadEagle " + instrument_model_.substr(3);
    
    return;
  }//if( a rad eagle )
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
      && fabs(deep_gamma_sum - gamma_count_sum_) > 1.0E-7*max(deep_gamma_sum, gamma_count_sum_) )
  {
    char buffer[1024];
    snprintf( buffer, sizeof(buffer),
             "recalc_total_counts() found a discrepance for sum gammas depending"
            " on if a shallow or deep count was done: %9f for shallow, %9f for"
            " deep\n", gamma_count_sum_, deep_gamma_sum );
    log_developer_error( __func__, buffer );
  }
  
  if( fabs(deep_neutron_sum - neutron_counts_sum_) > 0.1 )
  {
    char buffer[1024];
    snprintf( buffer, sizeof(buffer),
              "recalc_total_counts() found a discrepance for sum nuetrons depending"
              " on if a shallow or deep count was done: %9f for shallow, %9f for"
              " deep\n", neutron_counts_sum_, deep_neutron_sum );
    log_developer_error( __func__, buffer );
  }
  
#endif //#if( PERFORM_DEVELOPER_CHECKS )
}//void recalc_total_counts()




std::string SpecFile::generate_psuedo_uuid() const
{
  std::size_t seed = 0;
  
  
  boost::hash_combine( seed, gamma_live_time_ );
  boost::hash_combine( seed, gamma_real_time_ );
  
  boost::hash_combine( seed, gamma_count_sum_ );
  boost::hash_combine( seed, neutron_counts_sum_ );
//  boost::hash_combine( seed, filename_ );
  boost::hash_combine( seed, detector_names_ );
//  boost::hash_combine( seed, detector_numbers_ );
  boost::hash_combine( seed, neutron_detector_names_ );
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
  boost::hash_combine( seed, measurment_operator_ );
  
  for( const std::shared_ptr<const Measurement> meas : measurements_ )
  {
    boost::hash_combine( seed, meas->live_time() );
    boost::hash_combine( seed, meas->real_time() );
    boost::hash_combine( seed, meas->gamma_count_sum() );
    boost::hash_combine( seed, meas->neutron_counts_sum() );
    
    if( SpecUtils::valid_latitude(meas->latitude_) )
      boost::hash_combine( seed, meas->latitude_ );
    if( SpecUtils::valid_longitude(meas->longitude_) )
      boost::hash_combine( seed, meas->longitude_ );
    //  boost::hash_combine( seed, position_time_ );
  }//for( const std::shared_ptr<const Measurement> meas : measurements_ )

  
//  std::set<int> sample_numbers_;
//  std::shared_ptr<const DetectorAnalysis> detectors_analysis_;
  
  string uuid;
  
  if(!measurements_.empty() && measurements_[0]
      && !measurements_[0]->start_time().is_special() )
    uuid = SpecUtils::to_iso_string( measurements_[0]->start_time() );
  else
    uuid = SpecUtils::to_iso_string( time_from_string( "1982-07-28 23:59:59" ) );
  
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
                              const std::set<int> &det_nums ) const
{
  try
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
    if( sample_nums.empty() )
      sample_nums = sample_numbers_;
  
    const size_t ndet = detector_numbers_.size();
    vector<bool> detectors( ndet, true );
    if( !det_nums.empty() )
    {
      for( size_t i = 0; i < ndet; ++i )
        detectors[i] = (det_nums.count(detector_numbers_[i]) != 0);
    }//if( det_nums.empty() )
  
  
    std::shared_ptr<Measurement> summed = sum_measurements( sample_nums, detectors );
  
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






void Measurement::popuplate_channel_energies_from_coeffs()
{
  if( !gamma_counts_ || gamma_counts_->empty() )
    throw runtime_error( "popuplate_channel_energies_from_coeffs():"
                         " no gamma counts" );
  
  if( calibration_coeffs_.size() < 2 )
    throw runtime_error( "popuplate_channel_energies_from_coeffs():"
                         " no calibration coefficients" );
  
  if( !!channel_energies_ && !channel_energies_->empty() )
    throw runtime_error( "popuplate_channel_energies_from_coeffs():"
                         " channel energies already populated" );
  
  const size_t nbin = gamma_counts_->size();
    
  switch( energy_calibration_model_ )
  {
    case SpecUtils::EnergyCalType::Polynomial:
    case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
      channel_energies_ = SpecUtils::polynomial_binning( calibration_coeffs_,
                                              nbin, deviation_pairs_ );
      break;
        
    case SpecUtils::EnergyCalType::FullRangeFraction:
      channel_energies_ = SpecUtils::fullrangefraction_binning( calibration_coeffs_,
                                                    nbin, deviation_pairs_ );
      break;
        
    case SpecUtils::EnergyCalType::LowerChannelEdge:
      channel_energies_.reset( new vector<float>(calibration_coeffs_) );
      break;
        
    case SpecUtils::EnergyCalType::InvalidEquationType:
      throw runtime_error( "popuplate_channel_energies_from_coeffs():"
                           " unknown equation type" );
      break;
  }//switch( meas->energy_calibration_model_ )
}//void popuplate_channel_energies_from_coeffs()



std::string SpecFile::determine_rad_detector_kind_code() const
{
  string det_kind = "Other";
  switch( detector_type_ )
  {
    case DetectorType::DetectiveUnknown:
    case DetectorType::DetectiveEx:
    case DetectorType::DetectiveEx100:
    case DetectorType::DetectiveEx200:
    case DetectorType::Falcon5000:
    case DetectorType::MicroDetective:
    case DetectorType::DetectiveX:
      det_kind = "HPGe";
      break;
      
    case DetectorType::Exploranium:
    case DetectorType::IdentiFinder:
    case DetectorType::IdentiFinderNG:
    case DetectorType::RadHunterNaI:
    case DetectorType::Rsi701:
    case DetectorType::Rsi705:
    case DetectorType::AvidRsi:
    case DetectorType::OrtecRadEagleNai:
    case DetectorType::Sam940:
    case DetectorType::Sam945:
      det_kind = "NaI";
      break;
      
    case DetectorType::IdentiFinderLaBr3:
    case DetectorType::RadHunterLaBr3:
    case DetectorType::Sam940LaBr3:
    case DetectorType::OrtecRadEagleLaBr:
      det_kind = "LaBr3";
      break;
      
    case DetectorType::OrtecRadEagleCeBr2Inch:
    case DetectorType::OrtecRadEagleCeBr3Inch:
      det_kind = "CeBr3";
      break;
      
    case DetectorType::SAIC8:
    case DetectorType::Srpm210:
      det_kind = "PVT";
      break;
      
    case DetectorType::MicroRaider:
      det_kind = "CZT";
      break;
      
    case DetectorType::Unknown:
      if( num_gamma_channels() > 4100 )
        det_kind = "HPGe";
      else if( manufacturer_=="Raytheon" && instrument_model_=="Variant L" )
        det_kind = "NaI";
      else if( manufacturer_=="Mirion Technologies" && instrument_model_=="model Pedestrian G" )
        det_kind = "NaI";
      else if( manufacturer_=="Nucsafe" && instrument_model_=="G4_Predator" )
        det_kind = "PVT";
      break;
  }//switch( detector_type_ )
  
  return det_kind;
}//determine_rad_detector_kind_code()



bool SpecFile::load_micro_raider_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  try
  {
    reset();
    rapidxml::file<char> input_file( input );
    const bool success = load_from_micro_raider_from_data( input_file.data() );
    
    if( success )
      filename_ = filename;
    
    return success;
  }catch( std::exception & )
  {
    reset();
    return false;
  }//try/catch
  
  return false;
}//bool load_micro_raider_file(...)


bool SpecFile::load_from_micro_raider_from_data( const char *data )
{
  try
  {
    typedef char XmlChar;
    rapidxml::xml_document<XmlChar> doc;
    
    //Casting 'data' to non-const, BUT rapidxml::parse_non_destructive
    //  _should_ garuntee the source is no altered.  It's a little bit shaky
    //  but it allows us to potentially use mmap
    doc.parse<rapidxml::parse_non_destructive |rapidxml::allow_sloppy_parse>( (XmlChar *)data );
    const rapidxml::xml_node<XmlChar> *IdResult = XML_FIRST_NODE((&doc),"IdResult");
    
    if( !IdResult )
      throw runtime_error( "Invalid Micro Raider XML document" );
    
    const rapidxml::xml_node<XmlChar> *DeviceId = XML_FIRST_NODE(IdResult,"DeviceId");
    const rapidxml::xml_node<XmlChar> *SurveyId = XML_FIRST_NODE(IdResult,"SurveyId");
    const rapidxml::xml_node<XmlChar> *UUID = XML_FIRST_NODE(IdResult,"UUID");
    const rapidxml::xml_node<XmlChar> *EventNumber = XML_FIRST_NODE(IdResult,"EventNumber");
    const rapidxml::xml_node<XmlChar> *CrystalType = XML_FIRST_NODE(IdResult,"CrystalType");
    const rapidxml::xml_node<XmlChar> *UserMode = XML_FIRST_NODE(IdResult,"UserMode");
    const rapidxml::xml_node<XmlChar> *StartTime = XML_FIRST_NODE(IdResult,"StartTime");
//    const rapidxml::xml_node<XmlChar> *StopTime = XML_FIRST_NODE(IdResult,"StopTime");
    const rapidxml::xml_node<XmlChar> *GPS = XML_FIRST_NODE(IdResult,"GPS");
    const rapidxml::xml_node<XmlChar> *RealTime = XML_FIRST_NODE(IdResult,"RealTime");
    const rapidxml::xml_node<XmlChar> *LiveTime = XML_FIRST_NODE(IdResult,"LiveTime");
//    const rapidxml::xml_node<XmlChar> *DeadTime = XML_FIRST_NODE(IdResult,"DeadTime");
    const rapidxml::xml_node<XmlChar> *DoseRate = XML_FIRST_NODE(IdResult,"DoseRate");
//    const rapidxml::xml_node<XmlChar> *CountRate = XML_FIRST_NODE(IdResult,"CountRate");
    const rapidxml::xml_node<XmlChar> *NeutronCountRate = XML_FIRST_NODE(IdResult,"NeutronCountRate");
    
    
    const rapidxml::xml_node<XmlChar> *Nuclide = XML_FIRST_NODE(IdResult,"Nuclide");
    const rapidxml::xml_node<XmlChar> *Image = XML_FIRST_NODE(IdResult,"Image");
    const rapidxml::xml_node<XmlChar> *VoiceRecording = XML_FIRST_NODE(IdResult,"VoiceRecording");
    const rapidxml::xml_node<XmlChar> *Spectrum = XML_FIRST_NODE(IdResult,"Spectrum");
    
    if( !Spectrum || !Spectrum->value_size() )
      throw runtime_error( "No Spectrum Node" );
    
    std::shared_ptr< vector<float> > channel_counts
                                         = std::make_shared<vector<float> >();
    
    const bool validchannel
               = SpecUtils::split_to_floats( Spectrum->value(),
                                      Spectrum->value_size(), *channel_counts );
    if( !validchannel || channel_counts->empty() )
      throw runtime_error( "Couldnt parse channel counts" );
    
    std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
    
    meas->gamma_counts_ = channel_counts;
    
    meas->gamma_count_sum_ = 0.0;
    const size_t nchannel = meas->gamma_counts_->size();
    for( size_t i = 0; i < nchannel; ++i )
      meas->gamma_count_sum_ += (*channel_counts)[i];
    
    instrument_id_ = xml_value_str( DeviceId );
    if( SurveyId )
      remarks_.push_back( "Survey ID: " + xml_value_str( SurveyId ) );
    uuid_ = xml_value_str( UUID );
    if( EventNumber )
      remarks_.push_back( "EventNumber: " + xml_value_str( EventNumber ) );
    if( CrystalType )
      remarks_.push_back( "CrystalType: " + xml_value_str( CrystalType ) );
    if( UserMode )
      remarks_.push_back( "CrystalType: " + xml_value_str( UserMode ) );
    
    //Unecasary allocation to get time.
    const string start_time = xml_value_str(StartTime);
    meas->start_time_ = SpecUtils::time_from_string( start_time.c_str() );

    if( GPS && GPS->value_size() )
    {
      rapidxml::xml_attribute<XmlChar> *att = GPS->first_attribute("Valid",5);
      
      if( !att || XML_VALUE_ICOMPARE(att,"True") )
        parse_deg_min_sec_lat_lon(GPS->value(), GPS->value_size(),
                                  meas->latitude_, meas->longitude_ );
    }//if( GPS )
    
    if( RealTime && RealTime->value_size() )
      meas->real_time_ = time_duration_string_to_seconds( RealTime->value(), RealTime->value_size() );
    if( LiveTime && LiveTime->value_size() )
      meas->live_time_ = time_duration_string_to_seconds( LiveTime->value(), LiveTime->value_size() );
    
    
    std::shared_ptr<DetectorAnalysis> detana;
    
    if( DoseRate && DoseRate->value_size() )
    {
      DetectorAnalysisResult detanares;
      const float doseunit = dose_units_usvPerH( DoseRate->value(),
                                                 DoseRate->value_size() );
      
      //Avoidable allocation here below
      float dose;
      if( (stringstream(xml_value_str(DoseRate)) >> dose) )
        detanares.dose_rate_ = dose * doseunit;
      else
        cerr << "Failed to turn '" << xml_value_str(DoseRate) << "' into a dose" << endl;
      
      //detanares.start_time_ = meas->start_time_;
      detanares.real_time_ = meas->real_time_;
      
      if( !detana )
        detana = std::make_shared<DetectorAnalysis>();
      detana->results_.push_back( detanares );
    }//if( DoseRate && DoseRate->value_size() )
    
    if( NeutronCountRate && NeutronCountRate->value_size() )
    {
      const string neutroncountstr = xml_value_str(NeutronCountRate);
      meas->neutron_counts_.resize( 1, 0.0f );
      
      float neutrons = 0.0f;
      if( toFloat(neutroncountstr,neutrons) )
      {
        if( meas->real_time_ > 0.0f )
          neutrons *= meas->real_time_;
        else if( meas->live_time_ > 0.0f )
          neutrons *= meas->live_time_;
        else
          meas->remarks_.push_back( "NeutronCountRate: " + neutroncountstr + " (error computing gross count)" ); //meh, should be fine...
        
        meas->neutron_counts_sum_ = neutrons;
        meas->neutron_counts_[0] = neutrons;
        meas->contained_neutron_ = true;
      }else
      {
        meas->remarks_.push_back( "NeutronCountRate: " + neutroncountstr );
        cerr << "Failed to read '" << neutroncountstr << "' as neutroncountstr"
             << endl;
      }
    }//if( NeutronCountRate && NeutronCountRate->value_size() )
    
    while( Nuclide )
    {
      DetectorAnalysisResult res;
      
      const rapidxml::xml_node<XmlChar> *NuclideName = XML_FIRST_NODE(Nuclide,"NuclideName");
      const rapidxml::xml_node<XmlChar> *NuclideType = XML_FIRST_NODE(Nuclide,"NuclideType");
      const rapidxml::xml_node<XmlChar> *NuclideIDConfidenceIndication = XML_FIRST_NODE(Nuclide,"NuclideIDConfidenceIndication");
      const rapidxml::xml_node<XmlChar> *NuclideIDStrengthIndication = XML_FIRST_NODE(Nuclide,"NuclideIDStrengthIndication");
      const rapidxml::xml_node<XmlChar> *NuclideDescription = XML_FIRST_NODE(Nuclide,"NuclideDescription");
//      const rapidxml::xml_node<XmlChar> *NuclideInstruction = XML_FIRST_NODE(Nuclide,"NuclideInstruction");
      const rapidxml::xml_node<XmlChar> *NuclideHPRDSType = XML_FIRST_NODE(Nuclide,"NuclideHPRDSType");
    
      res.nuclide_ = xml_value_str(NuclideName);
      res.nuclide_type_ = xml_value_str(NuclideType);
      res.id_confidence_ = xml_value_str(NuclideIDConfidenceIndication);
      
      const string strength = xml_value_str(NuclideIDStrengthIndication);
      if( strength.size() )
        res.remark_ += "strength: " + strength;
      if( NuclideHPRDSType && NuclideHPRDSType->value_size() )
        res.remark_ += (res.remark_.size() ? ". " : "") + xml_value_str(NuclideHPRDSType);
      if( NuclideDescription && NuclideDescription->value_size() )
        res.remark_ += (res.remark_.size() ? ". " : "") + xml_value_str(NuclideDescription);
      
      Nuclide = XML_NEXT_TWIN(Nuclide);
      
      if( !detana )
        detana = std::make_shared<DetectorAnalysis>();
      detana->results_.push_back( res );
    }//while( Nuclide )
    
    while( Image && Image->value_size() )
    {
      remarks_.push_back( "Image: " + xml_value_str(Image) );
      Image = XML_NEXT_TWIN(Image);
    }
    
    if( VoiceRecording && VoiceRecording->value_size() )
      remarks_.push_back( "VoiceRecording: " + xml_value_str(VoiceRecording) );
    
    detectors_analysis_ = detana;
    
    //Following values taken from a Micro Raider ICD1 N42 2006 file
    manufacturer_ = "ICx Radiation";
    instrument_model_ = "Raider";
    instrument_type_ = "Radionuclide Identifier";  //or PersonalRadiationDetector
    detector_type_ = DetectorType::MicroRaider;
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
    
    return true;
  }catch( std::exception & )
  {
    return false;
  }
  
  
//  "<IdResult"
  
  return false;
}//bool load_from_micro_raider_from_data( const char *data )









void SpecFile::rebin_by_eqn( const std::vector<float> &eqn,
                                    const std::vector<std::pair<float,float>> &dev_pairs,
                                    SpecUtils::EnergyCalType type )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  std::shared_ptr<const std::vector<float>> new_binning; //to save memory we will only create one binning object

  SpecUtilsAsync::ThreadPool threadpool;

  for( auto &m : measurements_ )
  {
    if( !m->gamma_counts_ || m->gamma_counts_->empty() )
      continue;

    const bool noRecalNeeded = ( m->energy_calibration_model_ == type
                                 && ((type==SpecUtils::EnergyCalType::LowerChannelEdge && m->channel_energies_ && (*(m->channel_energies_)==eqn))
                                     || (type!=SpecUtils::EnergyCalType::LowerChannelEdge && m->calibration_coeffs_ == eqn) )
                                 && m->channel_energies_->size() > 0
                                 && m->deviation_pairs_ == dev_pairs );

    if( !new_binning )
    {
      if( !noRecalNeeded )
        m->rebin_by_eqn( eqn, dev_pairs, type );
      new_binning = m->channel_energies_;
    }//if( !new_binning )

    if( !noRecalNeeded )
	    threadpool.post( [m,&new_binning](){ m->rebin_by_lower_edge(new_binning); });
  }//for( auto &m : measurements_ )

  threadpool.join();

  for( auto &m : measurements_ )
  {
    m->calibration_coeffs_ = eqn;
    m->deviation_pairs_    = dev_pairs;
    m->energy_calibration_model_  = type;
  }//for( auto &m : measurements_ )
  
  properties_flags_ |= kHasCommonBinning;
  
  modified_ = modifiedSinceDecode_ = true;
}//void rebin_by_eqn( const std::vector<float> &eqn )


void SpecFile::recalibrate_by_lower_edge( std::shared_ptr<const std::vector<float>> binning )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  for( auto &m : measurements_ )
    if( m->gamma_counts_ && !m->gamma_counts_->empty() )
      m->recalibrate_by_eqn( vector<float>(), std::vector<std::pair<float,float>>(), SpecUtils::EnergyCalType::LowerChannelEdge, binning );
  
  modified_ = modifiedSinceDecode_ = true;
}//void recalibrate_by_lower_edge( const std::vector<float> &binning )



void SpecFile::recalibrate_by_eqn( const std::vector<float> &eqn,
                                          const std::vector<std::pair<float,float>> &dev_pairs,
                                          SpecUtils::EnergyCalType type )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );


  std::shared_ptr<const std::vector<float>> new_binning; //to save memory we will only create one binning object

  for( auto &m : measurements_ )
  {
    if( m->gamma_counts_->size() < 3 )  //GM tubes can be recorded as gamma - this should be corrected
      continue;

    if( !new_binning )
    {
      m->recalibrate_by_eqn( eqn, dev_pairs, type, std::shared_ptr<const std::vector<float>>() );
      new_binning = m->channel_energies_;
    }else
    {
      m->recalibrate_by_eqn( eqn, dev_pairs, type, new_binning );
    }
  }//for( auto &m : measurements_ )
  
  properties_flags_ |= kHasCommonBinning;
  
  modified_ = modifiedSinceDecode_ = true;
}//void recalibrate_by_eqn( const std::vector<float> &eqn )


//If only certain detectors are specified, then those detectors will be
//  recalibrated, and the other detectors will be rebinned.
void SpecFile::recalibrate_by_eqn( const std::vector<float> &eqn,
                                          const std::vector<std::pair<float,float>> &dev_pairs,
                                          SpecUtils::EnergyCalType type,
                                          const vector<string> &detectors,
                                          const bool rebin_other_detectors )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  if( detectors.empty() )
    throw runtime_error( "recalibrate_by_eqn(...): an empty set of detectors"
                         " to recalibrate was passed in." );
  
  //If we are recalibrating all the detectors, lets just call the other version
  //  of this function, but it doenst really matter if we dont
  if( detectors == detector_names_ )
  {
    recalibrate_by_eqn( eqn, dev_pairs, type );
    return;
  }//if( detectors == detector_names_ )
  
  
  std::shared_ptr<const std::vector<float>> new_binning; //to save memory we will only create one binning object
  vector<std::shared_ptr<Measurement>> rebinned;
  
  for( auto &m : measurements_ )
  {
    if( !m->gamma_counts_ || m->gamma_counts_->empty() )
      continue;
    
    vector<string>::const_iterator name_pos = std::find( detectors.begin(),
                                        detectors.end(), m->detector_name() );
    if( name_pos == detectors.end() )
    {
      rebinned.push_back( m );
    }else if( !new_binning )
    {
      m->recalibrate_by_eqn( eqn, dev_pairs, type, std::shared_ptr<const std::vector<float>>() );
      new_binning = m->channel_energies_;
    }else
    {
      m->recalibrate_by_eqn( eqn, dev_pairs, type, new_binning );
    }
  }//for( auto &m : measurements_ )
  
  if( !new_binning )
    throw runtime_error( "recalibrate_by_eqn(...): no valid detector"
                        " names were passed in to recalibrate." );
  
  if( rebin_other_detectors && rebinned.size() )
  {
    if( rebinned.size() > 4 )
    {
    SpecUtilsAsync::ThreadPool threadpool;
    for( auto &m : rebinned )
		  threadpool.post( [m,&new_binning](){ m->rebin_by_lower_edge(new_binning); } );
    threadpool.join();
    }else
    {
      for( auto &m : rebinned )
        m->rebin_by_lower_edge( new_binning );
    }
  
    for( auto &m : rebinned )
    {
      m->calibration_coeffs_ = eqn;
      m->deviation_pairs_    = dev_pairs;
      m->energy_calibration_model_  = type;
    }//for( auto &m : rebinned )
    
    properties_flags_ |= kHasCommonBinning;
  }else
  {
    //check to see if the binning of all the measurments_ happens to be same
    bool allsame = true;
    for( size_t i = 0; allsame && i < rebinned.size(); ++i )
      allsame = ((rebinned[i]->energy_calibration_model_ == type)
                 && (rebinned[i]->calibration_coeffs_ == eqn)
                 && (rebinned[i]->deviation_pairs_ == dev_pairs));

    if( allsame )
      properties_flags_ |= kHasCommonBinning;
    else
      properties_flags_ &= (~kHasCommonBinning);
  }//if( rebin_other_detectors && rebinned.size() )

  modified_ = modifiedSinceDecode_ = true;
}//void recalibrate_by_eqn(...)




size_t SpecFile::memmorysize() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  size_t size = sizeof(*this);

  size += filename_.capacity()*sizeof(string::value_type);
  for( const string &s : detector_names_ )
    size += s.capacity()*sizeof(string::value_type);
  size += detector_numbers_.capacity()*sizeof(int);
  for( const string &s : neutron_detector_names_ )
    size += s.capacity()*sizeof(string::value_type);

  size += uuid_.capacity()*sizeof(string::value_type);
  for( const string &str : remarks_ )
    size += str.capacity()*sizeof(string::value_type);
  size += measurement_location_name_.capacity()*sizeof(string::value_type);
  size += inspection_.capacity()*sizeof(string::value_type);
  size += sample_numbers_.size()*sizeof(int);

  size += sample_to_measurments_.size() * sizeof(vector<size_t>);
  typedef std::map<int, std::vector<size_t> > InIndVecMap;
  for( const InIndVecMap::value_type &t : sample_to_measurments_ )
    size += t.second.capacity() * sizeof(size_t);

  size += instrument_type_.capacity()*sizeof(string::value_type);
  size += manufacturer_.capacity()*sizeof(string::value_type);
  size += instrument_model_.capacity()*sizeof(string::value_type);
  size += instrument_id_.capacity()*sizeof(string::value_type);

  size += measurements_.capacity() * sizeof( std::shared_ptr<Measurement> );

  map< const vector<float> *, int> binnings;
  for( const auto &m : measurements_ )
  {
    size += m->memmorysize();
    const vector<float> *bin_ptr = m->channel_energies_.get();
    if( binnings.find( bin_ptr ) == binnings.end() )
      binnings[ bin_ptr ] = 0;
    ++binnings[bin_ptr];
  }//for( const auto &m, measurements_ )

  //What about this->channel_energies_ ?  In principle we've already counted
  //  the size of this->channel_energies_
  for( const map< const vector<float> *, int>::value_type &entry : binnings )
    if( entry.first && entry.second > 1 )
      size -= ((entry.second-1)*( sizeof(*(entry.first)) + entry.first->capacity()*sizeof(float)));

  return size;
}//size_t memmorysize() const


bool SpecFile::passthrough() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  return (properties_flags_ & kPassthroughOrSearchMode);
}//bool passthrough() const


size_t SpecFile::suggested_gamma_binning_index(
                                            const std::set<int> &sample_numbers,
                                            const vector<bool> &det_to_use ) const
{
  //XXX - this function is a quickly implemented (temporary?) hack
  size_t index;
  std::shared_ptr<const std::vector<float>> binning_ptr;
    
  if( detector_numbers_.size() != det_to_use.size() )
    throw runtime_error( "SpecFile::suggested_gamma_binning_index():"
                         " invalid det_to_use" );
  
  const bool same_nchannel = ((properties_flags_ & kAllSpectraSameNumberChannels) != 0);
  
  for( size_t i = 0; i < measurements_.size(); ++i )
  {
    const std::shared_ptr<Measurement> &meas = measurements_[i];
    
    if( !sample_numbers.empty() && !sample_numbers.count(meas->sample_number_) )
      continue;
    
    const size_t det_index = std::find( detector_numbers_.begin(),
                                        detector_numbers_.end(),
                                        meas->detector_number_ )
                              - detector_numbers_.begin();
    
    const std::shared_ptr<const std::vector<float>> &thisbinning = meas->channel_energies();
    
    if( det_to_use.at(det_index) && !!thisbinning && !thisbinning->empty() )
    {
#if(!PERFORM_DEVELOPER_CHECKS)
      if( !binning_ptr || (binning_ptr->size() < thisbinning->size()) )
      {
        if( same_nchannel )
          return i;
        
        index = i;
        binning_ptr = thisbinning;
      }//if( !binning_ptr || (binning_ptr->size() < thisbinning->size()) )
#else
      if( !binning_ptr || (binning_ptr->size() < thisbinning->size()) )
      {
        if( !!binning_ptr
            && (binning_ptr->size() != thisbinning->size())
            && same_nchannel )
        {
          char buffer[512];
          snprintf( buffer, sizeof(buffer),
                    "Found instance of differening number of gamma channels,"
                    " when I shouldnt have; measurment %i had %i channels,"
                    " while measurment %i had %i channels.",
                   static_cast<int>(index),
                   static_cast<int>(binning_ptr->size()),
                   static_cast<int>(i),
                   static_cast<int>(thisbinning->size() ) );
          log_developer_error( __func__, buffer );
        }
        
        index = i;
        binning_ptr = thisbinning;
      }//if( !binning_ptr || (binning_ptr->size() < thisbinning->size()) )
#endif
    }//if( use thisbinning )
  }//for( size_t i = 0; i < measurements_.size(); ++i )
  
  if( !binning_ptr )
    throw runtime_error( "SpecFile::suggested_gamma_binning_index():"
                         " no valid measurments." );
  
  return index;
}//suggested_gamma_binning_index(...)



std::shared_ptr<Measurement> SpecFile::sum_measurements( const std::set<int> &sample_numbers,
                                     const std::vector<std::string> &det_names ) const
{
  vector<bool> det_to_use( detector_numbers_.size(), false );
  
  for( const std::string name : det_names )
  {
    const vector<string>::const_iterator pos = std::find( begin(detector_names_),
                                                end(detector_names_),
                                                name );
    if( pos == end(detector_names_) )
      throw runtime_error( "SpecFile::sum_measurements(): invalid detector name in the input" );
    
    const size_t index = pos - detector_names_.begin();
    det_to_use[index] = true;
  }//for( const int num : det_nums )
  
  return sum_measurements( sample_numbers, det_to_use );
}//std::shared_ptr<Measurement> sum_measurements(...)



std::shared_ptr<Measurement> SpecFile::sum_measurements( const set<int> &sample_num,
                                            const vector<int> &det_nums ) const
{
  vector<bool> det_to_use( detector_numbers_.size(), false );
 
  for( const int num : det_nums )
  {
    vector<int>::const_iterator pos = std::find( detector_numbers_.begin(),
                                                 detector_numbers_.end(),
                                                 num );
    if( pos == detector_numbers_.end() )
      throw runtime_error( "SpecFile::sum_measurements(): invalid detector number in the input" );
    
    const size_t index = pos - detector_numbers_.begin();
    det_to_use[index] = true;
  }//for( const int num : det_nums )
  
  return sum_measurements( sample_num, det_to_use );
}//sum_measurements(...)


std::shared_ptr<Measurement> SpecFile::sum_measurements( const set<int> &sample_num,
                                                     const vector<int> &det_nums,
                                                     const std::shared_ptr<const Measurement> binTo ) const
{
  vector<bool> det_to_use( detector_numbers_.size(), false );
  
  for( const int num : det_nums )
  {
    vector<int>::const_iterator pos = std::find( detector_numbers_.begin(),
                                                detector_numbers_.end(),
                                                num );
    if( pos == detector_numbers_.end() )
      throw runtime_error( "SpecFile::sum_measurements(): invalid detector number in the input" );
    
    const size_t index = pos - detector_numbers_.begin();
    det_to_use[index] = true;
  }//for( const int num : det_nums )
  
  return sum_measurements( sample_num, det_to_use, binTo );
}//sum_measurements(...)



std::shared_ptr<Measurement> SpecFile::sum_measurements(
                                         const std::set<int> &sample_numbers,
                                         const vector<bool> &det_to_use ) const
{
  //For example example passthrough (using late 2011 mac book pro):
  //  Not using multithreading:          It took  0.135964s wall, 0.130000s user + 0.000000s system = 0.130000s CPU (95.6%)
  //  Using multithreading (4 threads):  It took  0.061113s wall, 0.220000s user + 0.000000s system = 0.220000s CPU (360.0%)
  //For example example HPGe Ba133:
  //  Not using multithreading:         It took  0.001237s wall, 0.000000s user
  //  Using multithreading (2 threads): It took  0.001248s wall, 0.000000s user

  size_t calIndex;
  try
  {
    calIndex = suggested_gamma_binning_index( sample_numbers, det_to_use );
  }catch(...)
  {
    return std::shared_ptr<Measurement>();
  }
  
  return sum_measurements( sample_numbers, det_to_use, measurements_[calIndex] );
}//sum_measurements(...)


std::shared_ptr<Measurement> SpecFile::sum_measurements( const std::set<int> &sample_numbers,
                                      const vector<bool> &det_to_use,
                                      const std::shared_ptr<const Measurement> binto ) const
{
  if( !binto || measurements_.empty() )
    return std::shared_ptr<Measurement>();
  
  std::shared_ptr<Measurement> dataH = std::make_shared<Measurement>();
  
  dataH->energy_calibration_model_  = binto->energy_calibration_model_;
  dataH->calibration_coeffs_ = binto->calibration_coeffs_;
  dataH->deviation_pairs_    = binto->deviation_pairs_;
  dataH->channel_energies_   = binto->channel_energies();
  
  if( measurements_.size() == 1 )
    dataH->set_title( measurements_[0]->title_ );
  else
    dataH->set_title( filename_ );
  
  if( detector_names_.size() != det_to_use.size() )
    throw runtime_error( "SpecFile::sum_measurements(...): "
                        "det_to_use.size() != sample_measurements.size()" );
  
  dataH->contained_neutron_ = false;
  dataH->real_time_ = 0.0f;
  dataH->live_time_ = 0.0f;
  dataH->gamma_count_sum_ = 0.0;
  dataH->neutron_counts_sum_ = 0.0;
  dataH->sample_number_ = -1;
  dataH->detector_name_ = "Summed";
  dataH->detector_number_ = -1;
  if( sample_numbers.size() == 1 )
    dataH->sample_number_ = *sample_numbers.begin();
  dataH->start_time_ = boost::posix_time::pos_infin;
  
  const size_t nenergies = dataH->channel_energies_ ? dataH->channel_energies_->size() : size_t(0);
  
  size_t ndet_to_use = 0;
  for( const bool i : det_to_use ) //could use std::count_if(...), for std::for_each(...) ...
    ndet_to_use += static_cast<size_t>( i );

  
  bool allBinningIsSame = ((properties_flags_ & kHasCommonBinning) != 0);
  
  if( allBinningIsSame )
  {
    const vector< std::shared_ptr<Measurement> >::const_iterator pos
               = std::find( measurements_.begin(), measurements_.end(), binto );
    const bool bintoInMeas = (pos != measurements_.end());
    if( !bintoInMeas )
      allBinningIsSame = (measurements_[0]->channel_energies() == binto->channel_energies());
  }//if( allBinningIsSame && measurements_.size() )

  //any less than 'min_per_thread' than the additional memorry allocation isnt
  //  worth it - briefly determined on my newer mbp using both example
  //  example passthrough (16k channel) and a 512 channel NaI portal passthrough
  const size_t min_per_thread = 8;

  size_t num_thread = static_cast<size_t>( SpecUtilsAsync::num_physical_cpu_cores() );
  const size_t num_potential_specta = ndet_to_use * sample_numbers.size();
  num_thread = min( num_thread, num_potential_specta/min_per_thread );
  num_thread = max( size_t(1), num_thread );
  
  vector< vector< std::shared_ptr<const Measurement> > > specs( num_thread );
  vector< vector< std::shared_ptr<const vector<float> > > > spectrums( num_thread );
  
  int current_total_sample_num = 0;
  set<string> remarks;
  for( const int sample_number : sample_numbers )
  {
    for( size_t index = 0; index < detector_names_.size(); ++index )
    {
      const std::string &det = detector_names_[index];
      std::shared_ptr<const Measurement> meas = measurement( sample_number, det );
      
      if( !meas )
        continue;
      
      std::shared_ptr<const vector<float> > spec = meas->gamma_counts();
      
      const size_t spec_size = (spec ? spec->size() : (size_t)0);
      
      //we'll allow measurement->channel_energies() to have more bins, since
      //  if meas->energy_calibration_model() == LowerChannelEdge then ther will be
      //  an extra bin to mark overflow
      if( allBinningIsSame && (spec_size > nenergies) )
      {
        stringstream msg;
        msg << SRC_LOCATION << "\n\tspec.size()=" << spec_size
            << "  measurement->channel_energies().size()=" << nenergies;
        cerr << msg.str() << endl;
        throw runtime_error( msg.str() );
      }//if( spec.size() > (binning_ptr->size()) )
      
      //Could add consistency check here to make sure all spectra are same size
      
      if( det_to_use[index] )
      {
        dataH->start_time_ = std::min( dataH->start_time_, meas->start_time_ );
        
        if( binto->gamma_counts_ && (binto->gamma_counts_->size() > 3) )
        {
          if( meas->gamma_counts_ && (meas->gamma_counts_->size() > 3) )
          {
            dataH->live_time_ += meas->live_time();
            dataH->real_time_ += meas->real_time();
          }
        }else
        {
          dataH->live_time_ += meas->live_time();
          dataH->real_time_ += meas->real_time();
        }
        
        dataH->neutron_counts_sum_ += meas->neutron_counts_sum();
        dataH->contained_neutron_ |= meas->contained_neutron_;
        
        if( dataH->neutron_counts_.size() < meas->neutron_counts_.size() )
          dataH->neutron_counts_.resize( meas->neutron_counts_.size(), 0.0f );
        const size_t nneutchannel = meas->neutron_counts_.size();
        for( size_t i = 0; i < nneutchannel; ++i )
          dataH->neutron_counts_[i] += meas->neutron_counts_[i];
        
        for( const std::string &remark : meas->remarks_ )
          remarks.insert( remark );
        
        if( spec_size )
        {
          dataH->gamma_count_sum_ += meas->gamma_count_sum();
          const size_t thread_num = current_total_sample_num % num_thread;
          specs[thread_num].push_back( meas );
          spectrums[thread_num].push_back( meas->gamma_counts() );
          ++current_total_sample_num;
        }
      }//if( det_to_use[index] )
    }//for( size_t index = 0; index < detector_names_.size(); ++index )
  }//for( const int sample_number : sample_numbers )
  
  if( !current_total_sample_num )
    return dataH->contained_neutron_ ? dataH : std::shared_ptr<Measurement>();
  
  //If we are only summing one sample, we can preserve some additional
  //  information
  if( current_total_sample_num == 1 )
  {
    dataH->latitude_ = specs[0][0]->latitude_;
    dataH->longitude_ = specs[0][0]->longitude_;
    dataH->position_time_ = specs[0][0]->position_time_;
    dataH->sample_number_ = specs[0][0]->sample_number_;
    dataH->occupied_ = specs[0][0]->occupied_;
    dataH->speed_ = specs[0][0]->speed_;
    dataH->detector_name_ = specs[0][0]->detector_name_;
    dataH->detector_number_ = specs[0][0]->detector_number_;
    dataH->detector_description_ = specs[0][0]->detector_description_;
    dataH->quality_status_ = specs[0][0]->quality_status_;
  }//if( current_total_sample_num == 1 )
  
  if( allBinningIsSame )
  {
    if( num_thread > 1 )
    {
      //Should consider using calloc( )  and free...
      vector< vector<float> > results( num_thread );
    
      SpecUtilsAsync::ThreadPool threadpool;
      for( size_t i = 0; i < num_thread; ++i )
      {
        vector<float> *dest = &(results[i]);
        const vector< std::shared_ptr<const vector<float> > > *spec = &(spectrums[i]);
        threadpool.post( [dest,spec](){ add_to(*dest, *spec); } );
      }
      threadpool.join();
    
      //Note: in principle for a multicore machine (>16 physical cores), we
      //      could combine results using a few different threads down to less
      //      than 'min_per_thread'
      const size_t spec_size = results[0].size();
      std::shared_ptr<vector<float> > result_vec = std::make_shared< vector<float> >(spec_size, 0.0f);
      dataH->gamma_counts_ = result_vec;
    
      for( size_t i = 0; i < num_thread; ++i )
      {
        if( results[i].size() )
        {
          const float *spec_array = &(results[i][0]);
          for( size_t bin = 0; bin < spec_size; ++bin )
            result_vec->operator[](bin) += spec_array[bin];
        }
      }//for( size_t i = 0; i < num_thread; ++i )
    }else
    {
      if( spectrums.size()!=1 || spectrums[0].empty() )
        throw runtime_error( string(SRC_LOCATION) + "\n\tSerious programming logic error" );
    
      const vector< std::shared_ptr<const vector<float> > > &spectra = spectrums[0];
      const size_t num_spectra = spectra.size();
    
      const size_t spec_size = spectra[0]->size();
      vector<float> *result_vec = new vector<float>( spec_size, 0.0 );
      dataH->gamma_counts_.reset( result_vec );
      float *result_vec_raw = &((*result_vec)[0]);
    
      for( size_t i = 0; i < num_spectra; ++i )
      {
        const size_t len = spectra[i]->size();
        const float *spec_array = &(spectra[i]->operator[](0));
      
        //Using size_t to get around possible variable size issues.
        //  In principle I would expect this below loop to get vectorized by the
        //  compiler - but I havent actually checked for this.
        for( size_t bin = 0; bin < spec_size && bin < len; ++bin )
          result_vec_raw[bin] += spec_array[bin];
      }//for( size_t i = 0; i < num_thread; ++i )
    }//if( num_thread > 1 ) / else
  
  }else //if( allBinningIsSame )
  {
    if( sample_numbers.size() > 1 || det_to_use.size() > 1 )
      cerr << "sum_measurements for case with without a common binning not tested yet!" << endl;
    
    vector< vector<float> > results( num_thread );
    SpecUtilsAsync::ThreadPool threadpool;
    for( size_t i = 0; i < num_thread; ++i )
	  {
      vector<float> *dest = &(results[i]);
      const vector< std::shared_ptr<const Measurement> > *measvec = &(specs[i]);
      threadpool.post( [dest,&dataH,measvec](){ sum_with_rebin( *dest, dataH, *measvec ); } );
	  }
    
	  threadpool.join();
    
    const size_t spec_size = results[0].size();
    vector<float> *result_vec = new vector<float>( results[0] );
    dataH->gamma_counts_.reset( result_vec );
    float *result_vec_raw = &((*result_vec)[0]);
    
    for( size_t i = 1; i < num_thread; ++i )
    {
      const size_t len = results[i].size();
      const float *spec_array = &(results[i][0]);
      for( size_t bin = 0; bin < spec_size && bin < len; ++bin )
        result_vec_raw[bin] += spec_array[bin];
    }//for( size_t i = 0; i < num_thread; ++i )
  }//if( allBinningIsSame ) / else
  
  if( dataH->start_time_.is_infinity() )
    dataH->start_time_ = boost::posix_time::not_a_date_time;
  
  for( const std::string &remark : remarks )
    dataH->remarks_.push_back( remark );

  return dataH;
}//std::shared_ptr<Measurement> sum_measurements( int &, int &, const SpecMeas & )



set<size_t> SpecFile::gamma_channel_counts() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  set<size_t> answer;

  for( const auto &meas : measurements_ )
    if( meas && meas->channel_energies_ && !meas->channel_energies_->empty() )
      answer.insert( meas->channel_energies_->size() );

  return answer;
}//std::set<size_t> gamma_channel_counts() const


size_t SpecFile::num_gamma_channels() const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  for( const auto &meas : measurements_ )
    if( meas->num_gamma_channels() )
      return meas->num_gamma_channels();
  
  //if( meas && meas->channel_energies_ && !meas->channel_energies_->empty()
  //   && meas->gamma_counts_ && !meas->gamma_counts_->empty() )
  //  return std::min( meas->channel_energies_->size(), meas->gamma_counts_->size() );

  return 0;
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
      const std::shared_ptr<const std::vector<float>> channel_energies = (m ? m->channel_energies()
                                                   : std::shared_ptr<const std::vector<float>>());

      if( !( ( !channel_energies || channel_energies->size()!=m_nbin )
          && (channel_energies || !channel_energies->empty()) ) )
      {
        m_keepers->push_back( m );
      }
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


size_t SpecFile::remove_neutron_measurments()
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  size_t nremoved = 0;

  for( size_t i = 0; i < measurements_.size();  )
  {
    std::shared_ptr<Measurement> &m = measurements_[i];
    if( m->contained_neutron_
        && (!m->channel_energies_ || m->channel_energies_->empty()) )
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
}//size_t remove_neutron_measurments();


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


size_t SpecFile::keep_energy_cal_variant( const std::string variant )
{
  const string ending = "_intercal_" + variant;
  std::vector< std::shared_ptr<Measurement> > keepers;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const set<string> origvaraints = energy_cal_variants();
  
  if( !origvaraints.count(variant) )
    throw runtime_error( "SpecFile::keep_energy_cal_variant():"
                         " measurment did not contain an energy variant named '"
                         + variant + "'" );
  
  if( origvaraints.size() == 1 )
    return 0;
  
  keepers.reserve( measurements_.size() );
  
  for( auto &ptr : measurements_ )
  {
    const std::string &detname = ptr->detector_name_;
    const size_t pos = detname.find( "_intercal_" );
    if( pos == string::npos )
    {
      keepers.push_back( ptr );
    }else if( ((pos + ending.size()) == detname.size())
               && (strcmp(detname.c_str()+pos+10,variant.c_str())==0) )
    {
      ptr->detector_name_ = detname.substr( 0, pos );
      keepers.push_back( ptr );
    }
    //else
      //cout << "Getting rid of: " << detname << endl;
  }//for( auto &ptr : measurements_ )
  
  measurements_.swap( keepers );
  cleanup_after_load();
  
  modified_ = modifiedSinceDecode_ = true;
  
  return (keepers.size() - measurements_.size());
}//void keep_energy_cal_variant( const std::string variant )



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
  uuid_.clear();
  remarks_.clear();
  parse_warnings_.clear();
  lane_number_ = -1;
  measurement_location_name_.clear();
  inspection_.clear();
  measurment_operator_.clear();
  sample_numbers_.clear();
  sample_to_measurments_.clear();
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
  //start_time_ = boost::posix_time::ptime();
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


void SpecFile::write_to_file( const std::string &filename,
                   const std::set<int> &sample_nums,
                   const std::vector<std::string> &det_names,
                   const SaveSpectrumAsType format ) const
{
  set<int> det_nums_set;
  
  {//begin lock on mutex_
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
    for( const std::string &name : det_names )
    {
      const vector<string>::const_iterator pos = std::find( begin(detector_names_),
                                                         end(detector_names_),
                                                         name );
      if( pos == end(detector_names_) )
        throw runtime_error( "SpecFile::write_to_file(): invalid detector name in the input" );
    
      const size_t index = pos - detector_names_.begin();
      det_nums_set.insert( detector_numbers_[index] );
    }//for( const int num : det_nums )
  }//end lock on mutex_
  
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
  
  for( const int detnum : det_nums )
  {
    if( std::find(detector_numbers_.begin(), detector_numbers_.end(), detnum) == detector_numbers_.end() )
      throw runtime_error( "Specified invalid detector number to write out" );
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
    
    info.remove_measurments( toremove );
  }//if( we dont want all the measurments )
  
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

#if( SpecUtils_ENABLE_D3_CHART )
    case SaveSpectrumAsType::HtmlD3:
    {
      D3SpectrumExport::D3SpectrumChartOptions options;
      success = info.write_d3_html( strm, options, samples, detectors );
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




bool Measurement::write_csv( std::ostream& ostr ) const
{
  const char *endline = "\r\n";

  ostr << "Energy, Data" << endline;

  for( size_t i = 0; i < gamma_counts_->size(); ++i )
    ostr << channel_energies_->at(i) << "," << gamma_counts_->operator[](i) << endline;

  ostr << endline;

  return !ostr.bad();
}//bool Measurement::write_csv( std::ostream& ostr ) const


bool SpecFile::write_csv( std::ostream& ostr ) const
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );

  for( const std::shared_ptr<const Measurement> meas : measurements_ )
    meas->write_csv( ostr );

  return !ostr.bad();
}//bool write_csv( std::ostream& ostr ) const




void Measurement::set_info_from_avid_mobile_txt( std::istream &istr )
{
  //There is a variant of refQQZGMTCC93, RSL mobile system ref8T2SZ11TQE
  
  using SpecUtils::safe_get_line;
  using SpecUtils::split_to_floats;
  
  const istream::pos_type orig_pos = istr.tellg();
  
  try
  {
    string line;
    if( !SpecUtils::safe_get_line(istr, line) )
      throw runtime_error(""); //"Failed getting first line"
  
    if( line.size() < 8 || line.size() > 100 )
      throw runtime_error(""); //"First line not reasonable length"
    
    //const size_t first_invalid_char = line.substr(0,8).find_first_not_of( "0123456789 ,\r\n\t+-e." );
    const size_t first_invalid_char = line.find_first_not_of( "0123456789 ,\r\n\t+-e." );
    
    if( first_invalid_char != string::npos )
      throw runtime_error( "" ); //"Invalid character in first 8 characters"
    
    vector<string> flinefields;
    SpecUtils::split( flinefields, line, " ,\t");
    if( flinefields.size() != 4 )
      throw runtime_error( "" ); //"First line not real time then calibration coefs"
    
    vector<float> fline;
    if( !split_to_floats(line, fline) || fline.size()!=4 )
      throw runtime_error( "" ); //We expect the first line to be all numbers
    
    const vector<float> eqn( fline.begin() + 1, fline.end() );
    const float realtime = fline[0];
    
    if( realtime < -FLT_EPSILON )
      throw runtime_error( "" ); //"First coefficient not real time"
    
    if( !safe_get_line(istr, line) )
      throw runtime_error(""); //"Failed getting second line"
    
    if( !split_to_floats(line, fline) )
      throw runtime_error( "" ); //"Second line not floats"
    
    if( fline.size() < 127 && fline.size() != 2 )
      throw runtime_error( "" ); //"Invalid second line"
    
    //If we got here, this is probably a valid file
    auto counts = std::make_shared< vector<float> >();
    
    if( fline.size() >= 127 )
    {
      //Second line is CSV of channel counts
      if( SpecUtils::safe_get_line(istr, line) && line.size() )
        throw runtime_error(""); //"Only expected two lines"
      
      counts->swap( fline );
    }else
    {
      //Rest of file is \t seperated column with two columns per line
      //  "channel\tcounts"
      float channelnum = fline[0];
      const float counts0 = fline[1];
    
      if( fabs(channelnum) > FLT_EPSILON && fabs(channelnum - 1.0) > FLT_EPSILON )
        throw runtime_error( "" ); //"First column doesnt refer to channel number"
    
      if( counts0 < -FLT_EPSILON )
        throw runtime_error( "" ); //"Second column doesnt refer to channel counts"

      channelnum = channelnum - 1.0f;
      istr.seekg( orig_pos, ios::beg );
      SpecUtils::safe_get_line( istr, line );
    
      while( safe_get_line( istr, line ) )
      {
        trim( line );
        if( line.empty() ) //Sometimes file will have a newline at the end of the file
          continue;
        
        if( !split_to_floats(line, fline) || fline.size() != 2 )
          throw runtime_error( "" ); //"Unexpected number of fields on a line"
        
        if( fabs(channelnum + 1.0f - fline[0]) > 0.9f /*FLT_EPSILON*/ )
          throw runtime_error( "" ); //"First column is not channel number"
        
        channelnum = fline[0];
        counts->push_back( fline[1] );
      }//while( SpecUtils::safe_get_line( istr, line ) )
    }//if( fline.size() >= 127 )
    
    const size_t nchannel = counts->size();
    if( nchannel < 127 )
      throw runtime_error(""); //"Not enought channels"
    
    const vector< pair<float,float> > devpairs;
    const bool validcalib
           = SpecUtils::calibration_is_valid( SpecUtils::EnergyCalType::Polynomial, eqn, devpairs,
                                                    nchannel );
    if( !validcalib )
      throw runtime_error( "" ); //"Invalid calibration"
    
//    real_time_ = realtime;
    live_time_ = realtime;
    contained_neutron_ = false;
    deviation_pairs_.clear();
    calibration_coeffs_ = eqn;
    energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
    neutron_counts_.clear();
    gamma_counts_ = counts;
    neutron_counts_sum_ = gamma_count_sum_ = 0.0;
    for( const float f : *counts )
      gamma_count_sum_ += f;
  }catch( std::exception &e )
  {
    istr.seekg( orig_pos, ios::beg );
    throw;
  }
}//void set_info_from_avid_mobile_txt( std::istream& istr )








bool SpecFile::load_spectroscopic_daily_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  char buffer[8];
  input.get( buffer, sizeof(buffer)-1 );
  buffer[sizeof(buffer)-1] = '\0'; //JIC
  
  string bufferstr = buffer;
  const bool isSDF = ((bufferstr.size() > 3 && bufferstr[2]==',')
                      && ( SpecUtils::starts_with( bufferstr, "GB" )
                          || SpecUtils::starts_with( bufferstr, "NB" )
                          || SpecUtils::starts_with( bufferstr, "S1" )
                          || SpecUtils::starts_with( bufferstr, "S2" )
                          || SpecUtils::starts_with( bufferstr, "GS" )
                          || SpecUtils::starts_with( bufferstr, "GS" )
                          || SpecUtils::starts_with( bufferstr, "NS" )
                          || SpecUtils::starts_with( bufferstr, "ID" )
                          || SpecUtils::starts_with( bufferstr, "AB" )));
  if( !isSDF )
    return false;
  
  input.seekg( 0, ios_base::beg );
  
  const bool success = load_from_spectroscopic_daily_file( input );
  
  if( !success )
    return false;
  
  filename_ = filename;
  //      Field 4, the equipment specifier, is as follows:
  //        -SPM-T for a Thermo ASP-C
  //        -SPM-C for a Canberra ASP-C
  //        -RDSC1 for the Radiation Detector Straddle Carrier in primary
  //        -RDSC2 for the Radiation Detector Straddle Carrier in secondary
  //        -MRDIS2 for the Mobile Radiation Detection and Identification System in secondary
  //        ex. refG8JBF6M229
  vector<string> fields;
  SpecUtils::split( fields, filename, "_" );
  if( fields.size() > 3 )
  {
    if( fields[3] == "SPM-T" )
    {
      manufacturer_ = "Thermo";
      instrument_model_ = "ASP";
    }else if( fields[3] == "SPM-C" )
    {
      manufacturer_ = "Canberra";
      instrument_model_ = "ASP";
    }else if( fields[3] == "RDSC1" )
    {
      inspection_ = "Primary";
      instrument_model_ = "Radiation Detector Straddle Carrier";
    }else if( fields[3] == "RDSC2" )
    {
      inspection_ = "Secondary";
      instrument_model_ = "Radiation Detector Straddle Carrier";
    }else if( fields[3] == "MRDIS2" )
    {
      inspection_ = "Secondary";
      instrument_model_ = "Mobile Radiation Detection and Identification System";
    }
  }//if( fields.size() > 3 )
      
  return true;
}//bool load_spectroscopic_daily_file( const std::string &filename )


bool SpecFile::load_amptek_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_amptek_mca( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_amptek_file( const std::string &filename )


bool SpecFile::load_ortec_listmode_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_ortec_listmode( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}


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


bool SpecFile::load_tka_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_tka( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_tka_file( const std::string &filename )


bool SpecFile::load_multiact_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_multiact( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_multiact_file( const std::string &filename );


bool SpecFile::load_phd_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_phd( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_phd_file( const std::string &filename );


bool SpecFile::load_lzs_file( const std::string &filename )
{
#ifdef _WIN32
  ifstream input( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream input( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  
  if( !input.is_open() )
    return false;
  
  const bool success = load_from_lzs( input );
  
  if( success )
    filename_ = filename;
  
  return success;
}//bool load_phd_file( const std::string &filename );





namespace SpectroscopicDailyFile
{
  struct DailyFileS1Info
  {
    bool success;
    std::string detTypeStr;
    std::string appTypeStr;
    int nchannels;
    std::vector<float> calibcoefs;
    std::string algorithmVersion;
    //caputures max 1 attribute...
    struct params{ std::string name, value, attname, attval; };
    vector<DailyFileS1Info::params> parameters;
  };//struct DailyFileS1Info
  
  
  struct DailyFileEndRecord
  {
    bool success;
    std::string alarmColor; //Red, Yellow, Gree
    int occupancyNumber;
    boost::posix_time::ptime lastStartTime;
    std::string icd1FileName;
    float entrySpeed, exitSpeed;
  };//struct DailyFileEndRecord

  
  struct DailyFileAnalyzedBackground
  {
    enum BackgroundType{ Gamma, Neutrons };
    
    bool success;
    BackgroundType type;
    float realTime;
    std::shared_ptr< std::vector<float> > spectrum;
  };//struct DailyFileAnalyzedBackground
  
  struct DailyFileNeutronSignal
  {
    bool success;
    int numTimeSlicesAgregated;
    int timeChunkNumber;  //identical to one used for gamma
    vector<float> counts;  //Aa1, Aa2, Aa3, Aa4, Ba1, Ba2, Ba3, Ba4, Ca1, Ca2, Ca3, Ca4, Da1, Da2, Da3, Da4
  };//enum DailyFileNeutronSignal
  
  struct DailyFileGammaSignal
  {
    bool success;
    std::string detectorName;
    int timeChunkNumber;
    std::shared_ptr< std::vector<float> > spectrum;
  };//struct DailyFileGammaSignal
  
  
  struct DailyFileGammaBackground
  {
    bool success;
    std::string detectorName;
    std::shared_ptr< std::vector<float> > spectrum;
  };//struct DailyFileGammaBackground
  
  struct DailyFileNeutronBackground
  {
    bool success;
    float realTime;
    vector<float> counts;
  };//struct DailyFileNeutronBackground
  
  
  void parse_s1_info( const char * const data, const size_t datalen, DailyFileS1Info &info )
  {
    const string s1str( data, datalen );
    
    info.calibcoefs.clear();
    
    vector<string> s1fields;
    SpecUtils::split( s1fields, s1str, "," );
    if( s1fields.size() < 5 )
    {
      cerr << "parse_s1_info(): Invalid S1 line" << endl;
      info.success = false;
      return;
    }
    
    info.detTypeStr = s1fields[1];  //NaI or HPGe
    info.appTypeStr = s1fields[2];  //SPM, RDSC, MRDIS
    info.nchannels = atoi( s1fields[3].c_str() );  //typically 512 or 4096
    info.algorithmVersion = s1fields[4];
    
    if( info.nchannels <= 0 )
    {
      cerr << "parse_s1_info(): Invalid claimed number of channels" << endl;
      info.nchannels = 512;
    }
    
    for( size_t i = 5; i < (s1fields.size()-1); i += 2 )
    {
      DailyFileS1Info::params p;
      
      p.name = s1fields[i];
      p.value = s1fields[i+1];
      
      const string::size_type spacepos = p.name.find(' ');
      if( spacepos != string::npos )
      {
        const string::size_type equalpos = p.name.find('=',spacepos);
        if( equalpos != string::npos )
        {
          p.attval = p.name.substr( equalpos+1 );
          p.attname = p.name.substr( spacepos + 1, equalpos - spacepos - 1 );
          p.name = p.name.substr( 0, spacepos );
        }//if( equalpos != string::npos )
      }//if( spacepos != string::npos )
    }//for( size_t i = 5; i < (s1fields.size()-1); i += 2 )

    //TODO 20200212: it isnt clear how or if energy calibration is set; should investigate.
    //if( info.calibcoefs.empty() )
    //{
    //  cerr << "parse_s1_info(): warning, couldnt find calibration coeffecicents"
    //       << endl;
    //  info.calibcoefs.push_back( 0.0f );
    //  info.calibcoefs.push_back( 3000.0f / std::max(info.nchannels-1, 1) );
    //}//if( info.calibcoefs.empty() )

    info.success = true;
  }//bool parse_s1_info( const std::string &s1str, DailyFileS1Info &info )
  
  void parse_s2_info( const char * const data, const size_t datalen,
                      map<string,vector< pair<float,float> > > &answer )
  {
    const std::string s2str( data, datalen );
    answer.clear();
  
    vector<string> s2fields;
    SpecUtils::split( s2fields, s2str, "," );
    string detname;
    for( size_t i = 1; i < (s2fields.size()-1); )
    {
      const string &field = s2fields[i];
      const string &nextfield = s2fields[i+1];
      
      if( field.empty() || nextfield.empty() )
      {
        i += 2;
        continue;
      }
      
      if( isdigit(field[0]) )
      {
        const float energy = static_cast<float>( atof( field.c_str() ) );
        const float offset = static_cast<float>( atof( nextfield.c_str() ) );
        answer[detname].push_back( pair<float,float>(energy,offset) );
        i += 2;
      }else
      {
        detname = s2fields[i];
        ++i;
      }
    }//for( size_t i = 1; i < (s2fields.size()-1); )
  }//void parse_s2_info(...)
  

  void parse_end_record( const char * const data, const size_t datalen, DailyFileEndRecord &info )
  {
    const std::string str( data, datalen );
    
    vector<string> fields;
    SpecUtils::split( fields, str, "," );
    
    if( fields.size() < 5 )
    {
      info.success = false;
      return;
    }
    
    info.alarmColor = fields[1];
    info.occupancyNumber = atoi( fields[2].c_str() );
    info.lastStartTime = time_from_string( fields[3].c_str() );
    
//    cout << "'" << fields[3] << "'--->" << info.lastStartTime << endl;
    info.icd1FileName = fields[4];
    info.entrySpeed = (fields.size()>5) ? static_cast<float>(atof(fields[5].c_str())) : 0.0f;
    info.exitSpeed = (fields.size()>6) ? static_cast<float>(atof(fields[6].c_str())) : info.entrySpeed;
    
    info.success = true;
  }//bool parse_end_record(...)
  
  void parse_analyzed_background( const char * const data, const size_t datalen,
                                 DailyFileAnalyzedBackground &info )
  {
    const string line( data, datalen );
    std::string::size_type pos1 = line.find( ',' );
    assert( line.substr(0,pos1) == "AB" );
    std::string::size_type pos2 = line.find( ',', pos1+1 );
    if( pos2 == string::npos )
    {
      cerr << "parse_analyzed_background: unexpected EOL 1" << endl;
      info.success = false;
      return;
    }
    
    string type = line.substr( pos1+1, pos2-pos1-1 );
    if( SpecUtils::iequals_ascii( type, "Gamma" ) )
      info.type = DailyFileAnalyzedBackground::Gamma;
    else if( SpecUtils::iequals_ascii( type, "Neutron" ) )
      info.type = DailyFileAnalyzedBackground::Neutrons;
    else
    {
      cerr << "parse_analyzed_background: invalid type '" << type << "'" << endl;
      info.success = false;
      return;
    }
    
    pos1 = pos2;
    info.realTime = static_cast<float>( atof( line.c_str() + pos1 + 1 ) );
    pos1 = line.find( ',', pos1+1 );
    
    if( pos1 == string::npos )
    {
      cerr << "parse_analyzed_background: unexpected EOL 2" << endl;
      info.success = false;
      return;
    }
    
    info.spectrum.reset( new vector<float>() );
    
    if( info.type == DailyFileAnalyzedBackground::Neutrons )
    {
      const float nneut = static_cast<float>( atof( line.c_str() + pos1 + 1 ) );
      info.spectrum->resize( 1, nneut );
    }else
    {
      const char *start = line.c_str() + pos1 + 1;
      const size_t len = line.size() - pos1 - 2;
      const bool success
              = SpecUtils::split_to_floats( start, len, *info.spectrum );
      
      if( !success )
      {
        cerr << "parse_analyzed_background: did not decode spectrum" << endl;
        info.success = false;
        return;
      }
    }//if( neutron ) / ( gamma )
    
    info.success = true;
  }//void parse_analyzed_background(...)
  
  void parse_neutron_signal( const char * const data, const size_t datalen,
                                 DailyFileNeutronSignal &info )
  {
    const string line( data, datalen );
    std::string::size_type pos = line.find( ',' );
    if( pos == string::npos )
    {
      info.success = false;
      return;
    }
    
    const char *start = line.c_str() + pos + 1;
    const size_t len = line.size() - pos - 1;
    
    vector<float> vals;
    const bool success = SpecUtils::split_to_floats( start, len, vals );
    if( !success || vals.size() < 2 )
    {
      cerr << "parse_neutron_signal: did not decode spectrum" << endl;
      info.success = false;
      return;
    }
    
    info.numTimeSlicesAgregated = static_cast<int>( vals[0] );
    info.timeChunkNumber = static_cast<int>( vals[1] );
    info.counts.clear();
    info.counts.insert( info.counts.end(), vals.begin()+2, vals.end() );
    
    info.success = true;
  }//bool parse_analyzed_background
  
  void parse_gamma_signal( const char * const data, const size_t datalen,
                           DailyFileGammaSignal &info )
  {
    const string line( data, datalen );
    std::string::size_type pos1 = line.find( ',' );
    if( pos1 == string::npos )
    {
      info.success = false;
      return;
    }
    
    std::string::size_type pos2 = line.find( ',', pos1+1 );
    if( pos2 == string::npos )
    {
      info.success = false;
      return;
    }
    
    info.detectorName = line.substr( pos1+1, pos2-pos1-1 );
    pos1 = pos2;
    pos2 = line.find( ',', pos1+1 );
    if( pos2 == string::npos )
    {
      info.success = false;
      return;
    }
    
    info.timeChunkNumber = static_cast<int>( atoi( line.c_str() + pos1 + 1 ) );
    info.spectrum.reset( new vector<float>() );
    
    vector<float> vals;
    const char *start = line.c_str() + pos2 + 1;
    const size_t len = line.size() - pos2 - 1;
    const bool success = SpecUtils::split_to_floats( start, len, *info.spectrum );
    if( !success || info.spectrum->size() < 2 )
    {
      cerr << "parse_gamma_signal: did not decode spectrum" << endl;
      info.success = false;
      return;
    }
    
    info.success = true;
    return;
  }//void parse_gamma_signal()

  
  void parse_gamma_background( const char * const data, const size_t datalen,
                               DailyFileGammaBackground &info )
  {
    const string line( data, datalen );
    std::string::size_type pos1 = line.find( ',' );
    if( pos1 == string::npos )
    {
      info.success = false;
      return;
    }
    
    std::string::size_type pos2 = line.find( ',', pos1+1 );
    if( pos2 == string::npos )
    {
      info.success = false;
      return;
    }
    
    info.detectorName = line.substr( pos1+1, pos2-pos1-1 );
    info.spectrum.reset( new vector<float>() );
    
    const char *start = line.c_str() + pos2 + 1;
    const size_t len = line.size() - pos2 - 1;
    const bool success = SpecUtils::split_to_floats( start, len, *info.spectrum );
    if( !success || info.spectrum->size() < 2 )
    {
      cerr << "parse_gamma_background: did not decode spectrum" << endl;
      info.success = false;
      return;
    }
    
    info.success = true;
  }//bool parse_gamma_background(...)
    
  
  void parse_neutron_background( const char * const data, const size_t datalen,
                                 DailyFileNeutronBackground &info )
  {
    const string line( data, datalen );
    std::string::size_type pos1 = line.find( ',' );
    if( pos1 == string::npos )
    {
      info.success = false;
      return;
    }
    
    std::string::size_type pos2 = line.find( ',', pos1+1 );
    if( pos2 == string::npos )
    {
      info.success = false;
      return;
    }
    
    info.realTime = static_cast<float>( atof( line.c_str() + pos1 + 1 ) );
    
    const char *start = line.c_str() + pos2 + 1;
    const size_t len = line.size() - pos2 - 1;
    
    
    //Files like fail: ref1E5GQ2SW76
    const bool success = SpecUtils::split_to_floats( start, len, info.counts );
    if( !success || info.counts.size() < 2 )
    {
      cerr << "parse_neutron_background: did not decode counts" << endl;
      info.success = false;
      return;
    }

    info.success = true;
    return;
  }//bool parse_neutron_background(...)
  
}//namespace SpectroscopicDailyFile


bool SpecFile::load_from_spectroscopic_daily_file( std::istream &input )
{
/* The daily file is a comma separated value file, with a carriage return and
   line feed denoting the end of each line.  The file is saved as a text (.txt) 
   file.  Spaces are not necessary after each comma, in an effort to minimize 
   the overall size of the file.
   In the future, all data provided in the Daily File will be energy calibrated, 
   according to the calibration parameters provided in the S1 and/or S2 lines.
   This is to ensure that calibration is being done correctly and consistently 
   between various institutions, laboratories, and individuals.  The calibration 
   parameters will be provided in case an individual wants to “unwrap” the data 
   back to the source information provided in the ICD-1 file.  This is not done 
   for GS line data as of Revision 3 of this data.
*/
  
  //This is a rough hack in; it would be nice to mmap things and read in that
  //  way, as well as handle potential errors better
  
#define DO_SDF_MULTITHREAD 0
  //Intitial GetLineSafe() for "dailyFile6.txt"
  //  To parse SDF took:  1.027030s wall, 1.000000s user + 0.020000s system = 1.020000s CPU (99.3%)
  //  Total File openeing time was:  1.164266s wall, 1.300000s user + 0.110000s system = 1.410000s CPU (121.1%)
  //
  //Adding an extra indirection of creating a copy of a string
  //  To parse SDF took:  1.162067s wall, 1.120000s user + 0.030000s system = 1.150000s CPU (99.0%)
  //  Total File openeing time was:  1.313293s wall, 1.420000s user + 0.130000s system = 1.550000s CPU (118.0%)
  //
  //Reading the file all at once
  //  To parse SDF took:  1.023828s wall, 0.980000s user + 0.030000s system = 1.010000s CPU (98.6%)
  //  Total File openeing time was:  1.191765s wall, 1.330000s user + 0.160000s system = 1.490000s CPU (125.0%)
  //
  //Making things niavly multithreaded
  //  To parse SDF took:  0.864120s wall, 1.140000s user + 0.110000s system = 1.250000s CPU (144.7%)
  //  Total File openeing time was:  0.995905s wall, 1.410000s user + 0.190000s system = 1.600000s CPU (160.7%)
  //
  //With error checking
  //  To parse SDF took:  0.855769s wall, 1.140000s user + 0.110000s system = 1.250000s CPU (146.1%)
  //
  //With current multithreaded implementation:
  //  To parse SDF took:  0.971223s wall, 0.950000s user + 0.020000s system = 0.970000s CPU (99.9%)
  //  Total File openeing time was:  1.102778s wall, 1.230000s user + 0.110000s system = 1.340000s CPU (121.5%)
  //
  //So I think I'll just stick to single threaded parsing for now since it only
  //  increases speed by ~15% to do mutliithreaded, at the cost of memory.
  //
  //TODO: Check whether creating the Measurment objects in a multithreaded
  //      fashion significantly helps things.
  //      Make it so the worker functions in the SpectroscopicDailyFile
  //      namespace dont re-copy all the strings passed in.
  
  
  using namespace SpectroscopicDailyFile;
  
  int occupancy_num = 0, background_num = 0, s1_num = 0, s2_num = 0;
  vector<DailyFileS1Info> s1infos;
  vector< map<string,vector< pair<float,float> > > > detname_to_devpairs;
  
  map<int,int> background_to_s1_num, background_to_s2_num;
  map<int,int> occupancy_num_to_s1_num, occupancy_num_to_s2_num;
  map<int,vector<std::shared_ptr<DailyFileGammaBackground> > > gamma_backgrounds;
  map<int,std::shared_ptr<DailyFileNeutronBackground> > neutron_backgrounds;  //*should* only have one per background number
  map<int, boost::posix_time::ptime > end_background;
  
  map<int,vector<std::shared_ptr<DailyFileGammaSignal> > > gamma_signal;
  map<int,vector<std::shared_ptr<DailyFileNeutronSignal> > > neutron_signal;
  
  map<int,DailyFileEndRecord> end_occupancy;
  
  //We *should* only have one analyzed background per occupancy, so we'll go
  //  with this assumption
  map<int,DailyFileAnalyzedBackground> analyzed_gamma_backgrounds;
  map<int,DailyFileAnalyzedBackground> analyzed_neutron_backgrounds;
  
  
  set<string> detectorNames;

  reset();
  const istream::pos_type orig_pos = input.tellg();
  
  
  
  //TODO 20180817 - only niavely addressed below:
  //Files like refRA2PVFVA5I look a lot like these types of files because they
  //  are text and start with GB or NB, but instead have formats of
  //NB,000002,000002,000002,000002,00-00-04.841
  //GB,000822,000750,000770,000757,00-00-04.919
  //  with no other line types.  so here we will test for this.
  {//begin test for wierd format
    string line;
    if( !SpecUtils::safe_get_line( input, line, 2048 ) )
      return false;
    int ndash = 0;
    auto pos = line.find_last_of( ',' );
    for( ; pos < line.size(); ++pos )
      ndash += (line[pos] == '-');
    
    if( ndash > 1 || pos == string::npos )
      return false;
    input.seekg( orig_pos, ios::beg );
  }//end test for wierd format
    
  
  
#if( DO_SDF_MULTITHREAD )
  if( !input.good() )
    return false;
  
  vector<char> filedata;
  input.seekg( 0, ios::end );
  const istream::pos_type eof_pos = input.tellg();
  input.seekg( orig_pos, ios::beg );
  
  const size_t filelength = 0 + eof_pos - orig_pos;
  filedata.resize( filelength + 1 );
  input.read( &filedata[0], filelength );
  if( !input.good() )
  {
    input.clear();
    input.seekg( orig_pos, ios::beg );
    return false;
  }
  
  filedata[filelength] = '\0';
  
  size_t pos = 0;
  const char * const data = &filedata[0];
  
  SpecUtilsAsync::ThreadPool pool;
#else
  string line;
#endif
  
  int nUnrecognizedLines = 0, nLines = 0, nGammaLines = 0;
  
#if( DO_SDF_MULTITHREAD )
  while( pos < filelength )
#else
  while( SpecUtils::safe_get_line( input, line ) )
#endif
  {
    
#if( DO_SDF_MULTITHREAD )
    const char * const linestart = data + pos;
    while( pos < filelength && data[pos] != '\r' && data[pos] != '\n' )
      ++pos;
    
    const char * const lineend = data + pos;
    const size_t linelen = lineend - linestart;
    
    ++pos;
    if( pos < filelength && data[pos]=='\n' )
      ++pos;
#else
    const size_t linelen = line.length();
    const char * const linestart = line.c_str();
#endif
    
    ++nLines;
    if( linelen < 3 )
      continue;
    
    const string linetype(linestart,2);
    
    //dates are writtn as yyyy-mm-ddThh:mm:ss.000Z-hh:mm
    
    if( linetype == "S1" )
    {
/*     First line of setup parameters
       “S1” is the first line of Setup Parameters.
       The second element on the S1 line is the type of detector, either “NaI” 
       or “HPGe”.
       The third element on the S1 line is the application, either “SPM”, “RDSC”
       , or “MRDIS”.
       The fourth element on the S1 line is the number of channels used per 
       detector.  This is typically 512 for NaI systems or 4096 for HPGe 
       systems.
       The fifth element on the S1 line is the algorithm version number.  This 
       is taken from the <AlgorithmVersion> tag in the ICD-2 file.
       The next series of elements are variable, and are additional setup 
       parameters taken from the ICD-2 file.  They are the children of the
       <AlgorithmParameter> tag in the ICD-2 file, and alternate <ParameterName>
       , <ParameterValue>.
       For example, if a ParameterName was “NSigma” and the ParameterValue was 
       “8”, and the next ParameterName was “Width” and the ParameterValue was 
       “14”, the S1 line would include the text:
       NSigma, 8, Width, 14
       This process continues through all <ParameterName> and <ParameterValue> 
       elements in the ICD-2 file.
*/
      DailyFileS1Info info;
      parse_s1_info( linestart, linelen, info );
      if( !info.success )
      {
        cerr << "load_from_spectroscopic_daily_file(): S1 line invalid" << endl;
        input.clear();
        input.seekg( orig_pos, ios::beg );
        return false;
      }
      
      s1infos.push_back( info );
      
      s1_num = static_cast<int>( s1infos.size() - 1 );
    }else if( linetype == "S2" )
    {
/*     “S2” is the second line of Setup Parameters.  
       This line provides any detector-specific calibration information that is 
       included in the ICD-1 file as a <NonlinearityCorrection> tag.
       The <NonlinearityCorrection> tags are listed in detector order.  So the 
       S2 line should read:
           S2, Aa1, 81, -5, 122, -6, …, Aa2, 81, -4, 122, -6, …, Aa3, 81, -5, …
       These elements are important for properly calibrating the detectors when 
       plotting spectra.
*/
      map<string,vector< pair<float,float> > > devpairs;
      parse_s2_info( linestart, linelen, devpairs );
      detname_to_devpairs.push_back( devpairs );
      
      s2_num = static_cast<int>( detname_to_devpairs.size() - 1 );
    }else if( linetype == "GB" )
    {
/*
      The monitors will produce a background on a regular, periodic interval.  
      The period is currently every 30 minutes; however, it is conceivable that 
      this could change in the future.  These periodic backgrounds are crucial 
      to evaluating the long-term health of the system as well for detecting and 
      troubleshooting intermittent failures.
*/
/*
      Each gamma detector has its own GB line.
      The specific gamma detector is denoted in the second element of the GB 
      line (e.g., Aa1, Ca3).
      The remaining elements are the channel counts in each channel, in order.  
      These are taken from the <ChannelData> child of the same 
      <BackgroundSpectrum> that the timestamp was derived from.  If 
      “zeros compression” is used, the data must be expanded such that each line 
      has 512 (or 4096) channels of data.  The data must also be energy calibrated.
*/
      if( nGammaLines == 0 )
      {
        //See "TODO 20180817" above
        if( (line.size()<50) && (line.find("00-00-")!=string::npos) )
        {
          cerr << "load_from_spectroscopic_daily_file(): "
          "Not a daily file we can decode (probably - giving up)" << endl;
          input.clear();
          input.seekg( orig_pos, ios::beg );
          
          return false;
        }
      }//if( nGammaLines == 0 )
      
      auto info = make_shared<DailyFileGammaBackground>();
      
#if( DO_SDF_MULTITHREAD )
      pool.post( std::bind( &parse_gamma_background, linestart, linelen, std::ref(*info) ) );
#else
      parse_gamma_background( linestart, linelen, *info );
      
      if( !info->success )  //See "TODO 20180817" above
      {
        cerr << "load_from_spectroscopic_daily_file(): "
                "Error Parsing gamma background" << endl;
        input.clear();
        input.seekg( orig_pos, ios::beg );
        
        return false;
      }//if( !info->success )
      
      detectorNames.insert( info->detectorName );
#endif
      ++nGammaLines;
      gamma_backgrounds[background_num].push_back( info );
    }else if( linetype == "NB" )
    {
/*    All neutron detectors are combined on a single NB line.
      The second element of the NB line is the duration of the background, in 
      seconds.  To help align columns, the duration in seconds should always be 
      a three-digit number (e.g., 030 for a 30-second background).  The daily 
      file assumes that the duration of the neutron background is identical to 
      the duration of the gamma background, which is supported by operational 
      observations.
      The remaining elements of the NB line are the counts for each neutron 
      detector recorded over the background period.  The detectors are listed in 
      order – Aa1N, then Aa2N, then Ba1N, etc.
*/
      auto info = make_shared<DailyFileNeutronBackground>();
      
#if( DO_SDF_MULTITHREAD )
      pool.post( std::bind(&parse_neutron_background, linestart, linelen, std::ref(*info) ) );
#else
      parse_neutron_background( linestart, linelen, *info );
      
      if( !info->success )
      {
        cerr << "load_from_spectroscopic_daily_file(): "
                "Error Parsing neutron background" << endl;
        
        input.clear();
        input.seekg( orig_pos, ios::beg );
        
        return false;
      }
#endif
      
      neutron_backgrounds[background_num] = info;
    }else if( linetype == "BX" )
    {
/*    Then end of the background is denoted by a BX line.
      The second element of the BX line is the timestamp of the background.  The 
      timestamp is the <StartTime> child to the <BackgroundSpectrum> tag in the 
      periodic background files.
*/
      if( linelen < 4 )
      {
        cerr << "load_from_spectroscopic_daily_file(): invalid BX line lenght"
             << endl;
        
        input.clear();
        input.seekg( orig_pos, ios::beg );
        
        return false;
      }
      
      const string line( linestart, linelen );
      end_background[background_num] = time_from_string( line.c_str() + 3 );
      background_to_s1_num[background_num] = s1_num;
      background_to_s2_num[background_num] = s2_num;
      
      ++background_num;
    }else if( linetype == "GS" )
    {
/*    Signals – GS and NS
      ICD-1 file contains large amounts of pre- and post-occupancy data, all 
      recorded in 0.1 second intervals.  These high-frequency, long-duration 
      measurements significantly increase the size of the ICD-1 file.  This 
      level of data fidelity is useful for detailed analysis; however, the cost 
      would be too great to use these as daily files, and is not warranted for 
      daily file analysis.  The signal lines in the daily file make two 
      important concessions to reduce overall file size:
        -The only data included in the daily file is occupied data.  Pre- and 
         post-occupancy data are discarded.
        -The data is only recorded in 1 second intervals, not 0.1 second 
         intervals.
      Future versions of this document may relax some of these concessions to 
      include data other than occupied, or at a higher frequency.
*/
/*    The second element of the GS line is the gamma detector name, for example 
      Aa1 or Da3.
      The third element of the GS line is the time chunk number, starting from 
      001.  In this version of the specification, the first ten time slices will 
      be aggregated into the first time chunk (001); the next ten time slices 
      will be aggregated into the second time chunk (002); and so on, resulting 
      in 1 second time chunks.  In the future, it is conceivable that a 
      different time chunk size (0.5 second, 0.2 second, or 2 seconds) may be 
      utilized.  The time chunk number serves as a timestamp within the occupancy.
      The remaining elements of the GS line are the counts in each channel for 
      that detector, aggregated over one second and energy calibrated per the 
      parameters provided in the S1 and S2 lines (and any other source as 
      necessary).  These are taken directly from the <ChannelData> elements.  
      Unfortunately, since these are taken directly from the ICD-1 file, GS line 
      data is not energy calibrated as of this version.
*/
      std::shared_ptr<DailyFileGammaSignal> info( new DailyFileGammaSignal );
      
#if( DO_SDF_MULTITHREAD )
      pool.post( std::bind(&parse_gamma_signal, linestart, linelen, std::ref(*info) ) );
#else
      parse_gamma_signal( linestart, linelen, *info );
      
      if( !info->success )
      {
        cerr << "load_from_spectroscopic_daily_file(): "
                "Error Parsing gamma signal" << endl;
        
        input.clear();
        input.seekg( orig_pos, ios::beg );
        
        return false;
      }//if( !info->success )
      
      detectorNames.insert( info->detectorName );
#endif
      ++nGammaLines;
      gamma_signal[occupancy_num].push_back( info );
    }else if( linetype == "NS" )
    {
/*    Neutron Signal
      The second element of the NS line is the number of time slices used to
      form one time chunk, represented as a 3 digit number to help align 
      columns.  In this case, since ten time slices contribute to each chunk, 
      the second element of the NS line should read, “010”.  (Again, future 
      versions could change this to 005 or 002 or 020.)
      The third element of the NS line is the time chunk number.  This should be 
      identical to the time chunk number used in the gamma signal.
      The remaining elements of the NS line are the counts from each detector 
      for the one second interval.  These are taken directly from the 
      <ChannelData> elements.  The signals are listed in order of the detectors: 
      Aa1N, Aa2N, Ba1N, and so forth.
*/
      std::shared_ptr<DailyFileNeutronSignal> info( new DailyFileNeutronSignal() );
      
#if( DO_SDF_MULTITHREAD )
      pool.post( std::bind(&parse_neutron_signal,linestart, linelen, std::ref(*info) ) );
#else
      parse_neutron_signal( linestart, linelen, *info );
      
      if( !info->success )
      {
        cerr << "load_from_spectroscopic_daily_file(): "
                "Error Parsing neutron signal" << endl;
        
        input.clear();
        input.seekg( orig_pos, ios::beg );
        
        return false;
      }//if( !info->success )
#endif
      
      neutron_signal[occupancy_num].push_back( info );
    }else if( linetype == "ID" )
    {
/*    One line is provided for each radionuclide identification.  Even if 
      multiple identifications are made within the same detector subset and time 
      slices, a new line should be provided for each radionuclide identified.  
      If a radionuclide is identified multiple times within the same occupancy 
      (based on different time slices or different detector subsets), a separate
      line should be provided for each ID.
      The second element of the ID line is the radionuclide identified.  The 
      nuclide ID comes from the <NuclideName> tag in the ICD-2 file.
      The next elements of the ID line are the detectors that were used to make
      the identification.  These stem from the <SubsetSampleList> element in the 
      ICD-2 file.
      The next elements of the ID line are the time slices that were used in 
      making the identification.  These are taken directly from the 
      <SubsetSampleList> tag name in the ICD-2 file.
      The ID field will state “NONE” if no radionuclide was identified.  This is 
      made clear if the <AlarmDescription> tag reads “No Alarm”.
*/
      //ToDo, implement this
    }else if( linetype == "AB" )
    {
/*    When evaluating an alarm, the background used by the algorithm is 
      extremely important to capture.  This is provided in the ICD-2 file as a
      common background aggregated over all gamma detectors, over a long period 
      of time – typically 300 seconds.
*/
/*    The second element of the AB line is “Neutron” for the neutron background.
      The third element of the AB line is the duration of the background, in 
      seconds.  This is taken from the <RealTime> child of the
      <BackgroundSpectrum> element in the ICD-2 file, the same as the gamma 
      background.
      The fourth element of the AB line is the <BackgroundCounts> child of the 
      <GrossCountSummed> element.  This is the sum of the counts from all 
      neutron detectors over the background period.
*/
      DailyFileAnalyzedBackground info;
      parse_analyzed_background( linestart, linelen, info );
      
      if( !info.success )
      {
        cerr << "load_from_spectroscopic_daily_file(): "
                "Error Parsing analyzed background" << endl;
        
        input.clear();
        input.seekg( orig_pos, ios::beg );
        
        return false;
      }
      
      if( info.type == DailyFileAnalyzedBackground::Gamma )
        analyzed_gamma_backgrounds[occupancy_num] = info;
      else
        analyzed_neutron_backgrounds[occupancy_num] = info;
    }else if( linetype == "GX" )
    {
/*    The end of the occupancy is denoted by a GX line.
      The second element on the GX line is the alarm light color, either Red, 
      Yellow, or Green, taken from the <AlarmLightColor> tag in the ICD-2 file.  
      This is useful for categorizing alarms in follow-up analysis.
      The third element on the GX line is the occupancy number, taken from the 
      <occupancyNumber> tag in the ICD-2 file.
      The fourth element on the GX line is the timestamp, taken from the last 
      time stamp in the ICD-1 file.  This is the <StartTime> child of the last 
      <DetectorData> element in the file.  This methodology should also work for 
      the RDSC, which does not record pre- and post-occupancy data.
      The fifth element of the GX line is the filename of the ICD-1 file that 
      provided data for this occupancy.  This information may be useful in case 
      the actual ICD-1 file is needed for additional analysis.
      The sixth element on the GX line is the entry speed, taken from the 
      <vehicleEntrySpeed> tag in the ICD-2 file.
      The seventh element on the GX line is the exit speed, taken from the 
      <vehicleExitSpeed> tag in the ICD-2 file.  This sixth element (exit speed) 
      may or may not exist, if the monitor records it.
*/
      
      DailyFileEndRecord info;
      parse_end_record( linestart, linelen, info );
      
      if( !info.success )
      {
        cerr << "load_from_spectroscopic_daily_file(): "
                "Error Parsing end of record line" << endl;
        
        input.clear();
        input.seekg( orig_pos, ios::beg );
        
        return false;
      }
      
      end_occupancy[occupancy_num] = info;
      
      occupancy_num_to_s1_num[occupancy_num] = s1_num;
      occupancy_num_to_s2_num[occupancy_num] = s2_num;
      
      ++occupancy_num;
    }else
    {
      string line(linestart, linelen);
      SpecUtils::trim( line );
      
      if( !line.empty() )
      {
        ++nUnrecognizedLines;
        const double fracBad = double(nUnrecognizedLines) / nLines;
        if( (nUnrecognizedLines > 10) && (fracBad > 0.1) )
        {
          input.clear();
          input.seekg( orig_pos, ios::beg );
          return false;
        }
      
        cerr << "load_from_spectroscopic_daily_file, unrecognized line begining: "
             << linetype << endl;
      }//if( !line.empty() )
    }//if / else (determine what this line means)
  }//while( SpecUtils::safe_get_line( input, line ) )
  
#if( DO_SDF_MULTITHREAD )
  pool.join();
#endif
  
  //Probably not necassary, but JIC
  background_to_s1_num[background_num] = s1_num;
  background_to_s2_num[background_num] = s2_num;
  occupancy_num_to_s1_num[occupancy_num] = s1_num;
  occupancy_num_to_s2_num[occupancy_num] = s2_num;
  
  //TODO: convert so that we are sure we are using the correct setup, incase
  //      there are multiple setup lines
  //Probably just create a struct to hold the information, and parse all the
  //      setups.

  
  //Heres what we have to work with:
  if( s1infos.empty() )
  {
    cerr << "load_from_spectroscopic_daily_file(): Either S1 line missing"
         << endl;
    
    input.clear();
    input.seekg( orig_pos, ios::beg );
    
    return false;
  }//
  

#if( DO_SDF_MULTITHREAD )
  //The below two loops are probably quite wasteful, and not necassary
  for( map<int,vector<std::shared_ptr<DailyFileGammaBackground> > >::const_iterator i = gamma_backgrounds.begin();
      i != gamma_backgrounds.end(); ++i )
  {
    for( size_t j = 0; j < i->second.size(); ++j )
      detectorNames.insert( i->second[j]->detectorName );
  }

  for( map<int,vector<std::shared_ptr<DailyFileGammaSignal> > >::const_iterator i = gamma_signal.begin();
      i != gamma_signal.end(); ++i )
  {
    for( size_t j = 0; j < i->second.size(); ++j )
      detectorNames.insert( i->second[j]->detectorName );
  }
#endif  //#if( DO_SDF_MULTITHREAD )
  
  
  map<string,int> detNameToNum;
  int detnum = 0;
  for( const string &name : detectorNames )
    detNameToNum[name] = detnum++;

  vector< std::shared_ptr<Measurement> > backgroundMeasurments, signalMeasurments;
  
  int max_occupancie_num = 0;
  
  for( int occnum = 0; occnum < occupancy_num; ++occnum )
  {
    const map<int,int>::const_iterator s1pos = occupancy_num_to_s1_num.find(occnum);
    const map<int,int>::const_iterator s2pos = occupancy_num_to_s2_num.find(occnum);
    
    if( s1pos == occupancy_num_to_s1_num.end() || s1pos->second >= int(s1infos.size()) )
    {
      cerr << "Serious programing logic error in "
      "load_from_spectroscopic_daily_file(): 0" << endl;
      
      input.clear();
      input.seekg( orig_pos, ios::beg );
      
      return false;
    }
    
    const DailyFileS1Info &sinfo = s1infos[s1pos->second];
    const map<string,vector< pair<float,float> > > *devpairs = 0;
    if( s2pos != occupancy_num_to_s2_num.end() && s2pos->second<int(detname_to_devpairs.size()))
      devpairs = &(detname_to_devpairs[s2pos->second]);

    const map<int,vector<std::shared_ptr<DailyFileGammaSignal> > >::const_iterator gammaiter
                                                  = gamma_signal.find( occnum );
    if( gammaiter == gamma_signal.end() )
    {
      cerr << "Serious programing logic error in "
      "load_from_spectroscopic_daily_file(): 0.1" << endl;
      
      input.clear();
      input.seekg( orig_pos, ios::beg );
      
      return false;
    }//if( gammaiter == gamma_signal.end() )
    
    const map<int,vector<std::shared_ptr<DailyFileNeutronSignal> > >::const_iterator neutiter
                                                = neutron_signal.find( occnum );
    
    const map<int,DailyFileEndRecord>::const_iterator endrecorditer
                                                 = end_occupancy.find( occnum );
    if( endrecorditer == end_occupancy.end() )
    {
      cerr << "Serious programing logic error in "
      "load_from_spectroscopic_daily_file(): 0.2" << endl;
      
      input.clear();
      input.seekg( orig_pos, ios::beg );
      
      return false;
    }//if( endrecorditer == end_occupancy.end() )
    
    const DailyFileEndRecord &endrecord = endrecorditer->second;
    const vector<std::shared_ptr<DailyFileGammaSignal> > &gammas = gammaiter->second;
    const vector<std::shared_ptr<DailyFileNeutronSignal> > *nutsignal = 0;
    if( neutiter != neutron_signal.end() )
      nutsignal = &neutiter->second;
    
    const DailyFileAnalyzedBackground *gammaback = 0, *neutback = 0;
    map<int,DailyFileAnalyzedBackground>::const_iterator backiter;
    backiter = analyzed_gamma_backgrounds.find( occnum );
    if( backiter != analyzed_gamma_backgrounds.end() )
      gammaback = &backiter->second;
    backiter = analyzed_neutron_backgrounds.find( occnum );
    if( backiter != analyzed_neutron_backgrounds.end() )
      neutback = &backiter->second;
    
    //Place the analyzed background into signalMeasurments
    if( gammaback )
    {
      std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
      
      meas->detector_number_    = static_cast<int>( detNameToNum.size() );
      meas->detector_name_      = "sum";
      meas->gamma_counts_       = gammaback->spectrum;
      meas->sample_number_      = 1000*endrecord.occupancyNumber;
      meas->source_type_        = SourceType::Background;
      meas->occupied_           = OccupancyStatus::NotOccupied;
      if( !sinfo.calibcoefs.empty() )
      {
        meas->energy_calibration_model_  = SpecUtils::EnergyCalType::Polynomial;
        meas->calibration_coeffs_ = sinfo.calibcoefs;
      }
      
      meas->remarks_.push_back( "Analyzed Background (sum over all detectors" );
      meas->real_time_ = meas->live_time_ = 0.1f*detNameToNum.size()*gammaback->realTime;
      
/*
      meas->start_time_         = endrecord.lastStartTime;
      if( gammas.size() )
      {
        //This is a bit of a hack; I want the analyzed backgrounds to appear
        //  just before the analyzed spectra, so to keep this being the case
        //  we have to falsify the time a bit, because the measurments will get
        //  sorted later on according to start time
        const int totalChunks = gammas[gammas.size()-1].timeChunkNumber;
        
        const DailyFileNeutronSignal *neut = 0;
        if( nutsignal && nutsignal->size() )
          neut = &(*nutsignal)[0];
        
        const float realTime = neut ? 0.1f*neut->numTimeSlicesAgregated : 1.0f;
        const float timecor = realTime * (totalChunks - 0.5);
        const boost::posix_time::seconds wholesec( static_cast<int>(floor(timecor)) );
        const boost::posix_time::microseconds fracsec( static_cast<int>(1.0E6 * (timecor-floor(timecor))) );
        meas->start_time_ -= wholesec;
        meas->start_time_ -= fracsec;
        
        cout << "Background meas->sample_number_=" << meas->sample_number_ << " has time " << meas->start_time_ << endl;
      }//if( gammas.size() )
*/
      
      if( neutback && !neutback->spectrum )
      {
        meas->neutron_counts_ = *neutback->spectrum;
        meas->neutron_counts_sum_ = 0.0;
        for( const float f : meas->neutron_counts_ )
          meas->neutron_counts_sum_ += f;
        meas->contained_neutron_ = true;
      }//if( neutback )
      
      meas->gamma_count_sum_ = 0.0;
      if( !!meas->gamma_counts_ )
      {
        for( const float f : *meas->gamma_counts_ )
          meas->gamma_count_sum_ += f;
      }//if( !!meas->gamma_counts_ )
      
      signalMeasurments.push_back( meas );
//      backgroundMeasurments.push_back( meas );
    }//if( gammaback )
    
    for( size_t i = 0; i < gammas.size(); ++i )
    {
      const DailyFileNeutronSignal *neut = 0;
      const DailyFileGammaSignal &gamma = *gammas[i];
      
#if( DO_SDF_MULTITHREAD )
      if( !gamma.success )
      {
        cerr << "load_from_spectroscopic_daily_file(): "
                "Error Parsing gamma signal" << endl;
        
        input.clear();
        input.seekg( orig_pos, ios::beg );
        
        return false;
      }//if( !gamma.success )
#endif
      
      if( nutsignal )
      {
        for( size_t j = 0; j < nutsignal->size(); ++j )
        {
          if( (*nutsignal)[j]->timeChunkNumber == gamma.timeChunkNumber )
          {
            neut = ((*nutsignal)[j]).get();
            break;
          }
        }//for( size_t j = 0; j < nutsignal->size(); ++j )
      }//if( nutsignal )
      
#if( DO_SDF_MULTITHREAD )
      if( neut && !neut->success )
      {
        cerr << "load_from_spectroscopic_daily_file(): "
                "Error Parsing neutron signal" << endl;
        
        input.clear();
        input.seekg( orig_pos, ios::beg );
        
        return false;
      }//if( neut && !neut->success )
#endif
      
      std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
      
      meas->detector_number_    = detNameToNum[gamma.detectorName];
      meas->detector_name_      = gamma.detectorName;
      meas->gamma_counts_       = gamma.spectrum;
      meas->sample_number_      = 1000*endrecord.occupancyNumber + gamma.timeChunkNumber;
      meas->source_type_        = SourceType::Foreground;
      meas->occupied_           = OccupancyStatus::Occupied;
      if( !sinfo.calibcoefs.empty() )
      {
        meas->energy_calibration_model_  = SpecUtils::EnergyCalType::Polynomial;
        meas->calibration_coeffs_ = sinfo.calibcoefs;
      }
      meas->speed_              = 0.5f*(endrecord.entrySpeed + endrecord.exitSpeed);
      meas->start_time_         = endrecord.lastStartTime;
      meas->remarks_.push_back( "ICD1 Filename: " + endrecord.icd1FileName );
      meas->remarks_.push_back( "Alarm Color: " + endrecord.alarmColor );
      meas->remarks_.push_back( "Occupancy Number: " + std::to_string(endrecord.occupancyNumber) );
      
      max_occupancie_num = std::max( endrecord.occupancyNumber, max_occupancie_num );
      
      meas->gamma_count_sum_ = 0.0;
      if( !!meas->gamma_counts_ )
      {
        for( const float f : *meas->gamma_counts_ )
          meas->gamma_count_sum_ += f;
      }
      
      meas->contained_neutron_ = false;
      meas->live_time_ = meas->real_time_ = 1.0f;
      if( neut )
      {
        meas->live_time_ = meas->real_time_ = 0.1f*neut->numTimeSlicesAgregated;
        
        if( meas->detector_number_ < static_cast<int>(neut->counts.size()) )
        {
          meas->neutron_counts_sum_ = neut->counts[meas->detector_number_];
          meas->neutron_counts_.resize( 1 );
          meas->neutron_counts_[0] = static_cast<float>( meas->neutron_counts_sum_ );
          meas->contained_neutron_ = true;
        }else
        {
          meas->neutron_counts_sum_ = 0.0;
        }
      }//if( neut )
      
      const int totalChunks = gammas[gammas.size()-1]->timeChunkNumber;
      const float dtMeasStart = meas->real_time_ * (totalChunks - 1);
      const float timecor = dtMeasStart * float(totalChunks-gamma.timeChunkNumber)/float(totalChunks);
      const boost::posix_time::seconds wholesec( static_cast<int>(floor(timecor)) );
      const boost::posix_time::microseconds fracsec( static_cast<int>(1.0E6 * (timecor-floor(timecor))) );
      meas->start_time_ -= wholesec;
      meas->start_time_ -= fracsec;
      
      signalMeasurments.push_back( meas );
    }//for( size_t i = 0; i < gammas.size(); ++i )
  }//for( int occnum = 0; occnum < occupancy_num; ++occnum )

  
  
  for( int backnum = 0; backnum < background_num; ++backnum )
  {
    const map<int,int>::const_iterator s1pos = background_to_s1_num.find(backnum);
    const map<int,int>::const_iterator s2pos = background_to_s2_num.find(backnum);
  
    if( s1pos == background_to_s1_num.end() || s1pos->second >= int(s1infos.size()) )
    {
      cerr << "Serious programing logic error in "
              "load_from_spectroscopic_daily_file(): 1" << endl;
      
      input.clear();
      input.seekg( orig_pos, ios::beg );
      
      return false;
    }
    
    const DailyFileS1Info &sinfo = s1infos[s1pos->second];
    const map<string,vector< pair<float,float> > > *devpairs = 0;
	const int ndets = static_cast<int>( detname_to_devpairs.size() );
    if( s2pos != background_to_s2_num.end() && s2pos->second < ndets )
      devpairs = &(detname_to_devpairs[s2pos->second]);

    const map<int,vector<std::shared_ptr<DailyFileGammaBackground> > >::const_iterator gammaback
                                             = gamma_backgrounds.find(backnum);
    
    const map<int,std::shared_ptr<DailyFileNeutronBackground> >::const_iterator neutback
                                           = neutron_backgrounds.find(backnum);
    
    const map<int, boost::posix_time::ptime >::const_iterator backtimestamp
                                                = end_background.find(backnum);
    
    if( gammaback == gamma_backgrounds.end() )
    {
      cerr << "Serious programing logic error in "
              "load_from_spectroscopic_daily_file(): 1.1" << endl;
      
      input.clear();
      input.seekg( orig_pos, ios::beg );
      
      return false;
    }
    
    if( backtimestamp == end_background.end() )
    {
      cerr << "Serious programing logic error in "
              "load_from_spectroscopic_daily_file(): 1.2" << endl;
      
      input.clear();
      input.seekg( orig_pos, ios::beg );
      
      return false;
    }
    
    const vector<std::shared_ptr<DailyFileGammaBackground> > &backgrounds = gammaback->second;
    const boost::posix_time::ptime &timestamp = backtimestamp->second;
    
    for( size_t i = 0; i < backgrounds.size(); ++i )
    {
      const DailyFileGammaBackground &back = *backgrounds[i];
      
#if( DO_SDF_MULTITHREAD )
      if( !back.success )
      {
        cerr << "load_from_spectroscopic_daily_file(): "
                "Error Parsing gamma background" << endl;
        
        input.clear();
        input.seekg( orig_pos, ios::beg );
        
        return false;
      }//if( !back.success )
#endif
      
      std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
      
      meas->source_type_        = SourceType::Background;
      meas->detector_name_      = back.detectorName;
      meas->detector_number_    = detNameToNum[back.detectorName];
      meas->gamma_counts_       = back.spectrum;
      meas->start_time_         = timestamp;
      if( !sinfo.calibcoefs.empty() )
      {
        meas->energy_calibration_model_  = SpecUtils::EnergyCalType::Polynomial;
        meas->calibration_coeffs_ = sinfo.calibcoefs;
      }
      meas->occupied_           =  OccupancyStatus::NotOccupied;
      
      meas->sample_number_ = 1000*(max_occupancie_num+1) + backnum;
      
      if( !meas->gamma_counts_ )
        cerr << "Warning, invalid gamma counts" << endl;
      else if( static_cast<int>(meas->gamma_counts_->size()) != sinfo.nchannels )
        cerr << "WArning, mismatch in spectrum size, got "
             << meas->gamma_counts_->size() << " expected " << sinfo.nchannels
             << endl;
      
      if( devpairs )
      {
        map<string,vector< pair<float,float> > >::const_iterator pos
                                      = devpairs->find( meas->detector_name_ );
        meas->deviation_pairs_ = pos->second;
      }//if( devpairs )
      
      meas->gamma_count_sum_ = 0.0;
      if( !!meas->gamma_counts_ )
      {
        for( const float f : *meas->gamma_counts_ )
          meas->gamma_count_sum_ += f;
      }//if( !!meas->gamma_counts_ )
      
      meas->contained_neutron_ = false;
      if( neutback != neutron_backgrounds.end() )
      {
        const DailyFileNeutronBackground &neutbackground = *neutback->second;
        
#if( DO_SDF_MULTITHREAD )
        if( !neutbackground.success )
        {
          cerr << "load_from_spectroscopic_daily_file(): "
                  "Error Parsing neutron background" << endl;
          
          input.clear();
          input.seekg( orig_pos, ios::beg );
          
          return false;
        }
#endif
        
        meas->live_time_ = meas->real_time_ = neutbackground.realTime;
        const int nneutdet = static_cast<int>( neutbackground.counts.size() );
        if( meas->detector_number_ < nneutdet )
        {
          const float counts = neutbackground.counts[meas->detector_number_];
          meas->neutron_counts_.resize( 1 );
          meas->neutron_counts_[0] = counts;
          meas->neutron_counts_sum_ = counts;
          meas->contained_neutron_ = true;
        }//if( meas->detector_number_ < neutbackground.counts.size() )
      }//if( neutback != neutron_backgrounds.end() )
      
      backgroundMeasurments.push_back( meas );
    }//for( size_t i = 0; i < backgrounds.size(); ++i )
  }//for( int backnum = 0; backnum < background_num; ++backnum )
  
  
  for( std::shared_ptr<Measurement> &m : signalMeasurments )
    measurements_.push_back( m );
  
  for( std::shared_ptr<Measurement> &m : backgroundMeasurments )
    measurements_.push_back( m );

  
  for( size_t i = 0; i < s1infos.size(); ++i )
  {
    const DailyFileS1Info &sinfo = s1infos[i];
    remarks_.push_back( "Algorithm Version: " + sinfo.algorithmVersion );
    remarks_.push_back( "Portal Type: " + sinfo.appTypeStr );  //SPM, RDSC, MRDIS
    instrument_type_ = sinfo.detTypeStr;
    
    if( sinfo.appTypeStr == "SPM" )
      instrument_model_ = "ASP";
    else if( sinfo.appTypeStr == "RDSC" )
      instrument_model_ = "Radiation Detector Straddle Carrier";
    else if( sinfo.appTypeStr == "MRDIS" )
      instrument_model_ = "Mobile Radiation Detection and Identification System";

    for( const DailyFileS1Info::params &p : sinfo.parameters )
    {
      string remark = p.name + " = " + p.value;
      if( p.attname.size() && p.attval.size() )
        remark += ", " + p.attname + " = " +p.attval;
      remarks_.push_back( remark );
    }//for( const DailyFileS1Info::params &p : sinfo.parameters )
  }//for( size_t i = 0; i < s1infos.size(); ++i )
  
  
#if( SpecUtils_REBIN_FILES_TO_SINGLE_BINNING )
  cleanup_after_load( StandardCleanup | DontChangeOrReorderSamples );
#else
  cleanup_after_load();
#endif
  
  //if( properties_flags_ & kNotSampleDetectorTimeSorted )
  //  cerr << "load_from_spectroscopic_daily_file: kNotSampleDetectorTimeSorted is set" << endl;
  //
  //if( properties_flags_ & kNotTimeSortedOrder )
  //  cerr << "load_from_spectroscopic_daily_file: kNotTimeSortedOrder is set" << endl;
  //
  //if( properties_flags_ & kNotUniqueSampleDetectorNumbers )
  //  cerr << "load_from_spectroscopic_daily_file: kNotUniqueSampleDetectorNumbers is set" << endl;
  //
  //if( properties_flags_ & kAllSpectraSameNumberChannels )
  //  cerr << "load_from_spectroscopic_daily_file: kAllSpectraSameNumberChannels is set" << endl;
  //
  //if( properties_flags_ & kHasCommonBinning )
  //  cerr << "load_from_spectroscopic_daily_file: kHasCommonBinning is set" << endl;


  return true;
}//bool load_from_spectroscopic_daily_file( std::istream &input )



bool SpecFile::load_from_srpm210_csv( std::istream &input )
{
  try
  {
    string line;
    if( !SpecUtils::safe_get_line(input, line) )
      return false;
    
    if( line.find("Fields, RSP 1, RSP 2") == string::npos )
      return false;
    
    vector<string> header;
    SpecUtils::split( header, line, "," );
    if( header.size() < 3 )
      return false; //we know this cant happen, but whatever
    header.erase( begin(header) );  //get rid of "Fields"
    
#if(PERFORM_DEVELOPER_CHECKS)
    set<string> header_names_check;
#endif
    for( auto &field : header )
    {
      SpecUtils::trim( field );
      if( field.size() < 2 )
      {
#if(PERFORM_DEVELOPER_CHECKS)
        header_names_check.insert( field );
#endif
        continue; //JIC, shouldnt ever hit though
      }
      
      //Transform "RSP 1" to "RSP 01", so this way when names get sorted
      //  "RSP 11" wont come before "RSP 2"
      if( isdigit( field[field.size()-1] ) && !isdigit( field[field.size()-2] ) )
        field = field.substr(0,field.size()-1) + '0' + field.substr(field.size()-1);
      
#if(PERFORM_DEVELOPER_CHECKS)
      header_names_check.insert( field );
#endif
    }//for( auto &field : header )
    
#if(PERFORM_DEVELOPER_CHECKS)
    if( header_names_check.size() != header_names_check.size() )
      log_developer_error( __func__, ("There was a duplicate detector name in SRPM CSV file: '" + line + "' - who knows what will happen").c_str() );
#endif
    
    
    vector<float> real_times, live_times;
    vector<vector<float>> gamma_counts, neutron_counts;
    
    while( SpecUtils::safe_get_line(input, line) )
    {
      SpecUtils::trim( line );
      if( line.empty() )
        continue;
      
      auto commapos = line.find(',');
      if( commapos == string::npos )
        continue;  //shouldnt happen
      
      const string key = line.substr( 0, commapos );
      line = line.substr(commapos+1);
      
      //All columns, other than the first are integral, however for conveince
      //  we will just read them into floats.  The time (in microseconds) would
      //  maybe be the only thing that would lose info, but it will be way to
      //  minor to worry about.
      
      //Meh, I dont think we care about any of the following lines
      const string lines_to_skip[] = { "PLS_CNTR", "GOOD_CNTR", "PU_CNTR",
        "COSM_CNTR", "PMT_COUNTS_1", "PMT_COUNTS_2", "PMT_COUNTS_3",
        "PMT_COUNTS_4", "XRAY_CNTR"
      };
      
      if( std::find(begin(lines_to_skip), end(lines_to_skip), key) != end(lines_to_skip) )
        continue;
      
      vector<float> line_data;
      if( !SpecUtils::split_to_floats(line, line_data) )
      {
#if(PERFORM_DEVELOPER_CHECKS)
        log_developer_error( __func__, ("Failed in parsing line of SRPM file: '" + line + "'").c_str() );
#endif
        continue;
      }
      
      if( line_data.empty() )
        continue;
      
      if( key == "ACC_TIME_us" )
      {
        real_times.swap( line_data );
      }else if( key == "ACC_TIME_LIVE_us" )
      {
        live_times.swap( line_data );
      }else if( SpecUtils::istarts_with( key, "Spectrum_") )
      {
        if( gamma_counts.size() < line_data.size() )
          gamma_counts.resize( line_data.size() );
        for( size_t i = 0; i < line_data.size(); ++i )
          gamma_counts[i].push_back(line_data[i]);
      }else if( SpecUtils::istarts_with( key, "Ntr_") )
      {
        if( SpecUtils::icontains(key, "Total") )
        {
          if( neutron_counts.size() < line_data.size() )
            neutron_counts.resize( line_data.size() );
          for( size_t i = 0; i < line_data.size(); ++i )
            neutron_counts[i].push_back(line_data[i]);
        }else if( SpecUtils::icontains(key, "Low")
                 || SpecUtils::icontains(key, "High")
                 || SpecUtils::icontains(key, "_Neutron") )
        {
          //Meh, ignore this I guess
        }else
        {
#if(PERFORM_DEVELOPER_CHECKS)
          log_developer_error( __func__, ("Unrecognized neutron type in SRPM file: '" + key + "'").c_str() );
#endif
        }
      }else
      {
#if(PERFORM_DEVELOPER_CHECKS)
        log_developer_error( __func__, ("Unrecognized line type in SRPM file: '" + key + "'").c_str() );
#endif
      }//if( key is specific value ) / else
    }//while( SpecUtils::safe_get_line(input, line) )
    
    if( gamma_counts.empty() )
      return false;
    
    reset();
    
    
    for( size_t i = 0; i < gamma_counts.size(); ++i )
    {
      vector<float> &gammacount = gamma_counts[i];
      if( gammacount.size() < 7 ) //7 is arbitrary
        continue;
      
      float livetime = 0.0f, realtime = 0.0f;
      if( i < live_times.size() )
        livetime = 1.0E-6f * live_times[i];
      if( i < real_times.size() )
        realtime = 1.0E-6f * real_times[i];
      
      //JIC something is whack getting time, hack it! (shouldnt happen that I'm aware of)
      if( livetime==0.0f && realtime!=0.0f )
        realtime = livetime;
      if( realtime==0.0f && livetime!=0.0f )
        livetime = realtime;
    
      auto m = std::make_shared<Measurement>();
      
      if( i < header.size() )
        m->detector_name_ = header[i];
      else
        m->detector_name_ = "Det" + to_string(i);
      m->detector_number_ = static_cast<int>( i );
      m->real_time_ = realtime;
      m->live_time_ = livetime;
      m->detector_description_ = "PVT";
      m->gamma_counts_ = std::make_shared<vector<float>>( gammacount );
      if( i < neutron_counts.size() )
        m->neutron_counts_ = neutron_counts[i];
      for( const float counts : *m->gamma_counts_ )
        m->gamma_count_sum_ += counts;
      for( const float counts : m->neutron_counts_ )
        m->neutron_counts_sum_ += counts;
      m->contained_neutron_ = !m->neutron_counts_.empty();
      m->sample_number_ = 1;
      
      //Further quantities it would be nice to fill out:
      /*
      OccupancyStatus  occupied_;
      float speed_;  //in m/s
      QualityStatus quality_status_;
      SourceType     source_type_;
      SpecUtils::EnergyCalType   energy_calibration_model_;
      std::vector<std::string>  remarks_;
      boost::posix_time::ptime  start_time_;
      std::vector<float>        calibration_coeffs_;  //should consider making a shared pointer (for the case of LowerChannelEdge binning)
      std::vector<std::pair<float,float>>          deviation_pairs_;     //<energy,offset>
       std::vector<float>        neutron_counts_;
      double latitude_;  //set to -999.9 if not specified
      double longitude_; //set to -999.9 if not specified
      boost::posix_time::ptime position_time_;
      std::string title_;  //Actually used for a number of file formats
      */
      measurements_.push_back( m );
      
    }//for( size_t i = 0; i < gamma_counts.size(); ++i )
    
    detector_type_ = DetectorType::Srpm210;  //This is deduced from the file
    instrument_type_ = "Spectroscopic Portal Monitor";
    manufacturer_ = "Leidos";
    instrument_model_ = "SRPM-210";
    
    //Further information it would be nice to fill out:
    //instrument_id_ = "";
    //remarks_.push_back( "..." );
    //lane_number_ = ;
    //measurement_location_name_ = "";
    //inspection_ = "";
    //measurment_operator_ = "";
    
    cleanup_after_load();
  }catch( std::exception & )
  {
    reset();
    return false;
  }
  
  return true;
}//bool load_from_srpm210_csv( std::istream &input );


namespace
{
  string getAmptekMcaLineInfo( const string &data, const string &heading )
  {
    const size_t pos = data.find( heading );
    if( pos == string::npos )
      return "";
    const size_t end = data.find_first_of( "\r\n", pos );
    if( end == string::npos )
      return "";
    return data.substr( pos + heading.size(), end - pos - heading.size() );
  }
}//anonomous namespace



bool SpecFile::load_from_amptek_mca( std::istream &input )
{
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  
  char firstline[18];
  input.read( firstline, 17 );
  firstline[17] = '\0';
  
  if( strcmp(firstline, "<<PMCA SPECTRUM>>") != 0 )
    return false;
  
  input.seekg( 0, ios::end );
  const istream::pos_type eof_pos = input.tellg();
  input.seekg( orig_pos, ios::beg );
  
  const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );
  
  
  //Assume maximum file size of 2.5 MB - this is _way_ more than expected for
  //  even a 16k channel spectrum
  if( filesize > 2.5*1024*1024 )
    return false;
  
  try
  {
    std::shared_ptr<Measurement> meas( new Measurement() );
  
    string filedata;
    filedata.resize( filesize );
    input.read( &filedata[0], filesize );
  
    string lineinfo = getAmptekMcaLineInfo( filedata, "TAG - " );
    if( !lineinfo.empty() )
      remarks_.push_back( "Tag: " + lineinfo );
  
    lineinfo = getAmptekMcaLineInfo( filedata, "DESCRIPTION - " );
    if( !lineinfo.empty() )
      remarks_.push_back( "Description: " + lineinfo );
  
    lineinfo = getAmptekMcaLineInfo( filedata, "GAIN - " );
    if( !lineinfo.empty() )
    {
      float gain;
      if( toFloat(lineinfo,gain) )
      {
        meas->calibration_coeffs_.push_back( 0.0f );
        meas->calibration_coeffs_.push_back( gain );
        meas->energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
      }
    }//if( !lineinfo.empty() )
  
  
    lineinfo = getAmptekMcaLineInfo( filedata, "LIVE_TIME - " );
    if( !lineinfo.empty() )
      meas->live_time_ = static_cast<float>( atof( lineinfo.c_str() ) );
  
    lineinfo = getAmptekMcaLineInfo( filedata, "REAL_TIME - " );
    if( !lineinfo.empty() )
      meas->real_time_ = static_cast<float>( atof( lineinfo.c_str() ) );
  
    lineinfo = getAmptekMcaLineInfo( filedata, "START_TIME - " );
    if( !lineinfo.empty() )
      meas->start_time_ = time_from_string( lineinfo.c_str() );
  
    lineinfo = getAmptekMcaLineInfo( filedata, "SERIAL_NUMBER - " );
    if( !lineinfo.empty() )
      instrument_id_ = lineinfo;
  
    size_t datastart = filedata.find( "<<DATA>>" );
    if( datastart == string::npos )
      throw runtime_error( "File doesnt contain <<DATA>> section" );
  
    datastart += 8;
    while( datastart < filedata.size() && !isdigit(filedata[datastart]) )
      ++datastart;
    
    const size_t dataend = filedata.find( "<<END>>", datastart );
    if( dataend == string::npos )
      throw runtime_error( "File doesnt contain <<END>> for data section" );
  
    const size_t datalen = dataend - datastart - 1;
    
    std::shared_ptr<vector<float> > counts( new vector<float>() );
    meas->gamma_counts_ = counts;
    
    const bool success = SpecUtils::split_to_floats(
                               filedata.c_str() + datastart, datalen, *counts );
    if( !success )
      throw runtime_error( "Couldnt parse channel data" );
    
    meas->gamma_count_sum_ = 0.0;
    for( const float f : *counts )
      meas->gamma_count_sum_ += f;
    
    const size_t dp5start = filedata.find( "<<DP5 CONFIGURATION>>" );
    if( dp5start != string::npos )
    {
      const size_t dp5end = filedata.find( "<<DP5 CONFIGURATION END>>", dp5start );
      
      if( dp5end != string::npos )
      {
        vector<string> lines;
        const string data = filedata.substr( dp5start, dp5end - dp5start );
        SpecUtils::split( lines, data, "\r\n" );
        for( size_t i = 1; i < lines.size(); ++i )
          meas->remarks_.push_back( lines[i] );
      }//if( dp5end != string::npos )
    }//if( dp5start == string::npos )
    
    
    const size_t dppstart = filedata.find( "<<DPP STATUS>>" );
    if( dppstart != string::npos )
    {
      const size_t dppend = filedata.find( "<<DPP STATUS END>>", dppstart );
      
      if( dppend != string::npos )
      {
        vector<string> lines;
        const string data = filedata.substr( dppstart, dppend - dppstart );
        SpecUtils::split( lines, data, "\r\n" );
        for( size_t i = 1; i < lines.size(); ++i )
        {
          if( SpecUtils::starts_with( lines[i], "Serial Number: " )
              && instrument_id_.size() < 3 )
            instrument_id_ = lines[i].substr( 15 );
          else if( SpecUtils::starts_with( lines[i], "Device Type: " ) )
            instrument_model_ = lines[i].substr( 13 );
          else
            remarks_.push_back( lines[i] );
        }
      }//if( dppend != string::npos )
    }//if( dp5start == string::npos )
  
    
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
}//bool SpecFile::load_from_amptek_mca( std::istream &input )



bool SpecFile::load_from_ortec_listmode( std::istream &input )
{
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  
  try
  {
    //http://www.ortec-online.com/download/List-Mode-File-Formats.pdf
    
    //For reference:
    //  2^21 = 2097152  (e.g. event us clock overflows every 2.097 seconds)
    //  2^31 = 2147483648
    //  2^32 = 4294967296
    //  2147.483647s = 35.79m (e.g. 31 bit clock us overflows every ~36 minutes)
    //  2^30 = 1073741824
    //  1073741.824 = 298 hours (e.g. digiBASE-E ms overflows every ~12 days, can ignore)
    const streampos orig_pos = input.tellg();
    
    double olestartdate;
    uint8_t energy_cal_valid, shape_cal_valid;
    float realtime, livetime;
    float offset, gain, quadratic;
    float shapeoffset, shapegain, shapequad;
    int32_t magicnum, lmstyle, conversiongain, detectoridint;
    char devaddress[81] = {'\0'}, mcb_type[10] = {'\0'}, energyunits[5] = {'\0'};
    char serialnumstr[17] = {'\0'}, txtdcreption[81] = {'\0'}, dummy[9];
    
    
    if( !input.read( (char *)&magicnum, 4) )
      throw runtime_error( "" );  //Failed to read from input stream
    
    if( magicnum != -13 )
      throw runtime_error( "Incorrect leading 4 bytes for .LIS file" );
    
    if( !input.read( (char *)&lmstyle, 4) )
      throw runtime_error( "" );  //Failed to read from input stream
    
    if( lmstyle != 1 && lmstyle != 2 && lmstyle != 4 )
      throw runtime_error( "Unrecognized listmode format" );
    
    if( lmstyle != 1 && lmstyle != 4 )
      throw runtime_error( "Listmode data not in digiBASE/digiBASE-E format (PRO List not supported yet)" );
    
    if( !input.read( (char *)&olestartdate, 8) )
      throw runtime_error( "" );
    
    if( !input.read( devaddress, 80) || !input.read( mcb_type, 9)
       || !input.read( serialnumstr, 16) || !input.read( txtdcreption, 80) )
      throw runtime_error( "" );
    
    if( !input.read( (char *)&energy_cal_valid, 1 ) || !input.read(energyunits,4)
       || !input.read( (char *)&offset, 4 ) || !input.read( (char *)&gain, 4 )
       || !input.read( (char *)&quadratic, 4 ) )
      throw runtime_error( "" );  //Failed reading energy calibration
    
    if( !input.read( (char *)&shape_cal_valid, 1 )
       || !input.read( (char *)&shapeoffset, 4 )
       || !input.read( (char *)&shapegain, 4 )
       || !input.read( (char *)&shapequad, 4 ) )
      throw runtime_error( "" ); //Failed reading shape calibration coefficents
    
    if( !input.read( (char *)&conversiongain, 4) )
      throw runtime_error( "" );
    
    if( !input.read( (char *)&detectoridint, 4) )
      throw runtime_error( "" );
    
    if( !input.read( (char *)&realtime, 4) || !input.read( (char *)&livetime, 4) )
      throw runtime_error( "" );
    
    if( !input.read(dummy, 9) )
      throw runtime_error( "" );
    
    assert( input.tellg() == (orig_pos + streampos(256)) );
  
    size_t ninitialbin = 1024;
    switch( lmstyle )
    {
      case 1: ninitialbin = 1024; break;
      case 2: ninitialbin = 1024; break;  //16k?
      case 4: ninitialbin = 2048; break;
    }
    
    std::shared_ptr< vector<float> > histogram = std::make_shared< vector<float> >( ninitialbin, 0.0f );
    
    uint32_t event;
    
    if( lmstyle == 1 )
    {
      //We need to track overflows in the 31bit microsecond counter, so we will
      //  check if the current 31bit timestamp is less than the previous, and if
      //  so know a overflow occured, and add 2^31 to timeepoch.
      uint32_t previous_time = 0;
      
      //Incase real time isnt excplicitly given in the header we will grab the
      //  first and last events timetampts
      uint64_t firsttimestamp = 0, lasttimestamp = 0;
      
      //To track measurments longer than 35.79 minutes we need to keep track of
      //  more than a 31 bit clock.
      uint64_t timeepoch = 0;
      
      //Bits 20 through 31 of the timestamp (e.g. the part not given with actual
      //  hits)
      uint32_t time_msb = 0;
      
      //It appears that events with a zero value of the 21 bit timestamp will be
      //  transmitted before the 31 bit clock update (it will be the next 32bit
      //  event), so we have to add 2^21 to these zero clock value hits - however
      //  since these events are rare to investigate, I'm adding in an additional
      //  check to make sure that the 31 bit clock wasnt just sent, since I cant be
      //  sure the ordering is always consistent.
      bool prev_was_timestamp = false;
      
      //First, look for timestamp.  If first two timestamps most significant bits
      //  (20 though 31) are different, then the data is starting with the time_msb
      //  first given in file.  If first two timestamps are the same, then begging
      //  timestamp is this value minus 2^21.
      uint32_t firsttimestamps[2] = { 0 };
      for( int i = 0; i < 2 && input.read((char *)&event, 4); )
      {
        if( event > 0x7fffffff )
          firsttimestamps[i++] = (event & 0x7fe00000);
      }
    
      if( firsttimestamps[0] && firsttimestamps[0] == firsttimestamps[1] )
        time_msb = firsttimestamps[0] - 2097152;
      else
        time_msb = firsttimestamps[0];
      
      input.seekg( orig_pos + streampos(256) );
      
      
      for( int64_t eventnum = 0; input.read((char *)&event, 4); ++eventnum )
      {
        if( event <= 0x7fffffff )
        {
          //Bits   Description
          //31     0 for event
          //30-21  Amplitude of pulse (10 bits)
          //20-0   Time in microseconds that the event occured (21 bits)
            
          //10-bit ADC value
          uint32_t amplitude = (uint32_t) ((event & 0x7fe00000) >> 21);
            
          //21-bit timestamp
          uint32_t time_lsb = (event & 0x001fffff);
          //Correct for time_lsb with value zero
          time_lsb = ((time_lsb || prev_was_timestamp) ? time_lsb : 2097152);
          const uint64_t timestamp = timeepoch + time_msb + time_lsb;
          
          if( amplitude > 16384 )
            throw runtime_error( "To high of a channel number" );
            
          amplitude = (amplitude > 0 ? amplitude-1 : amplitude);
          
          if( amplitude >= histogram->size() )
          {
            const uint32_t powexp = static_cast<uint32_t>( std::ceil(log(amplitude)/log(2)) );
            const size_t next_power_of_two = static_cast<size_t>( std::pow( 2u, powexp ) );
            histogram->resize( next_power_of_two, 0.0f );
          }
            
          ++((*histogram)[amplitude]);
            
          firsttimestamp = (firsttimestamp ? firsttimestamp : timestamp);
          lasttimestamp = timestamp;
            
          prev_was_timestamp = false;
        }else
        {
          //	The	number	rolls	over	to	0	every 2.097152 seconds. In	order	to	track
          //  the rollovers, a “time only” event is sent from the digiBASE to the
          //  computer every 1.048576 seconds.
            
          //Bits   Description
          //31     1 for time
          //30-0   Current time in microseconds
          const uint32_t this_time = (uint32_t) (event & 0x7fffffff);
          
          if( this_time < previous_time )
            timeepoch += 2147483648u;
          previous_time = this_time;
          time_msb = (this_time & 0xffe00000);
          prev_was_timestamp = true;
        }//if( a hit ) / else( a timestamp )
      }//for( int64_t eventnum = 0; input.read((char *)&event, 4); ++eventnum )
      
      
      if( realtime == 0.0f )
        realtime = 1.0E-6f*(lasttimestamp - firsttimestamp);
      if( livetime == 0.0f )
        livetime = 1.0E-6f*(lasttimestamp - firsttimestamp);
    }else if( lmstyle == 4 )
    {
      uint32_t firstlivetimes[2] = { 0 }, firstrealtimes[2] = { 0 };
      
      for( int i = 0, j = 0, k = 0; (i < 2 || j < 2) && input.read((char *)&event, 4); ++k)
      {
        const bool msb = (event & 0x80000000);
        const bool ssb = (event & 0x40000000);
        
        if( msb && ssb )
        {
          //ADC Word
        }else if( msb )
        {
          //RT Word, 30-bit Real Time counter in 10 mS Ticks
          firstrealtimes[i++] = (event & 0x3FFFFFFF);
        }else if( ssb )
        {
          //LT Word, 30 Bit Live Time counter in 10 mS Ticks
          firstlivetimes[j++] = (event & 0x3FFFFFFF);
        }else
        {
          //Ext Sync, 13-bit Real Time counter in 10 mS Ticks, 17-bit time pre-scale in 80 nS ticks
        }
      }
      
      uint64_t firsttimestamp = 0, lasttimestamp = 0;
      uint64_t realtime_ns = 10*1000000 * uint64_t(firstrealtimes[0]);
      uint64_t livetime_ns = 10*1000000 * uint64_t(firstlivetimes[0]);
      
      //if( firsttimestamps[0] && firsttimestamps[0] == firsttimestamps[1] )
        //time_msb = firsttimestamps[0] - 2097152;
      //else
        //time_msb = firsttimestamps[0];
      
      
      input.seekg( orig_pos + streampos(256) );
      for( int64_t eventnum = 0; input.read((char *)&event, 4); ++eventnum )
      {
        //Untested!
        if( (event & 0xC0000000) == 0xC0000000 )
        {
          //From DIGIBASE-E-MNL.pdf:
          //  Data[31:30] = 3
          //  Data[29] = Memory Routing bit
          //  Data[28:17] = Conversion Data[11:0]
          //  Data[16:0] = RealTime PreScale [17:1]
          
          //ADC Word: 17-bit time pre-scale in 80 nS Ticks, 13-bit ADC channel number
          //          const uint32_t adc_value = (event & 0x1FFF);
          const uint32_t ticks = (event & 0x0001FFFF);
          const uint64_t timestamp_ns = realtime_ns + 80*ticks;
          
          firsttimestamp = (!firsttimestamp ? timestamp_ns : firsttimestamp);
          lasttimestamp = timestamp_ns;
          
          
          //Have 2048 channels
          uint32_t amplitude = (uint32_t(event & 0x0FFE0000) >> 17);
          
          if( amplitude > 16384 )
            throw runtime_error( "To high of a channel number" );
          
          if( amplitude >= histogram->size() )
          {
            const uint32_t powexp = static_cast<uint32_t>( std::ceil(log(amplitude)/log(2.0)) );
            const size_t next_power_of_two = static_cast<size_t>( std::pow( 2u, powexp ) );
            histogram->resize( next_power_of_two, 0.0f );
          }
          
          ++((*histogram)[amplitude]);
        }else if( (event & 0x80000000) == 0x80000000 )
        {
          //RT Word: 30-bit Real Time counter in 10 mS Ticks
          realtime_ns = 10*1000000*uint64_t(event & 0x3FFFFFFF);
        }else if( (event & 0x40000000) == 0x40000000 )
        {
          //LT Word: 30 Bit Live Time counter in 10 mS Ticks
          livetime_ns = 10*1000000*uint64_t(event & 0x3FFFFFFF);
        }else if( ((event ^ 0xC0000000) >> 30) == 0x00000003 )
        {
          /*
           //Ext Sync: 13-bit Real Time counter in 10 mS Ticks; 17-bit time pre-scale in 80 nS ticks
           //The Ext Sync words contain the value of the external input pulse counters.
           // The external pulse counters count the positive pulses at the external
           // input of the spectrometer. The sync time is calculated by adding the
           //  real time counter to the time pre-scale value.
           const uint32_t tenms_ticks = (uint32_t(event & 0x3FFE0000) >> 17);
           const uint32_t ns_ticks = (event & 0x1FFFF);
           
           
           from: https://oaktrust.library.tamu.edu/bitstream/handle/1969.1/ETD-TAMU-2012-08-11450/CraneWowUserManual.pdf?sequence=3&isAllowed=y
           Care must be taken as the real time
           portion of the stamp, found at bits 12-0, will reset every 80 stamps. This means that the sync time
           stamps roll over every 8 seconds. Note that the time stamp is in units of 100ms, such that a reading of
           “10” would equal one second. The remaining bits are a prescale in the units of 80ns that can be used to
           pinpoint the sync pulse in relation to the digiBASE-Es own clock. See the Sync Gating section of
           Explanations of Terms for more details.
           */
        }
      }//for( int64_t eventnum = 0; input.read((char *)&event, 4); ++eventnum )
      
      //realtime=33.02, from hits->32.62
      //livetime=33.02, from hits->32.2
      //cout << "realtime=" << realtime << ", from hits->" << (1.0E-9*(lasttimestamp - firsttimestamp)) << endl;
      //cout << "livetime=" << realtime << ", from hits->" << (1.0E-9*(livetime_ns - (10*1000000 * uint64_t(firstlivetimes[0])))) << endl;
      
      if( realtime == 0.0f ) //not exact, but close enough
        realtime = 1.0E-9f*(lasttimestamp - firsttimestamp);
      
      if( livetime == 0.0 ) //get us within ~20ms live time calc, close enough!
        livetime = 1.0E-9f*(livetime_ns - (10*1000000 * uint64_t(firstlivetimes[0])));
    }else if( lmstyle == 2 )
    {
      assert( 0 );
      //This one is pretty complicated, so I would definetly need some example
      //  data to test against.
    }else
    {
      assert( 0 );
    }//if( lmstyle == 1 ) / else
    
    
    
    const double gammasum = std::accumulate( histogram->begin(), histogram->end(), 0.0, std::plus<double>() );
    if( gammasum < 1.0 && realtime == 0.0f )
      throw runtime_error( "" ); //"Empty listmode file"
    
    std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
    
    meas->live_time_ = livetime;
    meas->real_time_ = realtime;
    meas->contained_neutron_ = false;
    meas->sample_number_ = 1;
    meas->occupied_ = OccupancyStatus::Unknown;
    meas->gamma_count_sum_ = gammasum;
    meas->neutron_counts_sum_ = 0.0;
    meas->speed_ = 0.0;  //in m/s
    meas->detector_name_ = ((lmstyle==1) ? "digiBASE" : "digiBASE-E");
    meas->detector_number_ = 0;
    meas->detector_description_ = meas->detector_name_ + " ListMode data";
    meas->quality_status_ = SpecUtils::QualityStatus::Missing;
    meas->source_type_ = SourceType::Unknown;
    meas->energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
    
    //std::vector<std::string>  remarks_;
    
    if( olestartdate > 0 )
      meas->start_time_ = datetime_ole_to_posix( olestartdate );
    
    vector<float> energycoef;
    energycoef.push_back( offset );
    energycoef.push_back( gain );
    if( quadratic != 0.0 )
      energycoef.push_back( quadratic );
    const std::vector<std::pair<float,float>> devpairs;
    
    
    //Should check energyunits=="keV"
    if( energy_cal_valid && (gain != 0 || quadratic != 0)
       && calibration_is_valid( SpecUtils::EnergyCalType::Polynomial, energycoef, devpairs, histogram->size() ) )
    {
      meas->calibration_coeffs_ = energycoef;
    }
    
    meas->deviation_pairs_ = devpairs;
    //meas->channel_energies_ = ...;
    meas->gamma_counts_ = histogram;
    //meas->neutron_counts_ = ...;
    //meas->latitude_;  //set to -999.9 if not specified
    //meas->longitude_; //set to -999.9 if not specified
    //meas->position_time_;
    

    meas->title_ = txtdcreption;
    
    instrument_type_ = "Spectroscopic Personal Radiation Detector";
    manufacturer_ = "ORTEC";
    instrument_model_ = ((lmstyle==1) ? "digiBASE" : "digiBASE-E");
    instrument_id_ = serialnumstr;
    if( instrument_id_.empty() && detectoridint )
    {
      char buffer[32];
      snprintf( buffer, sizeof(buffer), "%i", int(detectoridint) );
      instrument_id_ = buffer;
    }

    if( strlen(txtdcreption) )
      remarks_.push_back( "Description: " + string(txtdcreption) );
    if( strlen(devaddress) )
      remarks_.push_back( "Device Address: " + string(devaddress) );
    if( strlen(mcb_type) )
      remarks_.push_back( "MCB Type: " + string(mcb_type) );
    
    measurements_.push_back( meas );

    cleanup_after_load();
    
    if( measurements_.empty() )
      throw std::runtime_error( "no measurments" );
    
    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }
  
  return false;
}//bool load_from_ortec_listmode( std::istream &input )


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
    
    const size_t initial_read = std::min( filesize, size_t(2048) );
    string data( initial_read, '\0' );
    input.read( &(data[0]), initial_read );
    
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
    
    auto getval = [&data]( const string &tag ) -> string {
      const size_t pos = data.find( tag );
      if( pos == string::npos )
        return "";
      
      const size_t value_start = pos + tag.size();
      const size_t endline = data.find_first_of( "\r\n", value_start );
      if( endline == string::npos )
        return "";
      
      const string value = data.substr( pos+tag.size(), endline - value_start );
      return SpecUtils::trim_copy( value );
    };//getval
    
    auto meas = make_shared<Measurement>();
    
    string startdate = getval( "MEASBEGIN=" );
    if( startdate.empty() )
    {
      startdate = getval( "DATE=" );
      startdate += getval( "TIME=" );
    }
    
    meas->start_time_ = SpecUtils::time_from_string( startdate.c_str() );
    
    if( !toFloat( getval("TLIVE="), meas->live_time_ ) )
      meas->live_time_ = 0.0f;
    
    if( !toFloat( getval("TREAL="), meas->real_time_ ) )
      meas->live_time_ = 0.0f;
    
    instrument_id_ = getval( "DETECTOR=" );
    
    const string energy = getval( "ENERGY=" );
    if( SpecUtils::split_to_floats( energy, meas->calibration_coeffs_ ) )
      meas->energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
    
    const string comment = getval( "COMMENT=" );
    if( !comment.empty() )
      remarks_.push_back( comment );
    
    const string fwhm = getval( "FWHM=" );
    if( !fwhm.empty() )
      remarks_.push_back( "FWHM=" + fwhm );
    
    //Other things we could look for:
    //"SHIFR=", "NOMER=", "CONFIGNAME=", "PREPBEGIN=", "PREPEND=", "OPERATOR=",
    //"GEOMETRY=", "SETTYPE=", "CONTTYPE=", "MATERIAL=", "DISTANCE=", "VOLUME="
    
    if( initial_read < filesize )
    {
      data.resize( filesize, '\0' );
      input.read( &(data[initial_read]), filesize-initial_read );
    }
    
    vector<int32_t> spectrumint( nchannel, 0 );
    memcpy( &(spectrumint[0]), &(data[spec_start_pos]), 4*nchannel );
    
    meas->gamma_count_sum_ = 0.0f;
    auto channel_counts = make_shared<vector<float>>(nchannel);
    for( size_t i = 0; i < nchannel; ++i )
    {
      (*channel_counts)[i] = static_cast<float>( spectrumint[i] );
      meas->gamma_count_sum_ += spectrumint[i];
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


bool SpecFile::load_from_tka( std::istream &input )
{
/*
 Simple file with one number on each line with format:
   Live time
   Real time
   counts first channel
   .
   .
   .
   counts last channel
 */
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
      throw runtime_error( "File to large to be TKA" );
    
    //ToDo: check UTF16 ByteOrderMarker [0xFF,0xFE] as first two bytes.
    
    auto get_next_number = [&input]( float &val ) -> int {
      const size_t max_len = 128;
      string line;
      if( !SpecUtils::safe_get_line( input, line, max_len ) )
        return -1;
      
      if( line.length() > 32 )
        throw runtime_error( "Invalid line length" );
      
      SpecUtils::trim(line);
      if( line.empty() )
        return 0;
      
      if( line.find_first_not_of("+-.0123456789") != string::npos )
        throw runtime_error( "Invalid char" );
      
      if( !(stringstream(line) >> val) )
        throw runtime_error( "Failed to convert '" + line + "' into number" );
      
      return 1;
    };//get_next_number lambda
    
    int rval;
    float realtime, livetime, dummy;
    
    while( (rval = get_next_number(livetime)) != 1 )
    {
      if( rval <= -1 )
        throw runtime_error( "unexpected end of file" );
    }
    
    while( (rval = get_next_number(realtime)) != 1 )
    {
      if( rval <= -1 )
        throw runtime_error( "unexpected end of file" );
    }
    
    if( livetime > (realtime+FLT_EPSILON) || livetime<0.0f || realtime<0.0f || livetime>2592000.0f || realtime>2592000.0f )
      throw runtime_error( "Livetime or realtime invalid" );
    
    double countssum = 0.0;
    auto channel_counts = make_shared<vector<float>>();
    while( (rval = get_next_number(dummy)) >= 0 )
    {
      if( rval == 1 )
      {
        countssum += dummy;
        channel_counts->push_back( dummy );
      }
    }
    
    if( channel_counts->size() < 16 )
      throw runtime_error( "Not enough counts" );
    
    auto meas = make_shared<Measurement>();
    
    meas->real_time_ = realtime;
    meas->live_time_ = livetime;
    meas->gamma_count_sum_ = countssum;
    meas->gamma_counts_ = channel_counts;
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
    
    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }//try / catch
  
  
  return false;
}//bool load_from_tka( std::istream &input );


bool SpecFile::load_from_multiact( std::istream &input )
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
    if( filesize > 512*1024 )  //The files I've seen are a few kilobytes
      throw runtime_error( "File to large to be MultiAct" );
    
    if( filesize < (128 + 24 + 48) )
      throw runtime_error( "File to small to be MultiAct" );
    
    string start = "                ";
    
    if( !input.read(&start[0], 8) )
      throw runtime_error( "Failed to read header" );
    
    if( !SpecUtils::istarts_with( start, "MultiAct") )
      throw runtime_error( "File must start with word 'MultiAct'" );
    
    double countssum = 0.0;
    auto channel_counts = make_shared<vector<float>>();
    
    vector<char> data;
    data.resize( filesize - 8, '\0' );
    input.read(&data.front(), static_cast<streamsize>(filesize-8) );
    
    //103: potentially channel counts (int of some sort)
    //107: real time in seconds (int of some sort)
    //111: real time in seconds (int of some sort)
    //115: live time in seconds (int of some sort)
    
    uint32_t numchannels, realtime, livetime;
    memcpy( &numchannels, (&(data[103])), 4 );
    memcpy( &realtime, (&(data[107])), 4 );
    memcpy( &livetime, (&(data[115])), 4 );
  
    if( realtime < livetime || livetime > 3600*24*5 )
    {
#if(PERFORM_DEVELOPER_CHECKS)
      log_developer_error( __func__, ("Got real time (" + std::to_string(realtime)
                                                    + ") less than (" + std::to_string(livetime) + ") livetime").c_str() );
#endif
      throw runtime_error( "Invalid live/real time values" );
    }
    
    for( size_t i = 128; i < (data.size()-21); i += 3 )
    {
      //ToDo: make sure channel counts are reasonable...
      uint32_t threebyte = 0;
      memcpy( &threebyte, (&(data[i])), 3 );
      channel_counts->push_back( static_cast<float>(threebyte) );
      countssum += threebyte;
    }
    
    if( channel_counts->size() < 16 )
      throw runtime_error( "Not enough channels" );
    
    auto meas = make_shared<Measurement>();
    
    meas->real_time_ = static_cast<float>( realtime );
    meas->live_time_ = static_cast<float>( livetime );
    meas->gamma_count_sum_ = countssum;
    meas->gamma_counts_ = channel_counts;
    
    measurements_.push_back( meas );
    
    cleanup_after_load();
    
    return true;
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
  }//try / catch
  
  
  return false;
}//bool load_from_tka( std::istream &input );


bool SpecFile::load_from_phd( std::istream &input )
{
  //Note: this function implemented off using only a couple files from a single
  //      source to determine file format; there is likely some assumptions that
  //      could be loosened or tightened up.
  
  reset();
  
  if( !input.good() )
    return false;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
  
  const istream::pos_type orig_pos = input.tellg();
  
  try
  {
    string line;
    size_t linenum = 0;  //for debug purposes only
    bool tested_first_line = false;
    while( input.good() )
    {
      const size_t max_len = 1024*1024;  //all the files I've seen have been less than like 80 characters
      SpecUtils::safe_get_line( input, line, max_len );
      ++linenum;
      
      if( line.size() >= (max_len-1) )
        throw runtime_error( "Line greater than 1MB" );
      
      if( linenum > 32*1024 )  //2048 would probably be plenty
        throw runtime_error( "Too many lines for PHD format" );
      
      trim( line );
      
      if( line.empty() )
        continue;
      
      if( !tested_first_line )
      {
        //First line for all files I've seen is "BEGIN IMS2.0"
        tested_first_line = true;
        if( !SpecUtils::istarts_with( line, "BEGIN" ) )
          throw runtime_error( "First line of PHD file must start with 'BEGIN'" );
        
        continue;
      }//if( !tested_first_line )
      
      if( SpecUtils::istarts_with( line, "#Collection") )
      {
        SpecUtils::safe_get_line( input, line, max_len );
        ++linenum;
        //line is somethign like "2012/10/11 09:34:51.7 2011/10/13 09:32:43.6 14377.2   "
        continue;
      }//if( SpecUtils::istarts_with( line, "#Collection") )
      
      
      if( SpecUtils::istarts_with( line, "#Acquisition") )
      {
        SpecUtils::safe_get_line( input, line, max_len );
        ++linenum;
        trim( line );
        //line is somethign like "2012/09/15 09:52:14.0 3605.0        3600.0"
        vector<string> fields;
        SpecUtils::split( fields, line, " \t");
        if( fields.size() < 4 )
          continue;
        
        //We wont worry about conversion error for now
        stringstream(fields[2]) >> meas->real_time_;
        stringstream(fields[3]) >> meas->live_time_;
        meas->start_time_ = SpecUtils::time_from_string( (fields[0] + " " + fields[1]).c_str() );
        continue;
      }//if( SpecUtils::istarts_with( line, "#Acquisition") )
      
      
      if( SpecUtils::istarts_with( line, "#g_Spectrum") )
      {
        SpecUtils::safe_get_line( input, line, max_len );
        ++linenum;
        trim( line );
        //line is something like "8192  2720.5"
        vector<float> fields;
        SpecUtils::split_to_floats( line, fields );
        
        if( fields.empty() || fields[0]<32 || fields[0]>65536 || floorf(fields[0])!=fields[0] )
          throw runtime_error( "Line after #g_Spectrum not as expected" );
        
        const float upper_energy = (fields.size()>1 && fields[1]>500.0f && fields[1]<13000.0f) ? fields[1] : 0.0f;
        const size_t nchannel = static_cast<size_t>( fields[0] );
        auto counts = std::make_shared< vector<float> >(nchannel,0.0f);
        
        double channelsum = 0.0;
        size_t last_channel = 0;
        while( SpecUtils::safe_get_line(input, line, max_len) )
        {
          SpecUtils::trim( line );
          if( line.empty() )
            continue;
          
          if( line[0] == '#')
            break;
          
          SpecUtils::split_to_floats( line, fields );
          if( fields.empty() ) //allow blank lines
            continue;
          
          if( fields.size() == 1 )  //you need at least two rows for phd format
            throw runtime_error( "Unexpected spectrum data line-size" );
          
          if( (floorf(fields[0]) != fields[0]) || (fields[0] < 0.0f) )
            throw runtime_error( "First col of spectrum data must be positive integer" );
          
          const size_t start_channel = static_cast<size_t>( fields[0] );
          
          if( (start_channel <= last_channel) || (start_channel > nchannel) )
            throw runtime_error( "Channels not ordered as expected" );
          
          for( size_t i = 1; (i < fields.size()) && (start_channel+i-2)<nchannel; ++i )
          {
            channelsum += fields[i];
            (*counts)[start_channel + i - 2] = fields[i];
          }
        }//while( spectrum data )
        
        meas->gamma_counts_ = counts;
        meas->gamma_count_sum_ = channelsum;
        
        if( upper_energy > 0.0f )
        {
          //There is maybe better energy calibration in the file, but since I
          //  so rarely see this file format, I'm not bothering with parsing it.
          meas->calibration_coeffs_.push_back( 0.0f );
          meas->calibration_coeffs_.push_back( upper_energy );
          meas->energy_calibration_model_ = SpecUtils::EnergyCalType::FullRangeFraction;
        }
      }//if( SpecUtils::istarts_with( line, "#g_Spectrum") )
      
      if( SpecUtils::istarts_with( line, "#Calibration") )
      {
        //Following line gives datetime of calibration
      }//if( "#Calibration" )
      
      if( SpecUtils::istarts_with( line, "#g_Energy") )
      {
        //Following lines look like:
        //59.540           176.1400         0.02968
        //88.040           260.7800         0.00000
        //122.060          361.7500         0.00000
        //165.860          491.7100         0.02968
        //...
        //1836.060         5448.4400        0.02968
      }//if( "#g_Energy" )
      
      if( SpecUtils::istarts_with( line, "#g_Resolution") )
      {
        //Following lines look like:
        //59.540           0.9400           0.00705
        //88.040           0.9700           0.00669
        //122.060          1.0100           0.00151
        //165.860          1.0600           0.00594
        //...
        //1836.060         2.3100           0.00393
      }//if( "#g_Resolution" )
      
      if( SpecUtils::istarts_with( line, "#g_Efficiency") )
      {
        //Following lines look like:
        //59.540           0.031033         0.0002359
        //88.040           0.083044         0.0023501
        //122.060          0.107080         0.0044224
        //165.860          0.103710         0.0026757
        //...
        //1836.060         0.020817         0.0012261
      }//if( "#g_Efficiency" )
    }//while( input.good() )
    
    if( !meas->gamma_counts_ || meas->gamma_counts_->empty() )
      throw runtime_error( "Didnt find gamma spectrum" );
  
    measurements_.push_back( meas );
  }catch( std::exception & )
  {
    //cerr  << SRC_LOCATION << "caught: " << e.what() << endl;
    reset();
    input.clear();
    input.seekg( orig_pos, ios::beg );
    return false;
  }
  
  
  cleanup_after_load();
  
  return true;
}//bool load_from_phd( std::istream &input );


bool SpecFile::load_from_lzs( std::istream &input )
{
  //Note: this function implemented off using a few files to determine
  //      file format; there is likely some assumptions that could be loosened
  //      or tightened up, or additional information to add.
  
  reset();
  
  if( !input.good() )
    return false;
  
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  
  const istream::pos_type start_pos = input.tellg();
  input.unsetf(ios::skipws);
  
  // Determine stream size
  input.seekg( 0, ios::end );
  const size_t file_size = static_cast<size_t>( input.tellg() - start_pos );
  input.seekg( start_pos );
  
  //I've only seen pretty small lzs files (40k), so assume if over 1MB, not a lzs
  if( file_size < 512 || file_size > 1*1024*1024 )
    return false;
  
  string filedata;
  filedata.resize(file_size + 1);
  
  input.read(&(filedata[0]), static_cast<streamsize>(file_size));
  filedata[file_size] = 0; //jic.
  
  //Look to find "<nanoMCA" and "spectrum" in the first kb
  const auto nano_mca_pos = filedata.find( "spectrum" );
  if( nano_mca_pos == string::npos || nano_mca_pos > 2048 )
    return false;
  
  const auto spectrum_pos = filedata.find( "data" );
  if( spectrum_pos == string::npos || spectrum_pos > 2048 )
    return false;
  
  try
  {
    rapidxml::xml_document<char> doc;
    doc.parse<rapidxml::parse_non_destructive|rapidxml::allow_sloppy_parse>( &(filedata[0]) );
    
    //<nanoMCA>  //But some files dont have this tag, but start with <spectrum>
    //  <serialnumber>28001</serialnumber>
    //  <spectrum>
    //    <tag>nanoMCA with Ortec HPGE-TRP, Model GEM-10195-PLUS, SN 24-P-12RA, 3000V-PLUS</tag>
    //    <hardsize>16384</hardsize>
    //    <softsize>16384</hardsize>
    //    <data>...this is spectrum...</data>
    //  </spectrum>
    //  <time>
    //    <real>  613.232</real>
    //    <live>  601.0000</live>
    //    <dead>    4.1769</dead>
    //    <date>11/06/2019  20:19:15</date>
    //  </time>
    //  <registers><size>128</size><data>...not syre what this is...</data></registers>
    //  <calibration>
    //    <enabled>YES</enabled>
    //    <units>2</units>
    //    <channelA>    0.0000</channelA>
    //    <energyA>    0.0000</energyA>
    //    <channelB>10436.2100</channelB>
    //    <energyB> 1332.0000</energyB>
    //  </calibration>
    //  <volatile>
    //    <firmware>30.20</firmware>
    //    <intemp>44</intemp>
    //    <slowadvc> 0.82</slowadc>
    //  </volatile>
    //</nanoMCA>
    
    const rapidxml::xml_node<char> *nano_mca_node = XML_FIRST_NODE((&doc),"nanoMCA");
    if( !nano_mca_node )  //Account for files that start with <spectrum> tag instead of nesting it under <nanoMCA>
      nano_mca_node = &doc;

    const rapidxml::xml_node<char> *spectrum_node = XML_FIRST_NODE(nano_mca_node,"spectrum");
    if( !spectrum_node )
      throw runtime_error( "Failed to get spectrum node" );
    
    const rapidxml::xml_node<char> *spec_data_node = XML_FIRST_NODE(spectrum_node,"data");
    if( !spec_data_node )
      throw runtime_error( "Failed to get spectrum/data node" );
    
    auto spec = std::make_shared<vector<float>>();
    const string spec_data_str = xml_value_str(spec_data_node);
    SpecUtils::split_to_floats( spec_data_str.c_str(), *spec, " \t\n\r", false );
    
    if( spec->empty() )
      throw runtime_error( "Failed to parse spectrum to floats" );
    
    auto meas = std::make_shared<Measurement>();
    meas->contained_neutron_ = false;
    meas->gamma_counts_ = spec;
    meas->gamma_count_sum_ = std::accumulate(begin(*spec), end(*spec), 0.0 );
    
    const rapidxml::xml_node<char> *time_node = xml_first_node(nano_mca_node,"time");
    
    const rapidxml::xml_node<char> *real_time_node = xml_first_node(time_node,"real");
    if( real_time_node )
      SpecUtils::parse_float(real_time_node->value(), real_time_node->value_size(), meas->real_time_ );
    
    const rapidxml::xml_node<char> *live_time_node = xml_first_node(time_node,"live");
    if( live_time_node )
      SpecUtils::parse_float(live_time_node->value(), live_time_node->value_size(), meas->live_time_ );
    
    //const rapidxml::xml_node<char> *dead_time_node = xml_first_node(time_node,"dead");
    
    const rapidxml::xml_node<char> *date_node = xml_first_node(time_node,"date");
    if( date_node )
    {
      string datestr = xml_value_str(date_node);
      SpecUtils::ireplace_all(datestr, "@", " ");
      SpecUtils::ireplace_all(datestr, "  ", " ");
      meas->start_time_ = SpecUtils::time_from_string_strptime( datestr, SpecUtils::DateParseEndianType::LittleEndianFirst );
    }
    
    const rapidxml::xml_node<char> *calibration_node = XML_FIRST_NODE(nano_mca_node,"calibration");
    //const rapidxml::xml_node<char> *enabled_node = xml_first_node(calibration_node, "enabled");
    //const rapidxml::xml_node<char> *units_node = xml_first_node(calibration_node, "units");
    const rapidxml::xml_node<char> *channelA_node = xml_first_node(calibration_node, "channelA");
    const rapidxml::xml_node<char> *energyA_node = xml_first_node(calibration_node, "energyA");
    const rapidxml::xml_node<char> *channelB_node = xml_first_node(calibration_node, "channelB");
    const rapidxml::xml_node<char> *energyB_node = xml_first_node(calibration_node, "energyB");
    if( channelA_node && energyA_node && channelB_node && energyB_node )
    {
      float channelA, energyA, channelB, energyB;
      if( SpecUtils::parse_float(channelA_node->value(), channelA_node->value_size(), channelA )
          && SpecUtils::parse_float(energyA_node->value(), energyA_node->value_size(), energyA )
          && SpecUtils::parse_float(channelB_node->value(), channelB_node->value_size(), channelB )
          && SpecUtils::parse_float(energyB_node->value(), energyB_node->value_size(), energyB ) )
      {
        const float gain = (energyB - energyA) / (channelB - channelA);
        const float offset = energyA - channelA*gain;
        if( !IsNan(gain) && !IsInf(gain) && !IsNan(offset) && !IsInf(offset)
           && gain > 0.0f && fabs(offset) < 350.0f )
        {
          meas->calibration_coeffs_.clear();
          meas->energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
          meas->calibration_coeffs_.push_back( offset );
          meas->calibration_coeffs_.push_back( gain );
        }
      }//if( parsed all float values okay )
    }//if( got at least calibration points )
    
    const rapidxml::xml_node<char> *volatile_node = XML_FIRST_NODE(nano_mca_node,"volatile");
    
    const rapidxml::xml_node<char> *firmware_node = xml_first_node(volatile_node,"firmware");
    if( firmware_node )
      component_versions_.push_back( pair<string,string>("firmware",xml_value_str(firmware_node)) );
    
    const rapidxml::xml_node<char> *intemp_node = xml_first_node(volatile_node,"intemp");
    if( intemp_node && intemp_node->value_size() )
      meas->remarks_.push_back( "Internal Temperature: " + xml_value_str(intemp_node) );
    const rapidxml::xml_node<char> *adctemp_node = xml_first_node(volatile_node,"adctemp");
    if( adctemp_node && adctemp_node->value_size() )
      meas->remarks_.push_back( "ADC Temperature: " + xml_value_str(adctemp_node) );
    
    //const rapidxml::xml_node<char> *slowadvc_node = xml_first_node(volatile_node,"slowadvc");
    
    const rapidxml::xml_node<char> *serialnum_node = XML_FIRST_NODE(nano_mca_node,"serialnumber");
    if( serialnum_node && serialnum_node->value_size() )
      instrument_id_ = xml_value_str(serialnum_node);
    
    const rapidxml::xml_node<char> *tag_node = XML_FIRST_NODE(spectrum_node,"tag");
    if( !tag_node )
      tag_node = XML_FIRST_NODE(nano_mca_node,"tag");
    
    if( tag_node && tag_node->value_size() )
    {
      const string value = xml_value_str(tag_node);
      
      remarks_.push_back( value );
      
      //nanoMCA with Ortec HPGE-TRP, Model GEM-10195-PLUS, SN 24-P-12RA, 3000V-PLUS
      //I'm not sure how reliable it is to assume comma-seperated
      vector<string> fields;
      SpecUtils::split(fields, value, ",");
      for( auto field : fields )
      {
        SpecUtils::trim(field);
        if( SpecUtils::istarts_with(field, "SN") )
          instrument_id_ = SpecUtils::trim_copy(field.substr(2));
        else if( SpecUtils::istarts_with(field, "model") )
          instrument_model_ = SpecUtils::trim_copy(field.substr(5));
      }//for( auto field : fields )
    }//if( tag_node )
    
    manufacturer_ = "labZY";
    
    measurements_.push_back( meas );
  }catch( std::exception & )
  {
    reset();
    input.clear();
    input.seekg( start_pos, ios::beg );
    return false;
  }//try / catch
  
  cleanup_after_load();
  
  return true;
}//bool load_from_phd( std::istream &input );




bool SpecFile::load_tracs_mps_file( const std::string &filename )
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
  const bool loaded = load_from_tracs_mps( file );
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_tracs_mps_file( const std::string &filename )


bool SpecFile::load_from_tracs_mps( std::istream &input )
{
/*
 Cabin Data
 Byte offset	Size	Description
 0	8	Memory address
 8	4	Memory address
 12	4	Connect Status
 16	4	Event
 20	4	Neutron AlarmLevel
 24	4	Gamma Alarm Level
 28	4	Ratio Alarm Level
 32	8	Latitude
 40	8	Longitude
 48	4	GPS Time of Day
 52	4	#1 pod status
 56	4	#2 pod status
 60	4	#1 det status
 64	4	#2 det status
 68	4	#3 det status
 72	4	#4 det status
 76	4	Index Number
 80	4	Neutron GC
 84	4	Gamma GC
 88	2048	Sum Spectra
 2136	4	Pod1 Index Number
 2140	4	Pod1 deltaTau
 
 2144	4	Pod1 Det1 Neutron GC
 2148	4	Pod1 Det2 Neutron GC
 
 2152	4	Pod1 Det1 Gamma GC
 2156	4	Pod1 Det2 Gamma GC
 2160	4	Pod1 Det1 DAC
 2164	4	Pod1 Det2 DAC
 2168	4	Pod1 Det1 calibration Peak
 2172	4	Pod1 Det2 calibration Peak
 2176	4	Pod1 Det1 calibration peak found
 2180	4	Pod1 Det2 calibration peak found
 
 2184	2048	Pod1 Det1 spectra
 4232	2	Pod1 Det1 clock time
 4234	2	Pod1 Det1 dead time
 4236	2	Pod1 Det1 live time
 
 4238	2048	Pod1 Det2 spectra
 6286	2	Pod1 Det2 clock time
 6288	2	Pod1 Det2 dead time
 6290	2	Pod1 Det2 live time
 
 6292	4	Pod2 Index Number
 6296	4	Pod2 deltaTau
 
 6300	4	Pod2 Det1 Neutron GC
 6304	4	Pod2 Det2 Neutron GC
 
 6308	4	Pod2 Det1 Gamma GC
 6312	4	Pod2 Det2 Gamma GC
 6316	4	Pod2 Det1 DAC
 6320	4	Pod2 Det2 DAC
 6324	4	Pod2 Det1 calibration Peak
 6328	4	Pod2 Det2 calibration Peak
 6332	4	Pod2 Det1 calibration peak found
 6336	4	Pod2 Det2 calibration peak found
 
 6340	2048	Pod2 Det1 spectra
 8388	2	Pod2 Det1 clock time
 8390	2	Pod2 Det1 dead time
 8392	2	Pod2 Det1 live time
 
 8394	2048	Pod2 Det2 spectra
 10442	2	Pod2 Det2 clock time
 10444	2	Pod2 Det2 dead time
 10446	2	Pod2 Det2 live time
 
 10448	4	Radar Altimeter
 10452	128	GPS String
 10580	8	GPS Source
 10588	6	GPS Age
 10594	3	GPS Num SV
 10597
*/
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  reset();
  
  const size_t samplesize = 10597;
  
  if( !input.good() )
    return false;
  
  const istream::pos_type orig_pos = input.tellg();
  input.seekg( 0, ios::end );
  const istream::pos_type eof_pos = input.tellg();
  input.seekg( orig_pos, ios::beg );

  const size_t filesize = static_cast<size_t>( 0 + eof_pos - orig_pos );
  const size_t numsamples = filesize / samplesize;
  const size_t leftoverbytes = (filesize % samplesize);
  
  if( leftoverbytes )
    return false;
  
  try
  {
    for( size_t sample = 0; sample < numsamples; ++sample )
    {
      double lat, lon;
      const size_t startpos = sample * samplesize;
      uint32_t gpsTOD, indexNum, overallGammaGC, overallNeutronGC, radarAltimiter;
    
      if( !input.seekg(startpos+32, ios::beg) )
        throw runtime_error( "Failed seek 1" );
      if( !input.read( (char *)&lat, sizeof(lat) ) )
        throw runtime_error( "Failed read 2" );
      if( !input.read( (char *)&lon, sizeof(lon) ) )
        throw runtime_error( "Failed read 3" );
      if( !input.read( (char *)&gpsTOD, sizeof(gpsTOD) ) )
        throw runtime_error( "Failed read 4" );
      if( !input.seekg(startpos+76, ios::beg) )
        throw runtime_error( "Failed read 5" );
      if( !input.read( (char *)&indexNum, sizeof(indexNum) ) )
        throw runtime_error( "Failed read 6" );
      if( !input.read( (char *)&overallNeutronGC, sizeof(overallNeutronGC) ) )
        throw runtime_error( "Failed read 7" );
      if( !input.read( (char *)&overallGammaGC, sizeof(overallGammaGC) ) )
        throw runtime_error( "Failed read 8" );
  
      char gpsstr[129];
      gpsstr[128] = '\0';
      if( !input.seekg(startpos+10448, ios::beg) )
        throw runtime_error( "Failed seek 9" );
      if( !input.read( (char *)&radarAltimiter, sizeof(radarAltimiter) ) )
        throw runtime_error( "Failed read 10" );
      if( !input.read( gpsstr, 128 ) )
        throw runtime_error( "Failed read 11" );
      
      for( size_t i = 0; i < 4; ++i )
      {
        const char *title;
        size_t datastart, neutrongc, gammagc, detstatus;
     
        switch( i )
        {
          case 0:
            detstatus = 60;
            datastart = 2184;
            gammagc   = 2152;
            neutrongc = 2144;
            title = "Pod 1, Det 1";
          break;
        
          case 1:
            detstatus = 64;
            datastart = 4238;
            gammagc   = 2156;
            neutrongc = 2148;
            title = "Pod 1, Det 2";
          break;
    
          case 2:
            detstatus = 68;
            datastart = 6340;
            gammagc   = 6308;
            neutrongc = 6300;
            title = "Pod 2, Det 1";
          break;
        
          case 3:
            detstatus = 72;
            datastart = 8394;
            gammagc   = 6312;
            neutrongc = 6304;
            title = "Pod 2, Det 2";
          break;
        }//switch( i )
    
        uint32_t neutroncount;
        uint16_t channeldata[1024];
        uint16_t realtime, livetime, deadtime;
        uint32_t gammaGC, detDAC, calPeak, calPeakFound, status, dummy;
      
        if( !input.seekg(startpos+detstatus, ios::beg) )
          throw runtime_error( "Failed seek 12" );
        if( !input.read( (char *)&status, sizeof(status) ) )
          throw runtime_error( "Failed read 13" );
      
        if( !input.seekg(startpos+gammagc, ios::beg) )
          throw runtime_error( "Failed seek 14" );
    
        if( !input.read( (char *)&gammaGC, sizeof(gammaGC) ) )
          throw runtime_error( "Failed read 15" );
        if( !input.read( (char *)&dummy, sizeof(dummy) ) )
          throw runtime_error( "Failed read 16" );
        
        if( !input.read( (char *)&detDAC, sizeof(detDAC) ) )
          throw runtime_error( "Failed read 17" );
        if( !input.read( (char *)&dummy, sizeof(dummy) ) )
          throw runtime_error( "Failed read 18" );
      
        if( !input.read( (char *)&calPeak, sizeof(calPeak) ) )
          throw runtime_error( "Failed read 19" );
        if( !input.read( (char *)&dummy, sizeof(dummy) ) )
          throw runtime_error( "Failed read 20" );
      
        if( !input.read( (char *)&calPeakFound, sizeof(calPeakFound) ) )
          throw runtime_error( "Failed read 21" );
        if( !input.read( (char *)&dummy, sizeof(dummy) ) )
          throw runtime_error( "Failed read 22" );
      
        if( !input.seekg(startpos+datastart, ios::beg) )
          throw runtime_error( "Failed seek 23" );
      
        if( !input.read( (char *)channeldata, sizeof(channeldata) ) )
          throw runtime_error( "Failed read 24" );
    
        if( !input.read( (char *)&realtime, sizeof(realtime) ) )
          throw runtime_error( "Failed read 25" );
    
      //if realtime == 6250, then its about 1 second... - I have no idea what these units means (25000/4?)
      
        if( !input.read( (char *)&deadtime, sizeof(deadtime) ) )
          throw runtime_error( "Failed read 26" );
    
        if( !input.read( (char *)&livetime, sizeof(livetime) ) )
          throw runtime_error( "Failed read 27" );
    
        if( !input.seekg(startpos+neutrongc, ios::beg) )
          throw runtime_error( "Failed seek 28" );
    
        if( !input.read( (char *)&neutroncount, sizeof(neutroncount) ) )
          throw runtime_error( "Failed read 29" );
      
        auto m = std::make_shared<Measurement>();
        m->live_time_ = livetime / 6250.0f;
        m->real_time_ = realtime / 6250.0f;
        m->contained_neutron_ = (((i%2)!=1) || neutroncount);
        m->sample_number_ = static_cast<int>( sample + 1 );
        m->occupied_ = OccupancyStatus::Unknown;
        m->gamma_count_sum_ = 0.0;
        m->neutron_counts_sum_ = neutroncount;
//        m->speed_ = ;
        m->detector_name_ = title;
        m->detector_number_ = static_cast<int>( i );
//        m->detector_type_ = "";
        m->quality_status_ = (status==0 ? SpecUtils::QualityStatus::Good : SpecUtils::QualityStatus::Suspect);
        m->source_type_  = SourceType::Unknown;
        
        if( calPeakFound != 0 )
        {
          m->energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
//        m->start_time_ = ;
          m->calibration_coeffs_.push_back( 0.0f );
//        m->calibration_coeffs_.push_back( 3.0 );
          m->calibration_coeffs_.push_back( 1460.0f / calPeakFound );
//        m->channel_energies_  //dont need to fill out here
        }//if( calPeakFound != 0 ) / else
      
        vector<float> *gammacounts = new vector<float>( 1024 );
        m->gamma_counts_.reset( gammacounts );
        for( size_t i = 0; i < 1024; ++i )
        {
          const float val = static_cast<float>( channeldata[i] );
          m->gamma_count_sum_ += val;
          (*gammacounts)[i] = val;
        }
      
        if( m->contained_neutron_ )
          m->neutron_counts_.resize( 1, static_cast<float>(neutroncount) );

        m->latitude_ = lat;
        m->longitude_ = lon;
//        m->position_time_ = ;//
        m->title_ = title;
      
        measurements_.push_back( m );
      }//for( size_t i = 0; i < 4; ++i )
    }//for( size_t sample = 0; sample < numsamples; ++sample )
    
    cleanup_after_load();
    
    if( measurements_.empty() )
      throw std::runtime_error( "no measurments" );
  }catch( std::exception & )
  {
    //cerr << SRC_LOCATION << "\n\tCaught: " << e.what() << endl;
    input.clear();
    input.seekg( orig_pos, ios::beg );
    reset();
    return false;
  }//try / catch
  
  return true;
}//bool load_from_tracs_mps( std::istream &input )


bool SpecFile::load_aram_file( const std::string &filename )
{
  std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
  reset();
  
  //Only seend pretty small ones, therefor limit to 25 MB, JIC
  if( SpecUtils::file_size(filename) > 25*1024*1024 )
    return false;
  
#ifdef _WIN32
  ifstream file( convert_from_utf8_to_utf16(filename).c_str(), ios_base::binary|ios_base::in );
#else
  ifstream file( filename.c_str(), ios_base::binary|ios_base::in );
#endif
  if( !file.is_open() )
    return false;
  const bool loaded = load_from_aram( file );
  if( loaded )
    filename_ = filename;
  
  return loaded;
}//bool load_aram_file( const std::string &filename )


bool SpecFile::load_from_aram( std::istream &input )
{
  //This is a wierd TXT and XML format hybrid, so we have to seprate out the
  //  XML from non-XML portions and parse them seperately.
  
  if( !input )
    return false;
  
  const istream::iostate origexceptions = input.exceptions();
  input.exceptions( istream::goodbit );  //make so stream never throws
  const istream::pos_type start_pos = input.tellg();
  input.unsetf(ios::skipws);
  
  // Determine stream size
  input.seekg( 0, ios::end );
  const size_t file_size = static_cast<size_t>( input.tellg() - start_pos );
  input.seekg( start_pos );
  
  //I've only seen pretty small ARAM files, so assume if over 25MB, not a ARAM
  if( file_size > 25*1024*1024 )
    return false;
  
  string filedata;
  filedata.resize(file_size + 1);
  
  input.read(&(filedata[0]), static_cast<streamsize>(file_size));
  filedata[file_size] = 0; //jic.

  //Look to find "<event" and "ARAM" in the first kb
  const auto event_tag_pos = filedata.find( "<event" );
  if( event_tag_pos == string::npos || event_tag_pos > 2048 )
    return false;
  
  const auto aram_pos = filedata.find( "ARAM" );
  if( aram_pos == string::npos || aram_pos > 2048 )
    return false;
  
  auto event_tag_clos_pos = filedata.find( "</event", event_tag_pos + 5 );
  if( event_tag_clos_pos == string::npos )
    return false;
  
  //It looks like there can be multiple <event> tags in the file, but in the
  //  file I have with a second <event> tag, it is empty, so wont worry about now
  
  try
  {
    rapidxml::xml_document<char> doc;
    const char orig_close_char = filedata[event_tag_clos_pos];
    filedata[event_tag_clos_pos] = '\0';
    doc.parse<rapidxml::parse_non_destructive|rapidxml::allow_sloppy_parse>( &(filedata[event_tag_pos]) );
    
    const rapidxml::xml_node<char> *event_node = XML_FIRST_NODE((&doc),"event");
    if( !event_node )
      throw runtime_error( "Failed to get event node, even though it really should be there" );
    
    const rapidxml::xml_node<char> *detectors_node = XML_FIRST_NODE(event_node,"detectors");
    if( !detectors_node )
      throw runtime_error( "No detectors node" );
    
    const rapidxml::xml_node<char> *gamma_node = XML_FIRST_NODE(detectors_node,"gamma");
    if( !gamma_node )
      throw runtime_error( "No detectors node" );
    
    const rapidxml::xml_node<char> *sample_node = XML_FIRST_NODE(gamma_node,"sample");
    if( !sample_node )
      throw runtime_error( "No sample node" );
    const rapidxml::xml_node<char> *channels_node = XML_FIRST_NODE(sample_node,"channels");
    if( !channels_node || !channels_node->value_size() )
      throw runtime_error( "No sample channels node" );
    
    const std::string start_iso_str = xml_value_str( XML_FIRST_ATTRIB(event_node,"start_iso8601") );
    const auto start_time = SpecUtils::time_from_string( start_iso_str.c_str() );
    
    //const std::string end_iso_str = xml_value_str( XML_FIRST_ATTRIB(event_node,"end_iso8601") );
    //const auto end_time = SpecUtils::time_from_string( end_iso_str.c_str() );
    
    //Other attributes we could put into the comments or somethign
    //XML_FIRST_ATTRIB(event_node,"monitor_type") //"ARAM"
    //XML_FIRST_ATTRIB(event_node,"version")
    //XML_FIRST_ATTRIB(event_node,"start_timestamp") //"1464191873756"
    //XML_FIRST_ATTRIB(event_node,"monitor_name")  //"IST"
    //XML_FIRST_ATTRIB(event_node,"event_id") //"1464191866846"
    
    
    std::shared_ptr<Measurement> fore_meas, back_meas;
    fore_meas = make_shared<Measurement>();
    auto fore_channels = std::make_shared<vector<float>>();
    fore_channels->reserve( 1024 );
    SpecUtils::split_to_floats( channels_node->value(), channels_node->value_size(), *fore_channels );
    if( fore_channels->size() < 64 ) //64 is arbitrary
      throw runtime_error( "Not enough channels" );
    
    float live_time = 0.0f, real_time = 0.0f;
    xml_value_to_flt( XML_FIRST_ATTRIB(channels_node, "realtime"), real_time );
    xml_value_to_flt( XML_FIRST_ATTRIB(channels_node, "livetime"), live_time );
    fore_meas->set_gamma_counts( fore_channels, live_time/1000.0f, real_time/1000.0f );
    fore_meas->source_type_ = SourceType::Foreground;
    fore_meas->occupied_ = OccupancyStatus::Occupied;
    if( !start_time.is_special() )
      fore_meas->set_start_time( start_time );
    
    //See if neutrons are around
    const rapidxml::xml_node<char> *neutron_node = XML_FIRST_NODE(detectors_node,"neutron");
    const rapidxml::xml_node<char> *neutron_sample = xml_first_node_nso(neutron_node, "sample", "");
    const rapidxml::xml_node<char> *neutron_counts = xml_first_node_nso(neutron_sample, "counts", "");
    if( neutron_counts )
    {
      float total_neutrons = 0.0;
      if( xml_value_to_flt( XML_FIRST_ATTRIB(neutron_counts, "total"), total_neutrons) )
      {
        fore_meas->neutron_counts_.push_back( total_neutrons );
        fore_meas->neutron_counts_sum_ = total_neutrons;
        fore_meas->contained_neutron_ = true;
     
        if( xml_value_to_flt( XML_FIRST_ATTRIB(neutron_counts, "realtime"), real_time ) )
          fore_meas->remarks_.push_back( "Neutron real time: " + std::to_string(real_time/1000.0) + "s" );
        if( xml_value_to_flt( XML_FIRST_ATTRIB(neutron_counts, "livetime"), live_time ) )
          fore_meas->remarks_.push_back( "Neutron live time: " + std::to_string(live_time/1000.0) + "s" );
      }
    }//if( neutron_counts )
    
    const rapidxml::xml_node<char> *background_node = XML_FIRST_NODE(gamma_node,"background");
    channels_node = xml_first_node_nso( background_node, "channels", "" );
    if( channels_node && channels_node->value_size() )
    {
      back_meas = make_shared<Measurement>();
      auto back_channels = std::make_shared<vector<float>>();
      SpecUtils::split_to_floats( channels_node->value(), channels_node->value_size(), *back_channels );
      if( back_channels->size() >= 64 ) //64 is arbitrary
      {
        xml_value_to_flt( XML_FIRST_ATTRIB(channels_node, "realtime"), real_time );
        xml_value_to_flt( XML_FIRST_ATTRIB(channels_node, "livetime"), live_time );
        back_meas->set_gamma_counts( back_channels, live_time/1000.0f, real_time/1000.0f );
        back_meas->set_title( "Background" );
        back_meas->source_type_ = SourceType::Background;
        back_meas->occupied_ = OccupancyStatus::NotOccupied;
        if( !start_time.is_special() )
          back_meas->set_start_time( start_time );
        measurements_.push_back( back_meas );
      }
    }//if( background data )
    
    measurements_.push_back( fore_meas );
    
    //ToDO:
    //This file contains a time history of the gross count data (but only a single summed spectrum)
    const rapidxml::xml_node<char> *gamma_counts_node = XML_FIRST_NODE(gamma_node,"counts");
    if( gamma_counts_node )
      remarks_.push_back( "The ARAM file format has a time history in it that is not decoded" );
    
    filedata[event_tag_clos_pos] = orig_close_char;
    
    //Try and get energy calibration.
    size_t calib_pos = string::npos, coef_start = string::npos, coef_end = string::npos;
    calib_pos = filedata.rfind("<Calibration");
    if( calib_pos != string::npos )
      coef_start = filedata.find( "<Coefficients>", calib_pos );
    if( coef_start != string::npos )
      coef_end = filedata.find( "</Coefficients>", coef_start );
    if( coef_end != string::npos && ((coef_start+14) < coef_end) )
    {
      vector<float> coefs;
      const size_t coef_strlen = coef_end - coef_start - 14;
      SpecUtils::split_to_floats( &filedata[coef_start+14], coef_strlen, coefs );
      
      if( coefs.size() > 1 && coefs.size() < 10 )
      {
        fore_meas->energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
        fore_meas->calibration_coeffs_ = coefs;
        if( back_meas )
        {
          back_meas->energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
          back_meas->calibration_coeffs_ = coefs;
        }
      }
    }//if( we found the coefficients )
    
    vector<string> begin_remarks;
    const string begindata( &(filedata[0]), &(filedata[event_tag_pos]) );
    SpecUtils::split(begin_remarks, begindata, "\r\n");
    string lat_str, lon_str;
    for( const auto &remark : begin_remarks )
    {
      if( SpecUtils::istarts_with(remark, "Site Name:") )
        measurement_location_name_ = SpecUtils::trim_copy( remark.substr(10) );
      else if( SpecUtils::istarts_with(remark, "Site Longitude:") )
        lon_str = remark.substr( 15 ); //ex. "39deg 18min 15.2sec N"
      else if( SpecUtils::istarts_with(remark, "Site Latitude:") )
        lat_str = remark.substr( 14 ); //ex. "124deg 13min 51.8sec W"
      else
        remarks_.push_back( remark );
    }//for( const auto &remark : begin_remarks )
    
    if( lon_str.size() && lat_str.size() )
    {
      double lat, lon;
      const string coord = lon_str + " / " + lat_str;
      if( parse_deg_min_sec_lat_lon( coord.c_str(), coord.size(), lat, lon ) )
      {
        fore_meas->longitude_ = lon;
        fore_meas->latitude_ = lat;
        if( back_meas )
        {
          back_meas->longitude_ = lon;
          back_meas->latitude_ = lat;
        }
      }//if( can parse string )
    }//if( lat / lon str )
    
    //manufacturer_;
    instrument_model_ = "ARAM";

    //There is a <trigger...> node under <event> that describes why the event
    //  alarmed, should read that into comments or analysis results, or something
    
    parse_warnings_.emplace_back( "The ARAM file format has a time history in it that is not decoded" );
    
    cleanup_after_load();
    
    return true;
  }catch( std::exception & )
  {
    reset();
    input.seekg( start_pos );
    input.clear();
  }//try / catch
  
  input.exceptions( origexceptions );
  
  
  return false;
}//bool load_from_aram( std::istream &input )



}//namespace SpecUtils

