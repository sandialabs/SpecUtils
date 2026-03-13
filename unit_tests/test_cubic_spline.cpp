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

#include <cmath>
#include <string>
#include <vector>
#include <iostream>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "SpecUtils/CubicSpline.h"
#include "SpecUtils/EnergyCalibration.h"


using namespace std;
using namespace SpecUtils;

TEST_CASE( "Cubic Spline Simple" ) {
   
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
  
  CHECK_MESSAGE( fabs( 0.915345 - val ) < 0.00001, \
                         "Sanity check failed: " \
                         << val << " vs expected " <<  0.915345 );  
}


/** Test apply_deviation_pair(...) gives same answer as eval_cubic_spline(...) */
TEST_CASE( "Apply Deviation Pair" ) {
  const vector<pair<float,float>> devpairs = { {60.0f,-23.0f }, {81.0f,-20.6f}, {239.0f,-32.0f },
    {356.0f,-37.0f }, {661.0f,-37.0f }, {898.0f,-23.5f}, {1332.0f,-12.0f }, {1460.0f,0.0f }, {1836.0f,35.0f }, {2223.0f,70.0f },
    {2614.0f,201.0f }, {3000.0f,320.0f }
  };
  
  vector<float> binning( 1024 );
  for( size_t i = 0; i < binning.size(); ++i ){
    binning[i] = i * ((3000.0f - 0.0f) / binning.size());
  }

  auto newbinning = SpecUtils::apply_deviation_pair( binning, devpairs );

  REQUIRE_MESSAGE( !!newbinning, "Failed to get binning with deviation pairs" );
  REQUIRE_MESSAGE( newbinning->size() == binning.size(), "Binning with deviation pairs returned different number of bins" );

  //std::vector<CubicSplineNode> nodes;
  //CHECK_NO_THROW( nodes = create_cubic_spline_for_dev_pairs( devpairs ) );

  for( size_t i = 0; i < binning.size(); ++i ){
    //const float from_eval = binning[i] + eval_cubic_spline( binning[i], nodes );
    const double from_eval = binning[i] + deviation_pair_correction( binning[i], devpairs );
    const double maxanswer = std::max( fabs((*newbinning)[i]), static_cast<float>(fabs(from_eval)) );
    const double diff = fabs( (*newbinning)[i] - from_eval );
    
    CHECK_MESSAGE( diff < (maxanswer * 1.0E-6), \
                         "apply_deviation_pair returned different answer than eval_cubic_spline: " \
                        << from_eval << " vs " <<  (*newbinning)[i] << " with diff " \
                        << fabs( (*newbinning)[i] - from_eval) );
  }
}//TEST_CASE(devPairApply)


TEST_CASE( "Cubic Spline Non-Zero Anchored" ) {
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
    
  REQUIRE_EQ( gamma_energies.size(), no_dev_pairs_peak_means.size() );
    
  const size_t ngammas = gamma_energies.size();
    
  std::vector<CubicSplineNode> nodes;
  CHECK_NOTHROW( nodes = create_cubic_spline_for_dev_pairs( devpairs ) );
  //const std::vector<CubicSplineNode> inv_nodes = create_inverse_dev_pairs_cubic_spline( devpairs );
    
  for( size_t i = 0; i < ngammas; ++i )
  {
     const double corrected = no_dev_pairs_peak_means[i] + eval_cubic_spline( no_dev_pairs_peak_means[i], nodes );
     //const float corr_to_normal = corrected - eval_cubic_spline( corrected, inv_nodes );
     const double back_corrected = corrected - correction_due_to_dev_pairs( corrected, devpairs );
 
     CHECK_MESSAGE( fabs(gamma_energies[i] - corrected) < 0.06, \
                         "Deviation pair CubicSpline interpolation failed: " \
                         << corrected << " vs expected " <<  gamma_energies[i] );

     CHECK_MESSAGE( fabs(back_corrected - no_dev_pairs_peak_means[i]) < 0.001, \
                         "Failed to go from true to polynomial energy: " \
                         << back_corrected << " vs expected " <<  no_dev_pairs_peak_means[i] );
  }

}//TEST_CASE(cubicSplineNonZeroAnchored)



TEST_CASE( "Cubic-Spline From Gadras" ) {
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
    
    REQUIRE_EQ( gamma_energies.size(), no_dev_pairs_peak_means.size() );
    
    const size_t ngammas = gamma_energies.size();
    
    std::vector<CubicSplineNode> nodes;
    CHECK_NOTHROW( nodes = create_cubic_spline_for_dev_pairs( devpairs ) );
    //const std::vector<CubicSplineNode> inv_nodes = create_inverse_dev_pairs_cubic_spline( devpairs );
    
    for( size_t i = 0; i < ngammas; ++i )
    {
      const double corrected = no_dev_pairs_peak_means[i] + eval_cubic_spline( no_dev_pairs_peak_means[i], nodes );
      //const float corr_to_normal = corrected - eval_cubic_spline( corrected, inv_nodes );
      const double back_corrected = corrected - correction_due_to_dev_pairs( corrected, devpairs );
 
     CHECK_MESSAGE( fabs(gamma_energies[i] - corrected) < 0.5, \
                         "Deviation pair CubicSpline interpolation failed: " \
                         << corrected << " vs expected " <<  gamma_energies[i] );

     CHECK_MESSAGE( fabs(back_corrected - no_dev_pairs_peak_means[i]) < 0.1, \
                         "Failed to go from true to polynomial energy: " \
                         << back_corrected << " vs expected " <<  no_dev_pairs_peak_means[i] );
  }

}


TEST_CASE( "correction_due_to_dev_pairs steep spline convergence" )
{
  // Regression test: correction_due_to_dev_pairs previously used a direct fixed-point iteration
  // a_{n+1} = S(E - a_n) that oscillated and failed to converge when the spline had steep
  // gradients (|S'| close to or greater than 1). The fix uses an averaged iteration
  // a_{n+1} = (a_n + S(E - a_n)) / 2, which converges much faster in these cases.

  // Deviation pairs that produce a steep, oscillatory spline with large offsets (up to ~55 keV)
  // These are representative of a challenging real-world energy calibration adjustment.
  const vector<pair<float,float>> steep_dev_pairs = {
    {29.14f, 1.86f}, {60.0f, 0.0f}, {236.78f, 2.22f},
    {330.0f, 1.74f}, {370.0f, 7.95f}, {431.0f, -6.51f},
    {440.0f, -3.93f}, {477.0f, 2.57f}, {507.0f, 1.16f},
    {565.0f, -0.06f}, {608.0f, -0.02f}, {652.0f, -1.05f},
    {709.0f, -12.09f}, {745.0f, 0.93f}, {807.0f, -13.0f},
    {853.0f, -6.45f}, {892.0f, 0.60f}, {950.0f, -9.42f},
    {988.0f, 2.04f}, {1029.0f, 9.92f}, {1076.0f, 11.26f},
    {1137.0f, -0.82f}, {1173.0f, 12.95f}, {1234.0f, 2.81f},
    {1283.0f, 0.28f}, {1331.0f, 0.79f}, {1381.0f, 0.07f},
    {1429.0f, 0.87f}, {1485.0f, -6.22f}, {1515.0f, 18.55f},
    {1555.0f, 27.48f}, {1610.0f, 20.53f}, {1640.0f, 41.66f},
    {1691.0f, 32.54f}, {1720.0f, 50.21f}, {1782.0f, 38.67f},
    {1815.0f, 54.65f}, {1879.0f, 40.06f}, {1914.0f, 53.63f},
    {1973.0f, 50.07f}, {2018.0f, 47.28f}, {2070.0f, 44.28f},
    {2124.0f, 39.65f}, {2178.0f, 34.58f}, {2232.0f, 29.29f},
    {2286.0f, 24.01f}, {2340.0f, 18.98f}, {2394.0f, 14.39f},
    {2462.0f, -4.40f}, {2515.0f, -7.50f}, {2567.0f, -9.59f},
    {2617.0f, -10.55f}, {2667.0f, -10.60f}, {2716.0f, -10.60f},
    {2751.0f, 4.24f}, {2815.0f, -10.60f}, {2850.0f, 4.24f},
    {2899.0f, 4.24f}, {2963.0f, -10.60f}, {2998.0f, 4.24f},
    {3055.0f, -3.18f}
  };

  const vector<CubicSplineNode> spline = create_cubic_spline_for_dev_pairs( steep_dev_pairs );

  SUBCASE( "convergence near 1640 keV" )
  {
    // This energy previously caused the direct fixed-point iteration to fail
    const double true_energy = 1640.4;
    const double correction = correction_due_to_dev_pairs( true_energy, steep_dev_pairs );
    const double check = eval_cubic_spline( true_energy - correction, spline );
    CHECK( fabs( correction - check ) < 0.001 );  // 1 eV tolerance
  }

  SUBCASE( "convergence near 1360 keV" )
  {
    const double true_energy = 1360.2;
    const double correction = correction_due_to_dev_pairs( true_energy, steep_dev_pairs );
    const double check = eval_cubic_spline( true_energy - correction, spline );
    CHECK( fabs( correction - check ) < 0.001 );
  }

  SUBCASE( "convergence across full steep range" )
  {
    // Sweep the entire range where offsets are large and rapidly changing
    for( double energy = 1300.0; energy <= 2000.0; energy += 10.0 )
    {
      const double correction = correction_due_to_dev_pairs( energy, steep_dev_pairs );
      const double check = eval_cubic_spline( energy - correction, spline );
      CHECK_MESSAGE( fabs( correction - check ) < 0.001,
        "Failed at energy=" << energy << " keV, diff=" << fabs( correction - check ) );
    }
  }
}



