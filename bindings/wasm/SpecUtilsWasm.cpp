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

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cstdlib>

#include <emscripten.h>

#include "SpecUtils/SpecFile.h"
#include "bindings/c/SpecUtils_c.h"

// The C bindings use opaque pointers; these are actually just casts.
// We need the real types for the buffer I/O helpers.

extern "C"
{

/** Load a spectrum file from an in-memory buffer, without needing MEMFS on the JS side.

 Internally writes to a temp file in MEMFS, calls the standard load, then cleans up.

 @param instance The SpecUtils_SpecFile to load into (must be created with SpecUtils_SpecFile_create)
 @param data Pointer to the file data in WASM memory
 @param length Number of bytes in data
 @param filename_hint The original filename - used for format detection (e.g., "file.spc")
 @returns true if parsing succeeded
 */
EMSCRIPTEN_KEEPALIVE
bool SpecUtils_load_from_buffer( SpecUtils_SpecFile *instance,
                                 const uint8_t *data, uint32_t length,
                                 const char *filename_hint )
{
  if( !instance || !data || length == 0 )
    return false;

  // Write data to a temp file in MEMFS so the existing file-based parsers can read it
  const char *tmp_path = "/tmp/_specutils_wasm_input";

  FILE *f = fopen( tmp_path, "wb" );
  if( !f )
    return false;

  const size_t written = fwrite( data, 1, length, f );
  fclose( f );

  if( written != length )
  {
    remove( tmp_path );
    return false;
  }

  // Use the C binding load function - it handles format auto-detection
  // The filename_hint helps with format guessing based on extension
  SpecUtils::SpecFile *spec = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  const std::string hint = filename_hint ? filename_hint : "";
  const bool ok = spec->load_file( tmp_path, SpecUtils::ParserType::Auto, hint );

  remove( tmp_path );

  return ok;
}


/** Export a spectrum file to an in-memory buffer.

 Writes to a temp file in MEMFS, reads it back into a malloc'd buffer.
 The caller must free the returned buffer with SpecUtils_free_export_buffer().

 @param instance The SpecUtils_SpecFile to export
 @param format_int The SaveSpectrumAsType enum value
 @param out_size Pointer to receive the number of bytes in the output buffer
 @returns Pointer to the exported file data, or NULL on failure.
          Caller must free with SpecUtils_free_export_buffer().
 */
EMSCRIPTEN_KEEPALIVE
uint8_t *SpecUtils_export_to_buffer( SpecUtils_SpecFile *instance,
                                     int format_int,
                                     uint32_t *out_size )
{
  if( !instance || !out_size )
    return nullptr;

  *out_size = 0;

  SpecUtils::SpecFile *spec = reinterpret_cast<SpecUtils::SpecFile *>( instance );
  const SpecUtils::SaveSpectrumAsType fmt =
    static_cast<SpecUtils::SaveSpectrumAsType>( format_int );

  const char *tmp_path = "/tmp/_specutils_wasm_output";

  try
  {
    spec->write_to_file( tmp_path, fmt );
  }
  catch( ... )
  {
    remove( tmp_path );
    return nullptr;
  }

  // Read the file back
  FILE *f = fopen( tmp_path, "rb" );
  if( !f )
    return nullptr;

  fseek( f, 0, SEEK_END );
  const long file_size = ftell( f );
  fseek( f, 0, SEEK_SET );

  if( file_size <= 0 )
  {
    fclose( f );
    remove( tmp_path );
    return nullptr;
  }

  uint8_t *buffer = static_cast<uint8_t *>( malloc( file_size ) );
  if( !buffer )
  {
    fclose( f );
    remove( tmp_path );
    return nullptr;
  }

  const size_t bytes_read = fread( buffer, 1, file_size, f );
  fclose( f );
  remove( tmp_path );

  if( bytes_read != static_cast<size_t>(file_size) )
  {
    free( buffer );
    return nullptr;
  }

  *out_size = static_cast<uint32_t>( file_size );
  return buffer;
}


/** Free a buffer returned by SpecUtils_export_to_buffer. */
EMSCRIPTEN_KEEPALIVE
void SpecUtils_free_export_buffer( uint8_t *buffer )
{
  free( buffer );
}


} // extern "C"


int main()
{
  return 0;
}
