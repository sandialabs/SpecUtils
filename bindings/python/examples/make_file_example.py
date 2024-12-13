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
 

"""This file demonstrates using the SpecUtils module to 
create a new PCF (or other format) spectrum file from data you have 
assembled, like from a physical detector or something.

Note, to use SpecUtils, you must install it, e.g.:
    pip install SpecUtils-0.0.1-cp310-cp310-win_amd64.whl
"""

import SpecUtils

from datetime import datetime

# Create a SpecFile object - this is a spectrum file
info = SpecUtils.SpecFile()

# Optionally set a number of meta-information quantities on the SpecFile
info.setRemarks( ["Some comments abo"] )
info.setInstrumentManufacturer( "SNL" )
info.setInstrumentModel( "Special Detector" )
info.setInstrumentType( "NaI 3x3" )
info.setMeasurementLocationName( "Some Special Place" )
info.setSerialNumber( "Some Serial Num" )
info.setDetectorType( SpecUtils.DetectorType.DetectiveEx100 ) #if you know if and SpecUtils defines it


# We also will want an energy calibration defined (if not defined, will default 
# to polynomial from 0 to 3 MeV).  Here, we'll also throw in some deviation pairs.
#  You could instead use polynomial energy calibration, or lower channel energies
numChannels = 10      # 10 channels in gamma spectrum
energyCalCoefficients = [0,3000]
deviationPairs = [(100,-10), (1460,15), (3000,0)]
eneCal = SpecUtils.EnergyCalibration.fromFullRangeFraction( numChannels, energyCalCoefficients, deviationPairs )

# Add a new Measurement, and set its various member variables
#  Create an empty SpecUtils.Measurement object
newMeas = SpecUtils.Measurement.new()

# Set channel counts, which also requires setting live/real time at same time
liveTime = 10 # 10 seconds live time
realTime = 15 # 15 seconds clock time
gammaCounts = [0,1.1,2,3,4,5.9,6,7,8,9]  # needs to have same entries as `numChannels`
newMeas.setGammaCounts( gammaCounts, liveTime, realTime )
newMeas.setStartTime( datetime.fromisoformat('2022-08-26T00:05:23') )
# Set sample number to make sure you preserve order of records in the
# file.  If more than one detection element is present, the measurments from all 
# detectors that are taken during the same time period, should have the same 
# sample number
newMeas.setSampleNumber( 1 )

# Set the energy calibration to the Measurement
newMeas.setEnergyCalibration( eneCal )

# You may have multiple detectors you want to write data for, if so,
#  use a different name for each on
newMeas.setDetectorName( "Aa1" )


# You can optionally set a few more things, that could make sense some times,
#  but probably not usually
newMeas.setTitle( "Measurement title" )
newMeas.setSourceType( SpecUtils.SourceType.Background )
newMeas.setOccupancyStatus( SpecUtils.OccupancyStatus.NotOccupied )
newMeas.setRemarks( ['Remark 1', 'Remark 2'] )
newMeas.setPosition( Latitude=37.6762183189832, Longitude=-121.70622613299014, PositionTime=datetime.fromisoformat('2022-08-26T00:05:23') )

# Set neutron counts if your system is making those
newMeas.setNeutronCounts( Counts=[120], LiveTime=59.9 )

# We can choose to "cleanup" the file (recompute sums, reorder measurements, compute mappings, etc)
#  either as we add each measurement, or explicitly when we are done adding measurements.
#  If you are adding a bunch of measurements, then do it afterwards for efficiencies sake.
#  Note though, until you do a cleanup, accessing things from the SpecFile may not return 
#  valid results.
cleanupAfterwards = True

# Add the new Measurement to the `info` SpecFile.
#  Once we do this, dont change `newMeas` directly, as SpecFile
#  keeps track of sums and detector names; instead call the
# `info.set...(..., newMeas)` family of functions.
#
# The SpecUtils.SpecFile.set...(..., const SpecUtils.Measurement)
#  family of functions is also useful when the SpecFile "owns"
#  the Measurement you want to modify (e.g., you read in from a 
#  file).
info.addMeasurement( newMeas, not cleanupAfterwards )


# Now we'll add a second measurement
nextMeas = SpecUtils.Measurement.new()
liveTime = 10 
realTime = 15
gammaCounts = [5,2.1,6,1,9,5.2,2,1,0,10]
nextMeas.setGammaCounts( gammaCounts, liveTime, realTime )
nextMeas.setStartTime( datetime.fromisoformat('2022-08-26T00:05:38') )
nextMeas.setSampleNumber( 2 )
nextMeas.setEnergyCalibration( eneCal )
nextMeas.setDetectorName( "Aa1" )
nextMeas.setSourceType( SpecUtils.SourceType.Foreground )

info.addMeasurement( nextMeas, not cleanupAfterwards )

if cleanupAfterwards:
    # We can choose if we want to force leave the measurements in the same order, or 
    #  to re-order them by start-time. 
    #  We can also choose to have the energy calibration rebinned, so all Measurements
    #  will have the same energy calibration.
    # (You probably want to not change ordering, and to not rebin)
    info.cleanupAfterLoad( DontChangeOrReorderSamples = True, RebinToCommonBinning = False, ReorderSamplesByTime = False )

# You can save the spectrum file to disk in a number of ways.
# Here CHN file using a direct call to make CHN files
savetoname = "ex_output.pcf"
f = open( savetoname, 'wb' )

try:
    # We could call the dedicated PCF writing function, but instead we'll 
    # use the common interface that allows filtering which sample numbers
    # and detectors we will write out.  There are about a dozen other 
    # formats you can write out to.

    # info.writePcf( f )
    info.writeToStream( f, info.sampleNumbers(), info.detectorNames(), SpecUtils.SaveSpectrumAsType.Pcf )
except RuntimeError as e:
    print( "Error writing PCF file: {0}.".format( e ) )
    exit( 1 )
    
f.close()
print( "Wrote", savetoname )


