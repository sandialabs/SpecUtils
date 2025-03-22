# Overview
In order to use **SpecUtils** from **node.js**, we need to compile the C++ into a node module, but once this is done, you can then parse spectrum files, access all spectral and meta data, and convert the file to a different format within JavaScript. 


# Building the Node Module

To create the add-on we use [cmake-js](https://www.npmjs.com/package/cmake-js) to build the [SpecUtils](https://github.com/sandialabs/SpecUtils) library as well as some [node-addon-api](https://www.npmjs.com/package/node-addon-api) interface code to allow accessing the C++ functions from JavaScript.  Once the module is built, it should work with any **node.js** versions >=6.14.2 thanks to the ABI stability offered by [n-api](https://nodejs.org/api/n-api.html).

## Prerequisites
You will need a reasonable recent version of [CMake](https://cmake.org/) (version >=3.1) installed, a C++ compiler that can handle C++11, and the [boost](https://www.boost.org/) libraries (developed against 1.65.1 version shouldn't matter much) to link against.   You will of course also need [node.js](https://nodejs.org/en/) installed.
- **Windows**: You will need the CMake and node executables in your **PATH** variable.  When installing node.js there may be an option to install the MSVC command line tools through Chocolatey, otherwise you will need to have MSVS-2012 or newer installed (development done with MSVS-2017). Other compilers (clang, [MingGW](https://nuwen.net/mingw.html)) *should* work, but have not been tested.  This project links against the static runtime libraries (e.g., uses the '/MT' flag), and statically links to boost, so the resulting module is self contained.  When executing the steps below to compile the code you will need to open the 'x64 Native Tools Command Prompt' provided by MSVS, or execute the appropriate *vcvarsall.bat*.
  - Instructions for building boost can be found [here](https://www.boost.org/doc/libs/1_65_1/more/getting_started/windows.html), but basically you want to do something like: 
    ```bash
       cd Path\To\boost_1_65_1
       boostrap.bat
       b2.exe -j4 runtime-link=static link=static threading=multi address-model=64 --prefix=C:\install\msvc2017\x64\boost_1_65_1 install
    ```
- **macOS**: You will need the Xcode command line tools installed, which can be done by running `xcode-select --install`.  CMake, node, and boost can all be installed using [MacPorts](https://www.macports.org/), [HomeBrew](https://brew.sh/), or manually installed.  Keep in mind it is best to statically link to boost (e.g., have the ".a" libraries availble).
- **Linux**: You can use your package manager to install node.js, CMake, and the C++ compiler and related tools (usually with something like `apt-get install build-essential`).  However, for boost some care may be needed.  The `SpecUtils` module is really a shared library that node.js loads.  To avoid dependencies we will statically link to the boost libraries (e.g., copy the needed boost code into the resulting SpecUtils.module file), meaning you need the `-fPIC` C/C++ compiler flag enabled not just for building `SpecUtils` code, but for all of the static libraries you link it against, namely, boost - which isn't the default when compiling static libraries.  Therefore when building boost you may need to add `-fPIC -std=c++11` to the compile flags.

# Build Instructions
From bash or the Windows Command Prompt, run:
```bash
# Globally install cmake-js
npm install -g cmake-js 

# For macOS only, you may want to define a deployment target
export MACOSX_DEPLOYMENT_TARGET=10.10

cd /path/to/SpecUtils/bindings/node/

# Install dependency for compiling a node.js add-on
npm install --save-dev node-addon-api

# If boost is in a standard location, you can just run
cmake-js --CDSpecUtils_FLT_PARSE_METHOD="strtod"

# Note that the 'SpecUtils_FLT_PARSE_METHOD' options are "FastFloat", "FromChars", 
# "boost", and "strtod"; See SpecUtils/CMakeLists.txt for details, but "strtod"
# should work everywhere. "FromChars" works with reasonably modern compilers, except 
# not with the Xcode supplied compiler.  "boost" is fastest, but requires boost 
# installed.  "FastFloat" is a great choice, and if its not in your include path,
# CMake will attempt to fetch it.

# Or to have a little more control over things
cmake-js --CDSpecUtils_FLT_PARSE_METHOD="strtod" --CDCMAKE_BUILD_TYPE="Release" --out="build_dir"

# If you make changes and want to recompile
cmake-js build --out="build_dir"
# Or you can use CMake to run the `make` command, which can be useful when
# the CMake generator isnt a command-line based system like (ex Xcode, MSVC)
cmake --build build_dir --config Release


# And also copy all the InterSpec resources to the 
# 'app' sub-directory of your build dir
cmake-js build --out=build_dir --target install
# Or
cmake --build build_dir --target install --config Release

# These commands will copy 'SpecUtilsJS.node' and 'example.js' 
# to 'node_release/' in your build directory, so you can run like:
node build_dir/node_release/example.js
```

# Example Use In JavaScript
```JavaScript
const specutils = require('./SpecUtilsJS.node');

// Open and parse spectrum file
let spec = new specutils.SpecFile( "example.n42" );

// Print out a little info about detection system
console.log( "Manufacturer = " + spec.manufacturer() );
console.log( "Instrument Model = " + spec.instrumentModel() );
console.log( "Serial Number = '" + spec.serialNumber() + "'" );

// Print a little information about each record in the file
let records = spec.records();
for( let i = 0; i < records.length; ++i){
  let record = records[i];
  console.log(  "SampleNumber " + record.sampleNumber() + ", DetectorName '" 
     + record.detectorName() + "' has LiveTime " + record.liveTime() + " s"
     + ", ChannelCounts=" + record.gammaChannelContents() );   
}

// Sum all spectrum that the file indicated was a foreground 
//  (aka item of interest), or didnt indicate the source type 
//  (almost always a foreground)
let summed = spec.sumRecords(null,null,["Foreground", "UnknownSourceType"]);
console.log( "Channel Counts = '" + summed.gammaChannelContents() + "'" );

// Write the file to a different format.
// You could also filter which records get saved with extra arguments
spec.writeToFile( "output.pcf", "PCF" );
```

# Useful Concepts for Working with Spectrum Files
Spectrum files can be quite varied in the information they contain, so **SpecUtils** tries to coalesce all formats to something almost kinda matching the [N42-2012](https://www.nist.gov/programs-projects/ansiieee-n4242-standard) representation model (not perfectly though).

Each spectrum file is represented as a `SpecFile` object that contains file-level information such as device manufacturer, serial number, RIID results, etc.  This object also contains the gamma-spectra included in the file.  Files may contain only a single spectrum, or they can contain spectrum from multiple different detection elements, and/or multiple measurements from each detection element.  A spectrum and its related information is held by a `SpecRecord` object.  To organize the spectra, each detection element is assigned a name (most file formats specify this name, if not a reasonable guess is made), and spectra spanning the same time periods are all assigned the same *sample number*.  A detector name and sample number will uniquely specify a `SpecRecord` inside of a `SpecFile`.
A `SpecRecord` object contains zero or one spectrums, potentially neutron count information (if a neutron detector can be unambiguously grouped with a neutron detector, the information from both will be combined into one `SpecRecord`, otherwise they will be separate), as well as live time, real (clock) time, energy calibration information, and other related information.  Each `SpecRecord` also has a `SourceType` assigned to it, which can be "Background", "Calibration", "Foreground", "IntrinsicActivity", or "UnknownSourceType"; this information can help you sort which `SpecRecord` you are interested in.

# JavaScript Reference
There are four main classes defined by this module: `SpecFile`, `SpecRecord`, `RiidAnaResult`, and `RiidAnalysis`.  For all the classes, information is accessed through accessor functions.  There are some additional classes defined (`SourceType`, `OccupancyStatus`, `EquationType`, `DetectorType`) solely to define constants.


- `SpecFile` This class represents a spectrum file on disk. Defined functions are:
  - `Constructor`: Takes a single *String* argument that gives the filesystem path to spectrum file to parse.  Throws exception if file can't be parsed.
  - `records( wantedDetectorNames, wantedSampleNumbers, wantedSourceTypes )`: Returns an array of `SpecRecord` objects.  If provided, the arguments filters the `SpecRecord`s returned so that they match the provided filters.
    -  `wantedDetectorNames`: Either a single detector name, or an *Array* of detector names that *should* be included in returned results.  Passing `null` for this argument results in using all detector names.  If a detector name is not in the `SpecFile` than an exception will be thrown.
    -  `wantedSampleNumbers`: Either a single *Number*, or an *Array* of *Number*s specifying the sample numbers that should be included in the returned results.  Passing `null` for this argument results in using all sample numbers.  If any sample numbers are not in the `SpecFile`, an exception will be thrown.
    - `wantedSourceTypes`: Either a single *String* or an *Array* of *String*s specifying the `SourceType`s to include in the result.  Passing `null` for this argument results in using all source types.  Valid strings are "Background", "Calibration", "Foreground", "IntrinsicActivity", and "UnknownSourceType", using an invalid string will result in an exception (these strings are also defined by the `SourceType` static member variables like `SourceType.Background`, `SourceType.Foreground`, etc).  Note that "UnknownSourceType" is used when the spectrum file does not specify the source type, and it cant unambiguously be determined, however, these are almost always equivalent to "Foreground".
  - `sampleNumbers( wantedSourceTypes )`: Returns an *Array* of *Number*s containing all the sample numbers in the spectrum file, passing the optional `SourceType` filtering; array may be empty if no `SpecRecord`s matched the optional filtering of `SourceType`s.
    - `wantedSourceTypes`: Either *null*, a *String*, or an *Array* of *String*s that specify the which `SourceType`s are wanted (eg, "Background", "Calibration", "Foreground", "IntrinsicActivity", and "UnknownSourceType"); has same semantics and meaning to the equivalent argument for the `records()` function.
  - `detectorNames()`: Returns an *Array* of *String*s containing the detector names in the spectrum file. Takes no arguments.
  - `sumRecords( wantedDetectorNames, wantedSampleNumbers, wantedSourceTypes )`: Returns a `SpecRecord` object, where all relevant gamma spectra, times, neutron counts, etc. have all been summed into a single record.  The arguments have identical semantics and meaning to the `records()` function.
  - `inferredInstrumentModel()`: The instrument model as inferred by the parsing code.  Will be "Unknown" if couldn't be determined, otherwise will be from a set number of strings, including "IdentiFINDER", "IdentiFINDER-NG", "Detective-EX100", etc.  If this value is not "Unknown", then this is usually the most reliable way to determine detector type, as many spectrum file formats do not give model information as the `manufacturer()` and `instrumentModel()` functions return.
  - `manufacturer()`: Returns *String* giving manufacturers name, usually as specified in the file (maybe be empty *String*).
  - `instrumentModel()`: Returns *String* giving instrument model, usually as specified in the file (maybe be empty *String*).
  - `serialNumber()`: Returns *String* giving the serial number of the detector, as specified in the file (maybe be empty *String*).
  - `riidAnalysis()`: Returns a `RiidAnalysis` object if available in spectrum file, otherwise `null`.
  - `writeToFile( path, format, wantedDetectorNames, wantedSampleNumbers, wantedSourceTypes, forceOverwrite )`: Writes the spectrum file information to a new file.  You must provide at least the first two arguments.  Will throw exception on error.
    - `path`: filesystem location, as a *String*, to write the file.
    - `format`: Spectrum file format to write. Must be one of the following *String*s:  "TXT", "CSV", "PCF", "N42-2006", "N42-2012", "CHN", "SPC-int", "SPC", "SPC-float", "SPC-ascii", "GR130v0", "GR135v2", "SPE", "IAEA", "HTML".
    - `wantedDetectorNames`, `wantedSampleNumbers`, `wantedSourceTypes`: same semantics and meaning as for `records()`.  Allows filtering which `SpecRecord` are included in the output file.
    - `forceOverwrite`: A *Boolean*, that if specified will force overwriting the output file, if it already exists.  If argument is not given, defaults to `false`.
  - `isSearchMode()`: Returns *Boolean* indicating if the spectrum file was a search-mode, or RPM measurement where there are samples for many consecutive time periods.
  - `hasGpsInfo()`: Returns *Boolean* indicating if the file contained any GPS information.
  - `meanLatitude()` : Returns *Number* indicating the mean latitude given for all the records in the file.  Returns `null` if no GPS information is in the file.
  - `meanLongitude()`: Returns *Number* indicating the mean longitude given for all the records in the file.  Returns `null` if no GPS information is in the file.
  - Additional functions: gammaLiveTime, gammaRealTime, gammaCountSum, containedNeutrons, neutronCountSum, numGammaChannels, numSpecRecords, instrumentType, uuid, filename, remarks, 
    
- `SpecRecord`
  - `liveTime()`: Returns *Number* giving live time, in seconds, of the records.
  - `realTime()`: Returns *Number* giving real time (i.e., clock time), in seconds, of the records.
  - `detectorName()`: Returns *String* giving detectors name.
  - `sampleNumber()`: Returns *Number* giving sample number of record.
  - `sourceType()`: Returns *String* giving source type.  Will be one of "Background", "Calibration", "Foreground", "IntrinsicActivity", and "UnknownSourceType".
  - `startTime()`: Returns the measurement start time as a *Number* giving the milliseconds after the Epoch.  If no start time was given in the file, will return null.
  - `title()`: Returns title of the record as a *String*.  Maybe be an empty string.
  - `gammaChannelEnergies()`: If the records includes a gamma spectrum, will return an *Array* of numbers representing the lower energy, in keV, of each gamma channel.  If record does not have a gamma spectrum, will return `null`.
  - `gammaChannelContents()`: If the records includes a gamma spectrum, will return an *Array* of numbers representing the channel counts for each gamma channel.  If record does not have a gamma spectrum, will return `null`.
  - Functions not yet documented: remarks, occupied, gammaCountSum, containedNeutron, neutronCountsSum, hasGpsInfo, latitude, longitude, positionTime, energyCalibrationModel, energyCalibrationCoeffs, deviationPairs

- `RiidAnalysis` Class representing the RIID analysis results included in the spectrum file.
  - `results`: Returns *Array* of `RiidAnaResult` objects that give the nuclides or sources identified by the RIID algorithm.  Returns `null` if RIID results not available in the file.
  - Functions not yet documented: remarks, algorithmName, algorithmCreator, algorithmDescription, algorithmResultDescription, 

- `RiidAnaResult` Class representing a RIID identification, usually a nuclide, or nuclear source.
  - `nuclide()`: Returns *String* giving nuclide.  May not strictly be a nuclide, but may be something like: "U-238", "U-ore", "HEU", "nuclear", "neutron", "Unknown", "Ind.", etc.  Will return `null` if no identification is given (most commonly happens when this `RiidAnaResult` is to give an activity or doe rate)
  - `nuclideType()`: Returns *String* giving type of nuclide, usually something like "Industrial",
   "Medical", etc.  Will return `null` when not provided in the spectrum file.
  - `idConfidence()`: Returns *String* describing nuclide confidence.  May be a number (ex. "9"),
   a word (ex "High"), a letter (ex 'L'), or a phrase. Will return `null` if not available.
  - `doseRate()`: Returns dose rate *Number* in micro-sievert.  Returns `null` if not available.
  - `detector()`: Returns the name, as a *String*, of the detector this result corresponds to.  If null or empty, then you should assume it is for all detectors in the file.
  - `remark()`: Returns *String* giving remark given in the file.  Returns `null` if one is not provided in spectrum file.
  

## Future Work
- Improvements in representing the C++ concepts and values in JavaScript
- Potentially allow mutating SpectrumFile objects, like with energy calibration, or adding or removing SpecRecord
- Expose the d3.js based representations.
