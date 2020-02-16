# SpecUtils

**SpecUtils** is a library for opening, manipulating, and exporting spectrum files produced by RadioIsotope Identification Devices (RIIDs), Radiation Portal Monitors (RPMs), radiation search systems, Personal Radiation Detectors (PRDs), and many laboratory detection systems.
It opens [N42](https://www.nist.gov/programs-projects/ansiieee-n4242-standard) (2006 and 2012 formats), [SPE](https://inis.iaea.org/collection/NCLCollectionStore/_Public/32/042/32042415.pdf), [SPC](https://web.archive.org/web/20160418031030/www.ortec-online.com/download/ortec-software-file-structure-manual.pdf), CSV, TXT, [PCF](https://www.osti.gov/biblio/1378172-pcf-file-format), [DAT](https://www.aseg.org.au/sites/default/files/gr-135.pdf), and many more files; altogether more than a 100 file format variants.  The library lets you either programmatically access and/or modify the parsed information, or you can save it to any of 13 different formats.

**SpecUtils** provides the spectrum file format capabilities for [Cambio](https://hekili.ca.sandia.gov/cambio/), [InterSpec](https://github.com/sandialabs/InterSpec), and number of other applications and websites.  The library is written in cross-platform (tested on Windows, macOS, Linux, iOS, and Android) c++ with Python and Java bindings.

## Using SpecUtils
If all you need is to convert spectrum files from your detector to a format you can work with, like CSV or N42, try using the command line version of  [Cambio](https://hekili.ca.sandia.gov/cambio/) - this is actually probably the most common way people and programs use **SpecUtils**.

But if you want to integrate **SpecUtils** into your program, you will need a c++11 capable compiler, [boost](https://www.boost.org/) (&ge;1.42), and [cmake](https://cmake.org/) (&ge;3.1.).   Linking your program to **SpecUtils** uses the standard CMake syntax, for example in your CMakeLists.txt file you would add the lines:

```CMake
add_subdirectory( path/to/SpecUtils )
...
target_link_libraries( MyExe PRIVATE SpecUtils )
```

A minimal program to print out the channel counts of a spectrum file, and then save as a CSV data file, could then be:
```c++
#include  <stdlib.h>
#include  <iostream>
#include  "SpecUtils/SpecFile.h"

int main() {
  SpecUtils::SpecFile specfile;
  if( !specfile.load_file("input.n42", kAutoParser) )
    return EXIT_FAILURE;
    
  for( auto sample : specfile.sample_numbers() ) {
    for( auto detector : specfile.detector_names() ) {
      std::shared_ptr<const  Measurement> meas = specfile.measurement( sample, detector );
      std::cout << "Sample " << sample << " detector " << detector
                << " has live time " << meas->live_time() << "s and real time "
                << meas->real_time() << "s with counts: ";
      for( size_t i = 0; i < meas->num_gamma_channels(); ++i )
        std::cout << meas->gamma_channel_content(i) << ",";
      std::cout << " with " << meas->neutron_counts_sum() << " neutrons" << std::endl;
    }
  }
  
  specfile.write_to_file( "outout,csv", kCsvSpectrumFile );
  return EXIT_SUCCESS;
}
``` 


### Relevant CMake Build options
* **SpecUtils_ENABLE_D3_CHART** [default ON]: Enabling this feature allows exporting specta to a [D3.js](https://d3js.org/) based plotting that allows viewing and interacting with the spectra in a web browser.  An example can be seen [here](https://github.com/sandialabs/specutils/tree/master/examples/d3_chart_example/self_contained_example.html).
	* **SpecUtils_D3_SUPPORT_FILE_STATIC** [default ON]: Only relevant if *SpecUtils_ENABLE_D3_CHART* is *ON*.  This option determines if all the JavaScript (including D3.js, SpectrumChartD3.js, etc) and CSS should be compiled into the library, or remain as seperate files accessed at runtime.
* **SpecUtils_PYTHON_BINDINGS** [default OFF]: Determines if the Python bindings should be built.  [Boost.Python](https://www.boost.org/doc/libs/1_69_0/libs/python/doc/html/index.html) is used to generate the bindings.  Have a look in [test_python.py](https://github.com/sandialabs/specutils/tree/master/test_python.py) for example use of **SpecUtils** from python.
* **SpecUtils_JAVA_SWIG** [default OFF]:  Determines if the Java bindings should be built.  [SWIG](http://www.swig.org/) is used to generate the bindings, and an example application using them is in [swig/java_example/](https://github.com/sandialabs/specutils/tree/master/swig/java_example/) directory.
* **PERFORM_DEVELOPER_CHECKS** [default OFF]: Performs additional tests during program execution, with failed tests being output to a log file.

## Features
* Parses &gt;100 spectrum file format variants.
* Writes 13 of the most useful output file formats.
* Multithreaded and time-efficient code.  For example a ~125 MB N42 file takes less than 5 seconds to parse.  Accessing any of the extracted information after that is nearly instant as all the work is done upfront, and the original file is no longer referenced after the initial parse.
* Supports Polynomial, Full Width Fraction, Lower Channel Energy, and non-Linear Deviation pair energy calibrations.  
* Supports creating spectrum files from raw data (ex, can be used in a detector to write output file).
* Extracts RIID results, GPS location information, and a lot of other meta-information, depending on the input file format.
* Allows changing the meta-information.
* Tools to help rebin, re-calibrate, truncate, and combine spectra.
* And more!

## Testing
This library uses a few methods to test the code, but unfortunately still likely contains many bugs and issues as resources are not available to implement comprehensive test coverage.

The testing methods are:
* Peppered throughout the code there are `#if( PERFORM_DEVELOPER_CHECKS )` statements that perform additional tests at runtime that make sure the correct answer was computed or action taken.  Most of these blocks of code will either recompute a quantity using an independent implementation, or in someway perform additional sanity checks, and when issues are found, they are logged to a file for fixing in the future.  This seems to work well as the primary developer of this library is also the primary user.
* An assortment of unit tests have been created and are occasionally run, but by no means offer anywhere near 100% coverage.  Since many of the test contain proprietary data, they are not distributed with the library.
* As the primary developer of this library comes across new file formats, or new variants of file formats, they get added to a library after manually verifying they are parsed sufficiently well.  Then [testing/regression_test.cpp](https://github.com/sandialabs/specutils/tree/master/testing/regression_test.cpp) is used to ensure the files continue to be parsed exactly the same as when they were manually verified.  Any changes to the information extracted from these files will then be manually verified to be an improvement to the parsing (like adding the ability to extract GPS coordinates from a format), or the issue will be corrected.  This helps keep regressions from occurring.  A keystone piece to this testing is that all information extracted from any file format can be written out to a N42-2012 file; when this N42-2012 file is read back in, the exact same information is available as from the original file (this property is of course also tested for).
* Daily use to parse a wide variety of formats by the primary developer.


## History
**SpecUtils** started to extract live-time and channel counts from two very specific detectors, for a specific application.  However, over time it was split out into its own library, and new file formats and capabilities have been continually bolted on as needs arose, with out the time available to do a proper refactoring of code.  It is hoped that at some point in the future resources will become available to refactor the interface to the library.  Currently the library is developed primarily as part of [InterSpec](https://github.com/sandialabs/InterSpec), but used by a number of other applications and websites.
  
  

## Authors
William Johnson is the primary author of the library.  Edward Walsh wrote the Java bindings and example program, as well as lead a substantial part of the QC effort.  Christian Morte is the primary developer of the D3.js charting capability. Noel Nachtigal provided extensive additional support.

  

## License
This project is licensed under the LGPL v2.1 License - see the [LICENSE.md](LICENSE.md) file for details

  

## Copyright
Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC (NTESS).
Under the terms of Contract DE-NA0003525 with NTESS, the U.S. Government retains certain rights in this software.

  

## Disclaimer

```
DISCLAIMER OF LIABILITY NOTICE:
The United States Government shall not be liable or responsible for any maintenance,
updating or for correction of any errors in the SOFTWARE or subsequent approved version
releases.
  
THE INTERSPEC (SOFTWARE) AND ANY OF ITS SUBSEQUENT VERSION
RELEASES, SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY OF
ANY KIND, EITHER EXPRESSED, IMPLIED, OR STATUTORY, INCLUDING, BUT
NOT LIMITED TO, ANY WARRANTY THAT THE SOFTWARE WILL CONFORM TO
SPECIFICATIONS, ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE, OR FREEDOM FROM INFRINGEMENT, ANY
WARRANTY THAT THE SOFTWARE WILL BE ERROR FREE, OR ANY WARRANTY
THAT THE DOCUMENTATION, IF PROVIDED, WILL CONFORM TO THE
SOFTWARE. IN NO EVENT SHALL THE UNITED STATES GOVERNMENT OR ITS
CONTRACTORS OR SUBCONTRACTORS BE LIABLE FOR ANY DAMAGES,
INCLUDING, BUT NOT LIMITED TO, DIRECT, INDIRECT, SPECIAL OR
CONSEQUENTIAL DAMAGES, ARISING OUT OF, RESULTING FROM, OR IN ANY
WAY CONNECTED WITH THE SOFTWARE OR ANY OTHER PROVIDED
DOCUMENTATION, WHETHER OR NOT BASED UPON WARRANTY, CONTRACT,
TORT, OR OTHERWISE, WHETHER OR NOT INJURY WAS SUSTAINED BY
PERSONS OR PROPERTY OR OTHERWISE, AND WHETHER OR NOT LOSS WAS
SUSTAINED FROM, OR AROSE OUT OF THE RESULT OF, OR USE OF, THE
SOFTWARE OR ANY PROVIDED DOCUMENTATION. THE UNITED STATES
GOVERNMENT DISCLAIMS ALL WARRANTIES AND LIABILITIES REGARDING
THIRD PARTY SOFTWARE, IF PRESENT IN THE SOFTWARE, AND DISTRIBUTES
IT "AS IS."

```

  

## Acknowledgement
This InterSpec Software was developed with funds from the Science and Technology Directorate of the Department of Homeland Security

InterSpecSCR# 2173.1
