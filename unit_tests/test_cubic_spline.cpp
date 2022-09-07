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


#define BOOST_TEST_MODULE testCubicSpline
#include <boost/test/unit_test.hpp>

//#define BOOST_TEST_DYN_LINK
// To use boost unit_test as header only (no link to boost unit test library):
//#include <boost/test/included/unit_test.hpp>


#include "SpecUtils/CubicSpline.h"
#include "SpecUtils/EnergyCalibration.h"


using namespace std;
using namespace SpecUtils;

BOOST_AUTO_TEST_CASE(cubicSplineSimple) {
   
  std::vector<std::pair<float,float>> data{
    {0.1f,0.1f},
    {0.4f,0.7f},
    {1.2f,0.6f},
    {1.8f,1.1f},
    {2.0f,0.9f}
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
  const vector<pair<float,float>> devpairs = { {60.0f,-23.0f }, {81.0f,-20.6f}, {239.0f,-32.0f },
    {356.0f,-37.0f }, {661.0f,-37.0f }, {898.0f,-23.5f}, {1332.0f,-12.0f }, {1460.0f,0.0f }, {1836.0f,35.0f }, {2223.0f,70.0f },
    {2614.0f,201.0f }, {3000.0f,320.0f }
  };
  
  vector<float> binning( 1024 );
  for( size_t i = 0; i < binning.size(); ++i ){
    binning[i] = i * ((3000.0f - 0.0f) / binning.size());
  }

  auto newbinning = SpecUtils::apply_deviation_pair( binning, devpairs );

  BOOST_REQUIRE_MESSAGE( !!newbinning, "Failed to get binning with deviation pairs" );
  BOOST_REQUIRE_MESSAGE( newbinning->size() == binning.size(), "Binning with deviation pairs returned different number of bins" );

  //std::vector<CubicSplineNode> nodes;
  //BOOST_CHECK_NO_THROW( nodes = create_cubic_spline_for_dev_pairs( devpairs ) );

  for( size_t i = 0; i < binning.size(); ++i ){
    //const float from_eval = binning[i] + eval_cubic_spline( binning[i], nodes );
    const double from_eval = binning[i] + deviation_pair_correction( binning[i], devpairs );
    const double maxanswer = std::max( fabs((*newbinning)[i]), static_cast<float>(fabs(from_eval)) );
    const double diff = fabs( (*newbinning)[i] - from_eval );
    
    BOOST_CHECK_MESSAGE( diff < (maxanswer * 1.0E-6), \
                         "apply_deviation_pair returned different answer than eval_cubic_spline: " \
                        << from_eval << " vs " <<  (*newbinning)[i] << " with diff " \
                        << fabs( (*newbinning)[i] - from_eval) );
  }
}//BOOST_AUTO_TEST_CASE(devPairApply)


BOOST_AUTO_TEST_CASE(cubicSplineNonZeroAnchored) {
  //Tests deviation pairs that arent anchored at 0 keV.  In this example K40 is used as the anchor.

  const vector<float> no_dev_pairs_peak_means{ 87.47f, 88.97f, 331.64f, 344.53f, 352.78f,
    506.90f, 627.52f, 643.11f, 650.94f, 96.05f, 98.07f, 134.14f, 148.28f, 174.90f,
    237.42f, 304.74f, 312.32f, 335.91f, 364.51f, 447.37f, 501.76f, 622.23f, 824.28f,
    934.16f, 2413.31f, 1191.90f, 1344.43f, 921.53f, 1801.03f, 82.51f, 100.85f, 319.54f,
    401.62f, 756.75f 
  };
    
  const vector<float> gamma_energies{ 65.12f, 66.83f, 295.96f, 308.46f,
    316.51f, 468.07f, 588.58f, 604.41f, 612.47f, 74.82f, 77.11f, 115.18f, 129.06f, 
    153.98f, 209.25f, 270.25f, 277.36f, 300.09f, 328.00f, 409.46f, 463.00f, 583.19f, 
    794.95f, 911.20f, 2614.53f, 1173.23f, 1332.49f, 898.04f, 1836.06f, 59.54f, 80.19f, 
    284.31f, 364.49f, 722.91f
  };
    

  const vector<pair<float,float>> devpairs = { {60.0f,-23.0f}, {81.0f,-20.6f}, {239.0f,-32.0f },
    {356.0f,-37.0f }, {661.0f,-37.0f }, {898.0f,-23.5f}, {1332.0f,-12.0f }, {1460.0f,0.0f }, {1836.0f,35.0f }, {2223.0f,70.0f },
    {2614.0f,201.0f }, {3000.0f,320.0f }
  };
    
  assert( gamma_energies.size() == no_dev_pairs_peak_means.size() );
    
  const size_t ngammas = gamma_energies.size();
    
  std::vector<CubicSplineNode> nodes;
  BOOST_CHECK_NO_THROW( nodes = create_cubic_spline_for_dev_pairs( devpairs ) );
  //const std::vector<CubicSplineNode> inv_nodes = create_inverse_dev_pairs_cubic_spline( devpairs );
    
  for( size_t i = 0; i < ngammas; ++i )
  {
     const double corrected = no_dev_pairs_peak_means[i] + eval_cubic_spline( no_dev_pairs_peak_means[i], nodes );
     //const float corr_to_normal = corrected - eval_cubic_spline( corrected, inv_nodes );
     const double back_corrected = corrected - correction_due_to_dev_pairs( corrected, devpairs );
 
     BOOST_CHECK_MESSAGE( fabs(gamma_energies[i] - corrected) < 0.06, \
                         "Deviation pair CubicSpline interpolation failed: " \
                         << corrected << " vs expected " <<  gamma_energies[i] );

     BOOST_CHECK_MESSAGE( fabs(back_corrected - no_dev_pairs_peak_means[i]) < 0.001, \
                         "Failed to go from true to polynomial energy: " \
                         << back_corrected << " vs expected " <<  no_dev_pairs_peak_means[i] );
  }

}//BOOST_AUTO_TEST_CASE(cubicSplineNonZeroAnchored)



BOOST_AUTO_TEST_CASE(cubicSplineFromGadras) {
    const vector<float> gamma_energies{ 74.82f, 77.11f, 129.06f, 153.98f, 209.25f,
      238.63f, 240.99f, 270.25f, 300.09f, 328.00f, 338.32f, 340.96f, 409.46f, 463.00f,
      562.50f, 583.19f, 727.33f, 772.29f, 794.95f, 830.49f, 835.71f, 840.38f, 860.56f,
      911.20f, 964.77f, 968.97f, 1078.62f, 1110.61f, 1247.08f, 1460.75f, 1495.91f,
      1501.57f, 1512.70f, 1580.53f, 1620.50f, 1630.63f, 2614.53f, 3000.0f, -10.0f
    };
    
    const vector<float> no_dev_pairs_peak_means{ 69.61f, 71.93f, 122.39f, 144.71f,
      193.13f, 219.20f, 221.36f, 247.72f, 275.08f, 301.02f, 310.72f, 313.15f, 378.92f,
      431.67f, 532.93f, 554.55f, 708.14f, 757.06f, 781.74f, 820.66f, 826.33f, 831.46f,
      853.52f, 908.81f, 967.06f, 971.60f, 1089.04f, 1123.04f, 1265.02f, 1481.66f,
      1517.46f, 1523.05f, 1534.45f, 1601.87f, 1641.55f, 1651.61f, 2614.54f, 3000.0f, -10.0f
    };
    
    const vector<pair<float,float>> devpairs = {
      {0.0f,0.00f}, {50.0f,5.0f}, {100.0f,5.00f}, {200.0f,15.00f}, {1000.0f,-5.00f},
      {2614.0f,0.00f}, {3000.0f,0.00f} //The 3 MeV was in GADRAS
    };
    
    assert( gamma_energies.size() == no_dev_pairs_peak_means.size() );
    
    const size_t ngammas = gamma_energies.size();
    
    std::vector<CubicSplineNode> nodes;
    BOOST_CHECK_NO_THROW( nodes = create_cubic_spline_for_dev_pairs( devpairs ) );
    //const std::vector<CubicSplineNode> inv_nodes = create_inverse_dev_pairs_cubic_spline( devpairs );
    
    for( size_t i = 0; i < ngammas; ++i )
    {
      const double corrected = no_dev_pairs_peak_means[i] + eval_cubic_spline( no_dev_pairs_peak_means[i], nodes );
      //const float corr_to_normal = corrected - eval_cubic_spline( corrected, inv_nodes );
      const double back_corrected = corrected - correction_due_to_dev_pairs( corrected, devpairs );
 
     BOOST_CHECK_MESSAGE( fabs(gamma_energies[i] - corrected) < 0.5, \
                         "Deviation pair CubicSpline interpolation failed: " \
                         << corrected << " vs expected " <<  gamma_energies[i] );

     BOOST_CHECK_MESSAGE( fabs(back_corrected - no_dev_pairs_peak_means[i]) < 0.1, \
                         "Failed to go from true to polynomial energy: " \
                         << back_corrected << " vs expected " <<  no_dev_pairs_peak_means[i] );
  }

}







