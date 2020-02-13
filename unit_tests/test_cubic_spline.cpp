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
#include <iostream>

//#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE testCubicSpline
//#include <boost/test/unit_test.hpp>
#include <boost/test/included/unit_test.hpp>
#include "SpecUtils/CubicSpline.h"
#include "SpecUtils/EnergyCalibration.h"


using namespace std;
using namespace SpecUtils;

BOOST_AUTO_TEST_CASE(cubicSplineSimple) {
   
  std::vector<std::pair<float,float>> data{
    {0.1,0.1},
    {0.4,0.7},
    {1.2,0.6},
    {1.8,1.1},
    {2.0,0.9}
  };
  
  const std::vector<CubicSplineNode> nodes = create_cubic_spline( data,
                                                           DerivativeType::Second, 0.0,
                                                           DerivativeType::Second, 0.0 );
  
  const float x = 1.5f;
  const float val = eval_cubic_spline(x,nodes);
  
  BOOST_CHECK_MESSAGE( fabs( 0.915345 - val ) < 0.00001, \
                         "Sanity check failed: " \
                         << val << " vs expected " <<  0.915345 );  
}


/** Test apply_deviation_pair(...) gives same answer as eval_cubic_spline(...) */
BOOST_AUTO_TEST_CASE(devPairApply) {
  const vector<pair<float,float>> devpairs = { {60,-23}, {81,-20.6}, {239,-32}, 
    {356,-37}, {661,-37}, {898,-23.5}, {1332,-12}, {1460,0}, {1836,35}, {2223,70}, 
    {2614,201}, {3000,320}
  };
  
  vector<float> binning( 1024 );
  for( size_t i = 0; i < binning.size(); ++i ){
    binning[i] = i * ((3000.0f - 0.0f) / binning.size());
  }

  auto newbinning =  SpecUtils::apply_deviation_pair( binning, devpairs );

  BOOST_REQUIRE_MESSAGE( !!newbinning, "Failed to get binning with deviation pairs" );
  BOOST_REQUIRE_MESSAGE( newbinning->size() == binning.size(), "Binning with deviation pairs returned different number of bins" );

  //std::vector<CubicSplineNode> nodes;
  //BOOST_CHECK_NO_THROW( nodes = create_cubic_spline_for_dev_pairs( devpairs ) );

  for( size_t i = 0; i < binning.size(); ++i ){
    //const float from_eval = binning[i] + eval_cubic_spline( binning[i], nodes );
    const float from_eval = binning[i] + deviation_pair_correction( binning[i], devpairs );

    BOOST_CHECK_MESSAGE( fabs( (*newbinning)[i] - from_eval) < 0.00001, \
                         "apply_deviation_pair returned different answer than eval_cubic_spline: " \
                         << from_eval << " vs " <<  (*newbinning)[i] );
  }
}//BOOST_AUTO_TEST_CASE(devPairApply)


BOOST_AUTO_TEST_CASE(cubicSplineNonZeroAnchored) {
  //Tests deviation pairs that arent anchored at 0 keV.  In this example K40 is used as the anchor.

  const vector<float> no_dev_pairs_peak_means{ 87.47, 88.97, 331.64, 344.53, 352.78,
    506.90, 627.52, 643.11, 650.94, 96.05, 98.07, 134.14, 148.28, 174.90,
    237.42, 304.74, 312.32, 335.91, 364.51, 447.37, 501.76, 622.23, 824.28,
    934.16, 2413.31, 1191.90, 1344.43, 921.53, 1801.03, 82.51, 100.85, 319.54,
    401.62, 756.75 
  };
    
  const vector<float> gamma_energies{ 65.12, 66.83, 295.96, 308.46,
    316.51, 468.07, 588.58, 604.41, 612.47, 74.82, 77.11, 115.18, 129.06, 
    153.98, 209.25, 270.25, 277.36, 300.09, 328.00, 409.46, 463.00, 583.19, 
    794.95, 911.20, 2614.53, 1173.23, 1332.49, 898.04, 1836.06, 59.54, 80.19, 
    284.31, 364.49, 722.91
  };
    

  const vector<pair<float,float>> devpairs = { {60,-23}, {81,-20.6}, {239,-32}, 
    {356,-37}, {661,-37}, {898,-23.5}, {1332,-12}, {1460,0}, {1836,35}, {2223,70}, 
    {2614,201}, {3000,320}
  };
    
  assert( gamma_energies.size() == no_dev_pairs_peak_means.size() );
    
  const size_t ngammas = gamma_energies.size();
    
  std::vector<CubicSplineNode> nodes;
  BOOST_CHECK_NO_THROW( nodes = create_cubic_spline_for_dev_pairs( devpairs ) );
  //const std::vector<CubicSplineNode> inv_nodes = create_inverse_dev_pairs_cubic_spline( devpairs );
    
  for( size_t i = 0; i < ngammas; ++i )
  {
     const float corrected = no_dev_pairs_peak_means[i] + eval_cubic_spline( no_dev_pairs_peak_means[i], nodes );
     //const float corr_to_normal = corrected - eval_cubic_spline( corrected, inv_nodes );
     const float back_corrected = corrected - correction_due_to_dev_pairs( corrected, devpairs );
 
     BOOST_CHECK_MESSAGE( fabs(gamma_energies[i] - corrected) < 0.06, \
                         "Deviation pair CubicSpline interpolation failed: " \
                         << corrected << " vs expected " <<  gamma_energies[i] );

     BOOST_CHECK_MESSAGE( fabs(back_corrected - no_dev_pairs_peak_means[i]) < 0.01, \
                         "Failed to go from true to polynomial energy: " \
                         << back_corrected << " vs expected " <<  no_dev_pairs_peak_means[i] );
  }

}//BOOST_AUTO_TEST_CASE(cubicSplineNonZeroAnchored)



BOOST_AUTO_TEST_CASE(cubicSplineFromGadras) {
    const vector<float> gamma_energies{ 74.82, 77.11, 129.06, 153.98, 209.25,
      238.63, 240.99, 270.25, 300.09, 328.00, 338.32, 340.96, 409.46, 463.00,
      562.50, 583.19, 727.33, 772.29, 794.95, 830.49, 835.71, 840.38, 860.56,
      911.20, 964.77, 968.97, 1078.62, 1110.61, 1247.08, 1460.75, 1495.91,
      1501.57, 1512.70, 1580.53, 1620.50, 1630.63, 2614.53, 3000.0, -10.0
    };
    
    const vector<float> no_dev_pairs_peak_means{ 69.61, 71.93, 122.39, 144.71,
      193.13, 219.20, 221.36, 247.72, 275.08, 301.02, 310.72, 313.15, 378.92,
      431.67, 532.93, 554.55, 708.14, 757.06, 781.74, 820.66, 826.33, 831.46,
      853.52, 908.81, 967.06, 971.60, 1089.04, 1123.04, 1265.02, 1481.66,
      1517.46, 1523.05, 1534.45, 1601.87, 1641.55, 1651.61, 2614.54, 3000.0, -10.0
    };
    
    const vector<pair<float,float>> devpairs = {
      {0,0.00}, {50,5}, {100,5.00}, {200,15.00}, {1000,-5.00}, {2614,0.00}, {3000.0,0.00} //The 3 MeV was in GADRAS
    };
    
    assert( gamma_energies.size() == no_dev_pairs_peak_means.size() );
    
    const size_t ngammas = gamma_energies.size();
    
    std::vector<CubicSplineNode> nodes;
    BOOST_CHECK_NO_THROW( nodes = create_cubic_spline_for_dev_pairs( devpairs ) );
    //const std::vector<CubicSplineNode> inv_nodes = create_inverse_dev_pairs_cubic_spline( devpairs );
    
    for( size_t i = 0; i < ngammas; ++i )
    {
      const float corrected = no_dev_pairs_peak_means[i] + eval_cubic_spline( no_dev_pairs_peak_means[i], nodes );
      //const float corr_to_normal = corrected - eval_cubic_spline( corrected, inv_nodes );
      const float back_corrected = corrected - correction_due_to_dev_pairs( corrected, devpairs );
 
     BOOST_CHECK_MESSAGE( fabs(gamma_energies[i] - corrected) < 0.5, \
                         "Deviation pair CubicSpline interpolation failed: " \
                         << corrected << " vs expected " <<  gamma_energies[i] );

     BOOST_CHECK_MESSAGE( fabs(back_corrected - no_dev_pairs_peak_means[i]) < 0.1, \
                         "Failed to go from true to polynomial energy: " \
                         << back_corrected << " vs expected " <<  no_dev_pairs_peak_means[i] );
  }

}







