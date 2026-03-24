#ifndef SpecUtils_UriLossySpectrum_h
#define SpecUtils_UriLossySpectrum_h
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
#include <vector>
#include <cstdint>

// Forward declarations
namespace SpecUtils
{
  struct UrlSpectrum;
}


namespace SpecUtils
{

/** Result of multi-level wavelet decomposition.

 Coefficients are stored flattened: [cA_n, cD_n, cD_{n-1}, ..., cD_1]
 where cA_n is the approximation at the deepest level, and cD_i are details.
 `level_lengths[0]` = length of cA_n, `level_lengths[1]` = length of cD_n, etc.
 */
struct WaveletDecomp
{
  std::vector<double> coeffs;
  std::vector<size_t> level_lengths;  ///< length of each subband
  size_t original_length;
  int num_levels;
};


/** Maximum decomposition level for given signal and filter lengths.

 @param signal_length  Length of the input signal.
 @param filter_length  Length of the wavelet filter (default 16 for sym8).
 @returns Maximum decomposition level.
 */
int dwt_max_level( size_t signal_length, size_t filter_length = 16 );

/** Single-level forward DWT with symmetric boundary extension (sym8 wavelet).

 @param signal  Input signal.
 @param approx  Output approximation coefficients.
 @param detail  Output detail coefficients.
 */
void dwt1d( const std::vector<double> &signal,
            std::vector<double> &approx,
            std::vector<double> &detail );

/** Single-level inverse DWT (sym8 wavelet).

 @param approx  Approximation coefficients.
 @param detail  Detail coefficients.
 @param signal  Output reconstructed signal.
 @param original_length  Length of the original signal at this decomposition level.
 */
void idwt1d( const std::vector<double> &approx,
             const std::vector<double> &detail,
             std::vector<double> &signal,
             size_t original_length );

/** Multi-level wavelet decomposition (like pywt.wavedec with sym8).

 @param signal  Input signal.
 @param max_level  Maximum decomposition level, or -1 to auto-compute.
 @returns WaveletDecomp containing the flattened coefficients and level lengths.
 */
WaveletDecomp wavedec( const std::vector<double> &signal, int max_level = -1 );

/** Multi-level wavelet reconstruction (like pywt.waverec).

 @param decomp  Wavelet decomposition to reconstruct from.
 @returns Reconstructed signal.
 */
std::vector<double> waverec( const WaveletDecomp &decomp );


/** Compressed wavelet representation of a spectrum.

 Coefficient values are quantized to 12-bit unsigned (0..4095), linearly mapping
 the range [coeff_min, coeff_max].

 12-bit was chosen over 16-bit based on empirical testing:
  - For typical spectra (max counts ~10k), RMSE is nearly identical to 16-bit.
  - Saves ~34% of value storage after deflate, allowing ~55% more coefficients
    to fit in the same QR code, which more than compensates for any precision loss.
  - Quantization error concentrates in peak channels (highest counts), not in the
    low-statistics background regions. For spectra with extreme peak counts (>100k),
    the peak reconstruction may show ~2x larger absolute error vs 16-bit, but this
    is typically within Poisson statistical uncertainty for those high-count channels.
  - The low-count and zero-count regions of the spectrum are reconstructed with
    essentially no degradation vs 16-bit quantization.
 */
struct CompressedSpectrum
{
  uint16_t original_num_channels = 0;
  uint8_t  decomp_level = 0;
  uint8_t  wavelet_id = 0;  ///< 0 = sym8

  std::vector<uint32_t> indices;  ///< sorted indices into flattened coeff array
  std::vector<uint16_t> values;   ///< 12-bit quantized values (0..4095)
  float coeff_min = 0.0f;
  float coeff_max = 0.0f;

  std::vector<uint16_t> level_lengths;
};


/** Compress spectrum channel data using wavelet transform.

 @param channel_data  Input channel counts.
 @param max_coefficients  Number of wavelet coefficients to keep. Must be > 0.
 @returns CompressedSpectrum containing sparse wavelet representation.
 */
CompressedSpectrum compress_spectrum( const std::vector<uint32_t> &channel_data,
                                     int max_coefficients );

/** Decompress a CompressedSpectrum back to channel counts.

 @param comp  The compressed spectrum to decompress.
 @returns Reconstructed channel data as floats (caller can round to integers).
 */
std::vector<float> decompress_spectrum( const CompressedSpectrum &comp );

/** Compute the Anscombe-transformed wavelet decomposition of channel data.

 This is the expensive part of compression. The result can be reused with
 select_and_quantize() to try different coefficient counts without
 recomputing the DWT.

 @param channel_data  Input channel counts.
 @returns WaveletDecomp of the Anscombe-transformed signal.
 */
WaveletDecomp compute_decomposition( const std::vector<uint32_t> &channel_data );

/** Select top-N coefficients from a precomputed decomposition and quantize.

 @param decomp  Precomputed wavelet decomposition from compute_decomposition().
 @param original_num_channels  Number of channels in the original spectrum.
 @param max_coefficients  Number of wavelet coefficients to keep. Must be > 0.
 @returns CompressedSpectrum with the selected and quantized coefficients.
 */
CompressedSpectrum select_and_quantize( const WaveletDecomp &decomp,
                                        size_t original_num_channels,
                                        int max_coefficients );


/** Serialize a CompressedSpectrum to a compact binary representation.

 This binary blob is what goes into the URI S: field.

 @param comp  The compressed spectrum to serialize.
 @returns Binary representation as a byte vector.
 */
std::vector<uint8_t> serialize_compressed( const CompressedSpectrum &comp );

/** Deserialize binary data back to a CompressedSpectrum.

 @param data  Pointer to the binary data.
 @param len   Length of the binary data in bytes.
 @returns Deserialized CompressedSpectrum.
 */
CompressedSpectrum deserialize_compressed( const uint8_t *data, size_t len );

/** Convenience overload for deserialize_compressed.

 @param data  Binary data as a byte vector.
 @returns Deserialized CompressedSpectrum.
 */
CompressedSpectrum deserialize_compressed( const std::vector<uint8_t> &data );


/** Result of lossy wavelet-compressed URI encoding. */
struct LossyEncodeResult
{
  std::vector<std::string> m_urls;   ///< The encoded URI(s)
  double m_rmse = 0.0;              ///< RMSE of the reconstructed spectrum vs original
  int m_num_coefficients = 0;       ///< Number of wavelet coefficients kept
};


/** Encode spectra to lossy wavelet-compressed URIs.

 Supports single-spectrum (one or more URI parts), and multi-spectrum (multiple spectra
 in a single URI).  For single spectrum with num_parts > 1, the spectrum is split across
 multiple URIs. For multi-spectrum, all spectra are concatenated into a single URI.

 The `0x40` (WaveletCompressed) bit is automatically set in the encode options.

 The function performs a binary search over the number of wavelet coefficients to find
 the maximum number that fits within the character budget of the URI(s).

 @param measurements  One or more spectra to encode.
 @param encode_options  EncodeOptions bits controlling base encoding, deflate, etc.
        The WaveletCompressed bit (0x40) will be auto-set.
 @param num_parts  Number of URI parts for a single spectrum.
        Must be 1 when encoding multiple spectra.
 @param max_chars_per_url  Maximum characters per URI.  For QR codes using base-45
        encoding (the default), this is the QR alphanumeric capacity, which depends on
        the QR version and error correction level:

        QR Version |  Low  | Medium | Quartile | High
        ---------- | ----- | ------ | -------- | -----
            10     |   395 |    311 |      221 |   167
            15     |   758 |    600 |      426 |   320
            20     | 1,249 |    970 |      702 |   528
            25     | 1,853 |  1,451 |    1,041 |   769
            30     | 2,520 |  1,994 |    1,429 | 1,016
            35     | 3,351 |  2,632 |    1,867 | 1,347
            40     | 4,296 |  3,391 |    2,420 | 1,729

        For a single QR code at version 40 with low error correction, use 4296.
        The returned URIs are URL-encoded (consistent with url_encode_spectra).
        For QR code generation, URL-decoding the URI first is recommended: the
        base-45 characters are all in the QR alphanumeric set, so the unescaped
        content encodes more efficiently.  The decode_spectrum_urls function
        accepts both URL-encoded and non-URL-encoded URIs.

 @returns LossyEncodeResult containing the URI(s), RMSE, and coefficient count.

 Throws exception on error.
 */
LossyEncodeResult url_encode_spectra_lossy(
  const std::vector<UrlSpectrum> &measurements,
  uint8_t encode_options,
  size_t num_parts,
  size_t max_chars_per_url );

}//namespace SpecUtils

#endif // SpecUtils_UriLossySpectrum_h
