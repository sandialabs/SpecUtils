#ifndef EnergyCalibration_h
#define EnergyCalibration_h
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
#include <memory>
#include <utility>

/**
 
 
 ToDo:
 - There is probably plenty of other energy calibration related stuff that could
   be moved into EnergyCalibration.h/.cpp
 - Define an energy calibraiton information struct that holds all the energy
   calibration stuff Measurement uses, and use it instead of the piec-meal stuff
   it currently uses.
 */
namespace SpecUtils
{
  void test_spline();
  
  /** The energy (or FWHM) calibration type that the calibration coefficients
   should be interpreted as; typically also the type in the file.
   */
  enum class EnergyCalType : int
  {
    /** for bin i, Energy_i = coef[0] + i*coef[1] + i*i*coef[2] + ... */
    Polynomial,
    
    /** */
    FullRangeFraction,
    
    /** The lower energies of each channel is specified. */
    LowerChannelEdge,
    
    /** Used for files that do not specify a energy calibration (that could be
     parsed).  For these files a polynomial energy calibration of 0 to 3 MeV
     is used unless a guess of values for the specfic detector being parsed
     is known (in which case the known energy range is used).
     */
    UnspecifiedUsingDefaultPolynomial,
    
    /** A placeholder to indicate an invalid calibration type.  After sucessfuly
     parsing a spectrum file, no gamma spectrum will have this equation type.
     ToDo: Could rename to NumberEquationTypes
     */
    InvalidEquationType
  };
  
  
  /** Returns each channels lower energy, based on the input polynomial
   calibration equation and deviation pairs.
   
   Note that this function uses the convention that the energy of the lower edge
   of the channel is given by:
      E_i = C_0 + i*C_1 + i*i*C_2 + ...
   
   @param coeffs The polynomial equation coefficients.
   @param nchannel Then number of gamma channels the returned answer will have.
   @param deviation_pairs The deviation pairs defined for the calibration; it is
          assumed they are sorted by energy already.
   @returns The lower energies of each gamma channels.  Will have 'nchannel'
            entries.
   */
  std::shared_ptr< const std::vector<float> >
  polynomial_binning( const std::vector<float> &coeffs,
                     const size_t nchannel,
                     const std::vector<std::pair<float,float>> &deviation_pairs );
  
  
  /** Returns lower channel energies from input full range fraction calibration
   equation and deviation pairs.
  
   Uses the definition for the i'th channel:
     x = i / nbin;
     E_i = C_0 + x*C_1 + x*x*C_2 + x*x*x*C_3 + C_4/(1+60*x);
   
   @param coeffs The full width fraction equation coefficients.
   @param nchannel Then number of gamma channels the returned answer will have.
   @param deviation_pairs The deviation pairs defined for the calibration; it is
          assumed they are sorted by energy already.
   @returns The lower energies of each gamma channels.  Will have 'nchannel'
            entries.
   */
  std::shared_ptr< const std::vector<float> >
  fullrangefraction_binning( const std::vector<float> &coeffs,
                            const size_t nchannel,
                            const std::vector<std::pair<float,float>> &dev_pairs );
  
  
  /** Gives the energy cooresponding to the passed in _channel_number_.
   
   @param channel_number The channel number you would like the energy for.
          This value may be non-integer (ex, if you want the energy of a peak
          whose mean within a channel); an integer value gives you the lower
          edge of the channel.
   @param coeffs The full width fraction equation coefficients.
   @param nchannel Then number of gamma channels the returned answer will have.
   @param deviation_pairs The deviation pairs defined for the calibration; it is
          assumed they are sorted by energy already.
   @returns The energy of the specified channel.
   */
  float fullrangefraction_energy( float channel_number,
                                 const std::vector<float> &coeffs,
                                 const size_t nchannel,
                                 const std::vector<std::pair<float,float>> &deviation_pairs );
  
  
  
  /** Applies the deviation pairs to the energy given by polynomial/FRF
     calibration to return the actual energy.
   
     For example, if there is a 10 keV offset defined at 1460 keV, and you pass
     in 1450, this function will return 1460.
   
     An example use is:
     \code{.cpp}
     size_t channel_num = 100;
     float polynomial_energy = C_0 + C_1*channel_num + C_2*channel_num*channel_num;
     float correct_energy = polynomial_energy + deviation_pair_correction(polynomial_energy, deviation_pairs);
     \endcode
   
     A cubic interpolation is used for energies between deviation pairs.
   
     @param polynomial_energy The energy produced by the polynomial or FRF
            energy calibration equation.
     @param dev_pairs The deviation pairs to use. dev_pairs[i].first is energy
            (in keV), and dev_pairs[i].second is offset.
     @returns energy adjusted for deviation pairs.
   
   Note: this function re-computes the cubic spline each time it is called, so
     if you plan to call for multiple energies, it may be more efficient to
     but them in a vector, and call #apply_deviation_pair with it.
   
   An example for using deviation pairs:
   - Determine offset and gain using the 239 keV and 2614 keV peaks of Th232
   - If the k-40 1460 keV peak is now at 1450 keV, a deviation of 10 keV should
     be set at 1460 keV
   - Deviation pairs set to zero would be defined for 239 keV and 2614 keV
   */
  float deviation_pair_correction( const float polynomial_energy,
                                      const std::vector<std::pair<float,float>> &dev_pairs );
  
  
  /** For a given actual energy, tells you how much contribution the deviation
   pairs gave over the polynomial/FRF calibration.
   
   Example usage to find channel correspoding to a peak at 511 keV:
   \code{.cpp}
   const size_t nchannel = 1024;
   float obs_energy = 511.0f;
   float frf_energy = obs_energy - correction_due_to_dev_pairs(obs_energy,deviation_pairs);
   float channel = find_fullrangefraction_channel( frf_energy, frf_coeffs, nchannel, {} );
   \endcode
   
   ToDo: Currently will return answer accurate within about 0.01 keV, but in the
         future this should be able to be made exactly accuate (within numerical
         limits anyway).
   */
  float correction_due_to_dev_pairs( const float true_energy,
                                     const std::vector<std::pair<float,float>> &dev_pairs );
  
  
  /** Takes an input vector of lower channel energies, applies the deviation
   pairs, and returns the resulting set of lower channel energies.
   
   @param binning The lower channel energies before applying deviation pairs
          (e.g., each channels left energy from the polynomial or FRF equation)
   @param dev_pairs The deviation pairs to apply.
   @returns The channel energies with deviation pairs applies.  Will always
            return a valid pointer.
   
   See discussion for #deviation_pair_correction for how deviation pairs
   are applied.
   */
  std::shared_ptr<const std::vector<float>>
  apply_deviation_pair( const std::vector<float> &binning,
                        const std::vector<std::pair<float,float>> &dev_pairs );
  
  
  /** Converts from polynomial coefficients, to full range fraction (aka FRF)
   coefficients.  Only works for up to the first four coefficints, as the fifth
   one for FRF doesnt translate easily to polynomial, so it will be ignored if
   present.
   
   @param coeffs The polynomial equation coefficients.
   @param nchannel The number of channels in the spectrum the equations are
          being used for.
   @returns the full range fraction coefficients.  If input coefficients is
            empty or number of channels is zero, will return empty vector.
   */
  std::vector<float>
  polynomial_coef_to_fullrangefraction( const std::vector<float> &coeffs,
                                        const size_t nchannel );
  
  
  /** Converts from full range fraction, to polynomial coefficients.  Only
   considers a maximum of the first four coefficients, as the fifth coefficient
   of FRF coorespods to a term like: C_4 / (1.0f+60.0*x).
   
   @param coeffs The full range fraction equation coefficients.
   @param nchannel The number of channels in the spectrum the equations are
          being used for.
   @returns the polynomial coefficients.  If input coefficients is empty or
            number of channels is zero, will return empty vector.
   */
  std::vector<float>
  fullrangefraction_coef_to_polynomial( const std::vector<float> &coeffs,
                                        const size_t nchannel );
  
  
  /** Converts coefficients from polynomial equation that uses the convention
   the energy given by the equation is the middle of the channel (which is
   non-standard) to standard full range fraction equation coefficients.
   */
  std::vector<float>
  mid_channel_polynomial_to_fullrangeFraction( const std::vector<float> &coeffs,
                                               const size_t nchannel );
  
  
  //calibration_is_valid(...): checks to make sure passed in calibration is
  //  valid.  Polynomial and FullRangeFraction types are checked to make sure
  //  that energy of the first two and last two bins are increasing left to
  //  right. LowerChannelEdge type is check that each bin is increasing over
  //  previous, and that it has at least as many bins as nbin.
  //  InvalidEquationType always returns false.
  bool calibration_is_valid( const EnergyCalType type,
                                   const std::vector<float> &eqn,
                                   const std::vector< std::pair<float,float> > &devpairs,
                                   size_t nbin );
  
  
  
  /** Converts the polynomial equation coefficients to allow removing channels
   from the left beggining of the spectrum.
   
   @param num_channels_remove The number of channels to remove from the
          beggining of the spectrum.
   @param orig_coefs The original polynomial coefficients.
   @returns The polynomial coefficients for the new spectrum with the first
            num_channels_remove channels removed.
   
   Truncates coefficients to a 6th order polynomial (which is already more
   orders than you should be using).
   
   Changes the polynomial calibration coefficients
   Would probably work for adding channels, but untested.
   */
  std::vector<float>
  polynomial_cal_remove_first_channels( const int num_channels_remove,
                                        const std::vector<float> &orig_coefs );
  
  
  
  /** Gives the channel (including fractional portion) corresponding to the
   specified energy.
   
   @param energy The energy to find the channel number for.
   @param coeffs The polynomial equation coefficients.
   @param nchannel Then number of gamma channels the returned answer will have.
   @param deviation_pairs The deviation pairs defined for the calibration; it is
          assumed they are sorted by energy already.
   @param accuracy The accuracy, in keV, the returned answer will be of the
          true answer; only used if deviation pairs are specified (otherwise
          answer is always exact)
   
   Will throw exception if there are not a t least two coefficients, or if a
   solution is failed to be found (e.g., invalid coefficients or deviation
   pairs).
   
   If deviation_pairs is empty then an algabraic approach is used, otherwise a
   binary search is performed to find the bin that comes within the specified
   accuracy.
   
   ToDo: Use #correction_due_to_dev_pairs to make it so algabraic approach can
         always be used.
*/
  float find_fullrangefraction_channel( const double energy,
                                   const std::vector<float> &coeffs,
                                   const size_t nchannel,
                                   const std::vector<std::pair<float,float>> &deviation_pairs,
                                   const double accuracy = 0.001 );
  
  
  /** Converts channel counts defined by one set of channel lower energies, to
   instead be defined by a different set of lower energies.
   
   @original_energies The original channel lower energies
   @original_counts The original channel counts
   @new_energies The lower channel energies to rebin the channel counts to
   @resulting_counts Results of the rebinning
   
   Not incredably well tested, but appears to be better than the previous
   rebin_by_lower_edge(...), but it so it currently used.  There is some code in
   the function that does test this function if PERFORM_DEVELOPER_CHECKS is true.
   
   There are some tests started for this function in testing/testRebinByLowerEnergy.cpp.
   
   Throw exception if any input has less than four channels.
   */
  void rebin_by_lower_edge( const std::vector<float> &original_energies,
                           const std::vector<float> &original_counts,
                           const std::vector<float> &new_energies,
                           std::vector<float> &resulting_counts );
  
  namespace details
  {
    

  }//namespace details
  
}//namespace SpecUtils

#endif
