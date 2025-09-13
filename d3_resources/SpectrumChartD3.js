/* 
This is part of Cambio 2.1 program (https://hekili.ca.sandia.gov/cambio) and is licensed under the LGPL v2.1 license
   
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

   As of 20221123 this file is 10773 lines long!  It is not allowed to grow, and even with fixes
   or feature additions, we must refactor and improve code to keep LOC this size or smaller
   (e.g., things are out of hand, lets keep it from getting worse).
 
Feature TODO list (created 20160220):

  - The touch implementation is especially bad.  Getting things like pageX, or dealing with touches is really inconsistent; some time needs to be spent to go through and clean up.
  - Could probably emit back to the c++ immediately when a single finger starts touching (from none) so the context menu can be hidden immediately, or whatever
  - For the y-axis scalers, should add some padding on either side of that area
    and also make the width of each slider more than 20px if a touch device and
    also increase radius of sliderToggle.
    - The number given at bottom of scaler while adjusting is cut off on the right side
  - When changing x-range with x-axis slider chart, it constantly calls back to C++ - this should be changed so it only happens when you you stop adjusting this
    - Could make it so its emmitted only when self.sliderBoxDown changes from true to false, and similar for touch and edges of box
  - Fix intermitten issue of zooming in messes up (especially aver dragging starting from the y-axis title)
    - (was this maybe fixed by ensuring getting animation frames is terminated?)
  - Add statistical error bars
  - Add support for deviation pairs with polynomial energy calibration (instead of just lower edge energy)
  - Customize mouse point to zoom-in/zoom-out where appropriate
  - Optimize frequency of rebinning of data (prevent extra rebinned data from being drawn)
  - Move to using D3 v6 with modules to minimize code sizes and such.
  - For peak labels, make a border around text (maybe add some padding, maybe make border same color as peak), make background translucent so you can read the text even if in a cluttered area.
*/

SpectrumChartD3 = function(elem, options) {
  var self = this;

  this.chart = typeof elem === 'string' ? document.getElementById(elem) : elem; 

  // Apply no-select class to prevent text selection during chart interactions
  d3.select(this.chart).classed("spectrum-chart-no-select", true);

  this.cx = this.chart.clientWidth;
  this.cy = this.chart.clientHeight;

  this.options = options || {}; 
  
  /* Validates a option is the correct type, and if not, or its not specified, will use default */
  let validateOptionsFromConfig = function(config) {
    for (var i = 0; i < config.length; i++) {
      var opt = config[i];
      var val = self.options[opt.name];
      
      switch(opt.type) {
        case 'boolean':
          if (typeof val !== 'boolean')
            self.options[opt.name] = opt.default;
          break;
          
        case 'number':
          if (typeof val !== 'number'
              || (typeof opt.min !== 'undefined' && val < opt.min) || (typeof opt.max !== 'undefined' && val > opt.max) || (opt.noNaN && isNaN(val)))
            self.options[opt.name] = opt.default;
          break;
          
        case 'string':
          if ((opt.default !== null && typeof val !== 'string') ||  (opt.allowed && opt.allowed.indexOf(val) < 0))
            self.options[opt.name] = opt.default;
          break;
          
        case 'custom': // For complex validation logic that can't be standardized
          if (opt.validator && !opt.validator(val))
            self.options[opt.name] = opt.default;
          break;
      }
    }
  };
  
  /* Comprehensive option validation - all options configured in one place */
  validateOptionsFromConfig([
    // Basic UI options
    { name: 'yscale', type: 'string', default: 'lin', allowed: ['lin', 'log', 'sqrt'] },
    { name: 'gridx', type: 'boolean', default: false },
    { name: 'gridy', type: 'boolean', default: false },
    { name: 'compactXAxis', type: 'boolean', default: false },
    { name: 'adjustYAxisPadding', type: 'boolean', default: true },
    { name: 'wheelScrollYAxis', type: 'boolean', default: true },
    { name: 'noYAxisNumbers', type: 'boolean', default: false },
    
    // Animation options
    { name: 'animationDuration', type: 'number', default: 1000, min: 0 },
    { name: 'showXAxisSliderChart', type: 'boolean', default: false },
    { name: 'compactXAxisWithSliderChart', type: 'boolean', default: true },
    { name: 'sliderChartHeightFraction', type: 'number', default: 0.1, min: 0, max: 0.75 },
    
    // Display options
    { name: 'showUserLabels', type: 'boolean', default: false },
    { name: 'showPeakLabels', type: 'boolean', default: false },
    { name: 'showNuclideNames', type: 'boolean', default: false },
    { name: 'showNuclideEnergies', type: 'boolean', default: false },
    { name: 'showLegend', type: 'boolean', default: true },
    { name: 'scaleBackgroundSecondary', type: 'boolean', default: false },
    { name: 'noEventsToServer', type: 'boolean', default: false },
    
    // Interaction options
    { name: 'doubleClickDelay', type: 'number', default: 500 },
    { name: 'showRefLineInfoForMouseOver', type: 'boolean', default: true },
    { name: 'showMouseStats', type: 'boolean', default: true },
    { name: 'showXRangeArrows', type: 'boolean', default: true },
    { name: 'allowDragRoiExtent', type: 'boolean', default: true },
    
    // Reference line options
    { name: 'refLineWidth', type: 'number', default: 1 },
    { name: 'refLineWidthHover', type: 'number', default: 2 },
    { name: 'refLineVerbosity', type: 'number', default: 0 },
    { name: 'featureLineWidth', type: 'number', default: 2 },
    
    // Peak analysis options
    { name: 'showComptonEdge', type: 'boolean', default: false },
    { name: 'showComptonPeaks', type: 'boolean', default: false },
    { name: 'comptonPeakAngle', type: 'number', default: 180, noNaN: true },
    { name: 'showEscapePeaks', type: 'boolean', default: false },
    { name: 'showSumPeaks', type: 'boolean', default: false },
    { name: 'backgroundSubtract', type: 'boolean', default: false },
    { name: 'showSliderCloseBtn', type: 'boolean', default: false },
    
    // Spectrum display options
    { name: 'spectrumLineWidth', type: 'number', default: 1.0, min: 0, max: 15 },
    
    // ROI options
    { name: 'roiDragLineExtent', type: 'number', default: 20 },
    { name: 'roiDragWidth', type: 'number', default: 10 },
    
    // Label options
    { name: 'peakLabelSize', type: 'string', default: null },
    { name: 'peakLabelRotation', type: 'number', default: 0 },
    { name: 'logYAxisMin', type: 'number', default: 0.1, min: 0 }
  ]);
  
  // Special handling for showAnimation which depends on animationDuration
  this.options.showAnimation = (typeof options.showAnimation == 'boolean' && this.options.animationDuration > 0) ? options.showAnimation : false;

  // Hard-coded options that don't need validation
  this.options.allowPeakFit = false;
  this.options.refLineTopPad = 30;
  this.options.maxScaleFactor = 10;
  
  // Fixed option values
  self.options.logYFracTop = 0.05;
  self.options.logYFracBottom = 0.025;
  self.options.linYFracTop = 0.1;
  self.options.linYFracBottom = 0.1;
  self.options.sqrtYFracTop = 0.1;
  self.options.sqrtYFracBottom = 0.1;
  
  // Set which spectrums to draw peaks for
  this.options.drawPeaksFor = {
    FOREGROUND: true,
    BACKGROUND: true,
    SECONDARY: false,
  };

  self.setLocalizations( {}, true );//Set default localization strings

  this.padding = {
     "top":  5,
     "titlePad" : 0,
     "right":   10,
     "bottom": 5,
     "xTitlePad": 5, // vertical padding between y-axis numbers (non-compact) and the title, or for compact just the height down from axis; TODO: make this more consistent/better.
     "left":     5,  //The distance between the left of chart, and y-axis text
     "labelPad": 5,  //The distance between y-axis title, and the y-axis count text
     "title":    23, //Chart title distance from top, if present
     "label":    8,
     "sliderChart":    8,
  };
  
  this.padding.leftComputed = this.padding.left + this.padding.label + this.padding.labelPad;
  this.padding.topComputed = this.padding.top + this.padding.titlePad + 15;
  this.padding.bottomComputed = this.padding.bottom +  this.padding.xTitlePad + 15;
  
  this.size = {
    "width":  Math.max(0, this.cx - this.padding.leftComputed - this.padding.right),
    "height": Math.max(0, this.cy - this.padding.topComputed  - this.padding.bottomComputed),
    "sliderChartHeight": 0,
    "sliderChartWidth": 0,
  };

  /**
   * Added by Christian 20180215
   * Used to differentiate between different types of spectra, and for future additional usage
   * Each spectrum object should have an associated type with it, which we can compare using this enum.
   */
  this.spectrumTypes = {
    FOREGROUND: 'FOREGROUND',
    BACKGROUND: 'BACKGROUND',
    SECONDARY: 'SECONDARY',
  };


  /*When dragging the plot, both dragging_plot and zooming_plot will be */
  /*  true.  When only zooming (e.g. mouse wheel), then only zooming_plot */
  /*  will be true. */
  this.dragging_plot = false;
  this.zooming_plot = false;

  this.refLines = [];

  /* x-scale */
  this.xScale = d3.scale.linear()
      .domain(this.options.xScaleDomain ? this.options.xScaleDomain : [0, 3000])
      .range([0, this.size.width]);


  /* drag x-axis logic */
  this.xaxisdown = null;

  if( this.options.yscale === "log" ) {
    this.yScale = d3.scale.log().clamp(true).domain([0, 100]).nice().range([1, this.size.height]).nice();
  } else if( this.options.yscale === "sqrt" ) {
    this.yScale = d3.scale.pow().exponent(0.5).domain([0, 100]).range([0, this.size.height]);
  } else {
    this.yScale = d3.scale.linear().domain([0, 100]).nice().range([0, this.size.height]).nice();
  }
  
  if( this.yGrid )
    this.yGrid.scale( this.yScale );
      
  this.yaxisdown = Math.NaN;

  /* Finds distance between two points */
  this.dist = function (a, b) {
    return Math.sqrt(Math.pow(a[0]-b[0],2) + Math.pow(a[1]-b[1],2));
  };

  this.min_max_x_values = function() {
    if (!self.rawData || !self.rawData.spectra || !self.rawData.spectra.length)
      return [-1,-1];

    var min = null, max = null;
    self.rawData.spectra.forEach(function(spectrum) {
      if (!spectrum.x)
        return;

      if (min == null || spectrum.x[0] < min) min = spectrum.x[0];
      if (max == null || spectrum.x[spectrum.x.length-1] > max) max = spectrum.x[spectrum.x.length-1];
    });

    return [min,max];
  };

  this.displayed_raw_start = function(spectrum){
    if( !self.rawData || !self.rawData.spectra || !self.rawData.spectra.length )
      return -1;
    const xstart = self.xScale.domain()[0];
    const bisector = d3.bisector(function(d, x) { return d - x; });
    return bisector.left(spectrum ? spectrum.x : self.rawData.spectra[0].x, xstart); /* use foreground by default */
  };

  this.displayed_raw_end = function(spectrum){
    if( !self.rawData || !self.rawData.spectra || !self.rawData.spectra.length)
      return -1;
    const xend = self.xScale.domain()[1];
    const bisector = d3.bisector(function(d, x) { return d - x; });
    return bisector.right(spectrum ? spectrum.x : self.rawData.spectra[0].x, xend);  /* use foreground by default */
  };



  this.displayed_start = function(spectrum){
    if( !spectrum || !spectrum.points || !spectrum.points.length )
      return -1;
    var xstart = self.xScale.domain()[0];
    var i = 0;
    while( i < spectrum.points.length && spectrum.points[i].x <= xstart )
      ++i;
    return Math.max(i - 1,0);
  };

  this.displayed_end = function(spectrum){
    if( !spectrum || !spectrum.points || !spectrum.points.length )
      return -1;
    var xend = self.xScale.domain()[1];
    var i = spectrum.points.length - 1;
    while( i > 0 && spectrum.points[i].x >= xend )
      --i;
    return Math.min(i + 1, spectrum.points.length);
  };

  this.rebinFactor = 1;
  this.firstRaw = this.lastRaw = -1;
  this.do_rebin();

  this.setYAxisDomain = function(){
    if( !isNaN(self.yaxisdown) )
      return;
    var yaxisDomain = self.getYAxisDomain(),
        y1 = yaxisDomain[0],
        y0 = yaxisDomain[1];
    this.yScale.domain([y1, y0]);
  }
  this.setYAxisDomain();


  /* drag y-axis logic */
  this.yaxisdown = Math.NaN;

  this.dragged = this.selected = null;

  // this.vis will be a <g> element that almost everything else will be appended to
  this.vis = d3.select(this.chart).append("svg")
      .attr("width",  this.cx)
      .attr("height", this.cy)
      .attr("class", "SpectrumChartD3 InterSpecD3Chart" )
      .append("g")
      .attr("transform", "translate(" + this.padding.leftComputed + "," + this.padding.topComputed + ")");

  this.plot = this.vis.append("rect")
      .attr("width", this.size.width)
      .attr("height", this.size.height)
      .attr("id", "chartarea"+this.chart.id )
      .attr("class", "chartarea" )
      //.style("fill", "#EEEEEE")
      ;
      /*.attr("pointer-events", "all"); */

  this.svg = d3.select(self.chart).select('svg');

  /*
  Chart vs. Vis vs. Document Body
  ___________________________________________
  | _______________________________________ |
  ||  ___________________________________  ||
  || |                                   | ||
  || |        VIS <g> (SPECTRUM)         | ||
  || |                                   | ||
  || |___________________________________| ||
  ||                                       ||
  ||                  CHART <div>          ||
  ||_______________________________________||
  |                                         |
  |                                         |
  |                 <BODY>                  |
  |                                         |
  |                                         |
  |_________________________________________|
  */


  this.zoom = d3.behavior.zoom()
    .x(self.xScale)
    .y(self.yScale)
    .on("zoom", function(){ return false; })
    .on("zoomend", function(){ return false; })
    ;

  /* Vis interactions */
  this.vis
    .call(this.zoom)
    //.on("click", function(){ console.log( 'Single CLick!' ); } )
    //.on("dblclick", function(){ console.log( 'Double CLick!' ); } )  //ToDo: Use 'dblclick' signal rahter than custom one
    .on("mousedown", self.handleVisMouseDown())
    .on("mouseup", self.handleVisMouseUp())
    .on("wheel", self.handleVisWheel())
    .on("touchstart", self.handleVisTouchStart())
    .on("touchmove", self.handleVisTouchMove())
    .on("touchend", self.handleVisTouchEnd())
    .on("touchcancel", self.handleVisTouchCancel() )
    ;

  /* Cancel the zooms for mouse events, we'll use our own implementation for these */
  this.vis
    .on("mousedown.zoom", null)
    .on("mousemove.zoom", null)
    .on("mouseup.zoom", null)
    .on("mouseover.zoom", null)
    .on("mouseout.zoom", null)
    .on("wheel.zoom", null)
    .on("click.zoom", null)
    .on("dblclick.zoom", null)
    .on("touchstart.zoom", null)
    .on("touchmove.zoom", null)
    .on("touchend.zoom", null)
    .on("*.zoom", null)
    .on(".zoom",  null )
    ;
  /// @TODO triggering the cancel events on document.body and window is probably a bit agressive; could probably do this for just this.vis + on leave events
  d3.select(document.body)
    .on("mouseup.chart" + this.chart.id, self.handleCancelAllMouseEvents() )
  d3.select(window)
    .on("mouseup.chart" + this.chart.id, self.handleCancelAllMouseEvents())
    .on("mousemove.chart" + this.chart.id, function() {
        if( d3.event && (self.sliderBoxDown || self.leftDragRegionDown || self.rightDragRegionDown || self.currentlyAdjustingSpectrumScale) ) {
          //d3.event.preventDefault();
          //d3.event.stopPropagation();
        }
      });



  /*  Chart Interactions */
  d3.select(this.chart)
    .on("mousemove", self.handleChartMouseMove())
    .on("mouseleave", self.handleChartMouseLeave())
    .on("mouseup", self.handleChartMouseUp())
    .on("wheel", self.handleChartWheel())
    .on("touchstart", self.handleChartTouchStart())
    .on("touchend", self.handleChartTouchEnd())
    ;
  
  // Handle window blur (alt-tab away) to clean up mouse interactions
  d3.select(window).on("blur.chart" + this.chart.id, self.handleChartMouseLeave());

  /*
  To allow markers to be updated while mouse is outside of the chart, but still inside the visual.
  */
  this.yAxis = d3.svg.axis().scale(this.yScale)
   .orient("left")
   .innerTickSize(7)
   .outerTickSize(1)
   .ticks(0);

  this.yAxisBody = this.vis.append("g")
    .attr("class", "yaxis axis" )
    .attr("transform", "translate(0,0)")
    .call(this.yAxis)
    .style("cursor", "ns-resize")
    .on("mousedown.drag",  self.yaxisDrag())
    .on("touchstart.drag", self.yaxisDrag())
    .on("mouseover", function() { 
      if( d3.select(d3.event.target).node().nodeName === "text" )
        d3.select(d3.event.target).style("font-weight", "bold"); 
     })
    .on("mouseout",  function() { 
      d3.select(this).selectAll('text').style("font-weight", null);
     })
    .on("dblclick", function(){ //toggle between linear and log when user double-clicks axis
      d3.event.preventDefault();
      d3.event.stopPropagation();
      const ytype = (self.options.yscale === "log") ? "lin" : "log";
      self.setYAxisType( (self.options.yscale === "log") ? "lin" : "log" );
      self.WtEmit(self.chart.id, {name: 'yAxisTypeChanged'}, ytype );
    });

  this.xAxis = d3.svg.axis().scale(this.xScale)
   .orient("bottom")
   .innerTickSize(7)
   .outerTickSize(1)
   .ticks(20, "f");

  this.xAxisBody = this.vis.append("g")
    .attr("class", "xaxis axis" )
    .attr("transform", "translate(0," + this.size.height + ")")
    .style("cursor", "ew-resize")
    .on("mouseover", function() { 
      if( d3.select(d3.event.target).node().nodeName === "text" )
        d3.select(d3.event.target).style("font-weight", "bold"); 
     })
    .on("mouseout",  function() { 
      d3.select(this).selectAll('text').style("font-weight", null);
     })
    .on("mousedown.drag",  self.xaxisDrag())
    .on("touchstart.drag", self.xaxisDrag())
    .call(this.xAxis)
    ;

  this.vis.append("svg:clipPath")
    .attr("id", "clip" + this.chart.id )
    .append("svg:rect")
    .attr("x", 0)
    .attr("y", 0)
    .attr("width", this.size.width )
    .attr("height", this.size.height );

  self.addMouseInfoBox();


  this.chartBody = this.vis.append("g")
    .attr("clip-path", "url(#clip" + this.chart.id + ")");

  self.peakVis = this.vis.append("g")
    .attr("class", "peakVis")
    .attr("transform","translate(0,0)")
    .attr("clip-path", "url(#clip" + this.chart.id + ")");

  /* add Chart Title */
  var title = this.options.txt.title;
  this.options.txt.title = null;
  this.setTitle( title, true );

  /* Add the x-axis label */
  if( this.options.txt.xAxisTitle.length > 0 ) {
    this.setXAxisTitle( this.options.txt.xAxisTitle, true );
    if( this.xaxistitle )
      this.xaxistitle.attr("x", 0.5*this.size.width).attr("dy",29);
  }

  /* Add the y-axis label. */
  this.yAxisTitle = this.vis.append("g")
    .on("click", function(){ //toggle between linear and log when user clicks title text
      d3.event.stopPropagation();
      const ytype = (self.options.yscale === "log") ? "lin" : "log";
      self.setYAxisType( (self.options.yscale === "log") ? "lin" : "log" );
      self.WtEmit(self.chart.id, {name: 'yAxisTypeChanged'}, ytype );
    })
    //Prevent it looking like a double-click on the chart, like fitting for a peak
    .on("mousedown", function(){ d3.event.stopPropagation(); } )
    .on("mouseup", function(){ d3.event.stopPropagation(); } )
    .on("touchstart", function(){ d3.event.stopPropagation(); } )
    .on("touchend", function(){ d3.event.stopPropagation(); } );
    
  this.yAxisTitleText = this.yAxisTitle
    .append("text")
    .attr("class", "yaxistitle")
    .text( "" )
    .style("text-anchor","middle");
  
  /*Make a <g> element to draw everything we want that follows the mouse around */
  /*  when we're displaying reference photopeaks.  If the mouse isnt close enough */
  /*  to a reference line, then this whole <g> will be hidden */
  self.refLineInfo = this.vis.append("g")
    .attr("class", "refLineInfo")
    .style("display", "none");

  /*Put the reference photopeak line text in its own <g> element so */
  /*  we can call getBBox() on it to get its extent to to decide where to */
  /*  position the text relative to the selected phtotopeak. */
  self.refLineInfoTxt = self.refLineInfo.append("g");

  /*Add the text to the <g>.  We will use tspan's to append each line of information */
  self.refLineInfoTxt.append("text")
   .attr("x", 0)
   .attr("dy", "1em");

  this.updateYAxisTitleText();
  
  if( this.options.gridx )
    this.setGridX(this.options.gridx,true);
  
  if( this.options.gridy )
    this.setGridY(this.options.gridy,true);

  this.vis.on("mouseout", function(){
    if( self.currentKineticRefLine ){
      self.currentKineticRefLine = null;
      self.candidateKineticRefLines = [];
      self.currentKineticRefLineIndex = 0;
      self.stopKineticRefLineCycling();
      self.drawRefGammaLines();
    }
  });
  
  // Initialize kinetic reference line cycling properties
  this.candidateKineticRefLines = [];
  this.currentKineticRefLineIndex = 0;
  this.kineticRefLineCycleTimer = null;
  
  // Register global keyboard handler
  d3.select(window).on("keydown.chart" + this.chart.id, this.keydown());
}

/** Destructor method to properly clean up global event handlers. Call this before removing/destroying a chart. */
SpectrumChartD3.prototype.destroy = function() {
  // Remove global event handlers using the namespaced IDs
  d3.select(window).on("keydown.chart" + this.chart.id, null);
  d3.select(window).on("mouseup.chart" + this.chart.id, null);
  d3.select(window).on("mousemove.chart" + this.chart.id, null);
  d3.select(window).on("blur.chart" + this.chart.id, null);
  d3.select(document.body).on("mouseup.chart" + this.chart.id, null);
  
  // Clear any active timers - we probably dont actually have to do this, probabilistically, but we will JIC
  window.clearTimeout(this.mousewait);
  window.clearTimeout(this.touchHold);
  window.clearTimeout(this.wheeltimer);
  window.clearTimeout(this.roiDragRequestTimeout);
  window.clearTimeout(this.kineticRefLineCycleTimer);
  window.cancelAnimationFrame(this.zoomAnimationID);
  this.mousewait = this.touchHold = this.wheeltimer = this.roiDragRequestTimeout = this.kineticRefLineCycleTimer = this.zoomAnimationID = null;
  
  d3.select(document.body).style("cursor", "default"); // Reset global cursor style
  
  console.log("SpectrumChartD3 instance destroyed: " + this.chart.id);
};



/**
 * -------------- Data Handlers --------------
 */
/**
 * elem: The element can be a DOM element, or the object ID of a WObject
 * event: must be an object which indicates also the JavaScript event and event target
 * args: array of args to pass into the Wt function
 */
SpectrumChartD3.prototype.WtEmit = function(elem, event) {
  if (!window.Wt) {
    console.warn('Wt not found! Canceling "' + event.name + '" emit function...');
    return;
  }

  if( this.options.noEventsToServer )
    return;
  
  //console.log( 'Emitting Wt event "' + ((event && event.name) ? event.name : 'null') + '", with ' + SpectrumChartD3.prototype.WtEmit.length + " arguments");

  // To support ES5 syntax in IE11, we replace spread operator with this
  var args = Array.prototype.slice.call(arguments, SpectrumChartD3.prototype.WtEmit.length);

  // Emit the function to Wt
  // Wt.emit( elem, event, ...args);  // ES6 syntax
  Wt.emit.apply(Wt, [elem, event].concat(args));
}

/* Utility function to delete and nullify a set of DOM elements - consolidates repeated deletion patterns */
SpectrumChartD3.prototype.deleteAndNullifyElements = function(elementRefs) {
  for (var i = 0; i < elementRefs.length; i++) {
    var ref = elementRefs[i];
    if (ref.element && this[ref.element]) {
      this[ref.element].remove();
      this[ref.element] = null;
    }
  }
};

/* Consolidated function to delete touch lines - replaces duplicate implementations */
SpectrumChartD3.prototype.deleteTouchLine = function() {
  this.deleteAndNullifyElements([
    { element: 'touchLineX' },
    { element: 'touchLineY' }
  ]);
};

SpectrumChartD3.prototype.getStaticSvg = function(){
  try{
    let w = this.svg.attr("width"), h = this.svg.attr("height");
        
    //We will need to propagate all the styles we set in the SVG (see SpectrumChartD3.css and
    //  D3SpectrumDisplayDiv::m_cssRules) to the <defs> section of the new SVG.
    
    function getStyle( sel ){
      const el = document.querySelector(sel);
      return el ? window.getComputedStyle( el ) : null;
    };
    
    function getSvgFill( sel ){
      let style = getStyle( sel );
      let fill = style && style.fill ? style.fill : "";
      let comps = fill.match(/\d+/g);
      if( (comps && (comps.reduce( function(a,b){ return parseFloat(a) + parseFloat(b); }) > 0.01))
         || (fill.length > 2 && fill.substring(0,1)=='#') )
        return fill;
      return null;
    };
    
    function getStroke( sel ){ const s = getStyle(sel); return s && s.stroke ? s.stroke : null; }
    
    const domstyle = getStyle( '.Wt-domRoot' );
    let dombackground = domstyle && domstyle.backgroundColor ? domstyle.backgroundColor : null; //ex "rgb(44, 45, 48)", or "rgba(0, 0, 0, 0)"
    
    // Check of dom background is something other than "rgba(0, 0, 0, 0)"; not perfect yet, but kinda works
    if( dombackground ) {
      let bgrndcomps = dombackground.match(/\d+/g); //Note: the double backslash is for the C++ compiler, if move to JS file, make into a single backslash
      if( !bgrndcomps
      || ((bgrndcomps.reduce( function(a,b){ return parseFloat(a) + parseFloat(b); }) < 0.01)
      && (dombackground.length < 2 || dombackground.substring(0,1)=='#')) )
      dombackground = null;
    }
    
    const svgstyle = getStyle('#' + this.chart.id + ' > svg');
    const svgback = svgstyle && svgstyle.background ? svgstyle.background : dombackground;
    const chartAreaFill = getSvgFill('#chartarea' + this.chart.id);
    const legStyle = getStyle( '.legend' );
    const legFontSize = legStyle && legStyle.fontSize ? legStyle.fontSize : null;
    const legColor = legStyle && legStyle.color ? legStyle.color : null;
    
    const legBackStyle = getStyle( '.legendBack' );
    const legBackFill = legBackStyle && legBackStyle.fill ? legBackStyle.fill : null;
    const legBackStroke = getStroke( '.legendBack' );
    
    const axisStyle = getStyle( '.xaxis' );
    const axisFill = axisStyle && axisStyle.fill ? axisStyle.fill : null;
    
    const tickStroke = getStroke( '.xaxis > .tick > line' );
    const gridTickStroke = getStroke( '.xgrid > .tick' );
    const minorGridStroke = getStroke( '.minorgrid' );
    
    const foreStroke = getStroke( 'path.speclinepath.FOREGROUND' );
    const backStroke = getStroke( 'path.speclinepath.BACKGROUND' );
    const secondStroke = getStroke( 'path.speclinepath.SECONDARY' );
    
    let svgDefs = '<defs><style type="text/css">\n'
    + 'svg{ background:' + (svgback ? svgback : 'rgb(255,255,255)') + ';}\n'
    + '.chartarea{ fill: ' + (chartAreaFill ? chartAreaFill : 'rgba(0,0,0,0)') + ';}\n'
    + '.legend{ ' + (legFontSize ? 'font-size: ' + legFontSize + ';' : "")
    + (legColor ? 'fill: ' + legColor + ';' : "")
    + ' }\n'
    + '.legendBack{ ' + (legBackFill ? 'fill:' + legBackFill + ';' : "")
    + (legBackStroke ? 'stroke: ' + legBackStroke + ';' : "")
    + ' }\n'
    + (axisFill ? '.xaxistitle, .yaxistitle, .yaxis, .yaxislabel, .xaxis, .xaxis > .tick > text, .yaxis > .tick > text { fill: ' + axisFill + '; }\n' : "")
    + (tickStroke ? '.xaxis > .domain, .yaxis > .domain, .xaxis > .tick > line, .yaxis > .tick > line, .yaxistick { stroke: ' + tickStroke + '; }\n' : "")
    + (gridTickStroke ? '.xgrid > .tick, .ygrid > .tick { stroke: ' + gridTickStroke + ';}\n' : "" )
    + (minorGridStroke ? '.minorgrid{ stroke: ' + minorGridStroke + ';}\n' : "" )
    + '.speclinepath, .SpectrumLegendLine { fill: none; }'
    + (foreStroke ? '.speclinepath.FOREGROUND, .SpectrumLegendLine.FOREGROUND { stroke: ' + foreStroke + '}' : "")
    + (backStroke ? '.speclinepath.BACKGROUND, .SpectrumLegendLine.BACKGROUND { stroke: ' + backStroke + '}' : "")
    + (secondStroke ? '.speclinepath.SECONDARY, .SpectrumLegendLine.SECONDARY { stroke: ' + secondStroke + '}' : "")
    + '</style></defs>';
    
    // Hide mouse information, and slider chart
    this.refLineInfo.style("display", "none");
    this.mouseInfo.style("display", "none");
    if( this.sliderChart && this.size.sliderChartHeight ){
      //  Remove slider chart from SVG, and also adjust height
      h -= this.size.sliderChartHeight;
      this.sliderChart.style("display", "none");
    }
    
    if( this.scalerWidget ){
      w -= this.scalerWidget.node().getBBox().width;
      this.scalerWidget.style("display", "none");
    }
    
    if( this.sliderClose )
      this.sliderClose.style("display", "none");
    
    if( this.peakInfo )
      this.peakInfo.style("display", "none");
    
    let svgMarkup = '<svg xmlns="http://www.w3.org/2000/svg"' + ' width="'  + w + '"' + ' height="' + h + '"' + '>'
                    + svgDefs + this.svg.node().innerHTML.toString() +'</svg>';
    
    // Make slider chart and/or scalerWidget visible again, if they should be showing
    if( this.sliderChart && this.size.sliderChartHeight )
      this.sliderChart.style("display", null);
    if( this.scalerWidget )
      this.scalerWidget.style("display", null);
    if( this.peakInfo )
      this.peakInfo.style("display", null);
    if( this.sliderClose )
      this.sliderClose.style("display", null);
    return svgMarkup;
  }catch(e){
    throw 'Error creating SVG spectrum: ' + e;
  }
}//getStaticSvg




SpectrumChartD3.prototype.dataPointDrag = function() {
  var self = this;
  return function(d) {
    self.selected = self.dragged = d;
    self.update(false); /* boolean set to false to indicate no animation needed */

  }
}

SpectrumChartD3.prototype.do_rebin = function() {
  var self = this;

  if( !this.rawData || !self.rawData.spectra || !self.rawData.spectra.length ) {
    return;
  }

  // We will choose the rebin factor based on spectrum with the least number of channels.  This is
  //  driven by case of comparing a HPGe spectrum (~16k channel) to a NaI spectrum (~1k channels),
  //  we dont want to lose the definition in the NaI spectrum by combining a bunch of channels.
  //  The side-effect of this is that the HPGe spectrum may have way more points than pixels...
  let newRebin = 9999;
  this.rawData.spectra.forEach(function(spectrum) {
    const firstRaw = self.displayed_raw_start(spectrum);
    const lastRaw = self.displayed_raw_end(spectrum);
    const npoints = lastRaw - firstRaw;
    
    if( npoints > 1 && self.size.width > 2 ){
      const thisrebin = Math.max( 1, Math.ceil(self.options.spectrumLineWidth * npoints / self.size.width) );
      newRebin = ((thisrebin > 0) && (thisrebin < newRebin)) ? thisrebin : newRebin;
    }
  } );
  
  if( newRebin === 9999 )
    newRebin = 1;
  
  if( this.rebinFactor !== newRebin ){
    this.rebinFactor = newRebin;
    this.updateYAxisTitleText();
  }
  
  this.rawData.spectra.forEach(function(spectrum) {
    let firstRaw = self.displayed_raw_start(spectrum);
    let lastRaw = self.displayed_raw_end(spectrum);
    
    if( newRebin != spectrum.rebinFactor || spectrum.firstRaw !== firstRaw || spectrum.lastRaw !== lastRaw ){
      spectrum.points = [];

      spectrum.rebinFactor = newRebin;
      spectrum.firstRaw = firstRaw;
      spectrum.lastRaw = lastRaw;

      /*Round firstRaw and lastRaw down and up to even multiples of newRebin */
      firstRaw -= (firstRaw % newRebin);
      lastRaw += newRebin - (lastRaw % newRebin);
      if( firstRaw >= newRebin )
        firstRaw -= newRebin;
      if( lastRaw > spectrum.x.length )
        lastRaw = spectrum.x.length;


      /*could do some optimizations here where we actually do a slightly larger */
      /*  range than displayed, so that next time we might not have to go back */
      /*  through the data to recompute things (it isnt clear if D3 will plot */
      /*  these datas, should check on this.) */
      /*Also, could _maybe_ use energy range, rather than indexes to track if we */
      /*  need to rebin the data or not... */

      for( var i = firstRaw; i < lastRaw; i += newRebin ){
        let thisdata = { x: 0, y: 0 };
        if (i >= spectrum.x.length)
          thisdata.x = spectrum.x[spectrum.x.length-1];
        else
          thisdata.x = spectrum.x[i];

        for( let j = 0; (j < newRebin) && ((i+j) < spectrum.y.length); ++j )
          thisdata.y += spectrum.y[i+j];
        thisdata.y *= spectrum.yScaleFactor;

        spectrum.points.push( thisdata );
      }
    }
  });
}

SpectrumChartD3.prototype.adjustYScaleOfDisplayedDataPoints = function(spectrumToBeAdjusted, linei) {
  let self = this;

  if( !this.rawData || !this.rawData.spectra || !this.rawData.spectra.length ) 
    return;

  /* Check for the which corresponding spectrum line is the specified one to be rebinned */
  if( linei == null || !spectrumToBeAdjusted )
    return;
    
  let firstRaw = self.displayed_raw_start(spectrumToBeAdjusted);
  let lastRaw = self.displayed_raw_end(spectrumToBeAdjusted);

  const rebinFactor = spectrumToBeAdjusted.rebinFactor; //should be same as this.rebinFactor

  spectrumToBeAdjusted.firstRaw = firstRaw;
  spectrumToBeAdjusted.lastRaw = lastRaw;

  /* Round firstRaw and lastRaw down and up to even multiples of rebinFactor */
  firstRaw -= (firstRaw % rebinFactor);
  lastRaw += rebinFactor - (lastRaw % rebinFactor);
  if( firstRaw >= rebinFactor )
    firstRaw -= rebinFactor;
  if( lastRaw > spectrumToBeAdjusted.x.length )
    lastRaw = spectrumToBeAdjusted.x.length;

  let i = firstRaw;
  for( let pointi = 0; pointi < spectrumToBeAdjusted.points.length; pointi++ ){
    let thisdata = spectrumToBeAdjusted.points[pointi];

    thisdata.y = 0;
    for( let j = 0; (j < rebinFactor) && ((i+j) < spectrumToBeAdjusted.y.length); ++j )
      thisdata.y += spectrumToBeAdjusted.y[i+j];
    thisdata.y *= spectrumToBeAdjusted.yScaleFactor;

    i += rebinFactor;
  }
}

/** Takes in an object like { yAxisTitle: 'Counts', realTime: 'Real Time',... } that maps 
 * from variables used in this code, to the strings that should be used.  Does not need to be complete
*/
SpectrumChartD3.prototype.setLocalizations = function( input, isInitialDef ) {
  const self = this;

  const defVals = {
    xAxisTitle: "Energy (keV)",
    yAxisTitle: "Counts",
    yAxisTitleMulti: "Counts per {1} Channels",
    realTime: "Real Time",
    liveTime: "Live Time",
    deadTime: "Dead Time",
    scaledBy: "Scaled by",
    zoomIn: "Zoom In",
    neutrons: "neutrons",
    Neutrons: "Neutrons",
    cps: "cps",
    roiCounts: "ROI counts",
    gammaCounts: "Gamma Counts",
    contArea: "cont. area",
    peakCps: "peak cps",
    peakArea: "peak area",
    fwhm: "FWHM",
    mean: "mean",
    spectrum: "Spectrum",
    comptonEdge: "Compton Edge",
    sumPeak: "Sum Peak",
    firstEnergyClick: "Click to set sum peak first energy.",
    singleEscape: "Single Escape",
    doubleEscape: "Double Escape",
    eraseInRange: "Will Erase Peaks In Range",
    zoomInY: "Zoom-In on Y-axis",
    zoomOutY: "Zoom-out on Y-axis",
    touchDefineRoi: "Move 2 fingers to right to define ROI",
    foreNSigmaBelowBack: "Foreground is {1} σ below background.",
    foreNSigmaAboveBack: "Foreground is {1} σ above background.",
    backSubCounts: "counts (BG sub)",
    afterScalingBy: "after scaling by ",
    clickedPeak: "Clicked Peak",
    recalFromTo: "Recalibrate data from {1} to {2} keV",
    sumFromTo: "{1} to {2} keV",
    comptonPeakAngle: "{1}° Compton Peak",
  };

  const notDefined = ( (typeof self.options.txt !== 'object') || (self.options.txt === null) || Array.isArray(self.options.txt) || (self.options.txt instanceof Function) );
  if( notDefined )
    self.options.txt = {}; //e.g., called from constructor of this object
    

  // Make sure self.options.txt has all properties we expect
  Object.keys(defVals).forEach(key => {
    if (!self.options.txt.hasOwnProperty(key)) {
      self.options.txt[key] = defVals[key];
    }
  });

  // Set values to new keys
  Object.keys(input).forEach(key => {
    if (!self.options.txt.hasOwnProperty(key) || (typeof input[key] !== 'string') ) {
      console.error( "setLocalizations: input has invalid property,", key, ", with value:", input[key] );
    }else{
      self.options.txt[key] = input[key];
    }
  });

  if( isInitialDef )
    return;

  if( input.hasOwnProperty("xAxisTitle") )
  {
    if( self.xaxistitle )
      self.xaxistitle.text(self.options.txt.xAxisTitle);
    else
      self.setXAxisTitle( self.options.txt.xAxisTitle, true );
  }
    
  if( input.hasOwnProperty("yAxisTitle") || input.hasOwnProperty("yAxisTitleMulti") )
    self.updateYAxisTitleText();

  if( !notDefined )
    self.handleResize( false );
}//SpectrumChartD3.prototype.setLocalizations = ...


/**
 * Adds or replaces the first spectrum seen with the passed-in type in the raw data with new spectrum data.
 */
SpectrumChartD3.prototype.setSpectrumData = function( spectrumData, resetdomain, spectrumType, id, backgroundID ) {
  var self = this;

  if (!spectrumData) return;
  if (!spectrumType || !(spectrumType in self.spectrumTypes)) return;

  if (!self.rawData) self.rawData = { updateTime: spectrumData.updateTime, spectra: [] };
  if (!self.rawData.updateTime) self.rawData.updateTime = spectrumData.updateTime;

  let spectra = self.rawData.spectra;
  let index = -1;
  spectrumData = spectrumData.spectra[0];

  //console.log('setting ', spectrumData);

  // Set the ID if it was specified
  if (typeof id !== 'undefined') spectrumData.id = id;
  if( spectrumData.id === 'undefined' || spectrumData.id === null ) spectrumData.id = Math.random();

  // Set the background ID if it was specified
  if (backgroundID) spectrumData.backgroundID = backgroundID;

  // Find index of first spectrum of this type
  for (let i = 0; i < spectra.length; i++) {
    if (spectra[i].type === spectrumType) {
      index = i;
      break;
    }
  }

  if (index < 0) {  // no spectrum of this type found, so add it onto raw data
    spectra.push(spectrumData);

  } else {  // spectrum found of this type, so replace it
    spectra[index] = spectrumData;
  }

  // Call primary function for setting data
  self.setData( self.rawData, resetdomain );
}

SpectrumChartD3.prototype.removeSpectrumData = function( resetdomain, spectrumType ) {
  /* Temporary depreciated function. */
  this.removeSpectrumDataByType( resetdomain, spectrumType );
}

/** Removes all spectra seen with the passed-in type in the raw data. */
SpectrumChartD3.prototype.removeSpectrumDataByType = function( resetdomain, spectrumType ) {
  var self = this;

  if (!spectrumType || !(spectrumType in self.spectrumTypes)) return;
  if (!self.rawData) self.rawData = { spectra: [] };

  let spectra = self.rawData.spectra;

  // Find index of first spectrum of this type
  let havemore = true;
  while( havemore && spectra.length > 0 ) {
    havemore = false;
    for (let i = 0; i < spectra.length; i++) {
      if (spectra[i].type === spectrumType) {
        havemore = true;
        spectra.splice(i, 1);
        break;
      }
    }
  }
  
  // Call primary function for setting data
  self.setData( self.rawData, resetdomain );
}

SpectrumChartD3.prototype.setData = function( data, resetdomain ) {
  // ToDo: need to make some consistency checks on data here
  /*  - Has all necessary variables */
  /*  - Energy is monotonically increasing */
  /*  - All y's are the same length, and consistent with x. */
  /*  - No infs or nans. */

  const self = this;
  
  //Remove all the lines for the current drawn histograms
  this.vis.selectAll(".speclinepath").remove();

  this.vis.selectAll('path.line').remove();
  if (this.sliderChart) this.sliderChart.selectAll('.sliderLine').remove(); // Clear x-axis slider chart lines if present

  this.rawData = null;
  this.rebinFactor = -1; /*force rebin calc */

  try
  {
    if( !data || !data.spectra ) throw null;
    if( !Array.isArray(data.spectra) || data.spectra.length < 1 )
      throw 'No spectrum-data specified';
    /*check that x is same length or one longer than y. */
    /*Check that all specified y's are the same length */
  }catch(e){
    this.updateLegend();
   
    this.redraw()();
    return;
  }

  // Sort data so foreground is first, then secondary, then background (this is mostly so order of legend is reasonable)
  data.spectra.sort( function(lhs,rhs){
    const order = [self.spectrumTypes.FOREGROUND, self.spectrumTypes.SECONDARY, self.spectrumTypes.BACKGROUND];
    const lhs_pos = order.indexOf(lhs.type);
    const rhs_pos = order.indexOf(rhs.type);
    return ((lhs_pos >= 0) ? lhs_pos : 4) - ((rhs_pos >= 0) ? rhs_pos : 4);
  });
  
  this.rawData = data;

  this.rawData.spectra.forEach( function(spectrum,i){
    if( !spectrum || !spectrum.y || !spectrum.y.length )
      return;
    if( spectrum.xeqn && spectrum.xeqn.length>1 && !spectrum.x && spectrum.y && spectrum.y.length )
    {
      /* A polynomial equation was specified, rather than lower channel energies */
      spectrum.x = [];
      for( var i = 0; i < spectrum.y.length; ++i )
      {
        spectrum.x[i] = spectrum.xeqn[0];
        for( var j = 1; j < spectrum.xeqn.length; ++j )
          spectrum.x[i] += spectrum.xeqn[j] * Math.pow(i,j);
      }
    }
  } );
  
  for (var i = 0; i < this.rawData.spectra.length; ++i)
    this['line'+i] = null;
  
  this.do_rebin();  /*this doesnt appear to be necassay, but JIC */

  /*Make it so the x-axis shows all the data */
  if( resetdomain ) {
    const bounds = self.min_max_x_values();
    this.setXAxisRange(bounds[0], bounds[1], true, false);
  }

  var maxYScaleFactor = 0.1;
  for (var i = 0; i < this.rawData.spectra.length; ++i)
    this.rawData.spectra[i].dataSum = 0;

  /* Create the lines - draw background first, then secondary, then unknown types, then foreground */
  const typeDrawOrder = {
    [self.spectrumTypes.BACKGROUND]: 0,
    [self.spectrumTypes.SECONDARY]: 1,
    [self.spectrumTypes.FOREGROUND]: 3
  };
  
  const drawindexes = data.spectra
    .map((spectrum, index) => ({ spectrum, index }))
    .sort((a, b) => {
      const orderA = typeDrawOrder[a.spectrum.type] !== undefined ? typeDrawOrder[a.spectrum.type] : 2;
      const orderB = typeDrawOrder[b.spectrum.type] !== undefined ? typeDrawOrder[b.spectrum.type] : 2;
      return orderA - orderB;
    })
    .map(item => item.index);
  
  for( let ind = 0; ind < drawindexes.length; ++ind ) {
    let i = drawindexes[ind];
    var spectrum = data.spectra[i];
    if (spectrum.y.length) {
      for (var j = 0; j < spectrum.y.length; ++j) {
        spectrum.dataSum += spectrum.y[j];
      }
      this['line' + i] = d3.svg.line()
        .interpolate("step-after")
        .x( function(d){ return self.xScale(d.x); })
        .y( function(d) {
          const y = self.yScale(d.y);
          return isNaN(y) ? 0 : y;
        } );

      this.chartBody.append("path")
        .attr("id", "spectrumline"+i)
        .attr("class", "speclinepath" + (spectrum.type ? " " + spectrum.type : "") )
        .attr("stroke-width", self.options.spectrumLineWidth)
        .attr("stroke", spectrum.lineColor ? spectrum.lineColor : null )
        .attr("d", this['line' + i](spectrum.points));
        
      if (spectrum.yScaleFactor)
        maxYScaleFactor = Math.max(spectrum.yScaleFactor, maxYScaleFactor);
    }
  }

  /* + 10 to add at least some padding for scale */
  self.options.maxScaleFactor = maxYScaleFactor + 10;
  var maxsfinput;
  if (maxsfinput = document.getElementById("max-sf")) {
    maxsfinput.value = self.options.maxScaleFactor;
  }

  if( this.options.gridx )
    this.setGridX(this.options.gridx,true);
  if( this.options.gridy )
    this.setGridY(this.options.gridy,true);
  
  this.addMouseInfoBox();

  this.updateLegend();
  this.drawScalerBackgroundSecondary();

  if( this.options.showXAxisSliderChart && !this.sliderChart )
    this.drawXAxisSliderChart();

  this.redraw()();
}

/** Sets (replacing any existing) peak data for first spectrum matching spectrumType
  Input should be like [{...},{...}]
 */
SpectrumChartD3.prototype.setRoiData = function( peak_data, spectrumType ) {
  let self = this;
  let hasset = false;
  
  if( !this.rawData || !this.rawData.spectra || !this.rawData.spectra )
    return;
  
  this.rawData.spectra.forEach( function(spectrum) {
    if( hasset || !spectrum || spectrum.type !== spectrumType )
      return;
    self.handleCancelRoiDrag();
    spectrum.peaks = peak_data;
    hasset = true;
  } );
  
  this.redraw()();
};


/**
 * Render/Drawing Functions
 */
SpectrumChartD3.prototype.update = function() {
  const self = this;

  if (!this.rawData || !this.rawData.spectra || !this.rawData.spectra.length)
    return;
  
  this.rawData.spectra.forEach(function(spectrum, i) {
    const line = self.vis.select("#spectrumline"+i);
  
    if (spectrum.type === self.spectrumTypes.BACKGROUND) {
      line.attr('visibility', self.options.backgroundSubtract ? 'hidden' : 'visible');
      if (self.options.backgroundSubtract) return;
    }
  
    // Figure out which set of points to use
    //  Use background subtract if we're in that view mode, otherwise use the normal set of points
    const key = self.options.backgroundSubtract && ('bgsubtractpoints' in self.rawData.spectra[i]) ? 'bgsubtractpoints' : 'points';  // Figure out which set of points to use
    
    line.attr("d", self['line'+i](self.rawData.spectra[i][key]));
  });

  if (d3.event && d3.event.keyCode) {
    d3.event.preventDefault();
    d3.event.stopPropagation();
  }
}

SpectrumChartD3.prototype.redraw = function() {
  const self = this;

  return function() {
    if( this.size && (this.size.nYScalers !== this.numYScalers()) ) {
      this.handleResize( true );
    }
    
    self.do_rebin();
    self.rebinForBackgroundSubtract();  // Get the points for background subtract
    self.setYAxisDomain();

    self.drawYTicks();
    
    self.calcLeftPadding( true );
    
    self.drawXTicks();

    self.drawXAxisArrows();

    if (self.options.showXAxisSliderChart) { self.drawXAxisSliderChart(); } 
    else                                   { self.cancelXAxisSliderChart(); }
    
    self.drawPeaks();
    self.drawSearchRanges();
    self.drawHighlightRegions();
    self.drawRefGammaLines();
    self.updateMouseCoordText();

    self.update();

    self.yAxisZoomedOutFully = true;
  }
}

SpectrumChartD3.prototype.calcLeftPadding = function( updategeom ){
  
  if( !this.options.adjustYAxisPadding ) {
    this.padding.leftComputed = this.padding.left; 
    return;
  }
  
  var labels = this.yAxisBody.selectAll("g.tick").selectAll("text"); // g.tick includes tick and number
  
  var labelw = 4;
  labels.forEach( function(label){
    labelw = Math.max( labelw, label.parentNode.getBBox().width );
  });
  
  let titlew = 0;
  const labelpad = this.padding.labelPad;
  if( this.yAxisTitleText ){
    titlew = this.yAxisTitleText[0][0].parentNode.getBBox().height;
    this.yAxisTitle.attr("transform","translate(-" + (labelw+labelpad) + " " + this.size.height/2+") rotate(-90)");
  }
  
  const newleft = labelw + (titlew > 0 ? (titlew + labelpad) : 0) + this.padding.left + 4;
  
  if( !updategeom ) {
    this.padding.leftComputed = newleft;
  } else if( Math.abs(this.padding.leftComputed - newleft) > 1.0 ) {
    this.padding.leftComputed = newleft;
    this.handleResize( true );
  } 
}

/* Sets the title of the graph */
SpectrumChartD3.prototype.setTitle = function(title,dontRedraw) {
  if( (title == null || typeof title !== 'string') || title.length === 0 ){
    this.options.txt.title = null;
    this.svg.select('.title').remove();
  } else {
    if( !(this.options.txt.title && (this.options.txt.title.length > 0)) )
      this.svg.append("text")
          .attr("class", "title")
          .text(title)
          .attr("x", this.cx/2)
          .attr("dy", this.padding.title)
          .style("text-anchor","middle");
    this.options.txt.title = title;
  }
  this.handleResize( dontRedraw );
}

SpectrumChartD3.prototype.hasCompactXAxis = function() {
  return (this.options.showXAxisSliderChart ? this.options.compactXAxisWithSliderChart : this.options.compactXAxis);
}

SpectrumChartD3.prototype.setXAxisTitle = function(title, dontResize) {
  var self = this;

  self.options.txt.xAxisTitle = null;
  self.svg.select('.xaxistitle').remove();

  if( (title == null || typeof title !== 'string') || title.length === 0 )
    return;
  
  let handleClick = function(){
    d3.event.stopPropagation();
    const show = !self.options.showXAxisSliderChart
    self.setShowXAxisSliderChart(show);
    self.WtEmit(self.chart.id, {name: 'sliderChartDisplayed'}, show );
  };
  
  self.options.txt.xAxisTitle = title;
  self.xaxistitle = this.xAxisBody
    .append("text")
    .attr("class", "xaxistitle")
    .attr("y", 0)
    .text(self.options.txt.xAxisTitle)
    .style("text-anchor", this.hasCompactXAxis() ? "start" : "middle")
    .on("click", handleClick )
    .on("mouseover", function(){ d3.event.stopPropagation(); })
    .on("mouseout", function(){ d3.event.stopPropagation(); })
    .on("mousedown.drag",  function(){ d3.event.stopPropagation(); } )
    .on("touchstart.drag", function(){
      if( d3.event.touches && (d3.event.touches.length < 2) )
        handleClick();
    } )
    ;

  if( !dontResize )
    self.handleResize( false );
}

SpectrumChartD3.prototype.setYAxisTitle = function(title, titleMulti) {
  this.options.txt.yAxisTitle = (typeof title === 'string') ? title : "";
  if( typeof titleMulti === 'string' )
    this.options.txt.yAxisTitleMulti = titleMulti;
  
  this.updateYAxisTitleText();
  
  this.handleResize( true );
}

SpectrumChartD3.prototype.updateYAxisTitleText = function() {
  if( (this.options.txt.yAxisTitle.length === 0) || (this.rebinFactor === 1) )
    this.yAxisTitleText.text( this.options.txt.yAxisTitle );
  else
    this.yAxisTitleText.text( this.options.txt.yAxisTitleMulti.replace("{1}", String(this.rebinFactor)) );
}


SpectrumChartD3.prototype.handleResize = function( dontRedraw ) {
  var self = this;

  const prevCx = this.cx;
  const prevCy = this.cy;
  
  //Using this.chart.clientWidth / .clientHeight can cause height to grow indefinetly 
  //  when inside of a scrolling div somewhere. chart.offsetWidth/.offsetHeight dont have this issue
  const parentRect = this.chart.getBoundingClientRect();
  if( !parentRect.width || !parentRect.height )
  {
    console.log( 'SpectrumChartD3::handleResize: parent didnt have size; not doing anything.' );
    return;
  }

  this.cx = parentRect.width;
  this.cy = parentRect.height;

  var titleh = 0, xtitleh = 0, xlabelh = 7 + 22;
  
  // TODO: actually measure `xlabelh`; we could either easily do it in `drawXTicks` or do it with something like:
  //   d3.selectAll(".xaxis g.tick")[0].forEach( function(a){ console.log( a.getBBox().height ); } );
  
  if( this.options.txt.title ) {
    this.svg.selectAll(".title").each( function(d){
      titleh = this.getBBox().height;  
   });
  }
  
  const xaxistitleBB = this.xaxistitle ? this.xaxistitle.node().getBBox() : null;
  if( xaxistitleBB )
    xtitleh = xaxistitleBB.height;
  
  this.padding.topComputed = titleh + this.padding.top + (titleh > 0 ? this.padding.titlePad : 0);
  
  /*Not sure where the -12 comes from, but things seem to work out... */
  if( this.hasCompactXAxis() && xtitleh > 0 ) {
    var txth = 4 + this.padding.xTitlePad + xtitleh;
    this.padding.bottomComputed = this.padding.bottom + Math.max( txth, xlabelh-10 );
  } else {
    this.padding.bottomComputed = -12 + this.padding.bottom + xlabelh + (xtitleh > 0 ? this.padding.xTitlePad : 0) + xtitleh; 
  }
  
  this.calcLeftPadding( false );
  
  if( self.sliderChartPlot ) {
    let ypad = self.padding.sliderChart + this.padding.topComputed + this.padding.bottomComputed;
    this.size.sliderChartHeight = Math.max( 0, self.options.sliderChartHeightFraction*(this.cy - ypad) );
    this.size.sliderChartWidth = Math.max(0.85*this.cx,this.cx-100);
  } else {
    this.size.sliderChartHeight = 0;
  }
 
  this.size.nYScalers = this.numYScalers();
  this.size.width = Math.max(0, this.cx - this.padding.leftComputed - this.padding.right - 20*this.size.nYScalers);
  this.size.height = Math.max(0, this.cy - this.padding.topComputed - this.padding.bottomComputed - this.size.sliderChartHeight);

  this.xScale.range([0, this.size.width]);
  this.vis
      .attr("width",  this.size.width)
      .attr("height", this.size.height)
      .attr("transform", "translate(" + this.padding.leftComputed + "," + this.padding.topComputed + ")");
  this.plot
      .attr("width", this.size.width)
      .attr("height", this.size.height);

  self.svg
      .attr("width", this.cx)
      .attr("height", this.cy)
      .attr("viewBox", "0 0 "+this.cx+" "+this.cy);

  this.vis.attr("width", this.cx )
          .attr("height", this.cy );

  d3.select("#chartarea"+this.chart.id)
          .attr("width", this.size.width )
          .attr("height", this.size.height );

  d3.select("#clip"+ this.chart.id).select("rect")
    .attr("width", this.size.width )
    .attr("height", this.size.height );

  /*Fix the text position */
  this.svg.selectAll(".title")
      .attr("x", this.cx/2);

  if( this.xaxistitle ){
    // this.xaxistitle is appended to this.xAxisBody
    //  Also, if this.hasCompactXAxis(), then "text-anchor" style is "start", else its "middle"
    
    if( this.hasCompactXAxis() ){
      this.xaxistitle.attr("x", this.size.width - xaxistitleBB.width - 10);
      this.xaxistitle.attr("dy", xtitleh + this.padding.xTitlePad );
    } else {
      this.xaxistitle.attr("x", 0.5*this.size.width);
      this.xaxistitle.attr("dy", xlabelh + this.padding.xTitlePad );
    }
  }

  this.xAxisBody.attr("width", this.size.width )
                .attr("height", this.size.height )
                .attr("transform", "translate(0," + this.size.height + ")");

  // Christian: Prevents bug of initially rendering chart w/all ticks displayed 
  if (this.cx !== prevCx && this.cy !== prevCy)
    this.xAxisBody.call(this.xAxis);

  this.yAxisBody.attr("height", this.size.height );
  
  this.mouseInfo.attr("transform","translate(" + this.size.width + "," + this.size.height + ")");

  if( this.xGrid ) {
    this.xGrid.innerTickSize(-this.size.height)
    this.xGridBody.attr("width", this.size.width )
        .attr("height", this.size.height )
        .attr("transform", "translate(0," + this.size.height + ")")
        .call(this.xGrid);
  }

  if( this.yGrid ) {
    this.yGrid.innerTickSize(-this.size.width)
    this.yGridBody.attr("width", this.size.width )
        .attr("height", this.size.height )
        .attr("transform", "translate(0,0)")
        .call(this.yGrid);
  }
  
  /*Make sure the legend stays visible */
  if( this.legend && this.size.height > 50 && this.size.width > 100 && prevCx > 50 && prevCy > 50 ) {
    var trans = d3.transform(this.legend.attr("transform")).translate;
    var bb = this.legendBox.node().getBBox();
    
    //If legend is closer to left side, keep distance to left of chart the same, else right.
    //Same for top/bottom.  Usses upper left of legend, and not center... oh well.
    var legx = (trans[0] < 0.5*prevCx) ? trans[0] : this.cx - (prevCx - trans[0]);
    var legy = (trans[1] < 0.5*prevCy) ? trans[1] : this.cy - (prevCy - trans[1]);
    
    //Make sure legend is visible
    legx = ((legx+bb.width) > this.cx) ? (this.cx - bb.width - this.padding.right - 20*this.size.nYScalers) : legx;
    legy = ((legy+bb.height) > this.cy) ? (this.cy - bb.height) : legy;
    
    this.legend.attr("transform", "translate(" + Math.max(0,legx) + "," + Math.max(legy,0) + ")" );
  }

  if( this.options.showXAxisSliderChart ){ 
    self.drawXAxisSliderChart(); 
  } else {
    self.cancelXAxisSliderChart(); 
  }
  
  this.xScale.range([0, this.size.width]);
  this.yScale.range([0, this.size.height]);

  this.drawScalerBackgroundSecondary();

  if( !dontRedraw ) {
    this.setData( this.rawData, false );
    this.redraw()();
  }
}

/* Pan chart is called when right-click dragging */
SpectrumChartD3.prototype.handlePanChart = function () {
  var self = this;

  var minx, maxx, bounds;
  if( !self.rawData || !self.rawData.spectra || !self.rawData.spectra.length ){
    minx = 0;
    maxx = 3000;
  }else {
    bounds = self.min_max_x_values();
    minx = bounds[0];
    maxx = bounds[1];
  }

  /* We are now right click dragging  */
  self.is_panning = true;

  /* For some reason, using the mouse position for self.vis makes panning slightly buggy, so I'm using the document body coordinates instead */
  var docMouse = d3.mouse(document.body);

  if (!docMouse || !self.rightClickDown)
    return;

  d3.select(document.body).attr("cursor", "pointer");

  /* Set the pan right / pan left booleans */
  var panRight = docMouse[0] < self.rightClickDown[0],   /* pan right if current mouse position to the left from previous mouse position */
      panLeft = docMouse[0] > self.rightClickDown[0],    /* pan left if current mouse position to the right from previous mouse position */
      dx = 0;

  function newXDomain() {
    var currentX = docMouse[0],
        delta = currentX - self.rightClickDown[0],
        oldMin = self.xScale.range()[0],
        oldMax = self.xScale.range()[1];

    /* Declare new x domain members */
    var newXMin = oldMin, 
        newXMax = oldMax;

    newXMin = oldMin - delta;
    newXMax = oldMax - delta;

    newXMin = self.xScale.invert(newXMin);
    newXMax = self.xScale.invert(newXMax);

    /* Make sure we don't go set the domain out of bounds from the data */
    if( newXMin < minx ){
     newXMax += (minx - newXMin);
     newXMin = minx; 
    }
    if( newXMax > maxx ){
     newXMin = Math.max(minx,newXMin-(newXMax-maxx));
     newXMax = maxx;
    }
    return [newXMin, newXMax];
  }

  /* Get the new x values */
  var newXMin = newXDomain()[0],
      newXMax = newXDomain()[1];

  /* Pan the chart */
  self.setXAxisRange(newXMin, newXMax, false, true);
  self.redraw()();

  /* New mouse position set to current moue position */
  self.rightClickDown = docMouse;
}


/**
 * -------------- Chart Event Handlers --------------
 */
SpectrumChartD3.prototype.handleChartMouseMove = function() {
  var self = this;

  return function() {
    //console.log( "handleChartMouseMove" );
    self.mousemove()();

    // If no data is loaded, then stop updating other mouse move parameters
    if( !self.rawData || !self.rawData.spectra || !self.rawData.spectra.length
        || self.xaxisdown || !isNaN(self.yaxisdown) || self.legdown ){
      return;
    }

    /* Get the absolute minimum and maximum x-values valid in the data */
    const bounds = self.min_max_x_values();
    const minx = bounds[0];
    const maxx = bounds[1];
     
    /* Set the mouse and chart parameters */
    const m = self.getMousePos();
    self.lastMouseMovePos = m;
    
    let x = m[0], y = m[1];

    /* Adjust the last mouse move position in case user starts dragging from out of bounds */
    if (self.lastMouseMovePos[1] < 0)
      self.lastMouseMovePos[1] = 0;
    else if (self.lastMouseMovePos[1] > self.size.height)
      self.lastMouseMovePos[1] = self.size.height;
    
    /* Do any other mousemove events that could be triggered by the left mouse drag */
    self.handleMouseMoveSliderChart()();
    self.handleMouseMoveScaleFactorSlider()();

    // If we are moving the mouse, get rid of the grey line used to show where you touched the screen
    if (self.touchLineX) {
      self.touchLineX.remove();
      self.touchLineX = null;
    }
    if (self.touchLineY) {
      self.touchLineY.remove();
      self.touchLineY = null; 
    }  


    /* It seems that d3.event.buttons is present on Chrome and Firefox, but is not in Safari. */
    /* d3.event.button is present in all browsers, but is a little less consistent so we need to keep track of when the left or right mouse is down. */
    if ((d3.event.button === 0 && self.leftMouseDown)) {        /* If left click being held down (left-click and drag) */
      //console.log( "handleChartMouseMove: left down" );

      const isWindows = self.isWindows();
      
      d3.select(document.body).attr("cursor", "move");

      /* Holding the Shift-key and left-click dragging --> Delete Peaks Mode */
      self.isDeletingPeaks = d3.event.shiftKey && !d3.event.altKey && !d3.event.ctrlKey && !d3.event.metaKey && !self.fittingPeak && !self.escapeKeyPressed;

      /* Holding the Alt+Shift-key and left-click dragging --> Count Gammas Mode */
      self.isCountingGammas = d3.event.altKey && d3.event.shiftKey && !d3.event.ctrlKey && !d3.event.metaKey && !self.fittingPeak && !self.escapeKeyPressed;

      /* Holding the Alt+Ctrl-key + Left-click Dragging --> Recalibration Mode */
      self.isRecalibrating = d3.event.altKey && d3.event.ctrlKey && !d3.event.metaKey && !d3.event.shiftKey && !self.fittingPeak && !self.escapeKeyPressed; 

      /* Holding the Command-key + Left-click dragging --> Zoom-in Y Mode */
      // For Windows, you can zoom-in on the y-axis using Alt + Left-drag
      self.isZoomingInYAxis = (isWindows ? d3.event.altKey : d3.event.metaKey) && !(isWindows ? d3.event.metaKey : d3.event.altKey) && !d3.event.ctrlKey && !d3.event.shiftKey && !self.fittingPeak && !self.escapeKeyPressed;

      /* Holding the Alt-key + Left-click dragging ---> Undefined, maybe a future implementation? */
      self.isUndefinedMouseAction = (isWindows ? d3.event.metaKey : d3.event.altKey) && !d3.event.ctrlKey && !(isWindows ? d3.event.altKey : d3.event.metaKey) && !d3.event.shiftKey && !self.fittingPeak && !self.escapeKeyPressed;

      var isZoomingInXAxis = !d3.event.altKey && !d3.event.ctrlKey && !d3.event.metaKey && !d3.event.shiftKey && !self.fittingPeak && !self.escapeKeyPressed && !self.roiIsBeingDragged;

      /* If erasing peaks */
      if( self.fittingPeak ) {          /* If fitting peaks */
        self.handleMouseMovePeakFit();
      } else if (self.isDeletingPeaks) {   /* If deleting peaks */
        self.handleMouseMoveDeletePeak();
      } else if (self.isCountingGammas) {   /* If counting gammas */
        self.updateGammaSum();
      } else if (self.isRecalibrating) {      /* If recalibrating the chart */
        self.handleMouseMoveRecalibration();
      } else if (self.isZoomingInYAxis) {        /* If zooming in y-axis */
        self.handleMouseMoveZoomY();
      } else if (self.isUndefinedMouseAction) {   /* If undefined mouse action */
        self.handleCancelAllMouseEvents()();      /* Do nothing, save for future feature */
      } else if (isZoomingInXAxis) {    /* If zooming in x-axis */
        self.handleMouseMoveZoomX();
      } else if( !self.roiIsBeingDragged ){
        self.handleCancelAllMouseEvents()();
      }

      return;
    } else if ( self.rightClickDown /*&& d3.event.button === 2*/ ){
      //console.log( "handleChartMouseMove: right down" );

      self.handleCancelRoiDrag();

      /* Right Click Dragging: pans the chart left and right */
      self.handlePanChart();
    } else if( self.rawData.spectra && self.rawData.spectra.length > 0
               && (x >= 0 && y >= 0 && y <= self.size.height && x <= self.size.width
               && !d3.event.altKey && !d3.event.ctrlKey && !d3.event.metaKey
               && !d3.event.shiftKey && !self.fittingPeak && !self.escapeKeyPressed ) ) {
      // If we are here, the mouse button is not down, and the user isnt holding control, shift, etc
      if( self.kineticRefLines )
        self.handleUpdateKineticRefLineUpdate();

      if( self.options.allowDragRoiExtent && self.roiDragBoxes && self.showDragLineWhileInRoi && !self.roiIsBeingDragged ){
        // If we're here, the user clicked on a peak to show the ROIS drag box/line, but the user
        //  hasnt moved mouse out of ROI, or clicked down, or hit esc or anything
        const dx = (self.showDragLineWhileInRoi ? 1 : 0.5) * self.options.roiDragWidth;
        const info = self.roiBeingDragged;
        const lpx = info.xRangePx[0];
        const upx = info.xRangePx[1];
        const within_x = ((x >= (lpx-dx)) && (x <= (upx+dx)));
        
        if( !within_x )
          self.handleCancelRoiDrag();
      }else if( self.options.allowDragRoiExtent && !self.roiIsBeingDragged ) {
      
        //Also check if we are between ymin and ymax of ROI....
        const drawn_roi = self.getDrawnRoiForCoordinate( m, true );
        const dx = 0.5 * self.options.roiDragWidth;
        
        //Dont create handles for narrow ranges (make user zoom in a bit more), or if not on edge
        if( drawn_roi
          && drawn_roi.xRangePx
          && ((drawn_roi.xRangePx[1] - drawn_roi.xRangePx[0]) >= 10)
          && ((Math.abs(drawn_roi.xRangePx[0] - x) <= dx) || (Math.abs(drawn_roi.xRangePx[1] - x) <= dx) ) ){
          self.showRoiDragOption(drawn_roi, m, false);
        }else if( self.roiDragBoxes && !self.roiIsBeingDragged ){
          self.handleCancelRoiDrag();
        }
      }//if( self.roiDragBoxes && self.showDragLineWhileInRoi ) / else
    }//if / else {what to do}
      
    self.updateFeatureMarkers(-1);
  }
}


/*
 Returns array of pixel coordinates relative to this.vis (i.e. the actual chart area inside
 the x and y axises) and coordinates relative to the svg.
  That is, the returned value is: [x_vis, y_vis, x_svg, y_svg]
 */
SpectrumChartD3.prototype.getMousePos = function(){
  const pad_left = this.padding.leftComputed;
  const pad_top = this.padding.topComputed;
  
  if( d3.event )
  {
    const m = d3.mouse(this.vis[0][0]);
    if( m )
      return [m[0], m[1], m[0] + pad_left, m[1] + pad_top];
      
    const t = d3.event.changedTouches;
    if( t && t.length ){
      const vt = d3.touches(this.vis[0][0], t);
      if( vt && vt.length )
        return [vt[0][0], vt[0][1], vt[0][0] + pad_left, vt[0][1] + pad_top];
    }
  }//if( d3.event )

  if( this.lastMouseMovePos && this.lastTapEvent )
  {
    console.assert( this.lastTapEvent.changedTouches && this.lastTapEvent.changedTouches.length === 1,
                    "need to update how this.lastTapEvent is updated"  );
    
    if( this.lastTapEvent.timeStamp > this.mousedowntime ){
      console.assert( this.lastTapEvent.visCoordinates.length == 4, "lastTapEvent.visCoordinates should have 4 elements" );
      return this.lastTapEvent.visCoordinates;
    }
    return this.lastMouseMovePos;
  }
    
  if( this.lastMouseMovePos ){
    console.log( 'getMousePos returning lastMouseMovePos (', this.lastMouseMovePos, ")" )
    return this.lastMouseMovePos;
  }
  
  if( this.lastTapEvent )
    return this.lastTapEvent.visCoordinates;
  
  //We can fail to get mouse position if a mouse/finger hasnt been over the chart yet
  //console.assert( this.lastTapEvent, "Failed to find mouse position!" );
  console.warn( "Failed to find mouse position!" );

  // I dont think we ever get here..., but I guess we'll just return _something_
  return [0, 0, pad_left, pad_top];
}//getMousePos(...)


SpectrumChartD3.prototype.showRoiDragOption = function(info, mouse_px, showBoth ){
  const self = this;

  const roi = info.roi;
  
  if( !roi
      || (d3.event && (d3.event.altKey || d3.event.ctrlKey || d3.event.metaKey || d3.event.shiftKey || self.escapeKeyPressed)) )
  {
    self.handleCancelRoiDrag();
    return;
  }
  
  const dwidth = self.options.roiDragWidth;
  const mouse_x_px = mouse_px[0];

  self.roiBeingDragged = { roi: roi, yRangePx: info.yRangePx, xRangePx: info.xRangePx, color: (info.color ? info.color : 'black') };
  
  if( !self.roiDragBoxes ){
    //self.roiDragBoxes will hold the vertical line and little box and stuff
    //  We will move self.roiDragBoxes as we move the mouse
    self.roiDragBoxes = [];
    self.roiDragLines = [];
    self.roiDragArea = [];
    
    for( let i = 0; i < 2; ++i ){
      let g = this.vis.append("g");
      let l = g.append("line")
        .attr("class", "roiDragLine")
        .attr("x1", 0)
        .attr("x2", 0);
    
      g.append("rect")
        .attr("class", "roiDragBox")
        .attr("rx", 2)
        .attr("ry", 2)
        .attr("x", -5)
        .attr("y", -10)
        .attr("width", 10)
        .attr("height", 20);

      //Add some lines inside the box for a little bit of texture
      for( const px of [-2,2] ){
        g.append("line")
          .attr("class", "roiDragBoxLine")
          .attr("stroke-width", 0.85*self.options.spectrumLineWidth)
          .attr("x1", px)
          .attr("x2", px)
          .attr("y1", -6)
          .attr("y2", 6);
        }
    
      let a = g.append("rect")
        .attr("width", (showBoth ? 2 : 1) * dwidth )
        .attr("x", (showBoth ? -1 : -0.5)*dwidth )
        .style("cursor", "ew-resize" )
        .attr( "stroke", "none" )
        .style("fill", 'rgba(0, 0, 0, 0)');
    
      let adjustRoiFcn = function(){
        // Fires whenever mouse/finger moves, after signal is connected in startDragFcn
        if( !self.roiIsBeingDragged )
        {
          console.error( 'In adjustRoiFcn even though self.roiIsBeingDragged is false' );
          return;
        }
        
        const m = self.getMousePos();
        self.handleRoiDrag( m );
        d3.event.preventDefault();
        d3.event.stopPropagation();
      };
    
    
      let startDragFcn = function(){
        // Fires when you click down in the self.roiDragBoxes areas
        const m = self.getMousePos();
        self.lastMouseMovePos = m;
        
        self.handleStartDragRoi( m );
        
        d3.event.preventDefault();
        d3.event.stopPropagation();
        
        d3.select(self.chart).on("mousemove.roidrag", adjustRoiFcn );
        d3.select(self.chart).on("touchmove.roidrag", adjustRoiFcn );
      };
        
      let endDragFcn = function(){
        // Fires when you let the mouse up, or lift your finger
        d3.select(self.chart).on("mousemove.roidrag", null );
        d3.select(self.chart).on("touchmove.roidrag", null );
        
        const m = self.getMousePos();
        self.lastMouseMovePos = m;
        self.handleMouseUpDraggingRoi( m );
        
        d3.event.preventDefault();
        d3.event.stopPropagation();
      };
    
      g.on("mousedown",  startDragFcn )
       .on("mouseup", endDragFcn )
       .on("touchstart",  startDragFcn )
       .on("touchend", endDragFcn );
    
      self.roiDragBoxes[i] = g;
      self.roiDragLines[i] = l;
      self.roiDragArea[i] = a;
    }//for( let i = 0; i < 2; ++i )
  }//if( !self.roiDragBoxes ){

  const lpx = self.xScale(roi.lowerEnergy);
  const upx = self.xScale(roi.upperEnergy);
  const on_lower = (Math.abs(lpx - mouse_x_px) < 0.5*dwidth);
  
  //d3.select('body').style("cursor", (on_lower || on_upper) ? "ew-resize" : "default");
    
  let y1 = 0, y2 = self.size.height
  if( info.yRangePx && info.yRangePx.length==2 ){
    const dheight = (showBoth ? 2 : 1) * self.options.roiDragLineExtent;
    y1 = Math.max(0,info.yRangePx[0] - dheight);
    y2 = Math.min(y2,info.yRangePx[1] + dheight);
  }
  
  const y_middle = 0.5*(y1 + y2);
  self.roiDragBoxes[0].attr("transform", "translate(" + lpx + "," + y_middle + ")");
  self.roiDragBoxes[1].attr("transform", "translate(" + upx + "," + y_middle + ")");
  
  d3.selectAll('.roiDragBoxLine').each( function(){
    d3.select(this).attr("stroke", self.roiBeingDragged.color );
  } );
      
  for( let i = 0; i < 2; ++i ){
    self.roiDragLines[i]
      .attr("y1", y1 - y_middle)
      .attr("y2", y2 - y_middle)
      .attr("stroke", self.roiBeingDragged.color );
      
    self.roiDragArea[i]
        .attr("y", y1 - y_middle)
        .attr("height", y2 - y1);
  }
  
  if( !showBoth ){
    // We are showing the drag-line(s) because the mouse went over the edge of the ROI;
    //  It doesnt look quite right to me to show lines on both sides of the ROI in this
    //  case, so we'll just hide the other line to keep the code simpler.
    self.roiDragBoxes[on_lower ? 1 : 0].style("visibility", "hidden");
  }
  
  self.showDragLineWhileInRoi = showBoth;
};//showRoiDragOption()



SpectrumChartD3.prototype.handleRoiDrag = function(m){
  //We have clicked the mouse down on the edge of a ROI, and now moved the mouse 
  //  - update roiDragBoxes location
  let self = this;
  
  const x = m[0], y = m[1];

  if( d3.event.altKey || d3.event.ctrlKey || d3.event.metaKey
     || d3.event.shiftKey || self.escapeKeyPressed
     || !self.roiBeingDragged )
  {
    self.handleCancelRoiDrag();
    return;
  }
  
  if( self.roiDragLastCoord && Math.abs(x-self.roiDragLastCoord[0]) < 1 )
    return;

  let roiinfo = self.roiBeingDragged;
  let roi = roiinfo.roi;
  let mdx = self.roiDragMouseDown;
  let xcenter = x + mdx[1] - mdx[0];
  let energy = self.xScale.invert(xcenter);
  let counts = self.yScale.invert(y);
  let lowerEdgeDrag = mdx[3];
        
  self.roiDragLastCoord = [x,y,energy,counts];
  
  
  let y1 = 0, y2 = self.size.height;
  if( roiinfo.yRangePx && roiinfo.yRangePx.length==2 ){
    const dheight = (self.showDragLineWhileInRoi ? 2 : 1) * self.options.roiDragLineExtent;
    y1 = Math.max(0,roiinfo.yRangePx[0] - dheight);
    y2 = Math.min(y2,roiinfo.yRangePx[1] + dheight);
  }
  
  const y_middle = 0.5*(y2 + y1);
  self.roiDragBoxes[lowerEdgeDrag ? 0 : 1].attr("transform", "translate(" + xcenter + "," + y_middle + ")");
  
  for( let i = 0; i < 2; ++i ){
    self.roiDragLines[i]
      .attr("y1", y1 - y_middle)
      .attr("y2", y2 - y_middle);
  }

  //Emit current position, no more often than twice per second, or if there
  //  are no requests pending.
  let emitFcn = function(){
    self.roiDragRequestTime = new Date();
    window.clearTimeout(self.roiDragRequestTimeout);
    self.roiDragRequestTimeout = null;
    self.roiDragRequestTimeoutFcn = null;

    let new_lower_energy = lowerEdgeDrag ? energy : roi.lowerEnergy;
    let new_upper_energy = lowerEdgeDrag ? roi.upperEnergy : energy;
    let new_lower_px = lowerEdgeDrag ? xcenter : self.xScale(roi.lowerEnergy);
    let new_upper_px = lowerEdgeDrag ? self.xScale(roi.upperEnergy) : xcenter;
    
    self.WtEmit(self.chart.id, {name: 'roiDrag'}, new_lower_energy, new_upper_energy, new_lower_px, new_upper_px, roi.lowerEnergy, false );
  };

  let timenow = new Date();
  if( self.roiDragRequestTime === null || (timenow-self.roiDragRequestTime) > 500 ){
    emitFcn();
  } else {
    let dt = Math.min( 500, Math.max(0, 500 - (timenow - self.roiDragRequestTime)) );
    window.clearTimeout( self.roiDragRequestTimeout );
    self.roiDragRequestTimeoutFcn = emitFcn;
    self.roiDragRequestTimeout = window.setTimeout( function(){ 
      if(self.roiDragRequestTimeoutFcn) 
        self.roiDragRequestTimeoutFcn();
    }, dt );
  }

};//SpectrumChartD3.prototype.handleRoiDrag = ...



SpectrumChartD3.prototype.handleStartDragRoi = function(mouse_pos_px){
  let self = this;
  
  console.assert( self.roiDragBoxes, 'handleStartDragRoi: shouldnt be here when !self.roiDragBoxes' );
  if( !self.roiDragBoxes )
    return;
  
  const energy = self.xScale.invert(mouse_pos_px[0]);
  const lpx = self.roiBeingDragged.xRangePx[0];
  const upx = self.roiBeingDragged.xRangePx[1];
  
  const dx = (this.showDragLineWhileInRoi ? 1 : 0.5) * self.options.roiDragWidth;
  
  const on_lower = (Math.abs(lpx - mouse_pos_px[0]) <= dx);
  const on_upper = (Math.abs(upx - mouse_pos_px[0]) <= dx);
  
  console.assert( on_lower || on_upper, 'handleStartDragRoi: not on upper or lower ROI?', lpx, upx, mouse_pos_px[0], this.showDragLineWhileInRoi, self.options.roiDragWidth, dx );
  if( !on_lower && !on_upper ){
    console.trace();
    return;
    }
  
  self.roiIsBeingDragged = true;
  //self.showDragLineWhileInRoi = false;
  
  const roiinfo = self.roiBeingDragged;
  const roiPx = (on_lower ? lpx : upx);
  self.roiDragMouseDown = [mouse_pos_px[0], roiPx, energy, on_lower];
  
  //d3.select('body').style("cursor", "ew-resize");
  
  const edge_index = on_lower ? 0 : 1;
  self.roiDragBoxes[edge_index].attr("class", "roiDragBox active");
  self.roiDragLines[edge_index].attr("class", "roiDragLine active");
};//SpectrumChartD3.prototype.handleStartDragRoi


SpectrumChartD3.prototype.handleCancelRoiDrag = function(){
  let self = this;
  if( !self.roiDragBoxes && !self.fittingPeak )
    return;
    
  if( self.roiDragBoxes ){
    self.roiDragBoxes[0].remove();
    self.roiDragBoxes[1].remove();
  }
  
  const wasBeingDrug = self.roiIsBeingDragged;
  
  self.roiDragBoxes = null;
  self.roiDragLines = null;
  self.roiDragArea = null;
  self.roiBeingDragged = null;
  self.roiDragLastCoord = null;
  self.roiDragRequestTime = null;
  self.roiBeingDrugUpdate = null;
  self.roiIsBeingDragged = false;
  self.fittingPeak = null;
  //self.forcedFitRoiNumPeaks = -1;
  self.showDragLineWhileInRoi = false;
  window.clearTimeout(self.roiDragRequestTimeout);
  self.roiDragRequestTimeout = null;
  self.roiDragRequestTimeoutFcn = null;
  d3.select('body').style("cursor", "default");
  d3.select(self.chart).on("mousemove.roidrag", null );
  
  // If we were drawing a modified ROI, lets put it back to original state
  if( wasBeingDrug )
    self.redraw()();
};


SpectrumChartD3.prototype.handleMouseUpDraggingRoi = function( m ){
  const self = this;
  
  if( !self.roiIsBeingDragged ){
    self.handleCancelRoiDrag();
    return;
  }

  const roi = self.roiBeingDragged.roi;
  const x = m[0], y = m[1];
  
  const mdx = self.roiDragMouseDown; // [m[0], roiPx, energy, isLowerEdge];
  const xcenter = x + mdx[1] - mdx[0];
  const energy = self.xScale.invert(xcenter);

  const lowerEdgeDrag = mdx[3];
  const new_lower_energy = lowerEdgeDrag ? energy : roi.lowerEnergy;
  const new_upper_energy = lowerEdgeDrag ? roi.upperEnergy : energy;
  const new_lower_px = lowerEdgeDrag ? xcenter : self.xScale(roi.lowerEnergy);
  const new_upper_px = lowerEdgeDrag ? self.xScale(roi.upperEnergy) : xcenter;

  self.WtEmit( self.chart.id, {name: 'roiDrag'},
               new_lower_energy, new_upper_energy, new_lower_px, new_upper_px,
              roi.lowerEnergy, true );

  self.handleCancelRoiDrag();
};//SpectrumChartD3.prototype.handleMouseUpDraggingRoi


/* This only updates the temporary ROI.  When you want changes to become final
   you need to call setSpectrumData(...) - this could maybe get changed because
   it is very inefficient
*/
SpectrumChartD3.prototype.updateRoiBeingDragged = function( newroi ){
  let self = this;
  
  if( !self.roiIsBeingDragged && !self.fittingPeak )
    return;

  window.clearTimeout(self.roiDragRequestTimeout);
  self.roiDragRequestTimeout = null;
  if( self.roiDragRequestTimeoutFcn ){
    self.roiDragRequestTimeoutFcn();
    self.roiDragRequestTimeoutFcn = null;
  }

  self.roiDragRequestTime = null;

  self.roiBeingDrugUpdate = newroi;
  self.redraw()();
  self.updateFeatureMarkers(-1);
};


SpectrumChartD3.prototype.handleChartMouseLeave = function() {
  var self = this;

  return function () {
      if (!d3.event)
        return;

      if( !d3.select(d3.event.toElement)[0].parentNode
          || d3.event.toElement === document.body
          || d3.event.toElement.nodeName === "HTML"
          || d3.event.toElement.nodeName === "DIV"
          || d3.event.toElement.offsetParent === document.body) {
        self.handleCancelMouseDeletePeak();
        self.handleCancelMouseRecalibration();
        self.handleCancelMouseZoomInX();
        self.handleCancelMouseZoomY();
        self.handleCancelMouseCountGammas();
        self.handleCancelRoiDrag();
      }

      self.updateFeatureMarkers(-1);
      
      if( self.currentKineticRefLine ){
        self.currentKineticRefLine = null;
        self.drawRefGammaLines();
      }

      self.mousedOverRefLine = null;
      self.refLineInfo.style("display", "none");
      self.mouseInfo.style("display", "none");
      
      const ref = self.vis.selectAll("g.ref");
      ref.select("line.temp-extension").remove();
      ref.select("circle.ref-hover-indicator").remove();
      ref.selectAll("line")
        .attr("stroke-width", self.options.refLineWidth );
      ref.select(".major-extension")
        .style("opacity", 0.5)
        .style("stroke-width", self.options.refLineWidth);
  }
}

SpectrumChartD3.prototype.handleChartMouseUp = function() {
  var self = this;

  return function () {
    self.xaxisdown = null;
    self.yaxisdown = Math.NaN;
    d3.select(document.body).style("cursor", "default");

    /* Here we can decide to either zoom-in or not after we let go of the left-mouse button from zooming in on the CHART.
       In this case, we are deciding to zoom-in if the above event occurs.

       If you want to do the opposite action, you call self.handleCancelMouseZoomInX()
     */

    self.handleMouseUpDeletePeak();
    self.handleMouseUpZoomX();
    self.handleMouseUpDraggingRoi( self.getMousePos() );
    self.handleMouseUpZoomY();
    self.handleMouseUpRecalibration();
    self.handleMouseUpCountGammas();
    self.drawRefGammaLines();

    self.lastMouseMovePos = null;
    self.sliderChartMouse = null;
  }
}

SpectrumChartD3.prototype.handleChartWheel = function () {
  var self = this;

  return function() {
    if (!d3.event)
      return;

    /* Keep event from bubbling up any further */
    d3.event.preventDefault();
    d3.event.stopPropagation();
    
    /*Get mouse pixel x and y location */
    var m = d3.mouse(self.vis[0][0]);

    /* Handle y axis zooming if wheeling in y-axis */
    if (m[0] < 0 && m[1] > 0 && m[0] < self.size.height && self.options.wheelScrollYAxis) {
      self.handleYAxisWheel();
      return;
    }
  }
}

SpectrumChartD3.prototype.handleChartTouchStart = function() {
  var self = this;

  return function() {
    d3.event.preventDefault();
    d3.event.stopPropagation();

    //console.log('handleChartTouchStart: nchart=' + d3.touches(d3.select(this.chart)).length + ', ndoc=' + d3.touches(document.body).length);
    //console.log( 'handleChartTouchStart' );
  }
}

SpectrumChartD3.prototype.handleChartTouchEnd = function() {
  var self = this;

  return function() {
    d3.event.preventDefault();
    d3.event.stopPropagation();

//const ntotaltouches = d3.touches(d3.select(self.chart)) ? d3.touches(d3.select(self.chart)).length : 0;
console.log( 'handleChartTouchEnd, self.touchesOnChart=' + (self.touchesOnChart ? self.touchesOnChart.length : 0 ) );

    self.sliderBoxDown = false;
    self.leftDragRegionDown = false;
    self.rightDragRegionDown = false;
    self.sliderChartTouch = null;
    self.savedSliderTouch = null;
  }
}

/** Returns the ROI path object corresponding to the coordinates passed in.
 @param coordinates the [x,y] coordinates of e.g., mouse or touch
 @returns the roi, ex. {path:..., paths:..., roi: {type:'linear',...}, lowerEnergy: 50, upperEnergy: 70, yRangePx: [12, 34], xRangePx: [40,50], color: 'black', isOutline: true, isFill: false, peak: {...}}
 */
SpectrumChartD3.prototype.getDrawnRoiForCoordinate = function( coordinates ){
  if( !coordinates || (coordinates.length < 2) || !this.peakPaths || !this.peakPaths.length )
    return null;
    
  let paths = null;
  const x = coordinates[0];
  const dx = 0.5 * this.options.roiDragWidth;
    
  // TODO: use a bisect method to find ROI (speed things up when we have tons and tons of peaks)
    
  for( let j = 0; j < this.peakPaths.length; ++j ){
    const info = this.peakPaths[j];
    if( !info.isOutline || !info.xRangePx )
      continue;
    
    const lpx = info.xRangePx[0];
    const upx = info.xRangePx[1];
    let is_in_roi = ((x >= lpx) && (x <= upx));
      
    if( is_in_roi )
      return info;
      
    is_in_roi = ((x >= (lpx-dx)) && (x <= (upx+dx)));
      
    if( is_in_roi )
      paths = info;
    else if( paths )
      return paths; //assumes peakPaths are sorted
  }//for( loop over self.peakPaths )
    
  return paths;
}//SpectrumChartD3.prototype.getDrawnRoiForCoordinate( [x,y] )


SpectrumChartD3.prototype.setMouseDownRoi = function( coordinates ){
  console.assert( coordinates || (coordinates.length < 2), 'setMouseDownRoi: coordinates null' );
  /* Note: for mouse events leading to here, `self.mousedownpos` should be equal to `coordinates`, need to check for touch events */
  this.mouseDownRoi = this.getDrawnRoiForCoordinate( coordinates, true );
}


/**
 * -------------- Vis Event Handlers --------------
 */
SpectrumChartD3.prototype.handleVisMouseDown = function () {
  var self = this;

  return function () {
    self.dragging_plot = true;

    self.updateFeatureMarkers(null);

    const m = self.getMousePos();
    
    self.mousedowntime = new Date();
    self.lastMouseMovePos = self.mousedownpos = m;

    if( self.xaxisdown || !isNaN(self.yaxisdown) || self.legdown )
      return;
    
    /* Cancel the default d3 event properties */
    d3.event.preventDefault();
    d3.event.stopPropagation(); 

    self.zoomInYMouse = d3.event.metaKey ? m : null;
    self.leftMouseDown = null;
    self.zoominmouse = self.deletePeaksMouse = self.countGammasMouse = self.recalibrationMousePos = null; 
    self.touchHoldEmitted = false;
    
    if( self.currentKineticRefLine )
      self.drawRefGammaLines();

    /*
      On Firefox, clicking while holding the Ctrl key triggers a "right click".
      To fix this problem, we save the condition for d3.event.buttons to keep consistent for Firefox/Chrome browsers.
      The d3.event.button condition is saved for other browsers, including Safari.
    */
    if( (d3.event.buttons === 1 || d3.event.button === 0) && m[0] >= 0 && m[0] < self.size.width && m[1] >= 0 && m[1] < self.size.height ) {    /* if left click-and-drag and mouse is in bounds */
      //console.log("left mousedown");

      /* Initially set the escape key flag false */
      self.escapeKeyPressed = false;

      /* Set cursor to move icon */
      d3.select('body').style("cursor", "move");

      /* Set the zoom in/erase mouse properties */
      self.leftMouseDown = self.zoominmouse = self.deletePeaksMouse = self.countGammasMouse = self.recalibrationMousePos = m;
      self.peakFitMouseDown = m;
      self.origdomain = self.xScale.domain();
      self.zoominaltereddomain = false;
      self.zoominx0 = self.xScale.invert(m[0]);

      self.recalibrationStartEnergy = [ self.xScale.invert(m[0]), self.xScale.invert(m[1]) ];
      self.isRecalibrating = false;

      /* We are fitting peaks (if alt-key held) */
      self.fittingPeak = d3.event.ctrlKey && !d3.event.altKey && !d3.event.metaKey && !d3.event.shiftKey && d3.event.keyCode !== 27;
      //self.forcedFitRoiNumPeaks = -1;
      
      self.setMouseDownRoi( m );
      
      if( !self.roiDragLines) {
        /* Create the initial zoom box if we are not fitting peaks */
        if( !self.fittingPeak && !self.roiIsBeingDragged ) {
          var zoomInXBox = self.vis.select("#zoomInXBox")
              zoomInXText = self.vis.select("#zoomInXText");

          zoomInXBox.remove();
          zoomInXText.remove();

          /* Set the zoom-in box and display onto chart */
          zoomInXBox = self.vis.append("rect")
            .attr("id", "zoomInXBox")
            .attr("class","leftbuttonzoombox")
            .attr("width", 1 )
            .attr("height", self.size.height)
            .attr("x", m[0])
            .attr("y", 0)
            .attr("pointer-events", "none");
        }

        self.updateFeatureMarkers(-1);

        // Add in a little debounce; i.e., check to make sure we arent in the middle of a zoom-in animation, or equiv time frame if animation turned off
        self.zooming_plot = (!self.startAnimationZoomTime && ((self.mousedowntime - self.mouseUpTime) > 500) );
      }
      return false;
    } else if ( d3.event.button === 2 ) {    /* listen to right-click mouse down event */
      self.rightClickDown = d3.mouse(document.body);
      self.is_panning = false;
      self.origdomain = self.xScale.domain();

      /* Since this is the right-mouse button, we are not zooming in */
      self.zooming_plot = false;
    } else{

      console.log("Something else: d3.event.buttons=" + d3.event.buttons + ", d3.event.buttom=" + d3.event.button + ", m[0]=" + m[0] + ", self.size.width=" + self.size.width + ", m[1]=" + m[1] + ", self.size.height=" + self.size.height );
    }
  }
}


SpectrumChartD3.prototype.getMouseUpOrSingleFingerUpHandler = function( coords, modKeyDown) {
  const self = this;
  
  // coords = [x,y,pageX,pageY,energy,count]
  
  return function(){
    
    // Don't emit the tap/click signal if there was a tap-hold
    if( self.touchHoldEmitted )
      return;
    
    // Highlight peaks where tap/click position falls
    const roi = self.mouseDownRoi;
    if( roi && roi.peak && Array.isArray(roi.peak.Centroid) )
      self.highlightPeakAtEnergy( roi.peak.Centroid[0] );
    
    // Emit the tap signal, unhighlight any peaks that are highlighted
    console.log( "Emit TAP/click signal! - coords:", coords );
    self.WtEmit(self.chart.id, {name: 'leftclicked'}, coords[4], coords[5], coords[2], coords[3], self.currentRefLineInfoStr() );
    
    if( self.options.allowDragRoiExtent && self.mouseDownRoi && !modKeyDown ){
      self.showRoiDragOption(self.mouseDownRoi, [coords[0],coords[1]], true);
    }else{
      self.unhighlightPeak(null);
    }
    
    self.mousewait = null;
    self.mouseDownRoi = null;
  };
};//handleSingleFingerUp

SpectrumChartD3.prototype.currentRefLineInfoStr = function () {
  let ref_line = "";
  if( this.mousedOverRefLine && this.mousedOverRefLine.__data__ && this.mousedOverRefLine.__data__.parent )
    ref_line = this.mousedOverRefLine.__data__.parent.parent;
  return ref_line;
}


SpectrumChartD3.prototype.handleVisMouseUp = function () {
  var self = this;

  return function () {
    //console.log("mouseup on vis!");

    if (!d3.event)
      return;

    /* Set the client/page coordinates of the mouse */
    const m = d3.mouse(self.vis[0][0]);
    const x = m[0], y = m[1];
    const pageX = d3.event.pageX;
    const pageY = d3.event.pageY;
    const energy = self.xScale.invert(x);
    const count = self.yScale.invert(y);

    /* Handle any of default mouseup actions */
    self.mouseup()();

    /* We are not dragging the plot anymore */
    self.dragging_plot = false;
     
    /* Update feature marker positions */
    self.updateFeatureMarkers(null);

    /* If the slider chart is displayed and the user clicks on that, cancel the mouse click action */
    if (y >= (self.size.height + 
      (self.xaxistitle != null && !d3.select(self.xaxistitle).empty() ? self.xaxistitle[0][0].clientHeight + 20 : 20) + 
      self.padding.sliderChart)) {
      return;
    }

    /* Figure out right clicks */
    if (d3.event.button === 2 && !self.is_panning && !d3.event.ctrlKey) {
      if( self.highlightedPeak ){
        console.log("Should alter context menu for the highlighted peak" );  
      }
      
      self.WtEmit(self.chart.id, {name: 'rightclicked'}, energy, count, pageX, pageY, self.currentRefLineInfoStr());
      self.handleCancelAllMouseEvents()();
      
      return;
    }
    
    const modKeyPressed = (d3.event.altKey || d3.event.ctrlKey || d3.event.metaKey
                           || d3.event.shiftKey || self.escapeKeyPressed);
    
    /* Figure out clicks and double clicks */
    const nowtime = new Date();

    if (self.mousedownpos && self.dist(self.mousedownpos, d3.mouse(self.vis[0][0])) < 5) {
      // user let the mouse up without having moved it much since they put it down
      
      if( (nowtime - self.mousedowntime) < self.options.doubleClickDelay ) {
        // User did not hold the mouse down very long
        
        if( self.lastClickEvent && ((nowtime - self.lastClickEvent) < self.options.doubleClickDelay) ) {
          // This is a double-click
          if( self.mousewait ) {
            window.clearTimeout(self.mousewait);
            self.mousewait = null;
            self.mouseDownRoi = null;
          }
          
          self.WtEmit(self.chart.id, {name: 'doubleclicked'}, energy, count, self.currentRefLineInfoStr() );
        } else {
          // This is the first click - maybe there will be another click, maybe not
          if( !modKeyPressed )
            self.updateFeatureMarkers( energy );
          
          self.mousewait = window.setTimeout(
            self.getMouseUpOrSingleFingerUpHandler([x,y,pageX,pageY,energy,count],modKeyPressed),
            self.options.doubleClickDelay
          );
        }
        self.lastClickEvent = new Date();
      }
    }

    if( self.xaxisdown || !isNaN(self.yaxisdown) || self.legdown )
      return;

    /* Handle fitting peaks (if needed) */
    if (self.fittingPeak)
      self.handleMouseUpPeakFit();

    /* Handle zooming in x-axis (if needed) */
    self.handleMouseUpZoomX();

    /* Handle altering ROI mouse up. */
    self.handleMouseUpDraggingRoi(m);

    /* Handle deleting peaks (if needed) */
    self.handleMouseUpDeletePeak();
    
    /* Handle recalibration (if needed) */
    self.handleMouseUpRecalibration();
    
    if( self.zoomingYPlot || self.zoomInYMouse )
      self.handleMouseUpZoomY();

    /* Handle zooming in x-axis (if needed); We'll require the mouse having been down for at least 75 ms - if its less than this its probably unintented. */
    if( (nowtime - self.mousedowntime) > 75 )
      self.handleMouseUpZoomX();
    else if( self.zooming_plot ) // 20221029: temporary message to make sure working okay
      console.log( 'dbg: Prevented zoom-in with dt:', (nowtime - self.mousedowntime) ); 

    /* HAndle counting gammas (if needed) */
    self.handleMouseUpCountGammas();

    self.endYAxisScalingAction()();
    
    let domain = self.xScale.domain();
    if( !self.origdomain || !domain || self.origdomain[0]!==domain[0] || self.origdomain[1]!==domain[1] ){
      //console.log( 'Mouseup xrangechanged' );
      self.WtEmit(self.chart.id, {name: 'xrangechanged'}, domain[0], domain[1], self.size.width, self.size.height, false );
    }

    /* Delete any other mouse actions going on */
    self.leftMouseDown = null;
    self.zoominbox = null;
    self.zoominx0 = null;
    self.zoominmouse = null;
    self.fittingPeak = null;
    self.escapeKeyPressed = false;
    self.origdomain = null;

    /* Set delete peaks mode off */
    self.isDeletingPeaks = false;
    self.deletePeaksMouse = null;

    /* Set the count gammas mode off */
    self.countGammasMouse = null;

    /* Set the recalibration mode off */
    self.recalibrationMousePos = null;

    /* Set the right click drag and mouse down off */
    self.rightClickDown = null;
    //console.log( 'handleVisMouseUp, setting rightClickDown = null)');
    self.is_panning = false;

    /* Cancel default d3 event properties */
    //d3.event.preventDefault();
    //d3.event.stopPropagation();

    /* Not zooming in anymore */
    self.zooming_plot = false;

    /* Not using x-axis slider anymore */
    self.sliderBoxDown = false;
    self.leftDragRegionDown = false;
    self.rightDragRegionDown = false;
    self.sliderChartMouse = null;
    self.savedSliderMouse = null;
  }
}

SpectrumChartD3.prototype.handleVisWheel = function () {
  var self = this;

  return function () {
    var e = d3.event;

    /*Keep event from bubbling up any further */
    e.preventDefault();
    e.stopPropagation();

    /*If the user is doing anything else, return */
    /*Note that if you do a two finger pinch on a mac book pro, you get e.ctrlKey==true and e.composed==true  */
    if( !e || e.altKey || (e.ctrlKey && !e.composed) || e.shiftKey || e.metaKey || e.button != 0 /*|| e.buttons != 0*/ ) {
     console.log( "Special condition with wheel, ignoring mousewheel e=" + e + ", e.altKey="
                  + e.altKey + ", e.ctrlKey=" + e.ctrlKey + ", e.shiftKey=" + e.shiftKey
                  + ", e.metaKey=" + e.metaKey + ", e.button=" + e.button + ", e.buttons=" + e.buttons );
     return;
    }

    /*Get mouse pixel x and y location */
    var m = d3.mouse(self.vis[0][0]);

    /*Make sure within chart area */
    if( m[0] < 0 || m[0] > self.size.width || m[1] < 0 || m[1] > self.size.height ){

      /* If wheeling in y-axis labels, zoom in the y-axis range */
      if (m[0] < 0 && m[1] > 0 && m[1] < self.size.height && self.options.wheelScrollYAxis && self.rawData && self.rawData.spectra && self.rawData.spectra.length) {
        self.handleYAxisWheel();
        return;
      }

      console.log( "Scroll outside of vis, ignoring mousewheel" );
      return;
    }  

    /*If we are doing any other actions with the chart, then to bad. */
    if( self.dragging_plot || self.zoominbox || self.fittingPeak ){
      console.log( "Plot is being dragged, zoomed, or peak fit, ignoring mousewheel" );
      return;
    }

    /*Dont do anything if there is no data */
    if( !self.rawData || !self.rawData.spectra || self.rawData.spectra.length < 1 ){
      console.log( "No data, ignoring mousewheel" );
      return;
    }

    var mindatax, maxdatax, bounds, foreground;
    foreground = self.rawData.spectra[0];
    bounds = self.min_max_x_values();
    mindatax = bounds[0];
    maxdatax = bounds[1];
    let currentdomain = self.xScale.domain();

    /*Function to clear out any variables assigned during scrolling, or finish */
    /*  up any actions that should be done  */
    function wheelcleanup(e){
      //console.log( "mousewheel, stopped" );

      self.wheeltimer = null;
      self.scroll_start_x = null;
      self.scroll_start_y = null;
      self.scroll_start_domain = null;
      self.scroll_start_raw_channel = null;
      self.scroll_total_x = null;
      self.scroll_total_y = null;

      /* This fun doesnt get called until the timer expires, even if user takes finger
         off of touch-pad or mouse wheel.  This maybe introduces a little bit of potential
         to become out of sinc with things, but we're sending the current range no matter
         what, so I think its okay.
      */
      let domain = self.xScale.domain();
      self.WtEmit(self.chart.id, {name: 'xrangechanged'}, domain[0], domain[1], self.size.width, self.size.height, true);
    };

    //
    if ((currentdomain[0] <= (mindatax+0.00001) && currentdomain[1] >= (maxdatax-0.00001) && e.deltaX > 0)
        || (currentdomain[1] >= (maxdatax-0.00001) && currentdomain[0] <= (maxdatax+0.00001) && e.deltaX < 0)) {
      //console.log( 'Skipping dealing with mouse wheel - outside data range' );

      //If user has scrolled farther than allowed in either direction, cancel scrolling so that
      //  if they start going the other way, it will immediately react (if we didnt cancel
      //  things, they would have to go past the amount of scrolling they have already done
      //  before they would see any effect)
      if( self.wheeltimer ){
        window.clearTimeout(self.wheeltimer);
        wheelcleanup();
      }

      return;
    }

    /*Here we will set a timer so that if it is more than 1 second since the */
    /*  last wheel movement, we will consider the wheel event over, and start  */
    /*  fresh. */
    /*  This is just an example, and likely needs changed, or just removed. */
    if( self.wheeltimer ){
      /*This is not the first wheel event of the current user wheel action, */
      /*  lets clear the previous timeout (we'll reset a little below).  */
      window.clearTimeout(self.wheeltimer);
    } else {
      //console.log( 'Starting mousewheel action' );
      /*This is the first wheel event of this user wheel action, lets record */
      /*  initial mouse energy, counts, as well as the initial x-axis range. */
      self.scroll_start_x = self.xScale.invert(m[0]);
      self.scroll_start_y = self.yScale.invert(m[1]);
      self.scroll_start_domain = self.xScale.domain();
      self.scroll_start_raw_channel = d3.bisector(function(d){return d;}).left(foreground.x, self.scroll_start_x);
      self.scroll_start_raw_channel = Math.max(0,self.scroll_start_raw_channel);
      self.scroll_start_raw_channel = Math.min(foreground.x.length-1,self.scroll_start_raw_channel);
      self.scroll_total_x = 0;
      self.scroll_total_y = 0;
      self.handleCancelRoiDrag();
    }

    /*scroll_total_x/y is the total number of scroll units the mouse has done */
    /*  since the user started doing this current mouse wheel. */
    self.scroll_total_x += e.deltaX;
    self.scroll_total_y += e.deltaY;

    var MAX_SCROLL_TOTAL = 1500;

    /*Zoom all the way out by the time we get self.scroll_total_y = +200, */
    /*  or, zoom in to 3 bins by the time self.scroll_total_y = -200; */
    self.scroll_total_y = Math.max( self.scroll_total_y, -MAX_SCROLL_TOTAL );
    self.scroll_total_y = Math.min( self.scroll_total_y, MAX_SCROLL_TOTAL );

    var initial_range_x = self.scroll_start_domain[1] - self.scroll_start_domain[0];
    var terminal_range_x;
    if( self.scroll_total_y > 0 ){
      terminal_range_x = (maxdatax - mindatax);
    } else {
     /*Find the bin one to the left of original mouse, and two to the right  */
     var terminalmin = Math.max(0,self.scroll_start_raw_channel - 1);
     var terminalmax = Math.min(foreground.x.length-1,self.scroll_start_raw_channel + 2);
     terminalmin = foreground.x[terminalmin];
     terminalmax = foreground.x[terminalmax];
     terminal_range_x = terminalmax - terminalmin; 
    }

    var frac_y = Math.abs(self.scroll_total_y) / MAX_SCROLL_TOTAL;
    var new_range = initial_range_x + frac_y * (terminal_range_x - initial_range_x);  

    /*Make it so the mouse is over the same energy as when the wheeling started */
    var vis_mouse_frac = m[0] / self.size.width;  

    var new_x_min = self.scroll_start_x - (vis_mouse_frac * new_range);

    var new_x_max = new_x_min + new_range;


    /*Now translate the chart left and right.  100 x wheel units is one initial  */
    /*  width left or right */
    /*  TODO: should probably make it so that on trackpads at least, there is */
    /*        is some threshold on the x-wheel before (like it has to be  */
    /*        greater than the y-wheel) before the panning is applied; this  */
    /*        would also imply treating the x-wheel using deltas on each event */
    /*        instead of using the cumulative totals like now. */
    var mouse_dx_wheel = Math.min(initial_range_x,new_range) * (self.scroll_total_x / MAX_SCROLL_TOTAL);
    new_x_min += mouse_dx_wheel;
    new_x_max += mouse_dx_wheel;

    if( new_x_min < mindatax ){
     new_x_max += (mindatax - new_x_min);
     new_x_min = mindatax; 
    }

    if( new_x_max > maxdatax ){
     new_x_min = Math.max(mindatax,new_x_min-(new_x_max-maxdatax));
     new_x_max = maxdatax;
    }
   
   /*Set a timeout to call wheelcleanup after a little time of not resieving  */
   /*  any user wheel actions. */
   self.wheeltimer = window.setTimeout( wheelcleanup, 250 );
   
   
    if( Math.abs(new_x_min - currentdomain[0]) < 0.000001 
        && Math.abs(new_x_max - currentdomain[1]) < 0.000001 )
    {
      /* We get here when we are fully zoomed out and e.deltaX==0. */
      return;
    }


    /*Finally set the new x domain, and redraw the chart (which will take care */
    /*  of setting the y-domain). */
    self.setXAxisRange(new_x_min, new_x_max, false, true);
    self.redraw()();

    self.updateFeatureMarkers(-1);
  }
}


SpectrumChartD3.prototype.handleVisTouchCancel = function(){
  const self = this;
  
  return function(){
    d3.event.stopPropagation();
    const touches = d3.event.changedTouches;
    
    const now = new Date();
    if( !self.canceledTouches || ((now - self.canceledTouches.time) > 500) ){
      self.canceledTouches = {};
      self.canceledTouches.touches = [];
    }
    
    for( let i = 0; i < touches.length; ++i )
      self.canceledTouches.touches.push( touches[i] );
    
    self.canceledTouches.time = new Date();
    
    if( self.touchesOnChart ){
      for( let i = 0; i < touches.length; ++i ) {
        const touch = self.touchesOnChart[touches[i].identifier];
        if( touch ){
          touch.wasCancelled = true;
          touch.origdomain = self.origdomain;
        }
      }
    }//if( self.touchesOnChart )
  }//function()
}//SpectrumChartD3.prototype.handleVisTouchCancel


SpectrumChartD3.prototype.handleVisTouchStart = function() {
  var self = this;

  return function() {
    /* Prevent default event actions from occurring (eg. zooming into page when trying to zoom into graph) */
    d3.event.preventDefault();
    d3.event.stopPropagation();

    /* Get the touches on the screen */
    var t = d3.touches(self.vis[0][0]),
        touchHoldTimeInterval = 800,
        evTouches = d3.event.changedTouches;
    
    /* Represent where we initialized our touch start value */
    self.touchStart = t;
    self.touchStartEvent = d3.event;
    self.touchPageStart = evTouches.length === 1 ? [evTouches[0].pageX, evTouches[0].pageY] : null; // Do we really need this?  OR maybe better yet should just use coordinates relative to <svg>

    if (t.length === 2) {
      self.zooming_plot = true;
      self.countGammasStartTouches = self.createPeaksStartTouches = self.touchStart;
      self.touchZoomStartEnergies = [self.xScale.invert(t[0][0]),self.xScale.invert(t[1][0])];
    }
    
    // If the touching is just begining, record the x domain:
    //  - if we start fitting for a peak (two horizantal fingers sliding to right), we will restore to these extents
    //  - we could have been panning, and now transitioned into zoom
    if( (!self.touchesOnChart || (Object.keys(self.touchesOnChart).length !== t.length))
        && ((t.length == 1) || (t.length === 2)) ){
      //self.origdomain will be set to null at the end of self.handleVisTouchEnd when there are no touches left
      self.origdomain = self.xScale.domain();
    }
    
    self.updateTouchesOnChart(d3.event);

    if (t.length === 1) {
      /* Boolean for the touch of a touch-hold signal */
      self.touchHoldEmitted = false;
      var origTouch = ((d3.event.touches && (d3.event.touches.length === 1)) ? d3.event.touches[0] : null);
      var energy = self.xScale.invert(t[0][0]),
          count = self.yScale.invert(t[0][1]);

      self.setMouseDownRoi( t[0] );
      
      if( self.roiDragBoxes && self.roiBeingDragged )
        self.handleCancelRoiDrag();
      
      
      self.touchHold = window.setTimeout( function() {
        // Clear the touch hold wait, the signal has already been emitted
        self.touchHold = null;

        if( !origTouch || self.touchHoldEmitted || !self.touchesOnChart )
          return;

        const keys = Object.keys(self.touchesOnChart);
        if( keys.length !== 1 )
          return;

        if( self.mousewait ) {
          window.clearTimeout(self.mousewait);
          self.mousewait = null;
        }
          
        var touch = self.touchesOnChart[keys[0]];
        var dx = Math.abs(origTouch.pageX - touch.pageX);
        var dy = Math.abs(origTouch.pageY - touch.pageY);
      
        // Emit the tap signal, unhighlight any peaks that are highlighted
        if ( dx <= 15 || dy <= 15 ) {
          console.log( "Emit TAP HOLD (RIGHT TAP) signal!", "\nenergy = ", energy, ", count = ", count, ", x = ", origTouch.pageX, ", y = ", origTouch.pageY );
          self.WtEmit(self.chart.id, {name: 'rightclicked'}, energy, count, origTouch.pageX, origTouch.pageY, self.currentRefLineInfoStr() );
          self.handleCancelAllMouseEvents()();
          self.unhighlightPeak(null);
          self.touchHoldEmitted = true;
        }
      }, touchHoldTimeInterval );
    }//if (t.length === 1)
    
    if( (t.length !== 1) && (self.roiIsBeingDragged || self.roiDragBoxes) ){
      self.handleCancelRoiDrag();
    }
      
    if( (t.length !== 1) && self.touchHold ){
      clearTimeout( self.touchHold );
      self.touchHold = null;
    }
  }//return function(){...}
}//SpectrumChartD3.prototype.handleVisTouchStart


SpectrumChartD3.prototype.handleVisTouchMove = function() {
  var self = this;
  
  /* Touch interaction helpers */
  function isDeletePeakSwipe() {

    if (!self.touchesOnChart)
      return false;

    var keys = Object.keys(self.touchesOnChart);

    /* Delete peak swipe = two-finger vertical swipe */
    if (keys.length !== 2)
      return false;

    var maxDyDiff = 15,
        minDxDiff = 25;

    var t1 = self.touchesOnChart[keys[0]],
        t2 = self.touchesOnChart[keys[1]];

    var dy1 = t1.startY - t1.pageY,
        dy2 = t2.startY - t2.pageY,
        dyDiff = Math.abs(dy2 - dy1);

    if (dyDiff > maxDyDiff || Math.abs(t1.pageX - t2.pageX) < minDxDiff) {
      return false;
    }

    var dy = Math.min(dy1,dy2),
        dx1 = t1.startX - t1.pageX,
        dx2 = t2.startX - t2.pageX,
        dx = Math.abs(dx1 - dx2);

    return dy > dx && dy > maxDyDiff;
  }

  function isControlDragSwipe() {

    if (!self.touchesOnChart)
      return false;

    if( self.fittingPeak )
      return true;
      
    var keys = Object.keys(self.touchesOnChart);

    if (keys.length !== 2)
      return false;

    var t1 = self.touchesOnChart[keys[0]],
        t2 = self.touchesOnChart[keys[1]];

    if (t1.startX > t1.pageX || t2.startX > t2.pageX)
      return false;

    if( !isFinite(t1.startX) || !isFinite(t1.pageX)
    || !isFinite(t2.startX) || !isFinite(t2.pageX)
    || !isFinite(t1.startY) || !isFinite(t1.pageY)
    || !isFinite(t2.startY) || !isFinite(t2.pageY) )
      return false;

    var startdx = t1.startX - t2.startX;
        nowdx = t1.pageX - t2.pageX;
        yavrg = 0.5*(t1.startY+t2.startY);

    if( Math.abs(yavrg-t1.pageY) > 20 || 
        Math.abs(yavrg-t2.pageY) > 20 || 
        Math.abs(startdx-nowdx) > 20 ) 
      return false;

    return Math.abs(t1.pageX - t1.startX) > 30;
  }

  function isAltShiftSwipe() {
    if( !self.touchesOnChart )
      return false;

    var keys = Object.keys(self.touchesOnChart);

    if( keys.length !== 2 ) 
      return false;

    var t1 = self.touchesOnChart[keys[0]],
        t2 = self.touchesOnChart[keys[1]];

    if( Math.abs(t1.startX-t2.startX) > 20 || Math.abs(t1.pageX-t2.pageX) > 25 )
      return false;

    return ( (t1.pageX - t1.startX) > 30 );
  }

  function isZoomInPinch( y_direction ){
    if (!self.touchesOnChart)
      return false;

    const keys = Object.keys(self.touchesOnChart);

    if (keys.length !== 2)
      return false;

    var touch1 = self.touchesOnChart[keys[0]];
    var touch2 = self.touchesOnChart[keys[1]];
    var adx1 = Math.abs( touch1.startX - touch2.startX );
    var adx2 = Math.abs( touch1.pageX  - touch2.pageX );
    var ady1 = Math.abs( touch1.startY - touch2.startY );
    var ady2 = Math.abs( touch1.pageY  - touch2.pageY );
    var ddx = Math.abs( adx2 - adx1 );
    var ddy = Math.abs( ady2 - ady1 );
    
    if( y_direction )
      return ((ddx < 0.5*ddy) && (ddy > 20));
    return ((ddy < ddx) && (ddx > 5));
  }
  

  return function() {
    /* Prevent default event actions from occurring (eg. zooming into page when trying to zoom into graph) */
    d3.event.preventDefault();
    d3.event.stopPropagation();

    // Update our map of touches on the chart
    self.updateTouchesOnChart(d3.event);
    
    // Get the touches on the chart
    const t = d3.touches(self.vis[0][0]);

    const touchPan = ((t.length === 1) && !self.roiIsBeingDragged); // Panning = one finger drag
    const deletePeakSwipe = isDeletePeakSwipe() && !self.currentlyAdjustingSpectrumScale; //two horizantal fingers swiping up
    const controlDragSwipe = isControlDragSwipe() && !self.currentlyAdjustingSpectrumScale; // Fit peak(s) by specyinf ROI (two horizantal finers moving from right to left)
    const altShiftSwipe = isAltShiftSwipe() && !self.currentlyAdjustingSpectrumScale; //Define region to sum gammas
    const zoomInXPinch = isZoomInPinch(false) && !self.currentlyAdjustingSpectrumScale; // Two horizantal fingers pinching in
    const zoomInYPinch = isZoomInPinch(true) && !self.currentlyAdjustingSpectrumScale; // Two horizantal fingers spreading out

    //console.log( 'handleVisTouchMove: touchPan=' + touchPan + ', deletePeakSwipe=' + deletePeakSwipe
    //          + ", controlDragSwipe=" + controlDragSwipe + ", altShiftSwipe=" + altShiftSwipe
    //            + ", zoomInXPinch=" + zoomInXPinch
    //            + ", zoomInYPinch=" + zoomInYPinch );

    if( controlDragSwipe ){
      // We check for fitting a peak by specifying a ROI first, because once we start this
      //  we dont want to switch to doing something else, since we have to complete the
      //  operation, so the user can decide to cancel the peak fit, or add the peak as a keeper,
      //  or else there could be a phantom peak displayed on the chart, that isnt tracked by the c++
      //console.log( "handleVisTouchMove: handleTouchMovePeakFit" );
      self.handleTouchMovePeakFit();
    }else if( deletePeakSwipe ){
      self.handleTouchMoveDeletePeak(t);
    }else if( altShiftSwipe ){
      self.updateGammaSum();
    }else if( zoomInXPinch ){
      self.handleTouchMoveZoomInX();
    }else if( zoomInYPinch ){
      self.handleTouchMoveZoomY();
    }else if( self.currentlyAdjustingSpectrumScale ){
      self.handleMouseMoveScaleFactorSlider()();
    }else if( self.roiIsBeingDragged ){
      self.handleRoiDrag( t[0] );
    }else{
      self.handleCancelTouchCountGammas();
      self.handleCancelTouchDeletePeak();
      self.handleCancelTouchPeakFit();
      self.handleTouchCancelZoomY();
    }

    /* Clear the touch hold signal if:
         - More than one touches on chart
         - No touch positions on the page detected
         - We moved our touches by > 5 pixels
    */
    let changedTouches = d3.event.changedTouches;
    if (self.touchHold) {
      if( t.length > 1 || !self.touchPageStart || (changedTouches.length !== 1)
          || self.dist([changedTouches[0].pageX, changedTouches[0].pageY], self.touchPageStart) > 5 ) {
        // Note that touchPageStart is from the current touch, so its ending position should be close to its start
        //  Not tested this condition works well
        // console.log( 'Clearing timeout' );
        
        window.clearTimeout(self.touchHold);
        self.touchHold = null;
      }//if( we know this isnt a touchHold anymore )
    }//if (self.touchHold)


    // Update mouse coordinates, feature markers on a touch pan action
    //  TODO: should we put this into the if/else above?
    if( touchPan ) {
      if( !self.rightClickDown ){
        self.rightClickDown = d3.mouse(document.body);
        self.is_panning = false;
        self.origdomain = self.xScale.domain();
        self.zooming_plot = false;
      }

      if( self.roiDragBoxes )
        self.handleCancelRoiDrag();
      
      self.mousemove()();
      self.handlePanChart();

      self.updateMouseCoordText();
      self.updateFeatureMarkers(-1);
    }//if( touchPan )

    // Hide the peak info */
    self.hidePeakInfo();

    // Delete the touch line */
    self.deleteTouchLine();

    self.lastTouches = t;
  };//function() that gets returned from handleVisTouchMove
};//SpectrumChartD3.prototype.handleVisTouchMove


SpectrumChartD3.prototype.handleVisTouchEnd = function() {
  var self = this;

  function updateTouchLine(touches) {

    /* If touches is not one touch long, or touch has x-value to left of displayed y-axis, dont update */
    if( !touches || (touches.length !== 1) || (touches[0][0] <= 0) ){
      self.deleteTouchLine();
      return;
    }

    const t = touches[0];
      
    /* Create the x-value touch line, or update its coordinates if it already exists */
    if (!self.touchLineX) {
      self.touchLineX = self.vis.append("line")
        .attr("class", "touchLine")
        .attr("y1", 0)
        .attr("y2", self.size.height);
    }
     
    self.touchLineX
      .attr("x1", t[0])
      .attr("x2", t[0]);

    /* Create the y-value touch line, or update its coordinates if it already exists */
    if (!self.touchLineY) {
      self.touchLineY = self.vis.append("line")
        .attr("class", "touchLine");
    }
    
    self.touchLineY
      .attr("x1", t[0]-10)
      .attr("x2", t[0]+10)
      .attr("y1", t[1])
      .attr("y2", t[1]);
  }//function updateTouchLine(touches)
  

  return function() {
    /* Prevent default event actions from occurring (eg. zooming into page when trying to zoom into graph) */
    d3.event.preventDefault();
    d3.event.stopPropagation();

    /* Get the touches on the screen */
    const t = d3.event.changedTouches;
    const touchesT = d3.touches(self.vis[0][0],t);
    
    const visTouches = d3.touches(self.vis[0][0]);
    if (visTouches.length === 0) {
      self.touchesOnChart = null;
    }

    if( self.zooming_plot && self.touchZoomStartEnergies && (visTouches.length !== 0)){
      self.zooming_plot = false;
      self.touchZoomStartEnergies = null;
      const d = self.xScale.domain();
      self.setXAxisRange(d[0], d[1], true, true);
      self.origdomain = d;
      
      // Reset pan starting positions and such; without this, if you pan with one finger, then add another finger to zoom, then remove the first finger, the chart will jump by calculating the delta from first finger start to current second finger pos.
      if( visTouches.length === 1 )
        self.rightClickDown = null;
    }

    /* Clear the single-tap wait if it exists */
    if (self.mousewait) {
      window.clearTimeout(self.mousewait);
      self.mousewait = null;
    }

    if( self.touchHold ){
      window.clearTimeout(self.touchHold);
      self.touchHold = null;
    }

    const wasRoiBeingDragged = self.roiIsBeingDragged;
    if( self.roiIsBeingDragged ){
      if( t.length === 1 )
        self.handleMouseUpDraggingRoi( touchesT[0] );
      else
        self.handleCancelRoiDrag();
    }
    
    /* Detect tap/double tap signals */
    if (t.length === 1 && touchesT.length === 1 && self.touchStart && !self.touchHoldEmitted ) {
      /* Get page, chart coordinates of event */
      const x = touchesT[0][0],
            y = touchesT[0][1],
            pageX = t[0].pageX,
            pageY = t[0].pageY
            currentTapEvent = d3.event,
            energy = self.xScale.invert(x),
            count = self.yScale.invert(y);

      /* Set the double tap setting parameters */
      const tapRadius = 35;                   /* Radius area for where a double-tap is valid (anything outside this considered a single tap) */

      
      /* Update the touch line position */
      if( wasRoiBeingDragged ){
        self.deleteTouchLine();
      } else {
        updateTouchLine(touchesT);
      }

      /* Emit the proper TAP/DOUBLE-TAP signal */
      if (self.touchPageStart && self.dist(self.touchPageStart, [pageX, pageY]) < tapRadius ) {
        if( self.lastTapEvent
            && ((currentTapEvent.timeStamp - self.lastTapEvent.timeStamp) < self.options.doubleClickDelay)
            && self.dist([self.lastTapEvent.changedTouches[0].pageX, self.lastTapEvent.changedTouches[0].pageY], [pageX, pageY]) < tapRadius) {

          // Emit the double-tap signal, clear any touch lines/highlighted peaks in chart
          //console.log( "Emit TAP doubleclicked signal! energy=", energy, ', count=', count );

          self.WtEmit(self.chart.id, {name: 'doubleclicked'}, energy, count, self.currentRefLineInfoStr());
          self.deleteTouchLine();
          self.unhighlightPeak(null);
        } else {
          // Create the single-tap wait action in case there is no more taps within double tap time interval
          self.mousewait = window.setTimeout(
            self.getMouseUpOrSingleFingerUpHandler([x,y,pageX,pageY,energy,count],false),
            self.options.doubleClickDelay
          );
        }//if( we have last tap event ) / else

        /* Set last tap event to current one */
        if( t && (t.length === 1) && currentTapEvent.timeStamp ){
          
          /* Update the feature marker positions (argument added for sum peaks) */
          self.updateFeatureMarkers( self.lastTapEvent ? self.lastTapEvent.energy : energy, energy );
          
          self.lastTapEvent = currentTapEvent;
          self.lastTapEvent.energy = energy;
          self.lastTapEvent.count = count;
          self.lastTapEvent.visCoordinates = [t[0][0], t[0][1], t[0][0] + self.padding.leftComputed, t[0][1] + self.padding.topComputed];
        }else{
          self.lastTapEvent = null;
        }
      }else{
        self.lastTapEvent = null;
      }// if (self.touchPageStart && self.dist(self.touchPageStart, [pageX, pageY]) < tapRadius ) / else
    }else{
      self.lastTapEvent = null;
      
      self.updateFeatureMarkers(-1);
    }//if (t.length === 1 && self.touchStart) /else
          
    self.updateTouchesOnChart(d3.event);
    self.updateMouseCoordText();
    self.updatePeakInfo();

    self.handleTouchEndCountGammas();
    self.handleTouchEndDeletePeak();
    self.handleCancelTouchPeakFit();
    self.handleTouchEndZoomY();

    self.touchZoom = false;
    self.touchStart = null;
    self.touchHoldEmitted = false;

    self.countGammasStartTouches = null;

    self.sliderBoxDown = false;
    self.leftDragRegionDown = false;
    self.rightDragRegionDown = false;
    self.sliderChartTouch = null;
    self.savedSliderTouch = null;

    if (visTouches.length === 0) {
      self.handleCancelAllMouseEvents()();
      self.origdomain = null;
    }//if (visTouches.length === 0)
  };//return function()
}//SpectrumChartD3.prototype.handleVisTouchEnd = function()...


/**
 * -------------- General Key/Mouse Event Handlers --------------
 */
SpectrumChartD3.prototype.mousemove = function () {
  var self = this;

  return function() {
    /*This function is called whenever a mouse movement occurs */
    var p = d3.mouse(self.vis[0][0]);

    self.updateFeatureMarkers(-1);
    self.updateMouseCoordText();
    self.updatePeakInfo();

    if( self.legdown ) {
      d3.event.preventDefault();
      d3.event.stopPropagation();

      var x = d3.event.x ? d3.event.x : d3.event.touches ?  d3.event.touches[0].clientX : d3.event.clientX,
          y = d3.event.y ? d3.event.y : d3.event.touches ?  d3.event.touches[0].clientY : d3.event.clientY,
          calculated_x = d3.mouse(self.vis[0][0])[0]; /* current mouse x position */

      if ( calculated_x >= -self.padding.leftComputed && y >= 0 && 
           calculated_x <= self.cx && y <= self.cy ) {
        /* console.log("change legend pos"); */
        var tx = (x - self.legdown.x) + self.legdown.x0;
        var ty = (y - self.legdown.y) + self.legdown.y0; 
        self.legend.attr("transform", "translate(" + tx + "," + ty + ")");
      }
    }

    if (self.adjustingBackgroundScale || self.adjustingSecondaryScale) {
      d3.event.preventDefault();
      d3.event.stopPropagation();
    }

    if (self.dragged) {
      /*We make it here if a data point is dragged (which is not allowed) */
      self.update(false); /* boolean set to false to indicate no animation needed */
    };
    
    if( self.xaxisdown && self.xScale.invert(p[0]) > 0) {
      /* make sure that xaxisDrag does not go lower than 0 (buggy behavior) */
      /* We make it here when a x-axis is clicked on, and has been dragged a bit */
      d3.select('body').style("cursor", "ew-resize");
      
      let newenergy = self.xScale.invert(p[0]);
      
      if ( self.rawData && self.rawData.spectra && self.rawData.spectra.length ) {
        let origxmin = self.xaxisdown[1];
        let origxmax = self.xaxisdown[2];
        let e_width = origxmax - origxmin;
        
        let newEnergyFrac = (newenergy - self.xScale.domain()[0]) / (origxmax - origxmin);
        let newX0 = self.xaxisdown[0] - newEnergyFrac*e_width;
        let newX1 = self.xaxisdown[0] + (1-newEnergyFrac)*e_width;
 
        let lowerData = self.rawData.spectra[0].x[0];
        let upperData = self.rawData.spectra[0].x[self.rawData.spectra[0].x.length-1];
       
        if( newX0 < lowerData ){
          newX1 = Math.min( upperData, newX1 + (lowerData - newX0) );
          newX0 = lowerData;
        }
       
        if( newX1 > upperData ){
          newX0 = Math.max( lowerData, newX0 - (newX1 - upperData) );
          newX1 = upperData;
        }
       
        //we'll emit on mouse up - ToDo: set a timer to periodically emit 'xrangechanged' while dragging.
        self.setXAxisRange(newX0, newX1, false, true);
        self.redraw()();
      }

      d3.event.preventDefault();
      d3.event.stopPropagation();
    };

    if (!isNaN(self.yaxisdown)) {
      d3.select('body').style("cursor", "ns-resize");
      var rupy = self.yScale.invert(p[1]),
          yaxis1 = self.yScale.domain()[1],
          yaxis2 = self.yScale.domain()[0],
          yextent = yaxis2 - yaxis1;
          
      if (rupy > 0) {
        var changey, new_domain;
        changey = self.yaxisdown / rupy;

        new_domain = [yaxis1 + (yextent * changey), yaxis1];
        
        let ydatarange = self.getYAxisDataDomain();
        let newYmin = new_domain[1];
        let newYmax = new_domain[0];
        let y0 = ydatarange[0];
        let y1 = ydatarange[1];
        
        if( self.options.yscale == "log" ) {
          if( newYmin > 0 && newYmax > newYmin && y1 > 0 ){
            let logY0 = ((y0<=0) ? -1 : Math.log10(y0));
            let logY1 = ((y1<=0) ? 0 : Math.log10(y1));
          
            let newLogUpperY = Math.log10(newYmax);
            
            if( newLogUpperY < logY1 ) {
              self.options.logYFracTop = 0;  //make sure we can at least see the whole chart.
            } else {
              let newfrac = (newLogUpperY - logY1) / (logY1 - logY0);
              if( !isNaN(newfrac) && isFinite(newfrac) && newfrac>=0 && newfrac<50 ){
                self.options.logYFracTop = (newLogUpperY - logY1) / (logY1 - logY0);
                //Should emit something noting we changed something
              }
            }
            
            //Dragging on the y-axis only adjusts the top fraction, not the bottom, so we wont set the bottom here (since it shouldnt change)
            //self.options.logYFracBottom should be between about 0 and 10
            //self.options.logYFracBottom = (logY1 - logY0) / (newYmin - logY0);
          }//if( new limits are reasonable )
        } else if( self.options.yscale == "lin" ) {
          let newfrac = (newYmax / y1) - 1.0;
          
          console.log( 'newfrac=' + newfrac );
          if( !isNaN(newfrac) && isFinite(newfrac) && newfrac>=0 && newfrac<50 ){
            self.options.linYFracTop = newfrac;
          }
        } else if( self.options.yscale == "sqrt" ) {
          //self.options.sqrtYFracBottom = 1 - (newYmin / y0);  //Shouldnt change though
          self.options.sqrtYFracTop = -1 + (newYmax / y1);
        }
      
        
        self.yScale.domain(new_domain);
        self.redraw()();
      }

      d3.event.preventDefault();
      d3.event.stopPropagation();
    }
  }
}

SpectrumChartD3.prototype.mouseup = function () {
  var self = this;
  return function() {
    d3.select('body').style("cursor", "auto");
    d3.select('body').style("cursor", "auto");
    if( self.xaxisdown ) {
      self.redraw()();
      self.xaxisdown = null;

      /*d3.event.preventDefault(); */
      /*d3.event.stopPropagation(); */
    };
    if (!isNaN(self.yaxisdown)) {
      self.redraw()();
      self.yaxisdown = Math.NaN;
      /*d3.event.preventDefault(); */
      /*d3.event.stopPropagation(); */
    }
    if (self.dragged) {
      self.dragged = null
    }

    self.sliderBoxDown = false;
    self.leftDragRegionDown = false;
    self.rightDragRegionDown = false;
    self.sliderChartMouse = null;
    self.savedSliderMouse = null;
    self.currentlyAdjustingSpectrumScale = null;
  }
}

SpectrumChartD3.prototype.keydown = function () {
  var self = this;
  return function() {

    /*if (!self.selected) return; */

    if( self.roiDragBoxes && (d3.event.ctrlKey || d3.event.altKey || d3.event.metaKey || d3.event.shiftKey) ){
      let needredraw = self.roiBeingDragged;
      self.handleCancelRoiDrag();
      if( needredraw )
        self.redraw()();
    }
    
    
    switch (d3.event.keyCode) {
      case 27: { /*escape */
        self.escapeKeyPressed = true;
        self.cancelYAxisScalingAction();
        self.handleCancelAllMouseEvents()();
        self.handleCancelAnimationZoom();
        self.handleCancelRoiDrag();
        self.redraw()();
      }
      
      case 38: { /* up arrow */
        if( self.candidateKineticRefLines && self.candidateKineticRefLines.length > 1 ) {
          self.cycleKineticRefLine(-1);
          d3.event.preventDefault();
        }
        break;
      }
      
      case 40: { /* down arrow */
        if( self.candidateKineticRefLines && self.candidateKineticRefLines.length > 1 ) {
          self.cycleKineticRefLine(1);
          d3.event.preventDefault();
        }
        break;
      }
      
      case 8: /* backspace */
      case 46: { /* delete */
        break;
      }
    }
    
    /* if the user is cntl/alt-dragging a ROI, they can force the number of peaks
     they would like in it by hitting the 1 through 9 keys.
     A value of self.forcedFitRoiNumPeaks equal or less (default) than zero means the fitting code should decide how many peaks
     (currently not enforcing
     */
    //if( self.fittingPeak && d3.event.keyCode >= 49 && d3.event.keyCode <= 57 ){
    //  self.forcedFitRoiNumPeaks = d3.event.keyCode - 48;
    //  const roi = self.roiBeingDrugUpdate;
    //  if( roi ){
    //    window.clearTimeout(self.roiDragRequestTimeout);
    //    self.WtEmit(self.chart.id, {name: 'fitRoiDrag'}, roi.lowerEnergy, roi.upperEnergy, self.forcedFitRoiNumPeaks, false, d3.event.pageX, d3.event.pageY );
    //  }
    //}
  }
}


SpectrumChartD3.prototype.updateTouchesOnChart = function (touchEvent) {
  const self = this;

  /* Don't do anything if touch event not recognized */
  if (!touchEvent || !touchEvent.type.startsWith("touch"))
    return false;

  /* Create dictionary of touches on the chart */
  if (!self.touchesOnChart)
    self.touchesOnChart = {};
    
  /* Add each touch start into map of touches on the chart */
  for (let i = 0; i < touchEvent.touches.length; i++) {
    const touch = touchEvent.touches[i];

    /* Add a new attribute to each touch: the start coordinates of the touch. We'll keep old value if touch already existed in our map */
    touch.startX = self.touchesOnChart[touch.identifier] ? self.touchesOnChart[touch.identifier].startX : touch.pageX;
    touch.startY = self.touchesOnChart[touch.identifier] ? self.touchesOnChart[touch.identifier].startY : touch.pageY;
    
    /* Add/replace touch into dictionary */
    self.touchesOnChart[touch.identifier] = touch;
  }

  /* Delete any touches that are not on the screen anymore (read from 'touchend' event) */
  if (touchEvent.type === "touchend") {
    for( let i = 0; i < touchEvent.changedTouches.length; ++i ) {
      const touch = touchEvent.changedTouches[i];

      if (self.touchesOnChart[touch.identifier])
        delete self.touchesOnChart[touch.identifier];
    }
  }

  //Check to make sure self.touchesOnChart has same number of entries as touchEvent.touches - if not, fix up
  const keys = Object.keys(self.touchesOnChart);
  if( keys.length && Object.keys(self.touchesOnChart).length !== touchEvent.touches.length ){
    // We get here when touchs were cancelled - we'll try to pick up where we left off if
    //  its been less than a second since the last cancelled touchs.
                    
    const now = new Date();
    if( self.canceledTouches && ((now - self.canceledTouches.time) < 1000) ){
      keys.forEach( function(key, index){
        const oldTouch = self.touchesOnChart[key];
        if( oldTouch.wasCancelled ){
          //find the closest touch on chart that hasnt already been used
          //  TODO: improve matching of old and new touches - and also should limit to only use new touches in touchEvent.touches
          let minDist = 99999, nearestTouch = null;
          for (let i = 0; i < touchEvent.touches.length; i++) {
            const touch = touchEvent.touches[i];
            const dist = Math.sqrt( Math.pow(touch.pageX - oldTouch.pageX,2) + Math.pow(touch.pageY - oldTouch.pageY,2) );
            if( (dist < minDist) && !touch.alreadyMatchedToCancelled ){
              minDist = dist;
              nearestTouch = touch;
            }
          }
          if( minDist < 200 ){ //200px distance is arbitrary
            //console.log( "Matched cancelled touch up to new touch. minDist:", minDist, " will update origdomain from", self.origdomain, " to ", oldTouch.origdomain  );
            nearestTouch.alreadyMatchedToCancelled = true;
            self.touchesOnChart[nearestTouch.identifier].startX = oldTouch.startX;
            self.touchesOnChart[nearestTouch.identifier].startY = oldTouch.startY;
            self.origdomain = oldTouch.origdomain;
          }else{
            console.error( "Couldnt match cancelled touch up to new touch; oldTouch:", oldTouch, ", touchEvent.touches:", touchEvent.touches );
          }
        }
      } );
    }else{
      console.error( "Couldnt recover cancelled touches. touchesOnChart:",
                      self.touchesOnChart, ", touchEvent.touches", touchEvent.touches );
    }
    
    const trimmed = {};
    for (let i = 0; i < touchEvent.changedTouches.length; ++i ) {
      const ident = touchEvent.changedTouches[i].identifier;
      trimmed[ident] = self.touchesOnChart[ident];
    }
    self.touchesOnChart = trimmed;
  }//if( we have become out of sync on how many touches are on chart )
}//SpectrumChartD3.prototype.updateTouchesOnChart


SpectrumChartD3.prototype.handleCancelAllMouseEvents = function() {
  var self = this;

  return function () {

    d3.select(document.body).style("cursor", "default");
    self.xaxisdown = null;
    self.yaxisdown = Math.NaN;

    /* Cancel recalibration */
    self.handleCancelMouseRecalibration();

    /* Cancel deleting peaks */
    self.handleCancelMouseDeletePeak();

    /* Cancel zooming in x-axis */
    self.handleCancelMouseZoomInX();

    /* Cancel zooming in y-axis */
    self.handleCancelMouseZoomY();

    /* Cancel counting gammas */
    self.handleCancelMouseCountGammas();

    /* Cancel dragging of ROI. */
    self.handleCancelRoiDrag();
    
    /* Canceling out all mouse events for chart */
    self.zooming_plot = false;
    self.dragging_plot = false;
    self.recalibrationStartEnergy = null;

    /* Cancel all kept mouse positions for mouse events */
    self.leftMouseDown = null;
    self.zoominmouse = self.deletePeaksMouse = self.countGammasMouse = self.recalibrationMousePos = null;
    self.rightClickDown = null;
    //console.log( 'handleCancelAllMouseEvents, setting rightClickDown = null)');

    /* Cancel all legend interactions */
    self.legdown = null;

    /* Cancel all slider drag region interactions */
    self.sliderBoxDown = false;
    self.leftDragRegionDown = false;
    self.rightDragRegionDown = false;

    /* Cancel all scaler widget interactions */
    self.endYAxisScalingAction()();
  }
}

SpectrumChartD3.prototype.drawSearchRanges = function() {
  var self = this;

  if( !self.searchEnergyWindows || !self.searchEnergyWindows.length ){
    self.vis.selectAll("g.searchRange").remove();
    return;
  }

  let domain = self.xScale.domain();
  let lx = domain[0], ux = domain[1];

  let inrange = [];
  
  self.searchEnergyWindows.forEach( function(w){
    let lw = w.energy - w.window;
    let uw = w.energy + w.window;

    if( (uw > lx && uw < ux) || (lw > lx && lw < ux) || (lw <= lx && uw >= ux) )
      inrange.push(w);
  } );

  var tx = function(d) { return "translate(" + self.xScale(Math.max(lx,d.energy-d.window)) + ",0)"; };
  var gy = self.vis.selectAll("g.searchRange")
            .data( inrange, function(d){return d.energy;} )
            .attr("transform", tx)
            .attr("stroke-width",1);

  var gye = gy.enter().insert("g", "a")
    .attr("class", "searchRange")
    .attr("transform", tx);

  let h = self.size.height;
  
  gye.append("rect")
    //.attr("class", "d3closebut")
    .attr('y', '0' /*function(d){ return self.options.refLineTopPad;}*/ )
    .attr("x", "0" )
    .style("fill", 'rgba(255, 204, 204, 0.4)')
     ;
     
  var stroke = function(d) { return d.energy>=lx && d.energy<=ux ? '#4C4C4C' : 'rgba(0,0,0,0);'; };

  gye.append("line")
    .attr("y1", h )
    .style("stroke-dasharray","4,8")
     ;

  /* Remove old elements as needed. */
  gy.exit().remove();

  gy.select("rect")
    .attr('height', h /*-self.options.refLineTopPad*/ )
    .attr('width', function(d){ 
      let le = Math.max( lx, d.energy - d.window );
      let ue = Math.min( ux, d.energy + d.window );
      return self.xScale(ue) - self.xScale(le); 
    } );
 
  
  let linepos = function(d){ 
    let le = Math.max( lx, d.energy - d.window );
    return self.xScale(d.energy) - self.xScale(le) /* - 0.5*/; 
  };

  gy.select("line")
    .attr("stroke", stroke )
    .attr("x1", linepos )
    .attr("x2", linepos )
    .attr("y1", h )  //needed for initial load sometimes
    .attr("y2", '0' /*function(d){ return self.options.refLineTopPad; }*/ );
}//drawSearchRanges(...)



SpectrumChartD3.prototype.drawHighlightRegions = function(){
  const self = this;
  
  if( !Array.isArray(self.highlightRegions) || self.highlightRegions.length===0 ){
    // self.setHighlightRegions(...) already removed the highlight regions
    return;
  }
  
  const lx = self.xScale.domain()[0], ux = self.xScale.domain()[1], h = self.size.height;
 
  const inrange = [];
 
  //self.highlightRegions is array of form: [{lowerEnergy,upperEnergy,fill,hash,drawRegion(optional),text(optional)}]
  //  Where drawRegion may be either 'All' (default if field not present), or 'BelowData'
  self.highlightRegions.forEach( function(w){
    const lw = w.lowerEnergy, uw = w.upperEnergy;
   
    if( (uw > lx && uw < ux) || (lw > lx && lw < ux) || (lw <= lx && uw >= ux) ){
      console.assert( !w.drawRegion || (w.drawRegion === "All") || (w.drawRegion === "BelowData"), "Invalid HighlightRegion.drawRegion" );
      inrange.push(w);
    }
  } );
 
  
  // Find the spectrum to be used if we are drawing a `w.drawRegion==="BelowData"` region
  //  TODO: allow the region to specify which spectrum to use - right now just using the last foreground
  const spectra = self.rawData ? self.rawData.spectra : null;
  let spectrum = (spectra && (spectra.length > 0)) ? spectra[0] : null;
  for( let j = 0; j < spectra.length; ++j ){
    if( spectra[j].type === "FOREGROUND" ){
      spectrum = spectra[j];
      break;
    }
  }
  
  //spectrum: { rebinFactor: 1, id: 0, backgroundID: 1, type: "FOREGROUND", points: [{x:0.2,x:0},{x:1.2,x:5},...], x: [0.2,1.2,...], y: [0,5,...], lineColor: 'rgb(0,0,0)', peaks: [{...}],... }
  // spectrum.points gets updated as channels are combined and stuff
 
  const belowDataPointsGenerator = function(region){
    if( !spectrum )
      return [];
      
    const points = self.options.backgroundSubtract && ('bgsubtractpoints' in spectrum) ? spectrum.bgsubtractpoints : spectrum.points;
    if( !points || !spectrum.points.length )
      return [];
    
    const le = Math.max( lx, region.lowerEnergy );
    const ue = Math.min( ux, region.upperEnergy );
    const bi = d3.bisector(function(d){return d.x;});
    const pl = spectrum.points.length;
     
    const lowerIndex = bi.left(points,le,1) - 1;
    const upperIndex = bi.right(points,ue,1);
    
    const answer = points.slice( lowerIndex, upperIndex + 1 );
    if( !answer.length )
      return [];
      
    // Close the path, limiting to regions lower and upper range
    answer[0] = {x: le, y: answer[0].y};
    answer[answer.length-1] = {x: ue, y: answer[answer.length-1].y};
    answer.push( {x: ue, y: 0} );
    answer.push( {x: le, y: 0} );
    answer.push( {x: le, y: answer[0].y} );
   
    return answer;
  };//belowDataPointsGenerator function
 
 
  const gy = self.vis.selectAll("g.highlight")
    .data( inrange, function(d){return d.hash;} );
 
  // Update the points we will plot on each region
  inrange.forEach( function(w){
    const le = Math.max( lx, w.lowerEnergy ), ue = Math.min( ux, w.upperEnergy );
    
    w.isRect = (!w.drawRegion || (w.drawRegion !== "BelowData"));
    if( w.isRect )
      w.points = [{x: le, y: 0}, {x: ue, y: 0}, {x: ue, y: 1}, {x: le, y: 1}, {x: le, y: 0}];
    else
      w.points = belowDataPointsGenerator(w);
  } );
 
  const added = gy.enter().append("g")
    .attr("class", "highlight");
               
  added.append("path");
  added.append("text");
 
  // Remove elements no longer needed
  gy.exit().remove();
 
  // Define the line to use if we are doing a `w.drawRegion === "All"`
  const rect_line = d3.svg.line()
    .x( function(d){ return self.xScale(d.x); })
    .y( function(d){ return d.y===0 ? 0 : h; } );
  
  // Define the line to use if we are doing a `w.drawRegion === "BelowData"`
  const below_data_line = d3.svg.line()
    .interpolate("step-after")
    .x( function(d) { return self.xScale(d.x); })
    .y( function(d) {
      const y_px = self.yScale(d.y);
      return isNaN(y_px) ? 0 : Math.min(y_px,h);
    } );
    
  // Update the paths of all the elements
  gy.select("path")
    .attr("fill", function(d){return d.fill;} )
    .attr("d", function(d){ return d.isRect ? rect_line(d.points) : below_data_line(d.points); } );
      
  // Update text content and position
  gy.select("text")
    .attr("y", h-10 )
    .attr("x", function(d){ return self.xScale(0.5*(d.lowerEnergy + d.upperEnergy)); } )
    .text( function(d){return d.text ? d.text : ""} );
}//drawHighlightRegions(...)


/**
 * -------------- Reference Gamma Lines Functions --------------
 */
SpectrumChartD3.prototype.drawRefGammaLines = function() {
  /*Drawing of the reference lines is super duper un-optimized!!! */
  const self = this;

  if( (!self.refLines || !self.refLines.length || !self.refLines[0].lines  || !self.refLines[0].lines.length)
      && (!self.currentKineticRefLine || !self.currentKineticRefLine.lines || !self.currentKineticRefLine.lines.length ) ) {
    self.vis.selectAll("g.ref").remove();
    return;
  }

  const is_zoomming = (self.dragging_plot || self.zooming_plot || self.leftMouseDown || self.rightClickDown || self.zoomAnimationID);

  // To reduce the number of SVG elements in the DOM for some of the heavier isotopes,
  // we will remove lines that are super small, and if if we are actively zooming, we'll
  // remove even more to make things a little smoother
  const disp_thresh = is_zoomming ? 0.025 : 0.0001;

  function getLinesInRange(xrange,lines) {
    var bisector = d3.bisector(function(d){return d.e;});
    var lindex = bisector.left( lines, xrange[0] );
    var rindex = bisector.right( lines, xrange[1] );
    return lines.slice(lindex,rindex);
  }

  let reflines = [];
  if( self.refLines ){
    self.refLines.forEach( function(input) {
      let lines = getLinesInRange(self.xScale.domain(),input.lines);
      input.maxVisibleAmp = d3.max(lines, function(d){return d.h;});  /*same as lines[0].parent.maxVisibleAmp = ... */
      const threshold = disp_thresh * input.maxVisibleAmp;
      lines = lines.filter(function(d) { return (d.h >= threshold) || d.major; });
      reflines = reflines.concat( lines );
    });
  }

  if( !is_zoomming && self.currentKineticRefLine ){
    let lines = getLinesInRange(self.xScale.domain(),self.currentKineticRefLine.lines);
    self.currentKineticRefLine.maxVisibleAmp = d3.max(lines, function(d){return d.h;});
    const threshold = disp_thresh * self.currentKineticRefLine.maxVisibleAmp;
    lines = lines.filter(function(d) { return (d.h >= threshold) || d.major; });
    reflines = reflines.concat( lines );
  }

  reflines.sort( function(l,r){ return ((l.e < r.e) ? -1 : (l.e===r.e ? 0 : 1)); } );

  var tx = function(d) { return "translate(" + self.xScale(d.e) + ",0)"; };
  var gy = self.vis.selectAll("g.ref")
            .data( reflines, function(d){return d.id;} )
            .attr("transform", tx)
            .attr("stroke-width", self.options.refLineWidth );

  function stroke(d){ return d.color ? d.color : d.parent.color; };

  function dashfunc(d){
    const particles = ["gamma", "xray", "beta", "alpha",   "positron", "electronCapture", "cascade-sum", "S.E.",   "D.E." ];
    const dash      = [null,    ("3,3"),("1,1"),("3,2,1"), ("3,1"),    ("6,6"),           ("6,6"),       ("4,1"),  ("4,1")];
    const index = particles.indexOf(d.particle);
    if( index < 0 && (d.particle !== "gamma, xray") && (d.particle !== "sum-gamma")) { console.log( 'Invalid particle: ' + d.particle ); return null; } //We can get here when lines that shared an energy were combined, so d.particle might for example be "gamma, xray"
    return (index > -1) ? dash[index] : null;
  };

  const h = self.size.height;
  const m = Math.min(h,self.options.refLineTopPad); // leave 20px margin at top of chart

  gy.enter()
    .insert("g", "a")
    .attr("class", "ref")
    .attr("transform", tx)
    .append("line")
    .style("stroke-dasharray", dashfunc )
    .attr("stroke", stroke )
    .attr("y1", h )
    .attr("dx", "-0.5" );

  gy.exit().remove();  // Remove old elements as needed.
  
  /* Now update the height of all the lines.  If we did this in the gy.enter().append("line")
  line above then the values for existing lines wouldnt be updated (only
  the new lines would have correct height) */
  const y2Lin = function(d){ return Math.min(h - (h-m)*d.h/d.parent.maxVisibleAmp,h-2); };
  
  /*
  const y2Log = function(d){
    // Map so that b.r. of zero will give a value at the bottom of the y-axis, and
    //  the max visible b.r. will give a value at the maximum of the y-axis.  Doesnt give good results.
    const ydomain = self.yScale.domain();
    const equiv_data = ydomain[1] + (ydomain[0] - ydomain[1]) * (d.h / d.parent.maxVisibleAmp);
    return Math.min( self.yScale( equiv_data ), h-2 );
  };
  */
  
  gy.select("line")
    .attr("stroke-width", self.options.refLineWidth )
    .attr("y2", y2Lin )
    //.attr("y2", y2Log )
    .attr("y1", h );  //needed for initial load sometimes

  // Add dotted extension lines for major reference lines (only if refLineVerbosity >= 2)
  if( self.options.refLineVerbosity >= 2 ) {
    const majorLines = gy.filter(function(d) { return d.major; });
    
    // Remove any existing major line extensions
    gy.select("line.major-extension").remove();
    
    // Add dotted extension lines for major lines
    const extension_dash = "" + self.options.refLineWidthHover + "," + 2*self.options.refLineWidthHover;
    majorLines.append("line")
      .attr("class", "major-extension")
      .style("stroke-dasharray", extension_dash)
      .style("opacity", 0.5)
      .attr("stroke", stroke)
      .attr("stroke-width", self.options.refLineWidth)
      .attr("y1", function(d) { return y2Lin(d); })
      .attr("y2", self.options.refLineTopPad)
      .attr("x1", 0)
      .attr("x2", 0);
  } else {
    // Remove any existing major line extensions if verbosity < 2
    gy.select("line.major-extension").remove();
  }
}


SpectrumChartD3.prototype.clearReferenceLines = function() {
  var self = this;

  // No need to clear, we don't have any data
  if (!self.refLines)
    return;

  self.refLines = null;
  self.redraw()();
}

SpectrumChartD3.prototype.setReferenceLines = function( data ) {
  var self = this;
  self.vis.selectAll("g.ref").remove();
 
  var default_colors = ["#0000FF","#006600", "#006666", "#0099FF","#9933FF", "#FF66FF", "#CC3333", "#FF6633","#FFFF99", "#CCFFCC", "#0000CC", "#666666", "#003333"];

  if( !data ){
    this.refLines = null;
  } else {
    try {
      if( !Array.isArray(data) )
        throw "Input is not an array of reference lines";

      /*this.refLines = JSON.parse(JSON.stringify(data));  //creates deep copy, but then also have to go through and */
      //
      this.refLines = data;
      let index = 0;
      this.refLines.forEach( function(a,i){ 
        if( !a.color )
          a.color = default_colors[i%default_colors.length];
        if( !a.lines || !Array.isArray(a.lines) )
          throw "Reference lines does not contain an array of lines";

        a.lines.forEach( function(d){ 
          d.parent = a; 
          d.id = ++index;  //We need to assign an ID to use as D3 data, that is unique (energy may not be unique)

          /*{e:30.27,h:6.22e-05,particle:'xray',decay:'xray',el:'barium'} */
          /*particle in ["gamma", "xray", "beta", "alpha",   "positron", "electronCapture"]; */
          if( (typeof d.e !== "number") || (typeof d.h !== "number") || (typeof d.particle !== "string") )
            throw "Reference line is invalid (" + JSON.stringify(d) + ")";
        } );
      } );
    }catch(e){
      this.refLines = null;
      console.log( "invalid input to setReferenceLines" );
    }
  }//if( !data ) / else

  this.redraw()();
}

SpectrumChartD3.prototype.setShowRefLineInfoForMouseOver = function( show ) {
  var self = this;

  self.options.showRefLineInfoForMouseOver = show;
  self.redraw()();
}

SpectrumChartD3.prototype.setRefLineWidths = function( width, hoverWidth ) {
  this.options.refLineWidth = width;
  this.options.refLineWidthHover = hoverWidth;
  this.drawRefGammaLines();
  this.updateMouseCoordText();
}

SpectrumChartD3.prototype.setRefLineVerbosity = function( verbosity ) {
  this.options.refLineVerbosity = verbosity;
  this.drawRefGammaLines();
}

SpectrumChartD3.prototype.setKineticReferenceLines = function( data ) {
  // data looks like: { fwhm_fcn: function(e){...}, ref_lines: [{weight: 1, src_lines:{color: "red", parent: "Eu152", age: "2.5 HL", lines: [{e: 39.1, h: 0.000135, particle: "xray", desc_ind: 0, parent: Object},....], desc_strs: ["Eu152 to Sm152 via Electron Capture",...]},{...}] }
  // TODO: Need to validate format of `data`  
  
  if( data ){
    let index = 1000000;
    data.ref_lines.forEach( function(a){
      a.src_lines.lines.forEach( function(d){
        d.id = ++index;
        d.parent = a.src_lines;  
      } );  
    } );
  }//if( data )
  //console.log( "setKineticReferenceLines:", data );

  this.kineticRefLines = data;

  this.handleUpdateKineticRefLineUpdate();
}//SpectrumChartD3.prototype.setKineticReferenceLines


SpectrumChartD3.prototype.handleUpdateKineticRefLineUpdate = function(){
  const m = this.getMousePos(); // Get current mouse position for energy calculation
  if( !this.kineticRefLines || !this.kineticRefLines.ref_lines || !this.kineticRefLines.ref_lines.length || ((m[0] == 0) && (m[1] == 0)) ){
    if( this.currentKineticRefLine ){
      this.currentKineticRefLine = null;
      this.candidateKineticRefLines = [];
      this.currentKineticRefLineIndex = 0;
      this.stopKineticRefLineCycling();
      this.drawRefGammaLines();
    }
    return;
  }//if( we dont need to draw the lines )

  const energy = this.xScale.invert(m[0]);
  const peak_sigma = (this.kineticRefLines.fwhm_fcn ? this.kineticRefLines.fwhm_fcn(energy) : 2.35482) / 2.35482;
  
  // Arrays to collect ref_lines within 10px and their weights for debugging
  const refLineMinWeights = [];
 
  // Get current visible energy range
  const domain = this.xScale.domain();
  
  // Create bisector for efficient range finding (lines are sorted by energy)
  const energyBisector = d3.bisector(function(d) { return d.e; }).left;

  // Find the line with the lowest weight across all reference line groups
  let minWeight = Number.MAX_VALUE;
  let bestRefLine = null;
  
  for( const refLineGroup of this.kineticRefLines.ref_lines ) {
    if( !refLineGroup.src_lines || !refLineGroup.src_lines.lines || !refLineGroup.src_lines.lines.length ) continue;
    
    const src_weight = refLineGroup.weight || 1.0;
    let minGroupWeight = Number.MAX_VALUE;
    let bestLineInGroup = null;
    
    const lines = refLineGroup.src_lines.lines;
    
    // Find the range of lines in the visible energy range using bisector
    const startIndex = energyBisector(lines, domain[0]);
    const endIndex = energyBisector(lines, domain[1]);
    
    let maxVisibleH = 0.0;
    for( let i = startIndex; i <= endIndex && i < lines.length; i++ )
      maxVisibleH = Math.max(maxVisibleH, lines[i].h);
    if( maxVisibleH < 1E-32 )
      continue;

    // Only iterate over lines in the visible energy range
    for( let i = startIndex; i <= endIndex && i < lines.length; i++ ) {
      const line = lines[i];
      const delta_energy = Math.abs(energy - line.e);
      const weight = ((0.25 * peak_sigma + delta_energy) / (Math.max(line.h,1E-32) / maxVisibleH)) / src_weight;
      
      if( weight < minGroupWeight && delta_energy < 5*peak_sigma ) {
        minGroupWeight = weight;
        bestLineInGroup = line;
      }
      
      if( (weight < minWeight) && (delta_energy < 5*peak_sigma)  ) {
        minWeight = weight;
        bestRefLine = refLineGroup.src_lines;
      }
    }
    
    // Store min weight for this ref line group with best line
    if( minGroupWeight < Number.MAX_VALUE )
      refLineMinWeights.push({ 
        minWeight: minGroupWeight, 
        lines: refLineGroup, 
        bestLine: bestLineInGroup 
      });
  }
  
  // Sort by ascending weights, filter (8x min weight), and limit to 5 elements
  refLineMinWeights
    .splice(0, refLineMinWeights.length, ...refLineMinWeights
    .sort((a, b) => a.minWeight - b.minWeight)
    .filter((item, i) => i < 5 && item.minWeight <= (refLineMinWeights[0]?.minWeight || 1) * 8.0));
  
  if( bestRefLine && this.refLines && this.refLines.some( input => bestRefLine.parent == input.parent ) ){
    bestRefLine = null;
  }
  
  // Store candidate lines for cycling with their weights and best lines
  const newCandidates = refLineMinWeights
    .filter(item => !this.refLines || !this.refLines.some( input => item.lines.src_lines.parent == input.parent ))
    .map(item => ({ 
      lines: item.lines.src_lines, 
      weight: item.minWeight,
      bestLine: item.bestLine
    }));
  
  // Check if candidate lines (parents) changed, not just weights
  const candidateParentsChanged = !this.candidateKineticRefLines 
    || this.candidateKineticRefLines.length !== newCandidates.length 
    || !this.candidateKineticRefLines.every((candidate, i) => candidate.lines.parent === newCandidates[i].lines.parent);
    
  if( candidateParentsChanged ) {
    // Lines actually changed - reset everything including cycling
    this.candidateKineticRefLines = newCandidates;
    this.currentKineticRefLineIndex = 0;
    this.currentKineticRefLine = newCandidates.length > 0 ? newCandidates[0].lines : null;
    this.drawRefGammaLines();
    this.updateMouseCoordText();
    
    // Only restart auto-cycling if candidates actually changed
    if( this.candidateKineticRefLines.length > 1 ) {
      this.startKineticRefLineCycling();
    } else {
      this.stopKineticRefLineCycling();
    }
  } else if( this.candidateKineticRefLines ) {
    // Same candidate lines, just update weights but preserve user selection
    this.candidateKineticRefLines.forEach((candidate, i) => {
      if( i < newCandidates.length ) {
        candidate.weight = newCandidates[i].weight;
        candidate.bestLine = newCandidates[i].bestLine;
      }
    });
    
    // Ensure current index is still valid
    if( this.currentKineticRefLineIndex >= this.candidateKineticRefLines.length ) {
      this.currentKineticRefLineIndex = 0;
    }
    
    // Update current line reference and display
    this.currentKineticRefLine = this.candidateKineticRefLines.length > 0 ? this.candidateKineticRefLines[this.currentKineticRefLineIndex].lines : null;
    this.drawRefGammaLines();
    this.updateMouseCoordText();
  }
}//SpectrumChartD3.prototype.handleUpdateKineticRefLineUpdate

SpectrumChartD3.prototype.startKineticRefLineCycling = function(){
  const self = this;
  
  this.stopKineticRefLineCycling();
  
  if( this.candidateKineticRefLines.length <= 1 ) {
    return;
  }
  
  this.kineticRefLineCycleTimer = setInterval(function(){
    if( self.kineticRefLineCycleTimer && self.candidateKineticRefLines.length > 1 ) {
      self.cycleKineticRefLine(1, true);
    }
  }, 2000);
}//SpectrumChartD3.prototype.startKineticRefLineCycling

SpectrumChartD3.prototype.stopKineticRefLineCycling = function(){
  if( this.kineticRefLineCycleTimer ) {
    clearInterval(this.kineticRefLineCycleTimer);
    this.kineticRefLineCycleTimer = null;
  }
}//SpectrumChartD3.prototype.stopKineticRefLineCycling

SpectrumChartD3.prototype.updateCurrentKineticDisplay = function(){
  const currentCandidate = this.candidateKineticRefLines[this.currentKineticRefLineIndex];
  if( currentCandidate && currentCandidate.bestLine ) {
    this.updateRefLineDisplay(currentCandidate.bestLine, currentCandidate.lines);
    
    // Find the DOM element for the best line in current kinetic reference and apply styling
    const targetElement = this.vis.selectAll("g.ref").filter(function(d) { 
      return d === currentCandidate.bestLine; 
    }).node();
    if( targetElement )
      this.applyRefLineHoverStyling(targetElement, false);
  }
  this.updateKineticRefLineCandidateDisplay();
}//SpectrumChartD3.prototype.updateCurrentKineticDisplay

SpectrumChartD3.prototype.cycleKineticRefLine = function( direction, isAutomatic ){
  if( !this.candidateKineticRefLines || this.candidateKineticRefLines.length <= 1 ) {
    return;
  }
  
  // Disable auto-cycling when user manually navigates (but not when called automatically)
  if( !isAutomatic ) {
    this.stopKineticRefLineCycling();
  }
  
  if( direction > 0 ) {
    this.currentKineticRefLineIndex = (this.currentKineticRefLineIndex + 1) % this.candidateKineticRefLines.length;
  } else {
    this.currentKineticRefLineIndex = (this.currentKineticRefLineIndex - 1 + this.candidateKineticRefLines.length) % this.candidateKineticRefLines.length;
  }
  
  this.currentKineticRefLine = this.candidateKineticRefLines[this.currentKineticRefLineIndex].lines;
  this.drawRefGammaLines();
  this.updateCurrentKineticDisplay();
}//SpectrumChartD3.prototype.cycleKineticRefLine

SpectrumChartD3.prototype.updateKineticRefLineCandidateDisplay = function(){
  const self = this;
  
  if( !self.refLineInfo || !self.candidateKineticRefLines || self.candidateKineticRefLines.length <= 1 ) {
    if( self.refLineCandidates ) {
      self.refLineCandidates.remove();
      self.refLineCandidates = null;
    }
    return;
  }
  
  // Create candidates element if it doesn't exist
  if( !self.refLineCandidates ) {
    self.refLineCandidates = self.refLineInfo.append("g").attr("class", "refLineCandidates");
    self.refLineCandidates.append("rect").attr("class", "refLineCandidatesBackground");
    self.refLineCandidates.append("text").attr("class", "refLineCandidatesText").attr("x", 0).attr("dy", "1em");
  }
  
  const candidatesText = self.refLineCandidates.select("text");
  const candidatesRect = self.refLineCandidates.select("rect");
  
  // Clear existing content
  candidatesText.selectAll("tspan").remove();
  
  // Add separator line
  candidatesText.append('svg:tspan')
    .attr('x', -3)
    .attr('dy', "1.4em")
    .style('font-size', '0.8em')
    .text( self.options.txt.candidates );
  
  // Display all candidate reference lines
  self.candidateKineticRefLines.forEach(function(candidate, index) {
    const isCurrent = (index === self.currentKineticRefLineIndex);
    const w = 1/(candidate.weight / self.candidateKineticRefLines[0].weight);
    const displayText = candidate.lines.parent + " (w: " + w.toFixed(2) + ")";
    
    candidatesText.append('svg:tspan')
      .attr('x', 0)
      .attr('dy', "1.1em")
      .style('font-size', '0.8em')
      .style('font-weight', isCurrent ? 'bold' : 'normal')
      .text( displayText );
  });

  candidatesText.append('svg:tspan')
    .attr('x', -4)
    .attr('dy', "1.2em")
    .style('font-size', '0.7em')
    .style('font-style', 'italic')
    .text( self.options.txt.useArrowsToSelect );
  
  // Size and position the background rectangle
  const bbox = candidatesText.node().getBBox();
  const extraPadding = 4;
  candidatesRect
    .attr("x", bbox.x - 3 - extraPadding)
    .attr("y", bbox.y - 2)
    .attr("width", bbox.width + 6 + 2*extraPadding)
    .attr("height", bbox.height + 4);
  
  // Position candidates (left if space, otherwise right)
  const candidatesWidth = bbox.width + 6 + 2*extraPadding;
  const currentTransform = self.refLineInfo.attr("transform");
  const refLineX = currentTransform ? parseFloat(currentTransform.match(/translate\(([^,]+),/)?.[1] || 0) : 0;
  const xOffset = (refLineX - candidatesWidth - 2 < 0) ? 
    self.refLineInfoTxt.select("text").node().getBBox().width + 15 : 
    -candidatesWidth - 2;
  
  self.refLineCandidates.attr("transform", "translate(" + xOffset + ",0)");
}//SpectrumChartD3.prototype.updateKineticRefLineCandidateDisplay

SpectrumChartD3.prototype.updateRefLineDisplay = function( linedata, refLineSource ){
  const self = this;
  
  if( !self.refLineInfoTxt || !linedata || !refLineSource ) {
    return;
  }
  
  const e = linedata.e;
  const sf = linedata.h;
  const detector = refLineSource.detector;
  const shielding = refLineSource.shielding;
  const shieldingThickness = refLineSource.shieldingThickness;
  const nearestLineParent = refLineSource.parent;
  
  const textdescrip = (linedata.src_label ? (linedata.src_label + ', ') : (nearestLineParent ? (nearestLineParent + ', ') : "") )
                    +  e + ' keV'
                    + (linedata.particle ? ' ' + linedata.particle : "")
                    + ', rel. amp. ' + sf;
  
  let txt = "";
  if( (typeof linedata.desc_ind === "number") && (linedata.desc_ind >= 0) && (linedata.desc_ind < refLineSource.desc_strs.length) )
    txt = refLineSource.desc_strs[linedata.desc_ind];
  
  let attTxt = "";
  if( linedata.particle === 'gamma' || linedata.particle === 'xray' ) {
    if( shielding ) {
      if( shieldingThickness )
        attTxt = shieldingThickness + ' of ';
      attTxt += shielding;
    }
    if( detector )
      attTxt = (attTxt ? (attTxt + ' with a ' + detector) : 'Assuming a ' + detector);
  }
  
  const svgtxt = self.refLineInfoTxt.select("text")
                   .attr("dy", "1em")
                   .attr("fill", refLineSource.color || "#000");
  
  // Remove existing main text spans (but keep any candidate spans)
  svgtxt.selectAll("tspan:not(.candidate)").remove();
  
  if ( self.options.showRefLineInfoForMouseOver ) {
    svgtxt.append('svg:tspan').attr('x', 0).attr('dy', "1em").text( textdescrip );
    if( txt )
      svgtxt.append('svg:tspan').attr('x', 0).attr('dy', "1em").text( txt );
    if( attTxt )
      svgtxt.append('svg:tspan').attr('x', 0).attr('dy', "1em").text( attTxt );
  }
}//SpectrumChartD3.prototype.updateRefLineDisplay

SpectrumChartD3.prototype.applyLineStyling = function( lineElement, linedata, isHovered ) {
  const self = this;
  const lineSelection = d3.select(lineElement);
  const strokeWidth = isHovered ? self.options.refLineWidthHover : self.options.refLineWidth;
  
  // Apply main line styling
  lineSelection.select("line")
    .attr("stroke-width", strokeWidth)
    .attr("dx", -0.5*strokeWidth);
  
  // Handle extension lines
  if( self.options.refLineVerbosity >= 1 ) {
    const hasMajorExtension = linedata.major && self.options.refLineVerbosity >= 2;
    
    if( hasMajorExtension ) {
      lineSelection.select("line.major-extension")
        .attr("stroke-width", strokeWidth)
        .style("opacity", isHovered ? 1.0 : 0.5);
    }
    
    if( isHovered && !hasMajorExtension ) {
      // Add temporary extension line for hover (same positioning as major-extension)
      const h = self.size.height;
      const m = self.options.refLineTopPad;
      const y2Lin = function(d){ return Math.min(h - (h-m)*d.h/d.parent.maxVisibleAmp, h-2); };
      const extension_dash = "" + self.options.refLineWidthHover + "," + 2*self.options.refLineWidthHover;
      
      lineSelection.selectAll("line.temp-extension").remove();
      lineSelection.append("line")
        .attr("class", "temp-extension")
        .style("stroke-dasharray", extension_dash) 
        .attr("stroke", linedata.color ? linedata.color : linedata.parent.color)
        .attr("stroke-width", self.options.refLineWidthHover)
        .attr("y1", y2Lin(linedata))
        .attr("y2", self.options.refLineTopPad)
        .attr("x1", 0)
        .attr("x2", 0);
    } else if( !isHovered ) {
      lineSelection.selectAll("line.temp-extension").remove();
    }
  }
}//SpectrumChartD3.prototype.applyLineStyling

SpectrumChartD3.prototype.applyRefLineHoverStyling = function( nearestline, skipPreviousReset ){
  const self = this;
  
  // Reset previous kinetic reference line styling if cycling
  if( !skipPreviousReset && self.candidateKineticRefLines && self.candidateKineticRefLines.length > 1 ) {
    self.candidateKineticRefLines.forEach(function(candidate) {
      const refLineElement = self.vis.selectAll("g.ref").filter(function(d) { return d === candidate.bestLine; }).node();
      if( refLineElement )
        self.applyLineStyling(refLineElement, candidate.bestLine, false);
    });
  }
  
  if( nearestline ) {
    const linedata = nearestline.__data__;
    self.applyLineStyling(nearestline, linedata, true);
    self.mousedOverRefLine = nearestline;
  } else {
    self.mousedOverRefLine = null;
  }
}//SpectrumChartD3.prototype.applyRefLineHoverStyling

/**
 * -------------- Grid Lines Functions --------------
 */
SpectrumChartD3.prototype.setGridX = function( onstate, dontRedraw ) {
  this.options.gridx = onstate;

  if( this.xGridBody )
    this.xGridBody.remove();
  this.xGrid = null;
  this.xGridBody = null;
  
  if( onstate ) {
    this.xGrid = d3.svg.axis().scale(this.xScale)
                   .orient("bottom")
                   .innerTickSize(-this.size.height)
                   .outerTickSize(0)
                   .tickFormat( "" )
                   .tickPadding(10)
                   .ticks( 20,"" );

    this.xGridBody = this.vis.insert("g", ".refLineInfo")
        .attr("width", this.size.width )
        .attr("height", this.size.height )
        .attr("class", "xgrid" )
        .attr("transform", "translate(0," + this.size.height + ")")
        .call( this.xGrid );
  }
  
  if( !dontRedraw )
    this.redraw(true)();
}

SpectrumChartD3.prototype.setGridY = function( onstate, dontRedraw ) {
  this.options.gridy = onstate;

  if( this.yGridBody )
    this.yGridBody.remove();
  this.yGrid = null;
  this.yGridBody = null;
  
  if( onstate ) {
    this.yGrid = d3.svg.axis().scale(this.yScale)
                   .orient("left")
                   .innerTickSize(-this.size.width)
                   .outerTickSize(0)
                   .tickFormat( "" )
                   .tickPadding(10);

    this.yGridBody = this.vis.insert("g", ".refLineInfo")
        .attr("width", this.size.width )
        .attr("height", this.size.height )
        .attr("class", "ygrid" )
        .attr("transform", "translate(0,0)")
        .call( this.yGrid );
  }

  if( !dontRedraw )
    this.redraw()();
}


/**
 * -------------- Mouse Coordinate Info Functions --------------
 */
SpectrumChartD3.prototype.addMouseInfoBox = function(){
  if( this.mouseInfo )
    this.mouseInfo.remove();

  this.mouseInfo = this.vis.append("g")
                     .attr("class", "mouseInfo")
                     .style("display", "none")
                     .attr("transform","translate(" + this.size.width + "," + this.size.height + ")");

  this.mouseInfoBox = this.mouseInfo.append('rect')
               .attr("class", "mouseInfoBox")
               .attr('width', "12em")
               .attr('height', "2.5em")
               .attr('x', "-12.5em")
               .attr('y', "-3.1em");

  this.mouseInfo.append("g").append("text");
}

SpectrumChartD3.prototype.updateMouseCoordText = function() {
  var self = this;

  if ( !d3.event || !self.rawData || !self.rawData.spectra || !self.rawData.spectra.length )
    return;

  const p = self.getMousePos();

  if( !p ){
    self.mousedOverRefLine = null;
    self.refLineInfo.style("display", "none");
    self.mouseInfo.style("display", "none");
    return;
  }

  var energy = self.xScale.invert(p[0]);
  var y = self.yScale.invert(p[1]);

  /*Find what channel this energy cooresponds */
  var channel, lowerchanval, counts = null, backgroundSubtractCounts = null;
  var foreground = self.rawData.spectra[0];
  var background = self.getSpectrumByID(foreground.backgroundID);
  var bisector = d3.bisector(function(d){return d.x;});
  channel = (foreground.x && foreground.x.length) ? d3.bisector(function(d){return d;}).right(foreground.x, energy) : -1;
  if( foreground.points && foreground.points.length ){
    lowerchanval = bisector.left(foreground.points,energy,1) - 1;
    counts = foreground.points[lowerchanval].y; 
  }
  if (self.options.backgroundSubtract && counts !== null && background) {
    lowerchanval = bisector.left(background.points,energy,1) - 1;
    backgroundSubtractCounts = Math.max(0, counts - (background.points[lowerchanval] ? background.points[lowerchanval].y : 0));
  }

  /*Currently two majorish issues with the mouse text */
  /*  1) The counts for the foreground are all that is given. Should give for background/second */
  /*  2) Currently gives a channel (singular), but when there this.rebinFactor!=1, should give range */
  /*Also, formatting could be a bit better */
  /*Also need to make displaying this box an option */
  /*Could also add a blue dot in the data or something, along with lines to the axis, both features should be optional */

  /*Right now if mouse stats arent deesired we are just not showing them, but */
  /*  still updating them... */

  if( self.options.showMouseStats ){
    self.mouseInfo.style("display", null );
    var mousetxt = self.mouseInfo.select("text");
    mousetxt.attr('dy', "-2em");
    mousetxt.selectAll("tspan").remove();

    var xmmsg = ""+(Math.round(10*energy)/10) + " keV";
    if( channel )
      xmmsg += ", chan: " + channel;
    var ymmsg = "";
    if( counts !== null )
      ymmsg += "counts: " + (Math.round(10*counts)/10) + (foreground.rebinFactor === 1 ? "" : ("/" + foreground.rebinFactor));
    ymmsg += (counts!==null?", ":"") + "y: " + (Math.round(10*y)/10);
    var bgsubmsg = "";
    if( backgroundSubtractCounts !== null )
      bgsubmsg += self.options.txt.backSubCounts + ": " + (Math.round(10*backgroundSubtractCounts)/10) + (foreground.rebinFactor === 1 ? "" : ("/" + foreground.rebinFactor));

      

    var bgsubmsglen = 0;
    if (backgroundSubtractCounts !== null)
      bgsubmsglen = mousetxt.append('svg:tspan').attr('x', "-12em").attr('dy', "-1em")
                      .text( bgsubmsg ).node().getComputedTextLength();


    var ymmsglen = mousetxt.append('svg:tspan').attr('x', "-12em").attr('dy', "-1em")
                    .text( ymmsg ).node().getComputedTextLength();
    var xmmsglen = mousetxt.append('svg:tspan').attr('x', "-12em").attr('dy', "-1em")
            .text( xmmsg ).node().getComputedTextLength();

    /*Resize the box to match the text size */
    self.mouseInfoBox.attr('width', Math.max(xmmsglen,ymmsglen,bgsubmsglen) + 9 )
      .attr('height', backgroundSubtractCounts !== null ? '3.1em' : '2.5em')
      .attr('y', backgroundSubtractCounts !== null ? '-3.9em' : '-3.1em');
  } else {
    self.mouseInfo.style("display", "none" );
  }

  // If we are zooming - we dont want to display any of the ref-line info
  if( self.dragging_plot || self.zooming_plot /*|| self.leftMouseDown || self.rightClickDown*/ || self.zoomAnimationID ){
    if( self.mousedOverRefLine ){ //Remove the info if we are showing it
      const line = d3.select(self.mousedOverRefLine);
      line.select("line.temp-extension").remove();
      line.select("circle").remove();
      line.selectAll("line").attr("stroke-width", self.options.refLineWidth);
      line.select("line.major-extension").style("opacity", 0.5);
      self.mousedOverRefLine.__data__.mousedover = null;
      self.mousedOverRefLine = null;
      self.refLineInfo.style("display", "none");
    }
    return;
  }//if( we are zooming in/out )

  var mindist = 9.0e20, nearestpx = 9.0e20;

  var nearestline = null;
  var h = self.size.height;
  var m = Math.min(h,self.options.refLineTopPad);
  var visy = Math.max(h-m,1);

  var reflines = self.vis.selectAll("g.ref");

  // Helper function to cleanup reference line hover state
  function cleanupRefLineHover(groupSelection, lineData) {
    const lineSelection = groupSelection.select("line");
    lineSelection
      .attr("stroke-width", self.options.refLineWidth)
      .attr("dx", -0.5*self.options.refLineWidth);
    
    // Handle extension line cleanup
    if( self.options.refLineVerbosity >= 1 ) {
      if( lineData.major && self.options.refLineVerbosity >= 2 ) {
        // For major lines with verbosity >= 2, reset the existing extension line
        groupSelection.select("line.major-extension")
          .attr("stroke-width", self.options.refLineWidth)
          .style("opacity", 0.5);
      } else {
        // For non-major lines or major lines with verbosity == 1, remove the temporary extension line
        groupSelection.select("line.temp-extension").remove();
      }
    }
    
    // Remove hover indicator circle
    groupSelection.select("circle.ref-hover-indicator").remove();
    lineData.mousedover = null;
  }

  reflines[0].forEach( function(d,i){
    var yh = d.childNodes[0].y1.baseVal.value - d.childNodes[0].y2.baseVal.value;
    var xpx = self.xScale(d.__data__.e);
    var dpx = Math.abs(xpx - p[0]);

    /*In principle, this check (or __data__.mousedover) shouldnt be necassary, */
    /*  but with out it sometimes lines will stay fat that arent supposed to. */
    /* Also, I think setting attr values is expensive, so only doing if necassary. */
    if( d.__data__.mousedover && d !== self.mousedOverRefLine ){
      cleanupRefLineHover(d3.select(d), d.__data__);
    }

    var dist = dpx + dpx/(yh/visy);
    if( dist < mindist ) {
      mindist = dist;
      nearestline = d;
      nearestpx = dpx;
    }
  } );

  if( nearestpx > 10 ) {
    if( self.mousedOverRefLine ){
      cleanupRefLineHover(d3.select(self.mousedOverRefLine), self.mousedOverRefLine.__data__);
    }
    self.mousedOverRefLine = null;
    self.refLineInfo.style("display", "none");
    return;
  }

  if( self.mousedOverRefLine===nearestline )
    return;

  if( self.mousedOverRefLine ){
    cleanupRefLineHover(d3.select(self.mousedOverRefLine), self.mousedOverRefLine.__data__);
  }

  var linedata = nearestline.__data__;
  linedata.mousedover = true;
  const e = linedata.e;
  const sf = linedata.h;
  const linepx = self.xScale(e);

  self.refLineInfo.style("display", null).attr("transform", "translate("+linepx+",0)" );
  
  // Add circle to the hovered reference line
  const lineSelection = d3.select(nearestline);
  const nearLineEl = lineSelection.select("line");
  
  // Update the main reference line text display
  self.updateRefLineDisplay(linedata, linedata.parent);
  
  // Update candidate display
  self.updateKineticRefLineCandidateDisplay();

  /*Now detect if text is running off the right side of the chart, and if so */
  /*  put to the left of the line */
  var tx = function(d) {
    var w = this.getBBox().width;
    return ((linepx+5+w)>self.size.width) ? ("translate("+(-5-w)+",0)") : "translate(5,0)";
  };
  self.refLineInfoTxt.attr("transform", tx );

  // Apply hover styling to the nearest line
  self.applyRefLineHoverStyling(nearestline, true);

  lineSelection.select("circle.ref-hover-indicator").remove(); // Remove any existing circle
  const circleY = self.options.refLineVerbosity >= 1 ? nearLineEl.attr("y2") : self.size.height;
  lineSelection.append("circle")
    .attr("class", "ref-hover-indicator")
    .attr("cx", 0)
    .attr("cy", circleY)
    .attr("r", 2)
    .style("fill", "red");
}

SpectrumChartD3.prototype.setShowMouseStats = function(d) {
  this.options.showMouseStats = d;
  this.mouseInfo.style("display", d ? null : "none");
  if( d )
    this.updateMouseCoordText();
}


/**
 * -------------- Legend Functions --------------
 */
SpectrumChartD3.prototype.updateLegend = function() {
  var self = this;
  
  if( !this.options.showLegend || !self.rawData || !self.rawData.spectra || !self.rawData.spectra.length ) {
    if( this.legend ) {
      this.legend.remove();
      this.legend = null;
      this.legendBox = null;
      this.legBody = null;
      this.legendHeaderClose = null;
      //Not emmitting 'legendClosed' since self.options.showLegend is not being changed.
      //self.WtEmit(self.chart.id, {name: 'legendClosed'} );
    }
    return;
  }
  
  if( !this.legend ) {

    function moveleg(){                 /* move legend  */
      if( self.legdown ) {
        /* console.log(d3.event); */
        d3.event.preventDefault();
        d3.event.stopPropagation();

        var x = d3.event.x ? d3.event.x : d3.event.touches ?  d3.event.touches[0].clientX : d3.event.clientX;
        var y = d3.event.y ? d3.event.y : d3.event.touches ?  d3.event.touches[0].clientY : d3.event.clientY;

        var calculated_x = d3.mouse(self.vis[0][0])[0];

        if ( calculated_x >= -self.padding.leftComputed && y >= 0 && 
             calculated_x <= self.cx && y <= self.cy ) {
          //console.log("change pos");
          var tx = (x - self.legdown.x) + self.legdown.x0;
          var ty = (y - self.legdown.y) + self.legdown.y0; 
          self.legend.attr("transform", "translate(" + tx + "," + ty + ")");
        }
      }
    }


    this.legend = d3.select(this.chart).select("svg").append("g")
                      .attr("class", "legend")
                      .attr("transform","translate(" + (this.cx - 120 - this.padding.right) + ","+ (this.padding.topComputed + 10) + ")");
    this.legendBox = this.legend.append('rect')
               .attr("class", "legendBack")
               .attr('width', "100")
               .attr('height', "1em")
               .attr( "rx", "5px")
               .attr( "ry", "5px");
    this.legBody = this.legend.append("g")
                       .attr("transform","translate(8,6)");
                       
    this.legendHeader = this.legend.append("g"); 
    this.legendHeader
               .style("display", "none")
               .append('rect')
               .attr("class", "legendHeader")
               .attr('width', "100px")
               .attr('height', "20px")
               .attr( "rx", "5px")
               .attr( "ry", "5px")
               .style("cursor", "pointer");
    
    /*Add a close button to get rid of the legend */
    this.legendHeaderClose = this.legendHeader.append('g').attr("transform","translate(4,4)");
    this.legendHeaderClose.append("rect")
            .attr("class", "d3closebut")
            .attr('height', "12")
            .attr('width', "12");
    this.legendHeaderClose.append("path")
        .attr("style", "stroke: white; stroke-width: 1.5px;" )
        .attr("d", "M 2,2 L 10,10 M 10,2 L 2,10");
    this.legendHeaderClose.on("click", function(){ 
      self.options.showLegend = false; 
      self.updateLegend(); 
      self.WtEmit(self.chart.id, {name: 'legendClosed'} );
    } ).on("touchend", function(){ 
      self.options.showLegend = false; 
      self.updateLegend(); 
      self.WtEmit(self.chart.id, {name: 'legendClosed'} );
    } );   
               
    /*this.legendHeader.append("text") */
    /*      .text("Legend") */
    /*      .attr("x", 62.5) */
    /*      .attr("y", "1.1em") */
    /*      .style("text-anchor","middle") */
    /*      .style("cursor", "pointer"); */
    
    this.legend.on("mouseover", function(d){if( !self.dragging_plot && !self.zooming_plot ) self.legendHeader.style("display", null);} )
      .on("mouseout", function(d){self.legendHeader.style("display", "none");} )
      .on("mousemove", moveleg)
      .on("touchmove", moveleg)
      .on("wheel", function(d){d3.event.preventDefault(); d3.event.stopPropagation();} );
    
    function mousedownleg(){
      //console.log("mouse down on legend");
      if (d3.event.defaultPrevented) return;
      if( self.dragging_plot || self.zooming_plot ) return;
      d3.event.preventDefault();
      d3.event.stopPropagation();
      var trans = d3.transform(self.legend.attr("transform")).translate;

      var x = d3.event.x ? d3.event.x : d3.event.touches ?  d3.event.touches[0].clientX : d3.event.clientX;
      var y = d3.event.y ? d3.event.y : d3.event.touches ?  d3.event.touches[0].clientY : d3.event.clientY;
      self.legdown = {x: x, y: y, x0: trans[0], y0: trans[1]};
    };
    
    this.legendHeader.on("mouseover", function(d) { if( !self.dragging_plot && !self.zooming_plot ) self.legend.attr("class", "legend activeLegend");} )
      .on("touchstart", function(d) { if( !self.dragging_plot && !self.zooming_plot ) self.legend.attr("class", "legend activeLegend"); } )
      .on("mouseout",  function(d) { if (self.legend) self.legend.attr("class", "legend"); } )
      .on("touchend",  function(d) { if (self.legend) self.legend.attr("class", "legend"); } )
      .on("mousedown.drag",  mousedownleg )
      .on("touchstart.drag",  mousedownleg )
      .on("touchend.drag", function() {self.legdown = null;})
      .on("mouseup.drag", function(){self.legdown = null;} )
      .on("mousemove.drag", moveleg)
      .on("mouseout.drag", moveleg);

    this.legend.on("touchstart", function(d) { if( !self.dragging_plot && !self.zooming_plot ) self.legendHeader.style("display", null); } )
      .on("touchstart.drag", mousedownleg)
      .on("touchend.drag",  function() 
      {
      if (!self.legend || !self.legendHeader) {
        self.legdown = null;
        return;
      }

      self.legend.attr("class", "legend"); 
      window.setTimeout(function() { self.legendHeader.style("display", "none"); }, 1500) 
      self.legdown = null; 
      });
  }
  
  var origtrans = d3.transform(this.legend.attr("transform")).translate;
  var fromRight = this.cx - origtrans[0] - this.legendBox.attr('width');
  
  this.legBody.selectAll("g").remove();

  var ypos = 0;
  const spectra = self.rawData ? self.rawData.spectra : [];
  spectra.forEach( function(spectrum,i){
    if( !spectrum || !spectrum.y || !spectrum.y.length )
      return;
      
    const sf = ((typeof spectrum.yScaleFactor === "number") ? spectrum.yScaleFactor: 1);
    const lt = spectrum.liveTime;
    const rt = spectrum.realTime;
    const neutsum = spectrum.neutrons;
      
    const nsum = sf*spectrum.dataSum;
    const title = (spectrum.title ? spectrum.title : ("Spectrum " + (i+1)))
                    + " (" + nsum.toFixed(nsum > 1000 ? 0 : 1) + " counts)";
    
    let thisentry = self.legBody.append("g")
        .attr("transform","translate(0," + ypos + ")");
      
    let thistxt = thisentry.append("text")
        .attr("class", "legentry");
    
    let titlenode = thistxt.append('svg:tspan')
          .attr('x', "15")
          .text( title );
    const txtStart = 0.5*titlenode.node().getBBox().height;
    titlenode.attr('y', txtStart);
    
    thisentry.append("path")
        //.attr("id", "spectrum-legend-line-" + i)  // reference for when updating color
        .attr("class", "SpectrumLegendLine " + (spectrum.type ? " " + spectrum.type : "") )
        .attr("stroke", spectrum.lineColor ? spectrum.lineColor : null)
        .attr("stroke-width", self.options.spectrumLineWidth )
        .attr("d", "M0," + (txtStart - 1) + " L12," + (txtStart - 1) );
    
    let ltnode, lttxt, dttxt;
    if( typeof lt === "number" )
    {
      lttxt = self.options.txt.liveTime + ": " + (sf*lt).toPrecision(4) + " s";
      ltnode = thistxt.append('svg:tspan')
        .attr('x', "20")
        .attr('y', txtStart + thisentry.node().getBBox().height)
        .text( lttxt );
    }
      
    if( typeof rt === "number" )
      thistxt.append('svg:tspan')
        .attr('x', "20")
        .attr('y', txtStart + thisentry.node().getBBox().height)
        .text( self.options.txt.realTime + ": " + (sf*rt).toPrecision(4) + " s");
          
    if( sf != 1 )
      thistxt.append('svg:tspan')
        .attr('x', "20")
        .attr('y', txtStart + thisentry.node().getBBox().height)
        .text( self.options.txt.scaledBy + " " + sf.toPrecision(4) );
      
    if( (typeof lt === "number") && (typeof rt === "number") && (rt > 0) && ltnode )
    {
      dttxt = self.options.txt.deadTime + ": " + (100*(rt - lt) / rt).toPrecision(3) + "%";
      // Hookup event handlers for mouse-over, jic we dont have neutron data
      thistxt
        .on("mouseover", function(){ ltnode.text(dttxt); } )
        .on("mouseout", function(){ ltnode.text(lttxt); });
    }
          
    if( typeof neutsum === "number" ){
      const nrt = (typeof spectrum.neutronLiveTime === "number") ? spectrum.neutronLiveTime : (rt > 1.0E-6 ? rt : lt);
      const isCps = (typeof nrt === "number");
      const neut = isCps ? neutsum/nrt : neutsum*sf;
      
        
      // Lets print the neutron counts as a human friendly number, to roughly
      //   the precision we would care about.  Could probably do a much better
      //   with less code, but whatever for now.
      let toLegendRateStr = function( val, ndig ){
        const powTen = Math.floor(Math.log10(Math.abs(val)));
        
        if( Number.isInteger(val) || Number.isNaN(ndig) || Number.isNaN(powTen) )  //Write integers out as integers
          return '' + val;
        else if( (powTen < -4) || (powTen > ndig) )        //Numbers less than 0.0001, use scientific notation, ex. 6.096e-6 (where ndig==3)
          return '' + val.toExponential(ndig);
        else if( powTen < 3 )         //Numbers between 0.0001 and 1000, ex. 0.06096 (where ndig==3)
          return '' + val.toFixed(ndig-powTen);
        else                          //Numbers greater than 1000 just write as integer
          return '' + val.toFixed(0);
      };//toLegendRateStr
        
        
      thistxt.append('svg:tspan')
              .attr('x', "20")
              .attr('y', txtStart + thisentry.node().getBBox().height)
              .text( self.options.txt.Neutrons + ": " + toLegendRateStr(neut,3) + (isCps ? " " + self.options.txt.cps : ""));
      
      //If we are displaying neutron CPS, and this is not a foreground, then lets add an easy way to compare this rate
      //  to the foreground
      if( isCps
          && (spectrum.type === self.spectrumTypes.FOREGROUND)
          || (spectrum.type === self.spectrumTypes.SECONDARY) )
      {
        //Get the neutron info for the foreground; note uses first foreground
        let forNeut = null, forNeutLT = null;
        
        for( let j = 0; j < spectra.length; ++j )
        {
          const spec = spectra[j];
          if( spec && (j !== i)
              && (spec.type === self.spectrumTypes.BACKGROUND)
              && ((typeof spec.neutronLiveTime === "number") || (typeof spec.realTime === "number"))
              && (typeof spec.neutrons === "number") )
          {
            forNeut = spec.neutrons;
            forNeutLT = (typeof spec.neutronLiveTime === "number") ? spec.neutronLiveTime
                                                                   : (spec.realTime > 1.0E-6 ? spec.realTime : spec.liveTime);
            break;
          }
        }//for( loop over spectrum )
         
        if( (typeof forNeut === "number") && (typeof forNeutLT === "number") && (neutsum>0 || forNeut>0) )
        {
          const forRate = forNeut/forNeutLT;
          const forRateSigma = Math.sqrt(forNeut) / forNeutLT;
          const rateSigma = Math.sqrt(neutsum) / nrt;
          const sigma = Math.sqrt(rateSigma*rateSigma + forRateSigma*forRateSigma);
          const nsigma = Math.abs(neut - forRate) / sigma;
          const isneg = (neut < forRate);
          
          thistxt.append('svg:tspan')
            .attr('x', "40")
            .attr('y', txtStart + thisentry.node().getBBox().height - 4)
            .attr('style', 'font-size: 75%')
            .html( "(" + (isneg ? self.options.txt.foreNSigmaBelowBack : self.options.txt.foreNSigmaAboveBack).replace("{1}", toLegendRateStr(nsigma,1)) + ")" );
        }//if( we have foreground neutron CPS info )
      }//if( this is not a foreground, and we are displaying neutron CPS )
      
      
      //It would be nice to display the total neutron
      if( isCps ){
        thisentry.neutinfo = thistxt.append('svg:tspan')
          .attr('x', "40")
          .attr('y', txtStart + thisentry.node().getBBox().height - 5)
          .attr('style', 'display: none')
          .text( toLegendRateStr(neutsum,3) + " " + self.options.txt.neutrons + (typeof nrt === "number" ? (" in " + nrt.toPrecision(4) + " s") : "") );
      
        thistxt  //This calls to .on("mouseover")/.on("mouseout") will overwrite eirlier hooked up calls
          .on("mouseover", function(){
            thisentry.neutinfo.attr('style', 'font-size: 75%' )
            self.legendBox.attr('height', self.legBody.node().getBBox().height + 10 );
            if( ltnode && dttxt ) ltnode.text(dttxt); //
          } )
          .on("mouseout", function(){
            thisentry.neutinfo.attr('style', 'display: none;')
            self.legendBox.attr('height', self.legBody.node().getBBox().height + 10 );
            if( ltnode && lttxt ) ltnode.text(lttxt);
          });
       }// if( is CPS instead of sum neutrons )
      
    }//if( typeof neut === "number" )
      
    ypos += thisentry.node().getBBox().height + 5;
  });//spectra.forEach
  
                    
  /*Resize the box to match the text size */
  var w = this.legBody.node().getBBox().width + 15; 
  this.legendBox.attr('width', w );
  this.legendBox.attr('height', this.legBody.node().getBBox().height + 10 );
  this.legendHeaderClose.attr("transform","translate(" + (w-16) + ",4)");
  
  this.legendHeader.select('rect').attr('width', w );
  this.legendHeader.select('text').attr("x", w/2);
  
  /*this.legendBox.attr('height', legtxt.node().getBBox().height + hh + 8 ); */

  /*Set the transform so the space on the right of the legend stays the same */
  this.legend.attr("transform", "translate(" + (this.cx - fromRight - w) + "," + origtrans[1] + ")" );
}

SpectrumChartD3.prototype.setShowLegend = function( show ) {
  this.options.showLegend = Boolean(show);
  this.updateLegend();
}


/**
 * -------------- Y-axis Functions --------------
 */
SpectrumChartD3.prototype.yticks = function() {
  const self = this;
  var ticks = [];
  var EPSILON = 1.0E-3;

  // Added check for raw data to not render ticks when no data is present
  if (!this.rawData || !this.rawData.spectra) return [];

  var formatYNumber = function(v) {
    /*poorly simulating "%.3g" in snprintf */
    /*Should get rid of so many regexs, and shorten code (shouldnt there be a builtin function to print to "%.3"?) */
    var t;
    if( v >= 1000 || v < self.options.logYAxisMin )
    {
      t = v.toPrecision(3);
      t = t.replace(/\.0+e/g, "e").replace(/\.0+$/g, "");
      if( t.indexOf('.') > 0 )
        t = t.replace(/0+e/g, "e");
    } else {
      t = v.toFixed( Math.max(0,2-Math.floor(Math.log10(v))) );
      if( t.indexOf('.') > 0 )
        t = t.replace(/0+$/g, "");
      t = t.replace(/\.$/g, "");
    }

    return t;
  }

  var renderymin = this.yScale.domain()[1],
      renderymax = this.yScale.domain()[0],
      heightpx = this.size.height;
  var range = renderymax - renderymin;

  if( this.options.yscale === "lin" )
  {
    /*px_per_div: pixels between major and/or minor labels. */
    var px_per_div = 50;

    /*nlabel: approx number of major + minor labels we would like to have. */
    var nlabel = heightpx / px_per_div;

    /*renderInterval: Inverse of how many large labels to place between powers */
    /*  of 10 (1 is none, 0.5 is is one). */
    var renderInterval;

    /*n: approx how many labels will be used */
    var n = Math.pow(10, Math.floor(Math.log10(range/nlabel)));

    /*msd: approx how many sub dashes we would expect there to have to be */
    /*     to satisfy the spacing of labels we want with the given range. */
    var msd = range / nlabel / n;

    if( isNaN(n) || !isFinite(n) || nlabel<=0 || n<=0.0 ) { /*JIC */
      return ticks;
    }
      var subdashes = 0;

      if( msd < 1.5 )
      {
        subdashes = 2;
        renderInterval = 0.5*n;
      }else if (msd < 3.3)
      {
        subdashes = 5;
        renderInterval = 0.5*n;
      }else if (msd < 7)
      {
        subdashes = 5;
        renderInterval = n;
      }else
      {
        subdashes = 10;
        renderInterval = n;
      }

      var biginterval = subdashes * renderInterval;
      var starty = biginterval * Math.floor((renderymin + 1.0E-15) / biginterval);

      if( starty < (renderymin-EPSILON*renderInterval) )
        starty += biginterval;

      for( var i = -Math.floor(Math.floor(starty-renderymin)/renderInterval); ; ++i)
      {
        if( i > 500 )  /*JIC */
          break;

        var v = starty + renderInterval * i;

        if( (v - renderymax) > EPSILON * renderInterval )
          break;

        var t = "";
        if( i>=0 && ((i % subdashes) == 0) )
          t += formatYNumber(v);
        
        var len = ((i % subdashes) == 0) ? true : false;

        ticks.push( {value: v, major: len, text: t } );
      }/*for( intervals to draw ticks for ) */
    }/*case Chart::LinearScale: */

    if( this.options.yscale === "log" )
    {
      const yMinPrefPow = Math.floor( Math.log10(this.options.logYAxisMin) );
      
      /*Get the power of 10 just below or equal to rendermin. */
      var minpower = (renderymin > 0.0) ? Math.floor( Math.log10(renderymin) ) : yMinPrefPow;
      
      /*Get the power of 10 just above or equal to renderymax.  If renderymax */
      /*  is less than or equal to 0, set power to be 0. */
      var maxpower = (renderymax > 0.0) ? Math.ceil( Math.log10(renderymax) ): 0;

      /*Adjust minpower and maxpower */
      if( maxpower == minpower )
      {
        /*Happens when renderymin==renderymax which is a power of 10 */
        ++maxpower;
        --minpower;
      }else if( maxpower > 2 && minpower < yMinPrefPow)
      {
        /*We had a tiny value (possibly a fraction of a count), as well as a */
        /*  large value (>1000). */
        minpower = yMinPrefPow;
      }else if( maxpower >= 0 && minpower < yMinPrefPow && (maxpower-minpower) > 6 )
      {
        /*we had a tiny power (1.0E-5), as well as one between 1 and 999, */
        /*  so we will only show the most significant decades */
        minpower = maxpower - 5;
      }/*if( minpower == maxpower ) / else / else */


      /*numdecades: number of decades the data covers, including the decade */
      /*  above and below the data. */
      var numdecades = maxpower - minpower + 1;

      /*minpxdecade: minimum number of pixels we need per decade. */
      var minpxdecade = 25;

      /*labeldelta: the number of decades between successive labeled large ticks */
      /*  each decade will have a large tick regardless */
      var labeldelta = 1;

      /*nticksperdecade: number of small+large ticks per decade */
      var nticksperdecade = 10;


      if( Math.floor(heightpx / minpxdecade) < numdecades )
      {
        labeldelta = numdecades / (heightpx / minpxdecade);
        nticksperdecade = 1;
      }/*if( (heightpx / minpxdecade) < numdecades ) */


      labeldelta = Math.round(labeldelta);

      var t = null;
      var nticks = 0;
      var nmajorticks = 0;

      for( var decade = minpower; decade <= maxpower; ++decade )
      {
        var startcounts = Math.pow( 10.0, decade );
        var deltacounts = 10.0 * startcounts / nticksperdecade;
        var eps = deltacounts * EPSILON;

        if( (startcounts - renderymin) > -eps && (startcounts - renderymax) < eps )
        {
          t = ((decade%labeldelta)==0) ? formatYNumber(startcounts) : "";
          ++nticks;
          ++nmajorticks;
          ticks.push( {value: startcounts, major: true, text: t } );
        }/*if( startcounts >= renderymin && startcounts <= renderymax ) */

        for( var i = 1; i < (nticksperdecade-1); ++i )
        {
          var y = startcounts + i*deltacounts;
          if( (y - renderymin) > -eps && (y - renderymax) < eps )
          {
            ++nticks;  
            ticks.push( {value: y, major: false, text: null } );
          }
        }/*for( int i = 1; i < nticksperdecade; ++i ) */
      }/*for( int decade = minpower; decade <= maxpower; ++decade ) */

      /*If we have a decent number of (sub) labels, the user can orient */
      /*  themselves okay, so well get rid of the minor labels. */
      if( (nticks > 8 || (heightpx/nticks) < 25 || nmajorticks > 1) && nmajorticks > 0 ) {
        for( var i = 0; i < ticks.length; ++i )
          if( !ticks[i].major )
            ticks[i].text = "";
      }
      
      if( nmajorticks < 1 && ticks.length ) {
        ticks[0].text = formatYNumber(ticks[0].value);
        ticks[ticks.length-1].text = formatYNumber(ticks[ticks.length-1].value);
      }
      
      if( ticks.length === 0 )
      {
        /*cerr << "Forcing a single axis point in" << endl; */
        var y = 0.5*(renderymin+renderymax);
        var t = formatYNumber(y);
        ticks.push( {value: y, major: true, text: t } );
      }
    }/*case Chart::LogScale: */

  /*TODO: should to properly implement sqrt */
  if( this.options.yscale === "sqrt" ){
    /*Take the easy way out for now until I can get around to customizing */
    this.yScale.copy().ticks(10)
        .forEach(function(e){ticks.push({value:e,major:true,text:formatYNumber(e)});});
  }

  if( this.options.noYAxisNumbers )
    ticks.forEach( function(obj){obj["text"] = null; } );

  return ticks;
}

/* Sets y-axis drag for the chart. These are actions done by clicking and dragging one of the labels of the y-axis. */
SpectrumChartD3.prototype.yaxisDrag = function(d) {
  var self = this;
  return function(d) {
    console.log('yaxisDrag work');
    var p = d3.mouse(self.vis[0][0]);
    self.yaxisdown = self.yScale.invert(p[1]);
  }
}

SpectrumChartD3.prototype.drawYTicks = function() {
  this.yAxis.scale(this.yScale);
  
  //If self.yScale(d) will return a NaN, then exit this function anyway
  if( this.yScale.domain()[0] === this.yScale.domain()[1] ){
    this.yAxisBody.selectAll("g.tick").remove();
    return;
  }
    
  /* Regenerate y-ticks */
  const ytick = this.yticks();
  const ytickvalues = ytick.map(function(d){return d.value;} );
  
  // Set out desired tick mark values to D3
  this.yAxis.tickValues(ytickvalues);

  // Have D3 create our tick marks
  this.yAxisBody.call(this.yAxis)
    
  // Customize the minor ticks, and labels text
  this.yAxisBody.selectAll('g.tick')
    .each(function(d,i){
      const v = ytick[i];
      const g = d3.select(this);
      
      g.select('line')
       .attr('x1', v.major ? '-7' : '-4')
       .attr('x2', '0');
      g.select('text')
       .text(v.major ? v.text : '');
    } );

    
    if( this.yGridBody ) {
      this.yGrid.tickValues( ytickvalues );

      /*Since the number of grid lines might change (but call(self.yGrid) expects the same number) */
      /*  we will remove, and re-add back in the grid... kinda a hack. */
      /*  Could probably go back to manually drawing the grid lined and get rid */
      /*  of yGrid and yGridBody... */
      this.yGridBody.remove();
      this.yGridBody = this.vis.insert("g", ".refLineInfo")
        .attr("width", this.size.width )
        .attr("height", this.size.height )
        .attr("class", "ygrid" )
        .attr("transform", "translate(0,0)")
        .call( this.yGrid );
      this.yGridBody.selectAll('g.tick')
        .filter(function(d,i){return !ytick[i].major;} )
        .attr("class","minorgrid");
    }
}

SpectrumChartD3.prototype.setAdjustYAxisPadding = function( adjust, pad ) {
  this.options.adjustYAxisPadding = Boolean(adjust);
  
  if( typeof pad === "number" )
    this.padding.left = pad;
    
  this.handleResize( false );
}


SpectrumChartD3.prototype.setNoYAxisNumbers = function(d) {
  if( typeof d === "boolean" )
    this.options.noYAxisNumbers = d;
  this.handleResize( false );
}

SpectrumChartD3.prototype.setWheelScrollYAxis = function(d) {
  this.options.wheelScrollYAxis = d;
}

SpectrumChartD3.prototype.setChartPadding = function(d) {
  if( !d )
    return;
  const allowed_keys = [ "top", "right", "bottom", "left", "titlePad", "xTitlePad", "labelPad", "label", "sliderChart" ];
  for( const key in d )
  {
    if( Object.hasOwnProperty.call(d, key) && allowed_keys.includes(key) && (typeof d[key] === "number") )
      this.padding[key] = d[key];
  }
  this.handleResize( false );
}



/**
 * -------------- Y-axis Scale Functions --------------
 */
SpectrumChartD3.prototype.setYAxisType = function( ytype ) {
  if( ytype !== "log" && ytype !== "lin" && ytype !== "sqrt" )
    throw 'Invalid y-axis scale: ' + ytype;
    
  if( this.options.yscale === ytype )
    return;
  
  this.options.yscale = ytype;
  if( ytype === "log" )
    this.yScale = d3.scale.log().clamp(true);
  else if( ytype === "lin" )
    this.yScale = d3.scale.linear();
  else
    this.yScale = d3.scale.pow().exponent(0.5);
  
  this.yScale.domain([0, 100])
    .nice()
    .range([(ytype === "log" ? 1 : 0), this.size.height])
    .nice();
  
  this.yAxis.scale(this.yScale);
  
  if( this.yGrid )
    this.yGrid.scale( this.yScale );
    
  this.redraw()();
  this.redraw()(); //Necessary to get y-axis all lined up correctly
}

// Next functions for backward compatibility
SpectrumChartD3.prototype.setLogY = function(){ this.setYAxisType("log"); }
SpectrumChartD3.prototype.setLinearY = function(){ this.setYAxisType("lin"); }
SpectrumChartD3.prototype.setSqrtY = function(){ this.setYAxisType("sqrt"); }

SpectrumChartD3.prototype.handleYAxisWheel = function() {
  /*This function doesnt have the best behavior in the world, but its a start */
  var self = this;
  /* console.log("handleYAxisWheel"); */
  
  if( !d3.event )
    return;
    
  var m = d3.mouse(this.vis[0][0]);
  var t = d3.touches(this.vis[0][0]);

  if( m[0] > 0 && !t )
    return false;
  
  let wdelta = d3.event.deltaY ? d3.event.deltaY : d3.event.sourceEvent ? d3.event.sourceEvent.wheelDelta : 0;

  /* Implementation for touch interface */
  /* if (!wdelta && wdelta !== 0) { */
  /*   var t1,t2,dx,dy; */

  /*   t1 = t[0]; */
  /*   t2 = t[1]; */

  /*   dy = Math.abs(t1[1] - t2[1]); */

  /*   wdelta = self.previous_dy - dy; */
  /* } */

  
  var mult = 0;
  if( wdelta > 0 ){
    mult = 0.02;  //zoom out
  } else if( wdelta != 0 ) {
    mult = -0.02; //zoom in
  }

  if( self.options.yscale == "log" ) {
    
    if( mult > 0 && self.options.logYFracBottom > 0.025 ){
      self.options.logYFracBottom -= mult;
    } else {
      self.options.logYFracTop += mult;
      self.options.logYFracTop = Math.min( self.options.logYFracTop, 10 );  
    }
    
    if( self.options.logYFracTop < 0 ){
      self.options.logYFracBottom += -self.options.logYFracTop;
      self.options.logYFracBottom = Math.min( self.options.logYFracBottom, 2.505 );
      self.options.logYFracTop = 0;
    }
  
    self.options.logYFracTop = Math.max( self.options.logYFracTop, -0.95 );
  } else if( self.options.yscale == "lin" ) {
    self.options.linYFracTop += mult;
    self.options.linYFracTop = Math.min( self.options.linYFracTop, 0.85 );
    self.options.linYFracTop = Math.max( self.options.linYFracTop, -0.95 );
    /*self.options.linYFracBottom = 0.1; */
  } else if( self.options.yscale == "sqrt" ) {
    self.options.sqrtYFracTop += mult;
    self.options.sqrtYFracTop = Math.min( self.options.sqrtYFracTop, 0.85 );
    self.options.sqrtYFracTop = Math.max( self.options.sqrtYFracTop, -0.95 );
    /*self.options.sqrtYFracBottom = 0.1;   */
  }            
            
  self.redraw()();
  
  if( d3.event.sourceEvent ){       
   d3.event.sourceEvent.preventDefault();
   d3.event.sourceEvent.stopPropagation();
  } else {
    d3.event.preventDefault();
    d3.event.stopPropagation();
  }
  return false;
}


/**
 * -------------- X-axis Functions --------------
 */
SpectrumChartD3.prototype.xticks = function() {

  var ticks = [];

  /*it so */
  /*  the x axis labels (hopefully) always line up nicely where we want them */
  /*  e.g. kinda like multiple of 5, 10, 25, 50, 100, etc. */
  var EPSILON = 1E-3;

  var rendermin = this.xScale.domain()[0];
  var rendermax = this.xScale.domain()[1];
  var range = rendermax - rendermin;
  
  /*ndigstart makes up for larger numbers taking more pixels to render, so */
  /* it keeps numbers from overlapping.  Below uses 15 to kinda rpresent the */
  /*  width of numbers in pixels. */
  var ndigstart = rendermin > 0 ? Math.floor(Math.log10(rendermin)) : 1;
  ndigstart = Math.max(1,Math.min(ndigstart,5));
  
  var nlabel = Math.floor( this.size.width / (50 + 15*(ndigstart-1)) );
  var n = Math.pow(10, Math.floor(Math.log10(range/nlabel)));
  var msd = range / (n * nlabel);

  if( isNaN(n) || !isFinite(n) || nlabel<=0 || n<=0.0 ) { /*JIC */
    console.log( "n=" + n + ", nlabel=" + nlabel + ", range=" + range );
    return ticks;
  }

  let subdashes, renderInterval;
  if (msd < 1.5)
  {
    subdashes = 2;
    renderInterval = 0.5*n;
  }else if (msd < 3.3)
  {
    subdashes = 5;
    renderInterval = 0.5*n;
  }else if (msd < 7)
  {
    subdashes = 5;
    renderInterval = n;
  }else
  {
    subdashes = 10;
    renderInterval = n;
  }

  var biginterval = subdashes * renderInterval;
  var startEnergy = biginterval * Math.floor((rendermin + 1.0E-15) / biginterval);

  if( startEnergy < (rendermin-EPSILON*renderInterval) )
    startEnergy += biginterval;

  for( var i = -Math.floor(Math.floor(startEnergy-rendermin)/renderInterval); ; ++i)
  {
    if( i > 5000 )  /*JIC */
      break;

    var v = startEnergy + renderInterval * i;

    if( (v - rendermax) > EPSILON * renderInterval )
      break;

    var t = "";
    if( i>=0 && (i % subdashes == 0) )
      t += v;

    ticks.push( {value: v, major: (i % subdashes == 0), text: t } );
  }/*for( intervals to draw ticks for ) */

  return ticks;
}

/* Sets x-axis drag for the chart. These are actions done by clicking and dragging one of the labels of the x-axis. */
SpectrumChartD3.prototype.xaxisDrag = function() {
  
  var self = this;
  return function(d) {
    /*This function is called once when you click on an x-axis label (which you can then start dragging it) */
    /*  And NOT when you click on the chart and drag it to pan */

    var p = d3.mouse(self.vis[0][0]);

    if (self.xScale.invert(p[0]) > 0){           /* set self.xaxisdown equal to value of your mouse pos */
      self.xaxisdown = [self.xScale.invert(p[0]), self.xScale.domain()[0], self.xScale.domain()[1]];
    }
  }
}

SpectrumChartD3.prototype.drawXAxisArrows = function(show_arrow) {
  var self = this;

  if (self.options.showXRangeArrows) {
    var max_x;

    if (!self.xaxis_arrow) {
      self.xaxis_arrow = self.svg.select('.xaxis').append('svg:defs')
        .attr("id", "xaxisarrowdef")
        .append("svg:marker")
        .attr("id", "arrowhead")
        .attr('class', 'xaxisarrow')
        .attr("refX", 0)
        .attr("refY", 6)
        .attr("markerWidth", 9)
        .attr("markerHeight", 14)
        .attr("orient", 0)
        .append("path")
          .attr("d", "M2,2 L2,13 L8,7 L2,2")
          .style("stroke", "black");
    }

    max_x = self.min_max_x_values()[1];

    if (!self.rawData || !self.rawData.spectra || !self.rawData.spectra.length || self.xScale.domain()[1] === max_x || (typeof show_arrow == 'boolean' && !show_arrow))            /* should be a better way to determine if can still pan */
      self.xAxisBody.select("path").attr("marker-end", null);
    else
      self.xAxisBody.select("path").attr("marker-end", "url(#arrowhead)");

  } else {
    if (self.xaxis_arrow) {
      self.xaxis_arrow.remove();
      self.xaxis_arrow = null;
    }

    self.svg.select("#xaxisarrowdef").remove();
  }
}

SpectrumChartD3.prototype.drawXTicks = function() {
  const self = this;
  
  const xticks = self.xticks();
  const xtickvalues = xticks.map(function(d){return d.value;} );
  self.xAxis.tickValues( xtickvalues );

  self.xAxisBody.call(self.xAxis);

  /*Check that the last tick doesnt go off the chart area. */
  /*  This could probably be accomplished MUCH more efficiently */
  var xgticks = self.xAxisBody.selectAll('g.tick');
  var majorticks = xgticks.filter(function(d,i){ return xticks[i] && xticks[i].major; } );
  var minorticks = xgticks.filter(function(d,i){ return xticks[i] && !xticks[i].major; } );

  minorticks.select('line').attr('y2', '4');
  minorticks.select('text').text("");
  
  const labelUpperXPx = function(tick){
    const bb = tick[0][0].getBBox();
    const trans = d3.transform(tick.attr("transform")).translate;
    return trans[0] + bb.width;
  };
  
  
  if( this.hasCompactXAxis() && self.xaxistitle ){
    
    // We'll check ticks to see if it overlaps with the title, starting from right-most tick
    const xtitle_px = Number( self.xaxistitle.attr("x") );
    for( let i = majorticks[0].length - 1; i >= 0; i -= 1 ){
      const tick = d3.select( majorticks[0][i] );
      const upper_x_px = labelUpperXPx( tick );
      
      // Stop this loop once the labels are no longer overlapping
      if( upper_x_px < xtitle_px )
        break;
      
      tick.select('text').text("");
    }
  } else {
    /* We only need to check the last tick to see if it goes off the chart */
    const lastmajor = majorticks[0].length ? majorticks[0][majorticks[0].length-1] : null;
    if( lastmajor ) {
      const tick = d3.select( lastmajor );
      const upper_x_px = labelUpperXPx( tick );
      
      // There seems to be about ~10 px (or maybe 15px) too much spacing with this next test; maybe it comes from this.padding.right or this.padding.left ?  Just leaving for now, since this is good enough
      if( (upper_x_px - 10) > this.size.width ){
        tick.select('text').text("");
      }
    }
  }//if( this.hasCompactXAxis() and have x-axis title )

  if( self.xGridBody ) {
    self.xGrid.tickValues( xtickvalues );
    self.xGridBody.remove();
    self.xGridBody = self.vis.insert("g", ".refLineInfo")
          .attr("width", self.size.width )
          .attr("height", self.size.height )
          .attr("class", "xgrid" )
          .attr("transform", "translate(0," + self.size.height + ")")
          .call( self.xGrid );
    self.xGridBody.selectAll('g.tick')
      .filter(function(d,i){ return !xticks[i].major; } )
      .attr("class","minorgrid");
  }
}//SpectrumChartD3.prototype.drawXTicks


SpectrumChartD3.prototype.setXAxisRange = function( minimum, maximum, doEmit, userAction ) {
  var self = this;

  //console.log( "setXAxisRange(" + minimum + ", " + maximum + ")" );

  self.xScale.domain([minimum, maximum]);

  if( doEmit )
    self.WtEmit(self.chart.id, {name: 'xrangechanged'}, minimum, maximum, self.size.width, self.size.height, userAction);
}

SpectrumChartD3.prototype.setXAxisMinimum = function( minimum ) {
  var self = this;

  const maximum = self.xScale.domain()[1];

  self.xScale.domain([minimum, maximum]);
  self.redraw()();
}

SpectrumChartD3.prototype.setXAxisMaximum = function( maximum ) {
  var self = this;

  const minimum = self.xScale.domain()[0];

  self.xScale.domain([minimum, maximum]);
  self.redraw()();
}

SpectrumChartD3.prototype.setXRangeArrows = function(d) {
  var self = this;
  this.options.showXRangeArrows = d;
  this.drawXAxisArrows(d);
}

SpectrumChartD3.prototype.setCompactXAxis = function( compact ) {
  this.options.compactXAxis = Boolean(compact);
  
  if( this.xaxistitle )
    this.xaxistitle.style("text-anchor", this.hasCompactXAxis() ? "start" : "middle");
    
  this.handleResize( false );
}


/**
 * -------------- Chart Pan (X-axis) Slider Functions --------------
 */
SpectrumChartD3.prototype.drawXAxisSliderChart = function() {
  var self = this;
  
  // Cancel if the chart or raw data are not present
  if (!self.chart || d3.select(self.chart).empty() || !self.rawData
    || !self.rawData.spectra || !self.rawData.spectra.length || self.size.height<=0 ) {
    self.cancelXAxisSliderChart();
    return;
  }
    
  // Cancel the action and clean up if the option for the slider chart is not checked
  if( !self.options.showXAxisSliderChart ) {
    self.cancelXAxisSliderChart();
    return;
  }

  function drawDragRegionLines() {
    d3.selectAll(".sliderDragRegionLine").remove();
    
    if (!self.sliderDragLeft || !self.sliderDragRight) {
      return;
    }
    var leftX = Number(self.sliderDragLeft.attr("x"));
    var leftY = Number(self.sliderDragLeft.attr("y"));
    var leftWidth =  Number(self.sliderDragLeft.attr("width"));
    var leftHeight = self.sliderDragLeft[0][0].height.baseVal.value;
    var rightX = Number(self.sliderDragRight.attr("x"));
    var rightY = Number(self.sliderDragRight.attr("y"));
    var rightWidth =  Number(self.sliderDragRight.attr("width"));
    var rightHeight = self.sliderDragRight[0][0].height.baseVal.value;

    var numberOfLines = 4;

    for (var i = 1; i < numberOfLines; i++) {
      self.sliderChart.append('line')
        .attr("class", "sliderDragRegionLine")
        .style("fill", "#444")
        .attr("stroke", "#444" )
        .attr("stroke-width", "0.08%")
        .attr("x1", leftX + (i*leftWidth)/numberOfLines)
        .attr("x2", leftX + (i*leftWidth)/numberOfLines)
        .attr("y1", leftY + (leftHeight/4))
        .attr("y2", leftY + (3*leftHeight/4))
        .on("mousedown", self.handleMouseDownLeftSliderDrag())
        .on("mousemove", self.handleMouseMoveLeftSliderDrag(false))
        .on("mouseout", self.handleMouseOutLeftSliderDrag())
        .on("touchstart", self.handleTouchStartLeftSliderDrag())
        .on("touchmove", self.handleTouchMoveLeftSliderDrag(false));

      self.sliderChart.append('line')
        .attr("class", "sliderDragRegionLine")
        .style("fill", "#444")
        .attr("stroke", "#444" )
        .attr("stroke-width", "0.08%")
        .attr("x1", rightX + (i*rightWidth)/numberOfLines)
        .attr("x2", rightX + (i*rightWidth)/numberOfLines)
        .attr("y1", rightY + (rightHeight/4))
        .attr("y2", rightY + (3*rightHeight/4))
        .on("mousedown", self.handleMouseDownRightSliderDrag())
        .on("mousemove", self.handleMouseMoveRightSliderDrag(false))
        .on("mouseout", self.handleMouseOutRightSliderDrag())
        .on("touchstart", self.handleTouchStartRightSliderDrag())
        .on("touchmove", self.handleTouchMoveRightSliderDrag(false));
    }
  };//function drawDragRegionLines()

  // Store the original x and y-axis domain (we'll use these to draw the slider lines and position the slider box)
  var origdomain = self.xScale.domain();
  var origdomainrange = self.xScale.range();
  var origrange = self.yScale.domain();
  var bounds = self.min_max_x_values();
  var maxX = bounds[1];
  var minX = bounds[0];

  // Change the x and y-axis domain to the full range (for slider lines)
  self.xScale.domain([minX, maxX]);
  self.xScale.range([0, self.size.sliderChartWidth]);
  self.do_rebin();
  self.rebinForBackgroundSubtract();
  self.yScale.domain(self.getYAxisDomain());
  
  // Draw the elements for the slider chart
  if( !self.sliderChart ) {
    // G element of the slider chart
    d3.select(self.chart)
    
    self.sliderChart = d3.select(self.chart).select("svg").append("g")
      //.attr("transform", "translate(" + self.padding.leftComputed + "," + (this.chart.clientHeight - self.size.sliderChartHeight) + ")")
      // .on("mousemove", self.handleMouseMoveSliderChart());
      .on("touchstart", self.handleTouchStartSliderChart())
      .on("touchmove", self.handleTouchMoveSliderChart());

    // Plot area for data lines in slider chart
    self.sliderChartPlot = self.sliderChart.append("rect")
      .attr("id", "sliderchartarea"+self.chart.id )
      .attr("class", "slider-plot");

    // Chart body for slider (keeps the data lines)
    self.sliderChartBody = self.sliderChart.append("g")
      .attr("clip-path", "url(#sliderclip" + this.chart.id + ")");

    // Clip path for slider chart
    self.sliderChartClipPath = self.sliderChart.append('svg:clipPath')
        .attr("id", "sliderclip" + self.chart.id )
        .append("svg:rect")
        .attr("x", 0)
        .attr("y", 0);

    // For adding peaks into slider chart 
    // self.sliderPeakVis = self.sliderChart.append('g')
    //   .attr("id", "sliderPeakVis")
    //   .attr("class", "peakVis")
    //   .attr("transform", "translate(0,0)")
    //   .attr("clip-path", "url(#sliderclip" + this.chart.id + ")");

    if( self.options.showSliderCloseBtn ){
      // Add a close icon in upper right-hand side of self.sliderChart 
      //  Note: we define a close button for the legend using a path instead - should probably unify which method we want to use
      self.sliderClose = self.sliderChart.append("g")
        .attr("class", "slider-close")
        .on("click", function(event){
          self.setShowXAxisSliderChart(false);
          self.WtEmit(self.chart.id, {name: 'sliderChartDisplayed'}, false );
        })
        .on("touchstart.drag", function(){ // I think this event gets cancelled somewhere, so it wont be converted to a "click" automatically
          if( d3.event.touches && (d3.event.touches.length < 2) ){
            self.setShowXAxisSliderChart(false);
            self.WtEmit(self.chart.id, {name: 'sliderChartDisplayed'}, false );
          }
        } );
    
      let cross = self.sliderClose.append("g");
      self.sliderClose.append("rect").attr("width", 10).attr("height", 10).style( { "fill-opacity": 0.0, "stroke": "none" } );
      cross.append("line").attr("x1", 2).attr("y1", 2).attr("x2", 8).attr("y2", 8);
      cross.append("line").attr("x1", 8).attr("y1", 2).attr("x2", 2).attr("y2", 8);
    }//if( self.options.showSliderCloseBtn )
  }

  self.sliderChartPlot.attr("width", self.size.sliderChartWidth)
      .attr("height", self.size.sliderChartHeight);
  self.sliderChartClipPath.attr("width", self.size.sliderChartWidth)
      .attr("height", self.size.sliderChartHeight);
  self.sliderChart
      .attr("transform", "translate(" + 0.5*(self.cx - self.size.sliderChartWidth) + "," + (this.chart.clientHeight - self.size.sliderChartHeight) + ")");
  
  // Commented out for adding peaks into slider chart sometime later
  // self.sliderPeakVis.selectAll('*').remove();
  // self.peakVis.select(function() {
  //   this.childNodes.forEach(function(path) {
  //     path = d3.select(path);
  //     self.sliderPeakVis.append('path')
  //       .attr("d", path.attr('d'))
  //       .attr("class", path.attr('class'))
  //       .attr("fill-opacity", path.attr('fill-opacity'))
  //       .attr("stroke-width", path.attr('stroke-width'))
  //       .attr("stroke", path.attr("stroke"))
  //       .attr("transform", "translate(0," + (self.size.height + extraPadding + self.padding.sliderChart) + ")");
  //   });
  // });

  // Add the slider draggable box and edges
  if (!self.sliderBox) {
    // Slider box
    self.sliderBox = self.sliderChart.append("rect")
      .attr("class", "sliderBox")
      .on("mousedown", self.handleMouseDownSliderBox())
      .on("touchstart", self.handleTouchStartSliderBox())
      .on("touchmove", self.handleTouchMoveSliderChart());

    // Left slider drag region
    self.sliderDragLeft = self.sliderChart.append("rect")
      .attr("class", "sliderDragRegion")
      .attr("rx", 2)
      .attr("ry", 2)
      .on("mousedown", self.handleMouseDownLeftSliderDrag())
      .on("mousemove", self.handleMouseMoveLeftSliderDrag(false))
      .on("mouseout", self.handleMouseOutLeftSliderDrag())
      .on("touchstart", self.handleTouchStartLeftSliderDrag())
      .on("touchmove", self.handleTouchMoveLeftSliderDrag(false));

    // Right slider drag region
    self.sliderDragRight = self.sliderChart.append("rect")
      .attr("class", "sliderDragRegion")
      .attr("rx", 2)
      .attr("ry", 2)
      .on("mousedown", self.handleMouseDownRightSliderDrag())
      .on("mousemove", self.handleMouseMoveRightSliderDrag(false))
      .on("mouseout", self.handleMouseOutRightSliderDrag())
      .on("touchstart", self.handleTouchStartRightSliderDrag())
      .on("touchmove", self.handleTouchMoveRightSliderDrag(false));
  }

  var sliderBoxX = self.xScale(origdomain[0]);
  var sliderBoxWidth = self.xScale(origdomain[1]) - sliderBoxX;

  // Adjust the position of the slider box to the particular zoom region
  self.sliderBox.attr("x", sliderBoxX)
    .attr("width", sliderBoxWidth)
    .attr("height", self.size.sliderChartHeight);
    // .on("mousemove", self.handleMouseMoveSliderChart());

  self.sliderDragLeft.attr("width", self.size.sliderChartWidth/100)
    .attr("height", self.size.sliderChartHeight/2.3);
  self.sliderDragRight.attr("width", self.size.sliderChartWidth/100)
    .attr("height", self.size.sliderChartHeight/2.3);

  self.sliderDragLeft.attr("x", sliderBoxX - Number(self.sliderDragLeft.attr("width"))/2)
    .attr("y", self.size.sliderChartHeight/2 - Number(self.sliderDragLeft.attr("height"))/2);

  self.sliderDragRight.attr("x", (sliderBoxX + sliderBoxWidth) - (Number(self.sliderDragRight.attr("width"))/2))
    .attr("y", self.size.sliderChartHeight/2 - Number(self.sliderDragRight.attr("height"))/2);

  if( self.sliderClose )
    self.sliderClose.attr("transform","translate(" + (self.size.width + 11) + ",0)");

  self.drawSliderChartLines();
  drawDragRegionLines();

  // Restore the original x and y-axis domain
  self.xScale.domain(origdomain);
  self.xScale.range(origdomainrange);
  self.do_rebin();
  self.rebinForBackgroundSubtract();
  self.yScale.domain(origrange);
}

SpectrumChartD3.prototype.drawSliderChartLines = function()  {
  var self = this;

  // Cancel the action and clean up if the option for the slider chart is not checked or there is no slider chart displayed
  if( !self.options.showXAxisSliderChart || !self.sliderChartBody || !self.rawData || !self.rawData.spectra )
    return;

  // Delete the data lines if they are present
  for (let i = 0; i < self.rawData.spectra.length; ++i) {
    //console.log(self['sliderLine' + i]);
    if (self['sliderLine' + i])
      self['sliderLine' + i].remove();
  }

  for (let i = 0; i < self.rawData.spectra.length; ++i) {
    let spectrum = self.rawData.spectra[i];
    if (self.options.backgroundSubtract && spectrum.type == self.spectrumTypes.BACKGROUND) continue;

    if (self['line'+i] && self.size.height>0 && self.size.sliderChartHeight>0 ){
      const key = self.options.backgroundSubtract && ('bgsubtractpoints' in spectrum) ? 'bgsubtractpoints' : 'points';  // Figure out which set of points to use
      
      self['sliderLine'+i] = self.sliderChartBody.append("path")
        .attr("id", 'sliderLine'+i)
        .attr("class", 'sline sliderLine')
        .attr("stroke", spectrum.lineColor ? spectrum.lineColor : 'black')
        .attr("d", self['line'+i](spectrum[key]))
        .attr("transform","scale(1," + (self.size.sliderChartHeight / self.size.height) + ")");
    }
  }
}

SpectrumChartD3.prototype.cancelXAxisSliderChart = function() {
  var self = this;

  if( !self.sliderChart )
    return;

  var height = Number(d3.select(this.chart)[0][0].style.height.substring(0, d3.select(this.chart)[0][0].style.height.length - 2));

  
  if (self.sliderChart) {
    self.sliderChart.remove();
    self.sliderChartBody.remove();
    self.sliderChartPlot.remove();  
    self.sliderChartClipPath.remove();
    self.sliderBox.remove();
    self.svg.selectAll(".sliderDragRegion").remove();

    if (self.rawData && self.rawData.spectra && self.rawData.spectra.length)
      for (var i = 0; i < self.rawData.spectra.length; ++i)
        if (self['sliderLine'+i]) {
          self['sliderLine'+i].remove();
          self['sliderLine'+i] = null;
        }

    self.sliderChart = null;
    self.sliderChartPlot = null;
    self.sliderChartBody = null;
    self.sliderChartClipPath = null;
    self.sliderBox = null;
    self.sliderClose = null;
  }

  self.sliderBoxDown = false;
  self.leftDragRegionDown = false;
  self.rightDragRegionDown = false;
  self.sliderChartMouse = null;
  self.savedSliderMouse = null;
  
  this.handleResize( false );
}

SpectrumChartD3.prototype.setShowXAxisSliderChart = function(d) {
  this.options.showXAxisSliderChart = d;

  if( this.xaxistitle )
    this.xaxistitle.style("text-anchor", this.hasCompactXAxis() ? "start" : "middle");

  this.drawXAxisSliderChart();
  this.handleResize( false );
}

SpectrumChartD3.prototype.handleMouseDownSliderBox = function() {
  var self = this;

  return function() {
    /* In order to stop selection of text around chart when clicking down on slider box. */
    d3.event.preventDefault();
    d3.event.stopPropagation();

    self.sliderBoxDown = true;
    self.leftDragRegionDown = false;
    self.rightDragRegionDown = false;

    /* Initially set the escape key flag false */
    /* ToDo: record initial range so if escape is pressed, can reset to it */
    self.escapeKeyPressed = false;    

    d3.select(document.body).style("cursor", "move");
  }
}

SpectrumChartD3.prototype.handleMouseMoveSliderChart = function() {
  var self = this;

  return function() {
    
    if (self.leftDragRegionDown || self.rightDragRegionDown || self.sliderBoxDown) {
      d3.event.preventDefault();
      d3.event.stopPropagation();
    }
    
    /* console.log("sliderboxdown = ", self.sliderBoxDown); */
    /* console.log("leftDragRegionDown = ", self.leftDragRegionDown); */
    /* console.log("rightDragRegionDown = ", self.rightDragRegionDown); */

    if (self.leftDragRegionDown) {
      return self.handleMouseMoveLeftSliderDrag(true)();
    }

    if (self.rightDragRegionDown) {
      return self.handleMouseMoveRightSliderDrag(true)();
    }

    if (self.sliderBoxDown) {
      d3.select(document.body).style("cursor", "move");
      var m = d3.mouse(self.sliderChart[0][0]);
      var origdomain = self.xScale.domain();
      var origdomainrange = self.xScale.range();
      var bounds = self.min_max_x_values();
      var maxX = bounds[1];
      var minX = bounds[0];

      if (!self.sliderChartMouse) {
        self.sliderChartMouse = m;
      }

      var sliderBoxX = Number(self.sliderBox.attr("x"));
      var sliderBoxWidth = Number(self.sliderBox.attr("width"));
      var sliderDragRegionWidth = 3;
      var x = Math.min( self.size.sliderChartWidth - sliderBoxWidth, Math.max(0, sliderBoxX + (m[0] - self.sliderChartMouse[0])) );

      if ((sliderBoxX == 0 || sliderBoxX + sliderBoxWidth == self.size.sliderChartWidth)) {
        if (!self.savedSliderMouse)
          self.savedSliderMouse = m;
      }

      if (self.savedSliderMouse && m[0] != self.savedSliderMouse[0]) {
        if (sliderBoxX == 0 && m[0] < self.savedSliderMouse[0]) return;
        else if (sliderBoxX + sliderBoxWidth == self.size.sliderChartWidth && m[0] > self.savedSliderMouse[0]) return;
        else self.savedSliderMouse = null;
      }

      self.xScale.domain([minX, maxX]);
      self.xScale.range([0, self.size.sliderChartWidth]);
      self.sliderBox.attr("x", x);
      self.sliderDragLeft.attr("x", x);
      self.sliderDragRight.attr("x", x + sliderBoxWidth - sliderDragRegionWidth);

      origdomain = [ self.xScale.invert(x), self.xScale.invert(x + sliderBoxWidth) ];
      self.setXAxisRange(origdomain[0], origdomain[1], true, true);
      self.xScale.range(origdomainrange);
      self.redraw()();

      self.sliderChartMouse = m;
    }
  }
}

SpectrumChartD3.prototype.handleMouseDownLeftSliderDrag = function() {
  var self = this;

  return function() {
    /* In order to stop selection of text around chart when clicking down on slider box. */
    d3.event.preventDefault();
    d3.event.stopPropagation();

    self.leftDragRegionDown = true;

    /* Initially set the escape key flag false */
    self.escapeKeyPressed = false;    
  }
}

SpectrumChartD3.prototype.handleMouseMoveLeftSliderDrag = function(redraw) {
  var self = this;

  return function() {
    d3.event.preventDefault();
    d3.event.stopPropagation();

    if (self.sliderBoxDown) {
      return; /*self.handleMouseMoveSliderChart()(); */
    }

    d3.select(document.body).style("cursor", "ew-resize");
    
    if (!self.leftDragRegionDown || !redraw) {
      return;
    }

    var m = d3.mouse(self.sliderChart[0][0]);
    var origdomain = self.xScale.domain();
    var origdomainrange = self.xScale.range();
    var bounds = self.min_max_x_values();
    var maxX = bounds[1];
    var minX = bounds[0];
    var x = Math.max(m[0], 0);
    var sliderDragPadding = 1;

    self.xScale.domain([minX, maxX]);
    self.xScale.range([0, self.size.sliderChartWidth]);

    if (m[0] > Number(self.sliderDragRight.attr("x") - sliderDragPadding)) {
      self.xScale.domain(origdomain);
      return;
    }

    self.sliderBox.attr("x", x);
    self.sliderDragLeft.attr("x", x);
    origdomain[0] = self.xScale.invert(x);

    self.setXAxisRange(origdomain[0], origdomain[1], true, true);
    self.xScale.range(origdomainrange);
    self.redraw()();
  }
}

SpectrumChartD3.prototype.handleMouseOutLeftSliderDrag = function() {
  var self = this;

  return function() {
    if (!self.leftDragRegionDown) {
      d3.select(document.body).style("cursor", "default");
    }
  }
}

SpectrumChartD3.prototype.handleMouseDownRightSliderDrag = function() {
  var self = this;

  return function() {
    /* In order to stop selection of text around chart when clicking down on slider box. */
    d3.event.preventDefault();
    d3.event.stopPropagation();

    self.rightDragRegionDown = true;

    /* Initially set the escape key flag false */
    self.escapeKeyPressed = false;    
  }
}

SpectrumChartD3.prototype.handleMouseMoveRightSliderDrag = function(redraw) {
  var self = this;

  return function() {
    if (self.sliderBoxDown)
      return;

    d3.event.preventDefault();
    d3.event.stopPropagation();
    d3.select('body').style("cursor", "ew-resize");

    if (!self.rightDragRegionDown || !redraw )
      return;

    const m = d3.mouse(self.sliderChart[0][0]);
    const origdomain = self.xScale.domain();
    const origdomainrange = self.xScale.range();
    const bounds = self.min_max_x_values();
    const maxX = bounds[1], minX = bounds[0];
    const x = Math.min(m[0], self.size.sliderChartWidth);
    const sliderDragRegionWidth = 3;

    self.xScale.domain([minX, maxX]);
    self.xScale.range([0, self.size.sliderChartWidth]);

    if (m[0] - sliderDragRegionWidth < Number(self.sliderDragLeft.attr("x")) + Number(self.sliderDragLeft.attr("width"))) {
      self.xScale.domain(origdomain);
      self.xScale.range(origdomainrange);
      return;
    }

    self.sliderBox.attr("width", Math.abs(x - Number(self.sliderDragRight.attr("x"))));
    self.sliderDragRight.attr("x", x - sliderDragRegionWidth);
    origdomain[1] = self.xScale.invert(x);

    self.setXAxisRange(origdomain[0], origdomain[1], true, true);
    self.xScale.range(origdomainrange);
    self.redraw()();
  }
}

SpectrumChartD3.prototype.handleMouseOutRightSliderDrag = function() {
  var self = this;

  return function() {
    if (!self.rightDragRegionDown) {
      d3.select(document.body).style("cursor", "default");
    }
  }
}

SpectrumChartD3.prototype.handleTouchStartSliderBox = function() {
  var self = this;

  return function() {
    d3.event.preventDefault();
    d3.event.stopPropagation();

    self.sliderBoxDown = true;
    self.leftDragRegionDown = false;
    self.rightDragRegionDown = false;

    var t = [d3.event.pageX, d3.event.pageY];
    var touchError = 25;
    var x1 = self.sliderBox[0][0].getBoundingClientRect().left;
    var x2 = self.sliderBox[0][0].getBoundingClientRect().right;

    if (d3.event.changedTouches.length !== 1)
      return;

    if (x1 - touchError <= t[0] && t[0] <= x1 + touchError) {
      self.sliderBoxDown = false;
      self.leftDragRegionDown = true;

    } else if (x2 - touchError <= t[0] && t[0] <= x2 + touchError) {
      self.sliderBoxDown = false;
      self.rightDragRegionDown = true;
    }
  }
}

SpectrumChartD3.prototype.handleTouchStartSliderChart = function() {
  var self = this;

  return function() {
    var t = [d3.event.pageX, d3.event.pageY];
    var touchError = 15;
    var x1 = self.sliderBox[0][0].getBoundingClientRect().left;
    var x2 = self.sliderBox[0][0].getBoundingClientRect().right;

    if (d3.event.changedTouches.length !== 1)
      return;

    if (x1 - touchError <= t[0] && t[0] <= x1 + touchError) {
      self.sliderBoxDown = false;
      self.leftDragRegionDown = true;

    } else if (x2 - touchError <= t[0] && t[0] <= x2 + touchError) {
      self.sliderBoxDown = false;
      self.rightDragRegionDown = true;
    }
  }
}

SpectrumChartD3.prototype.handleTouchMoveSliderChart = function() {
  var self = this;

  return function() {
    d3.event.preventDefault();
    d3.event.stopPropagation();

    if (self.leftDragRegionDown) {
      return self.handleTouchMoveLeftSliderDrag(true)();
    }

    if (self.rightDragRegionDown) {
      return self.handleTouchMoveRightSliderDrag(true)();
    }

    if (self.sliderBoxDown) {
      d3.select(document.body).style("cursor", "move");
      var t = d3.touches(self.sliderChart[0][0]);

      if (t.length !== 1)
        return;

      t = t[0];
      var origdomain = self.xScale.domain();
      var origdomainrange = self.xScale.range();
      var bounds = self.min_max_x_values();
      var maxX = bounds[1];
      var minX = bounds[0];

      if (!self.sliderChartTouch) {
        self.sliderChartTouch = t;
      }

      var sliderBoxX = Number(self.sliderBox.attr("x"));
      var sliderBoxWidth = Number(self.sliderBox.attr("width"));
      var sliderDragRegionWidth = 3;
      var x = Math.min( self.size.sliderChartWidth - sliderBoxWidth, Math.max(0, sliderBoxX + (t[0] - self.sliderChartTouch[0])) );

      if ((sliderBoxX == 0 || sliderBoxX + sliderBoxWidth == self.size.sliderChartWidth)) {
        if (!self.savedSliderTouch)
          self.savedSliderTouch = t;
      }

      if (self.savedSliderTouch && t[0] != self.savedSliderTouch[0]) {
        if (sliderBoxX == 0 && t[0] < self.savedSliderTouch[0]) return;
        else if (sliderBoxX + sliderBoxWidth == self.size.sliderChartWidth && t[0] > self.savedSliderTouch[0]) return;
        else self.savedSliderTouch = null;
      }

      self.xScale.domain([minX, maxX]);
      self.xScale.range([0, self.size.sliderChartWidth]);
      self.sliderBox.attr("x", x);
      self.sliderDragLeft.attr("x", x);
      self.sliderDragRight.attr("x", x + sliderBoxWidth - sliderDragRegionWidth);

      origdomain = [ self.xScale.invert(x), self.xScale.invert(x + sliderBoxWidth) ];
      self.setXAxisRange(origdomain[0], origdomain[1], true, true);
      self.xScale.range(origdomainrange);
      self.redraw()();

      self.sliderChartTouch = t;
    }
  }
}

SpectrumChartD3.prototype.handleTouchStartLeftSliderDrag = function() {
  var self = this;

  return function() {
    self.leftDragRegionDown = true;
  }
}

SpectrumChartD3.prototype.handleTouchMoveLeftSliderDrag = function(redraw) {
  var self = this;

  return function() {
    d3.event.preventDefault();
    d3.event.stopPropagation();

    if (self.sliderBoxDown) {
      return;
    }

    if (!self.leftDragRegionDown) {
      return;
    }

    var t = d3.touches(self.sliderChart[0][0]);
    if (t.length !== 1)
      return;

    t = t[0];
    var origdomain = self.xScale.domain();
    var origdomainrange = self.xScale.range();
    var bounds = self.min_max_x_values();
    var maxX = bounds[1];
    var minX = bounds[0];
    var x = Math.max(t[0], 0);

    var sliderDragPadding = 1;

    self.xScale.domain([minX, maxX]);
    self.xScale.range([0, self.size.sliderChartWidth]);

    if (t[0] > Number(self.sliderDragRight.attr("x") - sliderDragPadding)) {
      self.xScale.domain(origdomain);
      return;
    }

    self.sliderBox.attr("x", x);
    self.sliderDragLeft.attr("x", x);
    origdomain[0] = self.xScale.invert(x);

    self.setXAxisRange(origdomain[0], origdomain[1], true, true);
    self.xScale.range(origdomainrange);
    self.redraw()();
  }
}

SpectrumChartD3.prototype.handleTouchStartRightSliderDrag = function() {
  var self = this;

  return function() {
    self.rightDragRegionDown = true;
  }
}

SpectrumChartD3.prototype.handleTouchMoveRightSliderDrag = function(redraw) {
  var self = this;

  return function() {
    if (self.sliderBoxDown) {
      return;
    }

    d3.event.preventDefault();
    d3.event.stopPropagation();

    if (!self.rightDragRegionDown) {
      return;
    }

    var t = d3.touches(self.sliderChart[0][0]);
    if (t.length !== 1)
      return;
  
    t = t[0];
    var origdomain = self.xScale.domain();
    var origdomainrange = self.xScale.range();
    var bounds = self.min_max_x_values();
    var maxX = bounds[1];
    var minX = bounds[0];
    var x = Math.min(t[0], self.size.sliderChartWidth);

    var sliderBoxX = self.xScale(origdomain[0]);
    var sliderBoxWidth = Number(self.sliderBox.attr("width"));
    var sliderDragRegionWidth = 3;
    var sliderDragPadding = 1;

    self.xScale.domain([minX, maxX]);
    self.xScale.range([0, self.size.sliderChartWidth]);

    if (t[0] - sliderDragRegionWidth < Number(self.sliderDragLeft.attr("x")) + Number(self.sliderDragLeft.attr("width"))) {
      self.xScale.domain(origdomain);
      self.xScale.range(origdomainrange);
      return;
    }

    self.sliderBox.attr("width", Math.abs(x - Number(self.sliderDragRight.attr("x"))));
    self.sliderDragRight.attr("x", x - sliderDragRegionWidth);
    origdomain[1] = self.xScale.invert(x);

    self.setXAxisRange(origdomain[0], origdomain[1], true, true);
    self.xScale.range(origdomainrange);
    self.redraw()();
  }
}


/**
 * -------------- Scale Factor Functions --------------
 */
SpectrumChartD3.prototype.numYScalers = function() {
  var self = this;
  
  if( !this.options.scaleBackgroundSecondary || !self.rawData || !self.rawData.spectra )
    return 0;
  
  let nonFore = 0;
  self.rawData.spectra.forEach(function (spectrum) {
    if( spectrum && spectrum.type && spectrum.type !== self.spectrumTypes.FOREGROUND
      && spectrum.yScaleFactor != null && spectrum.yScaleFactor >= 0.0 )
    nonFore += 1;
  });
  
  return nonFore;
}


SpectrumChartD3.prototype.cancelYAxisScalingAction = function() {
  var self = this;
  
  if( !self.rawData || !self.rawData.spectra || self.currentlyAdjustingSpectrumScale === null )
    return;
  
  console.log( 'cancelYAxisScalingAction');
  
  var scale = null;
  for (var i = 0; i < self.rawData.spectra.length; ++i) {
    let spectrum = self.rawData.spectra[i];
    
    if( spectrum.type == self.currentlyAdjustingSpectrumScale ) {
      spectrum.yScaleFactor = spectrum.startingYScaleFactor;
      spectrum.startingYScaleFactor = null;
      self.endYAxisScalingAction()();
      self.redraw()();
      return;
    }
  }
}

SpectrumChartD3.prototype.endYAxisScalingAction = function() {
  var self = this;
  
  return function(){
    if( self.currentlyAdjustingSpectrumScale === null
      || !self.rawData || !self.rawData.spectra )
      return;

    console.log( 'endYAxisScalingAction');
    
    var scale = null;
  
    for( var i = 0; i < self.rawData.spectra.length; ++i ) {
    
      let spectrum = self.rawData.spectra[i];
    
      if( spectrum.type == self.currentlyAdjustingSpectrumScale ) {
        spectrum.sliderText.style( "display", "none" );
        spectrum.sliderToggle.attr("cy", Number(spectrum.sliderRect.attr("y"))
                                          + Number(spectrum.sliderRect.attr("height"))/2);
                                          
        spectrum.sliderRect.attr("stroke-opacity", 0.8).attr("fill-opacity", 0.3);
        spectrum.sliderToggle.attr("stroke-opacity", 0.8).attr("fill-opacity", 0.7);
                                          
        spectrum.startingYScaleFactor = null;
        scale = spectrum.yScaleFactor;
        break;
      }
    }
  
    if( scale !== null ){
      self.WtEmit(self.chart.id, {name: 'yscaled'}, scale, self.currentlyAdjustingSpectrumScale );
      console.log('Emmitted yscaled scale=' + scale + ', type=' + self.currentlyAdjustingSpectrumScale );
    } else {
      console.log('Failed to find scale factor being adjusted');
    }
  
    self.currentlyAdjustingSpectrumScale = null;
  }
}


SpectrumChartD3.prototype.drawScalerBackgroundSecondary = function() {
  var self = this;
  
  //This function called from setData() and handleResize(), so not too often.
  //  (ToDo: it also gets needlessly called occasitonally when zooming - should fix)
  
  //console.log( 'drawScalerBackgroundSecondary' );
  
  //ToDo: - Instead of using spectrum.type to identify which spectrum is being scaled, use spectrum.id (but make sure id is always unique)
  
  const nScalers = self.numYScalers();
  if( nScalers === 0 || self.size.height < 35 ){
    if( self.scalerWidget )
      self.removeSpectrumScaleFactorWidget();
    return;
  }

  if( !self.scalerWidget ){
    self.scalerWidget = d3.select(this.chart).select("svg").append("g")
      .attr("class", "scalerwidget")
      .attr("transform","translate(" + (this.cx - 20*nScalers) + "," + this.padding.topComputed + ")");

    self.scalerWidgetBody = self.scalerWidget.append("g")
      .attr("transform","translate(0,0)");
  }

  //The number of scalers may have changed since we created self.scalerWidget,
  //  so update its position.
  self.scalerWidget
      .attr("transform","translate(" + (this.cx - 20*nScalers) + "," + this.padding.topComputed + ")");
  
  
  self.scalerWidgetBody.selectAll("g").remove();

  var scalerHeight = self.size.height - 30;
  //var toggleRadius = self.isTouchDevice() ? 10 : 7;  //ToDo: For touch devices if we go to 10 px, then it isnt centered on spectrum.sliderRect, and also it hangs off the screen (or at least would got to very edge.
  var toggleRadius = 7;
  var ypos = 15;
  
  var scalenum = 0;
  self.rawData.spectra.forEach(function(spectrum,i) {
    var spectrumScaleFactor = spectrum.yScaleFactor;
    var spectrumSelector = 'Spectrum-' + spectrum.id;

    if (i == 0 || spectrum.type === self.spectrumTypes.FOREGROUND)   /* Don't add scaling functionality for foreground */
    return;
      
    if (spectrumScaleFactor != null && spectrumScaleFactor >= 0) {
      scalenum += 1;
      
      const speccolor = spectrum.lineColor; //If null, will use CSS variables
      
      var spectrumSliderArea = self.scalerWidgetBody.append("g")
        .attr("id", spectrumSelector + "SliderArea")
        .attr("transform","translate(" + 20*(scalenum-1) + "," + ypos + ")");
          
      if( spectrum.type === self.spectrumTypes.BACKGROUND )
        spectrumSliderArea.attr("class", "BackgroundScaler");
      else if( spectrum.type === self.spectrumTypes.SECONDARY )
        spectrumSliderArea.attr("class", "SecondaryScaler");
        
      spectrum.sliderText = spectrumSliderArea.append("text")
        .attr("class", "scalertxt")
        .attr("x", 0)
        .attr("y", self.size.height-15)
        .attr("text-anchor", "start")
        .style( "display", "none" )
        .text( "" + spectrumScaleFactor.toFixed(3));

      spectrum.sliderRect = spectrumSliderArea.append("rect")
        .attr("class", "scaleraxis")
        .attr("y", 0 /* + (isTouchDevice() ? 5 : 0)*/ )
        .attr("x", 8)
        .attr("rx", 5)
        .attr("ry", 5)
        .attr("fill", speccolor )
        .attr("width", "4px")
        .attr("height", scalerHeight );
 

      spectrum.sliderToggle = spectrumSliderArea.append("circle")
        .attr("class", "scalertoggle")
        .attr("cx", Number(spectrum.sliderRect.attr("x")) + toggleRadius/2 - 1)
        .attr("cy", Number(spectrum.sliderRect.attr("y")) + scalerHeight/2)
        .attr("r", toggleRadius)
        .attr("stroke-opacity", 0.8)
        .attr("fill", speccolor )
        .attr("fill-opacity", 0.7)
        .style("cursor", "pointer")
        .on("mousedown", function(){
          self.handleMouseMoveScaleFactorSlider();
          spectrum.sliderRect.attr("stroke-opacity", 1.0).attr("fill-opacity", 1.0);
          spectrum.sliderToggle.attr("stroke-opacity", 1.0).attr("fill-opacity", 1.0);
          spectrum.startingYScaleFactor = spectrum.yScaleFactor;
          self.currentlyAdjustingSpectrumScale = spectrum.type;
          spectrum.sliderText.style( "display", null )
          d3.event.preventDefault();
          d3.event.stopPropagation();
        })
        .on("mousemove", self.handleMouseMoveScaleFactorSlider())
        .on("mouseup", self.endYAxisScalingAction() )
        .on("touchstart", function(){
          self.handleMouseMoveScaleFactorSlider();
          spectrum.sliderRect.attr("stroke-opacity", 1.0).attr("fill-opacity", 1.0);
          spectrum.sliderToggle.attr("stroke-opacity", 1.0).attr("fill-opacity", 1.0);
          spectrum.startingYScaleFactor = spectrum.yScaleFactor;
          self.currentlyAdjustingSpectrumScale = spectrum.type;
          spectrum.sliderText.style( "display", null )
        })
        .on("touchmove", self.handleMouseMoveScaleFactorSlider())
        .on("touchend", self.endYAxisScalingAction() );
    }
  });
}

SpectrumChartD3.prototype.removeSpectrumScaleFactorWidget = function() {
  var self = this;

  if (self.scalerWidget) {
    self.scalerWidget.remove();
    self.scalerWidget = null;
    self.scalerWidgetBody = null;
    
    if( self.rawData && self.rawData.spectra ) {
      self.rawData.spectra.forEach( function(spectrum,i) {
        spectrum.sliderText = null;
        spectrum.sliderRect = null;
        spectrum.sliderToggle = null;
      } );
    }
  }
}


SpectrumChartD3.prototype.setShowSpectrumScaleFactorWidget = function(d) {
  this.options.scaleBackgroundSecondary = d;
  this.handleResize( false );
}

SpectrumChartD3.prototype.handleMouseMoveScaleFactorSlider = function() {
  var self = this;

  /* Here is a modified, slightly more efficient version of redraw 
    specific to changing the scale factors for spectrums. 
  */
  function scaleFactorChangeRedraw(spectrum, linei) {
    self.updateLegend();
    self.adjustYScaleOfDisplayedDataPoints(spectrum, linei);
    self.do_rebin();
    self.rebinForBackgroundSubtract();
    self.setYAxisDomain();
    self.drawYTicks();
    self.update();
    self.drawPeaks();
  }

  return function() {
    if (!self.rawData || !self.rawData.spectra || !self.rawData.spectra.length )
      return;
    if (!self.currentlyAdjustingSpectrumScale)
      return;

    /* Check for the which corresponding spectrum line is the background */
    var linei = null;
    var spectrum = null;
    for (var i = 0; i < self.rawData.spectra.length; ++i) {
      if (self.rawData.spectra[i].type == self.currentlyAdjustingSpectrumScale) {
        linei = i;
        spectrum = self.rawData.spectra[i];
        break;
      }
    }

    d3.event.preventDefault();
    d3.event.stopPropagation();

    if (linei === null || spectrum === null)
      return;

    d3.select(document.body).style("cursor", "pointer");

    var m = d3.mouse(spectrum.sliderRect[0][0]);
    if( !m ){
      //ToDo: test that this works, and is necassary on touch devices!
      //console.log( 'Using touches!' );
      m = d3.touches(spectrum.sliderRect[0][0]);
      if( m.length !== 1 )
        return;
      m = m[0];
    }
    
    
    let scalerHeight = Number(spectrum.sliderRect.attr("height"));
    let rad = Number(spectrum.sliderToggle.attr("r"));
    let newTogglePos = Math.min(Math.max(0,m[1]+rad/2),scalerHeight);
    
    let fracOnScale = 1.0 - newTogglePos/scalerHeight;
    
    
    var sf = 1.0;
    if( self.options.yscale === "log" ) {
      //[0.001*scale, 1000*scale]
      sf = Math.exp( Math.log(0.001) + fracOnScale*(Math.log(1000) - Math.log(0.001)) );
    } else if( self.options.yscale === "sqrt" ) {
      //ToDo: make so sf will go from 0.01 to 100 with 0.5 giving 1
      sf = 1.38734467428311845572*(Math.exp(fracOnScale) - 1) + 0.1;
    } else {
      //ToDo: make so when fracOnScale is zero, want scale=0.1, when 0.5 want scale=1.0, when 1.0 want scale=10
      //from 0.1 to 1.9
      //sf = 1.8*fracOnScale + 0.1;
      sf = 1.38734467428311845572*(Math.exp(fracOnScale) - 1) + 0.1;
    }
    
    var spectrumScaleFactor = sf*spectrum.startingYScaleFactor;
    
    spectrum.sliderToggle.attr("cy", newTogglePos );
    spectrum.sliderText.text( (spectrumScaleFactor % 1 != 0) ? spectrumScaleFactor.toFixed(3) : spectrumScaleFactor.toFixed() );
    spectrum.yScaleFactor = spectrumScaleFactor;
    
    // If we are using background subtract, we have to redraw the entire chart if we update the scale factors
    if (self.options.backgroundSubtract) self.redraw()();
    else scaleFactorChangeRedraw(spectrum, linei);


    /* Update the slider chart if needed */
    if (self["sliderLine"+linei]) {
      var origdomain = self.xScale.domain();
      var origdomainrange = self.xScale.range();
      var origrange = self.yScale.domain();
      var bounds = self.min_max_x_values();
      var maxX = bounds[1];
      var minX = bounds[0];

      /* Change the x and y-axis domain to the full range (for slider lines) */
      self.xScale.domain([minX, maxX]);
      self.xScale.range([0, self.size.sliderChartWidth]);
      self.do_rebin();
      self.rebinForBackgroundSubtract();
      self.yScale.domain(self.getYAxisDomain());

      self.rawData.spectra.forEach(function(spec, speci) {
        if (self["sliderLine"+speci]){
          const key = self.options.backgroundSubtract && ('bgsubtractpoints' in spec) ? 'bgsubtractpoints' : 'points';
          self["sliderLine"+speci].attr("d", self["line"+speci](spec[key]));
        }
      })
      

      /* Restore the original x and y-axis domain */
      self.xScale.domain(origdomain);
      self.xScale.range(origdomainrange);
      self.do_rebin();
      self.rebinForBackgroundSubtract();
      self.yScale.domain(origrange);
    }
  }
}


SpectrumChartD3.prototype.offset_integral = function(roi,x0,x1){
  if( roi.type === 'NoOffset' || x0===x1 )
    return 0.0;
  
  if( (roi.type === 'External') || (roi.type === 'FlatStep') || (roi.type === 'LinearStep')|| (roi.type === 'BiLinearStep') ){
    //console.log( roi );

    //let doDebug = false;
    //if( doDebug ) console.log( 'x0=' + x0 + ', x1=' + x1 );
    
    let energies = roi.continuumEnergies;
    let counts = roi.continuumCounts;

    if( !energies || !energies.length || !counts || !counts.length ){
      console.warn( 'External continuum does not have continuumEnergies or continuumCounts' );
      return 0.0;
    }

    let bisector = d3.bisector(function(d){return d;});
    let cstartind = bisector.left( energies, Math.max(roi.lowerEnergy,x0) );
    let cendind = bisector.right( energies, Math.min(roi.upperEnergy,x1) );
    
    if( cstartind >= (energies.length-1) )
      return 0.0;  //shouldnt ever happen
      
    if( cendind >= (energies.length-1) )
      cendind = energies.length - 1;

    if( cstartind > 0 && energies[cstartind] > x0 )
      cstartind = cstartind - 1;
    if( cendind > 0 && energies[cendind] >= x1 )
      cendind = cendind - 1;

    if( cstartind === cendind ){
      //if( doDebug ) console.log( 'counts[' + cstartind + ']=' + counts[cstartind] );
      return counts[cstartind] * (x1-x0) / (energies[cstartind+1] - energies[cstartind]);
    }

    //figure out fraction of first bin
    let frac_first = (energies[cstartind+1] - x0) / (energies[cstartind+1] - energies[cstartind]);
    let frac_last = 1.0 - (energies[cendind+1] - x1) / (energies[cendind+1] - energies[cendind]);
    
    //if( doDebug )
    //{
    //  console.log( 'frac_first=' + frac_first + ', frac_last=' + frac_last );
    //  for( var i = cstartind; i <= cendind; i++ )
    //  {
    //    console.log( 'index=' + i + ', energy={' + energies[i] + ',' + energies[i+1] + '}' );
    //  }
    //}
    //console.log( 'x1=' + x1 + ', cendind=' + cendind + ', energy={' + energies[cendind] + ',' + energies[cendind+1] + '}, frac_last=' + frac_last);

    let sum = frac_first*counts[cstartind] + frac_last*counts[cendind];
    for( let i = cstartind+1; i < cendind; ++i )
      sum += counts[i];
    return sum;
  }//if( roi.type is 'External' or 'Step' )

  x0 -= roi.referenceEnergy; x1 -= roi.referenceEnergy;
  var answer = 0.0;
  for( var i = 0; i < roi.coeffs.length; ++i )
    answer += (roi.coeffs[i]/(i+1)) * (Math.pow(x1,i+1) - Math.pow(x0,i+1));
  return Math.max( answer, 0.0 );
}

/**
 * -------------- Peak ROI/Label Rendering Functions --------------
 */
SpectrumChartD3.prototype.drawPeaks = function() {
  var self = this;

  self.peakVis.selectAll("*").remove();
  self.peakPaths = [];
  self.labelinfo = null;
  
  
  if( !this.rawData || !this.rawData.spectra ) 
    return;

  var minx = self.xScale.domain()[0], maxx = self.xScale.domain()[1];

  let labelinfo = [];
  let showlabels = (self.options.showUserLabels || self.options.showPeakLabels || self.options.showNuclideNames);
  
  
  /* Returns an array of paths.  
     - The first path will be an underline of entire ROI
     - The next roi.peaks.length entries are the fills for each of the peaks
     - The next roi.peaks.length entries are the path of the peak, that sits on the ROI
   */
  function roiPath(roi,points,scaleFactor,background){
    var paths = [];
    var labels = showlabels ? [] : null;
    var bisector = d3.bisector(function(d){return d.x;});
    
    let roiLB = Math.max(roi.lowerEnergy,minx);
    let roiUB = Math.min(roi.upperEnergy,maxx);
    
    let xstartind = bisector.left( points, roiLB );
    let xendind = bisector.right( points, roiUB );

    // Boolean to signify whether to subtract points from background
    const useBackgroundSubtract = self.options.backgroundSubtract && background;

    if( xstartind >= (points.length-2) )
      return { paths: paths };
      
    if( xendind >= (points.length-2) )
      xendind = points.length - 2;
   
    if( xstartind >= xendind )
      return { paths: paths };
   
    /*The continuum values used for the first and last bin of the ROI are fudged */
    /*  for now...  To be fixed */
    let thisy = null, thisx = null, m, s, peak_area, cont_area;
    
    let firsty = self.offset_integral( roi, points[xstartind-(xstartind?1:0)].x, points[xstartind+(xstartind?0:1)].x ) * scaleFactor;
    
    // Background Subtract - Subtract the initial y-value with the corresponding background point
    if (useBackgroundSubtract) {
      const bi = bisector.left(background.points, points[xstartind-(xstartind?1:0)].x);
      firsty -= background.points[bi] ? background.points[bi].y : 0;
    }
    
    //paths[0] = "M" + self.xScale(points[xstartind].x) + "," + self.yScale(firsty) + " L";
    paths[0] = "M" + self.xScale(roiLB) + "," + self.yScale(firsty) + " L";
    
    for( let j = 0; j < 2*roi.peaks.length; ++j )
      paths[j+1] = "";
      
    //Go from left to right and create lower path for each of the outlines that sit on the continuum
    for( var i = xstartind; i < xendind; ++i ) {
      const x0 = points[i].x, x1 = points[i+1].x;
      thisx = ((i===xstartind) ? roiLB : ((i===(xendind-1)) ? roiUB : (0.5*(x0 + x1))));
      thisy = self.offset_integral( roi, x0, x1 ) * scaleFactor;
      
      // Background Subtract - Subtract the current y-value with the corresponding background point
      if (useBackgroundSubtract) {
        var bi = bisector.left(background.points, points[i].x);
        thisy -= background.points[bi] ? background.points[bi].y : 0; 
      }

      paths[0] += " " + self.xScale(thisx) + "," + self.yScale(thisy);
      
      //console.log( "[points[" + i + "].x,points[" + "i+1" + "].x,thisy,yScale(thisy)]={", points[i].x,points[i+1].x,thisy, self.yScale(thisy), "}");
      
      for( let j = 0; j < roi.peaks.length; ++j ) {
        m = roi.peaks[j].Centroid[0];
        s = roi.peaks[j].Width[0];
        const vr = roi.peaks[j].visRange ? roi.peaks[j].visRange : [(m - 5*s),(m + 5*s)];
        
        if( labels && m>=points[i].x && m<points[i+1].x ) {
          //This misses any peaks with means not on the chart
          if( !labels[j] )
            labels[j] = {};
          //ToDo: optimize what we actually need in these objects
          labels[j].xindex = i;
          labels[j].roiPeakIndex = j;
          labels[j].roi = roi;
          labels[j].peak = roi.peaks[j];
          labels[j].centroidXPx = self.xScale(m);
          labels[j].centroidMinYPx = self.yScale(thisy);
          labels[j].energy = m;
          labels[j].userLabel = roi.peaks[j].userLabel;
        }//if( the centroid of this peak is in this bin )
        
        if( roi.peaks.length===1 || ((vr[0] <= x1) && (vr[1] >= x0)) || ((thisx >= vr[0]) && (thisx < vr[1])) ){
          // This next index looks suspect, should it be `j+1+roi.peaks.length`
          if( !paths[j+1].length ){
            paths[j+1+roi.peaks.length] = "M" + self.xScale(thisx) + "," + self.yScale(thisy) + " L";
          }else{
            paths[j+1+roi.peaks.length] += " " + self.xScale(thisx) + "," + self.yScale(thisy);
          }
        }
      }
    }//for( var i = xstartind; i < xendind; ++i )

    
    function erf(x) {
      // Error is less than 1.5 * 10-7 for all inputs (from Handbook of Mathematical Functions)
      const sign = (x >= 0) ? 1 : -1; /* save the sign of x */
      x = Math.abs(x);
      const t = 1.0/(1.0 + 0.3275911*x);
      const y = 1.0 - (((((1.061405429 * t + -1.453152027) * t) + 1.421413741) * t + -0.284496736) * t + 0.254829592) * t * Math.exp(-x * x);
      return sign * y; /* erf(-x) = -erf(x); */
    };
    
    function erfc( z ){
      // Adapted from boost erf implementation - same as used in InterSpecs C++, see Boost Software License - Version 1.0
      // The Bortel distribution requires a higher precision erfc, than just 1-erf, or even simpler approximations of erfc
      if( z < 0 )
        return (z < -0.5) ? (2 - erfc( -z )) : (1 + erf( -z ));

      if( z < 0.5 )
        return 1 - erf( z );
      
      if( z >= 28 )
        return 0;
      
      if(z < 1.5 )
      {
        const P = [ -0.098090592216281240205, 0.178114665841120341155, 0.191003695796775433986, 0.0888900368967884466578, 0.0195049001251218801359, 0.00180424538297014223957];
        const Q = [ 1.0, 1.84759070983002217845, 1.42628004845511324508, 0.578052804889902404909, 0.12385097467900864233, 0.0113385233577001411017, 0.337511472483094676155e-5
        ];
        
        const zarg = z - 0.5;
        const P_eval = ((((zarg*P[5] + P[4])*zarg + P[3])*zarg + P[2])*zarg + P[1])*zarg + P[0];
        const Q_eval = (((((Q[6]*zarg + Q[5])*zarg + Q[4])*zarg + Q[3])*zarg + Q[2])*zarg + Q[1])*zarg + Q[0];
        return (0.405935764312744140625 + P_eval / Q_eval) * (Math.exp(-z * z) / z);
      }//if(z < 1.5f)
      
      if( z < 2.5 )
      {
        const P = [ -0.0243500476207698441272, 0.0386540375035707201728, 0.04394818964209516296, 0.0175679436311802092299, 0.00323962406290842133584, 0.000235839115596880717416 ];
        const Q = [ 1.0, 1.53991494948552447182, 0.982403709157920235114, 0.325732924782444448493, 0.0563921837420478160373, 0.00410369723978904575884 ];
        
        const zarg = z - 1.5;
        const P_eval = ((((zarg*P[5] + P[4])*zarg + P[3])*zarg + P[2])*zarg + P[1])*zarg + P[0];
        const Q_eval = ((((zarg*Q[5] + Q[4])*zarg + Q[3])*zarg + Q[2])*zarg + Q[1])*zarg + Q[0];
        return (0.50672817230224609375 + P_eval / Q_eval) * (Math.exp(-z * z) / z);
      }//if( z < 2.5f
      
      if( z < 4.5 )
      {
        const P = [ 0.00295276716530971662634, 0.0137384425896355332126, 0.00840807615555585383007, 0.00212825620914618649141, 0.000250269961544794627958, 0.113212406648847561139e-4 ];
        const Q = [ 1.0, 1.04217814166938418171, 0.442597659481563127003, 0.0958492726301061423444, 0.0105982906484876531489, 0.000479411269521714493907 ];
        
        const zarg = z - 3.5;
        const P_eval = ((((zarg*P[5] + P[4])*zarg + P[3])*zarg + P[2])*zarg + P[1])*zarg + P[0];
        const Q_eval = ((((zarg*Q[5] + Q[4])*zarg + Q[3])*zarg + Q[2])*zarg + Q[1])*zarg + Q[0];
        return (0.5405750274658203125 + P_eval / Q_eval) * (Math.exp(-z * z) / z);
      }//if( z < 4.5f )
      
      const P = [ 0.00628057170626964891937, 0.0175389834052493308818, -0.212652252872804219852, -0.687717681153649930619, -2.5518551727311523996, -3.22729451764143718517, -2.8175401114513378771 ];
      const Q = [ 1.0, 2.79257750980575282228, 11.0567237927800161565, 15.930646027911794143, 22.9367376522880577224, 13.5064170191802889145, 5.48409182238641741584 ];
      
      const zarg = 1.0 / z;
      const P_eval = (((((P[6]*zarg + P[5])*zarg + P[4])*zarg + P[3])*zarg + P[2])*zarg + P[1])*zarg + P[0];
      const Q_eval = (((((Q[6]*zarg + Q[5])*zarg + Q[4])*zarg + Q[3])*zarg + Q[2])*zarg + Q[1])*zarg + Q[0];
      return (0.5579090118408203125 + P_eval / Q_eval) * (Math.exp(-z * z) / z);
    };//erfc

    
    
    function photopeak_integral( peak, x0, x1 ) {
      const mean = peak.Centroid[0], sigma = peak.Width[0], amp = peak.Amplitude[0];
      const sqrt2 = 1.414213562373095;
      
      // For the skew distributions, using PDF, rather than CDF (like we do for pure Gaussian), to
      //  save computation time (if using skew, bins are narrow enough that we'll never notice)
      if( !peak.skewType || (peak.skewType === '') || (peak.skewType === 'NoSkew') )
      {
        const t0 = (x0-mean)/(sqrt2*sigma);
        const t1 = (x1-mean)/(sqrt2*sigma);
        return amp * 0.5 * (erf(t1) - erf(t0));
      }
      
      const x = 0.5*(x1+x0);
      const t = (x-mean)/sigma;
      const norm = (x1-x0) * amp * (peak.DistNorm ? peak.DistNorm : 1);
      const skew = peak.Skew0[0];
      
      if( (peak.skewType === 'Bortel') || (peak.skewType === 'ExGauss') ) //https://doi.org/10.1016/0883-2889(87)90180-8
      {
        const exp_arg = ((x - mean)/skew) + (sigma*sigma/(2*skew*skew));
        const erfc_arg = 0.7071067812*(t + (sigma/skew));
        
        if( (skew <= 0) || (exp_arg > 87.0) || (erfc_arg > 10.0) )
          return norm*(1/(sigma*2.5066282746)) * Math.exp(-0.5*t*t);
        
        return norm*(0.5/skew)*Math.exp(exp_arg) * erfc(erfc_arg);
      }else if( peak.skewType === 'CB' )  //https://en.wikipedia.org/wiki/Crystal_Ball_function
      {
        const n = peak.Skew1[0];
        if( t <= -skew )
        {
          const A = Math.pow(n/skew, n) * Math.exp(-0.5*skew*skew);
          const B = (n/skew) - skew;
          return norm*A*Math.pow( B - t, -n );
        }
            
        return norm*Math.exp(-0.5*t*t);
      }else if( peak.skewType === 'DSCB' ) //http://nrs.harvard.edu/urn-3:HUL.InstRepos:29362185, chapter 6
      {
        // 20231109: Niave implementation gives the wrong sum when using two tails... totally not
        //           sure why, but using indefinite integrals to compute areas, for the moment
        //           (at least consistent with the c++, although the error could be in there...)
        /*
        if( (t >= -skew) && (t <= peak.Skew2[0]) ) // Return gaussian value
          return norm * Math.exp(-0.5*t*t);
          
        const n = (t > 0) ? peak.Skew3[0] : peak.Skew1[0];
        const alpha = (t > 0) ? peak.Skew2[0] : peak.Skew0[0];
        const a = (t > 0) ? -t : t;
        return norm * Math.exp(-0.5*alpha*alpha) * Math.pow( (alpha/n)*((n/alpha) - alpha - a), -n );
        */
        
        const a_l = peak.Skew0[0];
        const n_l = peak.Skew1[0];
        const a_r = peak.Skew2[0];
        const n_r = peak.Skew3[0];
        
        function left_tail_indefinite(t) {
          const t_1 = 1.0 - (a_l / (n_l / (a_l + t)));
          return -Math.exp(-0.5*a_l*a_l)*(t_1 / Math.pow(t_1, n_l)) / ((a_l / n_l) - a_l);
        };
        
        function right_tail_indefinite(t){
          return Math.exp(-a_r*a_r/2)*(1 / ((a_r / n_r) - a_r)) * Math.pow((1 + ((a_r * (t - a_r)) / n_r)), (1 - n_r));
        };
        
        function gauss_indefinite( t ){
          return 1.2533141 * erf( 0.707106781186 * t );
        };
        
        const t0 = (x0 - mean) / sigma, t1 = (x1 - mean) / sigma;
        let answer = 0;
        if( t0 < -a_l )
          answer += left_tail_indefinite( Math.min(-a_l,t1) ) - left_tail_indefinite( t0 );
        if( t1 > a_r )
          answer += right_tail_indefinite( t1 ) - right_tail_indefinite( Math.max(a_r,t0) );
        if( (t0 < a_r) && (t1 > -a_l) )
          answer += gauss_indefinite( Math.min(a_r,t1) ) - gauss_indefinite( Math.max(-a_l,t0) );
        
        return amp * peak.DistNorm * answer;
      }else if( peak.skewType === 'GaussExp' ) //https://arxiv.org/abs/1603.08591
      {
        return norm * Math.exp( (t >= -skew) ? (-0.5*t*t) : (0.5*skew*skew + skew*t) );
      }else if( peak.skewType === 'ExpGaussExp' ) //https://arxiv.org/abs/1603.08591
      {
        const skew_right = peak.Skew1[0];
        if( t > skew_right )
          return norm*Math.exp( 0.5*skew_right*skew_right - skew_right*t );
        if( t > -skew )
          return norm*Math.exp( -0.5*t*t );
        return norm*Math.exp( 0.5*skew*skew + skew*t );
      }else
      {
        console.log( 'Need to implement peak skew type ' + peak.skewType );
      }
    };

    var bisector = d3.bisector(function(d){return d.x;});

    var peakamplitudes = [];  //The peak amplitudes for each bin

    var leftMostLineValue = [];

    var minypx = self.size.height, maxypx = 0;
    
    
    //Go from right to left drawing the peak lines that sit on the continuum.
    for( let xindex = xendind - 1; xindex >= xstartind; --xindex ) {
      const x0 = points[xindex].x, x1 = points[xindex+1].x;
      peakamplitudes[xindex] = [];
      peak_area = 0.0;
      //thisx = 0.5*(x0 + x1);
      thisx = ((xindex===xstartind) ? roiLB : ((xindex===(xendind-1)) ? roiUB : (0.5*(x0 + x1))));
      
      cont_area = self.offset_integral( roi, x0, x1 ) * scaleFactor;

      // Background Subtract - Subtract the current y-value with the corresponding background point
      if( useBackgroundSubtract ) 
        cont_area -= background.points[bisector.left(background.points, x0)].y;

      peakamplitudes[xindex][0] = cont_area;
      peakamplitudes[xindex][1] = thisx;

      roi.peaks.forEach( function(peak,peakn){
        const ispeakcenter = (labels && labels[peakn] && labels[peakn].xindex===xindex);
        
        if( peak.type === 'GaussianDefined' ){
          let area = photopeak_integral( peak, x0, x1 ) * scaleFactor;
          peak_area += area;

          m = peak.Centroid[0];
          s = peak.Width[0];
          const vr = peak.visRange ? peak.visRange : [(m - 5*s),(m + 5*s)];
          
          if( (roi.peaks.length === 1) || ((vr[0] <= x1) && (vr[1] >= x0)) || ((thisx >= vr[0]) && (thisx < vr[1]))  ){
            peakamplitudes[xindex][peakn+2] = area;
            let yvalpx = self.yScale(cont_area + area);
            minypx = Math.min(minypx,yvalpx);
            maxypx = Math.max(maxypx,yvalpx);
            if( ispeakcenter )
              labels[peakn].centroidMaxYPx = yvalpx;
            paths[peakn+1+roi.peaks.length] += " " + self.xScale(thisx) + "," + yvalpx;
            leftMostLineValue[peakn] = {x : thisx, y: cont_area};
          }else{
            peakamplitudes[xindex][peakn+2] = 0.0;
          }
        } else if( peak.type === 'DataDefined' ) {
          let area = points[xindex].y - cont_area;
          peakamplitudes[xindex][peakn+2] = (area > 0 ? area : 0);
          let yvalpx = self.yScale(cont_area + (area >= 0 ? area : 0.0));
          minypx = Math.min(minypx,yvalpx);
          maxypx = Math.max(maxypx,yvalpx);
          if( ispeakcenter && labels[peakn] )
            labels[peakn].centroidMaxYPx = yvalpx;
          paths[peakn+1+roi.peaks.length] += " " + self.xScale(thisx) + "," + yvalpx;
          leftMostLineValue[peakn] = {x : thisx, y: cont_area};
        } else {
          console.log( 'Need to implement peak.type==' + peak.type );
          return;
        }
      });
    }//for( go right to left over 'xindex' drawing peak outlines )

    
    //Make sure the peak line top connects with the continuum
    for( let j = 0; j < roi.peaks.length; ++j ) {
      let pathnum = j+1+roi.peaks.length;
      if( leftMostLineValue[j] && paths[pathnum] && paths[pathnum].length )
        paths[pathnum] += " " + self.xScale(leftMostLineValue[j].x) + "," + self.yScale(leftMostLineValue[j].y);
    }

    let leftMostFillValue = [];
  
    function makePathForPeak(peakamps, xindex, leftToRight){
      const cont = peakamps[0], thisx = peakamps[1];
      const x0 = points[xindex].x, x1 = points[xindex+1].x;
      
      peakamps.forEach( function( peakamp, peakindex ){
        if( peakindex < 2 )
          return;
        const peaknum = (peakindex - 2);
        const peak = roi.peaks[peaknum];
        const m = peak.Centroid[0], s = peak.Width[0];
        const vr = peak.visRange ? peak.visRange : [(m - 5*s),(m + 5*s)];
        
        if( (roi.peaks.length !== 1) && !((vr[0] <= x1) && (vr[1] >= x0)) && !((thisx >= vr[0]) && (thisx < vr[1]))  )
          return;
        
        let thisy = cont;
        for( var j = 2; j < (peakindex + (leftToRight ? 0 : 1)); ++j )
          thisy += peakamps[j];
        
        const xvalpx = self.xScale(thisx);
        const yvalpx = self.yScale(thisy);
        minypx = Math.min(minypx,yvalpx);
        maxypx = Math.max(maxypx,yvalpx);
        
        if( !paths[peaknum+1].length ){
          if( leftToRight )
            leftMostFillValue[peaknum] = { x: xvalpx, y: yvalpx };
          paths[peaknum+1] = "M" + xvalpx + "," + yvalpx + " L";
        }else{
          paths[peaknum+1] += " " + xvalpx + "," + yvalpx;
        }
      } );
    }; //makePathForPeak function
    
    
    //go from left to right, drawing fill area bottom
    peakamplitudes.forEach( function(peakamps,xindex){ makePathForPeak(peakamps,xindex,true); } );

    //console.log( 'minypx=' + minypx + ', maxypx=' + maxypx + ' height=' + self.size.height );
    
    //go right to left and draw the fill areas top
    // TODO: We should be able to use this next line to make the paths for peaks, but it ends up being a bit wonky (See commit fc790795b24d21431467c32ca189c05e2f9b0f12 for when this issue was introduced)
    //peakamplitudes.reverse().forEach( function(peakamps,xindex){ makePathForPeak(peakamps,xindex,false); } );
    // But if we use this next loop instead, things are fine:
    peakamplitudes.reverse().forEach( function(peakamps,xindex){
      var cont = peakamps[0];
      var thisx = peakamps[1];
          
      peakamps.forEach( function( peakamp, peakindex ){
        if( peakindex < 2 )
          return;

        const peaknum = (peakindex - 2);
        const peak = roi.peaks[peaknum];
        const m = peak.Centroid[0];
        const s = peak.Width[0];
        const vr = peak.visRange ? peak.visRange : [(m - 5*s),(m + 5*s)];
          
        if( (roi.peaks.length > 1) && ((thisx < vr[0]) || (thisx > vr[1])) )
          return;
          
        var thisy = cont;
        for( var j = 2; j <= peakindex; ++j )
          thisy += peakamps[j];

        paths[peaknum+1] += " " + self.xScale(thisx) + "," + self.yScale(thisy);
      } );
    });

    for( var peaknum = 0; peaknum < roi.peaks.length; ++peaknum ){
      if( leftMostFillValue[peaknum] && paths[peaknum+1].length )
        paths[peaknum+1] += " " + leftMostFillValue[peaknum].x + "," + leftMostFillValue[peaknum].y;
    }
    
    return {paths: paths, yRangePx: [minypx,maxypx], labelinfo: labels };
  }/*function roiPath(roi) */

  function draw_roi(roi,specindex,spectrum) {
    if( roi.type !== 'NoOffset' && roi.type !== 'Constant'
        && roi.type !== 'Linear' && roi.type !== 'Quadratic'
        && roi.type !== 'Quardratic' //vestigual, can be deleted in the future.
        && roi.type !== 'Cubic' && roi.type !== 'FlatStep'
        && roi.type !== 'LinearStep' && roi.type !== 'BiLinearStep'
        && roi.type !== 'External' ){
      console.log( 'unrecognized roi.type: ' + roi.type );
      return;
    }

    if( roi.lowerEnergy > maxx || roi.upperEnergy < minx )
      return;

    if (!spectrum) {
      console.log("No spectrum specified to draw peaks");
      return;
    }
    
    if( self.yScale.domain()[0] === self.yScale.domain()[1] ){
      console.log( 'Y-domain not valid; not drawing ROI' );
      return;
    }

    let scaleFactor = spectrum.type !== self.spectrumTypes.FOREGROUND ? spectrum.yScaleFactor * 1.0 : 1.0;
    if (typeof scaleFactor === 'undefined' || scaleFactor === null) scaleFactor = 1.0;

    var pathsAndRange = roiPath( roi, spectrum.points, scaleFactor, self.getSpectrumByID(spectrum.backgroundID) );

    if( pathsAndRange.labelinfo )
      Array.prototype.push.apply(labelinfo,pathsAndRange.labelinfo);
    
    /* Draw label, set fill colors */
    pathsAndRange.paths.forEach( function(p,num){

      /* - The first path will be an underline of entire ROI
         - The next roi.peaks.length entries are the fills for each of the peaks
         - The next roi.peaks.length entries are the path of the peak, that sits on the ROI
       
       If a peak in the ROI is not visible (ROI partially off screen), then path will be empty.
      */
      if( (num === 0) || !p.length )
        return;
        
      console.assert( p.startsWith("M"), "Got path not starting with 'M': " + p );
      if( p.endsWith("L") || !p.startsWith("M") ) //Protect against single channel paths we didnt complete above; TODO: fix this
        return;

      //If only a single peak in a ROI, we will use the same path for outline and fill
      if( roi.peaks.length==1 && num > roi.peaks.length )
        return;
      
      let isOutline = ((num > (roi.peaks.length)) || (roi.peaks.length==1));
      let isFill  = (num <= (roi.peaks.length));

      var path = self.peakVis.append("path").attr("d", p );

      function onRightClickOnPeak() {
        console.log("Should (but not hooked up) Emit RIGHT CLICK (ON PEAK) signal. (Peak roi = ", roi, ")");
      }

      var peakind = (num-1) % roi.peaks.length;
      var peak = roi.peaks[peakind];
      var peakColor = peak && peak.lineColor && peak.lineColor.length ? peak.lineColor : spectrum.peakColor;

      let info = {
        path: path,
        paths: pathsAndRange.paths,
        roi: roi,
        lowerEnergy: roi.lowerEnergy,
        upperEnergy: roi.upperEnergy,
        yRangePx: pathsAndRange.yRangePx,
        xRangePx: [self.xScale(roi.lowerEnergy), self.xScale(roi.upperEnergy)],
        color: peakColor,
        isOutline: isOutline,
        isFill: isFill,
        peak: peak
      };
      
      self.peakPaths.push( info );

      path/* .attr("class", "peak") */
          /* .attr("class", "spectrum-peak-" + specindex) */
          .attr("stroke-width", 1)
          .attr("stroke", peakColor )
          ;
            
      path.attr("fill-opacity", ((isOutline && !isFill) ? 0.0 : 0.6) )
          .attr("data-energy", ((peak && peak.Centroid) ? peak.Centroid[0].toFixed(2) : 0) );

      if( isFill ){
        path.style("fill", peakColor )
            .attr("class", "peakFill" )
      } else if( isOutline ) {
        path.attr("class", "peakOutline" )
      }
      
      if( isOutline ){
        path.on("mouseover", function(){ self.handleMouseOverPeak(this); } )
            .on("mousemove", self.handleMouseMovePeak())
            .on("mouseout", function(d, peak) { self.handleMouseOutPeak(this, peak, pathsAndRange.paths); } )
             ;
      }
    
      /* For right-clicking on a peak */
      path.on("contextmenu", onRightClickOnPeak);
    });//pathsAndRange.paths.forEach( function(p,num){

    const p0 = (pathsAndRange.paths.length && roi.peaks.length) ? pathsAndRange.paths[0] : null;
    console.assert( !p0 || p0.startsWith("M"), "Got p0 path not starting with 'M': " + p0 );
    
    if( p0 && !p0.endsWith("L") && p0.startsWith("M") ){ //protect against single channel paths we didnt complete above; TODO: fix this
      //Draw the continuum line for multiple peak ROIs
      const path = self.peakVis.append("path").attr("d", pathsAndRange.paths[0] );
      path.attr("stroke-width",1)
          .attr("fill-opacity",0)
          .attr("stroke", spectrum.peakColor );
    }
  };//function draw_roi(roi,specindex,spectrum)

  for (let i = 0; i < this.rawData.spectra.length; i++) {
    const spectrum = this.rawData.spectra[i];
    
    if ((spectrum.type === self.spectrumTypes.FOREGROUND && !this.options.drawPeaksFor.FOREGROUND))
      continue;
    if ((spectrum.type === self.spectrumTypes.BACKGROUND && (!this.options.drawPeaksFor.BACKGROUND || this.options.backgroundSubtract)))
      continue;
    if ((spectrum.type === self.spectrumTypes.SECONDARY && !this.options.drawPeaksFor.SECONDARY))
      continue;

    const peaks = spectrum.peaks;
    if( peaks ){
      peaks.forEach( function(roi){
        //We test for self.roiBeingDrugUpdate below, even if self.roiIsBeingDragged is true, to make sure the server has
        //  actually returned a updated ROI, and if it hasnt, we'll draw the original ROI.
        //  This prevents to ROI disapearing after the user clicks down, but before they have moved the mouse.
        if( self.roiIsBeingDragged && self.roiBeingDrugUpdate && roi == self.roiBeingDragged.roi )
          draw_roi(self.roiBeingDrugUpdate,i,spectrum);
        else
          draw_roi(roi,i,spectrum);
      });
    }//if( this.rawData.spectra[i].peaks )
    
    if( self.fittingPeak && self.roiBeingDrugUpdate && (this.rawData.spectra[i].type === self.spectrumTypes.FOREGROUND) )
      draw_roi(self.roiBeingDrugUpdate,i,spectrum);
  }
  
  self.drawPeakLabels( labelinfo );
}


SpectrumChartD3.prototype.drawPeakLabels = function( labelinfos ) {
  const self = this;
  
  if( !labelinfos || !labelinfos.length )
    return;
  
  if( !self.options.showUserLabels && !self.options.showPeakLabels && !self.options.showNuclideNames )
    return;
  
  const chart = self.peakVis;
  const chartBox = self.plot.node().getBBox();    /* box coordinates for the chart area */
  
  // Don't draw peak label if chart isn't set up yet
  if( !chartBox.width || !chartBox.height )
    return;
  
  // We adjust label positions only after being idle for a little bit, using a timeout at the bottom of this function
  window.clearTimeout( self.adjustLabelTimeout );
  self.adjustLabelTimeout = null;
  
  /*
   labelinfos is an array of objects that look like:
   {
      centroidMaxYPx: 33.6
      centroidMinYPx: 43.0
      centroidXPx: 111.4
      energy: 244.162
      peak: {type: "GaussianDefined", skewType: "NoSkew", Centroid: Array(3), Width: Array(3), Amplitude: Array(3), ...}
      roi: {type: "Linear", lowerEnergy: 220.469, upperEnergy: 271.636, referenceEnergy: 220.469, coeffs: Array(2),...}
      roiPeakIndex: 0
      userLabel: undefined
   }
   */
  
  const fontSize = self.options.peakLabelSize;
  const rotationAngle = self.options.peakLabelRotation;
  
  // The labels we added, as well as their associated data, including position
  let label_array = [];
  
  // Some transformation we will use to help position labels
  const visCtm = self.peakVis.node().getCTM(); // Get x-form for self.peakVis, which we need coordinates relative to
 
  // Rotate `label` by X degrees, but keep bottom left of text at `peak_x` and `peak_uy`
  function setLabelTransform(label,roiInfo,dx,dy){
    const peak_x = roiInfo.centroidXPx;
    const peak_uy = roiInfo.centroidMaxYPx;
    const lbb = label.node().getBBox();
    
    // We will rotate text at bottom-left corner, but we want to niavely keep the label centered at the peak, in x
    const wx = Math.max(lbb.width,10) * Math.cos( -rotationAngle*(Math.PI/180) );
    const hx = Math.max(lbb.height,8) * Math.sin( -rotationAngle*(Math.PI/180) );
    const x_center = 0.5*(wx - hx);
    const xform = "translate(" + (peak_x - x_center + dx) + " " + (peak_uy + dy) + ") rotate(" + rotationAngle + " 0 0)";
    label.attr("transform", xform );
  };
  
  
  for( let index = 0; index < labelinfos.length; ++index ){
    let info = labelinfos[index];
    if( !info || typeof(info)==='undefined' || !info.peak )
      continue;
    
    let nuclide = info.peak.nuclide;
    if( !nuclide )
      nuclide = info.peak.xray
    if( !nuclide )
      nuclide = info.peak.reaction
    
    const peak_x = info.centroidXPx;
    const peak_ly = info.centroidMinYPx;
    const peak_uy = info.centroidMaxYPx;
    
    //This next check doesnt appear to be necessary anymore.
    if( peak_uy===null || Number.isNaN(peak_uy) || typeof(peak_uy)==='undefined' ){
      console.log( 'Got peak_uy=' + peak_uy + ', for energy ' + info.energy, info );
      continue;
    }
    
    let labelRows = [];
    if( self.options.showUserLabels && info.userLabel && info.userLabel.length )
      labelRows.push( info.userLabel );
    
    if( self.options.showPeakLabels )
      labelRows.push( info.energy.toFixed(2) + " keV" );
    
    if( self.options.showNuclideNames && nuclide ) {
      let txt = nuclide.name;
      if( self.options.showNuclideEnergies ) // Nuclide energy label displayed only if nuclide name labels are displayed!
      txt += ", " + nuclide.energy.toFixed(2).toString() + " keV" + (nuclide.type ? " " + nuclide.type : "");
      labelRows.push( txt );
    }//if( show nuclide name and we have a nuclide )
    
    //If we wont draw any text, skip this peaks
    if( !labelRows.length )
      continue;
    
    // Create label <text> element
    const label = chart.append("text")
      .attr("class", 'peaklabel')
      .attr("text-anchor", "start")
      .attr("y", 0)
      .attr("x", 0)
      .attr("data-peak-energy", info.energy.toFixed(2) )  //can access as label.dataset.peakEnergy
      .attr("data-peak-x-px", peak_x.toFixed(1) )         //can access as label.dataset.peakXPx
      .attr("data-peak-lower-y-px", peak_ly.toFixed(1) )  //can access as label.dataset.peakLowerYPx
      .attr("data-peak-upper-y-px", peak_uy.toFixed(1) )  //can access as label.dataset.peakUpperYPx
      ;
    
    for( let i = 0; i < labelRows.length; ++i ){
      label.append("tspan")
        .attr("dy", (i===0) ? ("-" + (labelRows.length-1) + ".2em") : "1em" ) //The extra 0.2em is to vertically center the text
        .attr("x", 0)
        .attr("font-size", fontSize)
        .attr("alignment-baseline","baseline")
        .text(labelRows[i]);
    }
    
    d3.select(label).data( info );
    
    // Add handlers to make text bold when you mouse over the label.
    label.on("mouseover", function(){ self.highlightLabel(this,false); } )
      .on("mouseout",  function(){ self.unHighlightLabel(true); } );
    
    if( self.isTouchDevice() )
      label.on("touchstart", function(){ self.highlightLabel(this,false); } );
    
    //Reposition label niavely.
    setLabelTransform( label, info, 0, -10 );
    
    // Calculate baseline position of label
    const svgCtm = label.node().getCTM();  // Get the transformation matrix (CTM) of the text element
    const labelBBox = label.node().getBBox();
      
    const fromTxtToVisCoords = function(x,y){
      const pt = self.svg.node().createSVGPoint(); //TODO: createSVGPoint() is depreciated
      pt.x = x;
      pt.y = y;
      const svgPoint = pt.matrixTransform(svgCtm); // Apply the transformation matrix to the point
      return svgPoint.matrixTransform( visCtm.inverse() ); // Get coordinates relative to self.peakVis
    };
    
    let data = {};
    data.initialPos = [ fromTxtToVisCoords( 0, 0 ), //bottom-left of text
      fromTxtToVisCoords( labelBBox.width, 0 ), //bottom right of text
      fromTxtToVisCoords( labelBBox.width, -labelBBox.height ), //top-right of text
      fromTxtToVisCoords( 0, -labelBBox.height ) //top-left of text
    ];
    
    data.offset = [0, -10];
    data.labelInfo = info;
    data.label = label;
    data.nominalX = peak_x;
    data.nominalY = peak_uy - 10;
    
    label_array.push( data );
  }//labelinfos.foreach(...)
  
    
  const chartw = chartBox.width;
  
  /* Uses simulated annealing to determine label placement (I couldnt get force layout to work).
  Idea taken from https://github.com/tinker10/D3-Labeler, however our needs are unique enough we couldnt just use it
   */
  function adjustLabels(){

    // Define a weighting function to define how we want the labels to be oriented
    function weightFcn( index ) {
      const label = label_array[index];
      const dx = label.offset[0], dy = label.offset[1] + 10;
        
      // penalty for moving away from default position - its okay to move in x, but dont go closer to peak in y
      let penalty = 0.5 * Math.abs(dx) * Math.sqrt(Math.abs(dx));
      penalty += ((dy > 10) ? 20 : ((dy > 0) ? 4 : -1)) * dy;
      
      // To calculate the overlap between the rectangles, we will rotate them (around origin for
      //  simplicity) so they are parallel with x and y-axis, to make the math easier
      function rotatePoint(p) { //rotates around origin
        const t = -rotationAngle * Math.PI / 180;
        return { x: (p.x*Math.cos(t) - p.y*Math.sin(t)), y: (p.x*Math.sin(t) + p.y*Math.cos(t)) };
      };
        
      const ll1 = rotatePoint( { x: label.initialPos[3].x + dx, y: label.initialPos[3].y + dy } ); //lower-left
      const ur1 = rotatePoint( { x: label.initialPos[1].x + dx, y: label.initialPos[1].y + dy } ); //upper-right
        
      for( let i = 0; i < label_array.length; ++i ){
        if( i === index )
          continue;
        
        function overlappingArea(l1, r1, l2, r2){
          const x_dist = (Math.min(r1.x, r2.x) - Math.max(l1.x, l2.x));
          const y_dist = (Math.min(r1.y, r2.y) - Math.max(l1.y, l2.y));
          return (x_dist <= 0 || y_dist <= 0) ? 0 : x_dist * y_dist;
        };
            
        const other = label_array[i];
        let ll2 = rotatePoint({ x: other.initialPos[3].x + other.offset[0], y: other.initialPos[3].y + other.offset[1] });
        let ur2 = rotatePoint({ x: other.initialPos[1].x + other.offset[0], y: other.initialPos[1].y + other.offset[1] });
            
        const overlap_area = overlappingArea(ll1, ur1, ll2, ur2);
        penalty += (5.0 * overlap_area);
            
        //console.log( "overlap_area:" + overlap_area + ", ", [ll1, ur1], ", ", [ll2, ur2]);
        let margin_overlap = overlap_area;
        for( let y = 0; y < 2; ++y ){
          ll2.x -= 3; ll2.y -= 3;
          ur2.x += 3; ur2.y += 3;
          
          const this_overlap = overlappingArea(ll1, ur1, ll2, ur2);
          console.assert( this_overlap >= margin_overlap );
          
          // Now add penalty if the nearest label is pretty close
          penalty += 0.5*(2 - y)*(this_overlap - margin_overlap);
          margin_overlap = this_overlap;
        }
      }
        
      return penalty;
    };//function weightFcn
      
    // Randomly repositions a label, and keeps if it is better, according to current temperature
    function tryNewMonteCarloPosition( currentTemperature ){
      const i = Math.floor(Math.random() * label_array.length);
      let label = label_array[i];
        
      const prev_x = label.offset[0], prev_y = label.offset[1];
        
      // Randomly move the label around
      let new_dx = prev_x + (Math.random() - 0.5) * 15.0;
      let new_dy = prev_y + (Math.random() - 0.5) * 7.5;
        
      // Make sure we arent going off the chart, or into the peak
      for( let posIndex = 0; posIndex < label.initialPos.length; ++posIndex ){
        const ip = label.initialPos[posIndex];
        if( (ip.x + new_dx) > chartw )
          new_dx = prev_x;
        if( (ip.x + new_dx) < 0 )
          new_dx = prev_x;
            
        // TODO: instead of having label use `centroidMaxYPx` as its floor, calculate the intersection area with the ROI outline path
        //       see https://gist.github.com/mbostock/8027637 or something like https://github.com/d3/d3-polygon to detect if the label is overlapping with any ROIs
        if( (ip.y + new_dy) < label.labelInfo.centroidMaxYPx )
          new_dy = prev_y;
      }
        
      // If this translation causes label to be _more_ above the chart, reject it
      const initialTop = Math.min(label.initialPos[0].y, label.initialPos[1].y, label.initialPos[2].y, label.initialPos[3].y);
      if( ((initialTop + new_dy) < 0) && (new_dy < prev_y) )
        new_dy = prev_y;
        
      // Dont swap x-positions - keep labels in same order
      let thisx = label.initialPos[0].x + new_dx;
      if( i > 0 ){
        const prevx = label_array[i-1].initialPos[0].x + label_array[i-1].offset[0];
        if( prevx > thisx )
          new_dx = prev_x;
      }
        
      if( i+1 < label_array.length ){
        const nextx = label_array[i+1].initialPos[0].x + label_array[i+1].offset[0];
        if( thisx > nextx )
          new_dx = prev_x;
      }
        
      if( (new_dx === prev_x) && (new_dy === prev_y) )
        return false;
        
      const orig_weight = weightFcn(i);
      label.offset[0] = new_dx;
      label.offset[1] = new_dy;
      const new_weight = weightFcn(i);
      const delta_weight = new_weight - orig_weight;
        
      const threshold = Math.random();
      const prob = Math.exp( -delta_weight / currentTemperature );
        
      //console.log( "threshold: " + threshold + ", Math.exp(-" + delta_weight + "/" +currentTemperature + ")=" + Math.exp(-delta_weight/currentTemperature) );
      if( threshold >= prob ){
          // move back to old coordinates
        label.offset[0] = prev_x;
        label.offset[1] = prev_y;
        return false;
      }
        
      return true; //we kept solution
    };//function tryNewMonteCarloPosition
   
    function monteCarloFindLabelPos( numIterations ){
      let currentTemperature = 1.0;
      for( let i = 0; i < numIterations; ++i ){
        for( let j = 0; j < label_array.length; ++j ){
          tryNewMonteCarloPosition( currentTemperature );
        }
        currentTemperature = currentTemperature - (1.0 / numIterations); // linear cooling
      }
    };//function mcFindLabelPos
  
  
    monteCarloFindLabelPos( 1000 );
  
    for( let i = 0; i < label_array.length; ++i ){
      setLabelTransform( label_array[i].label, label_array[i].labelInfo, label_array[i].offset[0], label_array[i].offset[1] );
    }
  }; //adjustLabels function
  
  // Setting the label positions is really expensive, only do if we havent updated labels for 50ms (arbitrarily chosen time)
  self.adjustLabelTimeout = window.setTimeout( adjustLabels, 50 );
}//SpectrumChartD3.prototype.drawPeakLabels = ...


/* Sets whether or not peaks are highlighted */
SpectrumChartD3.prototype.setShowPeaks = function(spectrum,show) {
  this.options.drawPeaksFor[spectrum] = show;
  this.redraw()();
}

SpectrumChartD3.prototype.setShowUserLabels = function(d) {
  this.options.showUserLabels = d;
  this.redraw()();
}

SpectrumChartD3.prototype.setShowPeakLabels = function(d) {
  //console.log('calling show peak labels = ', d);
  this.options.showPeakLabels = d;
  this.redraw()();
}

SpectrumChartD3.prototype.setShowNuclideNames = function(d) {
  this.options.showNuclideNames = d;
  this.redraw()();
}

SpectrumChartD3.prototype.setShowNuclideEnergies = function(d) {
  this.options.showNuclideEnergies = d;
  this.redraw()();
}


SpectrumChartD3.prototype.handleMouseMovePeakFit = function() {
/* ToDo:
     - implement if you hit the 1,2,3,4,... key while doing this, then it will force that many peaks to be fit for.
     - implement choosing different order polynomials while fitting, maybe l,c,q?
     - implement returning zero amplitude peak when fit fails in c++
     - cleanup naming of the temporary ROI and such to be consistent with handleRoiDrag and updateRoiBeingDragged
     - could maybe generalize the debounce mechanism
     - get this code working with touches (and in fact get touch code to just call this function)
     - remove/cleanup a number of functions like: handleTouchMovePeakFit, handleCancelTouchPeakFit
 */
  const self = this;
  
  /* If no spectra - bail */
  if( !self.rawData || !self.rawData.spectra || !self.rawData.spectra.length
      || self.rawData.spectra[0].y.length == 0 || this.rawData.spectra[0].y.length < 10 ) {
    return;
  }
  
  d3.select('body').style("cursor", "ew-resize");
  
  self.peakFitMouseMove = d3.mouse(self.vis[0][0]);
  self.peakFitTouchMove = d3.touches(self.vis[0][0]);
  
  let leftpospx, rightpospx;
  if( self.peakFitTouchMove.length > 0 ) {
    let startTouchs = self.createPeaksStartTouches;
    if( !startTouchs )
      startTouchs = self.peakFitTouchMove
    const nowTouchs = self.peakFitTouchMove;
    leftpospx = (startTouchs[0][0] < startTouchs[1][0]) ? startTouchs[0][0] : startTouchs[1][0];
    rightpospx = (nowTouchs[0][0] > nowTouchs[1][0]) ? nowTouchs[0][0] : nowTouchs[1][0];
  } else {
    if( !self.leftMouseDown || !self.peakFitMouseMove ) {
      console.log( 'Hit condition I didnt think would happen fitting roi as dragging.' );
      return;
    }
    
    leftpospx = self.leftMouseDown[0];  //self.peakFitMouseDown[0]  //I think we can eliminate self.peakFitMouseDown now
    rightpospx = self.peakFitMouseMove[0];
    if( rightpospx < leftpospx )
      leftpospx = [rightpospx, rightpospx=leftpospx][0];
  }

  const lowerEnergy = self.xScale.invert(leftpospx);
  const upperEnergy = self.xScale.invert(rightpospx);
  
  self.zooming_plot = false;

  if( typeof self.zoominx0 !== 'number' )
    self.zoominx0 = self.xScale.invert(leftpospx);
  
  /* Set the original X-domain if it does not exist */
  if( !self.origdomain )
    self.origdomain = self.xScale.domain();
  
  /*This next line is a hack to get around how D3 is managing the zoom, but we highkacked it for the peak fitting  */
  self.xScale.domain( self.origdomain );
  
  let pageX = d3.event.pageX; //((d3.event && d3.event.pageX) ? d3.event.pageX : window.pageXOffset + leftpospx + ;
  let pageY = d3.event.pageY;
  
  //Emit current position, no more often than every 2.5 seconds, or if there
  //  are no requests pending.
  let emitFcn = function(){
    self.roiDragRequestTime = new Date();
    window.clearTimeout(self.roiDragRequestTimeout);
    self.roiDragRequestTimeout = null;
    self.roiDragRequestTimeoutFcn = null;
    
    self.WtEmit(self.chart.id, {name: 'fitRoiDrag'},
                lowerEnergy, upperEnergy, -1 /*self.forcedFitRoiNumPeaks*/, false, pageX, pageY );
  };
  
  let timenow = new Date();
  if( self.roiDragRequestTime === null || (timenow-self.roiDragRequestTime) > 2500 ){
    emitFcn();
  } else {
    let dt = Math.min( 2500, Math.max(0, 2500 - (timenow - self.roiDragRequestTime)) );
    window.clearTimeout( self.roiDragRequestTimeout );
    self.roiDragRequestTimeoutFcn = emitFcn;
    self.roiDragRequestTimeout = window.setTimeout( function(){
      if(self.roiDragRequestTimeoutFcn)
      self.roiDragRequestTimeoutFcn();
    }, dt );
  }
}//handleMouseMovePeakFit



SpectrumChartD3.prototype.handleTouchMovePeakFit = function() {
  var self = this;

  if (!self.rawData || !self.rawData.spectra)
    return;
 
  /* Clear the delete peaks mode */
  self.handleCancelTouchDeletePeak();

  /* Clear the count gammas mode */
  self.handleCancelTouchCountGammas();

  /* Cancel the zoom-in y mode */
  self.handleTouchCancelZoomY();


  var t = d3.touches(self.vis[0][0]);

  /* Cancel the function if no two-finger swipes detected */
  if (!t || t.length !== 2 || !self.createPeaksStartTouches) {
    self.handleCancelTouchPeakFit();
    return;
  }

  /* Set the touch variables */
  var leftStartTouch = self.createPeaksStartTouches[0][0] < self.createPeaksStartTouches[1][0] ? self.createPeaksStartTouches[0] : self.createPeaksStartTouches[1];

  let leftTouch = t[0][0] < t[1][0] ? t[0] : t[1],
      rightTouch = leftTouch === t[0] ? t[1] : t[0];

  rightTouch[0] = Math.min(rightTouch[0], self.xScale.range()[1]);

  self.fittingPeak = true;
  
  // Incase there was some jitter and a little zooming happened before we got into fit peak mode, restore to original x-domain
  if( self.origdomain ){
    const currX = self.xScale.domain(), prevX = self.origdomain;
    if( (currX[0] != prevX[0]) || (currX[1] != prevX[1]) ){
      self.xScale.domain( prevX );
      self.redraw()();
    }
  }

  self.handleMouseMovePeakFit();

  /* To keep track of some of the line objects being drawn */
  let createPeakTouchCurrentLine = self.vis.select("#createPeakTouchCurrentLine"),
      createPeakTouchText = self.vis.select("#createPeakTouchText");

  /* Create the leftmost starting point line  */
  if (self.vis.select("#createPeakTouchStartLine").empty()) {
    self.vis.append("line")
      .attr("id", "createPeakTouchStartLine")
      .attr("class", "createPeakMouseLine")
      .attr("x1", leftStartTouch[0])
      .attr("x2", leftStartTouch[0])
      .attr("y1", 0)
      .attr("y2", self.size.height);
  }

  /* Create/refer the rightmost current point line */
  if (createPeakTouchCurrentLine.empty()) {
    createPeakTouchCurrentLine = self.vis.append("line")
      .attr("id", "createPeakTouchCurrentLine")
      .attr("class", "createPeakMouseLine")
      .attr("y1", 0)
      .attr("y2", self.size.height);
  }

  createPeakTouchCurrentLine
    .attr("x1", rightTouch[0])
    .attr("x2", rightTouch[0]);

  /* Create the text for the touch level */
  if (createPeakTouchText.empty())
    createPeakTouchText = self.vis.append("text")
      .attr("id", "createPeakTouchText")
      .attr("class", "createPeakTouchText")
      .attr("y", 10)
      .text( self.options.txt.touchDefineRoi );
  createPeakTouchText
      .attr("x", 0.5*(leftStartTouch[0] + rightTouch[0] - createPeakTouchText.node().getBoundingClientRect().width ) );
}


SpectrumChartD3.prototype.handleCancelTouchPeakFit = function() {
  const self = this;
  
  const touchStartLines = self.vis.selectAll("#createPeakTouchStartLine");
  if( !self.fittingPeak && (touchStartLines.length === 0) )
    return;
  
  self.handleMouseUpPeakFit();

  /* Delete the leftmost start line */
  touchStartLines.remove();
  
  /* Delete the right most current mouse line */
  self.vis.selectAll("#createPeakTouchCurrentLine").remove();


  /* Delete the arrows pointing to the mouse lines */
  d3.selectAll(".createPeakArrow").each(function () {
    d3.select(this).remove();
  });

  /* Delete the reference text for the create peak */
  self.vis.selectAll(".createPeakTouchText").each(function () {
    d3.select(this).remove();
  });

  self.fittingPeak = null;
}

/*Function called when use lets the mouse button up */
SpectrumChartD3.prototype.handleMouseUpPeakFit = function() {
  var self = this;

  const roi = self.roiBeingDrugUpdate;
  if( !self.fittingPeak || !self.rawData || !self.rawData.spectra || !roi )
    return;

  self.fittingPeak = null;
  
  self.redraw()();
  self.handleCancelRoiDrag();
  
  if( self.leftMouseDown ) {
    const pageX = d3.event.pageX;
    const pageY = d3.event.pageY;
    //const x0 = self.leftMouseDown[0],
    //      x1 = d3.mouse(self.vis.node())[0];
    //self.WtEmit(self.chart.id, {name: 'fitRoiDrag'}, x0, x1, -1, true );
    //Instead of updating with any movements the mouse may have made, leaving the final fit result
    //  different than whats currently showing; should re-evaluate after using for a while
    self.WtEmit( self.chart.id, {name: 'fitRoiDrag'},
                 roi.lowerEnergy, roi.upperEnergy, roi.peaks.length, true, pageX, pageY );
  }else if( self.peakFitTouchMove ) {
    const pageX = self.peakFitTouchMove[0][0];
    const pageY = self.peakFitTouchMove[0][1];
    
    self.WtEmit( self.chart.id, {name: 'fitRoiDrag'},
                 roi.lowerEnergy, roi.upperEnergy, roi.peaks.length, true, pageX, pageY );
  }
}

/*Function called when you hit escape while fitting peak */
SpectrumChartD3.prototype.handleCancelMousePeakFit = function() {
  this.handleCancelRoiDrag();
}


/** Updates Escape/Sum/Compton feature markers.
 
 If this function is called with no arguments, then feature lines will be updated with current
 mouse/touch position - unless there are none of those on the chart, in which case all lines/txt
 will be removed.
 
 @arg mouseDownEnergy Only used for "Sum Peak"; gives the original energy the user clicked (if mouse, or previous tap energy if touch)
 @arg overridePositionPx a two element array of mouse/touch px position; if provided, will be used to overide current mouse/touch position - currently only used for touches, if the user tapeed the screen, and they had previously tapped the screen.
 */
SpectrumChartD3.prototype.updateFeatureMarkers = function( mouseDownEnergy, overridePositionPx ) {
  const self = this;
  
  /* Positional variables (for mouse and touch) */
  let m;
  try{ m = d3.mouse(self.vis[0][0]) }catch(e){}

  try{
    const t = d3.touches(self.vis[0][0]);
    if( t && t.length > 0 )
      m = t[0];
  }catch (e) {
  }

  if( Array.isArray(overridePositionPx) )
    m = overridePositionPx;
  
  /** We'll first define a bunch of functions to remove all lines/txt we may put onto chart; this is
   to allow cleanup when this function is called with no arguments, and mouse/touches on chart. */
  
  
  function removeMouseEdgeFromChart(){
    if ( self.mouseEdge ) {
      self.mouseEdge.remove();
      self.mouseEdge = null;
    }
    if ( self.mouseEdgeText ) {
      self.mouseEdgeText.remove();
      self.mouseEdgeText = null;
    }
  };
  
  /* Consolidated function to delete escape peak markers - replaces 4 duplicate functions */
  function deleteEscapePeakMarker(peakType) {
    var elementSuffix = '';
    switch(peakType) {
      case 'single':
        elementSuffix = 'singleEscape';
        break;
      case 'double':
        elementSuffix = 'doubleEscape';
        break;
      case 'singleForward':
        elementSuffix = 'singleEscapeForward';
        break;
      case 'doubleForward':
        elementSuffix = 'doubleEscapeForward';
        break;
      default:
        return; // Invalid peakType
    }
    
    self.deleteAndNullifyElements([
      { element: elementSuffix },
      { element: elementSuffix + 'Text' },
      { element: elementSuffix + 'Meas' }
    ]);
  }
  
  
  function deleteComptonPeakLine(){
    self.deleteAndNullifyElements([
      { element: 'comptonPeak' },
      { element: 'comptonPeakText' },
      { element: 'comptonPeakMeas' }
    ]);
  };//function deleteComptonPeakLine()
  
  function deleteClickedSumPeakMarker() {
    self.deleteAndNullifyElements([
      { element: 'clickedSumPeak' },
      { element: 'clickedSumPeakMeas' }
    ]);
  }

  function deleteSumPeakMarker() {
    self.deleteAndNullifyElements([
      { element: 'sumPeak' },
      { element: 'sumPeakMeas' },
      { element: 'sumPeakText' }
    ]);
  }

  function deleteLeftSumPeakMarker() {
    self.deleteAndNullifyElements([
      { element: 'leftSumPeak' },
      { element: 'leftSumPeakMeas' },
      { element: 'leftSumPeakText' }
    ]);
  }
  
  if( !m )
  {
    // No arguments to this function, and
    removeMouseEdgeFromChart();
    deleteEscapePeakMarker('single');
    deleteEscapePeakMarker('double');
    deleteEscapePeakMarker('singleForward');
    deleteEscapePeakMarker('doubleForward');
    deleteComptonPeakLine();
    deleteClickedSumPeakMarker();
    deleteSumPeakMarker();
    deleteLeftSumPeakMarker();
    
    return;
  }//if( !m )
  
  /* Adjust the mouse position accordingly to touch (because some of these functions use mouse position in touch devices) */
  
  /* Chart coordinate values */
  const energy = self.xScale.invert(m[0]),
      xmax = self.size.width,
      ymax = self.size.height;
      
  /* Do not update feature markers if legend being dragged or energy value is undefined */
  if( self.legdown || isNaN(energy) || self.currentlyAdjustingSpectrumScale )
    return;

  const cursorIsOutOfBounds = (m[0] < 0  || m[0] > xmax || m[1] < 0 || m[1] > ymax);

  //Spacing between lines of text
  let linehspace = 13;

  /* Mouse edge should be deleted if: 
      none of the scatter/escape peak options are unchecked 
      OR if cursor is out of bounds 
      OR if the user is currently dragging in the graph
  */
  function shouldDeleteMouseEdge() {
    return (!self.options.showComptonEdge && !self.options.showComptonPeaks && !self.options.showEscapePeaks && !self.options.showSumPeaks) || 
    cursorIsOutOfBounds || 
    self.dragging_plot;
  }
  function deleteMouseEdge( shouldDelete ) {
    if ( shouldDelete || shouldDeleteMouseEdge() )
    {
      removeMouseEdgeFromChart();
      return true;
    }
    return false;
  }
  
  function updateMouseEdge() {
    /* If mouse edge could has been deleted, do not update the mouse edge */
    if (deleteMouseEdge())
      return;
console.log( "updateMouseEdge" );
    /* Update the mouse edge and corresponding text position  */
    if ( self.mouseEdge ) {
        self.mouseEdge
          .attr("x1", m[0])
          .attr("x2", m[0])
          .attr("y2", self.size.height);
        self.mouseEdgeText
          .attr( "y", self.size.height/4)
          .attr( "x", m[0] + xmax/125 )
          .text( energy.toFixed(1) + " keV");
    } else {  
        /* Create the mouse edge (and text next to it) */
        self.mouseEdge = self.vis.append("line")
          .attr("class", "mouseLine")
          .attr("stroke-width", 2)
          .attr("x1", m[0])
          .attr("x2", m[0])
          .attr("y1", 0)
          .attr("y2", self.size.height);

        self.mouseEdgeText = self.vis.append("text")
          .attr("class", "mouseLineText")
          .attr( "x", m[0] + xmax/125 )
          .attr( "y", self.size.height/4)
          .text( energy.toFixed(1) + " keV");
    }
  }


  function updateEscapePeaks() {
    /* Calculations for the escape peak markers */
    const singleEscapeEnergy = energy - 510.99891,
        singleEscapePix = self.xScale(singleEscapeEnergy),

        singleEscapeForwardEnergy = energy + 510.99891,
        singleEscapeForwardPix = self.xScale(singleEscapeForwardEnergy),

        doubleEscapeEnergy = energy - 1021.99782,
        doubleEscapePix = self.xScale(doubleEscapeEnergy),

        doubleEscapeForwardEnergy = energy + 1021.99782,
        doubleEscapeForwardPix = self.xScale(doubleEscapeForwardEnergy);
        

    if (shouldDeleteMouseEdge())
      deleteMouseEdge(true);
    
    if( !self.options.showEscapePeaks || cursorIsOutOfBounds || self.dragging_plot ) {
      deleteEscapePeakMarker('single');
      deleteEscapePeakMarker('double');
      deleteEscapePeakMarker('singleForward');
      deleteEscapePeakMarker('doubleForward');
      return;
    }

    var singleEscapeOutOfBounds = singleEscapePix < 0 || singleEscapePix > xmax,
        doubleEscapeOutOfBounds = doubleEscapePix < 0 || doubleEscapePix > xmax,
        singleEscapeForwardOutOfBounds = singleEscapeForwardPix < 0 || singleEscapeForwardPix > xmax,
        doubleEscapeForwardOutOfBounds = doubleEscapeForwardPix < 0 || doubleEscapeForwardPix > xmax;

    if ( doubleEscapeOutOfBounds ) {
      deleteEscapePeakMarker('double');

      if ( singleEscapeOutOfBounds )
        deleteEscapePeakMarker('single');
    }

    if ( doubleEscapeForwardOutOfBounds ) {
      deleteEscapePeakMarker('doubleForward');
      if ( singleEscapeForwardOutOfBounds )
        deleteEscapePeakMarker('singleForward');
    }

    updateMouseEdge();

    if (!singleEscapeForwardOutOfBounds) {
      if( !self.singleEscapeForward && singleEscapeForwardEnergy >= 0 ) {  
        self.singleEscapeForward = self.vis.append("line")    /* create single forward escape line */
        .attr("class", "escapeLineForward")
        .attr("x1", singleEscapeForwardPix)
        .attr("x2", singleEscapeForwardPix)
        .attr("y1", 0)
        .attr("y2", self.size.height);
      self.singleEscapeForwardText = self.vis.append("text") /* create Single Forward Escape label beside line */
            .attr("class", "peakText")
            .attr( "x", singleEscapeForwardPix + xmax/200 )
            .attr( "y", self.size.height/5.3)
            .text( self.options.txt.singleEscape );
      self.singleEscapeForwardMeas = self.vis.append("text") /* Create measurement label besides line, under Single Escape label */
            .attr("class", "peakText")
            .attr( "x", singleEscapeForwardPix + xmax/125 )
            .attr( "y", self.size.height/5.3 + linehspace)
            .text( singleEscapeForwardEnergy.toFixed(1) + " keV" );
      } else {
        if ( singleEscapeForwardEnergy < 0 && self.singleEscapeForward && self.singleEscapeForwardText && self.singleEscapeForwardMeas ) {
          deleteEscapePeakMarker('singleForward');
        } else if ( self.singleEscapeForward ) {      /* Move everything to where mouse is */
          self.singleEscapeForward
            .attr("y2", self.size.height)
            .attr("x1", singleEscapeForwardPix)
            .attr("x2", singleEscapeForwardPix);
          self.singleEscapeForwardText
            .attr( "y", self.size.height/5.3)
            .attr( "x", singleEscapeForwardPix + xmax/200 );
          self.singleEscapeForwardMeas
            .attr( "y", self.size.height/5.3 + linehspace)
            .attr( "x", singleEscapeForwardPix + xmax/125 )
            .text( singleEscapeForwardEnergy.toFixed(1) + " keV" );
        }
      }
    }

    if (!doubleEscapeForwardOutOfBounds) {
      if( !self.doubleEscapeForward && doubleEscapeForwardEnergy >= 0 ) {  
        self.doubleEscapeForward = self.vis.append("line")    /* create double forward escape line */
        .attr("class", "escapeLineForward")
        .attr("x1", doubleEscapeForwardPix)
        .attr("x2", doubleEscapeForwardPix)
        .attr("y1", 0)
        .attr("y2", self.size.height);
      self.doubleEscapeForwardText = self.vis.append("text") /* create double Forward Escape label beside line */
            .attr("class", "peakText")
            .attr( "x", doubleEscapeForwardPix + xmax/200 )
            .attr( "y", self.size.height/5.3)
            .text( self.options.txt.doubleEscape );
      self.doubleEscapeForwardMeas = self.vis.append("text") /* Create measurement label besides line, under double Escape label */
            .attr("class", "peakText")
            .attr( "x", doubleEscapeForwardPix + xmax/125 )
            .attr( "y", self.size.height/5.3 + linehspace)
            .text( doubleEscapeForwardEnergy.toFixed(1) + " keV" );
      } else {
        if ( doubleEscapeForwardEnergy < 0 && self.doubleEscapeForward && self.doubleEscapeForwardText && self.doubleEscapeForwardMeas ) {
          deleteEscapePeakMarker('doubleForward');
        } else if ( self.doubleEscapeForward ) {      /* Move everything to where mouse is */
          self.doubleEscapeForward
            .attr("y2", self.size.height)
            .attr("x1", doubleEscapeForwardPix)
            .attr("x2", doubleEscapeForwardPix);
          self.doubleEscapeForwardText
            .attr( "y", self.size.height/5.3)
            .attr("x", doubleEscapeForwardPix + xmax/200 );
          self.doubleEscapeForwardMeas
            .attr( "x", doubleEscapeForwardPix + xmax/125 )
            .attr( "y", self.size.height/5.3 + linehspace)
            .text( doubleEscapeForwardEnergy.toFixed(1) + " keV" );
        }
      }
    }

    if ( singleEscapeOutOfBounds ) 
      return;

    /* Draw single escape peak if not present in the grapy */
    if( !self.singleEscape && singleEscapeEnergy >= 0 ) {  
      self.singleEscape = self.vis.append("line")    /* create single escape line */
      .attr("class", "peakLine")
      .attr("x1", singleEscapePix)
      .attr("x2", singleEscapePix)
      .attr("y1", 0)
      .attr("y2", self.size.height);
    self.singleEscapeText = self.vis.append("text") /* create Single Escape label beside line */
          .attr("class", "peakText")
          .attr( "x", singleEscapePix + xmax/200 )
          .attr( "y", self.size.height/5.3)
          .text( self.options.txt.singleEscape );
    self.singleEscapeMeas = self.vis.append("text") /* Create measurement label besides line, under Single Escape label */
          .attr("class", "peakText")
          .attr( "x", singleEscapePix + xmax/125 )
          .attr( "y", self.size.height/5.3 + linehspace)
          .text( singleEscapeEnergy.toFixed(1) + " keV" );
    } else {
      if ( singleEscapeEnergy < 0 && self.singleEscape && self.singleEscapeText && self.singleEscapeMeas ) {
        self.singleEscape.remove();      /* Delete lines and labels if out of bounds */
        self.singleEscape = null;
        self.singleEscapeText.remove();
        self.singleEscapeText = null;
        self.singleEscapeMeas.remove();
        self.singleEscapeMeas = null;

      } else if ( self.singleEscape ) {      /* Move everything to where mouse is */
        self.singleEscape
          .attr("y2", self.size.height)
          .attr("x1", singleEscapePix)
          .attr("x2", singleEscapePix);
        self.singleEscapeText
          .attr( "y", self.size.height/5.3)
          .attr( "x", singleEscapePix + xmax/200 );
        self.singleEscapeMeas
          .attr( "x", singleEscapePix + xmax/125 )
          .attr( "y", self.size.height/5.3 + linehspace)
          .text( singleEscapeEnergy.toFixed(1) + " keV" );
      }
    }

    /* Do not update the double escape peak marker anymore */
    if (doubleEscapeOutOfBounds) 
      return;

      /* Draw double escape peak if not present in the grapy */
    if( !self.doubleEscape && doubleEscapeEnergy >= 0 ) {
      self.doubleEscape = self.vis.append("line")  /* create double escape line */
      .attr("class", "peakLine")
      .attr("stroke-width", self.options.featureLineWidth)
      .attr("x1", doubleEscapePix)
      .attr("x2", doubleEscapePix)
      .attr("y1", 0)
      .attr("y2", self.size.height);
    self.doubleEscapeText = self.vis.append("text") /* create Double Escape label beside line */
          .attr("class", "peakText")
          .attr( "x", doubleEscapePix + xmax/200 )
          .attr( "y", self.size.height/5.3)
          .text( self.options.txt.doubleEscape );
    self.doubleEscapeMeas = self.vis.append("text") /* Create measurement label besides line, under Double Escape label */
          .attr("class", "peakText")
          .attr( "x", doubleEscapePix + xmax/125 )
          .attr( "y", self.size.height/5.3 + linehspace)
          .text( doubleEscapeEnergy.toFixed(1) + " keV" );
    } else {
      if ( (doubleEscapeEnergy < 0) && self.doubleEscape && self.doubleEscapeText && self.doubleEscapeMeas ) {
        self.doubleEscape.remove();    /* Delete lines and labels if out of bounds */
      self.doubleEscape = null;
          self.doubleEscapeText.remove();
          self.doubleEscapeText = null;
          self.doubleEscapeMeas.remove();
          self.doubleEscapeMeas = null;

      } else if ( self.doubleEscape ) {    /* Move everything to where mouse is */

        self.doubleEscape
          .attr("y2", self.size.height)
          .attr("x1", doubleEscapePix)
          .attr("x2", doubleEscapePix);
        self.doubleEscapeText
          .attr("x", doubleEscapePix + xmax/200 );
        self.doubleEscapeMeas
          .attr( "x", doubleEscapePix + xmax/125 )
          .text( doubleEscapeEnergy.toFixed(1) + " keV" );
      }
    }
  }

  function updateComptonPeaks() {

    var compAngleRad = self.options.comptonPeakAngle * (3.14159265/180.0)   /* calculate radians of compton peak angle */
    var comptonPeakEnergy = energy / (1 + ((energy/510.99891)*(1-Math.cos(compAngleRad)))); /* get energy value from angle and current energy position */
    var comptonPeakPix = self.xScale(comptonPeakEnergy);

    if (shouldDeleteMouseEdge())
      deleteMouseEdge(true);
    
    var comptonPeakOutOfBounds = comptonPeakPix < 0 || comptonPeakPix > xmax;

    /* delete if compton peak option is turned off or cursor is out of the graph */
    if( !self.options.showComptonPeaks || cursorIsOutOfBounds || comptonPeakOutOfBounds || self.dragging_plot ) {
      deleteComptonPeakLine();
      updateMouseEdge();
      return;
    }

    if( !self.comptonPeak ) {
      /* draw compton edge line here */
      self.comptonPeak = self.vis.append("line")
        .attr("class", "peakLine")
        .attr("stroke-width", self.options.featureLineWidth)
        .attr("y1", 0);
      self.comptonPeakText = self.vis.append("text")
        .attr("class", "peakText")
        .attr( "y", self.size.height/10);
      self.comptonPeakMeas = self.vis.append("text")
        .attr("class", "peakText")
        .attr( "y", self.size.height/10 + linehspace);
    }
    
    self.comptonPeak
        .attr("y2", self.size.height)
        .attr("x1", comptonPeakPix)
        .attr("x2", comptonPeakPix);
      
    self.comptonPeakText
        .attr( "x", comptonPeakPix + xmax/200 )
        .text( self.options.txt.comptonPeakAngle.replace("{1}", String(self.options.comptonPeakAngle)) );
        
    self.comptonPeakMeas
        .attr( "x", comptonPeakPix + xmax/125 )
        .text( comptonPeakEnergy.toFixed(1) + " keV" );
    
    updateMouseEdge();
  }

  function updateComptonEdge() {

    var compedge = energy - (energy / (1 + (2*(energy/510.99891))));
    var compEdgePix = self.xScale(compedge);
    
    if ( shouldDeleteMouseEdge() ) 
      deleteMouseEdge(true);

    var comptonEdgeOutOfBounds = compEdgePix < 0  || compEdgePix > xmax ;

    /* delete if compton edge already if option is turned off or cursor is out of the graph */
    if( !self.options.showComptonEdge || cursorIsOutOfBounds || comptonEdgeOutOfBounds || self.dragging_plot ) {
      if( self.comptonEdge ) {
        self.comptonEdge.remove(); 
        self.comptonEdge = null;
      }
      if ( self.comptonEdgeText ) {
        self.comptonEdgeText.remove(); 
        self.comptonEdgeText = null;
      }
      if ( self.comptonEdgeMeas ) {
          self.comptonEdgeMeas.remove(); 
          self.comptonEdgeMeas = null;
      }

      updateMouseEdge();
      return;
    }
    
    if( !self.comptonEdge ) {
      /* draw compton edge line here */
      self.comptonEdge = self.vis.append("line")
        .attr("class", "peakLine")
        .attr("stroke-width", self.options.featureLineWidth);
      self.comptonEdgeText = self.vis.append("text")
        .attr("class", "peakText")
        .text( self.options.txt.comptonEdge );
      self.comptonEdgeMeas = self.vis.append("text")
        .attr("class", "peakText");
    }
    
    self.comptonEdge
        .attr("x1", compEdgePix)
        .attr("x2", compEdgePix)
        .attr("y1", 0)
        .attr("y2", self.size.height);
    self.comptonEdgeText
        .attr("x", compEdgePix + xmax/200 )
        .attr("y", self.size.height/22);
    self.comptonEdgeMeas
        .attr( "x", compEdgePix + xmax/125 )
        .attr( "y", self.size.height/22 + linehspace)
        .text( compedge.toFixed(1) + " keV" );
    
    updateMouseEdge();
  }

  function updateSumPeaks( clickedEnergy ) {
    if( shouldDeleteMouseEdge() )
      deleteMouseEdge(true);

    /* delete if sum peak option is already turned off or cursor is out of the graph */
    if( !self.options.showSumPeaks || cursorIsOutOfBounds || self.dragging_plot ) {
      /* delete the sum peak corresponding help text  */
      if ( self.sumPeakHelpText ) {
        self.sumPeakHelpText.remove(); 
        self.sumPeakHelpText = null;
      }

      if ( cursorIsOutOfBounds || self.dragging_plot ) {
        if ( self.clickedSumPeak ) {
          self.savedClickEnergy = self.xScale.invert( self.clickedSumPeak.attr("x1") );
        }
      } else if ( !self.options.showSumPeaks )
        self.savedClickEnergy = null;

      deleteClickedSumPeakMarker();
    }
    

    if( !self.options.showSumPeaks || cursorIsOutOfBounds || self.dragging_plot ) {
      deleteSumPeakMarker();
      deleteLeftSumPeakMarker();
      return;
    }

    if ( !self.options.showSumPeaks ) 
      self.savedClickEnergy = null;

    updateMouseEdge();

    var shouldUpdateClickedSumPeak = true,
        shouldUpdateSumPeak = true,
        shouldUpdateLeftSumPeak = true;

    if ( self.savedClickEnergy == null && clickedEnergy == null ) {
      shouldUpdateSumPeaks = shouldUpdateLeftSumPeak = shouldUpdateClickedSumPeak = false;
    }
    else if ( self.savedClickEnergy != null && (clickedEnergy == null || clickedEnergy < 0) )
      clickedEnergy = self.savedClickEnergy;
    else if ( clickedEnergy < 0 ) {
      if ( self.clickedSumPeak ) {
        self.savedClickEnergy = clickedEnergy = Number(self.clickedSumPeak.attr("energy")) ;
      }
      else shouldUpdateClickedSumPeak = false;
    }

    if ( !shouldUpdateClickedSumPeak && !shouldUpdateSumPeak && !shouldUpdateLeftSumPeak )
      return;

    var clickedEdgeOutOfBounds = false;
    if ( shouldUpdateClickedSumPeak ) {
      self.savedClickEnergy = null;

      var clickedEdgePix = self.xScale( clickedEnergy  );
      clickedEdgeOutOfBounds = clickedEdgePix < 0 || clickedEdgePix > xmax;

      if (clickedEdgeOutOfBounds) {
        self.savedClickEnergy = clickedEnergy;
        deleteClickedSumPeakMarker();
      } else {
        if( !self.clickedSumPeak ){
          /* draw compton edge line here */
          self.clickedSumPeak = self.vis.append("line")
              .attr("class", "peakLine")
              .attr("stroke-width", self.options.featureLineWidth);
          self.clickedSumPeakMeas = self.vis.append("text")
              .attr("class", "peakText");
        }
        
        self.clickedSumPeak
            .attr("x1", clickedEdgePix)
            .attr("x2", clickedEdgePix)
            .attr("y1", 0)
            .attr("y2", self.size.height)
            .attr("energy", clickedEnergy);
        self.clickedSumPeakMeas
            .attr( "x", clickedEdgePix + xmax/125 )
            .attr( "y", self.size.height/4)
            .text( clickedEnergy.toFixed(1) + " keV" );
      }
    }  

    if ( !self.clickedSumPeak && !self.savedClickEnergy ) {
      shouldUpdateSumPeak = false;

      if ( !self.sumPeakHelpText ) {
        /* create the sum peak help text */
        self.sumPeakHelpText = self.vis.append("text")
            .attr("class", "peakText")
            .attr("fill", "red")
            .text( self.options.txt.firstEnergyClick );
      }
      
      self.sumPeakHelpText
          .attr( "x", m[0] + xmax/125 )
          .attr( "y", self.size.height/3.5)
    } else {
        /* delete sum peak help text */
        if ( self.sumPeakHelpText ) {
          self.sumPeakHelpText.remove(); 
          self.sumPeakHelpText = null;
        }
    }

    if ( shouldUpdateLeftSumPeak && energy < clickedEnergy ) {
      var leftSumEnergy = clickedEnergy - energy,
          leftSumPix = self.xScale( leftSumEnergy  ),
          leftSumOutOfBounds = leftSumPix < 0 || leftSumPix > xmax;

      if( !self.leftSumPeak ) {
        /* draw left-sum peak line here */
        self.leftSumPeak = self.vis.append("line")
          .attr("class", "peakLine")
          .attr("stroke-width", self.options.featureLineWidth);
        self.leftSumPeakText = self.vis.append("text")
          .attr("class", "peakText")
          .text( self.options.txt.clickedPeak );
        self.leftSumPeakMeas = self.vis.append("text")
          .attr("class", "peakText");
      }
      
      /* update the left sum peak line here */
      self.leftSumPeak
          .attr("x1", leftSumPix)
          .attr("x2", leftSumPix)
          .attr("y1", 0)
          .attr("y2", self.size.height); 
      self.leftSumPeakText
          .attr( "x", leftSumPix + xmax/125 )
          .attr( "y", self.size.height/3.4);
      self.leftSumPeakMeas
          .attr( "x", leftSumPix + xmax/125 )
          .attr( "y", self.size.height/3.4 + linehspace)
          .text( energy.toFixed(1) + "+" + leftSumEnergy.toFixed(1) + "=" + clickedEnergy.toFixed(1) + " keV" );

      if( leftSumOutOfBounds )
        deleteLeftSumPeakMarker();

    } else
      deleteLeftSumPeakMarker();


    if ( shouldUpdateSumPeak ) {
      var sumEnergy = energy + clickedEnergy,
          sumPix = self.xScale( sumEnergy  );

      var sumPeakOutOfBounds = sumPix < 0 || sumPix > xmax;
      if ( sumPeakOutOfBounds ) {
        deleteSumPeakMarker();
        return;
      }

      if (!clickedEnergy)
        return;

      if( !self.sumPeak ) {
        /* draw sum peak line here */
        self.sumPeak = self.vis.append("line")
          .attr("class", "peakLine")
          .attr("stroke-width", self.options.featureLineWidth);
      self.sumPeakText = self.vis.append("text")
          .attr("class", "peakText")
          .text( self.options.txt.sumPeak );
      }
      
      if( !self.sumPeakMeas )
        self.sumPeakMeas = self.vis.append("text")
                .attr("class", "peakText");

      self.sumPeak
          .attr("x1", sumPix)
          .attr("x2", sumPix)
          .attr("y1", 0)
          .attr("y2", self.size.height);
      self.sumPeakText
          .attr( "x", sumPix + xmax/125 )
          .attr( "y", self.size.height/4);
      self.sumPeakMeas
          .attr( "x", sumPix + xmax/125 )
          .attr( "y", self.size.height/4 +  + linehspace)
          .text( clickedEnergy.toFixed(1) + "+" + energy.toFixed(1) + "=" + sumEnergy.toFixed(1) + " keV" );
    }
  }

  updateEscapePeaks();
  updateComptonPeaks();
  updateComptonEdge();
  updateSumPeaks( mouseDownEnergy, );
}

SpectrumChartD3.prototype.setComptonEdge = function(d) {
  this.options.showComptonEdge = d;
  this.updateFeatureMarkers();
}

SpectrumChartD3.prototype.setComptonPeakAngle = function(d) {
  var value = Number(d);
  this.options.comptonPeakAngle = (!isNaN(value) && 0 <= value && value <= 180 ) ? value : 180;
  this.updateFeatureMarkers();
}

SpectrumChartD3.prototype.setComptonPeaks = function(d) {
  this.options.showComptonPeaks = d;
  this.updateFeatureMarkers();
}

SpectrumChartD3.prototype.setEscapePeaks = function(d) {
  this.options.showEscapePeaks = d;
  this.updateFeatureMarkers();
}

SpectrumChartD3.prototype.setSumPeaks = function(d) {
  this.options.showSumPeaks = d;
  this.updateFeatureMarkers();
}

SpectrumChartD3.prototype.setPeakLabelSize = function(d) {
  this.options.peakLabelSize = (typeof d == 'string') ? d : null;
}

SpectrumChartD3.prototype.setPeakLabelRotation = function(d) {
  this.options.peakLabelRotation = (typeof d == 'number') ? d : 0;
}

SpectrumChartD3.prototype.setLogYAxisMin = function(d) {
  this.options.logYAxisMin = (typeof d == 'number') && (d > 0.0) ? d : 0.1;
}

SpectrumChartD3.prototype.setSearchWindows = function(ranges) {
  var self = this;

  if( !Array.isArray(ranges) || ranges.length===0 ){
    self.searchEnergyWindows = null;
  } else {
    self.searchEnergyWindows = ranges;
    self.searchEnergyWindows.sort( function(l,r){ return l.energy < r.energy }  )
  }

  self.redraw()();
}

//Function that takes regions to draw in a solid fill.
//[{lowerEnergy: 90, upperEnergy: 112, fill: 'rgba(23,53,12,0.1)', hash: 123112319}, ...]
SpectrumChartD3.prototype.setHighlightRegions = function(ranges) {
  var self = this;
  
  if( !Array.isArray(ranges) || ranges.length===0 ){
    self.highlightRegions = null;
    self.vis.selectAll("g.highlight").remove(); //drawHighlightRegions() returns immediately if self.highlightRegions is null.
  } else {
    //ToDo add checking that regions have appropriate variables and lowerEnergy is less than upperEnergy
    self.highlightRegions = ranges;
    self.highlightRegions.sort( function(l,r){ return l.lowerEnergy < r.lowerEnergy }  )
  }
  
  self.redraw()();
}


/**
 * -------------- Chart Animation Functions --------------
 */
SpectrumChartD3.prototype.redrawZoomXAnimation = function(targetDomain) {
  var self = this;

  /* Cancel animation if showAnimation option not checked */
  if( !self.options.showAnimation )
    return;

  return function() {
    //For a HPGe fore+back, with U238 lines showing, on M1 mac in Safari with debug console open
    // we usually get to this function every 17ms (60 hz).  But occasionally (~15% of time), it takes ~70 ms
    

    /* Cancel the animation once reached desired target domain */
    if( self.currentDomain === null || targetDomain === null
        || (self.currentDomain[0] == targetDomain[0] && self.currentDomain[1] == targetDomain[1]) ) {
      //console.log("Time for animation = ", Math.floor(Date.now()) - self.startAnimationZoomTime, " ms");
      self.handleCancelAnimationZoom();
      return;
    }

    /* Use fraction of time elapsed to calculate how far we will zoom in this frame */
    var animationFractionTimeElapsed = Math.min( Math.max((Math.floor(Date.now()) - self.startAnimationZoomTime) / self.options.animationDuration), 1 );

    if( animationFractionTimeElapsed >= 0.999 ){
      self.handleCancelAnimationZoom();
      self.setXAxisRange( targetDomain[0], targetDomain[1], true, true );  //do emit range change
      self.redraw()();
      self.updateFeatureMarkers(-1);
      return;
    }
    
    /* Set x-axis domain to new values */
    self.setXAxisRange(
      Math.min( self.savedDomain[0] + (animationFractionTimeElapsed * (targetDomain[0] - self.savedDomain[0])), targetDomain[0] ),
      Math.max( self.savedDomain[1] - (animationFractionTimeElapsed * (self.savedDomain[1] - targetDomain[1])), targetDomain[1] ),
      false /* dont emit x-range change. */,
      true
    );
    self.currentDomain = self.xScale.domain();

    /* Redraw and request a new animation frame */
    self.redraw()();
    self.zoomAnimationID = requestAnimationFrame(self.redrawZoomXAnimation(targetDomain));
  }
}


SpectrumChartD3.prototype.redrawZoomYAnimation = function(targetDomain) {
  const self = this;
  
  console.assert( targetDomain && targetDomain.length === 2, "Invalid targetDomain" );
  console.assert( targetDomain[0] >= targetDomain[1], "Expected targetDomain to be ordered other way" );
  console.assert( self.options.showAnimation, "redrawZoomYAnimation shouldnt be called when options not set" );
  console.assert( self.savedDomain && self.savedDomain.length === 2, "Invalid savedDomain" );
  
  const redraw = this.redrawYAxis();
  
  function roundTo3Dec(num) { return Math.round(num * 1000) / 1000; }

  return function() {
    /* Cancel the animation once reached desired target domain, or desired time has elapsed */
    const now = Math.floor( Date.now() );
    const fractionTimeElapsed = Math.min( Math.max((now - self.startAnimationZoomTime) / self.options.animationDuration), 1 );
    
    if( ( fractionTimeElapsed >= 0.999 )
        || (self.currentDomain == null)
        || (roundTo3Dec(self.currentDomain[0]) == roundTo3Dec(targetDomain[0])
            && roundTo3Dec(self.currentDomain[1]) == roundTo3Dec(targetDomain[1]))) {
      console.log("Time for animation = ", (now - self.startAnimationZoomTime), " ms");
      self.handleCancelAnimationZoom();
      self.yScale.domain( targetDomain );
      redraw();
      return;
    }// we're done here

    /* Use fraction of time elapsed to calculate how far we will zoom in this frame */
    const nowY0 = self.savedDomain[0] + fractionTimeElapsed*(targetDomain[0] - self.savedDomain[0]);
    const nowY1 = self.savedDomain[1] + fractionTimeElapsed*(targetDomain[1] - self.savedDomain[1]);
    
    self.currentDomain = [nowY0, nowY1];
    self.yScale.domain( self.currentDomain );
    
    redraw();
    self.zoomAnimationID = requestAnimationFrame( self.redrawZoomYAnimation(targetDomain) );
  }
}//SpectrumChartD3.prototype.redrawZoomYAnimation


SpectrumChartD3.prototype.setYAxisRangeAnimated = function( yrange ){
  if( !this.options.showAnimation ){
    this.yScale.domain([y1, y2]);
    this.redrawYAxis()();
    return;
  }
  
  this.currentDomain = this.savedDomain = this.yScale.domain();
  
  if( this.zoomAnimationID != null )
    cancelAnimationFrame(this.zoomAnimationID);
  
  this.startAnimationZoomTime = Math.floor( Date.now() );
  this.zoomAnimationID = requestAnimationFrame( this.redrawZoomYAnimation(yrange ? yrange : this.getYAxisDomain()) );
}//SpectrumChartD3.prototype.setYAxisRangeAnimated


SpectrumChartD3.prototype.setShowAnimation = function(d) {
  this.options.showAnimation = d;
}

SpectrumChartD3.prototype.setAnimationDuration = function(d) {
  this.options.animationDuration = d;
}

SpectrumChartD3.prototype.handleCancelAnimationZoom = function() {
  var self = this;

  /* Cancel the animation frames */
  if (self.zoomAnimationID != null) {
    cancelAnimationFrame(self.zoomAnimationID);
  }

  /* Set animation properties to null */
  self.zoomAnimationID = null;
  self.currentDomain = null;
  self.savedDomain = null;
  self.startAnimationZoomTime = null;
}


/**
 * -------------- X-axis Zoom Functions --------------
 */
SpectrumChartD3.prototype.handleMouseMoveZoomX = function () {
  var self = this;
  
  var zoomInXBox = self.vis.select("#zoomInXBox"),
      zoomInXText = self.vis.select("#zoomInXText");

  /* Cancel erase peaks mode, we're zooming in */
  self.handleCancelMouseDeletePeak();
  /* Cancel recalibration mode, we're zooming in */
  self.handleCancelMouseRecalibration();
  /* Cancel the zooming in y mode */
  self.handleCancelMouseZoomY();
  /* Cancel the count gammas mode */
  self.handleCancelMouseCountGammas();
  if( !self.zoominmouse )
    return;

  /* Adjust the mouse move position with respect to the bounds of the vis */
  if (self.lastMouseMovePos[0] < 0)
    self.lastMouseMovePos[0] = 0;
  else if (self.lastMouseMovePos[0] > self.size.width)
    self.lastMouseMovePos[0] = self.size.width;

  /* We are now zooming in */
  self.zooming_plot = true;

  console.assert( self.lastMouseMovePos.length === 4, "self.lastMouseMovePos should have 4 elements: ", self.lastMouseMovePos );
  console.assert( self.zoominmouse.length === 4, "self.lastMouseMovePos should have 4 elements: ", self.zoominmouse );
  
  /* If the mouse position is less than the zoombox starting position (zoom-out)
   Note that we we are using the coordinates relative to the this.svg element, and not this.vis
   (i.e., using element 2, instead of 0 in the below arrays); this is because when the y-axis area
   changes in the middle of zooming out, it can cause glitches, and nonsensical toggling between
   zooming in and zooming out because self.lastMouseMovePos will swing with the changes in y-axis
   area.
   */
  if( self.lastMouseMovePos[2] < self.zoominmouse[2] ) {
    /* If we were animating a zoom, cancel that animation */
    self.handleCancelAnimationZoom();

    /* Remove the zoom-in x-axis text, we are zooming out */
    zoomInXText.remove();
    zoomInXBox.remove();
    self.zoominaltereddomain = true;

    /*Do some zooming out */
    var bounds = self.min_max_x_values();
    var xaxismin = bounds[0],
        xaxismax = bounds[1];
        
    var frac = 4 * (self.zoominmouse[2] - self.lastMouseMovePos[2]) / self.zoominmouse[0];

    if( !isFinite(frac) || isNaN(frac) || frac < 0.0 )
      frac = 0;

    var origdx = self.origdomain[1] - self.origdomain[0],
        maxdx = xaxismax - xaxismin,
        newdx = origdx + frac*(maxdx - origdx);

    const deltadx = newdx - origdx;
    let newxmin = self.origdomain[0] - 0.5*deltadx;
    let newxmax = self.origdomain[1] + 0.5*deltadx;
    
    if( newxmin < xaxismin ){
      newxmax += (xaxismin - newxmin);
      newxmin = xaxismin;
    }
    if( newxmax > xaxismax ){
      newxmin -= (newxmax - xaxismax);
      newxmin = Math.max( newxmin, xaxismin );
      newxmax = xaxismax;
    }
    
    /* TODO: periodically send the xrangechanged signal during zooming out.  Right now only sent when mouse goes up. */
    self.setXAxisRange(newxmin,newxmax,false,true);
    self.redraw()();
  } else {
    /* Restore the zoom-in box */
    if (zoomInXBox.empty()) {
      zoomInXBox = self.vis.append("rect")
      .attr("id", "zoomInXBox")
      .attr("class","leftbuttonzoombox")
      .attr("y", 0)
      .attr("pointer-events", "none");
    }
    
    if( self.zoominaltereddomain ) {
      // We get here when we were zooming out, but now we're zooming in; we'll set back to the original domain
      self.setXAxisRange( self.origdomain[0], self.origdomain[1], true, true );
      self.zoominaltereddomain = false;
      self.redraw()();
    }

    /* Update the zoomin-box x-position and width */
    zoomInXBox
      .attr("x", self.zoominmouse[0])
      .attr("height", self.size.height)
      .attr("width", self.lastMouseMovePos[0] - self.zoominmouse[0] );

    if (self.lastMouseMovePos[0] - self.zoominmouse[0] > 7) {
      /* if zoom in box is at least 7px wide, update it */
      if( zoomInXText.empty() ){
        zoomInXText = self.vis.append("text")
          .attr("id", "zoomInXText")
          .attr("class", "chartLineText")
          .attr("y", Number(zoomInXBox.attr("height"))/2)
          .text( self.options.txt.zoomIn );
      }

      /* keep zoom in label centered on the box */
      const bb = zoomInXText.node().getBoundingClientRect();
      zoomInXText.attr("x", (0.5*(self.zoominmouse[0] + self.lastMouseMovePos[0])) - 0.5*bb.width);
    }else if( !zoomInXText.empty() ){
      /* delete if zoom in box not wide enough (eg. will not zoom) */
      zoomInXText.remove();
    }//if( draw "Zoom In" text ) / else
  }//if( zoom out ) / else ( zoom in )
}//SpectrumChartD3.prototype.handleMouseMoveZoomX


SpectrumChartD3.prototype.handleMouseUpZoomX = function () {
  var self = this;

  if (!self.rawData || !self.rawData.spectra || !self.rawData.spectra.length || !self.zooming_plot)
    return;

  if( self.zooming_plot && self.lastMouseMovePos ) {
    const foreground = self.rawData.spectra[0];

    let m = self.getMousePos(); // Get the current mouse position
    
    console.assert( m.length === 4, "Expected getMousePos to return array with length 4.", m );
    console.assert( self.zoominmouse.length === 4, "Expected zoominmouse to be array length 4,", self.zoominmouse );
    
    if( m[2] < self.zoominmouse[2] ) {
      // we were zooming out, nothing to do here, out x-axis limits are already where we want them
    } else {
      // If we are here, we assume the y-axis area width and domain to be the exact same as when we
      //  started, which should be true, but it still makes me feel a bit uneasy.
      
      m[0] = m[0] < 0 ? 0 : m[0];

      var oldXScale = self.xScale.domain();

      /*This is a big hack, because self.xScale.invert(m[0]) == self.zoominx0 otherwise.  I dont completely understand why! */
      var x0 = self.xScale.invert(self.zoominmouse[0]),
          x1 = self.xScale.invert(m[0]);

      /*require the mouse to have moved at least 6 pixels in x */
      if( m[0] - self.zoominmouse[0] > 7 && self.rawData ) {
          
        var bounds = self.min_max_x_values();
        var mindatax = bounds[0], 
            maxdatax = bounds[1];
        
        /* Make sure the new scale will span at least one bin , if not  */
        /*  make it just less than one bin, centered on that bin. */
        var rawbi = d3.bisector(function(d){return d;}); 
        var lbin = rawbi.left(foreground.x, x0+(x1-x0));
        if( lbin > 1 && lbin < (foreground.x.length-1) && lbin === rawbi.left(foreground.x,x1+(x1-x0)) ) {
          // This doesnt work correctly; it doesnt center on the bin, meaning you're always a little off from where you want...
          var corx0 = foreground.x[lbin-1];
          var corx1 = foreground.x[lbin];
          var p = 0.01*(corx1-corx0);
          corx0 += p;
          corx1 -= p;
          
          x0 = Math.max(corx0+(x1-x0), mindatax);
          x1 = Math.min(corx1+(x1-x0), maxdatax);
        }
        
        /* Draw zoom animations if option is checked */
        if( self.options.showAnimation ) {
          self.handleCancelAnimationZoom();  //Cancel any current zoom animations

          /* Start new zoom animation; 'xrangechanged' will be emitted when animation is complete. */
          self.currentDomain = self.savedDomain = oldXScale;
          self.zoomAnimationID = requestAnimationFrame(self.redrawZoomXAnimation([x0,x1]));
          self.startAnimationZoomTime = Math.floor(Date.now());
        } else {
          /* Zoom animation unchecked; draw new x-axis range; the true below causes the 'xrangechanged' signal to be emitted. */
          self.setXAxisRange(x0, x1, true, true);
          self.redraw()();
          self.updateFeatureMarkers(-1);
        }   
      }
    }
  }

  self.handleCancelMouseZoomInX();
}//SpectrumChartD3.prototype.handleMouseUpZoomX


SpectrumChartD3.prototype.handleCancelMouseZoomInX = function() {
  this.zooming_plot = false; //Not zooming in plot anymore
  
  /* Delete zoom in box and text */
  this.vis.select("#zoomInXBox").remove();
  this.vis.select("#zoomInXText").remove();
}//SpectrumChartD3.prototype.handleCancelMouseZoomInX


/**
 * -------------- Y-axis Zoom Functions --------------
 */
SpectrumChartD3.prototype.redrawYAxis = function() {
  var self = this;

  return function() {
    self.do_rebin();

    self.drawYTicks();
    self.calcLeftPadding( true );

    self.drawXTicks();
    self.drawXAxisArrows();
    
    self.drawPeaks();
    self.drawRefGammaLines();
    self.updateMouseCoordText();

    self.update();

    self.yAxisZoomedOutFully = false;
  }
}

SpectrumChartD3.prototype.setYAxisMinimum = function( minimum ) {
  const maximum = this.yScale.domain()[0];
  this.yScale.domain([maximum,minimum]);
  this.redrawYAxis()();
}

SpectrumChartD3.prototype.setYAxisMaximum = function( maximum ) {
  const minimum = this.yScale.domain()[1];
  this.yScale.domain([maximum,minimum]);
  this.redrawYAxis()();
}

// See also setYAxisRangeAnimated([min,max])
SpectrumChartD3.prototype.setYAxisRange = function( minimum, maximum ) {
  this.yScale.domain([maximum,minimum]);
  this.redrawYAxis()();
}

SpectrumChartD3.prototype.handleMouseMoveZoomY = function () {
  var self = this;

  /* Set the objects displayed for zooming in the y-axis */
  var zoomInYBox = self.vis.select("#zoomInYBox"),
      zoomInYText = self.vis.select("#zoomInYText");

  /* Cancel the zooming mode */
  self.handleCancelMouseZoomInX();

  /* Cancel erase peaks mode, we're zooming in */
  self.handleCancelMouseDeletePeak();

  /* Cancel recalibration mode, we're zooming in */
  self.handleCancelMouseRecalibration();

  /* Cancel the count gammas mode */
  self.handleCancelMouseCountGammas();

  /* Now zooming in y mode */
  self.zoomingYPlot = true;

  /* Create the reference for the start of the zoom-in (Yaxis) box */
  if( !self.zoomInYMouse )
    self.zoomInYMouse = self.lastMouseMovePos;

  /* Adjust the zoom in y mouse in case user starts dragging from out of bounds */
  if (self.zoomInYMouse[1] < 0)
    self.zoomInYMouse[1] = 0;
  else if (self.zoomInYMouse[1] > self.size.height)
    self.zoomInYMouse[1] = self.size.height;

  if (self.zoomInYMouse[0] < 0 || self.zoomInYMouse[0] > self.size.width) {
    self.handleCancelMouseZoomY();
    return;
  }

  var height = self.lastMouseMovePos[1] - self.zoomInYMouse[1];

  /* Set the zoom-y box */
  if (zoomInYBox.empty()) {
    zoomInYBox = self.vis.append("rect")
      .attr("id", "zoomInYBox")
      .attr("class","leftbuttonzoombox")
      .attr("width", self.size.width )
      .attr("height", 0)
      .attr("x", 0)
      .attr("y", self.zoomInYMouse[1])
      .attr("pointer-events", "none");
  } else {
    if (height >= 0)  zoomInYBox.attr("height", height);
    else              zoomInYBox.attr("y", self.lastMouseMovePos[1])
                        .attr("height", Math.abs(height));
  }

  if (Math.abs(self.lastMouseMovePos[1] - self.zoomInYMouse[1]) > 10) {   /* if zoom in box is at least 7px wide, update it */
    if (zoomInYText.empty()) {
      zoomInYText = self.vis.append("text")
        .attr("id", "zoomInYText")
        .attr("class", "chartLineText");
    }

    if (height > 0) {
      zoomInYText.text( self.options.txt.zoomInY );
      zoomInYBox.attr("class", "leftbuttonzoombox");
    } else {
      var zoomOutText = self.options.txt.zoomOutY;
      if (-height < 0.05*self.size.height)
        zoomOutText += " x2";
      else if (-height < 0.075*self.size.height)
        zoomOutText += " x4";
      else
        zoomOutText += " full";

      zoomInYText.text(zoomOutText);
      zoomInYBox.attr("class", "leftbuttonzoomoutboxy");
    }

    /* keep zoom in label centered on the box */
    const textBB = zoomInYText.node().getBoundingClientRect();
    zoomInYText.attr("y", Number(zoomInYBox.attr("y")) + (Number(zoomInYBox.attr("height"))/2) + 0.5*textBB.height );
    zoomInYText.attr("x", 0.5*self.size.width - 0.5*textBB.width );
  } else if (!zoomInYText.empty()) {
    /* delete if zoom in box not wide enough (eg. will not zoom) */
    zoomInYText.remove();
  }
}//SpectrumChartD3.prototype.handleMouseMoveZoomY


SpectrumChartD3.prototype.handleMouseUpZoomY = function () {
  let self = this;
  const zoomInYBox = self.vis.select("#zoomInYBox");
  const zoomInYText = self.vis.select("#zoomInYText");

  if( !this.zoomInYMouse || !self.zoomingYPlot || zoomInYBox.empty() || zoomInYText.empty() ) {
    self.handleCancelMouseZoomY();
    return;
  }

  /* Set the y-values for where zoom-in occurred */
  const start_px = this.zoomInYMouse[1];
  const end_px = this.lastMouseMovePos[1];
  const start_counts = self.yScale.invert( start_px );
  const end_counts = self.yScale.invert( end_px );
  
  console.assert( Math.abs(end_px - start_px) > 10, "handleMouseMoveZoomY() should have zoomInYText for dy_px < 10, end_px:", end_px, ", start_px:", start_px );

  if( Math.abs(end_px - start_px) <= 10 ){ //check again, jic
    self.handleCancelMouseZoomY();
    return;
  }
  
  if( start_counts > end_counts ) {
    // we are zooming in
    self.setYAxisRangeAnimated( [start_counts, end_counts] );
  } else {
    // we are zooming out
    if (zoomInYText.text().endsWith("full")) {
      /* Zoom out completely if user dragged up a considerable amount */
      self.setYAxisRangeAnimated( self.getYAxisDomain() );
    } else {
      /* Zoom out a portion of the full y-axis */

      /* This represents how much of the y-axis we will be zooming out */
      const mult = (zoomInYText.text().endsWith("x2") ? 2 : 4);

      /* Get the old values of the current y-domain */
      const domain = self.yScale.domain();
      const oldRange = Math.abs(domain[1] - domain[0]);
      const centroid = domain[0] + 0.5*oldRange;

      /* Values for the minimum and maximum y values */
      let minY = 0.1, maxY = 3000;

      if( self.rawData && self.rawData.y ){
        /* Get the min, max y values from the data */
        const ydomain = self.getYAxisDomain();
        minY = ydomain[1];
        maxY = ydomain[0];
      }

      /* Set the values for the new y-domain */
      let newY0 = centroid - 0.5*mult*oldRange;
      let newY1 = newY0 + mult*oldRange;
      
      if( newY0 < minY )
        newY1 += (minY - newY0);
      if( newY1 > maxY )
        newY0 -= (newY1 - maxY);

      self.setYAxisRangeAnimated( [Math.min(newY1, maxY), Math.max(newY0, minY)] );
    }//if( zoom all the way out ) / else (zoom to partial y-range )
  }//if( zooming in ) / else ( zoom out )

  /* Clean up objets from zooming in y-axis */
  self.handleCancelMouseZoomY();
}//SpectrumChartD3.prototype.handleMouseUpZoomY


SpectrumChartD3.prototype.handleCancelMouseZoomY = function() {
  /* Delete zoom box and text */
  this.vis.select("#zoomInYBox").remove();
  this.vis.select("#zoomInYText").remove();

  /* Not zooming in y anymore */
  this.zoomingYPlot = false;
  this.zoomInYMouse = null;
}//SpectrumChartD3.prototype.handleCancelMouseZoomY



SpectrumChartD3.prototype.handleTouchMoveZoomInX = function() {
  const self = this;

  if (!self.touchesOnChart)
    return;

  self.handleCancelTouchDeletePeak();
  self.handleCancelTouchPeakFit();
  self.handleCancelTouchCountGammas();

  var t = d3.touches(self.vis[0][0]);
  var keys = Object.keys(self.touchesOnChart);

  if( (keys.length !== 2) || (t.length !== 2) || !self.touchZoomStartEnergies )
    return;

  var x1 = t[0][0],  x2 = t[1][0];
  if( x1 > x2 )
    x2 = [x1, x1 = x2][0];

  var cur_e1 = self.xScale.invert(x1);
  var cur_e2 = self.xScale.invert(x2);
  
  // We want to adjust the energy so our fingers are on the original energies
  // energy = a + b*x_pixel; solve for a and b
  // cur_e1 = a + b*x1
  // cur_e2 = a + b*x2
  // cur_e1 - b*x1 = cur_e2 - bx2
  const b = (cur_e1 - cur_e2) / (x1 - x2);
  const xdomain = self.xScale.domain();

  var start_e1 = self.touchZoomStartEnergies[0], start_e2 = self.touchZoomStartEnergies[1];
  if( start_e1 > start_e2 )
    start_e2 = [start_e1, start_e1 = start_e2][0];
    
  var xscale = (start_e2 - start_e1) / (cur_e2 - cur_e1);
  var new_b = xscale * b;
  var new_a = start_e1 - new_b*x1;
  var startpx = self.xScale(xdomain[0]);
  var endpx = self.xScale(xdomain[1]);
  var new_lower_energy = new_a + new_b*startpx;
  var new_upper_energy = new_a + new_b*endpx;

  //Now need to make sure we arent zooming in too much or going past range of data.
  var minx, maxx, bounds, foreground;
  if( !self.rawData || !self.rawData.spectra || !self.rawData.spectra.length ){
    minx = 0;
    maxx = 3000;
  }else {
    bounds = self.min_max_x_values();
    minx = bounds[0];
    maxx = bounds[1];
    foreground = self.rawData.spectra[0];
  }

  if( new_lower_energy < minx )
    new_lower_energy = minx;
  if( new_lower_energy > maxx )
    new_lower_energy = maxx;

  if( new_upper_energy < minx )
    new_upper_energy = minx;
  if( new_upper_energy > maxx )
    new_upper_energy = maxx;
  
  if( new_upper_energy < new_lower_energy )
    new_upper_energy = [new_lower_energy, new_lower_energy = new_upper_energy][0];
  
  if( foreground ){
    var avrg_channel_width = (maxx - minx) / foreground.x.length;
    var xrange = new_upper_energy - new_lower_energy;

    // Make sure the new scale will span ~3 channels
    if( xrange < 4*avrg_channel_width ){
      var rawbi = d3.bisector(function(d){return d;}); 
      var lbin = rawbi.left(foreground.x, new_lower_energy);
      var channel_width = (lbin < (foreground.x.length-1)) ? foreground.x[lbin+1] - foreground.x[lbin] : 0;
      
      if( xrange < 3*channel_width  ) {   
        xrange = 3*channel_width;
        var middle_px = 0.5*(x1 + x2);
        var middle_frac = (middle_px - startpx) / (endpx - startpx);
        var middle_energy = 0.5*(start_e1 + start_e2);
        new_lower_energy = middle_energy - middle_frac*xrange;
        new_upper_energy = middle_energy + (1-middle_frac)*xrange;
      }//if( we are in the same channel )
    }//if( we should check if we are in the same channel )
  }//if( we have foreground data )
  
  if( !isNaN(new_lower_energy) && !isNaN(new_upper_energy) && isFinite(new_lower_energy) && isFinite(new_upper_energy) ){
    self.xScale.domain([new_lower_energy, new_upper_energy]);
    self.redraw()();
    self.updateFeatureMarkers(-1);
  }else
  {
    console.log( "Got inf in handleTouchMoveZoomInX" );
  }
}//handleTouchMoveZoomInX


SpectrumChartD3.prototype.handleTouchMoveZoomY = function() {
  const self = this;
  if( !this.touchesOnChart )
    return;

  this.handleCancelTouchDeletePeak();
  this.handleCancelTouchPeakFit();
  this.handleCancelTouchCountGammas();

  const t = d3.touches(this.vis[0][0]);
  const keys = Object.keys(this.touchesOnChart);

  if( (keys.length !== 2) || (t.length !== 2) )
    return;

  let zoomInYTopLine = this.vis.select("#zoomInYTopLine");
  let zoomInYBottomLine = this.vis.select("#zoomInYBottomLine");
  let zoomInYText = this.vis.select("#zoomInYText");

  let touch1 = this.touchesOnChart[keys[0]];
  let touch2 = this.touchesOnChart[keys[1]];

  if (!touch1.visY)
    touch1.visY = t[0][1];
  if (!touch2.visY)
    touch2.visY = t[1][1];

  const topTouch = touch1.pageY < touch2.pageY ? touch1 : touch2;
  const bottomTouch = topTouch == touch1 ? touch2 : touch1;

  if( zoomInYTopLine.empty() ) {
    zoomInYTopLine = this.vis.append("line")
      .attr("id", "zoomInYTopLine")
      .attr("class", "mouseLine")
      .attr("x1", 0)
      .attr("x2", this.size.width)
      .attr("y1", topTouch.visY)
      .attr("y2", topTouch.visY);
  }

  if( zoomInYBottomLine.empty() ) {
    zoomInYBottomLine = this.vis.append("line")
      .attr("id", "zoomInYBottomLine")
      .attr("class", "mouseLine")
      .attr("x1", 0)
      .attr("x2", this.size.width)
      .attr("y1", bottomTouch.visY)
      .attr("y2", bottomTouch.visY);
  }

  if( zoomInYText.empty() ) {
    zoomInYText = this.vis.append("text")
      .attr("id", "zoomInYText")
      .attr("class", "mouseLineText");
  }

  zoomInYText.text(function() {
    if (topTouch.visY > topTouch.startY && bottomTouch.visY < bottomTouch.startY)
      return self.options.txt.zoomOutY;
    else if (topTouch.visY == topTouch.startY && bottomTouch.visY == bottomTouch.startY)
      return "";
    else
      return self.options.txt.zoomInY;
  });

  
  const textBB = zoomInYText.node().getBoundingClientRect();
  zoomInYText.attr("x", 0.5*this.size.width - 0.5*textBB.width );
  zoomInYText.attr("y", 0.5*((Number(zoomInYTopLine.attr("y1")) + Number(zoomInYBottomLine.attr("y1"))) + textBB.height) );
}//SpectrumChartD3.prototype.handleTouchMoveZoomY


SpectrumChartD3.prototype.handleTouchEndZoomY = function() {
  const zoomInYTopLine = this.vis.select("#zoomInYTopLine");
  const zoomInYBottomLine = this.vis.select("#zoomInYBottomLine");
  const zoomInYText = this.vis.select("#zoomInYText");

  if( zoomInYTopLine.empty() || zoomInYBottomLine.empty() ){
    this.handleTouchCancelZoomY();
    return;
  }
  
  /* Set the y-values for where zoom-in occurred */
  const ypix1 = Number(zoomInYTopLine.attr("y1"));
  const ypix2 = Number(zoomInYBottomLine.attr("y1"));
  
  const y1 = this.yScale.invert(ypix1);
  const y2 = this.yScale.invert(ypix2);
  
  // Remove the lines and such
  this.handleTouchCancelZoomY();
  
  if( Math.abs(ypix2 - ypix1) > 10 ) {
    // we are zooming in or out
    if (zoomInYText.text() == self.options.txt.zoomInY ) {
      this.setYAxisRangeAnimated( [y1,y2] );
    }else if( zoomInYText.text() == self.options.txt.zoomOutY ) {
      this.setYAxisRangeAnimated( this.getYAxisDomain() );
    }
  }else{
    // I think zoomInYText should be empty if we are here
  }
  
}//SpectrumChartD3.prototype.handleTouchEndZoomY


SpectrumChartD3.prototype.handleTouchCancelZoomY = function() {
  this.vis.select("#zoomInYTopLine").remove();
  this.vis.select("#zoomInYBottomLine").remove();
  this.vis.select("#zoomInYText").remove();
}


/**
 * -------------- Energy Recalibration Functions --------------
 */
SpectrumChartD3.prototype.handleMouseMoveRecalibration = function() {
  var self = this;

  /* Clear the zoom, we're recalibrating the chart */
  self.handleCancelMouseZoomInX();

  /* Cancel erase peaks mode, we're recalibrating the chart */
  self.handleCancelMouseDeletePeak();

  /* Cancel the zooming in y mode */
  self.handleCancelMouseZoomY();

  /* Cancel the count gammas mode */
  self.handleCancelMouseCountGammas();

  if (!self.recalibrationMousePos)
    return;
  if (!self.rawData || !self.rawData.spectra || !self.rawData.spectra.length) 
    return;

  /* Adjust the mouse move position with respect to the bounds of the vis */
  if (self.lastMouseMovePos[0] < 0)
    self.lastMouseMovePos[0] = 0;
  else if (self.lastMouseMovePos[0] > self.size.width)
    self.lastMouseMovePos[0] = self.size.width;

  /* Set the line objects to be referenced */
  var recalibrationStartLine = self.vis.select("#recalibrationStartLine"),
      recalibrationText = self.vis.select("#recalibrationText"),
      recalibrationMousePosLines = self.vis.select("#recalibrationMousePosLines");

  var recalibrationG = self.vis.select("#recalibrationG");
  var recalibrationPeakVis = self.vis.select("#recalibrationPeakVis");

  /* Set the line that symbolizes where user initially began ctrl-option-drag */
  if (recalibrationStartLine.empty()) {
    recalibrationStartLine = self.vis.append("line")
      .attr("id", "recalibrationStartLine")
      .attr("class", "mouseLine")
      .attr("x1", self.recalibrationMousePos[0])
      .attr("x2", self.recalibrationMousePos[0])
      .attr("y1", 0)
      .attr("y2", self.size.height);
  }

  const start = recalibrationText.empty() ? self.lastMouseMovePos[0] : recalibrationStartLine.attr("x1");
  const now = self.lastMouseMovePos[0];

  const txt = self.options.txt.recalFromTo.replace("{1}",String(self.xScale.invert(start).toFixed(2))).replace("{2}",self.xScale.invert(now).toFixed(2) );
  
  if (recalibrationText.empty()) {                       /* ctrl-option-drag text to say where recalibration ranges are */
    recalibrationText = self.vis.append("text")
      .attr("id", "recalibrationText")
      .attr("class", "mouseLineText")
      .attr("x", self.recalibrationMousePos[0] + 5 /* As padding from the starting line */ )
      .attr("y", 15 ); //self.size.height/2
  }
  
  recalibrationText.text( txt )

  /* Draw the line to represent the mouse position for recalibration */
  if (recalibrationMousePosLines.empty())
    recalibrationMousePosLines = self.vis.append("line")
      .attr("id", "recalibrationMousePosLines")
      .attr("y1", 0)
      .attr("y2", self.size.height)
      .attr("class", "recalibrationMousePosLines")
      .style("opacity", 0.75)
      .attr("stroke-width",0.5);
   
  /* Update the mouse position line for recalibration */
  recalibrationMousePosLines.attr("x1", self.lastMouseMovePos[0])
    .attr("x2", self.lastMouseMovePos[0]);

  /* Add the background for the recalibration animation */
  if (recalibrationG.empty()) {
    recalibrationG = self.vis.append("g")
      .attr("id", "recalibrationG")
      .attr("clip-path", "url(#clip" + this.chart.id + ")");
  }

  /* Add the foreground, background, and secondary lines for recalibration animation */
  for (var i = 0; i < ((self.rawData && self.rawData.spectra) ? self.rawData.spectra.length : 0); ++i) {
    var spectrum = self.rawData.spectra[i];
    var recalibrationLine = self.vis.select("#recalibrationLine"+i);

    if (recalibrationLine.empty() && self['line'+i]) {
      recalibrationLine = recalibrationG.append("path")
        .attr("id", "recalibrationLine"+i)
        .attr("class", "rline")
        .attr("stroke", spectrum.lineColor ? spectrum.lineColor : 'black')
        .attr("d", self['line'+i](spectrum.points));
    }
  }

  if (recalibrationPeakVis.empty() && self.peakVis) {
    recalibrationPeakVis = recalibrationG.append("g")
      .attr("id", "recalibrationPeakVis")
      .attr("class", "peakVis")
      .attr("transform","translate(0,0)")
      .attr("clip-path", "url(#clip" + this.chart.id + ")");

    self.peakVis.selectAll("path").each(function() {
      path = d3.select(this);
      recalibrationPeakVis.append("path")
        .attr("class", path.attr("class"))
        .attr("d", path.attr("d"))
        .attr("fill-opacity", 0.4)
        .style("fill", path.style("fill"))
        ;
    });
  }

  recalibrationPeakVis.attr("transform", "translate(" + (self.lastMouseMovePos[0] - self.recalibrationMousePos[0]) + ",0)");

  /* Move the foreground, background, and secondary lines for recalibration animation with relation to mouse position */
  for (var i = 0; i < ((self.rawData && self.rawData.spectra) ? self.rawData.spectra.length : 0); ++i) {
    var recalibrationLine = self.vis.select("#recalibrationLine"+i);

    if (!recalibrationLine.empty())
      recalibrationLine.attr("transform", "translate(" + (self.lastMouseMovePos[0] - self.recalibrationMousePos[0]) + ",0)");
  }
}

SpectrumChartD3.prototype.handleMouseUpRecalibration = function() {
  var self = this;

  /* Handle Right-click-and-drag (for recalibrating data) */
  if (self.recalibrationMousePos) {

    /* Emit the signal here */
    if (self.isRecalibrating) {
      console.log("Emit RECALIBRATION SIGNAL from x0 = ", self.xScale(self.recalibrationStartEnergy[0]), 
        "(", self.recalibrationStartEnergy[0], " keV) to x1 = ", 
        self.lastMouseMovePos[0], "(", 
        self.xScale.invert(self.lastMouseMovePos[0]), " keV)");
      self.WtEmit(self.chart.id, {name: 'rightmousedragged'}, self.recalibrationStartEnergy[0], self.xScale.invert(self.lastMouseMovePos[0]));
    }

    self.handleCancelMouseRecalibration();
  }

  /* User is not right-click-and-dragging any more, so set this to null */
  if (self.isRecalibrating)
    self.recalibrationMousePos = null;
}

SpectrumChartD3.prototype.handleCancelMouseRecalibration = function() {
  var self = this;

  var recalibrationStartLine = self.vis.select("#recalibrationStartLine"),
      recalibrationText = self.vis.select("#recalibrationText"),
      recalibrationMousePosLines = self.vis.select("#recalibrationMousePosLines");

  var recalibrationG = self.vis.select("#recalibrationG");
  var recalibrationPeakVis = self.vis.select("#recalibrationPeakVis");

  /* Remove the right-click-and-drag initial starting point line */
  recalibrationStartLine.remove();

  /* Remove the right-click-and-drag text */
  recalibrationText.remove()

  /* Remove the peak vis */
  recalibrationPeakVis.remove();

  /* Remove all the arrow defs created */
  d3.selectAll("#recalibrationArrowDef").each(function(){
    this.remove();
  });

  /* Remove the right-click-and-drag mouse line */
  recalibrationMousePosLines.remove();

  self.isRecalibrating = false;

  /* /* User is not right-click-and-dragging any more, so set this to null */
  if (self.isRecalibrating)
    self.recalibrationMousePos = null;

  recalibrationG.remove();
}


/**
 * -------------- Delete Peak Functions --------------
 */
SpectrumChartD3.prototype.handleMouseMoveDeletePeak = function() {
  var self = this;

  var deletePeaksBox = self.vis.select("#deletePeaksBox"),
      deletePeaksText = self.vis.select("#deletePeaksText");

  d3.event.preventDefault();
  d3.event.stopPropagation();

  /* Cancel the zooming mode */
  self.handleCancelMouseZoomInX();

  /* Cancel the recalibration mode */
  self.handleCancelMouseRecalibration();

  /* Cancel the zooming in y mode */
  self.handleCancelMouseZoomY();

  /* Cancel the count gammas mode */
  self.handleCancelMouseCountGammas();

  self.handleCancelRoiDrag();
  

  if (!self.deletePeaksMouse)
    return;

  /* Adjust the mouse move position with respect to the bounds of the vis */
  if (self.lastMouseMovePos[0] < 0)
    self.lastMouseMovePos[0] = 0;
  else if (self.lastMouseMovePos[0] > self.size.width)
    self.lastMouseMovePos[0] = self.size.width;

  /* Create the erase-peaks range box and text  */
  if (deletePeaksBox.empty()) {
    deletePeaksBox = self.vis.append("rect")
      .attr("id", "deletePeaksBox")
      .attr("class","deletePeaksBox")
      .attr("width", Math.abs( self.deletePeaksMouse[0] - self.lastMouseMovePos[0] ))
      .attr("height", self.size.height)
      .attr("y", 0);

    deletePeaksText = self.vis.append("text")
      .attr("id", "deletePeaksText")
      .attr("class", "deletePeaksText")
      .attr("y", Number(deletePeaksBox.attr("height"))/2)
      .text( self.options.txt.eraseInRange );

  } else {  /* Erase-peaks range box has already been created, update it */


    /* Adjust the width of the erase peaks box */
    deletePeaksBox.attr("width", Math.abs( self.deletePeaksMouse[0] - self.lastMouseMovePos[0] ));
  }

  deletePeaksBox.attr("x", self.lastMouseMovePos[0] < self.deletePeaksMouse[0] ? self.lastMouseMovePos[0] : self.deletePeaksMouse[0])

  /* Move the erase peaks text in the middle of the erase peaks range box */
  deletePeaksText.attr("x", Number(deletePeaksBox.attr("x")) + (Number(deletePeaksBox.attr("width"))/2) - 40 );
}

/* Consolidated helper function for delete peak range calculation and emission */
SpectrumChartD3.prototype.processDeletePeakRange = function() {
  var self = this;
  
  var deletePeaksBox = self.vis.select("#deletePeaksBox");
  var deletePeaksRange;

  try {
    deletePeaksRange = [ 
      Math.min(self.xScale.invert(Number(deletePeaksBox.attr("x"))), self.xScale.invert(Number(deletePeaksBox.attr("x")) + Number(deletePeaksBox.attr("width")))), 
      Math.max(self.xScale.invert(Number(deletePeaksBox.attr("x"))), self.xScale.invert(Number(deletePeaksBox.attr("x")) + Number(deletePeaksBox.attr("width")))) 
      ];

    console.log("Emit ERASE PEAKS SIGNAL FROM ", deletePeaksRange[0], "keV to ", deletePeaksRange[1], " keV" );
    self.WtEmit(self.chart.id, {name: 'shiftkeydragged'}, deletePeaksRange[0], deletePeaksRange[1]);
    return true;

  } catch (TypeError) { /* For some reason, a type error is (seldom) returned when trying to access "x" attribute of deletePeaksBox, doesn't affect overall functionality though */
    return false;
  }
};

SpectrumChartD3.prototype.handleMouseUpDeletePeak = function() {
  var self = this;

  if (!self.processDeletePeakRange()) {
    return;
  }

  self.handleCancelMouseDeletePeak();
}

/* Consolidated helper function for canceling peak deletion */
SpectrumChartD3.prototype.cancelDeletePeak = function() {
  var self = this;

  var deletePeaksBox = self.vis.select("#deletePeaksBox"),
      deletePeaksText = self.vis.select("#deletePeaksText");

  /* Delete the erase peaks box since we are not erasing peaks anymore */
  deletePeaksBox.remove();

  /* Delete the erase peaks text since we are not erasing peaks anymore */
  deletePeaksText.remove();

  /* We are not erasing peaks anymore */
  self.isDeletingPeaks = false;
};

SpectrumChartD3.prototype.handleCancelMouseDeletePeak = function() {
  this.cancelDeletePeak();
}

SpectrumChartD3.prototype.handleTouchMoveDeletePeak = function(t) {
  const self = this;

  if (t.length !== 2) {
    self.handleCancelTouchDeletePeak();
    return;
  }

  /* Cancel the count gammas mode */
  self.handleCancelTouchCountGammas();

  /* Cancel the create peaks mode */
  self.handleCancelTouchPeakFit();

  /* Cancel the zoom-in y mode */
  self.handleTouchCancelZoomY();

  var deletePeaksBox = self.vis.select("#deletePeaksBox"),
      deletePeaksText = self.vis.select("#deletePeaksText");

  var leftTouch = t[0][0] < t[1][0] ? t[0] : t[1],
      rightTouch = leftTouch === t[0] ? t[1] : t[0];

  const leftTouchX = Math.min(leftTouch[0], self.xScale.range()[1]);
  const rightTouchX = Math.min(rightTouch[0], self.xScale.range()[1]);
  const width = Math.abs( rightTouchX - leftTouchX );

  // Note: we are not testing if touches are in our tagret - I think we can assume we are, if we are here.

  /* Create the erase-peaks range box and text  */
  if (deletePeaksBox.empty()) {
    deletePeaksBox = self.vis.append("rect")
      .attr("id", "deletePeaksBox")
      .attr("class","deletePeaksBox")
      .attr("height", self.size.height)
      .attr("y", 0);

    deletePeaksText = self.vis.append("text")
      .attr("id", "deletePeaksText")
      .attr("class", "deletePeaksText")
      .attr("y", Number(deletePeaksBox.attr("height"))/2);

  }

  /* Adjust the positioning and width of the delete peaks box */
  deletePeaksBox.attr("x", leftTouchX)
    .attr("width", width);


  /* Move the erase peaks text in the middle of the erase peaks range box */
  deletePeaksText.text(width > 0 ? self.options.txt.eraseInRange : "");
  deletePeaksText.attr("x", Number(deletePeaksBox.attr("x")) + (Number(deletePeaksBox.attr("width"))/2) - Number(deletePeaksText[0][0].clientWidth)/2 );
}

SpectrumChartD3.prototype.handleTouchEndDeletePeak = function() {
  var self = this;

  if (!self.processDeletePeakRange()) {
    return;
  }

  self.handleCancelTouchDeletePeak();
}

SpectrumChartD3.prototype.handleCancelTouchDeletePeak = function() {
  this.cancelDeletePeak();
}


/**
 * -------------- Count Gammas Functions --------------
 */

SpectrumChartD3.prototype.gammaIntegral = function(spectrum, lowerX, upperX) {
  let self = this;
  var sum = 0.0;
  
  if( !spectrum || !spectrum.x || !spectrum.y )
    return sum;
  
  var bounds = self.min_max_x_values();
  var maxX = bounds[1];
  var minX = bounds[0];
  
  lowerX = Math.min( maxX, Math.max(lowerX, minX) );
  upperX = Math.max( minX, Math.min(upperX, maxX) );
  
  if (lowerX == upperX)
    return sum;
  
  if (lowerX > upperX) {  /* swap the two values */
    upperX = [lowerX, lowerX = upperX][0];
  }
  
  var maxChannel = spectrum.x.length - 1;
  var lowerChannel = d3.bisector(function(d){return d;}).left(spectrum.x,lowerX,1) - 1;
  var upperChannel = d3.bisector(function(d){return d;}).left(spectrum.x,upperX,1) - 1;
  
  var lowerLowEdge = spectrum.x[lowerChannel];
  var lowerBinWidth = lowerChannel < maxChannel ? spectrum.x[lowerChannel+1] - spectrum.x[lowerChannel]
                                                : spectrum.x[lowerChannel] - spectrum.x[lowerChannel-1];
  var lowerUpEdge = lowerLowEdge + lowerBinWidth;
  
  if (lowerChannel === upperChannel) {
    var frac = (upperX - lowerX) / lowerBinWidth;
    //console.log("lowerChannel == upper channel, counts = ", frac * spectrum.y[lowerChannel]);
    return frac * spectrum.y[lowerChannel];
  }
  
  var fracLowBin = (lowerUpEdge - lowerX) / lowerBinWidth;
  sum += fracLowBin * spectrum.y[lowerChannel];
  
  var upperLowEdge = spectrum.x[upperChannel];
  var upperBinWidth = upperChannel < maxChannel ? spectrum.x[upperChannel+1] - spectrum.x[upperChannel]
                                                : spectrum.x[upperChannel] - spectrum.x[upperChannel-1];
  var fracUpBin = (upperX - upperLowEdge) / upperBinWidth;
  sum += fracUpBin * spectrum.y[upperChannel];
  
  
  for (var channel = lowerChannel + 1; channel < upperChannel; channel++) {
    sum += spectrum.y[channel];
  }
  
  return sum;
}


SpectrumChartD3.prototype.handleTouchEndCountGammas = function() {
  const self = this;
  
  const countGammasBox = self.vis.select("#countGammasBox");
  if( !countGammasBox )
    return;
  
  try {
    let countGammasRange = [
    Math.min(self.xScale.invert(Number(countGammasBox.attr("x"))), self.xScale.invert(Number(countGammasBox.attr("x")) + Number(countGammasBox.attr("width")))),
    Math.max(self.xScale.invert(Number(countGammasBox.attr("x"))), self.xScale.invert(Number(countGammasBox.attr("x")) + Number(countGammasBox.attr("width"))))
    ];
    
    console.log("Emit COUNT GAMMAS SIGNAL FROM ", countGammasRange[0], "keV to ", countGammasRange[1], " keV" );
    self.WtEmit(self.chart.id, {name: 'shiftaltkeydragged'}, countGammasRange[0], countGammasRange[1]);
    
  } catch (TypeError) { /* For some reason, a type error is (seldom) returned when trying to access "x" attribute of countGammasBox, doesn't affect overall functionality though */
    return;
  }
  
  self.handleCancelTouchCountGammas();
}

/** Updates sum numbers as the the mouse or fingers move, while highlighting a region of data to sum
 (e.g. option+shift+drag, or two fingers vertical+drag)
 */
SpectrumChartD3.prototype.updateGammaSum = function() {
  var self = this

  const pos = self.getMousePos();
  if( !pos ){
    console.log( "updateGammaSum: failed to get mouse/touch pos" );
    return;
  }
  
  if( !self.rawData || !self.rawData.spectra || !self.rawData.spectra.length )
    return;
  
  let isMouseEvent = true;
  if( d3.event ){
    d3.event.preventDefault();
    d3.event.stopPropagation();
    const t = d3.event.changedTouches;
    const vt = t ? d3.touches(this.vis[0][0], t) : null;
    if( vt )
    isMouseEvent = false;
  }else if( !self.lastMouseMovePos && this.lastTapEvent ){
    isMouseEvent = false;
  }
  
  let startx_px, nowx_px;
  if( isMouseEvent ){
    self.handleCancelMouseZoomInX();
    self.handleCancelMouseRecalibration();
    self.handleCancelMouseZoomY();
    self.handleCancelMouseDeletePeak();
    
    if (!self.countGammasMouse || !self.lastMouseMovePos )
    return;
    
    startx_px = self.countGammasMouse[0];
    nowx_px = self.lastMouseMovePos[0];
    if( startx_px > nowx_px )
      [startx_px, nowx_px] = [nowx_px, startx_px];
  }else{
    self.handleCancelTouchDeletePeak();
    self.handleCancelTouchPeakFit();
    self.handleTouchCancelZoomY();
    
    const t = d3.touches(self.vis[0][0]);
    const startT = self.countGammasStartTouches;
    console.assert( !startT || (startT.length === 2) );
    
    if( (t.length !== 2) || !startT || (startT.length !== 2) ) {
      self.handleCancelTouchCountGammas();
      return;
    }
    
    const x_px = [startT[0][0], startT[1][0], t[0][0], t[1][0]];
    startx_px = Math.min.apply(Math, x_px);
    nowx_px = Math.max.apply(Math, x_px);
  }
  
  
  /* Adjust the mouse move position with respect to the bounds of the vis */
  if (startx_px < 0)
    startx_px = 0;
  if( nowx_px > self.size.width )
    nowx_px = self.size.width;
  
  const xdomain = self.xScale.domain();
  
  const sumEnergyRange = [
    Math.max(self.xScale.invert(startx_px), xdomain[0]),
    Math.min(self.xScale.invert(nowx_px), xdomain[1])
  ];
  
  var countGammasBox = self.vis.select("#countGammasBox"),
  countGammasText = self.vis.select("#countGammasText"),
  countGammaRangeText = self.vis.select("#countGammaRangeText"),
  sigmaCount = self.vis.select("#sigmaCount");
  
  
  /* Create the yellow box and text associated with it */
  if (countGammasBox.empty()) {
    countGammasBox = self.vis.append("rect")
    .attr("id", "countGammasBox")
    .attr("width", Math.abs( startx_px - nowx_px ))
    .attr("height", self.size.height)
    .attr("y", 0);
  } else {
    /* Adjust the width of the erase peaks box */
    countGammasBox.attr("width", Math.abs( startx_px - nowx_px ));
  }
  
  if (countGammasText.empty()) {
    countGammasText = self.vis.append("text")
    .attr("id", "countGammasText")
    .attr("class", "countGammasText")
    .attr("y", Number(countGammasBox.attr("height"))/2)
    .text( self.options.txt.gammaCounts );
  }
  
  var ypos = Number(countGammasBox.attr("height"))/2 + 15;   /* signifies the y-position of the text displayed */
  
  countGammasBox.attr("class", "countGammasBoxForward")
    .attr("x", nowx_px < startx_px ? nowx_px : startx_px);
    countGammasText.text( self.options.txt.gammaCounts );
    
  if (countGammaRangeText.empty())
    countGammaRangeText = self.vis.append("text")
    .attr("id", "countGammaRangeText")
    .attr("class", "countGammasText")
    .attr("y", ypos);
    
  countGammaRangeText.text( self.options.txt.sumFromTo.replace("{1}", sumEnergyRange[0].toFixed(1)).replace("{2}", sumEnergyRange[1].toFixed(1)) );
  countGammaRangeText.attr("x", Number(countGammasBox.attr("x")) + (Number(countGammasBox.attr("width"))/2) - 30 );
  
  ypos += 15;
  
  /* Move the count gammas text in the middle of the count gammas box */
  countGammasText.attr("x", Number(countGammasBox.attr("x")) + (Number(countGammasBox.attr("width"))/2) - 30 );
  
  /* Display the count gammas text for all the spectrum */
  var nforeground, nbackground, backSF;
  var nsigma = 0, isneg;
  var asterickText = "";
  var rightPadding = 50;
  var specialScaleSpectras = [];
  self.rawData.spectra.forEach(function(spectrum, i) {   /* TODO: May need to change processing of */
    if (!spectrum)
    return;
    
    /* Get information from the spectrum */
    var spectrumSelector = 'Spectrum-' + spectrum.id;
    var spectrumCountsText = self.vis.select("#" + spectrumSelector + "CountsText");
    var spectrumScaleFactor = spectrum.yScaleFactor;
    var nspectrum = self.gammaIntegral(spectrum, sumEnergyRange[0], sumEnergyRange[1]);
    var spectrumGammaCount = Number((spectrumScaleFactor * nspectrum).toFixed(2));
    var countsText;
    
    /* Save information for the foreground and background (for sigma comparison) */
    if (spectrum.type === self.spectrumTypes.FOREGROUND)
    nforeground = nspectrum;
    else if (spectrum.type === self.spectrumTypes.BACKGROUND) {
      nbackground = nspectrum;
      backSF = spectrumScaleFactor;
    }
    
    /* Get the text to be displayed from the spectrum information */
    if (spectrumScaleFactor != null && spectrumScaleFactor !== -1)
      countsText = spectrum.title + ": " + spectrumGammaCount.toFixed(2);
    if (spectrumScaleFactor != 1) {
      asterickText += "*";
      if (countsText)
      countsText += asterickText;
      specialScaleSpectras.push( asterickText + self.options.txt.afterScalingBy + spectrumScaleFactor.toFixed(3) );
    }
    
    /* Output the count gammas information to the chart */
    if (countsText) {
      if (spectrumCountsText.empty())
      spectrumCountsText = self.vis.append("text")
      .attr("id", spectrumSelector + "CountsText")
      .attr("class", "countGammasText")
      .attr("y", ypos);
      spectrumCountsText.text(countsText);
      spectrumCountsText.attr("x", Number(countGammasText.attr("x")) - rightPadding );
      ypos += 15;
      
    } else {
      spectrumCountsText.remove();
    }
  });
  
  /* Get proper information for foreground-background sigma comparison */
  if (nforeground && nbackground && backSF) {
    const backSigma = backSF * Math.sqrt(nbackground);
    const sigma = Math.sqrt( backSigma*backSigma + nforeground );  //uncerFore = sqrt(nforeground) since foreground always scaled by 1.0
    nsigma = backSigma == 0 ? 0 : (Number((Math.abs(nforeground - backSF*nbackground) / sigma).toFixed(3)));
    isneg = ((backSF*nbackground) > nforeground);
  }
  
  /* Output foreground-background sigma information if it is available */
  if (nsigma > 0) {
    if (sigmaCount.empty())
    sigmaCount = self.vis.append("text")
    .attr("id", "sigmaCount")
    .attr("class", "countGammasText")
    .attr("y", ypos);
    
    sigmaCount.attr("x", Number(countGammasText.attr("x")) - rightPadding + 10)
    .text( (isneg ? self.options.txt.foreNSigmaBelowBack : self.options.txt.foreNSigmaAboveBack).replace("{1}", String(nsigma.toFixed(2))) );

    ypos += 15;
    
  } else if (!sigmaCount.empty()) {
    sigmaCount.remove();
  }
  
  /* Output all the corresponding asterick text with each spectrum (if scale factor != 1) */
  specialScaleSpectras.forEach(function(string, i) {
    if (string == null || !string.length)
    return;
    var stringnode = self.vis.select("#asterickText" + i);
    
    if (stringnode.empty())
    stringnode = self.vis.append("text")
    .attr("id", "asterickText"+i)
    .attr("class", "countGammasText asterickText")
    .text(string);
    stringnode.attr("x", Number(countGammasBox.attr("x")) + (Number(countGammasBox.attr("width"))/2) + rightPadding/3)
    .attr("y", ypos);
    ypos += 15;
  });
  
  /* Hide all the count gamma text when the mouse box is empty */
  if (Number(countGammasBox.attr("width")) == 0)
    d3.selectAll(".countGammasText").attr("fill-opacity", 0);
  else
    d3.selectAll(".countGammasText").attr("fill-opacity", 1);
}//SpectrumChartD3.prototype.updateGammaSum

SpectrumChartD3.prototype.handleMouseUpCountGammas = function() {
  var self = this;

  var countGammasBox = self.vis.select("#countGammasBox");

  try {
    let countGammasRange = [ 
      Math.min(self.xScale.invert(Number(countGammasBox.attr("x"))), self.xScale.invert(Number(countGammasBox.attr("x")) + Number(countGammasBox.attr("width")))), 
      Math.max(self.xScale.invert(Number(countGammasBox.attr("x"))), self.xScale.invert(Number(countGammasBox.attr("x")) + Number(countGammasBox.attr("width")))) 
      ];

    if (self.lastMouseMovePos[0] < self.countGammasMouse[0]) {
      console.log("Emit REMOVE GAMMA COUNT SIGNAL FROM ", countGammasRange[0], "keV to ", countGammasRange[1], " keV" );
    } else {
      console.log("Emit COUNT GAMMAS SIGNAL FROM ", countGammasRange[0], "keV to ", countGammasRange[1], " keV" );
    }

    self.WtEmit(self.chart.id, {name: 'shiftaltkeydragged'}, countGammasRange[0], countGammasRange[1]);
  } catch (TypeError) { /* For some reason, a type error is (seldom) returned when trying to access "x" attribute of countGammasBox, doesn't affect overall functionality though */
    return;
  }

  self.handleCancelMouseCountGammas();
}

SpectrumChartD3.prototype.handleCancelMouseCountGammas = function() {
  var self = this;

  var countGammasBox = self.vis.select("#countGammasBox");

  /* Delete the count gammas box since we are not counting gammas anymore */
  countGammasBox.remove();

  /* Delete the count gamma texts since we are not counting gammas anymore */
  self.vis.selectAll(".countGammasText").remove();

  /* We are not erasing peaks anymore */
  self.isCountingGammas = false;
}



SpectrumChartD3.prototype.handleCancelTouchCountGammas = function() {
  /* Delete the count gammas box since we are not counting gammas anymore */
  this.vis.select("#countGammasBox").remove();

  /* Delete the count gamma texts since we are not counting gammas anymore */
  d3.selectAll(".countGammasText").each(function () {
    d3.select(this).remove();
  });
}


/** -------------- Peak Info and Display Functions -------------- */
SpectrumChartD3.prototype.handleMouseOverPeak = function( peakElem ) {
  this.highlightPeak(peakElem,true);
  // self.displayPeakInfo(info, d3.event.x);
}

SpectrumChartD3.prototype.handleMouseOutPeak = function(peakElem, highlightedPeak, paths) {
  /* Returns true if a node is a descendant (or is) of a parent node. */
  function isElementDescendantOf(parent, node) {
    while (node != null) {
      if (node == parent) return true;
      node = node.parentNode;
    }
    return false;
  }
  
  if (this.peakInfo && isElementDescendantOf(this.peakInfo.node(), d3.event.toElement)) {
    return this.handleMouseMovePeak()();
  }

  this.unhighlightPeak(highlightedPeak);
}

SpectrumChartD3.prototype.handleMouseMovePeak = function() {
  var self = this;

  return function() {
    const event = d3.event;

    if (self.peakInfo) {
      const x = event.x;
      const box = self.peakInfoBox;
      self.peakInfo.attr("transform")
        .replace("translate(", "")
        .replace(")","")
        .split(',')[0];

      const shouldMovePeakInfoLeft = x >= box.x && x <= box.x + box.width;

      self.peakInfo.attr("transform","translate(" 
        + (shouldMovePeakInfoLeft ? (self.padding.leftComputed + 115) : self.size.width) 
        + "," + (self.size.height - 40) + ")");
    }
  }
}

SpectrumChartD3.prototype.getPeakInfoObject = function(roi, energy, spectrumIndex) {
  var self = this;

  // No ROI given, so skip
  if (!roi) 
    return null;

  let peak;
  let minDistance = Infinity;

  roi.peaks.forEach(function(roiPeak) {
    const distance = Math.abs(energy - roiPeak.Centroid[0]);

    if (distance < minDistance) {
      minDistance = distance;
      peak = roiPeak;
    }
  });

  // No peak found, so skip
  if (!peak) 
    return null;

  const lowerEnergy = roi.lowerEnergy;
  const upperEnergy = roi.upperEnergy;
  const roiSumCounts = ((typeof roi.roiCounts) === 'number') ? roi.roiCounts : null;
  

  const mean = peak.Centroid[0].toFixed(2);
  let fwhm = 2.35482 * peak.Width[0];
  fwhm = fwhm.toFixed( ((fwhm < 10) ? ((fwhm < 1) ? 4 : 3) : 2) );
  const fwhmPerc = (235.482 * peak.Width[0] / peak.Centroid[0]).toFixed(2);
  const chi2 = peak.Chi2[0].toFixed(2);
  const area = peak.Amplitude[0].toFixed(1);
  const areaUncert = peak.Amplitude[1].toFixed(1);

  let nuc = null;
  //nuclide: {name: "Eu152", decayParent: "Eu152", decayChild: "Sm152", energy: 1408.01}
  //xray: {name: "Uranium", energy: 114.8440 }
  //reaction: {name: "Fe(n,g)", energy: 4996.23, type: "D.E."}
  let nucinfo = peak.nuclide ? peak.nuclide : (peak.xray ? peak.xray : (peak.reaction ? peak.reaction : null));
  
  if( nucinfo && nucinfo.name ){
    nuc = nucinfo.name + " (";
    if( nucinfo.decayParent && (nucinfo.decayParent !== nucinfo.name) )
      nuc = nuc + nucinfo.decayParent + ", ";
    if( nucinfo.energy )
      nuc = nuc + nucinfo.energy.toFixed(2) + " keV";
    if( nucinfo.type )
      nuc = nuc + " " + nucinfo.type;
    nuc = nuc + ")";
  }
  
  // Note: info.roiCounts and info.cpsTxt added to InterSpec 20201129, should deprecate contArea
  const contArea = self.offset_integral(roi,lowerEnergy, upperEnergy).toFixed(1);
  
  const info = {
    mean: mean, 
    fwhm: fwhm, 
    fwhmPerc: fwhmPerc, 
    chi2: chi2, 
    area: area, 
    areaUncert: areaUncert,
    roiCounts: roiSumCounts,
    cpsTxt: (((typeof peak.cpsTxt) === 'string') ? peak.cpsTxt : null),
    contArea: contArea, 
    spectrumIndex: spectrumIndex,
    nuclide: nuc
  };
  return info;
}

SpectrumChartD3.prototype.updatePeakInfo = function() {
  var self = this;

  if (!self.rawData || !self.rawData.spectra || !self.rawData.spectra.length)
    return;

  const x = d3.mouse(self.vis.node())[0];
  const energy = self.xScale.invert(x);
  let resultROI;
  let spectrumIndex;

  // Find a peak that our mouse energy point is overlapping with
  self.rawData.spectra.forEach(function(spectrum, i) {
    if (resultROI) // we found a peak already, skip the rest
      return;
    if (!self.options.drawPeaksFor[spectrum.type])
      return;

    spectrum.peaks.forEach(function(peak, j) {
      if (!resultROI && peak.lowerEnergy <= energy && energy <= peak.upperEnergy) {  // we haven't found a peak yet
        resultROI = peak;
        spectrumIndex = i;
      }
    });
  });
  
  // No peak found, so hide the info box
  if (!resultROI) {
    self.hidePeakInfo();
    return;
  } 
  
  const info = self.getPeakInfoObject(resultROI, energy, spectrumIndex);
  self.displayPeakInfo(info);
}

SpectrumChartD3.prototype.displayPeakInfo = function(info) {
  var self = this;

  function createPeakInfoText(text, label, value) {
    let span = text.append("tspan")
      .attr('class', "peakInfoLabel")
      .attr('x', "-13.5em")
      .attr('dy', "-1em")
      .text(label + ': ');
    span.append("tspan")
      .style('font-weight', "normal")
      .text( value );
  }

  const areMultipleSpectrumPeaksShown = self.areMultipleSpectrumPeaksShown();
  let x = d3.event.clientX;

  self.hidePeakInfo();

  let boxy = -6.1, boxheight = 5.5;
  
  if( areMultipleSpectrumPeaksShown ){
    boxy -= 1;
    boxheight += 1;
  }
  
  if( info.cpsTxt ){
    boxy -= 1;
    boxheight += 1;
  }
  
  if( info.nuclide ){
    boxy -= 1;
    boxheight += 1;
  }
  
  self.peakInfo = self.vis.append("g")
    .attr("class", "peakInfo")
    .attr("transform","translate(" + self.size.width + "," + (self.size.height - 40) + ")");

  var rect = self.peakInfo.append('rect')
    .attr("class", "peakInfoBox")
    .attr('height', boxheight + "em")
    .attr('x', "-14em")
    .attr('y', boxy + "em")
    .attr('rx', "5px")
    .attr('ry', "5px");

  var text = self.peakInfo.append("g")
    .append("text")
    .attr("dy", "-2em");
  
  if( info.nuclide ){
    text.append("tspan")
        .style('font-weight', "normal")
        .attr('x', "-13.5em")
        .attr('dy', "-1em")
        .text( info.nuclide );
  }
  
  // Note: info.roiCounts and info.cpsTxt added to InterSpec 20201129, should deprecate contArea
  if( (typeof info.roiCounts) === 'number' )
    createPeakInfoText(text, self.options.txt.roiCounts, info.roiCounts);
  else
    createPeakInfoText(text, self.options.txt.contArea, info.contArea);
  
  if( info.cpsTxt )
    createPeakInfoText(text, self.options.txt.peakCps, info.cpsTxt);
  
  createPeakInfoText(text, self.options.txt.peakArea, info.area + ((info.areaUncert > 0.0) ? (String.fromCharCode(0x00B1) + info.areaUncert) : "") );
  createPeakInfoText(text, String.fromCharCode(0x03C7) + "2/dof", info.chi2);
  createPeakInfoText(text, self.options.txt.fwhm, info.fwhm + " keV (" + info.fwhmPerc + "%)");
  createPeakInfoText(text, self.options.txt.mean, info.mean + " keV");
  
  if (areMultipleSpectrumPeaksShown)
    createPeakInfoText(text, self.options.txt.spectrum, self.rawData.spectra[info.spectrumIndex].title);
    
  const width = text.node().getBoundingClientRect().width + 10; // + 10 for padding right
  rect.attr('width', width);

  self.peakInfoBox = self.peakInfo.node().getBoundingClientRect();

  if (x >= (self.peakInfoBox.x ? self.peakInfoBox.x : self.peakInfoBox.left))
    self.peakInfo.attr("transform","translate(" + (self.padding.leftComputed + 115) + "," + (self.size.height - 40) + ")");
}

SpectrumChartD3.prototype.hidePeakInfo = function() {
  var self = this;

  if (!self.peakInfo) return;

  self.peakInfo.remove();
  self.peakInfo = null;
}

/**
 @param peakElem The HTML peak path that
 */
SpectrumChartD3.prototype.highlightPeak = function( peakElem, highlightLabelTo ) {
  var self = this;

  if( self.zooming_plot || !peakElem || self.leftMouseDown || self.rightClickDown )
    return;

  if( self.highlightedPeak )
    self.unhighlightPeak(null);
    
  var peak = d3.select(peakElem);
  if( Array.isArray(peak[0][0]) )
    peak = peakElem;

  if( peak )
    peak.attr("stroke-width",2);
  
  self.highlightedPeak = peakElem;
  
  if( !highlightLabelTo )
    return;
  
  //Dont waste time selecting for label elements if there is no possibility of having them - not sure this saves anything...
  if( !this.options.showUserLabels && !this.options.showPeakLabels && !this.options.showNuclideNames )
    return;
  
  if( peakElem.dataset && peakElem.dataset.energy ){
    self.peakVis.select('text[data-peak-energy="' + peakElem.dataset.energy + '"].peaklabel').each( function(){
      self.highlightLabel(this,true);
    });
  }else{
    console.error( 'SpectrumChartD3::highlightPeak: peakElem does have expected attributes:', peakElem );
  }
}//SpectrumChartD3.prototype.highlightPeak = ...


/** Highlights a peak -specified by energy, as if you had moused over it. */
SpectrumChartD3.prototype.highlightPeakAtEnergy = function(energy) {
  const self = this;
  this.peakVis.select('path[data-energy="' + energy.toFixed(2) + '"]').each( function(){
    self.highlightPeak( this, true );
  });
}//SpectrumChartD3.prototype.highlightPeakAtEnergy


SpectrumChartD3.prototype.unhighlightPeak = function(highlightedPeak) {
  var self = this;

  if( !highlightedPeak )
    highlightedPeak = self.highlightedPeak;
  
  if( !highlightedPeak )
    return;
    
  if( !Array.isArray(highlightedPeak) )
    highlightedPeak = d3.select(highlightedPeak);
  
  highlightedPeak.attr("stroke-width",1);
  self.highlightedPeak = null;
  
  if( self.highlightedLabel )
    self.unHighlightLabel(false);
}


/* Highlight a peaks label.
 
 This means:
 - Label becomes bold
 - A line is drawn from the label to its corresponding peak
 
 @param labelEl The HTML the <text> element of the label to highlight.
 @param isFromPeakBeingHighlighted Boolean telling whether we are highlighting the
        label because the peak is being moused-over, or the label is being moused-over.
        If label is being moused over, we will also highlight the peak, and make a
        line that goes from the label to middle of peak.  If its the peak being
        moused over, we wont highlight the peak (because that is already being done),
        and we will draw a line to either the top or bottom of peak (whichever is closer)
 */
SpectrumChartD3.prototype.highlightLabel = function( labelEl, isFromPeakBeingHighlighted ) {
  const self = this;
  
  if( self.highlightedLabel === labelEl )
    return;
    
  if( self.highlightedLabel )
    self.unHighlightLabel(true);
  
  if( self.dragging_plot )
    return;
  
  // Bold the label text and add a line (arrow) that points to the peak when moused over text.
  self.highlightedLabel = labelEl;
  
  let thislabel = d3.select(labelEl);
  thislabel.attr("class", "peaklabel peakLabelBold");
  
  // We will be appending the line connecting the label to the peak, to the `self.peakVis` <g>
  //  element, which is a child of `self.vis` that has a transform of "translate(left-pad,top-pad)".
  //  Also, the <text> element may have a rotation applied.
  //  So we need to transform the the unrotated text position into the <svg> coordinates, then
  //  subtract the translation of `self.vis`, to get the lines end-point.
  const svgCtm = thislabel.node().getCTM();  // Get the transformation matrix (CTM) of the text element
  const visCtm = self.peakVis.node().getCTM(); // Get x-form for self.peakVis, which we need coordinates relative to
  
  const fromTxtToVisCoords = function(x,y){
    const pt = self.svg.node().createSVGPoint(); //TODO: createSVGPoint() is depreciated
    pt.x = x;
    pt.y = y;
    const svgPoint = pt.matrixTransform(svgCtm); // Apply the transformation matrix to the point
    return svgPoint.matrixTransform( visCtm.inverse() ); // Get coordinates relative to self.peakVis
  };
  
  const labelbbox = thislabel.node().getBBox(); // This is untranslated/unrotated
  
  const labelEndpoints = [ fromTxtToVisCoords( 0, 0 ), //bottom-left of text
    fromTxtToVisCoords( 0, -0.5*labelbbox.height ), //mid-height-left of text
    fromTxtToVisCoords( 0.5*labelbbox.width, 0 ), //bottom, middle-x of text
    fromTxtToVisCoords( labelbbox.width, 0 ), //bottom right of text
    fromTxtToVisCoords( labelbbox.width, -0.5*labelbbox.height ), //mid-height-right of text
    fromTxtToVisCoords( labelbbox.width, -labelbbox.height ), //top-right of text
    fromTxtToVisCoords( 0.5*labelbbox.width, -labelbbox.height ), //top middle-x of text
    fromTxtToVisCoords( 0, -labelbbox.height ) //top-left of text
  ];
  
  const visPt = labelEndpoints[0];
  
  // Visualize text extent, for debugging
  //for( let i = 0; i < labelEndpoints.length; i += 1 )
  //  self.peakVis.append('circle').attr('cx', labelEndpoints[i].x ).attr('cy', labelEndpoints[i].y ).attr('r', 1).style('fill', 'green');
  
  const x2 = labelEl.dataset.peakXPx;  //attribute 'data-peak-x-px' value
  let y2 = 0.5*(parseFloat(labelEl.dataset.peakLowerYPx) + parseFloat(labelEl.dataset.peakUpperYPx));
  if( isFromPeakBeingHighlighted )
    y2 = (visPt.y > labelEl.dataset.peakLowerYPx) ? labelEl.dataset.peakLowerYPx : labelEl.dataset.peakUpperYPx;
  
  // Pick x1 and x2 to be the shortest points to x2 and y2.
  let x1 = labelEndpoints[0].x, y1 = labelEndpoints[0].y, prevdist;
  for( let i = 0; i < labelEndpoints.length; i += 1 ){
    const x_0 = labelEndpoints[i].x, y_0 = labelEndpoints[i].y;
    const thisdist = Math.sqrt( (x_0 - x2)*(x_0 - x2) + (y_0 - y2)*(y_0 - y2) );
    if( (i===0) || thisdist < prevdist )
    {
      prevdist = thisdist;
      x1 = x_0;
      y1 = y_0;
    }
  }
  
  //Only draw line between label and peak if it will be at least 10 pixels.
  if( Math.sqrt( Math.pow(x1-x2,2) + Math.pow(y1-y2,2) ) > 10 )
    self.peakLabelLine = self.peakVis.append('line')
        .attr('class', 'peaklabelline')
        .attr('x1', x1)
        .attr('y1', y1)
        .attr('x2', x2)
        .attr('y2', y2)
        .attr("marker-end", "url(#triangle)");
  
  if( isFromPeakBeingHighlighted )
    return;
  
  //Highlight peak corresponding to this label
  self.peakVis.select('path[data-energy="' + labelEl.dataset.peakEnergy + '"].peakFill').each( function(){
    self.highlightPeak(this,false);
  });
}//function highlightLabel()

/** Un-highlight the currently highlighted peak (pointed to by self.highlightedLabel)
 
 @param unHighlightPeakTo Boolean describing if the peak cooresponding to this
        label should also be un-highlighted.
 */
SpectrumChartD3.prototype.unHighlightLabel = function( unHighlightPeakTo ) {
  let self = this;
  
  if( !self.highlightedLabel )
    return;
  
  if( unHighlightPeakTo ){
    const peakEnergy = self.highlightedLabel.dataset.peakEnergy;
    self.peakVis.select('path[data-energy="' + peakEnergy + '"].peakFill').each( function(){
      self.unhighlightPeak(this);
    });
  }
  
  /* Return label back to original style on mouse-out. */
  d3.select(self.highlightedLabel).attr('class', 'peaklabel');
  
  /* delete the pointer line from the label to the peak */
  if( self.peakLabelLine ) {
    self.peakLabelLine.remove();
    self.peakLabelLine = null;
  }
  
  self.highlightedLabel = null;
}//function unHighlightLabel(...)



/**
 * -------------- Background Subtract Functions --------------
 */
SpectrumChartD3.prototype.setBackgroundSubtract = function( subtract ) {
  this.options.backgroundSubtract = Boolean(subtract);
  this.redraw()();
}

SpectrumChartD3.prototype.setAllowDragRoiExtent = function( allow ){
  this.options.allowDragRoiExtent = Boolean(allow);
}


SpectrumChartD3.prototype.rebinForBackgroundSubtract = function() {
  var self = this;

  // Don't do anything if no data exists
  if (!self.rawData || !self.rawData.spectra || !self.rawData.spectra.length)
    return;

  // Don't do anything else if option is not toggled
  if (!self.options.backgroundSubtract)
    return;
  
  var bisector = d3.bisector(function(point) { return point.x });

  self.rawData.spectra.forEach(function(spectrum) {
    
    if( 'bgsubtractpoints' in spectrum )
      delete spectrum.bgsubtractpoints;
    
    // We don't need to generate the points for background subtract for a background spectrum
    if (spectrum.type === self.spectrumTypes.BACKGROUND) return;

    const background = self.getSpectrumByID(spectrum.backgroundID);

    // Don't add any points if there is no associated background with this spectrum
    if (!background) return;

    // Initialize background subtract points for this spectrum to be empty
    spectrum.bgsubtractpoints = [];
    
    // Get points for background subtract for this spectrum by getting the nearest background point and subtracting y-values
    spectrum.bgsubtractpoints = spectrum.points.map(function(point) {
      const x = point.x;
      const y = point.y;
      const bpoint = background.points[bisector.left(background.points, x)];
      if (!bpoint) return { x: x, y: 0 };
      return { x: x, y: Math.max(0, y - bpoint.y) };
    });
  });
}



/**
 * -------------- Helper Functions --------------
 */
/**
 * Returns true if the browser has touch capabilities, false otherwise.
 * Thanks to: https://stackoverflow.com/questions/4817029/whats-the-best-way-to-detect-a-touch-screen-device-using-javascript
 */
SpectrumChartD3.prototype.isTouchDevice = function() {
  return 'ontouchstart' in window        // works on most browsers 
      || (navigator && navigator.maxTouchPoints);       // works on IE10/11 and Surface
}

SpectrumChartD3.prototype.isWindows = function() {
  return (navigator.platform && (navigator.platform.indexOf('Win') === 0)) || (navigator.userAgent.indexOf('Windows') !== -1);
}

/**
 */
SpectrumChartD3.prototype.areMultipleSpectrumPeaksShown = function() {
  var self = this;

  if (!self.rawData || !self.rawData.spectra || !self.rawData.spectra.length)
    return false;

  let numberOfSpectraPeaks = 0;

  self.rawData.spectra.forEach(function(spectrum) {
    if (spectrum.peaks && spectrum.peaks.length && self.options.drawPeaksFor[spectrum.type])
      numberOfSpectraPeaks++;
  });

  return numberOfSpectraPeaks > 1;
}

/**
 * Returns the spectrum with a given ID. If no spectrum is found, then null is returned.
 * Assumes valid data, so if there are multiple spectra with same ID, then the first one is returned.
 */
SpectrumChartD3.prototype.getSpectrumByID = function(id) {
  var self = this;

  if (!self.rawData || !self.rawData.spectra || !self.rawData.spectra.length)
    return;

  for (let i = 0; i < self.rawData.spectra.length; i++) {
    if (self.rawData.spectra[i].id === id)
      return self.rawData.spectra[i];
  }

  return null;
}

/**
 * Returns a list of all spectra title names.
 */
SpectrumChartD3.prototype.getSpectrumTitles = function() {
  var self = this;

  if (!self.rawData || !self.rawData.spectra || !self.rawData.spectra.length)
    return;

  var result = [];
  self.rawData.spectra.forEach(function(spectrum, i) {
    if (spectrum.title)
      result.push(spectrum.title);
    else  /* Spectrum title doesn't exist, so use default format of "Spectrum #" */
      result.push("Spectrum " + (i + 1));
  });
  return result;
}


/** Returns the number of counts for a specific energy value. */
SpectrumChartD3.prototype.getCountsForEnergy = function(spectrum, energy) {
  if (!this.rawData || !this.rawData.spectra || !spectrum || !spectrum.x)
    return -1;

  let lowerchanval, counts = null;
  const spectrumIndex = this.rawData.spectra.indexOf(spectrum);

  if (spectrumIndex < 0)
    return -1;

  if( spectrum.points && spectrum.points.length ){
    lowerchanval = d3.bisector(function(d){return d.x;}).left(spectrum.points,energy,1) - 1;
    counts = spectrum.points[lowerchanval].y;
  }
  return counts;
}



/* Returns the data y-range for the currently viewed x-range.  Third element of returned array gives smallest non-zero height in the range */
SpectrumChartD3.prototype.getYAxisDataDomain = function(){
  var self = this;
  
  if( !self.rawData || !self.rawData.spectra || !self.rawData.spectra.length )
    return [0, 3000, self.options.logYAxisMin];
  
  var y0, y1, minNonZeroY0 = self.options.logYAxisMin;
  var foreground = self.rawData.spectra[0];
  var firstData = self.displayed_start(foreground);
  var lastData = self.displayed_end(foreground);
  
  
  if( firstData >= 0 ){
    const forkey = self.options.backgroundSubtract && ('bgsubtractpoints' in foreground) ? 'bgsubtractpoints' : 'points';
    y0 = y1 = foreground[forkey][firstData].y;
    if( y0 > 0 ) minNonZeroY0 = y0;
    
    self.rawData.spectra.forEach(function(spectrum) {
      // Don't consider background spectrum if we're viewing the Background Subtract
      if (self.options.backgroundSubtract && spectrum.type === self.spectrumTypes.BACKGROUND) return;
      firstData = self.displayed_start(spectrum);
      lastData = self.displayed_end(spectrum);
      var speckey = self.options.backgroundSubtract && ('bgsubtractpoints' in spectrum) ? 'bgsubtractpoints' : 'points';  // Figure out which set of points to use
      
      for (var i = firstData; i < lastData; i++) {
        if (spectrum[speckey][i]) {
          const y = spectrum[speckey][i].y;
          y0 = Math.min( y0, y );
          y1 = Math.max( y1, y );
          if( y > 0 ) minNonZeroY0 = Math.min( minNonZeroY0, y );
        }
      }
    });
  }else {
    y0 = 0;
    y1 = 3000;
  }
  
  if( y0 > y1 ) { y1 = [y0, y0 = y1][0]; }
  if( y0 == y1 ){ y0 -=1; y1 += 1; }

  return [y0, y1, minNonZeroY0];
}

/**
 * Returns the Y-axis domain based on the current set of zoomed-in data, with user preffered padding amounts accounted for.
 */
SpectrumChartD3.prototype.getYAxisDomain = function(){
  var self = this;

  if (!self.rawData || !self.rawData.spectra || !self.rawData.spectra.length)
    return [3000,0];
    
  let yrange, y0, y1;
  yrange = self.getYAxisDataDomain();
  y0 = yrange[0];
  y1 = yrange[1];
  
  
  if( self.options.yscale == "log" ) {
    // Specify the (approx) fraction of the chart that the scale should extend past the data
    var yfractop = self.options.logYFracTop, yfracbottom = self.options.logYFracBottom;

    var y0Intitial = ((y0<=0.0) ? self.options.logYAxisMin : y0);
    
    // If the y-range doesnt go above 1.0, lets set the y-axis minimum to be based on minimum non-zero counts
    if( (y0 <= 0.0) && (yrange.length > 2) && (yrange[2] > 0.0) && (yrange[2] < self.options.logYAxisMin) && (yrange[1] < 1) )
      y0 = y0Intitial = 0.5*yrange[2];
    
    var y1Intitial = ((y1<=0.0) ? 1.0 : y1);
    y1Intitial = ((y1Intitial<=y0Intitial) ? 1.1*y0Intitial : y1Intitial);

    var logY0 = Math.log10(y0Intitial);
    var logY1 = Math.log10(y1Intitial);

    var logLowerY = ((y0<=0.0) ? -1.0 : (logY0 - yfracbottom*(logY1-logY0)));
    var logUpperY = logY1 + yfractop*(logY1-logY0);

    var ylower = Math.pow( 10.0, logLowerY );
    var yupper = Math.pow( 10.0, logUpperY );

    y0 = ((y0<=0.0) ? self.options.logYAxisMin : ylower);
    y1 = ((y1<=0.0) ? 1.0 : yupper);
  } else if( self.options.yscale == "lin" )  {
    y0 = ((y0 <= 0.0) ? (1+self.options.linYFracBottom)*y0 : (1-self.options.linYFracBottom)*y0);
    y1 = (1 + self.options.linYFracTop)*y1;
  } else if( self.options.yscale == "sqrt" ) {
    y0 = ((y0 <= 0.0) ? 0.0 : (1-self.options.sqrtYFracBottom)*y0);
    y1 = (1+self.options.sqrtYFracTop)*y1;
  }

  return [y1,y0];
}
