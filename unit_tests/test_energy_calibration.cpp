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
#include <limits>
#include <sstream>
#include <ostream>

#include <boost/algorithm/string.hpp>

//#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE testCalibration
//#include <boost/test/unit_test.hpp>
#include <boost/test/included/unit_test.hpp>

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;
using namespace boost::unit_test;


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


BOOST_AUTO_TEST_CASE( testCalibration )
{
  using namespace SpecUtils;

  const size_t nbin = 1024;
  vector<float> frf_coefs;
  frf_coefs.push_back( 0.0f );
  frf_coefs.push_back( 3072.0f );
  frf_coefs.push_back( 0.0f );
  
  vector<float> poly_coefs = fullrangefraction_coef_to_polynomial( frf_coefs, nbin );
  vector<float> new_frf_coefs = polynomial_coef_to_fullrangefraction( poly_coefs, nbin );
  
  BOOST_CHECK_MESSAGE( is_similar( frf_coefs, new_frf_coefs ), \
                       "Full Width Fraction coefficnets didnt make round trip: " \
                       << print_vec(frf_coefs) << "--->" << print_vec(new_frf_coefs) );

  DeviationPairVec dev_pairs;
  ShrdConstFVecPtr frf_binning = fullrangefraction_binning( frf_coefs, nbin, dev_pairs );
  
  BOOST_REQUIRE_MESSAGE( !!frf_binning, \
                         "Failed to make Full Width Fraction Binning for " \
                         << print_vec(frf_coefs) );
  BOOST_REQUIRE_MESSAGE( frf_binning->size() == nbin, \
                         "Full range fraction returned " << frf_binning->size() \
                         << " intead of the expected " << nbin );
  
  for( size_t i = 0; i < nbin; ++i )
  {
    const float bin = static_cast<float>(i);
    const float lowerbinenergy = fullrangefraction_energy( bin, frf_coefs, nbin, dev_pairs );
    const float expected = frf_binning->at(i);
    const float larger = std::max(fabs(lowerbinenergy),fabs(expected));
    BOOST_REQUIRE_MESSAGE( fabs(lowerbinenergy-expected) <= 1.0E-5*larger, \
                           "fullrangefraction_energy disagreed with fullrangefraction_binning" \
                           " (starting) at bin " << i \
                           << " got " << lowerbinenergy << " and " << expected \
                           << "respectively  for coefs=" << print_vec(frf_coefs) );
  }
  
  
  ShrdConstFVecPtr poly_binning = polynomial_binning( poly_coefs, nbin, dev_pairs );
  BOOST_REQUIRE_MESSAGE( !!poly_binning, \
                        "Failed to make Polynomial Binning for " \
                        << print_vec(poly_coefs) );
  
  BOOST_REQUIRE_MESSAGE( poly_binning->size() == nbin, \
                        "Polynomial binning returned " << poly_binning->size() \
                        << " intead of the expected " << nbin );
  
  for( size_t i = 0; i < nbin; ++i )
  {
    const float bin = static_cast<float>(i);
    const float fwf = frf_binning->at(i);
    const float poly = poly_binning->at(i);
    const float larger = std::max(fabs(fwf),fabs(poly));
    BOOST_REQUIRE_MESSAGE( fabs(fwf-poly) <= 1.0E-5*larger, "" \
                          "Lower channel energies for FWF and Polynomial coefficnets arent equal" \
                          " (starting) at bin " << i \
                          << " got " << fwf << " and " << poly \
                          << "respectively  for coefs=" << print_vec(frf_coefs) \
                          << " giving values: " << fwf << " and " << poly << " respectively" );
  }
  //Need to do a lot more tests here...

//  Need to test MeasurementInfo::calibrationIsValid(...)
  
//  BOOST_TEST_MESSAGE( "Input Directory:" << indir );
//  BOOST_CHECK( false );
}//BOOST_AUTO_TEST_CASE( testCalibration )

BOOST_AUTO_TEST_CASE( testFindEnergy )
{
  using namespace SpecUtils;

  const size_t nbin = 1024;
  vector<float> fwf_coefs;
  fwf_coefs.push_back( -1.926107f );
  fwf_coefs.push_back( 3020.178f );
  fwf_coefs.push_back( -8.720629f );
  
  DeviationPairVec dev_pairs;
  const float accuracy = 0.001f;
  
  float binnum;
  const float energies[] = { 1121.68, 1450.87, 1480.65 };
  const size_t nenergies = sizeof(energies)/sizeof(energies[0]);
  
  for( size_t i = 0; i < nenergies; ++i )
  {
    const float energy = energies[i];
    BOOST_CHECK_NO_THROW( binnum = find_fullrangefraction_channel( energy, fwf_coefs, nbin, dev_pairs, accuracy ) );
    const float binenergy = SpecUtils::fullrangefraction_energy( binnum, fwf_coefs, nbin, dev_pairs );



    BOOST_REQUIRE_MESSAGE( fabs(binenergy-energy) < 0.1, "Found bin " << binnum \
                           << " for energy " << energy \
                           << " but found bin actually cooresponds to " \
                           << binenergy << " keV" );
  }//for( size_t i = 0; i < nenergies; ++i )
  
  //...

}//BOOST_AUTO_TEST_CASE( testFindEnergy )
