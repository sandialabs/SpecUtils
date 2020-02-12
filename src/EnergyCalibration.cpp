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
#include <vector>
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

std::shared_ptr< const std::vector<float> > polynomial_binning( const vector<float> &coeffs,
                                                                 const size_t nbin,
                                                                 const std::vector<std::pair<float,float>> &dev_pairs )
{
  auto answer = make_shared<vector<float>>(nbin, 0.0f);
  const size_t ncoeffs = coeffs.size();
  
  for( size_t i = 0; i < nbin; i++ )
  {
    float val = 0.0f;
    for( size_t c = 0; c < ncoeffs; ++c )
      val += coeffs[c] * pow( static_cast<float>(i), static_cast<float>(c) );
    answer->operator[](i) = val;
    
    //ToDo: check for infs and NaNs, and if larger than last energy
  }//for( loop over bins, i )
  
  if( dev_pairs.empty() )
    return answer;
  
  return apply_deviation_pair( *answer, dev_pairs );
}//std::shared_ptr< const std::vector<float> > polynomial_binning( const vector<float> &coefficients, size_t nbin )
  
  
std::shared_ptr< const std::vector<float> > fullrangefraction_binning( const vector<float> &coeffs,
                                                                        const size_t nbin,
                                                                        const std::vector<std::pair<float,float>> &dev_pairs  )
{
  auto answer = make_shared<vector<float>>(nbin, 0.0f);
  const size_t ncoeffs = std::min( coeffs.size(), size_t(4) );
  const float low_e_coef = (coeffs.size() > 4) ? coeffs[4] : 0.0f;
  
  for( size_t i = 0; i < nbin; i++ )
  {
    const float x = static_cast<float>(i)/static_cast<float>(nbin);
    float &val = answer->operator[](i);
    for( size_t c = 0; c < ncoeffs; ++c )
      val += coeffs[c] * pow(x,static_cast<float>(c) );
    val += low_e_coef / (1.0f+60.0f*x);
    
    //ToDo: check for infs and NaNs, and if larger than last energy
    
  }//for( loop over bins, i )
  
  if( dev_pairs.empty() )
    return answer;
  return apply_deviation_pair( *answer, dev_pairs );
}//std::shared_ptr< const std::vector<float> > fullrangefraction_binning(...)
  
  
float fullrangefraction_energy( float bin_number,
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

  
std::vector<float> fullrangefraction_coef_to_polynomial(
                                                          const std::vector<float> &coeffs,
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
  
  std::unique_ptr< std::vector<float> > frfeqn;
  const std::vector<float> *eqn = &ineqn;
  
  if( type == EnergyCalType::Polynomial
     || type == EnergyCalType::UnspecifiedUsingDefaultPolynomial )
  {
    frfeqn.reset( new vector<float>() );
    *frfeqn = polynomial_coef_to_fullrangefraction( ineqn, nbin );
    eqn = frfeqn.get();
  }//if( type == EnergyCalType::Polynomial )
  
  switch( type )
  {
    case EnergyCalType::Polynomial:
    case EnergyCalType::FullRangeFraction:
    case EnergyCalType::UnspecifiedUsingDefaultPolynomial:
    {
      for( const float &val : (*eqn) )
      {
        if( IsInf(val) || IsNan(val) )
          return false;
      }
      
      //ToDo:
      const float nearend   = fullrangefraction_energy( float(nbin-2), *eqn, nbin, devpairs );
      const float end       = fullrangefraction_energy( float(nbin-1), *eqn, nbin, devpairs );
      const float begin     = fullrangefraction_energy( 0.0f,      *eqn, nbin, devpairs );
      const float nearbegin = fullrangefraction_energy( 1.0f,      *eqn, nbin, devpairs );
      
      const bool valid = !( (nearend >= end) || (begin >= nearbegin) );
      
#if( PERFORM_DEVELOPER_CHECKS )
      if( valid )
      {
        auto binning = fullrangefraction_binning( *eqn, nbin, devpairs );
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
            for( size_t j = 0; j < eqn->size(); ++j )
              msg << (j ? ", " : "") << (*eqn)[j];
            msg << "} and deviation pairs {";
            
            for( size_t j = 0; j < devpairs.size(); ++j )
              msg << (j ? ", {" : "{") << devpairs[j].first << ", " << devpairs[j].second << "}";
            msg << "}";
            
            log_developer_error( __func__, msg.str().c_str() );
          }
        }//for( size_t i = 1; i < binning->size(); ++i )
      }//if( valid )
#endif
      return valid;
    }//case EnergyCalType::FullRangeFraction:
      
    case EnergyCalType::LowerChannelEdge:
    {
      for( size_t i = 1; i < ineqn.size(); ++i )
        if( ineqn[i-1] >= ineqn[i] )
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
  size_t ncoefs = 0;
  for( size_t i = 0; i < coeffs.size(); ++i )
    if( fabs(coeffs[i]) > std::numeric_limits<float>::epsilon() )
      ncoefs = i+1;
  
  if( ncoefs < 2  )
    throw std::runtime_error( "find_fullrangefraction_channel(...): must pass"
                             " in at least two coefficients" );
  
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
      
      vector<double> roots;
      if( root_1 > 0.0 && root_1 < static_cast<double>(nbin) )
        roots.push_back( root_1 );
      if( root_2 > 0.0 && root_2 < static_cast<double>(nbin) )
        roots.push_back( root_2 );
      
      if( roots.size() == 1 )
        return static_cast<float>( nbin * roots[0] );
      
      if( roots.size() == 2 )
      {
        const double e1 = coeffs[0] + coeffs[1]*root_1 + coeffs[2]*root_1*root_1;
        const double e2 = coeffs[0] + coeffs[1]*root_2 + coeffs[2]*root_2*root_2;
        if( fabs(e1-e2) < static_cast<double>(accuracy) )
          return static_cast<float>( nbin * root_1 );
      }//if( roots.size() == 2 )
      
#if( PERFORM_DEVELOPER_CHECKS )
      stringstream msg;
      msg << "find_fullrangefraction_channel(): found " << roots.size()
      << " energy solutions, shouldnt have happened: "
      << root_1 << " and " << root_2 << " for " << energy << " keV"
      << " so root coorespond to "
      << coeffs[0] + coeffs[1]*root_1 + coeffs[2]*root_1*root_1
      << " and "
      << coeffs[0] + coeffs[1]*root_2 + coeffs[2]*root_2*root_2
      << ". I will attempt to recover, but please check results of operation.";
      //    passMessage( msg.str(), "", 3 );
      log_developer_error( __func__, msg.str().c_str() );
#endif
      cerr << __func__ << "\n\tWarning, couldnt algebraicly find bin number\n"
      << "\tcoeffs[0]=" << coeffs[0] << ", coeffs[1]=" << coeffs[1]
      << ", coeffs[2]=" << coeffs[2] << ", energy=" << energy << endl;
    }//if( sqrtarg >= 0.0f )
  }//if( ncoefs < 4 && devpair.empty() )
  
  
  float lowbin = 0.0;
  float highbin = static_cast<float>( nbin );
  float testenergy = fullrangefraction_energy( highbin, coeffs, nbin, devpair );
  while( testenergy < energy )
  {
    highbin *= 2.0f;
    testenergy = fullrangefraction_energy( highbin, coeffs, nbin, devpair );
  }//while( testenergy < energy )
  
  testenergy = fullrangefraction_energy( lowbin, coeffs, nbin, devpair );
  while( testenergy > energy )
  {
    lowbin -= nbin;
    testenergy = fullrangefraction_energy( lowbin, coeffs, nbin, devpair );
  }//while( testenergy < energy )
  
  
  float bin = lowbin + ((highbin-lowbin)/2.0f);
  testenergy = fullrangefraction_energy( bin, coeffs, nbin, devpair );
  float dx = static_cast<float>( fabs(testenergy-energy) );
  
  while( dx > accuracy )
  {
    if( highbin == lowbin )
    {
      cerr << "Possible error in find_fullrangefraction_channel... check out" << endl;
      throw runtime_error( "find_fullrangefraction_channel(...): error finding bin coorespongin to deired energy (this shouldnt happen)" );
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
  }//while( dx > accuracy )
  
  return bin;
}//float find_fullrangefraction_channel(...)
  

  
  
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
