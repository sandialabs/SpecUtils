#include <string>

#include <napi.h>

#include "SpecUtilsJS.h"
#include "SpecUtils/SpectrumDataStructs.h"



Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  return SpecFile::Init(env, exports);
}

NODE_API_MODULE(SpecUtils, InitAll)


Napi::FunctionReference SpecRecord::constructor;
Napi::FunctionReference SpecFile::constructor;



void SpecRecord::Init( Napi::Env &env, Napi::Object &exports )
{
  Napi::Function func = DefineClass(env, "SpecRecord", {
    InstanceMethod("liveTime", &SpecRecord::live_time),
    InstanceMethod("realTime", &SpecRecord::real_time),
  });
  
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  
  exports.Set("SpecRecord", func);
}


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
    InstanceMethod("measurements", &SpecFile::measurements),
    InstanceMethod("records", &SpecFile::measurements),
    InstanceMethod("hasGpsInfo", &SpecFile::has_gps_info),
    InstanceMethod("meanLatitude", &SpecFile::mean_latitude),
    InstanceMethod("meanLongitude", &SpecFile::mean_longitude),
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
  
  return exports;
}

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

Napi::Value SpecFile::measurements(const Napi::CallbackInfo& info)
{
  assert( m_spec );
  
  auto arr = Napi::Array::New( info.Env() );
  
  std::vector< std::shared_ptr<const Measurement> > meass = m_spec->measurements();
  
  for( uint32_t i = 0; i < meass.size(); ++i )
  {
    Napi::Object obj = SpecRecord::constructor.New( {} );
    SpecRecord *record = SpecRecord::Unwrap(obj);
    record->m_meas = meass[i];
    arr.Set( i, obj );
  }
  
  return arr;
}

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



Napi::Value SpecFile::write_to_file(const Napi::CallbackInfo& info)
{
  int length = info.Length();
  
  if (length != 2 || !info[0].IsString() || !info[1].IsString() ) {
    Napi::TypeError::New(info.Env(), "Expected path to save to, and format").ThrowAsJavaScriptException();
  }
  
  const std::string path = info[0].ToString().Utf8Value();
  const std::string format = info[1].ToString().Utf8Value();
  
  
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
  
  try
  {
    m_spec->write_to_file( path, type );
  }catch( std::exception &e )
  {
    Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
  }
  
  return Napi::Value();
}//write_to_file(...)
  
