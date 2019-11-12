const specutils = require('./SpecUtilsJS.node');

/* This script demonstrates parsing a spectrum file, printing out information about
 the file, then looping over all records in the file and printing out information
 about them.
 */

const inputfile = "example.n42";
const outputfile = "./temp.pcf";
const outputformat = "PCF";  //Other possible formats: "TXT", "CSV", "PCF", "N42-2006", N42-2012", "CHN", "SPC-int", "SPC" (or equiv "SPC-float"), "SPC-ascii", "GR130v0", "GR135v2", "SPE" (or equiv "IAEA"), "HTML".

let spec;
try
{
  spec = new specutils.SpecFile( inputfile );
}catch(e)
{
  //If the file couldnt be parsed as a spectrum file will always throw exception.
  console.error( "Couldnt open file '" + inputfile + "' as a spectrum file." );
  return;
}

console.log( "Opened '" + spec.filename() + "' as a spectrum file." );

/* Sum of live time over all gamma measurements (SpecRecord objects) in the file */
console.log( "Sum Gamma Live Time = " + spec.gammaLiveTime() + " seconds" );

/* Sum of real time over all gamma measurements (SpecRecord objects) in the file */
console.log( "Sum Gamma Real Time = " + spec.gammaRealTime() + " seconds" );

/* Sum of all gamma counts in the file */
console.log( "Gamma Count Sum = " + spec.gammaCountSum() );

/* Print out if spectrum file contained neutron information, and if so the total number of counts. */
if( spec.containedNeutrons() )
  console.log( "Neutron Count Sum = " + spec.neutronCountSum() );
else
  console.log( "Did not contain neutron data" );

/* The number of gamma channels in the data.  Some files may have measurements with
   different number of channels, in which case this function returns the first
   measurement found with non-zero number of channels.
 */
console.log( "Num Gamma Channels = " + spec.numGammaChannels() );

/* The instrument type as infered from the spectrum file format and meta-information.
 
   This is the first function you should use to determine detector type, as it is
   generally the most reliable, at least for the detectors the SpecUtils library
   knows about (that is to say the models wcjohns has bothered to explicitly
   identify so far)
 
 Currently this function returns one of a fixed number of strings, but in the
 future would like to make it an enum. Current possible strings:
 [ "Unknown", "GR135", "IdentiFINDER", "IdentiFINDER-NG", "IdentiFINDER-LaBr3",
   "Detective", "Detective-EX", "Detective-EX100", "Detective-EX200", "Detective X",
   "SAIC8", "Falcon 5000", "MicroDetective", "MicroRaider", "SAM940", "SAM940LaBr3",
   "SAM945", "SRPM-210", "RS-701", "RS-705", "RadHunterNaI", "RadHunterLaBr3",
   "RSI-Unspecified", "RadEagle NaI 3x1", "RadEagle CeBr3 2x1", "RadEagle CeBr3 3x0.8",
   "RadEagle LaBr3 2x1" ]
 */
console.log( "Inferred Instrument Type = " + spec.inferredInstrumentModel() );

/* Instrument Type defined by N42 spec.  This function will return value specified
   by the file itself, or if value can be infered from other info in the file.
   Some example strings it may return are: "PortalMonitor", "SpecPortal",
   "RadionuclideIdentifier", "PersonalRadiationDetector", "SurveyMeter",
   "Spectrometer", "Other" or empty string.
 */
console.log( "Instrument Type = " + spec.instrumentType() );

/* The manufacturer of the instrument.  Either as reported by the file, or infered
   from the file format.  May be blank if could not be determined.
 */
console.log( "Manufacturer = " + spec.manufacturer() );

/* The instrument model, either as reported by the file, or infered from the format.
   May be blank if could not be determined.  Different file formats output from the same
   detector may give different values for this field, or for some formats but not others.
   Similarly differnt firmware versions of the same detector may output different values.
 */
console.log( "Instrument Model = " + spec.instrumentModel() );

/* Serial number reported in file.  Blank if not recorded in file. */
console.log( "Serial Number = '" + spec.serialNumber() + "'" );

/* If data was recorded in a successesion of short time intervals.  Function will
 return true for portals since they record 0.1 second time slices one after another.
 Search detector systems will also often times record time slices continually one
 after another.
 */
if( spec.isSearchMode() )
  console.log( "Was either search-mode data, or portal data." );

/* Some files hold data from multiple gamma detection elements, in which case each
 detection element will get a different name, either indicated in the file, or
 assigned during file parsing.  Examples are "Aa1", "Ba2", "gamma" etc.
 */
const detNames = spec.detectorNames();
if( detNames.length == 1 )
  console.log( "There was a single detector" );
else
  console.log( "There was " + detNames.length + " detectors, with names: " + detNames );

//Remarks are an array of strings.
//const remarks = spec.remarks();

/* Print if file contained GPS information, and if so the mean location averaged
over all measurements that ha GPS data.
*/
if( spec.hasGpsInfo() )
  console.log( "Median GPS lat,lon: " + spec.meanLatitude() + "," + spec.meanLongitude() );
else
  console.log( "No GPS information available." );

/* Get RIID analysis results, if included in the file. */
let ana = spec.riidAnalysis();
if( ana ){
  console.log( "RIID Analysis Results Info:" );
  
  /* String giving algorithm name, or null if not provided in file. */
  let algoName = ana.algorithmName();
  if( algoName )
    console.log( "\tAlgorthm Name: " + algoName );
  
  /* Creator of analysis algorithm. Null if not provided in the file. */
  let creator = ana.algorithmCreator();
  if( creator )
    console.log( "\tAlgorithm Creator: " + creator );
  
  /* Algorithm description (as String) if provided in file, null otherwise. */
  let algoDescrip = ana.algorithmDescription();
  if( algoDescrip )
    console.log( "\tAlgorthm Description: " + algoDescrip );
  
  /** Result description (as String) if provided in file, null otherwise. */
  let resultDescrip = ana.algorithmResultDescription();
  if( resultDescrip )
    console.log( "\tResult Description: " + resultDescrip );
  
  /* Array of Strings representing remarks about RIID analysis given in spectrum
   file, or null if none are provided.
   */
  let remarks = ana.remarks();
  if( remarks )
    console.log( "\tRemarks: " + remarks );
  
  
  /* Returns array of RiidAnaResult objects contained in this analysis. */
  let nuclides = ana.results();
  for( let i = 0; i < nuclides.length; ++i)
  {
    /* Get the i'th RiidAnaResult */
    let result = nuclides[i];
    
    /* Get nuclide (as String); may be null if not provided in the file (ex. if
     dose rate is provided instead)
    */
    let nuc = result.nuclide();
    
    /* Get String giving type of nuclide, usually somethign like "Industrial",
     "Medical", etc.  Will be null when not provided in the spectrum file.
     */
    let nuc_type = result.nuclideType();

    /* String describing nuclide confidence.  May be a number (ex. "9"),
     a word (ex "High"), a letter (ex 'L'), or a phrase.  Will be null if not
     provided in file
     */
    let id_conf = result.idConfidence();
    
    /* String giving remark, or null if one was not provided in spectrum file. */
    let remark = result.remark();
    
    /* Returns Number giving dose rate in micro-sievert, or null if not avaialble. */
    let doseRate = result.doseRate();
    
    /* Get the name (as a String) of the detector this result corresponds to.
     If null or blank then you should assum it is for all detectors in the file.
     */
    let detName = result.detector();
    
    console.log( "\t\tResult " + i + ":"
                 + " nuc=" + (nuc ? nuc : "NotSpecified")
                 + ", nuc_type=" + (nuc_type ? nuc_type : "NotSpecified")
                 + (id_conf ? ", Confidence="+id_conf : "NotSpecified")
                 + (remark ? ", Remark="+remark : "")
                 + (doseRate ? ", DoseRate="+doseRate+"uSv" : "")
                 + (detName ? ", Detector="+detName : "")
                 );
  }//for( let i = 0; i < nuclides.length; ++i)
  
}else{
  console.log( "No RIID analysis results given in file." );
}


console.log( "Total Number Spectrum Records = " + spec.numSpecRecords() );

/* Lets get an array of SpecRecord objects and loop over them.
 We can optionally filter what SpecRecord's are returned.  We can filter
 by detector names, sample numbers, and source types.  For any of these three
 quantities if null is passed in, no filtering will be done on that quantity.
 */
let wantedDetectors = spec.detectorNames();  //Equivalent to passing null for wantedDetectors
let wantedSamples = spec.sampleNumbers();    //Equivalent to passing null for wantedSamples
let wantedSourceTypes = ["Background", "Calibration", "Foreground", "IntrinsicActivity", "UnknownSourceType"];  //Equivalent to null

let records = spec.records( wantedDetectors, wantedSamples, wantedSourceTypes );

for( let i = 0; i < records.length; ++i)
{
  /* A SpecRecord object represents a measurement from a single detection element; e.g., a spectrum, or a neutron count.
     If a gamma and neutron detector are unambigously grouped together, the record may contain both gamma spectrum and neutron counts.
   */
  let record = records[i];
  
  console.log( "\tRecord " + i + " (SampleNumber " + record.sampleNumber() + " DetectorName '" + record.detectorName() + "'):" );
  /* sourceType() will be one of the following strings: ["IntrinsicActivity",
       "Calibration", "Background", "Foreground", "UnknownSourceType"]
       (Defined by static members of the SourceType class)
     occupied() will be one of the following strings: ["NotOccupied", "Occupied",
         "UnknownOccupancyStatus"]
       (Defined by static members of the OccupancyStatus class)
   */
  console.log( "\t\tSource Type=" + record.sourceType() + ", OccStatus=" + record.occupied() );
  
  /* The sample number together with either the detector name or number is a unique combination
   that identifies this record within the spectrum file.
   All records with the same sample number represent data from the same time period.
   A file may contain data from multiple detector elements.
   */
  console.log( "\t\tDetName=" + record.detectorName() + ", DetNum=" + record.detectorNumber() + ", SampleNum=" + record.sampleNumber() );
  console.log( "\t\tLT=" + record.liveTime() + " s, RT=" + record.realTime() + ", StartTime=" + (new Date(record.startTime())) );
  
  console.log( "\t\tHasNeutron=" + record.containedNeutron() + ", SumNeutrons=" + record.neutronCountsSum() );
  if( record.hasGpsInfo()  )
    console.log( "\t\tLatitude=" + record.latitude() + ", Longitude=" + record.longitude() + ", fix at " + (new Date(record.positionTime())) );
  
  /* If you care about the underlying energy calibration, you can access the
   info given in the file using:
   */
  console.log( "\t\tEnergy calibration of type " + record.energyCalibrationModel()
               + " with coefficients: " + record.energyCalibrationCoeffs()
               + " and deviation pairs: " + record.deviationPairs() );
  
  /* If you only care about the actual energies of the gamma channels, you can
     instead get an array of Numbers giving the lower energies for each channel.
    Will return null if record does not have gamma data (e.g., only neutron)
   */
  console.log( "\t\tGamma Channel Energies: " + record.gammaChannelEnergies() );
  /* Get array of Numbers giving the channel counts for each channel.
   Will return null if record does not have gamma data (e.g., only neutron)
   */
  console.log( "\t\tGamma Channel Counts: " + record.gammaChannelContents() );
}//for( loop over records )


/* You can also sum specified measurements into a SpecRecord.
 This is useful, for example, to display a single spectrum to users of all
 foreground measurements in a file.
 */
try
{
  /* Filtering for SpecFile.sumRecords() is same as for SpecFile.records() */
  let summedForeground = spec.sumRecords(null,null,["Foreground", "UnknownSourceType"]);
  let summedBackground = spec.sumRecords(null,null,["IntrinsicActivity"]);
  console.log( "All foreground (and unknown SourceType) spectrum have LiveTime=" + summedForeground.liveTime()
               + "s, while summed instrinsic activities have LiveTime=" + summedBackground.liveTime() + "s" );
}catch(e)
{
  console.log( "Couldnt sum specified records: " + e );
  return;
}



//Finally write the file to a different format.
try
{
  /* You can add an optional third Boolean argument to force overwriting file (but that isnt done atomically) */
  spec.writeToFile( outputfile, outputformat );
  console.log( "Wrote input file to '" + outputfile + "', format " + outputformat );
}catch( err )
{
  console.error( "" + err );
}
