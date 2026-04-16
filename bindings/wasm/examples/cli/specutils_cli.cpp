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

#include <string>
#include <cstdlib>
#include <iostream>

#include "SpecUtils/SpecFile.h"

// A command-line tool that converts any spectrum file to N42-2012.
// Built with Emscripten, runs via Node.js for sandboxed execution.
//
// Usage:
//   node specutils_cli.js <input_file> [output_file.n42]

int main( int argc, char **argv )
{
  if( argc < 2 || argc > 3 )
  {
    std::cerr << "SpecUtils spectrum file converter\n"
              << "Converts any supported spectrum file format to N42-2012.\n\n"
              << "Usage: node " << (argc > 0 ? argv[0] : "specutils_cli.js")
              << " <input_file> [output_file.n42]\n\n"
              << "Supported input formats: N42-2006, N42-2012, SPC, PCF, CHN,\n"
              << "  SPE (IAEA), CSV/TXT, CNF, Exploranium, Aram, Amptek MCA,\n"
              << "  TKA, PHD, LZS, JSON, RadiaCode, and more.\n"
              << std::endl;
    return EXIT_FAILURE;
  }

  const std::string input_path = argv[1];

  // Derive output path
  std::string output_path;
  if( argc == 3 )
  {
    output_path = argv[2];
  }
  else
  {
    output_path = input_path;
    const size_t dot_pos = output_path.rfind( '.' );
    if( dot_pos != std::string::npos && dot_pos > 0 )
      output_path = output_path.substr( 0, dot_pos );
    output_path += ".n42";
  }

  // Parse the input file
  SpecUtils::SpecFile specfile;
  if( !specfile.load_file( input_path, SpecUtils::ParserType::Auto ) )
  {
    std::cerr << "Error: could not parse '" << input_path << "' as a spectrum file." << std::endl;
    return EXIT_FAILURE;
  }

  // Print file summary
  std::cout << "Parsed '" << input_path << "':\n"
            << "  Measurements: " << specfile.num_measurements() << "\n"
            << "  Samples: " << specfile.sample_numbers().size() << "\n"
            << "  Detectors: " << specfile.detector_names().size();

  if( !specfile.detector_names().empty() )
  {
    std::cout << " (";
    for( size_t i = 0; i < specfile.detector_names().size(); ++i )
    {
      if( i > 0 ) std::cout << ", ";
      std::cout << specfile.detector_names()[i];
    }
    std::cout << ")";
  }
  std::cout << "\n";

  if( !specfile.manufacturer().empty() )
    std::cout << "  Manufacturer: " << specfile.manufacturer() << "\n";
  if( !specfile.instrument_model().empty() )
    std::cout << "  Model: " << specfile.instrument_model() << "\n";

  std::cout << "  Live time: " << specfile.gamma_live_time() << " s\n"
            << "  Real time: " << specfile.gamma_real_time() << " s\n"
            << "  Gamma counts: " << specfile.gamma_count_sum() << "\n"
            << std::endl;

  // Write N42-2012 output
  try
  {
    specfile.write_to_file( output_path, SpecUtils::SaveSpectrumAsType::N42_2012 );
  }
  catch( std::exception &e )
  {
    std::cerr << "Error writing '" << output_path << "': " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "Wrote '" << output_path << "'" << std::endl;

  return EXIT_SUCCESS;
}
