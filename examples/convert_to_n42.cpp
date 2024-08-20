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

#include <fstream>
#include <stdlib.h>
#include <iostream>

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/Filesystem.h"

using namespace std;

// This is a simple example to show converting from any spectrum file
// format, to a n42 file.

int main( int argc, char **argv )
{
  
  /*
  
  if( argc != 3 )
  {
    cout << "Usage: " << argv[0] << " <input filename> <output filename>\n"
         << "\tex: " << argv[0] << " input_file.spc output_file.n42" << endl;
    return EXIT_FAILURE;
  }
   */
  
  SpecUtils::SpecFile specfile;
  //if( !specfile.load_file( argv[1], SpecUtils::ParserType::Auto ) )
  if( !specfile.load_file( "/Users/wcjohns/Downloads/Tl-201 IdentiFINDER (NaI) 1-day Old.SPE", SpecUtils::ParserType::Auto ) )
  {
    cerr << "Sorry, could not parse '" << argv[1] << "' as a spectrum file." << endl;
    return EXIT_FAILURE;
  }

  //Print a little bit of info about the file out.
  cout << "'" << argv[1] << "' is a valid spectrum file with "
       << specfile.num_measurements() << " spectrum records (composed of"
       << specfile.sample_numbers().size() << " time records of "
       << specfile.detector_names().size() << " detectors).\n"
       << "The sum real time of the measurements is " 
       << specfile.gamma_real_time() << " seconds, with live time "
       << specfile.gamma_live_time() << " seconds." << endl;
  
  vector<shared_ptr<const SpecUtils::Measurement>> records = specfile.measurements();
  if( !records.empty() )
  {
    cout << "The first record has " << records[0]->num_gamma_channels() << " gamma channels." << endl;
  }

  return 1;
  
  //Save the output file.
  if( SpecUtils::is_file(argv[2]) )
  {
    cerr << "Output file '" << argv[2] << "' already exists - not overwriting!" << endl;
    return EXIT_FAILURE;
  }

  //We could write the file to a std::ostream, but will use the shorter method write_to_file
  //ofstream output( argv[2], ios::bin | ios::out );
  //if( !output.is_open() ){
  //  cerr << "Failed to open '" << argv[2] << "' for writing." << endl;
  //  return EXIT_FAILURE;
  //}
  //specfile.write_2012_N42( output );

  try 
  {
    //We will write all time samples and detectors to output file.
    //  For more control see overloads of MeasurementInfo::write_to_file
    //  and MeasurementInfo::write
    specfile.write_to_file( argv[2], SpecUtils::SaveSpectrumAsType::N42_2012 );
  }catch( std::exception &e )
  {
    cerr << "Failed to write '" << argv[2] << "', error: " << e.what() << endl;
    return EXIT_FAILURE;
  }

  cout << "Wrote '" << argv[2] << "'" << endl;

  return EXIT_SUCCESS;
}//main
