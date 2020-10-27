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
// format, to a CNF file.  If input file has more than one spectrum, they will
// all be summed together for output.

int main( int argc, char **argv )
{
  if( argc != 3 )
  {
    cout << "Usage: " << SpecUtils::filename(argv[0]) << " <input filename> <output filename>\n"
         << "\tex: " << SpecUtils::filename(argv[0]) << " input_file.spc output_file.cnf" << endl;
    return EXIT_FAILURE;
  }

  SpecUtils::SpecFile specfile;
  if( !specfile.load_file( argv[1], SpecUtils::ParserType::Auto ) )
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

  //Check if output file already exists, if so dont overwrite.
  //if( SpecUtils::is_file(argv[2]) )
  //{
  //  cerr << "Output file '" << argv[2] << "' already exists - not overwriting!" << endl;
  //  return EXIT_FAILURE;
  //}

  ofstream output( argv[2], ios::out | ios::binary | ios::trunc);
  
  if( !output )
  {
    cerr << "Could not open output file '" << argv[2] << "' for writing." << endl;
    return EXIT_FAILURE;
  }
  
  try 
  {
    // CNF files can only hold a single spectrum, but the file we read in may
    //  have had multiple spectra, maybe from multiple detectors.
    //  We either need to sum multiple spectra into one, or select a spectrum to
    //  write; we do this by specifying the sample numbers (each measurement at
    //  a different time has a different sample number), and the detector
    //  numbers (if two detectors in the system made a measurement at the same
    //  time the will generally have the same sample number).
    //  So to select a single spectrum from a file with multiple spectra we
    //  would specify one sample number, and one detector number below.
    //  However, for now lets just sum the whole file (which if the input file
    //  only had a single spectrum, its this spectrum that will be written).
   
    //Lets use all sample numbers.
    //  note: equivalently, if we dont specify any sample numbers, all samples
    //        will be used
    const set<int> sample_nums = specfile.sample_numbers();
    
    //Use all detectors.
    //  note: equivalently, if we dont specify any detector numbers, all
    //        detectors will be used
    const vector<int> det_nums_vec = specfile.detector_numbers();
    const set<int> detector_nums( begin(det_nums_vec), end(det_nums_vec) );
    
    const bool success = specfile.write_cnf( output, sample_nums, detector_nums );
    //equivalent to: specfile.write_cnf(output,{},{});
    
    if( !success )
    {
      output.close();
      SpecUtils::remove_file( argv[2] );
      throw std::runtime_error( "Error in SpecFile::write_cnf(...) - sorry" );
    }
  }catch( std::exception &e )
  {
    cerr << "Failed to write '" << argv[2] << "', error: " << e.what() << endl;
    return EXIT_FAILURE;
  }

  cout << "Wrote '" << argv[2] << "'" << endl;

  return EXIT_SUCCESS;
}//main
