/**
 SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 Copyright (C) 2016 William Johnson
 
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

#include <cmath>
#include <cctype>
#include <memory>
#include <string>
#include <limits>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <float.h>
#include <fstream>
#include <numeric>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <type_traits>

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_print.hpp"
#include "rapidxml/rapidxml_utils.hpp"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/Filesystem.h"
#include "SpecUtils/SpecUtilsAsync.h"
#include "SpecUtils/RapidXmlUtils.hpp"
#include "SpecUtils/SpecFile_location.h"
#include "SpecUtils/EnergyCalibration.h"

using namespace std;

static_assert( RAPIDXML_USE_SIZED_INPUT_WCJOHNS == 1,
               "The modified version of RapidXml is somehow not being used to compile SpecFile" );


#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#if( defined(_MSC_VER) && _MSC_VER <= 1700 )
#define SRC_LOCATION (std::string("File " TOSTRING(__FILE__) ": Function '") \
+  std::string(__FUNCTION__) \
+ std::string( "': Line " TOSTRING(__LINE__)))
#else
#define SRC_LOCATION (std::string("File " TOSTRING(__FILE__) ": Function '") \
+ std::string(__func__) \
+ std::string( "': Line " TOSTRING(__LINE__)))
#endif


namespace
{
  static const char * const s_parser_warn_prefix = "Parser Warning: ";
  
  //Sometimes a detector wont have a name, but we still need to refer to it in
  //  2012 XML files in various places, so we'll use s_unnamed_det_placeholder
  //  to do this.  A side effect to this is we have to be careful to use it
  //  everywhere, and account for it when reading it back in.
  const std::string s_unnamed_det_placeholder = "unamed";
  
  static const char * const s_energy_cal_not_available_remark = "Energy calibration not available.";
  
  static const std::string s_frf_to_poly_remark = "Energy calibration was originally specified as full-range-fraction.";

  
  //Take absolute difference between unsigned integers.
  template<typename T>
  T abs_diff(T a, T b) {
    return a > b ? a - b : b - a;
  }
  
  
  bool dev_pair_less_than( const std::pair<float,float> &lhs,
                          const std::pair<float,float> &rhs )
  {
    return (lhs.first < rhs.first);
  }
  
  bool toInt( const std::string &str, int &f )
  {
    const int nconvert = sscanf( str.c_str(), "%i", &f );
    return (nconvert == 1);
  }
  
  bool xml_value_to_flt( const rapidxml::xml_base<char> *node, float &val )
  {
    val = 0.0f;
    if( !node )
      return false;
    return SpecUtils::parse_float( node->value(), node->value_size(), val );
  }
  
  std::string get_n42_xmlns( const rapidxml::xml_node<char> *node )
  {
    const char * const default_xmlns = "dndons:"; //or maybe "n42:"
    if( !node )
      return default_xmlns;
    
    string node_name = SpecUtils::xml_name_str(node);
    size_t colon_pos = node_name.find(':');
    if( (colon_pos != string::npos) && (colon_pos > 0) && ((colon_pos+1) < node_name.size()) )
      return node_name.substr( 0, colon_pos + 1 );
    
    node_name = SpecUtils::xml_name_str(node->parent());
    colon_pos = node_name.find(':');
    if( (colon_pos != string::npos) && (colon_pos > 0) && ((colon_pos+1) < node_name.size()) )
      return node_name.substr( 0, colon_pos + 1 );
    
    if( node->first_node() )
    {
      node_name = SpecUtils::xml_name_str(node->first_node());
      colon_pos = node_name.find(':');
      if( (colon_pos != string::npos) && (colon_pos > 0) && ((colon_pos+1) < node_name.size()) )
        return node_name.substr( 0, colon_pos + 1 );
      
      node_name = SpecUtils::xml_name_str(node->first_node()->first_node());
      colon_pos = node_name.find(':');
      if( (colon_pos != string::npos) && (colon_pos > 0) && ((colon_pos+1) < node_name.size()) )
        return node_name.substr( 0, colon_pos + 1 );
    }//if( node->first_node() )
    
    const rapidxml::xml_node<char> *docnode = node->document();
    if( !docnode )
      return default_xmlns;
    if( docnode->name_size() == 0 )
      docnode = docnode->first_node(); //N42InstrumentData
    
    for( auto attrib = docnode->first_attribute(); attrib; attrib = attrib->next_attribute() )
    {
      //Some files use the "n42ns" namespace, IDK
      const string name = SpecUtils::xml_name_str(attrib);
      if( SpecUtils::starts_with(name, "xmlns:" )
          && (SpecUtils::icontains(name, "n42") || SpecUtils::icontains(name, "dndons")) )
        return name.substr(6) + ":";
    }//for( check for xmlns:n42ns="..."
    
    return default_xmlns;
  }//std::string get_n42_xmlns( const rapidxml::xml_node<char> *node )


  void add_multimedia_data_to_2012_N42(
                        const vector<shared_ptr<const SpecUtils::MultimediaData>> &multimedia_data,
                        rapidxml::xml_node<char> * const RadInstrumentData )
  {
    using namespace rapidxml;
    
    assert( RadInstrumentData );
    if( !RadInstrumentData )
      return;
    
    xml_document<char> *doc = RadInstrumentData->document();
    assert( doc );
    if( !doc )
      return;
    
    for( const shared_ptr<const SpecUtils::MultimediaData> &data_ptr : multimedia_data )
    {
      assert( data_ptr );
      if( !data_ptr )
        continue;
      
      const auto &data = *data_ptr;
      
      xml_node<char> *MultimediaData = doc->allocate_node( node_element, "MultimediaData" );
      RadInstrumentData->append_node( MultimediaData );
      
      if( !data.remark_.empty() )
      {
        char *val = doc->allocate_string( data.remark_.c_str(), data.remark_.size() + 1 );
        xml_node<char> *node = doc->allocate_node( node_element, "Remark", val );
        MultimediaData->append_node( node );
      }
      
      if( !data.descriptions_.empty() )
      {
        char *val = doc->allocate_string( data.descriptions_.c_str(), data.descriptions_.size() + 1 );
        xml_node<char> *node = doc->allocate_node( node_element, "MultimediaDataDescription", val );
        MultimediaData->append_node( node );
      }
      
      if( !data.data_.empty() )
      {
        const char *node_name = nullptr;
        switch( data.data_encoding_ )
        {
          case SpecUtils::MultimediaData::EncodingType::BinaryUTF8:
            node_name = "BinaryUTF8Object";
            break;
          case SpecUtils::MultimediaData::EncodingType::BinaryHex:
            node_name = "BinaryHexObject";
            break;
          case SpecUtils::MultimediaData::EncodingType::BinaryBase64:
            node_name = "BinaryBase64Object";
            break;
        }//switch( data.data_encoding_ )
        
        assert( node_name );
        if( node_name )
        {
          const size_t data_size = data.data_.size();
          char *val = doc->allocate_string( nullptr, data_size + 1 );
          memcpy( val, data.data_.data(), data_size );
          val[data_size] = '\0';
          
          xml_node<char> *node = doc->allocate_node( node_element, node_name, val, 0, data.data_.size() );
          MultimediaData->append_node( node );
        }
      }//if( !data.data_.empty() )
      
      
      if( !SpecUtils::is_special(data.capture_start_time_) )
      {
        const string dt = SpecUtils::to_extended_iso_string(data.capture_start_time_) + "Z";
        char *val = doc->allocate_string( dt.c_str(), dt.size()+1 );
        xml_node<char> *node = doc->allocate_node( node_element, "MultimediaCaptureStartDateTime", val );
        MultimediaData->append_node( node );
      }//if( !is_special(data.capture_start_time_) )
      
      //<MultimediaCaptureDuration>,
      
      if( !data.file_uri_.empty() )
      {
        char *val = doc->allocate_string( data.file_uri_.c_str(), data.file_uri_.size() + 1 );
        xml_node<char> *node = doc->allocate_node( node_element, "MultimediaFileURI", val );
        MultimediaData->append_node( node );
      }
      
      //<MultimediaFileSizeValue>,
      //<MultimediaDataMIMEKind>,
      
      if( !data.mime_type_.empty() )
      {
        char *val = doc->allocate_string( data.mime_type_.c_str(), data.mime_type_.size() + 1 );
        xml_node<char> *node = doc->allocate_node( node_element, "MultimediaDataMIMEKind", val );
        MultimediaData->append_node( node );
      }
      
      //<MultimediaDeviceCategoryCode>,
      //<MultimediaDeviceIdentifier>,
      //<ImagePerspectiveCode>,
      //<ImageWidthValue>,
      //<ImageHeightValue>,
      //<MultimediaDataExtension>
    }//for( const SpecUtils::MultimediaData &data : multimedia_data )
  }//add_multimedia_data_to_2012_N42(...)


  bool set_multimedia_data( SpecUtils::MultimediaData &data,
                            const rapidxml::xml_node<char> * const multimedia_node )
  {
    if( !multimedia_node )
      return false;
    
    auto node = XML_FIRST_INODE(multimedia_node,"Remark");
    if( node )
      data.remark_ = SpecUtils::xml_value_str( node );
    
    node = XML_FIRST_INODE(multimedia_node,"MultimediaDataDescription");
    if( node )
      data.descriptions_ = SpecUtils::xml_value_str( node );
    
    data.data_.resize( 0 );
    data.data_encoding_ = SpecUtils::MultimediaData::EncodingType::BinaryHex;
    const char *data_begin = nullptr, *data_end = nullptr;
    if( (node = XML_FIRST_INODE(multimedia_node,"BinaryUTF8Object")) )
    {
      data.data_encoding_ = SpecUtils::MultimediaData::EncodingType::BinaryUTF8;
      data_begin = (const char *)node->value();
      data_end = data_begin + node->value_size();
    }else if( (node = XML_FIRST_INODE(multimedia_node,"BinaryHexObject")) )
    {
      data.data_encoding_ = SpecUtils::MultimediaData::EncodingType::BinaryHex;
      data_begin = (const char *)node->value();
      data_end = data_begin + node->value_size();
    }else if( (node = XML_FIRST_INODE(multimedia_node,"BinaryBase64Object")) )
    {
      data.data_encoding_ = SpecUtils::MultimediaData::EncodingType::BinaryBase64;
      data_begin = (const char *)node->value();
      data_end = data_begin + node->value_size();
    }
    
    if( data_begin && data_end )
      data.data_.insert( end(data.data_), data_begin, data_end );
      
    node = XML_FIRST_INODE(multimedia_node,"MultimediaCaptureStartDateTime");
    const string capture_time_str = SpecUtils::xml_value_str(node);
    if( !capture_time_str.empty() )
      data.capture_start_time_ = SpecUtils::time_from_string( capture_time_str.c_str() );
    
    //<MultimediaCaptureDuration>,
      
    node = XML_FIRST_INODE(multimedia_node,"MultimediaFileURI");
    if( node )
      data.file_uri_ = SpecUtils::xml_value_str( node );
      
    //<MultimediaFileSizeValue>,
    //<MultimediaDataMIMEKind>,
      
    
    node = XML_FIRST_INODE(multimedia_node,"MultimediaDataMIMEKind");
    if( node )
      data.mime_type_ = SpecUtils::xml_value_str( node );
      
    //<MultimediaDeviceCategoryCode>,
    //<MultimediaDeviceIdentifier>,
    //<ImagePerspectiveCode>,
    //<ImageWidthValue>,
    //<ImageHeightValue>,
    //<MultimediaDataExtension>
    
    if( data.file_uri_.empty() && data.data_.empty() )
      return false;
    
    return true;
  }//set_multimedia_data(...)

}//anonomous namespace for XML utilities


//getCalibrationToSpectrumMap(...): builds map from the binning shared vector to
//  the the index of a std::shared_ptr<Measurement> that has that binning
namespace
{
  typedef std::map< std::shared_ptr<const SpecUtils::EnergyCalibration>, size_t > EnergyCalToIndexMap;
  
//add_calibration_to_2012_N42_xml(): writes calibration information to the
//  xml document, with the "id" == caliId for the <EnergyCalibration> tag.
//  Note, this funciton should probably be protected or private, but isnt
//  currently due to an implementation detail.
//  If invalid equation type, will throw exception.
void add_calibration_to_2012_N42_xml( const SpecUtils::EnergyCalibration &energy_cal,
                                      const size_t num_gamma_channel,
                                      rapidxml::xml_node<char> *RadInstrumentData,
                                      std::mutex &xmldocmutex,
                                      const int cal_number )
{
  using namespace rapidxml;
  const char *val = (const char *)0;
  char buffer[32];
  
  rapidxml::xml_document<char> *doc = RadInstrumentData->document();
  
  assert( doc );
  if( !doc )
    throw runtime_error( "add_calibration_to_2012_N42_xml: failed to get xml document." );
  
  const char *coefname = 0;
  xml_node<char> *EnergyCalibration = 0, *node = 0;
  
  string remark;
  stringstream valuestrm;
  vector<float> coefs;
  
  
  switch( energy_cal.type() )
  {
    case SpecUtils::EnergyCalType::InvalidEquationType:
      coefs = { 0.0f, 0.0f, 0.0f };
      coefname = "CoefficientValues";
      valuestrm << "0 0 0";
      remark = s_energy_cal_not_available_remark;
      break;

    case SpecUtils::EnergyCalType::FullRangeFraction:
      /// \TODO: add a "EnergyCalibrationExtension" element to cover Full Range Fraction calibration
      remark = s_frf_to_poly_remark;
      coefs = energy_cal.coefficients();
      coefs = SpecUtils::fullrangefraction_coef_to_polynomial( coefs, num_gamma_channel );
      //note intential fallthrough
      
    case SpecUtils::EnergyCalType::Polynomial:
    case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
    {
      assert( num_gamma_channel == energy_cal.num_channels() );
      
      if( coefs.empty() )
        coefs = energy_cal.coefficients();
      
      coefname = "CoefficientValues";
      
      //Technically this node must have exactly three coeficients, but here we'll
      //  slip in some more if we have them...
      const size_t ncoef = std::max( size_t(3), coefs.size() );
      for( size_t j = 0; j < ncoef; ++j )
      {
        snprintf( buffer, sizeof(buffer), "%s%.9g", (j?" ":""), (j<coefs.size() ? coefs[j] : 0.0f) );
        valuestrm << buffer;
      }
        
      break;
    }//case SpecUtils::EnergyCalType::Polynomial:
      
    case SpecUtils::EnergyCalType::LowerChannelEdge:
    {
      assert( num_gamma_channel == energy_cal.num_channels() );
      
      coefname = "EnergyBoundaryValues";
      const std::vector<float> &b = energy_cal.coefficients();
        
      //This next part should be formatted better!
      for( size_t j = 0; j < b.size(); ++j )
        valuestrm << (j?" ":"") << b[j];
      
      //According to N42 2012 standard, we must give energy of upper channel
      if( b.size() && b.size()<= num_gamma_channel )
        valuestrm << " " << ((2.0f*b[b.size()-1])-b[b.size()-2]);
      
      break;
    }//case SpecUtils::EnergyCalType::LowerChannelEdge:
  }//switch( energy_calibration_model() )
  
  
  if( coefname )
  {
    std::lock_guard<std::mutex> lock( xmldocmutex );
    EnergyCalibration = doc->allocate_node( node_element, "EnergyCalibration" );
    RadInstrumentData->append_node( EnergyCalibration );
    
    snprintf( buffer, sizeof(buffer), "EnergyCal%i", cal_number );
    val = doc->allocate_string( buffer );
    EnergyCalibration->append_attribute( doc->allocate_attribute( "id", val ) );
    
    if( !remark.empty() )
    {
      val = doc->allocate_string( remark.c_str() );
      node = doc->allocate_node( node_element, "Remark", val );
      EnergyCalibration->append_node( node );
    }//if( !remark.empty() )
    
    val = doc->allocate_string( valuestrm.str().c_str() );
    node = doc->allocate_node( node_element, coefname, val );
    EnergyCalibration->append_node( node );
  }//if( coefname )
  
  const vector<pair<float,float>> &devpairs = energy_cal.deviation_pairs();
  if( devpairs.size() )
  {
    stringstream EnergyValuesStrm, EnergyDeviationValuesStrm;
    for( size_t j = 0; j < devpairs.size(); ++j )
    {
      snprintf( buffer, sizeof(buffer), "%s%.9g", (j?" ":""), devpairs[j].first );
      EnergyValuesStrm << buffer;
      snprintf( buffer, sizeof(buffer), "%s%.9g", (j?" ":""), devpairs[j].second );
      EnergyDeviationValuesStrm << buffer;
    }
    
    std::lock_guard<std::mutex> lock( xmldocmutex );
    if( !EnergyCalibration )
    {
      EnergyCalibration = doc->allocate_node( node_element, "EnergyCalibration" );
      RadInstrumentData->append_node( EnergyCalibration );
    }
    
    val = doc->allocate_string( EnergyValuesStrm.str().c_str() );
    node = doc->allocate_node( node_element, "EnergyValues", val );
    EnergyCalibration->append_node( node );
    
    val = doc->allocate_string( EnergyDeviationValuesStrm.str().c_str() );
    node = doc->allocate_node( node_element, "EnergyDeviationValues", val );
    EnergyCalibration->append_node( node );
  }//if( devpairs.size() )
}//void add_calibration_to_2012_N42_xml(...)


  void insert_N42_calibration_nodes( const std::vector< std::shared_ptr<SpecUtils::Measurement> > &measurements,
                                    ::rapidxml::xml_node<char> *RadInstrumentData,
                                    std::mutex &xmldocmutex,
                                    EnergyCalToIndexMap &calToSpecMap )
  {
    using namespace rapidxml;
    calToSpecMap.clear();
    
    for( size_t i = 0; i < measurements.size(); ++i )
    {
      const std::shared_ptr<SpecUtils::Measurement> &meas = measurements[i];
      if( !meas || !meas->gamma_counts() || meas->gamma_counts()->empty() )
        continue;
      
      const auto energy_cal = meas->energy_calibration();
      const auto iter = calToSpecMap.find( energy_cal );
      if( iter == calToSpecMap.end() )
      {
        calToSpecMap[energy_cal] = i;
        add_calibration_to_2012_N42_xml( *energy_cal, meas->gamma_counts()->size(),
                                         RadInstrumentData, xmldocmutex, static_cast<int>(i) );
      }//if( iter == calToSpecMap.end() )
    }//for( size_t i = 0; i < measurements.size(); ++i )
  }//void insert_N42_calibration_nodes(...)

/** Returns the N42-2012 <RadDetectorKindCode> element value - for the gamma detector
*/
std::string determine_gamma_detector_kind_code( const SpecUtils::SpecFile &sf )
{
  string det_kind = "Other";
  switch( sf.detector_type() )
  {
    case SpecUtils::DetectorType::DetectiveUnknown:
    case SpecUtils::DetectorType::DetectiveEx:
    case SpecUtils::DetectorType::DetectiveEx100:
    case SpecUtils::DetectorType::DetectiveEx200:
    case SpecUtils::DetectorType::Falcon5000:
    case SpecUtils::DetectorType::MicroDetective:
    case SpecUtils::DetectorType::DetectiveX:
    case SpecUtils::DetectorType::Fulcrum40h:
    case SpecUtils::DetectorType::Fulcrum:
      det_kind = "HPGe";
      break;
      
    case SpecUtils::DetectorType::Exploranium:
    case SpecUtils::DetectorType::IdentiFinder:
    case SpecUtils::DetectorType::IdentiFinderNG:
    case SpecUtils::DetectorType::IdentiFinderUnknown:
    case SpecUtils::DetectorType::IdentiFinderTungsten:
    case SpecUtils::DetectorType::IdentiFinderR500NaI:
    case SpecUtils::DetectorType::RadHunterNaI:
    case SpecUtils::DetectorType::Rsi701:
    case SpecUtils::DetectorType::Rsi705:
    case SpecUtils::DetectorType::AvidRsi:
    case SpecUtils::DetectorType::OrtecRadEagleNai:
    case SpecUtils::DetectorType::Sam940:
    case SpecUtils::DetectorType::Sam945:
    case SpecUtils::DetectorType::RIIDEyeNaI:
    case SpecUtils::DetectorType::RadSeekerNaI:
    case SpecUtils::DetectorType::VerifinderNaI:
    case SpecUtils::DetectorType::IdentiFinderR425NaI:
    case SpecUtils::DetectorType::Sam950:
      det_kind = "NaI";
      break;
      
    case SpecUtils::DetectorType::IdentiFinderLaBr3:
    case SpecUtils::DetectorType::IdentiFinderR425LaBr:
    case SpecUtils::DetectorType::IdentiFinderR500LaBr:
    case SpecUtils::DetectorType::RadHunterLaBr3:
    case SpecUtils::DetectorType::Sam940LaBr3:
    case SpecUtils::DetectorType::OrtecRadEagleLaBr:
    case SpecUtils::DetectorType::RIIDEyeLaBr:
    case SpecUtils::DetectorType::RadSeekerLaBr:
    case SpecUtils::DetectorType::VerifinderLaBr:
      det_kind = "LaBr3";
      break;
      
    case SpecUtils::DetectorType::OrtecRadEagleCeBr2Inch:
    case SpecUtils::DetectorType::OrtecRadEagleCeBr3Inch:
      det_kind = "CeBr3";
      break;
      
    case SpecUtils::DetectorType::SAIC8:
    case SpecUtils::DetectorType::Srpm210:
      det_kind = "PVT";
      break;
      
    case SpecUtils::DetectorType::MicroRaider:
    case SpecUtils::DetectorType::Interceptor:
      det_kind = "CZT";
      break;
      
    case SpecUtils::DetectorType::KromekD3S:
    case SpecUtils::DetectorType::RadiaCode:
      det_kind = "CsI";
      break;
      
    case SpecUtils::DetectorType::Unknown:
    {
      const string &manufacturer = sf.manufacturer();
      const string &model = sf.instrument_model();
      
      if( manufacturer=="Raytheon" && SpecUtils::icontains(model,"Variant") )
        det_kind = "NaI";
      else if( manufacturer=="Mirion Technologies" && SpecUtils::icontains(model,"Pedestrian") )
        det_kind = "NaI";
      else if( manufacturer=="Nucsafe" && SpecUtils::icontains(model,"Predator") )
        det_kind = "PVT";
      break;
    }
  }//switch( detector_type_ )
  
  return det_kind;
}//determine_gamma_detector_kind_code()
}//namespace


namespace
{
  //anaonomous namespace for functions to help parse N42 files, that wont be
  //  usefull outside of this file
  
  //is_gamma_spectrum(): Trys to determine  if the spectrum_node cooresponds
  //  to a gamma or neutron node in the XML.  If it cant tell, will return true.
  bool is_gamma_spectrum( const rapidxml::xml_attribute<char> *detector_attrib,
                         const rapidxml::xml_attribute<char> *type_attrib,
                         const rapidxml::xml_node<char> *det_type_node,
                         const rapidxml::xml_node<char> *spectrum_node )
  {
    bool is_gamma = false, is_nuetron = false;
    
    //The ICD1 Spec it shall be the case that <DetectorType>
    //  will say 'Neutron' for neutron detectors, so if we find this, we
    //  wont bother to mess with names
    if( det_type_node && det_type_node->value_size() )
    {
      const string det_type = SpecUtils::xml_value_str(det_type_node);
      if( SpecUtils::icontains(det_type,"neutron")
         || SpecUtils::icontains(det_type,"GMTube")
         //|| SpecUtils::icontains(det_type,"He-3")
         //|| SpecUtils::icontains(det_type,"He3")
         )
        return false;
      if( SpecUtils::icontains(det_type,"Gamma") )
        return true;
    }//if( det_type_node && det_type_node->value_size() )
    
    if( detector_attrib )
    {
      string name = SpecUtils::xml_value_str(detector_attrib);
      SpecUtils::to_lower_ascii( name );
      
      if( SpecUtils::contains(name, "neutron") )
        is_nuetron = true;
      
      if( SpecUtils::iends_with(name, "1N") || SpecUtils::iends_with(name, "2N")
          || SpecUtils::iends_with(name, "3N") || SpecUtils::iends_with(name, "4N") )
        is_nuetron = true;
      
      if( SpecUtils::icontains(name, "GMTube") )
        is_nuetron = true;
      
      if( SpecUtils::contains(name, "pha") )
        is_gamma = true;
      
      if( SpecUtils::contains(name, "gamma") )
        is_gamma = true;
      
      if( name == "tungsten" ) //some FLIR identiFINDER
        return true;
      
      if( !is_nuetron && !is_gamma ) //try to match the name
      {
        const size_t len = name.length();
        bool matches_convention = (len >= 2);
        if( len >= 1 )
        {
          const char c = name[0];
          matches_convention |= (c=='a' || c=='b' || c=='c' || c=='d' );
        }
        if( len >= 2 )
        {
          const char c = name[1];
          matches_convention |= (isdigit(c) || c=='a' || c=='b' || c=='c' || c=='d' );
        }
        if( len >= 3 )
        {
          const char c = name[2];
          matches_convention |= (isdigit(c) || c=='n');
        }
        
        if( matches_convention )
          matches_convention = !SpecUtils::icontains( name, "Unknown" );
        
        if( matches_convention )
        {
          const char c = name[name.size()-1];
          is_nuetron = (c == 'n');
          is_gamma = (isdigit(c) != 0);
        }//if( matches_convention )
      }//if( !is_nuetron && !is_gamma ) //try to match the name
    }//if( detector_attrib )
    
    
    if( type_attrib )
    {
      const string name = SpecUtils::xml_value_str(type_attrib);
      if( SpecUtils::icontains(name, "pha") || SpecUtils::icontains(name, "Gamma") )
        is_gamma = true;
    }//if( type_attrib )
    
    
    if( is_nuetron == is_gamma )
    {
      for( const rapidxml::xml_node<char> *node = spectrum_node; node; node = node->parent() )
      {
        const rapidxml::xml_attribute<char> *attrib = node->first_attribute( "DetectorType", 12 );
        const string textstr = SpecUtils::xml_value_str(attrib);
        if( textstr.length() )
        {
          is_gamma = (SpecUtils::icontains( textstr, "gamma" ) || SpecUtils::icontains( textstr, "LaBr" ) || SpecUtils::icontains( textstr, "NaI" ) );
          is_nuetron = SpecUtils::icontains( textstr, "neutron" );
          break;
        }//if( textstr.length() )
      }//while( (parent = det_type_node->parent()) )
    }//if( is_nuetron == is_gamma )
    
    
    if( (is_nuetron == is_gamma) && !is_gamma && spectrum_node )
    {
      const auto node = XML_FIRST_INODE(spectrum_node,"ChannelData");
      
      //A cheap check to make sure the <ChannelData> is more than a single neutron count
      is_gamma = (node && node->value() && node->value_size() > 11); //11 is arbitrary
    }//if( is_nuetron == is_gamma && !is_gamma )
    
    
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
    if( is_nuetron == is_gamma )
    {
      stringstream msg;
      msg << SRC_LOCATION << "\n\tFound spectrum thats ";
      
      if( is_nuetron )
        msg << "a neutron and a gamma spectrum Detector=";
      else
        msg << "neither neutron or gamma spectrum Detector=";
      
      if( detector_attrib && detector_attrib->value_size() )
        msg << SpecUtils::xml_value_str(detector_attrib);
      else
        msg << "NULL";
      
      msg << ", Type=";
      
      if( type_attrib && type_attrib->value_size() )
        msg << SpecUtils::xml_value_str(type_attrib);
      else
        msg << "NULL";
      
      log_developer_error( __func__, msg.str().c_str() );
    }//if( is_nuetron && is_gamma )
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
    
    if( is_nuetron && !is_gamma )
      return false;
    
    //Lets just assume its a gamma detector here....
    return true;
  }//bool is_gamma_spectrum()
  
  
  // Tries to get the occupancy status from the <Occupied> xml element
  SpecUtils::OccupancyStatus parse_occupancy_status( const rapidxml::xml_node<char> *uccupied_node )
  {
    if( !uccupied_node || !uccupied_node->value_size() )
      return SpecUtils::OccupancyStatus::Unknown;
    
    if( uccupied_node->value()[0] == '0' )
      return SpecUtils::OccupancyStatus::NotOccupied;
    
    if( uccupied_node->value()[0] == '1' )
      return SpecUtils::OccupancyStatus::Occupied;
    
    if( XML_VALUE_ICOMPARE(uccupied_node, "true") )
      return SpecUtils::OccupancyStatus::Occupied;
    
    if( XML_VALUE_ICOMPARE(uccupied_node, "false") )
      return SpecUtils::OccupancyStatus::NotOccupied;
    
  #if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
    const string errmsg = "Found un-expected occupancy status value '"
                          + SpecUtils::xml_value_str(uccupied_node) + "'";
    log_developer_error( __func__, errmsg.c_str() );
  #endif  //#if( PERFORM_DEVELOPER_CHECKS )
    
    return SpecUtils::OccupancyStatus::Unknown;
  }//bool parse_occupancy_status( rapidxml::xml_node<char> *uccupied_node )
  
  
  const rapidxml::xml_attribute<char> *find_detector_attribute( const rapidxml::xml_node<char> *spectrum )
  {
    rapidxml::xml_attribute<char> *attribute = spectrum->first_attribute( "Detector", 8 );
    if( attribute )
      return attribute;
    
    using rapidxml::internal::compare;
    
    for( const rapidxml::xml_node<char> *node = spectrum->parent();
        (node && !XML_VALUE_ICOMPARE(node, "DetectorData"));
        node = node->parent() )
    {
      attribute = node->first_attribute( "Detector", 8 );
      if( attribute )
        return attribute;
    }//for( search parents incase it was out in the wrong node )
    
    //Avid N42 files contain a "Sensor" attribute in the <Spectrum> node that
    //  I think should be the detector name (unconfirmed for multiple detector
    //  systems as of 20150528)
    attribute = spectrum->first_attribute( "Sensor", 6 );
    
    return attribute;
  }//const rapidxml::xml_attribute<char> *find_detector_attribute( const rapidxml::xml_node<char> *spectrum )
  
  //speed_from_node(): returns speed in m/s; throws exception on error
  float speed_from_node( const rapidxml::xml_node<char> *speed_node )
  {
    if( !speed_node || !speed_node->value_size() )
      throw std::runtime_error( "speed_from_node(...): NULL <Speed> node" );
    
    float speed = 0.0f;
    if( !SpecUtils::parse_float( speed_node->value(), speed_node->value_size(), speed ) )
    {
      stringstream msg;
      msg << SRC_LOCATION << "\n\tUnable to convert '" << SpecUtils::xml_value_str(speed_node)
      << "' to a float";

#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
      log_developer_error( __func__, msg.str().c_str() );
#endif
      
      throw runtime_error( msg.str() );
    }//if( couldnt convert to float
    
    if( speed < 0.00000001f )
      return 0.0f;
    
    const rapidxml::xml_attribute<char> *unit_attrib = XML_FIRST_ATTRIB( speed_node, "Units" );
    if( !unit_attrib || !unit_attrib->value_size() )
    {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
      const string msg = "Warning no units attribute available in <Speed> node, assuming m/s";
      cerr << "\n\t" << msg << endl;
#endif

      return speed;
    }//if( no unit attribute" )
    
    string units = SpecUtils::xml_value_str( unit_attrib );
    SpecUtils::trim( units );
    SpecUtils::to_lower_ascii( units );
    if( units == "mph" )
      return 0.44704f * speed;
    if( units == "m/s" )
      return speed;
    
    const string msg = "Unknown speed units: '" + units + "' - please fix";
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
    log_developer_error( __func__, msg.c_str() );
#endif
    
    throw std::runtime_error( msg );
    
    return speed;
  }//float speed_from_node( rapidxml::xml_node<char> *speed_node )
  
  
  
  /**               Horrible hack for SpirMobile systems
   At least some of the files from SpirMobile systems have multiple _nested_
   <RadInstrumentData> tags, that it looks like we have to flatten out into
   a single <RadInstrumentData> tag.  Because the xml_node passed in is
   const, we cant modify it, so instead we will create a new xml_document
   and modify it.
   If this hack is not needed, the pointer passed in will not be modified, and the
   unique_ptr returned will be nullptr.
   If this hack is needed, the pointer passed in will be modified to point to the
   "RadInstrumentData" node on the new document, and the returned unique_ptr
   will own the created document (you must keep in scope for duration of use).
   Note that for the returned document, the xml names and values will still belong
   to the original document, not the new one.
   */
  unique_ptr<rapidxml::xml_document<char>> spir_mobile_2012_n42_hack( const rapidxml::xml_node<char> *&data_node )
  {
    size_t num_recursive_rad_item_tags = 0;
    for( auto node = data_node; node; node = XML_FIRST_NODE(node,"RadInstrumentData") )
      ++num_recursive_rad_item_tags;
    
    if( num_recursive_rad_item_tags <= 1 )
      return nullptr;
    
    unique_ptr<rapidxml::xml_document<char>> dummydoc( new rapidxml::xml_document<char>() );
    auto dummy_data_node = dummydoc->clone_node( data_node );
    dummydoc->append_node( dummy_data_node );
    
    vector<rapidxml::xml_node<char> *> inst_nodes;
    for( auto node = XML_FIRST_NODE(dummy_data_node,"RadInstrumentData");
        node; node = XML_FIRST_NODE(node,"RadInstrumentData") )
      inst_nodes.push_back( node );
    
    assert( inst_nodes.size() > 0 );
    
    for( auto subdatanode : inst_nodes )
    {
      vector<rapidxml::xml_node<char> *> children;
      for( auto node = subdatanode->first_node(); node; node = node->next_sibling() )
        children.push_back( node );
      
      for( auto child : children )
      {
        subdatanode->remove_node( child );
        dummy_data_node->append_node( child );
      }
      subdatanode->parent()->remove_node( subdatanode );
    }//for( size_t i = inst_nodes.size()-1; i > 0; --i )
    
    data_node = dummy_data_node;
    
    //std::ofstream tmpoutfile( "/Users/wcjohns/Downloads/fixedup.xml", ios::out | ios::binary  );
    //rapidxml::print( static_cast<std::basic_ostream<char> &>(tmpoutfile), *dummy_data_node->document() );
    
    return dummydoc;
  }//shared_ptr<rapidxml::xml_document<char>> spir_mobile_2012_n42_hack( const rapidxml::xml_node<char> *&data_node )


  //add_spectra_to_measurement_node_in_2012_N42_xml(...): Adds the given
    //  spectra to the specified RadMeasurementNode.  All measurements should
    //  have the sample sample number, and the entries in calibid should
    //  correspond one to one to the entries in measurements.
    //  If something drastically goes wrong, and an exception is thrown somewhere
    //  this function will not throw, it will print an error to stderror and not
    //  insert itself into the DOM; this is so this function is safe to call in
    //  its own thread with no error handling.  I expect this to never happen, so
    //  I'm not bothering with any better error handling.

void add_spectra_to_measurement_node_in_2012_N42_xml( ::rapidxml::xml_node<char> *RadMeasurement,
                  const std::vector< std::shared_ptr<const SpecUtils::Measurement> > measurements,
                  const std::vector<size_t> calibids,
                  std::mutex &xmldocmutex )
{
  using namespace SpecUtils;
  using namespace ::rapidxml;
  
  try
  {
    //Some checks that should never actually trigger
    if( !RadMeasurement )
      throw runtime_error( "null RadMeasurement" );
    if( measurements.empty() )
      throw runtime_error( "with empty input" );
    if( measurements.size() != calibids.size() )
      throw runtime_error( "measurements.size != calibids.size" );
    
    string radMeasID;
    xml_document<char> *doc = nullptr;
    
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      doc = RadMeasurement->document();
      radMeasID = xml_value_str( XML_FIRST_ATTRIB(RadMeasurement, "id") );
    }
    
    assert( doc );
    if( !doc )
      throw runtime_error( "failed to get xml document." );
    
    const char *val = 0;
    char buffer[256];
    
    //not dealing with radItemInformationReferences and radMeasurementGroupReferences attributes
    
    //Need child tags of <RadMeasurement> in following order
    //MeasurementClassCode, exactly once
    //StartDateTime, exactly once
    //RealTimeDuration, exactly once
    
    //Spectrum, 0 or more
    //GrossCounts, 0 or more
    //DoseRate, 0 or more
    //TotalDose, 0 or more
    //ExposureRate, 0 or more
    //TotalExposure, 0 or more
    
    //RadInstrumentState, 0 or more
    //RadDetectorState, 0 or more
    //RadItemState, 0 or more
    //OccupancyIndicator, 0 or more
    //RadMeasurementExtension, 0 or more
    
    //Since all samples might not have occupancy/speed/gps info, lets loop
    // through and grab it.  Not this is an artifact of this code not
    // originally being modeled after N42 2012.  In principle, this loop
    // shouldnt have an effect in the vast majority (maybe all I know of) of
    // situations, but jic
    float speed = measurements[0]->speed();
    time_point_t starttime = measurements[0]->start_time();
    
    OccupancyStatus occupancy = measurements[0]->occupied();
    SourceType source_type = measurements[0]->source_type();
  
    
    float realtime_used = measurements[0]->real_time();
    
    map<string,shared_ptr<const LocationState>> rad_det_states;
    shared_ptr<const LocationState>  instrument_state;
    set<shared_ptr<const LocationState>> item_states;
    
    
    for( size_t i = 0; i < measurements.size(); ++i )
    {
      realtime_used = max( measurements[i]->real_time(), realtime_used );
      const time_point_t tst = measurements[i]->start_time();
      starttime = ((is_special(tst) || (starttime < tst)) ? starttime : tst);
      
      speed = max( measurements[i]->speed(), speed );
      
      if( measurements[i]->occupied() == OccupancyStatus::Occupied )
        occupancy = measurements[i]->occupied();
      else if( occupancy == OccupancyStatus::Unknown )
        occupancy = measurements[i]->occupied();
      else if( measurements[i]->occupied() ==  OccupancyStatus::NotOccupied && occupancy == OccupancyStatus::Unknown )
        occupancy = measurements[i]->occupied();
      
      if( measurements[i]->source_type() != SourceType::Unknown )
        source_type = std::max( measurements[i]->source_type(), source_type );
    }//for( size_t i = 1; i < measurements.size(); ++i )
    
    
    char realtime[32], speedstr[32];
    
    snprintf( realtime, sizeof(realtime), "PT%fS", realtime_used );
    snprintf( speedstr, sizeof(speedstr), "%.8f", speed );
    
    const string startstr = SpecUtils::to_extended_iso_string(starttime) + "Z";
    
    const char *classcode = (const char *)0;
    const char *occupied = (const char *)0;
    switch( source_type )
    {
      case SourceType::Background:         classcode = "Background";        break;
      case SourceType::Calibration:        classcode = "Calibration";       break;
      case SourceType::Foreground:         classcode = "Foreground";        break;
      case SourceType::IntrinsicActivity:  classcode = "IntrinsicActivity"; break;
      case SourceType::Unknown:  classcode = "NotSpecified";      break;
    }//switch( source_type_ )
    
    switch( occupancy )
    {
      case OccupancyStatus::NotOccupied: occupied = "false"; break;
      case OccupancyStatus::Occupied:    occupied = "true";  break;
      case OccupancyStatus::Unknown:          break;
    }//switch( occupied_ )
    
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      val = doc->allocate_string( classcode );
      RadMeasurement->append_node( doc->allocate_node( node_element, "MeasurementClassCode", val ) );
      
      if( !is_special(measurements[0]->start_time()) )
      {
        val = doc->allocate_string( startstr.c_str(), startstr.size()+1 );
        RadMeasurement->append_node( doc->allocate_node( node_element, "StartDateTime", val, 13, startstr.size() ) );
      }
      
      if( measurements[0]->real_time() > 0.0f )
      {
        val = doc->allocate_string( realtime );
        RadMeasurement->append_node( doc->allocate_node( node_element, "RealTimeDuration", val ) );
      }
    }
    
    //Since gross count nodes have to come after
    map<string,xml_node<char> *> det_states;
    vector<xml_node<char> *> spectrum_nodes, gross_count_nodes, dose_rate_nodes, exposure_rate_nodes;
    
    for( size_t i = 0; i < measurements.size(); ++i )
    {
      assert( i < calibids.size() );
      
      const size_t calibid = calibids[i];
      const shared_ptr<const Measurement> m = measurements[i];
      assert( m );
      
      char livetime[32], calibstr[32], spec_id_cstr[48];
      
      string neutcounts;
      snprintf( livetime, sizeof(livetime), "PT%fS", m->live_time() );
      snprintf( calibstr, sizeof(calibstr), "EnergyCal%i", static_cast<int>(calibid) );
      
      if( SpecUtils::icontains(radMeasID, "Det") )
      {
        //This is case where all measurements of a sample number did not have a similar
        //  start time or background/foreground status so each sample/detector
        //  gets its own <RadMeasurement> element, with an id like "Sample3Det1"
        snprintf( spec_id_cstr, sizeof(spec_id_cstr), "%sSpectrum", radMeasID.c_str() );
      }else if( !radMeasID.empty() )
      {
        //radMeasID will be "Background", "Survey XXX" if passthrough() that
        //  starts with a long background, and "SampleXXX" otherwise.
        snprintf( spec_id_cstr, sizeof(spec_id_cstr), "%sDet%iSpectrum", radMeasID.c_str(), m->detector_number() );
      }else
      {
        //Probably shouldnt ever make it here.
        snprintf( spec_id_cstr, sizeof(spec_id_cstr), "Sample%iDet%iSpectrum", m->sample_number(), m->detector_number() );
      }
      spec_id_cstr[sizeof(spec_id_cstr) - 1] = '\0'; //jic
      string spec_idstr = spec_id_cstr;
      
      // If this is a derived data Measurement, we will append derived data properties to the "id"
      //  attribute value - painful I know, but seems to be the de facto standard.
      if( m->derived_data_properties() )
      {
        typedef Measurement::DerivedDataProperties DerivedProps;
        auto check_bit = [&m]( const DerivedProps &p ) -> bool {
          typedef std::underlying_type<DerivedProps>::type DerivedProps_t;
          return (m->derived_data_properties() & static_cast<DerivedProps_t>(p));
        };//check_bit
        
        //Note: these next four strings are detected in #N42DecodeHelper2012::set_deriv_data and
        //      #SpecFile::create_2012_N42_xml as well, so if you alter these strings, change them
        //      there as well
        
        if( check_bit(DerivedProps::ItemOfInterestSum) )
          spec_idstr += "-MeasureSum";
        
        if( check_bit(DerivedProps::UsedForAnalysis) )
          spec_idstr += "-Analysis";
        
        if( check_bit(DerivedProps::ProcessedFurther) )
          spec_idstr += "-Processed";
        
        if( check_bit(DerivedProps::BackgroundSubtracted) )
          spec_idstr += "-BGSub";
      }//if( m->derived_data_properties_ )
      
      
      const string detnam = !m->detector_name().empty() ? m->detector_name() : s_unnamed_det_placeholder;
      
      //Below choice of zero compressing if the gamma sum is less than 15 times the
      //  number of gamma channels is arbitrarily chosen, and has not been
      //  benchmarked or checked it is a reasonable value
      const bool zerocompressed = (!!m->gamma_counts() && (m->gamma_count_sum()<15.0*m->gamma_counts()->size()));
      vector<float> compressedchannels;
      
      if( zerocompressed )
        compress_to_counted_zeros( *m->gamma_counts(), compressedchannels );
      
      const vector<float> &data = (zerocompressed || !m->gamma_counts()) ? compressedchannels : (*m->gamma_counts());
      
      string channeldata;
      if( !zerocompressed )
        channeldata.reserve( 3*m->gamma_counts()->size() ); //3 has not been verified to be reasonable
      
      const size_t nchannel = data.size();
      
      //The hope is that writing 8 channels data at a time will be faster than one
      //  at a time - however I didnt check that it is, or check that doing somrthign
      //  like 16 or 32 would be faster.
      //"%.9G" specifies use exponential form (i.e. "1.234E5") if shorter than
      //  decimal (i.e 123450), printing up to 9 significant digits.  Also, it looks
      //  like the shortest expressible form of integers are used (e.g. 0.0f prints
      //  as "0", and 101.0f prints as "101").  The maximum sig figs is specified
      //  since floats get converted to doubles when given as arguments of printf.
      //  Also, 8 was chosen since we have integer acuracy of floats up to
      //  16,777,216 (after this floats have less precision than int).
      //  Also note that if we wanted to garuntee a round-trip of float-text-float
      //  we could use "%1.8e" or "%.9g".
      //For a lot of great float information, see:
      //  https://randomascii.wordpress.com/2013/02/07/float-precision-revisited-nine-digit-float-portability/
      if( (nchannel % 8) == 0 )
      {
        for( size_t i = 0; i < nchannel; i += 8 )
        {
          snprintf( buffer, sizeof(buffer),
                   (i?" %.8G %.8G %.8G %.8G %.8G %.8G %.8G %.8G"
                    :"%.8G %.8G %.8G %.8G %.8G %.8G %.8G %.8G"),
                   data[i], data[i+1], data[i+2], data[i+3],
                   data[i+4], data[i+5], data[i+6], data[i+7] );
          channeldata += buffer;
        }//for( size_t i = 0; i < nchannel; i += 8 )
      }else
      {
        for( size_t i = 0; i < nchannel; ++i )
        {
          snprintf( buffer, sizeof(buffer), (i?" %.8G":"%.8G"), data[i] );
          channeldata += buffer;
        }//for( size_t i = 0; i < nchannel; i += 8 )
      }//if( (nchannel % 8) == 0 )
      
      if( m->neutron_counts().size() > 1 )
      {
        for( size_t i = 0; i < m->neutron_counts().size(); ++i )
        {
          snprintf( buffer, sizeof(buffer), (i?" %.8G":"%.8G"), m->neutron_counts()[i] );
          neutcounts += buffer;
        }//for( size_t i = 0; i < nchannel; i += 8 )
      }else
      {
        snprintf( buffer, sizeof(buffer), "%.8G", m->neutron_counts_sum() );
        neutcounts += buffer;
      }
      
      
      const shared_ptr<const SpecUtils::LocationState> &location_state = m->location_state();
      if( location_state )
      {
        switch( location_state->type_ )
        {
          case SpecUtils::LocationState::StateType::Detector:
            rad_det_states[detnam] = location_state;
            break;
          case SpecUtils::LocationState::StateType::Instrument:
            instrument_state = location_state;
            break;
          case SpecUtils::LocationState::StateType::Item:
            item_states.insert( location_state );
            break;
          case SpecUtils::LocationState::StateType::Undefined:
            assert( 0 ); // I dont *think* we should get here, but lets test
            if( !instrument_state )
              instrument_state = location_state;
            break;
        }//switch( m->location_->type_ )
      }//if( m->location_ )
      
      std::lock_guard<std::mutex> lock( xmldocmutex );
      
      if( m->gamma_counts() && !m->gamma_counts()->empty())
      {
        xml_node<char> *Spectrum = doc->allocate_node( node_element, "Spectrum" );
        spectrum_nodes.push_back( Spectrum );
        
        //If there is a slight mismatch between the live times of this sample
        //  (~50 ms), we will still include all detectors in the same sample,
        //  but put in a remark notting a difference.  This is absolutely a
        //  hack, but some sort of comprimise is needed to cram stuff into N42
        //  2012 files from arbitrary sources.
        if( fabs(m->real_time() - realtime_used) > 0.00001 )
        {
          char thisrealtime[64];
          snprintf( thisrealtime, sizeof(thisrealtime), "RealTime: PT%fS", m->real_time() );
          val = doc->allocate_string( thisrealtime );
          xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
          Spectrum->append_node( remark );
        }
        
        
        if( !m->title().empty() )
        {
          const string title = "Title: " + m->title();
          val = doc->allocate_string( title.c_str(), title.size()+1 );
          xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
          Spectrum->append_node( remark );
        }
        
        const std::vector<std::string> &remarks = m->remarks();
        for( size_t i = 0; i < remarks.size(); ++i )
        {
          if( remarks[i].empty() )
            continue;
          const char *val = doc->allocate_string( remarks[i].c_str(), remarks[i].size()+1 );
          xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
          Spectrum->append_node( remark );
        }//for( size_t i = 0; i < remarks_.size(); ++i )
        
        const auto &parse_warnings = m->parse_warnings();
        for( size_t i = 0; i < parse_warnings.size(); ++i )
        {
          if( parse_warnings[i].empty() )
            continue;
          
          /// @TODO We should put the parse warnings common to all <spectrum> in this
          ///       measurement under the Measurement remark node, and not duplicated
          ///       in each spectrum node.
          const bool hasprefix = SpecUtils::starts_with( parse_warnings[i], s_parser_warn_prefix );
          string valstr = (hasprefix ? "" : s_parser_warn_prefix ) + parse_warnings[i];
          val = doc->allocate_string( valstr.c_str(), valstr.size()+1 );
          xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
          Spectrum->append_node( remark );
        }//for( size_t i = 0; i < m->parse_warnings_.size(); ++i )
        
        
        val = doc->allocate_string( calibstr );
        Spectrum->append_attribute( doc->allocate_attribute( "energyCalibrationReference", val ) );
        
        val = doc->allocate_string( detnam.c_str(), detnam.size()+1 );
        Spectrum->append_attribute( doc->allocate_attribute( "radDetectorInformationReference", val, 31, detnam.size() ) );
        
        //Add required ID attribute
        val = doc->allocate_string( spec_idstr.c_str(), spec_idstr.size() + 1 );
        Spectrum->append_attribute( doc->allocate_attribute( "id", val ) );
        
        if( m->live_time() > 0.0f )
        {
          val = doc->allocate_string( livetime );
          xml_node<char> *LiveTimeDuration = doc->allocate_node( node_element, "LiveTimeDuration", val );
          Spectrum->append_node( LiveTimeDuration );
        }//if( live_time_ > 0.0f )
        
        if(!channeldata.empty())
        {
          val = doc->allocate_string( channeldata.c_str(), channeldata.size()+1 );
          xml_node<char> *ChannelData = doc->allocate_node( node_element, "ChannelData", val, 11, channeldata.size() );
          Spectrum->append_node( ChannelData );
          
          if( zerocompressed )
            ChannelData->append_attribute( doc->allocate_attribute( "compressionCode", "CountedZeroes" ) );
        }//if( channeldata.size() )
      }//if( gamma_counts_ && gamma_counts_->size() )
      
      if( m->contained_neutron() )
      {
        xml_node<char> *GrossCounts = doc->allocate_node( node_element, "GrossCounts" );
        gross_count_nodes.push_back( GrossCounts );
        
        if( (!m->gamma_counts() || m->gamma_counts()->empty())  )
        {
          if( fabs(m->real_time() - realtime_used) > 0.00001 )
          {
            char thisrealtime[64];
            snprintf( thisrealtime, sizeof(thisrealtime), "RealTime: PT%fS", m->real_time() );
            val = doc->allocate_string( thisrealtime );
            xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
            GrossCounts->append_node( remark );
          }
          
          if(!m->title().empty())
          {
            const string title = "Title: " + m->title();
            val = doc->allocate_string( title.c_str(), title.size()+1 );
            xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
            GrossCounts->append_node( remark );
          }//if( m->title_.size() )
          
          const auto &remarks = m->remarks();
          for( size_t i = 0; i < remarks.size(); ++i )
          {
            if( remarks[i].empty() )
              continue;
            const char *val = doc->allocate_string( remarks[i].c_str(), remarks[i].size()+1 );
            xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
            GrossCounts->append_node( remark );
          }//for( size_t i = 0; i < remarks_.size(); ++i )
        }//if( (!m->gamma_counts_ || m->gamma_counts_->empty())  )
        
        
        char neutId[32];
        if( radMeasID.empty() )
          snprintf( neutId, sizeof(neutId), "Sample%iNeutron%i", m->sample_number(), m->detector_number() );
        else
          snprintf( neutId, sizeof(neutId), "%sNeutron%i", radMeasID.c_str(), m->detector_number() );
        
        val = doc->allocate_string( neutId );
        GrossCounts->append_attribute( doc->allocate_attribute( "id", val ) );
        val = doc->allocate_string( detnam.c_str(), detnam.size()+1 );
        GrossCounts->append_attribute( doc->allocate_attribute( "radDetectorInformationReference", val, 31, detnam.size() ) );
        
        val = doc->allocate_string( livetime );
        xml_node<char> *LiveTimeDuration = doc->allocate_node( node_element, "LiveTimeDuration", val );
        GrossCounts->append_node( LiveTimeDuration );
        
        val = doc->allocate_string( neutcounts.c_str(), neutcounts.size()+1 );
        xml_node<char> *CountData = doc->allocate_node( node_element, "CountData", val, 9, neutcounts.size() );
        GrossCounts->append_node( CountData );
      }//if( contained_neutron_ )
      
      
      if( m->dose_rate() >= 0.0f )
      {
        xml_node<char> *DoseRate = doc->allocate_node( node_element, "DoseRate" );
        dose_rate_nodes.push_back( DoseRate );
        
        const string idstr = "dose-" + std::to_string(m->sample_number()) + "-" + detnam; //where does this get referenced?
        val = doc->allocate_string( idstr.c_str(), idstr.size() + 1 );
        DoseRate->append_attribute( doc->allocate_attribute( "id", val, 2, idstr.size() ) );
        
        val = doc->allocate_string( detnam.c_str(), detnam.size() + 1 );
        DoseRate->append_attribute( doc->allocate_attribute( "radDetectorInformationReference", val, 31, detnam.size() ) );
        
        snprintf( buffer, sizeof(buffer), "%.8G", m->dose_rate() );
        val = doc->allocate_string( buffer );
        xml_node<char> *DoseRateValue = doc->allocate_node( node_element, "DoseRateValue", val );
        DoseRate->append_node( DoseRateValue );
      }//if( m->dose_rate_ >= 0.0f )
      
      
      if( m->exposure_rate() >= 0.0f )
      {
        xml_node<char> *ExposureRate = doc->allocate_node( node_element, "ExposureRate" );
        exposure_rate_nodes.push_back( ExposureRate );
        
        const string idstr = "exposure-" + std::to_string(m->sample_number()) + "-" + detnam; //where does this get referenced?
        val = doc->allocate_string( idstr.c_str(), idstr.size() + 1 );
        xml_attribute<char> *att = doc->allocate_attribute( "id", val, 2, idstr.size() );
        ExposureRate->append_attribute( att );
        
        val = doc->allocate_string( detnam.c_str(), detnam.size() + 1 );
        att = doc->allocate_attribute( "radDetectorInformationReference", val, 31, detnam.size() );
        ExposureRate->append_attribute( att );
        
        snprintf( buffer, sizeof(buffer), "%.8G", m->exposure_rate() );
        val = doc->allocate_string( buffer );
        xml_node<char> *ExposureRateValue = doc->allocate_node( node_element, "ExposureRateValue", val );
        ExposureRate->append_node( ExposureRateValue );
      }//if( m->exposure_rate() >= 0.0f )
      
      
      switch( measurements[i]->quality_status() )
      {
        case SpecUtils::QualityStatus::Good:
          //When reading in the 2012 N42, we will assume good unless indicated otherwise
          break;
          
        case SpecUtils::QualityStatus::Suspect: case SpecUtils::QualityStatus::Bad:
        {
          xml_node<char> *RadDetectorState = doc->allocate_node( node_element, "RadDetectorState" );
          det_states[detnam] = RadDetectorState;
          
          val = ((measurements[i]->quality_status()==QualityStatus::Suspect) ? "Warning" : "Fatal" ); //"Error" is also an option
          RadDetectorState->append_node( doc->allocate_node( node_element, "Fault", val ) );
          break;
        }//case Suspect: case Bad:
          
        case SpecUtils::QualityStatus::Missing:
        {
          //This next line is InterSpec specific for round-tripping files
          xml_node<char> *RadDetectorState = doc->allocate_node( node_element, "RadDetectorState" );
          det_states[detnam] = RadDetectorState;
          
          xml_node<char> *remark = doc->allocate_node( node_element, "Remark", "InterSpec could not determine detector state." );
          RadDetectorState->append_node( remark );
          break;
        }
      }//switch( quality_status_ )
    }//for( loop over input measurements )
    
    /*
     The order of the child elements, acording to n42_2012.xsd, is:
    <MeasurementClassCode >
    <StartDateTime >
    <RealTimeDuration >
    <Spectrum >
    <GrossCounts />
    <DoseRate />
    <TotalDose /> //Not implemented
    <ExposureRate />
    <TotalExposure /> //Not implemented
    <RadInstrumentState />
    <RadDetectorState />
    <RadItemState />
    <OccupancyIndicator />
    <RadMeasurementExtension /> //Not implemented
    */
     
    {//start put <Spectrum> and <GrossCount> nodes into tree
      std::lock_guard<std::mutex> lock( xmldocmutex );
      
      for( xml_node<char> *node : spectrum_nodes )
        RadMeasurement->append_node( node );
      
      for( xml_node<char> *node : gross_count_nodes )
        RadMeasurement->append_node( node );
      
      for( xml_node<char> *node : dose_rate_nodes )
        RadMeasurement->append_node( node );
      
      for( xml_node<char> *node : exposure_rate_nodes )
        RadMeasurement->append_node( node );
    }//end put <Spectrum> and <GrossCount> nodes into tree
    
    
    {//begin add other information
      std::lock_guard<std::mutex> lock( xmldocmutex );
      
      if( instrument_state )
      {
        xml_node<char> *RadInstrumentState = doc->allocate_node( node_element, "RadInstrumentState" );
        RadMeasurement->append_node( RadInstrumentState );
        
        xml_attribute<char> *att = doc->allocate_attribute( "radInstrumentInformationReference", "InstInfo1", 33, 9 );
        RadInstrumentState->append_attribute( att );
        
        instrument_state->add_to_n42_2012( RadInstrumentState, doc );
      }//if( instrument_state )
      
      // For <RadDetectorState> we have info stored in `det_states` and `rad_det_states`
      //  we need to merge and then add to document.
      for( auto &name_det : det_states )
      {
        const string &name = name_det.first;
        xml_node<char> *node = name_det.second;
        assert( node );
        
        const auto pos = rad_det_states.find(name);
        if( pos != end(rad_det_states) )
        {
          const shared_ptr<const LocationState> &loc = pos->second;
          loc->add_to_n42_2012( node, doc );
          rad_det_states.erase( pos );
        }//
      }//for( auto &name_det : det_states )
      
      for( auto &name_loc : rad_det_states )
      {
        const string &name = name_loc.first;
        const shared_ptr<const LocationState> &loc = name_loc.second;
        
        assert( det_states.count(name) == 0 );
        xml_node<char> *RadDetectorState = doc->allocate_node( node_element, "RadDetectorState" );
        loc->add_to_n42_2012( RadDetectorState, doc );
        det_states[name] = RadDetectorState;
      }//for( auto &name_loc : rad_det_states )
      
      rad_det_states.clear();
      
      for( auto &name_det : det_states )
      {
        const string &name = name_det.first;
        xml_node<char> *node = name_det.second;
        assert( node );
        
        assert( !XML_FIRST_IATTRIB(node, "radDetectorInformationReference") );
        
        const char *val = doc->allocate_string( name.c_str(), name.size() + 1 );
        xml_attribute<char> *att = doc->allocate_attribute( "radDetectorInformationReference", val, 31, name.size() );
        node->append_attribute( att );
        
        RadMeasurement->append_node( node );
      }//for( auto &name_det : det_states )
      
      for( const shared_ptr<const LocationState> &loc : item_states )
      {
        assert( loc );
        xml_node<char> *RadItemState = doc->allocate_node( node_element, "RadItemState" );
        loc->add_to_n42_2012( RadItemState, doc );
        RadMeasurement->append_node( RadItemState );
      }//for( const shared_ptr<const LocationState> &loc : item_states )
      
      if( occupied )
      {
        val = doc->allocate_string( occupied );
        RadMeasurement->append_node( doc->allocate_node( node_element, "OccupancyIndicator", val ) );
      }
    }//end add other information
  }catch( std::exception &e )
  {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
    string msg = "Measurement::add_spectra_to_measurement_node_in_2012_N42_xml(...): something horrible happened!: " + string(e.what());
    log_developer_error( __func__, msg.c_str() );
#endif
    assert( 0 );
  }//try catch
}//void add_spectra_to_measurement_node_in_2012_N42_xml(...)
}//namespace


namespace SpecUtils
{

/** A struct used internally while parsing files to */
struct MeasurementCalibInfo
{
  SpecUtils::EnergyCalType equation_type;
  
  std::vector<float> coefficients;
  std::vector< std::pair<float,float> > deviation_pairs_;
  
  //Map from number of channels to energy cal
  std::map<size_t,std::shared_ptr<const SpecUtils::EnergyCalibration>> energy_cals;
  std::string energy_cal_error;
  
  std::string calib_id; //optional
  
  MeasurementCalibInfo( std::shared_ptr<SpecUtils::Measurement> meas );
  MeasurementCalibInfo();
  
  void fill_binning( const size_t nbin );
  bool operator<( const MeasurementCalibInfo &rhs ) const;
  bool operator==( const MeasurementCalibInfo &rhs ) const;
};//struct MeasurementCalibInfo
  
  
MeasurementCalibInfo::MeasurementCalibInfo( std::shared_ptr<SpecUtils::Measurement> meas )
{
  energy_cals.clear();
  equation_type = meas->energy_calibration_model();
  const size_t nbin = meas->gamma_counts()->size();
  coefficients = meas->calibration_coeffs();
  
  deviation_pairs_ = meas->deviation_pairs();
  energy_cals[nbin] = meas->energy_calibration();
  
  if( equation_type == SpecUtils::EnergyCalType::InvalidEquationType
     && !coefficients.empty() )
  {
#if( PERFORM_DEVELOPER_CHECKS )
    log_developer_error( __func__, "Found case where equation_type!=Invalid, but there are coefficients - shouldnt happen!" );
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
    coefficients.clear();
    deviation_pairs_.clear();
    energy_cals.clear();
  }//
}//MeasurementCalibInfo constructor


MeasurementCalibInfo::MeasurementCalibInfo()
{
  equation_type = SpecUtils::EnergyCalType::InvalidEquationType;
}//MeasurementCalibInfo constructor


void MeasurementCalibInfo::fill_binning( const size_t nbin )
{
  if( energy_cals.count(nbin) )
    return;
  
  auto cal = make_shared<SpecUtils::EnergyCalibration>();
  energy_cals[nbin] = cal;
  
  if( nbin < 2 )  /// \TODO: maybe loosen up polynomial and FRF to not have nbin requirement.
    return;
  
  try
  {
    switch( equation_type )
    {
      case SpecUtils::EnergyCalType::Polynomial:
        cal->set_polynomial( nbin, coefficients, deviation_pairs_ );
        break;
        
      case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
        cal->set_default_polynomial( nbin, coefficients, deviation_pairs_ );
        break;
        
      case SpecUtils::EnergyCalType::FullRangeFraction:
        cal->set_full_range_fraction( nbin, coefficients, deviation_pairs_ );
        break;
        
      case SpecUtils::EnergyCalType::LowerChannelEdge:
        cal->set_lower_channel_energy( nbin, coefficients );
        break;
        
      case SpecUtils::EnergyCalType::InvalidEquationType:
        break;
    }//switch( equation_type )
  }catch( std::exception &e )
  {
    energy_cal_error = "An invalid energy calibration was found: " + std::string(e.what());
  }//try / catch
}//void fill_binning()


bool MeasurementCalibInfo::operator<( const MeasurementCalibInfo &rhs ) const
{
  if( equation_type != rhs.equation_type )
    return (equation_type < rhs.equation_type);
  
  if( coefficients.size() != rhs.coefficients.size() )
    return (coefficients.size() < rhs.coefficients.size());
  
  for( size_t i = 0; i < coefficients.size(); ++i )
  {
    const float leftcoef = coefficients[i];
    const float rightcoef = rhs.coefficients[i];
    const float maxcoef = std::max( fabs(leftcoef), fabs(rightcoef) );
    if( fabs(leftcoef - rightcoef) > (1.0E-5 * maxcoef) )
      return coefficients[i] < rhs.coefficients[i];
  }//for( size_t i = 0; i < coefficients.size(); ++i )
  
  if( deviation_pairs_.size() != rhs.deviation_pairs_.size() )
    return (deviation_pairs_.size() < rhs.deviation_pairs_.size());
  
  for( size_t i = 0; i < deviation_pairs_.size(); ++i )
  {
    const pair<float,float> &lv = deviation_pairs_[i];
    const pair<float,float> &rv = rhs.deviation_pairs_[i];
    const float maxenergy = std::max( fabs(lv.first), fabs(rv.first) );
    const float epsilon = 1.0E-5f;
    
    if( fabs(lv.first - rv.first) > (epsilon * maxenergy))
      return lv.first < rv.first;
      
    const float maxdeviation = std::max( fabs(lv.second), fabs(rv.second) );
    
    if( fabs(lv.second - rv.second) > (epsilon * maxdeviation) )
      return lv.second < rv.second;
  }//for( size_t i = 0; i < deviation_pairs_.size(); ++i )
      
  return false;
}//bool operator<(...)


bool MeasurementCalibInfo::operator==( const MeasurementCalibInfo &rhs ) const
{
  const bool rhsLt = operator<(rhs);
  const bool lhsLt = rhs.operator<(*this);
  return !lhsLt && !rhsLt;
}
}//namespace for MeasurementCalibInfo

      
namespace
{
/** Thread-safe class to handle creating, and re-using equivalent, energy calibration objects from
  N42-2006 documents.
 
 To be implemented after getting everything compiling (20200626)
*/
      

class N42CalibrationCache2006
{
  std::mutex m_mutex;
  
  // Map from energy calibration to the object to use for it
  std::set<SpecUtils::MeasurementCalibInfo> m_cal;
  
  // Detector name to deviation pairs to use for it
  std::map<std::string,vector<pair<float,float>>> m_det_to_devpair;
      
  //map from "ID" to the raw energy calibration coefficients, from before we know number of channels
  //  (a N42 file could use same calibration for spectra with different number of channels, but
  //   SpecUtils::EnergyCalibration cant have this, or similarly with deviation pairs)
  typedef pair<SpecUtils::EnergyCalType,vector<float>> RawCalInfo_t;
  std::map<std::string,RawCalInfo_t> m_id_cal_raw;
  
  // Keep track of the last new calibration used for each detector incase a <Spectrum> doesnt
  //  indicate a calibration to use (a number of N42-2006 variants only give the energy calibration
  //  for the first sample)
  std::map<std::string,std::map<size_t,std::shared_ptr<const SpecUtils::EnergyCalibration>>> m_detname_to_cal;
      
  void parse_dev_pairs_from_xml( const rapidxml::xml_node<char> * const doc_node );
  void parse_cals_by_id_from_xml( const rapidxml::xml_node<char> * const doc_node );
      
  static const rapidxml::xml_node<char> *find_N42InstrumentData_node(
                                                      const rapidxml::xml_node<char> *doc_node );
      
public:
  N42CalibrationCache2006( const rapidxml::xml_node<char> * const doc_node )
  {
    parse_dev_pairs_from_xml( doc_node );
    parse_cals_by_id_from_xml( doc_node );
  }
  
  void get_spectrum_energy_cal( const rapidxml::xml_node<char> *spectrum_node,
                                const size_t nchannels,
                                shared_ptr<const SpecUtils::EnergyCalibration> &energy_cal,
                                string &error_message );
      
  void get_calibration_energy_cal( const rapidxml::xml_node<char> *cal_node, const size_t nchannels,
                        const string &det_name,
                        const size_t coefficents_index,
                        shared_ptr<const SpecUtils::EnergyCalibration> &energy_cal,
                        string &error_message );
      
  /**
    @param coefficents_index The index of the <Coefficients> entry to use, under the <Equation>
           node (if its present)
    @returns True if probably parsed energy calibration parameters okay from <Calibration> node.
  */
  static bool parse_calibration_node( const rapidxml::xml_node<char> *calibration_node,
                                      const size_t coefficents_index,
                                      SpecUtils::EnergyCalType &type,
                                      std::vector<float> &coefs );
      
   /** Function to convert a #SpecUtils::MeasurementCalibInfo into a #SpecUtils::EnergyCalibration.
    
    @param info The input energy calibration information.
    @param energy_cal Resulting energy calibration; will be null if calibration invalid
    @param error_message Error message if any when creating calibration; if no error, then string
           is not changed (e.g., not cleared when no error)
    */
  static void make_energy_cal( const SpecUtils::MeasurementCalibInfo &info,
                               const size_t nchannels,
                               shared_ptr<const SpecUtils::EnergyCalibration> &energy_cal,
                               string &error_message );
};//class N42CalibrationCache2006

const rapidxml::xml_node<char> *N42CalibrationCache2006::find_N42InstrumentData_node(
                                                          const rapidxml::xml_node<char> *doc_node )
{
  using SpecUtils::xml_first_node_nso;
      
  if( !doc_node )
    return nullptr;
      
  const char target_name[] = "N42InstrumentData";
  const string xmlns = get_n42_xmlns(doc_node);
    
  auto xml_name_icontains = [=]( const rapidxml::xml_node<char> *node ) -> bool {
    const string name = SpecUtils::xml_name_str(node);
    return SpecUtils::icontains(name, target_name);
  };//xml_name_icontains
      
      
  if( xml_name_icontains(doc_node) )
    return doc_node;
  
  const rapidxml::xml_node<char> *N42InstrumentData = nullptr;
          
  //Search around a bit for it (XPath would be nice, maybe if we ever upgrade this code to pugixml)
      
  //Search doc_nodes children
  N42InstrumentData = xml_first_node_nso(doc_node,target_name,xmlns,false);
  if( N42InstrumentData )
    return N42InstrumentData;
      
  //Check case of HPRDS file
  const auto eventnode = xml_first_node_nso(doc_node,"Event",xmlns,false);
  if( eventnode )
  {
    N42InstrumentData = xml_first_node_nso(eventnode,target_name,xmlns,false);
      
    if( N42InstrumentData )
      return N42InstrumentData;
  }//if( eventnode )
  
  //Lets search doc_node parent and sibling
  if( doc_node->parent() )
  {
    if( xml_name_icontains(doc_node->parent()) )
      return doc_node->parent();
      
    for( auto node = doc_node->parent()->first_node(); node; node = node->next_sibling() )
    {
      if( xml_name_icontains(node) )
        return node;
      
      N42InstrumentData = xml_first_node_nso(node,target_name,xmlns,false);
      if( N42InstrumentData )
        return N42InstrumentData;
    }
  }//if( !N42InstrumentData && doc_node->parent() )
          
  //Lets search doc_node's grandchildren
  for( auto node = doc_node->first_node(); node; node = node->next_sibling() )
  {
    N42InstrumentData = xml_first_node_nso(node,target_name,xmlns,false);
    if( N42InstrumentData )
      return N42InstrumentData;
  }
  
  return nullptr;
}//find_N42InstrumentData_node(...)
      
      
void N42CalibrationCache2006::parse_dev_pairs_from_xml( const rapidxml::xml_node<char> * const doc_node )
{
  //spectrometer: <?xml> -> <N42InstrumentData> -> <Measurement "maybe many of these">
  //                     -> <InstrumentInformation> -> no example of NonlinearityCorrection
  // HPRDS:       <?xml> -> <Event> -> <N42InstrumentData> -> <InstrumentInformation>
  //                     -> no example of NonlinearityCorrection
  // Portal:      <?xml> -> <N42InstrumentData> -> <Measurement "one of these">
  //                     -> <InstrumentInformation>
  //                     -> <dndons:NonlinearityCorrection Detector="Aa1">
  // Specification: In descriptive portion of spec <dndons:NonlinearityCorrection> elements parent is
  //       <Spectrum>, although example gives as child of <InstrumentInformation>
      
  using namespace SpecUtils;
  
  // We will parse the deviation pairs from the first <InstrumentInformation> we find.  In principle
  //  spectrometers could update these evenre <Measurement>, but in practice this makes no sense
  
  const rapidxml::xml_node<char> *N42InstrumentData = find_N42InstrumentData_node(doc_node);
    
  if( !N42InstrumentData )
  {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
    log_developer_error( __func__, "Could not find N42InstrumentData in XML document" );
#endif
    return;
  }//if( !N42InstrumentData )
  
  const string xmlns = get_n42_xmlns(N42InstrumentData);
      
  bool found_dp = false;
  auto parse_pairs = [this,&found_dp,&xmlns]( const rapidxml::xml_node<char> *info_node ){
    
    for( auto nl_corr_node = xml_first_node_nso( info_node, "NonlinearityCorrection", xmlns );
         nl_corr_node;
         nl_corr_node = XML_NEXT_TWIN(nl_corr_node) )
    {
      const rapidxml::xml_attribute<char> *det_attrib = XML_FIRST_ATTRIB( nl_corr_node, "Detector" );
        
      if( !det_attrib )
      {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        log_developer_error( __func__, "Found NonlinearityCorrection without Detector tag" );
#endif
        continue;
      }//if( !det_attrib )
        
      const string det_name = xml_value_str(det_attrib);
        
      vector< pair<float,float> > deviatnpairs;
      for( const rapidxml::xml_node<char> *dev_node = xml_first_node_nso( nl_corr_node, "Deviation", xmlns );
            dev_node;
            dev_node = XML_NEXT_TWIN(dev_node) )
      {
        if( dev_node->value_size() )
        {
          vector<float> devpair;
          const bool success = SpecUtils::split_to_floats( dev_node->value(), dev_node->value_size(), devpair );
            
          if( success && devpair.size()==2 )
          {
            deviatnpairs.push_back( pair<float,float>(devpair[0],devpair[1]) );
          }else
          {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
            log_developer_error( __func__, ("Could not put '" + xml_value_str(dev_node) + "' into deviation pair").c_str() );
#endif
          }
        }//if( dev_node->value_size() )
      }//for( loop over <dndons:Deviation> )
        
      found_dp = !deviatnpairs.empty();
      std::unique_lock<std::mutex> scoped_lock( m_mutex );
      m_det_to_devpair[det_name] = deviatnpairs;
    }//for( loop over <dndons:NonlinearityCorrection> )
  };//parse_pairs lambda
     
  //First try the HPRDS case
  const rapidxml::xml_node<char> *inst_info = xml_first_node_nso( N42InstrumentData, "InstrumentInformation", xmlns );
  parse_pairs( inst_info );
  if( found_dp )
    return;
      
  //Next try the portal and spectrometer cases
  for( const rapidxml::xml_node<char> *Measurement = xml_first_node_nso( N42InstrumentData, "Measurement", xmlns );
      !found_dp && Measurement;
      Measurement = XML_NEXT_TWIN(Measurement) )
  {
    inst_info = xml_first_node_nso( Measurement, "InstrumentInformation", xmlns );
    parse_pairs( inst_info );
  }//for( lop over <Measurement> nodes )
  
  //We will take care of the "Specification" case seperately.
}//void parse_dev_pairs_from_xml( rapidxml::xml_document<char> &doc )
      
     
// For N42-2006 files that use the <Spectrum CalibrationIDs="..."> method of assigning energy
//  calibrations, we will parse these calibrations once.
void N42CalibrationCache2006::parse_cals_by_id_from_xml( const rapidxml::xml_node<char> * const doc_node )
{
  using namespace SpecUtils;
      
  if( !doc_node )
    return;
  
  const string xmlns = get_n42_xmlns(doc_node);
      
  //We will collect all the <Calibration> nodes, and then go over them.
  vector<const rapidxml::xml_node<char> *> cal_nodes;
  auto add_cal_nodes = [&cal_nodes,xmlns]( const rapidxml::xml_node<char> * const parent ){
    for( auto node = xml_first_node_nso( parent, "Calibration", xmlns );
         node;
         node = XML_NEXT_TWIN(node) )
    {
      cal_nodes.push_back( node );
    }
  };//add_cal_nodes lamda
      
  const rapidxml::xml_node<char> *N42InstrumentData = find_N42InstrumentData_node(doc_node);
    
  // <N42InstrumentData> -> <Calibration>
  add_cal_nodes( N42InstrumentData );
    
  //Case from a legacy comment that I think can be gotten rid of now.
  if( doc_node->parent() )
    add_cal_nodes( doc_node->parent() );
      
  //HPRDS
  if( N42InstrumentData != doc_node )
  {
    // <Event> -> <Calibration>
    add_cal_nodes( doc_node );
    
    // <Event> -> <InstrumentInformation> -> <Calibration>
    for( auto node = xml_first_node_nso( doc_node, "InstrumentInformation", xmlns );
         node; node = XML_NEXT_TWIN(node) )
    {
      add_cal_nodes( node );
    }
  }//if( HPRDS )
      
  // <N42InstrumentData> -> <InstrumentInformation> -> <Calibration>
  for( auto node = xml_first_node_nso( N42InstrumentData, "InstrumentInformation", xmlns );
       node;
       node = XML_NEXT_TWIN(node) )
  {
    add_cal_nodes( node );
  }
  
  // <N42InstrumentData> -> <Measurement> -> <Calibration>
  for( auto node = xml_first_node_nso( N42InstrumentData, "Measurement", xmlns );
       node;
       node = XML_NEXT_TWIN(node) )
  {
    add_cal_nodes( node );
    
    // <N42InstrumentData> -> <Measurement> -> <InstrumentInformation> -> <Calibration>
    for( auto inst_node = xml_first_node_nso( node, "InstrumentInformation", xmlns );
         inst_node;
         inst_node = XML_NEXT_TWIN(inst_node) )
    {
      add_cal_nodes( inst_node );
    }
  }//for( loop over <Measurement> )
      
        
  for( const auto cal_node : cal_nodes )
  {
    const auto spec_id_att = cal_node->first_attribute( "ID", 2, false );
    const string spec_id = xml_value_str( spec_id_att );
      
    //parse_calibration_node(...) makes sure its not a FWHM calibration; what other types are there?
    const auto type_att = XML_FIRST_ATTRIB( cal_node, "Type" );
    if( type_att && !XML_VALUE_ICOMPARE(type_att, "Energy") )
      continue;
    
    std::vector<float> coefs;
    SpecUtils::EnergyCalType type;
    
    /// \TODO: add in a check we dont already have information for 'spec_id'
    if( parse_calibration_node(cal_node, 0, type, coefs) )
      m_id_cal_raw[spec_id] = {type, std::move(coefs)};
  }//for( loop over calibrations )
}//void parse_cals_by_id_from_xml( rapidxml::xml_document<char> &doc )
      
      
bool N42CalibrationCache2006::parse_calibration_node( const rapidxml::xml_node<char> *calibration_node,
                             const size_t coefficents_index,
                             SpecUtils::EnergyCalType &type,
                             std::vector<float> &coefs )
{
  type = SpecUtils::EnergyCalType::InvalidEquationType;
  coefs.clear();
      
  if( !calibration_node )
    return false;
      
  const string xmlns = get_n42_xmlns(calibration_node);
      
  const auto type_node = XML_FIRST_ATTRIB(calibration_node, "Type");
       
  if( type_node && type_node->value_size() )
  {
    if( XML_VALUE_ICOMPARE(type_node, "FWHM") )
      return false;
         
    //20160601: Not adding in an explicit comparison for energy, but probably should...
    //if( !XML_VALUE_ICOMPARE(type, "Energy") )
    //  return false
  }//if( type && type->value_size() )
  
  float units = 1.0f;
  if( const auto units_node = XML_FIRST_ATTRIB(calibration_node,"EnergyUnits") )
  {
    const string unitstr = SpecUtils::xml_value_str(units_node);
    if( unitstr == "eV" )
      units = 0.001f;
    else if( unitstr == "MeV" )
      units = 1000.0f;
  }//if( units attribute )
      
      
  const auto equation_node = SpecUtils::xml_first_node_nso( calibration_node, "Equation", xmlns );
      
  if( equation_node )
  {
    size_t index = 0;
    auto coeff_node = SpecUtils::xml_first_node_nso( equation_node, "Coefficients", xmlns );
    for( ; coeff_node && (index != coefficents_index); coeff_node = XML_NEXT_TWIN(coeff_node) )
    {
      ++index;
    }
      
    if( !coeff_node || (index != coefficents_index) )
      return false;
    
    if( coeff_node->value_size() )
    {
      SpecUtils::split_to_floats( coeff_node->value(), coeff_node->value_size(), coefs );
    }else
    {
      //SmithsNaI HPRDS
      const auto subeqn = XML_FIRST_ATTRIB(coeff_node,"Subequation");
      if( subeqn && subeqn->value_size() )
        SpecUtils::split_to_floats( subeqn->value(), subeqn->value_size(), coefs );
    }//if( coeff_node->value_size() ) / else
           
    while( coefs.size() && coefs.back()==0.0f )
      coefs.erase( coefs.end()-1 );
      
    if( units != 1.0f )
    {
      for( float &f : coefs )
        f *= units;
    }
                
    const auto model_att = XML_FIRST_ATTRIB(equation_node,"Model");
    const string modelstr = SpecUtils::xml_value_str(model_att);
      
    if( SpecUtils::iequals_ascii( modelstr, "Polynomial") )
      type = SpecUtils::EnergyCalType::Polynomial;
    else if( SpecUtils::iequals_ascii( modelstr, "FullRangeFraction") )
      type = SpecUtils::EnergyCalType::FullRangeFraction;
    else if( SpecUtils::iequals_ascii( modelstr, "LowerChannelEdge")
             || SpecUtils::iequals_ascii( modelstr, "LowerBinEdge") )
      type = SpecUtils::EnergyCalType::LowerChannelEdge;
    else if( modelstr == "Other" )
    {
      const auto form_att = XML_FIRST_ATTRIB(equation_node,"Form");
      const string formstr = SpecUtils::xml_value_str(form_att);
      if( SpecUtils::icontains(formstr, "Lower edge") )
        type = SpecUtils::EnergyCalType::LowerChannelEdge;
    }//if( modelstr == ...) / if / else
         
    //Lets try to guess the equation type for Polynomial or FullRangeFraction
    if( type == SpecUtils::EnergyCalType::InvalidEquationType )
    {
      if( (coefs.size() <= 5) && (coefs.size() > 1) && (fabs(coefs[0]) < 300.0f) )
      {
        if( coefs[1] < 100.0 )
          type = SpecUtils::EnergyCalType::Polynomial;
        else if( coefs[1] > 1000.0 )
          type = SpecUtils::EnergyCalType::FullRangeFraction;
      }//if( coefs.size() <= 5 )
    }//if( type == InvalidEquationType )
      
    if( type == SpecUtils::EnergyCalType::InvalidEquationType )
      coefs.clear();
  
    if( coefs.size() < 2 )
      type = SpecUtils::EnergyCalType::InvalidEquationType;
      
    // \TODO: we could probably do some more sanity checks here
    if( type != SpecUtils::EnergyCalType::InvalidEquationType )
    {
      // TODO: Note, RadSeeker files contain a <ArrayXY> sibling (to <Equation>) element that contains ~6 <PointXY> elements that give channel-to-energy mappings, which could/should be interpreted as non-linear deviation pairs.
      
      return true;
    }
  }//if( equation_node )
      
  const auto array_node = SpecUtils::xml_first_node_nso( calibration_node, "ArrayXY", xmlns );
  if( !array_node )
    return false;
    
  //This case has been added in for FLIR identiFINDER N42 files
  //        <Calibration Type="Energy" ID="calibration" EnergyUnits="keV">
  //          <ArrayXY X="Channel" Y="Energy">
  //            <PointXY>
  //              <X>1 0</X>
  //              <Y>3 0</Y>
  //            </PointXY>
  //          </ArrayXY>
  //        </Calibration>
  //
  // However, RIIDEye files look like
  //      <Calibration Type="Energy" ID="en" EnergyUnits="keV">
  //        <Remark>FSE=3072 GrpSz=512</Remark>
  //        <CalibrationCreationDate>2018-07-18T13:54:29Z</CalibrationCreationDate>
  //        <ArrayXY X="Channels" Y="keV">
  //          <PointXY><X>0 0</X><Y>0 0</Y></PointXY>
  //          <PointXY><X>1 0</X><Y>0 0</Y></PointXY>
  //          <PointXY><X>2 0</X><Y>0 0</Y></PointXY>
  //          <PointXY><X>3 0</X><Y>1.93823 0</Y></PointXY>
  //          <PointXY><X>4 0</X><Y>4.63947 0</Y></PointXY>
  //          <PointXY><X>5 0</X><Y>7.44489 0</Y></PointXY>
  //          <PointXY><X>6 0</X><Y>10.3545 0</Y></PointXY>
  //          ...
  // There is also the RadSeeker case  (see note above) that uses this to give non-linear
  //  deviation info
                  
  vector<pair<float,float>> points;
                    
  for( auto point_node = SpecUtils::xml_first_node_nso( array_node, "PointXY", xmlns );
      point_node;
      point_node = XML_NEXT_TWIN(point_node) )
  {
    const auto x_node = SpecUtils::xml_first_node_nso( point_node, "X", xmlns );
    const auto y_node = SpecUtils::xml_first_node_nso( point_node, "Y", xmlns );
                      
    if( x_node && x_node->value_size() && y_node && y_node->value_size() )
    {
      float xval = 0.0f, yval = 0.0f;
      if( xml_value_to_flt(x_node, xval) && xml_value_to_flt(y_node, yval) )
        points.emplace_back(xval,yval);
    }//if( x and y nodes )
  }//for( loop over <PointXY> )
                    
  const size_t npoints = points.size();
              
  if( npoints == 1 && (points[0].second*units > 0.0f) && (points[0].second*units < 100.0f) )
  {
    //FLIR identiFINDER style
    type = SpecUtils::EnergyCalType::Polynomial;
    coefs = {0.0f, points[0].second*units };
    return true;
  }if( npoints == 1 && (points[0].second*units > 100.0f) )
  {
    //This is a guess - I havent encountered it
    type = SpecUtils::EnergyCalType::FullRangeFraction;
    coefs = {0.0f, points[0].second*units };
  }else if( npoints > 6 ) //The files I've seen have (npoints==nchannel)
  {
    //We also need to make sure the 'x' values are monotonically increasing channel numbers
    //  that start at zero or one.
    bool increasing_bin = ((fabs(points[0].first) < FLT_EPSILON) || (fabs(points[1].first-1.0) < FLT_EPSILON));
    for( size_t i = 1; increasing_bin && i < npoints; ++i )
      increasing_bin = ((fabs(points[i].first-points[i-1].first-1.0f) < FLT_EPSILON) && (points[i].second>=points[i-1].second));
                      
    if( increasing_bin )
    {
      type = SpecUtils::EnergyCalType::LowerChannelEdge;
      for( const auto &ff : points )
        coefs.push_back( ff.second*units );
      return true;
    }else
    {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
        log_developer_error( __func__, "Couldnt interpret energy calibration PointXY (not monototonically increasing)" );
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
    }
  }//if( npoints == 1 ) / else

  coefs.clear();
  type = SpecUtils::EnergyCalType::InvalidEquationType;
      
  return false;
}//parse_calibration_node(...)
      
      
void N42CalibrationCache2006::get_calibration_energy_cal( const rapidxml::xml_node<char> *cal_node,
                              const size_t nchannels,
                              const string &det_name,
                              const size_t coefficents_index,
                              shared_ptr<const SpecUtils::EnergyCalibration> &energy_cal,
                              string &error_message )
{
  energy_cal.reset();
      
  // \TODO: I havent seen any N42-2006 files that list deviation pairs under the spectrum or its
  //        calibration, but I wouldnt be suprised, so this should maybe be implemented at some
  //        point, I guess, maybe
  //const rapidxml::xml_node<char> *nonlinarity_node = xml_first_node_nso( spectrum_node, "NonlinearityCorrection", xmlns );
  //if( !nonlinarity_node )
  //  nonlinarity_node = xml_first_node_nso( calibration_node, "NonlinearityCorrection", xmlns );
      
  vector<float> coeffs;
  SpecUtils::EnergyCalType type;
      
  if( !parse_calibration_node(cal_node, coefficents_index, type, coeffs) )
    return;
      
  assert( type != SpecUtils::EnergyCalType::InvalidEquationType );
        
  SpecUtils::MeasurementCalibInfo calinfo;
  calinfo.equation_type = type;
  calinfo.coefficients = coeffs;
  
  {//begin lock on m_mutex
    std::unique_lock<std::mutex> scoped_lock( m_mutex );
        
    auto devpos = m_det_to_devpair.find(det_name);
    //  We'll see if there are deviation pairs without a name, and if so use them...
    if( !det_name.empty() && (devpos == end(m_det_to_devpair)) )
      devpos = m_det_to_devpair.find("");
    if( devpos != end(m_det_to_devpair) )
      calinfo.deviation_pairs_ = devpos->second;
        
    auto calinfopos = m_cal.find(calinfo);
    if( (calinfopos != end(m_cal)) && calinfopos->energy_cals.count(nchannels) )
    {
      //We have already cached and equivalent calibration - lets return it.
      error_message.clear();
      const auto calpos = calinfopos->energy_cals.find(nchannels);
      assert( calpos != end(calinfopos->energy_cals) );
      energy_cal = calpos->second;
      m_detname_to_cal[det_name][nchannels] = energy_cal;
      return;
    }
  }//end lock on m_mutex
      
  make_energy_cal( calinfo, nchannels, energy_cal, error_message );
        
  if( energy_cal )
  {
    //Add this new calibration into the cache.
    error_message.clear();
    std::unique_lock<std::mutex> scoped_lock( m_mutex );
    
    auto calinfopos = m_cal.find(calinfo);
    if( calinfopos != end(m_cal) )
    {
      calinfo = *calinfopos;
      m_cal.erase(calinfopos);
    }
    
    calinfo.energy_cals[nchannels] = energy_cal;
    
    calinfopos = m_cal.insert( calinfo ).first;
    m_detname_to_cal[det_name][nchannels] = energy_cal;
  }//if( energy_cal )
}//get_calibration_energy_cal( ... )
      
      
void N42CalibrationCache2006::make_energy_cal(
                              const SpecUtils::MeasurementCalibInfo &info,
                              const size_t nchannels,
                              shared_ptr<const SpecUtils::EnergyCalibration> &energycal,
                              string &error_message )
{
  auto energycal_tmp = make_shared<SpecUtils::EnergyCalibration>();
      
  try
  {
    switch( info.equation_type )
    {
      case SpecUtils::EnergyCalType::Polynomial:
        energycal_tmp->set_polynomial( nchannels, info.coefficients, info.deviation_pairs_ );
      break;
      
      case SpecUtils::EnergyCalType::FullRangeFraction:
        energycal_tmp->set_full_range_fraction( nchannels, info.coefficients, info.deviation_pairs_ );
      break;
      
      case SpecUtils::EnergyCalType::LowerChannelEdge:
        energycal_tmp->set_lower_channel_energy( nchannels, info.coefficients );
      break;
      
      case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
        energycal_tmp->set_default_polynomial( nchannels, info.coefficients, info.deviation_pairs_ );
      break;
      
      case SpecUtils::EnergyCalType::InvalidEquationType:
        energycal_tmp.reset();
      break;
    }//switch( type )
  }catch( std::exception &e )
  {
    energycal_tmp.reset();
    error_message = e.what();
  }//try / catch
      
  energycal = energycal_tmp;
}//make_energy_cal(...)
      
      
/// 20200628: put this as a member of energy calibration cache class and include deviation pairs and
/// such.  SHould probably return an error code or something too.
void N42CalibrationCache2006::get_spectrum_energy_cal( const rapidxml::xml_node<char> *spectrum_node,
                              const size_t nchannels,
                              shared_ptr<const SpecUtils::EnergyCalibration> &energy_cal,
                              string &error_message )
{
  using SpecUtils::xml_first_node_nso;
        
  energy_cal.reset();
  error_message.clear();
        
  if( !nchannels || !spectrum_node )
    return;
  
  const string xmlns = get_n42_xmlns( spectrum_node );// <Spectrum> wont have a namespace but calibration could
      
  const auto det_name_attrib = find_detector_attribute( spectrum_node );
  const string det_name = SpecUtils::xml_value_str( det_name_attrib );
      
  //Take the first <Calibration> node (sometimes theres more than one - we'll deal with those cases
  //  later)
  for( auto cal_node = xml_first_node_nso( spectrum_node, "Calibration", xmlns );
       cal_node;
       cal_node = XML_NEXT_TWIN(cal_node) )
  {
    get_calibration_energy_cal( cal_node, nchannels, det_name, 0, energy_cal, error_message );
    
    if( energy_cal )
      return;
  }//for( loop over <Calibration> )
    
  //Lets see if we can find a calibration by ID
  const auto cal_IDs_att = XML_FIRST_ATTRIB( spectrum_node, "CalibrationIDs" );
  const string cal_IDs_str = SpecUtils::xml_value_str(cal_IDs_att);
  vector<string> cal_ids;
  SpecUtils::split( cal_ids, cal_IDs_str, " \t" );
    
  {//begin lock on m_mutex
    std::unique_lock<std::mutex> scoped_lock( m_mutex );
    if( m_id_cal_raw.size() == 1 )
    {
      if( cal_ids.empty() || (begin(m_id_cal_raw)->first == "") )
        cal_ids.push_back( begin(m_id_cal_raw)->first );
    }
      
    //This matching based on only first two letters, if we dont already have a match, was based
    //  on the (legacy?) code 20200629 that didnt have any comments as to why it was done.
    bool is_match = false;
    for( const string &calid : cal_ids )
      is_match = (is_match || m_id_cal_raw.count(calid));
    
    if( !is_match )
    {
      for( const auto &haveids : m_id_cal_raw )
      {
        string idid = haveids.first;
        idid = idid.size()>1 ? idid.substr(0,2) : idid;
      
        for( string calid : cal_ids )
        {
          calid = calid.size()>1 ? calid.substr(0,2) : calid;
          if( calid == idid )
            cal_ids.push_back( haveids.first );
        }
      }//for( const auto &haveids : m_id_cal_raw )
    }//if( !is_match )
  }//end lock on m_mutex
  
      
  for( const string &calid : cal_ids )
  {
    std::unique_lock<std::mutex> scoped_lock( m_mutex );
      
    const auto pos = m_id_cal_raw.find( calid );
    if( pos == end(m_id_cal_raw) )
      continue;
      
    const pair<SpecUtils::EnergyCalType,vector<float>> &info = pos->second;
    
    SpecUtils::MeasurementCalibInfo calinfo;
    calinfo.equation_type = info.first;
    calinfo.coefficients = info.second;
    
    auto devpos = m_det_to_devpair.find(det_name);
    //  We'll see if there are deviation pairs without a name, and if so use them...
    if( !det_name.empty() && (devpos == end(m_det_to_devpair)) )
      devpos = m_det_to_devpair.find("");
    if( devpos != end(m_det_to_devpair) )
      calinfo.deviation_pairs_ = devpos->second;
    
    auto calpos = m_cal.find(calinfo);
    if( (calpos != end(m_cal)) && calpos->energy_cals.count(nchannels) )
    {
      error_message.clear();
      const auto calptrpos = calpos->energy_cals.find(nchannels);
      assert( calptrpos != end(calpos->energy_cals) );
      energy_cal = calptrpos->second;
      m_detname_to_cal[det_name][nchannels] = energy_cal;
      return;
    }
      
    make_energy_cal( calinfo, nchannels, energy_cal, error_message );
      
    if( energy_cal )
    {
      if( calpos != end(m_cal) )
      {
        calinfo = *calpos;
        m_cal.erase( calpos );
      }
      
      calinfo.energy_cals[nchannels] = energy_cal;
      calpos = m_cal.insert(calinfo).first;
      m_detname_to_cal[det_name][nchannels] = energy_cal;
      error_message.clear();
      return;
    }//if( energy_cal )
  }//for( const string &calid : cal_ids )
      
  //See if we have seen a calibration for this detector
  {//begin lock on m_mutex
    std::unique_lock<std::mutex> scoped_lock( m_mutex );
    const auto pos = m_detname_to_cal.find(det_name);
    if( pos != end(m_detname_to_cal) )
    {
      const auto nchanpos = pos->second.find(nchannels);
      if( nchanpos != end(pos->second) )
      {
        assert( nchanpos->second );
        energy_cal = nchanpos->second;
        error_message.clear();
        m_detname_to_cal[det_name][nchannels] = energy_cal;
      }
      return;
    }//if( we have seen a calibration for this detector )
  }//end lock on m_mutex
  
      
  //No calibrations found
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
  //If there is a cal ID, or a <Calibration> node, but we didnt find a calibration, lets note this
  // but it may not actually be a problem
  if( nchannels > 1
      && (!cal_IDs_str.empty() || xml_first_node_nso(spectrum_node, "Calibration", xmlns))
      && cal_IDs_str != "energy shape" )
  {
    string msg = "Failed to find calibration for ID='" + cal_IDs_str + "', Det='" + det_name + "'"
                 " (this may an issue with input file, and not parsing code)";
    log_developer_error( __func__, msg.c_str() );
  }
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
}//void get_spectrum_energy_cal(...)
      
}//namespace for N42CalibrationCache2006
      

//A private namespace to contain some structs to help us to mult-threaded decoding.
namespace SpecUtils
{
const string N42DecodeHelper2006_failed_decode_title = "AUniqueStringToMarkThatThisDecodingFailed";
struct N42DecodeHelper2006
{
  const rapidxml::xml_node<char> *m_spec_node;
  std::mutex *m_mutex;
  std::shared_ptr<SpecUtils::Measurement> m_meas;
  std::shared_ptr<SpecUtils::DetectorAnalysis> m_analysis_info;
  const rapidxml::xml_node<char> *m_dose_data_parent;
  const rapidxml::xml_node<char> *m_doc;
  N42CalibrationCache2006 &m_energy_cal;
      
  N42DecodeHelper2006( const rapidxml::xml_node<char> *node_in,
                       std::mutex *mutex_ptr,
                       std::shared_ptr<SpecUtils::Measurement> meas,
                       std::shared_ptr<SpecUtils::DetectorAnalysis> analysis_info_ptr,
                       const rapidxml::xml_node<char> *dose_data_parent,
                       const rapidxml::xml_node<char> *doc_node,
                       N42CalibrationCache2006 &energy_cal )
    : m_spec_node( node_in ),
    m_mutex( mutex_ptr ),
    m_meas( meas ),
    m_analysis_info( analysis_info_ptr ),
    m_dose_data_parent( dose_data_parent ),
    m_doc( doc_node ),
    m_energy_cal( energy_cal )
  {
    assert( meas );
  }
    
  static void filter_valid_measurements( vector< std::shared_ptr<SpecUtils::Measurement> > &meass )
  {
    vector< std::shared_ptr<SpecUtils::Measurement> > valid_meass;
    valid_meass.reserve( meass.size() );
      
    for( auto &meas : meass )
    {
      //      const bool hasGammaData = (meas->gamma_counts() && meas->gamma_counts()->size());
      //      const bool hasNeutData = (meas->neutron_counts().size() || meas->contained_neutron_);
      //      if( hasGammaData || hasNeutData )
        
      if( meas->title() != N42DecodeHelper2006_failed_decode_title )
        valid_meass.push_back( meas );
    }//for( std::shared_ptr<Measurement> &meas : meass )
      
    meass.swap( valid_meass );
  }
    
  static void decode_2006_n42_spectrum_node( const rapidxml::xml_node<char> *spec_node,
                                      N42CalibrationCache2006 &energy_cal_cache,
                                      SpecUtils::Measurement &meas )
  {
    if( !spec_node )
      throw runtime_error( "set_2006_N42_spectrum_node_info: Recieved NULL 'Spectrum' node" );
          
    const string xmlns = get_n42_xmlns( spec_node );
      
    std::shared_ptr<LocationState> location;
    
    for( const rapidxml::xml_node<char> *remark_node = xml_first_node_nso( spec_node, "Remark", xmlns );
        remark_node;
        remark_node = XML_NEXT_TWIN(remark_node) )
    {
      string remark_from_node = xml_value_str( remark_node );
      
      vector<string> remark_lines;
      split( remark_lines, remark_from_node, "\r\n" );
        
      for( string &remark : remark_lines )
      {
        trim( remark );
        if( remark.empty() )
          continue;
          
        if( SpecUtils::istarts_with( remark, s_parser_warn_prefix) )
        {
          SpecUtils::ireplace_all( remark, s_parser_warn_prefix, "" );
          meas.parse_warnings_.emplace_back( std::move(remark) );
          continue;
        }
          
        if( SpecUtils::istarts_with( remark, "Title:") )
        {
          remark = remark.substr(6);
          trim( remark );
          meas.title_ = remark;
          continue;
        }
          
        meas.remarks_.push_back( remark );
          
        if( meas.sample_number_ < 0 )
        {
          meas.sample_number_ = sample_num_from_remark( meas.remarks_.back() );
        }else
        {
          const int samplen = sample_num_from_remark( meas.remarks_.back() );
          if( samplen != meas.sample_number_ && samplen>=0 )
          {
            meas.parse_warnings_.push_back( "Multiple remarks provided different sample numbers" );
          }
            
          //marking it intrinsic activity will happen further down from the 'ID'
          //  attribute, so we wont wast cpu time here checking the remark for itww
          //      if( SpecUtils::icontains( remark, "intrinsic activity") )
          //        meas.source_type_ = SourceType::IntrinsicActivity;
        }
          
        try
        {
          const float thisspeed = speed_from_remark( remark );
          
          if( !location )
          {
            location = make_shared<LocationState>();
            
            // I think we are here primarily if we are a portal, so it makes sense to assign the
            //  speed as `item`.
            location->type_ = LocationState::StateType::Item;
            meas.location_ = location;
          }
          location->speed_ = thisspeed;
        }catch( std::exception & )
        {
        }
          
        const string found_detector_name = detector_name_from_remark( meas.remarks_.back() );
        if( !found_detector_name.empty() && meas.detector_name_.empty() )
        {
          meas.detector_name_ = found_detector_name;
        }else if( meas.detector_name_ != found_detector_name )
        {
          string msg = "Found another detector name, '" + found_detector_name
                        + "', for detector '" + meas.detector_name_ + "'";
          meas.parse_warnings_.emplace_back( std::move(msg) );
        }
      }//for( string remark, remark_lines )
    }//for( loop over remark_nodes )
      
      
    const auto sample_num_att = XML_FIRST_ATTRIB(spec_node, "SampleNumber");
    if( sample_num_att )
    {
      const string strvalue = xml_value_str( sample_num_att );
        
      int samplenum = -1;
      if( toInt( strvalue, samplenum ) )
      {
        if( meas.sample_number_ >= 2 )
        {
          string msg = "Replaced sample number " + std::to_string(meas.sample_number_)
                        + " with SampleNumber attribute value " + std::to_string(samplenum);
          meas.parse_warnings_.emplace_back( std::move(msg) );
        }
          
        meas.sample_number_ = samplenum;
      }else if( !strvalue.empty() )
      {
        string msg = "Couldnt convert SampleNumber '" + strvalue + "' to an integer";
        meas.parse_warnings_.emplace_back( std::move(msg) );
      }
    }//if( sample_num_att )
      
    const auto src_type_node = xml_first_node_nso( spec_node, "SourceType", xmlns );
      
    if( src_type_node )
    {
      if( XML_VALUE_ICOMPARE(src_type_node, "Item") )
        meas.source_type_ = SourceType::Foreground;
      else if( XML_VALUE_ICOMPARE(src_type_node, "Background") )
        meas.source_type_ = SourceType::Background;
      else if( XML_VALUE_ICOMPARE(src_type_node, "Calibration") )
        meas.source_type_ = SourceType::Calibration;
      else if( XML_VALUE_ICOMPARE(src_type_node, "Stabilization") ) //RadSeeker HPRDS files have the "Stabilization" source type, which looks like an intrinsic source
        meas.source_type_ = SourceType::IntrinsicActivity;
      else if( XML_VALUE_ICOMPARE(src_type_node, "IntrinsicActivity") )
        meas.source_type_ = SourceType::IntrinsicActivity;
      else
        meas.source_type_ = SourceType::Unknown;
    }//if( src_type_node )
      
    const rapidxml::xml_attribute<char> *id_att = spec_node->first_attribute( "ID", 2, false );
    if( id_att && XML_VALUE_ICOMPARE(id_att, "intrinsicActivity") )
      meas.source_type_ = SourceType::IntrinsicActivity;
      
    const rapidxml::xml_node<char> *uccupied_node = xml_first_node_nso( spec_node, "Occupied", xmlns );
      
    meas.occupied_ = parse_occupancy_status( uccupied_node );
      
    const rapidxml::xml_node<char> *det_type_node = xml_first_node_nso( spec_node, "DetectorType", xmlns );
    if( det_type_node && det_type_node->value_size() )
      meas.detector_description_ = xml_value_str( det_type_node );
      
    meas.quality_status_ = QualityStatus::Good; //Default to good, unless specified
    const rapidxml::xml_attribute<char> *quality_attrib = spec_node->first_attribute( "Quality", 7 );
    if( quality_attrib && quality_attrib->value_size() )
    {
      if( XML_VALUE_ICOMPARE( quality_attrib, "Good" ) )
        meas.quality_status_ = QualityStatus::Good;
      else if( XML_VALUE_ICOMPARE( quality_attrib, "Suspect" ) )
        meas.quality_status_ = QualityStatus::Suspect;
      else if( XML_VALUE_ICOMPARE( quality_attrib, "Bad" ) )
        meas.quality_status_ = QualityStatus::Bad;
      else if( XML_VALUE_ICOMPARE( quality_attrib, "Missing" )
                || XML_VALUE_ICOMPARE( quality_attrib, "Unknown" ) )
        meas.quality_status_ = QualityStatus::Missing;
      else
        meas.parse_warnings_.push_back( "Unknown quality status '"
                                            + SpecUtils::xml_value_str(quality_attrib) + "'" );
    }//if( quality_attrib is valid )
      
    const rapidxml::xml_attribute<char> *detector_attrib = find_detector_attribute( spec_node );
      
    if( detector_attrib && detector_attrib->value_size() )
    {
      if( !meas.detector_name_.empty()
          && (xml_value_str(detector_attrib) != meas.detector_name_) )
      {
      
        string msg = "Replacing detector name '" + meas.detector_name_ + "' with '"
                       + xml_value_str(detector_attrib) + "'";
        meas.parse_warnings_.emplace_back( std::move(msg) );
      }
      
      meas.detector_name_ = xml_value_str(detector_attrib);
    }//if( detector_attrib && detector_attrib->value() )
      
    const rapidxml::xml_node<char> *live_time_node  = xml_first_node_nso( spec_node, "LiveTime", xmlns );
    const rapidxml::xml_node<char> *real_time_node  = xml_first_node_nso( spec_node, "RealTime", xmlns );
    const rapidxml::xml_node<char> *start_time_node = xml_first_node_nso( spec_node, "StartTime", xmlns );
    
    if( live_time_node )
      meas.live_time_ = time_duration_string_to_seconds( live_time_node->value(), live_time_node->value_size() );
    if( real_time_node )
      meas.real_time_ = time_duration_string_to_seconds( real_time_node->value(), real_time_node->value_size() );
      
    if( !start_time_node )
      start_time_node = xml_first_node_nso( spec_node->parent(), "StartTime", xmlns );
      
    if( start_time_node )
      meas.start_time_ = time_from_string( xml_value_str(start_time_node).c_str() );
      
      
    //XXX Things we should look for!
    //Need to handle case <Calibration Type="FWHM" FWHMUnits="Channels"> instead of right now only handling <Calibration Type="Energy" EnergyUnits="keV">
      
    const rapidxml::xml_node<char> *channel_data_node = xml_first_node_nso( spec_node, "ChannelData", xmlns );  //can have attribute comsion, Start(The channel number (one-based) of the first value in this element), ListMode(string)
      
    if( !channel_data_node )
    {
      //The N42 analysis result file refU35CG8VWRM get here a lot (its not a valid
      //  spectrum file)
      throw runtime_error( "Error, didnt find <ChannelData> under <Spectrum>" );
    }//if( !channel_data_node )
      
    auto compress_attrib = channel_data_node->first_attribute( "Compression", 11 );
    if( !compress_attrib )
      compress_attrib = XML_FIRST_IATTRIB(channel_data_node, "compressionCode");
    
    const string compress_type = xml_value_str( compress_attrib );
    std::shared_ptr<std::vector<float>> contents = std::make_shared< vector<float> >();
      
    //Some variants have a <Data> tag under the <ChannelData> node.
    const rapidxml::xml_node<char> *datanode = xml_first_node_nso( channel_data_node, "Data", xmlns );
    if( datanode && datanode->value_size() )
      channel_data_node = datanode;
      
      
    const bool compressed_zeros = icontains(compress_type, "Counted"); //"CountedZeroes", at least one file has "CountedZeros"
      
    //XXX - this next call to split_to_floats(...) is not safe for non-destructively parsed XML!!!  Should fix.
    SpecUtils::split_to_floats( channel_data_node->value(), *contents, " ,\r\n\t", compressed_zeros );
      
    if( compressed_zeros )
    {
      expand_counted_zeros( *contents, *contents );
    }else if( (compress_type!="") && (contents->size()>2) && !icontains(compress_type, "Non" ) )
    {
      string msg = "Unknown spectrum Compression type: '" + compress_type + "', not applied.";
      meas.parse_warnings_.emplace_back( std::move(msg) );
    }
      
    //Fix cambio zero compression
    if( compressed_zeros )
    {
      for( float &val : *contents )
      {
        if( val > 0.0f && val <= 2.0f*FLT_MIN )
          val = 0.0f;
      }
    }//if( compressed_zeros )
      
    const rapidxml::xml_attribute<char> *type_attrib = spec_node->first_attribute( "Type", 4 );
      
    if( !type_attrib )
      type_attrib = spec_node->first_attribute( "DetectorType", 12 );
      
    if( !type_attrib && spec_node->parent() )
      type_attrib = spec_node->parent()->first_attribute( "DetectorType", 12 );            //<SpectrumMeasurement>
    if( !type_attrib && spec_node->parent() && spec_node->parent()->parent() )
      type_attrib = spec_node->parent()->parent()->first_attribute( "DetectorType", 12 );  //<DetectorMeasurement> node
      
    bool is_gamma = contents && !contents->empty();
    if( is_gamma )
      is_gamma = is_gamma_spectrum( detector_attrib, type_attrib, det_type_node, spec_node );
      
    if( is_gamma )
    {
      /* //20200702 The following can likely be delted as this logic looks to be covered under notes
         // for refSJHFSW1DZ4
      //The below handles a special case for Raytheon-Variant C-1/L-1 (see refSJHFSW1DZ4)
      const rapidxml::xml_node<char> *specsize = spec_node->first_node( "ray:SpectrumSize", 16 );
      if( specsize && specsize->value_size() )
      {
        vector<int> sizes;
        const char *str = specsize->value();
        const size_t strsize = specsize->value_size();
        if( SpecUtils::split_to_ints(str, strsize, sizes) && (sizes.size() == 1) )
        {
          const size_t origlen = contents->size();
          const size_t newlen = static_cast<size_t>(sizes[0]);
          if( newlen >= 64
              && newlen != origlen
              && newlen < origlen
              && (origlen % newlen)==0 )
          {
            contents->resize( newlen );
              
#if( PERFORM_DEVELOPER_CHECKS )
            char buffer[512];
            snprintf( buffer, sizeof(buffer),
                       "Reducing channel data from %i to %i channels on advice of"
                       " <ray:SpectrumSize>; note that this is throwing away %i"
                       " channels", int(origlen), int(newlen), int(origlen-newlen) );
            log_developer_error( __func__, buffer );
#endif  //#if( PERFORM_DEVELOPER_CHECKS )
          }
        }//if( SpecUtils::split_to_ints( str, strsize, sizes ) )
      }//if( specsize_node && specsize_node->value_size() )
      */
        
      meas.contained_neutron_ = false;
      meas.gamma_counts_ = contents;
        
      std::string cal_error_msg;
      shared_ptr<const EnergyCalibration> energy_cal;
      
      //Only get energy calibration if there is more than one channel (e.g., not for GMTubes)
      if( contents->size() > 1 )
        energy_cal_cache.get_spectrum_energy_cal( spec_node, contents->size(), energy_cal, cal_error_msg );
          
      if( energy_cal )
      {
        meas.energy_calibration_ = energy_cal;
      }else if( !cal_error_msg.empty()
            && !count(begin(meas.parse_warnings_), end(meas.parse_warnings_), cal_error_msg) )
      {
        meas.parse_warnings_.push_back( cal_error_msg );
      }
        
      for( const float x : *(meas.gamma_counts_) )
        meas.gamma_count_sum_ += x;
      
      
      if( (meas.gamma_count_sum_ <= DBL_EPSILON)
         && (meas.quality_status_ == QualityStatus::Missing) )
      {
        throw runtime_error( "Spectrum marked as missing, and all zeros" );
      }
    }else  //if( is_gamma )
    {
      meas.contained_neutron_ = true;
      if( meas.neutron_counts_.size() < contents->size() )
        meas.neutron_counts_.resize( contents->size(), 0.0 );
        
      for( size_t i = 0; i < contents->size(); ++i )
      {
        meas.neutron_counts_[i] += contents->operator[](i);
        meas.neutron_counts_sum_ += contents->operator[](i);
      }//for( loop over neutron counts )
    }//if( is_gamma ) / else
  }//void decode_2006_n42_spectrum_node()
      
      
  void operator()()
  {
    try
    {
      assert( m_meas );
      
      decode_2006_n42_spectrum_node( m_spec_node, m_energy_cal, *m_meas );
      
      const string xmlns = get_n42_xmlns(m_spec_node);
      const rapidxml::xml_node<char> *spec_parent = m_spec_node->parent();
        
      if( m_dose_data_parent )
      {
        //If m_spec_node has any immediate siblings, then we need to be careful in setting
        //  The count dose informiaton, so lets count them
        int nspectra = 0;
        if( spec_parent )
        {
          for( const rapidxml::xml_node<char> *node = spec_parent->first_node( m_spec_node->name(), m_spec_node->name_size() );
              node;
              node = XML_NEXT_TWIN(node) )
            ++nspectra;
        }
          
        for( const rapidxml::xml_node<char> *dose_data = xml_first_node_nso( m_dose_data_parent, "CountDoseData", xmlns );
            dose_data;
            (dose_data = XML_NEXT_TWIN(dose_data)))
        {
          if( nspectra < 2 )
          {
            m_meas->set_n42_2006_count_dose_data_info( dose_data );
          }else
          {
            const rapidxml::xml_node<char> *starttime = XML_FIRST_NODE(dose_data, "StartTime");
            if( !starttime )
            {
              // If we dont have <StartTime>, see if we can match up
              const rapidxml::xml_attribute<char> * const det_attrib = XML_FIRST_ATTRIB(dose_data, "Detector");
              if( !det_attrib )
                continue;
              
              const string name = xml_value_str(det_attrib);
              
              if( (m_meas->detector_name_ == name)
                 || SpecUtils::istarts_with(m_meas->detector_name_, name + "_intercal_") )
              {
                m_meas->set_n42_2006_count_dose_data_info( dose_data );
              }
              
              continue;
            }//if( !starttime )
              
            const time_point_t startptime = SpecUtils::time_from_string( xml_value_str(starttime).c_str() );
            if( is_special(startptime) )
              continue;
              
            time_point_t::duration thisdelta = startptime - m_meas->start_time_;
            if( thisdelta < time_point_t::duration::zero() )
              thisdelta = -thisdelta;
            //const float realtime = time_duration_string_to_seconds(realtime->value(), realtime->value_size());
            
            if( thisdelta < std::chrono::seconds(10) )
              m_meas->set_n42_2006_count_dose_data_info( dose_data );
          }
        }//for( loop over CountDoseData nodes, dos )
      }//if( m_dose_data_parent )
        
        
      //HPRDS (see refF4TD3P2VTG) have start time and remark as siblings to
      //  m_spec_node
      if( spec_parent )
      {
        for( const rapidxml::xml_node<char> *remark = xml_first_node_nso( spec_parent, "Remark", xmlns );
              remark;
              remark = XML_NEXT_TWIN(remark) )
        {
          string remarkstr = xml_value_str( remark );
          trim( remarkstr );
            
          if( remarkstr.empty() )
            continue;
            
          if( SpecUtils::istarts_with( remarkstr, s_parser_warn_prefix) )
          {
            SpecUtils::ireplace_all( remarkstr, s_parser_warn_prefix, "" );
            m_meas->parse_warnings_.emplace_back( std::move(remarkstr) );
          }else
          {
            m_meas->remarks_.push_back( remarkstr );
          }
        }//for( loop over spectrum spec_parent remarks )
            
        const rapidxml::xml_node<char> *start_time = xml_first_node_nso( spec_parent, "StartTime", xmlns );
        if( start_time && start_time->value_size() && is_special(m_meas->start_time_)
            && m_meas->source_type_ != SourceType::IntrinsicActivity )
          m_meas->start_time_ = time_from_string( xml_value_str(start_time).c_str() );
      }//if( spec_parent )
    }
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
    catch( std::exception &e )
    {
      m_meas->reset();
      m_meas->title_ = N42DecodeHelper2006_failed_decode_title;
      if( !SpecUtils::icontains( e.what(), "didnt find <ChannelData>" )
         && !SpecUtils::icontains( e.what(), "Spectrum marked as missing, and all zeros" ) )
      {
        char buffer[256];
        snprintf( buffer, sizeof(buffer), "Caught: %s", e.what() );
        log_developer_error( __func__, buffer );
      }//if( !SpecUtils::icontains( e.what(), "didnt find <ChannelData>" ) )
    }//try / catch
#else
    catch( std::exception & )
    {
      m_meas->reset();
      m_meas->title_ = N42DecodeHelper2006_failed_decode_title;
    }
#endif
  }//void operator()()
      
  //set_n42_2006_detector_data_node_info(): silently returns if information isnt found
  static void set_n42_2006_detector_data_node_info( const rapidxml::xml_node<char> *det_data_node,
                                                         std::vector<std::shared_ptr<Measurement>> &measurs_to_update )
  {
    const string xmlns = get_n42_xmlns(det_data_node);
    
    const rapidxml::xml_node<char> *speed_node = xml_first_node_nso( det_data_node, "Speed", xmlns );
    const rapidxml::xml_node<char> *occupancy_node = xml_first_node_nso( det_data_node, "Occupied", xmlns );
    const rapidxml::xml_node<char> *start_time_node = xml_first_node_nso( det_data_node, "StartTime", xmlns );
    const rapidxml::xml_node<char> *sample_real_time_node = xml_first_node_nso( det_data_node, "SampleRealTime", xmlns );
    
    float real_time = 0.0, speed = numeric_limits<float>::quiet_NaN();
    time_point_t start_time{};
    OccupancyStatus occupied = OccupancyStatus::Unknown;
    
    if( sample_real_time_node && sample_real_time_node->value_size() )
      real_time = time_duration_string_to_seconds( sample_real_time_node->value(), sample_real_time_node->value_size() );
    
    if( start_time_node )
      start_time = time_from_string( xml_value_str(start_time_node).c_str() );
    
    shared_ptr<LocationState> speed_loc;
    try{ speed = speed_from_node( speed_node ); }catch(...){}
    
    occupied = parse_occupancy_status( occupancy_node );
    
    for( auto &meas : measurs_to_update )
    {
      if( meas->occupied_ == OccupancyStatus::Unknown )
        meas->occupied_ = occupied;
      
      if( !IsNan(speed) )
      {
        if( meas->location_ )
        {
          if( IsNan(meas->location_->speed_) )
          {
            auto loc = make_shared<LocationState>(*meas->location_);
            loc->speed_ = speed;
            
            // TODO: we could check the other `measurs_to_update` to see if they have the same `location_`, and if so, also set them equal to `loc`
            meas->location_ = loc;
          }
        }else
        {
          if( !speed_loc )
          {
            speed_loc = make_shared<LocationState>();
            
            // I think we are here primarily if we are a portal, so it makes sense to assign the
            //  speed as `item`.
            speed_loc->type_ = LocationState::StateType::Item;
          }
          speed_loc->speed_ = speed;
          meas->location_ = speed_loc;
        }
      }//if( !IsNan(speed) )
      
      //dont set the start time for identiFINDER IntrinsicActivity spectrum (which
      //  doesnt have the time in the file), based on the StartTime node under
      //  <DetectorData> (of which, this time disagrees with the actual spectrum
      //  StartTime, so I dont know what to do about this anyway)
      if( is_special(meas->start_time_)
         && (meas->source_type_ != SourceType::IntrinsicActivity) )
        meas->start_time_ = start_time;
      
      if( meas->real_time_ < 0.000001f )
        meas->real_time_ = real_time;
      
      //For neutron only detectors, a lot of times live time isnt given, so fill
      //  this out
      if( meas->contained_neutron_ && meas->live_time_ < 0.000001f
         && (!meas->gamma_counts_ || meas->gamma_counts_->empty()) )
        meas->live_time_ = meas->real_time_;
    }//foeach( std::shared_ptr<Measurement> &meas, measurements_this_node )
  }//void set_n42_2006_detector_data_node_info(  )
  

};//struct N42DecodeHelper2006
  
  
  
  
  struct GrossCountNodeDecodeWorker
  {
    const rapidxml::xml_node<char> *m_node;
    Measurement *m_meas;
    
    GrossCountNodeDecodeWorker( const rapidxml::xml_node<char> *node,
                               Measurement *newmeas )
    : m_node( node ),
    m_meas( newmeas )
    {}
    
    void operator()()
    {
      try
      {
        m_meas->set_n42_2006_gross_count_node_info( m_node );
      }catch( std::exception & )
      {
        //      cerr << SRC_LOCATION << "\n\tCaught: " << e.what() << endl;
        m_meas->reset();
      }
    }//void operator()()
  };//struct GrossCountNodeDecodeWorker
  
}//namespace


//Implement N42DecodeHelper2012
namespace SpecUtils
{
//Some typedefs and enums used for decode_2012_N42_rad_measurement_node(...)
enum DetectionType{ GammaDetection, NeutronDetection, GammaAndNeutronDetection, OtherDetection };
typedef std::map<std::string,std::pair<DetectionType,std::string> > IdToDetectorType;
typedef std::map<std::string,MeasurementCalibInfo>                  DetectorToCalibInfo;

      
struct N42DecodeHelper2012
{
public:
      
  static std::string concat_2012_N42_characteristic_node( const rapidxml::xml_node<char> *char_node )
  {
    //      const rapidxml::xml_attribute<char> *char_id = char_node->first_attribute( "id", 2 );
    const rapidxml::xml_attribute<char> *date = char_node->first_attribute( "valueDateTime", 13 );
    const rapidxml::xml_attribute<char> *limits = char_node->first_attribute( "valueOutOfLimits", 16 );
    const rapidxml::xml_node<char> *remark_node = XML_FIRST_NODE(char_node, "Remark");
    const rapidxml::xml_node<char> *name_node = XML_FIRST_NODE(char_node, "CharacteristicName");
    const rapidxml::xml_node<char> *value_node = XML_FIRST_NODE(char_node, "CharacteristicValue");
    const rapidxml::xml_node<char> *unit_node = XML_FIRST_NODE(char_node, "CharacteristicValueUnits");
    //  const rapidxml::xml_node<char> *class_node = XML_FIRST_NODE(char_node, "CharacteristicValueDataClassCode");
        
    string comment;
    if( name_node && name_node->value_size() )
      comment = xml_value_str( name_node );
        
    if( (date && date->value_size()) || (limits && limits->value_size())
         || (remark_node && remark_node->value_size()) )
    {
      comment += "(";
      comment += xml_value_str( date );
      if( limits && limits->value_size() )
      {
        if( comment[comment.size()-1] != '(' )
          comment += ", ";
        comment += "value out of limits: ";
        comment += xml_value_str(limits);
      }//if( limits && limits->value_size() )
        
      if( remark_node && remark_node->value_size() )
      {
        if( comment[comment.size()-1] != '(' )
          comment += ", ";
        comment += "remark: ";
        comment += xml_value_str(remark_node);
      }//if( limits && limits->value_size() )
      comment += ")";
    }//if( an attribute has info )
        
    if( value_node )
      comment += string(":") + xml_value_str(value_node);
        
    if( unit_node && !XML_VALUE_ICOMPARE(unit_node, "unit-less") )
      comment += " " + xml_value_str(unit_node);
        
    return comment;
  }//std::string concat_2012_N42_characteristic_node( const rapidxml::xml_node<char> *node )
      
  
  static void set_deriv_data( const shared_ptr<Measurement> &meas, const string &dd_id, const string &spec_id )
  {
    typedef Measurement::DerivedDataProperties DerivedProps;
    
    const auto set_bit = [&meas]( const DerivedProps &p ){
      typedef std::underlying_type<DerivedProps>::type DerivedProps_t;
      meas->derived_data_properties_ |= static_cast<DerivedProps_t>(p);
    };
    
    set_bit( DerivedProps::IsDerived );
    
    
    if( icontains( dd_id, "MeasureSum" )
        || icontains( spec_id, "SumGamma" )
        || icontains( dd_id, "SumData" )
        || icontains( spec_id, "SumSpectrum" ) )
    {
      set_bit( DerivedProps::ItemOfInterestSum );
    }
       
    if( meas->source_type_ == SourceType::Unknown )
    {
      if( icontains(spec_id, "BGGamma") || icontains(spec_id, "Background") )
        meas->source_type_ = SourceType::Background;
      else if( icontains(spec_id, "Foreground") || icontains(spec_id, "Foreground") )
        meas->source_type_ = SourceType::Foreground;
    }//if( meas->source_type_ == SourceType::Unknown )
    
    
    if( icontains( dd_id, "Analysis" ) || icontains( spec_id, "Analysis" ) )
      set_bit( DerivedProps::UsedForAnalysis );
    
    if( !(icontains( dd_id, "raw" ) || icontains( spec_id, "raw" ))
       && (icontains( dd_id, "Processed" ) || icontains( spec_id, "Processed" )) )
      set_bit( DerivedProps::ProcessedFurther );
    
    if( icontains( dd_id, "BGSub" ) || icontains( spec_id, "BGSub" ) )
      set_bit( DerivedProps::BackgroundSubtracted );
    
    if( (icontains( dd_id, "background" ) || icontains( spec_id, "background" )
         || icontains( dd_id, "BGGamma" ) || icontains( spec_id, "BGGamma" ))
        && !(icontains( dd_id, "sub" ) || icontains( spec_id, "sub" )) )
      set_bit( DerivedProps::IsBackground );
    
    // The de facto convention seems to be to put the description of the sample/measurement
    //  into the "id" tags of the <DerivedData> and <Spectrum> elements, so we'll add this
    //  to the #Measurement title.
    //  But we'll only do this if the title doesnt already have the ID values, and the title doesnt
    //   have any of the strings we write to attribute values of <DerivedData> (e.g., "-MeasureSum",
    //   "-Analysis", "-Processed", "-BGSub" - see #SpecFile::create_2012_N42_xml and also
    //   #add_spectra_to_measurement_node_in_2012_N42_xml ).
    if( !icontains(meas->title_, dd_id)
       && !icontains(meas->title_, spec_id)
       && !icontains(meas->title_, "Derived Spectrum:")
       && !icontains(meas->title_, "MeasureSum")
       && !contains(meas->title_, "MeasureSum")
       && !icontains(meas->title_, "Analysis")
       && !icontains(meas->title_, "Processed")
       && !icontains(meas->title_, "BGSub")
       && !icontains(meas->title_, "BackgroundMeasure")
       && !icontains(meas->title_, "Gamma StabMeasurement")
       && !icontains(meas->title_, "Gamma Foreground Sum")
       && !icontains(meas->title_, "Gamma Cal")
       )
    {
      meas->title_ += (meas->title_.empty() ? "" : " ") + string("Derived Spectrum: ") + dd_id + " " + spec_id;
    }
  }//set_deriv_data(..)
  

  //decode_2012_N42_rad_measurement_node: a function to help decode 2012 N42
  //  RadMeasurement nodes in a mutlithreaded fashion. This helper function
  //  has to be a member function in order to access the member variables.
  //  I would preffer you didnt awknowledge the existence of this function.
  //  id_to_dettypeany_ptr and calibrations_ptr must be valid.
  static void decode_2012_N42_rad_measurement_node(
                                    std::vector< std::shared_ptr<Measurement> > &measurements,
                                    const rapidxml::xml_node<char> *meas_node,
                                    const size_t rad_measurement_index,
                                    const IdToDetectorType *id_to_dettype_ptr,
                                    DetectorToCalibInfo *calibrations_ptr,
                                    std::mutex &meas_mutex,
                                    std::mutex &calib_mutex )
  {
    assert( meas_node );
    assert( id_to_dettype_ptr );
    assert( calibrations_ptr );
    assert( XML_NAME_ICOMPARE(meas_node, "RadMeasurement")
            || XML_NAME_ICOMPARE(meas_node, "DerivedData") );
    
    try
    {
      //We will copy <remarks> and parse warnings from meas_node to each SpecUtils::Measurement that we will create from it
      vector<string> remarks, meas_parse_warnings;
      
      // To fill out the LocationState information, we will look for <RadInstrumentState> first,
      //   then <RadDetectorState>, then <RadItemState>
      shared_ptr<LocationState> inst_or_item_location;
      map<string,shared_ptr<LocationState>> det_location;
      
      float real_time = 0.0;
      time_point_t start_time{};
      SourceType spectra_type = SourceType::Unknown;
      OccupancyStatus occupied = OccupancyStatus::Unknown;
        
      // The <DerivedData> node is close enough <RadMeasurement> we will parse them both with the
      //  same code.
      const bool derived_data = XML_NAME_ICOMPARE(meas_node, "DerivedData");
      
      const auto meas_att = SpecUtils::xml_first_iattribute(meas_node, "id");
      //    rapidxml::xml_attribute<char> *info_att = meas_node->first_attribute( "radItemInformationReferences", 28 );
      //    rapidxml::xml_attribute<char> *group_att = meas_node->first_attribute( "radMeasurementGroupReferences", 29 );
      
      //Try to grab sample number from the 'id' attribute of <RadMeasurement>
      int sample_num_from_meas_node = -999;
      const string meas_id_att_str = xml_value_str( meas_att );
      if( meas_id_att_str.size() )
      {
        if( SpecUtils::icontains(meas_id_att_str,"background")
           && !SpecUtils::icontains(meas_id_att_str,"Survey")
           && !SpecUtils::icontains(meas_id_att_str,"Sample") )
        {
          sample_num_from_meas_node = 0;
        }else if( sscanf( meas_id_att_str.c_str(), "Sample%i", &(sample_num_from_meas_node)) == 1 )
        {
        }else if( sscanf( meas_id_att_str.c_str(), "Survey %i", &(sample_num_from_meas_node)) == 1 )
        {
        }else if( sscanf( meas_id_att_str.c_str(), "Survey_%i", &(sample_num_from_meas_node)) == 1 )
        {
        }else if( sscanf( meas_id_att_str.c_str(), "Survey%i", &(sample_num_from_meas_node)) == 1 )
        {
        //}else if( sscanf( meas_id_att_str.c_str(), "Foreground%i", &(sample_num_from_meas_node)) == 1 )
        //{
        }
        
        //else ... another format I dont recall seeing.
      }//if( samp_det_str.size() )
      
      XML_FOREACH_CHILD( remark_node, meas_node, "Remark" )
      {
        string remark = SpecUtils::trim_copy( xml_value_str(remark_node) );
        
        const bool parse_warning = SpecUtils::starts_with( remark, s_parser_warn_prefix );
        if( parse_warning )
        {
          SpecUtils::ireplace_all( remark, s_parser_warn_prefix, "" );
          meas_parse_warnings.emplace_back( std::move(remark) );
        }else if( remark.size() )
        {
          remarks.emplace_back( std::move(remark) );
        }
      }//for( loop over remarks _
      
      const rapidxml::xml_node<char> *class_code_node = XML_FIRST_NODE( meas_node, "MeasurementClassCode" );
      if( class_code_node && class_code_node->value_size() )
      {
        if( XML_VALUE_ICOMPARE(class_code_node, "Foreground") )
          spectra_type = SourceType::Foreground;
        else if( XML_VALUE_ICOMPARE(class_code_node, "Background") )
          spectra_type = SourceType::Background;
        else if( XML_VALUE_ICOMPARE(class_code_node, "Calibration") )
          spectra_type = SourceType::Calibration;
        else if( XML_VALUE_ICOMPARE(class_code_node, "IntrinsicActivity") )
          spectra_type = SourceType::IntrinsicActivity;
        else if( XML_VALUE_ICOMPARE(class_code_node, "NotSpecified") )
          spectra_type = SourceType::Unknown;
      }//if( class_code_node && class_code_node->value_size() )
      
      //Special check for RadSeeker.
      if( spectra_type == SourceType::Unknown
         && meas_att && XML_VALUE_ICOMPARE(meas_att, "Stabilization") )
        spectra_type = SourceType::IntrinsicActivity;
      
      
      const rapidxml::xml_node<char> *time_node = XML_FIRST_NODE( meas_node, "StartDateTime" );
      if( time_node && time_node->value_size() )
      {
        //ToDo: figure which endian-ess is best.  I've seen at least one N42 file
        //  that was little endian (ex "15-05-2017T18:51:50"), but by default
        //  #time_from_string tries middle-endian first.
        start_time = time_from_string( xml_value_str(time_node).c_str() );
        //start_time = SpecUtils::time_from_string( xml_value_str(time_node).c_str(), SpecUtils::LittleEndianFirst );
      }
      
      const rapidxml::xml_node<char> *real_time_node = XML_FIRST_NODE( meas_node, "RealTimeDuration" );
      if( !real_time_node )
        real_time_node = XML_FIRST_NODE(meas_node, "RealTime");
      if( real_time_node && real_time_node->value_size() )
        real_time = time_duration_string_to_seconds( real_time_node->value(), real_time_node->value_size() );
      
      const rapidxml::xml_node<char> *occupancy_node = XML_FIRST_NODE( meas_node, "OccupancyIndicator" );
      if( occupancy_node && occupancy_node->value_size() )
      {
        if( XML_VALUE_ICOMPARE(occupancy_node, "true") || XML_VALUE_ICOMPARE(occupancy_node, "1") )
          occupied = OccupancyStatus::Occupied;
        else if( XML_VALUE_ICOMPARE(occupancy_node, "false") || XML_VALUE_ICOMPARE(occupancy_node, "0") )
          occupied =  OccupancyStatus::NotOccupied;
      }//if( occupancy_node && occupancy_node->value_size() )
      
      
      //{<RadInstrumentState> or <RadItemState>} --> inst_or_item_location
      //<RadDetectorState> --> det_location
      
      // Returns nullptr if not successful
      //  TODO: move this to an independent function
      auto parse_state_node = []( const rapidxml::xml_node<char> *node ) -> shared_ptr<LocationState> {
        try
        {
          auto answer = make_shared<LocationState>();
          answer->from_n42_2012( node );
          return answer;
        }catch(std::exception &e )
        {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          char buffer[512];
          snprintf( buffer, sizeof(buffer), "Failed to parse <StateVector>: '%s'.", e.what() );
          log_developer_error( __func__, buffer );
          // assert( !node || XML_FIRST_INODE(node, "Remark") ); // The remark will say something like "InterSpec could not determine detector state"
#endif
        }
      
        return nullptr;
      };//parse_state_node
      
      
      XML_FOREACH_CHILD( inst_state_node, meas_node, "RadInstrumentState" )
      {
        if( inst_or_item_location )
          break;
        inst_or_item_location = parse_state_node( inst_state_node );
      }//for( loop over <RadInstrumentState> )
      
      XML_FOREACH_CHILD( item_state_node, meas_node, "RadItemState" )
      {
        if( inst_or_item_location )
          break;
        inst_or_item_location = parse_state_node( item_state_node );
      }//for( loop over <RadItemState> )
      
      XML_FOREACH_CHILD( det_state_node, meas_node, "RadDetectorState" )
      {
        if( inst_or_item_location )
          break;
        auto info = parse_state_node( det_state_node );
        if( !info )
          continue;
        auto det_attrib = XML_FIRST_IATTRIB(det_state_node, "radDetectorInformationReference");
        string det_info_ref = xml_value_str( det_attrib );
        if( det_info_ref == s_unnamed_det_placeholder )
          det_info_ref.clear();
        
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
        if( det_location.count(det_info_ref) )
        {
          char buffer[512];
          snprintf( buffer, sizeof(buffer),
                  "Found multiple <RadDetectorState> elements for detector '%s'.",
                   det_info_ref.c_str() );
          log_developer_error( __func__, buffer );
        }//if( det_location.count(det_info_ref) )
#endif
        
        det_location[det_info_ref] = info;
      }//for( loop over <RadDetectorState> )
      
      
      vector<shared_ptr<Measurement>> spectrum_meas, neutron_meas;
      
      //Lets track the Measurement to calibration id value incase multiple spectra
      //  are given for the same detector and <RadMeasurement>, but with different
      //  energy binning.
      vector< pair<std::shared_ptr<Measurement>,string> > meas_to_cal_id;
      
        XML_FOREACH_CHILD( spectrum_node, meas_node, "Spectrum" )
        {
          using rapidxml::internal::compare;
          rapidxml::xml_attribute<char> *id_att = XML_FIRST_IATTRIB( spectrum_node, "id" );
          rapidxml::xml_attribute<char> *det_info_att = XML_FIRST_IATTRIB( spectrum_node, "radDetectorInformationReference" );  //case in-sensitive for refAO7WGOXDJ4
          rapidxml::xml_attribute<char> *calib_att = XML_FIRST_IATTRIB(spectrum_node, "energyCalibrationReference");
          //  rapidxml::xml_attribute<char> *_att = XML_FIRST_ATTRIB( spectrum_node, "fullEnergyPeakEfficiencyCalibrationReference" );
          //  rapidxml::xml_attribute<char> *_att = XML_FIRST_ATTRIB( spectrum_node, "FWHMCalibrationReference" );
          //  rapidxml::xml_attribute<char> *_att = XML_FIRST_ATTRIB( spectrum_node, "intrinsicDoubleEscapePeakEfficiencyCalibrationReference" );
          //  rapidxml::xml_attribute<char> *_att = XML_FIRST_ATTRIB( spectrum_node, "intrinsicFullEnergyPeakEfficiencyCalibrationReference" );
          //  rapidxml::xml_attribute<char> *_att = XML_FIRST_ATTRIB( spectrum_node, "intrinsicSingleEscapePeakEfficiencyCalibrationReference" );
          //  rapidxml::xml_attribute<char> *_att = XML_FIRST_ATTRIB( spectrum_node, "radRawSpectrumReferences" );
          //  rapidxml::xml_attribute<char> *_att = XML_FIRST_ATTRIB( spectrum_node, "totalEfficiencyCalibrationReference" );
          
          auto meas = std::make_shared<Measurement>();
          DetectionType det_type = GammaDetection;
          
          //Get the detector name from the XML det_info_att if we have it, otherwise
          //  if there is only one detector description in the file, we will assume
          //  this spetrum is from that.
          if( det_info_att && det_info_att->value_size() )
          {
            meas->detector_name_ = xml_value_str( det_info_att );
          }else if( id_to_dettype_ptr->size()==1 )
          {
            meas->detector_name_ = id_to_dettype_ptr->begin()->first;
          }else if( meas->detector_name_.empty() && (id_to_dettype_ptr->size() > 1) )
          {
            //We will look to see if there is only one gamma, and if so, use it.
            for( const auto &info : *id_to_dettype_ptr ) //std::map<std::string,std::pair<DetectionType,std::string> >
            {
              if( !icontains(info.first, "neut" ) && (info.second.first != DetectionType::NeutronDetection) )
              {
                if( meas->detector_name_.empty() )
                {
                  meas->detector_name_ = info.first;
                }else
                {
                  meas->detector_name_ = "";
                  break;
                }
              }//if( !icontains(info.first, "neut" ) )
            }//for( loop over detector types )
          }//
          
          if( meas->detector_name_ == s_unnamed_det_placeholder )
            meas->detector_name_.clear();
          
          
          auto det_iter = id_to_dettype_ptr->find( meas->detector_name_ );
          if( det_iter != end(*id_to_dettype_ptr) )
          {
            det_type = det_iter->second.first;
            meas->detector_description_ = det_iter->second.second; //e.x. "HPGe 50%"
          }//if( det_iter != id_to_dettype_ptr->end() )
          
          const rapidxml::xml_node<char> *live_time_node = XML_FIRST_NODE(spectrum_node, "LiveTimeDuration");
          if( !live_time_node )
            live_time_node = XML_FIRST_NODE(spectrum_node, "LiveTime");
          
          //Some detectors mistakenly put the <LiveTimeDuration> tag under the
          //  <RadMeasurement> tag
          if( !live_time_node && spectrum_node->parent() )
          {
            live_time_node = XML_FIRST_NODE(spectrum_node->parent(), "LiveTimeDuration");
            if( !live_time_node )
              live_time_node = XML_FIRST_NODE(spectrum_node->parent(), "LiveTime");
          }
          
          
          const rapidxml::xml_node<char> *channel_data_node = XML_FIRST_NODE(spectrum_node, "ChannelData");
          
          for( size_t i = 0; i < remarks.size(); ++i )
            meas->remarks_.push_back( remarks[i] );
          
          bool use_remark_real_time = false;
          
          XML_FOREACH_CHILD( remark_node, spectrum_node, "Remark" )
          {
            string remark = xml_value_str( remark_node );
            trim( remark );
            if( remark.empty() )
              continue;
            
            if( SpecUtils::istarts_with( remark, s_parser_warn_prefix ) )
            {
              SpecUtils::ireplace_all( remark, s_parser_warn_prefix, "" );
              meas->parse_warnings_.emplace_back( std::move(remark) );
            }else if( SpecUtils::istarts_with( remark, "RealTime:") )
            {
              //Starting with SpecFile_2012N42_VERSION==3, a slightly more
              //  accurate RealTime may be recorded in the remark if necassary...
              //  see notes in create_2012_N42_xml() and add_spectra_to_measurement_node_in_2012_N42_xml()
              //snprintf( thisrealtime, sizeof(thisrealtime), "RealTime: PT%fS", realtime_used );
              remark = SpecUtils::trim_copy( remark.substr(9) );
              meas->real_time_ = time_duration_string_to_seconds( remark );
              
              use_remark_real_time = (meas->real_time_ > 0.0);
            }else if( SpecUtils::istarts_with( remark, "Title:") )
            {
              //Starting with SpecFile_2012N42_VERSION==3, title is encoded as a remark prepended with 'Title: '
              remark = SpecUtils::trim_copy( remark.substr(6) );
              meas->title_ += remark;

              /*
              // Try to get speed from remark, this will get overwritten later if there is a
              //  proper N42 node with this information.
              auto make_location_state = [&](){
                if( inst_or_item_location )
                  return;
                inst_or_item_location = make_shared<LocationState>();
                inst_or_item_location->type_ = LocationState::StateType::Item;
              };//make_location_state
              
              try
              {
                const float thisspeed = speed_from_remark(remark);
                make_location_state();
                inst_or_item_location->speed_ = thisspeed;
              }catch( std::exception & )
              {
              }

              float dx = std::numeric_limits<float>::quiet_NaN();
              float dy = dx, dz = dx;
              
              try{ dx = dx_from_remark(remark); }catch( std::exception & ){ }
              try{ dy = dy_from_remark(remark); }catch( std::exception & ){ }
              try{ dz = dz_from_remark(remark); }catch( std::exception & ){ }
              
              if( !IsNan(dx) || !IsNan(dy) || !IsNan(dz) )
              {
                make_location_state();
                auto rel_loc = make_shared<RelativeLocation>();
                inst_or_item_location->relative_location_ = rel_loc;
                rel_loc->from_cartesian( dx, dy, dz );
              }
              */
            }else if( remark.size() )
            {
              meas->remarks_.emplace_back( std::move(remark) );
            }
          }//for( loop over remarks )
          
          
          //This next line is specific to file written by InterSpec
          //const string samp_det_str = xml_value_str( meas_att ); //This was used pre 20180225, however I believe this was wrong due to it probably not containing DetXXX - we'll see.
          const auto spec_id = SpecUtils::xml_first_iattribute( spectrum_node, "id" );
          const string samp_det_str = SpecUtils::xml_value_str(spec_id);
          if( samp_det_str.size() )
          {
            if( SpecUtils::istarts_with(samp_det_str, "background") )
            {
              meas->sample_number_ = 0;
            }else if( sscanf( samp_det_str.c_str(), "Sample%i", &(meas->sample_number_)) == 1 )
            {
            }else if( sscanf( samp_det_str.c_str(), "Survey %i", &(meas->sample_number_)) == 1 )
            {
            }else if( sscanf( samp_det_str.c_str(), "Survey_%i", &(meas->sample_number_)) == 1 )
            {
            }else if( sscanf( samp_det_str.c_str(), "Survey%i", &(meas->sample_number_)) == 1 )
            {
            //}else if( sscanf( samp_det_str.c_str(), "Foreground%i", &(meas->sample_number_)) == 1 )
            //{
            }else if( sample_num_from_meas_node != -999 )
            {
              meas->sample_number_ = sample_num_from_meas_node;
            }else
            {
              meas->sample_number_ = static_cast<int>( rad_measurement_index );
            }
          }else if( sample_num_from_meas_node != -999 )
          {
            meas->sample_number_ = sample_num_from_meas_node;
          }else
          {
            meas->sample_number_ = static_cast<int>( rad_measurement_index );
          }//if( samp_det_str.size() )
          
  #if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
          if( (sample_num_from_meas_node != -999) && (meas->sample_number_ != sample_num_from_meas_node) )
          {
            char buffer[512];
            snprintf( buffer, sizeof(buffer),
                     "Found a case where RadMeasurement id ('%s') gave a different"
                     " sample number than Spectrum id ('%s').",
                     meas_id_att_str.c_str(), samp_det_str.c_str() );
            log_developer_error( __func__, buffer );
          }
  #endif
          
          if( !use_remark_real_time )
            meas->real_time_ = real_time;
          
          
          // Symetrica detectors sometimes have a <sym:RealTimeDuration>, node under the <Spectrum>
          //  node; it almost always agrees with the parent <RealTimeDuration> node, but notably
          //  for derived data, it _may_ be either a little, or a lot different from the parent
          //  <RealTimeDuration> node, and it looks like we should use <sym:RealTimeDuration> in
          //  this case.
          const rapidxml::xml_node<char> *sym_rt_node = XML_FIRST_NODE(spectrum_node, "sym:RealTimeDuration");
          if( sym_rt_node )
          {
            const float sym_rt = time_duration_string_to_seconds( sym_rt_node->value(),
                                                                    sym_rt_node->value_size() );
            if( sym_rt > 0.0f )
              meas->real_time_ = sym_rt;
          }//if( symetrica real time node is under the spectrum node )
          
          //RealTime shouldnt be under Spectrum node (should be under RadMeasurement)
          //  but some files mess this up, so check for real time under the spectrum
          //  node if we dont have the real time yet
          if(  (meas->real_time_ <= 0.0f) || (meas->real_time_ > meas->live_time_) )
          {
            const rapidxml::xml_node<char> *real_time_node = XML_FIRST_NODE(spectrum_node, "RealTimeDuration");
            
            if( !real_time_node )
              real_time_node = XML_FIRST_NODE(spectrum_node, "RealTime");
            if( real_time_node )
              meas->real_time_ = time_duration_string_to_seconds( real_time_node->value(), real_time_node->value_size() );
          }
          
          
          meas->start_time_ = start_time;
          meas->source_type_ = spectra_type;
          
          //For the sake of file_format_test_spectra/n42_2006/identiFINDER/20130228_184247Preliminary2010.n42
          if( meas->source_type_ == SourceType::Unknown
             && SpecUtils::iequals_ascii(meas->detector_name_, "intrinsicActivity")  )
            meas->source_type_ = SourceType::IntrinsicActivity;
          
          meas->occupied_ = occupied;
          
          if( live_time_node && live_time_node->value_size() )
            meas->live_time_ = time_duration_string_to_seconds( live_time_node->value(), live_time_node->value_size() );
          
          auto gamma_counts = std::make_shared<vector<float>>();
          
          if( channel_data_node && channel_data_node->value_size() )
          {
            const char *char_data = channel_data_node->value();
            const size_t char_data_len = channel_data_node->value_size();
            SpecUtils::split_to_floats( char_data, char_data_len, *gamma_counts );
            
            rapidxml::xml_attribute<char> *comp_att = XML_FIRST_IATTRIB(channel_data_node, "compressionCode");
            if( !comp_att )
              comp_att = XML_FIRST_IATTRIB(channel_data_node, "Compression");
            
            if( comp_att && icontains( xml_value_str(comp_att), "Counted") )  //"CountedZeroes" or at least one file has "CountedZeros"
              expand_counted_zeros( *gamma_counts, *gamma_counts );
          }//if( channel_data_node && channel_data_node->value() )
          
          //Well swapp meas->gamma_count_sum_ and meas->neutron_counts_sum_ in a bit if we need to
          meas->gamma_count_sum_ = 0.0;
          for( const float a : *gamma_counts )
            meas->gamma_count_sum_ += a;
          
          if( inst_or_item_location )
            meas->location_ = inst_or_item_location;
          else if( det_location.count(meas->detector_name_) )
            meas->location_ = det_location[meas->detector_name_];
          
          if( det_type == OtherDetection )
            continue;
          
          bool is_gamma = (det_type == GammaDetection);
          bool is_neutron = ((det_type == NeutronDetection) || (det_type == GammaAndNeutronDetection));
          if( det_type == GammaAndNeutronDetection )
          {
            const string id_att_val = xml_value_str( id_att );
            
            is_gamma = !(icontains(id_att_val,"Neutron") || icontains(id_att_val,"Ntr"));
            
            //If no calibration info is given, then assume it is not a gamma measurement
            if( !calib_att || !calib_att->value_size() )
              is_gamma = false;
          }//if( det_type == GammaAndNeutronDetection )
          
          //Sometimes a gamma and neutron detector will have only neutron counts
          
          if( is_gamma && gamma_counts && !gamma_counts->empty() )
          {
            meas->gamma_counts_ = gamma_counts;
            
            if( !calib_att || !calib_att->value_size() )
            {
  #if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
              log_developer_error( __func__, "Found a gamma spectrum without calibration information" );
  #endif
              //continue;
            }//if( !calib_att || !calib_att->value_size() )
            
            
            const string calib_id = xml_value_str(calib_att);
            
            std::lock_guard<std::mutex> lock( calib_mutex );
            
            map<string,MeasurementCalibInfo>::iterator calib_iter = calibrations_ptr->find( calib_id );
            
            //If there is only one energy calibration, use it, even if names dont
            //  match up.  IF more than one calibration then use default
            if( calib_iter == end(*calibrations_ptr) )
            {
              if( calibrations_ptr->size() == 1 )
              {
                calib_iter = calibrations_ptr->begin();
              }else
              {
                const string defCalName = "DidntHaveCalSoUsingDefCal_" + std::to_string( meas->gamma_counts_->size() );
                calib_iter = calibrations_ptr->find( defCalName );
                if( calib_iter == end(*calibrations_ptr) )
                {
                  DetectorToCalibInfo::value_type info( defCalName, MeasurementCalibInfo() );
                  info.second.equation_type = SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial;
                  
                  size_t nbin = meas->gamma_counts_->size();
                  if( nbin < 2 )
                    nbin = 2;
                  info.second.coefficients.push_back( 0.0f );
                  info.second.coefficients.push_back( 3000.0f / (nbin-1) );
                  //info.second.calib_id = defCalName;  //Leave commented out so wont get put into meas_to_cal_id
                  calib_iter = calibrations_ptr->insert( std::move(info) ).first;
                }//if( we havent yet created a default calibration )
              }//if( we have a single calibration we can use ) / else
            }//if( no calibration present already )
            
            assert( calib_iter != calibrations_ptr->end() );
            
            MeasurementCalibInfo &calib = calib_iter->second;
            
            const size_t nbin = meas->gamma_counts_->size();
            calib.fill_binning( nbin );
            
            auto calptrpos = calib.energy_cals.find(nbin);
            assert( calptrpos != end(calib.energy_cals) );
            assert( calptrpos->second );
            
            meas->energy_calibration_ = calptrpos->second;
            if( !calib.energy_cal_error.empty() )
              meas->parse_warnings_.push_back( calib.energy_cal_error );
        
            if( calib.calib_id.size() )
              meas_to_cal_id.push_back( make_pair(meas,calib.calib_id) );
            
            meas->contained_neutron_ = false;
          }else if( is_neutron && meas->gamma_counts_ && meas->gamma_counts_->size() < 6 && meas->gamma_counts_->size() > 0 )
          {
            meas->neutron_counts_sum_ = meas->gamma_count_sum_;
            meas->gamma_count_sum_ = 0.0;
            if( meas->gamma_counts_ && meas->gamma_counts_->size() )
              meas->gamma_counts_ = std::make_shared<vector<float>>();
            meas->contained_neutron_ = true;
            //        if( gamma_counts )
            //          meas->neutron_counts_.swap( *gamma_counts );
          }else
          {
            continue;
          }//if( is_gamma ) / else if ( neutron ) / else
          //      const rapidxml::xml_node<char> *extension_node = XML_FIRST_NODE(meas_node, "SpectrumExtension");
          
          decode_2012_N42_detector_quality( meas, meas_node );
          
          if( derived_data )
          {
            const string dd_id = xml_value_str( xml_first_iattribute(meas_node, "id") );
            const string spec_id = xml_value_str( xml_first_iattribute(spectrum_node, "id") );
            
            set_deriv_data( meas, dd_id, spec_id );
          }//if( derived_data )
          
          spectrum_meas.push_back( meas );
        }//for( loop over "Spectrum" nodes )
        
        //flir radHUNTER N42 files is the inspiration for this next loop that
        // checks if there is a min, max, and total neutron <GrossCounts> node
        //  for this <RadMeasurement> node.
        bool min_neut = false, max_neut = false, total_neut = false, has_other = false;
        
        XML_FOREACH_CHILD( node, meas_node, "GrossCounts" )
        {
          const rapidxml::xml_attribute<char> *att = node->first_attribute( "radDetectorInformationReference", 31, false );
          if( att )
          {
            const bool is_min = XML_VALUE_ICOMPARE(att, "minimumNeutrons");
            const bool is_max = XML_VALUE_ICOMPARE(att, "maximumNeutrons");
            const bool is_total = XML_VALUE_ICOMPARE(att, "totalNeutrons");
          
            min_neut = (min_neut || is_min);
            max_neut = (max_neut || is_max);
            total_neut = (total_neut || is_total);
            has_other = (has_other || (!is_min && !is_max && !is_total));
          }//if( att )
        }//for( loop over GrossCounts nodes )
        
        const bool has_min_max_total_neutron = ((min_neut && max_neut && total_neut) && !has_other);
        
        XML_FOREACH_CHILD( gross_counts_node, meas_node, "GrossCounts" )
        {
          const rapidxml::xml_node<char> *live_time_node = XML_FIRST_NODE(gross_counts_node, "LiveTimeDuration" );
          const rapidxml::xml_node<char> *count_data_node = XML_FIRST_NODE(gross_counts_node, "CountData" );
          const rapidxml::xml_attribute<char> *det_info_att = XML_FIRST_IATTRIB(gross_counts_node, "radDetectorInformationReference" );
          
          std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
          DetectionType det_type = GammaDetection;
          
          const string det_info_ref = xml_value_str( det_info_att );
          
          if( det_info_ref.empty() )
          {
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
            cerr << "Found GrossCounts node with no radDetectorInformationReference" << endl;
#endif
            continue;
          }//if( det_info_ref.empty() )
          
          
          if( has_min_max_total_neutron )
          {
            if( !SpecUtils::iequals_ascii(det_info_ref, "totalNeutrons") )
              continue;
          }
          
          meas->detector_name_ = det_info_ref;
          if( meas->detector_name_ == s_unnamed_det_placeholder )
            meas->detector_name_.clear();
          
          
          auto det_iter = id_to_dettype_ptr->find( meas->detector_name_ );
          if( det_iter == end(*id_to_dettype_ptr) )
          {
            if( icontains( meas->detector_name_, "neut" ) )
            {
              // BNC detectors may not include a RadDetectorInformation element for the neutron detector
              det_type = DetectionType::NeutronDetection;
            }else
            {
              //cerr << "No detector information for '" << meas->detector_name_
              //     << "' so skipping" << endl;
              continue;
            }
          }else
          {
          
            det_type = det_iter->second.first;
            meas->detector_description_ = det_iter->second.second; //e.x. "HPGe 50%"
          }//if( !id_to_dettype_ptr->count( meas->detector_name_ ) )
          
          
          if( icontains( det_info_ref, "Neutron" ) )
            det_type = NeutronDetection;
          
          if( (det_type != NeutronDetection) && (det_type != GammaAndNeutronDetection) )
          {
  #if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
            auto  det_iter = id_to_dettype_ptr->find( meas->detector_name_ );
            if( det_iter == id_to_dettype_ptr->end() )
            {
              stringstream msg;
              msg << "Found a non neutron GrossCount node (det info ref attrib='"
              << det_info_ref << "'); child nodes are: {";
              for( auto el = meas_node->first_node(); el; el = el->next_sibling() )
                msg << xml_name_str(el) << ", ";
              msg << "}. Skipping!!!";
              log_developer_error( __func__, msg.str().c_str() );
            }//if( det_iter == id_to_dettype_ptr->end() )
  #endif
            continue;
          }
          
          //This next line is specific to file written by InterSpec, and is used to make sure sample
          // numbers kinda-sorta persist across writing to a N42-2012 and reading back in.  It isnt
          // really necessary except for some poorly formed portal input in the N42-2006 format; it
          // is definitely a hack.
          //const string sample_det_att = xml_value_str( meas_att ); //See notes above about pre 20180225,
          const auto gross_id = SpecUtils::xml_first_iattribute( gross_counts_node, "id" );
          const string sample_det_att = SpecUtils::xml_value_str( gross_id );
          if( sample_det_att.size() )
          {
            if( SpecUtils::istarts_with(sample_det_att, "background") )
            {
              meas->sample_number_ = 0;
            }else if( sscanf( sample_det_att.c_str(), "Sample%i", &(meas->sample_number_) ) == 1 )
            {
            }else if( sscanf( sample_det_att.c_str(), "Survey%i", &(meas->sample_number_) ) == 1 )
            {
            }else if( sscanf( sample_det_att.c_str(), "Survey %i", &(meas->sample_number_) ) == 1 )
            {
            }else if( sscanf( sample_det_att.c_str(), "Survey_%i", &(meas->sample_number_) ) == 1 )
            {
            //}else if( sscanf( sample_det_att.c_str(), "Foreground%i", &(meas->sample_number_) ) == 1 )
            //{
            }else if( sample_num_from_meas_node != -999 )
            {
              //We'll end up here with taks like: 'M-13 Aa1N', 'M113N', 'Foreground-20100127130220-Aa1N',
              //  '4045bb4c-9c68-48fa-8121-48c5806f84d8-count', 'NeutronForeground58-CL1',
              //  'ForegroundMeasure6afc5c3010001NeutronCounts', etc
              meas->sample_number_ = sample_num_from_meas_node;
            }else
            {
              meas->sample_number_ = static_cast<int>( rad_measurement_index );
            }//if( samp_det_str.size() )
          }else if( sample_num_from_meas_node != -999 )
          {
            meas->sample_number_ = sample_num_from_meas_node;
          }else
          {
            meas->sample_number_ = static_cast<int>( rad_measurement_index );
          }//if( sample_det_att.size() ) / else
          
          bool use_remark_real_time = false;
          
          for( size_t i = 0; i < remarks.size(); ++i )
            meas->remarks_.push_back( remarks[i] );
          
          XML_FOREACH_CHILD( remark_node, gross_counts_node, "Remark" )
          {
            string remark = SpecUtils::trim_copy( xml_value_str(remark_node) );
            
            if( SpecUtils::istarts_with( remark, "RealTime:") )
            {
              //See notes in equivalent portion of code for the <Spectrum> tag
              remark = SpecUtils::trim_copy( remark.substr(9) );
              meas->real_time_ = time_duration_string_to_seconds( remark );
              use_remark_real_time = (meas->real_time_ > 0.0f);
            }else if( SpecUtils::istarts_with( remark, "Title:") )
            {
              //See notes in equivalent portion of code for the <Spectrum> tag
              meas->title_ += SpecUtils::trim_copy( remark.substr(6) );
            }else if( !remark.empty() )
            {
              meas->remarks_.push_back( remark );
            }
          }//for( loop over remark nodes )
          
          if( !use_remark_real_time )
            meas->real_time_ = real_time;
          meas->start_time_ = start_time;
          meas->source_type_ = spectra_type;
          meas->occupied_ = occupied;
          
          if( live_time_node && live_time_node->value_size() )
          {
            meas->live_time_ = time_duration_string_to_seconds( live_time_node->value(), live_time_node->value_size() );
            
            // For a neutron-only record, if neutron live time clearly differs from real time by
            //  more than 5% (chosen arbitrarily), then it is likely the neutron measurement time
            //  is not actually the same interval as the gammas, but the detector is reporting it
            //  like that anyway (at least a few systems do), so we will set the neutron
            //  live time equal to its real-time - since the "LiveTimeDuration" element is specific
            //  to the actual neutron measurement
            // TODO: This should be revisited once SpecFile::merge_neutron_meas_into_gamma_meas()
            if( (det_type == NeutronDetection)
               && !use_remark_real_time
               && (meas->real_time_ > 0.1f)
               && (meas->live_time_ > 0.1f)
               && (fabs(meas->live_time_ - meas->real_time_) > 0.05f*std::max(meas->live_time_, meas->real_time_)) )
            {
              meas->real_time_ = meas->live_time_;
            }
          }//if( live_time_node && live_time_node->value_size() )
          
          if( inst_or_item_location )
            meas->location_ = inst_or_item_location;
          else if( det_location.count(meas->detector_name_) )
            meas->location_ = det_location[meas->detector_name_];
          
          meas->contained_neutron_ = true;
          
          if( !count_data_node || !count_data_node->value_size() )
            count_data_node = XML_FIRST_NODE(gross_counts_node, "GrossCountData");
          
          if( !count_data_node || !count_data_node->value_size() )
          {
            //cerr << "Found a GrossCount node without a CountData node, skipping" << endl;
            continue;
          }
          
          if( icontains( count_data_node->value(), count_data_node->value_size(), "cps", 3 ) )
          {
            // SAM950 contain a value like "1.5 cps"
            float countrate = 0.0;
            if( !xml_value_to_flt(count_data_node, countrate) )
            {
              meas->parse_warnings_.push_back( "Could not interpret neutron counts: "
                                              + xml_value_str(count_data_node) );
            }else
            {
              meas->neutron_counts_.push_back( countrate * meas->real_time_ );
            }
          }else
          {
            SpecUtils::split_to_floats( count_data_node->value(),
                                       count_data_node->value_size(),
                                       meas->neutron_counts_ );
          }//if( data is in cps ) / else
          
          for( size_t i = 0; i < meas->neutron_counts_.size(); ++i )
            meas->neutron_counts_sum_ += meas->neutron_counts_[i];
          
          decode_2012_N42_detector_quality( meas, meas_node );
          
          
          if( derived_data )
          {
            const string dd_id = xml_value_str( xml_first_iattribute(meas_node, "id") );
            const string spec_id = xml_value_str( xml_first_iattribute(gross_counts_node, "id") );
            set_deriv_data( meas, dd_id, spec_id );
          }//if( derived_data )
          
          
          neutron_meas.push_back( meas );
        }//for( loop over GrossCounts Node )
        
        
        auto meas_for_det = [&]( const std::string &det_name ) -> shared_ptr<Measurement> {
          for( shared_ptr<Measurement> &meas : spectrum_meas )
          {
            if( meas->detector_name_ == det_name )
              return meas;
          }//for( shared_ptr<Measurement> &meas : spectrum_meas )
          
          for( shared_ptr<Measurement> &meas : neutron_meas )
          {
            if( meas->detector_name_ == det_name )
              return meas;
          }//for( shared_ptr<Measurement> &meas : neutron_meas )
          
          return nullptr;
        };//meas_for_det(...)
        
        
        XML_FOREACH_CHILD( dose_node, meas_node, "DoseRate" )
        {
          const rapidxml::xml_attribute<char> * const det_attrib = XML_FIRST_ATTRIB( dose_node, "radDetectorInformationReference" );
          const rapidxml::xml_node<char> * const value_node = XML_FIRST_NODE( dose_node, "DoseRateValue" );
          if( !det_attrib || !value_node || !det_attrib->value_size() || !value_node->value_size() )
            continue;
            
          // ORTEC Detective X's will include neutron flux as a DoseRate with units "N/S/cm2" - we
          // will ignore this value
          const rapidxml::xml_attribute<char> * const units_attrib = XML_FIRST_IATTRIB( value_node, "units" );
          if( units_attrib && units_attrib->value_size() )
          {
            if( XML_VALUE_ICOMPARE(units_attrib, "N/S/cm2") )
              continue;
            
            // TODO: even though N42-2012 says we can only have units of uSv/h, this may not always be obeyed - we should explicitly check the units if present.
          }//if( units_attrib && units_attrib->value_size() )
            
          string det_info_ref = xml_value_str( det_attrib );
          if( det_info_ref == s_unnamed_det_placeholder )
            det_info_ref.clear();
            
          shared_ptr<Measurement> meas = meas_for_det( det_info_ref );
            
          if( !meas && (spectrum_meas.size() == 1) && spectrum_meas[0] )
          {
            // In "identiFINDER 2 NG" files there is a "minimumDoserate", "averageDoserate", and "maximumDoserate"
            //  detector defined, which we'll map the average dose rate to #Measurement::dose_rate_
            //  and insert all three into remarks
            meas = spectrum_meas[0];
            
            if( (det_info_ref == "averageDoserate")
               || (det_info_ref == "minimumDoserate")
               || (det_info_ref == "maximumDoserate") )
            {
              meas->remarks_.push_back( det_info_ref + ": " + xml_value_str(value_node) + " uSv/h" );
              
              if( (spectrum_meas[0]->dose_rate_ >= 0) || (det_info_ref != "averageDoserate") )
                continue;
            }//if( det_info_ref is "minimumDoserate", "averageDoserate", or "maximumDoserate" )
          }//if( !meas && (spectrum_meas.size() == 1) && spectrum_meas[0] )
          
          if( !meas )
          {
            const rapidxml::xml_attribute<char> * const id_attrib = XML_FIRST_ATTRIB( dose_node, "id" );
            const string idval = xml_value_str(id_attrib);
            
            // We dont track GM-tubes, so if the dose-rate is from one of those, dont warn about this
            if( !icontains(det_info_ref, "gmtu")
               && !icontains(det_info_ref, "geiger")
               && !icontains(idval, "gmtu")
               && !icontains(idval, "geiger") )
            {
              const string val = xml_value_str(value_node);
              const string units = units_attrib ? xml_value_str(units_attrib) : string("uSv/h");
              meas_parse_warnings.push_back( "Unable to match dose rate ("
                         + val + " " + units + ") for id='" + det_info_ref + "' to a detector." );
            }
            
            continue;
          }//if( !meas )
            
          if( !xml_value_to_flt(value_node, meas->dose_rate_) )
          {
            meas->dose_rate_ = -1.0f; //xml_value_to_flt sets value to zero on conversion failure
            meas_parse_warnings.push_back( "Invalid dose rate given for detector '" + det_info_ref + "'." );
          }
        }//for( loop over DoseRate nodes )
        
        
        XML_FOREACH_CHILD( exposure_node, meas_node, "ExposureRate" )
        {
          const rapidxml::xml_attribute<char> * const det_attrib
                             = XML_FIRST_ATTRIB( exposure_node, "radDetectorInformationReference" );
          
          if( (spectrum_meas.size() != 1) && (!det_attrib || !det_attrib->value_size()) )
            continue;
          
          const rapidxml::xml_node<char> * const value_node
                                             = XML_FIRST_NODE( exposure_node, "ExposureRateValue" );
          
          if( !value_node || !value_node->value_size() )
            continue;
          
          const string det_info_ref = xml_value_str( det_attrib );
          shared_ptr<Measurement> meas = meas_for_det( det_info_ref );
          if( !meas && (spectrum_meas.size() == 1) && det_info_ref.empty() )
            meas = spectrum_meas[0];
          
          if( !meas )
          {
            meas_parse_warnings.push_back( "Unable to match exposure rate for id='" + det_info_ref + "' to a detector." );
            continue;
          }
          
          if( !xml_value_to_flt(value_node, meas->exposure_rate_) )
          {
            meas->exposure_rate_ = -1.0f;  //xml_value_to_flt sets value to zero on conversion failure
            meas_parse_warnings.push_back( "Invalid exposure rate given for detector '" + det_info_ref + "'." );
          }
        }//for( loop over ExposureRate nodes )
        
        
        vector<std::shared_ptr<Measurement>> meas_to_add;
        
        // TODO: Stop combining the neutron and gamma measurements.  It makes a mess of
        //       reproducibility of saving to N42-2012 and then reading back in.  Probably need to
        //       remove this from the N42-2006 code as well.  What should be done instead is making
        //       sure relevant neutron/gamma measurements have the same sample number.
        //       Also note: the gamma and neutron may not have the same live-time, so shouldnt be
        //       combined anyway.
        if( spectrum_meas.size() == neutron_meas.size() )
        {
          for( size_t i = 0; i < spectrum_meas.size(); ++i )
          {
            std::shared_ptr<Measurement> &gamma = spectrum_meas[i];
            std::shared_ptr<Measurement> &neutron = neutron_meas[i];
            
            gamma->neutron_counts_ = neutron->neutron_counts_;
            gamma->neutron_counts_sum_ = neutron->neutron_counts_sum_;
            gamma->contained_neutron_ = neutron->contained_neutron_;
            for( const string &s : neutron->remarks_ )
            {
              if( std::find(gamma->remarks_.begin(), gamma->remarks_.end(), s)
                 == gamma->remarks_.end() )
                gamma->remarks_.push_back( s );
            }
            
            if( (gamma->dose_rate_ >= 0) || (neutron->dose_rate_ >= 0) )
            {
              gamma->dose_rate_ = std::max( 0.0f, gamma->dose_rate_ );
              gamma->dose_rate_ += std::max( 0.0f, neutron->dose_rate_ );
            }
            
            if( (gamma->exposure_rate_ >= 0) || (neutron->exposure_rate_ >= 0) )
            {
              gamma->exposure_rate_ = std::max( 0.0f, gamma->exposure_rate_ );
              gamma->exposure_rate_ += std::max( 0.0f, neutron->exposure_rate_ );
            }
            
            meas_to_add.push_back( gamma );
          }//for( size_t i = 0; i < spectrum_meas.size(); ++i )
        }else
        {
          vector< pair<std::shared_ptr<Measurement>,std::shared_ptr<Measurement>> > gamma_and_neut_pairs;
          for( size_t i = 0; i < spectrum_meas.size(); ++i )
          {
            if( !spectrum_meas[i] )
              continue;
            
            for( size_t j = 0; j < neutron_meas.size(); ++j )
            {
              if( !neutron_meas[j] )
                continue;
              
              const string &gdetname = spectrum_meas[i]->detector_name_;
              const string &ndetname = neutron_meas[j]->detector_name_;
              if( gdetname.size() < 2 || ndetname.size() < 2 )
                continue;
              
              if( (gdetname == ndetname)
                  || (istarts_with(ndetname, gdetname) && SpecUtils::icontains(ndetname, "neut")) )
              {
                gamma_and_neut_pairs.push_back( make_pair(spectrum_meas[i], neutron_meas[j]) );
                spectrum_meas[i].reset();
                neutron_meas[j].reset();
                break;
              }
            }//for( size_t j = 0; j < neutron_meas.size(); ++j )
          }//for( size_t i = 0; i < spectrum_meas.size(); ++i )
          
          for( size_t i = 0; i < gamma_and_neut_pairs.size(); ++i )
          {
            std::shared_ptr<Measurement> &gamma = gamma_and_neut_pairs[i].first;
            std::shared_ptr<Measurement> &neutron = gamma_and_neut_pairs[i].second;
            
            gamma->neutron_counts_ = neutron->neutron_counts_;
            gamma->neutron_counts_sum_ = neutron->neutron_counts_sum_;
            gamma->contained_neutron_ = neutron->contained_neutron_;
            for( const string &s : neutron->remarks_ )
            {
              if( std::find(gamma->remarks_.begin(), gamma->remarks_.end(), s)
                 == gamma->remarks_.end() )
                gamma->remarks_.push_back( s );
            }
            
            if( (gamma->dose_rate_ >= 0) || (neutron->dose_rate_ >= 0) )
            {
              gamma->dose_rate_ = std::max( 0.0f, gamma->dose_rate_ );
              gamma->dose_rate_ += std::max( 0.0f, neutron->dose_rate_ );
            }
            
            if( (gamma->exposure_rate_ >= 0) || (neutron->exposure_rate_ >= 0) )
            {
              gamma->exposure_rate_ = std::max( 0.0f, gamma->exposure_rate_ );
              gamma->exposure_rate_ += std::max( 0.0f, neutron->exposure_rate_ );
            }
            
            meas_to_add.push_back( gamma );
          }//for( size_t i = 0; i < gamma_and_neut_pairs.size(); ++i )
          
          
          for( size_t i = 0; i < spectrum_meas.size(); ++i )
            if( spectrum_meas[i] )
              meas_to_add.push_back( spectrum_meas[i] );
          
          for( size_t i = 0; i < neutron_meas.size(); ++i )
            if( neutron_meas[i] )
              meas_to_add.push_back( neutron_meas[i] );
        }//if( spectrum_meas.size() == neutron_meas.size() ) / else
        
        //XXX - todo - should implement the below
        //    rapidxml::xml_node<char> *dose_rate_node = XML_FIRST_NODE(meas_node, "DoseRate" );
        //    rapidxml::xml_node<char> *total_dose_node = XML_FIRST_NODE(meas_node, "TotalDose" );
        //    rapidxml::xml_node<char> *exposure_rate_node = XML_FIRST_NODE(meas_node, "ExposureRate" );
        //    rapidxml::xml_node<char> *total_exposure_node = XML_FIRST_NODE(meas_node, "TotalExposure" );
        //    rapidxml::xml_node<char> *instrument_state_node = XML_FIRST_NODE(meas_node, "RadInstrumentState" );
        //    rapidxml::xml_node<char> *item_state_node = XML_FIRST_NODE(meas_node, "RadItemState" );
        
        //Check for duplicate spectrum in spectrum_meas for the same detector, but
        //  with different calibrations.
        //  See comments for #energy_cal_variants and #keep_energy_cal_variants.
        //Note: as of 20160531, this duplicate spectrum stuff is untested.
        const vector<std::shared_ptr<Measurement>>::const_iterator beginmeas = meas_to_add.begin();
        const vector<std::shared_ptr<Measurement>>::const_iterator endmeas = meas_to_add.end();
        for( size_t i = 1; i < meas_to_cal_id.size(); ++i )
        {
          std::shared_ptr<Measurement> &meas = meas_to_cal_id[i].first;
          if( std::find(beginmeas,endmeas,meas) == endmeas )
            continue;
          
          vector< pair<std::shared_ptr<Measurement>,string> > samenames;
          for( size_t j = 0; j < i; ++j )
          {
            std::shared_ptr<Measurement> &innermeas = meas_to_cal_id[j].first;
            
            if( std::find(beginmeas,endmeas,innermeas) == endmeas )
              continue;
            
            if( innermeas->detector_name_ == meas->detector_name_
               && innermeas->start_time_ == meas->start_time_
               && fabs(innermeas->real_time_ - meas->real_time_) < 0.01
               && fabs(innermeas->live_time_ - meas->live_time_) < 0.01
               && (meas_to_cal_id[i].second != meas_to_cal_id[j].second) )
            {
              // Detector name, start, real, and live times are all the same, but energy
              //  calibration is different, we will use the "_intercal_" detector renaming
              //  convention.
              samenames.push_back( make_pair(innermeas, meas_to_cal_id[j].second ) );
            }//if( detector name, real, live, and start times are all the same )
          }//for( size_t j = 0; j < i; ++j )
          
          if( samenames.size() )
          {
            // Usually when we're here it means that the same spectrum was placed into the spectrum
            //  file multiple times, but with different energy calibrations (maybe one with data up
            //  to 3 MEV, and the other up to 9 MeV, or maybe one is linearized, while the other is
            //  binned according to sqrt(energy), etc).
            meas->detector_name_ += "_intercal_" + meas_to_cal_id[i].second;
            for( size_t j = 0; j < samenames.size(); ++j )
              samenames[j].first->detector_name_ += "_intercal_" + samenames[j].second;
          }//if( samenames.size() )
        }//for( size_t i = 1; i < meas_to_cal_id.size(); ++i )
        
        
        // We will insert `meas_parse_warnings` into each Measurement we are adding; not ideal,
        //  but because we dont have a container for the <RadMeasurement> concept, this is the
        //  best we can do, atm.
        if( !meas_parse_warnings.empty() )
        {
          for( auto &m : meas_to_add )
          {
            for( size_t i = 0; i < meas_parse_warnings.size(); ++i )
            {
              const string &w = meas_parse_warnings[i];
              vector<string> &prev_warn = m->parse_warnings_;
              
              // We could check that the message is unique, but this doesnt appear to be necessary
              //if( find( begin(prev_warn), end(prev_warn), w ) == end(prev_warn) )
              //  prev_warn.push_back( w );
              prev_warn.push_back( w );
            }//for( size_t i = 0; i < meas_parse_warnings.size(); ++i )
          }//for( loop over meas_to_add )
        }//if( !meas_parse_warnings.empty() )
        
        
        {
          std::lock_guard<std::mutex> lock( meas_mutex );
          measurements.insert( measurements.end(), meas_to_add.begin(), meas_to_add.end() );
        }
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
      }catch( std::exception &e )
      {
        std::lock_guard<std::mutex> lock( meas_mutex );
        cerr << "Error decoding SpecFile::decode2012N42SpectrumNode(...): "
             << e.what() << endl;
      }//try / catch
#else
      }catch( std::exception &e )
      {
        //
      }//try / catch
#endif
  }//void decode_2012_N42_rad_measurement_node( const rapidxml::xml_node<char> *spectrum )
    

  //decode_2012_N42_detector_quality: gets GPS and detector quality
  //  status as well as InterSpec specific RadMeasurementExtension info (title)
  static void decode_2012_N42_detector_quality( std::shared_ptr<Measurement> meas,
                                                            const rapidxml::xml_node<char> *meas_node )
  {
    if( !meas_node || !meas )
      return;
    
    meas->quality_status_ = SpecUtils::QualityStatus::Good;  //2012 N42 defaults to good
    
    XML_FOREACH_CHILD( det_state_node, meas_node, "RadDetectorState" )
    {
      rapidxml::xml_attribute<char> *det_name_attrib = XML_FIRST_IATTRIB(det_state_node, "radDetectorInformationReference");
      string name = xml_value_str(det_name_attrib);
      if( name == s_unnamed_det_placeholder )
        name.clear();
      
      if( name != meas->detector_name_ )
        continue;
      
      const rapidxml::xml_node<char> *fault = XML_FIRST_NODE(det_state_node, "Fault" );
      const rapidxml::xml_node<char> *remark = XML_FIRST_NODE( det_state_node, "Remark" );
      
      if( fault && fault->value_size() )
      {
        if( XML_VALUE_ICOMPARE( fault, "Fatal" )
           || XML_VALUE_ICOMPARE( fault, "Error" ) )
          meas->quality_status_ = SpecUtils::QualityStatus::Bad;
        else if( XML_VALUE_ICOMPARE( fault, "Warning" ) )
          meas->quality_status_ = SpecUtils::QualityStatus::Suspect;
      }else if( !det_state_node->first_node() ||
               (remark && starts_with( xml_value_str(remark), "InterSpec could not")) )
      {
        meas->quality_status_ = SpecUtils::QualityStatus::Missing; //InterSpec Specific
      }
      
      break;
    }//XML_FOREACH_CHILD( det_state_node, meas_node, "RadDetectorState" )
    
    
    rapidxml::xml_node<char> *extension_node = XML_FIRST_NODE(meas_node, "RadMeasurementExtension");
    
    if( extension_node )
    {
      //This is vestigial for SpecFile_2012N42_VERSION==2
      rapidxml::xml_node<char> *title_node = XML_FIRST_NODE(extension_node, "InterSpec:Title");
      meas->title_ = xml_value_str( title_node );
      
      //This is vestigial for SpecFile_2012N42_VERSION==1
      rapidxml::xml_node<char> *type_node = XML_FIRST_NODE(extension_node, "InterSpec:DetectorType");
      meas->detector_description_ = xml_value_str( type_node );
    }//if( detector_type_.size() || title_.size() )
  }//void decode_2012_N42_detector_quality(...)
  
};//class N42DecodeHelper2012
      
}//SpecUtils namespace to implement N42DecodeHelper2012
      

namespace SpecUtils
{
  
  bool is_candidate_n42_file( const char * data )
  {
    if( !data )
      return false;
    
    //If smaller than 512 bytes, or doesnt contain a magic_strs string, bail
    const char *magic_strs[] = { "N42", "RadInstrumentData", "Measurement",
      "N42InstrumentData", "ICD1", "HPRDS",
    };
    
    size_t nlength = 0;
    while( nlength < 512 && data[nlength] )
      ++nlength;
    
    if( nlength < 512 )
      return false;
    
    const string filebegining( data, data+nlength );
    
    for( const char *substr : magic_strs )
    {
      if( SpecUtils::icontains( filebegining, substr ) )
        return true;
    }
    
    return false;
  }//bool is_candidate_n42_file( const char * data )
  
  
  bool is_candidate_n42_file( const char * const data, const char * const data_end )
  {
    if( !data || data_end <= data )
      return false;
    
    const size_t datalen = data_end - data;
    
    if( datalen < 512 )
      return false;
    
    //If smaller than 512 bytes, or doesnt contain a magic_strs string, bail
    const char *magic_strs[] = { "N42", "RadInstrumentData", "Measurement",
      "N42InstrumentData", "ICD1", "HPRDS",
    };
    
    //Check how many non-null bytes there are
    size_t nlength = 0;
    for( size_t i = 0; i < 512; ++i )
      nlength += (data[i] ? 1 : 0);
    
    //Allow for max of 8 zero bytes...
    
    if( nlength+8 < 512 )
      return false;
    
    const string filebegining( data, data+512 );
    
    for( const char *substr : magic_strs )
    {
      if( SpecUtils::icontains( filebegining, substr ) )
        return true;
    }
    
    return false;
  }//bool is_candidate_n42_file( const char * data, const char * const data_end )
  
  
  char *convert_n42_utf16_xml_to_utf8( char * data, char * const data_end )
  {
    if( !data || data_end <= data )
      return data_end;
    
    const size_t datalen = data_end - data;
    if( datalen < 512 )
      return data_end;
    
    //Look to see how often we alternate between a zero-byte, and non-zero byte
    size_t num_zero_alternations = 0;
    
    //First do a quick check of just the first 64 bytes, being a little loose.
    // (note, we could increment i by two each time, but I was too lazy to test
    //  for now)
    for( size_t i = 1; i < 64; ++i )
      num_zero_alternations += (((!data[i-1]) == (!data[i])) ? 0 : 1);
    
    //For nearly all N42 files num_zero_alternations will still be zero, so we
    //  can return here
    if( num_zero_alternations < 32 )
      return data_end;
    
    //This might be UTF16, lets continue looking at the first 512 bytes.
    for( size_t i = 64; i < 512; ++i )
      num_zero_alternations += (((!data[i-1]) == (!data[i])) ? 0 : 1);
    
    //Arbitrarily allow 16 non-ascii characters in the first 256 characters
    if( num_zero_alternations < 480 )
      return data_end;
    
    //Check that the '<' symbol is in the first ~128 bytes, and skip to it.
    //  The one file I've seen that was UTF16 (but claimed to be UTF8) had the
    //  '<' character at byte three
    char *new_data_start = data;
    while( ((*new_data_start) != '<') && (new_data_start != (data+128)) )
      ++new_data_start;
    
    if( (*new_data_start) != '<' )
      return data_end;
    
    //This is horrible and totally incorrect, but seems to work well enough for
    //  the files I've seen this in... sorry you have to see this, but since
    //  N42 is probably all ASCII, just remove all the zero bytes
    char *new_data_end = data;
    for( char *iter = new_data_start; iter != data_end; ++iter )
    {
      if( *iter )
      {
        *new_data_end = *iter;
        ++new_data_end;
      }
    }
    memset(new_data_end, 0, (data_end - new_data_end) );
    
    return new_data_end;
  }//convert_n42_utf16_xml_to_utf8
  
  bool SpecFile::load_from_N42( std::istream &input )
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    if( !input.good() )
      return false;
    
    const istream::pos_type orig_pos = input.tellg();
    
    try
    {
      rapidxml::file<char> input_file( input );
      return SpecFile::load_N42_from_data( input_file.data(), input_file.data()+input_file.size() );
    }catch( std::exception & )
    {
      input.clear();
      input.seekg( orig_pos, ios::beg );
      reset();
      return false;
    }//try/catch
    
    return true;
  }//bool load_from_N42( char *data )
  
  
  bool SpecFile::load_N42_file( const std::string &filename )
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    try
    {
      std::vector<char> data;
      SpecUtils::load_file_data( filename.c_str(), data );
      
      if( data.empty() )
        throw runtime_error( "Empty file." );
      
      const bool loaded = SpecFile::load_N42_from_data( &data.front(), (&data.front())+data.size() );
      
      if( !loaded )
        throw runtime_error( "Failed to load" );
      
      filename_ = filename;
    }catch(...)
    {
      reset();
      return false;
    }
    
    return true;
  }//bool load_N42_file( const std::string &filename );
  
  
  bool SpecFile::load_N42_from_data( char *data )
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    try
    {
      reset();
      
      if( !is_candidate_n42_file(data) )
        return false;
      
      rapidxml::xml_document<char> doc;
      doc.parse<rapidxml::parse_trim_whitespace|rapidxml::allow_sloppy_parse>( data );
      rapidxml::xml_node<char> *document_node = doc.first_node();
      
      const bool loaded = load_from_N42_document( document_node );
      
      if( !loaded )
        throw runtime_error( "Failed to load" );
    }catch(...)
    {
      reset();
      return false;
    }
    
    return true;
  }//bool load_N42_from_data( char *data )
  
  
  
  void SpecFile::set_n42_2006_instrument_info_node_info( const rapidxml::xml_node<char> *info_node )
  {
    if( !info_node )
      return;
    
    const string xmlns = get_n42_xmlns(info_node);
    
    const rapidxml::xml_node<char> *type_node = xml_first_node_nso( info_node, "InstrumentType", xmlns );
    if( type_node && !XML_VALUE_COMPARE(type_node, "unknown") && !XML_VALUE_ICOMPARE(type_node, "Other") )
      instrument_type_ = xml_value_str( type_node );
    
    const rapidxml::xml_node<char> *manufacturer_node = xml_first_node_nso( info_node, "Manufacturer", xmlns );
    if( manufacturer_node && !XML_VALUE_COMPARE(manufacturer_node, "unknown") )
      manufacturer_ = xml_value_str( manufacturer_node );
    
    const rapidxml::xml_node<char> *model_node = xml_first_node_nso( info_node, "InstrumentModel", xmlns );
    if( model_node && !XML_VALUE_COMPARE(model_node, "unknown") )
      instrument_model_ = xml_value_str( model_node );
    
    const rapidxml::xml_node<char> *id_node = xml_first_node_nso( info_node, "InstrumentID", xmlns );
    if( id_node && !XML_VALUE_COMPARE(id_node, "unknown") )
      instrument_id_ = xml_value_str( id_node );
    
    const rapidxml::xml_node<char> *probe_node = xml_first_node_nso( info_node, "ProbeType", xmlns );
    if( probe_node && !XML_VALUE_COMPARE(probe_node, "unknown") )
    {
      const string val = xml_value_str( probe_node );
      vector<string> fields;
      SpecUtils::split( fields, val, "," );
      
      //Sam940s make it here
      for( string &field : fields )
      {
        trim( field );
        if( SpecUtils::istarts_with( field, "Serial") )
        {
          // SAM940 makes it here
          instrument_id_ += ", Probe " + field;
          continue;
        }
        
        if( SpecUtils::istarts_with( field, "Type") )
        {
          // SAM940 makes it here
          instrument_model_ += "," + field.substr(4);
          continue;
        }
      
        //identiFINDER 2 NGH make it here
        //  "Gamma Detector: NaI 35x51 Neutron Detector: ³He tube 3He3/608/15NS"
        //  or "Gamma Detector: NaI 35 mm x 51 mm, Neutron Detector: ³He tube 8000 mBar/14.0 mm x 29.0 mm"
        //RadEagle will also:
        //  "Gamma Detector: NaI 3x1 GM Tube: 6x25 Type 716 Neutron: He3"
        
        string lowered = SpecUtils::to_lower_ascii_copy( field );
      
        if( lowered.size() != field.size() )
        {
          lowered.resize( field.size(), ' ' ); //things will get messed up, but at least not crash
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
          log_developer_error( __func__,  "to_lower_ascii_copy() changed string length" );
#endif
        }//if( lowered.size() != field.size() )
        
        size_t gamma_pos = lowered.find( "gamma detector:" );
        if( gamma_pos == string::npos )
          gamma_pos = lowered.find( "gamma:" );
        size_t neut_pos = lowered.find( "neutron detector:" );
        if( neut_pos == string::npos )
          neut_pos = lowered.find( "neutron:" );
        const size_t gm_pos = lowered.find( "gm tube:" );
        
        vector<size_t> posvec;
        if( gamma_pos != string::npos )
          posvec.push_back( gamma_pos );
        if( neut_pos != string::npos )
          posvec.push_back( neut_pos );
        if( gm_pos != string::npos )
          posvec.push_back( gm_pos );
        
        std::sort( begin(posvec), end(posvec) );
        
        for( size_t i = 0; i < posvec.size(); ++i )
        {
          size_t len = string::npos;
          if( (i+1) < posvec.size() )
            len = posvec[i+1] - posvec[i];
          remarks_.push_back( SpecUtils::trim_copy( field.substr( posvec[i], len ) ) );
        }
        
        if( posvec.empty() )
        {
          //identiFINDER 2 NG makes it here. "Gamma Detector: NaI 35x51"
          remarks_.push_back( field );
        }
      }//for( string &field : fields )
    }//if( probe_node && !XML_VALUE_COMPARE(probe_node, "unknown") )
    
    
    const rapidxml::xml_node<char> *lane_number_node = xml_first_node_nso( info_node, "LaneNumber", xmlns );
    if( lane_number_node && lane_number_node->value_size() )
    {
      const string lanestr = xml_value_str( lane_number_node );
      
      char *pEnd = NULL;
      const char *lanecstr = lanestr.c_str();
      
      const long int val = strtol( lanecstr, &pEnd, 10 );
      
      if( val || (lanecstr != pEnd) )
        lane_number_ = static_cast<int>( val );
    }//if( lane_number_node && lane_number_node->value_size() )
    
    const rapidxml::xml_node<char> *inst_version = xml_first_node_nso( info_node, "InstrumentVersion", xmlns );
    if( inst_version && inst_version->value_size() )
    {
      //"DETH-008"
      //"4822400HHB-D"
      //"Firmware DETD-306 Software 3.1.11.27229"
      //"Hardware: 4C  Firmware: 5.00.54  Operating System: 1.2.040  Application: 2.37"
      vector<string> fields;
      const string value = xml_value_str( inst_version );
      
      vector<pair<string,string> > subcomponents;
      const size_t ntab = std::count(value.begin(), value.end(), '\t');
      const size_t nsemi = std::count(value.begin(), value.end(), ':');
      
      bool hassub = false;
      if( nsemi == (ntab+1) )
      {
        hassub = true;
        //A rough, and easily breakable hack for:
        //"Hardware: 4C  Firmware: 5.00.54  Operating System: 1.2.040  Application: 2.37"
        SpecUtils::split( fields, value, "\t");
        for( size_t i = 0; hassub && i < fields.size(); ++i )
        {
          vector<string> subfields;
          SpecUtils::split( subfields, fields[i], ":");
          if( subfields.size() == 2 )
            subcomponents.push_back( make_pair(subfields[0], subfields[1]) );
          else
            hassub = false;
        }
      }
      
      if( !hassub )
      {
        SpecUtils::split( fields, value, " \t:");
        
        if( fields.size() )
        {
          if( (fields.size() % 2) != 0 )
          {
            component_versions_.push_back( make_pair("System", xml_value_str(inst_version)) );
          }else
          {
            for( size_t i = 0; i < fields.size(); i += 2 )
            {
              string &name = fields[i];
              string &value = fields[i+1];
              SpecUtils::trim(name);
              SpecUtils::trim(value);
              SpecUtils::ireplace_all( name, ":", "" );
              component_versions_.push_back( make_pair(name,value) );
            }
          }
        }//if( fields.size() )
      }
    }//if( inst_version && inst_version->value_size() )
    
    //<Canberra:Version>2.0.0.8</Canberra:Version>
    inst_version =  XML_FIRST_NODE(info_node,"Canberra:Version");
    if( inst_version && inst_version->value_size() )
      component_versions_.push_back( make_pair("CanberraVersion", xml_value_str(inst_version)) );
    
    
    //RadSeeker HPRDS.  Grab the detector type and append to model for now...
    //  This is a bit of a hack, and should be improved.
    const rapidxml::xml_node<char> *det_setup = xml_first_node_nso( info_node, "DetectorSetup", "sym:" );
    if( det_setup )
    {
      for( const rapidxml::xml_node<char> *det = XML_FIRST_NODE( det_setup, "Detector" );
          det; det = XML_NEXT_TWIN(det) )
      {
        const rapidxml::xml_attribute<char> *type_attrib = XML_FIRST_ATTRIB( det, "Type" );
        if( !type_attrib || !XML_VALUE_ICOMPARE(type_attrib,"MCA") )
          continue;
        
        const rapidxml::xml_node<char> *id_settings = XML_FIRST_NODE(det, "IdentificationSettings");
        if( !id_settings )
          continue;
        
        const rapidxml::xml_attribute<char> *Material = XML_FIRST_ATTRIB( id_settings, "Material" );
        const rapidxml::xml_attribute<char> *Size = XML_FIRST_ATTRIB( id_settings, "Size" );
        const rapidxml::xml_attribute<char> *Name = XML_FIRST_ATTRIB( id_settings, "Name" );
        if( Material || Size || Name )
        {
          //string val = "Gamma Detector: " + xml_value_str(Material) + " " + xml_value_str(Size) + " " + xml_value_str(Name);
          //remarks_.push_back( val );
              
          string val = xml_value_str(Material) + " " + xml_value_str(Size) + " " + xml_value_str(Name);
          SpecUtils::trim( val );
          SpecUtils::ireplace_all( val, "  ", " " );
          if( instrument_model_.size() )
            val = " " + val;
          instrument_model_ += val;
        }
      }
    }//if( det_setup )
    
  }//void set_n42_2006_instrument_info_node_info( const rapidxml::xml_node<char> *info_node )
  
  
  
  void Measurement::set_n42_2006_count_dose_data_info( const rapidxml::xml_node<char> *dose_data )
  {
    if( !dose_data )
      return;
    
    const string xmlns = get_n42_xmlns(dose_data);
    
    Measurement *m = this;
    
    //Detective N42 2006 files have a CountRate node that you need to multiply
    //  by the live time
    
    const rapidxml::xml_node<char> *count_node = xml_first_node_nso( dose_data, "CountRate", xmlns );
    const rapidxml::xml_node<char> *realtime_node = xml_first_node_nso( dose_data, "SampleRealTime", xmlns );
    
    
    const rapidxml::xml_base<char> *det_type_node = XML_FIRST_ATTRIB(dose_data, "DetectorType");
    if( !det_type_node )
      det_type_node = XML_FIRST_NODE( dose_data, "DetectorType" );
    
    
    const rapidxml::xml_node<char> *total_dose_node = xml_first_node_nso( dose_data, "TotalDose", xmlns );
    if( total_dose_node )
    {
      // TODO: position upgrade - can we just divide by time or something here?
    }//if( total_dose_node )
    
    
    const rapidxml::xml_node<char> *dose_node = xml_first_node_nso( dose_data, "DoseRate", xmlns );
    if( dose_node && dose_node->value_size() )
    {
      try
      {
        const rapidxml::xml_attribute<char> *units_attrib = XML_FIRST_ATTRIB( dose_node, "Units" );
        if( !units_attrib || !units_attrib->value_size() )
          throw runtime_error( "No units for dose" );
        
        float dose_rate = 0.0;
        if( xml_value_to_flt( dose_node, dose_rate ) )
        {
          dose_rate *= dose_units_usvPerH( units_attrib->value(), units_attrib->value_size() );
          
          if( m->dose_rate_ >= 0 )
            m->dose_rate_ += dose_rate;
          else
            m->dose_rate_ = dose_rate;
        }
      }catch( std::exception &e )
      {
        m->parse_warnings_.push_back( "Failed to parse dose rate: " + string(e.what()) );
      }
    }//if( dose_node && dose_node->value_size() )
    
    
    if( count_node && count_node->value_size()
       && (!det_type_node || XML_VALUE_ICOMPARE(det_type_node, "Neutron")) )
    {
      try
      {
        if( !realtime_node || !realtime_node->value_size() )
          throw runtime_error( "Couldnt find realtime for neutron count rate" );
        
        const float realtime = time_duration_string_to_seconds( realtime_node->value(),
                                                               realtime_node->value_size() );
        if( realtime <= 0.0f )
          throw runtime_error( "Couldnt read realtime" );
        
        rapidxml::xml_attribute<char> *units_attrib = count_node->first_attribute( "Units", 5 );
        if( units_attrib && !units_attrib->value_size() )
          units_attrib = 0;
        
        if( units_attrib && units_attrib->value_size()
           && !SpecUtils::icontains( xml_value_str(units_attrib), "CPS") )
          throw runtime_error( "Neutron count rate not in CPS" );
        
        float countrate;
        if( !xml_value_to_flt(count_node, countrate) )
          throw runtime_error( "Neutron count rate is non-numeric" );
        
        m->neutron_counts_sum_ = countrate * realtime;
        m->neutron_counts_.resize(1);
        m->neutron_counts_[0] = countrate * realtime;
        m->contained_neutron_ = true;
        m->remarks_.push_back( "Neutron Real Time: " + xml_value_str(realtime_node) );
        
        if( (m->real_time_ > FLT_EPSILON)
           && fabs(m->live_time_ - realtime) > 0.1f*m->live_time_ )
        {
          string msg = "Warning: The neutron live time may not correspond to the gamma live time.";
          if( std::find(begin(m->parse_warnings_),end(m->parse_warnings_),msg) == end(m->parse_warnings_) )
            m->parse_warnings_.emplace_back( std::move(msg) );
        }
        
        rapidxml::xml_node<char> *starttime_node = XML_FIRST_NODE(dose_data, "StartTime");
        if( starttime_node && starttime_node->value_size() )
        {
          time_point_t starttime = time_from_string( xml_value_str(starttime_node).c_str() );
          if( !is_special(starttime) && !is_special(m->start_time_) )
          {
            if( (starttime - m->start_time_) > chrono::minutes(1) )
            {
              string msg = "Warning: neutron start time doesnt match gamma start time!";
              if( std::find(begin(m->parse_warnings_), end(m->parse_warnings_),msg) == end(m->parse_warnings_) )
                m->parse_warnings_.emplace_back( std::move(msg) );
            }
          }
        }//if( starttime_node && starttime_node->value_size() )
      }catch( std::exception &e )
      {
        string msg = "Error decoding neutron count rate: " + string(e.what());
        if( std::find(begin(m->parse_warnings_), end(m->parse_warnings_),msg) == end(m->parse_warnings_) )
          m->parse_warnings_.emplace_back( std::move(msg) );
        
        //Could probably give more information here, but I doubt it will ever actually
        //  occur, so not bothering.
      }//try / catch
    }//if( we have count rate and sample real time nodes )
    
    
    if( !det_type_node )
      return;
  
    if( XML_VALUE_ICOMPARE(det_type_node, "Neutron") )
    {
      const rapidxml::xml_node<char> *counts = xml_first_node_nso( dose_data, "Counts", xmlns );
      if( counts && counts->value_size() )
      {
        float neut_counts = 0.0;
        if( xml_value_to_flt(counts,neut_counts) )
        {
          m->neutron_counts_sum_ += neut_counts;
          if( m->neutron_counts_.empty() )
            m->neutron_counts_.push_back( neut_counts );
          else if( m->neutron_counts_.size() == 1 )
            m->neutron_counts_[0] += neut_counts;
          else
          {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
            cerr << "Have both neutron spectrum and neutron dose count" << endl;
#endif
          }
          
          const rapidxml::xml_node<char> *real_time_node = xml_first_node_nso( dose_data, "SampleRealTime", xmlns );
          if( real_time_node && real_time_node->value_size() )
            m->remarks_.push_back( "Neutron Real Time: " + xml_value_str(real_time_node) );
          
          // TODO:
          //  We get this dose rate info from historical-Cambio wether or not there was
          //  a neutron detector, so we will only mark the detector as a neutron
          //  detector if at least one sample has at least one neutron
          //   ... I'm not a huge fan of this...
          m->contained_neutron_ |= (m->neutron_counts_[0]>0.0);
        }else
        {
          string msg = "Error converting neutron counts '" + xml_value_str(counts) + "' to float";
          m->parse_warnings_.push_back( msg );
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
          cerr << msg << "; ignoring" << endl;
#endif
        }
      }//if( counts )
    }
    
    //else if( XML_VALUE_ICOMPARE(det_type_node, "Gamma") )
    //{
    //  // I guess there is nothing to do here?
    //}//if( neutron detector ) / else( gamma detector )
    
  }//void set_n42_2006_count_dose_data_info( const rapidxml::xml_node<char> *dose_data )
  
  
  
  void Measurement::set_n42_2006_gross_count_node_info( const rapidxml::xml_node<char> *gross_count_meas )
  {
    if( !gross_count_meas )
      throw runtime_error( "!gross_count_measurement" );
    
    const string xmlns = get_n42_xmlns(gross_count_meas);
    
    Measurement *m = this;
    
    bool is_neuteron = m->contained_neutron_;
    
    if( !is_neuteron )
    {
      const rapidxml::xml_attribute<char> *det_type_attrib = XML_FIRST_ATTRIB( gross_count_meas, "DetectorType" );
      
      if( !det_type_attrib )
      {
        const rapidxml::xml_node<char> *parent = gross_count_meas->parent();
        if( parent )
          det_type_attrib = XML_FIRST_ATTRIB( parent, "DetectorType" );
      }//if( !det_type_attrib )
      if( det_type_attrib )
        is_neuteron = icontains( xml_value_str(det_type_attrib), "Neutron" );
    }//if( !is_neuteron )
    
    if( !is_neuteron )
      throw runtime_error( "!is_neuteron" );
    
    if( m->neutron_counts_sum_ > 0.0001 )
      throw runtime_error( "m->totalNeutronCounts > 0.0001" );
    
    float nprev = 0.0;
    for( size_t i = 0; i < m->neutron_counts_.size(); ++i )
      nprev +=  m->neutron_counts_[i];
    
    if( nprev > 0.0001 )
      throw runtime_error( "nprev > 0.0001" );
    
    m->contained_neutron_ = true;
    m->neutron_counts_.resize( 1 );
    m->neutron_counts_[0] = 0.0;
    
    const rapidxml::xml_node<char> *node = xml_first_node_nso( gross_count_meas, "GrossCounts", xmlns );
    
    if( node )
    {
      if( SpecUtils::split_to_floats( node->value(), node->value_size(), m->neutron_counts_ ) )
        m->neutron_counts_sum_ = std::accumulate( m->neutron_counts_.begin(), m->neutron_counts_.end(), 0.0f, std::plus<float>() );
      else
        m->neutron_counts_sum_ = 0.0;
    }//if( node )
    
    
    //attempt to set detctor name
    if( m->detector_name_.empty() )
    {
      const rapidxml::xml_attribute<char> *det_atrrib = NULL;
      for( node = gross_count_meas; !det_atrrib && node; node = node->parent() )
        det_atrrib = XML_FIRST_ATTRIB( node, "Detector" );
      if( det_atrrib )
        m->detector_name_ = xml_value_str( det_atrrib );
    }//if( m->detector_name_.empty() )
    
  }//void set_n42_2006_gross_count_node_info(...)
  
  
  
  
  void SpecFile::set_n42_2006_measurement_location_information(
                                                              const rapidxml::xml_node<char> *measured_item_info_node,
                                                              std::vector<std::shared_ptr<Measurement>> added_measurements )
  {
    if( !measured_item_info_node )
      return;
    
    const string xmlns = get_n42_xmlns(measured_item_info_node);
    
    vector<string> remarks;
    
    /// @TODO - get the remarks - but for right now just put into remarks_
    for( const rapidxml::xml_node<char> *remark_node = xml_first_node_nso( measured_item_info_node, "Remark", xmlns );
        remark_node;
        remark_node = XML_NEXT_TWIN(remark_node) )
    {
      string remark = xml_value_str(remark_node);
      trim( remark );
      if(!remark.empty())
        remarks_.push_back( remark );
    }
    
    // Speed is sometimes in the remarks of the spectrum, which would have already been set
    float speed = numeric_limits<float>::quiet_NaN();
    for( const auto &m : added_measurements )
    {
      if( m->location_ && !IsNan(m->location_->speed_))
        speed = m->location_->speed_;
    }
    
    double latitude = -999.9;
    double longitude = -999.9;
    time_point_t position_time{};
    
    const rapidxml::xml_node<char> *meas_loc_name = NULL;
    const rapidxml::xml_node<char> *meas_loc = xml_first_node_nso( measured_item_info_node, "MeasurementLocation", xmlns );
    
    if( !meas_loc )
      meas_loc = xml_first_node_nso( measured_item_info_node, "InstrumentLocation", xmlns );
    
    if( !meas_loc && XML_NAME_ICOMPARE(measured_item_info_node, "InstrumentLocation") )
      meas_loc = measured_item_info_node;
    
    
    if( meas_loc )
    {
      meas_loc_name = xml_first_node_nso( meas_loc, "MeasurementLocationName", xmlns );
      const rapidxml::xml_node<char> *coord_node = xml_first_node_nso( meas_loc, "Coordinates", xmlns );
      
      //We should actually loop over Coordinate nodes here and average values
      //  that occur while the measurement is being taken...
      if( coord_node && coord_node->value() )
      {
        if( (stringstream(xml_value_str(coord_node)) >> latitude >> longitude) )
        {
          //If there are a third coordinate is there, it is elevation in meters.
          //
          const rapidxml::xml_base<char> *time_element = XML_FIRST_ATTRIB( coord_node, "Time" );
          
          if( !time_element ) //Raytheon Portal
            time_element = xml_first_node_nso(meas_loc, "GPSDateTime", "ray:");
          
          if( time_element )
            position_time = time_from_string( xml_value_str(time_element).c_str() );
          
          //const rapidxml::xml_base<char> *delusion_element = xml_first_node_nso(meas_loc, "DelusionOfPrecision", "ray:");
        }
      }//if( coord_node )
    }//if( meas_loc )
    
    //I've notices some Detecive-EX100 have coordiantes specified similar to:
    //  <Coordinates Time="2017-08-18T10:33:52-04:00">3839.541600 -7714.840200 32</Coordinates>
    //  Which are actuall 38 degrees 39.541600 minutes North, 77 degrees 14.840200 minutes West, and an elevation of 32m
    //  which is 38.659028, -77.247333
    //  Unfortuanetly I havent yet noticed a solid tell in the file for this besides invalid coordinates
    if( !SpecUtils::valid_latitude(latitude)
       && !SpecUtils::valid_longitude(longitude)
       && (fabs(latitude)>999.99)
       && (fabs(longitude)>999.99) )
    {
      //As of 20170818 I have only tested this code for a handfull of points in the US
      //  (all positive latitude, and negative longitude).
      double lat_deg = floor( fabs(latitude) / 100.0);
      double lon_deg = floor( fabs(longitude) / 100.0);
      
      lat_deg += (fabs(latitude) - 100.0*lat_deg) / 60.0;
      lon_deg += (fabs(longitude) - 100.0*lon_deg) / 60.0;
      
      lat_deg *= (latitude > 0 ? 1.0 : -1.0);
      lon_deg *= (longitude > 0 ? 1.0 : -1.0);
      
      if( SpecUtils::valid_latitude(lat_deg) && SpecUtils::valid_longitude(lon_deg) )
      {
        latitude = lat_deg;
        longitude = lon_deg;
      }
    }//if( invalid lat/long, but maybe specified in a wierd format )
    
    if( SpecUtils::valid_latitude(latitude) && SpecUtils::valid_longitude(longitude) )
    {
      auto loc = make_shared<LocationState>();
      loc->type_ = LocationState::StateType::Instrument;
      
      auto geo_point = make_shared<GeographicPoint>();
      loc->speed_ = speed;
      loc->geo_location_ = geo_point;
      geo_point->latitude_ = latitude;
      geo_point->longitude_ = longitude;
      geo_point->position_time_ = position_time;
      
      for( auto &meas : added_measurements )
        meas->location_ = loc;
    }//if( !valid(latitude) && !valid(longitude) )
    
    if( !meas_loc_name )
      meas_loc_name = xml_first_node_nso( measured_item_info_node, "MeasurementLocationName", xmlns );
    measurement_location_name_ = xml_value_str( meas_loc_name );
    
    const rapidxml::xml_node<char> *operator_node = xml_first_node_nso( measured_item_info_node, "MeasurementOperator", xmlns );
    measurement_operator_ = xml_value_str( operator_node );
  }//void set_n42_2006_measurement_location_information()
      
  
  void SpecFile::load_2006_N42_from_doc( const rapidxml::xml_node<char> *document_node )
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    //CambioN42, ORTEC IDM, Thermo: document_node == "N42InstrumentData"
    
    //CambioN42: I think has multiple <Measurement> nodes
    //          The <Measurement> node then has child <Spectrum>, and possibly <CountDoseData>
    //            The <Spectrum> has children <StartTime>, <LiveTime>, <RealTime>, <Calibration>, and <ChannelData>, <Cambio:TagChar>, <Cambio:OriginalFileType>, <Cambio:Version>
    //          Note that cambio n42 files does necessarily preserve detector name, sample number, speed, etc
    //ORTEC IDM, Thermo, SAIC8 all have single <Measurement> node, which has children <InstrumentInformation>, <InstrumentType>, <Manufacturer>, <InstrumentModel>, <InstrumentID>, <LaneNumber>, <dndons:DetectorStatus>, <MeasuredItemInformation>, and <DetectorData>
    //           There is a <DetectorData> node for each timeslice, with daughtes <StartTime>, <SampleRealTime>, <Occupied>, <Speed>, and <DetectorMeasurement>
    //            The <DetectorMeasurement> node then has child <SpectrumMeasurement>
    //              The <SpectrumMeasurement> node then has children <SpectrumAvailable>, <Spectrum>
    //                There are then a <spectrum> node for each detector, which has the children <RealTime>, <LiveTime>, <SourceType>, <DetectorType>, <Calibration>, and <ChannelData>
    
    if( !document_node )
      throw runtime_error( "load_2006_N42_from_doc: Invalid document node" );
    
    //Indicates a "spectrometer" type file, see
    //  http://physics.nist.gov/Divisions/Div846/Gp4/ANSIN4242/2005/simple.html
    //  for example nucsafe g4 predators also go down this path
    const rapidxml::xml_node<char> *firstmeas = XML_FIRST_NODE(document_node, "Measurement" );
    const bool is_spectrometer = (firstmeas && XML_FIRST_NODE(firstmeas, "Spectrum"));
    
    std::mutex queue_mutex;
    std::shared_ptr<DetectorAnalysis> analysis_info = make_shared<DetectorAnalysis>();
    
    const rapidxml::xml_node<char> *actual_doc_node = document_node->document();
    if( !actual_doc_node )
      actual_doc_node = document_node;
    N42CalibrationCache2006 energy_cal( actual_doc_node );
      
    SpecUtilsAsync::ThreadPool workerpool;
    
    const string xmlns = get_n42_xmlns(document_node);
    
    if( is_spectrometer )
    {
      vector<const rapidxml::xml_node<char> *> countdoseNodes, locationNodes;
      
      for( const rapidxml::xml_node<char> *measurement = xml_first_node_nso( document_node, "Measurement", xmlns );
          measurement;
          measurement = XML_NEXT_TWIN(measurement) )
      {
        
        for( const rapidxml::xml_node<char> *spectrum = xml_first_node_nso( measurement, "Spectrum", xmlns );
            spectrum;
            spectrum = XML_NEXT_TWIN(spectrum) )
        {
          std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
          measurements_.push_back( meas );
          workerpool.post( N42DecodeHelper2006( spectrum, &queue_mutex,
                                                    meas, analysis_info,
                                                    measurement, document_node, energy_cal ) );
        }
        
        for( const rapidxml::xml_node<char> *dose = xml_first_node_nso( measurement, "CountDoseData", xmlns );
            dose;
            dose = XML_NEXT_TWIN(dose) )
        {
          countdoseNodes.push_back( dose );
          //We dont want to set file level information from this <Measurement> node
          //  so we will continue;
          //continue;
        }//if( spectrum ) / else ( dose )
        
        //XML files from "Princeton Gamma-Tech Instruments, Inc." detectors
        //  will have the InstrumentInformation, AnalysisResults, and
        //  Calibration here
        const rapidxml::xml_node<char> *inst_info = xml_first_node_nso( measurement, "InstrumentInformation", xmlns );
        set_n42_2006_instrument_info_node_info( inst_info );
        
        const rapidxml::xml_node<char> *analysis_node = xml_first_node_nso( measurement, "AnalysisResults", xmlns );
        if( analysis_node && analysis_info )
          set_analysis_info_from_n42( analysis_node, *analysis_info );
        
        //Try to find the MeasuredItemInformation node - this is all just barely hacked in
        const rapidxml::xml_node<char> *item_info_node = xml_first_node_nso( measurement, "MeasuredItemInformation", xmlns );
        if( !item_info_node && inst_info )
          item_info_node = xml_first_node_nso( inst_info, "MeasuredItemInformation", xmlns );
        
        if( !item_info_node )  //HPRDS files
          item_info_node = xml_first_node_nso( measurement, "InstrumentLocation", xmlns );
        
        locationNodes.push_back( item_info_node );
        
        //This should have already been taken care of by inst_info
        //      const rapidxml::xml_node<char> *info_node = XML_FIRST_NODE(measurement, "InstrumentInformation");
        //      set_n42_2006_instrument_info_node_info( info_node );
      }//for( loop over <Measurement> nodes )
      
      workerpool.join();
      
      N42DecodeHelper2006::filter_valid_measurements( measurements_ );
      
      //This loop may be taken care of by N42DecodeHelper2006 anyway
      for( size_t i = 0; i < countdoseNodes.size(); ++i )
      {
        //Detectives N42 are what end up here.
        //Detectives list the neutron information in "CountDoseData" nodes
        //  If this is the case, we are ignoring <InstrumentInformation>,
        //  <MeasuredItemInformation> nodes.  In
        const rapidxml::xml_node<char> *dose = countdoseNodes[i];
        
        const rapidxml::xml_attribute<char> *dettype = XML_FIRST_ATTRIB(dose, "DetectorType");
        if( dettype && !XML_VALUE_ICOMPARE(dettype, "Neutron") )
          continue;
        
        //bool addedCountDose = false;
        
        //find the nearest measurements and set intfo from it
        if( measurements_.size() == 1 )
        {
          //If only one measurement, set the data no matter whate
          measurements_[0]->set_n42_2006_count_dose_data_info( dose );
          //addedCountDose = true;
        }else if( measurements_.size() )
        {
          const rapidxml::xml_node<char> *starttime_node = xml_first_node_nso( dose, "StartTime", xmlns );
          if( starttime_node && starttime_node->value_size() )
          {
            const time_point_t starttime = time_from_string( xml_value_str(starttime_node).c_str() );
            if( !is_special(starttime) )
            {
              int nearestindex = -1;
              time_point_t::duration smallestdelta = time_point_t::duration::max();
              for( size_t j = 0; j < measurements_.size(); j++ )
              {
                const time_point_t &thisstartime = measurements_[j]->start_time_;
                time_point_t::duration thisdelta = thisstartime - starttime;
                if( thisdelta < time_point_t::duration::zero() )
                  thisdelta = -thisdelta;
                if( thisdelta < smallestdelta )
                {
                  smallestdelta = thisdelta;
                  nearestindex = (int)j;
                }
              }//for( size_t j = 1; j < measurements_.size(); j++ )
              
              //Make sure the start time maches within 1 minute (arbitrary), there
              //  is apparently a mode in detectives where the start time of
              //  neutrons can be well before the gamma, which causes confusion.
              if( nearestindex >= 0
                 && smallestdelta < chrono::minutes(1) )
              {
                if( !measurements_[nearestindex]->contained_neutron_ )
                {
                  measurements_[nearestindex]->set_n42_2006_count_dose_data_info( dose );
                  //addedCountDose = true;
                }
                
                if( measurements_.size() == 2
                   && measurements_[nearestindex]->source_type() == SourceType::Foreground
                   && measurements_[nearestindex?0:1]->source_type() == SourceType::Background )
                {
                  //For nucsafe g4 predator
                  const rapidxml::xml_attribute<char> *det_attrib = XML_FIRST_ATTRIB(dose, "DetectorType");
                  const rapidxml::xml_node<char> *backrate_node = xml_first_node_nso(dose, "BackgroundRate", "Nucsafe:");
                  std::shared_ptr<Measurement> back = measurements_[nearestindex?0:1];
                  
                  if( !back->contained_neutron_
                     && det_attrib && backrate_node && XML_VALUE_ICOMPARE(det_attrib, "Neutron") )
                  {
                    std::shared_ptr<Measurement> back = measurements_[nearestindex?0:1];
                    float rate;
                    if( xml_value_to_flt( backrate_node, rate) )
                    {
                      rate *= back->real_time_;
                      back->contained_neutron_ = true;
                      back->neutron_counts_.resize(1);
                      back->neutron_counts_[0] = rate;
                      back->neutron_counts_sum_ = rate;
                    }
                  }//( if( the foregorund neutron info als ocontains background neutron rate )
                }//if( there is a foreground and background spectrum for this measurement, and we're assigning the neutron info to foreground )
              }//if( found an appropriate measurement to put this neutron info in )
            }//if( !is_special(starttime) )
          }//if( starttime_node && starttime_node->value_size() )
        }//if( we only have one other measurement ) / else, find nearest one
        
        //#if(PERFORM_DEVELOPER_CHECKS)
        //      if( !addedCountDose )
        //        log_developer_error( __func__, "Failed to add count dose data!" );
        //#endif
      }//for( size_t i = 0; i < countdoseNodes.size(); ++i )
      
      
      //Add in GPS and other location information.
      //Note that this is a hack to read in information saved by this same class
      //  in 2006 N42 files,
      //I've also seen this for refOAD5L6QOTM, where this construct is actually
      //  wrong and we should matchup the GPS coordinated using a time based
      //  approach
      for( size_t i = 0; i < locationNodes.size(); ++i )
      {
        if( locationNodes[i] && (i<measurements_.size()) && measurements_[i] )
        {
          vector<std::shared_ptr<Measurement>> addedmeas( 1, measurements_[i] );
          set_n42_2006_measurement_location_information( locationNodes[i], addedmeas );
        }//if( locationNodes[i] && measurements_[i] )
      }//for( size_t i = 0; i < locationNodes.size(); ++i )
      
    }else  //if( is_spectrometer )
    {
      //A variable to hold the name we will append to detectors name when there is multiple
      //  calibrations for each spectrum (see notes for <ray:SpectrumSize> below)
      map<size_t,string> multical_name_append;
      
      //This below loop could be parallized a bit more, in terms of all
      //  <Spectrum> tags of all <Measurement>'s being put in the queue to work
      //  on asyncronously.  In practice though this isnt necassarry since it
      //  looks like most passthrough files only have one <Measurement> tag anyway
      
      for( const rapidxml::xml_node<char> *measurement = xml_first_node_nso( document_node,"Measurement",xmlns);
          measurement;
          measurement = XML_NEXT_TWIN(measurement) )
      {
        const rapidxml::xml_attribute<char> *uuid_attrib = measurement->first_attribute( "UUID", 4 );
        if( uuid_attrib && uuid_attrib->value_size() )
        {
          string thisuuid = xml_value_str( uuid_attrib );
          SpecUtils::trim( thisuuid );
          
          if( uuid_.empty() )
            uuid_ = thisuuid;
          else if( uuid_.length() < 32 && !thisuuid.empty() )
            uuid_ += " " + thisuuid;
        }//if( uuid_attrib && uuid_attrib->value_size() )
        
        for( const rapidxml::xml_node<char> *remark = xml_first_node_nso(measurement, "Remark", xmlns );
            remark;
            remark = XML_NEXT_TWIN(remark) )
        {
          string str = xml_value_str(remark);
          trim( str );
          
          if( SpecUtils::istarts_with( str, s_parser_warn_prefix) )
          {
            SpecUtils::ireplace_all( str, s_parser_warn_prefix, "" );
            parse_warnings_.emplace_back( std::move(str) );
          }else if( !str.empty() )
          {
            remarks_.emplace_back( std::move(str) );
          }
        }//for( loop over remarks )
        
        
        rapidxml::xml_attribute<char> *inspection_attrib = XML_FIRST_ATTRIB( measurement, "dndons:Inspection" );
        inspection_ = xml_value_str( inspection_attrib );
        
        vector<std::shared_ptr<Measurement>> added_measurements;
        
        //<SpectrumMeasurement> nodes should be under <DetectorMeasurement> nodes,
        //  however "Avid Annotated Spectrum" files (see ref40568MWYJS)
        //  dont obey this.  I didnt test this addition very well.
        if( XML_FIRST_NODE(measurement, "SpectrumMeasurement") )
        {
          vector< std::shared_ptr<Measurement> > measurements_this_node;
          
          
          for( const rapidxml::xml_node<char> *spec_meas_node = xml_first_node_nso( measurement, "SpectrumMeasurement", xmlns );
              spec_meas_node;
              spec_meas_node = XML_NEXT_TWIN(spec_meas_node) )
          {
            //each detector will have a <Spectrum> node (for each timeslice)
            for( const rapidxml::xml_node<char> *spectrum = xml_first_node_nso( spec_meas_node, "Spectrum", xmlns );
                spectrum;
                spectrum = XML_NEXT_TWIN(spectrum) )
            {
              std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
              measurements_this_node.push_back( meas );
              workerpool.post( N42DecodeHelper2006( spectrum, &queue_mutex,
                                                        meas, analysis_info, NULL,
                                                        document_node, energy_cal ) );
            }//for( rapidxml::xml_node<char> *spectrum = ... )
          }//for( const rapidxml::xml_node<char> *spec_meas_node = ... )
          
          workerpool.join();
          
          
          N42DecodeHelper2006::filter_valid_measurements( measurements_this_node );
          
          for( auto &meas : measurements_this_node )
          {
            measurements_.push_back( meas );
            added_measurements.push_back( meas );
          }//for( auto &meas : measurements_this_node )
        }//if( XML_FIRST_NODE(measurement, "SpectrumMeasurement" ) )
        
        
        
        //each timeslice will have a <DetectorData> node
        for( const rapidxml::xml_node<char> *det_data_node = xml_first_node_nso( measurement, "DetectorData", xmlns );
            det_data_node;
            det_data_node = XML_NEXT_TWIN(det_data_node) )
        {
          vector< std::shared_ptr<Measurement> > measurements_this_node, gross_counts_this_node;
          
          //For files that give multiple spectra, but with different energy
          //  calibration, for the same data, lets keep track of the <Spectrum>
          //  node each Measurement cooresponds to, so we can append the detector
          //  name with the calibration name (kinda a hack, see
          //  #energy_cal_variants and #keep_energy_cal_variants.
          vector< pair<std::shared_ptr<Measurement>, const rapidxml::xml_node<char> *> > meas_to_spec_node;
          
          //Raytheon portals do this wierd thing of putting two calibrations into
          //  one ChannelData node. lets find and fix this.  The pointer (second)
          //  will be to the second set of calibration coefficients.
          vector< pair<std::shared_ptr<Measurement>, vector<const rapidxml::xml_node<char> *>> > multiple_cals;
          
          const rapidxml::xml_node<char> *dose_data_parent = nullptr;
          if( XML_FIRST_NODE(det_data_node, "CountDoseData") )
            dose_data_parent = det_data_node;
          
          //Only N42 files that I saw in refMK252OLFFE use this next loop
          //  to initiate the N42DecodeHelper2006s
          for( const rapidxml::xml_node<char> *spectrum = xml_first_node_nso( det_data_node, "Spectrum", xmlns );
              spectrum;
              spectrum = XML_NEXT_TWIN(spectrum) )
          {
            std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
            measurements_this_node.push_back( meas );
            
            workerpool.post( N42DecodeHelper2006( spectrum, &queue_mutex,
                                                      meas, analysis_info, dose_data_parent,
                                                      document_node, energy_cal ) );
            
            if( spectrum->first_attribute( "CalibrationIDs", 14 ) )
              meas_to_spec_node.push_back( make_pair( meas, spectrum ) );
          }//for( loop over spectrums )
          
          
          //Most N42 files use the following loop
          for( const rapidxml::xml_node<char> *det_meas_node = xml_first_node_nso( det_data_node, "DetectorMeasurement", xmlns );
              det_meas_node;
              det_meas_node = XML_NEXT_TWIN(det_meas_node) )
          {
            //Look for <SpectrumMeasurement> measurements
            for( const rapidxml::xml_node<char> *spec_meas_node = xml_first_node_nso( det_meas_node, "SpectrumMeasurement", xmlns );
                spec_meas_node;
                spec_meas_node = XML_NEXT_TWIN(spec_meas_node) )
            {
              //each detector will have a <Spectrum> node (for each timeslice)
              
              for( const rapidxml::xml_node<char> *spectrum = xml_first_node_nso( spec_meas_node, "Spectrum", xmlns );
                  spectrum;
                  spectrum = XML_NEXT_TWIN(spectrum) )
              {
                std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
                measurements_this_node.push_back( meas );
                
                workerpool.post( N42DecodeHelper2006( spectrum, &queue_mutex,
                                                          meas, analysis_info, det_meas_node, document_node, energy_cal ) );
                
                if( XML_FIRST_ATTRIB(spectrum, "CalibrationIDs") )
                  meas_to_spec_node.push_back( make_pair( meas, spectrum ) );
                
                //The below handles a special case for Raytheon-Variant C-1/L-1 (see refSJHFSW1DZ4)
                //Raytheon portals (sometimes?) do a funky thing where they have both a 2.5MeV and a
                //  8 MeV scale - providing two sets of calibration coefficients under
                //  the SpectrumMeasurement->Spectrum->Calibration->Equation->Coefficients tag, and then placing the
                //  spectra, one after the other, in the <ChannelData>
                //  \TODO: double check that this logic altered 202020702 captures all cases
                const rapidxml::xml_node<char> *ray_specsize = XML_FIRST_NODE_CHECKED(spectrum, "ray:SpectrumSize");
                if( ray_specsize )
                {
                  const rapidxml::xml_node<char> *firstcal = XML_FIRST_NODE_CHECKED(spectrum, "Calibration");
                  const rapidxml::xml_node<char> *firsteqn = XML_FIRST_NODE_CHECKED(firstcal, "Equation");
                  const rapidxml::xml_node<char> *firstcoef = XML_FIRST_NODE_CHECKED(firsteqn  , "Coefficients");
                  const rapidxml::xml_node<char> *secondcoef = XML_NEXT_TWIN_CHECKED(firstcoef);
                  if( secondcoef && secondcoef->value_size() )
                    multiple_cals.push_back( std::make_pair(meas,
                      vector<const rapidxml::xml_node<char> *>{firstcal,ray_specsize}) );
                }//if( Raytheon <ray:SpectrumSize> node )
      
              }//for( rapidxml::xml_node<char> *spectrum = ... )
            }//for( const rapidxml::xml_node<char> *spec_meas_node ... )
            
            
            //now look for <GrossCountMeasurement> measurements
            for( const rapidxml::xml_node<char> *gross_count_meas = xml_first_node_nso( det_meas_node, "GrossCountMeasurement", xmlns );
                gross_count_meas;
                (gross_count_meas = XML_NEXT_TWIN(gross_count_meas)))
            {
              std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
              gross_counts_this_node.push_back( meas );
              workerpool.post( GrossCountNodeDecodeWorker( gross_count_meas, meas.get() ) );
            }//for( loop over CountDoseData nodes, dos )
            
            
            //This next section is probably not nesassary
            rapidxml::xml_attribute<char> *detector_attrib = det_meas_node->first_attribute( "Detector", 8 );
            if( detector_attrib && XML_VALUE_ICOMPARE(detector_attrib, "ORTEC Portal") )
            {
              detector_type_ = DetectorType::DetectiveEx200;
            }
          }//for( loop over <DetectorMeasurement> nodes under current <DetectorData> )
          
          workerpool.join();
          
          //Avid system in ref67CSUPJ531 has one Gamma, and one Nuetron
          //  DetectorMeasurement node for each <DetectorData> node, where the
          //  neutron detector isnt explicitly named, but it should clearly go
          //  with the gamma detector
          if( measurements_this_node.size() == gross_counts_this_node.size() )
          {
            for( size_t i = 0; i < measurements_this_node.size(); ++i )
            {
              std::shared_ptr<Measurement> &lhs = measurements_this_node[i];
              std::shared_ptr<Measurement> &rhs = gross_counts_this_node[i];
              if( rhs->detector_name_.empty()
                 || lhs->detector_name_ == rhs->detector_name_ )
              {
                lhs->neutron_counts_     = rhs->neutron_counts_;
                lhs->contained_neutron_  = rhs->contained_neutron_;
                lhs->neutron_counts_sum_ = rhs->neutron_counts_sum_;
                rhs.reset();
              }
            }//for( size_t i = 0; i < measurements_this_node.size(); ++i )
          }//if( measurements_this_node.size() == gross_counts_this_node.size() )
          
          
          N42DecodeHelper2006::filter_valid_measurements( measurements_this_node );
          
          
          //Now go through and look for Measurements with neutron data, but no
          //  gamma data, and see if can combine with onces that have gamma data...
          bool combined_gam_neut_spec = false;
          for( auto &neut : measurements_this_node )
          {
            if( !neut || !neut->contained_neutron_ || (neut->gamma_counts_ && neut->gamma_counts_->size()) )
              continue;
            
            for( const auto &gam : measurements_this_node )
            {
              if( !gam || gam == neut || gam->contained_neutron_ )
                continue;
              
              string gamdetname = gam->detector_name_;
              string::size_type intercal_pos = gamdetname.find("_intercal_");
              if( intercal_pos != string::npos )
                gamdetname = gamdetname.substr(0,intercal_pos);
              
              bool gamma_matches_neutron = (gamdetname == neut->detector_name_);
              if( !gamma_matches_neutron )
                gamma_matches_neutron = ((gamdetname+"N") == neut->detector_name_);
              
              //Some detectors will have names like "ChannelAGamma", "ChannelANeutron", "ChannelBGamma", "ChannelBNeutron", etc
              if( !gamma_matches_neutron && SpecUtils::icontains(gamdetname, "Gamma")
                 && SpecUtils::icontains(neut->detector_name_, "Neutron") )
              {
                //Note!: this next line alters gamdetname, so this should be the last test!
                SpecUtils::ireplace_all(gamdetname, "Gamma", "Neutron" );
                gamma_matches_neutron = SpecUtils::iequals_ascii(gamdetname, neut->detector_name_);
              }
              
              if( !gamma_matches_neutron )
                continue;
              
              //A couple basic checks
              
              //Note the real time for RadSeekers can easily be 0.45 seconds off
              if( neut->real_time_ > 0.0f && fabs(neut->real_time_ - gam->real_time_) > 1.0 )
                continue;
              
              //if( neut->live_time_ > 0.0f && fabs(neut->live_time_ - gam->live_time_) > 0.0001 )
              //continue;
              
              if( !is_special(neut->start_time_)
                 && !is_special(gam->start_time_)
                 && (neut->start_time_ != gam->start_time_) )
              {
                continue;
              }
              
              combined_gam_neut_spec = true;
              gam->neutron_counts_     = neut->neutron_counts_;
              gam->contained_neutron_  = neut->contained_neutron_;
              gam->neutron_counts_sum_ = neut->neutron_counts_sum_;
              
              //We should have a proper neutron real/live time fields, but until I add that, lets hack it.
              if( neut->real_time_ > 0.0f )
              {
                char buffer[128];
                snprintf( buffer, sizeof(buffer), "Neutron Real Time: %.5f s", neut->real_time_ );
                gam->remarks_.push_back( buffer );
              }//
              
              if( neut->live_time_ > 0.0f )
              {
                char buffer[128];
                snprintf( buffer, sizeof(buffer), "Neutron Live Time: %.5f s", neut->live_time_ );
                gam->remarks_.push_back( buffer );
              }//
              
              neut.reset();
              break;
            }//for( const auto &gam : measurements_this_node )
          }//for( const auto &neut : measurements_this_node )
          
          if( combined_gam_neut_spec )
          {
            vector< std::shared_ptr<Measurement> >::iterator begin, end, newend;
            begin = measurements_this_node.begin();
            end = measurements_this_node.end();
            newend = std::remove( begin, end, std::shared_ptr<Measurement>() );
            measurements_this_node.resize( newend - begin );
          }//if( combined_gam_neut_spec )
          
          
          if( gross_counts_this_node.size() )
          {
            //The Measurements in gross_counts might be just neutrons from
            //  detectors with the same name as a gamma detector, if
            //  so we need to combine measurements so ensure_unique_sample_numbers()
            //  wont go wonky
            std::lock_guard<std::mutex> lock( queue_mutex );
            
            for( auto &gross : gross_counts_this_node )
            {
              if( !gross
                 || !gross->contained_neutron_
                 || gross->gamma_count_sum_ > 0.000001 )
                continue;
              
              bool gross_used = false;
              for( auto &spec : measurements_this_node )
              {
                
                bool gamma_matches_neutron = (spec->detector_name_ == gross->detector_name_);
                if( !gamma_matches_neutron )
                  gamma_matches_neutron = ((spec->detector_name_+"N") == gross->detector_name_);
                
                //Some detectors will have names like "ChannelAGamma", "ChannelANeutron",
                //   "ChannelBGamma", "ChannelBNeutron", etc.
                if( !gamma_matches_neutron && SpecUtils::icontains(spec->detector_name_, "Gamma")
                   && SpecUtils::icontains(gross->detector_name_, "Neutron") )
                {
                  string gamdetname = spec->detector_name_;
                  SpecUtils::ireplace_all(gamdetname, "Gamma", "Neutron" );
                  gamma_matches_neutron = SpecUtils::iequals_ascii(gamdetname, gross->detector_name_);
                  
                  //TODO: should probably get rid of "Gamma" in the detector name
                  //  now that we have combined them but I havent evaluated the
                  //  effect of this yet...
                  //if( gamma_matches_neutron )
                  //  SpecUtils::ireplace_all( spec->detector_name_, "Gamma" );
                }
                
                if( !gamma_matches_neutron )
                  continue;
                
                if( spec->contained_neutron_
                   && (spec->neutron_counts_ != gross->neutron_counts_) )
                {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
                  log_developer_error( __func__,  "Warning: confusing gross count situation" );
#endif
                  continue;
                }//if( spec->neutron_counts_sum_ > 0.000001 )
                
                spec->neutron_counts_     = gross->neutron_counts_;
                spec->contained_neutron_  = gross->contained_neutron_;
                spec->neutron_counts_sum_ = gross->neutron_counts_sum_;
                gross_used = true;
                
                //Gamma data may be represented by multiple spectra, so we have to
                //  loop over all measurements_this_node and not just assume one
                //  match, so dont break once we found a match
              }//for( auto &spectru : measurements_this_node )
              
              if( gross_used )
                gross.reset();  //we dont want this measurement anymore
            }//for( auto &gross : gross_count_meas )
            
            for( const auto &gross : gross_counts_this_node )
            {
              if( gross )
                measurements_this_node.push_back( gross );
            }//for( const auto &gross : gross_counts_this_node )
          }//if( gross_counts_this_node.size() )
          
          N42DecodeHelper2006::set_n42_2006_detector_data_node_info( det_data_node, measurements_this_node );
          
          for( size_t multical_index = 0; multical_index < multiple_cals.size(); ++multical_index )
          {
            std::shared_ptr<Measurement> &meas = multiple_cals[multical_index].first;
            const vector<const rapidxml::xml_node<char> *> &nodes = multiple_cals[multical_index].second;
            assert( nodes.size() == 2 );
            const rapidxml::xml_node<char> * const cal_node = nodes[0];
            const rapidxml::xml_node<char> * const ray_size = nodes[1];
      
            if( !cal_node )  //This shouldnt happen, but check anyway
              continue;
      
            if( find( measurements_this_node.begin(), measurements_this_node.end(), meas )
                == measurements_this_node.end() )
            {
#if( PERFORM_DEVELOPER_CHECKS  && !SpecUtils_BUILD_FUZZING_TESTS )
              log_developer_error( __func__,  "Got Measurement not in measurements_this_node" );
#endif
              continue; //shouldnt ever happen
            }
            
            size_t num_lower_bins = 1024;
            if( ray_size )
            {
              int value = 0;
              if( toInt(xml_value_str(ray_size), value) && value>0 )
              num_lower_bins = static_cast<size_t>(value);
            }
      
            //Sanity check to make sure doing this matches the example data I have
            //  where this bizare fix is needed - this should probably be loosend up to
            //  work whenever <ray:SpectrumSize> is seen with a value that divides the spectrum size
            //  evenly.
            //  \TODO: could loosen things up a bit and allow an arbitrary number of calibrations
            const size_t norig_channel = meas->gamma_counts_ ? meas->gamma_counts_->size() : size_t(0);
            if( norig_channel && (norig_channel != (2*num_lower_bins)) )
            {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
              const string msg = "Found multiple calibrations, but size ("
                                 + to_string(num_lower_bins) + ") wasnt 1/2 of channel size ("
                                 + to_string(norig_channel) + ")";
              log_developer_error( __func__,  msg.c_str() );
#endif
              continue;
            }//if( number of channels dont match up )
      
            if( (num_lower_bins + 1) >= norig_channel )
            {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
              const string msg = "Found multiple calibrations, but size ("
              + to_string(num_lower_bins) + ") was larger than channel size ("
              + to_string(norig_channel) + ")";
              log_developer_error( __func__,  msg.c_str() );
#endif
              continue;
            }//if( we cant split the bins up )
            
            std::shared_ptr<const vector<float> > oldcounts = meas->gamma_counts_;
            auto lowerbins = make_shared<vector<float> >( oldcounts->begin(), oldcounts->begin()+num_lower_bins );
            auto upperbins = make_shared<vector<float> >( oldcounts->begin()+num_lower_bins, oldcounts->end() );
            
            std::shared_ptr<Measurement> newmeas = std::make_shared<Measurement>(*meas);
            
            meas->gamma_counts_ = lowerbins;
            meas->gamma_count_sum_ = 0.0;  //std::accumulate( begin(*lowerbins), end(*lowerbins), (double)0.0 );
            for( const float a : *lowerbins )
              meas->gamma_count_sum_ += a;
            
            newmeas->gamma_counts_ = upperbins;
            newmeas->gamma_count_sum_ = 0.0;
            for( const float a : *upperbins )
              newmeas->gamma_count_sum_ += a;
          
            string lower_cal_err, upper_cal_err;
            shared_ptr<const EnergyCalibration> lower_cal, upper_cal;
      
            energy_cal.get_calibration_energy_cal( cal_node, num_lower_bins,
                                                   meas->detector_name_, 0, lower_cal, lower_cal_err );
            energy_cal.get_calibration_energy_cal( cal_node, upperbins->size(),
                                                   meas->detector_name_, 1, upper_cal, upper_cal_err );
    
            if( lower_cal )
            {
              meas->energy_calibration_ = lower_cal;
            }else
            {
              meas->energy_calibration_ = make_shared<EnergyCalibration>();
              if( !lower_cal_err.empty() )
                meas->parse_warnings_.push_back( lower_cal_err );
            }//if( lower_cal ) / else
      
            if( upper_cal )
            {
              newmeas->energy_calibration_ = upper_cal;
            }else
            {
              newmeas->energy_calibration_ = make_shared<EnergyCalibration>();
              if( !upper_cal_err.empty() )
                newmeas->parse_warnings_.push_back( upper_cal_err );
            }//if( upper_cal ) / else
      
            //We will re-name the detectors based on the energy range... not a universal solution
            //  but works with the files I've seen that do this
            string lower_name = multical_name_append[0], upper_name = multical_name_append[1];
            auto make_MeV_name = []( const shared_ptr<const EnergyCalibration> &cal, string &name ){
              assert( cal );
              if( cal->type() == EnergyCalType::InvalidEquationType )
                return;
              assert( cal->channel_energies() && !cal->channel_energies()->empty() );
              const double energy = cal->channel_energies()->back();
              
              //Convert to MeV, rounding to nearest MeV if above 4 MeV, or nearest 1/4 if below
              string newname;
              if( energy > 4000 )
                newname = std::to_string( std::round(energy/1000) );
              else
                newname = std::to_string( std::round(energy/500) / 2 );
      
              while( !newname.empty() && (newname.back()=='0' || newname.back()=='.') )
                newname = newname.substr(0,newname.size()-1);
              if( !newname.empty() )
                name = newname + "MeV";
            };//make_MeV_name lamnda
        
            if( multical_name_append[0].empty() )
             make_MeV_name( meas->energy_calibration_, multical_name_append[0] );
            if( multical_name_append[1].empty() )
              make_MeV_name( newmeas->energy_calibration_, multical_name_append[1] );
            
            if( multical_name_append[0] == multical_name_append[1] )
            {
              multical_name_append[0] = "a";
              multical_name_append[1] = "b";
            }
      
            meas->detector_name_ += "_intercal_" + multical_name_append[0];
            newmeas->detector_name_ += "_intercal_" + multical_name_append[1];
              
            measurements_this_node.push_back( newmeas );
          }//for( multiple_cals )
          
          //Make sure if any of the spectrum had the <SourceType> tag, but some
          //  others didnt, we propogate this info to them.  This notably effects
          //  rad assist detectors
          SourceType sourcetype = SourceType::Unknown;
          for( auto &m : measurements_this_node )
          {
            if( !m ) continue;
            if( sourcetype == SourceType::Unknown )
              sourcetype = m->source_type_;
            else if( m->source_type_ != SourceType::Unknown )
              sourcetype = max( sourcetype, m->source_type_ );
          }//for( auto &m : measurements_this_node )
          
          for( auto &m : measurements_this_node )
          {
            if( m && (m->source_type_ == SourceType::Unknown) )
              m->source_type_ = sourcetype;
          }//for( auto &m : measurements_this_node )
          
          //Look for multiple spectra representing the same data, but that actually
          // have different calibrations.
          // See comments for #energy_cal_variants and #keep_energy_cal_variants.
          const vector< std::shared_ptr<Measurement> >::const_iterator datastart = measurements_this_node.begin();
          const vector< std::shared_ptr<Measurement> >::const_iterator dataend = measurements_this_node.end();
          
          for( size_t i = 1; i < meas_to_spec_node.size(); ++i )
          {
            std::shared_ptr<Measurement> &meas = meas_to_spec_node[i].first;
            
            //This check to make sure the Measurement hasnt been discarded is
            //  probably most costly than worth it.
            if( std::find(datastart,dataend,meas) == dataend )
              continue;
            
            vector< pair<std::shared_ptr<Measurement>, const rapidxml::xml_node<char> *> > samenames;
            for( size_t j = 0; j < i; ++j )
            {
              std::shared_ptr<Measurement> &innermeas = meas_to_spec_node[j].first;
              
              if( innermeas->detector_name_ == meas->detector_name_
                 && innermeas->start_time_ == meas->start_time_
                 && fabs(innermeas->real_time_ - meas->real_time_) < 0.001
                 && fabs(innermeas->live_time_ - meas->live_time_) < 0.001 )
              {
                samenames.push_back( meas_to_spec_node[j] );
              }
            }//for( size_t j = 0; j < i; ++j )
            
            if( samenames.size() )
            {
              const rapidxml::xml_node<char> *spec = meas_to_spec_node[i].second;
              const rapidxml::xml_attribute<char> *cal = spec->first_attribute( "CalibrationIDs", 14 );
              meas->detector_name_ += "_intercal_" + xml_value_str( cal );
              for( size_t j = 0; j < samenames.size(); ++j )
              {
                spec = samenames[j].second;
                cal = spec->first_attribute( "CalibrationIDs", 14 );
                samenames[j].first->detector_name_ += "_intercal_" + xml_value_str( cal );
              }
            }//if( samenames.size() )
          }//for( size_t i = 1; i < meas_to_spec_node.size(); ++i )
          
          for( auto &meas : measurements_this_node )
          {
            measurements_.push_back( meas );
            added_measurements.push_back( meas );
          }//for( auto &meas : measurements_this_node )
        }//for( loop over <DetectorData> nodes )
        
        
        //I'm a bad person and am allowing ICD2 files to get to here...  I guess I could
        //  claim code reuse or something, but really its laziness to allow parsing of
        //  ICD2 files for someone when I dont have enough time to properly implement
        //  parsing.
        //  I think this one loop is the only specialization of code to allow this
        //  IF were gonna keep this around, should parallelize the parsing, similar to the above loops
        for( const rapidxml::xml_node<char> *icd2_ana_res = xml_first_node_nso( measurement, "AnalysisResults", "dndoarns:" );
            icd2_ana_res;
            icd2_ana_res = XML_NEXT_TWIN(icd2_ana_res) )
        {
          for( const rapidxml::xml_node<char> *gamma_data = xml_first_node_nso( icd2_ana_res, "AnalyzedGammaData", "dndoarns:" );
              gamma_data;
              gamma_data = XML_NEXT_TWIN(gamma_data) )
          {
            vector< std::shared_ptr<Measurement> > measurements_this_node;
            vector<const rapidxml::xml_node<char> *> spectrum_nodes;
            
            const rapidxml::xml_node<char> *node = xml_first_node_nso( gamma_data, "BackgroundSpectrum", "dndoarns:" );
            if( node )
            {
              std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
              spectrum_nodes.push_back( node );
              measurements_this_node.push_back( meas );
              workerpool.post( N42DecodeHelper2006( node, &queue_mutex, meas, analysis_info, NULL, document_node, energy_cal ) );
            }
            
            for( node = xml_first_node_nso( gamma_data, "SpectrumSummed", "dndoarns:" );
                node; node = XML_NEXT_TWIN(node) )
            {
              std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
              spectrum_nodes.push_back( node );
              measurements_this_node.push_back( meas );
              workerpool.post( N42DecodeHelper2006( node, &queue_mutex, meas, analysis_info, NULL, document_node, energy_cal ) );
            }//for( loop over <SpectrumSummed> )
            
            workerpool.join();
            
            for( size_t i = 0; i < measurements_this_node.size(); ++i )
            {
              //<dndoarns:SpectrumSummed dndoarns:DetectorSubset="Partial" dndoarns:NuclidesIdentified="Th-232-001 Ra-226-002">
              std::shared_ptr<Measurement> meas = measurements_this_node[i];
              vector<std::shared_ptr<Measurement> > measv( 1, meas );
              N42DecodeHelper2006::filter_valid_measurements( measv );
              
              if( measv.empty() )
                continue;
              
              string name = xml_name_str(spectrum_nodes[i]);
              if( SpecUtils::icontains(name, "BackgroundSpectrum" ) )
              {
                meas->title_ += " " + string("Background");
                
                //Check if calibration is valid, and if not, fill it in from the next spectrum...
                //  seems a little sketch but works for all cases Ive encountered.
                assert( meas->energy_calibration_ );
                if( (meas->energy_calibration_->type() == EnergyCalType::InvalidEquationType)
                   && (i < (measurements_this_node.size()-1))
                   && measurements_this_node[i+1]
                   && measurements_this_node[i+1]->gamma_counts_
                   && meas->gamma_counts_
                   && !meas->gamma_counts_->empty()
                   && (measurements_this_node[i+1]->gamma_counts_->size() == meas->gamma_counts_->size())
                   && (measurements_this_node[i+1]->energy_calibration_->type() != EnergyCalType::InvalidEquationType) )
                {
                  meas->energy_calibration_ = measurements_this_node[i+1]->energy_calibration_;
                }
              }//if( SpecUtils::icontains(name, "BackgroundSpectrum" ) )
              
              const rapidxml::xml_attribute<char> *nuclides_att = XML_FIRST_ATTRIB( spectrum_nodes[i], "dndoarns:NuclidesIdentified" );
              const string nucstr = xml_value_str( nuclides_att );  //ex "Th-232-001 Ra-226-002"
              //vector<string> nuclides;
              //SpecUtils::split( nuclides, nucstr, " \t\n," );
              if( nucstr.size() )
                meas->title_ +=  " Nuclides Reported: " + nucstr + ".";
              
              string detectors;
              map<string,string> det_to_sequence;
              for( const rapidxml::xml_node<char> *subset = xml_first_node_nso( spectrum_nodes[i], "SubsetSampleList", "dndoarns:" );
                  subset;
                  subset = XML_NEXT_TWIN(subset) )
              {
                //<dndoarns:SubsetSampleList Detector="Aa4">299 300 301 302 303 304 305</dndoarns:SubsetSampleList>
                const rapidxml::xml_attribute<char> *det_att = XML_FIRST_ATTRIB(subset, "Detector");
                const string detname = xml_value_str(det_att);
                
                if( detname.size() )
                  detectors += (detectors.size() ? ", " : "") + detname;
                
                const string value = xml_value_str( subset );
                if( subset && subset->value_size() )
                {
                  vector<int> samples;
                  if( SpecUtils::split_to_ints( subset->value(), subset->value_size(), samples ) )
                  {
                    const set<int> samplesset( samples.begin(), samples.end() );
                    const string sequence = SpecUtils::sequencesToBriefString( samplesset );
                    if( sequence.size() )
                      det_to_sequence[detname] = sequence;
                  }else
                  {
                    string value = xml_value_str( subset );
                    if( value.size() )
                      det_to_sequence[detname] = value;
                    
                  }
                }//if( subset && subset->value_size() )
              }//for( loop over <SubsetSampleList> nodes )
              
              bool all_same = !det_to_sequence.empty();
              for( map<string,string>::const_iterator i = det_to_sequence.begin();
                  all_same && i != det_to_sequence.end(); ++i )
              {
                all_same = (i->second==det_to_sequence.begin()->second);
              }
              
              if( all_same )
              {
                meas->remarks_.push_back( "SampleNumbers: " + det_to_sequence.begin()->second );
              }else
              {
                for( map<string,string>::const_iterator i = det_to_sequence.begin();
                    i != det_to_sequence.end(); ++i )
                  meas->remarks_.push_back( "Detector " + i->first + " SampleNumbers: " + i->second );
              }
              
              if( detectors.size() )
                meas->title_ += " Detectors: " + detectors + ". ";
              
              SpecUtils::trim( meas->title_ );
              
              measurements_.push_back( meas );
              added_measurements.push_back( meas );
            }//for( loop over <SubsetSampleList> )
          }//for( loop over <AnalyzedGammaData> )
        }//for( loop over <AnalysisResults> in ICD2 file )
        
        
        
        
        const rapidxml::xml_node<char> *info_node = xml_first_node_nso( measurement, "InstrumentInformation", xmlns );
        set_n42_2006_instrument_info_node_info( info_node );
        
        //Try to find the MeasuredItemInformation node - this is all just barely hacked in
        const rapidxml::xml_node<char> *item_info_node = xml_first_node_nso( measurement, "MeasuredItemInformation", xmlns );
        if( !item_info_node && info_node )
          item_info_node = xml_first_node_nso( info_node, "MeasuredItemInformation", xmlns );
        if( !item_info_node ) //HPRDS
          item_info_node = xml_first_node_nso( measurement, "InstrumentLocation", xmlns );
        
        set_n42_2006_measurement_location_information( item_info_node, added_measurements );
        
        const rapidxml::xml_node<char> *analysis_node = xml_first_node_nso( measurement, "AnalysisResults", xmlns );
        
        if( !analysis_node )
        {
          // RadSeeker stores its results information in Event->AnalysisResults->RadiationDataAnalysis
          //  Which has a field <SpectrumID>...<SpectrumID> that should match the
          //  SpectrumID attribute of DetectorData->Spectrum.  Lets try to find this
          //  analysis information
          const rapidxml::xml_node<char> *DetectorData = xml_first_node_nso( measurement, "DetectorData", xmlns );
          const rapidxml::xml_node<char> *Spectrum = xml_first_node_nso( DetectorData, "Spectrum", xmlns );
          const rapidxml::xml_attribute<char> *SpectrumIDAttrib = Spectrum ? XML_FIRST_ATTRIB(Spectrum, "SpectrumID") : 0;
          
          if( SpectrumIDAttrib && SpectrumIDAttrib->value_size() )
          {
            const rapidxml::xml_node<char> *AnalysisResults = xml_first_node_nso( document_node->parent(), "AnalysisResults", xmlns );
            const rapidxml::xml_node<char> *RadiationDataAnalysis = xml_first_node_nso( AnalysisResults, "RadiationDataAnalysis", xmlns );
            for( const rapidxml::xml_node<char> *SpectrumID = xml_first_node_nso( RadiationDataAnalysis, "SpectrumID", xmlns );
                SpectrumID && !analysis_node;
                SpectrumID = XML_NEXT_TWIN(SpectrumID) )
            {
              if( rapidxml::internal::compare(SpectrumID->value(), SpectrumID->value_size(),
                                              SpectrumIDAttrib->value(), SpectrumIDAttrib->value_size(), false) )
                analysis_node = RadiationDataAnalysis;
            }
          }//if( SpectrumID && SpectrumID->value_size() )
        }//if( !analysis_node )
        
        if( analysis_node && analysis_info )  //analysis_info should always be valid
          set_analysis_info_from_n42( analysis_node, *analysis_info );
        
        
        //The identiFINDER has its neutron info in a <CountDoseData> node under the <Measurement> node
        bool haveUsedNeutMeas = false;
        for( const rapidxml::xml_node<char> *count_dose_data_node = xml_first_node_nso( measurement, "CountDoseData", xmlns );
            count_dose_data_node;
            count_dose_data_node = XML_NEXT_TWIN(count_dose_data_node) )
        {
          const rapidxml::xml_attribute<char> *dettype = XML_FIRST_ATTRIB(count_dose_data_node, "DetectorType");
          if( !dettype || !XML_VALUE_ICOMPARE(dettype, "Neutron") )
            continue;
          
          const rapidxml::xml_node<char> *starttime_node  = xml_first_node_nso( count_dose_data_node, "StartTime", xmlns );
          const rapidxml::xml_node<char> *realtime_node   = xml_first_node_nso( count_dose_data_node, "SampleRealTime", xmlns );
          const rapidxml::xml_node<char> *counts_node     = xml_first_node_nso( count_dose_data_node, "Counts", xmlns );
          const rapidxml::xml_node<char> *count_rate_node = xml_first_node_nso( count_dose_data_node, "CountRate", xmlns ); //Only seen in RadEagle files so far
          const rapidxml::xml_node<char> *remark_node     = xml_first_node_nso( count_dose_data_node, "Remark", xmlns );
          
          const bool has_counts_node = (counts_node && counts_node->value_size());
          const bool has_count_rate_node = (count_rate_node && count_rate_node->value_size());
          
          if( !starttime_node || !starttime_node->value_size()
             || !realtime_node || !realtime_node->value_size()
             || !(has_counts_node || has_count_rate_node) )
            continue;
          
          
          if( remark_node )
          {
            //RadEagle files contain three <CountDoseData> elements that
            //  each have a single <remark> element with values of "Minimum",
            //  "Average", or "Maximum".  These all give CountRate (not
            //  dose, or total counts), so lets only take the "Average" one
            //  20171218: I have only seen files with zero neutron counts, so this
            //            is all likely not yet correct (example measurements that
            //            did contain neutrons in SPE file, didnt have them in the
            //            N42 file, so something may be odd).
            if( XML_VALUE_ICOMPARE(remark_node,"Minimum") )
              continue;
            
            if( XML_VALUE_ICOMPARE(remark_node,"Maximum")  )
            {
              if( !has_count_rate_node )
                continue;
              
              const string units = xml_value_str( XML_FIRST_IATTRIB(count_rate_node, "Units") );
              if( haveUsedNeutMeas || units != "CPS" )
                continue;
            }//if( maximum says MAXIMUM )
          }//if( remark_node )
          
          float counts;
          if( has_counts_node )
          {
            if( !xml_value_to_flt( counts_node, counts ) )
              continue;
          }else
          {
            if( !xml_value_to_flt( count_rate_node, counts ) )
              continue;
          }
          
          //
          const time_point_t start_time = time_from_string( xml_value_str(starttime_node).c_str() );
          if( is_special(start_time) )
          {
            //Just because we failed to parse the start time doesnt mean we shouldnt
            //  parse the neutron information - however, we need to know which gamma
            //  spectra to add the neutron info to.  So we'll compromise and if
            //  we can unambiguously pair the information, despite not parsing dates
            //  we'll do it - I really dont like the brittleness of all of this!
            size_t nspectra = 0;
            for( const auto &meas : added_measurements )
              nspectra += (meas && (meas->source_type_ != SourceType::IntrinsicActivity) && is_special(meas->start_time_) );
            if( nspectra != 1 )
              continue;
          }
          
          const float realtimesec = time_duration_string_to_seconds( realtime_node->value(), realtime_node->value_size() );
          
          if( !has_counts_node && realtimesec > 0.0 )
            counts *= realtimesec;
          
          for( auto &meas : added_measurements )
          {
            if( !meas || meas->contained_neutron_
               || meas->start_time_ != start_time )
              continue;
            
            if( fabs(realtimesec - meas->real_time_) >= 1.0 )
              continue;
            
            haveUsedNeutMeas = true;
            meas->contained_neutron_ = true;
            meas->neutron_counts_.resize( 1 );
            meas->neutron_counts_[0] = counts;
            meas->neutron_counts_sum_ = counts;
            break;
          }
        }//for( loop over <CountDoseData> nodes )
        
      }//for( loop over all measurements )
    }//if( is_spectrometer ) / else
    
    
    //Some HPRDS files will have Instrument information right below the document node.
    //  For example see refR96VUZSYYM
    //  (I dont think checking here will ever overide info found for a specific
    //  spectrum, but not totally sure, so will leave in a warning, which can be taken out after running the tests)
    {
      const rapidxml::xml_node<char> *info_node = xml_first_node_nso( document_node, "InstrumentInformation", xmlns );
      if( info_node )
      {
        if( instrument_type_.size() )
        {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
          log_developer_error( __func__,  "SpecFile::load_2006_N42_from_doc(): may be overwriting InstrumentInformation already gathered from a specific spectrum" );
#endif
        }
        set_n42_2006_instrument_info_node_info( info_node );
      }
    }
    
    for( const rapidxml::xml_node<char> *remark = xml_first_node_nso( document_node, "Remark", xmlns );
        remark;
        remark = XML_NEXT_TWIN(remark) )
    {
      string str = xml_value_str( remark );
      trim( str );
      
      if( SpecUtils::istarts_with( str, s_parser_warn_prefix) )
      {
        SpecUtils::ireplace_all( str, s_parser_warn_prefix, "" );
        parse_warnings_.emplace_back( std::move(str) );
      }else if( !str.empty() )
      {
        remarks_.push_back( str );
      }
    }//for( loop over remarks )
    
    
    if( analysis_info && analysis_info->results_.size() )
      detectors_analysis_ = analysis_info;
    
    // Do a instrument specific have to fix CPS --> counts
    if( icontains(instrument_type_,"SpecPortal")
             && icontains(manufacturer_,"SSC Pacific")
             && icontains(instrument_model_,"MPS Pod") )
    {
      //Gamma spectrum is in CPS, so multiply each spectrum by live time.
      //  Note that there is no indication of this in the file, other than
      //  fractional counts
      for( auto &m : measurements_ )
      {
        if( !m || (m->live_time_ < 1.0f) )  //1 second is arbitrary
          continue;
        
        //We could probably add some count rate sanity check here too.
        
        if( m->contained_neutron_ )
        {
          for( auto &f : m->neutron_counts_ )
            f *= m->live_time_;
          m->neutron_counts_sum_ *= m->live_time_;
        }
        
        if( m->gamma_counts_ )
        {
          m->gamma_count_sum_ = 0.0;
          for( auto &f : const_cast<vector<float>&>(*m->gamma_counts_) )  //We know this is safe right here.
          {
            f *= m->live_time_;
            m->gamma_count_sum_ += f;
          }
        }
        if( m->gamma_counts_ || m->contained_neutron_ )
          m->remarks_.push_back( "Gamma/Neutron counts have been mutliplied by live time, "
                                "to account for observed shortcommings of this detectors N42-2006 format." );
      }//for( auto &m : measurements_ )
    }//if( SSC Pacific that need to
    
    if( measurements_.empty() )
      throw runtime_error( "No Measurements found inside ICD1/XML file" );
    
    cleanup_after_load();
  }//bool load_2006_N42_from_doc( rapidxml::xml_node<char> *document_node )

  
  
  

  
  
  
  
  void add_analysis_results_to_2012_N42(
                                        const DetectorAnalysis &ana,
                                        ::rapidxml::xml_node<char> *RadInstrumentData,
                                        std::mutex &xmldocmutex )
  {
    /* The relevant part of the N42 XSD (https://www.nist.gov/file/42636, or
     https://www.nist.gov/programs-projects/ansiieee-n4242-standard)
     <xsd:complexType name="AnalysisResultsType">
     <xsd:annotation>
     <xsd:documentation>A data type to provide information on the results of a radiation data analysis.</xsd:documentation>
     <xsd:appinfo>
     </xsd:appinfo>
     </xsd:annotation>
     <xsd:complexContent>
     <xsd:extension base="n42:OptIdComplexObjectType">
     <xsd:sequence>
     <xsd:element ref="n42:AnalysisStartDateTime" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:AnalysisComputationDuration" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:AnalysisAlgorithmName" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:AnalysisAlgorithmCreatorName" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:AnalysisAlgorithmDescription" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:AnalysisAlgorithmVersion" minOccurs="0" maxOccurs="unbounded"/>
     <xsd:element ref="n42:AnalysisAlgorithmSetting" minOccurs="0" maxOccurs="unbounded"/>
     <xsd:element ref="n42:AnalysisResultStatusCode" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:AnalysisConfidenceValue" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:AnalysisResultDescription" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:RadAlarm" minOccurs="0" maxOccurs="unbounded"/>
     <xsd:element ref="n42:NuclideAnalysisResults" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:SpectrumPeakAnalysisResults" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:GrossCountAnalysisResults" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:DoseAnalysisResults" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:ExposureAnalysisResults" minOccurs="0" maxOccurs="1"/>
     <xsd:element ref="n42:Fault" minOccurs="0" maxOccurs="unbounded"/>
     <xsd:element ref="n42:AnalysisResultsExtension" minOccurs="0" maxOccurs="unbounded"/>
     </xsd:sequence>
     <xsd:attribute name="radMeasurementGroupReferences" type="xsd:IDREFS" use="optional">
     <xsd:annotation>
     <xsd:documentation>Identifies the RadMeasurementGroup element(s) within the N42 XML document that applies to this particular analysis. There shall be no duplicate IDREF values in the list.</xsd:documentation>
     </xsd:annotation>
     </xsd:attribute>
     <xsd:attribute name="derivedDataReferences" type="xsd:IDREFS" use="optional">
     <xsd:annotation>
     <xsd:documentation>Identifies the DerivedData element(s) within the N42 XML document that applies to this particular analysis. There shall be no duplicate IDREF values in the list.</xsd:documentation>
     </xsd:annotation>
     </xsd:attribute>
     <xsd:attribute name="radMeasurementReferences" type="xsd:IDREFS" use="optional">
     <xsd:annotation>
     <xsd:documentation>Identifies the RadMeasurement element(s) within the N42 XML document that applies to a particular analysis. There shall be no duplicate IDREF values in the list.</xsd:documentation>
     </xsd:annotation>
     </xsd:attribute>
     </xsd:extension>
     </xsd:complexContent>
     </xsd:complexType>
     */
    using namespace rapidxml;
    const char *val = 0;
    char buffer[512];
    xml_attribute<> *attr = 0;
    
    ::rapidxml::xml_document<char> *doc = RadInstrumentData->document();
    
    assert( doc );
    if( !doc )
      throw runtime_error( "add_analysis_results_to_2012_N42: failed to get xml document." );
    
    xml_node<char> *AnalysisResults = nullptr;
    
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      AnalysisResults = doc->allocate_node( node_element, "AnalysisResults" );
      RadInstrumentData->append_node( AnalysisResults );
    }
    
  
    for( const string &remark : ana.remarks_ )
    {
      if( remark.size() )
      {
        std::lock_guard<std::mutex> lock( xmldocmutex );
        val = doc->allocate_string( remark.c_str(), remark.size()+1 );
        xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
        AnalysisResults->append_node( remark );
      }
    }//for( loop over remarks )
    
    
    if( !is_special(ana.analysis_start_time_) )
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      const string dt = SpecUtils::to_extended_iso_string(ana.analysis_start_time_) + "Z";
      val = doc->allocate_string( dt.c_str(), dt.size()+1 );
      xml_node<char> *desc = doc->allocate_node( node_element, "AnalysisStartDateTime", val );
      AnalysisResults->append_node( desc );
    }
    
    if( ana.analysis_computation_duration_ > FLT_EPSILON )
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      snprintf( buffer, sizeof(buffer), "PT%fS", ana.analysis_computation_duration_ );
      val = doc->allocate_string( buffer );
      xml_node<char> *desc = doc->allocate_node( node_element, "AnalysisComputationDuration", val );
      AnalysisResults->append_node( desc );
    }
    
    if( ana.algorithm_name_.size() )
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      val = doc->allocate_string( ana.algorithm_name_.c_str(), ana.algorithm_name_.size()+1 );
      xml_node<char> *name = doc->allocate_node( node_element, "AnalysisAlgorithmName", val );
      AnalysisResults->append_node( name );
    }
    
    if( ana.algorithm_creator_.size() )
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      val = doc->allocate_string( ana.algorithm_creator_.c_str(), ana.algorithm_creator_.size()+1 );
      xml_node<char> *creator = doc->allocate_node( node_element, "AnalysisAlgorithmCreatorName", val );
      AnalysisResults->append_node( creator );
    }
    
    if( ana.algorithm_description_.size() )
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      val = doc->allocate_string( ana.algorithm_description_.c_str(), ana.algorithm_description_.size()+1 );
      xml_node<char> *desc = doc->allocate_node( node_element, "AnalysisAlgorithmDescription", val );
      AnalysisResults->append_node( desc );
    }
        
    for( const auto &component : ana.algorithm_component_versions_ )
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      xml_node<char> *version = doc->allocate_node( node_element, "AnalysisAlgorithmVersion" );
      AnalysisResults->append_node( version );
      
      string compname = component.first;
      if( compname.empty() )
        compname = "main";
      
      val = doc->allocate_string( compname.c_str(), compname.size()+1 );
      xml_node<char> *component_name = doc->allocate_node( node_element, "AnalysisAlgorithmComponentName", val );
      version->append_node( component_name );
      
      val = doc->allocate_string( component.second.c_str(), component.second.size()+1 );
      xml_node<char> *component_version = doc->allocate_node( node_element, "AnalysisAlgorithmComponentVersion", val );
      version->append_node( component_version );
    }//
    
    //<AnalysisAlgorithmSetting>
    //<AnalysisResultStatusCode>
    //<AnalysisConfidenceValue>
    
    if( ana.algorithm_result_description_.size() )
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      val = doc->allocate_string( ana.algorithm_result_description_.c_str(), ana.algorithm_result_description_.size()+1 );
      xml_node<char> *desc = doc->allocate_node( node_element, "AnalysisResultDescription", val );
      AnalysisResults->append_node( desc );
    }
    
    //<RadAlarm>
    //<NuclideAnalysisResults>
    //<SpectrumPeakAnalysisResults>
    //<GrossCountAnalysisResults>
    //<DoseAnalysisResults>
    //<ExposureAnalysisResults>
    //<Fault>
    
    xml_node<char> *result_node = 0;
    
    for( const DetectorAnalysisResult &result : ana.results_ )
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      
      if( result.nuclide_.size() )
      {
        if( !result_node )
        {
          result_node = doc->allocate_node( node_element, "NuclideAnalysisResults" );
          AnalysisResults->append_node( result_node );
        }
        
        xml_node<char> *nuclide_node = doc->allocate_node( node_element, "Nuclide" );
        result_node->append_node( nuclide_node );
        
        if( result.remark_.size() )
        {
          val = doc->allocate_string( result.remark_.c_str(), result.remark_.size()+1 );
          xml_node<char> *Remark = doc->allocate_node( node_element, "Remark", val );
          nuclide_node->append_node( Remark );
        }
        
          //<xsd:element ref="n42:NuclideIdentifiedIndicator" minOccurs="1" maxOccurs="1"/>
          //<xsd:element ref="n42:NuclideName" minOccurs="1" maxOccurs="1"/>
          //<xsd:element ref="n42:NuclideActivityValue" minOccurs="0" maxOccurs="1"/>
          //<xsd:element ref="n42:NuclideActivityUncertaintyValue" minOccurs="0" maxOccurs="1"/>
          //<xsd:element ref="n42:NuclideMinimumDetectableActivityValue" minOccurs="0" maxOccurs="1"/>
          //<xsd:element ref="n42:NuclideIdentificationConfidence" minOccurs="1" maxOccurs="3"/>
          //<xsd:element ref="n42:NuclideCategoryDescription" minOccurs="0" maxOccurs="1"/>
          //<xsd:element ref="n42:NuclideSourceGeometryCode" minOccurs="0" maxOccurs="1"/>
          //<xsd:element ref="n42:SourcePosition" minOccurs="0" maxOccurs="1"/>
          //<xsd:element ref="n42:NuclideShieldingAtomicNumber" minOccurs="0" maxOccurs="1"/>
          //<xsd:element ref="n42:NuclideShieldingArealDensityValue" minOccurs="0" maxOccurs="1"/>
          //<xsd:element ref="n42:NuclideExtension" minOccurs="0" maxOccurs="1"/>
        
        xml_node<char> *NuclideIdentifiedIndicator = doc->allocate_node( node_element, "NuclideIdentifiedIndicator", "true" );
        nuclide_node->append_node( NuclideIdentifiedIndicator );
        
        
        val = doc->allocate_string( result.nuclide_.c_str(), result.nuclide_.size()+1 );
        xml_node<char> *NuclideName = doc->allocate_node( node_element, "NuclideName", val );
        nuclide_node->append_node( NuclideName );
        
        // result.activity_ will be -1.0f if it was never set; some files will explicitly say
        //  0.0, so we will write it to the file if it is 0.0, even though its probably an empty
        //  entry.
        if( result.activity_ >= 0.0f )
        {
          snprintf( buffer, sizeof(buffer), "%1.8E", (result.activity_/1000.0f) );
          val = doc->allocate_string( buffer );
          xml_node<char> *NuclideActivityValue = doc->allocate_node( node_element, "NuclideActivityValue", val );
          attr = doc->allocate_attribute( "units", "kBq" );
          NuclideActivityValue->append_attribute( attr );
          nuclide_node->append_node( NuclideActivityValue );
        }
        
        if( result.id_confidence_.size() )
        {
          //If result.id_confidence_ represents a number between 0 and 100, then
          //  use "NuclideIDConfidenceValue" otherwise use "NuclideIDConfidenceDescription"
          float dummy;
          if( SpecUtils::parse_float(result.id_confidence_.c_str(), result.id_confidence_.size(), dummy) && (dummy>=0.0f) && (dummy<=100.0f) )
          {
            val = doc->allocate_string( result.id_confidence_.c_str(), result.id_confidence_.size()+1 );
            xml_node<char> *NuclideIDConfidenceValue = doc->allocate_node( node_element, "NuclideIDConfidenceValue", val );
            nuclide_node->append_node( NuclideIDConfidenceValue );
          }else
          {
            val = doc->allocate_string( result.id_confidence_.c_str(), result.id_confidence_.size()+1 );
            xml_node<char> *NuclideIDConfidenceDescription = doc->allocate_node( node_element, "NuclideIDConfidenceDescription", val );
            nuclide_node->append_node( NuclideIDConfidenceDescription );
          }
        }//if( result.id_confidence_.size() )
        
        if( result.nuclide_type_.size() )
        {
          val = doc->allocate_string( result.nuclide_type_.c_str(), result.nuclide_type_.size()+1 );
          xml_node<char> *NuclideType = doc->allocate_node( node_element, "NuclideCategoryDescription", val );
          nuclide_node->append_node( NuclideType );
        }
        
        xml_node<char> *NuclideExtension = 0;
        
        if( result.real_time_ >= 0.0f )
        {
          NuclideExtension = doc->allocate_node( node_element, "NuclideExtension" );
          nuclide_node->append_node( NuclideExtension );
          snprintf( buffer, sizeof(buffer), "PT%fS", result.real_time_ );
          val = doc->allocate_string( buffer );
          xml_node<char> *SampleRealTime = doc->allocate_node( node_element, "SampleRealTime", val );
          NuclideExtension->append_node( SampleRealTime );
        }//if( we should record some more info )
        
        if( result.distance_ >= 0.0f )
        {
          xml_node<char> *SourcePosition = doc->allocate_node( node_element, "SourcePosition" );
          nuclide_node->append_node( SourcePosition );
          
          xml_node<char> *RelativeLocation = doc->allocate_node( node_element, "RelativeLocation" );
          SourcePosition->append_node( RelativeLocation );
          
          snprintf( buffer, sizeof(buffer), "%f", (result.distance_/1000.0f) );
          attr = doc->allocate_attribute( "units", "m" );
          val = doc->allocate_string( buffer );
          xml_node<char> *DistanceValue = doc->allocate_node( node_element, "DistanceValue", val );
          DistanceValue->append_attribute( attr );
          RelativeLocation->append_node( DistanceValue );
        }
        
        
        if( result.detector_.size() )
        {
          if( !NuclideExtension )
          {
            NuclideExtension = doc->allocate_node( node_element, "NuclideExtension" );
            nuclide_node->append_node( NuclideExtension );
          }
          
          val = doc->allocate_string( result.detector_.c_str(), result.detector_.size()+1 );
          xml_node<char> *Detector = doc->allocate_node( node_element, "Detector", val );
          NuclideExtension->append_node( Detector );
        }
      }//if( result.nuclide_.size() )
      
      if( result.dose_rate_ > 0.0f )
      {
        xml_node<char> *DoseAnalysisResults = doc->allocate_node( node_element, "DoseAnalysisResults" );
        AnalysisResults->append_node( DoseAnalysisResults );
        
        if( result.remark_.size() )
        {
          val = doc->allocate_string( result.remark_.c_str(), result.remark_.size()+1 );
          xml_node<char> *Remark = doc->allocate_node( node_element, "Remark", val );
          DoseAnalysisResults->append_node( Remark );
        }
        
        snprintf( buffer, sizeof(buffer), "%1.8E", result.dose_rate_ );
        val = doc->allocate_string( buffer );
        xml_node<char> *AverageDoseRateValue = doc->allocate_node( node_element, "AverageDoseRateValue", val );
        attr = doc->allocate_attribute( "units", "\xc2\xb5Sv/h" ); //\xc2\xb5 --> micro
        AverageDoseRateValue->append_attribute( attr );
        DoseAnalysisResults->append_node( AverageDoseRateValue );
        
        if( result.real_time_ > 0.f )
        {
          snprintf( buffer, sizeof(buffer), "%1.8E", (result.dose_rate_*result.real_time_) );
          val = doc->allocate_string( buffer );
          xml_node<char> *TotalDoseValue = doc->allocate_node( node_element, "TotalDoseValue", val );
          attr = doc->allocate_attribute( "units", "\xc2\xb5Sv" ); //\xc2\xb5 --> micro
          TotalDoseValue->append_attribute( attr );
          DoseAnalysisResults->append_node( TotalDoseValue );
        }
        
        if( result.distance_ > 0.0f )
        {
          xml_node<char> *SourcePosition = doc->allocate_node( node_element, "SourcePosition" );
          DoseAnalysisResults->append_node( SourcePosition );
          
          xml_node<char> *RelativeLocation = doc->allocate_node( node_element, "RelativeLocation" );
          SourcePosition->append_node( RelativeLocation );
          
          snprintf( buffer, sizeof(buffer), "%f", (result.distance_/1000.0f) );
          attr = doc->allocate_attribute( "units", "m" );
          val = doc->allocate_string( buffer );
          xml_node<char> *DistanceValue = doc->allocate_node( node_element, "DistanceValue", val );
          RelativeLocation->append_node( DistanceValue );
          RelativeLocation->append_attribute( attr );
        }
      }//if( result.dose_rate_ > 0.0f )
      
      
      //Children of <Nuclide> that are not checked for:
      //<NuclideIdentifiedIndicator>,
      //<NuclideActivityUncertaintyValue>,
      //<NuclideMinimumDetectableActivityValue>,
      //<NuclideCategoryDescription>,
      //<NuclideSourceGeometryCode>,
      //<NuclideShieldingAtomicNumber>,
      //<NuclideShieldingArealDensityValue>,
      //<NuclideExtension>
    }//for( const DetectorAnalysisResult &result : ana.results_ )
  }//void add_analysis_results_to_2012_N42(...)

  
  
  std::shared_ptr< ::rapidxml::xml_document<char> > SpecFile::create_2012_N42_xml() const
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    using namespace rapidxml;
    
    std::mutex xmldocmutex;
    EnergyCalToIndexMap calToSpecMap;
    
    std::shared_ptr<xml_document<char> > doc = std::make_shared<xml_document<char> >();
    
    const char *val = (const char *)0;
    
    xml_node<char> *declaration = doc->allocate_node( node_declaration );
    doc->append_node( declaration );
    xml_attribute<> *attr = doc->allocate_attribute( "version", "1.0" );
    declaration->append_attribute( attr );
    attr = doc->allocate_attribute( "encoding", "utf-8" );
    declaration->append_attribute( attr );
    
    
    xml_node<char> *RadInstrumentData = doc->allocate_node( node_element, "RadInstrumentData" );
    doc->append_node( RadInstrumentData );
    
    val = doc->allocate_string( uuid_.c_str(), uuid_.size()+1 );
    attr = doc->allocate_attribute( "n42DocUUID", val );
    RadInstrumentData->append_attribute( attr );
    
    attr = doc->allocate_attribute( "xmlns", "http://physics.nist.gov/N42/2011/N42" );
    RadInstrumentData->append_attribute( attr );
    
    
    {
      const time_point_t t = chrono::time_point_cast<chrono::microseconds>( chrono::system_clock::now() );
      const string datetime = SpecUtils::to_extended_iso_string(t) + "Z";
      val = doc->allocate_string( datetime.c_str(), datetime.size()+1 );
      attr = doc->allocate_attribute( "n42DocDateTime", val );
      RadInstrumentData->append_attribute( attr );
    }
    
    
    string original_creator;
    for( size_t i = 0; i < remarks_.size(); ++i )
    {
      if( remarks_[i].empty()
         || SpecUtils::starts_with(remarks_[i], "InstrumentVersion:")
         || SpecUtils::starts_with(remarks_[i], "Instrument ")
         )
        continue;
      
      if( SpecUtils::starts_with(remarks_[i], "N42 file created by: " ) )
      {
        original_creator = remarks_[i].substr(21);
        continue;
      }
      
      std::lock_guard<std::mutex> lock( xmldocmutex );
      val = doc->allocate_string( remarks_[i].c_str(), remarks_[i].size()+1 );
      
      xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
      RadInstrumentData->append_node( remark );
    }//for( size_t i = 0; i < remarks_.size(); ++i )
    
    
    
    for( size_t i = 0; i < parse_warnings_.size(); ++i )
    {
      const bool hasprefix = SpecUtils::starts_with( parse_warnings_[i], s_parser_warn_prefix );
      string valstr = (hasprefix ? "" : s_parser_warn_prefix ) + parse_warnings_[i];
      
      std::lock_guard<std::mutex> lock( xmldocmutex );
      val = doc->allocate_string( valstr.c_str(), valstr.size()+1 );
      xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
      RadInstrumentData->append_node( remark );
    }//for( size_t i = 0; i < remarks_.size(); ++i )
    
    
    
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      
      if( original_creator.empty() || original_creator=="InterSpec")
      {
        xml_node<char> *RadInstrumentDataCreatorName = doc->allocate_node( node_element, "RadInstrumentDataCreatorName", "InterSpec" );
        RadInstrumentData->append_node( RadInstrumentDataCreatorName );
      }else
      {
        SpecUtils::ireplace_all( original_creator, "InterSpec", "" );
        SpecUtils::ireplace_all( original_creator, "  ", "" );
        original_creator = "InterSpec. Original file by " + original_creator;
        
        val = doc->allocate_string( original_creator.c_str(), original_creator.size()+1 );
        xml_node<char> *RadInstrumentDataCreatorName = doc->allocate_node( node_element, "RadInstrumentDataCreatorName", val );
        RadInstrumentData->append_node( RadInstrumentDataCreatorName );
      }//if( original_creator.empty() ) / else
      
      //    xml_node<char> *interspec_node = doc->allocate_node( node_element, "InterSpec" );
      //    RadInstrumentData->append_node( interspec_node );
      
      //Should add original file name here
      //    val = doc->allocate_string( filename_.c_str(), filename_.size()+1 );
      //    xml_node<char> *node = doc->allocate_node( node_element, "FileName", val );
      //    interspec_node->append_node( node );
    }
    
    
    //We have to convert from 2006 N42 to 2012 instrument typs
    string classcode = convert_n42_instrument_type_from_2006_to_2012( instrument_type_ );
    
    //Class code is required tag.
    if( classcode.empty() )
      classcode = "Other";
    
    string descrip;
    if( lane_number_ >= 0 )
      descrip += "Lane " + std::to_string(lane_number_);
    if( measurement_location_name_.size() )
      descrip += (descrip.size() ? " Location " : "Location ") + measurement_location_name_;
    if( inspection_.size() )
      descrip += (descrip.size() ? " Inspection: " : "Inspection: ") + inspection_;
    
    xml_node<char> *RadInstrumentInformation = 0;
    
    //Child elements of RadInstrumentInformation must be in the order:
    //RadInstrumentManufacturerName
    //RadInstrumentIdentifier
    //RadInstrumentModelName
    //RadInstrumentDescription
    //RadInstrumentClassCode
    //RadInstrumentVersion
    //RadInstrumentQualityControl
    //RadInstrumentCharacteristics
    //RadInstrumentInformationExtension
    
    
    {//Begin lock code block
      std::lock_guard<std::mutex> lock( xmldocmutex );
      
      RadInstrumentInformation = doc->allocate_node( node_element, "RadInstrumentInformation" );
      RadInstrumentData->append_node( RadInstrumentInformation );
      
      attr = doc->allocate_attribute( "id", "InstInfo1" );
      RadInstrumentInformation->append_attribute( attr );
      
      
      if( manufacturer_.size() )
      {
        val = doc->allocate_string( manufacturer_.c_str(), manufacturer_.size()+1 );
        RadInstrumentInformation->append_node( doc->allocate_node( node_element, "RadInstrumentManufacturerName", val ) );
      }else
      {
        RadInstrumentInformation->append_node( doc->allocate_node( node_element, "RadInstrumentManufacturerName", "unknown" ) );
      }
      
      if( instrument_id_.size() )
      {
        val = doc->allocate_string( instrument_id_.c_str(), instrument_id_.size()+1 );
        RadInstrumentInformation->append_node( doc->allocate_node( node_element, "RadInstrumentIdentifier", val ) );
      }
      
      if( instrument_model_.size() )
      {
        val = doc->allocate_string( instrument_model_.c_str(), instrument_model_.size()+1 );
        RadInstrumentInformation->append_node( doc->allocate_node( node_element, "RadInstrumentModelName", val ) );
      }else
      {
        RadInstrumentInformation->append_node( doc->allocate_node( node_element, "RadInstrumentModelName", "unknown" ) );
      }
      
      if( descrip.size() )
      {
        val = doc->allocate_string( descrip.c_str(), descrip.size()+1 );
        RadInstrumentInformation->append_node( doc->allocate_node( node_element, "RadInstrumentDescription", val ) );
      }
      
      val = doc->allocate_string( classcode.c_str(), classcode.size()+1 );
      RadInstrumentInformation->append_node( doc->allocate_node( node_element, "RadInstrumentClassCode", val ) );
    }//End lock code block
    
    
    for( size_t i = 0; i < component_versions_.size(); ++i )
    {
      const string &name = component_versions_[i].first;
      const string &version = component_versions_[i].second;
      
      if( SpecUtils::istarts_with( name, "Software") && (version == "Unknown") )
        continue;
      
      std::lock_guard<std::mutex> lock( xmldocmutex );
      
      xml_node<char> *version_node = doc->allocate_node( node_element, "RadInstrumentVersion" );
      RadInstrumentInformation->append_node( version_node );
      
      if( SpecUtils::iequals_ascii( name, "Software" ) )
        val = doc->allocate_string( ("Original " +  name).c_str() );
      else
        val = doc->allocate_string( name.c_str(), name.size()+1 );
      version_node->append_node( doc->allocate_node( node_element, "RadInstrumentComponentName", val ) );
      val = doc->allocate_string( version.c_str(), version.size()+1 );
      version_node->append_node( doc->allocate_node( node_element, "RadInstrumentComponentVersion", val ) );
    }//for( size_t i = 0; i < component_versions_.size(); ++i )
    
    {//Begin lock code block
      /*
       At a minimum, there shall be an instance of this element with the
       component name Software that describes the version of the software and/or
       firmware that produced the current N42 XML document.
       ...
       To determine N42 file compatibility, the user shall always specify the
       “Software” component.
       */
      
      std::lock_guard<std::mutex> lock( xmldocmutex );
      
      xml_node<char> *instvrsn = doc->allocate_node( node_element, "RadInstrumentVersion" );
      RadInstrumentInformation->append_node( instvrsn );
      
      xml_node<char> *cmpntname = doc->allocate_node( node_element, "RadInstrumentComponentName", "Software" );
      instvrsn->append_node( cmpntname );
      
      //Could add InterSpec_VERSION
      xml_node<char> *cmpntvsn = doc->allocate_node( node_element, "RadInstrumentComponentVersion", "InterSpec" );
      instvrsn->append_node( cmpntvsn );
      
      //This next remark is invalid acording to the n42.xsd file, but valid according to the PDF spec
      //xml_node<char> *remark = doc->allocate_node( node_element, "Remark", "N42 file created by InterSpec" );
      //instvrsn->append_node( remark );
      
      
      //Could add InterSpec_VERSION
      
      char buff[8];
      snprintf( buff, sizeof(buff), "%d", SpecFile_2012N42_VERSION );
      
      instvrsn = doc->allocate_node( node_element, "RadInstrumentVersion" );
      RadInstrumentInformation->append_node( instvrsn );
      
      cmpntname = doc->allocate_node( node_element, "RadInstrumentComponentName", "InterSpecN42Serialization" );
      instvrsn->append_node( cmpntname );
      
      val = doc->allocate_string( buff );
      cmpntvsn = doc->allocate_node( node_element, "RadInstrumentComponentVersion", val );
      instvrsn->append_node( cmpntvsn );
    }//End lock code block
    
    //The <RadInstrumentQualityControl> node would be inserted here
    
    xml_node<char> *RadInstrumentCharacteristics = 0;
    xml_node<char> *RadInstrumentInformationExtension = 0;
    
    if(!measurement_operator_.empty())
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      RadInstrumentCharacteristics = doc->allocate_node( node_element, "RadInstrumentCharacteristics" );
      RadInstrumentInformation->append_node( RadInstrumentCharacteristics );
      
      xml_node<char> *CharacteristicGroup = doc->allocate_node( node_element, "CharacteristicGroup" );
      RadInstrumentCharacteristics->append_node( CharacteristicGroup );
      
      xml_node<char> *Characteristic = doc->allocate_node( node_element, "Characteristic" );
      CharacteristicGroup->append_node( Characteristic );
      
      xml_node<char> *CharacteristicName = doc->allocate_node( node_element, "CharacteristicName", "Operator Name" );
      Characteristic->append_node( CharacteristicName );
      
      string operatorname = measurement_operator_;
      //    ireplace_all( operatorname, "\t", "&#009;" );//replace tab characters with tab character code
      //    ireplace_all( operatorname, "&", "&#38;" );
      
      val = doc->allocate_string( operatorname.c_str(), operatorname.size()+1 );
      xml_node<char> *CharacteristicValue = doc->allocate_node( node_element, "CharacteristicValue", val );
      Characteristic->append_node( CharacteristicValue );
    }//if( measurement_operator_.size() )
    
    
    if( detector_type_ != DetectorType::Unknown )
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      if( !RadInstrumentInformationExtension )
        RadInstrumentInformationExtension = doc->allocate_node( node_element, "RadInstrumentInformationExtension" );
      
      const string &typestr = detectorTypeToString(detector_type_);
      xml_node<char> *type = doc->allocate_node( node_element, "InterSpec:DetectorType", typestr.c_str() );
      RadInstrumentInformationExtension->append_node( type );
    }
    
    if( RadInstrumentInformationExtension )
      RadInstrumentInformation->append_node( RadInstrumentInformationExtension );
    
    
    const size_t ndetectors = detector_names_.size();
    for( size_t i = 0; i < ndetectors; ++i )
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      
      xml_node<char> *RadDetectorInformation = doc->allocate_node( node_element, "RadDetectorInformation" );
      RadInstrumentData->append_node( RadDetectorInformation );
      
      if( detector_names_[i].empty() )
        val = s_unnamed_det_placeholder.c_str();
      else
        val = doc->allocate_string( detector_names_[i].c_str(), detector_names_[i].size()+1 );
      
      // TODO: `val` may not be valid; e.g., the id attribute must begin with a letter, and then only contain digits, hyphens, underscores, colons, and periods.
      xml_attribute<> *name = doc->allocate_attribute( "id", val );
      RadDetectorInformation->append_attribute( name );
      
      if( !detector_names_[i].empty() )
      {
        xml_node<char> *RadDetectorName = doc->allocate_node( node_element, "RadDetectorName", val );
        RadDetectorInformation->append_node( RadDetectorName );
      }
      
      string rad_det_desc;
      for( size_t j = 0; j < measurements_.size(); ++j )
      {
        if( detector_names_[i] == measurements_[j]->detector_name_ )
        {
          rad_det_desc = measurements_[j]->detector_description_;
          break;
        }
      }
      
      if( std::count(neutron_detector_names_.begin(), neutron_detector_names_.end(), detector_names_[i] ) )
      {
        bool hasgamma = false;
        
        for( int sample : sample_numbers_ )
        {
          std::shared_ptr<const Measurement> m = measurement( sample, detector_numbers_[i] );
          if( m && m->gamma_counts_ && !m->gamma_counts_->empty())
          {
            hasgamma = true;
            break;
          }
        }//for( int sample : sample_numbers_ )
        
        if( hasgamma )
        {
          xml_node<char> *RadDetectorCategoryCode = doc->allocate_node( node_element, "RadDetectorCategoryCode", "Gamma" );
          RadDetectorInformation->append_node( RadDetectorCategoryCode );
          
          if( !rad_det_desc.empty() )
            rad_det_desc += ", ";
          rad_det_desc += "Gamma and Neutron";
        }else
        {
          xml_node<char> *RadDetectorCategoryCode = doc->allocate_node( node_element, "RadDetectorCategoryCode", "Neutron" );
          RadDetectorInformation->append_node( RadDetectorCategoryCode );
        }
      }else
      {
        xml_node<char> *RadDetectorCategoryCode = doc->allocate_node( node_element, "RadDetectorCategoryCode", "Gamma" );
        RadDetectorInformation->append_node( RadDetectorCategoryCode );
      }
      
      /// \TODO: below 'det_kind' will be something like HPGe (since its based on the detection
      ///        system), even if we are writing information on the neutron detector here - should fix this.
      ///        Note that  when we parse from n42-2012, we will put the detector kind into detector_description_ if there are no
      ///        dimensions associated, which make the round trip to and from the N42 then relies on this - brittle, I know.
      const string det_kind = determine_gamma_detector_kind_code( *this );
      val = doc->allocate_string( det_kind.c_str(), det_kind.size()+1 );
      xml_node<char> *RadDetectorKindCode = doc->allocate_node( node_element, "RadDetectorKindCode", val );
      RadDetectorInformation->append_node( RadDetectorKindCode );
      
      if( !rad_det_desc.empty() )
      {
        val = doc->allocate_string( rad_det_desc.c_str(), rad_det_desc.size()+1 );
        xml_node<char> *RadDetectorDescription = doc->allocate_node( node_element, "RadDetectorDescription", val );
        RadDetectorInformation->append_node( RadDetectorDescription );
      }
      
      //    if( det_kind == "Other" )
      //    {
      //      val = "InterSpec could not determine detector type.";
      //      xml_node<char> *RadDetectorDescription = doc->allocate_node( node_element, "RadDetectorDescription", val );
      //      RadDetectorInformation->append_node( RadDetectorDescription );
      //    }
      
      //    xml_node<char> *RadDetectorInformationExtension = doc->allocate_node( node_element, "RadDetectorInformationExtension" );
      //    RadInstrumentData->append_node( RadDetectorInformationExtension );
      
      //detector_numbers_ are assigned in cleanup_after_load(), so dont need to be saved to the file
      //    snprintf( buffer, sizeof(buffer), "%i", detector_numbers_[i] );
      //    val = doc->allocate_string( buffer );
      //    xml_node<char> *DetectorNumber = doc->allocate_node( node_element, "InterSpec:DetectorNumber", val );
      //    RadDetectorInformationExtension->append_node( DetectorNumber );
    }//for( size_t i = 0; i < ndetectors; ++i )
    
    
    insert_N42_calibration_nodes( measurements_ , RadInstrumentData, xmldocmutex, calToSpecMap );
      
    SpecUtilsAsync::ThreadPool workerpool;
    
    //For the case of portal data, where the first sample is a long background and
    //  the rest of the file is the occupancy, GADRAS has some "special"
    //  requirements on id attribute values...
    bool first_sample_was_back = false;
    const vector<int> sample_nums_vec( begin(sample_numbers_), end(sample_numbers_) );
    
    
    // We want to write each spectrum that shares a sample number under the same <RadMeasurement>
    //  node, but we cant do this if start or real times differ.  How we handle grouping under
    //  RadMeasurement nodes should be handled consistently throughout the file, so if we think we
    //  might have to separate Measurements with the same sample numbers, we'll check on this
    //  and (try to) be consistent across the file.  Even if we dont check here, we'll still check
    //  when creating the RadMeasurement nodes to be sure.
    //
    //  Note: this is a bit of a hack, and only appears necessary for a single, rare N42-2006,
    //        system; see refY3GD2FOXFW in order to serialize to N42-2012 and back and be identical.
    //        The better solution might be to fix the N42-2006 parsing and remove this block.
    //        About 6% of the files in the regression test suite will hit this code path of doing
    //        the check, with about half of those setting well_behaving_samples to false.
    //
    // TODO: See if we can process the N42-2006 file better during parsing so we can get rid of this
    //       check.
    bool well_behaving_samples = true;
    const bool is_passthrough = (properties_flags_ & kPassthroughOrSearchMode);
    const bool notUniqueSamples = (properties_flags_ & kNotUniqueSampleDetectorNumbers);
    const bool notSampleSorted = (properties_flags_ & kNotSampleDetectorTimeSorted);
    const bool notTimeSorted = (properties_flags_ & kNotTimeSortedOrder);
    
    if( (detector_names().size() > 1)
        && (sample_numbers_.size() > 7) //arbitrarily chosen
        && is_passthrough
        && (notUniqueSamples || notSampleSorted || notTimeSorted) )
    {
      //cout << "Checking well_behaving_samples for " << filename() << endl;
      for( size_t i = 0; well_behaving_samples && (i < sample_nums_vec.size()); ++i )
      {
        const int sample = sample_nums_vec[i];
        vector<shared_ptr<const Measurement>> sample_meas = sample_measurements( sample );
        
        sample_meas.erase( std::remove_if( begin(sample_meas), end(sample_meas),
                                         [](shared_ptr<const Measurement> m) -> bool {
          return m->derived_data_properties_; } ), end(sample_meas) );
        
        if( sample_meas.empty() )
          continue;
        
        std::shared_ptr<const Measurement> first_meas = sample_meas.front();
        const float first_rt = first_meas->real_time();
        const time_point_t first_time = first_meas->start_time();
        
        for( size_t i = 1; well_behaving_samples && (i < sample_meas.size()); ++i )
        {
          std::shared_ptr<const Measurement> m = sample_meas[i];
          well_behaving_samples = (fabs(first_rt - m->real_time()) < 0.00001);
          if( (m->source_type() != SourceType::Background) || (i != 0) )
            well_behaving_samples = well_behaving_samples && (first_time == m->start_time());
          well_behaving_samples = well_behaving_samples && (m->source_type() == first_meas->source_type());
        }//for( check if all measurements can go under a single <RadMeasurement> tag )
      }//for( auto sni = begin(sample_numbers_); well_behaving_samples && (sni != end(sample_numbers_)); ++sni )
      
      //cout << "\twell_behaving_samples=" << well_behaving_samples << endl;
    }//if( notUniqueSamples || notSampleSorted || notTimeSorted )
    
    ///////////////
    
    
    for( set<int>::const_iterator sni = sample_numbers_.begin(); sni != sample_numbers_.end(); ++sni )
    {
      const int sample_num = *sni;
      
      // <DerivedData> are pretty much the same as <RadMeasurement> elements, so we'll use the same
      //  code to define both (we have to keep derived data Measurement's under <DerivedData> and
      //  non-derived data elements under <RadMeasurement>).
      auto add_sample_specs_lambda = [this, sample_num,&calToSpecMap,&first_sample_was_back,
                               &sample_nums_vec,&xmldocmutex,&doc,&workerpool,
                               &RadInstrumentData, well_behaving_samples](
                const vector< std::shared_ptr<const Measurement> > &smeas, const bool is_derived )
      {
        vector<size_t> calid;
        for( size_t i = 0; i < smeas.size(); ++i )
        {
          const auto iter = calToSpecMap.find( smeas[i]->energy_calibration_ );
          if( iter != end(calToSpecMap) )
          {
            calid.push_back( static_cast<int>(calToSpecMap[smeas[i]->energy_calibration_]) );
          }else
          {
            if( smeas[i]->gamma_counts_ && !smeas[i]->gamma_counts_->empty() )
            {
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
              log_developer_error( __func__, "Serious unexpected error mapping energy calibrations" );
              assert( 0 );
#endif
            }
            
            calid.push_back( 0 );
          }
        }//for( size_t i = 0; i < smeas.size(); ++i )
        
        
        if( smeas.empty() )  //probably should only happen when all measurements for this sample are derived data
          return;
        
        //Check if all smeas have same start and real times, if so, write them under
        //  a single <RadMeasurement> tag, otherwise write under separate ones.
        //Note: this test is a bit brittle, and maybe not quite fair.  For example on
        //  my portal the neutron
        time_point_t starttime = smeas[0]->start_time();
        float min_rtime = smeas[0]->real_time(), max_rtime = smeas[0]->real_time();
        
        //Below allows up to (an arbitrarily chosen) 50 ms difference in start and
        //  real times to allow rounding errors during parsing; There are also some
        //  multi-detector systems (ex Anthony NM HPGe)  where the various
        //  sub-detectors arnet perfectly synced, but they are close enough to
        //  capture the intent of the <RadMeasurement> node.  Note that this does
        //  mess with real time (and dead time) a little for some RPM systems where
        //  the real time for each subdetector isnt always the same for each
        //  timeslice; to make up for this, a <Remark>LiveTime: ...</Remark> live
        //  time is added in these situations...
        for( size_t i = 1; i < smeas.size(); ++i )
        {
          const time_point_t tst = smeas[i]->start_time();
          starttime = ((is_special(tst) || (starttime < tst)) ? starttime : tst);
          min_rtime = min( min_rtime, smeas[i]->real_time() );
          max_rtime = max( max_rtime, smeas[i]->real_time() );
        }
        
        bool sample_same_times = well_behaving_samples && (fabs(max_rtime - min_rtime) < 0.00001);
        
        for( size_t i = 1; sample_same_times && (i < smeas.size()); ++i )
        {
          std::shared_ptr<const Measurement> m = smeas[i];
          
          //Allow the first record in the file, if it is a background, to not need
          //  to have the same start time or real time as the other measurements in
          //  this sample.  This is to accommodate some portals whose RSPs may
          //  accumulate backgrounds a little weird, but none-the-less should all be
          //  considered the same sample.
          if( m->source_type() != SourceType::Background
             || (sample_num != (*sample_numbers_.begin())) )
          {
            sample_same_times = (starttime == m->start_time());
            
            //if( starttime != m->start_time() )
            //{
            //  const time_point_t::duration diff = starttime - m->start_time();
            //  sample_same_times = (!is_special(diff) && (std::abs(diff.total_microseconds()) < 100));
            //}
          }//if( not background or not first sample )
          
          sample_same_times = sample_same_times && (smeas[i]->source_type() == smeas[0]->source_type());
        }//for( check if all measurements can go under a single <RadMeasurement> tag )
        
        
        // Define a lambda to append a DerivedData specific string to the "id" attribute value.
        //  This seems to be the de facto way to specify properties of DerivedData elements...
        auto derived_id_extension_lambda = [&smeas,is_derived]() -> std::string {
          string id_attrib_val;
          
          if( !is_derived )
            return id_attrib_val;
          
          uint32_t common_bits = 0xFFFFFFFF;
          for( const auto &m : smeas )
            common_bits &= m->derived_data_properties_;
          
          typedef Measurement::DerivedDataProperties DerivedProps;
          auto check_bit = [common_bits]( const DerivedProps &p ) -> bool {
            typedef std::underlying_type<DerivedProps>::type DerivedProps_t;
            return (common_bits & static_cast<DerivedProps_t>(p));
          };//check_bit
          
          //Note: these next four strings are detected in N42DecodeHelper2012::set_deriv_data(...)
          //      as well, and also added in #add_spectra_to_measurement_node_in_2012_N42_xml,
          //      so if you alter these strings, change them in those places as well (this is to
          //      keep the round-trip from memory -> N42-2012 -> memory matching; brittle, but I
          //      guess thats just how N42 is (we could define an DerivedData extension, but thats
          //      too much trouble at this point).
          
          if( check_bit(DerivedProps::ItemOfInterestSum) )
            id_attrib_val += "-MeasureSum";
          
          if( check_bit(DerivedProps::UsedForAnalysis) )
            id_attrib_val += "-Analysis";
          
          if( check_bit(DerivedProps::ProcessedFurther) )
            id_attrib_val += "-Processed";
          
          if( check_bit(DerivedProps::BackgroundSubtracted) )
            id_attrib_val += "-BGSub";
          
          return id_attrib_val;
        };//derived_id_extension_lambda
        
        //sample_same_times = false;
        
        if( sample_same_times )
        {
          char RadMeasurementId[32];
          xml_node<char> *RadMeasurement = 0;
          
          if( passthrough() )
          {
            //Catch the case where its a searchmode file, or a portal file where
            //  the first sample is a long background.  Apparently GADRAS relies
            //  on the "id" attribute of RadMeasurement for this...
            if( (sample_num == (*sample_numbers_.begin()))
               && (smeas[0]->source_type() == SourceType::Background)
               && smeas[0]->live_time() > 10.0 )
            {
              first_sample_was_back = true;
              snprintf( RadMeasurementId, sizeof(RadMeasurementId), "Background" );
            }else
            {
              int sn = sample_num;
              if( first_sample_was_back )
              {
                auto pos = lower_bound(begin(sample_nums_vec), end(sample_nums_vec), sample_num );
                sn = static_cast<int>( pos - begin(sample_nums_vec) );
              }
              
              snprintf( RadMeasurementId, sizeof(RadMeasurementId), "Survey%i", sn );
            }
          }else
          {
            snprintf( RadMeasurementId, sizeof(RadMeasurementId), "Sample%i", sample_num );
          }
          
          RadMeasurementId[sizeof(RadMeasurementId)-1] = '\0'; //jic
          
          string id_attrib_val = RadMeasurementId;
          id_attrib_val += derived_id_extension_lambda();
          
          
          {
            std::lock_guard<std::mutex> lock( xmldocmutex );
            RadMeasurement = doc->allocate_node( node_element, (is_derived ? "DerivedData" : "RadMeasurement") );
            RadInstrumentData->append_node( RadMeasurement );
            
            char *val = doc->allocate_string( id_attrib_val.c_str(), id_attrib_val.size() + 1 );
            auto *attr = doc->allocate_attribute( "id", val );
            RadMeasurement->append_attribute( attr );
          }
          
          workerpool.post( [&xmldocmutex,RadMeasurement,smeas,calid](){
            add_spectra_to_measurement_node_in_2012_N42_xml( RadMeasurement, smeas, calid, xmldocmutex);
          } );
        }else //if( sample_same_times )
        {
          for( size_t i = 0; i < smeas.size(); ++i )
          {
            const vector< std::shared_ptr<const Measurement> > thismeas(1,smeas[i]);
            const vector<size_t> thiscalid( 1, calid[i] );
            
            char RadMeasurementId[32];
            xml_node<char> *RadMeasurement = 0;
            snprintf( RadMeasurementId, sizeof(RadMeasurementId), "Sample%iDet%i", sample_num, smeas[i]->detector_number_ );
            
            string id_attrib_val = RadMeasurementId;
            id_attrib_val += derived_id_extension_lambda();
            
            
            {
              std::lock_guard<std::mutex> lock( xmldocmutex );
              RadMeasurement = doc->allocate_node( node_element, (is_derived ? "DerivedData" : "RadMeasurement") );
              RadInstrumentData->append_node( RadMeasurement );
              
              char *val = doc->allocate_string( id_attrib_val.c_str(), id_attrib_val.size() + 1 );
              xml_attribute<> *attr = doc->allocate_attribute( "id", val );
              RadMeasurement->append_attribute( attr );
            }
            
            workerpool.post( [RadMeasurement, thismeas, thiscalid, &xmldocmutex](){
              add_spectra_to_measurement_node_in_2012_N42_xml( RadMeasurement, thismeas, thiscalid, xmldocmutex );
            } );
          }//for( loop over measuremtns for this sample number )
        }//if( sample_same_times ) / else
      };//add_sample_specs_lambda lamda
      
      
      vector< std::shared_ptr<const Measurement> > sample_meas, sample_derived_data;
      const vector< std::shared_ptr<const Measurement> > all_sample_meas = sample_measurements( sample_num );
      
      for( const auto &m : all_sample_meas )
      {
        if( !m->derived_data_properties_ )
          sample_meas.push_back( m );
        else
          sample_derived_data.push_back( m );
      }//for( size_t i = 0; i < all_sample_meas.size(); ++i )
      
      if( !sample_meas.empty() )
        add_sample_specs_lambda( sample_meas, false );
      
      if( !sample_derived_data.empty() )
        add_sample_specs_lambda( sample_derived_data, true );
    }//for( set<int>::const_iterator sni = sample_numbers_.begin(); sni != sample_numbers_.end(); ++sni )
    
    
    
    
    /*
     for( size_t i = 0; i < measurements_.size(); ++i )
     {
     const std::shared_ptr<const std::vector<float>> &binning = measurements_[i]->channel_energies();
     
     workerpool.post( std::bind( &Measurement::add_to_2012_N42_xml, measurements_[i],
     RadInstrumentData,
     calToSpecMap[binning],
     std::ref(xmldocmutex) ) );
     }//for( size_t i = 0; i < measurements_.size(); ++i )
     */
    
    if( detectors_analysis_ )
      add_analysis_results_to_2012_N42( *detectors_analysis_,
                                       RadInstrumentData, xmldocmutex );
    
    workerpool.join();
    
    if( !multimedia_data_.empty() )
      add_multimedia_data_to_2012_N42( multimedia_data_, RadInstrumentData );
    
    return doc;
  }//rapidxml::xml_node<char> *create_2012_N42_xml() const
  
  
  
  
  
  
  bool SpecFile::write_2012_N42( std::ostream& ostr ) const
  {
    std::shared_ptr< rapidxml::xml_document<char> > doc = create_2012_N42_xml();
    
    //  if( !!doc )
    //    rapidxml::print( ostr, *doc, rapidxml::print_no_indenting );
    if( !!doc )
      rapidxml::print( ostr, *doc );
    
    
    return !!doc;
  }//bool write_2012_N42( std::ostream& ostr ) const
  
  
  
  void SpecFile::set_2012_N42_instrument_info( const rapidxml::xml_node<char> *info_node )
  {
    std::unique_lock<std::recursive_mutex> lock( mutex_ );
    
    if( !info_node )
      return;
    
    //  const rapidxml::xml_attribute<char> *id = info_node->first_attribute( "id", 2 );
    
    const rapidxml::xml_node<char> *remark_node = XML_FIRST_NODE(info_node, "Remark");
    if( remark_node )
      remarks_.push_back( xml_value_str(remark_node) );
    
    
    const rapidxml::xml_node<char> *manufacturer_name_node = XML_FIRST_NODE(info_node, "RadInstrumentManufacturerName");
    if( manufacturer_name_node && !XML_VALUE_COMPARE(manufacturer_name_node,"unknown") )
      manufacturer_ = xml_value_str(manufacturer_name_node);  //ex. "FLIR Systems"
    
    const rapidxml::xml_node<char> *instr_id_node = info_node->first_node( "RadInstrumentIdentifier", 23, false );
    if( instr_id_node && instr_id_node->value_size() )
      instrument_id_ = xml_value_str(instr_id_node);          //ex. "932222-76"
    
    const rapidxml::xml_node<char> *model_node = XML_FIRST_NODE(info_node, "RadInstrumentModelName");
    
    if( !model_node )  //file_format_test_spectra/n42_2006/identiFINDER/20130228_184247Preliminary2010.n42
      model_node = XML_FIRST_NODE(info_node,"RadInstrumentModel");
    if( model_node && !XML_VALUE_COMPARE(model_node,"unknown") )
      instrument_model_ = xml_value_str( model_node );          //ex. "identiFINDER 2 ULCS-TNG"
    
    const rapidxml::xml_node<char> *desc_node = XML_FIRST_NODE(info_node, "RadInstrumentDescription");
    //Description: Free-form text describing the radiation measurement instrument.
    //Usage: This information can be a general description of the radiation measurement instrument or its use.
    if( desc_node && desc_node->value_size() )
    {
      string val = xml_value_str( desc_node );
      
      string::size_type lanepos = val.find( "Lane " );
      if( lanepos != string::npos )
      {
        if( !(stringstream(val.substr(lanepos+5) ) >> lane_number_) )
        {
          lanepos = string::npos;
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
          log_developer_error( __func__, ("Failed to read lane number from '" + val + "'").c_str() );
#endif
        }
      }//if( lanepos != string::npos )
      
      
      string::size_type locationpos = val.find( "Location " );
      if( locationpos != string::npos )
        measurement_location_name_ = val.substr(locationpos+9);
      else if( (locationpos = val.find( " at " ))==string::npos )
        measurement_location_name_ = val.substr(locationpos+4);
      
      if( measurement_location_name_.find("Inspection:") != string::npos )
        measurement_location_name_ = measurement_location_name_.substr(0,measurement_location_name_.find("Inspection:"));
      
      string::size_type inspectionpos = val.find( "Inspection: " );
      if( inspectionpos != string::npos )
        inspection_ = val.substr(inspectionpos+12);
      if( inspection_.find("Location ") != string::npos )
        inspection_ = inspection_.substr(0,inspection_.find("Location "));
      
      SpecUtils::trim( inspection_ );
      SpecUtils::trim( measurement_location_name_ );
      
      if( lanepos==string::npos && (locationpos==string::npos && val.size()<8) )
        remarks_.push_back( string("Instrument Description: ") + xml_value_str( desc_node ) );
    }
    
    
    const rapidxml::xml_node<char> *infoextension_node = XML_FIRST_NODE(info_node, "RadInstrumentInformationExtension");
    //  const rapidxml::xml_node<char> *operator_node = infoextension_node ? XML_FIRST_NODE(infoextension_node,"InterSpec:MeasurementOperator") : nullptr;
    
    //<InterSpec:Inspection> node is vestigual as of 20160607, and only kept in to read older files (with SpecFile_2012N42_VERSION==1), of which, there are probably not many, if any around
    const rapidxml::xml_node<char> *inspection_node = infoextension_node ? XML_FIRST_NODE(infoextension_node,"InterSpec:Inspection") : nullptr;
    
    const rapidxml::xml_node<char> *detector_type_node = infoextension_node ? XML_FIRST_NODE(infoextension_node,"InterSpec:DetectorType") : nullptr;
    
    //
    
    //  if( operator_node )
    //    measurement_operator_ = xml_value_str( operator_node );
    if( inspection_node )
      inspection_ = xml_value_str( inspection_node );
    
    if( detector_type_node )
    {
      const string type = xml_value_str( detector_type_node );
      for( DetectorType i = DetectorType(0);
          i < DetectorType::Unknown;
          i = DetectorType(static_cast<int>(i)+1) )
      {
        if( type == detectorTypeToString(i) )
        {
          detector_type_ = i;
          break;
        }
      }
    }//if( detector_type_node )
    
    
    const rapidxml::xml_node<char> *class_code_node = XML_FIRST_NODE(info_node, "RadInstrumentClassCode");
    instrument_type_ = xml_value_str( class_code_node );
    
    if( SpecUtils::iequals_ascii( instrument_type_, "Other" ) )
      instrument_type_ = "";
    
    for( const rapidxml::xml_node<char> *version_node = XML_FIRST_NODE(info_node, "RadInstrumentVersion");
        version_node;
        version_node = XML_NEXT_TWIN(version_node) )
    {
      const rapidxml::xml_node<char> *name = XML_FIRST_NODE(version_node, "RadInstrumentComponentName");
      const rapidxml::xml_node<char> *version = XML_FIRST_NODE(version_node, "RadInstrumentComponentVersion");
      
      //RadInstrumentVersion is requiren for N42 2012, so should add it as another field
      string comment;
      if( version_node->value_size() && !XML_VALUE_COMPARE( version_node, "unknown" ) )
      {
        //This is actually invalid...
        component_versions_.push_back( make_pair("unknown",xml_value_str(version_node) ) );
      }else if( XML_VALUE_COMPARE( version_node, "Software" ) && XML_VALUE_COMPARE( version_node, "InterSpec" ) )
      {
        //Skip this, since it is written by InterSpec.
      }else if( name && version )
      {
        string namestr = xml_value_str(name);
        if( istarts_with(namestr, "Original Software") )
          namestr = namestr.substr( 9 );
        
        component_versions_.push_back( make_pair( namestr, xml_value_str(version) ) );
        //const rapidxml::xml_node<char> *remark = XML_FIRST_NODE(version_node,"Remark");
        //if( remark && remark->value_size() )
        //comment += ((name->value_size() || version->value_size()) ? ", " : " ")
        //+ string("Remark: ") + xml_value_str(remark_node);
      }//if( unknown ) / else
    }//for( loop over "RadInstrumentVersion" nodes )
    
    
    const rapidxml::xml_node<char> *qc_node = XML_FIRST_NODE(info_node, "RadInstrumentQualityControl");
    if( qc_node )
    {
      const rapidxml::xml_attribute<char> *id = qc_node->first_attribute( "id", 2 );
      const rapidxml::xml_node<char> *remark_node = XML_FIRST_NODE(qc_node, "Remark");
      const rapidxml::xml_node<char> *date_node = XML_FIRST_NODE(qc_node, "InspectionDateTime");
      const rapidxml::xml_node<char> *indicator_node = XML_FIRST_NODE(qc_node, "InCalibrationIndicator");
      string comment = "Calibration Check";
      if( id && id->value_size() )
        comment = xml_value_str(id) + string(" ") + comment;
      if( date_node && date_node->value_size() )
        comment += string(" ") + xml_value_str(date_node);
      if( indicator_node && indicator_node->value_size() )
        comment += string(" pass=") + xml_value_str(indicator_node);
      if( remark_node && remark_node->value_size() )
        comment += string(", remark: ") + xml_value_str(remark_node);
    }//if( qc_node )
    
    for( const rapidxml::xml_node<char> *charac_node = XML_FIRST_NODE(info_node, "RadInstrumentCharacteristics");
        charac_node;
        charac_node = XML_NEXT_TWIN(charac_node) )
    {
      //    const rapidxml::xml_attribute<char> *id = charac_node->first_attribute( "id", 2 );
      const rapidxml::xml_node<char> *remark_node = XML_FIRST_NODE(charac_node, "Remark");
      
      if( remark_node )
      {
        string remark = xml_value_str( remark_node );
        trim( remark );
        if(!remark.empty())
          remarks_.push_back( remark );
      }
      
      for( const rapidxml::xml_node<char> *char_node = XML_FIRST_NODE(charac_node, "Characteristic");
          char_node;
          char_node = XML_NEXT_TWIN(char_node) )
      {
        const string comment = N42DecodeHelper2012::concat_2012_N42_characteristic_node( char_node );
        if(!comment.empty())
          remarks_.push_back( comment );
      }//for( loop over "Characteristic" nodes )
      
      for( const rapidxml::xml_node<char> *group_node = XML_FIRST_NODE(charac_node, "CharacteristicGroup");
          group_node;
          group_node = XML_NEXT_TWIN(group_node) )
      {
        //      const rapidxml::xml_attribute<char> *char_id = group_node->first_attribute( "id", 2 );
        const rapidxml::xml_node<char> *remark_node = XML_FIRST_NODE(group_node, "Remark");
        const rapidxml::xml_node<char> *name_node = XML_FIRST_NODE(group_node, "CharacteristicGroupName");
        
        string precursor;
        if( (name_node && name_node->value_size()) || (remark_node && remark_node->value_size()) )
        {
          precursor += "[";
          if( name_node && name_node->value_size() )
            precursor += xml_value_str(name_node);
          if( remark_node )
          {
            if( precursor.size() > 1 )
              precursor += " ";
            precursor += string("(remark: ") + xml_value_str(remark_node) + string(")");
          }
          precursor += "] ";
        }//if( name_node or remark_node )
        
        for( const rapidxml::xml_node<char> *char_node = XML_FIRST_NODE(group_node, "Characteristic");
            char_node;
            char_node = XML_NEXT_TWIN(char_node) )
        {
          
          using rapidxml::internal::compare;
          const rapidxml::xml_node<char> *name_node = XML_FIRST_NODE(char_node, "CharacteristicName");
          
          
          if( name_node && XML_VALUE_ICOMPARE(name_node, "Operator Name") )
          {
            const rapidxml::xml_node<char> *value_node = XML_FIRST_NODE(char_node, "CharacteristicValue");
            measurement_operator_ = xml_value_str(value_node);
          }else
          {
            const string comment = N42DecodeHelper2012::concat_2012_N42_characteristic_node( char_node );
            if(!comment.empty())
              remarks_.push_back( precursor + comment );
          }//
        }//for( loop over "Characteristic" nodes )
      }//for( loop over "CharacteristicGroup" nodes )
    }//for( loop over "RadInstrumentCharacteristics" nodes )
    
    
    //  rapidxml::xml_node<char> *info_extension_node = XML_FIRST_NODE(info_node, "RadInstrumentInformationExtension");
    //  if( info_extension_node )
    //    cerr << "Warning, the RadInstrumentInformationExtension tag isnt "
    //         << "implemented for N42 2012 files yet." << endl;
  }//void set_2012_N42_instrument_info( rapidxml::xml_node<char> *inst_info_node )
  
  
  void get_2012_N42_energy_calibrations( map<string,MeasurementCalibInfo> &calibrations,
                                        const rapidxml::xml_node<char> *data_node,
                                        vector<string> &remarks,
                                        vector<string> &parse_warnings )
  {
    using SpecUtils::split_to_floats;
    
    
    for( const rapidxml::xml_node<char> *cal_node = XML_FIRST_NODE(data_node, "EnergyCalibration");
        cal_node;
        cal_node = XML_NEXT_TWIN(cal_node) )
    {
      string id;
      rapidxml::xml_attribute<char> *id_att = cal_node->first_attribute( "id", 2, false );
      
      
      if( !id_att || !id_att->value_size() )
        id_att = cal_node->first_attribute( "Reference", 9, false );
      
      if( id_att && id_att->value_size() )
        id = xml_value_str(id_att);  //if attribute is _required_, so well rely on this
      
      rapidxml::xml_node<char> *coef_val_node = XML_FIRST_NODE(cal_node, "CoefficientValues");
      rapidxml::xml_node<char> *energy_boundry_node = XML_FIRST_NODE(cal_node, "EnergyBoundaryValues");
      rapidxml::xml_node<char> *date_node = XML_FIRST_NODE(cal_node, "CalibrationDateTime");
      
      if( !coef_val_node )
        coef_val_node = XML_FIRST_NODE(cal_node, "Coefficients");
      
      MeasurementCalibInfo info;
      
      XML_FOREACH_CHILD( remark_node, cal_node, "Remark" )
      {
        const string remark_value = xml_value_str(remark_node);
        
        if( !SpecUtils::icontains( remark_value, s_energy_cal_not_available_remark)
           && !SpecUtils::icontains( remark_value, s_frf_to_poly_remark) )
        {
          remarks.push_back( "Calibration for " + id + " remark: " + remark_value );
        }
      }//for( loop over remark nodes )
      
      if( date_node && date_node->value_size() )
        remarks.push_back( id + " calibrated " + xml_value_str(date_node) );
      
      if( coef_val_node && coef_val_node->value_size() )
      {
        info.equation_type = SpecUtils::EnergyCalType::Polynomial;
        const char *data = coef_val_node->value();
        const size_t len = coef_val_node->value_size();
        if( !SpecUtils::split_to_floats( data, len, info.coefficients ) )
          throw runtime_error( "Invalid calibration value: " + xml_value_str(coef_val_node) );
      
        //Technically there must be exactly 3 polynomial coefficients, but we wont enforce this
        // (this has to be an oversight of the spec, right?_
        const size_t nread_coeffs = info.coefficients.size();
      
        while( !info.coefficients.empty() && info.coefficients.back()==0.0f )
          info.coefficients.erase( info.coefficients.end()-1 );
      
      
        if( info.coefficients.size() < 2 )
        {
          if( info.coefficients.empty() && nread_coeffs >= 2 )
          {
            //The coefficients were all zero - this is usually used to indicate no calibration known
            //  or applicable.  We'll keep these around incase its intentional.  We see this when
            //  we write out a HPRDS file to n42-2012, an invalid energy cal (all zeros) gets put in
            //  for the GMTube
            info.equation_type = SpecUtils::EnergyCalType::InvalidEquationType;
          }else
          {
            parse_warnings.push_back( "An invalid EnergyCalibration CoefficientValues was"
                                      " encountered with value '" + string(data,data+len)
                                      + "', and wont be used" );
            continue;
          }
        }//if( info.coefficients.size() < 2 )
        
        info.calib_id = xml_value_str( coef_val_node->first_attribute( "id", 2 ) );
        if( info.calib_id.empty() ) //Some detectors have the ID attribute on the <EnergyCalibration> node
          info.calib_id = xml_value_str( id_att );
        
        
        //  XXX - deviation pairs not yet not, because I have no solid examples files.
        //  The EnergyDeviationValues and EnergyValues child elements provide a means
        //  to account for the difference in the energy predicted by the second-order
        //  polynomial equation and the true energy.
        rapidxml::xml_node<char> *energy_values_node = XML_FIRST_NODE(cal_node, "EnergyValues");
        rapidxml::xml_node<char> *energy_deviation_node = XML_FIRST_NODE(cal_node, "EnergyDeviationValues");
        if( energy_deviation_node && energy_values_node
           && energy_deviation_node->value_size() && energy_values_node->value_size() )
        {
          try
          {
            vector<float> energies, deviations;
            
            const char *energiesstr = energy_values_node->value();
            const size_t energystrsize = energy_values_node->value_size();
            const char *devstrs = energy_deviation_node->value();
            const size_t devstrsize = energy_deviation_node->value_size();
            
            if( !SpecUtils::split_to_floats( energiesstr, energystrsize, energies ) )
              throw runtime_error( "" );
            
            if( !SpecUtils::split_to_floats( devstrs, devstrsize, deviations ) )
              throw runtime_error( "" );
            
            if( energies.size() != deviations.size() )
              throw runtime_error( "" );
            
            vector< pair<float,float> > &devpairs = info.deviation_pairs_;
            
            for( size_t i = 0; i < energies.size(); ++i )
              devpairs.push_back( make_pair(energies[i],deviations[i]) );
            
            stable_sort( devpairs.begin(), devpairs.end(), &dev_pair_less_than );
          }catch( std::exception & )
          {
            parse_warnings.push_back( "Deviation pairs from file appear to be invalid, not using" );
          }
        }//if( energy deviation information is available )
        
      }else if( energy_boundry_node )
      {
        info.equation_type = SpecUtils::EnergyCalType::LowerChannelEdge;
        
        const char *data = energy_boundry_node->value();
        const size_t len = energy_boundry_node->value_size();
        
        if( !SpecUtils::split_to_floats( data, len, info.coefficients ) )
          throw runtime_error( "Failed to parse lower channel energies" );
      }else
      {
        const string msg = "Warning, found an invalid EnergyCalibration node";
        if( std::find(begin(parse_warnings), end(parse_warnings), msg) == end(parse_warnings) )
          parse_warnings.push_back( msg );
        continue;
      }
      
      if( calibrations.count(id) == 0 )
      {
        //We havent seen a calibration with this ID before
        calibrations[id] = info;
      }else
      {
        //We have seen a calibration with this ID.  We will only overwrite if its a different
        // calibration.
        MeasurementCalibInfo &oldcal = calibrations[id];
        if( !(oldcal == info) )
        {
          const string msg = "Energy calibration with ID='" + id
                          + "' was re-defined with different definition, which a file shouldnt do.";
#if( PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS )
          log_developer_error( __func__, msg.c_str() );
#endif
          if( std::find(begin(parse_warnings), end(parse_warnings), msg) == end(parse_warnings) )
            parse_warnings.push_back( msg );
        }
      }//if( havent seen this ID ) / else
      
      
    }//for( loop over "Characteristic" nodes )
    
  }//get_2012_N42_energy_calibrations(...)

  
  void SpecFile::load_2012_N42_from_doc( const rapidxml::xml_node<char> *data_node )
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    if( !data_node )
      throw runtime_error( "load_2012_N42_from_doc: Invalid document node" );
    
    if( !XML_NAME_ICOMPARE(data_node, "RadInstrumentData") )
      throw runtime_error( "load_2012_N42_from_doc: Unable to get RadInstrumentData node" );
    
    
    auto uuid_att = XML_FIRST_ATTRIB(data_node, "n42DocUUID");
    if( uuid_att && uuid_att->value_size() )
    {
      uuid_ = xml_value_str(uuid_att);
      
      //A certain HPGe detector always writes the same UUID, making it not unique...
      //  See ref3J9DRAPSZ1
      if( SpecUtils::istarts_with( uuid_, "d72b7fa7-4a20-43d4-b1b2-7e3b8c6620c1" )
         || SpecUtils::istarts_with( uuid_, "64a170f5-4c39-4bd8" ) )
        uuid_ = "";
    }
    //In the next call, the location in memory pointed to by 'data_node' may be
    //  changed to point to somewhere in a newly created rapidxml::xml_document
    //  so the returned result must be kept in scope so 'data_node' will be valid.
    auto spirdoc = spir_mobile_2012_n42_hack( data_node );
    
    /*
     If the N42 XML document has been created by translation from an original data file,
     include the name of the software that was used and the name and format type of the original data file.
     
     This information is kinda duplicate information of <RadInstrumentVersion><RadInstrumentComponentName>Software</>...</>
     
     */
    rapidxml::xml_node<char> *creator_node = XML_FIRST_NODE(data_node, "RadInstrumentDataCreatorName");
    if( creator_node && creator_node->value_size() )
      remarks_.push_back( "N42 file created by: " + xml_value_str(creator_node) );
    
    for( rapidxml::xml_node<char> *remark_node = XML_FIRST_NODE(data_node, "Remark");
        remark_node;
        remark_node = XML_NEXT_TWIN(remark_node) )
    {
      string remark = xml_value_str( remark_node );
      trim( remark );
      
      const bool parse_warn = SpecUtils::starts_with( remark, s_parser_warn_prefix );
      if( parse_warn )
      {
        SpecUtils::ireplace_all( remark, s_parser_warn_prefix, "" );
        parse_warnings_.emplace_back( std::move(remark) );
      }else if( !remark.empty() )
      {
        remarks_.push_back( remark );
      }
    }//for( loop over remarks )
    
    map<string,MeasurementCalibInfo> calibrations;
    map<string,pair<DetectionType,string> > id_to_dettype;
    
    // A certain mobile system (see refJ3WSODVDEC) creates an N42 document for
    //  each time slice, and then puts all the documents into a single file.
    //  So we have to loop over <RadInstrumentData> nodes to reach all the
    //  measurements.
    //  (the one file I've seen looks like calibration is always the same, but
    //  we will loop over all N42 documents to be sure).
    for( auto rad_data_node = data_node; rad_data_node; rad_data_node = XML_NEXT_TWIN(rad_data_node) )
    {
      rapidxml::xml_node<char> *inst_info_node = XML_FIRST_NODE(rad_data_node, "RadInstrumentInformation");
      set_2012_N42_instrument_info( inst_info_node );
  
      get_2012_N42_energy_calibrations( calibrations, rad_data_node, remarks_, parse_warnings_ );
      
      //TODO: implement using RadItemInformation
      //  for( const rapidxml::xml_node<char> *rad_item_node = XML_FIRST_NODE(rad_data_node, "RadItemInformation");
      //      rad_item_node;
      //      rad_item_node = XML_NEXT_TWIN(rad_item_node) )
      //  {
      //    rapidxml::xml_attribute<char> *id_att = rad_item_node->first_attribute( "id", 2, false );
      //    rapidxml::xml_node<char> *remark_node = XML_FIRST_NODE(rad_item_node, "Remark");
      //    rapidxml::xml_node<char> *descrip_node = XML_FIRST_NODE(rad_item_node, "RadItemDescription");
      //    rapidxml::xml_node<char> *quantity_node = XML_FIRST_NODE(rad_item_node, "RadItemQuantity");
      //    rapidxml::xml_node<char> *geometry_node = XML_FIRST_NODE(rad_item_node, "RadItemMeasurementGeometryDescription");
      //    rapidxml::xml_node<char> *characteristics_node = XML_FIRST_NODE(rad_item_node, "RadItemCharacteristics");
      //    rapidxml::xml_node<char> *extension_node = XML_FIRST_NODE(rad_item_node, "RadItemInformationExtension");
      //  }//for( loop over "RadItemInformation" nodes )
      
      
      XML_FOREACH_CHILD( info_node, rad_data_node, "RadDetectorInformation" )
      {
        rapidxml::xml_attribute<char> *id_att   = info_node->first_attribute( "id", 2, false );
        //rapidxml::xml_node<char> *remark_node   = XML_FIRST_NODE( info_node, "Remark" );
        rapidxml::xml_node<char> *name_node     = XML_FIRST_NODE( info_node, "RadDetectorName" );
        rapidxml::xml_node<char> *category_node = XML_FIRST_NODE( info_node, "RadDetectorCategoryCode" );
        
        //<RadDetectorKindCode> returns "NaI", "HPGe", "PVT", "He3", etc (see determine_gamma_detector_kind_code())
        //  and should be utilized at some point.  But would require adding a field to SpecUtils::SpecFile
        //  I think to kind of do it properly.
        
        rapidxml::xml_node<char> *kind_node     = XML_FIRST_NODE( info_node, "RadDetectorKindCode" );
        rapidxml::xml_node<char> *descrip_node  = XML_FIRST_NODE( info_node, "RadDetectorDescription" );
        rapidxml::xml_node<char> *length_node   = XML_FIRST_NODE( info_node, "RadDetectorLengthValue" );
        rapidxml::xml_node<char> *width_node    = XML_FIRST_NODE( info_node, "RadDetectorWidthValue" );
        rapidxml::xml_node<char> *depth_node    = XML_FIRST_NODE( info_node, "RadDetectorDepthValue" );
        rapidxml::xml_node<char> *diameter_node = XML_FIRST_NODE( info_node, "RadDetectorDiameterValue" );
        rapidxml::xml_node<char> *volume_node   = XML_FIRST_NODE( info_node, "RadDetectorVolumeValue" );
        rapidxml::xml_node<char> *characteristics_node = XML_FIRST_NODE( info_node, "RadDetectorCharacteristics" );
        //    rapidxml::xml_node<char> *extension_node = XML_FIRST_NODE( info_node, "RadDetectorInformationExtension" );
        
        string name = xml_value_str(id_att);
        if( name == s_unnamed_det_placeholder )
        {
          name.clear();
        }else
        {
          if( name.empty() )
            name = xml_value_str(name_node);
          
          if( name.empty() )
          {
            rapidxml::xml_attribute<char> *ref_att = info_node->first_attribute( "Reference", 9, false );
            name = xml_value_str(ref_att);
          }
        }//if( name == s_unnamed_det_placeholder ) / else
        
        //    rapidxml::xml_node<char> *detinfo_extension_node = XML_FIRST_NODE(info_node, "RadDetectorInformationExtension");
        //    rapidxml::xml_node<char> *det_num_node = detinfo_extension_node ? XML_FIRST_NODE(detinfo_extension_node, "InterSpec:DetectorNumber") : nullptr;
        //    if( det_num_node && det_num_node->value() )
        //    {
        //      int detnum;
        //      if( sscanf(det_num_node->value(), "%d", &detnum) == 1 )
        //        detname_to_num[name] = detnum;
        //    }
        
        DetectionType type = GammaDetection; //OtherDetection;
        if( category_node && category_node->value_size() )
        {
          if( XML_VALUE_ICOMPARE(category_node, "Gamma") )
            type = GammaDetection;
          else if( XML_VALUE_ICOMPARE(category_node, "Neutron") )
            type = NeutronDetection;
          else
            type = OtherDetection;
          
          //See refDUEL9G1II9, but basically if a detectors name ends with "Ntr",
          //  force it to be a neutron detector (to make up for manufacturers error)
          if( (type == GammaDetection)
             && SpecUtils::iends_with(name, "Ntr") )
          {
            type = NeutronDetection;
          }
          
          const string desc = xml_value_str( descrip_node );
          if( icontains( desc, "Gamma" ) && icontains( desc, "Neutron" ) )
            type = GammaAndNeutronDetection;
          
          if( type == OtherDetection )
          {
            const string idval = xml_value_str(id_att);
            if( SpecUtils::icontains(idval, "gamma") )
              type = GammaDetection;
            else if( SpecUtils::icontains(idval, "neutron") )
              type = NeutronDetection;
          }//if( type == OtherDetection )
        }//if( category_node && category_node->value_size() )
        
        string descrip; // = xml_value_str( kind_node );
        
        descrip = xml_value_str( descrip_node );
        SpecUtils::ireplace_all( descrip, ", Gamma and Neutron", "" );
        SpecUtils::ireplace_all( descrip, "Gamma and Neutron", "" );
        
        
        // This is definitely a hack, but SpecUtils will create a <RadDetectorKindCode> node, but
        //  not the other nodes.  When SpecUtils makes a n42-2012 file, it will add the
        //  RadDetectorKindCode node always, even if we dont have detector information, but we dont
        //  want to mess up the detector description.
        if( kind_node && kind_node->value_size()
          && (length_node || width_node || diameter_node || depth_node || volume_node) )
        {
          const string kind = xml_value_str(kind_node);
          if( !iequals_ascii(kind, "Other") && !istarts_with(kind, "Unk") )
            descrip += (descrip.length() ? ", " : "") + string("Kind: ") + kind;
        }
          
        //if( kind_node && kind_node->value_size() )
        //{
        //  const string kind = xml_value_str(kind_node);
        //  if( !iequals_ascii(kind, "Other") )
        //  {
        //    string remark = "DetectorKind: " + kind;
        //    if( name.size() )
        //      remark = "Detector '" + name + "' " + remark;
        //    remarks_.push_back( remark );
        //  }
        //}//if( kind_node && kind_node->value_size() )
        
        if( length_node && length_node->value_size() )
        {
          if( descrip.length() )
            descrip += ", ";
          descrip += string("Length: ") + xml_value_str(length_node) + string(" cm");
        }
        
        if( width_node && width_node->value_size() )
        {
          if( descrip.length() )
            descrip += ", ";
          descrip += string("Width: ") + xml_value_str(width_node) + string(" cm");
        }
        
        if( depth_node && depth_node->value_size() )
        {
          if( descrip.length() )
            descrip += ", ";
          descrip += string("Depth: ") + xml_value_str(depth_node) + string(" cm");
        }
        
        if( diameter_node && diameter_node->value_size() )
        {
          if( descrip.length() )
            descrip += ", ";
          descrip += string("Diameter: ") + xml_value_str(diameter_node) + string(" cm");
        }
        
        if( volume_node && volume_node->value_size() )
        {
          if( descrip.length() )
            descrip += ", ";
          descrip += string("Volume: ") + xml_value_str(volume_node) + string(" cc");
        }
        
        for( auto character = XML_FIRST_NODE_CHECKED(characteristics_node, "Characteristic");
            character; character = XML_NEXT_TWIN(character) )
        {
          const string charac_str = N42DecodeHelper2012::concat_2012_N42_characteristic_node(character);
          if( charac_str.size() )
            descrip += string(descrip.size() ? ", " : "") + "{" + charac_str + "}";
          
          // Kromek D3 data exported from the Android app looks to have the phone manufacturer/model
          //  as the "RadInstrumentManufacturerName" and "RadInstrumentModelName" values, with the
          //  actual detectors values (that we want) buried down in these Characteristics.
          //  The gamma and neutron detectors both have these values, and appear to be the same.
          const rapidxml::xml_node<char> *name_node = XML_FIRST_NODE(character, "CharacteristicName");
          const rapidxml::xml_node<char> *value_node = XML_FIRST_NODE(character, "CharacteristicValue");
          if( xml_value_compare(name_node, "Sensor Make") )
          {
            if( value_node && value_node->value_size() )
              manufacturer_ = xml_value_str(value_node);
          }else if( xml_value_compare(name_node, "Sensor Model") )
          {
            if( value_node && value_node->value_size() )
              instrument_model_ = xml_value_str(value_node);
          }else if( xml_value_compare(name_node, "Sensor Serial") )
          {
            if( value_node && value_node->value_size() )
              instrument_id_ = xml_value_str(value_node);
          }
        }//loop over characteristics
        
        
        if( type==GammaDetection || type==NeutronDetection || type==GammaAndNeutronDetection )
          id_to_dettype[name] = pair<DetectionType,string>(type,descrip);
      }//for( loop over "RadDetectorInformation" nodes )
      
      
      // There may be multiple <AnalysisResults> elements, for different algorithms, or whatever.
      //  Right now we'll just kinda append things
      XML_FOREACH_CHILD( analysis_node, rad_data_node, "AnalysisResults" )
      {
        std::shared_ptr<DetectorAnalysis> analysis_info = std::make_shared<DetectorAnalysis>();
        set_analysis_info_from_n42( analysis_node, *analysis_info );
        
        if( detectors_analysis_ )
        {
          const auto append_ana_str = []( const string &prev, string &current ){
            if( (prev == current) || prev.empty() || current.empty() )
              return;
            current = prev + "\n" + current;
          };//const auto append_str =
          
          append_ana_str( detectors_analysis_->algorithm_name_, analysis_info->algorithm_name_ );
          append_ana_str( detectors_analysis_->algorithm_creator_, analysis_info->algorithm_creator_ );
          append_ana_str( detectors_analysis_->algorithm_description_, analysis_info->algorithm_description_ );
          append_ana_str( detectors_analysis_->algorithm_result_description_, analysis_info->algorithm_result_description_ );
          
          analysis_info->remarks_.insert( begin(analysis_info->remarks_),
                                         begin(detectors_analysis_->remarks_),
                                         end(detectors_analysis_->remarks_) );
          
          analysis_info->algorithm_component_versions_.insert( begin(analysis_info->algorithm_component_versions_),
                                         begin(detectors_analysis_->algorithm_component_versions_),
                                         end(detectors_analysis_->algorithm_component_versions_) );
          
          if( analysis_info->analysis_start_time_ == time_point_t{} )
            analysis_info->analysis_start_time_ = detectors_analysis_->analysis_start_time_;
          else
            analysis_info->analysis_start_time_ = std::min( analysis_info->analysis_start_time_,
                                                      detectors_analysis_->analysis_start_time_ );
          analysis_info->analysis_computation_duration_ += detectors_analysis_->analysis_computation_duration_;
          
          analysis_info->results_.insert( begin(analysis_info->results_),
                                         begin(detectors_analysis_->results_),
                                         end(detectors_analysis_->results_) );
        }//if( detectors_analysis_ )
        
        detectors_analysis_ = analysis_info;
      }//XML_FOREACH_CHILD( analysis_node, rad_data_node, "AnalysisResults" )
      
      
      XML_FOREACH_CHILD( multimedia_node, rad_data_node, "MultimediaData" )
      {
        auto data = make_shared<MultimediaData>();
        if( set_multimedia_data( *data, multimedia_node ) )
          multimedia_data_.push_back( data );
      }
    }//for( loop over <RadInstrumentData> nodes - ya, I know we shouldnt have to )
    
    SpecUtilsAsync::ThreadPool workerpool;
    
    //  Need to make order in measurements_ reproducible
    vector<std::shared_ptr<std::mutex> > meas_mutexs;
    vector< std::shared_ptr< vector<std::shared_ptr<Measurement> > > > measurements_each_meas;
    vector< std::shared_ptr< vector<std::shared_ptr<Measurement> > > > measurements_each_derived;
    
    size_t numRadMeasNodes = 0;
    std::mutex meas_mutex, calib_mutex;
    
    // Symetrica detectors _may_ have a <RadMeasurement> node with id="ForegroundMeasureSum",
    //  "BackgroundMeasure...5" (for gamma), "BackgroundMeasure...6" (for neutron),
    //  "CalMeasurementGamma-...", "StabMeasurement...", and then they have a bunch
    //  of ("TickMeasurement..."|"ForegroundMeasure...") that are 0.2 seconds long.
    //  We probably want the "ForegroundMeasureSum" + "BackgroundMeasure..." (neut+gamma
    //  combined) - so we will separate all these from the "Tick" measurements, by
    //  marking them as derived (which I guess they kinda are).
    //  Its not great, but maybe better than nothing.
    //  A particular example where this was giving trouble can be seen in ref3Z3LPD6CY6
    const bool is_symetrica = SpecUtils::istarts_with( manufacturer_, "symetrica" );
    vector<shared_ptr<vector<shared_ptr<Measurement>>>> symetrica_special_meas;
    vector<pair<string,string>> symetrica_special_attribs;
    
    
    // Lets keep track of "RadMeasurement" and "DerivedData" nodes, to help fill in sample numbers
    size_t rad_meas_index = 1;  //we'll start at 1
    
    // See note above about system that has multiple N42 documents in a single file.
    for( const rapidxml::xml_node<char> *rad_data_node = data_node; rad_data_node;
        rad_data_node = rad_data_node->next_sibling("RadInstrumentData") )
    {
      XML_FOREACH_CHILD( meas_node, rad_data_node, "RadMeasurement" )
      {
        std::shared_ptr<std::mutex> mutexptr = std::make_shared<std::mutex>();
        auto these_meas = std::make_shared< vector<std::shared_ptr<Measurement> > >();
        
        ++numRadMeasNodes;
        meas_mutexs.push_back( mutexptr );
        measurements_each_meas.push_back( these_meas );
        
        if( is_symetrica )
        {
          const rapidxml::xml_attribute<char> *id_attrib = meas_node->first_attribute("id");
          const string id_val_str = xml_value_str( id_attrib );
          
          if( SpecUtils::istarts_with(id_val_str, "ForegroundMeasureSum")
             || SpecUtils::istarts_with(id_val_str, "BackgroundMeasure")
             || SpecUtils::istarts_with(id_val_str, "StabMeasurement")
             || SpecUtils::istarts_with(id_val_str, "CalMeasurementGamma")
             )
          {
            symetrica_special_meas.push_back( these_meas );
            
            const string spec_id = xml_value_str( XML_FIRST_NODE(meas_node, "Spectrum") );
            symetrica_special_attribs.push_back( std::make_pair(id_val_str, spec_id) );
          }//
        }//if( is_symetrica )
        
        workerpool.post( [these_meas, meas_node, rad_meas_index, &id_to_dettype,
                          &calibrations, mutexptr, &calib_mutex](){
          N42DecodeHelper2012::decode_2012_N42_rad_measurement_node( *these_meas, meas_node,
                            rad_meas_index, &id_to_dettype, &calibrations, *mutexptr, calib_mutex );
        } );
        
        rad_meas_index += 1;
      }//for( loop over "RadMeasurement" nodes )
      
      
      //Pickup "DerivedData".  As of Jan 2021 only tested for a few files, including refZYCXWZEO6Y
      XML_FOREACH_CHILD( meas_node, rad_data_node, "DerivedData" )
      {
        std::shared_ptr<std::mutex> mutexptr = std::make_shared<std::mutex>();
        auto these_meas = std::make_shared< vector<std::shared_ptr<Measurement> > >();
        meas_mutexs.push_back( mutexptr );
        measurements_each_derived.push_back( these_meas );
        
        workerpool.post( [these_meas, meas_node, rad_meas_index,
                         &id_to_dettype, &calibrations, mutexptr, &calib_mutex](){
          N42DecodeHelper2012::decode_2012_N42_rad_measurement_node( *these_meas, meas_node,
                            rad_meas_index, &id_to_dettype, &calibrations, *mutexptr, calib_mutex );
        } );
        
        rad_meas_index += 1;
      }//for( loop over "DerivedData" nodes )
    }//for( loop over "RadInstrumentData" nodes - we shouldnt have to, but we do )
    
    workerpool.join();
    
    
    // Now go back up and mark Symetrica special measurements as derived, so this way the
    //  "raw" and "derived" datas will kinda be sensical.
    assert( symetrica_special_meas.size() == symetrica_special_attribs.size() );
    if( !symetrica_special_meas.empty() )
    {
      // We will look for a background neutron-only record, and a background gamma-only
      //  record, and combine them, if found (and then remove from `measurements_each_meas`)
      //  TODO: For multi-detector systems (portals) we should actually try to match the neutrons
      //        to gammas for each detector, but we arent doing that yet
      bool single_gamma_neutron_back = true;
      shared_ptr<Measurement> neut_only_back, gamma_only_back;
      
      for( size_t sym_index = 0;
          single_gamma_neutron_back && (sym_index < symetrica_special_meas.size()); ++sym_index )
      {
        if( !symetrica_special_meas[sym_index] || (sym_index >= symetrica_special_attribs.size()) )
          continue;
        
        const shared_ptr<vector<shared_ptr<Measurement>>> &measv = symetrica_special_meas[sym_index];
        const pair<string,string> &attribs = symetrica_special_attribs[sym_index];
        
        for( const shared_ptr<Measurement> &m : *measv )
        {
          N42DecodeHelper2012::set_deriv_data( m, attribs.first, attribs.second );
          if( m->source_type() == SourceType::Background )
          {
            if( (m->num_gamma_channels() > 6) && !m->contained_neutron() )
            {
              single_gamma_neutron_back = (single_gamma_neutron_back && !gamma_only_back);
              gamma_only_back = m;
            }else if( !m->num_gamma_channels() && m->contained_neutron() )
            {
              single_gamma_neutron_back = (single_gamma_neutron_back && !neut_only_back);
              neut_only_back = m;
            }
            if( !single_gamma_neutron_back )
              break;
          }//if( m->source_type() == SourceType::Background )
        }//for( const shared_ptr<Measurement> &m : *measv )
      }//for( const auto &meass : symetrica_special_meas )
      
      if( single_gamma_neutron_back && gamma_only_back && neut_only_back )
      {
        // Below is commented out 20231004 - only leaving in for a short while to look at different variants
        //cout << "Symetrica\n\tNeutron:"
        //"\n\t\tstart_time=" << to_iso_string( neut_only_back->start_time() )
        //<< "\n\t\tRealTime=" << neut_only_back->real_time()
        //<< "\n\t\tLiveTime=" << neut_only_back->live_time()
        //<< "\n\tGamma:"
        //<< "\n\t\tstart_time=" << to_iso_string( gamma_only_back->start_time() )
        //<< "\n\t\tRealTime=" << gamma_only_back->real_time()
        //<< "\n\t\tLiveTime=" << gamma_only_back->live_time()
        //<< "\nDiffs:"
        //<< "\n\t\tStartTime: " << chrono::duration_cast<chrono::microseconds>(neut_only_back->start_time() - gamma_only_back->start_time()).count() << " us"
        //<< "\n\t\tRealTime: " << (neut_only_back->real_time() - gamma_only_back->real_time())
        //<< "\n\t\tLiveTime: " << (neut_only_back->live_time() - gamma_only_back->live_time())
        //<< endl;
        
        // The neutron background isnt always at the same time, or same duration, so dont combine
        //  in these cases
        const float gamma_rt = gamma_only_back->real_time();
        if( ((neut_only_back->start_time() - gamma_only_back->start_time()) < chrono::seconds(15))   //Arbitrary 15 seconds
           && fabs((neut_only_back->real_time() - gamma_rt) < 0.01*gamma_rt) )  //Arbitrary 1% match
        {
          gamma_only_back->contained_neutron_ = true;
          gamma_only_back->remarks_.push_back( "Neutron and gamma backgrounds have been combined into a single record." );
          gamma_only_back->remarks_.push_back( "Neutron Real Time: PT" + to_string(neut_only_back->real_time()) + "S" );
          gamma_only_back->remarks_.push_back( "Neutron Live Time: PT" + to_string(neut_only_back->live_time()) + "S" );
          
          gamma_only_back->neutron_counts_ = neut_only_back->neutron_counts_;
          gamma_only_back->neutron_counts_sum_ = neut_only_back->neutron_counts_sum_;

          if( (gamma_only_back->dose_rate_ >= 0.0) && (neut_only_back->dose_rate_ >= 0.0) )
            gamma_only_back->dose_rate_ += neut_only_back->dose_rate_;
          if( (gamma_only_back->exposure_rate_ >= 0.0) && (neut_only_back->exposure_rate_ >= 0.0) )
            gamma_only_back->exposure_rate_ += neut_only_back->exposure_rate_;
          
          for( auto miter = begin(measurements_each_meas); miter != end(measurements_each_meas); ++miter )
          {
            auto &measv = **miter;
            auto pos = std::find( begin(measv), end(measv), neut_only_back );
            if( pos != end(measv) )
            {
              measv.erase( pos );
              break;
            }//if( pos != end(measv) )
          }
        }//if( gamma/neutron start and real times are reasonably close )
      }//if( gamma_only_back && neut_only_back )
    }//if( !symetrica_special_meas.empty() )
    
    
    
    for( size_t i = 0; i < measurements_each_meas.size(); ++i )
      for( size_t j = 0; j < measurements_each_meas[i]->size(); ++j )
        measurements_.push_back( (*measurements_each_meas[i])[j] );
    
    for( size_t i = 0; i < measurements_each_derived.size(); ++i )
      for( size_t ii = 0; ii < measurements_each_derived[i]->size(); ++ii )
        measurements_.push_back( (*measurements_each_derived[i])[ii] );
    
    //test for files like "file_format_test_spectra/n42_2006/identiFINDER/20130228_184247Preliminary2010.n42"
    const rapidxml::xml_node<char> *inst_info_node = XML_FIRST_NODE(data_node, "RadInstrumentInformation");
    if( (measurements_.size() == 2)
       && inst_info_node
       && XML_FIRST_NODE(inst_info_node, "RadInstrumentModel") )
    {
      bool has_spectra = (measurements_[0]->detector_name_ == "spectra");
      bool has_intrinsic = (measurements_[0]->detector_name_ == "intrinsicActivity");
      
      has_spectra |= (measurements_[1]->detector_name_ == "spectra");
      has_intrinsic |= (measurements_[1]->detector_name_ == "intrinsicActivity");
      
      if( has_spectra && has_intrinsic )
      {
        detector_names_.clear();
        neutron_detector_names_.clear();
        measurements_[0]->detector_name_ = measurements_[1]->detector_name_ = "";
      }
    }//if( measurements_.size() == 2 )
    
    
    /*
     XXX - Elements still not addressed/implemented
     <RadMeasurementGroup>   //FLIR has
     <FWHMCalibration>
     <TotalEfficiencyCalibration>
     <FullEnergyPeakEfficiencyCalibration>
     <IntrinsicFullEnergyPeakEfficiencyCalibration>
     <IntrinsicSingleEscapePeakEfficiencyCalibration>
     <IntrinsicDoubleEscapePeakEfficiencyCalibration>
     <EnergyWindows>
     <DerivedData>
     <AnalysisResults>     //FLIR has
     <MultimediaData>
     <RadInstrumentDataExtension>
     */
    
    if( measurements_.empty() )
      throw runtime_error( "No valid measurements in 2012 N42 file." );
    
    cleanup_after_load();
  }//bool load_2012_N42_from_doc( rapidxml::xml_node<char> *document_node )
  
  
  bool SpecFile::load_from_N42_document( const rapidxml::xml_node<char> *document_node )
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    if( !document_node || !document_node->name_size() )
      throw runtime_error( "no first node" );
    
    const string doc_node_name = xml_name_str(document_node);
    if( doc_node_name == "RadInstrumentData" )
    {
      load_2012_N42_from_doc( document_node );
    }else if( doc_node_name == "Event" )
    {
      //HPRD files
      const rapidxml::xml_node<char> *child_node = XML_FIRST_NODE(document_node, "N42InstrumentData");
      
      if( !child_node )
        throw runtime_error( "Unrecognized N42 file structure" );
      
      load_2006_N42_from_doc( child_node );
      
      bool hprds = false;
      const rapidxml::xml_node<char> *dataformat = XML_FIRST_NODE(document_node, "ThisDataFormat");
      if( dataformat && dataformat->value_size() )
        hprds = SpecUtils::icontains( xml_value_str(dataformat), "HPRDS" );
      
      if( hprds )
      {
        // Note that for ORTEC HPRDS detectors:
        //  - We could ignore spectra shorter than 2 seconds, in which case we will be left with
        //    a few longer spectra (IOI, background, ??); not totally clear/consistent which ones
        //    are IOI.  It looks like usually the "long count" spectrum is the IOI, and the long
        //    "time slice" can be a background.
        //  - We could ignore longer spectra to get time history - but this time history
        //    doesnt exactly correspond to the dwell measurements.  It looks like the
        //    "total count" is the sum (or very very nearly) the sum of all these short 
        //    measurements.
        const rapidxml::xml_node<char> *node = nullptr;
        //node = XML_FIRST_NODE(document_node, "OnsetDateTime");
        node = XML_FIRST_NODE(document_node, "EventCategory");
        if( node && node->value_size() )
          remarks_.push_back( string("Event Category ") + xml_value_str(node) );
        
        node = XML_FIRST_NODE(document_node, "EventType");
        if( node && node->value_size() )
          remarks_.push_back( string("Event Type ") + xml_value_str(node) );
        
        node = XML_FIRST_NODE(document_node, "EventCode");
        if( node && node->value_size() )
          remarks_.push_back( string("Event Code ") + xml_value_str(node) );
        
        node = XML_FIRST_NODE(document_node, "EventNumber");
        if( node && node->value_size() )
          remarks_.push_back( string("Event Number ") + xml_value_str(node) );
        
        if( measurements_.size() == 2 )
        {
          std::shared_ptr<Measurement> gamma, neutron;
          
          for( int i = 0; i < 2; ++i )
          {
            const string &dettype = measurements_[i]->detector_description_;
            if( SpecUtils::icontains( dettype, "Gamma" ) )
              gamma = measurements_[i];
            else if( SpecUtils::icontains( dettype, "Neutron" ) )
              neutron = measurements_[i];
          }//for( int i = 0; i < 2; ++i )
          
          if( gamma && neutron && gamma != neutron
             && neutron->num_gamma_channels() < 2 )
          {
            gamma->neutron_counts_     = neutron->neutron_counts_;
            gamma->neutron_counts_sum_ = neutron->neutron_counts_sum_;
            gamma->contained_neutron_  = neutron->contained_neutron_;
            
            measurements_.erase( find(measurements_.begin(),measurements_.end(),neutron) );
            
            //cleanup_after_load() wont refresh neutron_detector_names_ unless
            //  its empty
            neutron_detector_names_.clear();
            
            cleanup_after_load();
          }//if( gamma && neutron && gamma != neutron )
        }else if( measurements_.size() > 10 )  //10 chosen arbitrarily - meant to seperate from files that have hundred of samples we dont want
        {
          //We want to keep only
          set<int> keepersamples;
          std::vector< std::shared_ptr<Measurement> > keepers;
          
          for( const auto &m : measurements_ )
          {
            bool keep = false;
            if( m->source_type() == SourceType::Background )
              keep = true;
            for( const std::string &c : m->remarks_ )
              keep |= SpecUtils::icontains( c, "count" );
            if( keep )
            {
              keepersamples.insert( m->sample_number_ );
              keepers.push_back( m );
            }
          }//for( const auto &m : measurements_ )
          
          
          {//begin codeblock to filter remarks
            vector<string> remarks_to_keep;
            remarks_to_keep.reserve( remarks_.size() );
            for( const string &remark : remarks_ )
            {
              if( !SpecUtils::icontains(remark, "DNDORadiationMeasurement") )
                remarks_to_keep.push_back( remark );
            }
            remarks_.swap( remarks_to_keep );
          }//end codeblock to filter remarks
          
          const vector<int> samples( keepersamples.begin(), keepersamples.end() );
          const vector<int>::const_iterator sbegin = samples.begin();
          const vector<int>::const_iterator send = samples.begin();
          
          if( keepers.size() > 1 )
          {
            typedef map<time_point_t, vector<std::shared_ptr<Measurement> > > time_to_meas_t;
            time_to_meas_t time_to_meas;
            
            for( size_t i = 0; i < keepers.size(); ++i )
            {
              std::shared_ptr<Measurement> &m = keepers[i];
              const int oldsn = m->sample_number_;
              const vector<int>::const_iterator pos = std::find(sbegin, send, oldsn);
              m->sample_number_ = 1 + static_cast<int>(pos - sbegin);
              time_to_meas[m->start_time_].push_back( m );
            }
            
            //ORTEC MicroDetective2 HPRDS files will have a gamma, nuetron, and
            //  GMTube spectrum under their <DetectorData> node, so lets try to
            //  combine gamma and neutron together.
            for( time_to_meas_t::value_type &vt : time_to_meas )
            {
              vector<std::shared_ptr<Measurement> > &meass = vt.second;
              
              size_t ngamma = 0, nneut = 0, ngm = 0;
              std::shared_ptr<Measurement> gm_meas, gamma_meas, neut_meas;
              for( size_t i = 0; i < meass.size(); ++i )
              {
                std::shared_ptr<Measurement> &m = meass[i];
                
                if( SpecUtils::iequals_ascii(m->detector_name_, "gamma" ) )
                {
                  ++ngamma;
                  gamma_meas = m;
                }else if( SpecUtils::iequals_ascii(m->detector_name_, "neutron" ) )
                {
                  ++nneut;
                  neut_meas = m;
                }else if( SpecUtils::iequals_ascii(m->detector_name_, "GMTube" ) )
                {
                  ++ngm;
                  gm_meas = m;
                }
              }//for( size_t i = 0; i < meass.size(); ++i )
              
              if( ngamma == 1 && nneut == 1
                 && neut_meas->gamma_count_sum_ < 1.0
                 && (!gamma_meas->contained_neutron_ || gamma_meas->neutron_counts_sum_ < 1.0) )
              {
                gamma_meas->neutron_counts_     = neut_meas->neutron_counts_;
                gamma_meas->neutron_counts_sum_ = neut_meas->neutron_counts_sum_;
                gamma_meas->contained_neutron_  = neut_meas->contained_neutron_;
                
                //cleanup_after_load() wont refresh neutron_detector_names_ unless
                //  its empty
                neutron_detector_names_.clear();
                
                keepers.erase( find( keepers.begin(), keepers.end(), neut_meas ) );
                if( ngm == 1 )
                {
                  //We could add a comment here about the GMTube counts...
                  keepers.erase( find( keepers.begin(), keepers.end(), gm_meas ) );
                }
              }//if( we have one neutron and one gamma measurement with same start time )
            }//for( time_to_meas_t::value_type &vt : time_to_meas )
            
            
            measurements_ = keepers;
            cleanup_after_load();
          }//if( keepers.size() > 1 )
        }//if( measurements_.size() == 2 ) / else if( measurements_.size() > 2 )
        
      }//if( hprds )
    }else
    {
      rapidxml::xml_node<char> *child = document_node->first_node();
      
      if( child && XML_FIRST_NODE(child, "Measurement") )
        document_node = child;
      
      load_2006_N42_from_doc( document_node );
    }//if( N42.42 2012 ) else if( variation on N42.42 2006 )
    
    return true;
  }//bool load_from_N42_document( rapidxml::xml_node<char> *document_node )

  
  bool SpecFile::load_N42_from_data( char *data, char *data_end )
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    try
    {
      reset(); 
      
      data_end = convert_n42_utf16_xml_to_utf8( data, data_end );
      
      if( !is_candidate_n42_file(data,data_end) )
        return false;
      
      rapidxml::xml_document<char> doc;
      doc.parse<rapidxml::parse_trim_whitespace|rapidxml::allow_sloppy_parse>( data, data_end );
      rapidxml::xml_node<char> *document_node = doc.first_node();
      
      const bool loaded = load_from_N42_document( document_node );
      
      if( !loaded )
        throw runtime_error( "Failed to load" );
    }catch(...)
    {
      reset();
      return false;
    }
    
    return true;
  }//bool load_N42_from_data(...)

  
  void set_analysis_info_from_n42( const rapidxml::xml_node<char> *analysis_node,
                              DetectorAnalysis &analysis )
  {
    if( !analysis_node )
      return;
    
    const rapidxml::xml_node<char> *nuc_ana_node = XML_FIRST_NODE( analysis_node, "NuclideAnalysis" );
    if( !nuc_ana_node )
      nuc_ana_node = XML_FIRST_NODE(analysis_node, "NuclideAnalysisResults");
    
    if( !nuc_ana_node )
    {
      //Special case for RadSeeker
      if( XML_FIRST_NODE(analysis_node, "Nuclide") )
        nuc_ana_node = analysis_node;
      
      //<Algorithm>
      //<AlgorithmVendor>SomeBrand</AlgorithmVendor>
      //<AlgorithmName>SomeName</AlgorithmName>
      //<AlgorithmVersion>2.9.3.701</AlgorithmVersion>
      //<FirmwareVersion>Not Set</FirmwareVersion>
      //<SoftwareVersion>2.24.144</SoftwareVersion>
      //<Parameters ParameterName="IsotopeLibrary" ParameterValue="11215" />
      //<Parameters ParameterName="DSP" ParameterValue="1.1.3.2" />
      //</Algorithm>
    }
    
    //  if( !nuc_ana_node )
    //    return;
    
    for( const rapidxml::xml_node<char> *remark_node = XML_FIRST_NODE(analysis_node, "Remark");
        remark_node;
        remark_node = XML_NEXT_TWIN(remark_node) )
    {
      string remark = xml_value_str( remark_node );
      SpecUtils::trim(remark);
      if(!remark.empty())
        analysis.remarks_.push_back( remark );
    }
    
    const rapidxml::xml_node<char> *algo_info_node = XML_FIRST_NODE_CHECKED( analysis_node->parent(), "Algorithm" );
    
    const rapidxml::xml_node<char> *algo_name_node = XML_FIRST_NODE( analysis_node, "AnalysisAlgorithmName" );
    if( !algo_name_node && algo_info_node )
      algo_name_node = XML_FIRST_NODE( algo_info_node, "AlgorithmName" );
    analysis.algorithm_name_ = xml_value_str(algo_name_node);
    
    if( analysis.algorithm_name_.empty() && nuc_ana_node )
    {
      const rapidxml::xml_attribute<char> *att = XML_FIRST_ATTRIB( nuc_ana_node, "AlgorithmName" );
      analysis.algorithm_name_ = xml_value_str(att);
    }
    
    const rapidxml::xml_node<char> *algo_version_node = XML_FIRST_NODE( analysis_node, "AnalysisAlgorithmVersion" );
    if( !algo_version_node && algo_info_node )
      algo_version_node = XML_FIRST_NODE( algo_info_node, "AlgorithmVersion" );
    
    if( algo_version_node && algo_version_node->value_size() )
      analysis.algorithm_component_versions_.push_back( make_pair("main",xml_value_str(algo_version_node)) );
    
    
    //N42-2012 should actually fall into this next loop
    XML_FOREACH_CHILD( versionnode, analysis_node, "AnalysisAlgorithmVersion")
    {
      auto component_node = XML_FIRST_NODE( versionnode, "AnalysisAlgorithmComponentName" );
      auto version_node = XML_FIRST_NODE( versionnode, "AnalysisAlgorithmComponentVersion" );
      string name = xml_value_str(component_node);
      string version = xml_value_str(version_node);
      
      if( !version.empty() )
      {
        if( name.empty() )
          name = "main";
        analysis.algorithm_component_versions_.push_back( make_pair(name, version) );
      }
    }//foreach( AnalysisAlgorithmVersion 0
    
    
    if( analysis.algorithm_component_versions_.empty() && nuc_ana_node )
    {
      const rapidxml::xml_attribute<char> *algorithm_version_att = nuc_ana_node->first_attribute( "AlgorithmVersion", 16 );
      if( algorithm_version_att && algorithm_version_att->value_size() )
        analysis.algorithm_component_versions_.push_back( make_pair("main", xml_value_str(algorithm_version_att) ) );
    }
    
    auto &versions = analysis.algorithm_component_versions_;
    
    for( auto &cv : versions ) //RadEagle has a new line in it...
    {
      trim( cv.first );
      trim( cv.second );
    }
    
    versions.erase( std::remove_if( begin(versions), end(versions),
                                   [](const pair<string,string> &p) -> bool { return p.second.empty(); } ), end(versions) );
    
    const rapidxml::xml_node<char> *algo_creator_node = XML_FIRST_NODE( analysis_node, "AnalysisAlgorithmCreatorName" );
    if( !algo_creator_node && algo_info_node )
      algo_creator_node = XML_FIRST_NODE( algo_info_node, "AlgorithmVendor" );
    analysis.algorithm_creator_ = xml_value_str(algo_creator_node);
    
    const rapidxml::xml_node<char> *algo_desc_node = XML_FIRST_NODE( analysis_node, "AnalysisAlgorithmDescription" );
    analysis.algorithm_description_ = xml_value_str(algo_desc_node);
    
    
    if( analysis.algorithm_description_.empty() && algo_info_node )
    {
      //RadSeeker specific
      const rapidxml::xml_node<char> *FirmwareVersion = XML_FIRST_NODE( algo_info_node, "FirmwareVersion" );
      const rapidxml::xml_node<char> *SoftwareVersion = XML_FIRST_NODE( algo_info_node, "SoftwareVersion" );
      
      string desc;
      if( FirmwareVersion && FirmwareVersion->value_size() )
        desc += "FirmwareVersion: " + xml_value_str(FirmwareVersion);
      if( SoftwareVersion && SoftwareVersion->value_size() )
        desc += string(desc.empty()?"":", ") + "SoftwareVersion: " + xml_value_str(SoftwareVersion);
      
      for( const rapidxml::xml_node<char> *Parameters = XML_FIRST_NODE( algo_info_node, "Parameters" );
          Parameters;
          Parameters = XML_NEXT_TWIN(Parameters) )
      {
        const rapidxml::xml_attribute<char> *name = XML_FIRST_ATTRIB(Parameters, "ParameterName");
        const rapidxml::xml_attribute<char> *value XML_FIRST_ATTRIB(Parameters, "ParameterValue");
        if( name && value && name->value_size() && value->value_size() )
          desc += string(desc.empty()?"":", ") + xml_value_str(name) + ": " + xml_value_str(value);
      }//for( loop over parameters )
      
      analysis.algorithm_description_.swap( desc );
    }//if( analysis.algorithm_description_.empty() && algo_info_node )
    
    const rapidxml::xml_node<char> *AnalysisStartDateTime = XML_FIRST_NODE( analysis_node, "AnalysisStartDateTime" );
    if( AnalysisStartDateTime )
      analysis.analysis_start_time_ = time_from_string( xml_value_str(AnalysisStartDateTime).c_str() );
    
    const rapidxml::xml_node<char> *anadur = XML_FIRST_NODE( analysis_node, "AnalysisComputationDuration" );
    if( anadur )
      analysis.analysis_computation_duration_ = SpecUtils::time_duration_string_to_seconds( anadur->value(), anadur->value_size() );
    
    const rapidxml::xml_node<char> *result_desc_node = XML_FIRST_NODE( analysis_node, "AnalysisResultDescription" );
    if( !result_desc_node )
      result_desc_node = XML_FIRST_NODE( analysis_node, "ThreatDescription" );
    analysis.algorithm_result_description_ = xml_value_str(result_desc_node);
    
    
    //Should loop over NuclideAnalysis/ NuclideAnalysisResults ...
    for( ; nuc_ana_node; nuc_ana_node = XML_NEXT_TWIN(nuc_ana_node) )
    {
      for( const rapidxml::xml_node<char> *nuclide_node = XML_FIRST_NODE( nuc_ana_node, "Nuclide" );
          nuclide_node;
          nuclide_node = XML_NEXT_TWIN(nuclide_node) )
      {
        const rapidxml::xml_node<char> *remark_node = XML_FIRST_NODE(nuclide_node, "Remark");
        const rapidxml::xml_node<char> *nuclide_name_node = XML_FIRST_NODE(nuclide_node, "NuclideName");
        const rapidxml::xml_node<char> *nuclide_type_node = XML_FIRST_NODE(nuclide_node, "NuclideType");
        const rapidxml::xml_node<char> *confidence_node = XML_FIRST_NODE( nuclide_node, "NuclideIDConfidenceIndication" );  //N42-2006?
        if( !confidence_node )
          confidence_node = XML_FIRST_NODE(nuclide_node, "NuclideIdentificationConfidence");  //N42-2006?
        if( !confidence_node )
          confidence_node = XML_FIRST_NODE(nuclide_node, "NuclideIDConfidence");  //RadSeeker
        if( !confidence_node )
          confidence_node = XML_FIRST_NODE(nuclide_node, "NuclideIDConfidenceValue");  //N42-2012
        if( !confidence_node )
          confidence_node = XML_FIRST_NODE(nuclide_node, "NuclideIDConfidenceDescription");  //N42-2012
        
        const rapidxml::xml_node<char> *id_desc_node = XML_FIRST_NODE(nuclide_node, "NuclideIDConfidenceDescription");
        const rapidxml::xml_node<char> *position_node = XML_FIRST_NODE(nuclide_node, "SourcePosition");
        const rapidxml::xml_node<char> *id_indicator_node = XML_FIRST_NODE(nuclide_node, "NuclideIdentifiedIndicator"); //says 'true' or 'false', seen in refZ077SD6DVZ
        const rapidxml::xml_node<char> *confidence_value_node = XML_FIRST_NODE(nuclide_node, "NuclideIDConfidenceValue"); //seen in refJHQO7X3XFQ (FLIR radHUNTER UL-LGH)
        const rapidxml::xml_node<char> *catagory_desc_node = XML_FIRST_NODE(nuclide_node, "NuclideCategoryDescription");  //seen in refQ7M2PV6MVJ
        
        //Some files list true/false if a nuclide in its "trigger" list is present
        //  If a nuclide is not there, lets not include it in results.
        if( id_indicator_node && XML_VALUE_ICOMPARE(id_indicator_node, "false") )
        {
          //Could also check if NuclideIDConfidenceValue is there and if so make sure its value is equal to "0"
          continue;
        }
        
        DetectorAnalysisResult result;
        result.remark_ = xml_value_str(remark_node);
        result.nuclide_ = xml_value_str(nuclide_name_node);
        
        if( nuclide_type_node && nuclide_type_node->value_size() )
          result.nuclide_type_ = xml_value_str(nuclide_type_node);
        else if( catagory_desc_node && catagory_desc_node->value_size() )
          result.nuclide_type_ = xml_value_str(catagory_desc_node);
        
        if( confidence_node && confidence_node->value_size() )
          result.id_confidence_ = xml_value_str(confidence_node);
        else if( confidence_value_node && confidence_value_node->value_size() )
          result.id_confidence_ = xml_value_str(confidence_value_node);
        
        const rapidxml::xml_node<char> *nuc_activity_node = XML_FIRST_NODE(nuclide_node, "NuclideActivityValue");
        if( nuc_activity_node && nuc_activity_node->value_size() )
        {
          const rapidxml::xml_attribute<char> *activity_units_att = nuc_activity_node->first_attribute( "units", 5 );
          
          double activity_units = 1.0e+3;
          if( activity_units_att && activity_units_att->value_size() )
          {
            //This is a mini implimentation of PhysicalUnits::stringToActivity(...)
            string letters = xml_value_str(activity_units_att);
            
            if( istarts_with(letters, "n" ) )
              activity_units = 1.0E-9;
            else if( istarts_with(letters, "u" )
                    || istarts_with(letters, "micro" )
                    || istarts_with(letters, "\xc2\xb5" ) )
              activity_units = 1.0E-6;
            else if( starts_with(letters, "m" )
                    || istarts_with(letters, "milli" ) )
              activity_units = 1.0E-3;
            else if( istarts_with(letters, "b" )
                    || istarts_with(letters, "c" ) )
              activity_units = 1.0;
            else if( istarts_with(letters, "k" ) )
              activity_units = 1.0E+3;
            else if( starts_with(letters, "M" )
                    || istarts_with(letters, "mega" ) )
              activity_units = 1.0E+6;
            else
              activity_units = 0.0;
            
            const bool hasb = icontains( letters, "b" );
            const bool hasc = icontains( letters, "c" );
            
            if( hasc && !hasb )
              activity_units *= 3.7e+10;
            else
              activity_units = 1.0e+3; //0.0;
          }//if( activity_units_att && activity_units_att->value() )
          
          xml_value_to_flt(nuc_activity_node, result.activity_);
          result.activity_ *= static_cast<float>( activity_units );
        }//if( nuc_activity_node && nuc_activity_node->value_size() )
        
        if( position_node )
        {
          const rapidxml::xml_node<char> *location_node = XML_FIRST_NODE(position_node, "RelativeLocation");
          if( location_node )
          {
            const rapidxml::xml_node<char> *dist_node = XML_FIRST_NODE(location_node, "DistanceValue");
            //Could check 'units' attribute, but only valid value is "m"
            if( xml_value_to_flt(dist_node, result.distance_ ) )
              result.distance_ *= 1000.0;
          }//if( position_node )
        }//if( position_node )
        
        
        const rapidxml::xml_node<char> *extention_node = XML_FIRST_NODE(nuclide_node, "NuclideExtension");
        if( extention_node )
        {
          const rapidxml::xml_node<char> *SampleRealTime = XML_FIRST_NODE(extention_node, "SampleRealTime");
          const rapidxml::xml_node<char> *Detector = XML_FIRST_NODE(extention_node, "Detector");
          
          if( SampleRealTime && SampleRealTime->value_size() )
            result.real_time_ = time_duration_string_to_seconds( SampleRealTime->value(), SampleRealTime->value_size() );
          
          result.detector_ = xml_value_str(Detector);
        }//if( extention_node )
        
        
        //Some N42 files include analysis results for all nuclides, even if they
        //  arent present, let not keep those.
        if( id_desc_node && XML_VALUE_ICOMPARE(id_desc_node, "Not present") )
        {
          continue;
        }
        
        if( result.isEmpty() )
          continue;
        
        //    cerr << "result.remark_=" << result.remark_
        //         << ", result.nuclide_=" << result.nuclide_
        //         << ", result.nuclide_type_=" << result.nuclide_type_
        //         << ", result.id_confidence_=" << result.id_confidence_ << endl;
        
        analysis.results_.push_back( result );
      }//For( loop over nuclide analysis )
    }//for( ; nuc_ana_node; nuc_ana_node = XML_NEXT_TWIN(nuc_ana_node) )
    
    for( const rapidxml::xml_node<char> *dose_node = XML_FIRST_NODE(analysis_node, "DoseAnalysisResults");
        dose_node;
        dose_node = XML_NEXT_TWIN(dose_node) )
    {
      const rapidxml::xml_node<char> *remark_node = XML_FIRST_NODE(dose_node, "Remark");
      //const rapidxml::xml_node<char> *start_time_node = XML_FIRST_NODE(dose_node, "StartTime");
      const rapidxml::xml_node<char> *avrg_dose_node = XML_FIRST_NODE(dose_node, "AverageDoseRateValue");
      const rapidxml::xml_node<char> *total_dose_node = XML_FIRST_NODE(dose_node, "TotalDoseValue");
      const rapidxml::xml_node<char> *position_node = XML_FIRST_NODE(dose_node, "SourcePosition");
      
      DetectorAnalysisResult result;
      result.remark_ = xml_value_str(remark_node);
      
      //if( start_time_node && start_time_node->value() )
      //result.start_time_ = time_from_string( xml_value_str(start_time_node).c_str() );
      
      //assuming in units of micro-sievert per hour (could check 'units' attribute
      //  but only valid value is uSv/h anyway
      xml_value_to_flt(avrg_dose_node, result.dose_rate_ );
      
      if( total_dose_node )
      {
        float total_dose;
        xml_value_to_flt(total_dose_node, total_dose );
        if( (result.dose_rate_> 0.0f) && (total_dose > 0.0f) )
        {
          result.real_time_ = total_dose / result.dose_rate_;
        }else if( total_dose > 0.0f )
        {
          //Uhhhg, this isnt correct, but maybe better than nothing?
          result.dose_rate_ = total_dose;
        }
      }
      
      if( position_node )
      {
        const rapidxml::xml_node<char> *location_node = XML_FIRST_NODE(position_node, "RelativeLocation");
        if( location_node )
        {
          const rapidxml::xml_node<char> *dist_node = XML_FIRST_NODE(location_node, "DistanceValue");
          
          //Could check 'units' attribute, but only valid value is "m"
          if( xml_value_to_flt( dist_node, result.distance_ ) )
            result.distance_ *= 1000.0;
        }//if( position_node )
      }//if( position_node )
      
      
      if( !result.isEmpty() )
      {
        // TODO: DetectorAnalysisResult should be split into NuclideAnalysisResults and DoseAnalysisResults; right now cleanup_after_load will split things up, but very poorly.
        analysis.results_.push_back( result );
      }
    }//for( loop over DoseAnalysisResults nodes )
  }//void set_analysis_info_from_n42(...)
  
  
  

  bool Measurement::write_2006_N42_xml( std::ostream& ostr ) const
  {
    const char *endline = "\r\n";
    
    //ostr << "  <Measurement>" << endline;
    
    string detname = detector_name_;
    if( detname == "" )
      detname = s_unnamed_det_placeholder;
    
    
    if( contained_neutron_ )
    {
      ostr << "    <CountDoseData DetectorType=\"Neutron\">" << endline
           << "      <Counts>" << neutron_counts_sum_ << "</Counts>" << endline;
      if( (dose_rate_ >= 0) && (!gamma_counts_ || gamma_counts_->empty()) )
        ostr << "      <DoseRate Units=\"mrem\">" << 0.1*dose_rate_ << "</DoseRate>" << endline;  //0.1 mrem = 1 uSv
      ostr << "    </CountDoseData>" << endline;
    }//if( contained_neutron_ )
    
    
    if( (dose_rate_ >= 0) && gamma_counts_ && !gamma_counts_->empty() )
    {
      ostr << "    <CountDoseData DetectorType=\"Gamma\">" << endline
           << "      <DoseRate Units=\"mrem\">" << 0.1*dose_rate_ << "</DoseRate>" << endline  //0.1 mrem = 1 uSv
           << "    </CountDoseData>" << endline;
    }//if( (dose_rate_ >= 0) && gamma_counts_ && !gamma_counts_->empty() )
    
    
    //XXX - deviation pairs completely untested, and probably not conformant to
    //      N42 2006 specification (or rather the dndons extension).
    //XXX - also, could put more detector information here!
    /*
     if( deviation_pairs_.size() )
     {
     ostr << "    <InstrumentInformation>" << endline;
     ostr << "      <dndons:NonlinearityCorrection";
     if( detector_name_.size() )
     ostr << " Detector=\"" << detector_name_ << "\"";
     ostr << ">";
     
     for( size_t i = 0; i < deviation_pairs_.size(); ++i )
     {
     ostr << "        <dndons:Deviation>"
     << deviation_pairs_[i].first << " " << deviation_pairs_[i].second
     << "</dndons:Deviation>" << endline;
     }//for( size_t i = 0; i < deviation_pairs_.size(); ++i )
     
     ostr << "      </dndons:NonlinearityCorrection>";
     ostr << "    </InstrumentInformation>" << endline;
     }//if( deviation_pairs_.size() )
     */
    
    //Further information we should write
    //   std::string detector_name_;
    //   int detector_number_;
    //   QualityStatus quality_status_;
    //   std::string title_;
    
    /*
     if( SpecUtils::valid_latitude(latitude_)
     && SpecUtils::valid_longitude(longitude_) )
     {
     ostr << "    <MeasuredItemInformation>" << endline
     << "      <MeasurementLocation>" << endline;
     
     //The SpecFile class contains the member variable
     //      measurement_location_name_, so we cant write the following
     //  if( measurement_location_name_.size() )
     //    ostr << "        <MeasurementLocationName>" << measurement_location_name_
     //         << "</MeasurementLocationName>" << endline;
     ostr << "        <Coordinates";
     if( is_special(position_time_) )
     ostr << ">";
     else
     ostr << " Time=\"" << SpecUtils::to_extended_iso_string(position_time_) << "Z\">";
     
     ostr << latitude_ << " " << longitude_ << "</Coordinates>" << endline;
     ostr << "      </MeasurementLocation>" << endline;
     ostr << "    </MeasuredItemInformation>" << endline;
     }//if( valid(latitude_) && !valid(longitude) )
     */
    
    
    ostr << "    <Spectrum Type=\"PHA\"";
    
    ostr << " Detector=\"" << detname << "\"";
    if( sample_number_ > 0 )
      ostr << " SampleNumber=\"" << sample_number_ << "\"";
    
    switch( quality_status_ )
    {
      case QualityStatus::Good:    ostr << " Quality=\"Good\""; break;
      case QualityStatus::Suspect: ostr << " Quality=\"Suspect\""; break;
      case QualityStatus::Bad:     ostr << " Quality=\"Bad\""; break;
      case QualityStatus::Missing:
        //      ostr << " Quality=\"Missing\"";
        break;
    }//switch( quality_status_ )
    
    ostr << ">" << endline;
    
    vector<string> remarks;
    
    if( title_.size() )
      remarks.push_back( "Title: " + title_ );
    
    bool wroteSurvey = false, wroteName = false, wroteSpeed = false;
    
    for( size_t i = 0; i < remarks_.size(); ++i )
    {
      const string &remark = remarks_[i];
      remarks.push_back( remark );
      
      if( i == 0 )
      {
        wroteSurvey = (remark.find( "Survey" ) != string::npos);
        wroteName   = (remark.find( detector_name_ ) != string::npos);
        wroteSpeed  = (remark.find( "Speed" ) != string::npos);
      }//if( i == 0 )
    }//for( size_t i = 0; i < remarks_.size(); ++i )
    
    if( remarks_.empty()
       && (sample_number_>=0 || !detector_name_.empty() || (location_ && !IsNan(location_->speed_)))
       )
    {
      string thisremark;
      if( sample_number_>=0 && !wroteSurvey )
      {
        thisremark = "Survey " + std::to_string(sample_number_);
      }
      
      if( !detector_name_.empty() && !wroteName )
      {
        if(!thisremark.empty())
          thisremark += " ";
        thisremark += detector_name_;
      }
      
      if( (location_ && !IsNan(location_->speed_)) && !wroteSpeed )
      {
        if(!thisremark.empty())
          thisremark += " ";
        thisremark += "Speed " + std::to_string(location_->speed_) + " m/s";
      }
      trim( thisremark );
      
      if(!thisremark.empty())
        remarks.push_back( thisremark );
    }//if( remarks_.empty() )
    
    //We're only allowed to have one Remark element here.
    if(!remarks.empty())
    {
      ostr << "      <Remark>";
      for( size_t i = 0; i < remarks.size(); ++i )
      {
        if( i )
          ostr << endline;
        ostr << remarks[i];
      }
      
      ostr << "</Remark>";
    }//if( remarks.size() )
    
    /*
     switch( occupied_ )
     {
     case Unknown:
     break;
     case Occupied:
     case NotOccupied:
     ostr << "      <Occupied>" << (occupied_==Occupied)
     << "</Occupied>" << endline;
     break;
     }//switch( occupied_ )
     */
    
    /*
     if( !is_special(start_time_) )
     ostr << "      <StartTime>"
     << SpecUtils::to_extended_iso_string(start_time_)
     << "Z</StartTime>" << endline;
     //    ostr << "      <StartTime>" << start_time_ << "</StartTime>" << endline;
     */
    
    ostr << "      <RealTime>PT" << real_time_ << "S</RealTime>" << endline;
    ostr << "      <LiveTime>PT" << live_time_ << "S</LiveTime>" << endline;
    
    switch( source_type_ )
    {
      case SourceType::IntrinsicActivity: ostr << "      <SourceType>Other</SourceType>" << endline; break; break;
      case SourceType::Calibration:       ostr << "      <SourceType>Calibration</SourceType>" << endline; break;
      case SourceType::Background:        ostr << "      <SourceType>Background</SourceType>" << endline; break;
      case SourceType::Foreground:        ostr << "      <SourceType>Item</SourceType>" << endline; break;
      case SourceType::Unknown: break;
    }//switch( source_type_ )
    
    if( !detector_description_.empty() )
      ostr << "      <DetectorType>" << detector_description_ << "</DetectorType>" << endline;
    
    ostr << "      <Calibration Type=\"Energy\" EnergyUnits=\"keV\">" << endline
         << "        <Equation Model=\"";
    
    assert( energy_calibration_ );
      
    switch( energy_calibration_->type() )
    {
      case SpecUtils::EnergyCalType::Polynomial:
      case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
        ostr << "Polynomial";
        break;
        
      case SpecUtils::EnergyCalType::FullRangeFraction:    ostr << "FullRangeFraction"; break;
      case SpecUtils::EnergyCalType::LowerChannelEdge:     ostr << "LowerChannelEdge";  break;
      case SpecUtils::EnergyCalType::InvalidEquationType:  ostr << "Unknown";           break;
    }//switch( energy_calibration_->type() )
    
    ostr << "\">" << endline;
    
    ostr << "          <Coefficients>";
    
    const vector<float> cal_coeffs = energy_calibration_->coefficients();
    for( size_t i = 0; i < cal_coeffs.size(); ++i )
      ostr << (i ? " " : "") << cal_coeffs[i];
    ostr << "</Coefficients>" << endline
    << "        </Equation>" << endline
    << "      </Calibration>" << endline;
    
    ostr << "      <ChannelData Compression=\"CountedZeroes\">";
    
    vector<float> compressed_counts;
    compress_to_counted_zeros( *gamma_counts_, compressed_counts );
    
    const size_t nCompressChannels = compressed_counts.size();
    for( size_t i = 0; i < nCompressChannels; ++i )
    {
      const size_t pos = (i%12);
      if( pos == 0 )
        ostr << endline;
      else
        ostr << " ";
      if( compressed_counts[i] == 0.0 )
        ostr << "0";
      else ostr << compressed_counts[i];
    }//for( size_t i = 0; i < compressed_counts.size(); ++i )
    
    ostr << "      </ChannelData>" << endline
    << "    </Spectrum>" << endline
    //       << "</Measurement>" << endline;
    ;
    
    return true;
  }//bool write_2006_N42_xml( std::ostream& ostr ) const
  
  
  bool SpecFile::write_2006_N42( std::ostream& ostr ) const
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    const char *endline = "\r\n";
    
    ostr << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endline
    << "<N42InstrumentData xmlns=\"http://physics.nist.gov/Divisions/Div846/Gp4/ANSIN4242/2005/ANSIN4242\"" << endline
    << "xmlns:n42ns=\"http://physics.nist.gov/Divisions/Div846/Gp4/ANSIN4242/2005/ANSIN4242\"" << endline
    << "xmlns:dndons=\"http://www.DNDO.gov/N42Schema/2006/DNDOSchema\"" << endline
    << "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"" << endline
    << "xmlns:Cambio=\"Cambio\"" << endline
    << "xmlns:DHS=\"DHS\"" << endline
    << "xsi:schemaLocation=\"http://physics.nist.gov/Divisions/Div846/Gp4/ANSIN4242/2005/ANSIN4242" << endline
    << "http://physics.nist.gov/Divisions/Div846/Gp4/ANSIN4242/2005/ANSIN4242.xsd\">" << endline;
    
    ostr << "<Measurement UUID=\"" << uuid_ << "\">" << endline;
    
    ostr << "  <InstrumentInformation>" << endline;
    
    if(!instrument_type_.empty())
    {
      if( instrument_type_ == "PortalMonitor" || instrument_type_ == "SpecPortal"
         || instrument_type_ == "RadionuclideIdentifier" || instrument_type_ == "PersonalRadiationDetector"
         || instrument_type_ == "SurveyMeter" || instrument_type_ == "Spectrometer"
         || instrument_type_ == "Other" )
      {
        ostr << "    <InstrumentType>" << instrument_type_ << "</InstrumentType>" << endline;
      }else if( instrument_type_ == "Portal Monitor" )
        ostr << "    <InstrumentType>PortalMonitor</InstrumentType>" << endline;
      else if( instrument_type_ == "Radionuclide Identifier" )
        ostr << "    <InstrumentType>RadionuclideIdentifier</InstrumentType>" << endline;
      else if( instrument_type_ == "Spectroscopic Portal Monitor" )
        ostr << "    <InstrumentType>SpecPortal</InstrumentType>" << endline;
      else if( instrument_type_ == "Personal Radiation Detector" )
        ostr << "    <InstrumentType>PersonalRadiationDetector</InstrumentType>" << endline;
      else if( instrument_type_ == "Spectroscopic Personal Radiation Detector" )
        ostr << "    <InstrumentType>PersonalRadiationDetector</InstrumentType>" << endline;
      else if( instrument_type_ == "Transportable System" )
        ostr << "    <InstrumentType>Other</InstrumentType>" << endline;
      else if( instrument_type_ == "Gamma Handheld" )
        ostr << "    <InstrumentType>Spectrometer</InstrumentType>" << endline;
      else
        ostr << "<!-- <InstrumentType>" << instrument_type_ << "</InstrumentType> -->" << endline;
    }//if( instrument_type_.size() )
    
    if(!manufacturer_.empty())
      ostr << "    <Manufacturer>" << manufacturer_ << "</Manufacturer>" << endline;
    if(!instrument_model_.empty())
      ostr << "    <InstrumentModel>" << instrument_model_ << "</InstrumentModel>" << endline;
    if(!instrument_id_.empty())
      ostr << "    <InstrumentID>" << instrument_id_ << "</InstrumentID>" << endline;
    
    for( const string &detname : detector_names_ )
    {
      ostr << "    <dndons:DetectorStatus Detector=\""
      << (detname.empty() ? s_unnamed_det_placeholder : detname)
      << "\" Operational=\"true\"/>" << endline;
    }
    
    vector<string> unwrittendets = detector_names_;
    
    for( size_t i = 0; !unwrittendets.empty() && i < measurements_.size(); ++i )
    {
      const std::shared_ptr<Measurement> &meas = measurements_[i];
      if( !meas )
        continue;
      
      vector<string>::iterator pos = std::find( unwrittendets.begin(), unwrittendets.end(), meas->detector_name_ );
      if( pos == unwrittendets.end() )
        continue;
      unwrittendets.erase( pos );
      
      if( !meas->gamma_counts() || meas->gamma_counts()->empty() )
        continue;
      
      string name = meas->detector_name_;
      if( name == "" )
        name = s_unnamed_det_placeholder;
      
      assert( meas->energy_calibration_ );
      const auto &dev_pairs = meas->energy_calibration_->deviation_pairs();
      if( dev_pairs.size() )
      {
        ostr << "    <dndons:NonlinearityCorrection Detector=\"" << name << "\">" << endline;
        for( size_t j = 0; j < dev_pairs.size(); ++j )
        {
          ostr << "      <dndons:Deviation>" << dev_pairs[j].first
               << " " << dev_pairs[j].second << "</dndons:Deviation>" << endline;
        }
        ostr << "    </dndons:NonlinearityCorrection>" << endline;
      }
    }//for( size_t i = 0; i < measurements.size(); ++i )
    
    
    
    if( measurement_location_name_.size() || measurement_operator_.size() )
    {
      ostr << "    <MeasuredItemInformation>" << endline;
      if( measurement_location_name_.size() )
        ostr << "      <MeasurementLocationName>" << measurement_location_name_ << "</MeasurementLocationName>" << endline;
      //<Coordinates>40.12 10.67</Coordinates>
      if( measurement_operator_.size() )
        ostr << "      <MeasurementOperator>" << measurement_operator_ << "</MeasurementOperator>" << endline;
      ostr << "    </MeasuredItemInformation>" << endline;
    }
    
    ostr << "  </InstrumentInformation>" << endline;
    
    if( inspection_.size() )
      ostr << "  <dndons:Inspection>" << inspection_ << "</dndons:Inspection>" << endline;
    
    
    for( const int samplenum : sample_numbers_ )
    {
      vector<std::shared_ptr<const Measurement>> meass = sample_measurements( samplenum );
      if( meass.empty() )
        continue;
      
      //Look for min start time
      time_point_t starttime = meass[0]->start_time();
      float rtime = meass[0]->real_time_;
      float speed = meass[0]->location_ ? meass[0]->location_->speed_ : numeric_limits<float>::quiet_NaN();
      OccupancyStatus occstatus = meass[0]->occupied_;
      
      for( size_t i = 1; i < meass.size(); ++i )
      {
        const time_point_t tst = meass[i]->start_time();
        starttime = ((is_special(tst) || (starttime < tst)) ? starttime : tst);
        rtime = max( rtime, meass[i]->real_time_ );
        
        if( meass[i]->location_ )
        {
          if( IsNan(speed) )
            speed = meass[i]->location_->speed_; //may still be NaN
          else if( !IsNan(meass[i]->location_->speed_) )
            speed = max( speed, meass[i]->location_->speed_ );
        }//if( meass[i]->location_ )
        
        if( occstatus == OccupancyStatus::Unknown )
          occstatus = meass[i]->occupied_;
        else if( meass[i]->occupied_ != OccupancyStatus::Unknown )
          occstatus = max( occstatus, meass[i]->occupied_ );
      }
      
      
      ostr << "  <DetectorData>" << endline;
      if( !is_special(starttime) )
        ostr << "    <StartTime>" << SpecUtils::to_extended_iso_string(starttime) << "Z</StartTime>" << endline;
      if( rtime > 0.0f )
        ostr << "    <SampleRealTime>PT" << rtime << "S</SampleRealTime>" << endline;
      if( occstatus != OccupancyStatus::Unknown )
        ostr << "    <Occupied>" << (occstatus== OccupancyStatus::NotOccupied ? "0" : "1") << "</Occupied>" << endline;
      if( !IsNan(speed) )
        ostr << "    <Speed Units=\"m/s\">" << speed << "</Speed>" << endline;
      
      string detsysname = measurement_location_name_;
      if( lane_number_ >= 0 )
        detsysname += "Lane" + std::to_string(lane_number_);
      if( inspection_.size() )
        detsysname += inspection_;
      if( detsysname.empty() )
        detsysname = "detector";
      
      ostr << "    <DetectorMeasurement Detector=\"" << detsysname << "\" DetectorType=\"Other\">" << endline;
      ostr << "      <SpectrumMeasurement>" << endline;
      ostr << "        <SpectrumAvailable>1</SpectrumAvailable>" << endline;
      
      for( const std::shared_ptr<const Measurement> meas : meass )
        meas->write_2006_N42_xml( ostr );
      
      ostr << "      </SpectrumMeasurement>" << endline;
      ostr << "    </DetectorMeasurement>" << endline;
      ostr << "  </DetectorData>" << endline;
    }
    
    //for( const std::shared_ptr<const Measurement> meas : measurements_ )
    //meas->write_2006_N42_xml( ostr );
    
    
    ostr << "</Measurement>" << endline;
    ostr << "</N42InstrumentData>" << endline;
    
    return !ostr.bad();
  }//bool write_2006_N42( std::ostream& ostr ) const

  void Measurement::set_info_from_2006_N42_spectrum_node( const rapidxml::xml_node<char> * const spectrum )
  {
    if( !spectrum )
      throw std::runtime_error( "Measurement::set_info_from_2006_N42_spectrum_node: invalid input" );
      
    N42CalibrationCache2006 energy_cal( spectrum->document() );
    N42DecodeHelper2006::decode_2006_n42_spectrum_node( spectrum, energy_cal, *this );
      
  }//set_info_from_2006_N42_spectrum_node(...)
}//namespace SpecUtils



