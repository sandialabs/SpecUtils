#ifndef SpecUtils_SpecFile_h
#define SpecUtils_SpecFile_h
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

#include <set>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>

#define BOOST_DATE_TIME_NO_LIB
#include <boost/date_time/posix_time/posix_time.hpp>


/*
Shortcommings that wcjohns should be addressed
 - Many of the N24 fields possible are not checked for
    - comments for multiple different tags, ...
 - Neutron meausruemtns should have their own live and real time
 - Neutron counts are typically merged into a gamma detectors Measurement if a
   reasonable pairing can be made. When and if this is done needs to be clearly
   specified, and either stopped of facilities added to keep neutron det. info.
   (should probably instead make own neutron info object (or more generally gross count) that can be
   associated with a Measurement, or maybe sample number, maybe multiple neutron to a Measurement)
 - Should add a DetectorInfo object that Measurement objects point to and share.
   - Should add things like dimention and RadDetectorKindCode to this object,
     as well as characteristics (as defined in N42-2012, but in a few other file
     formats)
   - Should probably get rid of detector number, and just keep name
 - Should eliminate the <InterSpec:DetectorType> tag in written N42 2012 files.
 - Should consider adding explicit dead_time field
 - Should add elevation and uncertainties to GPS coordinates
 - Should add detector and item orientation/positions
 - Should consider removing measurement_operator and inspection and make location
   part of detector location object
 - Should implement tracking N42 MeasurementGroupReferences to link Analysis
   with appropriate spectra, and InterSpec can use to link to peaks and such.
 - Should add in InstrumentToItemDistance and InstrumentToItemBearing to
 - There is a degeneracy in SpecFile between: detector_type_,
   instrument_type_, manufacturer_, and instrument_model_ - so this should
   be sorted out.
 - Should link derived spectra to analysis results (when applicable), and
   I'm not sure if the derived spectra should be in with the rest of the spectra
 - When multiple copies of data is included in file with different energy
   calibrations (different energy ranges, or linear vs log energy scale),
   currently denote this by artificaually creating new detector and adding
   "_intercal_<calid>" to its name; should impose a better mechanism to handle
   this.
 - the generated UUID should maybe be more stable with respect to just the
   spectroscopic information.
 - Should rename DetectorType to SystemType or DetectionSystemType
 - Should add in tag that indicates original file type, which will survive
   serialization to N42-2012 and back in
 - For analysis result should add information on what isotopes where in the
   alarm templates
 - Should add a Dose field to Measurement; see CountDose for a starting point
 - Should add in "Characteristics" a few places (for detectors, system,
*/

//Forward declarations not within SpecUtils namespace
namespace rapidxml
{
  template<class Ch> class xml_node;
  template<class Ch> class xml_document;
  template<class Ch> class xml_attribute;
}//namespace rapidxml

class SpecMeas;

#if( SpecUtils_ENABLE_D3_CHART )
namespace D3SpectrumExport{ struct D3SpectrumChartOptions; }
#endif



namespace SpecUtils
{
/** Enum used to specify which spectrum parsing function to call when opening a
 spectrum file.
 
 Users of this library should nearly always use #SpecUtils::ParserType::Auto
 and only use another value if efficiency is a concern, or the format is really
 known or needs to be forced to correctly open (in which case please report as a
 bug).
 */
enum class ParserType : int
{
  /** All N42-2006 like variants (aka ICD1), as well as some ICD2 variants. */
  N42_2006,
  /** All N42-2012 like variants. */
  N42_2012,
  /** ASCII or binary (both integer based and float based) SPC formats. */
  Spc,
  /** Exploranium GR-130, GR-135 v1 or v2 binary formats. */
  Exploranium,
  /** GADRAS PCF binary format. */
  Pcf,
  /** ORTEC binary CHN file. */
  Chn,
  /** The IAEA SPE ascii format; includes a number of vendor extensions */
  SpeIaea,
  /** Catch all for CSV, TSV, TXT and similar variants (ex TXT GR-135, SRPM210,
   Spectroscopic Daily Files, ...)
   */
  TxtOrCsv,
  /** Canberra binary CNF format. */
  Cnf,
  /** Tracs MPS binary format. */
  TracsMps,
  /** Aram TXT and XML hybrid format. */
  Aram,
  /** Spectroscopic Portal Monitor Daily File. */
  SPMDailyFile,
  /** Amptek MCA text-ish based format. */
  AmptekMca,
  /** Microraider XML based format. */
  MicroRaider,
  /** ORTEC list mode (.lis) from at least digiBASE(-E) detectors. */
  OrtecListMode,
  /** LSRM text based format. */
  LsrmSpe,
  /** TKA text based format. */
  Tka,
  /** MultiAct binary format - only partially supported. */
  MultiAct,
  /** PHD text based format. */
  Phd,
  /** LabZY XML based files. */
  Lzs,
  /** Automatically determine format - should be safe to be used with any format
   that can be parsed.  Will first guess format based on file extension, then
   on initial file contents, and if still not successfully identified, will try
   every parsing function.
   */
  Auto
};//enum ParserType




enum class SaveSpectrumAsType : int
{
  /** See #SpecFile::write_txt for details. */
  Txt,
  
  /** See #SpecFile::write_csv for details. */
  Csv,
  
  /** See #SpecFile::write_pcf for details. */
  Pcf,
  
  /** See #SpecFile::write_2006_N42_xml for details. */
  N42_2006,
  
  /** See #SpecFile::write_2012_N42 for details. */
  N42_2012,
  
  /** See #SpecFile::write_integer_chn for details. */
  Chn,
  
  /** See #SpecFile::write_binary_spc for details. */
  SpcBinaryInt,
  
  /** See #SpecFile::write_binary_spc for details. */
  SpcBinaryFloat,
  
  /** See #SpecFile::write_ascii_spc for details. */
  SpcAscii,
  
  /** See #SpecFile::write_binary_exploranium_gr130v0 for details. */
  ExploraniumGr130v0,
  
  /** See #SpecFile::write_binary_exploranium_gr135v2 for details. */
  ExploraniumGr135v2,
  
  /** See #SpecFile::write_iaea_spe for details. */
  SpeIaea,

  /** See #SpecFile::write_cnf for details. */
  Cnf,
  
#if( SpecUtils_ENABLE_D3_CHART )
  /** See #SpecFile::write_d3_html for details. */
  HtmlD3,
#endif
  
  NumTypes
};//enum SaveSpectrumAsType



/** Enum to indentify the detection system used to create data in a spectrum
 file.
 
 May be infered from spectrum file format or from comments or information within
 the spectrum file.
 
 It is currently known to not be comprehensive (e.g., some models not include in
 this list, or some models not identified from all possible formats of spectrum
 files the detection system can produce, or some models lumped together) for all
 spectrum files known to be openable by this library.
*/
enum class DetectorType : int
{
  /** GR130 or GR135 v1 or v2 systems. */
  Exploranium,
  
  /** First gen identiFINDER with smaller crystal than NGs; note sometimes
      called identiFINDER-N.
   */
  IdentiFinder,
  
  /** Used for both the NG and NGH since same crystal size (NGH has neutron
      tube)
  */
  IdentiFinderNG,
  
  IdentiFinderLaBr3,
  
  /** The DetectiveUnknown is a default for when the type of detective cant be
      determined, not an actual detector type.
   */
  DetectiveUnknown,
  
  /** Doesnt consider the difference between the EX and DX series; the DX are
   same gamma crystal, but do not have a neutron detector.  Same thing for
   100/200 series enums.
   */
  DetectiveEx,
  
  DetectiveEx100,
  
  /** There are a number of variants, a self contained model, a portal, etc */
  DetectiveEx200,
  
  DetectiveX,
  
  /** only identified from N42 files */
  SAIC8,
  Falcon5000,
  MicroDetective,
  MicroRaider,
  RadHunterNaI,
  RadHunterLaBr3,
  Rsi701,
  Rsi705,
  /** Unspecified RSI/Avid system, usually model is stated as RS??? */
  AvidRsi,
  OrtecRadEagleNai,
  OrtecRadEagleCeBr2Inch,
  OrtecRadEagleCeBr3Inch,
  OrtecRadEagleLaBr,
  /** The LaBr3 may not always be detector, and then it will be assigned
      kSame940
   */
  Sam940LaBr3,
  Sam940,
  Sam945,
  Srpm210,
  //RadSeekerLaBr1.5x1.5
  //RadSeekerNaI2x2 (although should check for this, see SpecFile::set_n42_2006_instrument_info_node_info
  
  Unknown
};//enum DetectorType


enum class OccupancyStatus : int
{
  //Reported occupancy status; not be applicable to all systems/formats, in
  //  which case is marked to Unknown.
  //
  //ToDo: now that this is an enum class should cleanup Unknown
  NotOccupied, Occupied, Unknown
};
  
  
  
/**
 */
enum class SourceType : int
{
  //Reported source type for a record; marked as Unknown unless
  //  file format explicitly specifies, or can reasonably be inferred.
  IntrinsicActivity, Calibration, Background, Foreground, Unknown
};

  
enum class QualityStatus : int
{
  //The detector status reported in the file; not applicable to all formats,
  //  in which case should be marked as Missing, although some formats
  //  (notable N42 and MPS) default to Good.
  Good, Suspect, Bad, Missing
};
  

  
// \TODO: move #SpectrumType definition and related function out of this file; is used for D3 and InterSpec plotting
//        only, so no need to have here.
enum class SpectrumType : int
{
  Foreground,
  SecondForeground,
  Background
};//enum SpecUtils::SpectrumType


const char *descriptionText( const SpecUtils::SpectrumType type );
  
//spectrumTypeFromDescription(..): the inverse of descriptionText(SpectrumType)
//  throw runtiem_exception if doesnt match
SpectrumType spectrumTypeFromDescription( const char *descrip );


//suggestedNameEnding(): returns suggested lowercase file name ending for type
//  passed in.  Does not contain the leading '.' for extentions
const char *suggestedNameEnding( const SaveSpectrumAsType type );
  
  
const char *descriptionText( const SaveSpectrumAsType type );

  
//Forward declarations within SpecUtils
enum class EnergyCalType : int;
  
class SpecFile;
class Measurement;

class DetectorAnalysis;
struct EnergyCalibration;
struct N42DecodeHelper2006;
struct N42DecodeHelper2012;
struct MeasurementCalibInfo; //defined in SpectrumDataStructs.cpp (used for parsing N42 2006/2012 files and rebinning)
struct GrossCountNodeDecodeWorker;
  
/** Checks the first 512 bytes of data for a few magic strings that *should* be
   in N42 files; if it contains any of them, it returns true
 
 @TODO move this function, and similar ones to a N42 utils header/source
*/
bool is_candidate_n42_file( const char * data );
  
/** Same as other version of this function, but input does not need to be null
   terminated.

  @TODO move this function, and similar ones to a N42 utils header/source
*/
bool is_candidate_n42_file( const char * const data, const char * const data_end );
  
/** Checks if the input data might be a N42 file, and if it might be UTF16
   instead of UTF8, and if so, uses a very niave/horrible approach of just
   eliminating '\0' bytes from the input data.  Returns new data_end (or if no
   changes, the one passed in)
 
 @TODO move this function, and similar ones to a N42 utils header/source
 */
char *convert_n42_utf16_xml_to_utf8( char * data, char * const data_end );
  
  
/** @TODO move this function, and similar ones to a N42 utils header/source
 
 */
void add_analysis_results_to_2012_N42( const DetectorAnalysis &ana,
                                        ::rapidxml::xml_node<char> *RadInstrumentData,
                                        std::mutex &xmldocmutex );
  
/** adds to analysis the information in AnalysisResults node.  Currently only
   adds the NuclideAnalysis to for a select few number of detectors.
 
  @TODO move this function, and similar ones to a N42 utils header/source
*/
void set_analysis_info_from_n42( const rapidxml::xml_node<char> *analysis_node,
                              DetectorAnalysis &analysis );


//gamma_integral(): get the integral of gamma counts between lowenergy and
//  upperunergy; linear approximation is used for fractions of channels.
double gamma_integral( const std::shared_ptr<const Measurement> &hist,
                       const float lowenergy, const float upperunergy );


//detectorTypeToString(): returns string which cooresponds to the convention
//  InterSpec is using to represent detector response functions on disk.
const std::string &detectorTypeToString( const DetectorType type );




 
  
  
  
class Measurement
{
public:
  Measurement();
  
  //operator=: operates as expected for most member variables.  Smart pointer
  //  to const objects (channel data and channel energies) are shallow copied.
  const Measurement &operator=( const Measurement &rhs );
  
  //memmorysize(): calculates the approximate amount of memorry this Measurement
  //  is taking up in memmory, including all of the objects which it owns (like
  //  pointers to float arrays and stuff).
  size_t memmorysize() const;

  //Simple accessor functions (cheap to call):
  
  //live_time(): returned in units of seconds.  Will be 0 if not known.
  float live_time() const;
  
  //real_time(): returned in units of seconds.  Will be 0 if not known.
  float real_time() const;
  
  //contained_neutron(): returns whether or not the measurement is thought to
  //  contain the possibility to detect neutrons (e.g. if a neutron detector was
  //  also present).  This may be true even if zero neutrons were detected.  For
  //  some detector types this value is infered through previous hardware
  //  knowledge.
  bool contained_neutron() const;
  
  //sample_number(): the sample number assigned to this Measurement.  If the
  //  'DontChangeOrReorderSamples' flag wasnt passed to the
  //  SpecFile::cleanup_after_load() function, then this value may be
  //  assigned during file parsing.
  int sample_number() const;
  
  //title(): some formats such as .PCF or .DAT files will contain a title for
  //  the spectrum, or this may be set through set_title(...).
  const std::string &title() const;
  
  //occupied(): returns the occupancy status.  Detectors which do not contain
  //  this capability will return 'Unknown'
  OccupancyStatus occupied() const;
  
  //gamma_count_sum(): returns the sum of channel data counts for gamma data.
  double gamma_count_sum() const;
  
  //neutron_counts_sum(): returns the sum of neutron counts.
  double neutron_counts_sum() const;
  
  //speed(): returns the speed of the vehicle, object or detector, in m/s if
  //  known.  Otherwise return 0.0.
  float speed() const;
  
  //latitude(): returns the latitude of the measurement, in degrees, if known.
  //  Returns -999.9 otherwise.
  double latitude() const;
  
  //longitude(): returns the longitude, in degrees, of the measurement if known.
  //  Returns -999.9 otherwise.
  double longitude() const;
  
  //has_gps_info(): returns true only if both latitude and longitude are valid.
  bool has_gps_info() const;
  
  
  //position_time(): returns the (local, or detector) time of the GPS fix, if
  //  known.  Returns boost::posix_time::not_a_date_time otherwise.
  const boost::posix_time::ptime &position_time() const;
  
  //detector_name(): returns the name of the detector within the device.
  //  May be empty string for single detector systems, or otherwise.
  //  ex: Aa1, Ba1, etc.
  const std::string &detector_name() const;
  
  //detector_number(): returns the detector number of the detector within the
  //  detection system.  Will have a 1 to 1 coorespondence with detector_name().
  int detector_number() const;
  
  //detector_type():  If the file specifies the detector type string, it _may_
  //  be retrieved here.  Note there is not much consistency between file
  //  formats in what you should expect to get from this function.
  //  e.x. "HPGe 50%", "NaI"
  const std::string &detector_type() const;
  
  //quality_status():  If not specified in file, will have value of 'Missing'.
  QualityStatus quality_status() const;
  
  //source_type():  Returns the source type if known.  For some formats (notably
  //  PCF snd spectroscopic daily files), anything not background will be marked
  //  as foreground (this behaviour may be cahnged in the future).  For other
  //  formats if not known, 'Unknown' is returned.
  SourceType source_type() const;
  
  //remarks(): the list of remarks found while parsing this record from the file
  //  that pertain to this record specifically.  See also
  //  MeasurementInformation::remarks().
  const std::vector<std::string> &remarks() const;
  
  /** Warnings from parsing that apply to this measurement.
   */
  const std::vector<std::string> &parse_warnings() const;
  
  //start_time(): start time of the measurement.  Returns
  //  boost::posix_time::not_a_date_time if could not be determined.
  const boost::posix_time::ptime &start_time() const;

  //start_time_copy(): start time of the measurement.  Returns
  //  boost::posix_time::not_a_date_time if could not be determined.
  const boost::posix_time::ptime start_time_copy() const;
  
  //energy_calibration_model(): returns calibration model used for energy
  //  binning.  If a value of 'InvalidEquationType' is returned, then
  //  channel_energies() may or may not return a valid pointer; otherwise, if
  //  this Measurement is part of a MeasurementInfo object constructed by parsing
  //  a file, then channel_energies() pointer _should_ be valid.
  //
  //  \deprecated Please start using #EnergyCalibration returned by #energy_calibration.
  SpecUtils::EnergyCalType energy_calibration_model() const;
  
  //calibration_coeffs(): returns the energy calibration coeificients.
  //  Returned vector should have at least two elements for Polynomial and
  //  FullRangeFraction energy calibration models.  Polynomial may have an
  //  arbitrary number of coefficients, while FullRangeFraction may have up to
  //  five.  For LowerChannelEdge calibration model the returned vector will
  //  most likely be empty (a memory optimization, may be changed in the future)
  //  so you should instead call channel_energies().
  //
  //  \deprecated Please start using #EnergyCalibration returned by #energy_calibration.
  const std::vector<float> &calibration_coeffs() const;
  
  //deviation_pairs(): returns the energy deviation pairs.  Sometimes also
  // refered to as nonlinear deviation pairs.
  //  TODO: insert description of how to actually use these.
  //
  // \deprecated Please start using #EnergyCalibration returned by #energy_calibration.
  const std::vector<std::pair<float,float>> &deviation_pairs() const;
  
  /** Returns the energy calibration. Will not be null. */
  std::shared_ptr<const EnergyCalibration> energy_calibration() const;
  
  //channel_energies(): returns a vector containing the starting (lower) energy
  //  of the gamma channels, calculated using the energy calibration
  //  coefficients as well as the deviation pairs.  These channel energies are
  //  calculated during file parsing, or any subsequent re-calibrations;  the
  //  owining MeasurementInfo object will make an attempt so that multiple
  //  Measurements that it owns, that have the same calibration, will also return
  //  pointers from channel_energies() that point to the same spot in memory
  //  (this is primarily a memory usage optimization).
  //  Typically the vector returned by channel_energies() will have the same
  //  number of channels as gamma_counts(), however for most LowerChannelEdge
  //  calibration model files, channel_energies() may have 1 more channels (to
  //  indicate end of last channel energy).
  //  Returned pointer may be null if energy calibration is unknown/unspecified.
  //
  // \deprecated Please start using #EnergyCalibration returned by #energy_calibration.
  const std::shared_ptr< const std::vector<float> > &channel_energies() const;
  
  //gamma_counts(): the channel counts of the gamma data.
  //  Returned pointer may be null if no gamma data present, or not thie
  //  Measurement is not properly initialized.
  const std::shared_ptr< const std::vector<float> > &gamma_counts() const;
  
  //neutron_counts(): the channel counts of neutron data.  Currently none of
  //  the file formats give channelized neutron data, so this function may
  //  be removed in the future; use neutron_counts_sum() instead.  Currently
  //  returned vector will have a size 1 if the file contained neutron counts.
  const std::vector<float> &neutron_counts() const;

  
  /** Sets the title property.
   
   Some file formats like PCF, CNF, and CHN support something like a title, but
   not all formats do.
   If written to an N42-2012 file the title will be written as a remark,
   prepended with the text "Title: " (this is looked for when reading N42-2012
   files in).
   */
  void set_title( const std::string &title );
  
  /** Set start time of this measurement. */
  void set_start_time( const boost::posix_time::ptime &timestamp );
  
  /** Set the remarks of this measurement; any previous remarks are removed. */
  void set_remarks( const std::vector<std::string> &remar );
  
  /** Set the source type of this measurement; default is #SourceType::Unknown.
   */
  void set_source_type( const SourceType type );

  /** Set the sample number of this measurement; default is 1. */
  void set_sample_number( const int samplenum );
  
  /** Set the occupancy status for this measurement; default is
   #OccupancyStatus::Unknown
   */
  void set_occupancy_status( const OccupancyStatus status );
  
  /** Set the detector name for this measurement; default is an empty string.
   
   Note: you may also wish to set detector number.
   */
  void set_detector_name( const std::string &name );
  
  /**  Set the detector number of this Measurement.
   
   Note: detector number is used by the SpecFile class in some places, but since
   it is essentially duplicate information to the detector name, it may be
   removed at some point in the future, so please use detector name instead.
   
   @deprecated
   */
  void set_detector_number( const int detnum );
  
  
  /** Set real and live times, as well as gamma counts.
   
   If the number of channels is not compatible with the old energy calibration (i.e., different
   number of channels), the energy calibration will be reset to an unknown state, which you
   can then fix by calling #set_energy_calibration.
   
   Updates gamma counts sum as well.
   
   @param counts The gamma channel counts to use; a copy is not made, but the
          actual vector pointed to is used - so you should be-careful about
          modifying it as then the sum gamma counts will be out of date.
          Currently if nullptr, and #gamma_counts_ is not a nullptr, then
          #gamma_counts_ will be set to a new vector of the previous size, but
          with all 0.0f entries (this behavior will be changed in the future).
   @param livetime The live time (i.e., real time minus dead time), in seconds,
          corresponding to the channel counts.
   @param realtime The real time (i.e., as measured on a clock), in seconds,
          corresponding to the channel counts.
   */
  void set_gamma_counts( std::shared_ptr<const std::vector<float>> counts,
                                const float livetime, const float realtime );
  
  /** Sets the neutron counts, and also updates
   #Measurement::neutron_counts_sum_ and #Measurement::contained_neutron_ .
   
   If this vector of counts passed in is empty, will mark
   #Measurement::contained_neutron_ as false, or else this variable will be
   marked true, even of all entries in 'counts' are zero.
   
   Each element in the vector corresponds to a different detection element.  So
   commonly if multiple He3 tubes are read out separately but paired with one
   gamma detector, the passed in vector will have one element for each of the
   He3 tubes.  Most handheld detection systems have a single neutron detector
   that is read out, so the passed in counts would have a size of one.
   */
  void set_neutron_counts( const std::vector<float> &counts );
  
  //To set real and live times, see SpecFile::set_live_time(...)
  
  /** returns the number of channels in #gamma_counts_.
   Note: energy calibration could still be invalid and not have channel energies defined, even
   when this returns a non-zero value.
   */
  size_t num_gamma_channels() const;
  
  /** Returns gamma channel containing 'energy'.
   If 'energy' is below the zeroth channel energy, 0 is returned; if 'energy' is above last channels
   energy, the index for the last channel is returned.
   
   Throws exception if energy calibration is not defined.
   */
  size_t find_gamma_channel( const float energy ) const;
  
  //gamma_channel_content(): returns the gamma channel contents for the
  //  specified channel.  Returns 0 if this->gamma_counts_ is not defined, or
  //  if specified channel is invalid (to large).
  float gamma_channel_content( const size_t channel ) const;
  
  //gamma_channel_lower(): returns the lower energy of the specified gamma
  //  channel.
  //Throws exception if invalid channel number, or no valid energy calibration.
  float gamma_channel_lower( const size_t channel ) const;
  
  //gamma_channel_center(): returns the central energy of the specified
  //  channel.  For the last channel, it is assumed its width is the same as
  //  the second to last channel.
  //Throws exception if invalid channel number, or no valid energy calibration.
  float gamma_channel_center( const size_t channel ) const;

  //gamma_channel_upper(): returns the energy just past the energy range the
  //  specified channel contains (e.g. the lower edge of the next bin).  For the
  //  last bin, the bin width is assumed to be the same as the previous bin.
  //Throws exception if invalid channel number, or no valid energy calibration.
  float gamma_channel_upper( const size_t channel ) const;

  //gamma_channel_width(): returns the energy width of the specified channel.
  //  For the last channel, it is assumed its width is the same as the second
  //  to last channel.
  //Throws exception if invalid channel number, or no valid energy calibration.
  float gamma_channel_width( const size_t channel ) const;
  
  //gamma_integral(): get the integral of gamma counts between lower_energy and
  //  upper_energy; linear approximation is used for fractions of channels.
  //If no valid energy calibration or this->gamma_counts_ is invalid, 0.0 is
  //  returned.
  double gamma_integral( float lower_energy, float upper_energy ) const;
  
  //gamma_channels_sum(): get the sum of gamma gamma channel contents for all
  //  channels, both including and inbetween, 'startbin' and 'endbin'.
  //  If 'startbin' is invalid (to large), 0.0 is returned, regardless of value
  //  of 'endbin'.
  //  If 'endbin' is to large, then it will be clamped to number of channels.
  //  'startbin' and 'endbin' will be swapped if endbin < startbin.
  //  Returns 0 if this->gamma_counts_ is invalid (doesnt throw).
  double gamma_channels_sum( size_t startbin, size_t endbin ) const;
  
  /** Gives the lower energy of each gamma channel.
   
   @returns channel energies, the lower edge energies of the gamma channels.  May be nullptr.
            See notes for #EnergyCalibration::channel_energies
  
  The exact same as channel_energies(), just renamed to be consistent with above accessors.
   */
  const std::shared_ptr< const std::vector<float> > &gamma_channel_energies() const;
  
  //gamma_channel_contents(): returns gamma_counts_, the gamma channel data
  //  (counts in each energy bin).
  //  The returned shared pointer may be null.
  //  The exact same as gamma_counts(), just renamed to be consistent with
  //  above accessors.
  const std::shared_ptr< const std::vector<float> > &gamma_channel_contents() const;
  
  float gamma_energy_min() const;
  float gamma_energy_max() const;
  
  
  //Functions to write this Measurement object out to external formats
  
  //write_2006_N42_xml(...): similar schema as Cambio uses, but with some added
  //  tags
  bool write_2006_N42_xml( std::ostream& ostr ) const;
  
  //write_csv(...): kinda similar to Cambio, except multiple spectra in 1 files
  bool write_csv( std::ostream& ostr ) const;
  
  //write_txt(...): kinda similar to Cambio, but contians most possible info
  bool write_txt( std::ostream& ostr ) const;

  
  //Shared pointers (such as gamma_counts_) are reset in the following function,
  //  meaning that shared_ptr's outside of this object will not be effected
  //  by reset() or set(...)
  void reset();

  
  //combine_gamma_channels(): combines every 'nchann' channel gamma channels
  //  together.  Will throw exception if (gamma_counts_->size() % nchann) != 0.
  //  If gamma_counts_ is undefined or empty, nothing will be done.
  void combine_gamma_channels( const size_t nchann );
  
  //truncate_gamma_channels(): removes channels below 'keep_first_channel'
  //  and above 'keep_last_channel'.  If 'keep_under_over_flow' is true, then
  //  adds the removed gamma counts to the first/last remaining bin.
  //Throws exception if keep_first_channel>=keep_last_channel, or if
  //  keep_last_channel>=num_gamma_channels()
  void truncate_gamma_channels( const size_t keep_first_channel,
                                const size_t keep_last_channel,
                                const bool keep_under_over_flow );
  
  /** Rebin the gamma_spectrum to match the passed in #EnergyCalibration.
   
   Spectrum features (i.e., peaks, compton edges, etc) will stay at the same energy, but the channel
   energy definitions will be changed, and coorispondingly the counts in each channel (i.e.
   #gamma_counts_) will be changed.  This will lead to channels having non-integer number of counts,
   and information being lost (e.g., doing a rebin followed by another rebin back to original energy
   calibration will result the channel counts probably being notably different than originaly), so
   use this function sparingly, and dont call multiple times.
   
   Both the current (and soon to be previous) and passed in EnergyCalibration are valid with
   #EnergyCalibration::channel_energies fields valid with size at least four, or an
   exception will be thrown.  The current (soon to be previous) energy calibration will always
   satisfy these requirements if it was parsed from a file by this library, and this #Measurement
   has gamma counts.
   
   After a succesful call to this function, #calibration will return the same value as passed into
   this function.
   */
  void rebin( const std::shared_ptr<const EnergyCalibration> &cal );
  
  
  //recalibrate_by_eqn(...) passing in a valid binning
  //  std::shared_ptr<const std::vector<float>> object will save recomputing this quantity, however no
  //  checks are performed to make sure 'eqn' actually cooresponds to 'binning'
  //  For type==LowerChannelEdge, eqn should coorespond to the energies of the
  //  lower edges of the bin.
  //Throws exception if 'type' is InvalidEquationType, or potentially (but not
  //  necassirily) if input is ill-specified, or if the passed in binning doesnt
  //  have the proper number of channels.
/*
  void recalibrate_by_eqn( const std::vector<float> &eqn,
                          const std::vector<std::pair<float,float>> &dev_pairs,
                          SpecUtils::EnergyCalType type,
                          std::shared_ptr<const std::vector<float>> binning
#if( !SpecUtils_JAVA_SWIG )
                          = std::shared_ptr<const std::vector<float>>()  //todo: fix this more better
#endif
                          );
  */
  
  /** Sets the energy calibration to the passed in value.
   
   This operation does not change #gamma_counts_, but instead changes the energie bounds for the
   gamma channels.
   
   Throws exception if nullptr is passed in, number of gamma channels dont match, or #gamma_counts_
   is null or empty.  If calibration type is #EnergyCalType::LowerChannelEdge the passed in channel
   energies must have at least, and any amount more, entries than #gamma_counts_.
   
   You should call #set_gamma_counts before this function if assembling a #Measurement not parsed
   from a file.
   */
  void set_energy_calibration( const std::shared_ptr<const EnergyCalibration> &cal );
  

#if( PERFORM_DEVELOPER_CHECKS )
  //equal_enough(...): tests whether the passed in Measurement objects are
  //  equal, for most intents and purposes.  Allows some small numerical
  //  rounding to occur.
  //Throws an std::exception with a brief explanaition when an issue is found.
  static void equal_enough( const Measurement &lhs, const Measurement &rhs );
#endif
  
  /** Sets information contained by the N42-2006 <Spectrum> node to this Measurement.
   
   Throws exception on error.
   
   \deprecated Will be removed; currently only used from InterSpec to de-serialize a peaks continuum
               when it is defined using a spectrum.
   */
  void set_info_from_2006_N42_spectrum_node( const rapidxml::xml_node<char> * const spectrum );
  
protected: 
  //set_info_from_txt_or_csv(...): throws upon failure
  //  XXX - currently doesnt make use of all the information written out by
  //         write_txt(...)
  //  XXX - could be made more rebust to parse more inputs
  //  XXX - currently very CPU innefiecient
  //  XXX - should be documented data formats it accepts
  void set_info_from_txt_or_csv( std::istream &istr );
  
  //set_info_from_avid_mobile_txt(): throws upon failure.
  //  Called from set_info_from_txt_or_csv().
  void set_info_from_avid_mobile_txt( std::istream &istr );
  
  void set_n42_2006_count_dose_data_info( const rapidxml::xml_node<char> *dose_data,
                                std::shared_ptr<DetectorAnalysis> analysis_info,
                                std::mutex *analysis_mutex );
  
  //set_gross_count_node_info(...): throws exception on error
  //  XXX - only implements nuetron gross gounts right now
  void set_n42_2006_gross_count_node_info(
                                 const rapidxml::xml_node<char> *gross_count_measu_node );

  
  
protected:
  
  //live_time_: in units of seconds.  Typially 0.0f if not specified.
  float live_time_;
  
  //real_time_: in units of seconds.  Typially 0.0f if not specified.
  float real_time_;

  //contained_neutron_: used to specify if there was a neutron detector, but
  //  0 counts were actually detected.
  bool contained_neutron_;

  /** Sample number of this #Measurement.
   
   The combination of detector name and sample number will uniquely identify a #Measurement within
   a #SpecFile.
   
   Sample numbers of Measurements in a #SpecFile typically starts at 1 (not zero like in c++), and
   increase by one for each time interval, usually with all detectors sharing a sample number for
   measurements taken during a common time period.  However, this is by no means a rule; may not
   start at 1 and there may be missing/skipped numbers.  The sample number may either be determined
   from the file as its parsed, or otherwise assigned by #SpecFile::cleanup_after_load.
  */
  int sample_number_;
  
  //occupied_: for portal data indicates if vehicle in RPM.  If non-portal data
  //  then will probably usually be OccupancyStatus::Unknown.
  OccupancyStatus occupied_;
  
  double gamma_count_sum_;
  double neutron_counts_sum_;
  float speed_;  //in m/s
  std::string detector_name_;
  int detector_number_;
  std::string detector_description_;  //e.x. "HPGe 50%". Roughly the equivalent N42 2012 "RadDetectorDescription" node
  //ToDo: add something like:
  //  struct Characteristic{std::string name, value, unit, data_class;};
  //  std::vector<Characteristic> detector_characteristics;
  //(useful for N42-2012, SPC, and a few other formats)
  QualityStatus quality_status_;

  //The following objects are for the energy calibration, note that there are
  //  similar quantities for resolution and such, that arent kept track of
  SourceType     source_type_;
  

  std::vector<std::string>  remarks_;
  std::vector<std::string>  parse_warnings_;
  boost::posix_time::ptime  start_time_;
  
  /// \ToDo: switch from ptime, to std::chrono::time_point
  //std::chrono::time_point<std::chrono::high_resolution_clock,std::chrono::milliseconds> start_timepoint_;
  
  /** Pointer to EnergyCalibration.
   This is a shared pointer to allow many #Measurement objects to share the same energy calibration
   to save memory.
   */
  std::shared_ptr<const EnergyCalibration> energy_calibration_;
  
  
  std::shared_ptr< const std::vector<float> >   gamma_counts_;        //gamma_counts_[energy_channel]
  
  //neutron_counts_[neutron_tube].  I dont think this this is actually used
  //  many places (i.e., almost always have zero or one entry in array).
  std::vector<float>        neutron_counts_;

  //could add Alt, Speed, and Dir here, as well as explicit valid flag
  double latitude_;  //set to -999.9 if not specified
  double longitude_; //set to -999.9 if not specified
//  double elevation_;
  
  boost::posix_time::ptime position_time_;
  
  std::string title_;  //Actually used for a number of file formats

  friend class ::SpecMeas;
  friend class SpecFile;
  friend struct N42DecodeHelper2006;
  friend struct N42DecodeHelper2012;
  friend struct GrossCountNodeDecodeWorker;
};//class Measurement

/*
class CountDose
{
public:
  enum DoseType
  {
    GammaDose,
    NeutronDose,
    TotalDose,
    OtherDoseType
  };//enum DoseType


  DoseType m_doseType;
  std::string m_remark;
  boost::posix_time::ptime m_startTime;
  float m_realTime;
  float m_doseRate;
};//class CountDose
*/


/** Class that represents a spectrum file.
 
 Can be used to parse spectrum file from disk, or to create a file from a sensors measurement and
 write it out to disk.  This class may hold one or more #Measurement class objects that represents
 a spectrum and/or a neutron gross count from a physical sensor for a given time interval.
 
 This class kinda, sorta, roughly transforms input spectrum files to a N42-2012 like representation
 suitable for use as part of user-display, analysis, or other programs.
 
 An important concept is that a spectrum file may contain multiple spectra that may be from
 multiple different physical detectors, and from multiple time periods.  Different physical
 detectors are differentiated by #Measurement::detector_name(), and different time periods are
 differentiated by #Measurement::sample_number(), such that usually all spectra that share a common
 time period will all have the same sample number.  A single #Measurement can be uniquely specified
 by a combination of sample number and detector name.
 
 Example of reading a spectrum file and printing out a partial summary of its information, and then
 saving to a different file format is:
 ```cpp
 #include <iostream>
 #include "SpecUtils/SpecFile.h"
 
 using namespace std;
 using namespace SpecUtils;
 
 int main()
 {
   SpecFile spec;
   const bool loaded = spec.load_file( "/path/to/file.n42", ParserType::Auto );
   if( !loaded )
     return -1;
 
   const set<int> &sample_numbers = spec.sample_numbers();
   const vector<string> &detector_names = spec.detector_names();
 
   for( const int sample_number : sample_numbers )
   {
     for( const string &det_name : detector_names )
     {
       shared_ptr<const Measurement> meas = spec.measurement( sample_number, det_name );
       if( !meas )
         continue;  //Its possible every sample number will have a #Measurement for each detector
       
       cout << "Sample " << sample_number << " Det " << det_name << " has real time "
            << meas->real_time() << "s, and live time: " << meas->live_time() << "s";
       
       if( meas->contained_neutron() )
         cout << " with " << meas->neutron_counts_sum() << " neutrons";
 
       const shared_ptr<const vector<float>> &gamma_counts = meas->gamma_counts();
       if( gamma_counts && gamma_counts->size() )
       {
         cout << " with " << gamma_counts->size() << " gamma channels with "
              << meas->gamma_count_sum() << " total gammas";
         
         const shared_ptr<const vector<float>> &channel_energies = meas->channel_energies();
         if( channel_energies ) //Will be null if input file didnt give a valid energy calibration
         {
           cout << " and energy range " << channel_energies->front() << " to "
                << channel_energies->back() << " keV";
         }//if( we have energy calibration info )
       }//if( we have gamma data )
 
       cout << endl;
     }//for( loop over detectors )
   }//for( loop over sample numbers )
 
   //Write all samples and detectors to a PCF file; we could remove some if we wanted.
   try
   {
     spec.write_to_file( "output.pcf", sample_numbers, detector_names, SaveSpectrumAsType::Pcf );
   }catch( std::exception &e )
   {
     cerr << "Failed to write output file: " << e.what() << endl;
     return -2;
   }//try / catch
 
   return 0;
 }//main()
 ```
 
 */
class SpecFile
{
public:
  SpecFile();
  SpecFile( const SpecFile &rhs );  //calls operator=
  virtual ~SpecFile();
 
  //operator=(...) copies all the 'rhs' information and creates a new set of
  //  Measurement objects so that if you apply changes to *this, it will not
  //  effect the lhs; this is however fairly effieient as the Measurement
  //  objects copy shallow copy of all std::shared_ptr<const std::vector<float>> instances.
  //  Since it is assumed the 'rhs' is in good shape, recalc_total_counts() and
  //  cleanup_after_load() are NOT called.
  const SpecFile &operator=( const SpecFile &rhs );

  //load_file(...): returns true when file is successfully loaded, false
  //  otherwise. Callling this function with parser_type==Auto is
  //  the easiest way to load a spectrum file when you dont know the type of
  //  file.  The file_ending_hint is only used in the case of Auto
  //  and uses the file ending to effect the order of parsers tried, example
  //  values for this mught be: "n24", "pcf", "chn", etc. The entire filename
  //  can be passed in since only the letters after the last period are used
  //  Note: on Windows the filename must be UTF-8, and not whatever the current
  //        codepoint is.
  bool load_file( const std::string &filename,
                  ParserType parser_type,
                  std::string file_ending_hint
#if( !SpecUtils_JAVA_SWIG )
                 = ""  //todo: fix this more better
#endif
                 );

  /** Returns the warnings or issues encountered during file parsing,
   appropriate for displaying to the user, as applicable to the entire file.
   For each Measurement in the file, you should also consult
   #Measurement::parse_warnings.
   
   An example condition when a message might be made is if it is know the
   neutron real time can sometimes not coorespond to the gamma real time.
   */
  const std::vector<std::string> &parse_warnings() const;
  
  //modified(): intended to indicate if object has been modified since last save
  bool modified() const;
  
  //reset_modified(): intended to be called after saving object
  void reset_modified();
  
  //modified_since_decode(): returns if object has been modified since decoding
  bool modified_since_decode() const;

  //reset_modified_since_decode(): intended to be called right after any initial
  //  adjustments following openeing of an object
  void reset_modified_since_decode();

  //simple accessors (no thread locks are aquired)
  float gamma_live_time() const;
  float gamma_real_time() const;
  double gamma_count_sum() const;
  double neutron_counts_sum() const;
  const std::string &filename() const;
  const std::vector<std::string> &detector_names() const;
  const std::vector<int> &detector_numbers() const;
  const std::vector<std::string> &neutron_detector_names() const;
  const std::string &uuid() const;
  const std::vector<std::string> &remarks() const;
  int lane_number() const;
  const std::string &measurement_location_name() const;
  const std::string &inspection() const;
  const std::string &measurement_operator() const;
  const std::set<int> &sample_numbers() const;
  size_t num_measurements() const;
  DetectorType detector_type() const;
  //instrument_type(): From ICD1 Specs InstrumentType can be:
  //   PortalMonitor, SpecPortal, RadionuclideIdentifier,
  //   PersonalRadiationDetector, SurveyMeter, Spectrometer, Other
  const std::string &instrument_type() const;
  const std::string &manufacturer() const;
  const std::string &instrument_model() const;
  const std::string &instrument_id() const;
  std::vector< std::shared_ptr<const Measurement> > measurements() const;
  std::shared_ptr<const Measurement> measurement( size_t num ) const;
  std::shared_ptr<const DetectorAnalysis> detectors_analysis() const;
  bool has_gps_info() const; //mean longitude/latitude are valid gps coords
  double mean_latitude() const;
  double mean_longitude() const;

  //passthrough(): returns true if it looked like this data was from a portal
  //  or search mode data.  Not 100% right always, but pretty close.
  bool passthrough() const;
  
  //simple setters (no thread locks are aquired)
  void set_filename( const std::string &n );
  void set_remarks( const std::vector<std::string> &n );
  void set_uuid( const std::string &n );
  void set_lane_number( const int num );
  void set_measurement_location_name( const std::string &n );
  void set_inspection( const std::string &n );
  void set_instrument_type( const std::string &n );
  void set_detector_type( const DetectorType type );
  void set_manufacturer( const std::string &n );
  void set_instrument_model( const std::string &n );
  void set_instrument_id( const std::string &n );


  //A little more complex setters:
  //set_live_time(...) and set_real_time(...) update both the measurement
  //  you pass in, as well as *this.  If measurement is invalid, or not in
  //  measurements_, than an exception is thrown.
  void set_live_time( const float lt, std::shared_ptr<const Measurement> measurement );
  void set_real_time( const float rt, std::shared_ptr<const Measurement> measurement );

  //set_start_time(...), set_remarks(...), set_spectra_type(...) allow
  //  setting the relevant variables of the 'measurement' passed in.  The reason
  //  you have to set these variables from MeasurementInfo class, instead of
  //  directly from the Measurement class is because you should only be dealing
  //  with const pointers to these object for both the sake of the modified_
  //  flag, but also to ensure some amount of thread safety.
  void set_start_time( const boost::posix_time::ptime &timestamp,
                       const std::shared_ptr<const Measurement> measurement  );
  void set_remarks( const std::vector<std::string> &remarks,
                    const std::shared_ptr<const Measurement> measurement  );
  void set_source_type( const SourceType type,
                         const std::shared_ptr<const Measurement> measurement );
  void set_position( double longitude, double latitude,
                     boost::posix_time::ptime position_time,
                     const std::shared_ptr<const Measurement> measurement );
  void set_title( const std::string &title,
                  const std::shared_ptr<const Measurement> measurement );
  
  //set_contained_neutrons(...): sets the specified measurement as either having
  //  contained neutron counts, or not.  If specified to be false, then counts
  //  is ignored.  If true, then the neutron sum counts is set to be as
  //  specified.
  void set_contained_neutrons( const bool contained, const float counts,
                               const std::shared_ptr<const Measurement> measurement );

  /** Sets the detectors analysis.
   
   If the passed in analysis is empty (e.g., default DetectorAnalysis) then the
   internal analysis will be made null (e.g., answer returned by
   #detectors_analysis will be nullptr); else a deep copy of the analysis passed
   in is made.
   */
  void set_detectors_analysis( const DetectorAnalysis &ana );
  
  /** Changes the detector, both as returned by #detector_names, and for each
      Measurement.
   
   @param original_name The original name of the detector.  Throws exception if
          a detector with this name did not exist.
   @param new_name The new name of the detector.  Throws exception if a detector
          with this name already exists.
   */
  void change_detector_name( const std::string &original_name,
                             const std::string &new_name );
  
  //add_measurement(...): adds the measurement to this MeasurementInfo object and
  //  if 'doCleanup' is specified, then all sums will be recalculated, and
  //  binnings made consistent.  If you do not specify 'doCleanup' then
  //  things will be roughly updated, but the more thorough cleanup_after_load()
  //  is not called.
  //Will throw if 'meas' is already in this SpecFile object, otherwise
  //  will ensure it has a unique sample number/ detector number combination
  //  (which means 'meas' may be modified).  Note that if a sample number
  //  needs to be assigned, it is chosen to be the very last available sample
  //  number available if that detector does not already have that one or else
  //  its assigned to be one larger sample number - this by no means captures
  //  all use cases, but good enough for now.
  void add_measurement( std::shared_ptr<Measurement> meas, const bool doCleanup );
  
  //remove_measurement(...): removes the measurement from this MeasurementInfo
  //  object and if 'doCleanup' is specified, then all sums will be
  //  recalculated.  If you do not specify 'doCleanup' then make sure to call
  //  cleanup_after_load() once you are done adding/removing Measurements if a
  //  rough fix up isnt good enough.
  //Will throw if 'meas' isnt currently in this SpecFile.
  void remove_measurement( std::shared_ptr<const Measurement> meas, const bool doCleanup );
  
  //remove_measurements(): similar to remove_measurement(...), but more efficient
  //  for removing large numbers of measurements.  This function assumes
  //  the internal state of this MeasurementInfo object is consistent
  //  (e.g. no measurements have been added or removed without 'cleaningup').
  void remove_measurements( const std::vector<std::shared_ptr<const Measurement>> &meas );
  
  /** Combines the specified number of gamma channels together for all measurements with the given
   number of channels.
   
   @param ncombine The number of channels to combine.
   @param nchannels Only #Measurements with this number of channels will have their channels
          combined.
   
   Throws exception if( (nchannels % ncombine) != 0 ).
   Throws exception if gamma calibration becomes invalid - which probably shouldnt ever happen,
   except for maybe real edge cases with crazy deviation pairs.
  */
  size_t combine_gamma_channels( const size_t ncombine, const size_t nchannels );
  
  //combine_gamma_channels(): calls equivalent function on non-const version of
  //  'm'. See Measurement::combine_gamma_channels() for comments.
  //Throws exception if 'm' isnt owned by *this.
  void combine_gamma_channels( const size_t ncombine,
                               const std::shared_ptr<const Measurement> &m );
  
  /** Removes all channels below 'keep_first_channel' and above 'keep_last_channel', for every
   measurement that has 'nchannels'.
   If keep_under_over_flow is true, then removed channel counts will be added to the first/last
   channel of the remaing data.
   
   @returns number of modified Measurements.
  
   Throws exception if keep_last_channel>=nchannels, or if keep_first_channel>=keep_last_channel,
   or if the energy calibration becomes invalid (which I dont *think* should happen, except maybe
   very rare edgecases).
   */
  size_t truncate_gamma_channels( const size_t keep_first_channel,
                                  const size_t keep_last_channel,
                                  const size_t nchannels,
                                  const bool keep_under_over_flow );
  
  //truncate_gamma_channels(): removes all channels below 'keep_first_channel'
  //  and above 'keep_last_channel', for specified measurement.
  //  If keep_under_over_flow is true, then removed channel counts will be added
  //  to the first/last channel of the remaing data.
  //Throws exception if invalid Measurement, or if
  //  keep_last_channel>=m->num_gamma_channels(), or if
  //  keep_first_channel>=keep_last_channel.
  void truncate_gamma_channels( const size_t keep_first_channel,
                                const size_t keep_last_channel,
                                const bool keep_under_over_flow,
                                const std::shared_ptr<const Measurement> &m );
  
  
  //Not so simple accessors
  //occupancy_number_from_remarks(): tries to find the occupancy number from
  //  remarks_.
  //  returns -1 if one cannot be found.
  //  Currently only succeds if a remark starts with the string
  //    "Occupancy number = ", where this should be improved in the future
  //  In Principle there should be an EventNumber under the Event
  //  tag in ICD1 files, but I dont havea any exammples of this, so implementing
  //  EventNumber will have to wait
  int occupancy_number_from_remarks() const;

  //get all measurements cooresponding to 'sample_number', where
  //  sample_number may not be a zero based index (eg may start at one)
  std::vector< std::shared_ptr<const Measurement> > sample_measurements(
                                                const int sample_number ) const;

  std::shared_ptr<const Measurement> measurement( const int sample_number,
                                           const std::string &det_name ) const;
  std::shared_ptr<const Measurement> measurement( const int sample_number,
                                           const int detector_number ) const;

  
  /** For a given set of sample numbers and detector names, the #Measurement may have different
   energy calibrations, number of channels, and/or energy ranges, meaning if you want to display
   the summed data, you will most likely want to pick a common energy calibration to rebin to
   for a reasonable display.
   
   This function attempts to provide the best EnergyCalibration object, from the indicated set of
   samples and detectors to use to sum to.
   
   Currently, this function chooses the Measurement with the largest number of gamma channels.
   
   @param sample_numbers The sample numbers to consider; if empty, will return nullptr.
   @param detector_names The detectors to consider; if empty, will return nullptr.
   @returns pointer to suggested #EnergyCalibration object. Will be nullptr if no suitable energy
            calibration is found (e.x., only neutron measurement are found, or no valid energy
            calibration is found).
   
   Throws exception if any sample_number or detector_names entries is invalid.
   */
  std::shared_ptr<const EnergyCalibration> suggested_sum_energy_calibration(
                                            const std::set<int> &sample_numbers,
                                            const std::vector<std::string> &detector_names ) const;
  

  /** Sum the gamma spectra, and neutron counts for the specified samples and detectors.
   
   @param sample_numbers The sample numbers to include in the sum.  If empty, will return nullptr.
                         If any sample numbers are invalid numbers, will throw exception.
   @param detector_names The names of detectors to include in the sum.  If empty, will return
                         nullptr.  If any names are invalid, will throw exception.
   @param energy_cal The energy calibration the result will use.  If nullptr, the energy calibration
                     suggested by #suggested_sum_energy_calibration will be used, and if there are
                     no valid calibrations, will return nullptr.  If EnergyCalibration::type() is
                     #EnergyCalType::InvalidEquationType, will throw exception.
   @returns a summed #Measurement of all the sample and detectors.  If no appropriate gamma spectra
            (i.e., have valid energy calibration and 4 or more channels) are included with the sum,
            will return nullptr.  I.e., if not null, returned #Measurement will have a valid gamma
            spectrum and energy calibration.
   
   Throws exception if invalid inputs are provided.
   Returns nullptr if no gamma spectra to sum are found.
   */
  std::shared_ptr<Measurement> sum_measurements( const std::set<int> &sample_numbers,
                                  const std::vector<std::string> &detector_names,
                                  std::shared_ptr<const EnergyCalibration> energy_cal ) const;
  
  
  
  //memmorysize(): should be reasonbly accurate, but could definetly be off by a
  //  little bit.  Tries to take into account the shared float vectors may be
  //  shared between Measurement objects.
  size_t memmorysize() const; //in bytes

  //gamma_channel_counts(): loops over the Measurements and returns a set<size_t>
  //  containing all the Measurement::num_gamma_channels() results
  std::set<size_t> gamma_channel_counts() const;

  //num_gamma_channels(): loops over the Measurements, and returns the size of
  //  the first Measurement that reports non-zero channels.
  size_t num_gamma_channels() const;

  //keep_n_bin_spectra_only(..): return number of removed spectra
  //  doesnt remove neutron detectors
  size_t keep_n_bin_spectra_only( size_t nbin );

  /** Returns true if the spectrum file contained data from a neutron detector.
   
   Calls Measurement::contained_neutron() for each measurement until one turns
   true, or all measurements have been queried, so may not be a cheap call.
   */
  bool contained_neutron() const;
  
  
  //energy_cal_variants(): Some N42 files may have the same spectra, binned
  //  differently multiple times (ex. "Lin" and "Sqrt"), and when this is
  //  detected the detector they are assigned to is appended with a prefix
  //  such as "_intercal_LIN"
  std::set<std::string> energy_cal_variants() const;
  
  
  //keep_energy_cal_variant(): When #energy_cal_variants() returns multiple
  //  variants, you can use this function to remove all energy calibration
  //  variants, besides the one you specify, from the measurement.  If a spectrum
  //  is not part of a variant, it is kept.
  //Returns return number of removed spectra.
  //Throws exception if you pass in an invalid variant.
  size_t keep_energy_cal_variant( const std::string variant );
  
  
  //rremove_neutron_measurements() only removes neutron measurements that do not
  //  have a gamma binning defined
  size_t remove_neutron_measurements();

  //background_sample_number() returns numeric_limits<int>::min() if no
  // background is found; behavior undefined for more than one background sample
  int background_sample_number() const;
  
  /** Uses things like gamma and neutron sums, real/live times, number of
   samples, and calibration to generate a pseudo-UUID unique to the measurement
   represented by this data.
   
   Its possible that the same measurement read in by two different formats may
   produce the same UUID (ex. a SPE format and SPC format), but if _all_ the
   fields contained and read from each format are not the same, then different
   values will be produced.
   
   Results kinda conform to the expected UUID v4 format (a random
   UUID) of YYMMDDHH-MMSS-4FFx-axxx-xxxxxxxxxxxx, where {Y,M,D,H,M,S,F}
   relate to the time of first measurement, and the x's are based off of
   hashing the various properties of the spectrum.
   
   Note: this is called from cleanup_after_load() if a UUID doesnt already exist
   in order to generate one.
  */
  std::string generate_psuedo_uuid() const;
  
  
  //reset(): resets all variables to same state as just after construction
  void reset();

  //The following do not throw, but return false on falure, as well as call
  //  reset().  None of these functions are tested very well.
  virtual bool load_N42_file( const std::string &filename );
  bool load_pcf_file( const std::string &filename );
  bool load_spc_file( const std::string &filename );
  bool load_chn_file( const std::string &filename );
  bool load_iaea_file( const std::string &filename );
  bool load_binary_exploranium_file( const std::string &file_name );
  bool load_micro_raider_file( const std::string &filename );
  bool load_txt_or_csv_file( const std::string &filename );
  bool load_cnf_file( const std::string &filename );
  bool load_tracs_mps_file( const std::string &filename );
  bool load_aram_file( const std::string &filename );
  bool load_spectroscopic_daily_file( const std::string &filename );
  bool load_amptek_file( const std::string &filename );
  bool load_ortec_listmode_file( const std::string &filename );
  bool load_lsrm_spe_file( const std::string &filename );
  bool load_tka_file( const std::string &filename );
  bool load_multiact_file( const std::string &filename );
  bool load_phd_file( const std::string &filename );
  bool load_lzs_file( const std::string &filename );
  
  //load_from_N42: loads spectrum from a stream.  If failure, will return false
  //  and set the stream position back to original position.
  virtual bool load_from_N42( std::istream &istr );
 
  //load_N42_from_data(...): raw xml file data - must be 0 terminated
  virtual bool load_N42_from_data( char *data );
  
  /** Load data from raw xml file data specified by data begin and end - does
      not need to be null terminated.
   */
  virtual bool load_N42_from_data( char *data, char *data_end );
  
  //load_from_iaea_spc(...) and load_from_binary_spc(...) reset the
  //  input stream to original position and sets *this to reset state upon
  //  failure
  //  20120910: not well tested yet - only implemented for files with a single
  //  spectrum (I didnt have any multi-spectrum files to view structure)
  bool load_from_iaea_spc( std::istream &input );
  bool load_from_binary_spc( std::istream &input );
  
  //load_from_N42_document(...): loads information from the N42 document, either
  //  2006 or 2012 variants.
  //  May throw.  Returns success status.
  bool load_from_N42_document( const rapidxml::xml_node<char> *document_node );
  
  //load_from_micro_raider_from_data(...): loads information from the Micro
  //  Raider XML format.  This function takes as input the files contents.
  bool load_from_micro_raider_from_data( const char *data );
  
  //load_from_binary_exploranium(...): returns success status; calls reset()
  //  upon falure, and puts istr to original location
  bool load_from_binary_exploranium( std::istream &istr );
  
  //load_from_pcf(...): Set info from GADRAS PCF files.  Returns success
  //  status.  Calls reset() upon failure, and puts istr to original location.
  bool load_from_pcf( std::istream &istr );

  //load_from_txt_or_csv(...): returns state of successfulness, and typically
  //  wont throw.  Resets the object/stream upon failure.
  //  XXX - currently very CPU innefiecient
  //  XXX - should be documented data formats it accepts
  //  XXX - should add some protections to make sure file is not a binary file
  //        (currently this is done in loadTxtOrCsvFile(...) )
  bool load_from_txt_or_csv( std::istream &istr );

  //load_from_Gr135_txt(...): called from load_from_txt_or_csv(...) if it
  //  thinks it could be a GR135 text file
  bool load_from_Gr135_txt( std::istream &istr );
  
  //setInfoFromSpectroscopicDailyFiles(...): file format used to reduce
  //  bandwidth for some portal data; a txt based format
  bool load_from_spectroscopic_daily_file( std::istream &input );
  
  /** Load the CSV format from SRPM210 files */
  bool load_from_srpm210_csv( std::istream &input );
  
  //setInfoFromAmetekMcaFiles(...): asncii file format used by Ametek MCA
  //  devices;
  bool load_from_amptek_mca( std::istream &input );
  
  //load_from_ortec_listmode(...): listmode data from ORTEC digiBASE (digibase-E
  //  and PRO list format not supported yet).
  bool load_from_ortec_listmode( std::istream &input );
  
  /** Load LSRM SPE file. */
  bool load_from_lsrm_spe( std::istream &input );
  
  /** Load TKA file */
  bool load_from_tka( std::istream &input );
  
  /** Load MultiAct SPM file - only barely supported (currently only extracts
   channel counts)
   */
  bool load_from_multiact( std::istream &input );
  
  /** Load from PHD file.
   As of 20191005 only tested on a limited number of files.
   */
  bool load_from_phd( std::istream &input );
  
  /** Load from lzs file format.
   As of 20200131 only tested on a few files.
   */
  bool load_from_lzs( std::istream &input );
  
  //bool load_from_iaea(...): an ASCII format standardized by the IAEA; not all
  //  portions of the standard have been implemented, since they are either
  //  not applicable, or I havent seen an example use of them. Also, only
  //  tested to work with multiple spectra in one file from PeakEasy (didnt
  //  seem like standard explain how it should be done, but I didnt look very
  //  closely).
  //see
  //  http://www.gbs-elektronik.de/old_page/mca/appendi1.pdf
  //  and
  //  http://www.gbs-elektronik.de/fileadmin/download/datasheets/spe-file-format_datasheet.pdf
  //  for specifications of IAEA file standards.
  //This function is computationally slower than it could be, to allow for
  //  a little more diverse set of intput.
  bool load_from_iaea( std::istream &istr );

  //bool load_from_chn(...): Load information from ORTECs binary CHN file.
  bool load_from_chn( std::istream &input );
  
  //bool load_from_cnf(...): loads info from CNF files.  Not
  //  particularly well tested, but seems to work okay
  bool load_from_cnf( std::istream &input );
  bool load_from_tracs_mps( std::istream &input );
  
  bool load_from_aram( std::istream &input );
  
  
  //cleanup_after_load():  Fixes up inconsistent calibrations, binnings and such,
  //  May throw exception on error.
  enum CleanupAfterLoadFlags
  {
    //RebinToCommonBinning: ensures all spectrums in measurements_ have the same
    //  binning.  Will cause the kHasCommonBinning flag to be set in
    //  properties_flags_ (this flag may get set anyway if all same spectrum in
    //  the file).
    RebinToCommonBinning = 0x1,
    
    //DontChangeOrReorderSamples: makes it so cleanup_after_load() wont change
    //  currently assigned sample or detector numbers, or change order of
    //  measurements_.
    DontChangeOrReorderSamples = 0x2,
    
    //Before 20141110 the rebin option was not available (always done), but for
    //  a few non-InterSpec applications, this isnt always desirable, so as a
    //  hack, we will define StandardCleanup, that you can change depending on
    //  the application; currently in the code cleanup_after_load() is always
    //  called without an argument, meaning it will default to StandardCleanup.
    //This is a bit of a hack, but time is limited...
#if( SpecUtils_REBIN_FILES_TO_SINGLE_BINNING )
    StandardCleanup = RebinToCommonBinning
#else
    StandardCleanup = 0x0
#endif
  };//enum CleanupFlags
  
  virtual void cleanup_after_load( const unsigned int flags = StandardCleanup );
  
  //recalc_total_counts(): does NOT recalculate totals for Measurements, just
  //  from them!  Is always called by cleanup_after_load().
  void recalc_total_counts();
  
  
  /** Only call if there are neutron measurements, and no gamma measurements
      contain neutrons.
   */
  void merge_neutron_meas_into_gamma_meas();
  
  
  /** Rebins the given measurement to the specified energy calibration.
   This does not change the energy of spectral features, but does alter the channel counts losing
   information, so use sparingly and really try to not call multiple times.
   For more information see documentation for #Measurement::rebin.
   
   Will throw exception if the measurement is not owned by this SpecFile, or the #EnergyCalibration
   object is invalid.
   */
  void rebin_measurement( const std::shared_ptr<const EnergyCalibration> &cal,
                          const std::shared_ptr<const Measurement> &measurement );
  
  
  /** Rebins all measurements to the passed in energy calibration.
   See notes for #SpecFile::rebin_measurement and #Measurement::rebin.
   */
  void rebin_all_measurements( const std::shared_ptr<const EnergyCalibration> &cal );
  
  
  /** Sets the energy calibration for the specified #Measurement.
   
   This does not change the channel counts (i.e., #gamma_counts_), but does shift the energy of
   spectral features (e.g., peaks, compton edges).
   
   Throws excpetion if energy calibration channel counts are incompatible, or passed in #Measurment
   is not owned by the SpecFile.
   */
  void set_energy_calibration( const std::shared_ptr<const EnergyCalibration> &cal,
                        const std::shared_ptr<const Measurement> &measurement );
  
  /** Sets the energy calibration for the specified sample numbers and detector names.
   
   @param cal The new energy calibration.
   @param sample_numbers The sample numbers to apply calibration to.  If empty will apply to all.
   @param detector_names The detector names to apply calibration to.  If empty will apply to all.
   @returns number of changed #Measurement objects.
   
   Will not set the calibration for any matching #Measurement that does not have a gamma spectrum
   (e.g., will skip that #Measurement, and not throw an exception - so its fine to pass in neutron
   only detector names).
   
   Throws exception if any matching #Measurement has an incompatible (but non-zero) number of gamma
   channels, or if energy calibration is nullptr.
   */
  size_t set_energy_calibration( const std::shared_ptr<const EnergyCalibration> &cal,
                               std::set<int> sample_numbers,
                               std::vector<std::string> detector_names );
  
  
  //If only certain detectors are specified, then those detectors will be
  //  recalibrated (channel contents not changed).  If rebin_other_detectors
  //  is true, then the other detectors will be rebinned (channel contents
  //  changed) so that they have the same calibration as the rest of the
  //  detectors.
  //Will throw exception if an empty set of detectors is passed in, or if none
  //  of the passed in names match any of the available names, since these are
  //  both likely a mistakes
  /*
  void recalibrate_by_eqn( const std::vector<float> &eqn,
                           const std::vector<std::pair<float,float>> &dev_pairs,
                           SpecUtils::EnergyCalType type,
                           const std::vector<std::string> &detectors,
                           const bool rebin_other_detectors );
*/
  
  //Functions to export to various file formats

  //write_to_file(...): Writes the contents of this object to the specified
  //  file in the specified format.  For file formats such as CHN and SPC files
  //  that can only have one record, all Measurement's are summed togoether
  //  and written to the file; for other formats that can have multiple records,
  //  all Measurement will be written to the output file.
  //  Throws exception if an error is encountered.  Will not overwrite  an
  //  existing file.  Assumes no other programs/threads will attempt to access
  //  the same file while this function is being called.
  //  If no exception is thrown the specified file will exist and contain the
  //  relevant information/format.
  //  If an exception is thrown, there are no garuntees as to if the file will
  //  exist, or what its contents will be.
  void write_to_file( const std::string filename,
                      const SaveSpectrumAsType format ) const;
  
  //write_to_file(...): Similar to above write_to_file(...), except only the
  //  specified sample and detector numbers will be written.  When writing to
  //  the output file, if the ouput format supports multiple records, then
  //  each Measurement will be written to its own record.  If the format
  //  only supports a single record, then the specified sample and detector
  //  numbers will be summed together.
  //  Throws exception on error.  Will not overwrite existing file.
  void write_to_file( const std::string filename,
                      const std::set<int> sample_nums,
                      const std::set<int> det_nums,
                      const SaveSpectrumAsType format ) const;

  //write_to_file(...): Similar to above write_to_file(...), except only the
  //  specified sample and detector numbers will be written.  When writing to
  //  the output file, if the ouput format supports multiple records, then
  //  each Measurement will be written to its own record.  If the format
  //  only supports a single record, then the specified sample and detector
  //  numbers will be summed together.
  //  Throws exception on error.  Will not overwrite existing file.
  void write_to_file( const std::string filename,
                      const std::vector<int> sample_nums,
                      const std::vector<int> det_nums,
                      const SaveSpectrumAsType format ) const;
  
  /** Convience function for calling #write_to_file with detector names, instead
   of detector numbers.  If any names are invalid, will throw exception.
   */
  void write_to_file( const std::string &filename,
                     const std::set<int> &sample_nums,
                     const std::vector<std::string> &det_names,
                     const SaveSpectrumAsType format ) const;
  
  //write(...): Wites the specified sample and detector numbers to the provided
  //  stream, in the format specified.  If the output format allows multiple
  //  records, each Measurement will be placed in its own record.  If the output
  //  format only allows a single records, the specified sample and detector
  //  numbers will be summed.
  //
  //  Throws exception on error.
  void write( std::ostream &strm,
              std::set<int> sample_nums,
              const std::set<int> det_nums,
              const SaveSpectrumAsType format ) const;
  
  //write_pcf(...): writes to GADRAS format, using the convention of what looks
  //  to be newer GADRAS files that include "DeviationPairsInFile" information.
  //  Tries to preserve speed, sample number, foreground/background information
  //  into the title if there is room
  bool write_pcf( std::ostream &ostr ) const;
  
  
  //write_2006_N42(...): writes 2006 N42 format, similar to that of Cambio (for
  //  compatibility), but tries to includes other information like GPS
  //  coordinates, speed, occupied, sample number, etc.
  //  XXX - deviation pairs are not tested, and probably not up to standard.
  virtual bool write_2006_N42( std::ostream &ostr ) const;
  
  
  /** The spectra in the current file are written out in a two column
   format (seperated by a comma); the first column is gamma channel
   lower edge energy, the second column is channel counts.  Each
   spectrum in the file are written out contiguously and seperated
   by a header that reads \"Energy, Data\".  Windows style line
   endings are used (\\n\\r).
   This format loses all non-spectral information, including live
   and real times, and is intended to be an easy way to import the
   spectral information into other programs like Excel.
   */
  bool write_csv( std::ostream &ostr ) const;
  
  
  /** Spectrum(s) will be written to an ascii text file.  At the
   beggining of the file the original file name, total live and
   real times, sum gamma counts, sum neutron counts, and any file
   level remarks will be written on seperate labeled lines.
   Then after two blank lines each spectrum in the current file
   will be written, seperated by two blank lines.
   Each spectrum will contain all remarks, measurement start time
   (if valid), live and real times, sample number, detector name,
   detector type, GPS coordinates/time (if valid), serial number
   (if present), energy
   calibration type and coefficient values, and neutron counts (if
   valid); the
   channel number, channel lower energy, and channel counts is then
   provided with each channel being placed on a seperate line and
   each field being seperated by a space.
   Any detector provided analysis in the original program, as well
   manufacturer, UUID, deviation pairs, lane information,
   location name, or spectrum title is lost.
   Cambio or other programs may not be able to read back in all
   information written to the txt file.
   The Windows line ending convention is used (\\n\\r).
   This is not a standard format commonly read by other programs,
   and is intended as a easily human readable summary of the
   spectrum file information
   */
  bool write_txt( std::ostream &ostr ) const;
  
  //write_integer_chn(): Sums over the passed in sample_nums and det_nums to
  //  write a single spectrum with integer count bins in CHN format to the
  //  stream.  If sample_nums and/or det_nums is empty, then all sample and/or
  //  detector numbers are assumed to be wanted; if any invalid values are specified, will throw
  //  excecption.  Values in det_nums coorespond to values in detector_numbers_.
  // This format preserves the gamma spectrum, measurement start time, spectrum
  //  title (up to 63 characters)," detector description, and energy
  //  calibration.
  //  Energy deviation pairs and neutron counts, as well as any other meta
  //  information is not preserved.
  //  Also, wcjohns reverse engineered the file format to begin with, so this
  //  function may not be writing all the information it could to the file.
  bool write_integer_chn( std::ostream &ostr, std::set<int> sample_nums,
                          const std::set<int> &det_nums ) const;
  
  /** Enum to specify the type of binary SPC file to write. */
  enum SpcBinaryType{ IntegerSpcType, FloatSpcType };
  
  /** This format allows a single spectrum in the output, so the sample and 
   detector numbers specified will be summed to produce a single spectrum.
   If sample_nums and/or det_nums is empty, then all sample and/or detector
   numbers are assumed to be wanted.
   This format preserves the gamma spectrum, neutron counts, gps info, 
   measurement start time, detector serial number, and energy calibration (if 
   polynomnial or FWHM).
   Energy deviation pairs, analysis results, and other meta information will be
   lost.
   */
  bool write_binary_spc( std::ostream &ostr,
                         const SpcBinaryType type,
                         std::set<int> sample_nums,
                         const std::set<int> &det_nums ) const;
  
  
  /** This format allows a single spectrum in the output, so the sample and 
      detector numbers specified will be summed to produce a single spectrum.
      If sample_nums and/or det_nums is empty, then all sample and/or detector
      numbers are assumed to be wanted.
      This format preserves the gamma spectrum, neutron counts, gps info,
      measurement start time, detector serial number, and energy calibration (if
      polynomnial or FWHM).
      Energy deviation pairs, some analysis analysis results, and possibly some, 
      but not all, meta information will be lost.
   */
  bool write_ascii_spc( std::ostream &output,
                       std::set<int> sample_nums,
                       const std::set<int> &det_nums ) const;
  
  /** This format allows multiple spectra to be written to the file, however
      information on detector will be lost (records ordered by detector number,
      then by sample number), and spectra will be rebinned to 256 channels, and
      energy calibration information will be lost.  All analysis results,
      meta-information, and neutron counts will be lost.  Channel counts are 
      stored as 16 bit unsigned ints; counts over 32,767 will be wrapped 
      moduloed.
      Currently, if spectra are not a multiple of 256 channels, they will be
      linearized to 256 channels; note that the full range of the spectrum will
      be preserved, although it looks like GR130 spectra should only be ranged
      from 0 to 1.5, or 1 to 3.0 MeV.
   */
  bool write_binary_exploranium_gr130v0( std::ostream &output ) const;
  
  /** This format allows multiple spectra to be written to the file, however
   information on detector will be lost (records ordered by detector number,
   then by sample number), and spectra will be rebinned to 1024 channels, and 
   neutron counts preserved (marked zero if original didnt have any counts).
   Energy calibration will be converted to third order polynomial if polynomial 
   or full width fraction; spectra with energy calibration of by lower energy 
   will be converted to second order polynomial.
   All analysis results and meta-information will be lost.
   Channel counts over 65,535 will be trucated to this value.
   Currently, if spectra are not a multiple of 1024 channels, they will be 
   linearized to 1024 channels.
   */
  bool write_binary_exploranium_gr135v2( std::ostream &output ) const;
  
  
  /** Write a SPE file to the output stream.
   
   SPE files can only contain a single spectrum and single neutron count (or at
   least I've never seen a SPE file with multiple spectra), so you must specify
   which samples numbers and detectors to sum to create the single spectrum.
   
   @param output Stream to write the output to.
   @param sample_nums The sample numbers to sum to make the one output spectrum;
          if empty will use all sample numbers.
   @param det_nums The detector numbers to sum over to make the one output
          spectrum; if empty will use all detectors.
   @returns if file was successfully written to the output stream.
   */
  virtual bool write_iaea_spe( std::ostream &output,
                               std::set<int> sample_nums,
                               const std::set<int> &det_nums ) const;

  /** Write a CNF file to the output stream.
  
   CNF files can only contain a single spectrum and single neutron count, so you
   must specify which samples numbers and detectors to sum to create the single
   spectrum.
   
   @param output Stream to write the output to.
   @param sample_nums The sample numbers to sum to make the one output spectrum;
         if empty will use all sample numbers.  If any invalid values are specified, will throw
         exception.
   @param det_nums The detector numbers to sum over to make the one output
         spectrum; if empty will use all detectors. If any invalid values are specified, will throw
         exception.
   @returns if file was successfully written to the output stream.
  */
  virtual bool write_cnf( std::ostream &output,
                          std::set<int> sample_nums,
                          const std::set<int> &det_nums ) const;
  
  
#if( SpecUtils_ENABLE_D3_CHART )
  bool write_d3_html( std::ostream &output,
                      const D3SpectrumExport::D3SpectrumChartOptions &options,
                      std::set<int> sample_nums,
                      std::vector<std::string> det_names ) const;
#endif
  
  //Incase InterSpec specific changes are made, please change this number
  //  Version 4: Made it so portal data that starts with a long background as
  //             its first sample will have the 'id' attribute of the
  //             <RadMeasurement> be "Background", and subsequent samples have
  //             ids similar to "Survey 1", "Survey 2", etc.  The id attrib
  //             of <spectrum> tags also start with these same strings.
  //             (Hack to be compatible with GADRAS)
  #define SpecFile_2012N42_VERSION 4
  
  //The goal of create_2012_N42_xml(...) is that when read back into
  //  load_2012_N42_from_doc(...), the results should be identical (i.e. can
  //  make round trip).  May return null pointer if something goes drasitcally
  //  wrong.
  virtual std::shared_ptr< ::rapidxml::xml_document<char> > create_2012_N42_xml() const;
  
  //write_2012_N42(): a convience function that uses create_2012_N42_xml(...) to
  //  do the actual work
  virtual bool write_2012_N42( std::ostream& ostr ) const;


#if( PERFORM_DEVELOPER_CHECKS )
  //equal_enough(...): tests whether the passed in SpecFile objects are
  //  equal, for most intents and purposes.  Allows some small numerical
  //  rounding to occur.
  //Throws an std::exception with a brief explanaition when an issue is found.
  static void equal_enough( const SpecFile &lhs, const SpecFile &rhs );
  
  double deep_gamma_count_sum() const;
  double deep_neutron_count_sum() const;
#endif
  
protected:
  
  //measurement(...): converts a const Measurement ptr to a non-const Measurement
  // ptr, as well as checking that the Measurement actually belong to this
  //  SpecFile object. Returns empty pointer on error.
  //  Does not obtain a thread lock.
  std::shared_ptr<Measurement> measurement( std::shared_ptr<const Measurement> meas );
  
  //find_detector_names(): looks through measurements_ to find all detector
  //  names.
  //  Does not obtain a thread lock.
  std::set<std::string> find_detector_names() const;
  
  //set_detector_type_from_other_info(): sets detector_type_ using
  //  manufacturer_, instrument_model_ instrument_id_, or other info, only if
  //  detector_type_ isnt set.  Called from cleanup_after_load()
  void set_detector_type_from_other_info();
  
  //set_n42_2006_instrument_info_node_info(...): called from load_2006_N42_from_doc(...)
  //  Does not obtain a thread lock.
  void set_n42_2006_instrument_info_node_info( const rapidxml::xml_node<char> *info_node );
  
  
  /** Ensures unique detector-name sample-number combos.
   
    If measurements_ already has unique sample and detector numbers, then
    if will be sorted according to #compare_by_sample_det_time (necassary
    so we can retrieve Measurements by sample number and detector name/number)
    and potentially set the #kNotTimeSortedOrder flag.
   
    If measurements_ is not already unique by sample and detector numbers, the,
    they will be set according to Measurement start times.
   
    Currently assumed to only be called from #cleanup_after_load.
   
    TODO: function should be parralized for measurements with many samples
        - currently measurements with large numbers of measurements (>500)
          dont ensure dense sample numbers as a computational workaround.
        - Function probably also use other work as well
   */
  void ensure_unique_sample_numbers();
  
  //has_unique_sample_and_detector_numbers(): checks to make sure 
  bool has_unique_sample_and_detector_numbers() const;
  
  //setSampleNumbersByTimeStamp():
  //For files with < 500 samples, doesn't guarantee unique detctor-name
  //  sample-number combinations, but does try to preserve initial sample-number
  //  assignments.
  //For files with >= 500 samples, it does garuntee unique detector-name
  //  sample-number combinations, but it does not preserve initial sample-number
  //  assignments.
  //Called from cleanup_after_load()
  void set_sample_numbers_by_time_stamp();
  
  
  //load20XXN42File(...): loads the specialized type on N42 file.
  //  Throws std::exception on error loading.
  //  Both functions assume they are being called from loadN42FileData(...).
  void load_2006_N42_from_doc( const rapidxml::xml_node<char> *document_node );
  
  //ICD1 2012 info at http://www.nist.gov/pml/div682/grp04/upload/n42.xsd
  //  Example files at: http://www.nist.gov/pml/div682/grp04/n42-2012.cfm
  //  Dose rates and gross sums not parsed
  //  Detector statuses and other information are also not supported
  void load_2012_N42_from_doc( const rapidxml::xml_node<char> *document_node );
  
  
  //2012 N42 helper functions for loading (may throw exceptions)
  void set_2012_N42_instrument_info( const rapidxml::xml_node<char> *inst_info_node );
  
  

  //setMeasurementLocationInformation(...):  sets the measurement information
  //  for a particular <Measurement> section of N42 data.  The parced data
  //  sets both MeasurementInfo member variables, as well as member variables
  //  (particularly gps) of the Measurnment's passed in (that should belong to
  //  the same <Measurement> section of the N42 file, since there may be
  //  multiple spectrums per measurement).
  void set_n42_2006_measurement_location_information(
                    const rapidxml::xml_node<char> *measured_item_info_node,
                    std::vector<std::shared_ptr<Measurement>> measurements_applicable );

  /** If this SpecFile is calibrated by lower channel energy, then this
   function will write a record (and it should be the first record) to the
   output with title "Energy" and channel counts equal to the energies of the
   channels.  Does not write the record if not SpecUtils::EnergyCalType::LowerChannelEdge
   or if all gamma spectra do not share an energy calibration.
  
   @param ostr The stream to write the record to
   @param lower_channel_energies The lower channel energies to write to file.
          This should have a size of N+1, where the spectra have N channels.
   @param nchannels_using The number of channels you are using for the PCF file.
          Must be multiple of 64.
   
   @returns Number of bytes written to the output stream, and zero if no
   record written.
   */
  size_t write_lower_channel_energies_to_pcf( std::ostream &ostr,
              std::shared_ptr<const std::vector<float>> lower_channel_energies,
              const size_t nchannels_using ) const;
  
  /** Writes the deviation pairs portion of the PCF file to the output stream,
      if there are any deviation pairs.
   
   Note: its not well specified how to deal with detectors that have names
         not conforming to the "Aa1", "Aa2", ... "Ba1", ... convention, and
         were there are more than one detector, so currenly just write these
         other detector in the first location available in the PCF, where the
         detectors are ordered alphabetically by their name.
   */
  void write_deviation_pairs_to_pcf( std::ostream &outputstrm ) const;
  
  
  /** Determines number of channels we should use for writing PCF files, and
      hence the record size; will be multiple of 64.  Also determines if the
      lower channel energy convention should be used for the file, and if so
      the lower channel energies that should be used (which may either be
      allocated, or an already existing #Measurement::channel_energies_); if
      used will be one channel larger then the gamma spectra and thus 63 less
      than the determined nchannel.
   */
  void pcf_file_channel_info( size_t &nchannel,
      std::shared_ptr<const std::vector<float>> &lower_channel_bining ) const;
  
  //do_channel_data_xform(): utility function called by
  //  truncate_gamma_channels() and combine_gamma_channels().  For each
  //  Measurement with 'nchannels', xform is called for it.  Returns
  //  number of modified Measurements.  Will appropriately modify the
  //  kHasCommonBinning and kAllSpectraSameNumberChannels bits of
  //  properties_flags_, as well as set modified_ and modifiedSinceDecode_.
  size_t do_channel_data_xform( const size_t nchannels,
                std::function< void(std::shared_ptr<Measurement>) > xform );
  
  //Data members
  float gamma_live_time_;      //sum over all measurements
  float gamma_real_time_;      //sum over all measurements
  double gamma_count_sum_;      //sum over all measurements
  double neutron_counts_sum_;   //sum over all measurements
  std::string                 filename_;
  std::vector<std::string>    detector_names_;          //Names may have "_intercal_..." appended to them to account for multiple binnings of the same data.
  std::vector<int>            detector_numbers_;        //in same order as detector_names_
  std::vector<std::string>    neutron_detector_names_;  //These are the names of detectors that may hold nuetron information

  std::string uuid_;
  std::vector<std::string> remarks_;

  std::vector<std::string> parse_warnings_;
  
  //These go under the <MeasuredItemInformation> node in N42 file
  //  There are more fields were not currently checking for (also remarks under
  //  this node are just put into remarks_), and not necassarily set for other
  //  file formats.
  //  Also, if multiple locations are specified in the file, the last one
  //  of the files what gets put here.
  //In the future all this info should be placed in a shared_ptr, and only
  //  pouplated if it actually exists in the file
  //  Should also consider moving to the Measurement class
  int lane_number_;
  std::string measurement_location_name_;
  std::string inspection_;
  std::string measurement_operator_;


  //Start dealing with sample numbers
  std::set<int> sample_numbers_;

  // map from sample_number to a vector with indices of measurements_ of all
  //  Measurement with that sample_number
  std::map<int, std::vector<size_t> > sample_to_measurements_;


  DetectorType detector_type_;  //This is deduced from the file

  //instrument_type_:
  //  From 2006 ICD1 specs (under node InstrumentType), can be:
  //    PortalMonitor, SpecPortal, RadionuclideIdentifier,
  //    PersonalRadiationDetector, SurveyMeter, Spectrometer, Other
  //  From 2012 ICD1 specs (under node RadInstrumentClassCode) can be:
  //    "Backpack", "Dosimeter",
  //    "Electronic Personal Emergency Radiation Detector", "Mobile System",
  //    "Network Area Monitor", "Neutron Handheld", "Personal Radiation Detector",
  //    "Radionuclide Identifier", "Portal Monitor",
  //    "Spectroscopic Portal Monitor",
  //    "Spectroscopic Personal Radiation Detector", "Gamma Handheld",
  //    "Transportable System", "Other"
  //I am currently using "RadionuclideIdentifier" for handheld detectors read in
  //  from not n42 files
  std::string instrument_type_;
  
  std::string manufacturer_;
  std::string instrument_model_;
  
  std::string instrument_id_;     //often times the serial number
  
  /** Equivalent of N42 2012 RadInstrumentVersion tag that has a name (first)
      and verson (second) field
   */
  std::vector<std::pair<std::string,std::string> > component_versions_;
  
  std::vector< std::shared_ptr<Measurement> > measurements_;

  double mean_latitude_, mean_longitude_;
  
  
  //This should actually be a map<> or something so there can be multiple
  // DetectorAnalysis objects for a file, each indexed by
  // radMeasurementGroupReferences, or radMeasurementReferences
  std::shared_ptr<const DetectorAnalysis> detectors_analysis_;

  
  //properties_flags_: intenteded to indicate boolean things about the
  //  measurement style, origin, properties, or other values.
  //  These flags are calculated and set in the cleanup_after_load() function.
  //  This value is also not included in the computation of the hash of this
  //  object in generate_psuedo_uuid().
  enum MeasurementPorperties
  {
    //kPassthroughOrSearchMode: gretaer than 5 samples, with average real time
    //  less than 2.5 seconds.  May be improved in the future to ensure
    //  measurements are sequential.
    kPassthroughOrSearchMode = (1 << 0),
    
    //kHasCommonBinning: ensures that all spectrums in measurements_ share the
    //  same binning.
    kHasCommonBinning = (1 << 1),
    
    //kRebinnedToCommonBinning: spectra had to be rebinned in order to make it
    //  so all spectrums in measurements_ share the same binning.
    kRebinnedToCommonBinning = (1 << 2),
    
    //kAllSpectraSameNumberChannels: lets you know that all spectra have the
    //  same number of channels.
    kAllSpectraSameNumberChannels = (1 << 3),
    
    //kNotTimeSortedOrder: lets you know measurements_ is not sorted by start
    //  time.  Only possibly marked when cleanup_after_load() is called with the
    //  DontChangeOrReorderSamples flag.
    kNotTimeSortedOrder = (1 << 4),
    
    //kNotSampleDetectorTimeSorted: if set, then indicates measurements_
    //  is not sorted by sample number, then detector number, then time.
    kNotSampleDetectorTimeSorted = (1 << 5),
    
    //kNotUniqueSampleDetectorNumbers: marked when the the combination of
    //  sample and detector numbers does not uniquly identify a Measurement.
    //  May be marked when cleanup_after_load() is called with the
    //  DontChangeOrReorderSamples flag.  If not set, then each measurement for
    //  a sample number has a the same start time.
    kNotUniqueSampleDetectorNumbers = (1 << 6)
  };//enum MeasurementPorperties
  
  uint32_t properties_flags_;
  
  //modified_: intended to be used to determine if changes have been
  //  made to oject since the last time the spectrum was saved to disk.
  //  Set to true by (hopefully) all SpecFile functions which are
  //  modifying, except cleanup_after_load() and reset() which sets it to false.
  //  Is _not_ set to false by any of the "saving" functions, this is your
  //  responsibility.
  bool modified_;
  
  //modifiedSinceDecode_: indicates if file has been modified at all since
  // cleanup_after_load() was called.  Set to true by (hopefully) all
  //  SpecFile functions which are modifying, except cleanup_after_load()
  //  and reset() which sets it to false.
  bool modifiedSinceDecode_;
 
protected:
  mutable std::recursive_mutex mutex_;
public:
  std::recursive_mutex &mutex() const { return mutex_; };
  
  friend struct N42DecodeHelper2012;
};//class SpecFile


//DetectorAnalysisResult and DetectorAnalysis are a first hack at recording
//  analysis information from the detectors file in order to use it later.
//  I'm not happy with the current design, and this information is not
//  retrieved from all (most) file types...  But I needed this in, and was
//  under a serious time crunch.
//
//Should seperate DetectorAnalysisResult into Nuclide and Dose rate analysis
//  types (have not seen RadAlarm, SpectrumPeakAnalysisResults or
//  GrossCountAnalysisResults results).
//  To nuclide result could add:
//    NuclideShieldingArealDensityValue, NuclideShieldingAtomicNumber,
//    NuclideActivityUncertaintyValue, SourcePosition
//    NuclideSourceGeometryCode, NuclideIDConfidenceValue, etc
//    as well as more better recording out SourcePosition.
//
//  To dose result could add:
//    AverageDoseRateUncertaintyValue, MaximumDoseRateValue,
//    MinimumDoseRateValue, BackgroundDoseRateValue,
//    BackgroundDoseRateUncertaintyValue, TotalDoseValue,
//    as well as more better recording out SourcePosition.
class DetectorAnalysisResult
{
public:
  std::string remark_;
  std::string nuclide_;
  float activity_;            //in units of becquerel (eg: 1.0 == 1 bq)
  std::string nuclide_type_;
  std::string id_confidence_;
  float distance_;            //in units of mm (eg: 1.0 == 1 mm )
  
  float dose_rate_;           //in units of micro-sievert per hour
                              //   (eg: 1.0 = 1 u-sv)
  
  float real_time_;           //in units of seconds (eg: 1.0 = 1 s)
  
  //20171220: removed start_time_, since there is no cooresponding equivalent
  //  in N42 2012.  Instead should link to which detectors this result is
  //  applicable to.
  //boost::posix_time::ptime start_time_;  //start time of the cooresponding data (its a little unclear in some formats if this is the case...)
  
  std::string detector_;
  
  DetectorAnalysisResult();
  void reset();
  bool isEmpty() const;
#if( PERFORM_DEVELOPER_CHECKS )
  static void equal_enough( const DetectorAnalysisResult &lhs,
                           const DetectorAnalysisResult &rhs );
#endif
};//struct DetectorAnalysisResult


/** A class that aims to eventually be about equivalent of the N42 2012 
    <AnalysisResults> tag.
 */
class DetectorAnalysis
{
public:
  //Need to make an association of this with the sample/detector number, and
  //  allow SpecFile to have multiple of these...

  /** Remarks included with the AnalysisResults */
  std::vector<std::string> remarks_;
  
  /** A unique name of the analysis algorithm. */
  std::string algorithm_name_;
  
  /** Information describing the version of a particular analysis algorithm
   component.
   If file type only gives one version, then the component name "main" is
   assigned.
   */
  std::vector<std::pair<std::string,std::string>> algorithm_component_versions_;
  
  /** Creator or implementer of the analysis algorithm. */
  std::string algorithm_creator_;
  
  /** Free-form text describing the analysis algorithm. */
  std::string algorithm_description_;
  
  /** Time at which the analysis was started, if available; */
  boost::posix_time::ptime analysis_start_time_;
  
  /** The number of seconds taken to perform the analysis; will be 0.0f if not
   specified.
   */
  float analysis_computation_duration_;
  
  /** Free-form text describing the overall conclusion of the analysis regarding 
      the source of concern.  Equivalent to <AnalysisResultDescription> or 
      <ThreatDescription> tag of N42 2012 or 2006 respectively
   */
  std::string algorithm_result_description_;
  
  //Also, could put the derived data spectra into this object (and make sure not otherwise in file)
  
  //N42 2012 also has the following fields:
  //AnalysisAlgorithmSetting
  //AnalysisResultStatusCode
  //AnalysisConfidenceValue
  //RadAlarm
  //NuclideAnalysisResults
  //SpectrumPeakAnalysisResults
  //GrossCountAnalysisResults
  //DoseAnalysisResults
  //ExposureAnalysisResults
  //Fault
  //AnalysisStartDateTime
  //AnalysisComputationDuration
  
  std::vector<DetectorAnalysisResult> results_;
  
  DetectorAnalysis();
  void reset();
  bool is_empty() const;
  
#if( PERFORM_DEVELOPER_CHECKS )
  static void equal_enough( const DetectorAnalysis &lhs,
                           const DetectorAnalysis &rhs );
#endif
};//struct DetectorAnalysisResults

}//namespace SpecUtils
#endif  //SpecUtils_SpecFile_h
