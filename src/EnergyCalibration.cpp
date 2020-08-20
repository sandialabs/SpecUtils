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

#include <cmath>
#include <string>
#include <vector>
#include <limits>
#include <cassert>
#include <iostream>
#include <iterator>
#include <algorithm>

#if( PERFORM_DEVELOPER_CHECKS )
#include <mutex>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <boost/current_function.hpp>
#endif

#include "SpecUtils/CubicSpline.h"
#include "SpecUtils/EnergyCalibration.h"


using namespace std;

namespace SpecUtils
{

EnergyCalibration::EnergyCalibration()
  : m_type( EnergyCalType::InvalidEquationType )
{
}


EnergyCalType EnergyCalibration::type() const
{
  return m_type;
}
  
 
const std::vector<float> &EnergyCalibration::coefficients() const
{
  if( m_type == EnergyCalType::LowerChannelEdge )
    return (m_channel_energies ? *m_channel_energies : m_coefficients);  //ptr check JIC, but shouldnt be needed.

  return m_coefficients;
}


const std::vector<std::pair<float,float>> &EnergyCalibration::deviation_pairs() const
{
  return m_deviation_pairs;
}
  

const std::shared_ptr<const std::vector<float>> &EnergyCalibration::channel_energies() const
{
  return m_channel_energies;
}
 

size_t EnergyCalibration::num_channels() const
{
  switch ( m_type )
  {
    case EnergyCalType::FullRangeFraction:
    case EnergyCalType::Polynomial:
    case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
    case EnergyCalType::LowerChannelEdge:
      assert( m_channel_energies );
      return ((m_channel_energies && (m_channel_energies->size() > 1))
               ? (m_channel_energies->size()-1) : size_t(0));
      
    case EnergyCalType::InvalidEquationType:
      return 0;
  }//switch ( m_type )
  
  assert( 0 );
  return 0;
}//num_channels()


void EnergyCalibration::set_polynomial( const size_t num_channels,
                                        const std::vector<float> &coeffs,
                                        const std::vector<std::pair<float,float>> &dev_pairs )
{
  /// \TODO: possibly loosen this up to not have a number of channel requirement... should be fine?
  if( num_channels < 2 )
    throw runtime_error( "EnergyCalibration::set_polynomial: must be called with >=2 channels" );
  
  //Find the last non-zero coefficients (e.g., strip off trailing zeros)
  size_t last_iter = coeffs.size();
  while( last_iter!=0 && coeffs[last_iter-1]==0.0f )
    --last_iter;
  
  if( last_iter < 2 )
    throw runtime_error( "EnergyCalibration::set_polynomial: must be called with >=2 coefficents" );
  
  // Do a sanity check on calibration coeffiecints that they are reasonable; #polynomial_binning
  //  will check if the are strictly increasing.
  if( (fabs(coeffs[0]) > 500.0)  //500 is arbitrary, but I have seen -450 in data!
      || (fabs(coeffs[1]) > 450.0)  //450 is arbitrary, lets 7 channels span 3000 keV
      || (last_iter==2 && coeffs[1]<=std::numeric_limits<float>::epsilon() )
      || (last_iter>=3 && coeffs[1]<=std::numeric_limits<float>::epsilon()
          && coeffs[2]<=std::numeric_limits<float>::epsilon()) )
   {
     throw runtime_error( "EnergyCalibration::set_polynomial: Coefficients are unreasonable" );
   }
  
  m_channel_energies = SpecUtils::polynomial_binning( coeffs, num_channels + 1, dev_pairs );
  
  m_type = EnergyCalType::Polynomial;
  m_coefficients.clear();
  m_coefficients.insert( end(m_coefficients), begin(coeffs), begin(coeffs)+last_iter );
  m_deviation_pairs = dev_pairs;
}//set_polynomial(...)
  
  
void EnergyCalibration::set_default_polynomial( const size_t num_channels,
                               const std::vector<float> &coeffs,
                               const std::vector<std::pair<float,float>> &dev_pairs )
{
  set_polynomial( num_channels, coeffs, dev_pairs );
  m_type = EnergyCalType::UnspecifiedUsingDefaultPolynomial;
}//set_default_polynomial(...)
  

void EnergyCalibration::set_full_range_fraction( const size_t num_channels,
                                const std::vector<float> &coeffs,
                                const std::vector<std::pair<float,float>> &dev_pairs )
{
  if( num_channels < 2 )
    throw runtime_error( "Full range fraction energy calibration requires >=2 channels" );
  
  //Find the last non-zero coefficients (e.g., strip off trailing zeros)
   size_t last_iter = coeffs.size();
   while( last_iter!=0 && coeffs[last_iter-1]==0.0f )
     --last_iter;
  
  if( last_iter < 2 )
    throw runtime_error( "Full range fraction energy calibration requires >=2 coefficents" );
  
  m_channel_energies = fullrangefraction_binning( coeffs, num_channels, dev_pairs, true );
  
  m_type = EnergyCalType::FullRangeFraction;
  m_coefficients.clear();
  m_coefficients.insert( end(m_coefficients), begin(coeffs), begin(coeffs)+last_iter );
  m_deviation_pairs = dev_pairs;
}//set_full_range_fraction(...)
  

void EnergyCalibration::check_lower_energies( const size_t num_channels,
                                              const std::vector<float> &channel_energies )
{
  if( num_channels < 2 )
    throw runtime_error( "EnergyCalibration::set_lower_channel_energy: must be called with >=2"
                         " channels" );
  
  if( num_channels > channel_energies.size() )
    throw runtime_error( "EnergyCalibration::set_lower_channel_energy: not enough channel energies"
                         " for the specified number of channels." );
  
  const size_t numsrc_channels = std::min( num_channels+1, channel_energies.size() );
  for( size_t i = 1; i < numsrc_channels; ++i )
  {
    if( channel_energies[i] < channel_energies[i-1] )
      throw std::runtime_error( "EnergyCalibration::set_lower_channel_energy: invalid calibration"
                                " passed in at channel " + std::to_string(i) );
  }
}//void check_lower_energies( const std::vector<float> &channel_energies )


void EnergyCalibration::set_lower_channel_energy( const size_t num_channels,
                                                  const std::vector<float> &channel_energies )
{
  check_lower_energies( num_channels, channel_energies );
  
  auto energies = std::make_shared<std::vector<float>>( num_channels + 1 );
  const size_t numsrc_channels = std::min( num_channels+1, channel_energies.size() );
  memcpy( &((*energies)[0]), &(channel_energies[0]), 4*numsrc_channels );
  
  if( numsrc_channels < (num_channels+1) )
  {
    assert( num_channels == channel_energies.size() );
    assert( numsrc_channels == channel_energies.size() );
    (*energies)[num_channels] = 2.0f*channel_energies[num_channels-1] - channel_energies[num_channels-2];
  }
  
  m_coefficients.clear();
  m_deviation_pairs.clear();
  m_type = EnergyCalType::LowerChannelEdge;
  m_channel_energies = energies;
}//set_lower_channel_energy(...)


void EnergyCalibration::set_lower_channel_energy( const size_t num_channels,
                                                  std::vector<float> &&channel_energies )
{
  check_lower_energies( num_channels, channel_energies );
  assert( channel_energies.size() >= num_channels );
  
  auto energies = std::make_shared<std::vector<float>>( std::move(channel_energies) );
  
  if( energies->size() < (num_channels+1) )
    energies->push_back( 2.0f*((*energies)[num_channels-1]) - ((*energies)[num_channels-2]) );
  if( energies->size() > (num_channels+1) )
    energies->resize( num_channels+1 );
  
  m_coefficients.clear();
  m_deviation_pairs.clear();
  m_type = EnergyCalType::LowerChannelEdge;
  m_channel_energies = energies;
}//set_lower_channel_energy(...)


size_t EnergyCalibration::memmorysize() const
{
  size_t nbytes = sizeof(EnergyCalibration);
  nbytes += m_coefficients.capacity() * sizeof(float);
  nbytes += m_deviation_pairs.capacity() * sizeof(std::pair<float,float>);
  if( m_channel_energies )
    nbytes += sizeof(std::vector<float>) + (m_channel_energies->capacity() * sizeof(float));
  return nbytes;
}//size_t memmorysize() const


float EnergyCalibration::channel_for_energy( const float energy ) const
{
  
  switch( m_type )
  {
    case EnergyCalType::InvalidEquationType:
      throw runtime_error( "EnergyCalibration::channel_for_energy: InvalidEquationType" );
      
    case EnergyCalType::Polynomial:
    case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
      return find_polynomial_channel( energy, m_coefficients, num_channels(),
                                      m_deviation_pairs, 0.001 );
      
    case EnergyCalType::FullRangeFraction:
      return find_fullrangefraction_channel( energy, m_coefficients, num_channels(),
                                             m_deviation_pairs, 0.001 );
      
    case EnergyCalType::LowerChannelEdge:
    {
      assert( m_channel_energies && m_channel_energies->size() >= 2 );
      const auto &energies = *m_channel_energies;
      
      //Using upper_bound instead of lower_bound to properly handle the case
      //  where x == bin lower energy.
      const auto iter = std::upper_bound( begin(energies), end(energies), energy );
      
      if( iter == begin(energies) )
        throw runtime_error( "EnergyCalibration::channel_for_energy: input below defined range" );
      
      if( iter == end(energies) )
        throw runtime_error( "EnergyCalibration::channel_for_energy: input above defined range" );
      
      //Linearly interpolate to get bin number.
      //  \TODO: use some sort of spline interpolation or something to make better estimate
      const float left_edge = *(iter - 1);
      const float right_edge = *iter;
      
      assert( energy >= left_edge );
      assert( energy <= right_edge );
      assert( right_edge > left_edge );
      const float fraction = (energy - left_edge) / (right_edge - left_edge);
      
      return (iter - begin(energies)) - 1 + fraction;
    }//case LowerChannelEdge:
  }//switch( m_type )
  
  assert( 0 );
  throw runtime_error( "Invalid cal - type - something really wack" );
  return 0.0;
}//float channel_for_energy(...)


float EnergyCalibration::energy_for_channel( const float channel ) const
{
  switch( m_type )
  {
    case EnergyCalType::InvalidEquationType:
      throw runtime_error( "EnergyCalibration::energy_for_channel: InvalidEquationType" );
    
    case EnergyCalType::Polynomial:
    case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
      return polynomial_energy( channel, m_coefficients, m_deviation_pairs );
      
    case EnergyCalType::FullRangeFraction:
      return fullrangefraction_energy( channel, m_coefficients, num_channels(), m_deviation_pairs );
      
    case EnergyCalType::LowerChannelEdge:
    {
      assert( m_channel_energies && m_channel_energies->size() >= 2 );
      const auto &energies = *m_channel_energies;
      
      if( channel < 0 )
        throw runtime_error( "EnergyCalibration::energy_for_channel: channel below zero" );
      
      if( (channel + 1) > energies.size() )
        throw runtime_error( "EnergyCalibration::energy_for_channel: channel to large" );
      
      const size_t chan = static_cast<size_t>( channel );
      const float frac = channel - chan;
      
      return energies[chan] + frac*(energies[chan+1] - energies[chan]);
    }//case EnergyCalType::LowerChannelEdge:
  }//switch( m_type )
  
  assert( 0 );
  throw runtime_error( "Invalid cal - type - something really wack" );
  return 0.0;
}//float energy_for_channel( const float channel ) const


bool EnergyCalibration::operator==( const EnergyCalibration &rhs ) const
{
  if( this == &rhs )
    return true;
  
  if( (m_type != rhs.m_type)
      || (m_coefficients != rhs.m_coefficients)
      || (m_deviation_pairs != rhs.m_deviation_pairs) )
    return false;
  
  if( (!m_channel_energies) != (!rhs.m_channel_energies) )
    return false;
  
  if( !m_channel_energies )
    return true;
  
  return (m_channel_energies->size() == rhs.m_channel_energies->size());
}//operator==


bool EnergyCalibration::operator!=( const EnergyCalibration &rhs ) const
{
  return !(operator==(rhs));
}//operator!=


bool EnergyCalibration::operator<( const EnergyCalibration &rhs ) const
{
  const size_t nleftchannel = m_channel_energies ? m_channel_energies->size() : 0u;
  const size_t nrightchannel = rhs.m_channel_energies ? rhs.m_channel_energies->size() : 0u;
  
  if( nleftchannel != nrightchannel )
    return (nleftchannel < nrightchannel);
  
  if( m_type != rhs.m_type )
    return (m_type < rhs.m_type);
  
  if( m_type == EnergyCalType::InvalidEquationType )
    return false;
  
  const bool is_lower_edge = (m_type == EnergyCalType::LowerChannelEdge);
  if( is_lower_edge )
  {
    assert( m_channel_energies );
    assert( rhs.m_channel_energies );
  }
  
  const vector<float> &lhscoef = (is_lower_edge ? *m_channel_energies : m_coefficients);
  const vector<float> &rhscoef = (is_lower_edge ? *rhs.m_channel_energies : rhs.m_coefficients);
  
  if( lhscoef.size() != rhscoef.size() )
    return (lhscoef.size() < rhscoef.size());
  
  for( size_t i = 0; i < lhscoef.size(); ++i )
  {
    const float leftcoef = lhscoef[i];
    const float rightcoef = rhscoef[i];
    const float maxcoef = std::max( fabs(leftcoef), fabs(rightcoef) );
    if( fabs(leftcoef - rightcoef) > (1.0E-5 * maxcoef) )
      return leftcoef < rightcoef;
  }//for( size_t i = 0; i < coefficients.size(); ++i )
  
  if( m_deviation_pairs.size() != rhs.m_deviation_pairs.size() )
    return (m_deviation_pairs.size() < rhs.m_deviation_pairs.size());
  
  for( size_t i = 0; i < m_deviation_pairs.size(); ++i )
  {
    const pair<float,float> &lv = m_deviation_pairs[i];
    const pair<float,float> &rv = rhs.m_deviation_pairs[i];
    const float maxenergy = std::max( fabs(lv.first), fabs(rv.first) );
    const float epsilon = 1.0E-5f;
    
    if( fabs(lv.first - rv.first) > (epsilon * maxenergy))
      return lv.first < rv.first;
      
    const float maxdeviation = std::max( fabs(lv.second), fabs(rv.second) );
    
    if( fabs(lv.second - rv.second) > (epsilon * maxdeviation) )
      return lv.second < rv.second;
  }//for( size_t i = 0; i < deviation_pairs_.size(); ++i )
      
  return false;
}//bool operator<(...)
      

#if( PERFORM_DEVELOPER_CHECKS )
void EnergyCalibration::equal_enough( const EnergyCalibration &lhs, const EnergyCalibration &rhs )
{
  char buffer[256] = { '\0' };
  
  auto floats_equiv = []( const float a, const float b ) -> bool {
    const auto diff = fabs(a - b);
    const auto maxval = std::max(fabs(a),fabs(b));
    return (diff <= (1.0E-5f*maxval) || (diff < std::numeric_limits<float>::epsilon()));
  };
  
  auto lhs_model = lhs.m_type;
  auto rhs_model = rhs.m_type;
  
  if( lhs_model == EnergyCalType::UnspecifiedUsingDefaultPolynomial )
    lhs_model = EnergyCalType::Polynomial;
  if( rhs_model == EnergyCalType::UnspecifiedUsingDefaultPolynomial )
    rhs_model = EnergyCalType::Polynomial;
  
  if( (lhs_model == rhs_model) && (lhs_model == EnergyCalType::InvalidEquationType) )
    return;
  
  if( (lhs_model != rhs_model)
      && (lhs_model == EnergyCalType::LowerChannelEdge || rhs_model == EnergyCalType::LowerChannelEdge) )
  {
    snprintf( buffer, sizeof(buffer), "Calibration model of LHS (%i) different from RHS (%i)",
              int(lhs.m_type), int(rhs.m_type) );
    throw runtime_error( buffer );
  }
  
  //Models are now: {both LowerChannelEdge}, {both Polynomial}, {both FullRangeFraction}, or
  //                {one Polynomial and one FullRangeFraction}
   
  assert( lhs.m_channel_energies );
  assert( rhs.m_channel_energies );
  
  const size_t nchannel = lhs.num_channels();
  
  if( nchannel != rhs.num_channels() )
  {
    snprintf( buffer, sizeof(buffer),
              "Calibrations have different number of channel energies (LHS %i, RHS %i)",
              static_cast<int>(lhs.m_channel_energies->size()),
              static_cast<int>(rhs.m_channel_energies->size()) );
    throw runtime_error( buffer );
  }
  
  const auto &lhsdev = lhs.m_deviation_pairs;
  const auto &rhsdev = rhs.m_deviation_pairs;
  
  if( lhsdev.size() != rhsdev.size() )
  {
    snprintf( buffer, sizeof(buffer), "Number of deviation pairs of RHS (%i) doesnt match LHS (%i)",
             static_cast<int>(lhsdev.size()), static_cast<int>(rhsdev.size()) );
    throw runtime_error( buffer );
  }
  
  for( size_t i = 0; i < lhsdev.size(); ++i )
  {
    if( fabs(lhsdev[i].first - rhsdev[i].first) > 0.001f )
    {
      snprintf( buffer, sizeof(buffer), "Deviation pair %i has RHS energy %f and LHS %f",
                static_cast<int>(i), lhsdev[i].first, rhsdev[i].first );
      throw runtime_error( buffer );
    }
    
    if( fabs(lhsdev[i].second - rhsdev[i].second) > 0.001f )
    {
      snprintf( buffer, sizeof(buffer), "Deviation pair %i has RHS offset %f and LHS %f",
                static_cast<int>(i), lhsdev[i].second, rhsdev[i].second );
      throw runtime_error( buffer );
    }
  }//for( loop over deviation pairs )
  
  auto lhscoef = lhs.m_coefficients;
  auto rhscoef = rhs.m_coefficients;
  
  //if( lhs_model == EnergyCalType::LowerChannelEdge )
  //{
  //  lhscoef = *lhs.m_channel_energies;
  //  rhscoef = *rhs.m_channel_energies;
  //}
  
  if( lhs_model != rhs_model )
  {
    if( lhs_model != EnergyCalType::Polynomial )
      lhscoef = fullrangefraction_coef_to_polynomial( lhscoef, nchannel );
    if( rhs_model != EnergyCalType::Polynomial )
      rhscoef = fullrangefraction_coef_to_polynomial( rhscoef, nchannel );
  }//if( lhs_model != rhs_model )
  
  while( lhscoef.size() && lhscoef.back() == 0.0f )
    lhscoef.erase( lhscoef.end() - 1 );
  while( rhscoef.size() && rhscoef.back() == 0.0f )
    rhscoef.erase( rhscoef.end() - 1 );
  
  string lhscoefstr = "{", rhscoefstr = "{";
  for( size_t i = 0; i < lhscoef.size(); ++i )
    lhscoefstr += (i ? ", " : "") + to_string(lhscoef[i]);
  lhscoefstr += "}";
  
  for( size_t i = 0; i < rhscoef.size(); ++i )
    rhscoefstr += (i ? ", " : "") + to_string(rhscoef[i]);
  rhscoefstr += "}";
  
  if( lhscoef.size() != rhscoef.size() )
  {
    snprintf( buffer, sizeof(buffer),
              "Number of calibration coefficients LHS (%s) and RHS (%s) do not match%s",
              lhscoefstr.c_str(), rhscoefstr.c_str(),
              ((lhs_model==rhs_model) ? "" : " after converting to be same type") );
    throw runtime_error( buffer );
  }
    
  for( size_t i = 0; i < rhscoef.size(); ++i )
  {
    if( !floats_equiv(lhscoef[i],rhscoef[i]) )
    {
      snprintf( buffer, sizeof(buffer),
                "Calibration coefficient %i of LHS (%1.8E) doesnt match RHS (%1.8E) (note, concerted calib type)",
                static_cast<int>(i), lhscoef[i], rhscoef[i] );
      throw runtime_error( buffer );
    }
  }//for( loop over coefficients )
  
  const size_t nenergies = std::min(lhs.m_channel_energies->size(), rhs.m_channel_energies->size());
  for( size_t i = 0; i < nenergies; ++i )
  {
    const float lhsenergy = lhs.m_channel_energies->at(i);
    const float rhsenergy = rhs.m_channel_energies->at(i);
    const float diff = fabs(lhsenergy - rhsenergy );
    if( diff > 0.00001*std::max(fabs(lhsenergy), fabs(rhsenergy)) && diff > 0.001 )
    {
      snprintf( buffer, sizeof(buffer),
               "Energy of channel %i of LHS (%1.8E) doesnt match RHS (%1.8E)",
               int(i), lhs.m_channel_energies->at(i),
               rhs.m_channel_energies->at(i) );
      throw runtime_error( buffer );
    }
  }//for( loop over channel energies )
  
}//equal_enough( lhs, rhs )
#endif //PERFORM_DEVELOPER_CHECKS



std::shared_ptr< const std::vector<float> > polynomial_binning( const vector<float> &coeffs,
                                                                 const size_t nbin,
                                                                 const std::vector<std::pair<float,float>> &dev_pairs )
{
  auto answer = make_shared<vector<float>>(nbin, 0.0f);
  const size_t ncoeffs = coeffs.size();
  
  float prev_energy = -std::numeric_limits<float>::infinity();
  for( size_t i = 0; i < nbin; i++ )
  {
    float val = 0.0f;
    for( size_t c = 0; c < ncoeffs; ++c )
      val += coeffs[c] * pow( static_cast<float>(i), static_cast<float>(c) );
    answer->operator[](i) = val;
    
    //Note, #apply_deviation_pair will also check for strickly increasing, but _after_ applying
    //  deviation pairs (since there you could have a FRF that is only valid with the dev pairs)
    // \ToDo: check for infs and NaNs too
    // \ToDo: could take derivative of coefficients instead and use that to check if negative
    //        or zero derivative anywhere, and see if this is faster than checking every entry.
    if( dev_pairs.empty() && (val <= prev_energy) )
    {
      string msg = "Invalid polynomial equation {";
      for( size_t c = 0; c < ncoeffs; ++c )
        msg += (c ? ", " : "") + std::to_string(coeffs[c]);
      msg += "} starting at channel " + std::to_string(i);
      throw std::runtime_error( msg );
    }//if( invalid energy calibration )
    
    prev_energy = val;
  }//for( loop over bins, i )
  
  if( dev_pairs.empty() )
    return answer;
  
  return apply_deviation_pair( *answer, dev_pairs );
}//std::shared_ptr< const std::vector<float> > polynomial_binning( const vector<float> &coefficients, size_t nbin )
  
  
std::shared_ptr< const std::vector<float> > fullrangefraction_binning( const vector<float> &coeffs,
                                                         const size_t nbin,
                                                         const vector<pair<float,float>> &dev_pairs,
                                                        const bool include_upper_energy )
{
  const size_t nentries = nbin + (include_upper_energy ? 1 : 0);
  auto answer = make_shared<vector<float>>( nentries, 0.0f );
  const size_t ncoeffs = std::min( coeffs.size(), size_t(4) );
  const float low_e_coef = (coeffs.size() > 4) ? coeffs[4] : 0.0f;
  
  float prev_energy = -std::numeric_limits<float>::infinity();
  for( size_t i = 0; i < nentries; i++ )
  {
    const float x = static_cast<float>(i)/static_cast<float>(nbin);
    float &val = answer->operator[](i);
    for( size_t c = 0; c < ncoeffs; ++c )
      val += coeffs[c] * pow(x,static_cast<float>(c) );
    val += low_e_coef / (1.0f+60.0f*x);
    
    //Note, #apply_deviation_pair will also check for strickly increasing, but _after_ applying
    //  deviation pairs (since there you could have a FRF that is only valid with the dev pairs)
    // \ToDo: check for infs and NaNs too
    if( dev_pairs.empty() && (val <= prev_energy) )
    {
      string msg = "Invalid FullRangeFranction equation {";
      for( size_t c = 0; c < ncoeffs; ++c )
        msg += (c ? ", " : "") + std::to_string(coeffs[c]);
      msg += "} starting at channel " + std::to_string(i);
      throw std::runtime_error( msg );
    }//if( invalid energy calibration )
  }//for( loop over bins, i )
  
  if( dev_pairs.empty() )
    return answer;
  
  return apply_deviation_pair( *answer, dev_pairs );
}//std::shared_ptr< const std::vector<float> > fullrangefraction_binning(...)
  
  
float fullrangefraction_energy( const float bin_number,
                                 const std::vector<float> &coeffs,
                                 const size_t nbin,
                                 const std::vector<std::pair<float,float>> &deviation_pairs )
{
  const float x = bin_number/static_cast<float>(nbin);
  float val = 0.0;
  for( size_t c = 0; c < coeffs.size(); ++c )
    val += coeffs[c] * pow(x,static_cast<float>(c) );
  return val + deviation_pair_correction( val, deviation_pairs );
}//float fullrangefraction_energy(...)


float polynomial_energy( const float channel_number,
                         const std::vector<float> &coeffs,
                         const std::vector<std::pair<float,float>> &deviation_pairs )
{
  float val = 0.0f;
  for( size_t i = 0; i < coeffs.size(); ++i )
    val += coeffs[i] * pow( channel_number, static_cast<float>(i) );
  return val + deviation_pair_correction( val, deviation_pairs );
}//polynomial_energy(...)

  
shared_ptr<const vector<float>> apply_deviation_pair( const vector<float> &binning,
                                      const vector<std::pair<float,float>> &dps )
{
  /* An example for using deviation pairs:
   - Determine offset and gain using the 239 keV and 2614 keV peaks of Th232
   - If the k-40 1460 keV peak is now at 1450 keV, a deviation of 10 keV should be set at 1460 keV
   - Deviation pairs set to zero would be defined for 239 keV and 2614 keV
   */
  
  auto answer = make_shared<vector<float>>( binning );
  if( dps.empty() || binning.empty() )
    return answer;
  
  const vector<CubicSplineNode> spline = create_cubic_spline_for_dev_pairs( dps );
  if( spline.empty() )
    return answer;
  
  vector<float> &ex = *answer;
  
  
  const auto lessThanUB = []( const float energy, const CubicSplineNode &node ) -> bool {
    return energy < node.x;
  };
  
  auto spline_pos = std::upper_bound( begin(spline), end(spline), ex[0], lessThanUB );
  if( spline_pos == end(spline) )
    return answer;
  
  for( size_t i = 0; i < ex.size(); ++i )
  {
    while( (spline_pos != end(spline)) && (ex[i] > spline_pos->x) )
      ++spline_pos;
    
    if( spline_pos == begin(spline) )
    {
      ex[i] += static_cast<float>( spline_pos->y );
    }else if( spline_pos == end(spline) )
    {
      ex[i] += static_cast<float>( (spline_pos-1)->y );
    }else
    {
      const CubicSplineNode &node = *(spline_pos-1);
      const double h = ex[i] - node.x;
      ex[i] += static_cast<float>( ((node.a*h + node.b)*h + node.c)*h + node.y );
    }
    
    //ToDo: Check for infs and NaNs here
    if( i && (ex[i] <= ex[i-1]) )
    {
      if( binning.at(i-1) >= binning.at(i) )
        throw std::runtime_error( "Invalid energy calibration starting at channel "
                                  + std::to_string(i) );
      throw std::runtime_error( "apply_deviation_pair: application of deviation pairs caused"
                                " calbration to become invalid starting at channel "
                                + std::to_string(i) );
    }
  }//for( size_t i = 0; i < ex.size(); ++i )
  
  return answer;
}//std::vector<float> apply_deviation_pair(...)


float deviation_pair_correction( const float energy, const std::vector<std::pair<float,float>> &dps )
{
  if( dps.empty() )
    return 0.0f;
  
  const vector<CubicSplineNode> spline = create_cubic_spline_for_dev_pairs( dps );
  
  return eval_cubic_spline( energy, spline );
}//float deviation_pair_correction(...)

  
float correction_due_to_dev_pairs( const float true_energy,
                                    const std::vector<std::pair<float,float>> &dev_pairs )
{
  if( dev_pairs.empty() )
    return 0.0f;
  
  const vector<CubicSplineNode> spline = create_cubic_spline_for_dev_pairs( dev_pairs );
  const vector<CubicSplineNode> inv_spline = create_inverse_dev_pairs_cubic_spline( dev_pairs );
  
  const float initial_answer = eval_cubic_spline( true_energy, inv_spline );
  
  //I would think once the going forward/backward with the spline is sorted out,
  //  'initial_answer' would always be right, but currently it can be off by up
  //  to a few keV in some cases... not exactly sure of the cause yet, so we'll
  //  hack it for the moment.
  
  const float initial_check = eval_cubic_spline( true_energy - initial_answer, spline );
  const float initial_diff = initial_answer - initial_check;
  const float tolerance  = 0.01f;
  
  //cout << "For " << true_energy << ", correction_due_to_dev_pairs was off by "
  //     << fabs(initial_answer - initial_check) << " keV" << endl;
  
  if( fabs(initial_diff) < tolerance )
    return initial_answer;
  
  int niters = 0;
  
  float answer = initial_answer;
  float check = initial_check;
  float diff = answer - check;
  
  //This loop seems to converge within 3 iterations, pretty much always.
  while( fabs(diff) > tolerance )
  {
    answer -= diff;
    check = eval_cubic_spline( true_energy - answer, spline );
    diff = answer - check;

    // Make sure we wont get stuck oscilating around for some reason.
    // I havent actually seen this happen, but JIC.
    if( ++niters > 10 )
    {
      const bool initial_is_closer = (fabs(initial_diff) < fabs(diff));
      const float final_answer = initial_is_closer ? initial_answer : answer;
      
#if( PERFORM_DEVELOPER_CHECKS )
      stringstream msg;
      msg << "correction_due_to_dev_pairs( " << true_energy << " keV ): Failed to converge on an answer"
          << " (current diff=" << diff << "), so returning with "
          << "(" << (initial_is_closer ? "initial" : "current") << ") diff of " << final_answer << endl;
      cout << msg.str() << endl;
      log_developer_error( __func__, msg.str().c_str() );
#endif

      return final_answer;
    }//if( niters > 10 )
  }//while( fabs(diff) > tolerance )
  
  //cout << "Final answer after " << niters << " iterations is accruate within "
  //     << diff << " keV; for true energy " << true_energy
  //     << " with result "
  //     << ((true_energy - answer) + eval_cubic_spline( true_energy - answer, spline ))
  //     << endl;
  
  return answer;
}//correction_due_to_dev_pairs
  

vector<float> polynomial_coef_to_fullrangefraction( const vector<float> &coeffs,
                                                                const size_t nbin )
{
  float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
  float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f;
  
  if( coeffs.size() > 0 )
    c0 = coeffs[0];
  if( coeffs.size() > 1 )
    c1 = coeffs[1];
  if( coeffs.size() > 2 )
    c2 = coeffs[2];
  if( coeffs.size() > 3 )
    c3 = coeffs[3];
  
  //See refKMZF7EBVUT
  //  a0 = c0 - 0.5f*c1 + 0.25f*c2 + (1.0f/8.0f)*c3;
  //  a1 = nbin*(c1 + c2 + 0.75f*c3);
  //  a2 = nbin*nbin*(c2 + 1.5f*c3);
  //  a3 = nbin*nbin*nbin*c3;
  
  //See ref9CDCULKVGZ, but basically some vendors implementations
  a0 = c0;
  a1 = nbin*c1;
  a2 = nbin*nbin*c2;
  a3 = nbin*nbin*nbin*c3;
  
  
  vector<float> answer;
  answer.push_back( a0 );
  answer.push_back( a1 );
  if( a2 != 0.0f || a3 != 0.0f )
    answer.push_back( a2 );
  if( a3 != 0.0f )
    answer.push_back( a3 );
  
  return answer;
}//vector<float> polynomial_coeef_to_fullrangefraction( const vector<float> &coeffs )

  
std::vector<float> fullrangefraction_coef_to_polynomial( const std::vector<float> &coeffs,
                                                         const size_t nbinint )
{
  if( !nbinint || coeffs.empty() )
    return std::vector<float>();
  
  //nbin needs to be a float, not an integer, or else integer overflow
  //  can happen on 32 bit systems when its cubed.
  const float nbin = static_cast<float>( nbinint );
  const size_t npars = coeffs.size();
  float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
  float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f;
  
  if( npars > 0 )
    a0 = coeffs[0];
  if( npars > 1 )
    a1 = coeffs[1];
  if( npars > 2 )
    a2 = coeffs[2];
  if( npars > 3 )
    a3 = coeffs[3];
  
  //Logic from from "Energy Calibration Conventions and Parameter Conversions"
  //  Dean J. Mitchell, George Lasche and John Mattingly, 6418 / MS-0791
  //The below "- (0.5f*a1/nbin)" should actually be "+ (0.5f*a1/nbin)"
  //  according to the paper above, I changed it to compensate for the
  //  similar hack in polynomial_coef_to_fullrangefraction.
  //  c0 = a0 + (0.5f*a1/nbin) + 0.25f*a2/(nbin*nbin) - (a3/(8.0f*nbin*nbin*nbin));
  //  c1 = (a1/nbin) - (a2/(nbin*nbin)) + 0.75f*(a3/(nbin*nbin*nbin));
  //  c2 = (a2/(nbin*nbin)) - 1.5f*(a3/(nbin*nbin*nbin));
  //  c3 = a3/(nbin*nbin*nbin);
  
  //See notes in polynomial_coef_to_fullrangefraction(...) for justification
  //  of using the below, instead of the above.
  c0 = a0;
  c1 = a1 / nbin;
  c2 = a2 / (nbin*nbin);
  c3 = a3 / (nbin*nbin*nbin);
  
  vector<float> answer;
  if( c0!=0.0f || c1!=0.0f || c2!=0.0f || c3!=0.0f )
  {
    answer.push_back( c0 );
    if( c1!=0.0f || c2!=0.0f || c3!=0.0f )
    {
      answer.push_back( c1 );
      if( c2 != 0.0f || c3!=0.0f )
      {
        answer.push_back( c2 );
        if( c3!=0.0f )
          answer.push_back( c3 );
      }
    }//if( p1!=0.0 || p2!=0.0 )
  }//if( p0!=0.0 || p1!=0.0 || p2!=0.0 )
  
  return answer;
}//std::vector<float> fullrangefraction_coef_to_polynomial( const std::vector<float> &coeffs, const size_t nbin )
  

vector<float> mid_channel_polynomial_to_fullrangeFraction( const vector<float> &coeffs, const size_t nbini )
{
  const float nbin = static_cast<float>( nbini );
  float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
  float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f;
  
  if( coeffs.size() > 0 )
    c0 = coeffs[0];
  if( coeffs.size() > 1 )
    c1 = coeffs[1];
  if( coeffs.size() > 2 )
    c2 = coeffs[2];
  if( coeffs.size() > 3 )
    c3 = coeffs[3];
  
  //Logic from from "Energy Calibration Conventions and Parameter Conversions"
  //  Dean J. Mitchell, George Lasche and John Mattingly, 6418 / MS-0791
  //The below "- 0.5f*c1" should actually be "+ 0.5f*c1" according to the
  //  paper above, but to agree with PeakEasy I changed this.
  a0 = c0 - 0.5f*c1 + 0.25f*c2 + (1.0f/8.0f)*c3;
  a1 = nbin*(c1 + c2 + 0.75f*c3);
  a2 = nbin*nbin*(c2 + 1.5f*c3);
  a3 = nbin*nbin*nbin*c3;
  
  vector<float> answer;
  answer.push_back( a0 );
  answer.push_back( a1 );
  if( a2 != 0.0f || a3 != 0.0f )
    answer.push_back( a2 );
  if( a3 != 0.0f )
    answer.push_back( a3 );
  
  return answer;
}//mid_channel_polynomial_to_fullrangeFraction()

  
bool calibration_is_valid( const EnergyCalType type,
                           const std::vector<float> &ineqn,
                           const std::vector< std::pair<float,float> > &devpairs,
                           size_t nbin )
{
  //ToDo: also check that the energy range is halfway reasonable...
  //ToDo: its pretty easy to check polynomial algebraicly...
  
#if( PERFORM_DEVELOPER_CHECKS )
  auto double_check_binning = [ineqn,devpairs]( shared_ptr<const vector<float>> binning ){
    for( size_t i = 1; i < binning->size(); ++i )
    {
      if( (*binning)[i] <= (*binning)[i-1] )
      {
        char buffer[512];
        snprintf( buffer, sizeof(buffer),
                 "Energy of bin %i is lower or equal to the one before it",
                 int(i) );
        stringstream msg;
        msg << "Energy of bin " << i << " is lower or equal to the one before it"
        << " for coefficients {";
        for( size_t j = 0; j < ineqn.size(); ++j )
          msg << (j ? ", " : "") << ineqn[j];
        msg << "} and deviation pairs {";
        
        for( size_t j = 0; j < devpairs.size(); ++j )
          msg << (j ? ", {" : "{") << devpairs[j].first << ", " << devpairs[j].second << "}";
        msg << "}";
        
        log_developer_error( __func__, msg.str().c_str() );
      }
    }//for( size_t i = 1; i < binning->size(); ++i )
  }; //double_check_binning lamda
#endif

  for( const float &val : ineqn )
  {
    if( IsInf(val) || IsNan(val) )
      return false;
  }
  
  
  switch( type )
  {
    case EnergyCalType::Polynomial:
    case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
    {
      // \TODO: Go through every channel or use derivatives if deviation pairs not available.
      const float nearend   = polynomial_energy( float(nbin-2), ineqn, devpairs );
      const float end       = polynomial_energy( float(nbin-1), ineqn, devpairs );
      const float begin     = polynomial_energy( 0.0f,      ineqn, devpairs );
      const float nearbegin = polynomial_energy( 1.0f,      ineqn, devpairs );
      
      const bool valid = !( (nearend >= end) || (begin >= nearbegin) );
      
#if( PERFORM_DEVELOPER_CHECKS )
      if( valid )
      {
        try
        {
          auto binning = polynomial_binning( ineqn, nbin, devpairs );
          double_check_binning( binning );
        }catch( std::exception &e )
        {
          log_developer_error( __func__, ("Couldnt enumerate polynomial binning for calibration"
                                          " thought to be valid: " + std::string(e.what())).c_str() );
        }
      }//if( valid )
#endif

    
      break;
    }//case polynomial
      
    case EnergyCalType::FullRangeFraction:
    {
      // \TODO: Go through every channel or use derivatives if deviation pairs not available.
      const float nearend   = fullrangefraction_energy( float(nbin-2), ineqn, nbin, devpairs );
      const float end       = fullrangefraction_energy( float(nbin-1), ineqn, nbin, devpairs );
      const float begin     = fullrangefraction_energy( 0.0f,      ineqn, nbin, devpairs );
      const float nearbegin = fullrangefraction_energy( 1.0f,      ineqn, nbin, devpairs );
      
      const bool valid = !( (nearend >= end) || (begin >= nearbegin) );
      
#if( PERFORM_DEVELOPER_CHECKS )
      if( valid )
      {
        try
        {
          auto binning = fullrangefraction_binning( ineqn, nbin, devpairs );
          double_check_binning( binning );
        }catch( std::exception &e )
        {
          log_developer_error( __func__, ("Couldnt enumerate FRF binning for calibration"
                               " thought to be valid: " + std::string(e.what())).c_str() );
        }
      }//if( valid )
#endif
      return valid;
    }//case EnergyCalType::FullRangeFraction:
      
    case EnergyCalType::LowerChannelEdge:
    {
      for( size_t i = 1; i < ineqn.size(); ++i )
        if( ineqn[i-1] > ineqn[i] )
          return false;
      return (!ineqn.empty() && (ineqn.size()>=nbin));
    }//case EnergyCalType::LowerChannelEdge:
      
    case EnergyCalType::InvalidEquationType:
      break;
  }//switch( type )
  
  return false;
}//checkFullRangeFractionCoeffsValid

  
  
std::vector<float> polynomial_cal_remove_first_channels( const int nchannel,
                                                          const std::vector<float> &a )
{
  const float n = static_cast<float>( nchannel );  //to avoid integer overflow
  std::vector<float> answer( a.size() );
  
  if( a.size() == 2 )
  {
    answer[0] = a[0] + n*a[1];
    answer[1] = a[1];
  }else if( a.size() == 3 )
  {
    answer[0] = a[0] + n*a[1] + n*n*a[2];
    answer[1] = a[1] + 2.0f*n*a[2];
    answer[2] = a[2];
  }else if( a.size() == 4 )
  {
    answer[0] = n*n*n*a[3] + n*n*a[2] + n*a[1] + a[0];
    answer[1] = 3.0f*n*n*a[3] + 2.0f*n*a[2] + a[1];
    answer[2] = 3.0f*n*a[3] + a[2];
    answer[3] = a[3];
  }else if( a.size() == 5 )
  {
    answer[0] = n*n*n*n*a[4] + n*n*n*a[3] + n*n*a[2] + n*a[1] + a[0];
    answer[1] = 4.0f*n*n*n*a[4] + 3.0f*n*n*a[3] + 2.0f*n*a[2] + a[1];
    answer[2] = 6.0f*n*n*a[4] + 3.0f*n*a[3] + a[2];
    answer[3] = 4.0f*n*a[4] + a[3];
    answer[4] = a[4];
  }else if( a.size() >= 6 )
  {
    //Ignore anything past the 6th coefficient (its questionalble if we even
    //  need to go up this high).
    /*
     //Sage code to solve for the coefficients.
     %var S, x, n, a0, a1, a2, a3, a4, a5, b0, b1, b2, b3, b4, b5
     S = b0 + b1*(x-n) + b2*(x-n)^2 + b3*(x-n)^3 + b4*(x-n)^4 + b5*(x-n)^5
     
     r5 = (a5 == S.expand().coefficients(x)[5][0]).solve(b5)
     r4 = (a4 == S.expand().coefficients(x)[4][0]).subs_expr(r5[0]).solve(b4)
     r3 = (a3 == S.expand().coefficients(x)[3][0]).subs_expr(r4[0],r5[0]).solve(b3)
     r2 = (a2 == S.expand().coefficients(x)[2][0]).subs_expr(r3[0],r4[0],r5[0]).solve(b2)
     r1 = (a1 == S.expand().coefficients(x)[1][0]).subs_expr(r2[0],r3[0],r4[0],r5[0]).solve(b1)
     r0 = (a0 == S.expand().coefficients(x)[0][0]).subs_expr(r1[0],r2[0],r3[0],r4[0],r5[0]).solve(b0)
     */
    answer.resize( 6, 0.0f );
    answer[0] = pow(n,5.0f)*a[5] + pow(n,4.0f)*a[4] + pow(n,3.0f)*a[3] + pow(n,2.0f)*a[2] + n*a[1] + a[0];
    answer[1] = 5*pow(n,4.0f)*a[5] + 4*pow(n,3.0f)*a[4] + 3*pow(n,2.0f)*a[3] + 2.0f*n*a[2] + a[1];
    answer[2] = 10*pow(n,3.0f)*a[5] + 6*pow(n,2.0f)*a[4] + 3*n*a[3] + a[2];
    answer[3] = 10*pow(n,2.0f)*a[5] + 4*n*a[4] + a[3];
    answer[4] = 5*n*a[5] + a[4];
    answer[5] = a[5];
  }
  
  return answer;
}//polynomial_cal_remove_first_channels(...)

  
  
  
  
float find_fullrangefraction_channel( const double energy,
                                   const std::vector<float> &coeffs,
                                   const size_t nbin,
                                   const std::vector<std::pair<float,float>> &devpair,
                                   const double accuracy )
{
  size_t ncoefs = 0; //Will be the index+1 of last non-zero coefficient
  for( size_t i = 0; i < coeffs.size(); ++i )
    if( fabs(coeffs[i]) > std::numeric_limits<float>::epsilon() )
      ncoefs = i+1;
  
  if( nbin < 2 )
    throw runtime_error( "find_fullrangefraction_channel: must have at least 2 channels" );
  
  if( ncoefs < 2  )
    throw runtime_error( "find_fullrangefraction_channel: must pass in at least two coefficients" );
  
  if( ncoefs < 4 && devpair.empty() )
  {
    if( ncoefs == 2  )
    {
      //  energy =  coeffs[0] + coeffs[1]*(bin/nbins)
      return static_cast<float>( nbin * (energy - coeffs[0]) / coeffs[1] );
    }//if( coeffs.size() == 2  )
    
    //Note purposeful use of double precision
    
    const double a = double(coeffs[0]) - double(energy);
    const double b = coeffs[1];
    const double c = coeffs[2];
    
    //energy = coeffs[0] + coeffs[1]*(bin/nbin) + coeffs[2]*(bin/nbin)*(bin/nbin)
    //--> 0 = a + b*(bin/nbin) + c*(bin/nbin)*(bin/nbin)
    //roots at (-b +- sqrt(b*b-4*a*c))/(2c)
    
    const double sqrtarg = b*b-4.0f*a*c;
    
    if( sqrtarg >= 0.0 )
    {
      const double root_1 = (-b + sqrt(sqrtarg))/(2.0f*c);
      const double root_2 = (-b - sqrt(sqrtarg))/(2.0f*c);
      
      // Check of one of the answers is in the expected range.
      const bool root_1_valid = (root_1 >= 0.0 && root_1 <= static_cast<double>(nbin + 1));
      const bool root_2_valid = (root_2 >= 0.0 && root_2 <= static_cast<double>(nbin + 1));
      
      // Preffer to return the answer within the defined bin range if only one of them is
      if( root_1_valid != root_2_valid )
        return static_cast<float>( nbin * (root_1_valid ? root_1 : root_2) );

      // If both answers are positive, or both negative, return the one with smaller absolute value
      if( (root_1 >= 0.0 && root_2 >= 0.0) || (root_1 <= 0.0 && root_2 <= 0.0) )
        return static_cast<float>( nbin * ((fabs(root_1) < fabs(root_2)) ? root_1 : root_2) );
      
      //  \TODO: determine upper valid bin (e.g., last channel were quadratic term doesnt overpower
      //         linear term) and return the bin below that, and if that doesnt work, throw
      //         exception.
      const double linanswer = (energy - coeffs[0]) / coeffs[1];
      const double d1 = fabs(root_1 - linanswer);
      const double d2 = fabs(root_2 - linanswer);
      return static_cast<float>(  nbin * ((d1 < d2) ? root_1 : root_2) );
    }//if( sqrtarg >= 0.0f )
  }//if( ncoefs < 4 && devpair.empty() )
  
  if( accuracy <= 0.0 )
    throw runtime_error( "find_fullrangefraction_channel: accuracy must be greater than zero" );
  
  const size_t max_iterations = 1000;
  size_t iteration = 0;
  
  float lowbin = 0.0;
  float highbin = static_cast<float>( nbin );
  float testenergy = fullrangefraction_energy( highbin, coeffs, nbin, devpair );
  while( (testenergy < energy) && (iteration < max_iterations) )
  {
    // At too high of channels the calibration can become invalid so we will only increase by
    //  1/8 the spectrum at a time.  Worst case sceneriou this could take a while to get to
    //  (if ncoefs < 3, then could double highbin safely)
    highbin += std::max(0.125*nbin,2.0);
    testenergy = fullrangefraction_energy( highbin, coeffs, nbin, devpair );
    ++iteration;
  }//while( testenergy < energy )
  
  if( iteration >= max_iterations )
    throw runtime_error( "find_fullrangefraction_channel: failed to find channel high-enough" );
  
  testenergy = fullrangefraction_energy( lowbin, coeffs, nbin, devpair );
  while( (testenergy > energy) && (iteration < max_iterations) )
  {
    lowbin -= std::max(0.125*nbin,2.0);
    testenergy = fullrangefraction_energy( lowbin, coeffs, nbin, devpair );
    ++iteration;
  }//while( testenergy < energy )
  
  if( iteration >= max_iterations )
    throw runtime_error( "find_fullrangefraction_channel: failed to find channel low-enough" );
  
  float bin = lowbin + ((highbin-lowbin)/2.0f);
  testenergy = fullrangefraction_energy( bin, coeffs, nbin, devpair );
  float dx = static_cast<float>( fabs(testenergy-energy) );
  
  while( (dx > accuracy) && (iteration < max_iterations) )
  {
    if( highbin == lowbin )
    {
#if( PERFORM_DEVELOPER_CHECKS )
      log_developer_error( __func__, "Possible error in find_fullrangefraction_channel... check out" );
#endif
      throw runtime_error( "find_fullrangefraction_channel(...): error finding bin coorespongin to"
                           " desired energy (this shouldnt happen)" );
    }
    
    if( testenergy == energy )
      return bin;
    if( testenergy > energy )
      highbin = bin;
    else
      lowbin = bin;
    
    bin = lowbin + ((highbin-lowbin)/2.0f);
    testenergy = fullrangefraction_energy( bin, coeffs, nbin, devpair );
    dx = static_cast<float>( fabs(testenergy-energy) );
    ++iteration;
  }//while( dx > accuracy )
  
  if( iteration >= max_iterations )
    throw runtime_error( "find_fullrangefraction_channel: failed to converge" );
  
  return bin;
}//float find_fullrangefraction_channel(...)
  

float find_polynomial_channel( const double energy, const vector<float> &coeffs,
                               const size_t nchannel,
                               const vector<pair<float,float>> &devpair,
                               const double accuracy )
{
  size_t ncoefs = 0; //Will be the index+1 of last non-zero coefficient
  for( size_t i = 0; i < coeffs.size(); ++i )
    if( fabs(coeffs[i]) > std::numeric_limits<float>::epsilon() )
      ncoefs = i+1;
  
  assert( coeffs.size() >= ncoefs );
  
  if( ncoefs < 2  )
    throw std::runtime_error( "find_polynomial_channel: must pass in at least two coefficients" );
  
  if( ncoefs < 4  )
  {
    double polyenergy = energy;
    if( !devpair.empty() )
     polyenergy -= correction_due_to_dev_pairs(energy,devpair);
    
    if( ncoefs == 2  )
    {
      //  energy =  coeffs[0] + coeffs[1]*channel
      return ((polyenergy - coeffs[0]) / coeffs[1]);
    }//if( coeffs.size() == 2  )
    
    //Note purposeful use of double precision
    const double a = double(coeffs[0]) - double(polyenergy);
    const double b = coeffs[1];
    const double c = coeffs[2];
    
    //polyenergy = coeffs[0] + coeffs[1]*bin + coeffs[2]*bin*bin
    //--> 0 = a + b*bin + c*bin*bin
    //roots at (-b +- sqrt(b*b-4*a*c))/(2c)
    
    const double sqrtarg = b*b - 4.0f*a*c;
    
    if( sqrtarg >= 0.0 )
    {
      const double root_1 = (-b + sqrt(sqrtarg))/(2.0f*c);
      const double root_2 = (-b - sqrt(sqrtarg))/(2.0f*c);
      
      // Check of one of the answers is in the expected range.
      const bool root_1_valid = (root_1 >= 0.0 && root_1 <= static_cast<double>(nchannel + 1));
      const bool root_2_valid = (root_2 >= 0.0 && root_2 <= static_cast<double>(nchannel + 1));
      
      // Preffer to return the answer within the defined bin range if only one of them is
      if( root_1_valid != root_2_valid )
        return static_cast<float>( root_1_valid ? root_1 : root_2 );
      
      // If both answers are positive, or both negative, return the one with smaller absolute value
      if( (root_1 >= 0.0 && root_2 >= 0.0) || (root_1 <= 0.0 && root_2 <= 0.0) )
        return static_cast<float>( (fabs(root_1) < fabs(root_2)) ? root_1 : root_2 );
      
      // If one answer is positive, and one negative, return the one closest to what the linearly
      //  truncated equation would give.
      //  \TODO: determine upper valid bin (e.g., last channel were quadratic term doesnt overpower
      //         linear term) and return the bin below that, and if that doesnt work, throw
      //         exception.
      //  Examples for Poly ceffs {-1.926107, 2.9493925, -0.00000831},
      //                  polyenergy=-10: linanswer=-2.73748, root_1=-2.73746, root_2=354640
      //                  polyenergy=60000: linanswer=20343.8, root_1=21667.7, root_2=332970
      const double linanswer = ((polyenergy - coeffs[0]) / coeffs[1]);
      
      //cout << "For polyenergy=" << polyenergy << " linanswer=" << linanswer << ", root_1=" << root_1 << ", root_2=" << root_2 << endl;
      const double d1 = fabs(root_1 - linanswer);
      const double d2 = fabs(root_2 - linanswer);
      return static_cast<float>( (d1 < d2) ? root_1 : root_2 );
    }//if( sqrtarg >= 0.0f )
  }//if( ncoefs < 4 && devpair.empty() )
  
  if( nchannel < 2 )
    throw runtime_error( "find_polynomial_channel: accuracy must be greater than zero" );
  
  if( accuracy <= 0.0 )
    throw runtime_error( "find_polynomial_channel: accuracy must be greater than zero" );
  
  const size_t max_iterations = 1000;
  size_t iteration = 0;
  
  float lowbin = 0.0;
  float highbin = nchannel;
  float testenergy = polynomial_energy( highbin, coeffs, devpair );
  while( (testenergy < energy) && (iteration < max_iterations) )
  {
    highbin += std::max(0.125*nchannel,2.0);
    testenergy = polynomial_energy( highbin, coeffs, devpair );
    ++iteration;
  }//while( testenergy < energy )
  
  if( iteration >= max_iterations )
    throw runtime_error( "find_polynomial_channel: failed to find channel high-enough" );
  
  iteration = 0;
  testenergy = polynomial_energy( lowbin, coeffs, devpair );
  while( testenergy > energy && (iteration < max_iterations)  )
  {
    lowbin -= std::max(0.125*nchannel,2.0);
    testenergy = polynomial_energy( lowbin, coeffs, devpair );
    ++iteration;
  }//while( testenergy < energy )
  
  if( iteration >= max_iterations )
    throw runtime_error( "find_polynomial_channel: failed to find channel low-enough" );
  
  float bin = lowbin + ((highbin-lowbin)/2.0f);
  testenergy = polynomial_energy( bin, coeffs, devpair );
  float dx = static_cast<float>( fabs(testenergy-energy) );
  
  iteration = 0;
  while( (dx > accuracy) && (iteration < max_iterations) )
  {
    if( highbin == lowbin )
    {
#if( PERFORM_DEVELOPER_CHECKS )
      log_developer_error( __func__, "Possible error in find_polynomial_channel... check out" );
#endif
      throw runtime_error( "find_polynomial_channel(...): error finding bin coorespongin to"
                          " desired energy (this shouldnt happen)" );
    }
    
    if( testenergy == energy )
      return bin;
    if( testenergy > energy )
      highbin = bin;
    else
      lowbin = bin;
    
    bin = lowbin + ((highbin-lowbin)/2.0f);
    testenergy = polynomial_energy( bin, coeffs, devpair );
    dx = static_cast<float>( fabs(testenergy-energy) );
    ++iteration;
  }//while( dx > accuracy )
  
  if( iteration >= max_iterations )
    throw runtime_error( "find_polynomial_channel: failed to converge" );
  
  return bin;
}//find_polynomial_channel
  
  
void rebin_by_lower_edge( const std::vector<float> &original_energies,
                           const std::vector<float> &original_counts,
                           const std::vector<float> &new_energies,
                           std::vector<float> &resulting_counts )
{
  const size_t old_nbin = min( original_energies.size(), original_counts.size());
  const size_t new_nbin = new_energies.size();
  
  if( old_nbin < 4 )
    throw runtime_error( "rebin_by_lower_edge: input must have more than 3 bins" );
  
  if( original_energies.size() < original_counts.size() )
    throw runtime_error( "rebin_by_lower_edge: input energies and gamma counts"
                        " have mismatched number of channels" );
  
  if( new_nbin < 4 )
    throw runtime_error( "rebin_by_lower_edge: output have more than 3 bins" );
  
  resulting_counts.resize( new_nbin, 0.0f );
  
  size_t newbinnum = 0;
  while( new_energies[newbinnum] < original_energies[0] && newbinnum < (new_nbin-1) )
    resulting_counts[newbinnum++] = 0.0f;
  
  //new_energies[newbinnum] is now >= original_energies[0]
  if( newbinnum && (new_energies[newbinnum] > original_energies[0]) )
  {
    if(new_energies[newbinnum] >= original_energies[1])
    {
      resulting_counts[newbinnum-1] = original_counts[0];
      resulting_counts[newbinnum-1] += static_cast<float>( original_counts[1]
                                                          * (double(new_energies[newbinnum]) - double(original_energies[1]))
                                                          / (double(original_energies[2]) - double(original_energies[1])) );
    }else
    {
      resulting_counts[newbinnum-1] = static_cast<float>( original_counts[0]
                                                         * (double(new_energies[newbinnum]) - double(original_energies[0]))
                                                         / (double(original_energies[1]) - double(original_energies[0])) );
    }
  }
  
  size_t oldbinlow = 0, oldbinhigh = 0;
  for( ; newbinnum < new_nbin; ++newbinnum )
  {
    const double newbin_lower = new_energies[newbinnum];
    const double newbin_upper = ( ((newbinnum+1)<new_nbin)
                                 ? new_energies[newbinnum+1]
                                 : (2.0*new_energies[new_nbin-1] - new_energies[new_nbin-2]));
    
    double old_lower_low, old_lower_up, old_upper_low, old_upper_up;
    
    for( ; oldbinlow < old_nbin; ++oldbinlow )
    {
      old_lower_low = original_energies[oldbinlow];
      if( (oldbinlow+1) < old_nbin )
        old_lower_up = original_energies[oldbinlow+1];
      else
        old_lower_up = 2.0*original_energies[oldbinlow]-original_energies[oldbinlow-1];
      
      if( newbin_lower >= old_lower_low && newbin_lower < old_lower_up )
        break;
    }
    
    double sum_lower_to_upper = 0.0;
    for( oldbinhigh = oldbinlow; oldbinhigh < old_nbin; ++oldbinhigh )
    {
      old_upper_low = original_energies[oldbinhigh];
      if( (oldbinhigh+1) < old_nbin )
        old_upper_up = original_energies[oldbinhigh+1];
      else
        old_upper_up = 2.0*original_energies[oldbinhigh]-original_energies[oldbinhigh-1];
      
      if( newbin_upper >= old_upper_low && newbin_upper < old_upper_up )
        break;
      sum_lower_to_upper += original_counts[oldbinhigh];
    }
    
    //new binning goes higher than old binning, lets take care of the last
    //  fraction of a bin, and zero everything else out
    if( oldbinhigh == old_nbin )
    {
      old_upper_low = original_energies[old_nbin-1];
      old_upper_up = 2.0*original_energies[old_nbin-1]-original_energies[old_nbin-2];
      
      //maybe more numerically accurate if we remove the next line, and uncomment
      //  the next two commented lines.  Didnt do because I didnt want to test
      resulting_counts[newbinnum] = static_cast<float>( sum_lower_to_upper );
      
      if( oldbinlow != old_nbin )
      {
        const double lower_old_width = old_lower_up - old_lower_low;
        const double lower_bin_delta_counts = original_counts[oldbinlow];
        const double lower_delta_energy = double(newbin_lower)-double(original_energies[oldbinlow]);
        const double lower_frac_energy = lower_delta_energy/lower_old_width;
        
        //        sum_lower_to_upper -= lower_bin_delta_counts*lower_frac_energy;
        resulting_counts[newbinnum] -= static_cast<float>(lower_bin_delta_counts*lower_frac_energy);
      }
      
      //      resulting_counts[newbinnum] = static_cast<float>( sum_lower_to_upper );
      
      newbinnum++;
      while( newbinnum < new_nbin )
        resulting_counts[newbinnum++] = 0.0f;
      break;
    }
    
    const double lower_old_width = old_lower_up - old_lower_low;
    const double lower_bin_delta_counts = original_counts[oldbinlow];
    const double lower_delta_energy = double(newbin_lower)-double(original_energies[oldbinlow]);
    const double lower_frac_energy = lower_delta_energy/lower_old_width;
    
    const double upper_old_width = old_upper_up - old_upper_low;
    const double upper_bin_delta_counts = original_counts[oldbinhigh];
    const double upper_delta_energy = double(newbin_upper)-double(original_energies[oldbinhigh]);
    const double upper_frac_energy = upper_delta_energy/upper_old_width;
    
    //interpolate sum height at newbin_lower
    resulting_counts[newbinnum] = static_cast<float>( sum_lower_to_upper + upper_bin_delta_counts*upper_frac_energy-lower_bin_delta_counts*lower_frac_energy );
  }//for( ; newbinnum < (new_nbin-1); ++newbinnum )
  
  
  //capture case where new energies starts higher than the original energies, so
  //  we should put the contents of the lower energy bins into the first bin
  //  of the new counts
  if( original_energies[0] < new_energies[0] )
  {
    size_t i = 0;
    while( (original_energies[i+1]<new_energies[0]) && (i<(old_nbin-1)) )
      resulting_counts[0] += original_counts[i++];
    
    //original_energies[i] is now >= new_energies[0]
    
    if( i < old_nbin )
      resulting_counts[0] += static_cast<float>( original_counts[i]*(double(new_energies[0])-double(original_energies[i]))/(double(original_energies[i+1])-original_energies[i]) );
  }//if( original_energies[0] < new_energies[0] )
  
  //Now capture the case where the old binning extends further than new binning
  const float upper_old_energy = 2.0f*original_energies[old_nbin-1] - original_energies[old_nbin-2];
  const float upper_new_energy = 2.0f*new_energies[new_nbin-1] - new_energies[new_nbin-2];
  if( upper_old_energy > upper_new_energy )
  {
    if( oldbinhigh < (old_nbin-1) )
      resulting_counts[new_nbin-1] += original_counts[oldbinhigh] * (original_energies[oldbinhigh]-upper_new_energy)/(original_energies[oldbinhigh+1]-original_energies[oldbinhigh]);
    else
      resulting_counts[new_nbin-1] += original_counts[oldbinhigh] * (original_energies[oldbinhigh]-upper_new_energy)/(original_energies[oldbinhigh]-original_energies[oldbinhigh-1]);
    
    for( ; oldbinhigh < old_nbin; ++oldbinhigh )
      resulting_counts[new_nbin-1] += original_counts[oldbinhigh];
  }//if( original_energies.back() > new_energies.back() )
  
  
  
#if( PERFORM_DEVELOPER_CHECKS )
  double oldsum = 0.0, newsum = 0.0;
  for( const float f : original_counts )
    oldsum += f;
  for( const float f : resulting_counts )
    newsum += f;
  
  if( (fabs(oldsum - newsum) > (old_nbin*0.1/16384.0)) && (fabs(oldsum - newsum) > 1.0E-7*oldsum) )
  {
    static std::mutex m;
    
    {
      std::lock_guard<std::mutex> loc(m);
      ofstream output( "rebin_by_lower_edge_error.txt", ios::app );
      output << "  std::vector<float> original_energies{" << std::setprecision(9);
      for( size_t i = 0; i < original_energies.size(); ++i )
        output << (i?", ":"") << original_energies[i];
      output << "};" << endl;
      
      output << "  std::vector<float> original_counts{" << std::setprecision(9);
      for( size_t i = 0; i < original_counts.size(); ++i )
        output << (i?", ":"") << original_counts[i];
      output << "};" << endl;
      
      output << "  std::vector<float> new_energies{" << std::setprecision(9);
      for( size_t i = 0; i < new_energies.size(); ++i )
        output << (i?", ":"") << new_energies[i];
      output << "};" << endl << endl;
      
      output << "  std::vector<float> new_counts{" << std::setprecision(9);
      for( size_t i = 0; i < resulting_counts.size(); ++i )
        output << (i?", ":"") << resulting_counts[i];
      output << "};" << endl << endl;
    }
    
    
    char buffer[1024];
    snprintf( buffer, sizeof(buffer),
             "rebin_by_lower_edge gives %1.8E vs pre rebin of %1.8E, an unacceptable error.\n",
             newsum, oldsum );
    log_developer_error( __func__, buffer );
  }
#endif
}//void rebin_by_lower_edge(...)

  
  //namespace details
  //{
  //}//namespace details
}//namespace SpecUtils
