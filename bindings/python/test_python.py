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
 
#Note: on macOS you may need to rename the libSpecUtils.dylib library 
#      created when building the library, to SpecUtils.so

import SpecUtils

from datetime import datetime

filename = "Cal.pcf"
info = SpecUtils.SpecFile()

try:
    info.loadFile( filename, SpecUtils.ParserType.Auto )
except RuntimeError as e:
    print( "Failed to decode file: {0}".format( e ) )
    exit( 1 )

meass = info.measurements()
nmeas = info.numMeasurements()

if nmeas < 1:
    print( filename, "didnt contain any spectroscopic data" )
    exit( 1 )

if len(meass) != nmeas :
    print( "Fatal Error: len(meass) != nmeas" )
    exit( 1 )

print( "There were", nmeas, "measurements (or records) in the file" )

meas = meass[0]
#meas = info.measurement(0)

counts = meas.gammaCounts()
startime = meas.startTime()

numchannel = len(counts)
print( "For measurement started at", startime, ":" )
print( numchannel, "channels, with a few mid channels of the first measurement having counts:" )

print( "\tChannel\tCounts" )
for i in range(int(numchannel/2),min(int(numchannel/2)+10,numchannel)):
    print( "\t", i, "\t", counts[i] )
print( "With live time:", meas.liveTime(), "seconds, and total counts:", meas.gammaCountSum() )


nenergy = 511
channel = meas.findGammaChannel(nenergy)
content = meas.gammaChannelContent( channel )
lenergy = meas.gammaChannelLower( channel )
uenergy = meas.gammaChannelUpper( channel )
print( nenergy, "keV corresponds to channel ", channel, "which has", content, "counts, and energy range (", lenergy, ",", uenergy, ")" )


lenergy = 400
uenergy = 800
gammaint = meas.gammaIntegral(lenergy,uenergy)
print( "Between", lenergy, "and", uenergy, "keV, the sum of gamma counts is", gammaint )

lchannel = 20
uchannel = 30
gammasum = meas.gammaChannelsSum(lchannel,uchannel)
print( "Channels", lchannel, "through", uchannel, "summed give", gammasum, "gamma sums" )

sampleNums = info.sampleNumbers()
detNames = info.detectorNames()

print( "DetectorNames:", detNames )
print( "SampleNumbers:", sampleNums )


summedmeas = info.sumMeasurements( [1,2], info.detectorNames() )
print( "Summed measurement has liveTime=", summedmeas.liveTime() )


# Now we'll create a new Measurement, and set its various member variables
newMeas = SpecUtils.Measurement.new()

# Instead of creating a new SpecUtils.Measurement object, we could have
#   started with a copy of an existing Measurement. i.e.,
# newMeas = meas.clone()

# And change a bunch of values, both for the SpecFile object, and the new Measurement object
newLiveTime = 10
newRealTime = 15


# Test setting channel counts, which also requires setting live/real time at same time
newGammaCounts = [0,1.1,2,3,4,5.9,6,7,8,9,8,7,6,5,4,3,2,1]
numChannels = len(newGammaCounts)
energyCalCoefficients = [0,3000]
newEnergyCal = SpecUtils.EnergyCalibration.fromFullRangeFraction( numChannels, energyCalCoefficients )
deviationPairs = [(100,-10), (1460,15), (3000,0)]
newEnergyCal = SpecUtils.EnergyCalibration.fromFullRangeFraction( numChannels, energyCalCoefficients, deviationPairs )
newMeas.setGammaCounts( newGammaCounts, newLiveTime, newRealTime )
newMeas.setEnergyCalibration( newEnergyCal )

newMeas.setTitle( "The new measurements title" )
newMeas.setStartTime( datetime.fromisoformat('2022-08-26T00:05:23') )
newMeas.setRemarks( ['Remark 1', 'Remark 2'] )
newMeas.setSourceType( SpecUtils.SourceType.Foreground )
newMeas.setPosition( Latitude=37.6762183189832, Longitude=-121.70622613299014, PositionTime=datetime.fromisoformat('2022-08-26T00:05:23') )
newMeas.setNeutronCounts( [120] )
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
info.addMeasurement( newMeas )

info.setDetectorType( SpecUtils.DetectorType.DetectiveEx100 )
info.setInstrumentManufacturer( "MyCustomManufacturer" )
info.setInstrumentModel( "SomeDetector" )
info.setSerialNumber( "SomeSerialNumber102" )


# Test setting real/live time themselves
info.setLiveTime( newLiveTime, newMeas )
info.setRealTime( newRealTime, newMeas )
info.setTitle( "The new measurements title", newMeas )


print( "Set live time to ", newMeas.liveTime(), " seconds, and real time to ", newMeas.realTime() )


# First we'll write a CHN file using a direct call to make CHN files
savetoname = "Cal_pyconverted.chn"
f = open( savetoname, 'wb' )


try:
    # For illistration purposes, we'll choose to write data from only a single detector
    detNumbesToUse = [0] #info.detectorNumbers()
    info.writeIntegerChn( f, sampleNums, detNumbesToUse )
except RuntimeError as e:
    print( "Error writing Integer CHN file: {0}.".format( e ) )
    exit( 1 )
    
f.close()
print( "Wrote", savetoname )

# But there is a more general way to write spectrum files, with a common 
#  interface between all formats, you just specify the format using
#  the SaveSpectrumAsType enum.
#  First we'll just name a filesystem path to write to
savetoname = "Cal_pyconverted_writeToFile.chn"
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
savetoname = "Cal_pyconverted_writeToStream.chn"
f = open( savetoname, 'wb' )

try:
    info.writeToStream( f, info.sampleNumbers(), info.detectorNames(), SpecUtils.SaveSpectrumAsType.Chn )
except RuntimeError as e:
    print( "Error writing Integer CHN file in writeToStream: {0}.".format( e ) )
    exit( 1 )
    
f.close()
print( "Wrote", savetoname )



savetoname = "Cal_pyconverted.pcf"
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



savetoname = "Cal_pyconverted.n42"
f = open( savetoname, 'wb' )

try:
    info.write2012N42Xml( f )
except RuntimeError as e:
    print( "Error writing 2011 N42 file: {0}.".format( e ) )
    exit( 1 )
    
f.close()
print( "Wrote", savetoname )

#writeToFile
#writeToStream

#still having trouble reading from python source when seeking is done by the reader
f = open( "Cal_pyconverted.pcf", 'rb' )
rereadinfo = SpecUtils.SpecFile()
try:
    rereadinfo.setInfoFromPcfFile( f )
except RuntimeError as e:
    print( "Failed to decode the converted PCF file: {0}.".format( e ) )
    exit( 1 )

print( "Read converted PCF file, that has", rereadinfo.numMeasurements(), " measurements" )


