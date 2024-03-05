#ifndef SpecUtils_UriSpectrum_h
#define SpecUtils_UriSpectrum_h
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

#include <chrono>
#include <string>
#include <vector>
#include <cstdint>

// Forward declarations
namespace SpecUtils
{
  class SpecFile;
  class Measurement;
  enum class SourceType : int;
}

/** The \c QRSpectrum namespace provides functions to encode spectra into URLs and QR codes.
 
 An example of using this library could be:
 \code{.cpp}
 SpecUtils::UrlSpectrum fore;
 fore.m_source_type = SpecUtils::SourceType::Foreground;
 fore.m_energy_cal_coeffs = {0.0f, 3.0f};
 fore.m_model = "SomeDetector";
 fore.m_title = "User entered Notes";
 fore.m_start_time = std::chrono::system_clock::now();
 fore.m_latitude = 10.1223;
 fore.m_longitude = -13.232;
 fore.m_neut_sum = 5;
 fore.m_live_time = 295.1f;
 fore.m_real_time = 300.0f;
 fore.m_channel_data = {0,0,0,5,6,22,15,... };
 
 SpecUtils::UrlSpectrum back;
 // No need to set energy cal, model, or lat/lon for background - will assume same as foreground
 back.m_source_type = SpecUtils::SourceType::Background;
 back.m_neut_sum = 1;
 back.m_live_time = 299.1;
 back.m_real_time = 300.0f;
 back.m_channel_data = {0,0,0,3,4,2,1,...};
 
 const size_t num_url_parts = 1;
 const uint8_t options = SpecUtils::EncodeOptions::UseUrlSafeBase64;
 vector<string> encoded = SpecUtils::url_encode_spectra( {fore, back}, options, num_url_parts );
 assert( encoded.size() == num_url_parts );
 cout << "URL: " << encoded[0] << endl;
 \endcode
 
 */

namespace SpecUtils
{
  /** Options for how the data can be encoded as a URL.*/
  enum EncodeOptions
  {
    /** Do not apply zlib based DEFLATE compression. */
    NoDeflate = 0x01,
  
    /** Do not encode data (after optional DEFLATE) as base-45 or as url-safe-base-64.
     E.g., for clickable URLs you may do this, or if you will encode to a binary QR-code, but
     will generally lead to longer URIs or larger QR codes.
     */
    NoBaseXEncoding = 0x02,
  
    /** Keep channel data as text-based numbers.  Will actually be separated by the '$'
     sign, since this is a base-45 character and commas arent - but commas would be valid
     to.
     */
    CsvChannelData = 0x04,
  
    /** Do not zero-compress channel data. */
    NoZeroCompressCounts = 0x08,
  
    /** Use a modified version of base-64 encoding (that doesnt have and invalid URL characters), instead of base-45.
   
     If this option is specified, then #NoBaseXEncoding must not be specified.
   
     This option is useful if you are generating a binary QR code, but want all printable characters; e.g.,
     when you create a QR code with a 'mailto:'.
     */
    UseUrlSafeBase64 = 0x10,
  
    /** If specified, the URI will be one to generate a email (i.e., a 'mailto:...' URI), instead of a 'raddata://' URI.
     This effects both the beginning of the URI starting with 'mailto:', but also the URL data is email encoded
     according to RFC 6068, (i.e., only the "%&;=/?#[]" characters are escaped), instead of total URL
     encoding (i.e., the " $&+,:;=?@'\"<>#%{}|\\^~[]`/" characters escaped).
   
     For example, a returned URI will start with:
     "mailto:user@example.com?subject=spectrum&body=Spectrum%20URI%0D%0Araddata:..."
     and it will not be strictly URL encoded.
   
     Note that this option will not be written/indicated in the URI; however, this value 0x20, was being written
     until 20231202, so we will ignore this bit when it is in URIs, for the time being, and in the future if more
     options are added, consider skipping this bit.
     */
    AsMailToUri = 0x20,
  };//enum EncodeOptions


  /** Struct that represents information that can be included in a spectrum URL. */
  struct UrlSpectrum
  {
    SpecUtils::SourceType m_source_type = SpecUtils::SourceType(4); // There is static_assert in .cpp file to make sure SpecUtils::SourceType::Unknown == 4
    std::vector<float> m_energy_cal_coeffs;
    std::vector<std::pair<float,float>> m_dev_pairs;
    
    std::string m_model;
    std::string m_title;
    
    // Maybe add serial number?
    
    // Use same time definition as SpecUtils, e.g. `SpecUtils::time_point_t`, and default to 0.
    std::chrono::time_point<std::chrono::system_clock,std::chrono::microseconds> m_start_time
                   = std::chrono::time_point<std::chrono::system_clock,std::chrono::microseconds>{};
    
    double m_latitude = -999.9;
    double m_longitude = -999.9;
    int m_neut_sum = -1; ///< Neutron count; will be negative if not present
    
    float m_live_time = -1;
    float m_real_time = -1;
    std::vector<uint32_t> m_channel_data;
  };//struct UrlSpectrum

  std::vector<UrlSpectrum> to_url_spectra(
                                  std::vector<std::shared_ptr<const SpecUtils::Measurement>> specs,
                                  std::string detector_model );

  std::shared_ptr<SpecFile> to_spec_file( const std::vector<UrlSpectrum> &meas );
  
  /** Encodes the specified measurements into one or more URIs.
   
   If a single spectrum is passed in, you may get back multiple urls.
   If multiple spectra are passed in, you will get back a single url, if they can all fit.
   
   Note: URLs will be URL-encoded, while #get_spectrum_url_info, #decode_spectrum_urls, and similar
   all expect non-URL-encoded URLs.
   
   @param measurements One or more measurement to put in the result
   @param encode_options Options, set by #EncodeOptions bits, for how the encoding should be done.
   Default value of 0x00 is binary channel data, DEFLATE compressed, and BASE-45 and URL
   encoded, so as to be sent as an ASCII QR code.
   @param num_parts How many URLs to encode the spectrum to.
   If you are encoding two or more spectra, then this must specify to encode to a single URI.
   If you are encoding a single URL, you should specify between 1 and 8.
   
   Throws exception on error.
   */
  std::vector<std::string> url_encode_spectra( const std::vector<UrlSpectrum> &measurements,
                                              const uint8_t encode_options,
                                              const size_t num_parts );
  
  /** Options to adjust how the URL for a spectrum is created.
   For a single spectrum going into one or more QR codes, you will not specify any of these.
   For creating a QR code with multiple spectra, you will specify to skip encoding, and you
   may be able to skip energy cal, detector model, GPS, and title for the second spectrum.
   */
  enum SkipForEncoding
  {
    Encoding = 0x01,      ///< Skip DEFLATE, base-45/url-safe-base-64, and URL encoding (e.g., for placing multiple spectra in a URL, where you will do this after combination)
    EnergyCal = 0x02,
    DetectorModel = 0x04,
    Gps = 0x08,
    Title = 0x10,
    UrlEncoding = 0x20   ///< Skip just URL encoding (e.g., for when you are making a 'mailto:' uri with an invalid-uri body
  };//enum SkipForEncoding
  
  /** Puts the specified spectrum into `num_parts` URLs.
   Will happily return a very long URL that cant fit into a QR code.
   
   @param meas The measurement to be encoded.
   @param det_model The detector model (ex. "Detective-x") to include.
   @param encode_options How the final URL will be encoded, as specified by #EncodeOptions.
   Note that #skip_encode_options may over-ride these options for when you are combining
   multiple spectra into a single URL.
   @param num_parts How many URLs the result should be broken into.  This is done by breaking the
   channel data into
   @param skip_encode_options Options given by #SkipForEncoding for what encoding or information not
   to do/include.  Useful for including multiple spectra in a single URL.
   
   Returned answer does not include the "RADDATA://G0/", or equivalent prefix.
   
   Throws exception on error.
   */
  std::vector<std::string> url_encode_spectrum( const UrlSpectrum &meas,
                                               const uint8_t encode_options,
                                               const size_t num_parts,
                                               const unsigned int skip_encode_options );


  struct EncodedSpectraInfo
  {
    /** See #EncodeOptions for bit meanings. */
    uint8_t m_encode_options = 0;
    uint8_t m_num_spectra = 0;
    /** starts at 0. */
    uint8_t m_url_num = 0;
    
    uint8_t m_number_urls = 0;
  
    /** CRC-16/ARC value provided in the URI - will only be non-zero for multi-part URIs. */
    uint16_t m_crc = 0;

    /** The original URL, before any manipulation.
   
    Note: will not be URL encoded.
    */
    std::string m_orig_url;
  
    /** The spectrum relevant data that wont be URL encoded, but may be base-45 encoded, as well
    as Deflate compressed.
   
    This is needed as the CRC-16 is computed from all the parts, after these steps, but before
    URL encoding (because URL encoding is not unique, and the OS may have .
    */
    std::string m_raw_data;
  
    /** The spectrum relevant data, after un-base-45/un-base-85 encoded, and un-Deflated, if applicable.
    */
    std::string m_data;
  };//struct EncodedSpectraInfo


  /** Breaks out the various information from the url, and un-base-45 encodes, and un-Deflates,
  if necessary.
 
  Expects the input url to have already been url-decoded.
 
  Throws exception on error.
  */
  EncodedSpectraInfo get_spectrum_url_info( std::string url );


  std::vector<UrlSpectrum> spectrum_decode_first_url( const std::string &url );
  std::vector<uint32_t> spectrum_decode_not_first_url( std::string url );

  /** Decodes the given urls to a single spectrum (one or more input urls), or
  multiple spectrum (a single input url).
 
  Expects each urls to have already been url-decoded.
  */
  std::vector<UrlSpectrum> decode_spectrum_urls( std::vector<std::string> urls );

  
  /** Implements RFC 9285, "The Base45 Data Encoding".
   
   The resulting string will only contain characters that can be encoded into a Alphanumeric QR code.
   */
  std::string base45_encode( const std::vector<uint8_t> &input );
  
  /** Same as other  `base45_encode` function, just takes a string as input. */
  std::string base45_encode( const std::string &input );

  /** Performs the reverse of `base45_encode`. */
  std::vector<uint8_t> base45_decode( const std::string &input );

  /** "base64url" encoding is a URL and filename safe variant of base64 encoding.
   See: https://datatracker.ietf.org/doc/html/rfc4648#section-5
   and: https://en.wikipedia.org/wiki/Base64#Implementations_and_history
   */
  std::string base64url_encode( const std::string &input, const bool use_padding );
  
  /** Performs "base64url" encoding, same as other function by same name. */
  std::string base64url_encode( const std::vector<uint8_t> &input, const bool use_padding );
  
  /** Decodes "base64url" encoded strings. */
  std::vector<uint8_t> base64url_decode( const std::string &input );
  
  /** Performs "percent encoding" of the input
   
   Converts any non-ascii character, or the characters " $&+,:;=?@'\"<>#%{}|\\^~[]`/" to their percent hex encodings.
   */
  std::string url_encode( const std::string &url );
  
  /** Decodes "percent encoded" strings. */
  std::string url_decode( const std::string &url );
  
  /** Similar to `url_encode`, but only encodes non-ascii characters, as well as "%&;=/?#[]", as specified by RFC 6068. */
  std::string email_encode( const std::string &url );
  
  /** Performs DEFLATE (aka, zip) compression */
  void deflate_compress( const void *in_data, size_t in_data_size, std::string &out_data );
  /** Performs DEFLATE (aka, zip) compression */
  void deflate_compress( const void *in_data, size_t in_data_size, std::vector<uint8_t> &out_data );
  
  /** Performs DEFLATE (aka, zip) de-compression */
  void deflate_decompress( void *in_data, size_t in_data_size, std::string &out_data );
  /** Performs DEFLATE (aka, zip) de-compression */
  void deflate_decompress( void *in_data, size_t in_data_size, std::vector<uint8_t> &out_data );

  
  /** Performs the same encoding as `streamvbyte_encode` from https://github.com/lemire/streamvbyte,
   but pre-pended with a uint16_t to give number of integer entries, has a c++ interface, and is way,
   way slower, but is a safer in terms of buffer overflow.
   */
  std::vector<uint8_t> encode_stream_vbyte( const std::vector<uint32_t> &input );

  /** Performs the same encoding as `streamvbyte_decode` from https://github.com/lemire/streamvbyte,
   but assumes data is prepended with uin16_t that gives the number of integer entries, has a c++
   interface, is way, way slower, but is a safer in terms of buffer overflow.
   */
  size_t decode_stream_vbyte( const std::vector<uint8_t> &inbuff, std::vector<uint32_t> &answer );
  
  /** Calculates the "CRC-16/ARC" (Augmented Reversed with Carry) of input string.
   
   The characters in the input string are treated as raw bytes; eg, `reinterpret_cast<const unsigned char&>(input[0])`.
   
   Poly                                       : 0x8005 (but actually bit mirrored value of 0xA001 is used)
   Initialization                           : 0x0000
   Reflect Input bytes                : True
   Reflect Output CRC              : True
   Xor constant to output CRC : 0x0000
   Output for "123456789"       : 47933 (0xBB3D)
   
   This is the equivalent of `boost::crc_16_type`.
  */
  uint16_t calc_CRC16_ARC( const std::string &input );
}//namespace QRSpectrum

#endif  //SpecUtils_UriSpectrum_h
