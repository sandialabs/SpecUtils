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
#include <vector>
#include <memory>
#include <string>
#include <cctype>
#include <limits>
#include <numeric>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <functional>

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
    const string node_name = SpecUtils::xml_name_str(node);
    const size_t colon_pos = node_name.find(':');
    if( colon_pos != string::npos && SpecUtils::icontains(node_name, "n42") )
      return node_name.substr( 0, colon_pos + 1 );
    return "";
  }//std::string get_n42_xmlns( const rapidxml::xml_node<char> *node )

}//anonomous namespace for XML utilities


//getCalibrationToSpectrumMap(...): builds map from the binning shared vector to
//  the the index of a std::shared_ptr<Measurement> that has that binning
namespace
{
  typedef std::map< std::shared_ptr< const std::vector<float> >, size_t > BinningToIndexMap;
  
  void insert_N42_calibration_nodes( const std::vector< std::shared_ptr<SpecUtils::Measurement> > &measurements,
                                    ::rapidxml::xml_node<char> *RadInstrumentData,
                                    std::mutex &xmldocmutex,
                                    BinningToIndexMap &calToSpecMap )
  {
    using namespace rapidxml;
    calToSpecMap.clear();
    
    for( size_t i = 0; i < measurements.size(); ++i )
    {
      const std::shared_ptr<SpecUtils::Measurement> &meas = measurements[i];
      
      if( !meas || !meas->gamma_counts() || meas->gamma_counts()->empty() )
        continue;
      
      std::shared_ptr< const std::vector<float> > binning = meas->channel_energies();
      BinningToIndexMap::const_iterator iter = calToSpecMap.find( binning );
      if( iter == calToSpecMap.end() )
      {
        calToSpecMap[binning] = i;
        meas->add_calibration_to_2012_N42_xml( RadInstrumentData, xmldocmutex, int(i) );
      }//if( iter == calToSpecMap.end() )
    }//for( size_t i = 0; i < measurements.size(); ++i )
  }//void insert_N42_calibration_nodes(...)
}//namespace


namespace
{
  //anaonomous namespace for functions to help parse N42 files, that wont be
  //  usefull outside of this file
  
  //is_gamma_spectrum(): Trys to determine  if the spectrum_node cooresponds
  //  to a gamma or neutron node in the XML.
  //  Will throw std::runtime_exception if if cant unambiguosly tell if its a
  //  gamma spectrum or not
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
         || SpecUtils::icontains(det_type,"GMTube") )
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
    
    
    if( is_nuetron == is_gamma )
    {
      //We should probably just assume its a gamma detector here....
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
      
      throw std::runtime_error( msg.str() );
    }//if( is_nuetron && is_gamma )
    
    return is_gamma;
  }//bool is_gamma_spectrum()
  
  
  //is_occupied(): throws exception on error
  bool is_occupied( const rapidxml::xml_node<char> *uccupied_node )
  {
    if( uccupied_node && uccupied_node->value_size() )
    {
      bool occupied = false;
      
      if( uccupied_node->value()[0] == '0' )
        occupied = false;
      else if( uccupied_node->value()[0] == '1' )
        occupied = true;
      else if( XML_VALUE_ICOMPARE(uccupied_node, "true") )
        occupied = true;
      else if( XML_VALUE_ICOMPARE(uccupied_node, "false") )
        occupied = false;
      else
      {
        stringstream msg;
        msg << SRC_LOCATION << "\n\tUnknown Occupied node value: '"
        << SpecUtils::xml_value_str(uccupied_node) << "'";
        cerr << msg.str() << endl;
        throw std::runtime_error( msg.str() );
      }
      
      return occupied;
    }//if( uccupied_node is valid )
    
    throw std::runtime_error( "NULL <Occupied> node" );
  }//bool is_occupied( rapidxml::xml_node<char> *uccupied_node )
  
  
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
      cerr << msg.str() << endl;
      throw runtime_error( msg.str() );
    }//if( couldnt convert to float
    
    if( speed < 0.00000001f )
      return 0.0f;
    
    const rapidxml::xml_attribute<char> *unit_attrib = XML_FIRST_ATTRIB( speed_node, "Units" );
    if( !unit_attrib || !unit_attrib->value_size() )
    {
      cerr << SRC_LOCATION << "\n\t:Warning no units attribut avaliable in "
      << "<Speed> node, assuming m/s" << endl;
      return speed;
    }//if( no unit attribute" )
    
    string units = SpecUtils::xml_value_str( unit_attrib );
    SpecUtils::trim( units );
    SpecUtils::to_lower_ascii( units );
    if( units == "mph" )
      return 0.44704f * speed;
    if( units == "m/s" )
      return speed;
    
    stringstream msg;
    msg << SRC_LOCATION << "\n\tUnknown speed units: '" << speed
    << "' - please fix";
    cerr << msg.str() << endl;
    throw std::runtime_error( msg.str() );
    
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
}//namespace


//A private namespace to contain some structs to help us to mult-threaded decoding.
namespace SpecUtils
{
  const string SpectrumNodeDecodeWorker_failed_decode_title = "AUniqueStringToMarkThatThisDecodingFailed";
  struct SpectrumNodeDecodeWorker
  {
    const rapidxml::xml_node<char> *m_spec_node;
    std::mutex *m_mutex;
    std::shared_ptr<SpecUtils::Measurement> m_meas;
    std::shared_ptr<SpecUtils::DetectorAnalysis> m_analysis_info;
    const rapidxml::xml_node<char> *m_dose_data_parent;
    const rapidxml::xml_node<char> *m_doc;
    
    SpectrumNodeDecodeWorker( const rapidxml::xml_node<char> *node_in,
                             std::mutex *mutex_ptr,
                             std::shared_ptr<SpecUtils::Measurement> meas,
                             std::shared_ptr<SpecUtils::DetectorAnalysis> analysis_info_ptr,
                             const rapidxml::xml_node<char> *dose_data_parent,
                             const rapidxml::xml_node<char> *doc_node )
    : m_spec_node( node_in ),
    m_mutex( mutex_ptr ),
    m_meas( meas ),
    m_analysis_info( analysis_info_ptr ),
    m_dose_data_parent( dose_data_parent ),
    m_doc( doc_node )
    {}
    
    static void filter_valid_measurments( vector< std::shared_ptr<SpecUtils::Measurement> > &meass )
    {
      vector< std::shared_ptr<SpecUtils::Measurement> > valid_meass;
      valid_meass.reserve( meass.size() );
      
      for( auto &meas : meass )
      {
        //      const bool hasGammaData = (meas->gamma_counts() && meas->gamma_counts()->size());
        //      const bool hasNeutData = (meas->neutron_counts().size() || meas->contained_neutron_);
        //      if( hasGammaData || hasNeutData )
        
        if( meas->title() != SpectrumNodeDecodeWorker_failed_decode_title )
          valid_meass.push_back( meas );
      }//for( std::shared_ptr<Measurement> &meas : meass )
      
      meass.swap( valid_meass );
    }
    
    void operator()()
    {
      try
      {
        const string xmlns = get_n42_xmlns(m_spec_node);
        
        m_meas->set_2006_N42_spectrum_node_info( m_spec_node );
        
        if( m_meas->calibration_coeffs().empty()
           && (!m_meas->channel_energies() || m_meas->channel_energies()->empty()) )
        {
          m_meas->set_n42_2006_spectrum_calibration_from_id( m_doc, m_spec_node );
        }
        
        if( m_dose_data_parent )
        {
          //If m_spec_node has any immediate siblings, then we need to be careful in setting
          //  The count dose informiaton, so lets count them
          int nspectra = 0;
          if( m_spec_node->parent() )
          {
            for( const rapidxml::xml_node<char> *node = m_spec_node->parent()->first_node( m_spec_node->name(), m_spec_node->name_size() );
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
              m_meas->set_n42_2006_count_dose_data_info( dose_data, m_analysis_info, m_mutex );
            }else
            {
              const rapidxml::xml_node<char> *starttime = XML_FIRST_NODE(dose_data, "StartTime");
              //const rapidxml::xml_node<char> *realtime = XML_FIRST_NODE(dose_data, "SampleRealTime");
              if( /*!realtime ||*/ !starttime )
                continue;
              
              const boost::posix_time::ptime startptime = SpecUtils::time_from_string( xml_value_str(starttime).c_str() );
              if( startptime.is_special() )
                continue;
              
              boost::posix_time::time_duration thisdelta = startptime - m_meas->start_time_;
              if( thisdelta < boost::posix_time::time_duration(0,0,0) )
                thisdelta = -thisdelta;
              //const float realtime = time_duration_string_to_seconds(realtime->value(), realtime->value_size());
              
              if( thisdelta < boost::posix_time::time_duration(0,0,10) )
                m_meas->set_n42_2006_count_dose_data_info( dose_data, m_analysis_info, m_mutex );
            }
          }//for( loop over CountDoseData nodes, dos )
        }//if( measurement_node_for_cambio )
        
        
        //HPRDS (see refF4TD3P2VTG) have start time and remark as siblings to
        //  m_spec_node
        const rapidxml::xml_node<char> *parent = m_spec_node->parent();
        if( parent )
        {
          for( const rapidxml::xml_node<char> *remark = xml_first_node_nso( parent, "Remark", xmlns );
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
          }//for( loop over spectrum parent remarks )
          
          
          const rapidxml::xml_node<char> *start_time = xml_first_node_nso( parent, "StartTime", xmlns );
          if( start_time && start_time->value_size() && m_meas->start_time_.is_special()
             && m_meas->source_type_ != SourceType::IntrinsicActivity )
            m_meas->start_time_ = time_from_string( xml_value_str(start_time).c_str() );
        }//if( parent )
      }
#if(PERFORM_DEVELOPER_CHECKS)
      catch( std::exception &e )
      {
        m_meas->reset();
        m_meas->title_ = SpectrumNodeDecodeWorker_failed_decode_title;
        if( !SpecUtils::icontains( e.what(), "didnt find <ChannelData>" ) )
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
        m_meas->title_ = SpectrumNodeDecodeWorker_failed_decode_title;
      }
#endif
    }//void operator()()
  };//struct SpectrumNodeDecodeWorker
  
  
  
  
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
  
  
  
  void SpecFile::set_n42_2006_deviation_pair_info( const rapidxml::xml_node<char> *info_node,
                                                  std::vector<std::shared_ptr<Measurement>> &measurs_to_update )
  {
    if( !info_node )
      return;
    
    set<string> det_names_set;
    
    for( const rapidxml::xml_node<char> *nl_corr_node = xml_first_node_nso( info_node, "NonlinearityCorrection", "dndons:" );
        nl_corr_node;
        nl_corr_node = XML_NEXT_TWIN(nl_corr_node) )
    {
      if( det_names_set.empty() )
        det_names_set = find_detector_names();
      
      const rapidxml::xml_attribute<char> *det_attrib = XML_FIRST_ATTRIB( nl_corr_node, "Detector" );
      
      if( !det_attrib && det_names_set.size()>1 )
      {
        cerr << SRC_LOCATION << "\n\tWarning, no Detector attribute "
        << "in <dndons:NonlinearityCorrection> node; skipping" << endl;
        continue;
      }//if( !det_attrib )
      
      const string det_name = det_attrib ? xml_value_str(det_attrib) : *(det_names_set.begin());
      bool have_det_with_this_name = false;
      for( const string &name : det_names_set )
      {
        //We may have appended "_intercal_" + "..." to the detector name, lets check
        have_det_with_this_name = (name == det_name || (istarts_with(name, det_name) && icontains(name, "_intercal_")));
        if( have_det_with_this_name )
          break;
      }
      
      if( !have_det_with_this_name )
      {
        cerr << SRC_LOCATION << "\n\tWarning: could find nedetctor name '"
        << det_name << "' in Measurements loaded, skipping deviation "
        << "pair" << endl;
        continue;
      }//if( an invalid detector name )
      
      vector< pair<float,float> > deviatnpairs;
      for( const rapidxml::xml_node<char> *dev_node = xml_first_node_nso( nl_corr_node, "Deviation", "dndons:" );
          dev_node;
          dev_node = XML_NEXT_TWIN(dev_node) )
      {
        if( dev_node->value_size() )
        {
          vector<float> devpair;
          const bool success = SpecUtils::split_to_floats( dev_node->value(), dev_node->value_size(), devpair );
          
          if( success && devpair.size()>=2 )
            deviatnpairs.push_back( pair<float,float>(devpair[0],devpair[1]) );
          else
            cerr << "Could not put '" << xml_value_str(dev_node) << "' into deviation pair" << endl;
        }//if( dev_node->value_size() )
      }//for( loop over <dndons:Deviation> )
      
      for( auto &meas : measurs_to_update )
        if( meas->detector_name_ == det_name ||
           (istarts_with(meas->detector_name_, det_name) && icontains(meas->detector_name_, "_intercal_")) )
          meas->deviation_pairs_ = deviatnpairs;
    }//for( loop over <dndons:NonlinearityCorrection> )
  }//void set_n42_2006_deviation_pair_info(...)
  
  
  void SpecFile::set_n42_2006_instrument_info_node_info( const rapidxml::xml_node<char> *info_node )
  {
    if( !info_node )
      return;
    
    string xmlns = get_n42_xmlns(info_node);
    if( xmlns.empty() && info_node->parent() )
      xmlns = get_n42_xmlns(info_node->parent());
    
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
      
      if( fields.size() == 1 )
      {
        //identiFINDER 2 NGH make it here
        //  "Gamma Detector: NaI 35x51 Neutron Detector: ³He tube 3He3/608/15NS"
        //RadEagle will also:
        //  "Gamma Detector: NaI 3x1 GM Tube: 6x25 Type 716 Neutron: He3"
        
        trim( fields[0] );
        string lowered = fields[0];  //We will make searches case independent
        SpecUtils::to_lower_ascii( lowered ); //We are relying on to_lower_ascii() not adding/removing bytes from string, which it doesnt since it is dumb (e.g., ASCII).
        
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
          remarks_.push_back( SpecUtils::trim_copy( fields[0].substr( posvec[i], len ) ) );
        }
        
        if( posvec.empty() )
        {
          //identiFINDER 2 NG makes it here. "Gamma Detector: NaI 35x51"
          remarks_.push_back( fields[0] );
        }
      }else
      {
        //Sam940s make it here
        for( string &field : fields )
        {
          trim( field );
          if( SpecUtils::istarts_with( field, "Serial") )
            instrument_id_ += ", Probe " + field;
          else if( SpecUtils::istarts_with( field, "Type") )
            instrument_model_ += "," + field.substr(4);
        }//for( string &field : fields )
      }
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
        if( type_attrib && XML_VALUE_ICOMPARE(type_attrib,"MCA") )
        {
          const rapidxml::xml_node<char> *id_settings = XML_FIRST_NODE(det, "IdentificationSettings");
          if( id_settings )
          {
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
        }
      }
    }//if( det_setup )
    
  }//void set_n42_2006_instrument_info_node_info( const rapidxml::xml_node<char> *info_node )
  
  
  
  void Measurement::set_n42_2006_count_dose_data_info( const rapidxml::xml_node<char> *dose_data,
                                                      std::shared_ptr<DetectorAnalysis> analysis_info,
                                                      std::mutex *analysis_mutex )
  {
    if( !dose_data )
      return;
    
    string xmlns = get_n42_xmlns(dose_data);
    if( xmlns.empty() && dose_data->parent() )
      xmlns = get_n42_xmlns(dose_data->parent());
    
    Measurement *m = this;
    
    //Detective N42 2006 files have a CountRate node that you need to multiply
    //  by the live time
    
    const rapidxml::xml_node<char> *count_node = xml_first_node_nso( dose_data, "CountRate", xmlns );
    const rapidxml::xml_node<char> *realtime_node = xml_first_node_nso( dose_data, "SampleRealTime", xmlns );
    
    
    const rapidxml::xml_attribute<char> *det_attrib = XML_FIRST_ATTRIB(dose_data, "DetectorType");
    
    if( count_node && count_node->value_size()
       && (!det_attrib || XML_VALUE_ICOMPARE(det_attrib, "Neutron")) )
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
        
        rapidxml::xml_node<char> *starttime_node = dose_data->first_node( "StartTime", 9 );
        if( starttime_node && starttime_node->value_size() )
        {
          boost::posix_time::ptime starttime = time_from_string( xml_value_str(starttime_node).c_str() );
          if( !starttime.is_special() && !m->start_time_.is_special() )
          {
            if( (starttime-m->start_time_) > boost::posix_time::minutes(1) )
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
    
    
    if( !det_attrib )
      return;
    
    if( XML_VALUE_ICOMPARE(det_attrib, "Neutron") )
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
            cerr << "Have both neutron spectrum and neutron dose count" << endl;
          
          //XXX
          //  We get this dose rate info from Cambio wether or not there was
          //  a neutron detector, so we will only mark the detector as a neutron
          //  detector if at least one sample has at least one neutron
          //   ... I'm not a huge fan of this...
          m->contained_neutron_ |= (m->neutron_counts_[0]>0.0);
        }else
          cerr << "Error converting neutron counts '" << xml_value_str(counts)
          << "' to float; ignoring" << endl;
      }//if( counts )
    }else if( XML_VALUE_ICOMPARE(det_attrib, "Gamma") )
    {
      const rapidxml::xml_node<char> *remark_node = xml_first_node_nso( dose_data, "Remark", xmlns );
      //const rapidxml::xml_node<char> *start_time_node = xml_first_node_nso( dose_data, "StartTime", xmlns );
      const rapidxml::xml_node<char> *real_time_node = xml_first_node_nso( dose_data, "SampleRealTime", xmlns );
      const rapidxml::xml_node<char> *dose_node = xml_first_node_nso( dose_data, "DoseRate", xmlns );
      
      DetectorAnalysisResult thisana;
      thisana.remark_ = xml_value_str( remark_node );
      //thisana.start_time_ = time_from_string( xml_value_str(start_time_node).c_str() );
      if( real_time_node && real_time_node->value_size() )
        thisana.real_time_ = time_duration_string_to_seconds( real_time_node->value(),
                                                             real_time_node->value_size() );
      if( dose_node && dose_node->value_size() )
      {
        try
        {
          const rapidxml::xml_attribute<char> *units_attrib = XML_FIRST_ATTRIB( dose_node, "Units" );
          if( !units_attrib || !units_attrib->value_size() )
            throw runtime_error( "No units for dose" );
          
          xml_value_to_flt( dose_node, thisana.dose_rate_ );
          thisana.dose_rate_ *= dose_units_usvPerH( units_attrib->value(), units_attrib->value_size() );
        }catch(...){}
      }//if( dose_node && dose_node->value_size() )
      
      if( !thisana.isEmpty() )
      {
        std::unique_ptr< std::lock_guard<std::mutex> > lock;
        if( analysis_mutex )
          lock.reset( new std::lock_guard<std::mutex>( *analysis_mutex ) );
        analysis_info->results_.push_back( thisana );
      }//if(
    }//if( neutron detector ) / else( gamma detector )
    
    
  }//void set_n42_2006_count_dose_data_info( const rapidxml::xml_node<char> *dose_data, std::shared_ptr<Measurement> m )
  
  
  
  void Measurement::set_n42_2006_gross_count_node_info( const rapidxml::xml_node<char> *gross_count_meas )
  {
    if( !gross_count_meas )
      throw runtime_error( "!gross_count_measurement" );
    
    string xmlns = get_n42_xmlns(gross_count_meas);
    if( xmlns.empty() && gross_count_meas->parent() )
      xmlns = get_n42_xmlns(gross_count_meas->parent());
    
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
  
  
  void Measurement::set_n42_2006_detector_data_node_info( const rapidxml::xml_node<char> *det_data_node,
                                                         std::vector<std::shared_ptr<Measurement>> &measurs_to_update )
  {
    string xmlns = get_n42_xmlns(det_data_node);
    if( xmlns.empty() && det_data_node && det_data_node->parent() )
      xmlns = get_n42_xmlns(det_data_node->parent());
    
    const rapidxml::xml_node<char> *speed_node = xml_first_node_nso( det_data_node, "Speed", xmlns );
    const rapidxml::xml_node<char> *occupancy_node = xml_first_node_nso( det_data_node, "Occupied", xmlns );
    const rapidxml::xml_node<char> *start_time_node = xml_first_node_nso( det_data_node, "StartTime", xmlns );
    const rapidxml::xml_node<char> *sample_real_time_node = xml_first_node_nso( det_data_node, "SampleRealTime", xmlns );
    
    float real_time = 0.0, speed = 0.0;
    boost::posix_time::ptime start_time;
    OccupancyStatus occupied = OccupancyStatus::Unknown;
    
    if( sample_real_time_node && sample_real_time_node->value_size() )
      real_time = time_duration_string_to_seconds( sample_real_time_node->value(), sample_real_time_node->value_size() );
    
    if( start_time_node )
      start_time = time_from_string( xml_value_str(start_time_node).c_str() );
    
    try{
      speed = speed_from_node( speed_node );
    }catch(...){}
    
    try{
      if( !occupancy_node )
        occupied = OccupancyStatus::Unknown;
      else if( is_occupied( occupancy_node ) )
        occupied = OccupancyStatus::Occupied;
      else
        occupied = OccupancyStatus::NotOccupied;
    }catch(...){}
    
    
    for( auto &meas : measurs_to_update )
    {
      if( meas->occupied_ == OccupancyStatus::Unknown )
        meas->occupied_ = occupied;
      
      if( meas->speed_ < 0.00000001f )
        meas->speed_ = speed;
      
      //dont set the start time for identiFINDER IntrinsicActivity spectrum (which
      //  doesnt have the time in the file), based on the StartTime node under
      //  <DetectorData> (of which, this time disagrees with the actual spectrum
      //  StartTime, so I dont know what to do about this anyway)
      if( meas->start_time_.is_special()
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
  
  
  void SpecFile::set_n42_2006_measurment_location_information(
                                                              const rapidxml::xml_node<char> *measured_item_info_node,
                                                              std::vector<std::shared_ptr<Measurement>> added_measurements )
  {
    if( !measured_item_info_node )
      return;
    
    string xmlns = get_n42_xmlns(measured_item_info_node);
    if( xmlns.empty() && measured_item_info_node->parent() )
      xmlns = get_n42_xmlns(measured_item_info_node->parent());
    
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
    
    //bool read_in_gps = false;
    double latitude = -999.9;
    double longitude = -999.9;
    boost::posix_time::ptime position_time;
    
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
      //  that occur while the measurment is being taken...
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
    
    if( SpecUtils::valid_latitude(latitude)
       && SpecUtils::valid_longitude(longitude) )
    {
      for( auto &meas : added_measurements )
      {
        //read_in_gps          = true;
        meas->latitude_      = latitude;
        meas->longitude_     = longitude;
        meas->position_time_ = position_time;
      }//for( std::shared_ptr<Measurement> &meas : measurements_this_node )
    }//if( !valid(latitude) &&  !valid(longitude) )
    
    if( !meas_loc_name )
      meas_loc_name = xml_first_node_nso( measured_item_info_node, "MeasurementLocationName", xmlns );
    measurement_location_name_ = xml_value_str( meas_loc_name );
    
    const rapidxml::xml_node<char> *operator_node = xml_first_node_nso( measured_item_info_node, "MeasurementOperator", xmlns );
    measurment_operator_ = xml_value_str( operator_node );
  }//void set_n42_2006_measurment_location_information()
  
  
  
  void SpecFile::load_2006_N42_from_doc( const rapidxml::xml_node<char> *document_node )
  {
    std::unique_lock<std::recursive_mutex> scoped_lock( mutex_ );
    
    //CambioN42, ORTEC IDM, Thermo: document_node == "N42InstrumentData"
    
    //CambioN42: I think has multiple <Measurement> nodes
    //          The <Measurement> node then has daughter <Spectrum>, and possibly <CountDoseData>
    //            The <Spectrum> has daughters <StartTime>, <LiveTime>, <RealTime>, <Calibration>, and <ChannelData>, <Cambio:TagChar>, <Cambio:OriginalFileType>, <Cambio:Version>
    //          Note that cambio n42 files does necassarily preserve detector name, sample number, speed, etc
    //ORTEC IDM, Thermo, SAIC8 all have single <Measurement> node, which has daughters <InstrumentInformation>, <InstrumentType>, <Manufacturer>, <InstrumentModel>, <InstrumentID>, <LaneNumber>, <dndons:DetectorStatus>, <MeasuredItemInformation>, and <DetectorData>
    //           There is a <DetectorData> node for each timeslice, with daughtes <StartTime>, <SampleRealTime>, <Occupied>, <Speed>, and <DetectorMeasurement>
    //            The <DetectorMeasurement> node then has daughter <SpectrumMeasurement>
    //              The <SpectrumMeasurement> node then has daughters <SpectrumAvailable>, <Spectrum>
    //                There are then a <spectrum> node for each detector, which has the daughters <RealTime>, <LiveTime>, <SourceType>, <DetectorType>, <Calibration>, and <ChannelData>
    
    if( !document_node )
      throw runtime_error( "load_2006_N42_from_doc: Invalid document node" );
    
    //Indicates a "spectrometer" type file, see
    //  http://physics.nist.gov/Divisions/Div846/Gp4/ANSIN4242/2005/simple.html
    //  for example nucsafe g4 predators also go down this path
    const rapidxml::xml_node<char> *firstmeas = document_node->first_node( "Measurement", 11 );
    const bool is_spectrometer = (firstmeas && firstmeas->first_node( "Spectrum", 8 ));
    
    std::mutex queue_mutex;
    std::shared_ptr<DetectorAnalysis> analysis_info = make_shared<DetectorAnalysis>();
    
    SpecUtilsAsync::ThreadPool workerpool;
    
    string xmlns = get_n42_xmlns(document_node);
    if( xmlns.empty() )
    {
      for( auto attrib = document_node->first_attribute(); attrib; attrib = attrib->next_attribute() )
      {
        //Some files use the "n42ns" namespace, IDK
        const string name = xml_name_str(attrib);
        if( SpecUtils::starts_with(name, "xmlns:" ) && SpecUtils::icontains(name, "n42") )
          xmlns = name.substr(6) + ":";
      }//for( check for xmlns:n42ns="..."
    }//if( doc_node_name has a namespace in it ) / else
    
    if( xmlns.empty() )
      xmlns = "n42:";  //might not actually be needed, but JIC until I bother to test.
    
    
    if( is_spectrometer )
    {
      vector<const rapidxml::xml_node<char> *> countdoseNodes;
      vector<const rapidxml::xml_node<char> *> locationNodes, instInfoNodes;
      
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
          workerpool.post( SpectrumNodeDecodeWorker( spectrum, &queue_mutex,
                                                    meas, analysis_info,
                                                    measurement, document_node ) );
        }
        
        for( const rapidxml::xml_node<char> *dose = xml_first_node_nso( measurement, "CountDoseData", xmlns );
            dose;
            dose = XML_NEXT_TWIN(dose) )
        {
          countdoseNodes.push_back( dose );
          //We dont want to set file level information from this <Measurment> node
          //  so we will continue;
          //continue;
        }//if( spectrum ) / else ( dose )
        
        //XML files from "Princeton Gamma-Tech Instruments, Inc." detectors
        //  will have the InstrumentInformation, AnalysisResults, and
        //  Calibration here
        const rapidxml::xml_node<char> *inst_info = xml_first_node_nso( measurement, "InstrumentInformation", xmlns );
        set_n42_2006_instrument_info_node_info( inst_info );
        instInfoNodes.push_back( inst_info );
        
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
        //      const rapidxml::xml_node<char> *info_node = measurement->first_node( "InstrumentInformation", 21 );
        //      set_n42_2006_instrument_info_node_info( info_node );
      }//for( loop over <Measurement> nodes )
      
      workerpool.join();
      
      SpectrumNodeDecodeWorker::filter_valid_measurments( measurements_ );
      
      //This loop may be taken care of by SpectrumNodeDecodeWorker anyway
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
          //If only one measurment, set the data no matter whate
          measurements_[0]->set_n42_2006_count_dose_data_info( dose, analysis_info, &queue_mutex );
          //addedCountDose = true;
        }else if( measurements_.size() )
        {
          const rapidxml::xml_node<char> *starttime_node = xml_first_node_nso( dose, "StartTime", xmlns );
          if( starttime_node && starttime_node->value_size() )
          {
            const boost::posix_time::ptime starttime = time_from_string( xml_value_str(starttime_node).c_str() );
            if( !starttime.is_special() )
            {
              int nearestindex = -1;
              boost::posix_time::time_duration smallestdelta = boost::posix_time::time_duration(10000,0,0);
              for( size_t j = 0; j < measurements_.size(); j++ )
              {
                const boost::posix_time::ptime &thisstartime = measurements_[j]->start_time_;
                boost::posix_time::time_duration thisdelta = thisstartime - starttime;
                if( thisdelta < boost::posix_time::time_duration(0,0,0) )
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
                 && smallestdelta < boost::posix_time::time_duration(0,1,0) )
              {
                if( !measurements_[nearestindex]->contained_neutron_ )
                {
                  measurements_[nearestindex]->set_n42_2006_count_dose_data_info( dose, analysis_info, &queue_mutex );
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
                }//if( there is a foreground and background spectrum for this measurment, and we're assigning the neutron info to foreground )
              }//if( found an appropriate measurment to put this neutron info in )
            }//if( !starttime.is_special() )
          }//if( starttime_node && starttime_node->value_size() )
        }//if( we only have one other measurment ) / else, find nearest one
        
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
          set_n42_2006_measurment_location_information( locationNodes[i], addedmeas );
        }//if( locationNodes[i] && measurements_[i] )
      }//for( size_t i = 0; i < locationNodes.size(); ++i )
      
      //Add in deviation pairs for the detectors.
      //Note that this is a hack to read in deviation pairs saved from this code,
      //  and I havent seen any simple spectrometer style files actually use this.
      //Also, it is completely untested if it actually works.
      //For the ref8TINMQY7PF, the below index based matching up shows this
      //  approach may not always be correct...
      for( size_t i = 0; i < instInfoNodes.size(); ++i )
      {
        if( instInfoNodes[i] && (i<measurements_.size()) && measurements_[i] )
        {
          vector<std::shared_ptr<Measurement>> addedmeas( 1, measurements_[i] );
          set_n42_2006_deviation_pair_info( instInfoNodes[i], addedmeas );
        }//if( instInfoNodes[i] && measurements_[i] )
      }//for( size_t i = 0; i < locationNodes.size(); ++i )
      
    }else
    {
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
        if( measurement->first_node( "SpectrumMeasurement", 19 ) )
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
              workerpool.post( SpectrumNodeDecodeWorker( spectrum, &queue_mutex,
                                                        meas, analysis_info, NULL,
                                                        document_node ) );
            }//for( rapidxml::xml_node<char> *spectrum = ... )
          }//for( const rapidxml::xml_node<char> *spec_meas_node = ... )
          
          workerpool.join();
          
          
          SpectrumNodeDecodeWorker::filter_valid_measurments( measurements_this_node );
          
          for( auto &meas : measurements_this_node )
          {
            measurements_.push_back( meas );
            added_measurements.push_back( meas );
          }//for( auto &meas : measurements_this_node )
        }//if( measurement->first_node( "SpectrumMeasurement", 19 ) )
        
        
        
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
          //  #energy_cal_variants and #keep_energy_cal_variant.
          vector< pair<std::shared_ptr<Measurement>, const rapidxml::xml_node<char> *> > meas_to_spec_node;
          
          //Raytheon portals do this wierd thing of putting two calibrations into
          //  one ChannelData node.. lets find and fix this.  The pointer (second)
          //  will be to the second set of calibration coefficients.
          vector< pair<std::shared_ptr<Measurement>, const rapidxml::xml_node<char> *> > multiple_cals;
          
          
          //Only N42 files that I saw in refMK252OLFFE use this next loop
          //  to initiate the SpectrumNodeDecodeWorkers
          for( const rapidxml::xml_node<char> *spectrum = xml_first_node_nso( det_data_node, "Spectrum", xmlns );
              spectrum;
              spectrum = XML_NEXT_TWIN(spectrum) )
          {
            std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
            measurements_this_node.push_back( meas );
            
            workerpool.post( SpectrumNodeDecodeWorker( spectrum, &queue_mutex,
                                                      meas, analysis_info, (rapidxml::xml_node<char> *)0,
                                                      document_node ) );
            
            if( spectrum->first_attribute( "CalibrationIDs", 14 ) )
              meas_to_spec_node.push_back( make_pair( meas, spectrum ) );
          }//for( loop over spectrums )
          
          
          //Most N42 files use the following loop
          for( const rapidxml::xml_node<char> *det_meas_node = xml_first_node_nso( det_data_node, "DetectorMeasurement", xmlns );
              det_meas_node;
              det_meas_node = XML_NEXT_TWIN(det_meas_node) )
          {
            //Look for <SpectrumMeasurement> measurments
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
                
                workerpool.post( SpectrumNodeDecodeWorker( spectrum, &queue_mutex,
                                                          meas, analysis_info, det_meas_node, document_node ) );
                
                if( XML_FIRST_ATTRIB(spectrum, "CalibrationIDs") )
                  meas_to_spec_node.push_back( make_pair( meas, spectrum ) );
                
                //Raytheon portals do a funky thing where they have both a 2.5MeV and a
                //  8 MeV scale - providing two sets of calibration coefficients under
                //  the SpectrumMeasurement->Spectrum->Calibration->Equation->Coefficients tag, and then placing the
                //  spectra, one after the other, in the <ChannelData>
                {//begin weird Ratheyon check
                  const rapidxml::xml_node<char> *firstcal = XML_FIRST_NODE_CHECKED(spectrum, "Calibration");
                  const rapidxml::xml_node<char> *firsteqn = XML_FIRST_NODE_CHECKED(firstcal, "Equation");
                  const rapidxml::xml_node<char> *firstcoef = XML_FIRST_NODE_CHECKED(firsteqn  , "Coefficients");
                  const rapidxml::xml_node<char> *secondcoef = XML_NEXT_TWIN_CHECKED(firstcoef);
                  if( secondcoef && secondcoef->value_size() )
                    multiple_cals.push_back( std::make_pair(meas,secondcoef) );
                }//end weird Ratheyon check
              }//for( rapidxml::xml_node<char> *spectrum = ... )
            }//for( const rapidxml::xml_node<char> *spec_meas_node ... )
            
            
            //now look for <GrossCountMeasurement> measurments
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
          //  DetectorMeasurment node for each <DetectorData> node, where the
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
          
          
          SpectrumNodeDecodeWorker::filter_valid_measurments( measurements_this_node );
          
          
          //Now go through and look for Measurments with neutron data, but no
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
              
              if( !neut->start_time_.is_special() && !gam->start_time_.is_special()
                 && neut->start_time_ != gam->start_time_ )
                continue;
              
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
            //The Measurments in gross_counts might be just neutrons from
            //  detectors with the same name as a gamma detector, if
            //  so we need to combine measurments so ensure_unique_sample_numbers()
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
                
                //Some detectors will have names like "ChannelAGamma", "ChannelANeutron", "ChannelBGamma", "ChannelBNeutron", etc
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
                  cerr << SRC_LOCATION << "\n\tWarning: confusing gross count"
                  << " situation" << endl;
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
                gross.reset();  //we dont want this measurment anymore
            }//for( auto &gross : gross_count_meas )
            
            for( const auto &gross : gross_counts_this_node )
            {
              if( gross )
                measurements_this_node.push_back( gross );
            }//for( const auto &gross : gross_counts_this_node )
          }//if( gross_counts_this_node.size() )
          
          Measurement::set_n42_2006_detector_data_node_info( det_data_node, measurements_this_node );
          
          for( size_t multical_index = 0; multical_index < multiple_cals.size(); ++multical_index )
          {
            std::shared_ptr<Measurement> &meas = multiple_cals[multical_index].first;
            const rapidxml::xml_node<char> *second_coefs = multiple_cals[multical_index].second;
            
            if( find( measurements_this_node.begin(), measurements_this_node.end(), meas ) == measurements_this_node.end() )
              continue;
            
            //Sanity check to make sure doing this matches the example data I have
            //  where this bizare fix is needed.
            if( !meas->gamma_counts_ || meas->gamma_counts_->size()!=2048
               || meas->energy_calibration_model_!=SpecUtils::EnergyCalType::Polynomial )
              continue;
            
            if( meas->channel_energies_ && meas->channel_energies_->size() )
              meas->channel_energies_.reset();
            
            std::shared_ptr<const vector<float> > oldcounts = meas->gamma_counts_;
            auto lowerbins = std::make_shared<vector<float> >( oldcounts->begin(), oldcounts->begin()+1024 );
            auto upperbins = std::make_shared<vector<float> >( oldcounts->begin()+1024, oldcounts->end() );
            
            std::shared_ptr<Measurement> newmeas = std::make_shared<Measurement>(*meas);
            
            meas->gamma_counts_ = lowerbins;
            meas->gamma_count_sum_ = 0.0;
            for( const float a : *lowerbins )
              meas->gamma_count_sum_ += a;
            
            newmeas->gamma_counts_ = upperbins;
            newmeas->gamma_count_sum_ = 0.0;
            for( const float a : *upperbins )
              newmeas->gamma_count_sum_ += a;
            
            vector<float> coeffs;
            if( SpecUtils::split_to_floats( second_coefs->value(),
                                           second_coefs->value_size(), coeffs )
               && coeffs != meas->calibration_coeffs_ )
            {
              newmeas->calibration_coeffs_ = coeffs;
              
              //Should come up with better way to decide which calibrarion is which
              //  or make more general or something
              meas->detector_name_ += "_intercal_9MeV";
              newmeas->detector_name_ += "_intercal_2.5MeV";
              
              measurements_this_node.push_back( newmeas );
            }else
            {
#if(PERFORM_DEVELOPER_CHECKS)
              log_developer_error( __func__, "Failed to split second energy calibration coefficents into floats" );
#endif
            }//if( second_coefs contained arrray of floats )
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
          // See comments for #energy_cal_variants and #keep_energy_cal_variant.
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
              workerpool.post( SpectrumNodeDecodeWorker( node, &queue_mutex, meas, analysis_info, NULL, document_node ) );
            }
            
            for( node = xml_first_node_nso( gamma_data, "SpectrumSummed", "dndoarns:" );
                node; node = XML_NEXT_TWIN(node) )
            {
              std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
              spectrum_nodes.push_back( node );
              measurements_this_node.push_back( meas );
              workerpool.post( SpectrumNodeDecodeWorker( node, &queue_mutex, meas, analysis_info, NULL, document_node ) );
            }//for( loop over <SpectrumSummed> )
            
            workerpool.join();
            
            for( size_t i = 0; i < measurements_this_node.size(); ++i )
            {
              //<dndoarns:SpectrumSummed dndoarns:DetectorSubset="Partial" dndoarns:NuclidesIdentified="Th-232-001 Ra-226-002">
              std::shared_ptr<Measurement> meas = measurements_this_node[i];
              vector<std::shared_ptr<Measurement> > measv( 1, meas );
              SpectrumNodeDecodeWorker::filter_valid_measurments( measv );
              
              if( measv.empty() )
                continue;
              
              string name = xml_name_str(spectrum_nodes[i]);
              if( SpecUtils::icontains(name, "BackgroundSpectrum" ) )
              {
                meas->title_ += " " + string("Background");
                
                //Check if calibration is valid, and if not, fill it in from the
                //  next spectrum... seems a little sketch but works on all the
                //  example files I have.
                if( SpecUtils::EnergyCalType::InvalidEquationType == meas->energy_calibration_model_
                   && meas->calibration_coeffs_.empty()
                   && (i < (measurements_this_node.size()-1))
                   && measurements_this_node[i+1]
                   && !measurements_this_node[i+1]->calibration_coeffs_.empty()
                   && measurements_this_node[i+1]->energy_calibration_model_ != SpecUtils::EnergyCalType::InvalidEquationType )
                {
                  meas->energy_calibration_model_ = measurements_this_node[i+1]->energy_calibration_model_;
                  meas->calibration_coeffs_ = measurements_this_node[i+1]->calibration_coeffs_;
                  meas->deviation_pairs_ = measurements_this_node[i+1]->deviation_pairs_;
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
        
        set_n42_2006_measurment_location_information( item_info_node, added_measurements );
        set_n42_2006_deviation_pair_info( info_node, added_measurements );
        
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
        
        
        //THe identiFINDER has its neutron info in a <CountDoseData> node under the <Measurement> node
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
            if( XML_VALUE_ICOMPARE(remark_node,"Minimum") || XML_VALUE_ICOMPARE(remark_node,"Maximum") )
              continue;
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
          const boost::posix_time::ptime start_time = time_from_string( xml_value_str(starttime_node).c_str() );
          if( start_time.is_special() )
          {
            //Just because we failed to parse the start time doesnt mean we shouldnt
            //  parse the neutron information - however, we need to know which gamma
            //  spectra to add the neutron info to.  So we'll comprimize and if
            //  we can unabigously pair the information, despite not parsing dates
            //  we'll do it - I really dont like the brittlness of all of this!
            size_t nspectra = 0;
            for( const auto &meas : added_measurements )
              nspectra += (meas && (meas->source_type_ != SourceType::IntrinsicActivity) && meas->start_time_.is_special() );
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
          cerr << "SpecFile::load_2006_N42_from_doc(): may be overwriting InstrumentInformation already gathered from a specific spectrum" << endl;
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
    
    if( measurements_.empty() )
    {
      stringstream msg;
      msg << SRC_LOCATION << "\n\rNo Measurments found inside ICD1/XML file";
      throw runtime_error( msg.str() );
    }//if( measurements_.empty() )
    
    
    //Lets try to figure out if we can fill out detector_type_
    if( iequals_ascii( manufacturer_,"ORTEC" ) )
    {
      if( iequals_ascii(instrument_model_,"OSASP") )
        detector_type_ = DetectorType::DetectiveEx200;
      else if( icontains(instrument_model_,"100") )
        detector_type_ = DetectorType::DetectiveEx100;
      else if( icontains(instrument_model_,"Detective-EX") )
        detector_type_ = DetectorType::DetectiveEx;
      else if( icontains(instrument_model_,"Detective") && contains(instrument_model_,"100") )
        detector_type_ = DetectorType::DetectiveEx100;
      else if( icontains(instrument_model_,"Detective") && icontains(instrument_model_,"micro") )
        detector_type_ = DetectorType::MicroDetective;
      else if( icontains(instrument_model_,"Detective") )
        detector_type_ = DetectorType::DetectiveUnknown;
    }else if( iequals_ascii(instrument_type_,"PVT Portal")
             && iequals_ascii(manufacturer_,"SAIC") )
    {
      detector_type_ = DetectorType::SAIC8;
    }else if( icontains(instrument_model_,"identiFINDER")
             //&& icontains(manufacturer_,"FLIR")
             )
    {
      //WE could probably get more specific detector fine tuning here, cause we have:
      //      <InstrumentModel>identiFINDER 2 ULCS-TNG</InstrumentModel>
      //      <InstrumentVersion>Hardware: 4C  Firmware: 5.00.54  Operating System: 1.2.040  Application: 2.37</InstrumentVersion>
      
      if( icontains(instrument_model_,"LG") )
        detector_type_ = DetectorType::IdentiFinderLaBr3;
      else
        detector_type_ = DetectorType::IdentiFinderNG;
    }else if( icontains(manufacturer_,"FLIR") || icontains(instrument_model_,"Interceptor") )
    {
      
    }else if( icontains(instrument_model_,"SAM940") || icontains(instrument_model_,"SAM 940") || icontains(instrument_model_,"SAM Eagle") )
    {
      if( icontains(instrument_model_,"LaBr") )
        detector_type_ = DetectorType::Sam940LaBr3;
      else
        detector_type_ = DetectorType::Sam940;
    }else if( istarts_with(instrument_model_,"RE ") || icontains(instrument_model_,"RadEagle") || icontains(instrument_model_,"Rad Eagle" ) )
    {
      if( !manufacturer_.empty() && !icontains(manufacturer_, "ortec") )
        manufacturer_ += " (Ortec)";
      else if( !icontains(manufacturer_, "ortec") )
        manufacturer_ = "Ortec";
      //set_detector_type_from_other_info() will set detector_type_ ...
    }else if( icontains(instrument_model_,"SAM") && icontains(instrument_model_,"945") )
    {
      detector_type_ = DetectorType::Sam945;
    }else if( (icontains(manufacturer_,"ICx Radiation") || icontains(manufacturer_,"FLIR"))
             && icontains(instrument_model_,"Raider") )
    {
      detector_type_ = DetectorType::MicroRaider;
    }else if( icontains(manufacturer_,"Canberra Industries, Inc.") )
    {
      //Check to see if detectors like "Aa1N+Aa2N", or "Aa1N+Aa2N+Ba1N+Ba2N+Ca1N+Ca2N+Da1N+Da2N"
      //  exist and if its made up of other detectors, get rid of those spectra
      
    }else if( icontains(instrument_type_,"SpecPortal")
             && icontains(manufacturer_,"SSC Pacific")
             && icontains(instrument_model_,"MPS Pod") )
    {
      //Gamma spectrum is in CPS, so multiply each spectrum by live time.
      //  Note that there is no indication of this in the file, other than
      //  fracitonal counts
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
    }else if( icontains(instrument_model_,"SRPM") && icontains(instrument_model_,"210") )
    {
      if( manufacturer_.size() < 2 )
        manufacturer_ = "Leidos";  //"EXPLORANIUM" would be the other option
      detector_type_ = DetectorType::Srpm210;
    }else if( (icontains(instrument_type_,"innoRIID") || icontains(instrument_type_,"ortec"))
             && istarts_with(instrument_model_, "RE ") )
    {
      
    }else if( manufacturer_.size() || instrument_model_.size() )
    {
      //    if( (manufacturer_=="ICx Technologies" && instrument_model_=="identiFINDER") )
      //    {
      //    }
      //In priniciple we should add all of these following detectors to the
      //  DetectorType enum, but being lazy for now.
      if( !(manufacturer_=="Princeton Gamma-Tech Instruments, Inc." && instrument_model_=="RIIDEye")
         && !(manufacturer_=="ICx Technologies" && instrument_model_=="")
         && !(manufacturer_=="Radiation Solutions Inc." /* && instrument_model_=="RS-701"*/)
         && !(manufacturer_=="Raytheon" && instrument_model_=="Variant L" )
         && !(manufacturer_=="Avid Annotated Spectrum" /* && instrument_model_==""*/)
         && !(manufacturer_=="Mirion Technologies" && instrument_model_=="model Pedestrian G")
         && !(manufacturer_=="Princeton Gamma-Tech Instruments, Inc." && instrument_model_=="")
         && !(manufacturer_=="Nucsafe" && instrument_model_=="G4_Predator")
         && !(manufacturer_=="Princeton Gamma-Tech Instruments, Inc." && instrument_model_=="Model 135")
         && !(manufacturer_=="" && instrument_model_=="Self-Occuluding Quad NaI Configuration")
         && !(manufacturer_=="" && instrument_model_=="3x3x12 inch NaI Side Ortec Digibase MCA")
         && !(manufacturer_=="Berkeley Nucleonics Corp." && instrument_model_=="SAM 945")
         && !(manufacturer_=="Canberra Industries, Inc." && instrument_model_=="ASP EDM")
         && !(manufacturer_=="Smiths Detection" && instrument_model_=="RadSeeker_DL")
         && !(manufacturer_=="Raytheon" && instrument_model_=="Variant C")
         && !(manufacturer_=="" && instrument_model_=="")
         )
        cerr << "Unknown detector type: maufacturer=" << manufacturer_ << ", ins_model=" << instrument_model_ << endl;
      
      //Unknown detector type: maufacturer=Smiths Detection, ins_model=RadSeeker_CS
      //Unknown detector type: maufacturer=Smiths Detection, ins_model=RadSeeker_DL
      //Unknown detector type: maufacturer=Princeton Gamma-Tech Instruments, Inc., ins_model=RIIDEye, Ext2x2
    }
    
    cleanup_after_load();
  }//bool load_2006_N42_from_doc( rapidxml::xml_node<char> *document_node )

  
  
  void Measurement::add_calibration_to_2012_N42_xml(
                                                    rapidxml::xml_node<char> *RadInstrumentData,
                                                    std::mutex &xmldocmutex,
                                                    const int i ) const
  {
    using namespace rapidxml;
    const char *val = (const char *)0;
    char buffer[32];
    
    rapidxml::xml_document<char> *doc = RadInstrumentData->document();
    
    const char *coefname = 0;
    xml_node<char> *EnergyCalibration = 0, *node = 0;
    
    stringstream valuestrm;
    std::vector<float> coefs = calibration_coeffs();
    const size_t nbin = gamma_counts()->size();
    
    switch( energy_calibration_model() )
    {
      case SpecUtils::EnergyCalType::FullRangeFraction:
        coefs = SpecUtils::fullrangefraction_coef_to_polynomial( coefs, nbin );
        //note intential fallthrough
      case SpecUtils::EnergyCalType::Polynomial:
      case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
      {
        coefname = "CoefficientValues";
        
        //Technically this node must have exactly three coeficients, but here we'll
        //  slip in some more if we have them...
        const size_t ncoef = std::max( size_t(3), coefs.size() );
        for( size_t j = 0; j < ncoef; ++j )
        {
          snprintf( buffer, sizeof(buffer), "%s%.9g", (j?" ":""), (j>=coefs.size() ? 0.0f : coefs[j]) );
          valuestrm << buffer;
        }
        
        break;
      }//case SpecUtils::EnergyCalType::Polynomial:
        
      case SpecUtils::EnergyCalType::LowerChannelEdge:
      case SpecUtils::EnergyCalType::InvalidEquationType:  //Taking a guess here (we should alway capture FRF, Poly, and LowrBinEnergy correctly,
        if( !!channel_energies_ || calibration_coeffs().size() )
        {
          coefname = "EnergyBoundaryValues";
          const std::vector<float> &b = (!!channel_energies_ ? *channel_energies_ : calibration_coeffs());
          
          //This next part should be formatted better!
          for( size_t j = 0; j < b.size(); ++j )
            valuestrm << (j?" ":"") << b[j];
          
          //According to N42 2012 standard, we must give energy of upper channel
          if( b.size() && b.size()<=nbin )
            valuestrm << " " << ((2.0f*b[b.size()-1])-b[b.size()-2]);
        }//if( we have some sort of coefficients )
        break;
    }//switch( energy_calibration_model() )
    
    
    if( coefname )
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      EnergyCalibration = doc->allocate_node( node_element, "EnergyCalibration" );
      RadInstrumentData->append_node( EnergyCalibration );
      
      snprintf( buffer, sizeof(buffer), "EnergyCal%i", int(i) );
      val = doc->allocate_string( buffer );
      EnergyCalibration->append_attribute( doc->allocate_attribute( "id", val ) );
      
      val = doc->allocate_string( valuestrm.str().c_str() );
      node = doc->allocate_node( node_element, coefname, val );
      EnergyCalibration->append_node( node );
    }//if( coefname )
    
    const std::vector<std::pair<float,float>> &devpairs = deviation_pairs();
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
    
    xml_node<char> *AnalysisResults = 0;
    
    {
      std::lock_guard<std::mutex> lock( xmldocmutex );
      AnalysisResults = doc->allocate_node( node_element, "AnalysisResults" );
      RadInstrumentData->append_node( AnalysisResults );
    }
    
    for( size_t i = 0; i < ana.remarks_.size(); ++i )
    {
      const string &remark = ana.remarks_[i];
      if( remark.size() )
      {
        std::lock_guard<std::mutex> lock( xmldocmutex );
        val = doc->allocate_string( remark.c_str(), remark.size()+1 );
        xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
        AnalysisResults->append_node( remark );
      }
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
        
        /*
         xml_node<char> *AnalysisStartDateTime = (xml_node<char> *)0;
         if( !AnalysisStartDateTime && !result.start_time_.is_special() )
         {
         const string timestr = SpecUtils::to_iso_string( result.start_time_ );
         val = doc->allocate_string( timestr.c_str(), timestr.size()+1 );
         AnalysisStartDateTime = doc->allocate_node( node_element, "StartTime", val );
         AnalysisResults->append_node( AnalysisStartDateTime );
         }
         */
        
        xml_node<char> *nuclide_node = doc->allocate_node( node_element, "Nuclide" );
        result_node->append_node( nuclide_node );
        
        val = doc->allocate_string( result.nuclide_.c_str(), result.nuclide_.size()+1 );
        xml_node<char> *NuclideName = doc->allocate_node( node_element, "NuclideName", val );
        nuclide_node->append_node( NuclideName );
        
        if( result.remark_.size() )
        {
          val = doc->allocate_string( result.remark_.c_str(), result.remark_.size()+1 );
          xml_node<char> *Remark = doc->allocate_node( node_element, "Remark", val );
          nuclide_node->append_node( Remark );
        }
        
        if( result.activity_ > 0.0f )
        {
          snprintf( buffer, sizeof(buffer), "%1.8E", (result.activity_/1000.0f) );
          val = doc->allocate_string( buffer );
          xml_node<char> *NuclideActivityValue = doc->allocate_node( node_element, "NuclideActivityValue", val );
          attr = doc->allocate_attribute( "units", "kBq" );
          NuclideActivityValue->append_attribute( attr );
          nuclide_node->append_node( NuclideActivityValue );
        }
        
        if( result.nuclide_type_.size() )
        {
          val = doc->allocate_string( result.nuclide_type_.c_str(), result.nuclide_type_.size()+1 );
          xml_node<char> *NuclideType = doc->allocate_node( node_element, "NuclideType", val );
          nuclide_node->append_node( NuclideType );
        }
        
        if( result.id_confidence_.size() )
        {
          val = doc->allocate_string( result.id_confidence_.c_str(), result.id_confidence_.size()+1 );
          xml_node<char> *NuclideIDConfidenceDescription = doc->allocate_node( node_element, "NuclideIDConfidenceIndication", val ); //NuclideIDConfidenceDescription
          nuclide_node->append_node( NuclideIDConfidenceDescription );
        }
        
        xml_node<char> *NuclideExtension = 0;
        
        if( result.real_time_ > 0.0f )
        {
          NuclideExtension = doc->allocate_node( node_element, "NuclideExtension" );
          nuclide_node->append_node( NuclideExtension );
          snprintf( buffer, sizeof(buffer), "PT%fS", result.real_time_ );
          val = doc->allocate_string( buffer );
          xml_node<char> *SampleRealTime = doc->allocate_node( node_element, "SampleRealTime", val );
          NuclideExtension->append_node( SampleRealTime );
        }//if( we should record some more info )
        
        if( result.distance_ > 0.0f )
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
        
        /*
         if( !AnalysisStartDateTime && !result.start_time_.is_special() )
         {
         const string timestr = SpecUtils::to_iso_string( result.start_time_ );
         val = doc->allocate_string( timestr.c_str(), timestr.size()+1 );
         AnalysisStartDateTime = doc->allocate_node( node_element, "StartTime", val );
         AnalysisResults->append_node( AnalysisStartDateTime );
         }
         */
        
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
    BinningToIndexMap calToSpecMap;
    SpecUtilsAsync::ThreadPool workerpool;
    
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
    
    attr = doc->allocate_attribute( "xmlns", "http://physics.nist.gov/N42/2012/N42" );
    RadInstrumentData->append_attribute( attr );
    
    {
      const boost::posix_time::ptime t = boost::posix_time::second_clock::universal_time();
      const string datetime = SpecUtils::to_extended_iso_string(t) + "Z";
      val = doc->allocate_string( datetime.c_str(), datetime.size()+1 );
      attr = doc->allocate_attribute( "n42DocDateTime", val );
      RadInstrumentData->append_attribute( attr );
    }
    
    
    workerpool.post( [this,RadInstrumentData,&xmldocmutex,&calToSpecMap](){
      insert_N42_calibration_nodes( measurements_ , RadInstrumentData, xmldocmutex, calToSpecMap );
    });
    
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
      
      if( original_creator.empty() )
      {
        xml_node<char> *RadInstrumentDataCreatorName = doc->allocate_node( node_element, "RadInstrumentDataCreatorName", "InterSpec" );
        RadInstrumentData->append_node( RadInstrumentDataCreatorName );
      }else
      {
        SpecUtils::ireplace_all( original_creator, "InterSpec", "" );
        SpecUtils::ireplace_all( original_creator, "  ", "" );
        original_creator = "InterSpec. Original file by " + original_creator;
        
        val = doc->allocate_string( original_creator.c_str() );
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
      
      if( SpecUtils::icontains( name, "Software") && version == "Unknown" )
        continue;
      
      std::lock_guard<std::mutex> lock( xmldocmutex );
      
      xml_node<char> *version_node = version_node = doc->allocate_node( node_element, "RadInstrumentVersion" );
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
    
    if(!measurment_operator_.empty())
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
      
      string operatorname = measurment_operator_;
      //    ireplace_all( operatorname, "\t", "&#009;" );//replace tab characters with tab character code
      //    ireplace_all( operatorname, "&", "&#38;" );
      
      val = doc->allocate_string( operatorname.c_str(), operatorname.size()+1 );
      xml_node<char> *CharacteristicValue = doc->allocate_node( node_element, "CharacteristicValue", val );
      Characteristic->append_node( CharacteristicValue );
    }//if( measurment_operator_.size() )
    
    
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
      
      xml_attribute<> *name = doc->allocate_attribute( "id", val );
      RadDetectorInformation->append_attribute( name );
      
      //<RadDetectorName>: free-form text name for the detector description, ex, "Gamma Front/Right"
      
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
      
      if(!rad_det_desc.empty())
      {
        val = doc->allocate_string( rad_det_desc.c_str(), rad_det_desc.size()+1 );
        xml_node<char> *RadDetectorDescription = doc->allocate_node( node_element, "RadDetectorDescription", val );
        RadDetectorInformation->append_node( RadDetectorDescription );
      }
      
      //"RadDetectorDescription"
      
      
      
      const string det_kind = determine_rad_detector_kind_code();
      val = doc->allocate_string( det_kind.c_str() );
      xml_node<char> *RadDetectorKindCode = doc->allocate_node( node_element, "RadDetectorKindCode", val );
      RadDetectorInformation->append_node( RadDetectorKindCode );
      
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
    
    workerpool.join();  //ensures all calibrations have been written to the DOM and can be referenced using calToSpecMap
    
    //For the case of portal data, where the first sample is a long backgorund and
    //  the rest of the file is the occupancy, GADRAS has some "special"
    //  requirements on id attribute values...
    bool first_sample_was_back = false;
    const vector<int> sample_nums_vec( sample_numbers_.begin(), sample_numbers_.end() );
    
    for( set<int>::const_iterator sni = sample_numbers_.begin(); sni != sample_numbers_.end(); ++sni )
    {
      const int sample_num = *sni;
      const vector< std::shared_ptr<const Measurement> > smeas
      = sample_measurements( sample_num );
      if( smeas.empty() )  //probably shouldnt happen, but jic
        continue;
      
      vector<size_t> calid;
      for( size_t i = 0; i < smeas.size(); ++i )
      {
        const std::shared_ptr<const std::vector<float>> &binning = smeas[i]->channel_energies();
        calid.push_back( calToSpecMap[binning] );
      }
      
      
      //Check if all smeas have same start and real times, if so, write them under
      //  a single <RadMeasurement> tag, otherwise write under sepearte ones.
      //Note: this test is abit brittle, and maybe not quite fair.  For example on
      //  my portal the neutron
      bool allsame = true;
      boost::posix_time::ptime starttime = smeas[0]->start_time();
      float rtime = smeas[0]->real_time();
      
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
        const boost::posix_time::ptime tst = smeas[i]->start_time();
        starttime = ((tst.is_special() || (starttime < tst)) ? starttime : tst);
        rtime = max( rtime, smeas[i]->real_time() );
      }
      
      for( size_t i = 1; allsame && (i < smeas.size()); ++i )
      {
        std::shared_ptr<const Measurement> m = smeas[i];
        
        //Allow the first record in the file, if it is a background, to not need
        //  to have the same start time or real time as the other measurments in
        //  this sample.  This is to accomidate some portals whos RSPs may
        //  accumulate backgrounds a little weird, but none-the-less should all be
        //  considered teh same sample.
        if( m->source_type() != SourceType::Background
           || (sample_num != (*sample_numbers_.begin())) )
        {
          if( starttime != m->start_time() )
          {
            const boost::posix_time::time_duration diff = starttime - m->start_time();
            if( diff.is_special() || std::abs( diff.total_microseconds() ) > 50000 )
              allsame = false;
          }
          if( fabs(rtime - m->real_time()) > 0.05f )
            allsame = false;
        }//if( not background or not first sample )
        
        allsame = allsame && (smeas[i]->source_type() == smeas[0]->source_type());
      }//for( check if all measurments can go under a single <RadMeasurement> tag )
      
      
      //allsame = false;
      
      if( allsame )
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
        
        {
          std::lock_guard<std::mutex> lock( xmldocmutex );
          RadMeasurement = doc->allocate_node( node_element, "RadMeasurement" );
          RadInstrumentData->append_node( RadMeasurement );
          
          char *val = doc->allocate_string( RadMeasurementId );
          xml_attribute<> *attr = doc->allocate_attribute( "id", val );
          RadMeasurement->append_attribute( attr );
        }
        
        workerpool.post( [&xmldocmutex,RadMeasurement,smeas,calid](){
          add_spectra_to_measurment_node_in_2012_N42_xml( RadMeasurement, smeas, calid, xmldocmutex);
        } );
      }else //if( allsame )
      {
        for( size_t i = 0; i < smeas.size(); ++i )
        {
          const vector< std::shared_ptr<const Measurement> > thismeas(1,smeas[i]);
          const vector<size_t> thiscalid( 1, calid[i] );
          
          char RadMeasurementId[32];
          xml_node<char> *RadMeasurement = 0;
          snprintf( RadMeasurementId, sizeof(RadMeasurementId), "Sample%iDet%i", sample_num, smeas[i]->detector_number_ );
          
          {
            std::lock_guard<std::mutex> lock( xmldocmutex );
            RadMeasurement = doc->allocate_node( node_element, "RadMeasurement" );
            RadInstrumentData->append_node( RadMeasurement );
            
            char *val = doc->allocate_string( RadMeasurementId );
            xml_attribute<> *attr = doc->allocate_attribute( "id", val );
            RadMeasurement->append_attribute( attr );
          }
          
          workerpool.post( [RadMeasurement, thismeas, thiscalid, &xmldocmutex](){
            add_spectra_to_measurment_node_in_2012_N42_xml( RadMeasurement, thismeas, thiscalid, xmldocmutex ); } );
        }//for( loop over measuremtns for this sample number )
      }//if( allsame ) / else
    }//for( )
    
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
    
    if( !!detectors_analysis_ )
      add_analysis_results_to_2012_N42( *detectors_analysis_,
                                       RadInstrumentData, xmldocmutex );
    
    workerpool.join();
    
    
    return doc;
  }//rapidxml::xml_node<char> *create_2012_N42_xml() const
  
  
  void SpecFile::add_spectra_to_measurment_node_in_2012_N42_xml(
                                                                ::rapidxml::xml_node<char> *RadMeasurement,
                                                                const std::vector< std::shared_ptr<const Measurement> > measurments,
                                                                const std::vector<size_t> calibids,
                                                                std::mutex &xmldocmutex )
  {
    using namespace ::rapidxml;
    
    try
    {
      //Some checks that should never actualy trigger
      if( !RadMeasurement )
        throw runtime_error( "null RadMeasurement" );
      if( measurments.empty() )
        throw runtime_error( "with empty input" );
      if( measurments.size() != calibids.size() )
        throw runtime_error( "measurments.size != calibids.size" );
      
      string radMeasID;
      xml_document<char> *doc = 0;
      
      {
        std::lock_guard<std::mutex> lock( xmldocmutex );
        doc = RadMeasurement->document();
        radMeasID = xml_value_str( XML_FIRST_ATTRIB(RadMeasurement, "id") );
      }
      
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
      float speed = measurments[0]->speed_;
      boost::posix_time::ptime starttime = measurments[0]->start_time();
      
      OccupancyStatus occupancy = measurments[0]->occupied_;
      SourceType source_type = measurments[0]->source_type();
      
      bool has_gps = false;
      string positiontime;
      char latitude[16], longitude[16];
      float realtime_used = measurments[0]->real_time_;
      
      for( size_t i = 0; i < measurments.size(); ++i )
      {
        realtime_used = max( measurments[i]->real_time_, realtime_used );
        const boost::posix_time::ptime tst = measurments[i]->start_time();
        starttime = ((tst.is_special() || (starttime < tst)) ? starttime : tst);
        
        speed = max( measurments[i]->speed_, speed );
        
        if( measurments[i]->occupied_ == OccupancyStatus::Occupied )
          occupancy = measurments[i]->occupied_;
        else if( occupancy == OccupancyStatus::Unknown )
          occupancy = measurments[i]->occupied_;
        else if( measurments[i]->occupied_ ==  OccupancyStatus::NotOccupied && occupancy == OccupancyStatus::Unknown )
          occupancy = measurments[i]->occupied_;
        
        if( !has_gps && measurments[i]->has_gps_info() )
        {
          has_gps = true;
          snprintf( latitude, sizeof(latitude), "%.12f", measurments[i]->latitude_ );
          snprintf( longitude, sizeof(longitude), "%.12f", measurments[i]->longitude_ );
          if( !measurments[i]->position_time_.is_special() )
            positiontime = SpecUtils::to_extended_iso_string(measurments[i]->position_time_) + "Z";
        }//if( !has_gps )
        
        if( measurments[i]->source_type_ != SourceType::Unknown )
          source_type = std::max( measurments[i]->source_type_, source_type );
      }//for( size_t i = 1; i < measurments.size(); ++i )
      
      
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
        
        if( !measurments[0]->start_time_.is_special() )
        {
          val = doc->allocate_string( startstr.c_str(), startstr.size()+1 );
          RadMeasurement->append_node( doc->allocate_node( node_element, "StartDateTime", val, 13, startstr.size() ) );
        }
        
        if( measurments[0]->real_time_ > 0.0f )
        {
          val = doc->allocate_string( realtime );
          RadMeasurement->append_node( doc->allocate_node( node_element, "RealTimeDuration", val ) );
        }
      }
      
      //Since gross count nodes have to come after
      vector< xml_node<char> *> spectrum_nodes, gross_count_nodes, det_states;
      
      for( size_t i = 0; i < measurments.size(); ++i )
      {
        const size_t calibid = calibids[i];
        const std::shared_ptr<const Measurement> m = measurments[i];
        
        char livetime[32], calibstr[16], spec_idstr[48];
        
        string neutcounts;
        snprintf( livetime, sizeof(livetime), "PT%fS", m->live_time_ );
        snprintf( calibstr, sizeof(calibstr), "EnergyCal%i", static_cast<int>(calibid) );
        
        if( SpecUtils::icontains(radMeasID, "Det") )
        {
          //This is case where all measurements of a sample number did not have a similar
          //  start time or background/foreground status so each sample/detector
          //  gets its own <RadMeasurement> element, with an id like "Sample3Det1"
          snprintf( spec_idstr, sizeof(spec_idstr), "%sSpectrum", radMeasID.c_str() );
        }else if( !radMeasID.empty() )
        {
          //radMeasID will be "Background", "Survey XXX" if passthrough() that
          //  starts with a long background, and "SampleXXX" otherwise.
          snprintf( spec_idstr, sizeof(spec_idstr), "%sDet%iSpectrum", radMeasID.c_str(), m->detector_number_ );
        }else
        {
          //Probably shouldnt ever make it here.
          snprintf( spec_idstr, sizeof(spec_idstr), "Sample%iDet%iSpectrum", m->sample_number_, m->detector_number_ );
        }
        
        
        const string detnam = !m->detector_name_.empty() ? m->detector_name_ : s_unnamed_det_placeholder;
        
        //Below choice of zero compressing if the gamma sum is less than 15 times the
        //  number of gamma channels is arbitrarily chosen, and has not been
        //  benchmarked or checked it is a reasonable value
        const bool zerocompressed = (!!m->gamma_counts_ && (m->gamma_count_sum_<15.0*m->gamma_counts_->size()));
        vector<float> compressedchannels;
        
        if( zerocompressed )
          compress_to_counted_zeros( *m->gamma_counts_, compressedchannels );
        
        const vector<float> &data = (zerocompressed || !m->gamma_counts_) ? compressedchannels : (*m->gamma_counts_);
        
        string channeldata;
        if( !zerocompressed )
          channeldata.reserve( 3*m->gamma_counts_->size() ); //3 has not been verified to be reasonalbe
        
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
        
        if( m->neutron_counts_.size() > 1 )
        {
          for( size_t i = 0; i < m->neutron_counts_.size(); ++i )
          {
            snprintf( buffer, sizeof(buffer), (i?" %.8G":"%.8G"), m->neutron_counts_[i] );
            neutcounts += buffer;
          }//for( size_t i = 0; i < nchannel; i += 8 )
        }else
        {
          snprintf( buffer, sizeof(buffer), "%.8G", m->neutron_counts_sum_ );
          neutcounts += buffer;
        }
        
        
        std::lock_guard<std::mutex> lock( xmldocmutex );
        
        if( m->gamma_counts_ && !m->gamma_counts_->empty())
        {
          xml_node<char> *Spectrum = doc->allocate_node( node_element, "Spectrum" );
          spectrum_nodes.push_back( Spectrum );
          
          //If there is a slight mismatch between the live times of this sample
          //  (~50 ms), we will still include all detectors in the same sample,
          //  but put in a remark notting a difference.  This is absolutely a
          //  hack, but some sort of comprimise is needed to cram stuff into N42
          //  2012 files from arbitrary sources.
          if( fabs(m->real_time_ - realtime_used) > 0.00001 )
          {
            char thisrealtime[64];
            snprintf( thisrealtime, sizeof(thisrealtime), "RealTime: PT%fS", m->real_time_ );
            val = doc->allocate_string( thisrealtime );
            xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
            Spectrum->append_node( remark );
          }
          
          
          if( !m->title_.empty() )
          {
            const string title = "Title: " + m->title_;
            val = doc->allocate_string( title.c_str(), title.size()+1 );
            xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
            Spectrum->append_node( remark );
          }
          
          for( size_t i = 0; i < m->remarks_.size(); ++i )
          {
            if( m->remarks_[i].empty() )
              continue;
            const char *val = doc->allocate_string( m->remarks_[i].c_str(), m->remarks_[i].size()+1 );
            xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
            Spectrum->append_node( remark );
          }//for( size_t i = 0; i < remarks_.size(); ++i )
          
          
          for( size_t i = 0; i < m->parse_warnings_.size(); ++i )
          {
            if( m->parse_warnings_[i].empty() )
              continue;
            
            /// @TODO We should put the parse warnings common to all <spectrum> in this
            ///       measurement under the Measurement remark node, and not duplicated
            ///       in each spectrum node.
            const bool hasprefix = SpecUtils::starts_with( m->parse_warnings_[i], s_parser_warn_prefix );
            string valstr = (hasprefix ? "" : s_parser_warn_prefix ) + m->parse_warnings_[i];
            val = doc->allocate_string( valstr.c_str(), valstr.size()+1 );
            xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
            Spectrum->append_node( remark );
          }//for( size_t i = 0; i < m->parse_warnings_.size(); ++i )
          
          
          val = doc->allocate_string( calibstr );
          Spectrum->append_attribute( doc->allocate_attribute( "energyCalibrationReference", val ) );
          
          val = doc->allocate_string( detnam.c_str(), detnam.size()+1 );
          Spectrum->append_attribute( doc->allocate_attribute( "radDetectorInformationReference", val, 31, detnam.size() ) );
          
          //Add required ID attribute
          val = doc->allocate_string( spec_idstr );
          Spectrum->append_attribute( doc->allocate_attribute( "id", val ) );
          
          if( m->live_time_ > 0.0f )
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
        
        if( m->contained_neutron_ )
        {
          xml_node<char> *GrossCounts = doc->allocate_node( node_element, "GrossCounts" );
          gross_count_nodes.push_back( GrossCounts );
          
          if( (!m->gamma_counts_ || m->gamma_counts_->empty())  )
          {
            if( fabs(m->real_time_ - realtime_used) > 0.00001 )
            {
              char thisrealtime[64];
              snprintf( thisrealtime, sizeof(thisrealtime), "RealTime: PT%fS", m->real_time_ );
              val = doc->allocate_string( thisrealtime );
              xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
              GrossCounts->append_node( remark );
            }
            
            if(!m->title_.empty())
            {
              const string title = "Title: " + m->title_;
              val = doc->allocate_string( title.c_str(), title.size()+1 );
              xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
              GrossCounts->append_node( remark );
            }//if( m->title_.size() )
            
            for( size_t i = 0; i < m->remarks_.size(); ++i )
            {
              if( m->remarks_[i].empty() )
                continue;
              const char *val = doc->allocate_string( m->remarks_[i].c_str(), m->remarks_[i].size()+1 );
              xml_node<char> *remark = doc->allocate_node( node_element, "Remark", val );
              GrossCounts->append_node( remark );
            }//for( size_t i = 0; i < remarks_.size(); ++i )
          }//if( (!m->gamma_counts_ || m->gamma_counts_->empty())  )
          
          char neutId[32];
          if( radMeasID.empty() )
            snprintf( neutId, sizeof(neutId), "Sample%iNeutron%i", m->sample_number_, m->detector_number_ );
          else
            snprintf( neutId, sizeof(neutId), "%sNeutron%i", radMeasID.c_str(), m->detector_number_ );
          
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
        
        
        switch( measurments[i]->quality_status_ )
        {
          case SpecUtils::QualityStatus::Good:
            //When reading in the 2012 N42, we will assume good unless indicated otherwise
            break;
            
          case SpecUtils::QualityStatus::Suspect: case SpecUtils::QualityStatus::Bad:
          {
            xml_node<char> *RadDetectorState = doc->allocate_node( node_element, "RadDetectorState" );
            det_states.push_back( RadDetectorState );
            
            const char *val = doc->allocate_string( detnam.c_str() );
            xml_attribute<char> *att = doc->allocate_attribute( "radDetectorInformationReference", val );
            RadDetectorState->append_attribute( att );
            
            val = ((measurments[i]->quality_status_==SpecUtils::QualityStatus::Suspect) ? "Warning" : "Fatal" ); //"Error" is also an option
            RadDetectorState->append_node( doc->allocate_node( node_element, "Fault", val ) );
            break;
          }//case Suspect: case Bad:
            
          case SpecUtils::QualityStatus::Missing:
          {
            //This next line is InterSpec specific for round-tripping files
            xml_node<char> *RadDetectorState = doc->allocate_node( node_element, "RadDetectorState" );
            det_states.push_back( RadDetectorState );
            
            const char *val = doc->allocate_string( detnam.c_str() );
            xml_attribute<char> *att = doc->allocate_attribute( "radDetectorInformationReference", val );
            RadDetectorState->append_attribute( att );
            
            xml_node<char> *remark = doc->allocate_node( node_element, "Remark", "InterSpec could not determine detector state." );
            RadDetectorState->append_node( remark );
            break;
          }
        }//switch( quality_status_ )
      }//for( loop over input measurments )
      
      
      {//start put <Spectrum> and <GrossCount> nodes into tree
        std::lock_guard<std::mutex> lock( xmldocmutex );
        for( size_t i = 0; i < spectrum_nodes.size(); ++i )
          RadMeasurement->append_node( spectrum_nodes[i] );
        for( size_t i = 0; i < gross_count_nodes.size(); ++i )
          RadMeasurement->append_node( gross_count_nodes[i] );
      }//end put <Spectrum> and <GrossCount> nodes into tree
      
      
      {//begin add other information
        std::lock_guard<std::mutex> lock( xmldocmutex );
        
        if( has_gps )
        {
          xml_node<char> *RadInstrumentState = doc->allocate_node( node_element, "RadInstrumentState" );
          RadMeasurement->append_node( RadInstrumentState );
          
          xml_node<char> *StateVector = doc->allocate_node( node_element, "StateVector" );
          RadInstrumentState->append_node( StateVector );
          
          xml_node<char> *GeographicPoint = doc->allocate_node( node_element, "GeographicPoint" );
          StateVector->append_node( GeographicPoint );
          
          
          val = doc->allocate_string( latitude );
          xml_node<char> *LatitudeValue = doc->allocate_node( node_element, "LatitudeValue", val );
          GeographicPoint->append_node( LatitudeValue );
          
          val = doc->allocate_string( longitude );
          xml_node<char> *LongitudeValue = doc->allocate_node( node_element, "LongitudeValue", val );
          GeographicPoint->append_node( LongitudeValue );
          
          //<PositionTime> is an InterSpec addition since it didnt look like there wa a place for it in the spec
          if(!positiontime.empty())
          {
            val = doc->allocate_string( positiontime.c_str(), positiontime.size()+1 );
            xml_node<char> *PositionTime = doc->allocate_node( node_element, "PositionTime", val, 12, positiontime.size() );
            GeographicPoint->append_node( PositionTime );
          }
        }//if( has_gps_info() )
        
        for( size_t i = 0; i < det_states.size(); ++i )
          RadMeasurement->append_node( det_states[i] );
        
        if( speed > 0.0f )
        {
          xml_node<char> *RadItemState = doc->allocate_node( node_element, "RadItemState" );
          RadMeasurement->append_node( RadItemState );
          
          xml_node<char> *StateVector = doc->allocate_node( node_element, "StateVector" );
          RadItemState->append_node( StateVector );
          
          val = doc->allocate_string( speedstr );
          xml_node<char> *SpeedValue = doc->allocate_node( node_element, "SpeedValue", val );
          StateVector->append_node( SpeedValue );
        }//if( speed_ > 0 )
        
        if( occupied )
        {
          val = doc->allocate_string( occupied );
          RadMeasurement->append_node( doc->allocate_node( node_element, "OccupancyIndicator", val ) );
        }
      }//end add other information
      
      
      //Potential child nodes of <RadMeasurement> we could
      //<GrossCounts>, <DoseRate>, <TotalDose>, <ExposureRate>, <TotalExposure>,
      //  <RadInstrumentState>, <RadDetectorState>, <RadItemState>, <RadMeasurementExtension>
      
    }catch( std::exception &e )
    {
      cerr << "Measurement::add_spectra_to_measurment_node_in_2012_N42_xml(...): something horrible happened!: " << e.what() << endl;
    }//try catch
  }//void Measurement::add_to_2012_N42_xml(...)
  
  
  
  bool SpecFile::write_2012_N42( std::ostream& ostr ) const
  {
    std::shared_ptr< rapidxml::xml_document<char> > doc = create_2012_N42_xml();
    
    //  if( !!doc )
    //    rapidxml::print( ostr, *doc, rapidxml::print_no_indenting );
    if( !!doc )
      rapidxml::print( ostr, *doc );
    
    
    return !!doc;
  }//bool write_2012_N42( std::ostream& ostr ) const
  
  
  std::string SpecFile::concat_2012_N42_characteristic_node( const rapidxml::xml_node<char> *char_node )
  {
    //      const rapidxml::xml_attribute<char> *char_id = char_node->first_attribute( "id", 2 );
    const rapidxml::xml_attribute<char> *date = char_node->first_attribute( "valueDateTime", 13 );
    const rapidxml::xml_attribute<char> *limits = char_node->first_attribute( "valueOutOfLimits", 16 );
    const rapidxml::xml_node<char> *remark_node = char_node->first_node( "Remark", 6 );
    const rapidxml::xml_node<char> *name_node = char_node->first_node( "CharacteristicName", 18 );
    const rapidxml::xml_node<char> *value_node = char_node->first_node( "CharacteristicValue", 19 );
    const rapidxml::xml_node<char> *unit_node = char_node->first_node( "CharacteristicValueUnits", 24 );
    //  const rapidxml::xml_node<char> *class_node = char_node->first_node( "CharacteristicValueDataClassCode", 32 );
    
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
  
  
  void SpecFile::set_2012_N42_instrument_info( const rapidxml::xml_node<char> *info_node )
  {
    if( !info_node )
      return;
    
    //  const rapidxml::xml_attribute<char> *id = info_node->first_attribute( "id", 2 );
    
    const rapidxml::xml_node<char> *remark_node = info_node->first_node( "Remark", 6 );
    if( remark_node )
      remarks_.push_back( xml_value_str(remark_node) );
    
    
    const rapidxml::xml_node<char> *manufacturer_name_node = info_node->first_node( "RadInstrumentManufacturerName", 29 );
    if( manufacturer_name_node && !XML_VALUE_COMPARE(manufacturer_name_node,"unknown") )
      manufacturer_ = xml_value_str(manufacturer_name_node);  //ex. "FLIR Systems"
    
    const rapidxml::xml_node<char> *instr_id_node = info_node->first_node( "RadInstrumentIdentifier", 23, false );
    if( instr_id_node && instr_id_node->value_size() )
      instrument_id_ = xml_value_str(instr_id_node);          //ex. "932222-76"
    
    const rapidxml::xml_node<char> *model_node = info_node->first_node( "RadInstrumentModelName", 22 );
    
    if( !model_node )  //file_format_test_spectra/n42_2006/identiFINDER/20130228_184247Preliminary2010.n42
      model_node = info_node->first_node( "RadInstrumentModel", 18 );
    if( model_node && !XML_VALUE_COMPARE(model_node,"unknown") )
      instrument_model_ = xml_value_str( model_node );          //ex. "identiFINDER 2 ULCS-TNG"
    
    const rapidxml::xml_node<char> *desc_node = info_node->first_node( "RadInstrumentDescription", 24 );
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
          cerr << "Failed to read lane number from '" << val << "'" << endl;
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
    
    
    const rapidxml::xml_node<char> *infoextension_node = info_node->first_node( "RadInstrumentInformationExtension", 33 );
    //  const rapidxml::xml_node<char> *operator_node = infoextension_node ? infoextension_node->first_node("InterSpec:MeasurmentOperator",18) : (const rapidxml::xml_node<char> *)0;
    
    //<InterSpec:Inspection> node is vestigual as of 20160607, and only kept in to read older files (with SpecFile_2012N42_VERSION==1), of which, there are probably not many, if any around
    const rapidxml::xml_node<char> *inspection_node = infoextension_node ? infoextension_node->first_node("InterSpec:Inspection",20) : (const rapidxml::xml_node<char> *)0;
    
    const rapidxml::xml_node<char> *detector_type_node = infoextension_node ? infoextension_node->first_node("InterSpec:DetectorType",22) : (const rapidxml::xml_node<char> *)0;
    
    //
    
    //  if( operator_node )
    //    measurment_operator_ = xml_value_str( operator_node );
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
    
    
    const rapidxml::xml_node<char> *class_code_node = info_node->first_node( "RadInstrumentClassCode", 22 );
    instrument_type_ = xml_value_str( class_code_node );
    
    if( SpecUtils::iequals_ascii( instrument_type_, "Other" ) )
      instrument_type_ = "";
    
    for( const rapidxml::xml_node<char> *version_node = XML_FIRST_NODE(info_node, "RadInstrumentVersion");
        version_node;
        version_node = XML_NEXT_TWIN(version_node) )
    {
      const rapidxml::xml_node<char> *name = version_node->first_node( "RadInstrumentComponentName", 26 );
      const rapidxml::xml_node<char> *version = version_node->first_node( "RadInstrumentComponentVersion", 29 );
      
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
        //const rapidxml::xml_node<char> *remark = version_node->first_node( "Remark", 6 );
        //if( remark && remark->value_size() )
        //comment += ((name->value_size() || version->value_size()) ? ", " : " ")
        //+ string("Remark: ") + xml_value_str(remark_node);
      }//if( unknown ) / else
    }//for( loop over "RadInstrumentVersion" nodes )
    
    
    const rapidxml::xml_node<char> *qc_node = info_node->first_node( "RadInstrumentQualityControl", 27 );
    if( qc_node )
    {
      const rapidxml::xml_attribute<char> *id = qc_node->first_attribute( "id", 2 );
      const rapidxml::xml_node<char> *remark_node = qc_node->first_node( "Remark", 6 );
      const rapidxml::xml_node<char> *date_node = qc_node->first_node( "InspectionDateTime", 18 );
      const rapidxml::xml_node<char> *indicator_node = qc_node->first_node( "InCalibrationIndicator", 22 );
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
    
    for( const rapidxml::xml_node<char> *charac_node = info_node->first_node( "RadInstrumentCharacteristics", 28 );
        charac_node;
        charac_node = XML_NEXT_TWIN(charac_node) )
    {
      //    const rapidxml::xml_attribute<char> *id = charac_node->first_attribute( "id", 2 );
      const rapidxml::xml_node<char> *remark_node = charac_node->first_node( "Remark", 6 );
      
      if( remark_node )
      {
        string remark = xml_value_str( remark_node );
        trim( remark );
        if(!remark.empty())
          remarks_.push_back( remark );
      }
      
      for( const rapidxml::xml_node<char> *char_node = charac_node->first_node( "Characteristic", 14 );
          char_node;
          char_node = XML_NEXT_TWIN(char_node) )
      {
        const string comment = concat_2012_N42_characteristic_node( char_node );
        if(!comment.empty())
          remarks_.push_back( comment );
      }//for( loop over "Characteristic" nodes )
      
      for( const rapidxml::xml_node<char> *group_node = charac_node->first_node( "CharacteristicGroup", 19 );
          group_node;
          group_node = XML_NEXT_TWIN(group_node) )
      {
        //      const rapidxml::xml_attribute<char> *char_id = group_node->first_attribute( "id", 2 );
        const rapidxml::xml_node<char> *remark_node = group_node->first_node( "Remark", 6 );
        const rapidxml::xml_node<char> *name_node = group_node->first_node( "CharacteristicGroupName", 23 );
        
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
        
        for( const rapidxml::xml_node<char> *char_node = group_node->first_node( "Characteristic", 14 );
            char_node;
            char_node = XML_NEXT_TWIN(char_node) )
        {
          
          using rapidxml::internal::compare;
          const rapidxml::xml_node<char> *name_node = char_node->first_node( "CharacteristicName", 18 );
          
          
          if( name_node && XML_VALUE_ICOMPARE(name_node, "Operator Name") )
          {
            const rapidxml::xml_node<char> *value_node = char_node->first_node( "CharacteristicValue", 19 );
            measurment_operator_ = xml_value_str(value_node);
          }else
          {
            const string comment = concat_2012_N42_characteristic_node( char_node );
            if(!comment.empty())
              remarks_.push_back( precursor + comment );
          }//
        }//for( loop over "Characteristic" nodes )
      }//for( loop over "CharacteristicGroup" nodes )
    }//for( loop over "RadInstrumentCharacteristics" nodes )
    
    
    //  rapidxml::xml_node<char> *info_extension_node = info_node->first_node( "RadInstrumentInformationExtension", 33 );
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
      
      rapidxml::xml_node<char> *remark_node = cal_node->first_node( "Remark", 6 );
      rapidxml::xml_node<char> *coef_val_node = cal_node->first_node( "CoefficientValues", 17 );
      rapidxml::xml_node<char> *energy_boundry_node = cal_node->first_node( "EnergyBoundaryValues", 20 );
      rapidxml::xml_node<char> *date_node = cal_node->first_node( "CalibrationDateTime", 19 );
      
      if( !coef_val_node )
        coef_val_node = cal_node->first_node( "Coefficients", 12 );
      
      if( remark_node && remark_node->value_size() )
        remarks.push_back( "Calibration for " + id + " remark: " + xml_value_str(remark_node) );
      
      if( date_node && date_node->value_size() )
        remarks.push_back( id + " calibrated " + xml_value_str(date_node) );
      
      MeasurementCalibInfo info;
      if( coef_val_node && coef_val_node->value_size() )
      {
        info.equation_type = SpecUtils::EnergyCalType::Polynomial;
        vector<string> fields;
        const char *data = coef_val_node->value();
        const size_t len = coef_val_node->value_size();
        if( !split_to_floats( data, len, info.coefficients ) )
          throw runtime_error( "Invalid calibration value: " + xml_value_str(coef_val_node) );
        
        while(!info.coefficients.empty() && info.coefficients.back()==0.0f )
          info.coefficients.erase( info.coefficients.end()-1 );
        
        if( info.coefficients.size() < 2 )
        {
          cerr << "Warning: found a EnergyCalibration CoefficientValues with "
          << info.coefficients.size() << " coefficients, which isnt enough, "
          << "skipping calibration" << endl;
          continue;
        }//if( info.coefficients.size() < 2 )
        
        info.calib_id = xml_value_str( coef_val_node->first_attribute( "id", 2 ) );
        if( info.calib_id.empty() ) //Some detectors have the ID attribute on the <EnergyCalibration> node
          info.calib_id = xml_value_str( id_att );
        
        
        //  XXX - deviation pairs not yet not, because I have no solid examples files.
        //  The EnergyDeviationValues and EnergyValues child elements provide a means
        //  to account for the difference in the energy predicted by the second-order
        //  polynomial equation and the true energy.
        rapidxml::xml_node<char> *energy_values_node = cal_node->first_node( "EnergyValues", 12 );
        rapidxml::xml_node<char> *energy_deviation_node = cal_node->first_node( "EnergyDeviationValues", 21 );
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
            
            if( !split_to_floats( energiesstr, energystrsize, energies ) )
              throw runtime_error( "" );
            
            if( !split_to_floats( devstrs, devstrsize, deviations ) )
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
        
        if( !split_to_floats( data, len, info.coefficients ) )
          throw runtime_error( "Failed to parse lower channel energies" );
      }else
      {
        cerr << "Warning, found an invalid EnergyCalibration node" <<endl;
        continue;
      }
      
      if( calibrations.count(id) != 0 )
        cerr << "Warning, overwriting calibration '" << id << "'" << endl;
      calibrations[id] = info;
    }//for( loop over "Characteristic" nodes )
    
  }//get_2012_N42_energy_calibrations(...)
  
  
  
  
  void SpecFile::decode_2012_N42_detector_state_and_quality( std::shared_ptr<Measurement> meas,
                                                            const rapidxml::xml_node<char> *meas_node )
  {
    using rapidxml::internal::compare;
    
    if( !meas_node || !meas )
      return;
    
    meas->quality_status_ = SpecUtils::QualityStatus::Good;  //2012 N42 defaults to good
    const rapidxml::xml_node<char> *detector_state_node = meas_node->first_node( "RadDetectorState", 16 );
    
    if( detector_state_node )
    {
      const rapidxml::xml_node<char> *fault = detector_state_node->first_node( "Fault", 5 );
      const rapidxml::xml_node<char> *remark = XML_FIRST_NODE( detector_state_node, "Remark" );
      
      if( fault && fault->value_size() )
      {
        if( XML_VALUE_ICOMPARE( fault, "Fatal" )
           || XML_VALUE_ICOMPARE( fault, "Error" ) )
          meas->quality_status_ = SpecUtils::QualityStatus::Bad;
        else if( XML_VALUE_ICOMPARE( fault, "Warning" ) )
          meas->quality_status_ = SpecUtils::QualityStatus::Suspect;
      }else if( !detector_state_node->first_node() ||
               (remark && SpecUtils::starts_with( xml_value_str(remark), "InterSpec could not")) )
      {
        meas->quality_status_ = SpecUtils::QualityStatus::Missing; //InterSpec Specific
      }
    }//if( detector_state_node )
    
    
    const rapidxml::xml_node<char> *inst_state_node = XML_FIRST_NODE(meas_node, "RadInstrumentState" );
    if( !inst_state_node )
      inst_state_node = XML_FIRST_NODE(meas_node, "RadItemState" );
    if( !inst_state_node )
      inst_state_node = XML_FIRST_NODE(meas_node, "RadDetectorState");
    
    if( inst_state_node )
    {
      const rapidxml::xml_node<char> *StateVector = inst_state_node->first_node( "StateVector", 11 );
      const rapidxml::xml_node<char> *GeographicPoint = (StateVector ? StateVector->first_node( "GeographicPoint", 15 ) : (const rapidxml::xml_node<char> *)0);
      
      if( GeographicPoint )
      {
        const rapidxml::xml_node<char> *longitude = GeographicPoint->first_node( "LongitudeValue", 14 );
        const rapidxml::xml_node<char> *latitude = GeographicPoint->first_node( "LatitudeValue", 13 );
        //      const rapidxml::xml_node<char> *elevation= GeographicPoint->first_node( "ElevationValue", 14 );
        const rapidxml::xml_node<char> *time = GeographicPoint->first_node( "PositionTime", 12 ); //An InterSpec Addition
        
        if( !longitude )
          longitude = GeographicPoint->first_node( "Longitude", 9 );
        if( !latitude )
          latitude = GeographicPoint->first_node( "Latitude", 8 );
        //      if( !elevation )
        //        elevation = GeographicPoint->first_node( "Elevation", 9 );
        
        const string longstr = xml_value_str( longitude );
        const string latstr = xml_value_str( latitude );
        //      const string elevstr = xml_value_str( elevation );
        const string timestr = xml_value_str( time );
        
        if( longstr.size() )
          meas->longitude_ = atof( longstr.c_str() );
        if( latstr.size() )
          meas->latitude_ = atof( latstr.c_str() );
        //      if( elevstr.size() )
        //        meas->elevation_ = atof( elevstr.c_str() );
        
        if( timestr.size()
           && SpecUtils::valid_longitude(meas->longitude_)
           && SpecUtils::valid_latitude(meas->latitude_) )
          meas->position_time_ =  time_from_string( timestr.c_str() );
        
        //      static std::mutex sm;
        //      {
        //        std::lock_guard<std::mutex> lock(sm);
        //        cerr << "time=" << time << ", timestr=" << timestr << ", meas->position_time_=" << SpecUtils::to_iso_string(meas->position_time_) << endl;
        //      }
      }//if( GeographicPoint )
    }//if( RadInstrumentState )
    
    rapidxml::xml_node<char> *extension_node = meas_node->first_node( "RadMeasurementExtension", 23 );
    
    if( extension_node )
    {
      //This is vestigial for SpecFile_2012N42_VERSION==2
      rapidxml::xml_node<char> *title_node = extension_node->first_node( "InterSpec:Title", 15 );
      meas->title_ = xml_value_str( title_node );
      
      //This is vestigial for SpecFile_2012N42_VERSION==1
      rapidxml::xml_node<char> *type_node = extension_node->first_node( "InterSpec:DetectorType", 22 );
      meas->detector_description_ = xml_value_str( type_node );
    }//if( detector_type_.size() || title_.size() )
  }//void decode_2012_N42_detector_state_and_quality(...)
  
  
  void SpecFile::decode_2012_N42_rad_measurment_node(
                                                     vector< std::shared_ptr<Measurement> > &measurments,
                                                     const rapidxml::xml_node<char> *meas_node,
                                                     const IdToDetectorType *id_to_dettype_ptr,
                                                     DetectorToCalibInfo *calibrations_ptr,
                                                     std::mutex &meas_mutex,
                                                     std::mutex &calib_mutex )
  {
    assert( id_to_dettype_ptr );
    assert( calibrations_ptr );
    
    try
    {
      //We will copy <remarks> and parse warnings from meas_node to each SpecUtils::Measurement that we will create from it
      vector<string> remarks, parse_warnings;
      float real_time = 0.0;
      boost::posix_time::ptime start_time;
      SourceType spectra_type = SourceType::Unknown;
      OccupancyStatus occupied = OccupancyStatus::Unknown;
      
      rapidxml::xml_attribute<char> *meas_att = meas_node->first_attribute( "id", 2, false );
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
        }//else ... another format I dont recall seeing.
      }//if( samp_det_str.size() )
      
      for( rapidxml::xml_node<char> *remark_node = meas_node->first_node( "Remark", 6 );
          remark_node;
          remark_node = XML_NEXT_TWIN(remark_node) )
      {
        string remark = SpecUtils::trim_copy( xml_value_str(remark_node) );
        
        const bool parse_warning = SpecUtils::starts_with( remark, s_parser_warn_prefix );
        if( parse_warning )
        {
          SpecUtils::ireplace_all( remark, s_parser_warn_prefix, "" );
          parse_warnings.emplace_back( std::move(remark) );
        }else if( remark.size() )
        {
          remarks.emplace_back( std::move(remark) );
        }
      }//for( loop over remarks _
      
      rapidxml::xml_node<char> *class_code_node = meas_node->first_node( "MeasurementClassCode", 20 ); //XML_FIRST_NODE( meas_node, "MeasurementClassCode" )
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
      
      rapidxml::xml_node<char> *time_node = meas_node->first_node( "StartDateTime", 13 );
      if( time_node && time_node->value_size() )
      {
        //ToDo: figure which endian-ess is best.  I've seen at leats one N42 file
        //  that was little endian (ex "15-05-2017T18:51:50"), but by default
        //  #time_from_string tries middle-endian first.
        start_time = time_from_string( xml_value_str(time_node).c_str() );
        //start_time = SpecUtils::time_from_string_strptime( xml_value_str(time_node).c_str(), SpecUtils::LittleEndianFirst );
      }
      
      rapidxml::xml_node<char> *real_time_node = meas_node->first_node( "RealTimeDuration", 16 );
      if( !real_time_node )
        real_time_node = meas_node->first_node( "RealTime", 8 );
      if( real_time_node && real_time_node->value_size() )
        real_time = time_duration_string_to_seconds( real_time_node->value(), real_time_node->value_size() );
      
      rapidxml::xml_node<char> *occupancy_node = meas_node->first_node( "OccupancyIndicator", 18 );
      if( occupancy_node && occupancy_node->value_size() )
      {
        if( XML_VALUE_ICOMPARE(occupancy_node, "true") || XML_VALUE_ICOMPARE(occupancy_node, "1") )
          occupied = OccupancyStatus::Occupied;
        else if( XML_VALUE_ICOMPARE(occupancy_node, "false") || XML_VALUE_ICOMPARE(occupancy_node, "0") )
          occupied =  OccupancyStatus::NotOccupied;
      }//if( occupancy_node && occupancy_node->value_size() )
      
      vector< std::shared_ptr<Measurement> > spectrum_meas, neutron_meas;
      
      //Lets track the Measurement to calibration id value incase multiple spectra
      //  are given for the same detector and <RadMeasurement>, but with different
      //  energy binnings.
      vector< pair<std::shared_ptr<Measurement>,string> > meas_to_cal_id;
      
      
      for( const rapidxml::xml_node<char> *spectrum_node = XML_FIRST_NODE(meas_node, "Spectrum");
          spectrum_node;
          spectrum_node = XML_NEXT_TWIN(spectrum_node) )
      {
        using rapidxml::internal::compare;
        rapidxml::xml_attribute<char> *id_att = spectrum_node->first_attribute( "id", 2, false );
        rapidxml::xml_attribute<char> *det_info_att = spectrum_node->first_attribute( "radDetectorInformationReference", 31, false );  //case sensitive false for refAO7WGOXDJ4
        rapidxml::xml_attribute<char> *calib_att = spectrum_node->first_attribute( "energyCalibrationReference", 26, false );
        //      rapidxml::xml_attribute<char> *_att = spectrum_node->first_attribute( "fullEnergyPeakEfficiencyCalibrationReference", 44 );
        //      rapidxml::xml_attribute<char> *_att = spectrum_node->first_attribute( "FWHMCalibrationReference", 24 );
        //      rapidxml::xml_attribute<char> *_att = spectrum_node->first_attribute( "intrinsicDoubleEscapePeakEfficiencyCalibrationReference", 55 );
        //      rapidxml::xml_attribute<char> *_att = spectrum_node->first_attribute( "intrinsicFullEnergyPeakEfficiencyCalibrationReference", 53 );
        //      rapidxml::xml_attribute<char> *_att = spectrum_node->first_attribute( "intrinsicSingleEscapePeakEfficiencyCalibrationReference", 55 );
        //      rapidxml::xml_attribute<char> *_att = spectrum_node->first_attribute( "radRawSpectrumReferences", 24 );
        //      rapidxml::xml_attribute<char> *_att = spectrum_node->first_attribute( "totalEfficiencyCalibrationReference", 35 );
        
        
        auto meas = std::make_shared<Measurement>();
        DetectionType det_type = GammaDetection;
        
        //Get the detector name from the XML det_info_att if we have it, otherwise
        //  if there is only one detector description in the file, we will assume
        //  this spetrum is from that.
        if( det_info_att && det_info_att->value_size() )
          meas->detector_name_ = xml_value_str( det_info_att );
        else if( id_to_dettype_ptr->size()==1 )
          meas->detector_name_ = id_to_dettype_ptr->begin()->first;
        
        if( meas->detector_name_ == s_unnamed_det_placeholder )
          meas->detector_name_.clear();
        
        
        auto det_iter = id_to_dettype_ptr->find( meas->detector_name_ );
        if( det_iter != end(*id_to_dettype_ptr) )
        {
          det_type = det_iter->second.first;
          meas->detector_description_ = det_iter->second.second; //e.x. "HPGe 50%"
        }//if( det_iter != id_to_dettype_ptr->end() )
        
        const rapidxml::xml_node<char> *live_time_node = spectrum_node->first_node( "LiveTimeDuration", 16 );
        if( !live_time_node )
          live_time_node = spectrum_node->first_node( "LiveTime", 8 );
        
        //Some detectors mistakenly put the <LiveTimeDuration> tag under the
        //  <RadMeasurement> tag
        if( !live_time_node && spectrum_node->parent() )
        {
          live_time_node = XML_FIRST_NODE(spectrum_node->parent(), "LiveTimeDuration");
          if( !live_time_node )
            live_time_node = XML_FIRST_NODE(spectrum_node->parent(), "LiveTime");
        }
        
        
        const rapidxml::xml_node<char> *channel_data_node = spectrum_node->first_node( "ChannelData", 11 );
        
        for( size_t i = 0; i < remarks.size(); ++i )
          meas->remarks_.push_back( remarks[i] );
        
        for( size_t i = 0; i < parse_warnings.size(); ++i )
          meas->parse_warnings_.push_back( parse_warnings[i] );
        
        bool use_remark_real_time = false;
        
        for( rapidxml::xml_node<char> *remark_node = spectrum_node->first_node( "Remark", 6 );
            remark_node;
            remark_node = XML_NEXT_TWIN(remark_node) )
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
            //  see notes in create_2012_N42_xml() and add_spectra_to_measurment_node_in_2012_N42_xml()
            //snprintf( thisrealtime, sizeof(thisrealtime), "RealTime: PT%fS", realtime_used );
            remark = SpecUtils::trim_copy( remark.substr(9) );
            meas->real_time_ = time_duration_string_to_seconds( remark );
            
            use_remark_real_time = (meas->real_time_ > 0.0);
          }else if( SpecUtils::istarts_with( remark, "Title:") )
          {
            //Starting with SpecFile_2012N42_VERSION==3, title is encoded as a remark prepended with 'Title: '
            remark = SpecUtils::trim_copy( remark.substr(6) );
            meas->title_ += remark;
          }else if( remark.size() )
          {
            meas->remarks_.emplace_back( std::move(remark) );
          }
        }//for( loop over remarks )
        
        
        //This next line is specific to file written by InterSpec
        //const string samp_det_str = xml_value_str( meas_att ); //This was used pre 20180225, however I believe this was wrong due to it probably not containing DetXXX - we'll see.
        const string samp_det_str = xml_value_str( spectrum_node );
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
          }else if( sample_num_from_meas_node != -999 )
          {
            meas->sample_number_ = sample_num_from_meas_node;
          }
        }else if( sample_num_from_meas_node != -999 )
        {
          meas->sample_number_ = sample_num_from_meas_node;
        }//if( samp_det_str.size() )
        
#if(PERFORM_DEVELOPER_CHECKS)
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
        
        //RealTime shouldnt be under Spectrum node (should be under RadMeasurement)
        //  but some files mess this up, so check for real time under the spectrum
        //  node if we dont have the real time yet
        if(  meas->real_time_ <= 0.0f )
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
          
          rapidxml::xml_attribute<char> *comp_att = channel_data_node->first_attribute( "compressionCode", 15 );
          if( icontains( xml_value_str(comp_att), "CountedZeroes") )  //( comp_att && XML_VALUE_ICOMPARE(comp_att, "CountedZeroes") )
            expand_counted_zeros( *gamma_counts, *gamma_counts );
        }//if( channel_data_node && channel_data_node->value() )
        
        //Well swapp meas->gamma_count_sum_ and meas->neutron_counts_sum_ in a bit if we need to
        meas->gamma_count_sum_ = 0.0;
        for( const float a : *gamma_counts )
          meas->gamma_count_sum_ += a;
        
        const rapidxml::xml_node<char> *RadItemState = meas_node->first_node( "RadItemState", 12 );
        const rapidxml::xml_node<char> *StateVector = (RadItemState ? RadItemState->first_node( "StateVector", 11 ) : (const rapidxml::xml_node<char> *)0);
        const rapidxml::xml_node<char> *SpeedValue = (StateVector ? StateVector->first_node( "SpeedValue", 10 ) : (const rapidxml::xml_node<char> *)0);
        
        if( SpeedValue && SpeedValue->value_size() )
        {
          const string val = xml_value_str( SpeedValue );
          if( !(stringstream(val) >> (meas->speed_)) )
            cerr << "Failed to convert '" << val << "' to a numeric speed" << endl;
        }//if( speed_ > 0 )
        
        if( det_type == OtherDetection )
          continue;
        
        bool is_gamma = (det_type == GammaDetection);
        bool is_neutron = ((det_type == NeutronDetection) || (det_type == GammaAndNeutronDetection));
        if( det_type == GammaAndNeutronDetection )
        {
          const string att_val = xml_value_str( id_att );
          is_gamma = !(icontains(att_val,"Neutron") || icontains(att_val,"Ntr"));
          
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
#if(PERFORM_DEVELOPER_CHECKS)
            log_developer_error( __func__, "Found a gamma spectrum without calibration information" );
#endif
            //continue;
          }//if( !calib_att || !calib_att->value_size() )
          
          
          const string detnam = xml_value_str(calib_att);
          
          std::lock_guard<std::mutex> lock( calib_mutex );
          
          map<string,MeasurementCalibInfo>::iterator calib_iter
          = calibrations_ptr->find( detnam );
          
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
                info.second.nbin = meas->gamma_counts_->size();
                info.second.coefficients.push_back( 0.0f );
                info.second.coefficients.push_back( 3000.0f / std::max(info.second.nbin-1,size_t(1)) );
                //info.second.calib_id = defCalName;  //Leave commented out so wont get put into meas_to_cal_id
                calib_iter = calibrations_ptr->insert( std::move(info) ).first;
              }//if( we havent yet created a default calibration )
            }//if( we have a single calibration we can use ) / else
          }//if( no calibration present already )
          
          assert( calib_iter != calibrations_ptr->end() );
          
          MeasurementCalibInfo &calib = calib_iter->second;
          
          calib.nbin = meas->gamma_counts_->size();
          calib.fill_binning();
          
          if( !calib.binning )
          {
            cerr << "Calibration somehow invalid for '" << detnam
            << "', skipping filling out." << endl;
            continue;
          }//if( !calib.binning )
          
          meas->calibration_coeffs_ = calib.coefficients;
          meas->deviation_pairs_    = calib.deviation_pairs_;
          meas->channel_energies_   = calib.binning;
          meas->energy_calibration_model_  = calib.equation_type;
          
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
        //      const rapidxml::xml_node<char> *extension_node = meas_node->first_node( "SpectrumExtension", 17 );
        
        decode_2012_N42_detector_state_and_quality( meas, meas_node );
        
        spectrum_meas.push_back( meas );
      }//for( loop over "Spectrum" nodes )
      
      //flir radHUNTER N42 files is the inspiration for this next loop that
      // checks if there is a min, max, and total neutron <GrossCounts> node
      //  for this <RadMeasurement> node.
      bool min_neut = false, max_neut = false, total_neut = false, has_other = false;
      
      //XML_FOREACH_DAUGHTER( node, meas_node, "GrossCounts" )
      for( auto node = XML_FIRST_NODE( meas_node, "GrossCounts" );
          node;
          node = XML_NEXT_TWIN(node) )
      {
        const rapidxml::xml_attribute<char> *att = node->first_attribute( "radDetectorInformationReference", 31, false );
        const bool is_min = XML_VALUE_ICOMPARE(att, "minimumNeutrons");
        const bool is_max = XML_VALUE_ICOMPARE(att, "maximumNeutrons");
        const bool is_total = XML_VALUE_ICOMPARE(att, "totalNeutrons");
        
        min_neut = (min_neut || is_min);
        max_neut = (max_neut || is_max);
        total_neut = (total_neut || is_total);
        has_other = (has_other || (!is_min && !is_max && !is_total));
      }//for( loop over GrossCounts nodes )
      
      const bool has_min_max_total_neutron = ((min_neut && max_neut && total_neut) && !has_other);
      
      for( auto gross_counts_node = XML_FIRST_NODE( meas_node, "GrossCounts" );
          gross_counts_node;
          gross_counts_node = XML_NEXT_TWIN(gross_counts_node) )
      {
        const rapidxml::xml_node<char> *live_time_node = gross_counts_node->first_node( "LiveTimeDuration", 16 );
        const rapidxml::xml_node<char> *count_data_node = gross_counts_node->first_node( "CountData", 9 );
        const rapidxml::xml_attribute<char> *det_info_att = gross_counts_node->first_attribute( "radDetectorInformationReference", 31, false );
        
        std::shared_ptr<Measurement> meas = std::make_shared<Measurement>();
        DetectionType det_type = GammaDetection;
        
        const string det_info_ref = xml_value_str( det_info_att );
        
        if( det_info_ref.empty() )
        {
          cerr << "Found GrossCounts node with no radDetectorInformationReference"
          << endl;
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
          cerr << "No detector information for '" << meas->detector_name_
          << "' so skipping" << endl;
          continue;
        }//if( !id_to_dettype_ptr->count( meas->detector_name_ ) )
        
        
        det_type = det_iter->second.first;
        meas->detector_description_ = det_iter->second.second; //e.x. "HPGe 50%"
        
        if( icontains( det_info_ref, "Neutrons" ) )
          det_type = NeutronDetection;
        
        if( (det_type != NeutronDetection)
           && (det_type != GammaAndNeutronDetection) )
        {
#if(PERFORM_DEVELOPER_CHECKS)
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
            cerr << endl;
          }//if( det_iter == id_to_dettype_ptr->end() )
#endif
          continue;
        }
        
        //This next line is specific to file written by InterSpec
        //const string sample_det_att = xml_value_str( meas_att ); //See notes above about pre 20180225,
        const string sample_det_att = xml_value_str( gross_counts_node );
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
          }else if( sample_num_from_meas_node != -999 )
          {
            meas->sample_number_ = 0;
          }else
          {
#if(PERFORM_DEVELOPER_CHECKS)
            char buffer[256];
            snprintf( buffer, sizeof(buffer),
                     "Unrecognized 'id' attribute of Spectrum node: '%s'", sample_det_att.c_str() );
            log_developer_error( __func__, buffer );
#endif
          }
        }//if( sample_det_att.size() )
        
        bool use_remark_real_time = false;
        
        for( size_t i = 0; i < remarks.size(); ++i )
          meas->remarks_.push_back( remarks[i] );
        
        
        for( auto remark_node = XML_FIRST_NODE(gross_counts_node, "Remark");
            remark_node;
            remark_node = XML_NEXT_TWIN(remark_node) )
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
          meas->live_time_ = time_duration_string_to_seconds( live_time_node->value(), live_time_node->value_size() );
        
        const rapidxml::xml_node<char> *RadItemState = XML_FIRST_NODE(meas_node, "RadItemState");
        const rapidxml::xml_node<char> *StateVector  = xml_first_node( RadItemState, "StateVector" );
        const rapidxml::xml_node<char> *SpeedValue   = xml_first_node( StateVector, "SpeedValue" );
        
        if( SpeedValue && SpeedValue->value_size() )
        {
          const string val = xml_value_str( SpeedValue );
          if( !(stringstream(val) >> (meas->speed_)) )
            cerr << "Failed to convert '" << val << "' to a numeric speed" << endl;
        }//if( speed_ > 0 )
        
        meas->contained_neutron_ = true;
        
        if( !count_data_node || !count_data_node->value_size() )
          count_data_node = gross_counts_node->first_node( "GrossCountData", 14 );
        
        if( !count_data_node || !count_data_node->value_size() )
        {
          cerr << "Found a GrossCount node without a CountData node, skipping" << endl;
          continue;
        }
        
        SpecUtils::split_to_floats( count_data_node->value(),
                                   count_data_node->value_size(),
                                   meas->neutron_counts_ );
        for( size_t i = 0; i < meas->neutron_counts_.size(); ++i )
          meas->neutron_counts_sum_ += meas->neutron_counts_[i];
        
        decode_2012_N42_detector_state_and_quality( meas, meas_node );
        
        neutron_meas.push_back( meas );
      }//for( loop over GrossCounts Node )
      
      vector<std::shared_ptr<Measurement>> meas_to_add;
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
          
          meas_to_add.push_back( gamma );
        }//for( size_t i = 0; i < gamma_and_neut_pairs.size(); ++i )
        
        
        for( size_t i = 0; i < spectrum_meas.size(); ++i )
          if( spectrum_meas[i] )
            meas_to_add.push_back( spectrum_meas[i] );
        
        for( size_t i = 0; i < neutron_meas.size(); ++i )
          if( neutron_meas[i] )
            meas_to_add.push_back( neutron_meas[i] );
      }//
      
      //XXX - todo - should implement the below
      //    rapidxml::xml_node<char> *dose_rate_node = meas_node->first_node( "DoseRate", 8 );
      //    rapidxml::xml_node<char> *total_dose_node = meas_node->first_node( "TotalDose", 9 );
      //    rapidxml::xml_node<char> *exposure_rate_node = meas_node->first_node( "ExposureRate", 12 );
      //    rapidxml::xml_node<char> *total_exposure_node = meas_node->first_node( "TotalExposure", 13 );
      //    rapidxml::xml_node<char> *instrument_state_node = meas_node->first_node( "RadInstrumentState", 18 );
      //    rapidxml::xml_node<char> *item_state_node = meas_node->first_node( "RadItemState", 12 );
      
      //Check for duplicate spectrum in spectrum_meas for the same detector, but
      //  with different calibrations.
      //  See comments for #energy_cal_variants and #keep_energy_cal_variant.
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
             && fabs(innermeas->live_time_ - meas->live_time_) < 0.01 )
          {
            samenames.push_back( make_pair(innermeas, meas_to_cal_id[j].second ) );
          }
        }//for( size_t j = 0; j < i; ++j )
        
        if( samenames.size() )
        {
          meas->detector_name_ += "_intercal_" + meas_to_cal_id[i].second;
          for( size_t j = 0; j < samenames.size(); ++j )
            samenames[j].first->detector_name_ += "_intercal_" + samenames[j].second;
        }//if( samenames.size() )
      }//for( size_t i = 1; i < meas_to_cal_id.size(); ++i )
      
      {
        std::lock_guard<std::mutex> lock( meas_mutex );
        measurments.insert( measurments.end(), meas_to_add.begin(), meas_to_add.end() );
      }
    }catch( std::exception &e )
    {
      std::lock_guard<std::mutex> lock( meas_mutex );
      cerr << "Error decoding SpecFile::decode2012N42SpectrumNode(...): "
      << e.what() << endl;
    }//try / catch
  }//void decode_2012_N42_rad_measurment_node( const rapidxml::xml_node<char> *spectrum )
  
  
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
      if( SpecUtils::istarts_with( uuid_, "d72b7fa7-4a20-43d4-b1b2-7e3b8c6620c1" ) )
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
    rapidxml::xml_node<char> *creator_node = data_node->first_node( "RadInstrumentDataCreatorName", 28 );
    if( creator_node && creator_node->value_size() )
      remarks_.push_back( "N42 file created by: " + xml_value_str(creator_node) );
    
    for( rapidxml::xml_node<char> *remark_node = data_node->first_node( "Remark", 6 );
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
    
    rapidxml::xml_node<char> *inst_info_node = data_node->first_node( "RadInstrumentInformation", 24 );
    set_2012_N42_instrument_info( inst_info_node );
    
    map<string,MeasurementCalibInfo> calibrations;
    get_2012_N42_energy_calibrations( calibrations, data_node, remarks_, parse_warnings_ );
    
    //XXX - implement using RadItemInformation
    //  for( const rapidxml::xml_node<char> *rad_item_node = data_node->first_node( "RadItemInformation", 18 );
    //      rad_item_node;
    //      rad_item_node = XML_NEXT_TWIN(rad_item_node) )
    //  {
    //    rapidxml::xml_attribute<char> *id_att = rad_item_node->first_attribute( "id", 2, false );
    //    rapidxml::xml_node<char> *remark_node = rad_item_node->first_node( "Remark", 6 );
    //    rapidxml::xml_node<char> *descrip_node = rad_item_node->first_node( "RadItemDescription", 18 );
    //    rapidxml::xml_node<char> *quantity_node = rad_item_node->first_node( "RadItemQuantity", 15 );
    //    rapidxml::xml_node<char> *geometry_node = rad_item_node->first_node( "RadItemMeasurementGeometryDescription", 37 );
    //    rapidxml::xml_node<char> *characteristics_node = rad_item_node->first_node( "RadItemCharacteristics", 22 );
    //    rapidxml::xml_node<char> *extension_node = rad_item_node->first_node( "RadItemInformationExtension", 27 );
    //  }//for( loop over "RadItemInformation" nodes )
    
    //  map<string,int> detname_to_num; //only used when reading in InterSpec generated files
    map<string,pair<DetectionType,string> > id_to_dettype;
    
    XML_FOREACH_DAUGHTER( info_node, data_node, "RadDetectorInformation" )
    {
      rapidxml::xml_attribute<char> *id_att   = info_node->first_attribute( "id", 2, false );
      //rapidxml::xml_node<char> *remark_node   = XML_FIRST_NODE( info_node, "Remark" );
      rapidxml::xml_node<char> *name_node     = XML_FIRST_NODE( info_node, "RadDetectorName" );
      rapidxml::xml_node<char> *category_node = XML_FIRST_NODE( info_node, "RadDetectorCategoryCode" );
      
      //<RadDetectorKindCode> returns "NaI", "HPGe", "PVT", "He3", etc (see determine_rad_detector_kind_code())
      //  and should be utilized at some point.  But would require adding a field to MeasurmentInfo
      //  I think to kind of do it properly.
      
      //rapidxml::xml_node<char> *kind_node     = info_node->first_node( "RadDetectorKindCode" );
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
      
      //    rapidxml::xml_node<char> *detinfo_extension_node = info_node->first_node( "RadDetectorInformationExtension", 31 );
      //    rapidxml::xml_node<char> *det_num_node = detinfo_extension_node ? detinfo_extension_node->first_node( "InterSpec:DetectorNumber", 24 ) : (rapidxml::xml_node<char> *)0;
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
        const string charac_str = SpecFile::concat_2012_N42_characteristic_node(character);
        if( charac_str.size() )
          descrip += string(descrip.size() ? ", " : "") + "{" + charac_str + "}";
      }//loop over characteristics
      
      
      if( type==GammaDetection || type==NeutronDetection || type==GammaAndNeutronDetection )
        id_to_dettype[name] = pair<DetectionType,string>(type,descrip);
    }//for( loop over "RadDetectorInformation" nodes )
    
    rapidxml::xml_node<char> *analysis_node = data_node->first_node( "AnalysisResults", 15 );
    if( analysis_node )
    {
      std::shared_ptr<DetectorAnalysis> analysis_info = std::make_shared<DetectorAnalysis>();
      set_analysis_info_from_n42( analysis_node, *analysis_info );
      //    if( analysis_info->results_.size() )
      detectors_analysis_ = analysis_info;
    }//if( analysis_node )
    
    SpecUtilsAsync::ThreadPool workerpool;
    
    //  Need to make order in measurements_ reproducable
    vector<std::shared_ptr<std::mutex> > meas_mutexs;
    vector< std::shared_ptr< vector<std::shared_ptr<Measurement> > > > measurements_each_meas;
    
    size_t numRadMeasNodes = 0;
    std::mutex meas_mutex, calib_mutex;
    
    for( auto meas_node = XML_FIRST_NODE(data_node, "RadMeasurement");
        meas_node;
        meas_node = XML_NEXT_TWIN(meas_node) )
    {
      //see ref3Z3LPD6CY6
      if( numRadMeasNodes > 32 && xml_value_compare(meas_node->first_attribute("id"), "ForegroundMeasureSum") )
      {
        continue;
      }
      
      std::shared_ptr<std::mutex> mutexptr = std::make_shared<std::mutex>();
      auto these_meas = std::make_shared< vector<std::shared_ptr<Measurement> > >();
      
      ++numRadMeasNodes;
      meas_mutexs.push_back( mutexptr );
      measurements_each_meas.push_back( these_meas );
      
      workerpool.post( [these_meas,meas_node,&id_to_dettype,&calibrations,mutexptr,&calib_mutex](){
        decode_2012_N42_rad_measurment_node( *these_meas, meas_node, &id_to_dettype, &calibrations, *mutexptr, calib_mutex );
      } );
    }//for( loop over "RadMeasurement" nodes )
    
    workerpool.join();
    
    for( size_t i = 0; i < measurements_each_meas.size(); ++i )
      for( size_t j = 0; j < measurements_each_meas[i]->size(); ++j )
        measurements_.push_back( (*measurements_each_meas[i])[j] );
    
    //test for files like "file_format_test_spectra/n42_2006/identiFINDER/20130228_184247Preliminary2010.n42"
    if( measurements_.size() == 2
       && inst_info_node && inst_info_node->first_node( "RadInstrumentModel", 18 ) )
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
      throw runtime_error( "No valid measurments in 2012 N42 file." );
    
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
      const rapidxml::xml_node<char> *daughter = document_node->first_node( "N42InstrumentData", 17 );
      
      if( !daughter )
        throw runtime_error( "Unrecognized N42 file structure" );
      
      load_2006_N42_from_doc( daughter );
      
      bool hprds = false;
      const rapidxml::xml_node<char> *dataformat = document_node->first_node( "ThisDataFormat", 14 );
      if( dataformat && dataformat->value_size() )
        hprds = SpecUtils::icontains( xml_value_str(dataformat), "HPRDS" );
      
      if( hprds )
      {
        const rapidxml::xml_node<char> *node = document_node->first_node( "OnsetDateTime", 13 );
        node = document_node->first_node( "EventCategory", 13 );
        if( node && node->value_size() )
          remarks_.push_back( string("Event Category ") + xml_value_str(node) );
        
        node = document_node->first_node( "EventType", 9 );
        if( node && node->value_size() )
          remarks_.push_back( string("Event Type ") + xml_value_str(node) );
        
        node = document_node->first_node( "EventCode", 9 );
        if( node && node->value_size() )
          remarks_.push_back( string("Event Code ") + xml_value_str(node) );
        
        node = document_node->first_node( "EventNumber", 11 );
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
            typedef map< boost::posix_time::ptime, vector<std::shared_ptr<Measurement> > > time_to_meas_t;
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
              }//if( we have one neutron and one gamma measurment with same start time )
            }//for( time_to_meas_t::value_type &vt : time_to_meas )
            
            
            measurements_ = keepers;
            cleanup_after_load();
          }//if( keepers.size() > 1 )
        }//if( measurements_.size() == 2 ) / else if( measurements_.size() > 2 )
        
      }//if( hprds )
    }else
    {
      rapidxml::xml_node<char> *daughter = document_node->first_node();
      
      if( daughter && daughter->first_node( "Measurement", 11 ) )
        document_node = daughter;
      
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
    
    //  boost::posix_time::ptime ana_start_time;
    //  const rapidxml::xml_node<char> *AnalysisStartDateTime = XML_FIRST_NODE( analysis_node, "AnalysisStartDateTime" );
    //  if( AnalysisStartDateTime )
    //    ana_start_time = time_from_string( xml_value_str(AnalysisStartDateTime).c_str() );
    
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
    XML_FOREACH_DAUGHTER( versionnode, analysis_node, "AnalysisAlgorithmVersion")
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
        const rapidxml::xml_node<char> *remark_node = nuclide_node->first_node( "Remark", 6 );
        const rapidxml::xml_node<char> *nuclide_name_node = nuclide_node->first_node( "NuclideName", 11 );
        const rapidxml::xml_node<char> *nuclide_type_node = nuclide_node->first_node( "NuclideType", 11 );
        const rapidxml::xml_node<char> *confidence_node = nuclide_node->first_node( "NuclideIDConfidenceIndication", 29 );
        if( !confidence_node )
          confidence_node = XML_FIRST_NODE(nuclide_node, "NuclideIDConfidence");  //RadSeeker
        const rapidxml::xml_node<char> *id_desc_node = nuclide_node->first_node( "NuclideIDConfidenceDescription", 30 );
        const rapidxml::xml_node<char> *position_node = nuclide_node->first_node( "SourcePosition", 14 );
        const rapidxml::xml_node<char> *id_indicator_node = nuclide_node->first_node( "NuclideIdentifiedIndicator", 26 ); //says 'true' or 'false', seen in refZ077SD6DVZ
        const rapidxml::xml_node<char> *confidence_value_node = nuclide_node->first_node( "NuclideIDConfidenceValue", 24 ); //seen in refJHQO7X3XFQ (FLIR radHUNTER UL-LGH)
        const rapidxml::xml_node<char> *catagory_desc_node = nuclide_node->first_node( "NuclideCategoryDescription", 26 );  //seen in refQ7M2PV6MVJ
        
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
        
        const rapidxml::xml_node<char> *nuc_activity_node = nuclide_node->first_node( "NuclideActivityValue", 20 );
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
          const rapidxml::xml_node<char> *location_node = position_node->first_node( "RelativeLocation", 16 );
          if( location_node )
          {
            const rapidxml::xml_node<char> *dist_node = location_node->first_node( "DistanceValue", 13 );
            //Could check 'units' attribute, but only valid value is "m"
            if( xml_value_to_flt(dist_node, result.distance_ ) )
              result.distance_ *= 1000.0;
          }//if( position_node )
        }//if( position_node )
        
        
        const rapidxml::xml_node<char> *extention_node = nuclide_node->first_node( "NuclideExtension", 16 );
        if( extention_node )
        {
          const rapidxml::xml_node<char> *SampleRealTime = extention_node->first_node( "SampleRealTime", 14 );
          const rapidxml::xml_node<char> *Detector = extention_node->first_node( "Detector", 8 );
          
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
    
    for( const rapidxml::xml_node<char> *dose_node = analysis_node->first_node( "DoseAnalysisResults", 19 );
        dose_node;
        dose_node = XML_NEXT_TWIN(dose_node) )
    {
      const rapidxml::xml_node<char> *remark_node = dose_node->first_node( "Remark", 6 );
      //const rapidxml::xml_node<char> *start_time_node = dose_node->first_node( "StartTime", 9 );
      const rapidxml::xml_node<char> *avrg_dose_node = dose_node->first_node( "AverageDoseRateValue", 20 );
      const rapidxml::xml_node<char> *total_dose_node = dose_node->first_node( "TotalDoseValue", 14 );
      const rapidxml::xml_node<char> *position_node = dose_node->first_node( "SourcePosition", 14 );
      
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
          //Uhhhg, this isnt correct, but maybye better than nothing?
          result.dose_rate_ = total_dose;
        }
      }
      
      if( position_node )
      {
        const rapidxml::xml_node<char> *location_node = position_node->first_node( "RelativeLocation", 16 );
        if( location_node )
        {
          const rapidxml::xml_node<char> *dist_node = location_node->first_node( "DistanceValue", 13 );
          
          //Could check 'units' attribute, but only val;id value is "m"
          if( xml_value_to_flt( dist_node, result.distance_ ) )
            result.distance_ *= 1000.0;
        }//if( position_node )
      }//if( position_node )
      
      
      if( !result.isEmpty() )
        analysis.results_.push_back( result );
    }//for( loop over DoseAnalysisResults nodes )
  }//void set_analysis_info_from_n42(...)
  
  
  void Measurement::set_2006_N42_spectrum_node_info( const rapidxml::xml_node<char> *spectrum )
  {
    
    if( !spectrum )
      throw runtime_error( "set_2006_N42_spectrum_node_info: Recieved NULL 'Spectrum' node" );
    
    const string xmlns = get_n42_xmlns( spectrum );
    
    for( const rapidxml::xml_node<char> *remark_node = xml_first_node_nso( spectrum, "Remark", xmlns );
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
          parse_warnings_.emplace_back( std::move(remark) );
          continue;
        }
        
        if( SpecUtils::istarts_with( remark, "Title:") )
        {
          remark = remark.substr(6);
          trim( remark );
          title_ = remark;
          continue;
        }
        
        remarks_.push_back( remark );
        
        if( sample_number_ < 0 )
        {
          sample_number_ = sample_num_from_remark( remarks_.back() );
        }else
        {
          const int samplen = sample_num_from_remark( remarks_.back() );
          if( samplen!=sample_number_ && samplen>=0 )
            cerr << "Got multiple sample_nums: " << sample_number_
            << " vs: " << samplen << " from " << remarks_.back() << endl;
          
          //marking it intrinsic activity will happen further down from the 'ID'
          //  attribute, so we wont wast cpu time here checking the remark for itww
          //      if( SpecUtils::icontains( remark, "intrinsic activity") )
          //        source_type_ = SourceType::IntrinsicActivity;
        }
        
        const float thisspeed = speed_from_remark( remark );
        if( thisspeed > 0.0f )
          speed_ = thisspeed;
        
        const string found_detector_name = detector_name_from_remark( remarks_.back() );
        if( !found_detector_name.empty() && detector_name_.empty() )
          detector_name_ = found_detector_name;
        else if( detector_name_ != found_detector_name )
          cerr << "Got multiple detector names: " << detector_name_
          << " vs " << found_detector_name << endl;
      }//for( string remark, remark_lines )
    }//for( loop over remark_nodes )
    
    const rapidxml::xml_attribute<char> *sample_num_att = spectrum->first_attribute( "SampleNumber", 12 );
    if( sample_num_att )
    {
      const string strvalue = xml_value_str( sample_num_att );
      
      if( sample_number_ >= 2 )
        cerr << SRC_LOCATION << "\n\tWarning: replacing sample_number_="
        << sample_number_ << " with whatever will come from "
        << strvalue << endl;
      
      
      if( !toInt( strvalue, sample_number_ ) && !strvalue.empty() )
        cerr << SRC_LOCATION << "\n\tWarning: couldnt convert '" << strvalue
        << "' to an int" << endl;
      else sample_number_ = 1;
    }//if( sample_num_att )
    
    const rapidxml::xml_node<char> *src_type_node = xml_first_node_nso( spectrum, "SourceType", xmlns );
    
    if( src_type_node )
    {
      if( XML_VALUE_ICOMPARE(src_type_node, "Item") )
        source_type_ = SourceType::Foreground;
      else if( XML_VALUE_ICOMPARE(src_type_node, "Background") )
        source_type_ = SourceType::Background;
      else if( XML_VALUE_ICOMPARE(src_type_node, "Calibration") )
        source_type_ = SourceType::Calibration;
      else if( XML_VALUE_ICOMPARE(src_type_node, "Stabilization") ) //RadSeeker HPRDS files have the "Stabilization" source type, which looks like an intrinsic source
        source_type_ = SourceType::IntrinsicActivity;
      else if( XML_VALUE_ICOMPARE(src_type_node, "IntrinsicActivity") )
        source_type_ = SourceType::IntrinsicActivity;
      else
        source_type_ = SourceType::Unknown;
    }//if( src_type_node )
    
    const rapidxml::xml_attribute<char> *id_att = spectrum->first_attribute( "ID", 2, false );
    if( id_att )
    {
      if( XML_VALUE_ICOMPARE( id_att, "intrinsicActivity" ) )
        source_type_ = SourceType::IntrinsicActivity;
    }// if( id_att )
    
    const rapidxml::xml_node<char> *uccupied_node = xml_first_node_nso( spectrum, "Occupied", xmlns );
    
    try
    {
      if( !uccupied_node )                  occupied_ = OccupancyStatus::Unknown;
      else if( is_occupied(uccupied_node) ) occupied_ = OccupancyStatus::Occupied;
      else                                  occupied_ = OccupancyStatus::NotOccupied;
    }catch(...){                            occupied_ = OccupancyStatus::Unknown; }
    
    const rapidxml::xml_node<char> *det_type_node = xml_first_node_nso( spectrum, "DetectorType", xmlns );
    if( det_type_node && det_type_node->value_size() )
      detector_description_ = xml_value_str( det_type_node );
    
    const rapidxml::xml_attribute<char> *quality_attrib = spectrum->first_attribute( "Quality", 7 );
    if( quality_attrib && quality_attrib->value_size() )
    {
      if( XML_VALUE_ICOMPARE( quality_attrib, "Good" ) )
        quality_status_ = QualityStatus::Good;
      else if( XML_VALUE_ICOMPARE( quality_attrib, "Suspect" ) )
        quality_status_ = QualityStatus::Suspect;
      else if( XML_VALUE_ICOMPARE( quality_attrib, "Bad" ) )
        quality_status_ = QualityStatus::Bad;
      else if( XML_VALUE_ICOMPARE( quality_attrib, "Missing" )
              || XML_VALUE_ICOMPARE( quality_attrib, "Unknown" ) )
        quality_status_ = QualityStatus::Missing;
      else
      {
        cerr << SRC_LOCATION << "\n\tWarning: unknow quality status: '"
        << quality_attrib->value() << "' setting to Missing." << endl;
        quality_status_ = QualityStatus::Missing;
      }//if(0.../else/...
    }//if( quality_attrib is valid )
    
    const rapidxml::xml_attribute<char> *detector_attrib = find_detector_attribute( spectrum );
    
    if( detector_attrib && detector_attrib->value_size() )
    {
      if(!detector_name_.empty())
        cerr << SRC_LOCATION << "\n\tWarning: replacing detector name '"
        << detector_name_ << "'' with '" << xml_value_str(detector_attrib) << "'"
        << endl;
      detector_name_ = xml_value_str(detector_attrib);
    }//if( detector_attrib && detector_attrib->value() )
    
    const rapidxml::xml_node<char> *live_time_node  = xml_first_node_nso( spectrum, "LiveTime", xmlns );
    const rapidxml::xml_node<char> *real_time_node  = xml_first_node_nso( spectrum, "RealTime", xmlns );
    const rapidxml::xml_node<char> *start_time_node = xml_first_node_nso( spectrum, "StartTime", xmlns );
    
    if( live_time_node )
      live_time_ = time_duration_string_to_seconds( live_time_node->value(), live_time_node->value_size() );
    if( real_time_node )
      real_time_ = time_duration_string_to_seconds( real_time_node->value(), real_time_node->value_size() );
    
    if( !start_time_node && spectrum->parent() )
      start_time_node = xml_first_node_nso( spectrum->parent(), "StartTime", xmlns );
    
    if( start_time_node )
      start_time_ = time_from_string( xml_value_str(start_time_node).c_str() );
    
    
    //XXX Things we should look for!
    //Need to handle case <Calibration Type="FWHM" FWHMUnits="Channels"> instead of right now only handling <Calibration Type="Energy" EnergyUnits="keV">
    
    const rapidxml::xml_node<char> *channel_data_node = xml_first_node_nso( spectrum, "ChannelData", xmlns );  //can have attribute comsion, Start(The channel number (one-based) of the first value in this element), ListMode(string)
    
    if( !channel_data_node )
    {
      //The N42 analysis result file refU35CG8VWRM get here a lot (its not a valid
      //  spectrum file)
      const char *msg = "Error, didnt find <ChannelData> under <Spectrum>";
      throw runtime_error( msg );
    }//if( !channel_data_node )
    
    const rapidxml::xml_attribute<char> *compress_attrib = channel_data_node->first_attribute(
                                                                                              "Compression", 11 );
    const string compress_type = xml_value_str( compress_attrib );
    std::shared_ptr<std::vector<float>> contents = std::make_shared< vector<float> >();
    
    //Some variants have a <Data> tag under the <ChannelData> node.
    const rapidxml::xml_node<char> *datanode = xml_first_node_nso( channel_data_node, "Data", xmlns );
    if( datanode && datanode->value_size() )
      channel_data_node = datanode;
    
    
    const bool compressed_zeros = icontains(compress_type, "CountedZeroe");
    
    //XXX - this next call to split_to_floats(...) is not safe for non-destructively parsed XML!!!  Should fix.
    SpecUtils::split_to_floats( channel_data_node->value(), *contents, " ,\r\n\t", compressed_zeros );
    //  SpecUtils::split_to_floats( channel_data_node->value(), channel_data_node->value_size(), *contents );
    
    if( compressed_zeros )
      expand_counted_zeros( *contents, *contents );
    else if( (compress_type!="") && (contents->size()>2) && !icontains(compress_type, "Non" ) )
    {
      stringstream msg;
      msg << SRC_LOCATION << "\n\tUnknown spectrum compression type: '"
      << compress_type << "', Compression atribute value='"
      << xml_value_str(compress_attrib) << "'";
      
      cerr << msg.str() << endl;
      throw runtime_error( msg.str() );
    }//if( counted zeros ) / else some other compression
    
    //Fix cambio zero compression
    if( compressed_zeros )
    {
      for( float &val : *contents )
      {
        if( val > 0.0f && val <= 2.0f*FLT_MIN )
          val = 0.0f;
      }
    }//if( compressed_zeros )
    
    const rapidxml::xml_attribute<char> *type_attrib = spectrum->first_attribute( "Type", 4 );
    
    if( !type_attrib )
      type_attrib = spectrum->first_attribute( "DetectorType", 12 );
    
    if( !type_attrib && spectrum->parent() )
      type_attrib = spectrum->parent()->first_attribute( "DetectorType", 12 );            //<SpectrumMeasurement>
    if( !type_attrib && spectrum->parent() && spectrum->parent()->parent() )
      type_attrib = spectrum->parent()->parent()->first_attribute( "DetectorType", 12 );  //<DetectorMeasurement> node
    
    bool is_gamma = true;
    
    try
    {
      is_gamma = is_gamma_spectrum( detector_attrib, type_attrib,
                                   det_type_node, spectrum );
    }catch( std::exception &e )
    {
      if( !channel_data_node || (channel_data_node->value_size() < 10) )
        cerr << SRC_LOCATION << "\n\t: Coudlnt determine detector type: "
        << e.what() << endl << "\tAssuming is a gamma detector" << endl;
    }
    
    if( is_gamma )
    {
      //The below handles a special case for Raytheon-Variant L-1 (see refSJHFSW1DZ4)
      const rapidxml::xml_node<char> *specsize = spectrum->first_node( "ray:SpectrumSize", 16 );
      if( !!contents && contents->size() && specsize && specsize->value_size() )
      {
        vector<int> sizes;
        const char *str = specsize->value();
        const size_t strsize = specsize->value_size();
        if( SpecUtils::split_to_ints( str, strsize, sizes )
           && sizes.size() == 1 )
        {
          const size_t origlen = gamma_counts_->size();
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
      
      
      contained_neutron_ = false;
      gamma_counts_ = contents;
      
      
      for( const rapidxml::xml_node<char> *calibration_node = xml_first_node_nso( spectrum, "Calibration", xmlns );
          calibration_node;
          calibration_node = XML_NEXT_TWIN(calibration_node) )
      {
        try
        {
          decode_n42_2006_binning( calibration_node,
                                  calibration_coeffs_, energy_calibration_model_ );
          
          break;
        }catch( std::exception & )
        {
          calibration_coeffs_.clear();
          energy_calibration_model_ = SpecUtils::EnergyCalType::InvalidEquationType;
        }
      }
      
      //    const rapidxml::xml_node<char> *nonlinarity_node = xml_first_node_nso( spectrum, "NonlinearityCorrection", xmlns );
      for( const float x : *(gamma_counts_) )
        gamma_count_sum_ += x;
    }else
    {
      contained_neutron_ = true;
      if( neutron_counts_.size() < contents->size() )
        neutron_counts_.resize( contents->size(), 0.0 );
      
      for( size_t i = 0; i < contents->size(); ++i )
      {
        neutron_counts_[i] += contents->operator[](i);
        neutron_counts_sum_ += contents->operator[](i);
      }//for( loop over neutron counts )
    }//if( is_gamma ) / else
  }//void set_2006_N42_spectrum_node_info( rapidxml::xml_node<char> *measurementNode )
  
  
  
  void Measurement::set_n42_2006_spectrum_calibration_from_id( const rapidxml::xml_node<char> *doc_node,
                                                              const rapidxml::xml_node<char> *spectrum_node )
  {
    if( !doc_node || !spectrum_node )
      return;
    
    const string xmlns = get_n42_xmlns( spectrum_node );
    
    const rapidxml::xml_attribute<char> *cal_IDs_att = XML_FIRST_ATTRIB( spectrum_node, "CalibrationIDs" );
    
    vector<string> cal_ids;
    split( cal_ids, xml_value_str(cal_IDs_att), " \t" );
    
    
    //If there is only calibration node, but the ID we want doesnt match the only
    //  calibration node, then let the match work off of the first two charcters
    //  of the ID.
    //This is to allow the N42 files in C:\GADRAS\Detector\HPRDS\SmithsNaI
    //  to decode.
    
    int ncalnodes = 0;
    for( const rapidxml::xml_node<char> *node = xml_first_node_nso( doc_node, "Calibration", xmlns );
        node; node = XML_NEXT_TWIN(node) )
    {
      ++ncalnodes;
    }
    
    if( !ncalnodes && doc_node && doc_node->parent() )
    {
      for( auto node = xml_first_node_nso(doc_node->parent(), "Calibration", xmlns); node; node = XML_NEXT_TWIN(node) )
        ++ncalnodes;
    }
    
    if( cal_ids.empty() && ncalnodes != 1 )
      return;
    
    auto *cal_node = xml_first_node_nso( doc_node, "Calibration", xmlns );
    if( !cal_node && doc_node  )
      cal_node = xml_first_node_nso(doc_node->parent(), "Calibration", xmlns);
    
    for( ; cal_node; cal_node = XML_NEXT_TWIN(cal_node) )
    {
      const rapidxml::xml_attribute<char> *id_att = cal_node->first_attribute( "ID", 2, false );
      
      //    if( id_att && id_att->value_size()
      //        && compare( cal_IDs_att->value(), cal_IDs_att->value_size(),
      //                    id_att->value(), id_att->value_size(), case_sensitive) )
      const string id = xml_value_str( id_att );
      bool id_match = (!id.empty() && (std::find(cal_ids.begin(), cal_ids.end(), id) != cal_ids.end()));
      
      if( !id_match && ncalnodes==1 && id.empty() )
        id_match = true;
      
      if( !id_match && ncalnodes==1 && cal_ids.empty() )
        id_match = true;
      
      if( !id_match && ncalnodes==1 && cal_ids.size()==1 )
      {
        string calid = *cal_ids.begin();
        calid = calid.size()>1 ? calid.substr(0,2) : calid;
        string idid = id.size()>1 ? id.substr(0,2) : id;
        id_match = (calid == idid);
      }
      
      if( id_match )
      {
        const rapidxml::xml_attribute<char> *type_att = XML_FIRST_ATTRIB( cal_node, "Type" );
        const rapidxml::xml_attribute<char> *unit_att = XML_FIRST_ATTRIB( cal_node, "EnergyUnits" );
        
        if( type_att && !XML_VALUE_ICOMPARE(type_att, "Energy") )
        {
          continue;
        }//if( not energy calibration node )
        
        float units = 1.0f;
        if( unit_att )
        {
          if( XML_VALUE_ICOMPARE(unit_att, "eV") )
            units = 0.001f;
          else if( XML_VALUE_ICOMPARE(unit_att, "keV") )
            units = 1.0f;
          else if( XML_VALUE_ICOMPARE(unit_att, "MeV") )
            units = 1000.0f;
        }//if( unit_att )
        
        const rapidxml::xml_node<char> *array_node = xml_first_node_nso( cal_node, "ArrayXY", xmlns );
        const rapidxml::xml_node<char> *eqn_node = xml_first_node_nso( cal_node, "Equation", xmlns ); //hmm, was this a mistake
        
        if( array_node && !eqn_node )
        {
          //This function has been added in for FLIR identiFINDER N42 files
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
          //          <PointXY><X>7 0</X><Y>13.2645 0</Y></PointXY>
          //          <PointXY><X>8 0</X><Y>16.1747 0</Y></PointXY>
          //          ...
          
          vector<pair<float,float>> points;
          
          for( const rapidxml::xml_node<char> *point_node = xml_first_node_nso( array_node, "PointXY", xmlns );
              point_node;
              point_node = XML_NEXT_TWIN_CHECKED(point_node) )
          {
            const rapidxml::xml_node<char> *x_node = xml_first_node_nso( point_node, "X", xmlns );
            const rapidxml::xml_node<char> *y_node = xml_first_node_nso( point_node, "Y", xmlns );
            
            if( x_node && x_node->value_size() && y_node && y_node->value_size() )
            {
              float xval = 0.0f, yval = 0.0f;
              if( xml_value_to_flt(x_node, xval)
                 && xml_value_to_flt(y_node, yval) )
              {
                points.emplace_back(xval,yval);
              }//if( could read x and y values )
            }//if( x and y nodes )
          }//for( ; point_node; point_node = XML_NEXT_TWIN_CHECKED(point_node) )
          
          const size_t npoints = points.size();
          const size_t nchannel = gamma_counts_ ? gamma_counts_->size() : size_t(0);
          
          calibration_coeffs_.clear();
          
          if( npoints > 0 && npoints < 3 )  //3 is arbitrary, the cases I've seen has been ==1
          {
            //FLIR identiFINDER style
            energy_calibration_model_ = SpecUtils::EnergyCalType::Polynomial;
            calibration_coeffs_.push_back( 0.0f );
            calibration_coeffs_.push_back( points[0].second );
          }else if( (nchannel>7) && ::abs_diff(npoints,nchannel)<3 ) //The files I've seen have (npoints==nchannel)
          { //The 8 and 3 make sure its actually a gamma spectrum with eenergies given for each channel
            //We also need to make sure the 'x' values are monotonically increasing channel numbers
            //  that start at zero or one.
            bool increasing_bin = ((fabs(points[0].first) < FLT_EPSILON) || (fabs(points[1].first-1.0) < FLT_EPSILON));
            for( size_t i = 1; increasing_bin && i < npoints; ++i )
              increasing_bin = ((fabs(points[i].first-points[i-1].first-1.0f) < FLT_EPSILON) && (points[i].second>=points[i-1].second));
            
            if( increasing_bin )
            {
              energy_calibration_model_ = SpecUtils::EnergyCalType::LowerChannelEdge;
              for( const auto &ff : points )
                calibration_coeffs_.push_back( ff.second );
            }else
            {
              cerr << SRC_LOCATION << "\n\tI couldnt interpret energy calibration PointXY (not monototonically increasing)" << endl;
              energy_calibration_model_ = SpecUtils::EnergyCalType::InvalidEquationType;
            }
          }else
          {
            cerr << SRC_LOCATION << "\n\tI couldnt interpret energy calibration PointXY (unrecognized coefficient meaning, or no channel data)" << endl;
            energy_calibration_model_ = SpecUtils::EnergyCalType::InvalidEquationType;
          }//
          
          if( units != 1.0f )
            for( size_t i = 0; i < calibration_coeffs_.size(); ++i )
              calibration_coeffs_[i] *= units;
          
          return;
        }else if( eqn_node )
        {
          try
          {
            decode_n42_2006_binning( cal_node, calibration_coeffs_, energy_calibration_model_ );
            return;
          }catch( std::exception & )
          {
            //
          }
          
        }//if( array_node ) else if( eqn_node )
      }//if( this is the calibration we want )
    }//for( loop over calibrations )
  }//set_n42_2006_spectrum_calibration_from_id
  
  
  void Measurement::decode_n42_2006_binning(  const rapidxml::xml_node<char> *calibration_node,
                                            vector<float> &coeffs,
                                            SpecUtils::EnergyCalType &eqnmodel )
  {
    coeffs.clear();
    
    if( !calibration_node )
    {
      const string msg = "decode_n42_2006_binning(...): Couldnt find node 'Calibration'";
      throw std::runtime_error( msg );
    }//if( !calibration_node )
    
    string xmlns = get_n42_xmlns(calibration_node);
    if( xmlns.empty() && calibration_node->parent() )
      xmlns = get_n42_xmlns(calibration_node->parent());
    
    const rapidxml::xml_attribute<char> *type = calibration_node->first_attribute( "Type", 4 );
    
    if( type && type->value_size() )
    {
      if( XML_VALUE_ICOMPARE(type, "FWHM") )
        throw runtime_error( "decode_n42_2006_binning(...): passed in FWHM cal node" );
      
      //20160601: Not adding in an explicit comparison for energy, but probably should...
      //if( !XML_VALUE_ICOMPARE(type, "Energy") )
      //  throw runtime_error( "decode_n42_2006_binning(...): passed in non-energy cal node - " + xml_value_str(type) );
    }//if( type && type->value_size() )
    
    const rapidxml::xml_attribute<char> *units = calibration_node->first_attribute( "EnergyUnits", 11 );
    
    string unitstr = xml_value_str(units);
    
    const rapidxml::xml_node<char> *equation_node = xml_first_node_nso( calibration_node, "Equation", xmlns );
    
    if( equation_node )
    {
      const rapidxml::xml_node<char> *coeff_node = xml_first_node_nso( equation_node, "Coefficients", xmlns );
      
      if( coeff_node )
      {
        if( coeff_node->value_size() )
        {
          coeffs.clear();
          SpecUtils::split_to_floats( coeff_node->value(),
                                     coeff_node->value_size(), coeffs );
        }else
        {
          //SmithsNaI HPRDS
          rapidxml::xml_attribute<char> *subeqn = coeff_node->first_attribute( "Subequation", 11 );
          if( subeqn && subeqn->value_size() )
          {
            coeffs.clear();
            SpecUtils::split_to_floats( subeqn->value(),
                                       subeqn->value_size(), coeffs );
          }
        }//if( coeff_node->value_size() ) / else
        
        while( coeffs.size() && coeffs.back()==0.0f )
          coeffs.erase( coeffs.end()-1 );
        
        float units = 1.0f;
        if( unitstr == "eV" )
          units = 0.001f;
        else if( unitstr == "MeV" )
          units = 1000.0f;
        
        if( units != 1.0f )
        {
          for( float &f : coeffs )
            f *= units;
        }
        
        //      static std::recursive_mutex maa;
        //      std::unique_lock<std::recursive_mutex> scoped_lock( maa );
        //      cerr << "coeffs.size()=" << coeffs.size() << "from '"
        //           << coeff_node->value() << "'"
        //           << " which has length " << value_size
        //           << " and strlen of " << strlen(coeff_node->value()) << endl;
      }else
      {
        const string msg = "Couldnt find node 'Coefficients'";
        throw runtime_error( msg );
      }//if( coeff_node ) / else
      
      
      const rapidxml::xml_attribute<char> *model = equation_node->first_attribute( "Model", 5 );
      //    rapidxml::xml_attribute<char> *units = equation_node->first_attribute( "Units", 5 );
      
      eqnmodel = SpecUtils::EnergyCalType::InvalidEquationType;
      
      string modelstr = xml_value_str(model);
      if( modelstr == "Polynomial" )
        eqnmodel = SpecUtils::EnergyCalType::Polynomial;
      else if( modelstr == "FullRangeFraction" )
        eqnmodel = SpecUtils::EnergyCalType::FullRangeFraction;
      else if( modelstr == "LowerChannelEdge" || modelstr == "LowerBinEdge")
        eqnmodel = SpecUtils::EnergyCalType::LowerChannelEdge;
      else if( modelstr == "Other" )
      {
        rapidxml::xml_attribute<char> *form = equation_node->first_attribute( "Form", 4 );
        const string formstr = xml_value_str(form);
        if( icontains(formstr, "Lower edge") )
          eqnmodel = SpecUtils::EnergyCalType::LowerChannelEdge;
      }//if( modelstr == ...) / if / else
      
      //Lets try to guess the equation type for Polynomial or FullRangeFraction
      if( eqnmodel == SpecUtils::EnergyCalType::InvalidEquationType )
      {
        if( (coeffs.size() < 5) && (coeffs.size() > 1) )
        {
          if( coeffs[1] < 10.0 )
            eqnmodel = SpecUtils::EnergyCalType::Polynomial;
          else if( coeffs[1] > 1000.0 )
            eqnmodel = SpecUtils::EnergyCalType::FullRangeFraction;
        }//if( coeffs.size() < 5 )
      }//if( eqnmodel == InvalidEquationType )
      
      
      if( eqnmodel == SpecUtils::EnergyCalType::InvalidEquationType )
      {
        coeffs.clear();
        cerr << "Equation model is not polynomial" << endl;
        const string msg = "Equation model is not polynomial or FullRangeFraction, but is " +
        (modelstr.size()?modelstr:string("NULL"));
        cerr << msg << endl;
        throw runtime_error( msg );
      }//if( model isnt defined )
    }else
    {
      const string msg = "Couldnt find node 'Equation'";
      throw runtime_error( msg );
    }//if( equation_node ) / else
    
  }//void decode_n42_2006_binning(...)

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
      << "      <Counts>" << neutron_counts_sum_ << "</Counts>" << endline
      << "    </CountDoseData>" << endline;
    }//if( contained_neutron_ )
    
    //XXX - deviation pairs completeley untested, and probably not conformant to
    //      N42 2006 specification (or rather the dndons extention).
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
     if( position_time_.is_special() )
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
       && (sample_number_>=0 || !detector_name_.empty() || speed_>0.00000001) )
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
      
      if( (speed_ > 0.00000001) && !wroteSpeed )
      {
        if(!thisremark.empty())
          thisremark += " ";
        thisremark += "Speed " + std::to_string(speed_) + " m/s";
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
     if( !start_time_.is_special() )
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
    
    if(!detector_description_.empty())
      ostr << "      <DetectorType>" << detector_description_ << "</DetectorType>" << endline;
    
    ostr << "      <Calibration Type=\"Energy\" EnergyUnits=\"keV\">" << endline
    << "        <Equation Model=\"";
    
    switch( energy_calibration_model_ )
    {
      case SpecUtils::EnergyCalType::Polynomial:
      case SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial:
        ostr << "Polynomial";
        break;
        
      case SpecUtils::EnergyCalType::FullRangeFraction:    ostr << "FullRangeFraction"; break;
      case SpecUtils::EnergyCalType::LowerChannelEdge:     ostr << "LowerChannelEdge";  break;
      case SpecUtils::EnergyCalType::InvalidEquationType:  ostr << "Unknown";           break;
    }//switch( energy_calibration_model_ )
    
    ostr << "\">" << endline;
    
    ostr << "          <Coefficients>";
    for( size_t i = 0; i < calibration_coeffs_.size(); ++i )
      ostr << (i ? " " : "") << calibration_coeffs_[i];
    
    //Not certain we need this next loop, but JIC
    if( energy_calibration_model_ == SpecUtils::EnergyCalType::LowerChannelEdge
       && calibration_coeffs_.empty()
       && !!channel_energies_
       && !channel_energies_->empty())
    {
      for( size_t i = 0; i < channel_energies_->size(); ++i )
        ostr << (i ? " " : "") << (*channel_energies_)[i];
    }
    
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
      
      if( meas->deviation_pairs_.size() )
      {
        ostr << "    <dndons:NonlinearityCorrection Detector=\"" << name << "\">" << endline;
        for( size_t j = 0; j < meas->deviation_pairs_.size(); ++j )
        {
          ostr << "      <dndons:Deviation>" << meas->deviation_pairs_[j].first
          << " " << meas->deviation_pairs_[j].second << "</dndons:Deviation>" << endline;
        }
        ostr << "    </dndons:NonlinearityCorrection>" << endline;
      }
    }//for( size_t i = 0; i < measurements.size(); ++i )
    
    
    
    if( measurement_location_name_.size() || measurment_operator_.size() )
    {
      ostr << "    <MeasuredItemInformation>" << endline;
      if( measurement_location_name_.size() )
        ostr << "      <MeasurementLocationName>" << measurement_location_name_ << "</MeasurementLocationName>" << endline;
      //<Coordinates>40.12 10.67</Coordinates>
      if( measurment_operator_.size() )
        ostr << "      <MeasurementOperator>" << measurment_operator_ << "</MeasurementOperator>" << endline;
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
      boost::posix_time::ptime starttime = meass[0]->start_time();
      float rtime = meass[0]->real_time_;
      float speed = meass[0]->speed_;
      OccupancyStatus occstatus = meass[0]->occupied_;
      
      for( size_t i = 1; i < meass.size(); ++i )
      {
        const boost::posix_time::ptime tst = meass[i]->start_time();
        starttime = ((tst.is_special() || (starttime < tst)) ? starttime : tst);
        rtime = max( rtime, meass[i]->real_time_ );
        speed = max( speed, meass[i]->speed_ );
        if( occstatus == OccupancyStatus::Unknown )
          occstatus = meass[i]->occupied_;
        else if( meass[i]->occupied_ != OccupancyStatus::Unknown )
          occstatus = max( occstatus, meass[i]->occupied_ );
      }
      
      
      ostr << "  <DetectorData>" << endline;
      if( !starttime.is_special() )
        ostr << "    <StartTime>" << SpecUtils::to_extended_iso_string(starttime) << "Z</StartTime>" << endline;
      if( rtime > 0.0f )
        ostr << "    <SampleRealTime>PT" << rtime << "S</SampleRealTime>" << endline;
      if( occstatus != OccupancyStatus::Unknown )
        ostr << "    <Occupied>" << (occstatus== OccupancyStatus::NotOccupied ? "0" : "1") << "</Occupied>" << endline;
      if( speed > 0.0f )
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

}//namespace SpecUtils



