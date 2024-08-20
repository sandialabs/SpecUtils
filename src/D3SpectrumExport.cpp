/**
 SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 Copyright (C) 2016 William Johnson
 
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

#include "SpecUtils_config.h"

#include <iostream>

#include <cmath>
#include <stack>
#include <limits>
#include <sstream>
#include <fstream>
#include <cstddef>
#include <iomanip>
#include <streambuf>

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_print.hpp"
#include "rapidxml/rapidxml_utils.hpp"

#include "SpecUtils/SpecFile.h"
#include "SpecUtils/DateTime.h"
#include "SpecUtils/StringAlgo.h"
#include "SpecUtils/Filesystem.h"
#include "D3SpectrumExportResources.h"
#include "SpecUtils/D3SpectrumExport.h"
#include "SpecUtils/EnergyCalibration.h"


using namespace std;

namespace
{
  //anonymous namespace for functions to help parse D3.js HTML files, that wont be
  //  useful outside of this file
#if( SpecUtils_D3_SUPPORT_FILE_STATIC )
  const unsigned char * const ns_libJsFiles[] = {
    D3_MIN_JS // D3.js library
  };//ns_libJsFiles[]
  
  const unsigned char * const ns_jsFiles[] = {
    SPECTRUM_CHART_D3_JS // For chart interactions, animations, etc.
  };//ns_jsFiles[]
  
  const unsigned char * const ns_cssFiles[] = {
    SPECTRUM_CHART_D3_CSS  // For spectrum stylesheet
  };//D3SpectrumChartOptions::cssFiles[]
#else
  string file_to_string( const std::string &filename )
  {
#ifdef _WIN32
    const std::wstring wfilename = SpecUtils::convert_from_utf8_to_utf16(filename);
    std::ifstream t( wfilename.c_str() );
#else
    std::ifstream t( filename.c_str() );
#endif
    
    if( !t.is_open() )
      throw runtime_error( "file_to_string: Failed to open '" + string(filename) + "'" );
    
    return std::string((std::istreambuf_iterator<char>(t)),
                       std::istreambuf_iterator<char>());
  }
#endif
  
  const char * SPECTRUM_CHART_SETUP_JS = "var ondatachange=function(e,t,c){\"data\"===e.value?t.setData(c,!0):t.setData(null,!0)},onyscalechange=function(e,t){var c=e.value;\"lin\"===c?t.setLinearY():\"log\"===c?t.setLogY():\"sqrt\"===c&&t.setSqrtY()},ongridychange=function(e,t){t.setGridY(e.checked)},ongridxchange=function(e,t){t.setGridX(e.checked)},onrefgammachange=function(e,t,c){console.log(c);for(var n=[],o=0;o<e.options.length;o++){var s=e.options[o];if(s.selected){var a=-1;c.forEach(function(e,t){e&&e.parent&&e.parent===s.value&&(a=t)}),-1!==a&&n.push(c[a])}}t.setReferenceLines(n)},setShowMouseStats=function(e,t){t.setShowMouseStats(e.checked)},setCompactXAxis=function(e,t){t.setCompactXAxis(e.checked)},setAdjustYAxisPadding=function(e,t){t.setAdjustYAxisPadding(e.checked,e.checked?5:60)},setWheelScrollYAxis=function(e,t){t.setWheelScrollYAxis(e.checked)},setShowAnimation=function(e,t){t.setShowAnimation(e.checked)},setAnimationDuration=function(e,t){t.setAnimationDuration(e)},setShowLegend=function(e,t){t.setShowLegend(e.checked)},setShowUserLabels=function(e,t){t.setShowUserLabels(e.checked)},setShowPeakLabels=function(e,t){t.setShowPeakLabels(e.checked)},setShowNuclideNames=function(e,t){t.setShowNuclideNames(e.checked)},setShowNuclideEnergies=function(e,t){t.setShowNuclideEnergies(e.checked)},setComptonEdge=function(e,t){t.setComptonEdge(e.checked)},setComptonPeaks=function(e,t){t.setComptonPeaks(e.checked)},setComptonPeakAngle=function(e,t){t.setComptonPeakAngle(e)},setEscapePeaks=function(e,t){t.setEscapePeaks(e.checked)},setSumPeaks=function(e,t){t.setSumPeaks(e.checked)},showForegroundPeaks=function(e,t){t.setShowPeaks(0,e.checked)},showTitle=function(e,t){t.setTitle(e.checked?\"Simple Chart\":null)},setXRangeArrows=function(e,t){t.setXRangeArrows(e.checked)},setShowXAxisSliderChart=function(e,t){t.setShowXAxisSliderChart(e.checked)},setShowSpectrumScaleFactorWidget=function(e,t){t.setShowSpectrumScaleFactorWidget(e.checked)},setBackgroundSubtract=function(e,t){t.setBackgroundSubtract(e.checked)};";
  
  //Taken from the rapidxml.hpp that Wt uses
  template <class Ch>
  void copy_check_utf8( const Ch *& src, Ch *& dest )
  {
    // skip entire UTF-8 encoded characters at once,
    // checking their validity based on
    // http://www.dwheeler.com/secure-programs/Secure-Programs-HOWTO/character-encoding.html (5.9.4 column 3)
    
    assert( src );
    size_t src_len;
    for( src_len = 0; src[src_len] && src_len < 4; ++src_len )
    {
    }
    assert( src_len );
    if( !src_len )
      return;
    
    
    unsigned length = 1;
    bool legal = false;
    if ((unsigned char)src[0] <= 0x7F)
    {
      unsigned char c = src[0];
      if (c == 0x09 || c == 0x0A || c == 0x0D || c >= 0x20)
        legal = true;
    }else if( ((unsigned char)src[0] >= 0xF0) && (src_len >= 4) )
    {
      length = 4;
      
      if ((
           // F0 90-BF 80-BF 80-BF
           ((unsigned char)src[0] == 0xF0) &&
           (0x90 <= (unsigned char)src[1] &&
            (unsigned char)src[1] <= 0xBF) &&
           (0x80 <= (unsigned char)src[2] &&
            (unsigned char)src[2] <= 0xBF) &&
           (0x80 <= (unsigned char)src[3] &&
            (unsigned char)src[3] <= 0xBF)
           ) ||
          (
           // F1-F3 80-BF 80-BF 80-BF
           (0xF1 <= (unsigned char)src[0] &&
            (unsigned char)src[0] <= 0xF3) &&
           (0x80 <= (unsigned char)src[1] &&
            (unsigned char)src[1] <= 0xBF) &&
           (0x80 <= (unsigned char)src[2] &&
            (unsigned char)src[2] <= 0xBF) &&
           (0x80 <= (unsigned char)src[3] &&
            (unsigned char)src[3] <= 0xBF)
           ))
        legal = true;
      
    }else if( ((unsigned char)src[0] >= 0xE0) && (src_len >= 3) )
    {
      length = 3;
      
      if ((
           // E0 A0*-BF 80-BF
           ((unsigned char)src[0] == 0xE0) &&
           (0xA0 <= (unsigned char)src[1] &&
            (unsigned char)src[1] <= 0xBF) &&
           (0x80 <= (unsigned char)src[2] &&
            (unsigned char)src[2] <= 0xBF)
           ) ||
          (
           // E1-EF 80-BF 80-BF
           (0xE1 <= (unsigned char)src[0] &&
            (unsigned char)src[0] <= 0xF1) &&
           (0x80 <= (unsigned char)src[1] &&
            (unsigned char)src[1] <= 0xBF) &&
           (0x80 <= (unsigned char)src[2] &&
            (unsigned char)src[2] <= 0xBF)
           ))
        legal = true;
      
    }else if( ((unsigned char)src[0] >= 0xC0) && (src_len >= 2) )
    {
      length = 2;
      
      if (
          // C2-DF 80-BF
          (0xC2 <= (unsigned char)src[0] &&
           (unsigned char)src[0] <= 0xDF) &&
          (0x80 <= (unsigned char)src[1] &&
           (unsigned char)src[1] <= 0xBF)
          )
        legal = true;
    }
    
    if( legal )
    {
      if( dest )
      {
        if( length == 3 )
        {
          /*
           U+2028 and U+2029 may cause problems, they are line
           separators that mess up JavaScript string literals.
           We will replace them here by '\n'.
           */
          if ((unsigned char)src[0] == 0xe2 &&
              (unsigned char)src[1] == 0x80 &&
              ((unsigned char)src[2] == 0xa8 ||
               (unsigned char)src[2] == 0xa9)) {
                *dest++ = '\n';
                src += length;
              } else
                for (unsigned i = 0; i < length; ++i)
                  *dest++ = *src++;
        }else
        {
          for( unsigned i = 0; i < length; ++i )
            *dest++ = *src++;
        }
      }else
      {
        src += length;
      }
    }else  //if( legal )
    {
      if( dest )
      {
        if( length >= 3 )
        { 
          /* insert U+FFFD, the replacement character */
          *dest++ = (Ch)0xef;
          *dest++ = (Ch)0xbf;
          *dest++ = (Ch)0xbd;
          src += length;
        }else
        {
          for (unsigned i = 0; i < length; ++i)
          {
            *dest++ = '?';
            src++;
          }
        }
      }else
      {
        //const Ch *problem_src = src;
        src += length;
        throw runtime_error( "Invalid UTF-8 sequence" /* + const_cast<Ch *>(problem_src)*/ );
      }//if( dest )
    }//if( legal ) / else
  }//void copy_check_utf8
  
  void sanitize_unicode( stringstream &sout, const std::string& text )
  {
    char buf[4] = {'\0'};
    
    for (const char *c = text.c_str(); *c;) {
      assert( c <= (text.c_str() + text.size()) );
      char *b = buf;
      // but copy_check_utf8() does not declare the following ranges illegal:
      //  U+D800-U+DFFF
      //  U+FFFE-U+FFFF
      copy_check_utf8<char>(c, b);
      assert( c <= (text.c_str() + text.size()) );
      assert( (b - buf) <= 4 );
      for (char *i = buf; i < b; ++i)
        sout << *i;
    }
  }
  
  
  /// This function is probably pretty specialized for just legend text, and is hugely inefficient
  string escape_text( const string &input )
  {
    stringstream sout;
    sanitize_unicode( sout, input );
    
    //ToDo: implement, or call out to EscapeOStream.C/.h ...
    string answer = sout.str();
    
    //Get rid of spaces and newlines on either side of txt
    SpecUtils::trim( answer );
    
    // Now keep html or JS content from being injected into legend titles.
    //  Note that this is probable a incomplete list of things to replace, and is also pretty
    //  inefficient - it also misses things like escaped quotes and stuff you may want to go through
    const vector<pair<char,const char *>> replacements = {
      { '&', "&amp;" },
      { '<', "&lt;" },
      { '>', "&gt;" },
      //{ '\'', "\\'" },
      //{ '\"', "\\\"" },
      { '\"', "&#34;" },
      { '\'', "&#39;" },
      //{ '\n', "<br />" },
      { '\n', " " },
      { '\r', " " },
      { '\t', " " },
      { '\\', "\\\\" },
      //{ '\n', "\\n" },
      //{ '\r', "\\r" },
      //{ '\t', "\\t" }
    };//replacements
    
    
    for( const auto &p : replacements )
    {
      char pattern[2] = { p.first, '\0' };
      SpecUtils::ireplace_all( answer, pattern, p.second );
    }
    
    return answer;
  }
  
}//namespace


namespace D3SpectrumExport
{
#if( SpecUtils_BUILD_FUZZING_TESTS )
  std::string escape_text_test( const std::string &input )
  {
    return escape_text( input );
  }
  
  void copy_check_utf8_test( const char *& src, char *& dest )
  {
    copy_check_utf8( src, dest );
  }
  
  void sanitize_unicode_test( std::stringstream &sout, const std::string& text )
  {
    sanitize_unicode( sout, text );
  }
#endif
  
  
#if( SpecUtils_D3_SUPPORT_FILE_STATIC )
  const unsigned char *d3_js(){ return D3_MIN_JS; }
  const unsigned char *spectrum_chart_d3_js(){ return SPECTRUM_CHART_D3_JS; }
  const unsigned char *spectrum_char_d3_css(){ return SPECTRUM_CHART_D3_CSS; }
#else
  const char *d3_js_filename(){ return D3_MIN_JS_FILENAME; }
  const char *spectrum_chart_d3_js_filename(){ return SPECTRUM_CHART_D3_JS_FILENAME; }
  const char *spectrum_chart_d3_css_filename(){ return SPECTRUM_CHART_D3_CSS_FILENAME; }
#endif
  
  const char *spectrum_chart_setup_js(){ return SPECTRUM_CHART_SETUP_JS; }
  
  D3SpectrumOptions::D3SpectrumOptions()
  :  peaks_json( "" ),
     line_color( "black" ),
     peak_color( "blue" ),
     display_scale_factor( 1.0 ),
     spectrum_type( SpecUtils::SpectrumType::Foreground )
  {
  }

D3SpectrumChartOptions::D3SpectrumChartOptions(  string title,
          string xAxisTitle, string yAxisTitle,
          string dataTitle,
          bool useLogYAxis,
          bool showVerticalGridLines, bool showHorizontalGridLines,
          bool legendEnabled,
          bool compactXAxis,
          bool showPeakUserLabels, bool showPeakEnergyLabels, bool showPeakNuclideLabels, bool showPeakNuclideEnergyLabels,
          bool showEscapePeakMarker, bool showComptonPeakMarker, bool showComptonEdgeMarker, bool showSumPeakMarker,
          bool backgroundSubtract,
          float xMin, float xMax,
          std::map<std::string,std::string> refernce_lines_json
  )
: m_title( title ),
  m_xAxisTitle( xAxisTitle ), m_yAxisTitle( yAxisTitle ),
  m_dataTitle( dataTitle ),
  m_useLogYAxis( useLogYAxis ),
  m_showVerticalGridLines( showVerticalGridLines ),
  m_showHorizontalGridLines( showHorizontalGridLines ),
  m_legendEnabled( legendEnabled ),
  m_compactXAxis( compactXAxis ),
  m_showPeakUserLabels( showPeakUserLabels ),
  m_showPeakEnergyLabels( showPeakEnergyLabels ),
  m_showPeakNuclideLabels( showPeakNuclideLabels ),
  m_showPeakNuclideEnergyLabels( showPeakNuclideEnergyLabels ),
  m_showEscapePeakMarker( showEscapePeakMarker ),
  m_showComptonPeakMarker( showComptonPeakMarker ),
  m_showComptonEdgeMarker( showComptonEdgeMarker ),
  m_showSumPeakMarker( showSumPeakMarker ),
  m_backgroundSubtract( backgroundSubtract ),
  m_allowDragRoiExtent( false ),
  m_xMin( xMin ), m_xMax( xMax ),
  m_reference_lines_json( refernce_lines_json )
{
}//D3SpectrumChartOptions contructor


D3SpectrumChartOptions::D3SpectrumChartOptions()
: m_title( "" ),
  m_xAxisTitle( "Energy (keV)" ),
  m_yAxisTitle( "Counts" ),
  m_dataTitle( "" ),
  m_useLogYAxis( true ),
  m_showVerticalGridLines( false ),
  m_showHorizontalGridLines( false ),
  m_legendEnabled( true ),
  m_compactXAxis( false ),
  m_showPeakUserLabels( false ),
  m_showPeakEnergyLabels( false ),
  m_showPeakNuclideLabels( false ),
  m_showPeakNuclideEnergyLabels( false ),
  m_showEscapePeakMarker( false ),
  m_showComptonPeakMarker( false ),
  m_showComptonEdgeMarker( false ),
  m_showSumPeakMarker( false ),
  m_backgroundSubtract( false ),
  m_allowDragRoiExtent( false ),
  m_xMin( 0.0 ), m_xMax( 0.0 )
{
}


  bool write_html_page_header( std::ostream &ostr, const std::string &title )
  {
    const char *endline = "\r\n";
    
    ostr << "<!DOCTYPE html><html>" << endline;
    ostr << "<head>" << endline;
    ostr << "<title>" << title << "</title>" << endline;
    
    
#if( SpecUtils_D3_SUPPORT_FILE_STATIC )
    for (const unsigned char * const libJsFile : ns_libJsFiles) { // output JS libraries
      ostr << "<script>" << libJsFile << "</script>" << endline;
    }
    
    for (const unsigned char * const jsFile : ns_jsFiles) { // output support JS files
      ostr << "<script>" << jsFile << "</script>" << endline;
    }
    
    ostr << "<script>" << SPECTRUM_CHART_SETUP_JS << "</script>" << endline;
    
    for (const unsigned char * const cssFile : ns_cssFiles) { // output CSS stylesheets
      ostr << "<style>" << cssFile << "</style>" << endline;
    }
    
#else
    //could also:
    //ostr << "include(\"" << D3_MIN_JS_FILENAME << "\");" << endline;
    using SpecUtils::append_path;
    const std::string basdir = D3_SCRIPT_RUNTIME_DIR;
    
    ostr << "<script>" << file_to_string( append_path(basdir, D3_MIN_JS_FILENAME) ) << "</script>" << endline;
    
    ostr << "<script>" << file_to_string( append_path(basdir, SPECTRUM_CHART_D3_JS_FILENAME) ) << "</script>" << endline;
    ostr << "<script>" << SPECTRUM_CHART_SETUP_JS << "</script>" << endline;
    ostr << "<style>" << file_to_string( append_path(basdir, SPECTRUM_CHART_D3_CSS_FILENAME) ) << "</style>" << endline;
    // TODO: add cutsom style/color here
    
    //ostr << "<link rel=\"stylesheet\" type=\"text/css\" href=\"" << SPECTRUM_CHART_D3_CSS_FILENAME << "\">" << endline;
#endif
    ostr << "</head>" << endline;
    
    return ostr.good();
  }//write_html_page_header

  
  bool write_js_for_chart( std::ostream &ostr, const std::string &div_name,
                           const std::string &chart_title,
                           const std::string &x_axis_title,
                           const std::string &y_axis_title  )
  {
    ostr << "var spec_chart_" << div_name
    << " = new SpectrumChartD3('" << div_name << "', {"
    << "'title': '" << chart_title << "'"
    << ", 'xlabel':'" << x_axis_title
    << "', 'ylabel':'" << y_axis_title << "'"
    << "});\r\n";
    return ostr.good();
  }//write_js_for_chart(...)
  

  
  
  bool write_and_set_data_for_chart( std::ostream &ostr, const std::string &div_name,
                            const std::vector< std::pair<const SpecUtils::Measurement *,D3SpectrumOptions> > &measurements )
  {
    const char *endline = "\r\n";
    
    ostr << endline << "var data_" << div_name << " = {" << endline;
    
    // TODO: std::localtime is not necessarily thread safe; there is a localtime_s, but its not clear how widely available it is; should make this thread-safe
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    ostr << "\"updateTime\": \"" << std::put_time(std::localtime(&in_time_t), "%Y%m%dT%H%M%S") << "\"," << endline;
    
    ostr << "\"spectra\": [" << endline;
    
    // Christian [20180301]: Assign an ID to each spectrum, as well as a background ID to signify which background spectrum is associated with this one.
    //  Note that background spectra do not have a background ID property, since they are already a background spectrum.
    //  When a background spectrum is found, all foreground spectrums defined before it in the vector is assigned with that background
    //  ie. [Foreground A, Foregorund B, Background C, Foreground D, Background E, ...]
    //    Foreground A, B -> Background C
    //    Foreground D -> Background E
    std::map<size_t,int> backgroundIDs;
    std::stack<size_t> foregroundIDs;
    for ( size_t id = 0; id < measurements.size(); ++id )
    {
      if( !measurements[id].first )
        continue;
      if (measurements[id].second.spectrum_type == SpecUtils::SpectrumType::Background) {
        while (!foregroundIDs.empty()) {
          size_t foregroundID = foregroundIDs.top();
          backgroundIDs[foregroundID] = static_cast<int>( id );
          foregroundIDs.pop();
        }
      } else {
        backgroundIDs[id] = -1; // Set to -1, meaning no background spectrum assigned
        foregroundIDs.push(id);
      } //if (measurements[id].second.spectrum_type == SpecUtils::SpectrumType::Background)
    }//for( size_t i = 0; i < measurements.size(); ++i )
    
    for( size_t i = 0; i < measurements.size(); ++i )
    {
      if( !measurements[i].first )
        continue;
      if( i )
        ostr << "," << endline;
      write_spectrum_data_js( ostr, *measurements[i].first, measurements[i].second, i, backgroundIDs[i] );
    }//for( size_t i = 0; i < measurements.size(); ++i )
    
    ostr << endline << "]" << endline;
    ostr << "};" << endline;
    
    ostr << "spec_chart_" << div_name << ".setData(data_" << div_name << ");" << endline;
    
    return ostr.good();
  }//write_and_set_data_for_chart
  

  bool write_set_options_for_chart( std::ostream &ostr, const std::string &div_name, const D3SpectrumChartOptions &options )
  {
    const char *endline = "\r\n";
    const string graph = "spec_chart_" + div_name;
    
    if( fabs(options.m_xMin - options.m_xMax) < std::numeric_limits<double>::epsilon() && options.m_xMax > options.m_xMin )
      ostr << graph << ".setXRange(" << options.m_xMin << "," << options.m_xMax << ");" << endline;
    
    if (options.m_useLogYAxis) ostr << graph << ".setLogY();" << endline; // Set the y-axis scale
    else  ostr << graph << ".setLinearY();" << endline;
    
    if (options.m_showVerticalGridLines) ostr << graph << ".setGridX(true);" << endline;  // Set up grid lines for x-axis
    if (options.m_showHorizontalGridLines) ostr << graph << ".setGridY(true);" << endline;  // Set up grid lines for y-axis
    
    if (options.m_legendEnabled) ostr << graph << ".setShowLegend(true);" << endline; // Set up display for legend
    if (options.m_compactXAxis) ostr << graph << ".setCompactXAxis(true);" << endline; // Set up compact axis
    
    if (options.m_showPeakUserLabels) ostr << graph << ".setShowUserLabels(true);" << endline; // Set up user labels for peaks
    if (options.m_showPeakEnergyLabels) ostr << graph << ".setShowPeakLabels(true);" << endline; // Set up energy labels for peaks
    if (options.m_showPeakNuclideLabels) ostr << graph << ".setShowNuclideNames(true);" << endline; // Set up nuclide labels for peaks
    if (options.m_showPeakNuclideEnergyLabels) ostr << graph << ".setShowNuclideEnergies(true);" << endline; // Set up nuclide energy labels for peaks
    
    if (options.m_backgroundSubtract) ostr << graph << ".setBackgroundSubtract(true);" << endline;  // Set background subtract
    
    if (!options.m_allowDragRoiExtent) ostr << graph << ".setAllowDragRoiExtent(false);" << endline;  // Set allowing to drag ROI edges
    
    // Set up feature markers
    if (options.m_showEscapePeakMarker) ostr << graph << ".setEscapePeaks(true);" << endline;
    if (options.m_showComptonPeakMarker) ostr << graph << ".setComptonPeaks(true);" << endline;
    if (options.m_showComptonEdgeMarker) ostr << graph << ".setComptonEdge(true);" << endline;
    if (options.m_showSumPeakMarker) ostr << graph << ".setSumPeaks(true);" << endline;

    string reference_json;
    if( options.m_reference_lines_json.size() )
    {
      reference_json += "[";
      size_t numberOfLines = 0;
      for( map<string,string>::const_iterator iter = options.m_reference_lines_json.begin();
          iter != options.m_reference_lines_json.end(); ++iter )
      {
        if( numberOfLines )
          reference_json += ",";
        reference_json += iter->second;
        ++numberOfLines;
      }
      
      reference_json += "];";
      ostr << "var reference_lines_" << div_name << " = " << reference_json << ";" << endline; // add reference gamma line data for spectrum
    }else
    {
      ostr << "var reference_lines_" << div_name << " = [];"  << endline; // add reference gamma line data for spectrum
    }
    
    return ostr.good();
  }//write_set_options_for_chart(...)

  
  bool write_html_display_options_for_chart( std::ostream &ostr, const std::string &div_id, const D3SpectrumChartOptions &options )
  {
    const char *endline = "\r\n";
    
    // Set up option for y-axis scale
    ostr << "<div style=\"margin-top: 10px; display: inline-block;\"><label>" << endline
    << "Y Scale:" << endline
    << "<select onchange=\"onyscalechange(this,spec_chart_" << div_id << ")\" >" << endline
    << "<option value=\"lin\" " << (!options.m_useLogYAxis ? "selected" : "") << ">Linear</option>" << endline
    << "<option value=\"log\" " << (options.m_useLogYAxis ?  "selected" : "") << ">Log</option>" << endline
    << "<option value=\"sqrt\">Sqrt</option>" << endline
    << "</select></label>" << endline << endline;
    
    // Set up options for grid lines on chart
    ostr << "<label><input type=\"checkbox\" onchange=\"ongridxchange(this,spec_chart_" << div_id << ")\" "
    << (options.m_showVerticalGridLines ?  "checked" : "") << ">Grid X</label>" << endline;
    ostr << "<label><input type=\"checkbox\" onchange=\"ongridychange(this,spec_chart_" << div_id << ")\" "
    << (options.m_showHorizontalGridLines ?  "checked" : "") << ">Grid Y</label>" << endline << endline;
    
    // Set up options for displaying which data to display
    ostr << "<label>" << endline
    << "Data to display:" << endline
    << "<select onchange=\"ondatachange(this,spec_chart_" << div_id << ",data_" << div_id << ")\">" << endline
    << "<option value=\"none\">none</option>" << endline
    << "<option value=\"data\" selected>"
    << options.m_dataTitle
    << "</option></select></label>" << endline << endline;
    
    
    // Set up options for displaying peaks/title
    ostr << "<br />"
    << "<label><input type=\"checkbox\" onchange=\"showForegroundPeaks(this,spec_chart_" << div_id << ")\" checked>Draw foreground peaks</label>" << endline
    << "<label><input type=\"checkbox\" onchange=\"alert('Peak drawing not implemented yet');\">Draw background peaks</label>" << endline
    << "<label><input type=\"checkbox\" onchange=\"alert('Peak drawing not implemented yet');\">Draw secondary peaks</label>" << endline
    << "<label><input type=\"checkbox\" onchange=\"showTitle(this,spec_chart_" << div_id << ")\" checked>Show Title</label>" <<endline << endline;
    
    // Set up option for displaying legend
    ostr << "<br />" << "<label>"
    << "<input id=\"legendoption\" type=\"checkbox\" onchange=\"setShowLegend(this,spec_chart_" << div_id << ");\" "
    << (options.m_legendEnabled ? "checked" : "") << ">Draw Legend</label>" << endline;
    
    // Set up option for compact x-axis
    ostr << "<label><input type=\"checkbox\" onchange=\"setCompactXAxis(this,spec_chart_" << div_id << ");\" "
    << (options.m_compactXAxis ? "checked" : "") << ">Compact x-axis</label>" << endline;
    
    // Set up option for mouse position statistics
    ostr << "<label><input type=\"checkbox\" onchange=\"setShowMouseStats(this,spec_chart_" << div_id << ");\" checked>Mouse Position stats</label>" << endline;
    
    // Set up option for animations
    ostr << "<label><input type=\"checkbox\" onchange=\"setShowAnimation(this,spec_chart_" << div_id << ")\">Show zoom animation with duration: "
    << "<input type=\"number\" size=3 value=\"200\" min=\"0\" id=\"animation-duration\" "
    << "oninput=\"setAnimationDuration(this.value,spec_chart_" << div_id << ");\"><label>ms</label></label>" << endline << endline;
    ostr << "<br /> ";
    
    // Set up options for peak labels
    ostr << "<label><input type=\"checkbox\" onchange=\"setShowUserLabels(this,spec_chart_" << div_id << ");\" " << (options.m_showPeakUserLabels ? "checked" : "") << ">Show User Labels</label>" << endline;
    ostr << "<label><input type=\"checkbox\" onchange=\"setShowPeakLabels(this,spec_chart_" << div_id << ");\" " << (options.m_showPeakEnergyLabels ? "checked" : "") << ">Show Peak Labels</label>" << endline;
    ostr << "<label><input type=\"checkbox\" onchange=\"setShowNuclideNames(this,spec_chart_" << div_id << ");\" " << (options.m_showPeakNuclideLabels ? "checked" : "") << ">Show Nuclide Names</label>" << endline;
    ostr << "<label><input type=\"checkbox\" onchange=\"setShowNuclideEnergies(this,spec_chart_" << div_id << ");\" " << (options.m_showPeakNuclideEnergyLabels ? "checked" : "") << ">Show Nuclide Energies</label>" << endline;
    
    ostr << endline << endline;
    ostr << "<br />" << endline
    << "<label><input type=\"checkbox\" onchange=\"setAdjustYAxisPadding(this,spec_chart_" << div_id << ");\" checked>Adjust for y-labels</label>" << endline // Set up option for adjusting y-axis padding
    << "<label><input type=\"checkbox\" onchange=\"setWheelScrollYAxis(this,spec_chart_" << div_id << ");\" checked>Scroll over y-axis zooms-y</label>" << endline << endline;  // Set up option for scrolling over y-axis
    
    ostr << "<br />" << endline
    << "<label><input type=\"checkbox\" onchange=\"setComptonEdge(this,spec_chart_" << div_id << ");\" " << (options.m_showComptonEdgeMarker ? "checked" : "")
    << ">Show compton edge</label>" << endline  // Set up option for compton edge marker
    << "<label><input type=\"checkbox\" onchange=\"setComptonPeaks(this,spec_chart_" << div_id << ");\" " << (options.m_showComptonPeakMarker ? "checked" : "")
    << ">Show compton peak energy with angle: <input type=\"number\" size=5 placeholder=\"180\" value=\"180\" max=\"180\" min=\"0\" id=\"angle-text\" oninput=\"setComptonPeakAngle(this.value,spec_chart_" << div_id << ");\"><label>degrees</label></label>" << endline // Set up option for compton peak energy marker
    << "<label><input type=\"checkbox\" onchange=\"setEscapePeaks(this,spec_chart_" << div_id << ");\" " << (options.m_showEscapePeakMarker ? "checked" : "")
    << ">Show escape peak energies</label>" << endline  // Set up option for escape peak marker
    << "<label><input type=\"checkbox\" onchange=\"setSumPeaks(this,spec_chart_" << div_id << ");\" " << (options.m_showSumPeakMarker ? "checked" : "") << ">Show sum peak energies</label>" << endline << endline;  // Set up option for sum peak marker
    
    ostr << "<br />" << endline
    << "<label><input type=\"checkbox\" onchange=\"setBackgroundSubtract(this,spec_chart_" << div_id << ")\" " << (options.m_backgroundSubtract ? "checked" : "") << ">Background subtract</label>" << endline  // Set up option for background subtract
    << "<label><input id=\"scaleroption\" type=\"checkbox\" onchange=\"setShowSpectrumScaleFactorWidget(this,spec_chart_" << div_id << ")\">Enable scale background and secondary</label>" << endline // Set up option for spectrum y-scaler widget
    << "<label><input type=\"checkbox\" onchange=\"setShowXAxisSliderChart(this,spec_chart_" << div_id << ");\">Show x-axis slider chart</label>" << endline  // Set up option for x-axis slider chart widget
    << "<label><input type=\"checkbox\" onchange=\"setXRangeArrows(this,spec_chart_" << div_id << ")\" checked>Show x-axis range continuse arrows</label>" << endline << endline; // Set up option for x-axis range arrows
    
    // Set up option for spectrum scale factor widget
    ostr << "<br />" << endline
    << endline
    << "</div>";
    
    // Set up inline div element for reference gamma selection
    ostr << "<div class=\"referenceGammaSelectDiv\"><span>Reference Gammas: </span><br />" << endline;
    
    
    if( options.m_reference_lines_json.size() )
    {
      ostr << "<select id=\"referenceGammaSelect" << div_id << "\" multiple class=\"referenceGammaSelect\" "
      " onchange=\"onrefgammachange(this,spec_chart_" << div_id << ",reference_lines_" << div_id << ")\">" << endline;
      
      for( map<string,string>::const_iterator iter = options.m_reference_lines_json.begin();
          iter != options.m_reference_lines_json.end(); ++iter )
      {
        ostr << "<option value=\"" << iter->first << "\" selected>" << iter->first << "</option>" << endline;
      }
      
      ostr << "</select>" << endline;
    }//if( options.m_reference_lines_json.size() )
    
    ostr << "</div>" << endline;

    
    return ostr.good();
  }//write_html_display_options_for_chart
  
  bool write_d3_html( std::ostream &ostr,
                      const std::vector< std::pair<const SpecUtils::Measurement *,D3SpectrumOptions> > &measurements,
                      const D3SpectrumChartOptions &options )
  {
    const char *endline = "\r\n";
    
    write_html_page_header( ostr, options.m_title );
    
    const string div_id = "chart1";
    
    ostr << "<body><div id=\"" << div_id << "\" class=\"chart\" oncontextmenu=\"return false;\";></div>" << endline;  // Adding the main chart div
    
    
    ostr << "<script>" << endline;
    
    write_js_for_chart( ostr, div_id, options.m_dataTitle, options.m_xAxisTitle, options.m_yAxisTitle );
    
    write_and_set_data_for_chart( ostr, div_id, measurements );
    
    ostr << R"delim(
    const resizeChart = function(){
      let height = window.innerHeight;
      let width = window.innerWidth;
      let el = spec_chart_chart1.chart;
      el.style.width = (width - 40) + "px";
      el.style.height = Math.max(250, Math.min(0.4*width,height-175)) + "px";
      el.style.marginLeft = "20px";
      el.style.marginRight = "20px";
      
      spec_chart_chart1.handleResize();
    };
    
    window.addEventListener('resize', resizeChart);
    )delim" << endline;
    
    write_set_options_for_chart( ostr, div_id, options );
    //todo, get rid of this next couple lines
    ostr << "spec_chart_" << div_id << ".setShowPeaks(1,false);" << endline;
    ostr << "spec_chart_" << div_id << ".setShowPeaks(2,false);" << endline;
    ostr << "resizeChart();" << endline;
    ostr << "</script>" << endline;
    
    
    write_html_display_options_for_chart( ostr, div_id, options );
    
    if( options.m_reference_lines_json.size() )
      ostr << "<script>onrefgammachange(document.getElementById('referenceGammaSelect"
           << div_id << "'),spec_chart_" << div_id << ",reference_lines_" << div_id << ");</script>" << endline;
    
    
    ostr << "</body>" << endline;
    ostr << "</html>" << endline;
    
    return !ostr.bad();
  }

  bool write_spectrum_data_js( std::ostream &ostr, const SpecUtils::Measurement &meas, const D3SpectrumOptions &options, const size_t specID, const int backgroundID )
  {
    const char *q = "\"";  // for creating valid json format
    
    ostr << "\n\t\t{\n\t\t\t" << q << "title" << q << ":";
    if( options.title.size() )
    {
      ostr << q << escape_text( options.title ) << q << ",";
    }else if( meas.title().size() )
      ostr << q << escape_text( meas.title() ) << q << ",";
    else
      ostr << "null,";
    
    // foreground id
    ostr << "\n\t\t\t" << q << "id" << q << ":" << specID << ",";
    
    // foreground's assigned background spectrum ID
    ostr << "\n\t\t\t" << q << "backgroundID" << q << ":" << backgroundID << ",";
    
    // foreground spectrum type
    ostr << "\n\t\t\t" << q << "type" << q << ":";
    switch (options.spectrum_type) {
      case SpecUtils::SpectrumType::Foreground: ostr << q << "FOREGROUND" << q; break;
      case SpecUtils::SpectrumType::Background: ostr << q << "BACKGROUND" << q; break;
      case SpecUtils::SpectrumType::SecondForeground: ostr << q << "SECONDARY" << q; break;
      default: ostr << "null"; break;
    }
    ostr << ",";
    
    // foreground spectrum peaks
    ostr << "\n\t\t\t" << q << "peaks" << q << ":";
    if( options.peaks_json.size() )
      ostr << options.peaks_json << ",";
    else
      ostr << "[],";
    
    // foreground live time
    ostr << "\n\t\t\t" << q << "liveTime" << q << ":";
    float lt = meas.live_time();
    if( lt <= 0.0f || IsInf(lt) || IsNan(lt) )
      lt = 0.0f;
    ostr << lt << ",";
    
    // foreground real time
    ostr << "\n\t\t\t" << q << "realTime" << q << ":";
    float rt = meas.real_time();
    if( rt <= 0.0f || IsInf(rt) || IsNan(rt) )
      rt = 0.0f;
    ostr << rt << ",";
    
    // foreground neutron count
    ostr << "\n\t\t\t" << q << "neutrons" << q << ":";
    if( meas.contained_neutron() )
    {
      double ns = meas.neutron_counts_sum();
      if( ns <= 0.0 || IsInf(ns) || IsNan(ns) )
        ns = 0.0;
      ostr << ns << ",neutronLiveTime:" << meas.neutron_live_time() << ",";
    }else
    {
      ostr << "null,";
    }
    
    // line color
    if( !options.line_color.empty() )
    {
      ostr << "\n\t\t\t" << q << "lineColor" << q << ":" << q
      << (options.line_color.size() ? options.line_color.c_str() : "black") << q << ",";
    }
    
    // peak color
    ostr << "\n\t\t\t" << q << "peakColor" << q << ":";
    if (options.peak_color.size())
      ostr << q << options.peak_color.c_str() << q;
    else
      ostr << "null";
    ostr << ",";
    
    // Increase precision for calibration coefficients or energy channel boundries
    const auto oldprecision = ostr.precision();
    ostr << std::setprecision(std::numeric_limits<float>::digits10 + 1);
    
    const SpecUtils::EnergyCalType caltype = meas.energy_calibration_model();
    if( (caltype == SpecUtils::EnergyCalType::Polynomial
         || caltype == SpecUtils::EnergyCalType::FullRangeFraction
         || caltype == SpecUtils::EnergyCalType::UnspecifiedUsingDefaultPolynomial )
       && meas.deviation_pairs().empty() )
    {
      std::vector<float> calcoefs = meas.calibration_coeffs();
      if( caltype == SpecUtils::EnergyCalType::FullRangeFraction )
        calcoefs = SpecUtils::fullrangefraction_coef_to_polynomial( calcoefs, meas.num_gamma_channels() );
      
      ostr << "\n\t" << q << "xeqn" << q << ": [";
      for( size_t i = 0; i < calcoefs.size(); ++i )
        ostr << (i ? "," : "") << calcoefs[i];
      ostr << "],";
    }else// if( caltype == SpecUtils::EnergyCalType::LowerChannelEdge )
    {
      // foreground x-point values
      ostr << "\n\t" << q << "x" << q << ": [";
      if( meas.num_gamma_channels() && meas.channel_energies() )
      {
        ostr << std::setprecision( static_cast<std::streamsize>(std::numeric_limits<float>::digits10 + 1) );
        const vector<float> &x  = *meas.channel_energies();
        for( size_t i = 0; i < x.size(); ++i )
        {
          ostr << (i ? "," : "") << x[i];
        }
      }
      ostr << "],";
    }//
    
    ostr << std::setprecision(static_cast<int>(oldprecision));
    
    
    // foreground y-point values
    ostr << "\n\t\t\t" << q << "y" << q << ":[";
    if( meas.num_gamma_channels() )
    {
      const vector<float> &y0 = *meas.gamma_channel_contents();
      for( size_t i = 0; i < y0.size(); ++i )
        ostr << (i ? "," : "") << ((IsNan(y0[i]) || IsInf(y0[i])) ? 0.0f : y0[i]);
    }
    ostr << "],";
    
    // foreground y-scale factor
    double sf = options.display_scale_factor;
    if( sf <= 0.0 || IsInf(sf) || IsNan(sf) )
      sf = 1.0;

    ostr << "\n\t\t\t" << q << "yScaleFactor" << q << ":" << sf;
    
    ostr << "\n\t\t}";
    
    return !ostr.bad();
  }
  
  
  
}//namespace D3SpectrumExport




