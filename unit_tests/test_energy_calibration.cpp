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

#include <random>
#include <string>
#include <vector>
#include <limits>
#include <sstream>
#include <ostream>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/EnergyCalibration.h"

#ifdef _WIN32
#undef min
#undef max
#endif

using namespace std;
using namespace SpecUtils;

string print_vec( const vector<float> &info )
{
  stringstream out;
  out << "{";
  for( size_t i = 0; i < info.size(); ++i )
    out << (i?",":"") << info[i];
  out << "}";
  return out.str();
}


bool is_similar( const vector<float> &lhs, const vector<float> &rhs )
{
  const size_t minsize = std::min( lhs.size(), rhs.size() );
  for( size_t i = 0; i < minsize; ++i )
  {
    const float larger = std::max(fabs(lhs[i]),fabs(rhs[i]));
    if( fabs(lhs[i]-rhs[i]) > 1.0E-5 * larger )
      return false;
  }
  
  for( size_t i = minsize; i < lhs.size(); ++i )
    if( fabs(lhs[i]) > numeric_limits<float>::epsilon() )
      return false;
  for( size_t i = minsize; i < rhs.size(); ++i )
    if( fabs(rhs[i]) > numeric_limits<float>::epsilon() )
      return false;
  
  return true;
}//bool is_similar( const vector<float> &lhs, const vector<float> &rhs )


TEST_CASE( "testCalibration" )
{
  using namespace SpecUtils;

  const size_t nbin = 1024;
  vector<float> frf_coefs;
  frf_coefs.push_back( 0.0f );
  frf_coefs.push_back( 3072.0f );
  frf_coefs.push_back( 0.0f );
  
  vector<float> poly_coefs = fullrangefraction_coef_to_polynomial( frf_coefs, nbin );
  vector<float> new_frf_coefs = polynomial_coef_to_fullrangefraction( poly_coefs, nbin );
  
  CHECK_MESSAGE( is_similar( frf_coefs, new_frf_coefs ), \
                       "Full Width Fraction coefficnets didnt make round trip: " \
                       << print_vec(frf_coefs) << "--->" << print_vec(new_frf_coefs) );

  vector<pair<float,float>> dev_pairs;
  auto frf_binning = fullrangefraction_binning( frf_coefs, nbin, dev_pairs );
  
  REQUIRE_MESSAGE( !!frf_binning, \
                         "Failed to make Full Width Fraction Binning for " \
                         << print_vec(frf_coefs) );
  REQUIRE_MESSAGE( frf_binning->size() == nbin, \
                         "Full range fraction returned " << frf_binning->size() \
                         << " intead of the expected " << nbin );
  
  for( size_t i = 0; i < nbin; ++i )
  {
    const float bin = static_cast<float>(i);
    const float lowerbinenergy = fullrangefraction_energy( bin, frf_coefs, nbin, dev_pairs );
    const float expected = frf_binning->at(i);
    const float larger = std::max(fabs(lowerbinenergy),fabs(expected));
    REQUIRE_MESSAGE( fabs(lowerbinenergy-expected) <= 1.0E-5*larger, \
                           "fullrangefraction_energy disagreed with fullrangefraction_binning" \
                           " (starting) at bin " << i \
                           << " got " << lowerbinenergy << " and " << expected \
                           << "respectively  for coefs=" << print_vec(frf_coefs) );
  }
  
  
  auto poly_binning = polynomial_binning( poly_coefs, nbin, dev_pairs );
  REQUIRE_MESSAGE( !!poly_binning, \
                        "Failed to make Polynomial Binning for " \
                        << print_vec(poly_coefs) );
  
  REQUIRE_MESSAGE( poly_binning->size() == nbin, \
                        "Polynomial binning returned " << poly_binning->size() \
                        << " intead of the expected " << nbin );
  
  for( size_t i = 0; i < nbin; ++i )
  {
    //const float bin = static_cast<float>(i);
    const float fwf = frf_binning->at(i);
    const float poly_eqn_energy = polynomial_energy( i, poly_coefs, dev_pairs );
    const float poly = poly_binning->at(i);
    const float larger = std::max(fabs(fwf),fabs(poly));
    
    REQUIRE_MESSAGE( fabs(fwf-poly) <= 1.0E-5*larger, "" \
                           "Lower channel energies for FWF and Polynomial coefficnets arent equal" \
                           " (starting) at bin " << i \
                           << " got " << fwf << " and " << poly \
                           << "respectively  for coefs=" << print_vec(frf_coefs) \
                           << " giving values: " << fwf << " and " << poly << " respectively" );
    
    REQUIRE_MESSAGE( fabs(poly_eqn_energy-poly) <= 1.0E-5*larger, "" \
                          "Lower channel energy for polynomial_energy and Poly binning arent equal" \
                          " (starting) at bin " << i \
                          << " got " << poly_eqn_energy << " and " << poly \
                          << "respectively  for coefs=" << print_vec(frf_coefs) \
                          << " giving values: " << poly_eqn_energy << " and " << poly
                          << " respectively" );
  }
  //Need to do a lot more tests here...

//  Need to test SpecFile::calibrationIsValid(...)
  
//  TEST_MESSAGE( "Input Directory:" << indir );
//  CHECK( false );
}//TEST_CASE( testCalibration )

TEST_CASE( "testFullRangeFractionFindEnergy" )
{
  // \TODO: Further tests to make:
  //        - Add deviation pairs
  //        - Test channels less than zero or greater than nbin
  //        - Test with 64k channels
  //        - Test with invalid calibration
  //        - Test with 2, 4 and 5 coefficents
  using namespace SpecUtils;

  const size_t nbin = 1024;
  vector<float> fwf_coefs;
  fwf_coefs.push_back( -1.926107f );
  fwf_coefs.push_back( 3020.178f );
  fwf_coefs.push_back( -8.720629f );
  
  vector<pair<float,float>> dev_pairs;
  const float accuracy = 0.001f;
  
  float binnum;
  const float energies[] = { 1121.68f, 1450.87f, 1480.65f };
  const size_t nenergies = sizeof(energies)/sizeof(energies[0]);
  
  for( size_t i = 0; i < nenergies; ++i )
  {
    const float energy = energies[i];
    CHECK_NOTHROW( binnum = find_fullrangefraction_channel( energy, fwf_coefs, nbin, dev_pairs, accuracy ) );
    const float binenergy = SpecUtils::fullrangefraction_energy( binnum, fwf_coefs, nbin, dev_pairs );



    REQUIRE_MESSAGE( fabs(binenergy-energy) < 0.1, "Found bin " << binnum \
                           << " for energy " << energy \
                           << " but found bin actually cooresponds to " \
                           << binenergy << " keV" );
  }//for( size_t i = 0; i < nenergies; ++i )
  
  //...

}//TEST_CASE( testFullRangeFractionFindEnergy )




TEST_CASE( "testPolynomialFindEnergy" )
{
  // \TODO: Further tests to make:
  //        - Add more deviation pairs
  //        - Test channels less than zero or greater than nbin
  //        - Test with 64k channels
  //        - Test with invalid calibration
  //        - Test with 2, 4 and 5 coefficents
  const size_t nbin = 1024;
  
  vector<float> poly_coefs = { -1.926107f, 2.9493925f, -0.00000831663990020752f };
  vector<pair<float,float>> dev_pairs = { {0.0f,0.0f}, {1460.0f,-10.0f}, {2614.0f,0.0f} };
  const float accuracy = 0.001f;
  
  float binnum;
  const float energies[] = { -100.0f, -10.0f, 511.0f, 1121.68f, 1450.87f, 1480.65f, 60000.0f };
  
  for( const float energy : energies )
  {
    CHECK_NOTHROW( binnum = find_polynomial_channel( energy, poly_coefs, nbin, dev_pairs, accuracy ) );
    const float binenergy = polynomial_energy( binnum, poly_coefs, dev_pairs );
    
    //Note: this doesnt test the case for multiple solution that the wanted solution is returned,
    //      it just checks the solution is correct.
    
    REQUIRE_MESSAGE( fabs(binenergy-energy) < 0.1, "Found bin " << binnum \
                           << " for energy " << energy \
                           << " but found bin actually cooresponds to " \
                           << binenergy << " keV" );
  }//for( loop over energies )

}//TEST_CASE( testPolynomialFindEnergy )


TEST_CASE( "testPolynomialFindEnergyLinearSimple" )
{
  const float energies[] = { -100.1f, -10.0f, 511.005f, 1121.68f, 1450.87f, 1480.65f, 60000.0f };
  
  for( const float energy : energies )
  {
    float binnum;
    CHECK_NOTHROW( binnum = find_polynomial_channel( energy, {0.0f, 1.0f}, 1024, {}, 0.001f ) );
    REQUIRE_MESSAGE( fabs(binnum - energy) < 0.1, "Found bin " << binnum \
                           << " for energy " << energy \
                           << " but found bin actually cooresponds to " \
                           << binnum << " keV" );
  }//for( loop over energies )
}//TEST_CASE( testPolynomialFindEnergyLinearSimple )


TEST_CASE( "testPolynomialFindEnergyRand" )
{
  const size_t nbin = 1024;
  const vector<float> poly_coefs = { -10.0f, 3.0f, -1.0f/(4.0f*nbin) };
  const vector<pair<float,float>> dev_pairs = { {0.0f,0.0f}, {661.0f,-19.0f}, {1460.0f,-10.0f}, {2614.0f,0.0f} };
  const float accuracy = 0.001f;
  
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_real_distribution<float> dist(-4.0*nbin, 4.0*nbin);
  
  //We will loop over bin-nummbers and make sure find channels returns the same channel-number as
  //  wanted
  for( size_t i = 0; i < 10000; ++i )
  {
    const float channel = dist(mt);
    const float channel_energy = polynomial_energy( channel, poly_coefs, dev_pairs );
    float found_channel;
    CHECK_NOTHROW( found_channel = find_polynomial_channel( channel_energy, poly_coefs, nbin, dev_pairs, accuracy ) );
    REQUIRE_MESSAGE( fabs(channel - found_channel) < 0.01, "Found channel " << found_channel \
                           << " for channel_energy " << channel_energy \
                           << " but actually wanted channel " << channel );
  }//for( loop over energies )
}//TEST_CASE( testPolynomialFindEnergyRand )


TEST_CASE( "testEnergyCalibrationLowerChannel" )
{
  const size_t nbin = 1024;
  vector<float> lower_channel( nbin + 1, 0.0f );
  for( size_t i = 0; i <= nbin; ++i )
    lower_channel[i] = i;
  
  EnergyCalibration cal;
  CHECK_NOTHROW( cal.set_lower_channel_energy( nbin, lower_channel ) );
  
  CHECK_THROWS_AS( cal.channel_for_energy(nbin+2), std::exception );
  CHECK_THROWS_AS( cal.channel_for_energy( -1 ), std::exception );
  CHECK_THROWS_AS( cal.energy_for_channel( nbin+2 ), std::exception );
  CHECK_THROWS_AS( cal.energy_for_channel( -1 ), std::exception );
  
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_real_distribution<float> dist( 0.0, nbin );
  
  for( size_t i = 0; i < 2000; ++i )
  {
    const float energy = dist(mt);
    
    float found_channel;
    CHECK_NOTHROW( found_channel = cal.channel_for_energy( energy ) );
    REQUIRE_MESSAGE( fabs(found_channel - energy) < 0.001, "Found channel " << found_channel \
                           << " for energy " << energy );
    
    const float channel = dist(mt);
    const float found_energy = cal.energy_for_channel( channel );
    REQUIRE_MESSAGE( fabs(found_energy - channel) < 0.001, "Found energy " << found_energy \
                           << " for channel " << channel );
  }//for( loop over energies )
  
}//TEST_CASE( testEnergyCalibrationLowerChannel )


TEST_CASE( "testCALpFile" )
{
  string calp_contents = R"(#PeakEasy CALp File Ver:  4.00
Offset (keV)           :  1.50000e+00
Gain (keV / Chan)      :  3.00000e+00
2nd Order Coef         :  0.00000e+00
3rd Order Coef         :  0.00000e+00
4th Order Coef         :  0.00000e+00
Deviation Pairs        :  5
7.70000e+01 -1.00000e+00
1.22000e+02 -5.00000e+00
2.39000e+02 -5.00000e+00
6.61000e+02 -2.90000e+01
2.61400e+03  0.00000e+00
#END)";
  
  stringstream input( calp_contents );


  string det_name;
  size_t num_channels = 1024;
  shared_ptr<EnergyCalibration> cal;
  
  CHECK_NOTHROW( cal = SpecUtils::energy_cal_from_CALp_file( input, num_channels, det_name ) );
  REQUIRE_MESSAGE( !!cal, "Failed to read basic CALp file" );
  
  CHECK( cal->valid() );
  CHECK_EQ( static_cast<int>(cal->type()), static_cast<int>(SpecUtils::EnergyCalType::Polynomial) );
  CHECK_EQ( cal->num_channels(), num_channels );
  CHECK_EQ( cal->deviation_pairs().size(), 5 );
  CHECK_EQ( cal->coefficients().size(), 2 );

  REQUIRE( cal->coefficients().size() >= 2 );
  CHECK_EQ( cal->coefficients()[0], 1.5 );
  CHECK_EQ( cal->coefficients()[1], 3.0 );

  REQUIRE( cal->deviation_pairs().size() == 5 );
  CHECK_EQ( cal->deviation_pairs()[0].first, 77.0f );
  CHECK_EQ( cal->deviation_pairs()[0].second, -1.0f );
  CHECK_EQ( cal->deviation_pairs()[1].first, 122.0f );
  CHECK_EQ( cal->deviation_pairs()[1].second, -5.0f );
  //...
  CHECK_EQ( cal->deviation_pairs()[4].first, 2614.0f );
  CHECK_EQ( cal->deviation_pairs()[4].second, 0.0f );

  // Test an invalid calibration specified in the CALp file
  calp_contents = R"(#PeakEasy CALp File Ver:  4.00
Offset (keV)           :  1.50000e+00
Gain (keV / Chan)      :  -3.00000e+00
2nd Order Coef         :  0.00000e+00
3rd Order Coef         :  0.00000e+00
4th Order Coef         :  0.00000e+00
#END)";
  input.str( calp_contents );
  cal = nullptr;
  CHECK_THROWS_AS( cal = SpecUtils::energy_cal_from_CALp_file( input, num_channels, det_name ), std::exception );
  CHECK( !cal );

  // Test an empty CALp file
  calp_contents = R"()";
  input.str( calp_contents );
  input.clear();
  CHECK_THROWS_AS( SpecUtils::energy_cal_from_CALp_file( input, num_channels, det_name ), std::exception );

  // TODO: add tests a multiple named detector demo, and then tests for `SpecFile::set_energy_calibration_from_CALp_file(...)`
  //       could/should also add tests for other calibration types

}//TEST_CASE( testCALpFile )
