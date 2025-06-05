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
#include <array>
#include <vector>
#include <cassert>
#include <stdexcept>
#include <algorithm>
#include <sstream>

#include "SpecUtils/CubicSpline.h"


using namespace std;

namespace
{
/** Tells you if the passed in deviation pairs can be directly made into a spline, or if they need
 some cleanup first.  Note that this function doesnt tell you if the deviation pairs are valid, just
 if there are duplicates, or they arent sorted, or that there isnt at least two pairs.
 */
bool dev_pairs_need_cleanup( const vector<pair<float,float>> &orig )
{
  bool inputOk = (orig.size() >= 2);
  for( size_t i = 1; inputOk && (i < orig.size()); ++i )
    inputOk = (orig[i-1].first < orig[i].first);
   
  return !inputOk;
}

/** Prepares deviation pairs for creating a spine by sorting them, removing duplicates, and adding
 in a base {0,0} if necassary.
 */
vector<pair<float,float>> cleanup_dev_pairs( const vector<pair<float,float>> &orig )
{
  vector<pair<float,float>> altered = orig;
  
  std::sort( begin(altered), end(altered) );
    
  const double epsilon = 0.1;
  for( size_t i = 1; i < altered.size(); ++i )
  {
    if( fabs(altered[i].first - altered[i-1].first) < epsilon )
      altered.erase( begin(altered) + i-- );
  }
    
  if( (altered.size() == 1) && (fabs(altered[0].first) < epsilon) )
  {
    altered.clear();
  }else if( (altered.size() == 1) && (altered[0].first > 0.0f) )
  {
    altered.insert( begin(altered), {0.0f,0.0f} );
  }
    
  return altered;
}//cleanup_dev_pairs(...)
}//namespace


namespace SpecUtils
{

std::vector<CubicSplineNode>
create_cubic_spline( const std::vector<std::pair<float,float>> &data,
                     const DerivativeType left_bc_type,
                     const double left_bc_value,
                     const DerivativeType right_bc_type,
                     const double right_bc_value )
{
  const size_t ndata = data.size();
  
  if( ndata < 2 )
    return std::vector<CubicSplineNode>{};
  
  assert( right_bc_type == DerivativeType::Second || right_bc_type == DerivativeType::First );
  assert( left_bc_type == DerivativeType::Second || left_bc_type == DerivativeType::First );
  
  // TODO: sort input
  for( size_t i = 0; i < ndata - 1; ++i ) {
    if( data[i].first >= data[i+1].first )
    {
      std::ostringstream strm( "create_cubic_spline: input data not sorted.");
      strm << "i: " << i << "; data[i].first: " << data[i].first << "; data[i+1].first: " << data[i+1].first; 
      throw std::runtime_error( strm.str() );
    }

  }
  
  // Create tri-diagonal matrix that we will solve for.
  //  The fourth element is 1/orig_diag, which is just saved for later.
  //
  // (Alternatively we could just use the matrix operations in boost to save
  //  some of this work, e.g.,
  //  boost::numeric::ublas::banded_matrix<double> A(ndata, ndata, 1, 1);
  //  but I'm slowly trying to remove boost from SpecUtils, so )
  
  std::vector< std::array<double,4> > A_matrix( ndata, {0.0,0.0,0.0,0.0} );
  
  std::vector<double>  rhs( ndata );
  for( size_t i = 1; i < (ndata-1); ++i )
  {
    A_matrix[i][0] = (static_cast<double>(data[i].first) - data[i-1].first)/3.0;
    A_matrix[i][1] = (static_cast<double>(data[i+1].first) - data[i-1].first)/1.5;
    A_matrix[i][2] = (static_cast<double>(data[i+1].first) - data[i].first)/3.0;
    rhs[i] = ((data[i+1].second - data[i].second) / (data[i+1].first - data[i].first))
             - ((static_cast<double>(data[i].second) - data[i-1].second) / (static_cast<double>(data[i].first) - data[i-1].first));
  }
  
  //h = x - x'
  //f = ((a*h + b)*h + c)*h + y
  switch( left_bc_type )
  {
    case DerivativeType::Second:
      // 2*b[0] = f''
      A_matrix[0][1] = 2.0;
      A_matrix[0][2] = 0.0;
      rhs[0] = left_bc_value;
      break;
      
    case DerivativeType::First:
      // c[0] = f'
      // (2b[0]+b[1])(x[1]-x[0]) = 3 ((y[1]-y[0])/(x[1]-x[0]) - f')
      A_matrix[0][1] = 2.0*(data[1].first - data[0].first);
      A_matrix[0][2] = 1.0*(data[1].first - data[0].first);
      rhs[0] = 3.0*(((data[1].second - data[0].second) / (data[1].first - data[0].first)) - left_bc_value);
      break;
  }//switch( left_bc_type )
  
  switch( right_bc_type )
  {
    case DerivativeType::Second:
      // 2*b[n-1] = f''
      A_matrix[ndata-1][1] = 2.0;
      A_matrix[ndata-1][0] = 0.0;
      rhs[ndata-1] = right_bc_value;
      break;
      
    case DerivativeType::First:
      // c[n-1] = f'
      // (b[n-2]+2b[n-1])(x[n-1]-x[n-2]) = 3(f' - (y[n-1]-y[n-2])/(x[n-1]-x[n-2]))
      A_matrix[ndata-1][1] = 2.0*(data[ndata-1].first - data[ndata-2].first);
      A_matrix[ndata-1][0] = 1.0*(data[ndata-1].first - data[ndata-2].first);
      rhs[ndata-1] = 3.0*(right_bc_value-(data[ndata-1].second - data[ndata-2].second) / (data[ndata-1].first - data[ndata-2].first));
      break;
  }//switch( right_bc_type )
  
  
  //LR Decompose the matrix
  // Normalize column i so that diagonal elements are 1.0
  //  but save 1/diagonal_el into A_matrix[i][3]
  for( size_t i = 0; i < A_matrix.size(); ++i)
  {
    assert( A_matrix[i][1] != 0.0 );
    A_matrix[i][3] = 1.0 / A_matrix[i][1];
    A_matrix[i][0] *= A_matrix[i][3];
    A_matrix[i][2] *= A_matrix[i][3];
    A_matrix[i][1] = 1.0;
  }
  
  // Gauss LR-Decomposition
  for( size_t k = 0; k < (A_matrix.size()-1); ++k )
  {
    assert( A_matrix[k][1] != 0.0 );
    const double x = -A_matrix[k+1][0] / A_matrix[k][1];
    A_matrix[k+1][0] = -x;
    A_matrix[k+1][1] = A_matrix[k+1][1] + x*A_matrix[k][2];
  }
  
  //solve Ly=b
  std::vector<double> y( A_matrix.size() );
  y[0] = (rhs[0] * A_matrix[0][3]);
  for( size_t i = 1; i < A_matrix.size(); ++i)
    y[i] = (rhs[i] * A_matrix[i][3]) - A_matrix[i][0]*y[i-1];
  
  //solve Rx=y
  std::vector<double> b( A_matrix.size() );
  b.back() = y.back() / A_matrix.back()[1];
  for( size_t i = A_matrix.size()-1; i != 0; --i )
    b[i-1] = (y[i-1] - A_matrix[i-1][2]*b[i]) / A_matrix[i-1][1];
  
  
  // calculate parameters a_i and c_i now that we have b_i
  std::vector<CubicSplineNode> nodes( ndata );
  
  for( size_t i = 0; i < ndata-1; ++i )
  {
    nodes[i].x = data[i].first;
    nodes[i].y = data[i].second;
    
    nodes[i].a = (b[i+1] - b[i])/(data[i+1].first - data[i].first) / 3.0;
    nodes[i].b = b[i];
    nodes[i].c = ((data[i+1].second - data[i].second)/(data[i+1].first - data[i].first))
                 - ((2.0*b[i]+b[i+1])*(data[i+1].first - data[i].first)/3.0);
  }
  
  //The last node would only be used for extrapolation, however since we arent
  //  using this we'll just set {x,y} so we know where the interval ends
  nodes[ndata-1].x = data.back().first;
  nodes[ndata-1].y = data.back().second;
  nodes[ndata-1].a = 0.0;
  nodes[ndata-1].b = 0.0;
  nodes[ndata-1].c = 0.0;
  
  return nodes;
}//create_cubic_spline()

  
  
double eval_cubic_spline( const double x, const std::vector<CubicSplineNode> &nodes )
{
  if( nodes.empty() )
    return 0.0;
  
  const auto it = std::upper_bound( std::begin(nodes), std::end(nodes), x,
                                   []( const double energy, const CubicSplineNode &node ) -> bool {
                                     return energy < node.x;
                                   });
  
  if( it == std::begin(nodes) )
    return nodes.front().y;
  
  //We should only do this next thing in the context of deviation pairs (or zero
  //  first and second upper derivatives)
  if( it == std::end(nodes) )
    return nodes.back().y;
  
  const auto node = it - 1;
  const double h = x - node->x;
  return ((node->a*h + node->b)*h + node->c)*h + node->y;
}//eval_cubic_spline()

  
vector<CubicSplineNode> create_cubic_spline_for_dev_pairs( const vector<pair<float,float>> &dps )
{
  if( dps.empty() || ((dps.size() == 1) && (dps[0].first < 0.1)) )
    return vector<CubicSplineNode>{};
  
  vector<pair<float,float>> offsets;
  if( dev_pairs_need_cleanup(dps) )
    offsets = cleanup_dev_pairs( dps );  //probably unlikely, in general
  else
    offsets = dps;
  
  if( offsets.empty() || ((offsets.size() == 1) && (offsets[0].first < 0.1)) )
    return vector<CubicSplineNode>{};

  // \TODO: This next step makes it so the deviation pairs could appear to be unsorted (in the
  //        unusual case of the offset differences being larger than differences in energies),
  //        which will cause #create_cubic_spline to throw an exception - should think through how
  //        to handle this a little better
  for( size_t i = 0; i < offsets.size(); ++i )
    offsets[i].first -= offsets[i].second;
  
  return create_cubic_spline( offsets, DerivativeType::Second, 0.0, DerivativeType::First, 0.0 );
}//create_cubic_spline_for_dev_pairs(...)

  
vector<CubicSplineNode> create_inverse_dev_pairs_cubic_spline( const vector<pair<float,float>> &dps )
{
  if( dps.empty() || ((dps.size() == 1) && (dps[0].first < 0.1)) )
    return vector<CubicSplineNode>{};
  
  if( dev_pairs_need_cleanup(dps) )
  {
    // It is probably unlickely we will end up here
    const auto offsets = cleanup_dev_pairs( dps );
    if( offsets.empty() || ((offsets.size() == 1) && (offsets[0].first < 0.1)) )
      return vector<CubicSplineNode>{};
    
    return create_cubic_spline( offsets, DerivativeType::Second, 0.0, DerivativeType::First, 0.0 );
  }//if( the passed in deviation pairs needed cleaning up )
  
  return create_cubic_spline( dps, DerivativeType::Second, 0.0, DerivativeType::First, 0.0 );
}//create_inverse_dev_pairs_cubic_spline(...)

}//namespace SpecUtils
