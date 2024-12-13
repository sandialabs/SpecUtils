#!/usr/bin/python3

# SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
# 
# Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC
# (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
# Government retains certain rights in this software.
# For questions contact William Johnson via email at wcjohns@sandia.gov, or
# alternative emails of interspec@sandia.gov, or srb@sandia.gov.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 

"""This file demonstrates using the SpecUtils module to open spectrum
files, read their information, create a new spectrum file, and also
save the information to disk in various spectrum file formats.

Note, to use SpecUtils, you must install it, e.g.:
    pip install SpecUtils-0.0.1-cp310-cp310-win_amd64.whl
"""

import SpecUtils

from datetime import datetime

# First we will open an existing file from disk 
#   Here we will use a file that comes with InterSpec,
#     https://github.com/sandialabs/InterSpec/blob/master/example_spectra/passthrough.n42
filename = "passthrough.n42"
info = SpecUtils.SpecFile()

try:
    info.loadFile( filename, SpecUtils.ParserType.Auto )
except RuntimeError as e:
    print( "Failed to decode file: {0}".format( e ) )
    exit( 1 )

# Now we'll look at some of its information
meass = info.measurements()
nmeas = info.numMeasurements() # same as len(meass)

if nmeas < 1:
    print( filename, "didnt contain any spectroscopic data" )
    exit( 1 )

if len(meass) != nmeas :
    print( "Fatal Error: len(meass) != nmeas" )
    exit( 1 )

print( "There were", nmeas, "measurements (or records) in the file" )

meas = meass[0] # same as instead calling `meas = info.measurement(0)`

# Get the array of gamma channel counts
counts = meas.gammaCounts()

# Get the measurement start time
startime = meas.startTime()


# Now we'll printout the counts from a few channels in the middle of the spectrum, just as a demonstration
numchannel = len(counts)
print( "For measurement started at", startime, ":" )
print( numchannel, "channels, with a few mid channels of the first measurement having counts:" )

print( "\tChannel\tCounts" )
for i in range(int(numchannel/2),min(int(numchannel/2)+10,numchannel)):
    print( "\t", i, "\t", counts[i] )
print( "With live time:", meas.liveTime(), "seconds, and total counts:", meas.gammaCountSum() )


# Now, we will find the channel that corresponds to a specific energy.
nenergy = 511
channel = meas.findGammaChannel(nenergy)

# Get the number of gamma counts in this channel
content = meas.gammaChannelContent( channel )  # same as counts[channel]

# And now get the energy range of this channel
lenergy = meas.gammaChannelLower( channel )
uenergy = meas.gammaChannelUpper( channel )
print( nenergy, "keV corresponds to channel ", channel, "which has", content, "counts, and energy range (", lenergy, ",", uenergy, ")" )


# Get the sum of gammas for an energy range.
lenergy = 400
uenergy = 800
gammaint = meas.gammaIntegral(lenergy,uenergy)
print( "Between", lenergy, "and", uenergy, "keV, the sum of gamma counts is", gammaint )


# Get the sum of gammas for a channel range.
lchannel = 20
uchannel = 30
gammasum = meas.gammaChannelsSum(lchannel,uchannel)
print( "Channels", lchannel, "through", uchannel, "summed give", gammasum, "gamma sums" )

# The spectrum file may have multiple detectors, and many time intervals
sampleNums = info.sampleNumbers()
detNames = info.detectorNames()  #includes gamma detectors, neutron detectors, and detectors with both
gammaDetNames = info.gammaDetectorNames()  #includes gamma detectors, and detectors with both gamma and neutron

print( "DetectorNames:", detNames )
print( "SampleNumbers:", sampleNums )


# You can sum multiple Measurement's into a single one (e.g., get a single 
# spectrum to analyze), by specifying which sample numbers (time intervals),
# and which detectors you want summed.  Here we'll take samples 1 and 2, for 
# all detectors in the spectrum file.
summedmeas = info.sumMeasurements( [1,2], info.detectorNames() )
print( "Summed measurement has liveTime=", summedmeas.liveTime() )


# Now we'll create a new Measurement, and set its various member variables
newMeas = SpecUtils.Measurement.new()

# Instead of creating a new SpecUtils.Measurement object, we could have
#   started with a copy of an existing Measurement. i.e.,
# newMeas = meas.clone()

# We'll need to set bunch of values
# We'll set channel counts, which also requires setting live/real time at same time
newLiveTime = 10
newRealTime = 15
newGammaCounts = [0,1.1,2,3,4,5.9,6,7,8,9,8,7,6,5,4,3,2,1]
newMeas.setGammaCounts( newGammaCounts, newLiveTime, newRealTime )


# We also will want an energy calibration defined (if not defined, will default 
# to polynomial from 0 to 3 MeV).  Here, we'll also throw in some deviation pairs.
energyCalCoefficients = [0,3000]
deviationPairs = [(100,-10), (1460,15), (3000,0)]
numChannels = len(newGammaCounts)
newEnergyCal = SpecUtils.EnergyCalibration.fromFullRangeFraction( numChannels, energyCalCoefficients, deviationPairs )

# Finally, set the energy calibration to the Measurement
newMeas.setEnergyCalibration( newEnergyCal )

# We can set some meta-information for the Measurement
newMeas.setTitle( "The new measurements title" )
newMeas.setStartTime( datetime.fromisoformat('2022-08-26T00:05:23') )
newMeas.setRemarks( ['Remark 1', 'Remark 2'] )
newMeas.setSourceType( SpecUtils.SourceType.Foreground )
newMeas.setPosition( Latitude=37.6762183189832, Longitude=-121.70622613299014, PositionTime=datetime.fromisoformat('2022-08-26T00:05:23') )
newMeas.setNeutronCounts( Counts=[120], LiveTime=59.9 )
remarks = info.remarks()
remarks.append( "Remark added from python to measurement" )
newMeas.setRemarks( remarks )


# Add the new Measurement to the `info` SpecFile.
#  Once we do this, dont change `newMeas` directly, as SpecFile
#  keeps track of sums and detector names; instead call the
# `info.set...(..., newMeas)` family of functions.
#
# The SpecUtils.SpecFile.set...(..., const SpecUtils.Measurement)
#  family of functions is also useful when the SpecFile "owns"
#  the Measurement you want to modify (e.g., you read in from a 
#  file).
#
# Since we are only adding one-measurement, we will do the "cleanup"
#  of re-computing sums and all that in this next call  
info.addMeasurement( newMeas, DoCleanup = True )

# We can also set a number of meta-information quantities on the SpecFile
info.setDetectorType( SpecUtils.DetectorType.DetectiveEx100 )
info.setInstrumentManufacturer( "MyCustomManufacturer" )
info.setInstrumentModel( "SomeDetector" )
info.setSerialNumber( "SomeSerialNumber102" )


# If you want to change a Measurement that "belongs" to a SpecFile
# (e.g., after you've added it, or if you opened it from a file
# on disk), you can use setters like the below:
info.setLiveTime( 55, newMeas )
info.setRealTime( 60, newMeas )
info.setTitle( "The new measurements title", newMeas )
# There are a number more of these.

print( "Set live time to ", newMeas.liveTime(), " seconds, and real time to ", newMeas.realTime() )


# You can save the spectrum file to disk in a number of ways.
# First we'll create a CHN file using a direct call to make CHN files
savetoname = "Ex_pyconverted.chn"
f = open( savetoname, 'wb' )


try:
    # For illustration purposes, we'll choose to write data from only a single detector
    #  We didnt set a detector name for `newMeas`, so first entry in `info.gammaDetectorNames()`
    #  will be the blank string
    detNamesToUse = [ gammaDetNames[0] ]
    info.writeIntegerChn( f, sampleNums, detNamesToUse )
except RuntimeError as e:
    print( "Error writing Integer CHN file: {0}.".format( e ) )
    exit( 1 )
    
f.close()
print( "Wrote", savetoname )

# But there is a more general way to write spectrum files, with a common 
#  interface between all formats, you just specify the format using
#  the SaveSpectrumAsType enum.
#  First we'll just name a filesystem path to write to
savetoname = "Ex_pyconverted_writeToFile.chn"
try:
    info.writeToFile( savetoname, sampleNums, info.detectorNames(), SpecUtils.SaveSpectrumAsType.Chn )
    print( "Wrote", savetoname )
except RuntimeError as e:
    # One reason for failure is if the file already exists, it wont be 
    # overwritten, and instead will throw exception.
    print( "Error writing Integer CHN file in writeToFile: {0}.".format( e ) )

    # We'll only exit if its an error other than from overwriting
    if not "not overwriting" in str(e):
        exit( 1 )


# Now instead of writing to a filesystem path, we'll write to a stream
savetoname = "Ex_pyconverted_writeToStream.chn"
f = open( savetoname, 'wb' )

try:
    info.writeToStream( f, info.sampleNumbers(), info.detectorNames(), SpecUtils.SaveSpectrumAsType.Chn )
except RuntimeError as e:
    print( "Error writing Integer CHN file in writeToStream: {0}.".format( e ) )
    exit( 1 )
    
f.close()
print( "Wrote", savetoname )



savetoname = "Ex_pyconverted.pcf"
f = open( savetoname, 'wb' );

try:
    # We could call the dedicated PCF writing function, but instead we'll use the common interface
    # info.writePcf( f )
    info.writeToStream( f, sampleNums, detNames, SpecUtils.SaveSpectrumAsType.Pcf )
except RuntimeError as e:
    print( "Error writing PCF file: {0}.".format( e ) )
    exit( 1 )
    
f.close()
print( "Wrote", savetoname )


# One of the most useful formats to write to is N42-2012; if you write the
# SpecFile out to this format, and later read it back in, no information 
# will be lost
savetoname = "Ex_pyconverted.n42"
f = open( savetoname, 'wb' )

try:
    info.write2012N42Xml( f )
    # Or you could do the same thing with:
    # info.writeToStream( f, info.sampleNumbers(), info.detectorNames(), SpecUtils.SaveSpectrumAsType.N42_2012 )
except RuntimeError as e:
    print( "Error writing 2011 N42 file: {0}.".format( e ) )
    exit( 1 )
    
f.close()
print( "Wrote", savetoname )

#writeToFile
#writeToStream

# And finally, we can read in spectrum files from a stream:
f = open( "Ex_pyconverted.pcf", 'rb' )
rereadinfo = SpecUtils.SpecFile()
try:
    rereadinfo.setInfoFromPcfFile( f )
except RuntimeError as e:
    print( "Failed to decode the converted PCF file: {0}.".format( e ) )
    exit( 1 )

print( "Read converted PCF file, that has", rereadinfo.numMeasurements(), " measurements" )


