#ifndef SpecUtils_SpecFile_location_h
#define SpecUtils_SpecFile_location_h
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

#include <memory>

namespace rapidxml
{
  template<class Ch> class xml_node;
  template<class Ch> class xml_document;
}//namespace rapidxml


namespace SpecUtils
{
/** Geographical coordinates providing latitude, longitude, elevation, as well as
 uncertainty of the coordinates.
 
 Represents the data of N42-2012:
 RadInstrumentData->RadMeasurement->RadInstrumentState->StateVector->GeographicPoint
 
 For reading from other formats
 */
struct GeographicPoint
{
  GeographicPoint();
  
  // <LatitudeValue>, <LongitudeValue>, <ElevationValue>, <ElevationOffsetValue>
  // <GeoPointAccuracyValue>, <ElevationAccuracyValue>, <ElevationOffsetAccuracyValue>
  double latitude_;  //set to std::numeric_limits<double>::quiet_NaN() if not specified
  double longitude_; //set to std::numeric_limits<double>::quiet_NaN() if not specified
  float elevation_;  // per N42-2012, in units of meters
  
  /** The difference between the Elevation at the point of coordinate measurement and the
   earth's surface in meters.
   
   Positive values indicate the point of coordinate measurement is above the earth surface, and
   negative values are below.
   
   In units of meters.
   */
  float elevation_offset_; // per N42-2012, in units of meters
  float coords_accuracy_; // per N42-2012, in units of meters
  float elevation_accuracy_; // per N42-2012, in units of meters
  float elevation_offset_accuracy_; // per N42-2012, in units of meters
  
  time_point_t position_time_;
  
  bool has_coordinates() const;
  
#if( SpecUtils_ENABLE_EQUALITY_CHECKS )
  static void equal_enough( const GeographicPoint &lhs, const GeographicPoint &rhs );
#endif
};//struct GeographicPoint


/** Describes the location of an object (i.e., radiation measurement instrument, radiation
 detector, or measured item) or a radiation source relative to a reference point (Origin).
 
 Often this is used to indicate the item of interest location relative to the detector.
 
 Stores coordinates in either cartesian (x,y,z) or polar (azimuth, cartesian, distance) coordinates
 to avoid rounding issues, but you can access either style of coordinates either way, as long
 as you adhere to the N42-2012 units (i.e., use degrees, and not radians).
 
 Important note: the (x,y,z) directions are defined so that x is left-right, y is up/down, and z
 is along detectors axis, which is not consistent with the definition of azimuth (horizontal angle),
 inclination (vertical angle), and distance, as is used in by standard conversions (i.e., the y and
 z axises are swapped).
 
 Roughly corresponds the the N42-2012 RelativeLocation element.
 
 #TODO: conversions between cartesian and polar coordinates totally untested!
 #TODO: Need to put in an Origin, e.g., <GeographicPoint> or <OriginDescription>
 */
struct RelativeLocation
{
  RelativeLocation();
  
  /** Sets coordinates from dx, dx, and dz.
   
   @param dx horizontal displacement.  A common usage is if you are holding a detector,
             positive values are to your right-hand side, and negative values to the left.
   @param dy vertical displacement.  A common usage is positive values are above the nominal
             detector height; negative values below.
   @param dz Displacement along the detector axis.
   
   Commonly used to indicate the item of interests location, relative to the detector.
   
   Units of millimeters are recommended to be consistent with with the N42-2012 standard, but
   if you use
  */
  void from_cartesian( const float dx, const float dy, const float dz );
  
  /** Sets coordinates from azimuth, inclination, distance
   
   @param azimuth The vertical angle, in degrees, w.r.t. the horizontal plane from a reference point
                  (i.e., Origin, which is usually your detector) to an object/nuclide. A value of
                  zero implies the center of the object/nuclide is at the same altitude/elevation
                  as the reference point (e.g., usually your detector); positive values imply the
                  object or nuclide is higher than the reference point; negative values imply the
                  object or nuclide is lower than the reference point.
                  Values range from –90.0 to +90.0. Use be NaN if not set.
                  Corresponds to N42-2012 RelativeLocationInclinationValue element.
   @param inclination The vertical angle, in degrees, w.r.t. the horizontal plane from a reference
                  point (i.e., Origin, which is usually your detector) to an object/nuclide.
                  A value of zero implies the center of the object/nuclide is at the same
                  altitude/elevation as the reference point (e.g., usually your detector); positive
                  values imply the object or nuclide is higher than the reference point; negative
                  values imply the object or nuclide is lower than the reference point.
                  Values range from –90.0 to +90.0.
                  Will be NaN if not set.
                  Corresponds to N42-2012 RelativeLocationInclinationValue element.
   @param distance Distance, in units of millimeters (i.e., 1.0 == 1 mm ), usually total distance
                  between detector and item.
                  Will be NaN if not set.
                  Corresponds to N42-2012 DistanceValue element.
   */
  void from_polar( const float azimuth, const float inclination, const float distance );
  
  
  
  /** Horizontal displacement.

   Will return zero if necessary coordinates are not defined.
   */
  float dx() const;
  
  /** Vertical, i.e., up/down
   
   Will return zero if necessary coordinates are not defined.
   */
  float dy() const;
  
  /** Detector axis.
   
   Will return zero if necessary coordinates are not defined.
   */
  float dz() const;
  
  /** The horizontal angle, in degrees, w.r.t. the reference point (e.g., the direction a detector
   is pointing, or it could be true north, etc) to an object (i.e., instrument, detector, or item)
   or a nuclide.
   
   Positive values mean object/nuclide is to the right of the ref. point; negative values mean
   the object/nuclide is to the left of the ref. point.  A zero value means center of
   object/nuclide is directly in front of ref. point.
   
   Values range from –180.0 to +180.0.
   
   Will be NaN if not set.
   
   Corresponds to N42-2012 RelativeLocationAzimuthValue element.
   */
  float azimuth() const;
  
  /** The vertical angle, in degrees, w.r.t. the horizontal plane from a reference point
   (i.e., Origin, which is usually your detector) to an object/nuclide.
   A value of zero implies the center of the object/nuclide is at the same altitude/elevation
   as the reference point (e.g., usually your detector); positive values imply the object or nuclide
   is higher than the reference point; negative values imply the object or nuclide is lower than
   the reference point.
   
   Values range from –90.0 to +90.0.
   
   Will be NaN if not set.
   
   Corresponds to N42-2012 RelativeLocationInclinationValue element.
   */
  float inclination() const;
  
  /** Distance, in units of millimeters (i.e., 1.0 == 1 mm ), usually total distance between
   detector and item.
   
   Use with #RelativeLocation::azimuth_ and #RelativeLocation::inclination_ to get location.
   
   Will be NaN if not set.
   */
  float distance() const;
  
  
#if( SpecUtils_ENABLE_EQUALITY_CHECKS )
  static void equal_enough( const RelativeLocation &lhs, const RelativeLocation &rhs );
#endif
  
  enum class CoordinateType{ Cartesian, Polar, Undefined };
  
  CoordinateType type_;
  
  float coordinates_[3];
  
  /** Description of the Origin for this RelativeLocation.
   
   If neither this field or #origin_geo_point_ is specified, then a reasonable assumption would
   the origin is on the face of the detector, on the center.
   */
  std::string origin_description_;
  
  /** Origin for this RelativeLocation.
   
   If neither this field or #origin_geo_point_ is specified, then a reasonable assumption would
   the origin is on the face of the detector, on the center.
   */
  std::shared_ptr<const GeographicPoint> origin_geo_point_;
};//struct RelativeLocation


/** The orientation of an object (e.g., radiation measurement instrument, radiation
 detector, or measured item).
 
 Corresponds to the N42-2012 Orientation element.
 
 Values are in degrees, not radians.
 */
struct Orientation
{
  Orientation();
  
  float azimuth_;
  float inclination_;
  float roll_;
  
#if( SpecUtils_ENABLE_EQUALITY_CHECKS )
  static void equal_enough( const Orientation &lhs, const Orientation &rhs );
#endif
};//struct Orientation


/** Provides information on the relative position and/or geographical position of a measurement or
 source.
 
 An approximate representation of the N42-2012 <StateVector> element, that can be the child
 element of <RadDetectorState>, <RadInstrumentState>, or <RadItemState> (which will be indicated
 by #LocationState::type_).
 
 Note: N42-2012, allows a state to provide Choice of {<GeographicPoint>, <LocationDescription>,
 or <RelativeLocation>}, but we currently only implement <GeographicPoint>.
 */
struct LocationState
{
  LocationState();
  
  enum class StateType{ Detector, Instrument, Item, Undefined };
  StateType type_;
  
  float speed_;  //will be NaN if not set
  
  std::shared_ptr<const GeographicPoint> geo_location_;
  std::shared_ptr<const RelativeLocation> relative_location_;
  std::shared_ptr<const Orientation> orientation_;
  
#if( SpecUtils_ENABLE_EQUALITY_CHECKS )
  static void equal_enough( const LocationState &lhs, const LocationState &rhs );
#endif
  
  /** Set information from the N42-2012 <RadDetectorState>, <RadInstrumentState>, or <RadItemState>
   elements; e.g., expects there to be a child <StateVector> element, and if the node you pass
   in isnt one of the three listed above, #type_ will remain #StateType::Undefined.
   
   Throws exception on failure.
   */
  void from_n42_2012( const rapidxml::xml_node<char> * const state_parent_node );
  
  /** Adds in LocationState information to an existing tag of the type <RadDetectorState>,
   <RadInstrumentState>, or <RadItemState> (based value of #type_).
   */
  void add_to_n42_2012( rapidxml::xml_node<char> *node, rapidxml::xml_document<char> *doc ) const;
  
  /** Returns approximate memory this object takes up. */
  size_t memmorysize() const;
};//struct LocationState

}//namespace SpecUtils

#endif //SpecUtils_SpecFile_location_h
