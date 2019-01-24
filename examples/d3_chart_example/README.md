This directory, d3_chart_example, contains two examples of how you might use the D3-based charting output of **SpecUtils**:

* **self_contained_example.html**: This file contains the [D3](https://d3js.org/) source code, 
  as well as the *SpectrumChartD3* JS and CSS, and the spectrum JSON, so that no other resources
  are necassary to view and interact with the spectrum in a browser.  
  This file is the result of calling: `D3SpectrumExport::write_d3_html(...)`.
* **json_example**: This example keeps the charting JavaScript, JSON, CSS, D3, HTML, etc. all in seperate
  files, similar to how you would do if you wanted to make a website served over the internet
  that displayed multiple different spectrum files.  If you plot multiple spectra files, you would
  need to serve only a single copy of all the files, except for the JSON (created using 
  `D3SpectrumExport::write_spectrum_data_js(...)` - perhaps a poorly named function) which each
  spectrum file would have their own JSON file. 