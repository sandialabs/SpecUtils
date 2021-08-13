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
 

# This script reads in each spectrum file supplied on the command line, and then
# adds a spectrum to the plot for each sample number (with all the detectors of 
# the sample number summed together), outputting into output.html.
# (this file completely untested, and likely not working as of 20210726)

import sys

#Note: on macOS you may need to rename the libSpecUtils.dylib library 
#      created when building the library, to SpecUtils.so
import SpecUtils


# We will fine a list of tuples, where each tuple will contain a SpecUtils.Measurement
# object, and a SpecUtils.D3SpectrumOptions object
spectra_to_plot = []

# We will loop over the command line arguments, and try to open them as spectrum files
# and then add each sample in each file to the plot
for filename in sys.argv[1:]:
    info = SpecUtils.SpecFile()
    
    try:
        info.loadFile( filename, SpecUtils.ParserType.Auto )
        
    except RuntimeError as e:
        print( "Failed to open spectrum file: {0}".format( e ) )
        exit( 1 )

    
    # If the file contains multiple measurements, we will sum together each
    # sample (e.g., sum all detectors that made a measurement for the same
    # time interval), and then add it to what we will plot
    sampleNums = info.sampleNumbers()

    print( "Loaded {} that has {} sample numbers".format(filename, len(sampleNums)) )

    for sample in sampleNums:
        detNames = info.detectorNames()
        m = info.sumMeasurements( [sample], detNames )

        # If this measurement doesnt have a spectrum (e.g., maybe a neutron-only meas), skip it
        if m.numGammaChannels() < 7:
            continue

        opt = SpecUtils.D3SpectrumOptions()
        # set the line color to a valid CSS line color - here we'll just do some nonsense color progression
        r = ( 7*len(spectra_to_plot)) % 255
        g = (19*len(spectra_to_plot)) % 255
        b = (37*len(spectra_to_plot)) % 255
        opt.line_color = "rgb({},{},{})".format(r, g, b)
        
        opt.display_scale_factor = 1.0

        # If we have already added a plot, lets live-time normalize to it
        if len(spectra_to_plot) > 0:
            opt.display_scale_factor = spectra_to_plot[0][0].liveTime() / m.liveTime()

        opt.title = filename + " sample " + str(sample)
        opt.spectrum_type = SpecUtils.SpectrumType.Foreground  #SecondForeground, or Background

        spectra_to_plot.append( (m,opt) )

if len(spectra_to_plot) < 1:
    print( "No spectra to plot" )
    exit( 1 ) 

print( "Will add {} plots to output.html".format(len(spectra_to_plot)) )

f = open( "output.html", 'w' )

# Set options for displaying the chart
options = SpecUtils.D3SpectrumChartOptions()
#options.title = "Some Title"
options.x_axis_title = "Energy (keV)"
#options.y_axis_title = "Counts"
#options.data_title = "Data Title"
options.use_log_y_axis = True
options.show_vertical_grid_lines = False
options.show_horizontal_grid_lines = False
options.legend_enabled = True
options.compact_x_axis = True
options.show_escape_peak_marker = False
options.show_compton_peak_marker = False
options.show_compton_edge_marker = False
options.show_sum_peak_marker = False
options.background_subtract = False
options.allow_drag_roi_extent = False
#options.x_min = 0
#options.x_max = 3000

SpecUtils.write_d3_html( f, spectra_to_plot, options )

print( "Wrote output.html" )

exit( 1 )
