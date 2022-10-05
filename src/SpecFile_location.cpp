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
#include <numeric>

#include "rapidxml/rapidxml.hpp"

#include "SpecUtils/DateTime.h"
#include "SpecUtils/ParseUtils.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/RapidXmlUtils.hpp"
#include "SpecUtils/SpecFile_location.h"


using namespace std;

namespace
{

/** Gets the child element of a parent, and parses its value into a float, returning if that all
 was successful or not.  Doesnt change `answer` unless successful.
 */
template<class Ch,size_t n>
bool parse_child_float( const ::rapidxml::xml_node<Ch> * const parent, const char (&child_name)[n], float &answer )
{
  if( !parent )
    return false;
  
  const ::rapidxml::xml_node<char> * const child = XML_FIRST_NODE(parent, child_name);
  if( !child || !child->value_size() )
    return false;
  
  float val; // Use temp variable as `parse_float` will set it to 0.0, which we dont want
  if( !SpecUtils::parse_float( child->value(), child->value_size(), val) )
    return false;

  answer = val;
  return true;
}//parse_child_float


template<class Ch,size_t n>
bool parse_child_double( const ::rapidxml::xml_node<Ch> * const parent, const char (&child_name)[n], double &answer )
{
  if( !parent )
    return false;
  
  const ::rapidxml::xml_node<char> * const child = XML_FIRST_NODE(parent, child_name);
  if( !child || !child->value_size() )
    return false;
  
  double val; // Use temp variable as `parse_float` will set it to 0.0, which we dont want
  if( !SpecUtils::parse_double( child->value(), child->value_size(), val) )
    return false;
  
  answer = val;
  return true;
}//parse_child_double

}//namespace


namespace SpecUtils
{

GeographicPoint::GeographicPoint()
: latitude_( numeric_limits<double>::quiet_NaN() ),
longitude_( numeric_limits<double>::quiet_NaN() ),
elevation_( numeric_limits<float>::quiet_NaN() ),
elevation_offset_( numeric_limits<float>::quiet_NaN() ),
coords_accuracy_( numeric_limits<float>::quiet_NaN() ),
elevation_accuracy_( numeric_limits<float>::quiet_NaN() ),
elevation_offset_accuracy_( numeric_limits<float>::quiet_NaN() ),
position_time_() //time_point_t::clock::from_time_t(0)
{
  assert( position_time_.time_since_epoch().count() == 0 );
}


bool GeographicPoint::has_coordinates() const
{
  return (SpecUtils::valid_longitude(longitude_) && SpecUtils::valid_latitude(latitude_));
}


RelativeLocation::RelativeLocation()
: type_( RelativeLocation::CoordinateType::Undefined ),
coordinates_{ numeric_limits<float>::quiet_NaN(),
  numeric_limits<float>::quiet_NaN(),
  numeric_limits<float>::quiet_NaN() }
{
  
}


void RelativeLocation::from_cartesian( const float dx, const float dy, const float dz )
{
  type_ = RelativeLocation::CoordinateType::Cartesian;
  coordinates_[0] = dx;
  coordinates_[1] = dy;
  coordinates_[2] = dz;
}//void from_dx_dy_dz( const float dx, const float dy, const float dz )


void RelativeLocation::from_polar( const float azimuth, const float inclination, const float distance )
{
  type_ = RelativeLocation::CoordinateType::Polar;
  coordinates_[0] = azimuth;
  coordinates_[1] = inclination;
  coordinates_[2] = distance;
}


float RelativeLocation::dx() const
{
  switch( type_ )
  {
    case CoordinateType::Undefined:
      return 0.0f;
      
    case CoordinateType::Cartesian:
      return coordinates_[0];
      
    case CoordinateType::Polar:
      break;
  }//switch( type_ )
  
  if( IsNan(coordinates_[2]) )
    return 0.0f;
  
  const double pi = 3.14159265358979323846;
  const double deg_to_rad = pi / 180.0;
  const double azimuth = deg_to_rad * (IsNan(coordinates_[0]) ? 0.0 : coordinates_[0]);
  const double inclination = deg_to_rad * (IsNan(coordinates_[1]) ? 0.0 : coordinates_[1]);
  const double dist = coordinates_[2];
  
  return static_cast<float>( dist * sin(azimuth) * cos(inclination) );
}//dx()


float RelativeLocation::dy() const
{
  switch( type_ )
  {
    case CoordinateType::Undefined:
      return 0.0f;
      
    case CoordinateType::Cartesian:
      return coordinates_[1];
      
    case CoordinateType::Polar:
      break;
  }//switch( type_ )
  
  if( IsNan(coordinates_[2]) )
    return 0.0f;
  
  const double pi = 3.14159265358979323846;
  const double deg_to_rad = pi / 180.0;
  const double azimuth = deg_to_rad * (IsNan(coordinates_[0]) ? 0.0 : coordinates_[0]);
  const double inclination = deg_to_rad * (IsNan(coordinates_[1]) ? 0.0 : coordinates_[1]);
  const double dist = coordinates_[2];
  
  return static_cast<float>( dist * sin(inclination) );
}//dy()


float RelativeLocation::dz() const
{
  switch( type_ )
  {
    case CoordinateType::Undefined:
      return 0.0f;
      
    case CoordinateType::Cartesian:
      return coordinates_[2];
      
    case CoordinateType::Polar:
      break;
  }//switch( type_ )
  
  if( IsNan(coordinates_[2]) )
    return 0.0f;
  
  const double pi = 3.14159265358979323846;
  const double deg_to_rad = pi / 180.0;
  const double azimuth = deg_to_rad * (IsNan(coordinates_[0]) ? 0.0 : coordinates_[0]);
  const double inclination = deg_to_rad * (IsNan(coordinates_[1]) ? 0.0 : coordinates_[1]);
  const double dist = coordinates_[2];
  
  return static_cast<float>( dist * cos(azimuth) * cos(inclination) );
}//dz()


float RelativeLocation::azimuth() const
{
  switch( type_ )
  {
    case CoordinateType::Undefined:
      return 0.0f;
      
    case CoordinateType::Polar:
      return IsNan(coordinates_[0]) ? 0.0f : coordinates_[0];
      
    case CoordinateType::Cartesian:
      break;
  }//switch( type_ )
  
  if( IsNan(coordinates_[0]) || IsNan(coordinates_[2]) )
    return 0.0f;
  
  
  const double rad_to_deg = 180.0 / 3.14159265358979323846;
  
  // Note y and z swapped below
  const double x = coordinates_[0], y = coordinates_[2];
  
  if( x > 0 )
    return static_cast<float>( rad_to_deg * atan( y / x ) );
  
  if( (x < 0) && (y >= 0) )
    return static_cast<float>( (rad_to_deg * atan( y / x )) + 180.0 );
  
  if( (x < 0) && (y < 0) )
    return static_cast<float>( (rad_to_deg * atan( y / x )) - 180.0 );
  
  if( (x == 0) && (y > 0) )
    return 90.0f;
  
  if( (x == 0) && (y < 0) )
    return -90.0f;
  
  // if x==0 && y==0, azimuth undefined; just return 0
  return 0.0;
}//azimuth()


float RelativeLocation::inclination() const
{
  switch( type_ )
  {
    case CoordinateType::Undefined:
      return 0.0f;
      
    case CoordinateType::Polar:
      return IsNan(coordinates_[1]) ? 0.0f : coordinates_[1];
      
    case CoordinateType::Cartesian:
      break;
  }//switch( type_ )
  
  const double rad_to_deg = 180.0 / 3.14159265358979323846;
  
  // Note y and z swapped below
  const double x = IsNan(coordinates_[0]) ? 0.0f : coordinates_[0];
  const double y = IsNan(coordinates_[2]) ? 0.0f : coordinates_[2];
  const double z = IsNan(coordinates_[1]) ? 0.0f : coordinates_[1];
  
  const double r = sqrt( x*x + y*x + z*z );
  
  if( fabs(r) <= numeric_limits<float>::epsilon() ) //reasonably arbitrary
    return 0.0f;
  
  return static_cast<float>( rad_to_deg * acos( z / r ) );
}//inclination()


float RelativeLocation::distance() const
{
  switch( type_ )
  {
    case CoordinateType::Undefined:
      return 0.0f;
      
    case CoordinateType::Polar:
      return coordinates_[2];
      
    case CoordinateType::Cartesian:
      break;
  }//switch( type_ )
  
  // Note y and z swapped below
  const double x = IsNan(coordinates_[0]) ? 0.0f : coordinates_[0];
  const double y = IsNan(coordinates_[2]) ? 0.0f : coordinates_[2];
  const double z = IsNan(coordinates_[1]) ? 0.0f : coordinates_[1];
  
  return static_cast<float>( sqrt( x*x + y*x + z*z ) );
}//distance()


Orientation::Orientation()
: azimuth_( numeric_limits<float>::quiet_NaN() ),
inclination_( numeric_limits<float>::quiet_NaN() ),
roll_( numeric_limits<float>::quiet_NaN() )
{
}


LocationState::LocationState()
: type_( LocationState::StateType::Undefined ),
speed_( numeric_limits<float>::quiet_NaN() ),
geo_location_( nullptr ),
relative_location_( nullptr ),
orientation_( nullptr )
{
}//LocationState


void LocationState::from_n42_2012( const rapidxml::xml_node<char> * const state_parent_node )
{
  if( !state_parent_node )
    throw runtime_error( "LocationState::from_n42_2012: nullptr passed in." );
  
  const rapidxml::xml_node<char> *state_vector = XML_FIRST_NODE(state_parent_node, "StateVector");
  if( !state_vector )
    throw runtime_error( "LocationState::from_n42_2012: no 'StateVector' child element." );
  
  if( XML_NAME_ICOMPARE(state_parent_node, "RadDetectorState") )
    type_ = StateType::Detector;
  else if( XML_NAME_ICOMPARE(state_parent_node, "RadInstrumentState") )
    type_ = StateType::Instrument;
  else if( XML_NAME_ICOMPARE(state_parent_node, "RadItemState") )
    type_ = StateType::Item;
  else
  {
    type_ = StateType::Undefined;
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
    log_developer_error( __func__, ("Unknown parent node passed in: " + xml_name_str(state_parent_node)).c_str() );
#endif
  }
  
  bool read_something_in = false;
  //StateVector child elements: <GeographicPoint> <LocationDescription> <RelativeLocation>}, <Orientation>, <SpeedValue>
  
  // We could look for units attribute of "SpeedValue", but its always supposed to be "m/s", so
  //  we wont do this until its needed for a non-compliant detection system
  parse_child_float( state_vector, "SpeedValue", speed_ );
  
  const rapidxml::xml_node<char> *orientation_node = XML_FIRST_NODE(state_vector, "Orientation");
  if( orientation_node )
  {
    auto tmp = make_shared<Orientation>() ;
    parse_child_float( orientation_node, "AzimuthValue", tmp->azimuth_ );
    parse_child_float( orientation_node, "InclinationValue", tmp->inclination_ );
    parse_child_float( orientation_node, "RollValue", tmp->roll_ );
    
    if( !IsNan(tmp->azimuth_ ) || !IsNan(tmp->inclination_) || !IsNan(tmp->roll_) )
    {
      orientation_ = tmp;
      read_something_in = true;
    }
  }//if( orientation_node )
  
  
  // Now try to fill out GeographicPoint
  auto parse_geo_point = []( const rapidxml::xml_node<char> *geo_node ) -> shared_ptr<GeographicPoint> {
    if( !geo_node )
      return nullptr;
    
      auto geo = make_shared<GeographicPoint>();
      
      // we could check XML_FIRST_ATTRIB(geo_node, "units"), but N42-2012 specifies must be "m", so we wont
      
      parse_child_double(geo_node, "LatitudeValue", geo->latitude_ );
      parse_child_double(geo_node, "LongitudeValue", geo->longitude_ );
      parse_child_float(geo_node, "ElevationValue", geo->elevation_ );
      parse_child_float(geo_node, "ElevationOffsetValue", geo->elevation_offset_ );
      parse_child_float(geo_node, "GeoPointAccuracyValue", geo->coords_accuracy_ );
      parse_child_float(geo_node, "ElevationAccuracyValue", geo->elevation_accuracy_ );
      parse_child_float(geo_node, "ElevationOffsetAccuracyValue", geo->elevation_offset_accuracy_ );
      
      // The N42-2012 spec doesnt include an accounting for reading date time, so we'll try a few
      //   reasonable ways to put it in.
      const rapidxml::xml_base<char> *time = XML_FIRST_NODE(geo_node, "PositionTime" );  //This is how InterSpec does it.
      if( !time )
        time = XML_FIRST_IATTRIB(geo_node, "valueDateTime");
      if( !time )
        time = XML_FIRST_IATTRIB(geo_node, "DateTime");
      if( !time )
        time = XML_FIRST_INODE(geo_node, "DateTime");
      if( !time )
        time = XML_FIRST_INODE(geo_node, "ValueDateTime");
      
      if( time && time->value_size() )
      {
        const string timestr = xml_value_str( time );
        if( timestr.size() )
          geo->position_time_ = time_from_string( timestr.c_str() );
      }
      
      if( (SpecUtils::valid_longitude(geo->longitude_)
           && SpecUtils::valid_latitude(geo->latitude_))
         || !IsNan(geo->elevation_) )
      {
        return geo;
      }
    
#if(PERFORM_DEVELOPER_CHECKS && !SpecUtils_BUILD_FUZZING_TESTS)
      log_developer_error( __func__, "Failed to parse anything useful from 'GeographicPoint' node." );
#endif
    
    return nullptr;
  };//parse_geo_point lamda

  const rapidxml::xml_node<char> *geo_node = XML_FIRST_NODE(state_vector, "GeographicPoint");

  geo_location_ = parse_geo_point( geo_node );
  if( geo_location_ )
    read_something_in = true;

  
  // Now try to fill out RelativeLocation
  //std::shared_ptr<RelativeLocation> relative_location_;
  const rapidxml::xml_node<char> *rel_loc_node = XML_FIRST_NODE(state_vector, "RelativeLocation");
  if( rel_loc_node )
  {
    // RelativeLocation child elements: <RelativeLocationAzimuthValue>, <RelativeLocationInclinationValue>, <DistanceValue>, <Origin>
    auto loc = std::make_shared<RelativeLocation>();
    loc->type_ = RelativeLocation::CoordinateType::Polar;
    
    parse_child_float( rel_loc_node, "RelativeLocationAzimuthValue", loc->coordinates_[0] );
    parse_child_float( rel_loc_node, "RelativeLocationInclinationValue", loc->coordinates_[1] );
    parse_child_float( rel_loc_node, "DistanceValue", loc->coordinates_[2] );
    
    const rapidxml::xml_node<char> *origin_node = XML_FIRST_NODE(rel_loc_node, "Origin");
    if( origin_node )
    {
      const rapidxml::xml_node<char> *desc_node = XML_FIRST_NODE(origin_node, "OriginDescription");
      loc->origin_description_ = xml_value_str(desc_node);
    }
    
    const rapidxml::xml_node<char> *geo_point_node = XML_FIRST_NODE(origin_node, "GeographicPoint");
    loc->origin_geo_point_ = parse_geo_point( geo_point_node );
    
    if( !IsNan(loc->coordinates_[0]) || !IsNan(loc->coordinates_[1]) || !IsNan(loc->coordinates_[2])
       || loc->origin_geo_point_ )
    {
      relative_location_ = loc;
      read_something_in = true;
    }
  }//if( rel_loc_node )
  
  if( !read_something_in )
    throw runtime_error( "No info read in" );
}//void from_n42_2012( const rapidxml::xml_node<char> *state_vector_node )


void LocationState::add_to_n42_2012( ::rapidxml::xml_node<char> *node ) const
{
  using namespace rapidxml;
  
  xml_document<char> * const doc = node ? node->document() : nullptr;
  if( !doc )
    throw runtime_error( "No XML document available." );
  
  assert( XML_NAME_ICOMPARE(node, "RadDetectorState")
         || XML_NAME_ICOMPARE(node, "RadInstrumentState")
         || XML_NAME_ICOMPARE(node, "RadItemState") );
  
  
  xml_node<char> *StateVector = doc->allocate_node( node_element, "StateVector" );
  node->prepend_node( StateVector ); // <StateVector> should be first element, according to n42_2012.xsd
  
  auto make_float_el = [doc]( xml_node<char> *parent, const char *name, double value, bool more_precision ){
    assert( parent );
    if( IsNan(value) )
      return;
    
    char valstr[32] = { '\0' };
    snprintf(valstr, sizeof(valstr), (more_precision ? "%.12f" : "%.8f"), value);
    
    char *val = doc->allocate_string( valstr );
    xml_node<char> *child = doc->allocate_node( node_element, name, val );
    parent->append_node( child );
  };//make_float_el lambda
  
  auto make_geo_point_node = [doc, make_float_el]( const std::shared_ptr<const GeographicPoint> &geo ) -> xml_node<char> * {
    assert( geo );
    xml_node<char> *GeographicPoint = doc->allocate_node( node_element, "GeographicPoint" );
    
    make_float_el( GeographicPoint, "LatitudeValue", geo->latitude_, true );
    make_float_el( GeographicPoint, "LongitudeValue", geo->longitude_, true );
    make_float_el( GeographicPoint, "ElevationValue", geo->elevation_, false );
    make_float_el( GeographicPoint, "ElevationOffsetValue", geo->elevation_offset_, false );
    make_float_el( GeographicPoint, "GeoPointAccuracyValue", geo->coords_accuracy_, false );
    make_float_el( GeographicPoint, "ElevationAccuracyValue", geo->elevation_accuracy_, false );
    make_float_el( GeographicPoint, "ElevationOffsetAccuracyValue", geo->elevation_offset_accuracy_, false );
    
    return GeographicPoint;
  };//make_geo_point_node
  
  if( geo_location_ )
  {
    xml_node<char> *GeographicPoint = make_geo_point_node( geo_location_ );
    StateVector->append_node( GeographicPoint );
  }//if( geo_location_ )
  
  if( relative_location_ )
  {
    xml_node<char> *RelativeLocation = doc->allocate_node( node_element, "RelativeLocation" );
    StateVector->append_node( RelativeLocation );
    
    make_float_el( RelativeLocation, "RelativeLocationAzimuthValue", relative_location_->azimuth(), false );
    make_float_el( RelativeLocation, "RelativeLocationInclinationValue", relative_location_->inclination(), false );
    make_float_el( RelativeLocation, "DistanceValue", relative_location_->distance(), false );
    
    xml_node<char> *Origin = doc->allocate_node( node_element, "Origin" );
    RelativeLocation->append_node( Origin );
    
    if( relative_location_->origin_geo_point_ )
    {
      xml_node<char> *geo_node = make_geo_point_node( relative_location_->origin_geo_point_ );
      Origin->append_node(geo_node);
    }
    
    if( !relative_location_->origin_description_.empty() )
    {
      const string &desc = relative_location_->origin_description_;
      char *val = doc->allocate_string( desc.c_str(), desc.size() + 1 );
      xml_node<char> *OriginDescription = doc->allocate_node( node_element, "OriginDescription", val, 17, desc.size() );
      Origin->append_node( OriginDescription );
    }
  }//if( relative_location_ )
  
  
  if( orientation_ )
  {
    xml_node<char> *Orientation = doc->allocate_node( node_element, "Orientation" );
    StateVector->append_node( Orientation );
    
    make_float_el( Orientation, "AzimuthValue", orientation_->azimuth_, false );
    make_float_el( Orientation, "InclinationValue", orientation_->inclination_, false );
    make_float_el( Orientation, "RollValue", orientation_->roll_, false );
  }//if( orientation_ )
  
  make_float_el( StateVector, "SpeedValue", speed_, false );
  
}//void add_to_n42_2012( ::rapidxml::xml_node<char> *node ) const


#if( SpecUtils_ENABLE_EQUALITY_CHECKS )

void test_float( const float lhs, const float rhs, const char *name, string &error_msg )
{
  if( IsNan(lhs) && IsNan(rhs) )
    return;
  
  if( (IsNan(lhs) != IsNan(rhs)) && (IsNan(lhs) || IsNan(rhs)) )
  {
    error_msg += error_msg.length() ? "\n" : "";
    error_msg += "variable " + string(name) + " has "
                 + string( IsNan(lhs) ? "RHS" : "LHS" ) + " set, and not "
                 + string( IsNan(lhs) ? "LHS" : "RHS" );
    return;
  }//
  
  const double diff = fabs( static_cast<double>(lhs) - static_cast<double>(rhs) );
  const double maxval = std::max( fabs(lhs), fabs(rhs) );
  
  if( (diff > (maxval*1.0E-5)) && (diff > 1.0E-4) )  //Arbitrarily chosen constants, more-or-less
  {
    error_msg += error_msg.length() ? "\n" : "";
    
    char buffer[256] = { '\0' };
    snprintf( buffer, sizeof(buffer),
             "variable %s LHS value (%1.8E) doesnt match RHS (%1.8E)",
             name, lhs, rhs );
    error_msg += buffer;
  }//if( (diff > (maxval*1.0E-5)) && (diff > 1.0E-4) )
}//void test_float( const float lhs, const float rhs, const char *name, string &error_msg )


void GeographicPoint::equal_enough( const GeographicPoint &lhs, const GeographicPoint &rhs )
{
  string error_msg;
  test_float( lhs.latitude_, rhs.latitude_, "GeographicPoint::latitude_", error_msg );
  test_float( lhs.longitude_, rhs.longitude_, "GeographicPoint::longitude_", error_msg );
  test_float( lhs.elevation_, rhs.elevation_, "GeographicPoint::elevation_", error_msg );
  test_float( lhs.elevation_offset_, rhs.elevation_offset_, "GeographicPoint::elevation_offset_", error_msg );
  test_float( lhs.coords_accuracy_, rhs.coords_accuracy_, "GeographicPoint::coords_accuracy_", error_msg );
  test_float( lhs.elevation_accuracy_, rhs.elevation_accuracy_, "GeographicPoint::elevation_accuracy_", error_msg );
  test_float( lhs.elevation_offset_accuracy_, rhs.elevation_offset_accuracy_, "GeographicPoint::elevation_offset_accuracy_", error_msg );
  
  if( lhs.position_time_ != rhs.position_time_ )
  {
    error_msg += error_msg.length() ? "\n" : "";
    error_msg += "variable position_time_ LHS value ("
                 + SpecUtils::to_iso_string(lhs.position_time_)
                 + ") doesnt match RHS (" + SpecUtils::to_iso_string(rhs.position_time_) + ")";
  }//if( lhs.position_time_ != rhs.position_time_ )
  
  if( error_msg.length() )
    throw runtime_error( error_msg );
}//void GeographicPoint::equal_enough(...)


void RelativeLocation::equal_enough( const RelativeLocation &lhs, const RelativeLocation &rhs )
{
  string error_msg;
  
  if( lhs.type_ != rhs.type_ )
  {
    auto to_str = []( const RelativeLocation::CoordinateType t ) -> std::string {
      switch( t )
      {
        case CoordinateType::Cartesian: return "Cartesian";
        case CoordinateType::Polar:     return "Polar";
        case CoordinateType::Undefined: return "Undefined";
      }
      assert( 0 );
      return "invalid";
    };//auto to_str
   
    
    // If original non-N42-2012 file gave dx, dy, dz, then when we write it out to N42-2012,
    //  it will now be in Polar coordinates, so we need to account for this.
    if( (lhs.type_ == CoordinateType::Undefined) || (rhs.type_ == CoordinateType::Undefined) )
    {
      error_msg = "RelativeLocation LHS type (" + to_str(lhs.type_) + ") doesnt match RHS type ("
      + to_str(rhs.type_) + ").";
      throw runtime_error( error_msg );
    }//if( either side is undefined )
    
    test_float( lhs.dx(), rhs.dx(), "RelativeLocation::dx", error_msg );
    test_float( lhs.dy(), rhs.dy(), "RelativeLocation::dy", error_msg );
    test_float( lhs.dz(), rhs.dz(), "RelativeLocation::dz", error_msg );
  }else
  {
    test_float( lhs.coordinates_[0], rhs.coordinates_[0], "RelativeLocation::coordinate[0]", error_msg );
    test_float( lhs.coordinates_[1], rhs.coordinates_[1], "RelativeLocation::coordinate[1]", error_msg );
    test_float( lhs.coordinates_[2], rhs.coordinates_[2], "RelativeLocation::coordinate[2]", error_msg );
  }//if( lhs.type_ != rhs.type_ ) / else
  
  if( lhs.origin_description_ != rhs.origin_description_ )
  {
    error_msg += error_msg.length() ? "\n" : "";
    error_msg += "Origin description of LHS ('" + lhs.origin_description_
                 + "') doesnt match RHS ('" + rhs.origin_description_ + "').";
  }//if( lhs.origin_description_ != rhs.origin_description_ )
  
  if( (!lhs.origin_geo_point_) != (!rhs.origin_geo_point_) )
  {
    error_msg += error_msg.length() ? "\n" : "";
    error_msg += "Origin geography point specified for "
                + string(lhs.origin_geo_point_ ? "LHS" : "RHS")
                + " but not "+ string(lhs.origin_geo_point_ ? "RHS" : "LHS") + ".";
  }else if( lhs.origin_geo_point_ && rhs.origin_geo_point_ )
  {
    try
    {
      GeographicPoint::equal_enough( *lhs.origin_geo_point_, *rhs.origin_geo_point_ );
    }catch( std::exception &e )
    {
      error_msg += error_msg.length() ? "\n" : "";
      error_msg += e.what();
    }
  }//if( (!lhs.origin_geo_point_) != (!rhs.origin_geo_point_) ) / else
  
  if( error_msg.length() )
    throw runtime_error( error_msg );
}//void RelativeLocation::equal_enough(...)


void Orientation::equal_enough( const Orientation &lhs, const Orientation &rhs )
{
  string error_msg;
  
  test_float( lhs.azimuth_, rhs.azimuth_, "Orientation::azimuth_", error_msg );
  test_float( lhs.inclination_, rhs.inclination_, "Orientation::inclination_", error_msg );
  test_float( lhs.roll_, rhs.roll_, "Orientation::roll_", error_msg );
  
  if( error_msg.length() )
    throw runtime_error( error_msg );
}//void equal_enough(...)


void LocationState::equal_enough( const LocationState &lhs, const LocationState &rhs )
{
  auto to_str = []( const LocationState::StateType t ) -> std::string {
    switch( t )
    {
      case StateType::Detector: return "Detector";
      case StateType::Instrument: return "Instrument";
      case StateType::Item: return "Item";
      case StateType::Undefined: return "Undefined";
    }
    assert( 0 );
    return "Invalid";
  };//to_str( LocationState::StateType )
    
  string error_msg;
  
  if( lhs.type_ != rhs.type_ )
  {
    error_msg += "LocationState::type_ LHS value (" + to_str(lhs.type_)
                 + ") doesnt match RHS value (" + to_str(rhs.type_) + ").";
  }//if( lhs.type_ != rhs.type_ )
  
  test_float( lhs.speed_, rhs.speed_, "LocationState::speed_", error_msg );
  
  try
  {
    if( (!lhs.geo_location_) != (!rhs.geo_location_) )
      throw runtime_error( "LocationState::geo_location_ set on "
                           + string(lhs.geo_location_ ? "LHS" : "RHS")
                           + " but not "
                          + string(lhs.geo_location_ ? "RHS" : "LHS") );
    if( lhs.geo_location_ && rhs.geo_location_ )
      GeographicPoint::equal_enough( *lhs.geo_location_, *rhs.geo_location_ );
  }catch( std::exception &e )
  {
    error_msg += (error_msg.empty() ? "" : "\n");
    error_msg += e.what();
  }//try / catch test geo_location_
  
  try
  {
    if( (!lhs.relative_location_) != (!rhs.relative_location_) )
      throw runtime_error( "LocationState::relative_location_ set on "
                          + string(lhs.relative_location_ ? "LHS" : "RHS")
                          + " but not "
                          + string(lhs.relative_location_ ? "RHS" : "LHS") );
    if( lhs.relative_location_ && rhs.relative_location_ )
      RelativeLocation::equal_enough( *lhs.relative_location_, *rhs.relative_location_ );
  }catch( std::exception &e )
  {
    error_msg += (error_msg.empty() ? "" : "\n");
    error_msg += e.what();
  }//try / catch test relative_location_
  
  try
  {
    if( (!lhs.orientation_) != (!rhs.orientation_) )
      throw runtime_error( "LocationState::orientation_ set on "
                          + string(lhs.orientation_ ? "LHS" : "RHS")
                          + " but not "
                          + string(lhs.orientation_ ? "RHS" : "LHS") );
    if( lhs.orientation_ && rhs.orientation_ )
      Orientation::equal_enough( *lhs.orientation_, *rhs.orientation_ );
  }catch( std::exception &e )
  {
    error_msg += (error_msg.empty() ? "" : "\n");
    error_msg += e.what();
  }//try / catch test relative_location_
  
  if( error_msg.length() )
    throw runtime_error( error_msg );
}//void equal_enough(...)
#endif  //#if( SpecUtils_ENABLE_EQUALITY_CHECKS )


}//namespace SpecUtils


