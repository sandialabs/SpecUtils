#include <set>
#include <string>
#include <iterator>

#include <napi.h>
#include <napi-inl.h>

#include "SpecUtilsJS.h"
#include "SpecUtils/UtilityFunctions.h"
#include "SpecUtils/SpectrumDataStructs.h"


namespace
{
  
  const std::string &to_str( const Measurement::SourceType t )
  {
    static const std::string back = "Background";
    static const std::string cal = "Calibration";
    static const std::string fore = "Foreground";
    static const std::string intrins = "IntrinsicActivity";
    static const std::string unknown = "UnknownSourceType";
    
    switch( t )
    {
      case Measurement::Background:        return back;
      case Measurement::Calibration:       return cal;
      case Measurement::Foreground:        return fore;
      case Measurement::IntrinsicActivity: return intrins;
      case Measurement::UnknownSourceType: return unknown;
    }//switch( m_meas->source_type() )
    assert(0);
    return unknown;
  }//to_str( const Measurement::SourceType t )
  
  const std::string &to_str( const Measurement::OccupancyStatus t )
  {
    static const std::string notocc = "NotOccupied";
    static const std::string occ = "Occupied";
    static const std::string unknownocc = "UnknownOccupancyStatus";
    
    switch( t )
    {
      case Measurement::NotOccupied: return notocc;
      case Measurement::Occupied: return occ;
      case Measurement::UnknownOccupancyStatus: return unknownocc;
    }//switch( m_meas->source_type() )
    assert(0);
    return unknownocc;
  }//to_str( const Measurement::SourceType t )
  
  const std::string &to_str( const Measurement::EquationType t )
  {
    static const std::string polynomial = "Polynomial";
    static const std::string fullRangeFraction = "FullRangeFraction";
    static const std::string lowerChannelEdge = "LowerChannelEdge";
    static const std::string unspecifiedUsingDefaultPolynomial = "UnspecifiedUsingDefaultPolynomial";
    static const std::string invalidEquationType = "InvalidEquationType";
    
    switch( t )
    {
      case Measurement::Polynomial: return polynomial;
      case Measurement::FullRangeFraction: return fullRangeFraction;
      case Measurement::LowerChannelEdge: return lowerChannelEdge;
      case Measurement::UnspecifiedUsingDefaultPolynomial: return unspecifiedUsingDefaultPolynomial;
      case Measurement::InvalidEquationType: return invalidEquationType;
    }//switch( m_meas->source_type() )
    assert(0);
    return invalidEquationType;
  }//to_str( const Measurement::SourceType t )
  
  
}//namespace



Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  return SpecFile::Init(env, exports);
}

NODE_API_MODULE(SpecUtils, InitAll)

Napi::FunctionReference RiidAnaResult::constructor;
Napi::FunctionReference RiidAnalysis::constructor;
Napi::FunctionReference SpecRecord::constructor;
Napi::FunctionReference SpecFile::constructor;




class SourceType : public Napi::ObjectWrap<SourceType>
{
public:
  static Napi::FunctionReference constructor;
  
  static void Init(Napi::Env &env, Napi::Object &exports)
  {
    exports.Set("SourceType", DefineClass(env, "SourceType", {
      StaticValue(to_str(Measurement::Background).c_str(), Napi::String::New(env, to_str(Measurement::Background))),
      StaticValue(to_str(Measurement::Calibration).c_str(), Napi::String::New(env, to_str(Measurement::Calibration))),
      StaticValue(to_str(Measurement::Foreground).c_str(), Napi::String::New(env, to_str(Measurement::Foreground))),
      StaticValue(to_str(Measurement::IntrinsicActivity).c_str(), Napi::String::New(env, to_str(Measurement::IntrinsicActivity))),
      StaticValue(to_str(Measurement::UnknownSourceType).c_str(), Napi::String::New(env, to_str(Measurement::UnknownSourceType)))
    } ) );
  }
  
  SourceType() = delete;
  
  SourceType( const Napi::CallbackInfo& info )
    : Napi::ObjectWrap<SourceType>( info )
  {
  }
};//class SourceType


class OccupancyStatus : public Napi::ObjectWrap<OccupancyStatus>
{
public:
  static Napi::FunctionReference constructor;
  
  static void Init(Napi::Env &env, Napi::Object &exports)
  {
    exports.Set("OccupancyStatus", DefineClass(env, "OccupancyStatus", {
      StaticValue(to_str(Measurement::NotOccupied).c_str(), Napi::String::New(env, to_str(Measurement::NotOccupied))),
      StaticValue(to_str(Measurement::Occupied).c_str(), Napi::String::New(env, to_str(Measurement::Occupied))),
      StaticValue(to_str(Measurement::UnknownOccupancyStatus).c_str(), Napi::String::New(env, to_str(Measurement::UnknownOccupancyStatus))),
    } ) );
  }
  
  OccupancyStatus() = delete;
  
  OccupancyStatus( const Napi::CallbackInfo& info )
  : Napi::ObjectWrap<OccupancyStatus>( info )
  {
  }
};//class OccupancyStatus


class EquationType : public Napi::ObjectWrap<EquationType>
{
public:
  static Napi::FunctionReference constructor;
  
  static void Init(Napi::Env &env, Napi::Object &exports)
  {
    exports.Set("EquationType", DefineClass(env, "EquationType", {
      StaticValue(to_str(Measurement::Polynomial).c_str(), Napi::String::New(env, to_str(Measurement::Polynomial))),
      StaticValue(to_str(Measurement::FullRangeFraction).c_str(), Napi::String::New(env, to_str(Measurement::FullRangeFraction))),
      StaticValue(to_str(Measurement::LowerChannelEdge).c_str(), Napi::String::New(env, to_str(Measurement::LowerChannelEdge))),
      StaticValue(to_str(Measurement::UnspecifiedUsingDefaultPolynomial).c_str(), Napi::String::New(env, to_str(Measurement::UnspecifiedUsingDefaultPolynomial))),
      StaticValue(to_str(Measurement::InvalidEquationType).c_str(), Napi::String::New(env, to_str(Measurement::InvalidEquationType)))
    } ) );
  }
  
  EquationType() = delete;
  
  EquationType( const Napi::CallbackInfo& info )
  : Napi::ObjectWrap<EquationType>( info )
  {
  }
};//class EquationType


class DetectorTypeEnum : public Napi::ObjectWrap<DetectorTypeEnum>
{
public:
  static Napi::FunctionReference constructor;
  
  static void Init(Napi::Env &env, Napi::Object &exports)
  {
    std::vector<PropertyDescriptor> properties;
    
    for( DetectorType type = DetectorType(0); type <= kUnknownDetector; type = DetectorType(type+1) )
    {
      const std::string &name = detectorTypeToString(type);
      properties.push_back( StaticValue( name.c_str(), Napi::String::New(env,name) )  );
    }
    
    exports.Set("DetectorType", DefineClass(env, "DetectorType", properties ) );
  }//
  
  DetectorTypeEnum() = delete;
  
  DetectorTypeEnum( const Napi::CallbackInfo& info )
  : Napi::ObjectWrap<DetectorTypeEnum>( info )
  {
  }
};//class DetectorTypeEnum



Napi::FunctionReference SourceType::constructor;
Napi::FunctionReference OccupancyStatus::constructor;
Napi::FunctionReference EquationType::constructor;
Napi::FunctionReference DetectorTypeEnum::constructor;




void RiidAnalysis::Init(Napi::Env &env, Napi::Object &exports)
{
  Napi::Function func = DefineClass(env, "RiidAnalysis", {
    InstanceMethod("remarks", &RiidAnalysis::remarks),
    InstanceMethod("algorithmName", &RiidAnalysis::algorithm_name),
    InstanceMethod("algorithmCreator", &RiidAnalysis::algorithm_creator),
    InstanceMethod("algorithmDescription", &RiidAnalysis::algorithm_description),
    InstanceMethod("algorithmResultDescription", &RiidAnalysis::algorithm_result_description),
    InstanceMethod("results", &RiidAnalysis::results)
  });
  
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  
  exports.Set("RiidAnalysis", func);
}


void RiidAnaResult::Init(Napi::Env &env, Napi::Object &exports)
{
  Napi::Function func = DefineClass(env, "RiidAnaResult", {
    InstanceMethod("nuclide", &RiidAnaResult::nuclide),
    InstanceMethod("nuclideType", &RiidAnaResult::nuclide_type),
    InstanceMethod("idConfidence", &RiidAnaResult::id_confidence),
    InstanceMethod("remark", &RiidAnaResult::remark),
    InstanceMethod("doseRate", &RiidAnaResult::dose_rate),
    InstanceMethod("detector", &RiidAnaResult::detector)
  });
  
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  
  exports.Set("RiidAnaResult", func);
}
  

RiidAnaResult::RiidAnaResult( const Napi::CallbackInfo& info )
: Napi::ObjectWrap<RiidAnaResult>(info)
{
}
  

  /* Returns String giving nuclide.
   May not strictly be a nuclide, but may be something like: "U-238", "U-ore",
   "HEU", "nuclear", "neutron", "Unknown", "Ind.", etc.
   Will return null if no identification is given (most commonly happens when
   this RiidAnaResult is to give an activity or doe rate)
   )
   */
Napi::Value RiidAnaResult::nuclide(const Napi::CallbackInfo& info)
{
  assert( m_ana && (m_index < m_ana->results_.size()) );
  
  const auto &res = m_ana->results_[m_index];
  if( res.nuclide_.empty() )
    return Napi::Value();
  return Napi::String::New( info.Env(), res.nuclide_ );
}

  /* Returns String giving type of nuclide, usually somethign like "Industrial",
   "Medical", etc.  Will return null when not provided in the spectrum file.
   */
Napi::Value RiidAnaResult::nuclide_type(const Napi::CallbackInfo& info)
{
  assert( m_ana && (m_index < m_ana->results_.size()) );
  
  const auto &res = m_ana->results_[m_index];
  if( res.nuclide_type_.empty() )
    return Napi::Value();
  return Napi::String::New( info.Env(), res.nuclide_type_ );
}

  /* Returns String describing nuclide confidence.  May be a number (ex. "9"),
   a word (ex "High"), a letter (ex 'L'), or a phrase.
   Will return null if not available.
   */
Napi::Value RiidAnaResult::id_confidence(const Napi::CallbackInfo& info)
{
  assert( m_ana && (m_index < m_ana->results_.size()) );
  
  const auto &res = m_ana->results_[m_index];
  if( res.id_confidence_.empty() )
    return Napi::Value();
  return Napi::String::New( info.Env(), res.id_confidence_ );
}

  /* Returns String giving remark.  Returns one is not provided in spectrum file. */
Napi::Value RiidAnaResult::remark(const Napi::CallbackInfo& info)
{
  assert( m_ana && (m_index < m_ana->results_.size()) );
  
  const auto &res = m_ana->results_[m_index];
  if( res.remark_.empty() )
    return Napi::Value();
  return Napi::String::New( info.Env(), res.remark_ );
}
  
  /* Returns dose rate in micro-sievert.  Returns null if not avaialble.
   */
Napi::Value RiidAnaResult::dose_rate(const Napi::CallbackInfo& info)
{
  assert( m_ana && (m_index < m_ana->results_.size()) );
  
  const auto &res = m_ana->results_[m_index];
  if( res.dose_rate_ <= 0.0 )
    return Napi::Value();
  return Napi::Number::New( info.Env(), static_cast<double>(res.dose_rate_) );
}

/* Returns the name of the detector this result corresponds to.  If null or
   blank then you should assum it is for all detectors in the file.
*/
Napi::Value RiidAnaResult::detector(const Napi::CallbackInfo& info)
{
  assert( m_ana && (m_index < m_ana->results_.size()) );
  
  const auto &res = m_ana->results_[m_index];
  if( res.detector_.empty() )
    return Napi::Value();
  return Napi::String::New( info.Env(), res.detector_ );
}



RiidAnalysis::RiidAnalysis( const Napi::CallbackInfo& info )
  : Napi::ObjectWrap<RiidAnalysis>(info)
{
    
}

  /* Returns Array of Strings representing remarks provided in the spectrum file.
   Returns null if no remarks are provided.
   */
Napi::Value RiidAnalysis::remarks(const Napi::CallbackInfo& info)
{
  assert( m_ana );
  
  if( m_ana->remarks_.empty() )
    return Napi::Value();
  
  auto arr = Napi::Array::New( info.Env() );
  
  for( uint32_t i = 0; i < m_ana->remarks_.size(); ++i )
    arr.Set( i, Napi::String::New(info.Env(), m_ana->remarks_[i]) );
  
  return arr;
}
  
/* A a String giving the unique name of the analysis algorithm.
   Returns null if not provided in the spectrum file.
*/
Napi::Value RiidAnalysis::algorithm_name(const Napi::CallbackInfo& info)
{
  assert( m_ana );
  
  if( m_ana->algorithm_name_.empty() )
    return Napi::Value();
  return Napi::String::New(info.Env(),m_ana->algorithm_name_);
}

  /* Creator or implementer of the analysis algorithm.
   Will return null if not provided in the file.
   */
Napi::Value RiidAnalysis::algorithm_creator(const Napi::CallbackInfo& info)
{
  assert( m_ana );
  
  if( m_ana->algorithm_creator_.empty() )
    return Napi::Value();
  return Napi::String::New(info.Env(),m_ana->algorithm_creator_);
}

  /* Free-form String describing the analysis algorithm. Will be null if not
   provided in the spectrum file.
   */
Napi::Value RiidAnalysis::algorithm_description(const Napi::CallbackInfo& info)
{
  assert( m_ana );
  
  if( m_ana->algorithm_description_.empty() )
    return Napi::Value();
  return Napi::String::New(info.Env(),m_ana->algorithm_description_);
}

  /** Returns free-form String describing the overall conclusion of the analysis regarding
   the source of concern.  Equivalent to <AnalysisResultDescription> or
   <ThreatDescription> tag of N42 2012 or 2006 respectively.
   Will return null if not provided in the file.
   */
Napi::Value RiidAnalysis::algorithm_result_description(const Napi::CallbackInfo& info)
{
  assert( m_ana );
  
  if( m_ana->algorithm_result_description_.empty() )
    return Napi::Value();
  return Napi::String::New(info.Env(),m_ana->algorithm_result_description_);
}


  /* Returns array of RiidAnaResult objects contained in this analysis.  May be
   empty (but wont be null)
   */
Napi::Value RiidAnalysis::results(const Napi::CallbackInfo& info)
{
  assert( m_ana );
  
  auto arr = Napi::Array::New( info.Env() );
  
  uint32_t nset = 0;
  for( size_t i = 0; i < m_ana->results_.size(); ++i )
  {
    if( m_ana->results_[i].isEmpty() )
      continue;
    
    Napi::Object obj = RiidAnaResult::constructor.New( {} );
    RiidAnaResult *res = RiidAnaResult::Unwrap(obj);
    res->m_ana = m_ana;
    res->m_index = i;
    arr.Set( nset++, obj );
  }
  
  return arr;
}//



void SpecRecord::Init( Napi::Env &env, Napi::Object &exports )
{
  SourceType::Init( env, exports );
  OccupancyStatus::Init( env, exports );
  EquationType::Init( env, exports );
  DetectorTypeEnum::Init( env, exports );
  
  Napi::Function func = DefineClass(env, "SpecRecord", {
    InstanceMethod("liveTime", &SpecRecord::live_time),
    InstanceMethod("realTime", &SpecRecord::real_time),
    InstanceMethod("detectorName", &SpecRecord::detector_name),
    InstanceMethod("detectorNumber", &SpecRecord::detector_number),
    InstanceMethod("sampleNumber", &SpecRecord::sample_number),
    InstanceMethod("sourceType", &SpecRecord::source_type),
    InstanceMethod("startTime", &SpecRecord::start_time),
    InstanceMethod("title", &SpecRecord::title),
    InstanceMethod("remarks", &SpecRecord::remarks),
    InstanceMethod("occupied", &SpecRecord::occupied),
    InstanceMethod("gammaCountSum", &SpecRecord::gamma_count_sum),
    InstanceMethod("containedNeutron", &SpecRecord::contained_neutron),
    InstanceMethod("neutronCountsSum", &SpecRecord::neutron_counts_sum),
    InstanceMethod("hasGpsInfo", &SpecRecord::has_gps_info),
    InstanceMethod("latitude", &SpecRecord::latitude),
    InstanceMethod("longitude", &SpecRecord::longitude),
    InstanceMethod("positionTime", &SpecRecord::position_time),
    InstanceMethod("gammaChannelEnergies", &SpecRecord::gamma_channel_energies),
    InstanceMethod("gammaChannelContents", &SpecRecord::gamma_channel_contents),
    InstanceMethod("energyCalibrationModel", &SpecRecord::energy_calibration_model),
    InstanceMethod("energyCalibrationCoeffs", &SpecRecord::energy_calibration_coeffs),
    InstanceMethod("deviationPairs", &SpecRecord::deviation_pairs),
  });
  
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  
  exports.Set("SpecRecord", func);
}//SpecRecord::Init(...)


SpecRecord::SpecRecord( const Napi::CallbackInfo& info )
: Napi::ObjectWrap<SpecRecord>(info)
{
  
}

Napi::Value SpecRecord::live_time(const Napi::CallbackInfo& info)
{
  if( !m_meas )
    return Napi::Value();
  return Napi::Number::New( info.Env(), m_meas->live_time() );
}


Napi::Value SpecRecord::real_time(const Napi::CallbackInfo& info)
{
  if( !m_meas )
    return Napi::Value();
  return Napi::Number::New( info.Env(), m_meas->real_time() );
}

/** Returns String detector name. */
Napi::Value SpecRecord::detector_name(const Napi::CallbackInfo& info)
{
  return Napi::String::New( info.Env(), m_meas->detector_name() );
}


/** Returns Number detector name. */
Napi::Value SpecRecord::detector_number(const Napi::CallbackInfo& info)
{
  return Napi::Number::New( info.Env(), m_meas->detector_number() );
}


/** Returns the integer sample number. */
Napi::Value SpecRecord::sample_number(const Napi::CallbackInfo& info)
{
  return Napi::Number::New( info.Env(), m_meas->sample_number() );
}




/** Returns string indicating source type.  WIll be one of the following values:
 "IntrinsicActivity", "Calibration", "Background", "Foreground", "UnknownSourceType"
 */
Napi::Value SpecRecord::source_type(const Napi::CallbackInfo& info)
{
  return Napi::String::New( info.Env(), to_str(m_meas->source_type()) );
}


/** Returns start time, as a Date object of measurement start, if avaialble, otherwise null. */
Napi::Value SpecRecord::start_time(const Napi::CallbackInfo& info)
{
  if( m_meas->start_time().is_special() )
    return Napi::Value();
  
  const boost::posix_time::ptime epoch(boost::gregorian::date(1970,1,1));
  const auto x = (m_meas->start_time() - epoch).total_milliseconds();
  
  //return Napi::Date::New( info.Env(), static_cast<double>(x) );
  return Napi::Number::New( info.Env(), static_cast<double>(x) );
}

/** Returns the String title.  Not supported by all input spectrum file formats. */
Napi::Value SpecRecord::title(const Napi::CallbackInfo& info)
{
  return Napi::String::New( info.Env(), m_meas->title() );
}

/** Returns an array of strings representing the 'remarks' for this specific spectrum record. */
Napi::Value SpecRecord::remarks(const Napi::CallbackInfo& info)
{
  const auto &rem = m_meas->remarks();
  if( rem.empty() )
    return Napi::Value();
  
  auto arr = Napi::Array::New( info.Env() );
  
  for( uint32_t i = 0; i < rem.size(); ++i )
    arr.Set( i, Napi::String::New(info.Env(), rem[i]) );
  
  return arr;
}

/** Returns a string thats one of the follwoing: "NotOccupied", "Occupied", "UnknownOccupancyStatus" */
Napi::Value SpecRecord::occupied(const Napi::CallbackInfo& info)
{
  const char *val = nullptr;
  
  switch( m_meas->occupied() )
  {
    case Measurement::Occupied:                val = "Occupied"; break;
    case Measurement::NotOccupied:             val = "NotOccupied"; break;
    case Measurement::UnknownOccupancyStatus:  val = "UnknownOccupancyStatus"; break;
  }//switch( m_meas->occupied() )
  
  assert( val );
  
  return Napi::String::New( info.Env(), val );
}


/** Returns float sum of gamma counts. */
Napi::Value SpecRecord::gamma_count_sum(const Napi::CallbackInfo& info)
{
  return Napi::Number::New( info.Env(), m_meas->gamma_count_sum() );
}


/** Returns boolean indicating if neutron data is available. */
Napi::Value SpecRecord::contained_neutron(const Napi::CallbackInfo& info)
{
  return Napi::Boolean::New( info.Env(), m_meas->contained_neutron() );
}

/** Returns float sum of neutron counts. Will return null if neutron data not avaiable. */
Napi::Value SpecRecord::neutron_counts_sum(const Napi::CallbackInfo& info)
{
  if( !m_meas->contained_neutron() )
    return Napi::Value();
  
  return Napi::Number::New( info.Env(), m_meas->neutron_counts_sum() );
}


/** Returns boolean indicating if GPS is available. */
Napi::Value SpecRecord::has_gps_info(const Napi::CallbackInfo& info)
{
  return Napi::Boolean::New( info.Env(), m_meas->has_gps_info() );
}

/** Returns Number latitidue if available, otherwise null. */
Napi::Value SpecRecord::latitude(const Napi::CallbackInfo& info)
{
  if( !m_meas->has_gps_info() )
    return Napi::Value();
  
  return Napi::Number::New( info.Env(), m_meas->latitude() );
}


/** Returns Number longitude if available, otherwise null. */
Napi::Value SpecRecord::longitude(const Napi::CallbackInfo& info)
{
  if( !m_meas->has_gps_info() )
    return Napi::Value();
  
  return Napi::Number::New( info.Env(), m_meas->longitude() );
}


/** Returns Date object of GPS fix.  Null if not avaialble. */
Napi::Value SpecRecord::position_time(const Napi::CallbackInfo& info)
{
  if( !m_meas->has_gps_info() || m_meas->position_time().is_special() )
    return Napi::Value();
  
  const boost::posix_time::ptime epoch(boost::gregorian::date(1970,1,1));
  const auto x = (m_meas->position_time() - epoch).total_milliseconds();
  
  //return Napi::Date::New( info.Env(), static_cast<double>(x) );
  return Napi::Number::New( info.Env(), static_cast<double>(x) );
}

/* Returns an array of numbers representign the lower energy, in keV, of each gamma channel.
 If this SpecRecord did not have gamma data associated with it, will return null.
 */
Napi::Value SpecRecord::gamma_channel_energies(const Napi::CallbackInfo& info)
{
  const auto &energies = m_meas->channel_energies();
  
  if( !energies || energies->empty() )
    return Napi::Value();
  
  auto arr = Napi::Array::New( info.Env() );
  
  for( uint32_t i = 0; i < energies->size(); ++i )
    arr.Set( i, Napi::Number::New(info.Env(), (*energies)[i]) );
  
  return arr;
}

/* Returns an array of numbers representign the gamma channel counts.
 If this SpecRecord did not have gamma data associated with it, will return null.
 */
Napi::Value SpecRecord::gamma_channel_contents(const Napi::CallbackInfo& info)
{
  const auto &counts = m_meas->gamma_counts();
  
  if( !counts || counts->empty() )
  return Napi::Value();
  
  auto arr = Napi::Array::New( info.Env() );
  
  for( uint32_t i = 0; i < counts->size(); ++i )
    arr.Set( i, Napi::Number::New(info.Env(), (*counts)[i]) );
  
  return arr;
}


/* Returns a string in EquationType, i.e.
 ["Polynomial","FullRangeFraction","LowerChannelEdge",
 "UnspecifiedUsingDefaultPolynomial","InvalidEquationType"]
 */
Napi::Value SpecRecord::energy_calibration_model(const Napi::CallbackInfo& info)
{
  return Napi::String::New(info.Env(), to_str(m_meas->energy_calibration_model()) );
}


/* Aray of numbers representing the energy calibration coefficients.
 Interpretation is dependant on the energy_calibration_model.
 */
Napi::Value SpecRecord::energy_calibration_coeffs(const Napi::CallbackInfo& info)
{
  auto arr = Napi::Array::New( info.Env() );
  
  const auto coefs = m_meas->calibration_coeffs();
  for( uint32_t i = 0; i < coefs.size(); ++i )
    arr.Set( i, Napi::Number::New(info.Env(), coefs[i]) );
  
  return arr;
}

/* Array of deviation pairs.
 Ex. [[0,0],[122,15],661,-13],[2614,0]]
 */
Napi::Value SpecRecord::deviation_pairs(const Napi::CallbackInfo& info)
{
  auto arr = Napi::Array::New( info.Env() );
  
  const auto coefs = m_meas->deviation_pairs();
  for( uint32_t i = 0; i < coefs.size(); ++i )
  {
    auto dev = Napi::Array::New( info.Env() );
    dev.Set( static_cast<uint32_t>(0), Napi::Number::New(info.Env(), static_cast<double>(coefs[i].first)) );
    dev.Set( static_cast<uint32_t>(1), Napi::Number::New(info.Env(), static_cast<double>(coefs[i].second)) );
    
    arr.Set( i, dev );
  }
  
  return arr;
  
}





Napi::Object SpecFile::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);
  
  Napi::Function func = DefineClass(env, "SpecFile", {
    InstanceMethod("gammaLiveTime", &SpecFile::gamma_live_time),
    InstanceMethod("gammaRealTime", &SpecFile::gamma_real_time),
    InstanceMethod("gammaCountSum", &SpecFile::gamma_count_sum),
    InstanceMethod("containedNeutrons", &SpecFile::contained_neutrons),
    InstanceMethod("neutronCountSum", &SpecFile::neutron_counts_sum),
    InstanceMethod("numGammaChannels", &SpecFile::num_gamma_channels),
    InstanceMethod("numSpecRecords", &SpecFile::num_spec_records),
    InstanceMethod("inferredInstrumentModel", &SpecFile::inferred_instrument_model),
    InstanceMethod("manufacturer", &SpecFile::manufacturer),
    InstanceMethod("instrumentType", &SpecFile::instrument_type),
    InstanceMethod("instrumentModel", &SpecFile::instrument_model),
    InstanceMethod("instrumentId", &SpecFile::instrument_id),
    InstanceMethod("serialNumber", &SpecFile::serial_number),
    InstanceMethod("uuid", &SpecFile::uuid),
    InstanceMethod("isSearchMode", &SpecFile::passthrough),
    InstanceMethod("isPassthrough", &SpecFile::passthrough),
    InstanceMethod("filename", &SpecFile::filename),
    InstanceMethod("remarks", &SpecFile::remarks),
    InstanceMethod("detectorNames", &SpecFile::detector_names),
    InstanceMethod("sampleNumbers", &SpecFile::sample_numbers),
    InstanceMethod("measurements", &SpecFile::measurements),
    InstanceMethod("sumMeasurements", &SpecFile::sum_measurements),
    InstanceMethod("sumRecords", &SpecFile::sum_measurements),
    InstanceMethod("records", &SpecFile::measurements),
    InstanceMethod("hasGpsInfo", &SpecFile::has_gps_info),
    InstanceMethod("meanLatitude", &SpecFile::mean_latitude),
    InstanceMethod("meanLongitude", &SpecFile::mean_longitude),
    InstanceMethod("riidAnalysis", &SpecFile::riid_analysis),
    InstanceMethod("writeToFile", &SpecFile::write_to_file),
    
    
    /* To define an enum like specutils.SpecFile.SomeValue in JS do: */
    /* , StaticValue( "SomeValue", Napi::Number::New( env, 10 ), napi_property_attributes::napi_enumerable) */
    /* The best example I could find of including different features is in:
     https://github.com/nodejs/node-addon-api/blob/master/test/objectwrap.js
     https://github.com/nodejs/node-addon-api/blob/master/test/objectwrap.cc
     */
  });
  
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  
  exports.Set("SpecFile", func);
  
  SpecRecord::Init( env, exports );
  RiidAnalysis::Init( env, exports );
  RiidAnaResult::Init( env, exports );
  
  return exports;
}//SpecFile::Init(...)


SpecFile::SpecFile(const Napi::CallbackInfo& info)
  : Napi::ObjectWrap<SpecFile>(info)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  
  int length = info.Length();
  
  if (length != 1 || !info[0].IsString() ) {
    Napi::TypeError::New(env, "Expected String Path To File").ThrowAsJavaScriptException();
  }
  
  const std::string path = info[0].ToString().Utf8Value();
  
  auto ptr = std::make_shared<MeasurementInfo>();
  const bool loaded = ptr->load_file( path, ParserType::kAutoParser );
  if( !loaded ){
    Napi::TypeError::New(env, "Could not decode as a spectrum file.").ThrowAsJavaScriptException();
  }
  
  m_spec = ptr;
}//SpecFile constructor

SpecFile::~SpecFile()
{
}

Napi::Value SpecFile::gamma_live_time(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::Number::New( info.Env(), m_spec->gamma_live_time() );
}


Napi::Value SpecFile::gamma_real_time(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::Number::New( info.Env(), m_spec->gamma_real_time() );
}


Napi::Value SpecFile::gamma_count_sum(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::Number::New( info.Env(), m_spec->gamma_count_sum() );
}


Napi::Value SpecFile::contained_neutrons(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::Boolean::New( info.Env(), !m_spec->neutron_detector_names().empty() );
}

Napi::Value SpecFile::neutron_counts_sum(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::Number::New( info.Env(), m_spec->neutron_counts_sum() );
}

Napi::Value SpecFile::num_gamma_channels(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::Number::New( info.Env(), m_spec->num_gamma_channels() );
}


Napi::Value SpecFile::num_spec_records(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::Number::New( info.Env(), m_spec->num_measurements() );
}


Napi::Value SpecFile::inferred_instrument_model(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::String::New( info.Env(), detectorTypeToString(m_spec->detector_type()) );
}


Napi::Value SpecFile::instrument_type(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::String::New( info.Env(), m_spec->instrument_type() );
}


Napi::Value SpecFile::manufacturer(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::String::New( info.Env(), m_spec->manufacturer() );
}


Napi::Value SpecFile::instrument_model(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::String::New( info.Env(), m_spec->instrument_model() );
}


Napi::Value SpecFile::instrument_id(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::String::New( info.Env(), m_spec->instrument_id() );
}

Napi::Value SpecFile::serial_number(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::String::New( info.Env(), m_spec->instrument_id() );
}

Napi::Value SpecFile::uuid(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::String::New( info.Env(), m_spec->uuid() );
}


Napi::Value SpecFile::passthrough(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::Boolean::New( info.Env(), m_spec->passthrough() );
}

Napi::Value SpecFile::filename(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::String::New( info.Env(), m_spec->filename() );
}

Napi::Value SpecFile::remarks(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  
  auto arr = Napi::Array::New( info.Env() );
  
  const std::vector<std::string> &remarks = m_spec->remarks();
  
  for( uint32_t i = 0; i < remarks.size(); ++i )
    arr.Set( i, Napi::String::New(info.Env(), remarks[i]) );
  
  return arr;
}


Napi::Value SpecFile::detector_names(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  const std::vector<std::string> &names = m_spec->detector_names();
  
  auto arr = Napi::Array::New( info.Env() );
  for( uint32_t i = 0; i < names.size(); ++i )
    arr.Set( i, Napi::String::New(info.Env(), names[i]) );
  
  return arr;
}


Napi::Value SpecFile::sample_numbers(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  const std::set<int> &samples = m_spec->sample_numbers();
  
  auto arr = Napi::Array::New( info.Env() );
  uint32_t index = 0;
  for( const int sn : samples )
    arr.Set( index++, Napi::Number::New(info.Env(), static_cast<double>(sn)) );
  
  return arr;
}


std::set<std::string> SpecFile::to_valid_det_names( Napi::Value value, const Napi::Env &env )
{
  auto check_name = [&]( const std::string &n ){
    const std::vector<std::string> &names = m_spec->detector_names();
    if( std::find(std::begin(names), std::end(names), n) == std::end(names) )
    {
      std::string validnames;
      for( size_t i = 0; i < names.size(); ++i )
        validnames += (i ? ", '" : "'") + names[i] + "'";
      Napi::Error::New(env, "Detector name '" + n + "' is not a valid detector"
                       " name for this file; valid names are: [" + validnames + "].").ThrowAsJavaScriptException();
    }
  };//check_name lambda
  
  if( value.IsNull() )
  {
    std::set<std::string> names;
    for( const std::string &n : m_spec->detector_names() )
      names.insert( n );
    return names;
  }
  
    
  if( value.IsString() )
  {
    const std::string det = value.ToString().Utf8Value();
    check_name( det );
    return std::set<std::string>{ det };
  }
  
  if( !value.IsArray() )
  {
    Napi::TypeError::New(env, "First argument to SpecFile.measurements must be null,"
                         " a string that is a detector name, or an array of strings"
                         " giving detector names.").ThrowAsJavaScriptException();
  }
  
  std::set<std::string> detnames;
  Napi::Array arr = Napi::Array( env, value );
  
  for( uint32_t i = 0; i < arr.Length(); ++i )
  {
    if( !arr.Get(i).IsString() )
      Napi::TypeError::New(env, "First argument to SpecFile.measurements must be null,"
                            " a string that is a detector name, or an array of strings"
                            " giving detector names.").ThrowAsJavaScriptException();
    const std::string det = arr.Get(i).ToString().Utf8Value();
    
    check_name( det );
    detnames.insert( det );
  }
    
  return detnames;
}//std::vector<std::string> to_valid_det_names( Napi::Value value )


std::set<int> SpecFile::to_valid_sample_numbers( Napi::Value value, const Napi::Env &env )
{
  auto check_sample_num = [&]( const int sample ){
    const std::set<int> &samples = m_spec->sample_numbers();
    if( !samples.count(sample) )
    {
      std::string validnames;
      for( auto iter = std::begin(samples); iter != std::end(samples); ++iter )
        validnames += (iter != std::begin(samples) ? "," : "") + std::to_string(*iter);
      Napi::Error::New(env, "Sample number " + std::to_string(sample) + " is not valid"
                       " for this file; valid sample numbers are: [" + validnames + "].").ThrowAsJavaScriptException();
    }
  };//check_name lambda
  
  if( value.IsNull() )
    return m_spec->sample_numbers();
  
  if( value.IsNumber() )
  {
    const int32_t sample = value.ToNumber().Int32Value();
    check_sample_num( sample );
    return std::set<int>{ sample };
  }
  
  if( !value.IsArray() )
  {
    Napi::TypeError::New(env, "Second argument to SpecFile.measurements must be null,"
                         " a integer sample number, or an array of integer sample numbers.").ThrowAsJavaScriptException();
  }
  
  std::set<int> samplenums;
  Napi::Array arr = Napi::Array( env, value );
  
  for( uint32_t i = 0; i < arr.Length(); ++i )
  {
    if( !arr.Get(i).IsNumber() )
      Napi::TypeError::New(env, "Second argument to SpecFile.measurements must be null,"
                            " a integer sample number, or an array of integer sample numbers.").ThrowAsJavaScriptException();
    const int32_t sample = arr.Get(i).ToNumber().Int32Value();
    check_sample_num( sample );
    samplenums.insert( sample );
  }
  
  return samplenums;
}//std::set<int> to_valid_sample_numbers( Napi::Value value )


std::set<std::string> SpecFile::to_valid_source_types( Napi::Value value, const Napi::Env &env )
{
  auto check_source_type = [&]( const std::string &n ){
    if( n == to_str(Measurement::Background)
       || n == to_str(Measurement::Calibration)
       || n == to_str(Measurement::Foreground)
       || n == to_str(Measurement::IntrinsicActivity)
       || n == to_str(Measurement::UnknownSourceType) )
      return;
    
    Napi::Error::New(env, "Source type '" + n + "' is not a valid; must be one of ['"
                     + std::string(to_str(Measurement::Background))
                     + "', '" + std::string(to_str(Measurement::Calibration))
                     + "', '" + std::string(to_str(Measurement::Foreground))
                     + "', '" + std::string(to_str(Measurement::IntrinsicActivity))
                     + "', '" + std::string(to_str(Measurement::UnknownSourceType))
                     + "']"
                     ).ThrowAsJavaScriptException();
  };//check_source_type lambda
  
  if( value.IsNull() )
  {
    return std::set<std::string>{ to_str(Measurement::Background),
      to_str(Measurement::Calibration), to_str(Measurement::Foreground),
      to_str(Measurement::IntrinsicActivity), to_str(Measurement::UnknownSourceType)
    };
  }//if( value.IsNull() )
  
  
  if( value.IsString() )
  {
    const std::string det = value.ToString().Utf8Value();
    check_source_type( det );
    return std::set<std::string>{ det };
  }
  
  if( !value.IsArray() )
  {
    Napi::TypeError::New(env, "Third argument to SpecFile.measurements must be null,"
                         " a string that is a SourceType, or an array of strings"
                         " giving SourceType's.").ThrowAsJavaScriptException();
  }
  
  std::set<std::string> source_types;
  Napi::Array arr = Napi::Array( env, value );
    
  for( uint32_t i = 0; i < arr.Length(); ++i )
  {
    if( !arr.Get(i).IsString() )
      Napi::TypeError::New(env, "Third argument to SpecFile.measurements must be null,"
                             " a string that is a SourceType, or an array of strings"
                             " giving SourceType's.").ThrowAsJavaScriptException();
    const std::string source_type = arr.Get(i).ToString().Utf8Value();
      
    check_source_type( source_type );
    source_types.insert( source_type );
  }
  
  return source_types;
}//std::vector<std::string> to_valid_source_types( Napi::Value value )



Napi::Value SpecFile::measurements(const Napi::CallbackInfo& info)
{
  assert( m_spec );

  const int nargs = info.Length();
  
  const std::set<std::string> detnames = SpecFile::to_valid_det_names( (nargs >= 1 ? info[0]  : Napi::Value()), info.Env() );
  const std::set<int> samplenums = SpecFile::to_valid_sample_numbers( (nargs >= 2 ? info[1]  : Napi::Value()), info.Env() );
  const std::set<std::string> source_types = SpecFile::to_valid_source_types( (nargs >= 3 ? info[2]  : Napi::Value()), info.Env() );
  
  
  auto arr = Napi::Array::New( info.Env() );
  
  std::vector< std::shared_ptr<const Measurement> > meass = m_spec->measurements();
  
  uint32_t index = 0;
  for( size_t i = 0; i < meass.size(); ++i )
  {
    if( !meass[i] )  //shouldnt ever happen, but jic
      continue;
    
    const Measurement &m = *meass[i];
    
    if( !samplenums.count(m.sample_number()) )
      continue;
    if( !detnames.count(m.detector_name()) )
      continue;
    if( !source_types.count(to_str(m.source_type())) )
      continue;
    
    Napi::Object obj = SpecRecord::constructor.New( {} );
    SpecRecord *record = SpecRecord::Unwrap(obj);
    record->m_meas = meass[i];
    arr.Set( index++, obj );
  }
  
  return arr;
}


Napi::Value SpecFile::sum_measurements(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  
  const int nargs = info.Length();
  
  //Lets make sure people dont try to filter by SourceType like measurements()
  //if( nargs >= 3 )
  //  Napi::Error::New( info.Env(), "SpecFile.sumMeasurements only accepts 0, 1, or 2 arguments." ).ThrowAsJavaScriptException();
  
  const std::set<std::string> input_detnames = SpecFile::to_valid_det_names( (nargs >= 1 ? info[0]  : Napi::Value()), info.Env() );
  const std::set<int> input_samplenums = SpecFile::to_valid_sample_numbers( (nargs >= 2 ? info[1]  : Napi::Value()), info.Env() );
  const std::set<std::string> input_source_types = SpecFile::to_valid_source_types( (nargs >= 3 ? info[2]  : Napi::Value()), info.Env() );
  
  /* Super slow and inefficient.
   If any {DetectorName,SampleNumber} has a SourceType in input_source_types,
   then all detectors for the SampleNumber will be included in the sum, even if one
   of the detectors SourceType is not valid.
   */
  std::set<std::string> detnames;
  std::set<int> samplenums;
  const auto meass = m_spec->measurements();
  for( const auto &m : meass )
  {
    if( !m || !input_samplenums.count(m->sample_number())
       || !input_detnames.count(m->detector_name())
       || !input_source_types.count( to_str(m->source_type()) ) )
      continue;
    detnames.insert( m->detector_name() );
    samplenums.insert( m->sample_number() );
  }//

  
  const std::vector<std::string> detectornames( std::begin(detnames), std::end(detnames) );
  
  std::shared_ptr<Measurement> meas;
  try
  {
    meas = m_spec->sum_measurements( samplenums, detectornames );
  }catch( std::exception &e )
  {
    Napi::Error::New( info.Env(), "Failed summing SpecRecords: "
                         + std::string(e.what())).ThrowAsJavaScriptException();
  }
  
  if( !meas )
    Napi::Error::New( info.Env(), "There were no SpecRecords to sum with input filters." ).ThrowAsJavaScriptException();
  
  Napi::Object obj = SpecRecord::constructor.New( {} );
  SpecRecord *record = SpecRecord::Unwrap(obj);
  record->m_meas = meas;
  
  return obj;
}//Napi::Value sum_measurements(const Napi::CallbackInfo& info);



Napi::Value SpecFile::has_gps_info(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  return Napi::Boolean::New( info.Env(), m_spec->has_gps_info() );
}

Napi::Value SpecFile::mean_latitude(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  if( !m_spec->has_gps_info() )
    return Napi::Value();
  
  return Napi::Number::New( info.Env(), m_spec->mean_latitude() );
}

Napi::Value SpecFile::mean_longitude(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  
  if( !m_spec->has_gps_info() )
    return Napi::Value();
  return Napi::Number::New( info.Env(), m_spec->mean_longitude() );
}


Napi::Value SpecFile::riid_analysis(const Napi::CallbackInfo& info)
{
  const std::shared_ptr<const DetectorAnalysis> ana = m_spec->detectors_analysis();
  if( !ana )
    return Napi::Value();
  
  Napi::Object obj = RiidAnalysis::constructor.New( {} );
  RiidAnalysis *riidana = RiidAnalysis::Unwrap(obj);
  riidana->m_ana = ana;
  
  return obj;
}


Napi::Value SpecFile::write_to_file(const Napi::CallbackInfo& info)
{
  int length = info.Length();
  
  if (length < 2 || !info[0].IsString() || !info[1].IsString() || (length>=3 && !info[2].IsBoolean()) ) {
    Napi::TypeError::New(info.Env(), "Expected path to save to, and format").ThrowAsJavaScriptException();
  }
  
  const std::string path = info[0].ToString().Utf8Value();
  const std::string format = info[1].ToString().Utf8Value();
  const bool force = length<3 ? false : info[2].ToBoolean().Value();
  
  
  SaveSpectrumAsType type = kNumSaveSpectrumAsType;
  if( format == "TXT" )
    type = SaveSpectrumAsType::kTxtSpectrumFile;
  else if( format == "CSV" )
    type = SaveSpectrumAsType::kCsvSpectrumFile;
  else if( format == "PCF" )
    type = SaveSpectrumAsType::kPcfSpectrumFile;
  else if( format == "N42-2006" )
    type = SaveSpectrumAsType::kXmlSpectrumFile;
  else if( format == "N42-2012" )
    type = SaveSpectrumAsType::k2012N42SpectrumFile;
  else if( format == "CHN" )
    type = SaveSpectrumAsType::kChnSpectrumFile;
  else if( format == "SPC-int" )
    type = SaveSpectrumAsType::kBinaryIntSpcSpectrumFile;
  else if( format == "SPC" || format == "SPC-float" )
    type = SaveSpectrumAsType::kBinaryFloatSpcSpectrumFile;
  else if( format == "SPC-ascii" )
    type = SaveSpectrumAsType::kAsciiSpcSpectrumFile;
  else if( format == "GR130v0" )
    type = SaveSpectrumAsType::kExploraniumGr130v0SpectrumFile;
  else if( format == "GR135v2" )
    type = SaveSpectrumAsType::kExploraniumGr135v2SpectrumFile;
  else if( format == "SPE" || format == "IAEA" )
    type = SaveSpectrumAsType::kIaeaSpeSpectrumFile;
#if( SpecUtils_ENABLE_D3_CHART )
  else if( format == "HTML" )
    type = SaveSpectrumAsType::kD3HtmlSpectrumFile;
#endif
  else {
    Napi::Error::New(info.Env(), "Invalid file-type specification").ThrowAsJavaScriptException();
    return Napi::Value();
  }
  
  assert( type != kNumSaveSpectrumAsType );
  
  if( force && UtilityFunctions::is_file(path) )
    UtilityFunctions::remove_file(path);
  
  try
  {
    m_spec->write_to_file( path, type );
  }catch( std::exception &e )
  {
    Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
  }
  
  return Napi::Value();
}//write_to_file(...)
  
