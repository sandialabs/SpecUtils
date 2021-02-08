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

#include <stdint.h>
#include <stddef.h>

#include <chrono>
#include <cstdint>
#include <sstream>

#include "fuzz_interface.h"
#include "SpecUtils/SpecFile.h"
#include "SpecUtils/Filesystem.h"

/** This program runs fuzz test cases to enable tracing down where the 
 * problem is, and fixing it.
*/
int main( int argc, char **argv )
{
  try
  {
    const char *crash_filename = "/path/to/file/crash-...";
    std::vector<char> data;
    SpecUtils::load_file_data( crash_filename, data );
    assert( data.size() > 1 );
    std::vector<char> data_actual( begin(data), end(data) - 1 ); //strip trailing '\0' inserted by SpecUtils::load_file_data
    
    auto t1 = std::chrono::high_resolution_clock::now();
    
    const int rval = run_file_parse_fuzz( (uint8_t *)(&(data_actual[0])), data_actual.size() );
    
    auto t2 = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>( t2 - t1 ).count();
    
    std::cout << "Parsing took " << duration << " micro-seconds" << std::endl;
  }catch( std::exception &e )
  {
    std::cerr << "Caught exception: " << e.what() << std::endl;
  }//try / catch
  
  return EXIT_SUCCESS;
}// int main(argc,argv)
