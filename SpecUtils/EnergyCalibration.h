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
 - Define an energy calibration information struct that holds all the energy
   calibration stuff Measurement uses, and use it instead of the piece-meal stuff
   it currently uses.
 */
namespace SpecUtils
{
  /** The energy (or FWHM) calibration type that the calibration coefficients
   should be interpreted as.
   
   When parsing spectrum files, the calibration type used will typically be the
   type the file used.
   */
  enum class EnergyCalType : int
  {
    /**  Polynomial calibration.
     Most common energy calibration type, and type used in nearly all N42 files.
     
     For bin i:
     Energy_i = coef[0] + i*coef[1] + i*i*coef[2] + ...
     */
    Polynomial,
    
    /** Full range fraction (energy calibration).
     Used by GADRAS-DRF in PCF files, and in a few niche other places.
     
     For bin i:
     let x = i / nbin;
     E_i = C_0 + x*C_1 + x*x*C_2 + x*x*x*C_3 + C_4/(1+60*x);
     */
    FullRangeFraction,
    
    /** The lower energies of each channel is specified.
     
     Commonly used in CSV or TXT files, rarely in some N42 files, an
     occasionally in a few other places.  May either specify the same number
     of channels as the spectral data, or one more to specify the upper energy
     of the last channel.
     */
    LowerChannelEdge,
    
    /** Used for files that do not specify a energy calibration (that could be
     parsed).  For these files a polynomial energy calibration of 0 to 3 MeV
     is used unless a guess of values for the specific detector being parsed
     is known (in which case the known energy range is used).
     */
    UnspecifiedUsingDefaultPolynomial,
    
    /** A placeholder to indicate an invalid calibration type.  After
     successfully parsing a spectrum file, no gamma spectrum will have this
     equation type.
     @ToDo: Could rename to NumberEquationTypes
     */
    InvalidEquationType
  };
  

  /** Holds information about energy calibration.
   
   \TODO: move find_gamma_channel, gamma_energy_min/max, gamma_channel_width,
          gamma_channel_lower/upper/center, etc from #SpecUtils::Measurement to #EnergyCalibration
   */
  struct EnergyCalibration
  {
    /** @returns the energy calibration type. */
    EnergyCalType type() const;
    
    /** @returns true if a valid calibration.
     
     Shorthand for (EnergyCalibration::type() != EnergyCalType::InvalidEquationType).
     */
    bool valid() const;
    
    /** @returns the energy calibration coefficients.
     
     Will only be empty for #EnergyCalType::InvalidEquationType.
     For #EnergyCalType::LowerChannelEdge returns values in #m_channel_energies.
     */
    const std::vector<float> &coefficients() const;
    
    /** @returns the deviation pairs. */
    const std::vector<std::pair<float,float>> &deviation_pairs() const;
    
    /** @returns the lower channel energies. Will be a nullptr if
     type is #EnergyCalType::InvalidEquationType, and otherwise will point to a non-empty vector
     with one more entry than the number of channels specified when setting calibration.
     */
    const std::shared_ptr<const std::vector<float>> &channel_energies() const;
   
    /** Returns the number of channels this energy calibration is for. */
    size_t num_channels() const;
    
    
    /** Default constructs to type EnergyCalType::InvalidEquationType. */
    EnergyCalibration();
    
    /** Sets the type to #EnergyCalType::Polynomial, and the coefficients and deviation pairs to
     values passed in.  The channel energies will be created and have #num_channels entries.
     
     Will throw exception if an invalid calibration is passed in (less than sm_min_channels
     channels, more than sm_max_channels, or non-increasing channel energies), and no member
     variables will be changed.
     */
    void set_polynomial( const size_t num_channels,
                         const std::vector<float> &coeffs,
                         const std::vector<std::pair<float,float>> &dev_pairs );
    
    /** Functionally the same as #set_polynomial, but will set type to
     #EnergyCalType::UnspecifiedUsingDefaultPolynomial.
     
     This function is useful for denoting that the energy calibration is polynomial, but wasnt
     parsed from the file, but instead guessed.
     */
    void set_default_polynomial( const size_t num_channels,
                                 const std::vector<float> &coeffs,
                                 const std::vector<std::pair<float,float>> &dev_pairs );
    
    /** Sets the type to #EnergyCalType::FullRangeFraction, and the coefficients and deviation pairs
    to values passed in.  The channel energies will be created and have #num_channels entries.
    
    Will throw exception if an invalid calibration is passed in (less than sm_min_channels
    channels, more than sm_max_channels, or non-increasing channel energies), and no member
    variables will be changed.
    */
    void set_full_range_fraction( const size_t num_channels,
                                  const std::vector<float> &coeffs,
                                  const std::vector<std::pair<float,float>> &dev_pairs );
    
    /** Sets the type to #EnergyCalType::LowerChannelEdge, and creates a new channel energies
     matching the values passed in.
    
     @param num_channels The number of channels in the gamma spectrum.
     @param channel_energies The lower energies for each channel, in keV.
            At most the first num_channels+1 entries will be copied internally.  If this vector has
            exactly num_channels entries, then one more will be added to the end to represent the
            upper energy of the last channel.  If it has less entries than num_channels, than an
            exception will be thrown.  For the case of a single channel, this vector must have
            two entries.
     
    Will throw exception (without changing any member variables) if an invalid calibration is passed
    in (less than sm_min_channels channels, more than sm_max_channels, or non-monotonically
    increasing channel energies, or a single channel without two energies passed in), or if channel
    energies does not have at least num_channels entries.
     
    \TODO: overload this function call take a rvalue reference to the vector
    */
    void set_lower_channel_energy( const size_t num_channels,
                                   const std::vector<float> &channel_energies );
    
    /** Overload to use of std::move semantics (rvalues).
     
     Ideally you want channel_energies to have one more entry than num_channels, otherwise the
     upper energy of the last channel will be guessed.
     */
    void set_lower_channel_energy( const size_t num_channels,
                                   std::vector<float> &&channel_energies );
    
    
    /** Comparison operator so we can use this class as a key in associative containers.
     Compares first by number of channels, then calibration type, then calibration coefficents,
     then by deviation pairs.  Does not compare channel energies (except for
     m_type==#EnergyCalType::LowerChannelEdge) as this should/would be redundant.
     */
    bool operator<( const EnergyCalibration &rhs ) const;
    
    /** Compares the value of type, coefficients, deviation pairs, and if channel energies are
     defined, the number of them (doesnt compare each entry in channel energies).
     
     Note: the test is for exact matches, so float values off due to rounding somewhere will fail.
     */
    bool operator==( const EnergyCalibration &rhs ) const;
    
    /** Returns the oposite of operator==.
     */
    bool operator!=( const EnergyCalibration &rhs ) const;
    
    /** Returns the approximate number of bytes being taken up by *this. */
    size_t memmorysize() const;
    
    
    /** Returns the fractional channel that cooresponds to energy.
     
     Throws exception if #EnergyCalType::InvalidEquationType, or if #EnergyCalType::LowerChannelEdge
     and energy is outside range, or if outside range is valid for Polynomial and FullRangeFraction.
     
     For Polynomial or FullRangeFraction returned value may be below 0, or above number of channels.
     */
    double channel_for_energy( const double energy ) const;
    
    /** Returns the energy cooresponding to the fractional channel passed in.
     
     Note: for polynomial and full range fraction will return an answer, even if channel is less
           than zero or greater than the number of channels.  No check is made that the channel
           is in valid range (e.g., that quadratic order term isnt overpowering linear term.).
     
     Throws exception if #EnergyCalType::InvalidEquationType, or if #EnergyCalType::LowerChannelEdge
     and channel is less than zero or greater than number of channels+1, or if outside range is
     valid for Polynomial and FullRangeFraction.
     */
    double energy_for_channel( const double channel ) const;
    
    
    /// \TODO: add channel_for_energy and energy_for_channel equivalents that use lower channel
    ///        energies
    
    /** @returns lower energy of first channel.
     
     Throws exception if #EnergyCalType::InvalidEquationType
     */
    float lower_energy() const;
    
    /** @returns upper energy of last channel.
    
    Throws exception if #EnergyCalType::InvalidEquationType
    */
    float upper_energy() const;
    
    
#if( SpecUtils_ENABLE_EQUALITY_CHECKS )
    /** Tests if the two calibrations passed in are equal for most intents and purposes.
     
     Allows some small numerical rounding to occur, and will allow polynomial and FRF to compare
     equal if they are equivalent.
    
     Throws an std::exception with a brief explanation when an issue is found.
    */
    static void equal_enough( const EnergyCalibration &lhs, const EnergyCalibration &rhs );
#endif
    
    /** Define a minimum number of channels to be 1; we could probably make it zero, but this
     doesnt make much sense.
     
     A value of zero will cause an exception to be thrown when setting the calibration.
     */
    static const size_t sm_min_channels; // = 1
    
    /** Define a maximum number of channels to be 8 more channels than 64k (i.e., 65544);
     this is entirely as a sanity check so some errant code doesnt try to have us allocate
     gigabytes of ram.
     
     Values larger than this will cause an exception to be thrown when setting the calibration.
     */
    static const size_t sm_max_channels; // = 65536 + 8
    
    /** The largest positive value of the offset (zeroth energy cal term) allowed for normal polynomial energy calibration.
     i.e., if a gamma spectrum has a larger value than this, then the calibration coefficients will be considered garbage and not used.
     
     Current value is 5500 keV (only alpha particle spectra seem to have values more than a few hundred keV).
     
     A lower bound of -500 keV is currently hard-coded.
     
     \sa set_polynomial, EnergyCalCheckType::Normal
     */
    static const float sm_polynomial_offset_limit;
    
  protected:
    /** Checks the channel energies is acceptable (e.g., enough channels, and monotonically
     increasing values).
     
     Throws exception if error is found.
     */
    void check_lower_energies( const size_t nchannels, const std::vector<float> &energies );
    
    EnergyCalType m_type;
    std::vector<float> m_coefficients;
    std::vector<std::pair<float,float>> m_deviation_pairs;
    std::shared_ptr<const std::vector<float>> m_channel_energies;
  };//struct EnergyCalibration

  /** Returns an energy calibration with the specified number of channels combined.
   
   If the number of channels in the input energy calibration does not evenly divide by the number
   of channels to combine, then the resulting energy calibration will round-up to have one more
   channel than the integer division gives.
   
   @param orig_cal The original energy calibration.
   @param num_channel_combine The number of channels to combine.  Ex, if 2, then returned energy
          cal will have half as many channels as \p orig_cal.  Must not be zero.
   @return The new energy calibration; will not be nullptr.  If input energy calibration is invalid
           or zero channels, then returns an invalid energy calibration.
          
   Throws exception if channels to combine is 0.
   */
  std::shared_ptr<EnergyCalibration> energy_cal_combine_channels( const EnergyCalibration &orig_cal,
                                                                 const size_t num_channel_combine );
  

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
   
   Throws exception if an invalid energy calibration (i.e., channel energies not increasing)
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
   @param include_upper_energy If true, the reurned answer will have one more entry than 'nchannel'
          that indicates the upper energy of the last bin
   @returns The lower energies of each gamma channels.  Will have 'nchannel'
            entries.
   
   Throws exception if an invalid energy calibration (i.e., channel energies not increasing)
   */
  std::shared_ptr< const std::vector<float> >
  fullrangefraction_binning( const std::vector<float> &coeffs,
                            const size_t nchannel,
                            const std::vector<std::pair<float,float>> &dev_pairs,
                            const bool include_upper_energy = false );
  
  
  /** Gives the energy cooresponding to the passed in _channel_number_.
   
   @param channel_number The channel number you would like the energy for.
          This value may be non-integer (ex, if you want the energy of a peak
          whose mean within a channel); an integer value gives you the lower
          edge of the channel.
   @param coeffs The full width fraction equation coefficients.
   @param nchannel Then number of gamma channels the returned answer will have.
   @param deviation_pairs The sorted deviation pairs defined for the calibration.
   @returns The energy of the specified channel.
   
   Doesnt perform a check that the coefficients or deviation pairs are actually valid.
   Throws exception if deviation pairs are not sorted.
   */
  double fullrangefraction_energy( const double channel_number,
                                 const std::vector<float> &coeffs,
                                 const size_t nchannel,
                                 const std::vector<std::pair<float,float>> &deviation_pairs );
  
  /** Gives the energy cooresponding to the passed in _channel_number_.
   
   @param channel_number The channel number you would like the energy for.
          This value may be non-integer (ex, if you want the energy of a peak
          whose mean within a channel); an integer value gives you the lower
          edge of the channel.
   @param coeffs The polynomial equation coefficients.
   @param deviation_pairs The sorted deviation pairs defined for the calibration.
   @returns The energy of the specified channel.
   
   Note: doesnt perform a check that the coefficients or deviation pairs are actually valid.
   Note: doesnt check that channel_number passed in is valid for given coefficients (e.g.,
         quadratic term is overpowering linear term, etc).
   
   Throws exception if deviation pairs are not sorted.
   */
  double polynomial_energy( const double channel_number,
                           const std::vector<float> &coeffs,
                           const std::vector<std::pair<float,float>> &deviation_pairs );

  
  /** Applies the deviation pairs to the energy given by polynomial/FRF
     calibration to return the actual energy.
   
     For example, if there is a 10 keV offset defined at 1460 keV, and you pass
     in 1450, this function will return 1460.
   
     An example use is:
     \code{.cpp}
     size_t channel_num = 100;
     double polynomial_energy = C_0 + C_1*channel_num + C_2*channel_num*channel_num;
     double correct_energy = polynomial_energy + deviation_pair_correction(polynomial_energy, deviation_pairs);
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
  double deviation_pair_correction( const double polynomial_energy,
                                      const std::vector<std::pair<float,float>> &dev_pairs );
  
  
  /** For a given actual energy, tells you how much contribution the deviation
   pairs gave over the polynomial/FRF calibration.
   
   Example usage to find channel correspoding to a peak at 511 keV:
   \code{.cpp}
   const size_t nchannel = 1024;
   double obs_energy = 511.0;
   double frf_energy = obs_energy - correction_due_to_dev_pairs(obs_energy,deviation_pairs);
   double channel = find_fullrangefraction_channel( frf_energy, frf_coeffs, nchannel, {} );
   \endcode
   
   \TODO: Currently will return answer accurate within about 0.0001 keV, but in the
         future this should be able to be made exactly accuate (within numerical
         limits anyway).
   */
  double correction_due_to_dev_pairs( const double true_energy,
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
   
   Throws exception if resulting energy calibration wont be strictly increasing after applying
   deviation pair.
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
   @param coeffs The FullRangeFraction equation coefficients.
   @param nchannel Then number of gamma channels of the spectrum, must be at least 2.
   @param deviation_pairs The deviation pairs defined for the calibration; it is
          assumed they are sorted by energy already.
   @param accuracy The accuracy, in keV, the returned answer will be of the
          true answer; only used if deviation pairs are specified (otherwise
          answer is always exact)
   
   Will throw exception if there are not at least two coefficients, or if a
   solution is failed to be found (e.g., invalid coefficients or deviation
   pairs).
   
   If deviation_pairs is empty then an algabraic approach is used, otherwise a
   binary search is performed to find the bin that comes within the specified
   accuracy.
   
   \TODO: Use #correction_due_to_dev_pairs to make it so algabraic approach can
          always be used.
  */
  double find_fullrangefraction_channel( const double energy,
                                   const std::vector<float> &coeffs,
                                   const size_t nchannel,
                                   const std::vector<std::pair<float,float>> &deviation_pairs,
                                   const double accuracy = 0.001 );
  
  /** Gives the channel (including fractional portion) corresponding to the
     specified energy.
     
     @param energy The energy to find the channel number for.
     @param coeffs The Polynomial equation coefficients.
     @param nchannel Then number of gamma channels of the spectrum; used to help determine
                     correct answer when multiple solutions exist.
     @param deviation_pairs The deviation pairs defined for the calibration; it is
            assumed they are sorted by energy already.
     @param accuracy The accuracy, in keV, the returned answer will be of the
            true answer; only used if coefficents are grater than third order (otherwise answer is
            always exact). Must be greater than zero.
     
     Will throw exception if there are not at least two coefficients, or if a
     solution is failed to be found (e.g., invalid coefficients or deviation
     pairs).
     
     If number of coefficients is three or less, than an algabraic approach is used, otherwise a
     binary search is performed to find the bin that comes within the specified accuracy.
   
     Note: that #correction_due_to_dev_pairs is used to correct for deviation pairs when using the,
     algebraic appoach, and currently (20200820) may be correct to only 0.01 keV.
  */
  double find_polynomial_channel( const double energy,
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
   
   If original_counts.size() is one less than original_energies.size, then resulting_counts.size()
   will also be one less than new_energies.size(), otherwise new_energies and resulting_counts will
   be same size.
   
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
  
  /** Reads an input CALp file and returns a valid energy calibration.
   
   @param input Input stream with CALp file information.  If nullptr is returned, this function will seekg the stream back to its tellg position
          but if calibration is successfully parsed, than position will be at the end of information used.
   @param num_channels The number of channels this calibration *might* be applied to; needed to fill in channel energies.  If less than
          two, will return nullptr.  If CALp file is for lower channel energies (e.g., has a "Exact Energies" segment), then if the CALp has
          more channels than the data, the input energy calibration will be truncated to specified \p num_channels.  If CALp has less
          channels than nullptr will be returned.  If energy in CALp is invalid, then nullptr will be returned.
   @param det_name [out] The detector name as given in the CALp file, or empty if not give.  Note that detector name is an InterSpec
          specific extension to CALp files.
   @returns a valid energy calibration.
   
   Throws exception on error.
   
   Example CALp file:
   \verbatim
   #PeakEasy CALp File Ver:  4.00
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
   #END
   \endverbatim
   */
  std::shared_ptr<EnergyCalibration> energy_cal_from_CALp_file( std::istream &input,
                                                               const size_t num_channels,
                                                               std::string &det_name );
  
  
  
  /** Writes the given energy calibration object as a CALp file.
   
   If a spectrum file has multiple detectors, you may write out each calibration, with the detectors name, to a single file
   
   @param output The stream to write the output to.
   @param The energy calibration to write.
   @param detector_name The name of the detector - an InterSpec/SpecUtils specific extension of the CALp file format.  If blank,
          wont be written.
   @returns if CALp file was successfully written.
   
   Note, if the energy calibration is Full Range Fraction, then it will be converted to polynomial, and those coefficients written out, but also
   the original FRF coefficients will be written out after the other content - this is a InterSpec/SpecUtils specific extension of CALp file
   format.
   */
  bool write_CALp_file( std::ostream &output,
                        const std::shared_ptr<const EnergyCalibration> &cal,
                        const std::string &detector_name );
}//namespace SpecUtils

#endif
