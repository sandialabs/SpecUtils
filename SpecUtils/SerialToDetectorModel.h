#ifndef SerialToDetectorModel_h
#define SerialToDetectorModel_h
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

#include <string>
#include <cstdint>

#if( PERFORM_DEVELOPER_CHECKS )
#include <vector>
#include <memory>
#endif

/** For some detector models, most notable Ortec HPGe detectors, the exact model
    of the detector may not be included in the file.  However, it can be
    important to know the exact model to use the correct detector response
    function (DRF).
 
    For example using a EX100 DRF for data taken with an Micro will give you
    about a factor of four off for Cs137; worse, if you are fitting for
    shielding amount (for an isotope with multiple photopeaks), you will get the
    wrong shielding amount/type, which may throw your answer off even more.
 
    This namespace allows you to read in a CSV file that maps serial number to
    detector model, so the correct one can be assigned at the time of parsing
    the files.
 */
namespace SerialToDetectorModel
{
  /** The CSV file format major version.
   
      Updates indicate that the newer CSV file can not be used by an older
      version of the program/parser, but older versions of the CSV should be
      able to be used (as much as possible) by the program (e.g., parser will be
      backwards compatible).  An example change would be no longer representing
      serial number by a uint32_t.
   
      May be specified on first non-comment line of CSV file in format
      "SerialToDetectorModelVersion,MAJOR,MINOR"
      where MAJOR and MINOR are >=0 integers.  If not specified, latest version
      is assumed.
   */
  static const int SerialToDetectorModel_CURRENT_MAJOR_VERSION = 0;
  
  
  /** The CSV file format minor version.
   
      Updates indicate non-breaking changes added to CSV format version.
      E.x., a new detector model added to CSV file; older parsers will just skip
      over those models.
   */
  static const int SerialToDetectorModel_CURRENT_MINOR_VERSION = 0;
  
  
  /** Sets the file to read in the serial number to model mapping.
   
      This function is intentended to be set once at executable startup, before
      parsing any spectrum files.
   
      This function does not read the file in - this is done in a thread safe
      manner the first time a detector model is requested (e.g., once you call
      this function, thread safety is garunteed in all of SpecUtils).  Delayed
      file parsing is done to prevent application startup delay.
   
      The file must be UTF-8 encoded, and quoting or escaping is not supported
      (e.g., commas always start a new field, even if they are within quotes).
   
      Each row in the file represents a different detector, with the first
      column giving the serial number, and the second column giving the detector
      model.  The detector model must exactly match the values in the
      #DetectorModel enum, including case.  There may optionally be more columns
      in each row, where the third column gives the description of how the model
      was determined, and the fourth column gives other places where data for
      this detector may be found.
   */
  void set_detector_model_input_csv( const std::string &filename );
  
  
  /** An enum to list the detector models supported by the utilities in this
      namespace.  Not using #DetectorType to avoid pulling in dependancy on
      SpectrumDataStructs.h, and to disambiguate the functionality of this
      utility.
   */
  enum class DetectorModel : uint32_t
  {
    /** The input CSV has not been set, or was invalid. */
    NotInitialized,
    
    /** The serial number passed in was not in the input CSV file. */
    UnknownSerialNumber,
    
    /** The serial number was specified as unknown in the CSV file.
        This is generally used to indicate detectors for which we are aware of
        their serial number, but have been unable to definitively identify their
        model, but would like to provide notes (e.x., propable a EX100) or
        other places data from that detector has been seen.
     */
    Unknown,
    
    /** A Detective-DX, Detective-EX, or MicroDetective.  May be a
        MicroDetective if the detector model determined via ratios of escape
        peaks (Micro, DX, and EX all have same size HPGe crystal).
     */
    DetectiveEx,
    
    /** A MicroDetective, as indicated by N42 file, an image of detector,
        SPC file, or other definitive information.
     */
    MicroDetective,
    
    /** */
    DetectiveEx100,
    
    /** */
    Detective200
  };//enum class DetectorModel
  
  
  /** return string representation of DetectorModel; exact same as how enum
      value is defined (e.g., "Unknown", "DetectiveEx100", etc.).
  */
  const std::string &to_str( const DetectorModel model );
  
  
  
  /** Returns the #DetectorModel based on serial number match, or
      #DetectorModel::Unknown if the serial number isnt known.
   */
  DetectorModel detective_model_from_serial( const std::string &instrument_id );

  
  /** Use a general serial number rule for Detectives:
      Between S/N 500 and <4000, assume Detective-EX. For S/N between 4000 and
      5000 assume Detective-EX100.
      Above 5000 assume number gives production date and serial number, for
      example: 120233612 would be a Det-EX serial number 612 built on the 233rd
      day of 2012.
   
      Also will look for things like "Micro", "EX100" or similar in the seraial
      number to determine model.
   
      Returns DetectorModel::UnknownSerialNumber if couldnt figure anything out.
   */
  DetectorModel guess_detective_model_from_serial( const std::string &instrument_id );
  
  
  struct DetectorModelInfo
  {
    /** So far, all Detective detector serial numbers I've seen either fit into
        (some only barely) a 32bit int, or are non-ASCII text.  For non ASCII
        text the uin32_t serial will be a hash of the string.
     
     Note: if we made this a size_t, then could use hashes of the serial number
           string for matching, but this would cost us 8 additional bytes for
           each serial number/model pair, and we would have to modify how we
           match Detective files a bit (but doable I think).
     */
    uint32_t serial;
    DetectorModel model;
    
#if( PERFORM_DEVELOPER_CHECKS )
    /* For development purposes, include the original info from the CSV */
    std::string serial_str;
    std::string model_str;
    std::string description;
    std::string file_locations;
#endif
  };//struct DetectorModelInfo
  
  
#if( PERFORM_DEVELOPER_CHECKS )
  void write_csv_file( std::ostream &strm );
  
  std::shared_ptr<std::vector<DetectorModelInfo>> serial_informations();
  
  /** Grabbing the serial numbers from binary Ortec files may result is getting
   a string like "Detective EX S06244431  1354", where it isnt clear which
   run of numbers is the actual serial number (I could probably improve
   grabbing this string from the binary file, but havent had time), so we
   will try each run of numbers.
   
   Note: This function is always implemented for internal use, but only exposed
   externally if PERFORM_DEVELOPER_CHECKS is enabled.
   */
  std::vector<uint32_t> candidate_serial_nums_from_str( const std::string &instrument_id );
#endif
}//namespace SerialToDetectorModel

#endif  //SerialToDetectorModel_h
