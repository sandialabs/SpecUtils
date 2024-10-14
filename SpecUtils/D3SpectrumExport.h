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

#include "SpecUtils_config.h"

#include <map>
#include <vector>
#include <string>
#include <utility>
#include <ostream>

#if( SpecUtils_BUILD_FUZZING_TESTS )
#include <sstream>
#endif


namespace SpecUtils
{
  class Measurement;
  enum class SpectrumType : int;
}

/* 20170701: compiling with SpecUtils_ENABLE_D3_CHART and SpecUtils_D3_SUPPORT_FILE_STATIC
  on adds about 0.7Mb to the library size (of which almost all of it is SpecUtils_D3_SUPPORT_FILE_STATIC).
 */
namespace D3SpectrumExport
{
  struct D3SpectrumOptions;
  struct D3SpectrumChartOptions;
  
  /** Writes the spectrum JSON (not javascript as the function name implies) that SpectrumChartD3 expects.
   
   @param ostr The stream to write the JSON to
   @param options The options to use for plotting the data.
   @param specID The ID to assign to this spectrum - this should be a unique value for the spectra you are
          plotting in the same plot.
   @param backgroundID The ID of the background spectrum, for this spectrum; set to a negative value if
          no background.  Used for background subtraction mostly.
   @returns If stream was able to accept all the data that was written to it.
  
   An annotated example of what would be written is:
   { "title": "Line Title", "peaks":"[...]", "liveTime": 300, "realTime": 320, "neutrons": 0,
      "lineColor": "black", "x": [0, 2.93, 5.86, ..., 3000], "y": [0,1,10,...0], "yScaleFactor": 1.0 }
   */
  bool write_spectrum_data_js( std::ostream &ostr,
                               const SpecUtils::Measurement &meas,
                               const D3SpectrumOptions &options,
                               const size_t specID, const int backgroundID );
  
  //Legacy function for the moment... makes an entire HTML page for the provided Measurement
  bool write_d3_html( std::ostream &ostr,
                      const std::vector< std::pair<const SpecUtils::Measurement *,D3SpectrumOptions> > &measurements,
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
  
  /** Creates a var data_`div_name` that represents the measurements and options
      passed into this function, and calls setData on spec_chart_`div_name`.
      Does not create <script></script> tags
   */
  bool write_and_set_data_for_chart( std::ostream &ostr,
                                     const std::string &div_name,
                                     const std::vector< std::pair<const SpecUtils::Measurement *,D3SpectrumOptions> > &measurements );
  
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
#else
  const char *d3_js_filename();
  const char *spectrum_chart_d3_js_filename(); // For chart interactions, animations, etc.
  const char *spectrum_chart_d3_css_filename();
#endif
  const char *spectrum_chart_setup_js();
  
  
  /** Specifies the options for a single spectrum displayed on the chart
   */
  struct SpecUtils_DLLEXPORT D3SpectrumOptions
  {
    D3SpectrumOptions(); //black line, no peaks, scale factor 1.0
    
    //An array of peaks. See peak_json(...) in InterSpec.cpp
    std::string peaks_json;
    
    /** Standard css color, "steelblue", "black", rgba(23,52,99,0.8), etc.
     If empty, then line color will default to the color specified by `D3SpectrumOptions::spectrum_type`;
     see  --d3spec-fore-line-color, --d3spec-back-line-color, and --d3spec-second-line-color.
     */
    std::string line_color;
    
    /** The default peak color, if the peak itself doesnt define a color. 
     
     Will default to a random color if not specified.
     */
    std::string peak_color;
    
    /** If empty, title from Measurement will be used, but if non-empty, will override Measurement.
     
     This is hopefully a temporary over-ride until proper escaping is implemented.
     */
    std::string title;
    
    /** The y-axis scale factor to use for displaying the spectrum.
     This is typically used for live-time normalization of the background
     spectrum to match the foreground live-time.  Ex., if background live-time
     is twice the foreground, you would want this factor to be 0.5 (e.g., the
     ratio of the live-times).
     
     Note: this value is displayed on the legend, but no where else on the
     chart.
     */
    double display_scale_factor;
    
    /** The spectrum type (foreground/background/secondary) for this spectrum,
     
     Note that peaks will only be displayed for foreground spectra currently.
     */
    SpecUtils::SpectrumType spectrum_type;
  };//struct D3SpectrumOptions
  
  
  
  /** D3SpectrumChartOptions is a class that maintains the options of the current user
   chart to export into a D3.js HTML file. Ideally, exporting the HTML file would
   hold almost the exact state the user was in, and this struct helps hold some
   of those state values.
   
   */
  struct SpecUtils_DLLEXPORT D3SpectrumChartOptions
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
  
#if( SpecUtils_BUILD_FUZZING_TESTS )
  std::string escape_text_test( const std::string &input );
  void copy_check_utf8_test( const char * &src, char * &dest );
  void sanitize_unicode_test( std::stringstream &sout, const std::string& text );
#endif
}//namespace D3SpectrumExport
#endif //D3SpectrumExport
