#ifndef CubicSpline_h
#define CubicSpline_h
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

#include "SpecUtils_config.h"

#include <vector>
#include <utility>

/** This is a basic cubic spline implementation that allows specifying boundary
 conditions.
 */
namespace SpecUtils
{
  /** The derivative type used to specify boundary conditions for the cubic spline
   */
  enum class DerivativeType
  {
    First = 1,
    Second = 2
  };//enum class DerivativeType

  
  /** Struct used to hold the parameters to calcuate values of a cubic spline
   within an interval.
   
   f(x) = a*(x-x_i)^3 + b*(x-x_i)^2 + c*(x-x_i) + y_i
   
   Where x_i and y_i are #CubicSplineNode::x and #CubicSplineNode::y respectively.
   */
  struct CubicSplineNode
  {
    /** The starting x-value of the interval. */
    double x;
    
    /** The input data value of the left hand side (e.g., that cooresponds to
     #CubicSplineNode::x).
     */
    double y;
    
    /** The polynomial coefficients of the spline for the interval. */
    double a, b, c;
  };//struct CubicSplineNode

  
  /** Given input of x,y points (as .first,.second elements respectively),
   creates a the cubic spline nodes for the data.
   
   No two input elements can have the same x elements, and the input data must
   be sorted
   
   Will throw exception on error.
   */
  std::vector<CubicSplineNode>
  create_cubic_spline( const std::vector<std::pair<float,float>> &data,
                       const DerivativeType left_bc_type,
                       const double left_bc_value,
                       const DerivativeType right_bc_type,
                       const double right_bc_value );
  
  
  /** Evaluates the cubic spline at the specified x-value.
   If 'x' is less than the first CubicSplineNode.x, or larger than the last
   CubicSplineNode.x, then returns the first or last CubicSplineNode.y value
   respecitvely (this is non-standard behaviour for cubic splines, but how
   non-linear deviation pairs are defined).
   */
  double eval_cubic_spline( const double x,
                           const std::vector<CubicSplineNode> &nodes );
  
  
  /**  Creates the Cubic spline coefficients.
   Does some filtering of intput to make sure they are sorted and there are
   no duplicate x values (if there are dublicates, the one with the smallest
   y-value is used).
   */
  std::vector<CubicSplineNode>
  create_cubic_spline_for_dev_pairs( const std::vector<std::pair<float,float>> &dps );
  
  
  /** Gives a spline that transforms back from the correct energy, to the
   calibration before deviation pairs are defined.
   
   Currently generally gives a result accurate to within a few tenths of keV
   - I'm not to sure if this method of correction is just fundamentally
   incorrect, or just an issue with boundary conditions or something... I need
   to think about this more.
   
   See #correction_due_to_dev_pairs for a functioned garuneteed to get you within
   0.01 keV of the correct answer.
   */
  std::vector<CubicSplineNode>
  create_inverse_dev_pairs_cubic_spline( const std::vector<std::pair<float,float>> &dps );
  
}//namespace SpecUtils

#endif //CubicSpline_h
