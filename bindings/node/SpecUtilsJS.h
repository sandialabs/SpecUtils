#ifndef SPEC_UTILS_JS_H
#define SPEC_UTILS_JS_H

#include <memory>

#include <napi.h>

// Forward declarations
class SpecFile;
namespace SpecUtils
{
  class SpecFile;
  class Measurement;
  class RiidAnalysis;
  class DetectorAnalysis;
}

/**
 Current known shortcommings:
   - Defining the SourceType, OccupancyStatus, and EquationType as classes with
     static string values to represent the C++ enum values.  I dont like this.
     Would be better to have somethign like specutils.SpecRecord.Foreground,
     but I couldnt figure out how to give SpecRecord a const static SourceType
     object (and also didnt know how to do stuff in the most JavaScript way).
   - The SpecRecord, RiidAnalysis, and RiidAnaResult classes should not be
     allowed to be created outside of C++ (didnt spend any time on this).
   - modifying (ex recalibrating), or assembling spectrum files from scratch not
     implemented.
   - Can probably replace many InstanceMethod definitions with InstanceAccessor
     isntead to make things a more JavaScripty...
   - Strings are assumed UTF-8 encoded everywhere but this isnt (yet) enforced
     by SpecUtils when parsing, meaning we may get exceptions when creating
     JavaScript strings.
   - Things could be made more efficient in general.  Ex. there are a lot of
     string comparisons where integer enum comparisons would be good, or a lot
     of objects are created where the dont have to be, or general arrays are
     copied from JavaScript to C++, etc.
 
 ToDo:
   - Add in D3 code saving.
   - Document (in markdown)
   - Make things mutable? (E.g., allow file re-calibration)
   - Add symbols for SaveSpectrumAsType
 */




class RiidAnaResult : public Napi::ObjectWrap<RiidAnaResult>
{
public:
  static void Init(Napi::Env &env, Napi::Object &exports);
  
  RiidAnaResult() = delete;
  
  RiidAnaResult( const Napi::CallbackInfo& info );
  
  static Napi::FunctionReference constructor;

  /* Returns String giving nuclide.
   May not strictly be a nuclide, but may be something like: "U-238", "U-ore",
   "HEU", "nuclear", "neutron", "Unknown", "Ind.", etc.
   Will return null if no identification is given (most commonly happens when
   this RiidAnaResult is to give an activity or doe rate)
   )
   */
  Napi::Value nuclide(const Napi::CallbackInfo& info);
  
  /* Returns String giving type of nuclide, usually somethign like "Industrial",
   "Medical", etc.  Will return null when not provided in the spectrum file.
   */
  Napi::Value nuclide_type(const Napi::CallbackInfo& info);
  
  /* Returns String describing nuclide confidence.  May be a number (ex. "9"),
   a word (ex "High"), a letter (ex 'L'), or a phrase.
   Will return null if not available.
   */
  Napi::Value id_confidence(const Napi::CallbackInfo& info);
  
  /* Returns String giving remark.  Returns null if one is not provided in spectrum file. */
  Napi::Value remark(const Napi::CallbackInfo& info);
  
  
  /* Returns dose rate in micro-sievert.  Returns null if not avaialble.
   */
  Napi::Value dose_rate(const Napi::CallbackInfo& info);
  
  /* Returns the name of the detector this result corresponds to.  If null or
   blank then you should assume it is for all detectors in the file.
   */
  Napi::Value detector(const Napi::CallbackInfo& info);
  
protected:
  size_t m_index;
  std::shared_ptr<const SpecUtils::DetectorAnalysis> m_ana;
  
  friend class RiidAnalysis;
};//struct RiidAnaResult


/** A class that aims to eventually be about equivalent of the N42 2012
 <AnalysisResults> tag.
 */
class RiidAnalysis : public Napi::ObjectWrap<RiidAnalysis>
{
public:
  
  static void Init(Napi::Env &env, Napi::Object &exports);
  
  RiidAnalysis() = delete;
  
  RiidAnalysis( const Napi::CallbackInfo& info );
  
  static Napi::FunctionReference constructor;
  
  /* Returns Array of Strings representing remarks provided in the spectrum file.
   Returns null if no remarks are provided.
  */
  Napi::Value remarks(const Napi::CallbackInfo& info);
  
  /* A a String giving the unique name of the analysis algorithm.
   Returns null if not provided in the spectrum file.
   */
  Napi::Value algorithm_name(const Napi::CallbackInfo& info);
  
  /* Creator or implementer of the analysis algorithm.
   Will return null if not provided in the file.
   */
  Napi::Value algorithm_creator(const Napi::CallbackInfo& info);
  
  /* Free-form String describing the analysis algorithm. Will be null if not
   provided in the spectrum file.
   */
  Napi::Value algorithm_description(const Napi::CallbackInfo& info);
  
  /** Returns free-form String describing the overall conclusion of the analysis regarding
   the source of concern.  Equivalent to <AnalysisResultDescription> or
   <ThreatDescription> tag of N42 2012 or 2006 respectively.
   Will return null if not provided in the file.
   */
  Napi::Value algorithm_result_description(const Napi::CallbackInfo& info);
  
  /* Returns array of RiidAnaResult objects contained in this analysis.  May be
   empty (but wont be null)
   */
  Napi::Value results(const Napi::CallbackInfo& info);
  
  
  //ToDo: return algorithm_component_versions.
  /* Information describing the version of a particular analysis algorithm
   component.
   If file type only gives one version, then the component name "main" is
   assigned.
   */
  //std::vector<std::pair<std::string,std::string>> algorithm_component_versions_;

  
protected:
  std::shared_ptr<const SpecUtils::DetectorAnalysis> m_ana;
  
  
  friend class SpecFile;
};//struct RiidAnalysis






/** SpecFile cooresponds to the C++ SpecUtils::Measurement class, which holds the
 information of a gamma spectrum from a single detection element.  If there is
 a neutron detector associated with the gamma detector, its counts are also
 in this class.
 */
class SpecRecord : public Napi::ObjectWrap<SpecRecord>
{
public:
  static void Init(Napi::Env &env, Napi::Object &exports);
  
  SpecRecord() = delete;
  
  SpecRecord( const Napi::CallbackInfo& info );
  
  static Napi::FunctionReference constructor;
  
  /** Returns String detector name. */
  Napi::Value detector_name(const Napi::CallbackInfo& info);
  /** Returns Number detector name. */
  Napi::Value detector_number(const Napi::CallbackInfo& info);
  /** Returns the integer sample number. */
  Napi::Value sample_number(const Napi::CallbackInfo& info);
  
  /** Returns string indicating source type.  WIll be one of the following values:
       "IntrinsicActivity", "Calibration", "Background", "Foreground", "UnknownSourceType",
       See the SourceType class defined in cpp for possible values.
   */
  Napi::Value source_type(const Napi::CallbackInfo& info);
  /** Live time in seconds of measurement */
  Napi::Value live_time(const Napi::CallbackInfo& info);
  /** Real time in seconds of measurement */
  Napi::Value real_time(const Napi::CallbackInfo& info);
  /** Returns start time of measurement start, as number of milliseconds since January 1970 00:00:00 UTC, if avaialble, otherwise null. */
  Napi::Value start_time(const Napi::CallbackInfo& info);
  
  /** Returns the String title.  Not supported by all input spectrum file formats, in which case will be emty string. */
  Napi::Value title(const Napi::CallbackInfo& info);
  /** Returns an array of strings representing the 'remarks' for this specific spectrum record.
   Returns null if no remarks.
   */
  Napi::Value remarks(const Napi::CallbackInfo& info);
  /** Returns a string thats one of the follwoing: "NotOccupied", "Occupied", "UnknownOccupancyStatus" */
  Napi::Value occupied(const Napi::CallbackInfo& info);
  /** Returns float sum of gamma counts. */
  Napi::Value gamma_count_sum(const Napi::CallbackInfo& info);
  /** Returns boolean indicating if neutron data is available. */
  Napi::Value contained_neutron(const Napi::CallbackInfo& info);
  /** Returns float sum of neutron counts. Will return null if neutron data not avaiable. */
  Napi::Value neutron_counts_sum(const Napi::CallbackInfo& info);
  /** Returns boolean indicating if GPS is available. */
  Napi::Value has_gps_info(const Napi::CallbackInfo& info);
  /** Returns Number latitidue if available, otherwise null. */
  Napi::Value latitude(const Napi::CallbackInfo& info);
  /** Returns Number longitude if available, otherwise null. */
  Napi::Value longitude(const Napi::CallbackInfo& info);
  /** Returns date/time of GPS fix, as milliseconds since Epoch.  Null if not avaialble. */
  Napi::Value position_time(const Napi::CallbackInfo& info);
  
  /* Returns an array of numbers representign the lower energy, in keV, of each gamma channel.
   If this SpecRecord did not have gamma data associated with it, will return null.
   */
  Napi::Value gamma_channel_energies(const Napi::CallbackInfo& info);
  
  /* Returns an array of numbers representign the gamma channel counts.
   If this SpecRecord did not have gamma data associated with it, will return null.
   */
  Napi::Value gamma_channel_contents(const Napi::CallbackInfo& info);
  
  /* Returns a string in EquationType, i.e.
   ["Polynomial","FullRangeFraction","LowerChannelEdge",
    "UnspecifiedUsingDefaultPolynomial","InvalidEquationType"]
   */
  Napi::Value energy_calibration_model(const Napi::CallbackInfo& info);
  /* Aray of numbers representing the energy calibration coefficients.
   Returned value may have zero entries.
   Interpretation is dependant on the energy_calibration_model.
   */
  Napi::Value energy_calibration_coeffs(const Napi::CallbackInfo& info);
  
  /* Array of deviation pairs.
   Returned value may have zero entries, but each sub-array will always have
   exactly two values (energy and offset, both in keV).
   Ex. [[0,0],[122,15],661,-13],[2614,0]]
   */
  Napi::Value deviation_pairs(const Napi::CallbackInfo& info);
  
  /* Unimplemented functions:
  inline const std::vector<float> &neutron_counts() const;
  inline size_t num_gamma_channels() const;
  inline size_t find_gamma_channel( const float energy ) const;
  inline float gamma_channel_content( const size_t channel ) const;
  inline float gamma_channel_lower( const size_t channel ) const;
  inline float gamma_channel_center( const size_t channel ) const;
  inline float gamma_channel_upper( const size_t channel ) const;
  inline float gamma_channel_width( const size_t channel ) const;
  double gamma_integral( float lower_energy, float upper_energy ) const;
  double gamma_channels_sum( size_t startbin, size_t endbin ) const;
  inline float gamma_energy_min() const;
  inline float gamma_energy_max() const;
  inline float speed() const;
  inline const std::string &detector_type() const;
  inline QualityStatus quality_status() const;
*/
  
  
  
protected:
  std::shared_ptr<const SpecUtils::Measurement> m_meas;
  
  friend class SpecFile;
};//class SpecRecord


/** SpecFile cooresponds to the C++ SpecUtils::SpecFile class, which basically
 represents a spectrum file.
 */
class SpecFile : public Napi::ObjectWrap<SpecFile>
{
  public:
  static Napi::Object Init( Napi::Env env, Napi::Object exports );
  
  
  SpecFile(const Napi::CallbackInfo& info);
  virtual ~SpecFile();
  //virtual void Finalize( Napi::Env env );  //Doesnt seem to get called, so not implementing
  
  private:
  static Napi::FunctionReference constructor;
  
  Napi::Value gamma_live_time(const Napi::CallbackInfo& info);
  Napi::Value gamma_real_time(const Napi::CallbackInfo& info);
  Napi::Value gamma_count_sum(const Napi::CallbackInfo& info);
  Napi::Value contained_neutrons(const Napi::CallbackInfo& info);
  Napi::Value neutron_counts_sum(const Napi::CallbackInfo& info);
  
  Napi::Value num_gamma_channels(const Napi::CallbackInfo& info);
  Napi::Value num_spec_records(const Napi::CallbackInfo& info);

  
  /**
   "Unknown", "GR135", "IdentiFINDER", "IdentiFINDER-NG", "IdentiFINDER-LaBr3", "Detective", "Detective-EX", "Detective-EX100", "Detective-EX200", "Detective X", "SAIC8", "Falcon 5000", "MicroDetective", "MicroRaider", "SAM940", "SAM940LaBr3", "SAM945", "SRPM-210", "RS-701", "RS-705", "RadHunterNaI", "RadHunterLaBr3", "RSI-Unspecified", "RadEagle NaI 3x1", "RadEagle CeBr3 2x1", "RadEagle CeBr3 3x0.8", "RadEagle LaBr3 2x1";
   
   ToDo: make C++ DetectorType a JS enum, and also equiv for detectorTypeToString( const DetectorType type );
  */
  Napi::Value inferred_instrument_model(const Napi::CallbackInfo& info);
  
  
  /**
   PortalMonitor, SpecPortal, RadionuclideIdentifier,
   PersonalRadiationDetector, SurveyMeter, Spectrometer, Other
   */
  Napi::Value instrument_type(const Napi::CallbackInfo& info);
  
  /**
   */
  Napi::Value manufacturer(const Napi::CallbackInfo& info);
  
  /**
   */
  Napi::Value instrument_model(const Napi::CallbackInfo& info);
  
  Napi::Value instrument_id(const Napi::CallbackInfo& info);
  
  Napi::Value serial_number(const Napi::CallbackInfo& info);
  
  
  Napi::Value uuid(const Napi::CallbackInfo& info);
  
  Napi::Value passthrough(const Napi::CallbackInfo& info);
  Napi::Value filename(const Napi::CallbackInfo& info);
  Napi::Value remarks(const Napi::CallbackInfo& info);
  Napi::Value detector_names(const Napi::CallbackInfo& info);
  Napi::Value sample_numbers(const Napi::CallbackInfo& info);
  
  Napi::Value measurements(const Napi::CallbackInfo& info);
  
  Napi::Value sum_measurements(const Napi::CallbackInfo& info);
  
  Napi::Value has_gps_info(const Napi::CallbackInfo& info);
  Napi::Value mean_latitude(const Napi::CallbackInfo& info);
  Napi::Value mean_longitude(const Napi::CallbackInfo& info);
  
  Napi::Value riid_analysis(const Napi::CallbackInfo& info);
  
  /* Takes two arguments.
       First is a string giving the path to save to.
       Second is string giving format to save to, must be one of following:
         "TXT", "CSV", "PCF", "N42-2006", N42-2012", "CHN", "SPC-int",
         "SPC" (or equiv "SPC-float"), "SPC-ascii", "GR130v0", "GR135v2",
         "SPE" (or equiv "IAEA"), "HTML".
   
   ToDo: specify file format as an enum
   ToDo: Add additional argument to allow filtering which measurements get saved....
   */
  Napi::Value write_to_file(const Napi::CallbackInfo& info);
  
  /* Unimplemented functions:
  inline const std::vector<int> &detector_numbers() const;
  inline const std::vector<std::string> &neutron_detector_names() const;
  inline int lane_number() const;
  inline const std::string &measurement_location_name() const;
  inline const std::string &inspection() const;
  inline const std::string &measurment_operator() const;
  
  inline std::shared_ptr<const Measurement> measurement( size_t num ) const;
  */
  
  std::set<std::string> to_valid_det_names( Napi::Value value, const Napi::Env &env );
  std::set<int> to_valid_sample_numbers( Napi::Value value, const Napi::Env &env );
  std::set<std::string> to_valid_source_types( Napi::Value value, const Napi::Env &env );
  
  
  std::shared_ptr<const SpecUtils::SpecFile> m_spec;
};//class SpecFile



#endif //SPEC_UTILS_JS_H
