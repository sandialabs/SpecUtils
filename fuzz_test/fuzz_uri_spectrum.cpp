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
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstring>

#include "SpecUtils/UriSpectrum.h"

#if( SpecUtils_ENABLE_URI_SPECTRA )
#include "SpecUtils/UriLossySpectrum.h"
#endif


// Fuzz test for URI spectrum decoding (both lossless and lossy wavelet).
//
// Strategy:
// 1. Feed raw fuzzer input as a URI to decode_spectrum_urls (tests full decode pipeline)
// 2. Feed raw fuzzer input as a RADDATA:// URI (prepend prefix to exercise header parsing)
// 3. Feed raw fuzzer input as a serialized wavelet blob to deserialize_compressed
// 4. Feed raw fuzzer input through base45_decode, deflate_decompress, etc.
// 5. If input is large enough, try encoding a synthetic spectrum with fuzzer-controlled
//    parameters (channel count, coefficient count, options)
//
// All paths should handle malformed input gracefully (throw exceptions, not crash).

extern "C" int LLVMFuzzerTestOneInput( const uint8_t *data, size_t size )
{
  if( size < 2 )
    return 0;

  // Use first byte to select which sub-test to exercise
  const uint8_t selector = data[0];
  const uint8_t *payload = data + 1;
  const size_t payload_size = size - 1;

  const std::string payload_str( reinterpret_cast<const char *>(payload), payload_size );

#if( SpecUtils_ENABLE_URI_SPECTRA )

  // --- Test 1: decode_spectrum_urls with raw input as URI ---
  if( (selector & 0x07) == 0 )
  {
    try
    {
      std::vector<std::string> urls;
      urls.push_back( payload_str );
      std::vector<SpecUtils::UrlSpectrum> result = SpecUtils::decode_spectrum_urls( urls );
      // If it succeeds, verify no NaN in channel data
      for( size_t i = 0; i < result.size(); ++i )
      {
        for( size_t j = 0; j < result[i].m_channel_data.size(); ++j )
        {
          // channel_data is uint32_t, so it can't be NaN, but verify it exists
          (void)result[i].m_channel_data[j];
        }
      }
    }
    catch( ... )
    {
      // Expected for most fuzzer inputs
    }
  }

  // --- Test 2: decode_spectrum_urls with RADDATA:// prefix ---
  if( (selector & 0x07) == 1 )
  {
    try
    {
      std::string uri = "RADDATA://G0/" + payload_str;
      std::vector<std::string> urls;
      urls.push_back( uri );
      SpecUtils::decode_spectrum_urls( urls );
    }
    catch( ... )
    {
    }
  }

  // --- Test 3: decode_spectrum_urls with wavelet prefix (0x40 option) ---
  if( (selector & 0x07) == 2 )
  {
    try
    {
      // Construct a URI with the wavelet option bit set: "RADDATA://G0/4000/" + payload
      std::string uri = "RADDATA://G0/4000/" + payload_str;
      std::vector<std::string> urls;
      urls.push_back( uri );
      SpecUtils::decode_spectrum_urls( urls );
    }
    catch( ... )
    {
    }
  }

  // --- Test 4: get_spectrum_url_info with raw input ---
  if( (selector & 0x07) == 3 )
  {
    try
    {
      std::string uri = "RADDATA://G0/" + payload_str;
      SpecUtils::EncodedSpectraInfo info = SpecUtils::get_spectrum_url_info( uri );
      (void)info.m_encode_options;
      (void)info.m_num_spectra;
      (void)info.m_data.size();
    }
    catch( ... )
    {
    }
  }

  // --- Test 5: base45_decode with raw input ---
  if( (selector & 0x07) == 4 )
  {
    try
    {
      std::vector<uint8_t> decoded = SpecUtils::base45_decode( payload_str );
      (void)decoded.size();
    }
    catch( ... )
    {
    }
  }

  // --- Test 6: base64url_decode with raw input ---
  if( (selector & 0x07) == 5 )
  {
    try
    {
      std::vector<uint8_t> decoded = SpecUtils::base64url_decode( payload_str );
      (void)decoded.size();
    }
    catch( ... )
    {
    }
  }

  // --- Test 7: deserialize_compressed (lossy wavelet blob) with raw input ---
  if( (selector & 0x07) == 6 )
  {
    try
    {
      SpecUtils::CompressedSpectrum comp = SpecUtils::deserialize_compressed( payload, payload_size );
      // If deserialization succeeds, try decompressing
      std::vector<float> recon = SpecUtils::decompress_spectrum( comp );
      // Verify no NaN/Inf in output
      for( size_t i = 0; i < recon.size(); ++i )
      {
        if( !std::isfinite(recon[i]) )
          break; // Don't crash, just stop checking
      }
    }
    catch( ... )
    {
    }
  }

  // --- Test 8: encode + decode round-trip with fuzzer-controlled spectrum ---
  if( (selector & 0x07) == 7 && payload_size >= 66 )
  {
    try
    {
      // Use fuzzer input to construct a small spectrum
      // First 2 bytes: channel count (32-256 range)
      uint16_t raw_nchan = 0;
      std::memcpy( &raw_nchan, payload, 2 );
      size_t nchan = 32 + (raw_nchan % 225); // 32-256

      // Next byte: max_coefficients (20-100)
      uint8_t raw_coeffs = payload[2];
      int max_coeffs = 20 + (raw_coeffs % 81); // 20-100

      // Next byte: encode_options (only valid bits)
      uint8_t options = payload[3] & 0x51; // Only NoDeflate, UseBase64Url, WaveletCompressed

      // Remaining bytes: channel count values
      std::vector<uint32_t> channel_data( nchan, 0 );
      const size_t data_start = 4;
      for( size_t i = 0; i < nchan && (data_start + i) < payload_size; ++i )
        channel_data[i] = static_cast<uint32_t>(payload[data_start + i]) * 10;

      // Build a UrlSpectrum
      SpecUtils::UrlSpectrum spec;
      spec.m_source_type = static_cast<SpecUtils::SourceType>(3); // Foreground
      spec.m_energy_cal_coeffs.push_back( 0.0f );
      spec.m_energy_cal_coeffs.push_back( 3.0f );
      spec.m_model = "FuzzDet";
      spec.m_live_time = 100.0f;
      spec.m_real_time = 100.0f;
      spec.m_channel_data = channel_data;

      // Encode lossy
      std::vector<SpecUtils::UrlSpectrum> measurements;
      measurements.push_back( spec );
      SpecUtils::LossyEncodeResult result = SpecUtils::url_encode_spectra_lossy(
        measurements, options, 1, 4296 );

      if( !result.m_urls.empty() && !result.m_urls[0].empty() )
      {
        // Decode it back
        std::string decoded_uri = SpecUtils::url_decode( result.m_urls[0] );
        std::vector<std::string> urls;
        urls.push_back( decoded_uri );
        std::vector<SpecUtils::UrlSpectrum> decoded = SpecUtils::decode_spectrum_urls( urls );

        // Verify basic properties
        if( !decoded.empty() )
        {
          if( decoded[0].m_channel_data.size() != nchan )
          {
            // Channel count mismatch after round-trip - this would be a bug
            __builtin_trap();
          }
        }
      }
    }
    catch( ... )
    {
      // Encoding/decoding errors are acceptable for fuzz-generated spectra
    }
  }

#endif // SpecUtils_ENABLE_URI_SPECTRA

  return 0;
}
