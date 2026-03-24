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

#include "SpecUtils/UriLossySpectrum.h"
#include "SpecUtils/UriSpectrum.h"

#include <cmath>
#include <cassert>
#include <cstring>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>


namespace
{
  const char * const sm_hex_digits = "0123456789ABCDEF";
}//anonymous namespace


namespace SpecUtils
{

// ============================================================================
//  Sym8 wavelet filter banks (exact values from PyWavelets)
// ============================================================================

static const double SYM8_DEC_LO[16] = {
  -3.38241595100612557276e-03,
  -5.42132331791148123872e-04,
   3.16950878114929807117e-02,
   7.60748732491760542435e-03,
  -1.43294238350809705063e-01,
  -6.12733590676585240797e-02,
   4.81359651258372212013e-01,
   7.77185751700523508312e-01,
   3.64441894835331403613e-01,
  -5.19458381077090372568e-02,
  -2.72190299170560028041e-02,
   4.91371796736075061585e-02,
   3.80875201389061510474e-03,
  -1.49522583370482308601e-02,
  -3.02920514721366799724e-04,
   1.88995033275946087807e-03
};

static const double SYM8_DEC_HI[16] = {
  -1.88995033275946087807e-03,
  -3.02920514721366799724e-04,
   1.49522583370482308601e-02,
   3.80875201389061510474e-03,
  -4.91371796736075061585e-02,
  -2.72190299170560028041e-02,
   5.19458381077090372568e-02,
   3.64441894835331403613e-01,
  -7.77185751700523508312e-01,
   4.81359651258372212013e-01,
   6.12733590676585240797e-02,
  -1.43294238350809705063e-01,
  -7.60748732491760542435e-03,
   3.16950878114929807117e-02,
   5.42132331791148123872e-04,
  -3.38241595100612557276e-03
};

static const double SYM8_REC_LO[16] = {
   1.88995033275946087807e-03,
  -3.02920514721366799724e-04,
  -1.49522583370482308601e-02,
   3.80875201389061510474e-03,
   4.91371796736075061585e-02,
  -2.72190299170560028041e-02,
  -5.19458381077090372568e-02,
   3.64441894835331403613e-01,
   7.77185751700523508312e-01,
   4.81359651258372212013e-01,
  -6.12733590676585240797e-02,
  -1.43294238350809705063e-01,
   7.60748732491760542435e-03,
   3.16950878114929807117e-02,
  -5.42132331791148123872e-04,
  -3.38241595100612557276e-03
};

static const double SYM8_REC_HI[16] = {
  -3.38241595100612557276e-03,
   5.42132331791148123872e-04,
   3.16950878114929807117e-02,
  -7.60748732491760542435e-03,
  -1.43294238350809705063e-01,
   6.12733590676585240797e-02,
   4.81359651258372212013e-01,
  -7.77185751700523508312e-01,
   3.64441894835331403613e-01,
   5.19458381077090372568e-02,
  -2.72190299170560028041e-02,
  -4.91371796736075061585e-02,
   3.80875201389061510474e-03,
   1.49522583370482308601e-02,
  -3.02920514721366799724e-04,
  -1.88995033275946087807e-03
};


// ============================================================================
//  DWT implementation
// ============================================================================

int dwt_max_level( size_t signal_length, size_t filter_length )
{
  // Same as pywt.dwt_max_level
  if( signal_length < 1 || filter_length < 2 )
    return 0;
  return static_cast<int>( std::floor( std::log2( static_cast<double>(signal_length) / (filter_length - 1) ) ) );
}


// Whole-point symmetric boundary extension (numpy 'symmetric' mode).
// This REPEATS the edge values: [a,b,c] -> [..., b, a, a, b, c, c, b, ...]
// This matches pywt's 'symmetric' mode for DWT.
static std::vector<double> symmetric_extend( const std::vector<double> &signal, size_t pad )
{
  const size_t n = signal.size();
  if( n == 0 )
    throw std::runtime_error( "symmetric_extend: empty signal" );

  std::vector<double> extended( n + 2 * pad );

  // Copy original signal
  for( size_t i = 0; i < n; ++i )
    extended[pad + i] = signal[i];

  // Left padding: whole-point symmetric (repeats edge)
  // signal[0] is repeated, then signal[1], signal[2], ...
  for( size_t i = 0; i < pad; ++i )
  {
    // Index into signal for position (pad - 1 - i)
    // Wrapping for whole-point symmetric: period is 2*n, and we reflect at 0 and n-1
    size_t idx = i; // distance from edge (0 = at edge = repeat of signal[0])
    idx = idx % (2 * n);
    if( idx >= n )
      idx = 2 * n - 1 - idx;
    extended[pad - 1 - i] = signal[idx];
  }

  // Right padding: whole-point symmetric (repeats edge)
  for( size_t i = 0; i < pad; ++i )
  {
    size_t idx = i;
    idx = idx % (2 * n);
    if( idx >= n )
      idx = 2 * n - 1 - idx;
    extended[pad + n + i] = signal[n - 1 - idx];
  }

  return extended;
}


void dwt1d( const std::vector<double> &signal,
            std::vector<double> &approx,
            std::vector<double> &detail )
{
  const size_t n = signal.size();
  if( n < 16 )
    throw std::runtime_error( "dwt1d: signal too short for sym8 filter" );

  const int filt_len = 16;

  // Output length matches pywt: ceil(n/2) + ceil(filt_len/2) - 1
  // For even n and even filt_len: n/2 + filt_len/2 - 1
  // Equivalently: (n + filt_len - 1) / 2  (integer division)
  const size_t out_len = (n + filt_len - 1) / 2;

  // Pad the signal symmetrically so the convolution can access all needed samples.
  // The convolution accesses ext[2*i + filt_len - 1 - k] for i in [0, out_len)
  // and k in [0, filt_len). Max index = 2*(out_len - 1) + filt_len - 1.
  // Required array size = 2*(out_len - 1) + filt_len.
  // With pad_left = filt_len - 2, for odd n the right side needs one extra sample.
  const size_t pad = filt_len - 2;
  const size_t required_size = 2 * (out_len - 1) + filt_len;
  std::vector<double> ext = symmetric_extend( signal, pad );

  // For odd-length signals, the symmetric extension may be one sample short.
  // Append additional reflected samples as needed.
  while( ext.size() < required_size )
  {
    size_t dist = ext.size() - pad - n; // distance past the original signal's end
    size_t idx = dist % (2 * n);
    if( idx >= n )
      idx = 2 * n - 1 - idx;
    ext.push_back( signal[n - 1 - idx] );
  }

  approx.resize( out_len );
  detail.resize( out_len );

  // Convolution formula matching pywt:
  // output[i] = sum_{k=0}^{N-1} filter[k] * ext[2*i + N - 1 - k]
  for( size_t i = 0; i < out_len; ++i )
  {
    double lo = 0.0, hi = 0.0;
    for( int k = 0; k < filt_len; ++k )
    {
      lo += SYM8_DEC_LO[k] * ext[2 * i + filt_len - 1 - k];
      hi += SYM8_DEC_HI[k] * ext[2 * i + filt_len - 1 - k];
    }
    approx[i] = lo;
    detail[i] = hi;
  }
}


void idwt1d( const std::vector<double> &approx,
             const std::vector<double> &detail,
             std::vector<double> &signal,
             size_t original_length )
{
  const int filt_len = 16;
  const size_t coeff_len = approx.size();

  if( detail.size() != coeff_len )
    throw std::runtime_error( "idwt1d: approx and detail must have same length" );

  // Upsample by 2 and convolve with reconstruction filters
  // The upsampled length is 2 * coeff_len
  const size_t up_len = 2 * coeff_len;

  // We need to pad the upsampled signal for convolution
  // Result length after convolution with filter of length filt_len = up_len + filt_len - 1
  // We then extract the correct slice matching original_length

  // Direct computation: for each output sample, sum contributions
  // from upsampled approx and detail convolved with rec filters

  const size_t result_len = up_len + filt_len - 1;
  std::vector<double> result( result_len, 0.0 );

  for( size_t i = 0; i < coeff_len; ++i )
  {
    const size_t pos = 2 * i; // position in upsampled signal (even indices)
    for( int k = 0; k < filt_len; ++k )
    {
      result[pos + k] += approx[i] * SYM8_REC_LO[k] + detail[i] * SYM8_REC_HI[k];
    }
  }

  // Extract the portion corresponding to the original signal.
  // pywt convention: skip (filt_len - 2) samples from the start.
  // Validate that original_length is consistent with the coefficient length
  // to prevent out-of-bounds reads from malformed deserialized data.
  const size_t skip = filt_len - 2;
  if( skip + original_length > result_len )
    throw std::runtime_error( "idwt1d: original_length " + std::to_string(original_length)
      + " is inconsistent with coefficient length " + std::to_string(coeff_len)
      + " (would read past reconstruction buffer)" );

  signal.resize( original_length );
  for( size_t i = 0; i < original_length; ++i )
    signal[i] = result[skip + i];
}


WaveletDecomp wavedec( const std::vector<double> &signal, int max_level )
{
  if( max_level < 0 )
    max_level = dwt_max_level( signal.size() );

  if( max_level < 1 )
    throw std::runtime_error( "wavedec: signal too short for any decomposition" );

  WaveletDecomp result;
  result.original_length = signal.size();
  result.num_levels = max_level;

  // Decompose iteratively
  std::vector<std::vector<double>> details;
  std::vector<double> current = signal;
  std::vector<size_t> signal_lengths; // track signal length at each level for reconstruction

  for( int level = 0; level < max_level; ++level )
  {
    signal_lengths.push_back( current.size() );

    std::vector<double> approx, detail;
    dwt1d( current, approx, detail );
    details.push_back( std::move(detail) );
    current = std::move( approx );
  }

  // Build flattened coefficient array: [cA_n, cD_n, cD_{n-1}, ..., cD_1]
  result.level_lengths.push_back( current.size() ); // cA_n
  result.coeffs = current;

  for( int i = max_level - 1; i >= 0; --i )
  {
    result.level_lengths.push_back( details[i].size() );
    result.coeffs.insert( result.coeffs.end(), details[i].begin(), details[i].end() );
  }

  return result;
}


std::vector<double> waverec( const WaveletDecomp &decomp )
{
  if( decomp.num_levels < 1 )
    throw std::runtime_error( "waverec: no decomposition levels" );

  const int n_levels = decomp.num_levels;

  if( decomp.level_lengths.size() < static_cast<size_t>(n_levels + 1) )
    throw std::runtime_error( "waverec: level_lengths has "
      + std::to_string(decomp.level_lengths.size()) + " entries but need at least "
      + std::to_string(n_levels + 1) + " for " + std::to_string(n_levels) + " decomposition levels" );

  // Extract subbands from flattened array
  // level_lengths[0] = cA_n, level_lengths[1] = cD_n, ..., level_lengths[n_levels] = cD_1
  std::vector<std::vector<double>> subbands;
  size_t offset = 0;
  for( size_t i = 0; i < decomp.level_lengths.size(); ++i )
  {
    size_t len = decomp.level_lengths[i];
    subbands.push_back( std::vector<double>( decomp.coeffs.begin() + offset,
                                              decomp.coeffs.begin() + offset + len ) );
    offset += len;
  }

  // subbands[0] = cA_n, subbands[1] = cD_n, subbands[2] = cD_{n-1}, ..., subbands[n_levels] = cD_1

  // Reconstruct from deepest level
  std::vector<double> current = subbands[0]; // cA_n

  // We need the original signal length at each decomposition step.
  // Compute forward: lengths[0] = original, then for each level:
  //   lengths[i+1] = (lengths[i] + 15) / 2  (the output length of dwt1d)
  std::vector<size_t> input_lengths( n_levels );
  {
    size_t len = decomp.original_length;
    for( int i = 0; i < n_levels; ++i )
    {
      input_lengths[i] = len;
      len = (len + 15) / 2; // output of dwt1d
    }
  }

  // Reconstruct: detail subbands[1]=cD_n, subbands[2]=cD_{n-1}, ...
  // Reconstruction goes from level n_levels-1 down to 0
  // At step j (j=0 is deepest), we reconstruct from current + subbands[j+1]
  // and the target length is input_lengths[n_levels - 1 - j]
  for( int j = 0; j < n_levels; ++j )
  {
    const std::vector<double> &detail = subbands[j + 1]; // cD_{n-j}
    size_t target_len = input_lengths[n_levels - 1 - j];

    std::vector<double> reconstructed;
    idwt1d( current, detail, reconstructed, target_len );
    current = std::move( reconstructed );
  }

  // Trim to original length (should already be correct, but just in case)
  current.resize( decomp.original_length );

  return current;
}


// ============================================================================
//  Anscombe transform for Poisson variance stabilization
// ============================================================================

// Anscombe transform: stabilizes Poisson variance
static double anscombe( double x )
{
  return 2.0 * std::sqrt( x + 0.375 );
}

// Inverse Anscombe transform with asymptotic bias correction.
// For large y (corresponding to counts >> 1), uses the Makitalo & Foi (2011)
// closed-form approximation which removes the negative bias of the naive
// algebraic inverse (y/2)^2 - 3/8.
// For small y (< 1.0, corresponding to counts near zero), the correction terms
// have 1/y singularities, so we fall back to the naive formula which is
// well-behaved and adequate (the bias correction is negligible for near-zero counts).
static double inverse_anscombe( double y )
{
  if( y <= 0.0 )
    return 0.0;

  // The unbiased correction terms diverge for small y; use naive inverse there.
  // Threshold of 1.0 corresponds to original counts ~0, where bias is irrelevant.
  if( y < 1.0 )
  {
    double val = (y / 2.0) * (y / 2.0) - 0.375;
    return (val < 0.0) ? 0.0 : val;
  }

  const double y2 = y * y;
  const double y3 = y2 * y;
  const double sqrt_3_2 = std::sqrt( 1.5 ); // sqrt(3/2)

  double val = 0.25 * y2
             + 0.25 * sqrt_3_2 / y
             - 11.0 / (8.0 * y2)
             + 5.0 * sqrt_3_2 / (8.0 * y3)
             - 1.0 / 8.0;

  return (val < 0.0) ? 0.0 : val;
}


// ============================================================================
//  Compression / decompression
// ============================================================================

CompressedSpectrum compress_spectrum( const std::vector<uint32_t> &channel_data,
                                     int max_coefficients )
{
  const size_t n = channel_data.size();
  if( n < 32 )
    throw std::runtime_error( "compress_spectrum: need at least 32 channels" );
  if( n > 65535 )
    throw std::runtime_error( "compress_spectrum: too many channels (max 65535)" );
  if( max_coefficients < 1 )
    throw std::runtime_error( "compress_spectrum: max_coefficients must be > 0" );

  // Step 1: Anscombe transform
  std::vector<double> transformed( n );
  for( size_t i = 0; i < n; ++i )
    transformed[i] = anscombe( static_cast<double>(channel_data[i]) );

  // Step 2: Wavelet decomposition
  WaveletDecomp decomp = wavedec( transformed );

  // Step 3+4: Select and quantize coefficients
  return select_and_quantize( decomp, n, max_coefficients );
}


CompressedSpectrum select_and_quantize( const WaveletDecomp &decomp,
                                        size_t original_num_channels,
                                        int max_coefficients )
{
  if( max_coefficients < 1 )
    throw std::runtime_error( "select_and_quantize: max_coefficients must be > 0" );

  const size_t total_coeffs = decomp.coeffs.size();
  size_t num_keep = static_cast<size_t>( max_coefficients );

  if( num_keep > total_coeffs )
    num_keep = total_coeffs;

  // Find the top num_keep coefficients by absolute value
  std::vector<size_t> sorted_indices( total_coeffs );
  std::iota( sorted_indices.begin(), sorted_indices.end(), 0 );

  std::partial_sort( sorted_indices.begin(), sorted_indices.begin() + num_keep,
                     sorted_indices.end(),
                     [&decomp]( size_t a, size_t b ) {
                       return std::fabs(decomp.coeffs[a]) > std::fabs(decomp.coeffs[b]);
                     } );

  // Keep the top num_keep indices, sorted by index for delta coding
  std::vector<size_t> keep_indices( sorted_indices.begin(), sorted_indices.begin() + num_keep );
  std::sort( keep_indices.begin(), keep_indices.end() );

  // Quantize to 12-bit unsigned: map [c_min, c_max] -> [0, 4095]
  double c_min = decomp.coeffs[keep_indices[0]];
  double c_max = c_min;
  for( size_t j = 0; j < keep_indices.size(); ++j )
  {
    double v = decomp.coeffs[keep_indices[j]];
    if( v < c_min ) c_min = v;
    if( v > c_max ) c_max = v;
  }

  CompressedSpectrum result;
  result.original_num_channels = static_cast<uint16_t>( original_num_channels );
  result.decomp_level = static_cast<uint8_t>( decomp.num_levels );
  result.wavelet_id = 0; // sym8
  result.coeff_min = static_cast<float>( c_min );
  result.coeff_max = static_cast<float>( c_max );

  result.level_lengths.resize( decomp.level_lengths.size() );
  for( size_t i = 0; i < decomp.level_lengths.size(); ++i )
    result.level_lengths[i] = static_cast<uint16_t>( decomp.level_lengths[i] );

  result.indices.resize( num_keep );
  result.values.resize( num_keep );

  const double range = c_max - c_min;
  const int max_quant = 4095; // 2^12 - 1
  const double scale = (range > 1e-30) ? max_quant / range : 0.0;

  for( size_t i = 0; i < num_keep; ++i )
  {
    result.indices[i] = static_cast<uint32_t>( keep_indices[i] );
    double v = decomp.coeffs[keep_indices[i]];
    int32_t q = static_cast<int32_t>( std::round( (v - c_min) * scale ) );
    if( q < 0 ) q = 0;
    if( q > max_quant ) q = max_quant;
    result.values[i] = static_cast<uint16_t>( q );
  }

  return result;
}


std::vector<float> decompress_spectrum( const CompressedSpectrum &comp )
{
  // Reconstruct wavelet decomposition
  WaveletDecomp decomp;
  decomp.original_length = comp.original_num_channels;
  decomp.num_levels = comp.decomp_level;
  decomp.level_lengths.resize( comp.level_lengths.size() );
  for( size_t i = 0; i < comp.level_lengths.size(); ++i )
    decomp.level_lengths[i] = comp.level_lengths[i];

  // Total coefficient array size
  size_t total = 0;
  for( size_t i = 0; i < decomp.level_lengths.size(); ++i )
    total += decomp.level_lengths[i];

  decomp.coeffs.assign( total, 0.0 );

  // Dequantize 12-bit values: map [0, 4095] -> [coeff_min, coeff_max]
  const double c_min = comp.coeff_min;
  const double c_max = comp.coeff_max;
  const double range = c_max - c_min;
  const int max_quant = 4095;

  for( size_t i = 0; i < comp.indices.size(); ++i )
  {
    if( comp.indices[i] >= total )
      throw std::runtime_error( "decompress_spectrum: coefficient index "
        + std::to_string(comp.indices[i]) + " out of bounds (total coefficients: "
        + std::to_string(total) + ")" );

    double q = static_cast<double>( comp.values[i] );
    double v = (range > 1e-30) ? q / max_quant * range + c_min : c_min;
    decomp.coeffs[comp.indices[i]] = v;
  }

  // Inverse wavelet transform
  std::vector<double> reconstructed = waverec( decomp );

  // Inverse Anscombe transform
  std::vector<float> result( comp.original_num_channels );
  for( size_t i = 0; i < comp.original_num_channels; ++i )
  {
    double val = inverse_anscombe( reconstructed[i] );
    result[i] = static_cast<float>( val );
  }

  return result;
}


WaveletDecomp compute_decomposition( const std::vector<uint32_t> &channel_data )
{
  const size_t n = channel_data.size();
  if( n < 32 )
    throw std::runtime_error( "compute_decomposition: need at least 32 channels" );
  if( n > 65535 )
    throw std::runtime_error( "compute_decomposition: too many channels (max 65535)" );

  std::vector<double> transformed( n );
  for( size_t i = 0; i < n; ++i )
    transformed[i] = anscombe( static_cast<double>(channel_data[i]) );

  return wavedec( transformed );
}


// ============================================================================
//  Binary serialization
// ============================================================================

// Binary serialization format (version 2 -- 12-bit quantization):
//
// [1B magic: 0xA0 | version] [1B wavelet_id:4|decomp_level:4] [2B original_channels LE]
// [1B num_level_lengths] [2B * num_level_lengths, each LE]
// [4B coeff_min float LE] [4B coeff_max float LE]
// [2B num_coeffs LE]
// [2B vbyte_len LE] [vbyte_len bytes: delta-coded indices via streamvbyte]
// [12-bit packed values: 3 bytes per 2 values, last value padded if odd count]
//
// Magic byte: high nibble 0xA0 identifies the wavelet compression family.
// Low nibble is the format version (currently 2 = 12-bit quantization).
// This allows up to 16 format versions in a single byte.
//
// 12-bit value packing: two consecutive 12-bit values A, B are stored as:
//   byte0 = A[7:0]          (low 8 bits of A)
//   byte1 = B[3:0]:A[11:8]  (low 4 of B in high nibble, high 4 of A in low nibble)
//   byte2 = B[11:4]         (high 8 bits of B)

static const uint8_t FORMAT_MAGIC_MASK = 0xF0;
static const uint8_t FORMAT_MAGIC_VALUE = 0xA0;
static const uint8_t FORMAT_VERSION_12BIT = 2;

// Write a float in little-endian byte order (portable across endianness)
static void push_f32_le( std::vector<uint8_t> &buf, float v )
{
  uint32_t bits;
  std::memcpy( &bits, &v, 4 );
  buf.push_back( static_cast<uint8_t>( bits & 0xFF ) );
  buf.push_back( static_cast<uint8_t>( (bits >> 8) & 0xFF ) );
  buf.push_back( static_cast<uint8_t>( (bits >> 16) & 0xFF ) );
  buf.push_back( static_cast<uint8_t>( (bits >> 24) & 0xFF ) );
}

// Read a float in little-endian byte order (portable across endianness)
static float read_f32_le_from( const uint8_t *data )
{
  uint32_t bits = static_cast<uint32_t>(data[0])
                | (static_cast<uint32_t>(data[1]) << 8)
                | (static_cast<uint32_t>(data[2]) << 16)
                | (static_cast<uint32_t>(data[3]) << 24);
  float v;
  std::memcpy( &v, &bits, 4 );
  return v;
}


std::vector<uint8_t> serialize_compressed( const CompressedSpectrum &comp )
{
  const size_t num_coeffs = comp.indices.size();
  const size_t num_levels = comp.level_lengths.size();

  std::vector<uint8_t> buf;
  buf.reserve( 32 + 2*num_levels + 3*num_coeffs );

  // push_u8 and push_u16_le as local helpers
  buf.push_back( FORMAT_MAGIC_VALUE | FORMAT_VERSION_12BIT );
  buf.push_back( static_cast<uint8_t>( (comp.wavelet_id << 4) | (comp.decomp_level & 0x0F) ) );

  // original_num_channels LE
  buf.push_back( static_cast<uint8_t>( comp.original_num_channels & 0xFF ) );
  buf.push_back( static_cast<uint8_t>( (comp.original_num_channels >> 8) & 0xFF ) );

  buf.push_back( static_cast<uint8_t>( num_levels ) );
  for( size_t i = 0; i < num_levels; ++i )
  {
    buf.push_back( static_cast<uint8_t>( comp.level_lengths[i] & 0xFF ) );
    buf.push_back( static_cast<uint8_t>( (comp.level_lengths[i] >> 8) & 0xFF ) );
  }

  push_f32_le( buf, comp.coeff_min );
  push_f32_le( buf, comp.coeff_max );

  if( num_coeffs > 65535 )
    throw std::runtime_error( "serialize_compressed: too many coefficients ("
      + std::to_string(num_coeffs) + ", max 65535)" );

  // num_coeffs LE
  const uint16_t nc16 = static_cast<uint16_t>( num_coeffs );
  buf.push_back( static_cast<uint8_t>( nc16 & 0xFF ) );
  buf.push_back( static_cast<uint8_t>( (nc16 >> 8) & 0xFF ) );

  // Delta-code the sorted indices and encode with streamvbyte
  std::vector<uint32_t> deltas( num_coeffs );
  for( size_t i = 0; i < num_coeffs; ++i )
    deltas[i] = (i == 0) ? comp.indices[i] : (comp.indices[i] - comp.indices[i-1]);

  std::vector<uint8_t> vbyte = SpecUtils::encode_stream_vbyte( deltas );
  if( vbyte.size() > 65535 )
    throw std::runtime_error( "serialize_compressed: streamvbyte encoding too large ("
      + std::to_string(vbyte.size()) + " bytes, max 65535)" );

  const uint16_t vb16 = static_cast<uint16_t>( vbyte.size() );
  buf.push_back( static_cast<uint8_t>( vb16 & 0xFF ) );
  buf.push_back( static_cast<uint8_t>( (vb16 >> 8) & 0xFF ) );
  buf.insert( buf.end(), vbyte.begin(), vbyte.end() );

  // Pack 12-bit values: 3 bytes per 2 values
  for( size_t i = 0; i < num_coeffs; i += 2 )
  {
    uint16_t a = comp.values[i] & 0x0FFF;
    uint16_t b = (i + 1 < num_coeffs) ? (comp.values[i+1] & 0x0FFF) : static_cast<uint16_t>(0);
    buf.push_back( static_cast<uint8_t>( a & 0xFF ) );
    buf.push_back( static_cast<uint8_t>( ((b & 0x0F) << 4) | ((a >> 8) & 0x0F) ) );
    buf.push_back( static_cast<uint8_t>( (b >> 4) & 0xFF ) );
  }

  return buf;
}


CompressedSpectrum deserialize_compressed( const uint8_t *data, size_t len )
{
  if( len < 12 )
    throw std::runtime_error( "deserialize_compressed: data too short" );

  size_t pos = 0;

  CompressedSpectrum result;

  // read_u8
  if( pos >= len ) throw std::runtime_error("deserialize: unexpected end");
  uint8_t magic = data[pos++];
  if( (magic & FORMAT_MAGIC_MASK) != FORMAT_MAGIC_VALUE )
    throw std::runtime_error( "deserialize_compressed: bad magic byte" );

  uint8_t version = magic & 0x0F;
  if( version != FORMAT_VERSION_12BIT )
    throw std::runtime_error( "deserialize_compressed: unsupported format version "
                              + std::to_string(version) + " (expected "
                              + std::to_string(FORMAT_VERSION_12BIT) + ")" );

  // read wavelet_id and decomp_level
  if( pos >= len ) throw std::runtime_error("deserialize: unexpected end");
  uint8_t wl_level = data[pos++];
  result.wavelet_id = (wl_level >> 4) & 0x0F;
  result.decomp_level = wl_level & 0x0F;

  // read original_num_channels LE
  if( pos + 2 > len ) throw std::runtime_error("deserialize: unexpected end");
  result.original_num_channels = static_cast<uint16_t>( data[pos] | (static_cast<uint16_t>(data[pos+1]) << 8) );
  pos += 2;

  // read num_levels
  if( pos >= len ) throw std::runtime_error("deserialize: unexpected end");
  uint8_t num_levels = data[pos++];
  if( num_levels < (result.decomp_level + 1) )
    throw std::runtime_error( "deserialize_compressed: num_levels ("
      + std::to_string(num_levels) + ") inconsistent with decomp_level ("
      + std::to_string(result.decomp_level) + "); need at least decomp_level + 1 entries" );

  result.level_lengths.resize( num_levels );
  for( uint8_t i = 0; i < num_levels; ++i )
  {
    if( pos + 2 > len ) throw std::runtime_error("deserialize: unexpected end");
    result.level_lengths[i] = static_cast<uint16_t>( data[pos] | (static_cast<uint16_t>(data[pos+1]) << 8) );
    pos += 2;
  }

  // read coeff_min, coeff_max
  if( pos + 4 > len ) throw std::runtime_error("deserialize: unexpected end");
  result.coeff_min = read_f32_le_from( data + pos );
  pos += 4;
  if( pos + 4 > len ) throw std::runtime_error("deserialize: unexpected end");
  result.coeff_max = read_f32_le_from( data + pos );
  pos += 4;

  // read num_coeffs
  if( pos + 2 > len ) throw std::runtime_error("deserialize: unexpected end");
  uint16_t num_coeffs = static_cast<uint16_t>( data[pos] | (static_cast<uint16_t>(data[pos+1]) << 8) );
  pos += 2;

  // Read streamvbyte-encoded delta indices
  if( pos + 2 > len ) throw std::runtime_error("deserialize: unexpected end");
  uint16_t vbyte_len = static_cast<uint16_t>( data[pos] | (static_cast<uint16_t>(data[pos+1]) << 8) );
  pos += 2;

  if( pos + vbyte_len > len )
    throw std::runtime_error( "deserialize: streamvbyte data extends past end" );

  std::vector<uint8_t> vbyte_data( data + pos, data + pos + vbyte_len );
  pos += vbyte_len;

  std::vector<uint32_t> deltas;
  SpecUtils::decode_stream_vbyte( vbyte_data, deltas );

  if( deltas.size() < num_coeffs )
    throw std::runtime_error( "deserialize: streamvbyte decoded "
                              + std::to_string(deltas.size()) + " deltas, expected "
                              + std::to_string(num_coeffs) );

  // Reconstruct indices from deltas (prefix sum)
  result.indices.resize( num_coeffs );
  uint32_t running = 0;
  for( uint16_t i = 0; i < num_coeffs; ++i )
  {
    running += deltas[i];
    result.indices[i] = running;
  }

  // Unpack 12-bit values: 3 bytes per 2 values
  result.values.resize( num_coeffs );
  for( uint16_t i = 0; i < num_coeffs; i += 2 )
  {
    if( pos + 3 > len )
      throw std::runtime_error( "deserialize: 12-bit value data extends past end" );

    uint8_t b0 = data[pos++];
    uint8_t b1 = data[pos++];
    uint8_t b2 = data[pos++];

    result.values[i] = static_cast<uint16_t>( b0 | ((b1 & 0x0F) << 8) );
    if( i + 1 < num_coeffs )
      result.values[i+1] = static_cast<uint16_t>( ((b1 >> 4) & 0x0F) | (b2 << 4) );
  }

  return result;
}


CompressedSpectrum deserialize_compressed( const std::vector<uint8_t> &data )
{
  return deserialize_compressed( data.data(), data.size() );
}


// ============================================================================
//  Lossy URI encoding
// ============================================================================

// The WaveletCompressed option flag for the URI encoding options byte.
static const uint8_t WAVELET_COMPRESSED_FLAG = 0x40;


LossyEncodeResult url_encode_spectra_lossy(
  const std::vector<UrlSpectrum> &measurements,
  uint8_t encode_options,
  size_t num_parts,
  size_t max_chars_per_url )
{
  const size_t num_spectra = measurements.size();

  if( num_spectra == 0 )
    throw std::runtime_error( "url_encode_spectra_lossy: no measurements passed in." );
  if( num_spectra > 9 )
    throw std::runtime_error( "url_encode_spectra_lossy: too many measurements (max 9)." );
  if( num_parts == 0 || num_parts > 9 )
    throw std::runtime_error( "url_encode_spectra_lossy: invalid num_parts (must be 1..9)." );
  if( num_spectra > 1 && num_parts != 1 )
    throw std::runtime_error( "url_encode_spectra_lossy: multi-spectrum requires num_parts == 1." );
  if( max_chars_per_url < 50 )
    throw std::runtime_error( "url_encode_spectra_lossy: max_chars_per_url too small." );

  // Auto-set the wavelet compressed flag
  encode_options |= WAVELET_COMPRESSED_FLAG;

  const bool use_deflate = !(encode_options & EncodeOptions::NoDeflate);
  const bool use_url_safe_base64 = (encode_options & EncodeOptions::UseUrlSafeBase64) != 0;
  // We always use base-45 for QR alphanumeric unless url-safe-base64 is requested
  // NoBaseXEncoding is not compatible with this function (we need base encoding to fit in QR).

  // Build the URL start prefix using the same pattern as url_encode_spectra
  // Format: RADDATA://G0/[options_hex][num_parts-1][num_spectra-1_or_url_num]/[crc]/[data]
  const uint8_t used_options = encode_options;
  const char more_sig_char = sm_hex_digits[(used_options >> 4) & 0x0F];
  const char less_sig_char = sm_hex_digits[used_options & 0x0F];

  // Lambda to make the URL start for a given url_num
  auto make_url_start = [&]( size_t url_num, size_t total_parts ) -> std::string {
    std::string url_start = "RADDATA://G0/";
    if( more_sig_char != '0' )
      url_start += more_sig_char;
    url_start += less_sig_char;
    url_start += std::to_string( total_parts - 1 );
    url_start += std::to_string( (num_spectra > 1) ? (num_spectra - 1) : url_num );
    url_start += "/";
    return url_start;
  };


  if( num_spectra == 1 )
  {
    // ---- Single spectrum encoding ----
    // Use url_encode_spectrum with SkipForEncoding::Encoding to get the metadata text,
    // then replace channel data after S: with the wavelet blob.

    const UrlSpectrum &meas = measurements[0];

    if( meas.m_channel_data.empty() )
      throw std::runtime_error( "url_encode_spectra_lossy: no channel data." );

    // Get metadata text using the existing url_encode_spectrum with skip encoding
    // This gives us the metadata fields up to and including "S:" followed by channel data.
    // We only want the metadata part up to "S:".
    std::vector<std::string> meta_parts = url_encode_spectrum( meas, encode_options, 1,
                                                                SkipForEncoding::Encoding );
    assert( meta_parts.size() == 1 );

    // Find "S:" in the metadata text - everything before it is metadata, after is channel data
    std::string meta_text = meta_parts[0];
    // Search for " S:" (with leading space) to avoid matching binary data that
    // may contain the byte sequence 0x53 0x3A before the real S: marker.
    size_t s_pos = meta_text.find( " S:" );
    std::string meta_prefix;
    if( s_pos != std::string::npos )
      meta_prefix = meta_text.substr( 0, s_pos + 3 ); // includes " S:"
    else
      meta_prefix = meta_text + " S:"; // should not happen, but handle gracefully

    // Precompute the DWT once -- this is the expensive step.
    const size_t n_channels = meas.m_channel_data.size();
    WaveletDecomp decomp = compute_decomposition( meas.m_channel_data );

    // The total character budget for data (after URL prefix and CRC)
    const size_t prefix_len = make_url_start( 0, num_parts ).size();

    // Lambda to build the complete payload for a given number of coefficients.
    // Returns the encoded data string (after deflate + base45) and the CompressedSpectrum.
    auto try_encode = [&]( int num_coeffs ) -> std::pair<std::string, CompressedSpectrum>
    {
      CompressedSpectrum comp = select_and_quantize( decomp, n_channels, num_coeffs );
      std::vector<uint8_t> blob = serialize_compressed( comp );

      // Combine metadata prefix + blob
      std::vector<uint8_t> full_data( meta_prefix.size() + blob.size() );
      std::memcpy( full_data.data(), meta_prefix.data(), meta_prefix.size() );
      std::memcpy( full_data.data() + meta_prefix.size(), blob.data(), blob.size() );

      std::string data_str( full_data.begin(), full_data.end() );

      // Deflate compress if requested
      if( use_deflate )
        deflate_compress( &(data_str[0]), data_str.size(), data_str );

      // Base encoding (NOT URL-encoded -- callers handle URL-encoding)
      std::string encoded;
      if( use_url_safe_base64 )
        encoded = base64url_encode( data_str, false );
      else
        encoded = base45_encode( data_str );

      return std::pair<std::string, CompressedSpectrum>( encoded, comp );
    };

    if( num_parts == 1 )
    {
      // ---- Single URL: binary search for max coefficients that fit ----
      // Budget accounts for URL-encoding expansion of the base45 data
      // by comparing url_encode(encoded).size() against the available space.
      const size_t max_data_chars = max_chars_per_url - prefix_len;

      int lo = 20;
      int hi = static_cast<int>( std::min( n_channels * 15 / static_cast<size_t>(100),
                                            static_cast<size_t>(5000) ) );
      if( hi < lo ) hi = lo;

      std::string best_encoded;
      CompressedSpectrum best_comp;
      int best_num = lo;

      // First check if minimum fits
      {
        std::pair<std::string, CompressedSpectrum> result = try_encode( lo );
        if( url_encode( result.first ).size() > max_data_chars )
          throw std::runtime_error( "url_encode_spectra_lossy: cannot fit even minimum coefficients into URL" );
        best_encoded = result.first;
        best_comp = result.second;
        best_num = lo;
      }

      // Binary search (compare URL-encoded size against budget)
      while( hi - lo > 1 )
      {
        int mid = (lo + hi) / 2;
        std::pair<std::string, CompressedSpectrum> result = try_encode( mid );
        if( url_encode( result.first ).size() <= max_data_chars )
        {
          lo = mid;
          best_encoded = result.first;
          best_comp = result.second;
          best_num = mid;
        }
        else
        {
          hi = mid;
        }
      }

      // Try hi as well
      if( hi != lo )
      {
        std::pair<std::string, CompressedSpectrum> result = try_encode( hi );
        if( url_encode( result.first ).size() <= max_data_chars )
        {
          best_encoded = result.first;
          best_comp = result.second;
          best_num = hi;
        }
      }

      // Compute RMSE
      double rmse = 0.0;
      {
        std::vector<float> reconstructed = decompress_spectrum( best_comp );
        double sum_sq = 0.0;
        const size_t count = std::min( n_channels, reconstructed.size() );
        for( size_t i = 0; i < count; ++i )
        {
          double diff = static_cast<double>(meas.m_channel_data[i])
                      - static_cast<double>(reconstructed[i]);
          sum_sq += diff * diff;
        }
        rmse = (n_channels > 0) ? std::sqrt( sum_sq / n_channels ) : 0.0;
      }

      // Build the final URL, URL-encoding the data for URI transmission
      std::string url = make_url_start( 0, 1 ) + url_encode( best_encoded );

      LossyEncodeResult lr;
      lr.m_urls.push_back( url );
      lr.m_rmse = rmse;
      lr.m_num_coefficients = best_num;
      return lr;
    }
    else
    {
      // ---- Multi-part: single spectrum split across multiple URLs ----
      // Split the serialized wavelet blob across parts, consistent with how
      // the lossless path splits channel data. Each part independently gets
      // deflated + base45 + URL-encoded, so they can be decoded independently.

      // Binary search: target total URL-encoded payload across all parts.
      // We build all parts per trial to get accurate total size.
      auto try_encode_multipart = [&]( int num_coeffs ) ->
        std::pair<std::vector<std::string>, CompressedSpectrum>
      {
        CompressedSpectrum comp = select_and_quantize( decomp, n_channels, num_coeffs );
        std::vector<uint8_t> blob = serialize_compressed( comp );

        // Split blob bytes across parts, with metadata in part 0
        const size_t bytes_per_part = blob.size() / num_parts;

        std::vector<std::string> part_payloads;
        for( size_t part = 0; part < num_parts; ++part )
        {
          size_t start = bytes_per_part * part;
          size_t end_pos = (part + 1 == num_parts) ? blob.size() : (start + bytes_per_part);

          std::string data_str;
          if( part == 0 )
          {
            // Part 0: metadata prefix + first blob chunk
            data_str = meta_prefix;
            data_str.append( reinterpret_cast<const char *>(blob.data() + start), end_pos - start );
          }
          else
          {
            // Parts 1+: just the blob chunk
            data_str.assign( reinterpret_cast<const char *>(blob.data() + start), end_pos - start );
          }

          // Independently deflate + base45 each part
          if( use_deflate )
            deflate_compress( &(data_str[0]), data_str.size(), data_str );

          std::string encoded;
          if( use_url_safe_base64 )
            encoded = base64url_encode( data_str, false );
          else
            encoded = base45_encode( data_str );

          part_payloads.push_back( encoded );
        }

        return std::pair<std::vector<std::string>, CompressedSpectrum>( part_payloads, comp );
      };

      // Compute total URL-encoded size of all parts
      auto total_url_size = [&]( const std::vector<std::string> &payloads, const std::string &crc ) -> size_t
      {
        size_t total = 0;
        for( size_t i = 0; i < payloads.size(); ++i )
          total += make_url_start( i, num_parts ).size() + crc.size() + url_encode( payloads[i] ).size();
        return total;
      };

      int lo = 20;
      int hi = static_cast<int>( std::min( n_channels * 30 / static_cast<size_t>(100),
                                            static_cast<size_t>(10000) ) );
      if( hi < lo ) hi = lo;

      std::vector<std::string> best_payloads;
      CompressedSpectrum best_comp;
      int best_num = lo;

      // Placeholder CRC for size estimation (actual CRC computed from final data)
      std::string est_crc = "65535/";

      // First check if minimum fits
      {
        std::pair<std::vector<std::string>, CompressedSpectrum> result = try_encode_multipart( lo );
        size_t total = total_url_size( result.first, est_crc );
        if( total > num_parts * max_chars_per_url )
          throw std::runtime_error( "url_encode_spectra_lossy: cannot fit even minimum coefficients" );
        best_payloads = result.first;
        best_comp = result.second;
        best_num = lo;
      }

      // Binary search
      while( hi - lo > 1 )
      {
        int mid = (lo + hi) / 2;
        std::pair<std::vector<std::string>, CompressedSpectrum> result = try_encode_multipart( mid );
        size_t total = total_url_size( result.first, est_crc );
        // Check that no single part exceeds the per-URL limit
        bool fits = (total <= num_parts * max_chars_per_url);
        if( fits )
        {
          for( size_t i = 0; i < result.first.size() && fits; ++i )
          {
            size_t part_size = make_url_start(i, num_parts).size() + est_crc.size()
                             + url_encode( result.first[i] ).size();
            if( part_size > max_chars_per_url )
              fits = false;
          }
        }
        if( fits )
        {
          lo = mid;
          best_payloads = result.first;
          best_comp = result.second;
          best_num = mid;
        }
        else
        {
          hi = mid;
        }
      }

      // Try hi as well
      if( hi != lo )
      {
        std::pair<std::vector<std::string>, CompressedSpectrum> result = try_encode_multipart( hi );
        bool fits = true;
        for( size_t i = 0; i < result.first.size() && fits; ++i )
        {
          size_t part_size = make_url_start(i, num_parts).size() + est_crc.size()
                           + url_encode( result.first[i] ).size();
          if( part_size > max_chars_per_url )
            fits = false;
        }
        if( fits )
        {
          best_payloads = result.first;
          best_comp = result.second;
          best_num = hi;
        }
      }

      // Compute CRC over concatenated URL-decoded payloads (matching lossless convention)
      std::string combined_for_crc;
      for( size_t i = 0; i < best_payloads.size(); ++i )
        combined_for_crc += best_payloads[i];
      uint16_t crc = calc_CRC16_ARC( combined_for_crc );
      std::string crc_str = std::to_string( static_cast<unsigned int>(crc) ) + "/";

      // Build final URLs
      std::vector<std::string> urls;
      for( size_t part = 0; part < num_parts; ++part )
      {
        std::string url = make_url_start( part, num_parts ) + crc_str + url_encode( best_payloads[part] );
        urls.push_back( url );
      }

      // Compute RMSE
      double rmse = 0.0;
      {
        std::vector<float> reconstructed = decompress_spectrum( best_comp );
        double sum_sq = 0.0;
        const size_t count = std::min( n_channels, reconstructed.size() );
        for( size_t i = 0; i < count; ++i )
        {
          double diff = static_cast<double>(meas.m_channel_data[i])
                      - static_cast<double>(reconstructed[i]);
          sum_sq += diff * diff;
        }
        rmse = (n_channels > 0) ? std::sqrt( sum_sq / n_channels ) : 0.0;
      }

      LossyEncodeResult lr;
      lr.m_urls = urls;
      lr.m_rmse = rmse;
      lr.m_num_coefficients = best_num;
      return lr;
    }
  }
  else
  {
    // ---- Multi-spectrum: all spectra in a single URI ----
    // Encode each spectrum with SkipForEncoding flags (same as existing url_encode_spectra logic),
    // but replace channel data with wavelet blob.

    // Total budget for the combined data
    const size_t prefix_len = make_url_start( 0, 1 ).size();
    const size_t max_data_chars = max_chars_per_url - prefix_len;

    // Precompute decompositions for all spectra
    std::vector<WaveletDecomp> decomps( num_spectra );
    for( size_t s = 0; s < num_spectra; ++s )
    {
      if( measurements[s].m_channel_data.empty() )
        throw std::runtime_error( "url_encode_spectra_lossy: spectrum " + std::to_string(s) + " has no channel data." );
      decomps[s] = compute_decomposition( measurements[s].m_channel_data );
    }

    // Get metadata texts for each spectrum (with skip flags matching url_encode_spectra)
    std::vector<std::string> meta_prefixes( num_spectra );
    for( size_t s = 0; s < num_spectra; ++s )
    {
      unsigned int skip_encode_options = SkipForEncoding::Encoding;
      if( s > 0 )
        skip_encode_options |= SkipForEncoding::DetectorModel;

      if( s > 0 && (measurements[s].m_energy_cal_coeffs == measurements[0].m_energy_cal_coeffs)
          && (measurements[s].m_dev_pairs == measurements[0].m_dev_pairs) )
        skip_encode_options |= SkipForEncoding::EnergyCal;

      if( s > 0 && (measurements[s].m_latitude == measurements[0].m_latitude)
          && (measurements[s].m_longitude == measurements[0].m_longitude) )
        skip_encode_options |= SkipForEncoding::Gps;

      if( s > 0 && (measurements[s].m_title == measurements[0].m_title) )
        skip_encode_options |= SkipForEncoding::Title;

      std::vector<std::string> meta_parts = url_encode_spectrum( measurements[s], encode_options, 1,
                                                                  skip_encode_options );
      assert( meta_parts.size() == 1 );

      std::string meta_text = meta_parts[0];
      size_t s_pos = meta_text.find( "S:" );
      if( s_pos != std::string::npos )
        meta_prefixes[s] = meta_text.substr( 0, s_pos + 2 ); // includes "S:"
      else
        meta_prefixes[s] = meta_text + "S:";
    }

    // Lambda to build the combined payload for a given number of coefficients (same for all spectra)
    auto try_encode_multi = [&]( int num_coeffs ) -> std::pair<std::string, std::vector<CompressedSpectrum> >
    {
      std::string combined;
      std::vector<CompressedSpectrum> comps( num_spectra );

      for( size_t s = 0; s < num_spectra; ++s )
      {
        const size_t n_ch = measurements[s].m_channel_data.size();
        comps[s] = select_and_quantize( decomps[s], n_ch, num_coeffs );
        std::vector<uint8_t> blob = serialize_compressed( comps[s] );

        if( s > 0 )
          combined += ":0A:";

        combined += meta_prefixes[s];
        combined.append( reinterpret_cast<const char*>(blob.data()), blob.size() );
      }

      // Deflate compress
      if( use_deflate )
        deflate_compress( &(combined[0]), combined.size(), combined );

      // Base encoding (NOT URL-encoded -- caller handles URL-encoding)
      std::string encoded;
      if( use_url_safe_base64 )
        encoded = base64url_encode( combined, false );
      else
        encoded = base45_encode( combined );

      return std::pair<std::string, std::vector<CompressedSpectrum> >( encoded, comps );
    };

    // Binary search for maximum coefficients that fit
    int lo = 20;
    // For multi-spectrum, each spectrum gets the same coefficient count; use smaller budget
    int hi_per = 0;
    for( size_t s = 0; s < num_spectra; ++s )
    {
      int h = static_cast<int>( std::min( measurements[s].m_channel_data.size() * 15 / static_cast<size_t>(100),
                                           static_cast<size_t>(5000) ) );
      if( h > hi_per )
        hi_per = h;
    }
    int hi = hi_per;
    if( hi < lo ) hi = lo;

    std::string best_encoded;
    std::vector<CompressedSpectrum> best_comps;
    int best_num = lo;

    // First check if minimum fits
    {
      std::pair<std::string, std::vector<CompressedSpectrum> > result = try_encode_multi( lo );
      if( url_encode( result.first ).size() > max_data_chars )
        throw std::runtime_error( "url_encode_spectra_lossy: cannot fit even minimum coefficients" );
      best_encoded = result.first;
      best_comps = result.second;
      best_num = lo;
    }

    // Binary search
    while( hi - lo > 1 )
    {
      int mid = (lo + hi) / 2;
      std::pair<std::string, std::vector<CompressedSpectrum> > result = try_encode_multi( mid );
      if( url_encode( result.first ).size() <= max_data_chars )
      {
        lo = mid;
        best_encoded = result.first;
        best_comps = result.second;
        best_num = mid;
      }
      else
      {
        hi = mid;
      }
    }

    // Try hi as well
    if( hi != lo )
    {
      std::pair<std::string, std::vector<CompressedSpectrum> > result = try_encode_multi( hi );
      if( url_encode( result.first ).size() <= max_data_chars )
      {
        best_encoded = result.first;
        best_comps = result.second;
        best_num = hi;
      }
    }

    // Compute RMSE (weighted average across all spectra)
    double total_sum_sq = 0.0;
    size_t total_channels = 0;
    for( size_t s = 0; s < num_spectra; ++s )
    {
      std::vector<float> reconstructed = decompress_spectrum( best_comps[s] );
      const size_t n_ch = measurements[s].m_channel_data.size();
      const size_t count = std::min( n_ch, reconstructed.size() );
      for( size_t i = 0; i < count; ++i )
      {
        double diff = static_cast<double>(measurements[s].m_channel_data[i])
                    - static_cast<double>(reconstructed[i]);
        total_sum_sq += diff * diff;
      }
      total_channels += n_ch;
    }
    double rmse = (total_channels > 0) ? std::sqrt( total_sum_sq / total_channels ) : 0.0;

    // Build the final URL, URL-encoding the data
    std::string url = make_url_start( 0, 1 ) + url_encode( best_encoded );

    LossyEncodeResult lr;
    lr.m_urls.push_back( url );
    lr.m_rmse = rmse;
    lr.m_num_coefficients = best_num;
    return lr;
  }
}

}//namespace SpecUtils
