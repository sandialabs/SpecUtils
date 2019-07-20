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

#ifndef D3SpectrumExport_h
#define D3SpectrumExport_h

#include <map>
#include <vector>
#include <string>
#include <utility>
#include <ostream>

#include "SpectrumDataStructs.h"
#include "SpecUtils_config.h"

class Measurement;
class MeasurementInfo;

/* 20170701: compiling with SpecUtils_ENABLE_D3_CHART and SpecUtils_D3_SUPPORT_FILE_STATIC
  on adds about 0.7Mb to the library size (of which almost all of it is SpecUtils_D3_SUPPORT_FILE_STATIC).
 */
namespace D3SpectrumExport
{
  struct D3SpectrumOptions;
  struct D3SpectrumChartOptions;
  
  //Writes the spectrum component of the js that SpectrumChartD3 expects.
  // An anotatated example of what would be written is:
  // { "title": "Line Title", "peaks":"[...]", "liveTime": 300, "realTime": 320, "neutrons": 0,
  //   "lineColor": "black", "x": [0, 2.93, 5.86, ..., 3000], "y": [0,1,10,...0], "yScaleFactor": 1.0 }
  bool write_spectrum_data_js( std::ostream &ostr, const Measurement &meas, const D3SpectrumOptions &options, const size_t specID, const int backgroundID );
  
  //Legacy function for the moment... makes an entire HTML page for the provided Measurement
  bool write_d3_html( std::ostream &ostr,
                      const std::vector< std::pair<const Measurement *,D3SpectrumOptions> > &measurements,
                      const D3SpectrumChartOptions &options );
  
  /** Writes the HTML page header (</head> is the last thing written), including
      all JSS and CSS necassary for SpectrumChartD3.
   */
  bool write_html_page_header( std::ostream &ostr, const std::string &page_title );
  
  
  /** Write the javascript for a SpectrumChartD3 that should be displayed in a 
      <div> with id specified by div_name.
      Creates a local variable for the chart with the name spec_chart_`div_name`
      Does not create <script></script> tags
   */
  bool write_js_for_chart( std::ostream &ostr, const std::string &div_name,
                           const std::string &chart_title,
                           const std::string &x_axis_title,
                           const std::string &y_axis_title );
  
  /** Creates a var data_`div_name` that represents the measurments and options
      passed into this function, and calls setData on spec_chart_`div_name`.
      Does not create <script></script> tags
   */
  bool write_and_set_data_for_chart( std::ostream &ostr, const std::string &div_name,
                             const std::vector< std::pair<const Measurement *,D3SpectrumOptions> > &measurements );
  
  /** Sets the selected options to the chart dispayed in the div with id 
      specified by div_name.
      Does not create <script></script> tags
   */
  bool write_set_options_for_chart( std::ostream &ostr, const std::string &div_name, const D3SpectrumChartOptions &options );
  
  /**
   */
  bool write_html_display_options_for_chart( std::ostream &ostr, const std::string &div_name, const D3SpectrumChartOptions &options );
  
  
#if( SpecUtils_D3_SUPPORT_FILE_STATIC )
  const unsigned char *d3_js();
  const unsigned char *spectrum_chart_d3_js(); // For chart interactions, animations, etc.
  const unsigned char *spectrum_char_d3_css();
  const unsigned char *spectrum_chart_d3_standalone_css();
  
  const unsigned char *cassowary_js(); // Library for smart label-placement on chart
#else
  const char *d3_js_filename();
  const char *spectrum_chart_d3_js_filename(); // For chart interactions, animations, etc.
  const char *spectrum_chart_d3_css_filename();
  const char *spectrum_chart_d3_css_standalone_filename();
  const char *cassowary_js_filename(); // Library for smart label-placement on chart
#endif
  const char *spectrum_chart_setup_js();
  
  
  /** Specifies the options for a single spectrum displayed on the chart
   */
  struct D3SpectrumOptions
  {
    D3SpectrumOptions(); //black line, no peaks, scale factor 1.0
    
    //An array of peaks. See peak_json(...) in InterSpec.cpp
    std::string peaks_json;
    std::string line_color;  //standard css names, "steelblue", "black", etc
    std::string peak_color;
    double display_scale_factor;  //
    SpectrumType spectrum_type;  // spectrum type
  };//struct D3SpectrumOptions
  
  
  
  /** D3SpectrumChartOptions is a class that maintains the options of the current user
   chart to export into a D3.js HTML file. Ideally, exporting the HTML file would
   hold almost the exact state the user was in, and this struct helps hold some
   of those state values.
   
   */
  struct D3SpectrumChartOptions
  {
    D3SpectrumChartOptions();
    D3SpectrumChartOptions(  std::string title,
                           std::string xAxisTitle, std::string yAxisTitle,
                           std::string dataTitle,
                           bool useLogYAxis,
                           bool showVerticalGridLines, bool showHorizontalGridLines,
                           bool legendEnabled,
                           bool compactXAxis,
                           bool showPeakUserLabels, bool showPeakEnergyLabels, bool showPeakNuclideLabels, bool showPeakNuclideEnergyLabels,
                           bool showEscapePeakMarker, bool showComptonPeakMarker, bool showComptonEdgeMarker, bool showSumPeakMarker,
                           bool backgroundSubtract,
                           float xMin, float xMax,
                           std::map<std::string,std::string> refernce_lines_json
                           );
    
    std::string m_title;
    std::string m_xAxisTitle;
    std::string m_yAxisTitle;
    std::string m_dataTitle;
    
    bool m_useLogYAxis;
    bool m_showVerticalGridLines, m_showHorizontalGridLines;
    bool m_legendEnabled;
    bool m_compactXAxis;
    bool m_showPeakUserLabels, m_showPeakEnergyLabels, m_showPeakNuclideLabels, m_showPeakNuclideEnergyLabels;
    bool m_showEscapePeakMarker, m_showComptonPeakMarker, m_showComptonEdgeMarker, m_showSumPeakMarker;
    bool m_backgroundSubtract;
    bool m_allowDragRoiExtent;
    
    float m_xMin, m_xMax; // energy range user is zoomed into
    
    std::map<std::string,std::string> m_reference_lines_json;  //map from nuclide to their JSON
  };//struct D3SpectrumChartOptions
}//namespace D3SpectrumExport
#endif //D3SpectrumExport
