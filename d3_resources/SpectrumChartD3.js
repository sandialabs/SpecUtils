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

   This file is already very long; please refactor and simplify alongside any fix or feature, so it
   does not keep growing.
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
  this.options.showAnimation = (typeof this.options.showAnimation == 'boolean' && this.options.animationDuration > 0) ? this.options.showAnimation : false;

  // Hard-coded / fixed option values (not user-configurable, so no validation needed).
  Object.assign( this.options, {
    refLineTopPad: 30,
    maxScaleFactor: 10,
    logYFracTop: 0.05,  logYFracBottom: 0.025,
    linYFracTop: 0.1,   linYFracBottom: 0.1,
    sqrtYFracTop: 0.1,  sqrtYFracBottom: 0.1
  } );
  
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
     "xTitlePad": 5, // vertical padding between y-axis numbers (non-compact) and the title, or for compact just the height down from axis.
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

  // Each spectrum object carries one of these as its `type`.
  this.spectrumTypes = {
    FOREGROUND: 'FOREGROUND',
    BACKGROUND: 'BACKGROUND',
    SECONDARY: 'SECONDARY',
  };

  // Dragging the plot sets both flags; zooming only (e.g. mouse wheel) sets just zooming_plot.
  this.dragging_plot = false;
  this.zooming_plot = false;

  this.refLines = [];

  this.xScale = d3.scale.linear()
      .domain(this.options.xScaleDomain ? this.options.xScaleDomain : [0, 3000])
      .range([0, this.size.width]);

  this.xaxisdown = null;

  if( this.options.yscale === "log" ) {
    this.yScale = d3.scale.log().clamp(true).domain([0, 100]).nice().range([1, this.size.height]).nice();
  } else if( this.options.yscale === "sqrt" ) {
    this.yScale = d3.scale.pow().exponent(0.5).domain([0, 100]).range([0, this.size.height]);
  } else {
    this.yScale = d3.scale.linear().domain([0, 100]).nice().range([0, this.size.height]).nice();
  }

  /* Finds distance between two points */
  this.dist = function (a, b) {
    return Math.sqrt(Math.pow(a[0]-b[0],2) + Math.pow(a[1]-b[1],2));
  };

  // True when rawData has at least one normal spectrum OR one stacked template.
  this.hasAnyData = function(){
    const rd = self.rawData;
    return !!rd && (((rd.spectra && rd.spectra.length))
                   || (rd.templates && rd.templates.length));
  };

  // True when at least one normal spectrum is loaded (templates dont count) - the
  // common "is there anything to operate on" guard used throughout.
  this.hasSpectrumData = function(){
    const rd = self.rawData;
    return !!( rd && rd.spectra && rd.spectra.length );
  };

  this.min_max_x_values = function() {
    if( !self.hasAnyData() )
      return [-1,-1];

    var min = null, max = null;
    const update = function(spectrum){
      if (!spectrum || !spectrum.x) return;
      if (min == null || spectrum.x[0] < min) min = spectrum.x[0];
      if (max == null || spectrum.x[spectrum.x.length-1] > max) max = spectrum.x[spectrum.x.length-1];
    };
    if( self.rawData.spectra )   self.rawData.spectra.forEach(update);
    if( self.rawData.templates ) self.rawData.templates.forEach(update);

    return [min,max];
  };

  // Index range of the raw channels (`spectrum.x`; default the first spectrum, else template) in view
  const rawXSource = function(spectrum){
    if( spectrum )
      return spectrum.x;
    const rd = self.rawData;
    return self.hasSpectrumData() ? rd.spectra[0].x : (self.hasAnyData() ? rd.templates[0].x : null);
  };
  this.displayed_raw_start = function(spectrum){
    const src = self.hasAnyData() ? rawXSource(spectrum) : null;
    return src ? self._bisecRaw.left(src, self.xScale.domain()[0]) : -1;
  };
  this.displayed_raw_end = function(spectrum){
    const src = self.hasAnyData() ? rawXSource(spectrum) : null;
    return src ? self._bisecRaw.right(src, self.xScale.domain()[1]) : -1;
  };

  // Index range of the displayed (rebinned) `spectrum.points` in view
  this.displayed_start = function(spectrum){
    if( !spectrum || !spectrum.points || !spectrum.points.length )
      return -1;
    return Math.max( self._bisecX.right(spectrum.points, self.xScale.domain()[0]) - 1, 0 );
  };
  this.displayed_end = function(spectrum){
    if( !spectrum || !spectrum.points || !spectrum.points.length )
      return -1;
    return Math.max( self._bisecX.left(spectrum.points, self.xScale.domain()[1]), 1 );
  };

  this.rebinFactor = 1;
  this.do_rebin();

  this.setYAxisDomain = function(){
    if( isNaN(self.yaxisdown) )
      self.yScale.domain( self.getYAxisDomain() );
  };
  this.setYAxisDomain();

  this.yaxisdown = NaN;

  /* Left-mouse-drag mode: captured ONCE at mousedown from the modifier-key state,
     and treated as sticky for the entire gesture. Releasing modifier keys mid-drag
     does NOT change the mode; ESC cancels by setting it back to 'none'. Touch
     peak-fit also uses 'fitPeak' so cross-device readers have one source of truth.
       'none'         — no left-drag in progress (or aborted)
       'fitPeak'      — Ctrl-drag (mouse) or two-finger swipe (touch)
       'deletePeaks'  — Shift-drag
       'countGammas'  — Alt+Shift-drag
       'recalibrate'  — Alt+Ctrl-drag
       'zoomY'        — Meta/Cmd-drag (y-axis zoom)
       'zoomX'        — no modifiers (default x-axis zoom)
       'altOnly'      — Alt-only (reserved; same effect as 'none') */
  this.leftDragMode = 'none';

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
      .attr("class", "chartarea" );

  this.svg = d3.select(self.chart).select('svg');

  /*
  DOM nesting, event listeners, and coordinate frames:

   ____________________________________________________________________________
  |                                  <body>                                    |
  |   ______________________________________________________________________   |
  |  |                          this.chart <div>                            |  |
  |  |   ________________________________________________________________   |  |
  |  |  |                        this.svg <svg>                          |  |  |
  |  |  |  svg-level <g>/<text> siblings of vis (in the padding gap or   |  |  |
  |  |  |  overlaid): chart title, x/y-axis titles, this.legend,         |  |  |
  |  |  |  this.scalerWidget (y-scale sliders), this.sliderChart         |  |  |
  |  |  |  (x-axis slider: slider-plot rect, sliderChartBody with        |  |  |
  |  |  |  sliderLine{i} paths, sliderBox, drag handles, close btn)      |  |  |
  |  |  |   ____________________________________________________________ |  |  |
  |  |  |  | this.vis <g>  (the plot area), children in paint order:    ||  |  |
  |  |  |  |   this.plot bg rect ("chartarea")                          ||  |  |
  |  |  |  |   this.chartBody <g, clipped>: spectrumline{i} +           ||  |  |
  |  |  |  |     templatepath{i} paths                                  ||  |  |
  |  |  |  |   this.peakVis <g, clipped>: peak fill/outline/continuum   ||  |  |
  |  |  |  |     paths (each carrying node.__peakInfo) + peak labels    ||  |  |
  |  |  |  |   yAxisBody / xAxisBody <g> (tick marks + numbers)         ||  |  |
  |  |  |  |   xGridBody / yGridBody <g> (when grid on; inserted        ||  |  |
  |  |  |  |     before .refLineInfo)                                   ||  |  |
  |  |  |  |   g.ref reference lines, this.refLineInfo readout          ||  |  |
  |  |  |  |   this.mouseInfo (bottom-right readout), this.peakInfo     ||  |  |
  |  |  |  |   transient gesture visuals: #zoomInXBox/#zoomInYBox +     ||  |  |
  |  |  |  |     texts, #deletePeaksBox, #countGammasBox, touch lines,  ||  |  |
  |  |  |  |     #createPeakTouch* lines, recalibration* lines/clone,   ||  |  |
  |  |  |  |     ROI drag boxes/lines                                   ||  |  |
  |  |  |  |____________________________________________________________||  |  |
  |  |  |________________________________________________________________|  |  |
  |  |______________________________________________________________________|  |
  |____________________________________________________________________________|

  this.vis is translated by (padding.leftComputed, padding.topComputed) inside
  this.svg, which fills this.chart.  this.sliderChart is translated to the strip
  below the x-axis and its contents use their own full-data-range x mapping with
  a scale(1, sliderChartHeight/size.height) squish on the line paths.

  Event listeners:
    vis     : mousedown, mouseup, wheel, touch{start,move,end,cancel}, mouseout
              (kinetic ref-line cleanup) - the pointer gestures themselves
    chart   : mousemove, mouseleave, mouseup, wheel, touch{start,end} - move/up
              dispatch keeps working when the pointer leaves the plot area (e.g.
              over the axes), and gesture-mode moves route from here
    peakVis : mouseover/mousemove/mouseout/contextmenu, DELEGATED once for all
              peak paths - handlers key off event.target.__peakInfo (paths
              without it, e.g. the multi-peak continuum line, are ignored)
    axis bodies + titles: mousedown.drag/touchstart.drag (axis drag-rescale),
              dblclick / click (log-lin toggle), hover bolding
    sliderChart parts: mousedown/touchstart/touchmove on the box + handles;
              document-level mousemove/mouseup namespaced
              ".sliderdrag"+chart.id while a slider drag is active
    scalerWidget: mousedown/touchstart per slider + document-level tracking via
              handleMouseMoveScaleFactorSlider through the chart mousemove
    chart (roi drag): mousemove.roidrag/touchmove.roidrag installed only while
              dragging a ROI edge
    document.body + window: "mouseup.chart"+id -> handleCancelAllMouseEvents
              (mouse released outside the chart), "blur.chart"+id ->
              handleChartMouseLeave (alt-tab cleanup), "keydown.chart"+id
    window CAPTURE-phase safety nets: touchend/touchcancel + pointerup/
              pointercancel (Android WebView drops touchend on SVG paths); see
              chartTouchEndSafety below
  All document/window listeners are namespaced by chart.id (multi-chart safety)
  and removed in destroy().

  Gesture dispatch: pointer gestures resolve to one of the modes in
  this._gestureModes (fitPeak/deletePeaks/countGammas/recalibrate/zoomX/zoomY);
  mouse picks the mode once at mousedown from modifier keys (sticky in
  leftDragMode), touch re-recognizes from finger geometry each touchmove (except
  fitPeak, which latches).  See _initGestureModes for the per-mode move/up/
  cancel slots and the order arrays.

  Coordinate frames:
    d3.mouse(document.body)   -> page coords (right-click / one-finger pan track)
    d3.mouse(this.vis[0][0])  -> vis frame; (0,0) at top-left of the plot area
    d3.touches(this.vis[0][0])-> vis frame, one [x,y] per active touch
    this.touchesOnChart       -> map of Touch objects keyed by identifier; their
                                 pageX/pageY + recorded startX/startY are PAGE
                                 coords (only differences are comparable against
                                 vis-frame values)
    getMousePos() returns [x_vis, y_vis, x_svg, y_svg] (svg values are stable
    against mid-gesture y-axis-width changes; used by the zoom-out direction test)
    xScale/yScale map energy/counts <-> vis-frame px; the slider chart positions
    its box with a local full-range copy of xScale
  */

  /* Vis interactions */
  this.vis
    .on("mousedown", self.handleVisMouseDown())
    .on("mouseup", self.handleVisMouseUp())
    .on("wheel", self.handleVisWheel())
    .on("touchstart", self.handleVisTouchStart())
    .on("touchmove", self.handleVisTouchMove())
    .on("touchend", self.handleVisTouchEnd())
    .on("touchcancel", self.handleVisTouchCancel() )
    ;

  /// @TODO triggering the cancel events on document.body and window is probably a bit agressive; could probably do this for just this.vis + on leave events
  d3.select(document.body)
    .on("mouseup.chart" + this.chart.id, self.handleCancelAllMouseEvents() )
  d3.select(window)
    .on("mouseup.chart" + this.chart.id, self.handleCancelAllMouseEvents());

  // Window-capture safety net: Android WebView sometimes drops touchend on the chart (e.g. on a peak
  // path), leaving touchHold running, rightClickDown stale, and the tap unprocessed.  Mirror
  // handleCancelAllMouseEvents, then (next tick, so handleVisTouchEnd can run normally) synthesize the
  // single tap if handleVisTouchEnd never ran.  touchHoldEmitted is only cleared in the deferred block,
  // else the tap check would emit a leftclicked that hides the menu the long-press just opened.
  function chartTouchEndSafety(ev) {
    // Skip multi-finger end (e.g. one finger lifted mid-zoom): don't reset pan state for a still-active gesture.
    const remainingTouches = (ev && ev.touches) ? ev.touches.length : 0;
    if (ev && (ev.type === 'touchend' || ev.type === 'touchcancel') && remainingTouches > 0) {
      return;
    }
    // PointerEvents don't expose other active pointers; use the captured touchStart count instead.
    if (ev && (ev.type === 'pointerup' || ev.type === 'pointercancel')
        && self.touchStart && self.touchStart.length > 1) {
      return;
    }

    const wasLongPress = self.touchHoldEmitted;

    self._clearTouchHold();
    self.rightClickDown = null;
    self.is_panning = false;
    self.dragging_plot = false;
    self.zooming_plot = false;
    self.leftMouseDown = null;
    self.leftDragMode = 'none';   // else a dropped two-finger-fit touchend leaves a stuck 'fitPeak'

    setTimeout(function () {
      if (wasLongPress) {
        self.touchHoldEmitted = false;
        return;
      }
      if (self.mouseDownRoi && !self.mousewait && !self.touchHoldEmitted
          && self.touchStart && self.touchStart.length === 1 && self.touchPageStart) {
        try {
          const x = self.touchStart[0][0], y = self.touchStart[0][1];
          const pageX = self.touchPageStart[0], pageY = self.touchPageStart[1];
          const energy = self.xScale.invert(x);
          const count = self.yScale.invert(y);
          // Refresh before handler() so the emitted leftclicked carries this tap's nuclide.
          if( self.kineticRefLines )
            self.handleUpdateKineticRefLineUpdate( [x, y] );
          const handler = self.getMouseUpOrSingleFingerUpHandler([x, y, pageX, pageY, energy, count], false, true);
          handler();
          self.touchStart = null;

          // handleVisTouchEnd never ran, so the box handleVisTouchMove hid was never restored.
          self.updateMouseCoordText( [x, y] );
          self.updatePeakInfo( [x, y] );
        } catch (e) {
          console.warn("chartTouchEndSafety: synthesizing tap failed:", e);
        }
      }
    }, 0);
  }
  // Android WebView drops touchend entirely on SVG paths with a contextmenu handler (the peak
  // paths), but pointer events still fire, so also bind pointerup/pointercancel - touch only, so
  // desktop right-drag pan isn't affected (handleVisMouseUp gates on !self.is_panning, which this
  // would clear too early).
  function chartPointerUpSafety(ev) {
    if (!ev || ev.pointerType !== 'touch') return;
    chartTouchEndSafety(ev);
  }
  this._touchEndSafetyHandler = chartTouchEndSafety;
  this._pointerUpSafetyHandler = chartPointerUpSafety;
  window.addEventListener("touchend",     chartTouchEndSafety, { capture: true, passive: true });
  window.addEventListener("touchcancel",  chartTouchEndSafety, { capture: true, passive: true });
  window.addEventListener("pointerup",    chartPointerUpSafety, { capture: true, passive: true });
  window.addEventListener("pointercancel",chartPointerUpSafety, { capture: true, passive: true });

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

  // Bold an axis' tick label while hovered
  const boldTickText = function(){
    if( d3.event.target.nodeName === "text" )
      d3.select(d3.event.target).style("font-weight", "bold");
  };
  const unboldTickText = function(){ d3.select(this).selectAll('text').style("font-weight", null); };

  this.yAxis = d3.svg.axis().scale(this.yScale)
   .orient("left")
   .innerTickSize(7)
   .outerTickSize(1)
   .ticks(0);

  this.yAxisBody = this.vis.append("g")
    .attr("class", "yaxis axis" )
    .call(this.yAxis)
    .style("cursor", "ns-resize")
    .on("mousedown.drag",  self.yaxisDrag())
    .on("touchstart.drag", self.yaxisDrag())
    .on("mouseover", boldTickText)
    .on("mouseout", unboldTickText)
    .on("dblclick", function(){ //toggle between linear and log when user double-clicks axis
      SpectrumChartD3.stopEvent();
      self.toggleYAxisType();
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
    .on("mouseover", boldTickText)
    .on("mouseout", unboldTickText)
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

  /* Peak hover/context-menu listeners are delegated here, since drawPeaks re-creates the peak
     paths on every redraw.  Each peak path carries its info object on `__peakInfo`; events on
     paths without it (the multi-peak continuum line) or on non-outline fills are ignored. */
  const movePeakHandler = self.handleMouseMovePeak();
  self.peakVis = this.vis.append("g")
    .attr("class", "peakVis")
    .attr("clip-path", "url(#clip" + this.chart.id + ")")
    .on("mouseover", function(){
      const info = d3.event.target.__peakInfo;
      if( info && info.isOutline )
        self.highlightPeak( d3.event.target, true );
    })
    .on("mousemove", function(){
      const info = d3.event.target.__peakInfo;
      if( info && info.isOutline )
        movePeakHandler();
    })
    .on("mouseout", function(){
      const info = d3.event.target.__peakInfo;
      if( info && info.isOutline )
        self.handleMouseOutPeak( d3.event.target );
    })
    .on("contextmenu", function(){
      /* Suppress the WebView's native long-press menu on a peak path; our touchHold
         timer handles the rightclicked emit. */
      if( d3.event && d3.event.target.__peakInfo ){
        SpectrumChartD3.stopEvent();
      }
    });

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
      self.toggleYAxisType();
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
  
  // Reference-line info that follows the mouse; hidden when not near a reference line.
  self.refLineInfo = this.vis.append("g")
    .attr("class", "refLineInfo")
    .style("display", "none");

  // Text in its own <g> so getBBox() gives its extent for positioning; one tspan per info line.
  self.refLineInfoTxt = self.refLineInfo.append("g");
  self.refLineInfoTxt.append("text")
   .attr("x", 0)
   .attr("dy", "1em");

  this.updateYAxisTitleText();
  
  if( this.options.gridx )
    this.setGridX(this.options.gridx,true);
  
  if( this.options.gridy )
    this.setGridY(this.options.gridy,true);

  this.vis.on("mouseout", function(){ self._clearKineticRefLine(); });
  
  // Initialize kinetic reference line cycling properties
  this.candidateKineticRefLines = [];
  this.currentKineticRefLineIndex = 0;
  this.kineticRefLineCycleTimer = null;
  this.lastKineticTapPos = null;
  this.lastKineticTapDomain = null;
  
  d3.select(window).on("keydown.chart" + this.chart.id, this.keydown());

  this._initGestureModes();
}

/** Destructor method to properly clean up global event handlers. Call this before removing/destroying a chart. */
SpectrumChartD3.prototype.destroy = function() {
  // Remove global event handlers using the namespaced IDs
  d3.select(window).on("keydown.chart" + this.chart.id, null);
  d3.select(window).on("mouseup.chart" + this.chart.id, null);
  d3.select(window).on("blur.chart" + this.chart.id, null);
  d3.select(document.body).on("mouseup.chart" + this.chart.id, null);
  // Transient slider-drag listeners, in case we are destroyed mid-drag.
  d3.select(document).on("mouseup.sliderdrag" + this.chart.id, null);
  d3.select(document).on("mousemove.sliderdrag" + this.chart.id, null);

  // Window-capture touchend/touchcancel + pointerup/pointercancel safety nets (see constructor).
  if( this._touchEndSafetyHandler ){
    window.removeEventListener("touchend",    this._touchEndSafetyHandler, { capture: true });
    window.removeEventListener("touchcancel", this._touchEndSafetyHandler, { capture: true });
    this._touchEndSafetyHandler = null;
  }
  if( this._pointerUpSafetyHandler ){
    window.removeEventListener("pointerup",    this._pointerUpSafetyHandler, { capture: true });
    window.removeEventListener("pointercancel",this._pointerUpSafetyHandler, { capture: true });
    this._pointerUpSafetyHandler = null;
  }
  
  // Defensive timer cleanup.
  window.clearTimeout(this.mousewait);
  window.clearTimeout(this.touchHold);
  window.clearTimeout(this.wheeltimer);
  window.clearTimeout(this.roiDragRequestTimeout);
  window.clearTimeout(this.adjustLabelTimeout);
  window.clearInterval(this.kineticRefLineCycleTimer);  // created with setInterval
  window.cancelAnimationFrame(this.zoomAnimationID);
  window.cancelAnimationFrame(this._legendUpdateRaf);
  this.mousewait = this.touchHold = this.wheeltimer = this.roiDragRequestTimeout = this.adjustLabelTimeout = this.kineticRefLineCycleTimer = this.zoomAnimationID = this._legendUpdateRaf = null;
  
  d3.select(document.body).style("cursor", "default"); // Reset global cursor style
  
};

/** -------------- Data Handlers -------------- */
/* Shared (stateless) bisectors, so hot paths don't allocate them.  _bisecX: arrays of {x:...}
   points;  _bisecE: arrays of {e:...} ref-line energies;  _bisecRaw: plain numeric arrays. */
SpectrumChartD3.prototype._bisecX = d3.bisector( function(d){ return d.x; } );
SpectrumChartD3.prototype._bisecE = d3.bisector( function(d){ return d.e; } );
SpectrumChartD3.prototype._bisecRaw = d3.bisector( function(d){ return d; } );

/* Maps a point in a peak-label <text>'s local (untranslated/unrotated) coordinates into
   peakVis coordinates: through the label's CTM, then back out of peakVis's (pass the
   already-inverted matrix - callers reuse it across several corner points).
   Shared by drawPeakLabels and highlightLabel. */
SpectrumChartD3.prototype._txtToVisCoords = function( svgCtm, visCtmInv, x, y ){
  const pt = this.svg.node().createSVGPoint(); //TODO: createSVGPoint() is deprecated
  pt.x = x;
  pt.y = y;
  return pt.matrixTransform( svgCtm ).matrixTransform( visCtmInv );
};

/**
 * elem: a DOM element, or the object ID of a WObject
 * event: object naming the signal (and JS event/target)
 * Remaining arguments are passed through to Wt.emit.
 */
SpectrumChartD3.prototype.WtEmit = function(elem, event, ...args) {
  if (!window.Wt) {
    console.warn('Wt not found! Canceling "' + event.name + '" emit function...');
    return;
  }

  if( this.options.noEventsToServer )
    return;

  Wt.emit(elem, event, ...args);
}

/* Stops the current d3 event's default action and propagation. */
SpectrumChartD3.stopEvent = function(){
  d3.event.preventDefault();
  d3.event.stopPropagation();
};

/* The event's modifier keys as bits: shift=1, ctrl=2, alt=4, meta=8. */
SpectrumChartD3.modifierBits = function( ev ){
  return (ev.shiftKey ? 0x01 : 0) | (ev.ctrlKey ? 0x02 : 0) | (ev.altKey ? 0x04 : 0) | (ev.metaKey ? 0x08 : 0);
};

/* Removes and nulls the elements stored on `this` under the given names. */
SpectrumChartD3.prototype.deleteAndNullifyElements = function(...names) {
  for( const name of names ){
    if( this[name] ){
      this[name].remove();
      this[name] = null;
    }
  }
};

SpectrumChartD3.prototype.deleteTouchLine = function() {
  this.deleteAndNullifyElements( 'touchLineX', 'touchLineY' );
};

SpectrumChartD3.prototype.getStaticSvg = function(){
  try{
    let w = this.svg.attr("width"), h = this.svg.attr("height");

    // Propagate the styles set on the SVG (SpectrumChartD3.css and D3SpectrumDisplayDiv::m_cssRules)
    //  into the <defs> section of the new SVG.

    // A computed style property of the first element matching `sel`, or null
    function getProp( sel, p ){
      const el = document.querySelector(sel);
      return (el && window.getComputedStyle( el )[p]) || null;
    }

    function getSvgFill( sel ){
      let fill = getProp( sel, 'fill' ) || "";
      let comps = fill.match(/\d+/g);
      if( (comps && (comps.reduce( function(a,b){ return parseFloat(a) + parseFloat(b); }) > 0.01))
         || (fill.length > 2 && fill.substring(0,1)=='#') )
        return fill;
      return null;
    };
    
    const getStroke = (sel) => getProp( sel, 'stroke' );

    let dombackground = getProp( '.Wt-domRoot', 'backgroundColor' ); //ex "rgb(44, 45, 48)", or "rgba(0, 0, 0, 0)"
    
    // Treat a transparent or near-zero dom background as "not set".
    if( dombackground ) {
      let bgrndcomps = dombackground.match(/\d+/g);
      if( !bgrndcomps
      || ((bgrndcomps.reduce( function(a,b){ return parseFloat(a) + parseFloat(b); }) < 0.01)
      && (dombackground.length < 2 || dombackground.substring(0,1)=='#')) )
      dombackground = null;
    }
    
    const svgback = getProp( '#' + this.chart.id + ' > svg', 'background' ) || dombackground;
    const chartAreaFill = getSvgFill('#chartarea' + this.chart.id);
    const legFontSize = getProp( '.legend', 'fontSize' );
    const legColor = getProp( '.legend', 'color' );
    const legBackFill = getProp( '.legendBack', 'fill' );
    const legBackStroke = getStroke( '.legendBack' );
    const axisFill = getProp( '.xaxis', 'fill' );
    
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
    + '.templatepath { stroke: none; }'
    + (foreStroke ? '.speclinepath.FOREGROUND, .SpectrumLegendLine.FOREGROUND { stroke: ' + foreStroke + '}' : "")
    + (backStroke ? '.speclinepath.BACKGROUND, .SpectrumLegendLine.BACKGROUND { stroke: ' + backStroke + '}' : "")
    + (secondStroke ? '.speclinepath.SECONDARY, .SpectrumLegendLine.SECONDARY { stroke: ' + secondStroke + '}' : "")
    + '</style></defs>';
    
    // Hide mouse info, slider chart, etc. while serializing
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
    
    // Restore what was hidden above
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

SpectrumChartD3.prototype.do_rebin = function() {
  var self = this;

  if( !self.hasAnyData() )
    return;

  // Choose the rebin factor from the spectrum with the fewest displayed channels, so e.g. a NaI
  //  spectrum (~1k channels) shown with a HPGe one (~16k) keeps its definition (the HPGe may then have
  //  more points than pixels).  Templates are included so stacked-area steps match the data lines.
  let newRebin = 9999;
  const considerForRebinFactor = function(spectrum){
    const firstRaw = self.displayed_raw_start(spectrum);
    const lastRaw = self.displayed_raw_end(spectrum);
    const npoints = lastRaw - firstRaw;

    if( npoints > 1 && self.size.width > 2 ){
      const thisrebin = Math.max( 1, Math.ceil(self.options.spectrumLineWidth * npoints / self.size.width) );
      newRebin = ((thisrebin > 0) && (thisrebin < newRebin)) ? thisrebin : newRebin;
    }
  };
  if( this.rawData.spectra )   this.rawData.spectra.forEach(considerForRebinFactor);
  if( this.rawData.templates ) this.rawData.templates.forEach(considerForRebinFactor);

  if( newRebin === 9999 )
    newRebin = 1;

  if( this.rebinFactor !== newRebin ){
    this.rebinFactor = newRebin;
    this.updateYAxisTitleText();
  }

  const rebinOne = function(spectrum){
    let firstRaw = self.displayed_raw_start(spectrum);
    let lastRaw = self.displayed_raw_end(spectrum);
    const sf = (typeof spectrum.yScaleFactor === "number") ? spectrum.yScaleFactor : 1.0;

    // Rebuild points when the rebin factor, displayed raw range, or applied scale factor (scaledBy)
    // changes; the scaledBy term lets a pure scaler-slider change re-scale here (no separate pass).
    if( newRebin != spectrum.rebinFactor || spectrum.firstRaw !== firstRaw || spectrum.lastRaw !== lastRaw || spectrum.scaledBy !== sf ){
      spectrum.points = [];

      spectrum.rebinFactor = newRebin;
      spectrum.firstRaw = firstRaw;
      spectrum.lastRaw = lastRaw;
      spectrum.scaledBy = sf;

      // Round firstRaw and lastRaw down and up to multiples of newRebin
      firstRaw -= (firstRaw % newRebin);
      lastRaw += newRebin - (lastRaw % newRebin);
      if( firstRaw >= newRebin )
        firstRaw -= newRebin;
      if( lastRaw > spectrum.x.length )
        lastRaw = spectrum.x.length;

      for( var i = firstRaw; i < lastRaw; i += newRebin ){
        const thisdata = { x: spectrum.x[Math.min(i, spectrum.x.length-1)], y: 0 };
        for( let j = 0; (j < newRebin) && ((i+j) < spectrum.y.length); ++j )
          thisdata.y += spectrum.y[i+j];
        thisdata.y *= sf;

        spectrum.points.push( thisdata );
      }
    }
  };

  if( this.rawData.spectra )   this.rawData.spectra.forEach(rebinOne);
  if( this.rawData.templates ) this.rawData.templates.forEach(rebinOne);
}

/** Takes an object like { yAxisTitle: 'Counts', realTime: 'Real Time',... } mapping the keys used in
 * this code to the strings to display.  Need not be complete. */
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
    templateDefaultTitle: "Template",
    candidates: "Candidates:",
    useArrowsToSelect: "(use ↑ and ↓ to select)",
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

  self.updateLegend();   // legend text (live/real/dead time, scaled-by, ...) uses options.txt
  self.handleResize( false );
}//SpectrumChartD3.prototype.setLocalizations = ...

/** Adds, or replaces the first spectrum of the passed-in type with, the new spectrum data. */
SpectrumChartD3.prototype.setSpectrumData = function( spectrumData, resetdomain, spectrumType, id, backgroundID ) {
  var self = this;

  if (!spectrumData) return;
  if (!spectrumType || !(spectrumType in self.spectrumTypes)) return;

  if (!self.rawData) self.rawData = { updateTime: spectrumData.updateTime, spectra: [] };
  if (!self.rawData.updateTime) self.rawData.updateTime = spectrumData.updateTime;

  const spectra = self.rawData.spectra;
  spectrumData = spectrumData.spectra[0];

  if (typeof id !== 'undefined') spectrumData.id = id;
  if( (typeof spectrumData.id === 'undefined') || (spectrumData.id === null) ) spectrumData.id = Math.random();

  if (backgroundID) spectrumData.backgroundID = backgroundID;

  const index = spectra.findIndex( function(s){ return s.type === spectrumType; } );
  if (index < 0)
    spectra.push(spectrumData);
  else
    spectra[index] = spectrumData;

  self.setData( self.rawData, resetdomain );
}

/** Removes all spectra of the passed-in type. */
SpectrumChartD3.prototype.removeSpectrumDataByType = function( resetdomain, spectrumType ) {
  var self = this;

  if (!spectrumType || !(spectrumType in self.spectrumTypes)) return;
  if (!self.rawData) self.rawData = { spectra: [] };

  self.rawData.spectra = self.rawData.spectra.filter( function(s){ return s.type !== spectrumType; } );

  self.setData( self.rawData, resetdomain );
}

SpectrumChartD3.prototype.setData = function( data, resetdomain ) {
  // TODO: validate data (required fields, increasing energies, consistent x/y lengths, no inf/NaN).

  const self = this;

  // New data invalidates the cached slider-chart lines (see _sliderChartSignature)
  this._dataGeneration = (this._dataGeneration|0) + 1;
  this._sliderPathSig = null;

  //Remove all the lines for the current drawn histograms
  this.vis.selectAll(".speclinepath").remove();
  this.vis.selectAll(".templatepath").remove();
  if (this.sliderChart) this.sliderChart.selectAll('.sliderLine').remove();

  this.rawData = null;
  this.rebinFactor = -1; /*force rebin calc */

  // Accept any of: a non-empty spectra array, a non-empty templates array, or both.
  const hasSpectra   = !!(data && Array.isArray(data.spectra)   && data.spectra.length > 0);
  const hasTemplates = !!(data && Array.isArray(data.templates) && data.templates.length > 0);
  if( !hasSpectra && !hasTemplates )
  {
    this.updateLegend();
    this.redraw()();
    return;
  }

  if( !data.spectra )   data.spectra   = [];
  if( !data.templates ) data.templates = [];

  // Foreground first, then secondary, then background (mostly for a sensible legend order)
  data.spectra.sort( function(lhs,rhs){
    const order = [self.spectrumTypes.FOREGROUND, self.spectrumTypes.SECONDARY, self.spectrumTypes.BACKGROUND];
    const lhs_pos = order.indexOf(lhs.type);
    const rhs_pos = order.indexOf(rhs.type);
    return ((lhs_pos >= 0) ? lhs_pos : 4) - ((rhs_pos >= 0) ? rhs_pos : 4);
  });

  this.rawData = data;

  // Expand polynomial xeqn into per-channel x for both spectra and templates.
  const expandXeqn = function(spectrum){
    if( !spectrum || !spectrum.y || !spectrum.y.length )
      return;
    if( spectrum.xeqn && spectrum.xeqn.length>1 && !spectrum.x )
    {
      spectrum.x = [];
      for( var i = 0; i < spectrum.y.length; ++i )
      {
        spectrum.x[i] = spectrum.xeqn[0];
        for( var j = 1; j < spectrum.xeqn.length; ++j )
          spectrum.x[i] += spectrum.xeqn[j] * Math.pow(i,j);
      }
    }
  };
  this.rawData.spectra.forEach(expandXeqn);
  this.rawData.templates.forEach(expandXeqn);

  for (var i = 0; i < this.rawData.spectra.length; ++i)
    this['line'+i] = null;

  this.do_rebin();  // fills spectrum.points, used to draw the lines below

  if( resetdomain ) {
    const bounds = self.min_max_x_values();
    this.setXAxisRange(bounds[0], bounds[1], true, false);
  }

  var maxYScaleFactor = 0.1;
  const sum = (y) => (y || []).reduce( (a, b) => a + b, 0 );

  // Create stacked-template <path> elements first so they sit UNDER the spectrum lines in z-order
  // (SVG paint order = DOM order). Filled with template.lineColor; stroke disabled.
  this.rawData.templates.forEach( function(template,i){
    template.dataSum = sum( template.y );
    const fill = (template.lineColor && template.lineColor.length) ? template.lineColor : "#888888";
    self.chartBody.append("path")
      .attr("id", "templatepath" + i)
      .attr("class", "templatepath")
      .attr("fill", fill)
      .attr("stroke", "none");
  } );

  // Draw background first, then secondary, then unknown types, then foreground (on top)
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
    spectrum.dataSum = sum( spectrum.y );
    if (spectrum.y.length) {
      this['line' + i] = d3.svg.line()
        .interpolate("step-after")
        .x( function(d){ return self.xScale(d.x); })
        .y( function(d) {
          const y = self.yScale(d.y);
          return isFinite(y) ? y : 0;
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

  // + 10 to add some padding for the scale sliders
  self.options.maxScaleFactor = maxYScaleFactor + 10;

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

/** Sets (replacing any existing) the ROIs, e.g. [{...},{...}], of the first spectrum of spectrumType. */
SpectrumChartD3.prototype.setRoiData = function( peak_data, spectrumType ) {
  if( !this.hasSpectrumData() )
    return;

  const spectrum = this.rawData.spectra.find( function(s){ return s && (s.type === spectrumType); } );
  if( spectrum ){
    this.handleCancelRoiDrag();
    spectrum.peaks = peak_data;
  }

  this.redraw()();
};

/** -------------- Render/Drawing Functions -------------- */
SpectrumChartD3.prototype.update = function() {
  const self = this;

  if( !self.hasAnyData() )
    return;

  this.rawData.spectra.forEach(function(spectrum, i) {
    const line = self.vis.select("#spectrumline"+i);

    if (spectrum.type === self.spectrumTypes.BACKGROUND) {
      line.attr('visibility', self.options.backgroundSubtract ? 'hidden' : 'visible');
      if (self.options.backgroundSubtract) return;
    }

    const key = self.options.backgroundSubtract && ('bgsubtractpoints' in spectrum) ? 'bgsubtractpoints' : 'points';
    line.attr("d", self['line'+i](spectrum[key]));
  });

  if (d3.event && d3.event.keyCode) {
    SpectrumChartD3.stopEvent();
  }
}

/** Step-after value of `points` at energy xVal: the y of the rightmost point with x <= xVal. */
SpectrumChartD3.prototype._templateLookupStepAfter = function( points, xVal ){
  const idx = points ? (this._bisecX.right(points, xVal) - 1) : -1;
  return (idx < 0) ? 0 : points[idx].y;
};

/** Sum of step-after lookups across an array of layers at xVal. */
SpectrumChartD3.prototype._templateBaselineAt = function( belowLayers, xVal ){
  let sum = 0;
  for( let j = 0; j < belowLayers.length; ++j )
    sum += this._templateLookupStepAfter(belowLayers[j].points, xVal);
  return sum;
};

/** Sets the `d` of each #templatepath{i} so templates render as stacked filled areas.  Layer i's
 * baseline at x is the sum of the lower layers' step-after values there, so layers may have differing
 * binnings.  Templates intentionally do NOT participate in background-subtract mode. */
SpectrumChartD3.prototype.drawTemplates = function() {
  const self = this;
  if( !self.rawData || !self.rawData.templates || !self.rawData.templates.length )
    return;
  const templates = self.rawData.templates;
  const ypx = function(y){ const v = self.yScale(y); return isFinite(v) ? v : 0; };
  const area = d3.svg.area()
    .interpolate("step-after")
    .x( function(d){ return self.xScale(d.x); } )
    .y0( function(d){ return ypx(d._tplBase); } )
    .y1( function(d){ return ypx(d._tplBase + d.y); } );

  // Per-point baseline cached on each datum as `_tplBase` (shared by y0 and y1); rewritten every
  // redraw, as x-zoom (which doesn't change `points`) or the layers below may have changed.
  for( let i = 0; i < templates.length; ++i ){
    const t = templates[i];
    if( !t || !t.points || !t.points.length ){
      self.vis.select("#templatepath" + i).attr("d", null);
      continue;
    }
    const below = templates.slice(0, i);
    for( let pi = 0; pi < t.points.length; ++pi )
      t.points[pi]._tplBase = self._templateBaselineAt(below, t.points[pi].x);
    self.vis.select("#templatepath" + i).attr("d", area(t.points));
  }
};

SpectrumChartD3.prototype.redraw = function() {
  const self = this;

  return function() {
    if( self.size && (self.size.nYScalers !== self.numYScalers()) ) {
      self.handleResize( true );
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

    self.drawTemplates();
    self.update();
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
  if( (typeof title !== 'string') || title.length === 0 ){
    this.options.txt.title = null;
    this.svg.select('.title').remove();
  } else {
    var existing = this.svg.select('.title');
    if( existing.empty() ) {
      this.svg.append("text")
          .attr("class", "title")
          .text(title)
          .attr("x", this.cx/2)
          .attr("dy", this.padding.title)
          .style("text-anchor","middle");
    } else {
      existing.text(title);
    }
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
  self.xaxistitle = null;

  if( (typeof title !== 'string') || title.length === 0 )
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
  
  // clientWidth/clientHeight can make the height grow indefinitely inside a scrolling div
  const parentRect = this.chart.getBoundingClientRect();
  if( !parentRect.width || !parentRect.height )
  {
    return;
  }

  this.cx = parentRect.width;
  this.cy = parentRect.height;

  var titleh = 0, xtitleh = 0, xlabelh = 7 + 22;  // TODO: measure xlabelh (e.g. `.xaxis g.tick` bbox)

  if( this.options.txt.title ) {
    this.svg.selectAll(".title").each( function(d){
      titleh = this.getBBox().height;  
   });
  }
  
  const xaxistitleBB = this.xaxistitle ? this.xaxistitle.node().getBBox() : null;
  if( xaxistitleBB )
    xtitleh = xaxistitleBB.height;
  
  this.padding.topComputed = titleh + this.padding.top + (titleh > 0 ? this.padding.titlePad : 0);
  
  // Not sure where the -12 comes from, but it works out
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
  this.vis.attr("transform", "translate(" + this.padding.leftComputed + "," + this.padding.topComputed + ")");
  this.plot
      .attr("width", this.size.width)
      .attr("height", this.size.height);

  self.svg
      .attr("width", this.cx)
      .attr("height", this.cy)
      .attr("viewBox", "0 0 "+this.cx+" "+this.cy);

  d3.select("#clip"+ this.chart.id).select("rect")
    .attr("width", this.size.width )
    .attr("height", this.size.height );

  this.svg.selectAll(".title")
      .attr("x", this.cx/2);

  if( this.xaxistitle ){
    // xaxistitle is in xAxisBody; its text-anchor is "start" when compact, else "middle"
    if( this.hasCompactXAxis() ){
      this.xaxistitle.attr("x", this.size.width - xaxistitleBB.width - 10);
      this.xaxistitle.attr("dy", xtitleh + this.padding.xTitlePad );
    } else {
      this.xaxistitle.attr("x", 0.5*this.size.width);
      this.xaxistitle.attr("dy", xlabelh + this.padding.xTitlePad );
    }
  }

  this.xAxisBody.attr("transform", "translate(0," + this.size.height + ")");

  // Prevents initially rendering the chart with all ticks displayed
  if (this.cx !== prevCx && this.cy !== prevCy)
    this.xAxisBody.call(this.xAxis);

  this.mouseInfo.attr("transform","translate(" + this.size.width + "," + this.size.height + ")");

  if( this.xGrid ) {
    this.xGrid.innerTickSize(-this.size.height)
    this.xGridBody.attr("transform", "translate(0," + this.size.height + ")")
        .call(this.xGrid);
  }

  if( this.yGrid ) {
    this.yGrid.innerTickSize(-this.size.width)
    this.yGridBody.call(this.yGrid);
  }
  
  // Keep the legend visible
  if( this.legend && this.size.height > 50 && this.size.width > 100 && prevCx > 50 && prevCy > 50 ) {
    var trans = d3.transform(this.legend.attr("transform")).translate;
    var bb = this.legendBox.node().getBBox();
    
    // Anchor legend to whichever chart edge it's closer to (top-left reference, not center).
    var legx = (trans[0] < 0.5*prevCx) ? trans[0] : this.cx - (prevCx - trans[0]);
    var legy = (trans[1] < 0.5*prevCy) ? trans[1] : this.cy - (prevCy - trans[1]);
    
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
    // Re-rendering is enough for a geometry change (no setData rebuild needed)
    this.redraw()();
  }
}

/* Records the pan-gesture start state; shared by right-mouse-down and one-finger touch pan. */
SpectrumChartD3.prototype._startPanState = function(){
  this.rightClickDown = d3.mouse(document.body);
  this.is_panning = false;
  this.origdomain = this.xScale.domain();
  this.zooming_plot = false;   /* panning, not zooming */
};

/* Clamps [x0,x1] to [minx,maxx] preserving the requested width where possible: the window is
   shifted rather than shrunk, until it hits both edges.  Returns [newX0,newX1].
   Shared by pan, wheel, x-axis drag and drag-zoom-out. */
SpectrumChartD3.prototype._clampXRangeToData = function( x0, x1, minx, maxx ){
  if( x0 < minx ){
    x1 += (minx - x0);
    x0 = minx;
  }
  if( x1 > maxx ){
    x0 = Math.max( minx, x0 - (x1 - maxx) );
    x1 = maxx;
  }
  return [x0, x1];
};

SpectrumChartD3.prototype.handlePanChart = function () {
  var self = this;
  const bounds = self.hasSpectrumData() ? self.min_max_x_values() : [0, 3000];

  self.is_panning = true;

  // Document-body coordinates: vis coordinates made panning slightly buggy
  var docMouse = d3.mouse(document.body);

  if (!docMouse || !self.rightClickDown)
    return;

  const delta = docMouse[0] - self.rightClickDown[0];
  const range = self.xScale.range();
  const newX = self._clampXRangeToData( self.xScale.invert( range[0] - delta ),
                                        self.xScale.invert( range[1] - delta ),
                                        bounds[0], bounds[1] );

  self.setXAxisRange(newX[0], newX[1], false, true);
  self.redraw()();

  self.rightClickDown = docMouse;
}

/** -------------- Gesture-mode dispatch --------------
 The chart has six pointer "drag modes": fitPeak, deletePeaks, countGammas, recalibrate,
 zoomX and zoomY.  For MOUSE the mode is chosen once from the modifier keys at mousedown
 (sticky in this.leftDragMode until mouseup / ESC); for TOUCH it is re-recognized from
 finger geometry every touchmove (except fitPeak, which latches via leftDragMode).
 This table is the single source of truth for what runs per mode.  ROI-edge dragging and
 the y-scaler sliders are deliberately NOT modes - they preempt mode dispatch entirely. */
SpectrumChartD3.prototype._initGestureModes = function() {
  const self = this;
  this._gestureModes = {
    fitPeak: {
      mouseMove: function(){ self.handleMouseMovePeakFit(); },
      cancelMouse: null,              /* no mouse-side transient visuals of its own */
      cancelOthersOnMove: false,      /* committed gesture - never cancels others mid-move */
      recognize: function(){ return self._isControlDragSwipe(); },
      touchMove: function(){ self.handleMouseMovePeakFit(); },
      cancelTouch: function(){ self.handleCancelTouchPeakFit(); },  /* emits the final fitRoiDrag if a fit was in flight */
      touchEnd: function(){ self.handleCancelTouchPeakFit(); }
      /* mouseup: fitPeak completion runs explicitly FIRST in handleVisMouseUp - it must
         precede the ROI-drag mouseup, whose handleCancelRoiDrag would clear the latch. */
    },
    deletePeaks: {
      mouseMove: function(){ self.handleMouseMoveDeletePeak(); },
      extraMoveCancel: function(){ self.handleCancelRoiDrag(); },
      cancelMouse: function(){ self.handleCancelMouseDeletePeak(); },
      visUp: function(){ self.handleMouseUpDeletePeak(); },
      recognize: function(){ return self._isDeletePeakSwipe(); },
      touchMove: function(t){ self.handleTouchMoveDeletePeak(t); },
      cancelTouch: function(){ self.handleCancelTouchDeletePeak(); },
      touchEnd: function(){ self.handleTouchEndDeletePeak(); }
    },
    countGammas: {
      mouseMove: function(){ self.updateGammaSum(); },
      cancelMouse: function(){ self.handleCancelMouseCountGammas(); },
      visUp: function(){ self.handleMouseUpCountGammas(); },
      recognize: function(){ return self._isAltShiftSwipe(); },
      touchMove: function(){ self.updateGammaSum(); },
      cancelTouch: function(){ self.handleCancelTouchCountGammas(); },
      touchEnd: function(){ self.handleTouchEndCountGammas(); }
    },
    recalibrate: {                    /* mouse-only - no touch gesture maps to it */
      mouseMove: function(){ self.handleMouseMoveRecalibration(); },
      cancelMouse: function(){ self.handleCancelMouseRecalibration(); },
      visUp: function(){ self.handleMouseUpRecalibration(); }
    },
    zoomX: {
      mouseMove: function(){ self.handleMouseMoveZoomX(); },
      cancelMouse: function(){ self.handleCancelMouseZoomInX(); },
      /* vis mouseup requires the button to have been down >75ms - suppresses zooms from
         accidental micro-drags; the sub-75ms path still tears the zoom box down. */
      visUp: function(nowtime){
        if( (nowtime - self.mousedowntime) > 75 )
          self.handleMouseUpZoomX();
        else
          self.handleCancelMouseZoomInX();
      },
      recognize: function(){ return self._isZoomInPinch(false); },
      touchMove: function(){ self.handleTouchMoveZoomInX(); }
      /* the pinch applies live and draws nothing -> no cancelTouch/touchEnd */
    },
    zoomY: {
      mouseMove: function(){ self.handleMouseMoveZoomY(); },
      cancelMouse: function(){ self.handleCancelMouseZoomY(); },
      visUp: function(){
        if( self.zoomingYPlot || self.zoomInYMouse )
          self.handleMouseUpZoomY();
      },
      recognize: function(){ return self._isZoomInPinch(true); },
      touchMove: function(){ self.handleTouchMoveZoomY(); },
      cancelTouch: function(){ self.handleTouchCancelZoomY(); },
      touchEnd: function(){ self.handleTouchEndZoomY(); }
    }
  };

  /* Touch recognizer priority: peak fit first - once started it must run to completion (else a
     phantom peak the C++ doesn't know about could be left on the chart). */
  this._touchModeOrder = ['fitPeak','deletePeaks','countGammas','zoomX','zoomY'];

  /* Teardown order for touch-end and no-gesture touchmoves; fitPeak's teardown can emit. */
  this._touchEndOrder = ['countGammas','deletePeaks','fitPeak','zoomY'];

  /* Mouse-up completion order in handleVisMouseUp, run AFTER the explicit fitPeak and
     ROI-drag completions (fitPeak must precede the ROI-drag mouseup - see its entry). */
  this._visUpOrder = ['deletePeaks','recalibrate','zoomY','zoomX','countGammas'];
};

/* Cancels the touch-side transient visuals of every mode except `activeName` (null cancels all),
   in _touchEndOrder, since fitPeak's cancel can emit a final fitRoiDrag. */
SpectrumChartD3.prototype._cancelOtherTouchModes = function( activeName ){
  const self = this;
  this._touchEndOrder.forEach( function(name){
    const mode = self._gestureModes[name];
    if( (name !== activeName) && mode.cancelTouch )
      mode.cancelTouch();
  } );
};

/* Clears the slider-chart drag-in-progress flags/pointers; shared by several teardown paths. */
SpectrumChartD3.prototype._resetSliderDragFlags = function(){
  this.sliderBoxDown = false;
  this.leftDragRegionDown = false;
  this.rightDragRegionDown = false;
  this.sliderChartPointer = null;
  this.savedSliderPointer = null;
};

/** -------------- Touch gesture recognizers --------------
 All are pure reads of this.touchesOnChart (page-coordinate start/current positions)
 and this.leftDragMode; called from the touch-move dispatch every touchmove. */

/* The two touches on the chart, as [t1, t2], or null if there aren't exactly two. */
SpectrumChartD3.prototype._twoTouches = function(){
  const keys = this.touchesOnChart ? Object.keys(this.touchesOnChart) : [];
  return (keys.length === 2) ? [this.touchesOnChart[keys[0]], this.touchesOnChart[keys[1]]] : null;
};

/* Delete-peak = two fingers, separated horizontally, swiping upward together. */
SpectrumChartD3.prototype._isDeletePeakSwipe = function(){
  const tt = this._twoTouches();
  if( !tt )
    return false;

  const [t1, t2] = tt, maxDyDiff = 15, minDxDiff = 25;
  const dy1 = t1.startY - t1.pageY, dy2 = t2.startY - t2.pageY;
  if( (Math.abs(dy2 - dy1) > maxDyDiff) || (Math.abs(t1.pageX - t2.pageX) < minDxDiff) )
    return false;

  const dy = Math.min(dy1, dy2), dx = Math.abs( (t1.startX - t1.pageX) - (t2.startX - t2.pageX) );
  return dy > dx && dy > maxDyDiff;
};

/* Peak-fit = two fingers roughly level, constant separation, moving right; latches once started. */
SpectrumChartD3.prototype._isControlDragSwipe = function(){
  if( !this.touchesOnChart )
    return false;

  if( this.leftDragMode === 'fitPeak' )   /* the latch */
    return true;

  const tt = this._twoTouches();
  if( !tt )
    return false;

  const [t1, t2] = tt;
  if( (t1.startX > t1.pageX) || (t2.startX > t2.pageX)
      || ![t1.startX, t1.pageX, t2.startX, t2.pageX, t1.startY, t1.pageY, t2.startY, t2.pageY].every( isFinite ) )
    return false;

  const startdx = t1.startX - t2.startX, nowdx = t1.pageX - t2.pageX, yavrg = 0.5*(t1.startY + t2.startY);
  if( (Math.abs(yavrg - t1.pageY) > 20) || (Math.abs(yavrg - t2.pageY) > 20) || (Math.abs(startdx - nowdx) > 20) )
    return false;

  return Math.abs(t1.pageX - t1.startX) > 30;
};

/* Count-gammas = two fingers stacked vertically (~same x), swiping right together. */
SpectrumChartD3.prototype._isAltShiftSwipe = function(){
  const tt = this._twoTouches();
  if( !tt )
    return false;

  const [t1, t2] = tt;
  if( (Math.abs(t1.startX - t2.startX) > 20) || (Math.abs(t1.pageX - t2.pageX) > 25) )
    return false;

  return (t1.pageX - t1.startX) > 30;
};

/* Pinch zoom: y_direction=false -> horizontal separation change (zoom x);
   y_direction=true -> vertical separation change (zoom y). */
SpectrumChartD3.prototype._isZoomInPinch = function( y_direction ){
  const tt = this._twoTouches();
  if( !tt )
    return false;

  const [t1, t2] = tt;
  const ddx = Math.abs( Math.abs(t1.pageX - t2.pageX) - Math.abs(t1.startX - t2.startX) );
  const ddy = Math.abs( Math.abs(t1.pageY - t2.pageY) - Math.abs(t1.startY - t2.startY) );

  if( y_direction )
    return ((ddx < 0.5*ddy) && (ddy > 20));
  return ((ddy < ddx) && (ddx > 5));
};

/* Cancels the mouse-side transient visuals/state of every mode except `activeName` (the cancels
   touch disjoint DOM/state, so their order doesn't matter). */
SpectrumChartD3.prototype._cancelOtherMouseModes = function( activeName ){
  for( const name in this._gestureModes ){
    const mode = this._gestureModes[name];
    if( (name !== activeName) && mode.cancelMouse )
      mode.cancelMouse();
  }
};

/** -------------- Chart Event Handlers (outer chart element) -------------- */
SpectrumChartD3.prototype.handleChartMouseMove = function() {
  var self = this;

  return function() {
    self.mousemove()();

    if( !self.hasSpectrumData()
        || self.xaxisdown || !isNaN(self.yaxisdown) || self.legdown ){
      return;
    }

    const m = self.getMousePos();
    const x = m[0], y = m[1];

    // Clamp, in case the user starts dragging from out of bounds
    self.lastMouseMovePos = m;
    m[1] = Math.min( Math.max( m[1], 0 ), self.size.height );

    self.handleMouseMoveSliderChart()();
    self.handleMouseMoveScaleFactorSlider()();

    // Moving the mouse removes the grey lines showing where the screen was touched
    self.deleteTouchLine();

    // d3.event.buttons is missing in Safari, and .button is less consistent, so we track which button is down
    if ((d3.event.button === 0 && self.leftMouseDown)) {        // left-click drag
      /* Mode was captured at mousedown and is sticky (ESC clears it via handleCancelAllMouseEvents);
         a ROI drag preempts mode dispatch. */
      if( !self.roiIsBeingDragged ){
        const mode = self._gestureModes[self.leftDragMode];
        if( !mode ){
          /* 'altOnly' (reserved) or 'none' (no mode / ESC-cancelled) -> ensure visuals are torn down */
          self.handleCancelAllMouseEvents()();
        }else{
          if( mode.cancelOthersOnMove !== false )
            self._cancelOtherMouseModes( self.leftDragMode );
          if( mode.extraMoveCancel )
            mode.extraMoveCancel();
          mode.mouseMove();
        }
      }

      return;
    } else if ( self.rightClickDown ){
      // Right-click drag pans the chart
      self.handleCancelRoiDrag();
      self.handlePanChart();
    } else if( x >= 0 && y >= 0 && y <= self.size.height && x <= self.size.width
               && !d3.event.altKey && !d3.event.ctrlKey && !d3.event.metaKey
               && !d3.event.shiftKey && (self.leftDragMode !== 'fitPeak') && !self.escapeKeyPressed ) {
      // No button down, and no modifier keys held
      if( self.kineticRefLines )
        self.handleUpdateKineticRefLineUpdate();

      if( self.options.allowDragRoiExtent && self.roiDragBoxes && self.showDragLineWhileInRoi && !self.roiIsBeingDragged ){
        // The user clicked a peak to show the ROI drag handles; cancel them once the mouse leaves the ROI
        const dx = self.options.roiDragWidth, xRangePx = self.roiBeingDragged.xRangePx;
        if( (x < (xRangePx[0] - dx)) || (x > (xRangePx[1] + dx)) )
          self.handleCancelRoiDrag();
      }else if( self.options.allowDragRoiExtent && !self.roiIsBeingDragged ) {
        const drawn_roi = self.getDrawnRoiForCoordinate( m );
        const dx = 0.5 * self.options.roiDragWidth;
        
        // No handles for narrow ROIs (the user can zoom in), or when not on an edge
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

/* Returns the pointer position as [x_vis, y_vis, x_svg, y_svg]: relative to this.vis (the plot area
   inside the axes), and to the svg. */
SpectrumChartD3.prototype.getMousePos = function(){
  const pad_left = this.padding.leftComputed;
  const pad_top = this.padding.topComputed;

  // Cleared only on the defensive no-position fallback below, so callers can tell a real
  // hover at the data origin [0,0] apart from "no mouse/finger has been over the chart yet."
  this.mousePosIsValid = true;

  if( d3.event )
  {
    try {
      const m = d3.mouse(this.vis[0][0]);
      if( m )
        return [m[0], m[1], m[0] + pad_left, m[1] + pad_top];
    } catch(e) {
      // d3.mouse() can throw if event has non-finite coordinates (e.g., window losing focus)
    }

    const t = d3.event.changedTouches;
    if( t && t.length ){
      try {
        const vt = d3.touches(this.vis[0][0], t);
        if( vt && vt.length )
          return [vt[0][0], vt[0][1], vt[0][0] + pad_left, vt[0][1] + pad_top];
      } catch(e) {
        // d3.touches() can similarly throw with non-finite coordinates
      }
    }
  }//if( d3.event )

  if( this.lastMouseMovePos )
    return this.lastMouseMovePos;

  if( this.lastTapEvent )
    return this.lastTapEvent.visCoordinates;

  // We can fail to get mouse position if a mouse/finger hasnt been over the chart yet.
  console.warn( "Failed to find mouse position!" );

  // Defensive fallback.
  this.mousePosIsValid = false;
  return [0, 0, pad_left, pad_top];
}//getMousePos(...)

SpectrumChartD3.prototype.showRoiDragOption = function(info, mouse_px, showBoth, showMoreButton ){
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

  self.roiBeingDragged = { roi: roi, yRangePx: info.yRangePx, xRangePx: info.xRangePx, color: (info.color ? info.color : 'black'), spectrumIndex: info.spectrumIndex };
  
  if( !self.roiDragBoxes ){
    // The vertical line + grab box at each ROI edge; moved as the mouse moves
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

      // A little texture inside the box
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
        SpectrumChartD3.stopEvent();
      };
    
      let startDragFcn = function(){
        // Fires when you click down in the self.roiDragBoxes areas
        const m = self.getMousePos();
        self.lastMouseMovePos = m;
        
        self.handleStartDragRoi( m );
        
        SpectrumChartD3.stopEvent();
        
        d3.select(self.chart).on("mousemove.roidrag", adjustRoiFcn ).on("touchmove.roidrag", adjustRoiFcn );
      };
        
      let endDragFcn = function(){
        // Fires when you let the mouse up, or lift your finger
        d3.select(self.chart).on("mousemove.roidrag", null ).on("touchmove.roidrag", null );
        
        const m = self.getMousePos();
        self.lastMouseMovePos = m;
        self.handleMouseUpDraggingRoi( m );
        
        SpectrumChartD3.stopEvent();
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

  const [y1, y2] = self._roiDragYExtent( info.yRangePx, showBoth );
  const y_middle = 0.5*(y1 + y2);
  self.roiDragBoxes[0].attr("transform", "translate(" + lpx + "," + y_middle + ")");
  self.roiDragBoxes[1].attr("transform", "translate(" + upx + "," + y_middle + ")");
  
  self.vis.selectAll('.roiDragBoxLine').attr("stroke", self.roiBeingDragged.color );
      
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
    // Shown because the mouse went over a ROI edge: only show that edge's handle
    self.roiDragBoxes[on_lower ? 1 : 0].style("visibility", "hidden");
  }

  // "more..." button: discoverable touch shortcut to the long-press context menu.  Only when the user
  // explicitly tapped a peak/ROI (showBoth), not for a passive mouse-hover.
  if( showBoth && showMoreButton ){
    // Snapshot the energy to emit, as self.roiBeingDragged can be cleared (by a synthesized
    // mouseup) before the user taps.
    const peak0 = (info.roi.peaks && info.roi.peaks[0]) ? info.roi.peaks[0] : null;
    const triggerEnergy = (peak0 && peak0.Centroid) ? peak0.Centroid[0] : 0.5*(info.roi.lowerEnergy + info.roi.upperEnergy);

    if( !self.roiMoreButton ){
      const g = this.vis.append("g").attr("class", "roiMoreButton");
      const rect = g.append("rect").attr("class", "roiMoreButtonRect").attr("rx", 3).attr("ry", 3);
      const text = g.append("text").attr("class", "roiMoreButtonText")
        .attr("text-anchor", "middle").attr("dominant-baseline", "central")
        .text("more...");
      const trigger = function (e) {
        if (e) { e.preventDefault(); e.stopPropagation(); }
        // Page coords from the rect (the parent <g>'s bounding rect can be all-zero on some
        // browsers); fall back to the event coords if the rect's box is empty.
        let pageX = 0, pageY = 0;
        const rectBox = rect.node().getBoundingClientRect();
        if (rectBox && rectBox.width > 0) {
          pageX = rectBox.left + 0.5*rectBox.width;
          pageY = rectBox.bottom + 4;  // a few px below the button so the menu doesn't cover it
        } else if (e && e.changedTouches && e.changedTouches[0]) {
          pageX = e.changedTouches[0].pageX;
          pageY = e.changedTouches[0].pageY;
        } else if (e && typeof e.pageX === 'number') {
          pageX = e.pageX;
          pageY = e.pageY;
        }
        const energy = self.roiMoreButton ? self.roiMoreButton.energy : triggerEnergy;
        self.WtEmit(self.chart.id, {name: 'rightclicked'}, energy, 0, pageX, pageY, self.currentRefLineInfoStr());
      };
      const node = g.node();
      node.addEventListener("click",      trigger, false);
      node.addEventListener("touchend",   trigger, false);
      // Swallow touchstart/mousedown so the press isn't a new tap on the chart (closing the handles)
      node.addEventListener("touchstart", function(e){ e.preventDefault(); e.stopPropagation(); }, false);
      node.addEventListener("mousedown",  function(e){ e.preventDefault(); e.stopPropagation(); }, false);
      self.roiMoreButton = { g: g, rect: rect, text: text, energy: triggerEnergy };
    } else {
      // Re-use existing button -- update the snapshot energy in case the ROI moved.
      self.roiMoreButton.energy = triggerEnergy;
    }
    const centerX = 0.5*(lpx + upx);
    const tbox = self.roiMoreButton.text.node().getBBox();
    self.roiMoreButton.rect
      .attr("x", tbox.x - 8).attr("y", tbox.y - 4)
      .attr("width", tbox.width + 16).attr("height", tbox.height + 8);
    const buttonY = Math.max( 14, y1 - 14 );
    self.roiMoreButton.g
      .attr("transform", "translate(" + centerX + "," + buttonY + ")")
      .style("visibility", "visible");
  } else if( self.roiMoreButton ){
    self.roiMoreButton.g.style("visibility", "hidden");
  }

  self.showDragLineWhileInRoi = showBoth;
};//showRoiDragOption()

/* Mouse went down on a ROI edge and has now moved: update the ROI drag handles. */
SpectrumChartD3.prototype.handleRoiDrag = function(m){
  const self = this;
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

  const roiinfo = self.roiBeingDragged;
  const mdx = self.roiDragMouseDown;
  const xcenter = x + mdx[1] - mdx[0];
  const lowerEdgeDrag = mdx[3];
  const spectrum_type = self.rawData.spectra[roiinfo.spectrumIndex].type;

  self.roiDragLastCoord = [x, y, self.xScale.invert(xcenter), self.yScale.invert(y)];

  const [y1, y2] = self._roiDragYExtent( roiinfo.yRangePx, self.showDragLineWhileInRoi );
  const y_middle = 0.5*(y2 + y1);
  self.roiDragBoxes[lowerEdgeDrag ? 0 : 1].attr("transform", "translate(" + xcenter + "," + y_middle + ")");
  
  for( let i = 0; i < 2; ++i ){
    self.roiDragLines[i]
      .attr("y1", y1 - y_middle)
      .attr("y2", y2 - y_middle);
  }

  // Emit the current position, at most twice per second
  self._throttledDragEmit( 500, function(){
    self._emitRoiDrag( roiinfo.roi, xcenter, lowerEdgeDrag, spectrum_type, false );
  });
};//SpectrumChartD3.prototype.handleRoiDrag = ...

/* The [y1, y2] pixel extent of the ROI drag lines: the ROI's y-range, padded by roiDragLineExtent
   (doubled when both handles show), within the plot. */
SpectrumChartD3.prototype._roiDragYExtent = function( yRangePx, showBoth ){
  if( !yRangePx || (yRangePx.length != 2) )
    return [0, this.size.height];
  const dheight = (showBoth ? 2 : 1) * this.options.roiDragLineExtent;
  return [Math.max(0, yRangePx[0] - dheight), Math.min(this.size.height, yRangePx[1] + dheight)];
};

/* Emits 'roiDrag' for the lower or upper edge of `roi` dragged to pixel `xcenter`. */
SpectrumChartD3.prototype._emitRoiDrag = function( roi, xcenter, lowerEdgeDrag, spectrumType, isFinal ){
  const energy = this.xScale.invert(xcenter);
  const lowerPx = lowerEdgeDrag ? xcenter : this.xScale(roi.lowerEnergy);
  const upperPx = lowerEdgeDrag ? this.xScale(roi.upperEnergy) : xcenter;
  this.WtEmit( this.chart.id, {name: 'roiDrag'}, (lowerEdgeDrag ? energy : roi.lowerEnergy),
               (lowerEdgeDrag ? roi.upperEnergy : energy), (upperPx - lowerPx), roi.lowerEnergy, spectrumType, isFinal );
};

/* Throttles drag emits to at most one per intervalMs, with a trailing-edge emit.  The shared
   roiDragRequest* state is flushed/cleared by handleCancelRoiDrag and updateRoiBeingDragged. */
SpectrumChartD3.prototype._throttledDragEmit = function( intervalMs, emitFcn ){
  const self = this;
  const fire = function(){
    self.roiDragRequestTime = new Date();
    window.clearTimeout( self.roiDragRequestTimeout );
    self.roiDragRequestTimeout = null;
    self.roiDragRequestTimeoutFcn = null;
    emitFcn();
  };

  const timenow = new Date();
  if( self.roiDragRequestTime === null || (timenow - self.roiDragRequestTime) > intervalMs ){
    fire();
  } else {
    const dt = Math.min( intervalMs, Math.max(0, intervalMs - (timenow - self.roiDragRequestTime)) );
    window.clearTimeout( self.roiDragRequestTimeout );
    self.roiDragRequestTimeoutFcn = fire;
    self.roiDragRequestTimeout = window.setTimeout( function(){
      if( self.roiDragRequestTimeoutFcn )
        self.roiDragRequestTimeoutFcn();
    }, dt );
  }
};//SpectrumChartD3.prototype._throttledDragEmit

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
  // Hide the "more..." shortcut while resizing
  if( self.roiMoreButton )
    self.roiMoreButton.g.style("visibility", "hidden");

  const roiPx = (on_lower ? lpx : upx);
  self.roiDragMouseDown = [mouse_pos_px[0], roiPx, energy, on_lower];

  const edge_index = on_lower ? 0 : 1;
  self.roiDragBoxes[edge_index].attr("class", "roiDragBox active");
  self.roiDragLines[edge_index].attr("class", "roiDragLine active");
};//SpectrumChartD3.prototype.handleStartDragRoi

SpectrumChartD3.prototype.handleCancelRoiDrag = function(){
  let self = this;
  if( !self.roiDragBoxes && (self.leftDragMode !== 'fitPeak') && !self.roiMoreButton )
    return;

  if( self.roiDragBoxes ){
    self.roiDragBoxes[0].remove();
    self.roiDragBoxes[1].remove();
  }

  if( self.roiMoreButton ){
    self.roiMoreButton.g.remove();
    self.roiMoreButton = null;
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
  // Only clear a fit-peak mode: other modes may have ROI handles left visible from a prior hover
  if( self.leftDragMode === 'fitPeak' )
    self.leftDragMode = 'none';
  self.showDragLineWhileInRoi = false;
  window.clearTimeout(self.roiDragRequestTimeout);
  self.roiDragRequestTimeout = null;
  self.roiDragRequestTimeoutFcn = null;
  d3.select('body').style("cursor", "default");
  d3.select(self.chart).on("mousemove.roidrag", null ).on("touchmove.roidrag", null );

  // Redraw the ROI in its original state
  if( wasBeingDrug )
    self.redraw()();
};

SpectrumChartD3.prototype.handleMouseUpDraggingRoi = function( m ){
  const self = this;
  
  if( !self.roiIsBeingDragged ){
    self.handleCancelRoiDrag();
    return;
  }

  const mdx = self.roiDragMouseDown; // [m[0], roiPx, energy, isLowerEdge];
  const spectrum_type = self.rawData.spectra[self.roiBeingDragged.spectrumIndex].type;
  self._emitRoiDrag( self.roiBeingDragged.roi, m[0] + mdx[1] - mdx[0], mdx[3], spectrum_type, true );
  self.handleCancelRoiDrag();
};//SpectrumChartD3.prototype.handleMouseUpDraggingRoi

/* Updates only the temporary ROI being dragged/fit; final changes come through setRoiData(...). */
SpectrumChartD3.prototype.updateRoiBeingDragged = function( newroi ){
  let self = this;
  
  if( !self.roiIsBeingDragged && (self.leftDragMode !== 'fitPeak') )
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

      const to = d3.event.relatedTarget;
      if( !d3.select(to)[0].parentNode
          || to === document.body
          || to.nodeName === "HTML"
          || to.nodeName === "DIV"
          || to.offsetParent === document.body) {
        self._cancelOtherMouseModes( null );   /* null -> every mode */
        self.handleCancelRoiDrag();
        self._resetTransientPointerState();
      }

      self.updateFeatureMarkers(-1);
      self._clearKineticRefLine();

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
    self._endXAxisDrag();
    self.yaxisdown = NaN;
    d3.select(document.body).style("cursor", "default");

    // A peak fit released outside the plot area (inside it, handleVisMouseUp completed it already)
    if( self.leftDragMode === 'fitPeak' )
      self.handleMouseUpPeakFit();

    self.handleMouseUpDeletePeak();
    self.handleMouseUpZoomX();
    self.handleMouseUpDraggingRoi( self.getMousePos() );
    self.handleMouseUpZoomY();
    self.handleMouseUpRecalibration();
    self.handleMouseUpCountGammas();
    self.drawRefGammaLines();

    self.lastMouseMovePos = null;
    self.sliderChartPointer = null;
  }
}

SpectrumChartD3.prototype.handleChartWheel = function () {
  var self = this;

  return function() {
    if (!d3.event)
      return;

    SpectrumChartD3.stopEvent();

    // Wheel over the y-axis zooms the y-axis
    var m = d3.mouse(self.vis[0][0]);
    if (m[0] < 0 && m[1] > 0 && m[1] < self.size.height && self.options.wheelScrollYAxis) {
      self.handleYAxisWheel();
      return;
    }
  }
}

SpectrumChartD3.prototype.handleChartTouchStart = function() {
  return SpectrumChartD3.stopEvent;
}

SpectrumChartD3.prototype.handleChartTouchEnd = function() {
  var self = this;

  return function() {
    SpectrumChartD3.stopEvent();
    self._resetSliderDragFlags();
  }
}

/** Returns the ROI path object corresponding to the coordinates passed in.
 @param coordinates the [x,y] coordinates of e.g., mouse or touch
 @returns the roi, ex. {path:..., roi: {type:'linear',...}, lowerEnergy: 50, upperEnergy: 70, yRangePx: [12, 34], xRangePx: [40,50], color: 'black', isOutline: true, isFill: false, peak: {...}}
 */
SpectrumChartD3.prototype.getDrawnRoiForCoordinate = function( coordinates ){
  if( !coordinates || (coordinates.length < 2) || !this.peakPaths || !this.peakPaths.length )
    return null;
    
  let paths = null;
  const x = coordinates[0];
  const dx = 0.5 * this.options.roiDragWidth;
  let min_dist = 9.999E12;

  // TODO: bisect to find the ROI (faster with very many peaks)
  for( let j = 0; j < this.peakPaths.length; ++j ){
    const info = this.peakPaths[j];
    if( !info.isOutline || !info.xRangePx )
      continue;
    
    const lpx = info.xRangePx[0], upx = info.xRangePx[1];
    if( ((x >= (lpx-dx)) && (x <= (upx+dx))) )
    {
      if( info.path[0][0] === this.highlightedPeak ) // prefer the highlighted peak
        return info;
      const dist = Math.abs(x - 0.5*(lpx + upx));
      if( dist < min_dist ){
        paths = info;
        min_dist = dist;
      }
    }
  }//for( loop over self.peakPaths )
    
  return paths;
}//SpectrumChartD3.prototype.getDrawnRoiForCoordinate( [x,y] )

SpectrumChartD3.prototype.setMouseDownRoi = function( coordinates ){
  console.assert( coordinates && (coordinates.length >= 2), 'setMouseDownRoi: coordinates null' );
  this.mouseDownRoi = this.getDrawnRoiForCoordinate( coordinates );
}

/** -------------- Vis Event Handlers (inner plot area) -------------- */
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

    SpectrumChartD3.stopEvent();

    self.zoomInYMouse = d3.event.metaKey ? m : null;
    self.leftMouseDown = null;
    self.touchHoldEmitted = false;
    
    if( self.currentKineticRefLine )
      self.drawRefGammaLines();

    // Left button down inside the plot.  `buttons` is checked as Firefox reports Ctrl-click as a right click.
    if( (d3.event.buttons === 1 || d3.event.button === 0) && m[0] >= 0 && m[0] < self.size.width && m[1] >= 0 && m[1] < self.size.height ) {
      self.escapeKeyPressed = false;
      d3.select('body').style("cursor", "move");

      self.leftMouseDown = m;
      self.origdomain = self.xScale.domain();
      self.zoominaltereddomain = false;
      self.recalibrationStartEnergy = self.xScale.invert(m[0]);

      // Capture the drag mode once, from the modifier keys; sticky for the gesture (ESC sets 'none')
      const modeForModifiers = { 0: 'zoomX', 1: 'deletePeaks', 2: 'fitPeak', 4: 'altOnly', 5: 'countGammas', 6: 'recalibrate', 8: 'zoomY' };
      self.leftDragMode = modeForModifiers[SpectrumChartD3.modifierBits(d3.event)] || 'none';

      self.setMouseDownRoi( m );

      if( !self.roiDragLines) {
        if( self.leftDragMode !== 'fitPeak' && !self.roiIsBeingDragged ) {
          self.vis.selectAll("#zoomInXBox,#zoomInXText").remove();
          self.vis.append("rect")
            .attr("id", "zoomInXBox")
            .attr("class","leftbuttonzoombox")
            .attr("width", 1 )
            .attr("height", self.size.height)
            .attr("x", m[0])
            .attr("y", 0)
            .attr("pointer-events", "none");
        }

        self.updateFeatureMarkers(-1);

        // zooming_plot is armed by the first zoom-X mousemove; the 75ms gate in handleVisMouseUp
        // suppresses accidental zooms.
        self.zooming_plot = false;
      }
      return false;
    } else if ( d3.event.button === 2 ) {
      self._startPanState();
    }
  }
}

SpectrumChartD3.prototype.getMouseUpOrSingleFingerUpHandler = function( coords, modKeyDown, isTouch ) {
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

    self.WtEmit(self.chart.id, {name: 'leftclicked'}, coords[4], coords[5], coords[2], coords[3], self.currentRefLineInfoStr() );

    if( self.options.allowDragRoiExtent && self.mouseDownRoi && !modKeyDown ){
      // "more..." button is touch-only -- desktop users get the right-click menu instead.
      self.showRoiDragOption(self.mouseDownRoi, [coords[0],coords[1]], true, !!isTouch);
    }else{
      self.unhighlightPeak(null);

      // updatePeakInfo() keys off highlightedPeak and already ran (in touchend/mouseup) before
      // this deferred handler cleared it; without this the old box lingers until the next tap.
      self.updatePeakInfo( [coords[0], coords[1]] );
    }

    self.mousewait = null;
    self.mouseDownRoi = null;
  };
};//getMouseUpOrSingleFingerUpHandler

/* Cancels a pending long-press (touch-hold) timer. */
SpectrumChartD3.prototype._clearTouchHold = function(){
  if( this.touchHold ){
    window.clearTimeout( this.touchHold );
    this.touchHold = null;
  }
};

/* Clears a pending deferred single-click/tap emit (see _scheduleSingleClickEmit). */
SpectrumChartD3.prototype._clearPendingClickEmit = function(){
  if( this.mousewait ){
    window.clearTimeout(this.mousewait);
    this.mousewait = null;
  }
};

/** Schedules the single-click/tap emit to fire after doubleClickDelay, unless a second click/tap
 makes it a double first.  When replacePending is false, an already-pending emit is kept: Android
 WebView fires touchend twice for many taps, and replacing would lose the original coordinates. */
SpectrumChartD3.prototype._scheduleSingleClickEmit = function( coords, modKeyDown, isTouch, replacePending ){
  if( !replacePending && this.mousewait )
    return;
  this.mousewait = window.setTimeout(
    this.getMouseUpOrSingleFingerUpHandler( coords, modKeyDown, isTouch ),
    this.options.doubleClickDelay
  );
};

SpectrumChartD3.prototype.currentRefLineInfoStr = function () {
  let ref_line = "";
  if( this.mousedOverRefLine && this.mousedOverRefLine.__data__ && this.mousedOverRefLine.__data__.parent )
    ref_line = this.mousedOverRefLine.__data__.parent.parent;
  else if( this.currentKineticRefLine && this.currentKineticRefLine.parent )
    // Nothing hovered, but a dynamic line is showing - associate with it (touch has no hover).
    // currentKineticRefLine is the src_lines group, so .parent matches the hover branch above.
    ref_line = this.currentKineticRefLine.parent;
  return ref_line;
}

SpectrumChartD3.prototype.handleVisMouseUp = function () {
  var self = this;

  return function () {

    if (!d3.event)
      return;

    const m = d3.mouse(self.vis[0][0]);
    const x = m[0], y = m[1];
    const pageX = d3.event.pageX;
    const pageY = d3.event.pageY;
    const energy = self.xScale.invert(x);
    const count = self.yScale.invert(y);

    self.mouseup()();
    self.dragging_plot = false;
    self.updateFeatureMarkers(null);

    // Ignore clicks on the slider chart
    if (y >= (self.size.height + 
      (self.xaxistitle != null && !d3.select(self.xaxistitle).empty() ? self.xaxistitle[0][0].clientHeight + 20 : 20) + 
      self.padding.sliderChart)) {
      return;
    }

    if (d3.event.button === 2 && !self.is_panning && !d3.event.ctrlKey) {
      self.WtEmit(self.chart.id, {name: 'rightclicked'}, energy, count, pageX, pageY, self.currentRefLineInfoStr());
      self.handleCancelAllMouseEvents()();

      return;
    }
    
    const modifiers = SpectrumChartD3.modifierBits( d3.event );
    const modKeyPressed = ((modifiers != 0) || self.escapeKeyPressed);

    // Clicks and double-clicks: mouse released near where it went down, and not held long
    const nowtime = new Date();
    if (self.mousedownpos && self.dist(self.mousedownpos, m) < 5) {
      if( (nowtime - self.mousedowntime) < self.options.doubleClickDelay ) {
        if( self.lastClickEvent && ((nowtime - self.lastClickEvent) < self.options.doubleClickDelay) ) {
          // Double-click
          if( self.mousewait ) {
            self._clearPendingClickEmit();
            self.mouseDownRoi = null;
          }
          
          self.WtEmit(self.chart.id, {name: 'doubleclicked'}, energy, count, self.currentRefLineInfoStr(), modifiers );
        } else {
          // First click - maybe there will be another
          if( !modKeyPressed )
            self.updateFeatureMarkers( energy );

          if( self.candidateKineticRefLines && (self.candidateKineticRefLines.length > 1) )
            self._toggleKineticCycle();

          self._scheduleSingleClickEmit( [x,y,pageX,pageY,energy,count], modKeyPressed, false, true );
        }
        self.lastClickEvent = nowtime;
      }
    }

    if( self.xaxisdown || !isNaN(self.yaxisdown) || self.legdown )
      return;

    // Peak fitting must complete before the ROI-drag mouseup, whose handleCancelRoiDrag clears the latch
    if (self.leftDragMode === 'fitPeak')
      self.handleMouseUpPeakFit();

    self.handleMouseUpDraggingRoi(m);

    // Complete the active gesture mode (inactive modes no-op; zoomX has the 75ms anti-accidental gate)
    self._visUpOrder.forEach( function(name){
      self._gestureModes[name].visUp( nowtime );
    } );

    self.endYAxisScalingAction()();

    let domain = self.xScale.domain();
    if( !self.origdomain || !domain || self.origdomain[0]!==domain[0] || self.origdomain[1]!==domain[1] ){
      self.WtEmit(self.chart.id, {name: 'xrangechanged'}, domain[0], domain[1], self.size.width, self.size.height, false );
    }

    self._resetTransientPointerState();
  }
}

SpectrumChartD3.prototype.handleVisWheel = function () {
  var self = this;

  return function () {
    const e = d3.event;
    SpectrumChartD3.stopEvent();

    // Ignore the wheel with modifier keys - except a MacBook two-finger pinch, which gives ctrlKey and composed
    if( e.altKey || (e.ctrlKey && !e.composed) || e.shiftKey || e.metaKey || e.button != 0 ) {
     return;
    }

    var m = d3.mouse(self.vis[0][0]);

    if( m[0] < 0 || m[0] > self.size.width || m[1] < 0 || m[1] > self.size.height ){
      // Wheel over the y-axis labels zooms the y-axis
      if (m[0] < 0 && m[1] > 0 && m[1] < self.size.height && self.options.wheelScrollYAxis && self.hasSpectrumData())
        self.handleYAxisWheel();
      return;
    }

    if( self.dragging_plot || (self.leftDragMode === 'fitPeak') ){
      return;
    }

    if( !self.hasSpectrumData() ){
      return;
    }

    const foreground = self.rawData.spectra[0];
    const [mindatax, maxdatax] = self.min_max_x_values();
    const currentdomain = self.xScale.domain();

    // Ends the wheel action: clears the scroll state, and emits the final range.  Runs once the
    //  timer expires after the last wheel event.
    function wheelcleanup(){
      self.wheeltimer = null;
      self.scroll_start_x = null;
      self.scroll_start_domain = null;
      self.scroll_start_raw_channel = null;
      self.scroll_total_x = null;
      self.scroll_total_y = null;

      let domain = self.xScale.domain();
      self.WtEmit(self.chart.id, {name: 'xrangechanged'}, domain[0], domain[1], self.size.width, self.size.height, true);
    };

    // At a panning boundary and scrolling further that way: reset the scroll state, so reversing
    //  direction reacts immediately (instead of first unwinding the excess scroll).
    if ((currentdomain[1] >= (maxdatax-0.00001) && e.deltaX > 0)
        || (currentdomain[0] <= (mindatax+0.00001) && e.deltaX < 0)) {
      if( self.wheeltimer ){
        window.clearTimeout(self.wheeltimer);
        wheelcleanup();
      }

      return;
    }

    // A wheel action ends 250 ms after its last wheel event (timer set below).
    if( self.wheeltimer ){
      window.clearTimeout(self.wheeltimer);
    } else {
      // First wheel event of this action: record the starting energy, counts, and x-range
      self.scroll_start_x = self.xScale.invert(m[0]);
      self.scroll_start_domain = self.xScale.domain();
      self.scroll_start_raw_channel = Math.min( foreground.x.length - 1,
                                                Math.max( 0, self._bisecRaw.left(foreground.x, self.scroll_start_x) ) );
      self.scroll_total_x = 0;
      self.scroll_total_y = 0;
      self.handleCancelRoiDrag();
    }

    // Total scroll units of this wheel action
    self.scroll_total_x += e.deltaX;
    self.scroll_total_y += e.deltaY;

    // Fully zoomed out at scroll_total_y = +MAX_SCROLL_TOTAL, or in to 3 bins at -MAX_SCROLL_TOTAL
    const MAX_SCROLL_TOTAL = 1500;
    self.scroll_total_y = Math.min( Math.max( self.scroll_total_y, -MAX_SCROLL_TOTAL ), MAX_SCROLL_TOTAL );

    var initial_range_x = self.scroll_start_domain[1] - self.scroll_start_domain[0];
    var terminal_range_x;
    if( self.scroll_total_y > 0 ){
      terminal_range_x = (maxdatax - mindatax);
    } else {
      // One bin left of the original mouse position, to two to the right
      const ch = self.scroll_start_raw_channel;
      terminal_range_x = foreground.x[Math.min(foreground.x.length - 1, ch + 2)] - foreground.x[Math.max(0, ch - 1)];
    }

    var frac_y = Math.abs(self.scroll_total_y) / MAX_SCROLL_TOTAL;
    var new_range = initial_range_x + frac_y * (terminal_range_x - initial_range_x);  

    // Keep the mouse over the same energy as when the wheeling started
    var vis_mouse_frac = m[0] / self.size.width;
    var new_x_min = self.scroll_start_x - (vis_mouse_frac * new_range);
    var new_x_max = new_x_min + new_range;

    // Pan by the x-wheel: MAX_SCROLL_TOTAL units is one initial width.
    //  TODO: on trackpads, maybe require the x-wheel to exceed the y-wheel before panning (which
    //        would mean using per-event deltas, rather than the cumulative totals).
    var mouse_dx_wheel = Math.min(initial_range_x,new_range) * (self.scroll_total_x / MAX_SCROLL_TOTAL);
    new_x_min += mouse_dx_wheel;
    new_x_max += mouse_dx_wheel;

    [new_x_min, new_x_max] = self._clampXRangeToData( new_x_min, new_x_max, mindatax, maxdatax );

    self.wheeltimer = window.setTimeout( wheelcleanup, 250 );

    if( Math.abs(new_x_min - currentdomain[0]) < 0.000001 
        && Math.abs(new_x_max - currentdomain[1]) < 0.000001 )
    {
      return; // fully zoomed out and e.deltaX==0
    }

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
    if( !self.canceledTouches || ((now - self.canceledTouches.time) > 500) )
      self.canceledTouches = { touches: [] };

    for( let i = 0; i < touches.length; ++i )
      self.canceledTouches.touches.push( touches[i] );

    self.canceledTouches.time = now;

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
    // Prevent default actions (e.g. zooming the page when trying to zoom the chart)
    SpectrumChartD3.stopEvent();

    // touchHoldTimeInterval (ms): a stationary single-finger press becomes a `rightclicked`
    var t = d3.touches(self.vis[0][0]),
        touchHoldTimeInterval = 700,
        evTouches = d3.event.changedTouches;
    
    self.touchStart = t;
    self.touchPageStart = evTouches.length === 1 ? [evTouches[0].pageX, evTouches[0].pageY] : null;

    if (t.length === 2) {
      self.zooming_plot = true;
      self.countGammasStartTouches = self.createPeaksStartTouches = self.touchStart;
      self.touchZoomStartEnergies = [self.xScale.invert(t[0][0]),self.xScale.invert(t[1][0])];
    }
    
    // When the number of touches changes, record the x domain: a peak fit restores to it, and a pan
    //  may be turning into a zoom.  (Set back to null in handleVisTouchEnd once no touches are left.)
    if( (!self.touchesOnChart || (Object.keys(self.touchesOnChart).length !== t.length))
        && ((t.length == 1) || (t.length === 2)) ){
      self.origdomain = self.xScale.domain();
    }
    
    self.updateTouchesOnChart(d3.event);

    if (t.length === 1) {
      self.touchHoldEmitted = false;
      var origTouch = ((d3.event.touches && (d3.event.touches.length === 1)) ? d3.event.touches[0] : null);
      var energy = self.xScale.invert(t[0][0]),
          count = self.yScale.invert(t[0][1]);

      self.setMouseDownRoi( t[0] );
      
      if( self.roiDragBoxes && self.roiBeingDragged )
        self.handleCancelRoiDrag();

      self.touchHold = window.setTimeout( function() {
        self.touchHold = null;

        if( !origTouch || self.touchHoldEmitted || !self.touchesOnChart )
          return;

        const keys = Object.keys(self.touchesOnChart);
        if( keys.length !== 1 )
          return;

        self._clearPendingClickEmit();

        var touch = self.touchesOnChart[keys[0]];
        var dx = Math.abs(origTouch.pageX - touch.pageX);
        var dy = Math.abs(origTouch.pageY - touch.pageY);

        // 10px ~ Material/iOS touch-slop; matches the touchmove cancel threshold
        if ( (dx*dx + dy*dy) <= (10*10) ) {
          self.WtEmit(self.chart.id, {name: 'rightclicked'}, energy, count, origTouch.pageX, origTouch.pageY, self.currentRefLineInfoStr() );
          self.handleCancelAllMouseEvents()();
          self.unhighlightPeak(null);
          self.touchHoldEmitted = true;
        }
      }, touchHoldTimeInterval );
    }//if (t.length === 1)
    
    if( t.length !== 1 ){
      if( self.roiIsBeingDragged || self.roiDragBoxes )
        self.handleCancelRoiDrag();
      self._clearTouchHold();
    }
  }//return function(){...}
}//SpectrumChartD3.prototype.handleVisTouchStart

SpectrumChartD3.prototype.handleVisTouchMove = function() {
  var self = this;

  return function() {
    SpectrumChartD3.stopEvent();

    self.updateTouchesOnChart(d3.event);

    const t = d3.touches(self.vis[0][0]);
    const touchPan = ((t.length === 1) && !self.roiIsBeingDragged); // one-finger drag pans

    /* First recognizer in _touchModeOrder to match wins.  Two-finger gestures are disabled while the
       y-scaler slider is being adjusted. */
    let activeMode = null;
    if( !self.currentlyAdjustingSpectrumScale ){
      for( let i = 0; !activeMode && (i < self._touchModeOrder.length); ++i ){
        const name = self._touchModeOrder[i];
        if( self._gestureModes[name].recognize() )
          activeMode = name;
      }
    }

    if( activeMode ){
      self._cancelOtherTouchModes( activeMode );
      self._gestureModes[activeMode].touchMove( t );
    }else if( self.currentlyAdjustingSpectrumScale ){
      self.handleMouseMoveScaleFactorSlider()();
    }else if( self.roiIsBeingDragged ){
      self.handleRoiDrag( t[0] );
    }else{
      self._cancelOtherTouchModes( null );
    }

    // No long-press on multi-touch, missing touchPageStart, or movement > 10 px (Material/iOS slop)
    let changedTouches = d3.event.changedTouches;
    if( self.touchHold && (t.length > 1 || !self.touchPageStart || (changedTouches.length !== 1)
        || self.dist([changedTouches[0].pageX, changedTouches[0].pageY], self.touchPageStart) > 10) )
      self._clearTouchHold();

    if( touchPan ) {
      if( !self.rightClickDown )
        self._startPanState();

      if( self.roiDragBoxes )
        self.handleCancelRoiDrag();
      
      self.mousemove()();
      self.handlePanChart();

      self.updateMouseCoordText();
      self.updateFeatureMarkers(-1);
    }//if( touchPan )

    self.hidePeakInfo();
    self.deleteTouchLine();
  };//function() that gets returned from handleVisTouchMove
};//SpectrumChartD3.prototype.handleVisTouchMove

SpectrumChartD3.prototype.handleVisTouchEnd = function() {
  var self = this;

  // Shows grey lines where a single finger was lifted (removed for multi-touch, or left of the y-axis)
  function updateTouchLine(touches) {
    if( !touches || (touches.length !== 1) || (touches[0][0] <= 0) ){
      self.deleteTouchLine();
      return;
    }

    const t = touches[0];
    if (!self.touchLineX) {
      self.touchLineX = self.vis.append("line")
        .attr("class", "touchLine")
        .attr("y1", 0)
        .attr("y2", self.size.height);
    }
     
    self.touchLineX
      .attr("x1", t[0])
      .attr("x2", t[0]);

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
    SpectrumChartD3.stopEvent();

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
      
      // Reset the pan start; else pan, add a finger to zoom, lift the first, and the chart jumps
      if( visTouches.length === 1 )
        self.rightClickDown = null;
    }

    // Don't clear self.mousewait here: Android WebView fires touchend twice for many taps, and the
    //  second would cancel the timer the first just scheduled (it is cleared on double-tap, or when it fires).
    self._clearTouchHold();

    const wasRoiBeingDragged = self.roiIsBeingDragged;
    if( self.roiIsBeingDragged ){
      if( t.length === 1 )
        self.handleMouseUpDraggingRoi( touchesT[0] );
      else
        self.handleCancelRoiDrag();
    }
    
    // Taps and double-taps
    if (t.length === 1 && touchesT.length === 1 && self.touchStart && !self.touchHoldEmitted ) {
      const x = touchesT[0][0],
            y = touchesT[0][1],
            pageX = t[0].pageX,
            pageY = t[0].pageY,
            currentTapEvent = d3.event,
            energy = self.xScale.invert(x),
            count = self.yScale.invert(y);

      const tapRadius = 35;  // max px between the taps of a double-tap

      if( wasRoiBeingDragged ){
        self.deleteTouchLine();
      } else {
        updateTouchLine(touchesT);
      }

      if (self.touchPageStart && self.dist(self.touchPageStart, [pageX, pageY]) < tapRadius ) {
        if( self.lastTapEvent
            && ((currentTapEvent.timeStamp - self.lastTapEvent.timeStamp) < self.options.doubleClickDelay)
            && self.dist([self.lastTapEvent.changedTouches[0].pageX, self.lastTapEvent.changedTouches[0].pageY], [pageX, pageY]) < tapRadius) {

          // Double-tap: cancel the pending single-tap emit
          self._clearPendingClickEmit();

          self.WtEmit(self.chart.id, {name: 'doubleclicked'}, energy, count, self.currentRefLineInfoStr());
          self.deleteTouchLine();
          self.unhighlightPeak(null);
        } else {
          // Kinetic reference lines follow the cursor on desktop; touch has no hover, so refresh them
          // here, before 'leftclicked'/'doubleclicked' read currentRefLineInfoStr().  A repeat tap at
          // the same spot (and x-domain) cycles through the candidates.
          if( self.kineticRefLines ){
            const tapPos = [x,y], domain = self.xScale.domain();
            const sameDomain = (self.lastKineticTapDomain
                                && (self.lastKineticTapDomain[0] === domain[0])
                                && (self.lastKineticTapDomain[1] === domain[1]));
            if( sameDomain && self.lastKineticTapPos
                && (self.dist(tapPos,self.lastKineticTapPos) < tapRadius)
                && self.candidateKineticRefLines && (self.candidateKineticRefLines.length > 1) ){
              self._toggleKineticCycle();
            }else{
              self.handleUpdateKineticRefLineUpdate( tapPos );
            }
            self.lastKineticTapPos = tapPos;
            self.lastKineticTapDomain = domain;
          }//if( self.kineticRefLines )

          self._scheduleSingleClickEmit( [x,y,pageX,pageY,energy,count], false, true, false );
        }//if( we have last tap event ) / else

        if( t && (t.length === 1) && currentTapEvent.timeStamp ){
          self.updateFeatureMarkers( self.lastTapEvent ? self.lastTapEvent.energy : energy );

          self.lastTapEvent = currentTapEvent;
          self.lastTapEvent.energy = energy;
          self.lastTapEvent.count = count;
          self.lastTapEvent.visCoordinates = [x, y, x + self.padding.leftComputed, y + self.padding.topComputed];
        }else{
          self.lastTapEvent = null;
        }
      }else{
        self.lastTapEvent = null;
      }
    }else{
      self.lastTapEvent = null;
      self.updateFeatureMarkers(-1);
    }

    self.updateTouchesOnChart(d3.event);
    self.updateMouseCoordText();
    self.updatePeakInfo();

    // Complete/tear down whichever gesture was in flight (emitting its range signal, where applicable)
    self._touchEndOrder.forEach( function(name){
      const end = self._gestureModes[name].touchEnd;
      if( end )
        end();
    } );

    self.touchStart = null;
    self.touchHoldEmitted = false;

    self.countGammasStartTouches = null;

    self._resetSliderDragFlags();

    if (visTouches.length === 0) {
      self.handleCancelAllMouseEvents()();
      self.origdomain = null;
    }//if (visTouches.length === 0)
  };//return function()
}//SpectrumChartD3.prototype.handleVisTouchEnd = function()...

/** -------------- General Key/Mouse Event Handlers -------------- */
SpectrumChartD3.prototype.mousemove = function () {
  var self = this;

  return function() {
    var p = d3.mouse(self.vis[0][0]);

    self.updateFeatureMarkers(-1);
    self.updateMouseCoordText();
    self.updatePeakInfo();
    self._moveLegendDrag();

    // x-axis drag (not past 0 keV)
    const newenergy = self.xScale.invert(p[0]);
    if( self.xaxisdown && (newenergy > 0) ) {
      d3.select('body').style("cursor", "ew-resize");

      if( self.hasSpectrumData() ){
        const e_width = self.xaxisdown[2] - self.xaxisdown[1];
        const newEnergyFrac = (newenergy - self.xScale.domain()[0]) / e_width;
        const x = self.rawData.spectra[0].x;
        const clamped = self._clampXRangeToData( self.xaxisdown[0] - newEnergyFrac*e_width,
                                                 self.xaxisdown[0] + (1-newEnergyFrac)*e_width, x[0], x[x.length-1] );

        // TODO: periodically emit 'xrangechanged' while dragging
        self.setXAxisRange(clamped[0], clamped[1], false, true);
        self.redraw()();
      }

      SpectrumChartD3.stopEvent();
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
              self.options.logYFracTop = 0;  // keep the whole chart visible
            } else {
              let newfrac = (newLogUpperY - logY1) / (logY1 - logY0);
              if( !isNaN(newfrac) && isFinite(newfrac) && newfrac>=0 && newfrac<50 ){
                self.options.logYFracTop = (newLogUpperY - logY1) / (logY1 - logY0);
              }
            }
            // Dragging the y-axis only adjusts the top fraction, not the bottom
          }//if( new limits are reasonable )
        } else if( self.options.yscale == "lin" ) {
          let newfrac = (newYmax / y1) - 1.0;
          
          if( !isNaN(newfrac) && isFinite(newfrac) && newfrac>=0 && newfrac<50 ){
            self.options.linYFracTop = newfrac;
          }
        } else if( self.options.yscale == "sqrt" ) {
          self.options.sqrtYFracTop = -1 + (newYmax / y1);
        }

        self.yScale.domain(new_domain);
        self.redraw()();
      }

      SpectrumChartD3.stopEvent();
    }
  }
}

SpectrumChartD3.prototype.mouseup = function () {
  var self = this;
  return function() {
    d3.select('body').style("cursor", "auto");
    if( self.xaxisdown ) {
      self.redraw()();
      self.xaxisdown = null;
    };
    if (!isNaN(self.yaxisdown)) {
      self.redraw()();
      self.yaxisdown = NaN;
    }

    self._resetSliderDragFlags();
    self.endYAxisScalingAction()();
  }
}

SpectrumChartD3.prototype.keydown = function () {
  var self = this;
  return function() {

    if( self.roiDragBoxes && (d3.event.ctrlKey || d3.event.altKey || d3.event.metaKey || d3.event.shiftKey) ){
      let needredraw = self.roiBeingDragged;
      self.handleCancelRoiDrag();
      if( needredraw )
        self.redraw()();
    }
    
    switch (d3.event.keyCode) {
      case 27: { // escape
        self.escapeKeyPressed = true;
        self.cancelYAxisScalingAction();
        self.handleCancelAllMouseEvents()();
        self.handleCancelAnimationZoom();
        self.handleCancelRoiDrag();
        self.redraw()();
        break;
      }

      case 38: // up arrow
      case 40: // down arrow
        if( self.candidateKineticRefLines && self.candidateKineticRefLines.length > 1 ) {
          self.cycleKineticRefLine( (d3.event.keyCode === 38) ? -1 : 1 );
          d3.event.preventDefault();
        }
        break;
    }
  }
}

SpectrumChartD3.prototype.updateTouchesOnChart = function (touchEvent) {
  const self = this;

  if (!touchEvent || !touchEvent.type.startsWith("touch"))
    return false;

  // Map from touch identifier to Touch, each tagged with its (page) start position
  if (!self.touchesOnChart)
    self.touchesOnChart = {};

  for (let i = 0; i < touchEvent.touches.length; i++) {
    const touch = touchEvent.touches[i], prev = self.touchesOnChart[touch.identifier];
    touch.startX = prev ? prev.startX : touch.pageX;
    touch.startY = prev ? prev.startY : touch.pageY;
    self.touchesOnChart[touch.identifier] = touch;
  }

  // Remove lifted touches
  if (touchEvent.type === "touchend") {
    for( let i = 0; i < touchEvent.changedTouches.length; ++i )
      delete self.touchesOnChart[touchEvent.changedTouches[i].identifier];
  }

  // Out of sync with touchEvent.touches happens when touches were cancelled; if that was less than a
  //  second ago, try to pick up where we left off.
  const keys = Object.keys(self.touchesOnChart);
  if( keys.length && (keys.length !== touchEvent.touches.length) ){
    const now = new Date();
    if( self.canceledTouches && ((now - self.canceledTouches.time) < 1000) ){
      keys.forEach( function(key, index){
        const oldTouch = self.touchesOnChart[key];
        if( oldTouch.wasCancelled ){
          // Match to the closest not-yet-matched touch (TODO: better matching, and only consider new touches)
          let minDist = 99999, nearestTouch = null;
          for (let i = 0; i < touchEvent.touches.length; i++) {
            const touch = touchEvent.touches[i];
            const dist = self.dist( [touch.pageX, touch.pageY], [oldTouch.pageX, oldTouch.pageY] );
            if( (dist < minDist) && !touch.alreadyMatchedToCancelled ){
              minDist = dist;
              nearestTouch = touch;
            }
          }
          if( minDist < 200 ){ // 200px is arbitrary
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
    
    // Keep just the active touches
    const trimmed = {};
    for (let i = 0; i < touchEvent.touches.length; ++i ) {
      const ident = touchEvent.touches[i].identifier;
      trimmed[ident] = self.touchesOnChart[ident];
    }
    self.touchesOnChart = trimmed;
  }//if( we have become out of sync on how many touches are on chart )
}//SpectrumChartD3.prototype.updateTouchesOnChart

/* Resets the transient pointer state (drag start, leftDragMode, slider/right-button/legend drags) shared by
   mouseup, cancel-all, and mouseleave.  Does not touch the DOM (the handleCancelMouse* helpers do), nor
   axis-drag / dragging_plot / recalibrationStartEnergy (callers clear those as needed). */
SpectrumChartD3.prototype._resetTransientPointerState = function() {
  this.leftMouseDown = null;
  this.leftDragMode = 'none';
  this.escapeKeyPressed = false;
  this.origdomain = null;
  this.rightClickDown = null;
  this.is_panning = false;
  this.zooming_plot = false;
  this._resetSliderDragFlags();
  this.legdown = null;
};

/* Ends an x-axis label drag released outside the plot area, emitting the new range (handleVisMouseUp
   does this for releases inside it). */
SpectrumChartD3.prototype._endXAxisDrag = function(){
  if( !this.xaxisdown )
    return;
  this.xaxisdown = null;
  const d = this.xScale.domain();
  this.WtEmit( this.chart.id, {name: 'xrangechanged'}, d[0], d[1], this.size.width, this.size.height, true );
};

SpectrumChartD3.prototype.handleCancelAllMouseEvents = function() {
  var self = this;

  return function () {

    d3.select(document.body).style("cursor", "default");
    self._endXAxisDrag();
    self.yaxisdown = NaN;

    self._cancelOtherMouseModes( null );   // null -> every mode
    self.handleCancelRoiDrag();

    // These live longer than a pointer gesture, so aren't in _resetTransientPointerState
    self.dragging_plot = false;
    self.recalibrationStartEnergy = null;

    // Android sometimes drops touchend on SVG paths; the document-body mouseup is our backstop
    self._clearTouchHold();
    self.touchHoldEmitted = false;

    self._resetTransientPointerState();
    self.endYAxisScalingAction()();
  }
}

SpectrumChartD3.prototype.drawSearchRanges = function() {
  var self = this;

  if( !self.searchEnergyWindows || !self.searchEnergyWindows.length ){
    self.vis.selectAll("g.searchRange").remove();
    return;
  }

  const [lx, ux] = self.xScale.domain();
  const inrange = self.searchEnergyWindows.filter( function(w){
    return ((w.energy + w.window) > lx) && ((w.energy - w.window) < ux);
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
    .attr('y', '0' )
    .attr("x", "0" )
    .style("fill", 'rgba(255, 204, 204, 0.4)')
     ;
     
  var stroke = function(d) { return d.energy>=lx && d.energy<=ux ? '#4C4C4C' : 'rgba(0,0,0,0)'; };

  gye.append("line")
    .attr("y1", h )
    .style("stroke-dasharray","4,8")
     ;

  gy.exit().remove();

  gy.select("rect")
    .attr('height', h )
    .attr('width', function(d){ 
      let le = Math.max( lx, d.energy - d.window );
      let ue = Math.min( ux, d.energy + d.window );
      return self.xScale(ue) - self.xScale(le); 
    } );
 
  let linepos = function(d){ 
    let le = Math.max( lx, d.energy - d.window );
    return self.xScale(d.energy) - self.xScale(le);
  };

  gy.select("line")
    .attr("stroke", stroke )
    .attr("x1", linepos )
    .attr("x2", linepos )
    .attr("y1", h )  //needed for initial load sometimes
    .attr("y2", '0' );
}//drawSearchRanges(...)

SpectrumChartD3.prototype.drawHighlightRegions = function(){
  const self = this;
  
  if( !Array.isArray(self.highlightRegions) || self.highlightRegions.length===0 ){
    // self.setHighlightRegions(...) already removed the highlight regions
    return;
  }
  
  const [lx, ux] = self.xScale.domain(), h = self.size.height;

  // highlightRegions: [{lowerEnergy,upperEnergy,fill,hash,drawRegion,text}]; drawRegion (optional) is 'All' (default) or 'BelowData'
  const inrange = self.highlightRegions.filter( function(w){
    console.assert( !w.drawRegion || (w.drawRegion === "All") || (w.drawRegion === "BelowData"), "Invalid HighlightRegion.drawRegion" );
    return (w.upperEnergy > lx) && (w.lowerEnergy < ux);
  } );

  // Spectrum for 'BelowData' regions: the first foreground (TODO: let the region specify it)
  const spectra = (self.rawData && self.rawData.spectra) ? self.rawData.spectra : [];
  const spectrum = spectra.find( function(s){ return s.type === "FOREGROUND"; } ) || spectra[0] || null;

  const belowDataPointsGenerator = function(region){
    if( !spectrum )
      return [];
      
    const points = self.options.backgroundSubtract && ('bgsubtractpoints' in spectrum) ? spectrum.bgsubtractpoints : spectrum.points;
    if( !points || !points.length )
      return [];

    const le = Math.max( lx, region.lowerEnergy );
    const ue = Math.min( ux, region.upperEnergy );
    const bi = self._bisecX;

    const lowerIndex = bi.left(points,le,1) - 1;
    const upperIndex = bi.right(points,ue,1);
    
    const answer = points.slice( lowerIndex, upperIndex + 1 );
    if( !answer.length )
      return [];
      
    // Close the path, limited to the region's range
    answer[0] = {x: le, y: answer[0].y};
    answer[answer.length-1] = {x: ue, y: answer[answer.length-1].y};
    answer.push( {x: ue, y: 0} );
    answer.push( {x: le, y: 0} );
    answer.push( {x: le, y: answer[0].y} );
   
    return answer;
  };//belowDataPointsGenerator function
 
  const gy = self.vis.selectAll("g.highlight")
    .data( inrange, function(d){return d.hash;} );
 
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
 
  gy.exit().remove();

  const rect_line = d3.svg.line()
    .x( function(d){ return self.xScale(d.x); })
    .y( function(d){ return d.y===0 ? 0 : h; } );
  
  const below_data_line = d3.svg.line()
    .interpolate("step-after")
    .x( function(d) { return self.xScale(d.x); })
    .y( function(d) {
      const y_px = self.yScale(d.y);
      return isNaN(y_px) ? 0 : Math.min(y_px,h);
    } );
    
  gy.select("path")
    .attr("fill", function(d){return d.fill;} )
    .attr("d", function(d){ return d.isRect ? rect_line(d.points) : below_data_line(d.points); } );
      
  gy.select("text")
    .attr("y", h-10 )
    .attr("x", function(d){ return self.xScale(0.5*(d.lowerEnergy + d.upperEnergy)); } )
    .text( function(d){return d.text ? d.text : ""} );
}//drawHighlightRegions(...)

/** -------------- Reference Gamma Lines -------------- */
SpectrumChartD3.prototype.drawRefGammaLines = function() {
  const self = this;

  if( (!self.refLines || !self.refLines.some( function(r){ return r.lines && r.lines.length; } ))
      && (!self.currentKineticRefLine || !self.currentKineticRefLine.lines || !self.currentKineticRefLine.lines.length ) ) {
    self.vis.selectAll("g.ref").remove();
    return;
  }

  const is_zoomming = (self.dragging_plot || self.zooming_plot || self.leftMouseDown || self.rightClickDown || self.zoomAnimationID);

  // Skip tiny lines to limit DOM size for heavy nuclides - more of them while zooming, for smoothness
  const disp_thresh = is_zoomming ? 0.025 : 0.0001;

  // The lines of `group` in view, and large enough to show; also sets group.maxVisibleAmp
  const domain = self.xScale.domain();
  const visibleLines = function( group ){
    const lines = group.lines.slice( self._bisecE.left( group.lines, domain[0] ), self._bisecE.right( group.lines, domain[1] ) );
    // Include the no-summing height d.hu, so the shaded summing-out segment fits on the chart
    group.maxVisibleAmp = d3.max(lines, function(d){return Math.max(d.h, d.hu||0);});  // same object as lines[i].parent
    const threshold = disp_thresh * group.maxVisibleAmp;
    return lines.filter(function(d) { return (Math.max(d.h, d.hu||0) >= threshold) || d.major; });
  };

  let reflines = [];
  if( self.refLines )
    self.refLines.forEach( function(input){ reflines = reflines.concat( visibleLines(input) ); } );
  if( !is_zoomming && self.currentKineticRefLine )
    reflines = reflines.concat( visibleLines(self.currentKineticRefLine) );

  reflines.sort( function(l,r){ return ((l.e < r.e) ? -1 : (l.e===r.e ? 0 : 1)); } );

  var tx = function(d) { return "translate(" + self.xScale(d.e) + ",0)"; };
  var gy = self.vis.selectAll("g.ref")
            .data( reflines, function(d){return d.id;} )
            .attr("transform", tx)
            .attr("stroke-width", self.options.refLineWidth );

  function stroke(d){ return d.color ? d.color : d.parent.color; };

  // Dash pattern per particle type; gammas, and combined lines (e.g. "gamma, xray"), are solid
  const dashes = { xray: "3,3", beta: "1,1", alpha: "3,2,1", positron: "3,1", electronCapture: "6,6",
                   "cascade-sum": "6,6", "S.E.": "4,1", "D.E.": "4,1" };
  const dashfunc = function(d){ return dashes[d.particle] || null; };

  const h = self.size.height;
  const m = Math.min(h,self.options.refLineTopPad); // margin at the top of the chart

  gy.enter()
    .insert("g", "a")
    .attr("class", "ref")
    .attr("transform", tx)
    .append("line")
    .style("stroke-dasharray", dashfunc )
    .attr("stroke", stroke );

  gy.exit().remove();

  // Line heights scale linearly with branching ratio (a log mapping was tried, but looked poor)
  const y2Lin = function(d){ return Math.min(h - (h-m)*d.h/d.parent.maxVisibleAmp,h-2); };
  // The no-summing (uncorrected) top, when cascade summing changed the height.
  const y2Hu = function(d){ return Math.min(h - (h-m)*d.hu/d.parent.maxVisibleAmp,h-2); };
  // Cascade summing (d.hu = height WITHOUT summing).  Summing-out (d.hu > d.h): solid line to d.h, plus
  //  a translucent segment for the amplitude lost, up to d.hu.  Summing-in (d.h > d.hu): solid line to
  //  d.hu, plus a dashed segment for the amplitude gained, up to d.h.
  const isGain = function(d){ return (typeof d.hu === "number") && (d.h > d.hu*1.0001); };
  const isLoss = function(d){ return (typeof d.hu === "number") && (d.hu > d.h*1.0001); };
  const y2Solid = function(d){ return isGain(d) ? y2Hu(d) : y2Lin(d); };

  gy.select("line")
    .attr("stroke-width", self.options.refLineWidth )
    .attr("y2", y2Solid )
    .attr("y1", h );  //needed for initial load sometimes

  // The summing segment, between the heights with and without summing
  const appendSumSegment = function( cls, filterFcn, styleName, styleValue ){
    gy.select("line." + cls).remove();
    gy.filter( filterFcn )
      .append("line")
      .attr("class", cls)
      .style(styleName, styleValue)
      .attr("stroke", stroke )
      .attr("stroke-width", self.options.refLineWidth )
      .attr("y1", y2Hu )
      .attr("y2", y2Lin )
      .attr("x1", 0)
      .attr("x2", 0);
  };
  appendSumSegment( "sumout-lost", isLoss, "opacity", 0.35 );
  appendSumSegment( "sumin-gain", isGain, "stroke-dasharray", "3,2" );

  // Dotted extensions above major lines (refLineVerbosity >= 2)
  gy.select("line.major-extension").remove();
  if( self.options.refLineVerbosity >= 2 ) {
    const majorLines = gy.filter(function(d) { return d.major; });
    self._appendRefLineExtension( majorLines, "major-extension", stroke,
                                  self.options.refLineWidth, function(d){ return y2Lin(d); } )
        .style("opacity", 0.5);
  }
}

/* Appends the dotted vertical extension line drawn above a reference line; shared by the
   always-on major-line extensions and the hover-only temp extension. */
SpectrumChartD3.prototype._appendRefLineExtension = function( sel, cls, stroke, width, y1 ){
  const dash = "" + this.options.refLineWidthHover + "," + 2*this.options.refLineWidthHover;
  return sel.append("line")
    .attr("class", cls)
    .style("stroke-dasharray", dash)
    .attr("stroke", stroke)
    .attr("stroke-width", width)
    .attr("y1", y1)
    .attr("y2", this.options.refLineTopPad)
    .attr("x1", 0)
    .attr("x2", 0);
};

SpectrumChartD3.prototype.clearReferenceLines = function() {
  var self = this;
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

      this.refLines = data;
      let index = 0;
      this.refLines.forEach( function(a,i){ 
        if( !a.color )
          a.color = default_colors[i%default_colors.length];
        if( !a.lines || !Array.isArray(a.lines) )
          throw "Reference lines does not contain an array of lines";

        a.lines.forEach( function(d){ 
          d.parent = a; 
          d.id = ++index;  // unique D3 data key (energy may not be unique)

          // e.g. {e:30.27,h:6.22e-05,particle:'xray',decay:'xray',el:'barium'}
          if( (typeof d.e !== "number") || (typeof d.h !== "number") || (typeof d.particle !== "string") )
            throw "Reference line is invalid (" + JSON.stringify(d) + ")";
        } );
      } );
    }catch(e){
      this.refLines = null;
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
  // TODO: validate `data`
  if( data ){
    let index = 1000000;
    data.ref_lines.forEach( function(a){
      a.src_lines.lines.forEach( function(d){
        d.id = ++index;
        d.parent = a.src_lines;  
      } );  
    } );
  }//if( data )

  this.kineticRefLines = data;

  this.handleUpdateKineticRefLineUpdate();
}//SpectrumChartD3.prototype.setKineticReferenceLines

SpectrumChartD3.prototype.handleUpdateKineticRefLineUpdate = function( posOverride ){
  // posOverride: position from callers with no live d3.event (e.g. the touch-tap path); in that case
  //  `mousePosIsValid` (which describes a getMousePos() result) doesn't apply.
  const m = posOverride ? posOverride : this.getMousePos();
  const badPos = (!posOverride && !this.mousePosIsValid);
  if( !this.kineticRefLines || !this.kineticRefLines.ref_lines || !this.kineticRefLines.ref_lines.length || badPos ){
    this._clearKineticRefLine();
    return;
  }//if( we dont need to draw the lines )

  const energy = this.xScale.invert(m[0]);
  const peak_sigma = (this.kineticRefLines.fwhm_fcn ? this.kineticRefLines.fwhm_fcn(energy) : 2.35482) / 2.35482;
  
  // Candidate ref-line groups near the cursor, each with its lowest line weight
  const refLineMinWeights = [];
  const domain = this.xScale.domain();
  const energyBisector = this._bisecE.left;  // lines are sorted by energy

  for( const refLineGroup of this.kineticRefLines.ref_lines ) {
    if( !refLineGroup.src_lines || !refLineGroup.src_lines.lines || !refLineGroup.src_lines.lines.length ) continue;
    
    const src_weight = refLineGroup.weight || 1.0;
    let minGroupWeight = Number.MAX_VALUE;
    let bestLineInGroup = null;
    
    const lines = refLineGroup.src_lines.lines;
    
    const startIndex = energyBisector(lines, domain[0]);
    const endIndex = energyBisector(lines, domain[1]);
    
    let maxVisibleH = 0.0;
    for( let i = startIndex; i <= endIndex && i < lines.length; i++ )
      maxVisibleH = Math.max(maxVisibleH, lines[i].h);
    if( maxVisibleH < 1E-32 )
      continue;

    for( let i = startIndex; i <= endIndex && i < lines.length; i++ ) {
      const line = lines[i];
      const delta_energy = Math.abs(energy - line.e);
      const weight = ((0.25 * peak_sigma + delta_energy) / (Math.max(line.h,1E-32) / maxVisibleH)) / src_weight;
      
      if( weight < minGroupWeight && delta_energy < 5*peak_sigma ) {
        minGroupWeight = weight;
        bestLineInGroup = line;
      }
    }
    
    if( minGroupWeight < Number.MAX_VALUE )
      refLineMinWeights.push({ 
        minWeight: minGroupWeight, 
        lines: refLineGroup, 
        bestLine: bestLineInGroup 
      });
  }
  
  // Sort by ascending weight; keep at most 5, within 8x the minimum weight
  refLineMinWeights.sort( (a, b) => a.minWeight - b.minWeight );
  const cutoff = (refLineMinWeights[0]?.minWeight || 1) * 8.0;

  // Candidates to cycle through (excluding sources already shown as reference lines)
  const newCandidates = refLineMinWeights
    .filter( (item, i) => (i < 5) && (item.minWeight <= cutoff) )
    .filter(item => !this.refLines || !this.refLines.some( input => item.lines.src_lines.parent == input.parent ))
    .map(item => ({ 
      lines: item.lines.src_lines, 
      weight: item.minWeight,
      bestLine: item.bestLine
    }));
  
  const candidateParentsChanged = !this.candidateKineticRefLines 
    || this.candidateKineticRefLines.length !== newCandidates.length 
    || !this.candidateKineticRefLines.every((candidate, i) => candidate.lines.parent === newCandidates[i].lines.parent);
    
  if( candidateParentsChanged ) {
    // Candidates changed: reset the selection and auto-cycling
    this.candidateKineticRefLines = newCandidates;
    this.lastKineticTapPos = null;   // pixel-based, so stale once the candidates change
    this.currentKineticRefLineIndex = 0;
    this.currentKineticRefLine = newCandidates.length > 0 ? newCandidates[0].lines : null;
    this.drawRefGammaLines();
    this.updateMouseCoordText( posOverride );

    if( this.candidateKineticRefLines.length > 1 ) {
      this.startKineticRefLineCycling();
    } else {
      this.stopKineticRefLineCycling();
    }
  } else if( this.candidateKineticRefLines ) {
    // Same candidates: update weights, keeping the user's selection
    this.candidateKineticRefLines.forEach((candidate, i) => {
      if( i < newCandidates.length ) {
        candidate.weight = newCandidates[i].weight;
        candidate.bestLine = newCandidates[i].bestLine;
      }
    });
    
    if( this.currentKineticRefLineIndex >= this.candidateKineticRefLines.length ) {
      this.currentKineticRefLineIndex = 0;
    }

    this.currentKineticRefLine = this.candidateKineticRefLines.length > 0 ? this.candidateKineticRefLines[this.currentKineticRefLineIndex].lines : null;
    this.drawRefGammaLines();
    this.updateMouseCoordText( posOverride );
  }
}//SpectrumChartD3.prototype.handleUpdateKineticRefLineUpdate

/* Clears the kinetic reference line (and its candidates), if one is shown. */
SpectrumChartD3.prototype._clearKineticRefLine = function(){
  if( !this.currentKineticRefLine )
    return;
  this.currentKineticRefLine = null;
  this.candidateKineticRefLines = [];
  this.currentKineticRefLineIndex = 0;
  this.lastKineticTapPos = null;
  this.stopKineticRefLineCycling();
  this.drawRefGammaLines();
  this.updateKineticRefLineCandidateDisplay();
};

SpectrumChartD3.prototype.startKineticRefLineCycling = function(){
  const self = this;
  
  this.stopKineticRefLineCycling();
  if( this.candidateKineticRefLines.length <= 1 )
    return;
  
  // The arrow keys may not get through while an element has focus
  if (document.activeElement instanceof HTMLElement)
    document.activeElement.blur();
  
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

/* Stops kinetic ref-line auto-cycling if it's running, else steps to the next candidate. */
SpectrumChartD3.prototype._toggleKineticCycle = function(){
  if( this.kineticRefLineCycleTimer )
    this.stopKineticRefLineCycling();
  else
    this.cycleKineticRefLine(1);
};

SpectrumChartD3.prototype.updateCurrentKineticDisplay = function(){
  const currentCandidate = this.candidateKineticRefLines[this.currentKineticRefLineIndex];
  if( currentCandidate && currentCandidate.bestLine ) {
    this.updateRefLineDisplay(currentCandidate.bestLine, currentCandidate.lines);
    
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
  
  // Manual navigation stops auto-cycling
  if( !isAutomatic )
    this.stopKineticRefLineCycling();

  const n = this.candidateKineticRefLines.length;
  this.currentKineticRefLineIndex = (this.currentKineticRefLineIndex + ((direction > 0) ? 1 : (n - 1))) % n;

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
  
  if( !self.refLineCandidates ) {
    self.refLineCandidates = self.refLineInfo.append("g").attr("class", "refLineCandidates");
    self.refLineCandidates.append("rect").attr("class", "refLineCandidatesBackground");
    self.refLineCandidates.append("text").attr("class", "refLineCandidatesText").attr("x", 0).attr("dy", "1em");
  }
  
  const candidatesText = self.refLineCandidates.select("text");
  const candidatesRect = self.refLineCandidates.select("rect");
  
  candidatesText.selectAll("tspan").remove();

  // Header, one line per candidate (current in bold), then the arrow-keys hint
  candidatesText.append('svg:tspan')
    .attr('x', -3)
    .attr('dy', "1.4em")
    .style('font-size', '0.8em')
    .text( self.options.txt.candidates );
  
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
  
  const bbox = candidatesText.node().getBBox();
  const extraPadding = 4;
  candidatesRect
    .attr("x", bbox.x - 3 - extraPadding)
    .attr("y", bbox.y - 2)
    .attr("width", bbox.width + 6 + 2*extraPadding)
    .attr("height", bbox.height + 4);
  
  // Place the candidates left of the ref-line info if there is space, otherwise right
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

  // Coincidence (cascade) summing: label sum peaks, else show the fractional amplitude gained or lost
  //  relative to the no-summing height d.hu.
  let sumTxt = "";
  if( linedata.particle === 'cascade-sum' ) {
    sumTxt = "Coincidence sum peak";
  } else if( (typeof linedata.hu === "number") && (linedata.hu > 0) ) {
    const pct = Math.round( 100.0 * ((sf / linedata.hu) - 1.0) );
    if( pct > 0 )
      sumTxt = "Coincidence summing: +" + pct + "% of amplitude";
    else if( pct < 0 )
      sumTxt = "Coincidence summing: −" + (-pct) + "% of amplitude";
  }

  const svgtxt = self.refLineInfoTxt.select("text")
                   .attr("dy", "1em")
                   .attr("fill", refLineSource.color || "#000");
  
  svgtxt.selectAll("tspan").remove();

  if( self.options.showRefLineInfoForMouseOver ){
    [textdescrip, txt, sumTxt, attTxt].forEach( function(line){
      if( line )
        svgtxt.append('svg:tspan').attr('x', 0).attr('dy', "1em").text( line );
    } );
  }
}//SpectrumChartD3.prototype.updateRefLineDisplay

SpectrumChartD3.prototype.applyLineStyling = function( lineElement, linedata, isHovered ) {
  const self = this;
  const lineSelection = d3.select(lineElement);
  const strokeWidth = isHovered ? self.options.refLineWidthHover : self.options.refLineWidth;
  
  lineSelection.select("line")
    .attr("stroke-width", strokeWidth);
  
  // Extension lines above the reference line
  if( self.options.refLineVerbosity >= 1 ) {
    const hasMajorExtension = linedata.major && self.options.refLineVerbosity >= 2;
    
    if( hasMajorExtension ) {
      lineSelection.select("line.major-extension")
        .attr("stroke-width", strokeWidth)
        .style("opacity", isHovered ? 1.0 : 0.5);
    }
    
    if( isHovered && !hasMajorExtension ) {
      // Temporary hover extension, positioned like the major-extension
      const h = self.size.height;
      const m = self.options.refLineTopPad;
      const y2Lin = function(d){ return Math.min(h - (h-m)*d.h/d.parent.maxVisibleAmp, h-2); };

      lineSelection.selectAll("line.temp-extension").remove();
      self._appendRefLineExtension( lineSelection, "temp-extension",
                                    (linedata.color ? linedata.color : linedata.parent.color),
                                    self.options.refLineWidthHover, y2Lin(linedata) );
    } else if( !isHovered ) {
      lineSelection.selectAll("line.temp-extension").remove();
    }
  }
}//SpectrumChartD3.prototype.applyLineStyling

SpectrumChartD3.prototype.applyRefLineHoverStyling = function( nearestline, skipPreviousReset ){
  const self = this;
  
  // Un-hover the other kinetic candidates' lines
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

/** -------------- Grid Lines -------------- */
SpectrumChartD3.prototype.setGridX = function( onstate, dontRedraw ) { this._setGrid( true, onstate, dontRedraw ); };
SpectrumChartD3.prototype.setGridY = function( onstate, dontRedraw ) { this._setGrid( false, onstate, dontRedraw ); };

/* Shows or hides the x (isX) or y grid lines. */
SpectrumChartD3.prototype._setGrid = function( isX, onstate, dontRedraw ) {
  const gridName = isX ? 'xGrid' : 'yGrid', bodyName = gridName + 'Body';
  this.options[isX ? 'gridx' : 'gridy'] = onstate;

  if( this[bodyName] )
    this[bodyName].remove();
  this[gridName] = null;
  this[bodyName] = null;

  if( onstate ) {
    const grid = d3.svg.axis().scale( isX ? this.xScale : this.yScale )
                   .orient( isX ? "bottom" : "left" )
                   .innerTickSize( isX ? -this.size.height : -this.size.width )
                   .outerTickSize(0)
                   .tickFormat( "" )
                   .tickPadding(10);
    if( isX )
      grid.ticks( 20, "" );

    const body = this.vis.insert("g", ".refLineInfo").attr("class", isX ? "xgrid" : "ygrid" );
    if( isX )
      body.attr("transform", "translate(0," + this.size.height + ")");
    body.call( grid );

    this[gridName] = grid;
    this[bodyName] = body;
  }

  if( !dontRedraw )
    this.redraw()();
};

/* Updates a grid in place to the axis' ticks ([{value,major}]).  The minor/major class is re-stamped BY
   VALUE, as the axis reuses tick nodes keyed by value; minors keep the "tick" class, as d3's axis join
   selects .tick (else ticks would duplicate). */
SpectrumChartD3.prototype._updateGridTicks = function( body, grid, ticks, tickValues ){
  const major = {};
  ticks.forEach( function(t){ if( t.major ) major[t.value] = true; } );
  body.call( grid.tickValues( tickValues ) );
  body.selectAll('g.tick').attr("class", function(d){ return major[d] ? "tick" : "tick minorgrid"; });
};

/** -------------- Mouse Coordinate Info -------------- */
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

SpectrumChartD3.prototype.updateMouseCoordText = function( posOverride ) {
  var self = this;

  if ( (!d3.event && !posOverride) || !self.hasSpectrumData() )
    return;

  const p = posOverride ? posOverride : self.getMousePos();

  // TODO: also give background/secondary counts, and a channel range when rebinFactor != 1
  if( self.options.showMouseStats ){
    // Channel and counts at this energy
    const energy = self.xScale.invert(p[0]);
    const foreground = self.rawData.spectra[0];
    const background = self.getSpectrumByID(foreground.backgroundID);
    const channel = (foreground.x && foreground.x.length) ? self._bisecRaw.right(foreground.x, energy) : -1;
    let counts = null, backgroundSubtractCounts = null;
    if( foreground.points && foreground.points.length )
      counts = foreground.points[self._bisecX.left(foreground.points,energy,1) - 1].y;
    if( self.options.backgroundSubtract && (counts !== null) && background ){
      const bpoint = background.points[self._bisecX.left(background.points,energy,1) - 1];
      backgroundSubtractCounts = Math.max(0, counts - (bpoint ? bpoint.y : 0));
    }

    const round = (v) => Math.round(10*v)/10;
    const perRebin = (foreground.rebinFactor === 1) ? "" : ("/" + foreground.rebinFactor);
    const xmmsg = round(energy) + " keV" + (channel ? (", chan: " + channel) : "");
    const ymmsg = ((counts !== null) ? ("counts: " + round(counts) + perRebin + ", ") : "") + "y: " + round(self.yScale.invert(p[1]));

    self.mouseInfo.style("display", null );
    const mousetxt = self.mouseInfo.select("text");
    mousetxt.attr('dy', "-2em");
    mousetxt.selectAll("tspan").remove();

    // Adds a line of text, returning its length
    const addLine = (txt) => mousetxt.append('svg:tspan').attr('x', "-12em").attr('dy', "-1em").text( txt ).node().getComputedTextLength();
    const bgsubmsglen = (backgroundSubtractCounts === null) ? 0
                        : addLine( self.options.txt.backSubCounts + ": " + round(backgroundSubtractCounts) + perRebin );
    const ymmsglen = addLine( ymmsg );
    const xmmsglen = addLine( xmmsg );

    self.mouseInfoBox.attr('width', Math.max(xmmsglen,ymmsglen,bgsubmsglen) + 9 )
      .attr('height', backgroundSubtractCounts !== null ? '3.1em' : '2.5em')
      .attr('y', backgroundSubtractCounts !== null ? '-3.9em' : '-3.1em');
  } else {
    self.mouseInfo.style("display", "none" );
  }

  // No ref-line info while zooming
  if( self.dragging_plot || self.zooming_plot || self.zoomAnimationID ){
    if( self.mousedOverRefLine ){
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

  // Undoes the hover styling of a reference line <g>
  function cleanupRefLineHover( node ){
    self.applyLineStyling( node, node.__data__, false );
    d3.select(node).select("circle.ref-hover-indicator").remove();
    node.__data__.mousedover = null;
  }

  reflines[0].forEach( function(d,i){
    var yh = d.childNodes[0].y1.baseVal.value - d.childNodes[0].y2.baseVal.value;
    var xpx = self.xScale(d.__data__.e);
    var dpx = Math.abs(xpx - p[0]);

    // In principle unnecessary, but without it lines sometimes stay fat
    if( d.__data__.mousedover && d !== self.mousedOverRefLine ){
      cleanupRefLineHover( d );
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
      cleanupRefLineHover( self.mousedOverRefLine );
    }
    self.mousedOverRefLine = null;
    self.refLineInfo.style("display", "none");
    return;
  }

  if( self.mousedOverRefLine===nearestline )
    return;

  if( self.mousedOverRefLine ){
    cleanupRefLineHover( self.mousedOverRefLine );
  }

  var linedata = nearestline.__data__;
  linedata.mousedover = true;
  const linepx = self.xScale(linedata.e);

  self.refLineInfo.style("display", null).attr("transform", "translate("+linepx+",0)" );

  const lineSelection = d3.select(nearestline);
  const nearLineEl = lineSelection.select("line");

  self.updateRefLineDisplay(linedata, linedata.parent);
  self.updateKineticRefLineCandidateDisplay();

  // Put the text left of the line if it would run off the right of the chart
  var tx = function(d) {
    var w = this.getBBox().width;
    return ((linepx+5+w)>self.size.width) ? ("translate("+(-5-w)+",0)") : "translate(5,0)";
  };
  self.refLineInfoTxt.attr("transform", tx );

  self.applyRefLineHoverStyling(nearestline, true);

  // Red dot on the hovered line
  lineSelection.select("circle.ref-hover-indicator").remove();
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

/** -------------- Legend -------------- */
SpectrumChartD3.prototype.updateLegend = function() {
  var self = this;
  
  if( !this.options.showLegend || !self.hasAnyData() ) {
    if( this.legend ) {
      this.legend.remove();
      this.legend = null;
      this.legendBox = null;
      this.legBody = null;
      this.legendHeaderClose = null;
      // No 'legendClosed' emit, since options.showLegend isn't changing
    }
    return;
  }
  
  if( !this.legend ) {

    const moveleg = function(){ self._moveLegendDrag(); };

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
    
    // Close button
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

    this.legend.on("mouseover", function(d){if( !self.dragging_plot && !self.zooming_plot ) self.legendHeader.style("display", null);} )
      .on("mouseout", function(d){self.legendHeader.style("display", "none");} )
      .on("mousemove", moveleg)
      .on("touchmove", moveleg)
      .on("wheel", SpectrumChartD3.stopEvent );
    
    function mousedownleg(){
      if (d3.event.defaultPrevented) return;
      if( self.dragging_plot || self.zooming_plot ) return;
      SpectrumChartD3.stopEvent();
      const trans = d3.transform(self.legend.attr("transform")).translate;
      const [x, y] = self._eventClientXY();
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

  // Adds an entry, at ypos, with its title line; returns {entry, text, txtStart}
  const addEntry = function( title ){
    const entry = self.legBody.append("g").attr("transform","translate(0," + ypos + ")");
    const text = entry.append("text").attr("class", "legentry");
    const titlenode = text.append('svg:tspan').attr('x', "15").text( title );
    const txtStart = 0.5*titlenode.node().getBBox().height;
    titlenode.attr('y', txtStart);
    return { entry: entry, text: text, txtStart: txtStart };
  };

  // Adds a line of text below what the entry already has
  const addLine = function( e, txt ){
    return e.text.append('svg:tspan').attr('x', "20").attr('y', e.txtStart + e.entry.node().getBBox().height).text( txt );
  };

  const spectra = self.rawData ? self.rawData.spectra : [];
  spectra.forEach( function(spectrum,i){
    if( !spectrum || !spectrum.y || !spectrum.y.length )
      return;

    const sf = ((typeof spectrum.yScaleFactor === "number") ? spectrum.yScaleFactor: 1);
    const lt = spectrum.liveTime;
    const rt = spectrum.realTime;
    const neutsum = spectrum.neutrons;

    const nsum = sf*spectrum.dataSum;
    const e = addEntry( (spectrum.title ? spectrum.title : ("Spectrum " + (i+1)))
                        + " (" + nsum.toFixed(nsum > 1000 ? 0 : 1) + " counts)" );
    const thisentry = e.entry, thistxt = e.text, txtStart = e.txtStart;

    thisentry.append("path")
        .attr("class", "SpectrumLegendLine " + (spectrum.type ? " " + spectrum.type : "") )
        .attr("stroke", spectrum.lineColor ? spectrum.lineColor : null)
        .attr("stroke-width", self.options.spectrumLineWidth )
        .attr("d", "M0," + (txtStart - 1) + " L12," + (txtStart - 1) );

    let ltnode, lttxt, dttxt;
    if( typeof lt === "number" )
    {
      lttxt = self.options.txt.liveTime + ": " + (sf*lt).toPrecision(4) + " s";
      ltnode = addLine( e, lttxt );
    }

    if( typeof rt === "number" )
      addLine( e, self.options.txt.realTime + ": " + (sf*rt).toPrecision(4) + " s" );

    if( sf != 1 )
      addLine( e, self.options.txt.scaledBy + " " + sf.toPrecision(4) );

    if( (typeof lt === "number") && (typeof rt === "number") && (rt > 0) && ltnode )
    {
      dttxt = self.options.txt.deadTime + ": " + (100*(rt - lt) / rt).toPrecision(3) + "%";
      // Show dead time on hover (replaced below if there is neutron data)
      thistxt
        .on("mouseover", function(){ ltnode.text(dttxt); } )
        .on("mouseout", function(){ ltnode.text(lttxt); });
    }

    if( typeof neutsum === "number" ){
      const nrt = (typeof spectrum.neutronLiveTime === "number") ? spectrum.neutronLiveTime : (rt > 1.0E-6 ? rt : lt);
      const isCps = (typeof nrt === "number");
      const neut = isCps ? neutsum/nrt : neutsum*sf;

      // Human-friendly number, to roughly the precision we care about
      let toLegendRateStr = function( val, ndig ){
        const powTen = Math.floor(Math.log10(Math.abs(val)));

        if( Number.isInteger(val) || Number.isNaN(ndig) || Number.isNaN(powTen) )
          return '' + val;
        else if( (powTen < -4) || (powTen > ndig) )  // e.g. 6.096e-6 (ndig==3)
          return '' + val.toExponential(ndig);
        else if( powTen < 3 )                        // e.g. 0.06096 (ndig==3)
          return '' + val.toFixed(ndig-powTen);
        else                                         // >= 1000: as an integer
          return '' + val.toFixed(0);
      };//toLegendRateStr

      addLine( e, self.options.txt.Neutrons + ": " + toLegendRateStr(neut,3) + (isCps ? " " + self.options.txt.cps : "") );

      // For a foreground/secondary neutron CPS, show how many sigma it is from the background's rate
      if( isCps
          && ((spectrum.type === self.spectrumTypes.FOREGROUND)
              || (spectrum.type === self.spectrumTypes.SECONDARY)) )
      {
        // Neutron info of the first background
        let backNeut = null, backNeutLT = null;

        for( let j = 0; j < spectra.length; ++j )
        {
          const spec = spectra[j];
          if( spec && (j !== i)
              && (spec.type === self.spectrumTypes.BACKGROUND)
              && ((typeof spec.neutronLiveTime === "number") || (typeof spec.realTime === "number"))
              && (typeof spec.neutrons === "number") )
          {
            backNeut = spec.neutrons;
            backNeutLT = (typeof spec.neutronLiveTime === "number") ? spec.neutronLiveTime
                                                                    : (spec.realTime > 1.0E-6 ? spec.realTime : spec.liveTime);
            break;
          }
        }//for( loop over spectrum )

        if( (typeof backNeut === "number") && (typeof backNeutLT === "number") && (neutsum>0 || backNeut>0) )
        {
          const backRate = backNeut/backNeutLT;
          const backRateSigma = Math.sqrt(backNeut) / backNeutLT;
          const rateSigma = Math.sqrt(neutsum) / nrt;
          const sigma = Math.sqrt(rateSigma*rateSigma + backRateSigma*backRateSigma);
          const nsigma = Math.abs(neut - backRate) / sigma;
          const isneg = (neut < backRate);

          thistxt.append('svg:tspan')
            .attr('x', "40")
            .attr('y', txtStart + thisentry.node().getBBox().height - 4)
            .attr('style', 'font-size: 75%')
            .html( "(" + (isneg ? self.options.txt.foreNSigmaBelowBack : self.options.txt.foreNSigmaAboveBack).replace("{1}", toLegendRateStr(nsigma,1)) + ")" );
        }//if( we have background neutron CPS info )
      }//if( foreground/secondary, and displaying neutron CPS )

      // Total neutron counts, shown on hover
      if( isCps ){
        thisentry.neutinfo = thistxt.append('svg:tspan')
          .attr('x', "40")
          .attr('y', txtStart + thisentry.node().getBBox().height - 5)
          .attr('style', 'display: none')
          .text( toLegendRateStr(neutsum,3) + " " + self.options.txt.neutrons + (typeof nrt === "number" ? (" in " + nrt.toPrecision(4) + " s") : "") );

        thistxt  // replaces the dead-time hover handlers set above
          .on("mouseover", function(){
            thisentry.neutinfo.attr('style', 'font-size: 75%' )
            self.legendBox.attr('height', self.legBody.node().getBBox().height + 10 );
            if( ltnode && dttxt ) ltnode.text(dttxt);
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

  const templates = (self.rawData && self.rawData.templates) ? self.rawData.templates : [];
  templates.forEach( function(template,i){
    if( !template || !template.y || !template.y.length )
      return;

    const sf = ((typeof template.yScaleFactor === "number") ? template.yScaleFactor : 1);
    const nsum = sf * (template.dataSum || 0);
    const defaultTitle = (self.options.txt.templateDefaultTitle || "Template");
    const e = addEntry( (template.title ? template.title : (defaultTitle + " " + (i+1)))
                        + " (" + nsum.toFixed(nsum > 1000 ? 0 : 1) + " counts)" );

    // Filled swatch, matching the stacked-area fill
    e.entry.append("rect")
        .attr("class", "SpectrumLegendFill")
        .attr("x", "0")
        .attr("y", e.txtStart - 8)
        .attr("width", "12")
        .attr("height", "10")
        .attr("fill", template.lineColor ? template.lineColor : "#888888")
        .attr("stroke", "none");

    if( sf != 1 )
      addLine( e, self.options.txt.scaledBy + " " + sf.toPrecision(4) );

    ypos += e.entry.node().getBBox().height + 5;
  } );

  var w = this.legBody.node().getBBox().width + 15;
  this.legendBox.attr('width', w );
  this.legendBox.attr('height', this.legBody.node().getBBox().height + 10 );
  this.legendHeaderClose.attr("transform","translate(" + (w-16) + ",4)");
  
  this.legendHeader.select('rect').attr('width', w );
  this.legendHeader.select('text').attr("x", w/2);

  // Keep the space to the right of the legend the same
  this.legend.attr("transform", "translate(" + (this.cx - fromRight - w) + "," + origtrans[1] + ")" );
}

/* The client [x,y] of the current mouse or touch event. */
SpectrumChartD3.prototype._eventClientXY = function(){
  const e = d3.event;
  return [ e.x ? e.x : (e.touches ? e.touches[0].clientX : e.clientX),
           e.y ? e.y : (e.touches ? e.touches[0].clientY : e.clientY) ];
};

/* Moves the legend with an in-progress legend drag (this.legdown). */
SpectrumChartD3.prototype._moveLegendDrag = function(){
  if( !this.legdown )
    return;
  SpectrumChartD3.stopEvent();
  const [x, y] = this._eventClientXY();
  const calculated_x = d3.mouse(this.vis[0][0])[0];
  if( (calculated_x >= -this.padding.leftComputed) && (y >= 0) && (calculated_x <= this.cx) && (y <= this.cy) )
    this.legend.attr("transform", "translate(" + ((x - this.legdown.x) + this.legdown.x0) + "," + ((y - this.legdown.y) + this.legdown.y0) + ")");
};

SpectrumChartD3.prototype.setShowLegend = function( show ) {
  this.options.showLegend = Boolean(show);
  this.updateLegend();
}

/** -------------- Y-axis Drawing -------------- */
/* "Nice" linear tick generator for both axes; returns [{value, major, text}].  nlabel is the approximate
   label count, iterCap a loop safety bound, and formatFn(value)->string formats major labels. */
SpectrumChartD3.prototype._linearTicks = function( rmin, rmax, nlabel, iterCap, formatFn ){
  const ticks = [];
  const EPSILON = 1.0E-3;
  const range = rmax - rmin;
  const n = Math.pow(10, Math.floor(Math.log10(range/nlabel)));
  const msd = range / (n * nlabel);

  if( isNaN(n) || !isFinite(n) || nlabel<=0 || n<=0.0 )
    return ticks;

  let subdashes, renderInterval;
  if( msd < 1.5 ){      subdashes = 2;  renderInterval = 0.5*n; }
  else if( msd < 3.3 ){ subdashes = 5;  renderInterval = 0.5*n; }
  else if( msd < 7 ){   subdashes = 5;  renderInterval = n; }
  else {                subdashes = 10; renderInterval = n; }

  const biginterval = subdashes * renderInterval;
  let start = biginterval * Math.floor((rmin + 1.0E-15) / biginterval);
  if( start < (rmin - EPSILON*renderInterval) )
    start += biginterval;

  for( let i = -Math.floor(Math.floor(start - rmin)/renderInterval); ; ++i ){
    if( i > iterCap )  /*JIC*/
      break;
    const v = start + renderInterval * i;
    if( (v - rmax) > EPSILON * renderInterval )
      break;
    const major = ((i % subdashes) == 0);
    ticks.push( { value: v, major: major, text: ((i >= 0 && major) ? formatFn(v) : "") } );
  }

  return ticks;
};

SpectrumChartD3.prototype.yticks = function() {
  const self = this;
  var ticks = [];
  var EPSILON = 1.0E-3;

  if (!this.rawData || !this.rawData.spectra) return [];

  var formatYNumber = function(v) {
    // Roughly printf's "%.3g"
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

    if( this.options.yscale === "lin" )
    {
      ticks = self._linearTicks( renderymin, renderymax, heightpx / 50, 500, formatYNumber );
    }

    if( this.options.yscale === "log" )
    {
      const yMinPrefPow = Math.floor( Math.log10(this.options.logYAxisMin) );
      
      // Powers of 10 at or below renderymin, and at or above renderymax (0 if renderymax <= 0)
      var minpower = (renderymin > 0.0) ? Math.floor( Math.log10(renderymin) ) : yMinPrefPow;
      var maxpower = (renderymax > 0.0) ? Math.ceil( Math.log10(renderymax) ): 0;

      if( maxpower == minpower )
      {
        // renderymin==renderymax, a power of 10
        ++maxpower;
        --minpower;
      }else if( maxpower > 2 && minpower < yMinPrefPow)
      {
        // A tiny value (e.g. a fraction of a count), and a large one (>1000)
        minpower = yMinPrefPow;
      }else if( maxpower >= 0 && minpower < yMinPrefPow && (maxpower-minpower) > 6 )
      {
        // A tiny power (1.0E-5), and one between 1 and 999: only show the most significant decades
        minpower = maxpower - 5;
      }

      var numdecades = maxpower - minpower + 1;  // including the decades above and below the data
      var minpxdecade = 25;      // minimum pixels per decade
      var labeldelta = 1;        // decades between labeled (large) ticks; every decade gets a large tick
      var nticksperdecade = 10;  // small+large ticks per decade

      if( Math.floor(heightpx / minpxdecade) < numdecades )
      {
        labeldelta = numdecades / (heightpx / minpxdecade);
        nticksperdecade = 1;
      }

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
        }

        for( var i = 1; i < (nticksperdecade-1); ++i )
        {
          var y = startcounts + i*deltacounts;
          if( (y - renderymin) > -eps && (y - renderymax) < eps )
          {
            ++nticks;  
            ticks.push( {value: y, major: false, text: null } );
          }
        }
      }

      // With a decent number of labels the user can orient themselves, so drop the minor labels
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
        var y = 0.5*(renderymin+renderymax);
        var t = formatYNumber(y);
        ticks.push( {value: y, major: true, text: t } );
      }
    }

  // TODO: proper sqrt ticks (this just uses d3's)
  if( this.options.yscale === "sqrt" ){
    this.yScale.copy().ticks(10)
        .forEach(function(e){ticks.push({value:e,major:true,text:formatYNumber(e)});});
  }

  if( this.options.noYAxisNumbers )
    ticks.forEach( function(obj){obj["text"] = null; } );

  return ticks;
}

/* Mousedown on a y-axis label starts a y-axis drag. */
SpectrumChartD3.prototype.yaxisDrag = function() {
  var self = this;
  return function() {
    var p = d3.mouse(self.vis[0][0]);
    self.yaxisdown = self.yScale.invert(p[1]);
  }
}

SpectrumChartD3.prototype.drawYTicks = function() {
  this.yAxis.scale(this.yScale);
  
  // yScale would give NaN
  if( this.yScale.domain()[0] === this.yScale.domain()[1] ){
    this.yAxisBody.selectAll("g.tick").remove();
    return;
  }
    
  const ytick = this.yticks();
  const ytickvalues = ytick.map(function(d){return d.value;} );

  this.yAxis.tickValues(ytickvalues);
  this.yAxisBody.call(this.yAxis)

  // Shorter minor ticks; label text
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

    if( this.yGridBody )
      this._updateGridTicks( this.yGridBody, this.yGrid, ytick, ytickvalues );
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

/** -------------- Y-axis Scale -------------- */
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

SpectrumChartD3.prototype.setLogY = function(){ this.setYAxisType("log"); }
SpectrumChartD3.prototype.setLinearY = function(){ this.setYAxisType("lin"); }
SpectrumChartD3.prototype.setSqrtY = function(){ this.setYAxisType("sqrt"); }

/* Toggle the y-axis between log and linear (used by the axis dblclick and title click). */
SpectrumChartD3.prototype.toggleYAxisType = function(){
  const ytype = (this.options.yscale === "log") ? "lin" : "log";
  this.setYAxisType( ytype );
  this.WtEmit( this.chart.id, {name: 'yAxisTypeChanged'}, ytype );
}

SpectrumChartD3.prototype.handleYAxisWheel = function() {
  var self = this;
  
  if( !d3.event )
    return;

  const wdelta = d3.event.deltaY || 0;
  const mult = (wdelta > 0) ? 0.02 : ((wdelta < 0) ? -0.02 : 0);  // zoom out, or in

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
    self.options.linYFracTop = Math.max( Math.min( self.options.linYFracTop + mult, 0.85 ), -0.95 );
  } else if( self.options.yscale == "sqrt" ) {
    self.options.sqrtYFracTop = Math.max( Math.min( self.options.sqrtYFracTop + mult, 0.85 ), -0.95 );
  }

  self.redraw()();
  SpectrumChartD3.stopEvent();
  return false;
}

/** -------------- X-axis Drawing -------------- */
SpectrumChartD3.prototype.xticks = function() {
  const rendermin = this.xScale.domain()[0];
  const rendermax = this.xScale.domain()[1];

  // Wider per-label pixel budget for larger numbers, so labels don't overlap
  let ndigstart = rendermin > 0 ? Math.floor(Math.log10(rendermin)) : 1;
  ndigstart = Math.max(1, Math.min(ndigstart, 5));
  const nlabel = Math.floor( this.size.width / (50 + 15*(ndigstart-1)) );

  return this._linearTicks( rendermin, rendermax, nlabel, 5000, function(v){ return "" + v; } );
}

/* Mousedown on an x-axis label starts an x-axis drag. */
SpectrumChartD3.prototype.xaxisDrag = function() {
  var self = this;
  return function() {
    const energy = self.xScale.invert( d3.mouse(self.vis[0][0])[0] );
    if( energy > 0 )
      self.xaxisdown = [energy, self.xScale.domain()[0], self.xScale.domain()[1]];
  }
}

SpectrumChartD3.prototype.drawXAxisArrows = function(show_arrow) {
  var self = this;

  if (self.options.showXRangeArrows) {
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

    const atEnd = !self.hasSpectrumData() || (self.xScale.domain()[1] === self.min_max_x_values()[1]);
    self.xAxisBody.select("path").attr("marker-end", (atEnd || (show_arrow === false)) ? null : "url(#arrowhead)");

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

  var xgticks = self.xAxisBody.selectAll('g.tick');
  var majorticks = xgticks.filter(function(d,i){ return xticks[i] && xticks[i].major; } );
  var minorticks = xgticks.filter(function(d,i){ return xticks[i] && !xticks[i].major; } );

  minorticks.select('line').attr('y2', '4');
  minorticks.select('text').text("");

  // Hide major labels overlapping the compact x-axis title, or running off the chart
  const labelUpperXPx = function(tick){
    const bb = tick[0][0].getBBox();
    const trans = d3.transform(tick.attr("transform")).translate;
    return trans[0] + bb.width;
  };
  
  if( this.hasCompactXAxis() && self.xaxistitle ){
    // Check ticks for overlap with the title, from the right-most tick
    const xtitle_px = Number( self.xaxistitle.attr("x") );
    for( let i = majorticks[0].length - 1; i >= 0; i -= 1 ){
      const tick = d3.select( majorticks[0][i] );
      const upper_x_px = labelUpperXPx( tick );
      
      if( upper_x_px < xtitle_px )
        break;
      
      tick.select('text').text("");
    }
  } else {
    // Only the last tick can go off the chart
    const lastmajor = majorticks[0].length ? majorticks[0][majorticks[0].length-1] : null;
    if( lastmajor ) {
      const tick = d3.select( lastmajor );
      const upper_x_px = labelUpperXPx( tick );
      
      // The -10 is empirical (maybe from padding.right/left)
      if( (upper_x_px - 10) > this.size.width ){
        tick.select('text').text("");
      }
    }
  }//if( this.hasCompactXAxis() and have x-axis title )

  if( self.xGridBody ) {
    self.xGridBody.attr("transform", "translate(0," + self.size.height + ")");
    self._updateGridTicks( self.xGridBody, self.xGrid, xticks, xtickvalues );
  }
}//SpectrumChartD3.prototype.drawXTicks

SpectrumChartD3.prototype.setXAxisRange = function( minimum, maximum, doEmit, userAction ) {
  var self = this;
  self.xScale.domain([minimum, maximum]);

  if( doEmit )
    self.WtEmit(self.chart.id, {name: 'xrangechanged'}, minimum, maximum, self.size.width, self.size.height, userAction);
}

SpectrumChartD3.prototype.setXAxisMinimum = function( minimum ) {
  this.setXAxisRange( minimum, this.xScale.domain()[1] );
  this.redraw()();
}

SpectrumChartD3.prototype.setXAxisMaximum = function( maximum ) {
  this.setXAxisRange( this.xScale.domain()[0], maximum );
  this.redraw()();
}

SpectrumChartD3.prototype.setXRangeArrows = function(d) {
  this.options.showXRangeArrows = d;
  this.drawXAxisArrows(d);
}

SpectrumChartD3.prototype.setCompactXAxis = function( compact ) {
  this.options.compactXAxis = Boolean(compact);
  
  if( this.xaxistitle )
    this.xaxistitle.style("text-anchor", this.hasCompactXAxis() ? "start" : "middle");
    
  this.handleResize( false );
}

/** -------------- X-axis Pan Slider Chart --------------
 *  Mini chart below the x-axis whose box pans/zooms the main x-axis range. */
/* Everything the slider-chart lines depend on; when unchanged, drawXAxisSliderChart skips their
   expensive rebuild (two full-spectrum rebins).  The zoom domain is deliberately not an input, as
   the lines always show the full data range. */
SpectrumChartD3.prototype._sliderChartSignature = function(){
  const b = this.min_max_x_values();
  let sig = b[0] + "," + b[1]
            + "," + this.size.sliderChartWidth + "," + this.size.sliderChartHeight
            + "," + this.size.width + "," + this.size.height   /* feed do_rebin + the line y-transform */
            + "," + this.options.yscale + "," + (!!this.options.backgroundSubtract)
            + "," + this.options.spectrumLineWidth
            + "," + this.options.logYFracTop + "," + this.options.logYAxisMin
            + "," + (this._dataGeneration|0);
  const spec = (this.rawData && this.rawData.spectra) ? this.rawData.spectra : [];
  for( let i = 0; i < spec.length; ++i )
    sig += ";" + spec[i].yScaleFactor + "," + (spec[i].y ? spec[i].y.length : 0) + "," + spec[i].lineColor;
  const tmpl = (this.rawData && this.rawData.templates) ? this.rawData.templates : [];
  for( let i = 0; i < tmpl.length; ++i )
    sig += ";t" + ((tmpl[i] && tmpl[i].y) ? tmpl[i].y.length : 0);
  return sig;
};

SpectrumChartD3.prototype.drawXAxisSliderChart = function() {
  var self = this;

  if (!self.chart || d3.select(self.chart).empty() || !self.hasSpectrumData() || self.size.height<=0 ) {
    self.cancelXAxisSliderChart();
    return;
  }

  if( !self.options.showXAxisSliderChart ) {
    self.cancelXAxisSliderChart();
    return;
  }

  function drawDragRegionLines() {
    self.sliderChart.selectAll(".sliderDragRegionLine").remove();
    
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

  var origdomain = self.xScale.domain();
  var bounds = self.min_max_x_values();
  var maxX = bounds[1];
  var minX = bounds[0];

  // Positions the slider box/handles: the full data range across the slider width
  const sliderX = self.xScale.copy().domain([minX, maxX]).range([0, self.size.sliderChartWidth]);

  if( !self.sliderChart ) {
    d3.select(self.chart)

    self.sliderChart = d3.select(self.chart).select("svg").append("g")
      .on("touchstart", self.handleTouchStartSliderChart())
      .on("touchmove", self.handleTouchMoveSliderChart());

    self.sliderChartPlot = self.sliderChart.append("rect")
      .attr("id", "sliderchartarea"+self.chart.id )
      .attr("class", "slider-plot");

    // Holds the data lines
    self.sliderChartBody = self.sliderChart.append("g")
      .attr("clip-path", "url(#sliderclip" + this.chart.id + ")");

    self.sliderChartClipPath = self.sliderChart.append('svg:clipPath')
        .attr("id", "sliderclip" + self.chart.id )
        .append("svg:rect")
        .attr("x", 0)
        .attr("y", 0);

    if( self.options.showSliderCloseBtn ){
      // Close icon at the upper right (the legend's close button uses a path instead; could unify)
      self.sliderClose = self.sliderChart.append("g")
        .attr("class", "slider-close")
        .on("click", function(event){
          self.setShowXAxisSliderChart(false);
          self.WtEmit(self.chart.id, {name: 'sliderChartDisplayed'}, false );
        })
        .on("touchstart.drag", function(){ // the touch doesn't become a "click" (cancelled somewhere?)
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

  // The draggable box, and its edge handles
  if (!self.sliderBox) {
    self.sliderBox = self.sliderChart.append("rect")
      .attr("class", "sliderBox")
      .on("mousedown", self.handleMouseDownSliderBox())
      .on("touchstart", self.handleTouchStartSliderBox())
      .on("touchmove", self.handleTouchMoveSliderChart());

    self.sliderDragLeft = self.sliderChart.append("rect")
      .attr("class", "sliderDragRegion")
      .attr("rx", 2)
      .attr("ry", 2)
      .on("mousedown", self.handleMouseDownLeftSliderDrag())
      .on("mousemove", self.handleMouseMoveLeftSliderDrag(false))
      .on("mouseout", self.handleMouseOutLeftSliderDrag())
      .on("touchstart", self.handleTouchStartLeftSliderDrag())
      .on("touchmove", self.handleTouchMoveLeftSliderDrag(false));

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

  var sliderBoxX = sliderX(origdomain[0]);
  var sliderBoxWidth = sliderX(origdomain[1]) - sliderBoxX;

  self.sliderBox.attr("x", sliderBoxX)
    .attr("width", sliderBoxWidth)
    .attr("height", self.size.sliderChartHeight);

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

  /* Rebuild the slider lines only when an input changed (see _sliderChartSignature): the live scales
     are swapped to the full data range, every spectrum rebinned and its path rebuilt, then everything
     is swapped back and rebinned again. */
  const sig = self._sliderChartSignature();
  if( sig !== self._sliderPathSig ){
    self._sliderPathSig = sig;

    const origdomainrange = self.xScale.range();
    const origrange = self.yScale.domain();

    self.xScale.domain([minX, maxX]);
    self.xScale.range([0, self.size.sliderChartWidth]);
    self.do_rebin();
    self.rebinForBackgroundSubtract();
    self.yScale.domain(self.getYAxisDomain());

    self.drawSliderChartLines();

    self.xScale.domain(origdomain);
    self.xScale.range(origdomainrange);
    self.do_rebin();
    self.rebinForBackgroundSubtract();
    self.yScale.domain(origrange);
  }

  drawDragRegionLines();
}

SpectrumChartD3.prototype.drawSliderChartLines = function()  {
  var self = this;

  if( !self.options.showXAxisSliderChart || !self.sliderChartBody || !self.rawData || !self.rawData.spectra )
    return;

  for (let i = 0; i < self.rawData.spectra.length; ++i) {
    if (self['sliderLine' + i])
      self['sliderLine' + i].remove();
  }

  for (let i = 0; i < self.rawData.spectra.length; ++i) {
    let spectrum = self.rawData.spectra[i];
    if (self.options.backgroundSubtract && spectrum.type == self.spectrumTypes.BACKGROUND) continue;

    if (self['line'+i] && self.size.height>0 && self.size.sliderChartHeight>0 ){
      const key = self.options.backgroundSubtract && ('bgsubtractpoints' in spectrum) ? 'bgsubtractpoints' : 'points';

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

  self._resetSliderDragFlags();
  self._sliderPathSig = null;   // so re-showing the slider rebuilds its lines

  this.handleResize( false );
}

SpectrumChartD3.prototype.setShowXAxisSliderChart = function(d) {
  this.options.showXAxisSliderChart = d;

  if( this.xaxistitle )
    this.xaxistitle.style("text-anchor", this.hasCompactXAxis() ? "start" : "middle");

  this.drawXAxisSliderChart();
  this.handleResize( false );
}

/* Installs document-level mouseup/mousemove listeners (namespaced per chart) so a slider drag keeps
   tracking, and ends cleanly, even if the cursor leaves the slider. */
SpectrumChartD3.prototype.installSliderDragListeners = function() {
  var self = this;
  const upName = "mouseup.sliderdrag" + this.chart.id, moveName = "mousemove.sliderdrag" + this.chart.id;
  d3.select(document).on(upName, function() {
    self._resetSliderDragFlags();
    d3.select(document.body).style("cursor", "default");
    d3.select(document).on(upName, null);
    d3.select(document).on(moveName, null);
  });
  d3.select(document).on(moveName, self.handleMouseMoveSliderChart());
};

/** -------------- Slider Chart Pointer Helpers --------------
 *  Mouse and touch share the sliderChartPointer / savedSliderPointer state (a drag uses one or the
 *  other); the *Move handlers read the pointer and dispatch to the shared bodies below. */

/* Moves the slider box to track the pointer, updating the x-range. */
SpectrumChartD3.prototype._sliderBoxMove = function(p) {
  var self = this;
  d3.select(document.body).style("cursor", "move");
  var origdomain = self.xScale.domain();
  var origdomainrange = self.xScale.range();
  var bounds = self.min_max_x_values();
  var maxX = bounds[1];
  var minX = bounds[0];

  if (!self.sliderChartPointer)
    self.sliderChartPointer = p;

  var sliderBoxX = Number(self.sliderBox.attr("x"));
  var sliderBoxWidth = Number(self.sliderBox.attr("width"));
  var sliderDragRegionWidth = 3;
  var x = Math.min( self.size.sliderChartWidth - sliderBoxWidth, Math.max(0, sliderBoxX + (p[0] - self.sliderChartPointer[0])) );

  if ((sliderBoxX == 0 || sliderBoxX + sliderBoxWidth == self.size.sliderChartWidth)) {
    if (!self.savedSliderPointer)
      self.savedSliderPointer = p;
  }

  if (self.savedSliderPointer && p[0] != self.savedSliderPointer[0]) {
    if (sliderBoxX == 0 && p[0] < self.savedSliderPointer[0]) return;
    else if (sliderBoxX + sliderBoxWidth == self.size.sliderChartWidth && p[0] > self.savedSliderPointer[0]) return;
    else self.savedSliderPointer = null;
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

  self.sliderChartPointer = p;
};

/* Drags the 'left' or 'right' slider handle to the pointer, without crossing the other handle. */
SpectrumChartD3.prototype._sliderHandleMove = function(side, p) {
  var self = this;
  var origdomain = self.xScale.domain();
  var origdomainrange = self.xScale.range();
  var bounds = self.min_max_x_values();
  var isLeft = (side === 'left');
  var sliderDragRegionWidth = 3;
  var x = isLeft ? Math.max(p[0], 0) : Math.min(p[0], self.size.sliderChartWidth);

  self.xScale.domain([bounds[0], bounds[1]]);
  self.xScale.range([0, self.size.sliderChartWidth]);

  var crossed = isLeft
    ? (p[0] > Number(self.sliderDragRight.attr("x") - 1))
    : (p[0] - sliderDragRegionWidth < Number(self.sliderDragLeft.attr("x")) + Number(self.sliderDragLeft.attr("width")));
  if (crossed) {
    self.xScale.domain(origdomain);
    self.xScale.range(origdomainrange);
    return;
  }

  if (isLeft) {
    self.sliderBox.attr("x", x);
    self.sliderDragLeft.attr("x", x);
    origdomain[0] = self.xScale.invert(x);
  } else {
    self.sliderBox.attr("width", Math.abs(x - Number(self.sliderDragRight.attr("x"))));
    self.sliderDragRight.attr("x", x - sliderDragRegionWidth);
    origdomain[1] = self.xScale.invert(x);
  }

  self.setXAxisRange(origdomain[0], origdomain[1], true, true);
  self.xScale.range(origdomainrange);
  self.redraw()();
};

SpectrumChartD3.prototype.handleMouseDownSliderBox = function() {
  var self = this;

  return function() {
    // Prevent text selection around the chart
    SpectrumChartD3.stopEvent();

    self.sliderBoxDown = true;
    self.leftDragRegionDown = false;
    self.rightDragRegionDown = false;

    // TODO: record the initial range, to restore if escape is pressed
    self.escapeKeyPressed = false;

    d3.select(document.body).style("cursor", "move");

    self.installSliderDragListeners();
  }
}

SpectrumChartD3.prototype.handleMouseMoveSliderChart = function() {
  var self = this;
  return function() {
    if (self.leftDragRegionDown || self.rightDragRegionDown || self.sliderBoxDown) {
      SpectrumChartD3.stopEvent();
    }
    if (self.leftDragRegionDown)  return self.handleMouseMoveLeftSliderDrag(true)();
    if (self.rightDragRegionDown) return self.handleMouseMoveRightSliderDrag(true)();
    if (self.sliderBoxDown) self._sliderBoxMove( d3.mouse(self.sliderChart[0][0]) );
  };
}

/* Mousedown handler for a slider handle; flagName is the side's flag to set. */
SpectrumChartD3.prototype._makeSliderDragMouseDownHandler = function(flagName) {
  var self = this;
  return function() {
    SpectrumChartD3.stopEvent();
    self[flagName] = true;
    self.escapeKeyPressed = false;
    self.installSliderDragListeners();
  };
}

SpectrumChartD3.prototype.handleMouseDownLeftSliderDrag = function() {
  return this._makeSliderDragMouseDownHandler('leftDragRegionDown');
}

SpectrumChartD3.prototype.handleMouseDownRightSliderDrag = function() {
  return this._makeSliderDragMouseDownHandler('rightDragRegionDown');
}

/* Mousemove handler for a slider handle: redraw=true (from the slider-chart dispatcher) drives the
   drag, redraw=false (the handle's own listener) only sets the hover cursor.  While the box itself is
   dragged, the event propagates so the chart-level handler keeps tracking. */
SpectrumChartD3.prototype._makeSliderDragMouseMoveHandler = function(flagName, side, redraw) {
  var self = this;
  return function() {
    if (self.sliderBoxDown) return;
    SpectrumChartD3.stopEvent();
    d3.select(document.body).style("cursor", "ew-resize");
    if (!self[flagName] || !redraw) return;
    self._sliderHandleMove(side, d3.mouse(self.sliderChart[0][0]));
  };
}

SpectrumChartD3.prototype.handleMouseMoveLeftSliderDrag = function(redraw) {
  return this._makeSliderDragMouseMoveHandler('leftDragRegionDown', 'left', redraw);
}

SpectrumChartD3.prototype.handleMouseMoveRightSliderDrag = function(redraw) {
  return this._makeSliderDragMouseMoveHandler('rightDragRegionDown', 'right', redraw);
}

SpectrumChartD3.prototype._makeSliderDragMouseOutHandler = function(flagName) {
  var self = this;
  return function() {
    if (!self[flagName])
      d3.select(document.body).style("cursor", "default");
  };
}

SpectrumChartD3.prototype.handleMouseOutLeftSliderDrag = function() {
  return this._makeSliderDragMouseOutHandler('leftDragRegionDown');
}

SpectrumChartD3.prototype.handleMouseOutRightSliderDrag = function() {
  return this._makeSliderDragMouseOutHandler('rightDragRegionDown');
}

SpectrumChartD3.prototype.handleTouchStartSliderBox = function() {
  var self = this;

  return function() {
    SpectrumChartD3.stopEvent();

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

/* The single touch's [x,y] on the slider chart, or null on multi-touch. */
SpectrumChartD3.prototype._singleTouchOnSliderChart = function() {
  var t = d3.touches(this.sliderChart[0][0]);
  if (t.length !== 1) return null;
  return t[0];
};

SpectrumChartD3.prototype.handleTouchMoveSliderChart = function() {
  var self = this;
  return function() {
    SpectrumChartD3.stopEvent();
    if (self.leftDragRegionDown)  return self.handleTouchMoveLeftSliderDrag(true)();
    if (self.rightDragRegionDown) return self.handleTouchMoveRightSliderDrag(true)();
    if (self.sliderBoxDown) {
      var p = self._singleTouchOnSliderChart();
      if (p) self._sliderBoxMove(p);
    }
  };
}

SpectrumChartD3.prototype._makeSliderDragTouchStartHandler = function(flagName) {
  var self = this;
  return function() { self[flagName] = true; };
}

SpectrumChartD3.prototype.handleTouchStartLeftSliderDrag = function() {
  return this._makeSliderDragTouchStartHandler('leftDragRegionDown');
}

SpectrumChartD3.prototype.handleTouchStartRightSliderDrag = function() {
  return this._makeSliderDragTouchStartHandler('rightDragRegionDown');
}

/* Touchmove handler for a slider handle (the touch handlers' `redraw` argument is unused). */
SpectrumChartD3.prototype._makeSliderDragTouchMoveHandler = function(flagName, side) {
  var self = this;
  return function() {
    if (self.sliderBoxDown) return;
    SpectrumChartD3.stopEvent();
    if (!self[flagName]) return;
    var p = self._singleTouchOnSliderChart();
    if (p) self._sliderHandleMove(side, p);
  };
}

SpectrumChartD3.prototype.handleTouchMoveLeftSliderDrag = function(redraw) {
  return this._makeSliderDragTouchMoveHandler('leftDragRegionDown', 'left');
}

SpectrumChartD3.prototype.handleTouchMoveRightSliderDrag = function(redraw) {
  return this._makeSliderDragTouchMoveHandler('rightDragRegionDown', 'right');
}

/** -------------- Scale Factor (per-spectrum y-scale sliders) -------------- */
SpectrumChartD3.prototype.numYScalers = function() {
  if( !this.options.scaleBackgroundSecondary || !this.rawData || !this.rawData.spectra )
    return 0;

  return this.rawData.spectra.filter( (spectrum) => spectrum && spectrum.type && (spectrum.type !== this.spectrumTypes.FOREGROUND)
                                                    && (spectrum.yScaleFactor != null) && (spectrum.yScaleFactor >= 0.0) ).length;
}

/* Returns the spectrum currently being y-scaled by the scaler slider, or null. */
SpectrumChartD3.prototype._adjustingSpectrum = function() {
  if( !this.rawData || !this.rawData.spectra || this.currentlyAdjustingSpectrumScale === null )
    return null;
  return this.rawData.spectra.find( (s) => s.type == this.currentlyAdjustingSpectrumScale ) || null;
}

SpectrumChartD3.prototype.cancelYAxisScalingAction = function() {
  var self = this;

  const spectrum = self._adjustingSpectrum();
  if( !spectrum )
    return;

  spectrum.yScaleFactor = spectrum.startingYScaleFactor;
  spectrum.startingYScaleFactor = null;
  self.endYAxisScalingAction()();
  self.redraw()();
}

SpectrumChartD3.prototype.endYAxisScalingAction = function() {
  var self = this;
  
  return function(){
    let scale = null;
    const spectrum = self._adjustingSpectrum();
    if( spectrum ){
      spectrum.sliderText.style( "display", "none" );
      spectrum.sliderToggle.attr("cy", Number(spectrum.sliderRect.attr("y"))
                                        + Number(spectrum.sliderRect.attr("height"))/2);
      spectrum.sliderRect.attr("stroke-opacity", 0.8).attr("fill-opacity", 0.3);
      spectrum.sliderToggle.attr("stroke-opacity", 0.8).attr("fill-opacity", 0.7);
      spectrum.startingYScaleFactor = null;
      scale = spectrum.yScaleFactor;
    }

    if( scale !== null )
      self.WtEmit(self.chart.id, {name: 'yscaled'}, scale, self.currentlyAdjustingSpectrumScale );

    self.currentlyAdjustingSpectrumScale = null;
  }
}

SpectrumChartD3.prototype.drawScalerBackgroundSecondary = function() {
  var self = this;

  // TODO: identify scaled spectra by spectrum.id (ensuring id uniqueness) instead of spectrum.type.
  const nScalers = self.numYScalers();
  if( nScalers === 0 || self.size.height < 35 ){
    if( self.scalerWidget )
      self.removeSpectrumScaleFactorWidget();
    return;
  }

  if( !self.scalerWidget ){
    self.scalerWidget = d3.select(this.chart).select("svg").append("g")
      .attr("class", "scalerwidget");

    self.scalerWidgetBody = self.scalerWidget.append("g");
  }

  // The number of scalers may have changed, so update the position
  self.scalerWidget
      .attr("transform","translate(" + (this.cx - 20*nScalers) + "," + this.padding.topComputed + ")");

  self.scalerWidgetBody.selectAll("g").remove();

  var scalerHeight = self.size.height - 30;
  // 7 px on all devices: a larger touch radius hung off-screen and wasn't centered on the slider
  var toggleRadius = 7;
  var ypos = 15;
  
  var scalenum = 0;
  self.rawData.spectra.forEach(function(spectrum,i) {
    var spectrumScaleFactor = spectrum.yScaleFactor;
    var spectrumSelector = 'Spectrum-' + spectrum.id;

    if (i == 0 || spectrum.type === self.spectrumTypes.FOREGROUND)   // the foreground isn't scalable
    return;
      
    if (spectrumScaleFactor != null && spectrumScaleFactor >= 0) {
      scalenum += 1;
      
      const speccolor = spectrum.lineColor; // if null, CSS variables are used
      
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
        .attr("y", 0 )
        .attr("x", 8)
        .attr("rx", 5)
        .attr("ry", 5)
        .attr("fill", speccolor )
        .attr("width", "4px")
        .attr("height", scalerHeight );

      // Starts adjusting this spectrum's scale
      const beginScaling = function(){
        spectrum.sliderRect.attr("stroke-opacity", 1.0).attr("fill-opacity", 1.0);
        spectrum.sliderToggle.attr("stroke-opacity", 1.0).attr("fill-opacity", 1.0);
        spectrum.startingYScaleFactor = spectrum.yScaleFactor;
        self.currentlyAdjustingSpectrumScale = spectrum.type;
        spectrum.sliderText.style( "display", null );
      };

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
          beginScaling();
          SpectrumChartD3.stopEvent();
        })
        .on("mousemove", self.handleMouseMoveScaleFactorSlider())
        .on("mouseup", self.endYAxisScalingAction() )
        .on("touchstart", beginScaling)
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

  // A lighter redraw for scale-factor changes
  function scaleFactorChangeRedraw() {
    // The legend rebuild costs ~15 getBBox reflows; coalesce to once per animation frame
    if( !self._legendUpdateRaf ){
      self._legendUpdateRaf = requestAnimationFrame( function(){
        self._legendUpdateRaf = null;
        self.updateLegend();
      } );
    }
    self.do_rebin();  // re-scales the adjusted spectrum's points (do_rebin's scaledBy guard)
    self.rebinForBackgroundSubtract();
    self.setYAxisDomain();
    self.drawYTicks();
    self.update();
    self.drawPeaks();
  }

  return function() {
    if (!self.hasSpectrumData() )
      return;
    if (!self.currentlyAdjustingSpectrumScale)
      return;

    const spectrum = self._adjustingSpectrum();

    SpectrumChartD3.stopEvent();

    if( !spectrum )
      return;

    const linei = self.rawData.spectra.indexOf( spectrum );

    d3.select(document.body).style("cursor", "pointer");

    const m = d3.mouse(spectrum.sliderRect[0][0]);  // (d3.mouse handles touches too)
    const scalerHeight = Number(spectrum.sliderRect.attr("height"));
    const rad = Number(spectrum.sliderToggle.attr("r"));
    const newTogglePos = Math.min(Math.max(0,m[1]+rad/2),scalerHeight);
    const fracOnScale = 1.0 - newTogglePos/scalerHeight;

    // log y: from 0.001 to 1000x.  TODO: for lin and sqrt, 0.1, 1 and 10x at fracOnScale of 0, 0.5 and 1
    const sf = (self.options.yscale === "log")
               ? Math.exp( Math.log(0.001) + fracOnScale*(Math.log(1000) - Math.log(0.001)) )
               : (1.38734467428311845572*(Math.exp(fracOnScale) - 1) + 0.1);
    const spectrumScaleFactor = sf*spectrum.startingYScaleFactor;

    spectrum.sliderToggle.attr("cy", newTogglePos );
    spectrum.sliderText.text( (spectrumScaleFactor % 1 != 0) ? spectrumScaleFactor.toFixed(3) : spectrumScaleFactor.toFixed() );
    spectrum.yScaleFactor = spectrumScaleFactor;
    
    // Background subtraction needs the full redraw
    if (self.options.backgroundSubtract) self.redraw()();
    else scaleFactorChangeRedraw();

    // Update the slider chart lines (with the scales swapped to the full data range)
    if (self["sliderLine"+linei]) {
      var origdomain = self.xScale.domain();
      var origdomainrange = self.xScale.range();
      var origrange = self.yScale.domain();
      var bounds = self.min_max_x_values();
      var maxX = bounds[1];
      var minX = bounds[0];

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

      self.xScale.domain(origdomain);
      self.xScale.range(origdomainrange);
      self.do_rebin();
      self.rebinForBackgroundSubtract();
      self.yScale.domain(origrange);
    }
  }
}

/** -------------- Peak and Continuum Shapes --------------
 *  Peaks and continua are drawn from cumulative functions, F(x) = counts below energy x (up to a constant),
 *  so the counts in any [x0,x1] are F(x1) - F(x0), exactly, for any channel width or rebinning. */

/** erf(x), from Abramowitz & Stegun 7.1.26; absolute error < 1.5E-7. */
SpectrumChartD3.erf = function( x ){
  const sign = (x >= 0) ? 1 : -1;
  x = Math.abs(x);
  const t = 1.0/(1.0 + 0.3275911*x);
  const y = 1.0 - (((((1.061405429 * t + -1.453152027) * t) + 1.421413741) * t + -0.284496736) * t + 0.254829592) * t * Math.exp(-x * x);
  return sign * y;
};

/** erfc(z), adapted from the boost implementation (Boost Software License - Version 1.0), as used in
 InterSpec's C++; unlike 1 - erf(z), it stays accurate far into the tails (needed by the Exp*Gauss shapes).
 */
SpectrumChartD3.erfc = function( z ){
  if( z < 0 )
    return (z < -0.5) ? (2 - SpectrumChartD3.erfc( -z )) : (1 + SpectrumChartD3.erf( -z ));

  if( z < 0.5 )
    return 1 - SpectrumChartD3.erf( z );

  if( z >= 28 )
    return 0;

  if( z < 1.5 )
  {
    const P = [ -0.098090592216281240205, 0.178114665841120341155, 0.191003695796775433986, 0.0888900368967884466578, 0.0195049001251218801359, 0.00180424538297014223957];
    const Q = [ 1.0, 1.84759070983002217845, 1.42628004845511324508, 0.578052804889902404909, 0.12385097467900864233, 0.0113385233577001411017, 0.337511472483094676155e-5
    ];

    const zarg = z - 0.5;
    const P_eval = ((((zarg*P[5] + P[4])*zarg + P[3])*zarg + P[2])*zarg + P[1])*zarg + P[0];
    const Q_eval = (((((Q[6]*zarg + Q[5])*zarg + Q[4])*zarg + Q[3])*zarg + Q[2])*zarg + Q[1])*zarg + Q[0];
    return (0.405935764312744140625 + P_eval / Q_eval) * (Math.exp(-z * z) / z);
  }

  if( z < 2.5 )
  {
    const P = [ -0.0243500476207698441272, 0.0386540375035707201728, 0.04394818964209516296, 0.0175679436311802092299, 0.00323962406290842133584, 0.000235839115596880717416 ];
    const Q = [ 1.0, 1.53991494948552447182, 0.982403709157920235114, 0.325732924782444448493, 0.0563921837420478160373, 0.00410369723978904575884 ];

    const zarg = z - 1.5;
    const P_eval = ((((zarg*P[5] + P[4])*zarg + P[3])*zarg + P[2])*zarg + P[1])*zarg + P[0];
    const Q_eval = ((((zarg*Q[5] + Q[4])*zarg + Q[3])*zarg + Q[2])*zarg + Q[1])*zarg + Q[0];
    return (0.50672817230224609375 + P_eval / Q_eval) * (Math.exp(-z * z) / z);
  }

  if( z < 4.5 )
  {
    const P = [ 0.00295276716530971662634, 0.0137384425896355332126, 0.00840807615555585383007, 0.00212825620914618649141, 0.000250269961544794627958, 0.113212406648847561139e-4 ];
    const Q = [ 1.0, 1.04217814166938418171, 0.442597659481563127003, 0.0958492726301061423444, 0.0105982906484876531489, 0.000479411269521714493907 ];

    const zarg = z - 3.5;
    const P_eval = ((((zarg*P[5] + P[4])*zarg + P[3])*zarg + P[2])*zarg + P[1])*zarg + P[0];
    const Q_eval = ((((zarg*Q[5] + Q[4])*zarg + Q[3])*zarg + Q[2])*zarg + Q[1])*zarg + Q[0];
    return (0.5405750274658203125 + P_eval / Q_eval) * (Math.exp(-z * z) / z);
  }

  const P = [ 0.00628057170626964891937, 0.0175389834052493308818, -0.212652252872804219852, -0.687717681153649930619, -2.5518551727311523996, -3.22729451764143718517, -2.8175401114513378771 ];
  const Q = [ 1.0, 2.79257750980575282228, 11.0567237927800161565, 15.930646027911794143, 22.9367376522880577224, 13.5064170191802889145, 5.48409182238641741584 ];

  const zarg = 1.0 / z;
  const P_eval = (((((P[6]*zarg + P[5])*zarg + P[4])*zarg + P[3])*zarg + P[2])*zarg + P[1])*zarg + P[0];
  const Q_eval = (((((Q[6]*zarg + Q[5])*zarg + Q[4])*zarg + Q[3])*zarg + Q[2])*zarg + Q[1])*zarg + Q[0];
  return (0.5579090118408203125 + P_eval / Q_eval) * (Math.exp(-z * z) / z);
};//erfc

/** Returns `{cdf, lo, hi}` for a 'GaussianDefined' peak, or null for other types (i.e., 'DataDefined').
 `cdf(x)` is the peak counts below energy x (up to a constant), and outside [lo,hi] the peak is negligible.
 Per-peak constants are computed once here, so `cdf` is cheap to call for every drawn point.
 These are the integrals of the PeakDists (C++) distributions; each is normalized by its own total, so the
 peak area is `Amplitude` (the C++ `DistNorm` is not needed).
 */
SpectrumChartD3.prototype._peakShape = function( peak ){
  if( !peak || (peak.type !== 'GaussianDefined') )
    return null;

  const erfc = SpectrumChartD3.erfc;
  const mean = peak.Centroid[0], sigma = peak.Width[0], amp = peak.Amplitude[0];
  const skew = (i) => peak['Skew' + i][0];
  const t = (x) => (x - mean)/sigma;
  const INV_SQRT2 = 0.7071067811865476, INV_SQRT_PI = 0.5641895835477563, SQRT_2PI = 2.5066282746310002;

  // Skewed peaks come with the range they are visible over (`visRange`), or else we use the whole ROI.  A
  //  Gaussian is cut where its tails fall below ~1e-4 counts per sigma: 4.6 sigma for amp=10, 7.3 for 1e8.
  const vr = peak.visRange, gaus = !peak.skewType || (peak.skewType === 'NoSkew');
  const nsigma = Math.max( 4, Math.sqrt( 2*Math.log( 4e3*Math.max( 1, Math.abs(amp) ) ) ) );
  const answer = { cdf: null, lo: (vr ? vr[0] : (gaus ? (mean - nsigma*sigma) : -Infinity)),
                              hi: (vr ? vr[1] : (gaus ? (mean + nsigma*sigma) : Infinity)) };

  // Standard normal CDF; computed via erfc so differences stay accurate in both tails.
  const phi = (z) => 0.5*erfc( -INV_SQRT2*z );

  // erfcx(x) = exp(x*x)*erfc(x) for x >= 0; past x=25 uses the asymptotic series to avoid overflow.
  const erfcx = (x) => {
    if( x < 25.0 )
      return Math.exp(x*x)*erfc(x);
    const inv_2x2 = 1.0/(2.0*x*x);
    let term = 1.0, sum = 1.0;
    for( let k = 1; k <= 6; ++k ){ term *= -(2*k-1)*inv_2x2; sum += term; }
    return (INV_SQRT_PI/x)*sum;
  };

  // 0.5*exp(exp_arg)*erfc(erfc_arg), overflow-safe (exp_arg - erfc_arg^2 == -0.5 z^2).
  const tailTerm = (exp_arg, erfc_arg, z) => ((exp_arg > 87.0) || (erfc_arg > 10.0))
                                     ? 0.5*erfcx(erfc_arg)*Math.exp(-0.5*z*z)
                                     : 0.5*Math.exp(exp_arg)*erfc(erfc_arg);

  // CDFs, in z = (x - mean)/sigma, of a Gaussian convolved with a low-side (Exp*Gauss / Bortel), or
  //  high-side, exponential tail of scale `s` (in units of sigma).
  const emgLow = (z, s) => (s > 0) ? (phi(z) + tailTerm( z/s + 0.5/(s*s), INV_SQRT2*(z + 1/s), z )) : phi(z);
  const emgHigh = (z, s) => (s > 0) ? (phi(z) - tailTerm( 0.5/(s*s) - z/s, INV_SQRT2*(1/s - z), z )) : phi(z);

  switch( peak.skewType )
  {
    case 'Bortel': case 'ExGauss': //https://doi.org/10.1016/0883-2889(87)90180-8
    {
      const s = skew(0)/sigma;
      answer.cdf = (x) => amp*emgLow( t(x), s );
      break;
    }

    case 'CB': //https://en.wikipedia.org/wiki/Crystal_Ball_function
    {
      const a = skew(0), n = skew(1);
      const C = (n/a)*Math.exp(-0.5*a*a)/(n - 1), phiA = phi(-a), f = amp/(C + SQRT_2PI*phi(a));
      // The power-law tail integrates to C*((n/a)/(n/a - a - z))^(n-1), for z <= -a
      answer.cdf = (x) => {
        const z = t(x);
        return f*((z <= -a) ? (C*Math.pow( (n/a)/((n/a) - a - z), n - 1 )) : (C + SQRT_2PI*(phi(z) - phiA)));
      };
      break;
    }

    case 'DSCB': //http://nrs.harvard.edu/urn-3:HUL.InstRepos:29362185, chapter 6
    {
      // Piecewise indefinite integrals (in units of t), as the C++ uses; the direct closed-form areas sum wrong.
      const al = skew(0), nl = skew(1), ar = skew(2), nr = skew(3);
      const left = (z) => {
        const t_1 = 1.0 - (al / (nl / (al + z)));
        return -Math.exp(-0.5*al*al)*(t_1 / Math.pow(t_1, nl)) / ((al / nl) - al);
      };
      const right = (z) => Math.exp(-0.5*ar*ar)*(1 / ((ar / nr) - ar)) * Math.pow((1 + ((ar * (z - ar)) / nr)), (1 - nr));
      const coreL = SQRT_2PI*phi(-al), coreR = SQRT_2PI*phi(ar), leftL = left(-al), rightR = right(ar);
      const f = amp/(leftL + coreR - coreL - rightR);  // left(-inf) = right(inf) = 0
      answer.cdf = (x) => {
        const z = t(x);
        if( z < -al )
          return f*(left(z) - leftL);
        if( z <= ar )
          return f*(SQRT_2PI*phi(z) - coreL);
        return f*(coreR - coreL + right(z) - rightR);
      };
      break;
    }

    case 'GaussExp': //https://arxiv.org/abs/1603.08591
    {
      const k = skew(0), edge = Math.exp(-0.5*k*k)/k, phiK = phi(-k), f = amp/(edge + SQRT_2PI*phi(k));
      answer.cdf = (x) => {
        const z = t(x);
        return f*((z < -k) ? (Math.exp(0.5*k*k + k*z)/k) : (edge + SQRT_2PI*(phi(z) - phiK)));
      };
      break;
    }

    case 'ExpGaussExp': //https://arxiv.org/abs/1603.08591
    {
      const kl = skew(0), kr = skew(1);
      const edgeL = Math.exp(-0.5*kl*kl)/kl, phiL = phi(-kl), phiR = phi(kr);
      const f = amp/(edgeL + SQRT_2PI*(phiR - phiL) + Math.exp(-0.5*kr*kr)/kr);
      answer.cdf = (x) => {
        const z = t(x);
        if( z < -kl )
          return f*Math.exp(0.5*kl*kl + kl*z)/kl;
        if( z <= kr )
          return f*(edgeL + SQRT_2PI*(phi(z) - phiL));
        return f*(edgeL + SQRT_2PI*(phiR - phiL) + (Math.exp(-0.5*kr*kr) - Math.exp(0.5*kr*kr - kr*z))/kr);
      };
      break;
    }

    case 'VoigtBortel': case 'VoigtPlusExGauss':
    {
      // (1-R)*Voigt + R*Exp*Gauss; Skew0=gamma_lor (Lorentzian HWHM), Skew1=R, Skew2=tau (Exp*Gauss skew).
      //  The Voigt is the Thompson-Cox-Hastings pseudo-Voigt: a mixture of Gaussian and Lorentzian.
      const gamma_lor = skew(0), R = skew(1), s = skew(2)/sigma;
      const sqrt2ln2 = 1.17741002251;
      const fG = 2 * sigma * sqrt2ln2, fL = 2 * gamma_lor;
      const fV = Math.pow( Math.pow(fG,5) + 2.69269*Math.pow(fG,4)*fL + 2.42843*Math.pow(fG,3)*fL*fL
                           + 4.47163*fG*fG*Math.pow(fL,3) + 0.07842*fG*Math.pow(fL,4) + Math.pow(fL,5), 0.2 );
      const r = fL / fV;
      const eta = Math.max(0, Math.min(1, 1.36603*r - 0.47719*r*r + 0.11116*r*r*r));
      const sigma_p = fV / (2 * sqrt2ln2), gamma_p = fV / 2;
      answer.cdf = (x) => {
        const voigt = eta*(0.5 + Math.atan((x - mean)/gamma_p)/Math.PI) + (1 - eta)*phi((x - mean)/sigma_p);
        return amp*((1 - R)*voigt + R*emgLow( t(x), s ));
      };
      break;
    }

    case 'GaussBortel': case 'GaussPlusExGauss':
    {
      // (1-R)*Gaussian + R*Exp*Gauss; Skew0=R, Skew1=tau
      const R = skew(0), s = skew(1)/sigma;
      answer.cdf = (x) => { const z = t(x); return amp*((1 - R)*phi(z) + R*emgLow(z, s)); };
      break;
    }

    case 'DoubleBortel': case 'DoubleExGauss':
    {
      // Bortels & Collaers 1987: (1-eta)*Exp*Gauss(tau1) + eta*Exp*Gauss(tau1 + tau2_delta);
      //  Skew0=tau1, Skew1=tau2_delta, Skew2=eta
      const s1 = skew(0)/sigma, s2 = (skew(0) + skew(1))/sigma, eta = skew(2);
      answer.cdf = (x) => { const z = t(x); return amp*((1 - eta)*emgLow(z, s1) + eta*emgLow(z, s2)); };
      break;
    }

    case 'GadrasGeneric': case 'GadrasCZT':
    {
      // GADRAS peak shape: port of PeakDists_imp.hpp gadras_build_peak_shape / gadras_peak_shape_cdf.  A
      //  Gaussian core mixed with one-sided exponential tails convolved with the Gaussian (closed-form EMGs),
      //  the continuum limit of GADRAS's 128-point discrete form.
      //  Skew params: Skew0=low_skew, Skew1=high_skew, Skew2=low_power, Skew3=high_power,
      //               Skew4=low_extent, Skew5=high_extent.
      // If the C++ was built with USE_GADRAS_TRUNCATION, peak.GadrasTrunc = [R_low, R_high] gives where (in
      //  shape-zeta units) GADRAS's tails stop, and we truncate them the same way.
      // (The PVT-like tail is not an exposed skew type, so is not implemented here.)
      const isCZT = (peak.skewType === 'GadrasCZT');
      const low_skew = skew(0), high_skew = skew(1), low_power = skew(2), high_power = skew(3);
      const low_extent = skew(4), high_extent = skew(5);
      const trunc = (Array.isArray(peak.GadrasTrunc) && (peak.GadrasTrunc.length === 2)) ? peak.GadrasTrunc : null;
      const low_reach = trunc ? trunc[0] : 0.0, high_reach = trunc ? trunc[1] : 0.0;

      // sum_skew (GetSumSkew): raw magnitudes, energy-scaled only when power >= 0.
      let skew_p = Math.abs(high_skew), skew_n = Math.abs(low_skew);
      if( high_power >= 0 ) skew_p *= Math.pow(mean/661.0, high_power);
      if( low_power >= 0 ) skew_n *= Math.pow(mean/661.0, low_power);
      const sum_skew = Math.min(1.0, (skew_p + skew_n)/100.0);

      let low_val = Math.max(0.0, low_skew), high_val = Math.max(0.0, high_skew);
      if( high_val > 0.0 ) low_val = Math.max(low_val, 0.1);
      if( (sum_skew <= 0.0) || ((low_val <= 0.0) && (high_val <= 0.0)) )
      {
        answer.cdf = (x) => amp*phi( t(x) );
        break;
      }

      // Weight e^{-R/s} of the shifted copy subtracted off to truncate a tail at R (0 => none).
      const trunc_weight = (s, reach) => ((reach <= 0.0) || (reach > 40.0*s)) ? 0.0 : Math.exp(-reach/s);
      const slope_scale = (extent) => (extent >= 0.0) ? (1.0 + extent/3.0) : Math.exp(extent/3.0);

      // Area-normalized (over the possibly-truncated range) EMG tail mixtures, as [{w,s,tw}], with the weights
      //  including SCN/SCP.
      const make_side = (coeff, scale, side_weight, reach) => {
        const tw = scale.map( (s) => trunc_weight(s, reach) );
        const area = coeff.map( (c, i) => c*scale[i]*(1.0 - tw[i]) );
        const total = area.reduce( (a, b) => a + b, 0.0 );
        return scale.map( (s, i) => ({ w: ((total > 0.0) ? (side_weight*area[i]/total) : 0.0), s: s, tw: tw[i] }) );
      };

      const scn = low_val/(low_val + high_val), scp = high_val/(low_val + high_val);
      let low = [], high = [];
      if( low_val > 0.0 )
      {
        const sl = (isCZT ? 0.1 : 0.2)*slope_scale(low_extent)*low_val;
        low = isCZT ? make_side( [0.8, 0.2], [sl, sl/0.8], scn, low_reach )
                    : make_side( [1.0 - 0.04*low_val, 0.04*low_val], [sl, sl/0.4], scn, low_reach );
      }
      if( high_val > 0.0 )
      {
        const sh = (isCZT ? 0.1 : 0.2)*slope_scale(high_extent)*high_val;
        high = isCZT ? make_side( [0.8, 0.2], [sh, sh/0.65], scp, high_reach )
                     : make_side( [1.0], [sh], scp, high_reach );
      }

      const low_zeta_factor = Math.pow(661.0/mean, Math.max(0.0, low_power));
      const high_zeta_factor = Math.pow(661.0/mean, Math.max(0.0, high_power));

      answer.cdf = (x) => {
        const zeta = t(x);
        const z = zeta*((zeta > 0.0) ? high_zeta_factor : low_zeta_factor);
        const shape_gauss = phi(z);
        let shp = shape_gauss;
        for( const c of low )
        {
          let F = emgLow( z, c.s );
          if( c.tw > 0.0 )
            F = (F - c.tw*emgLow( z + low_reach, c.s ))/(1.0 - c.tw);
          shp += c.w*(F - shape_gauss);
        }
        for( const c of high )
        {
          let F = emgHigh( z, c.s );
          if( c.tw > 0.0 )
            F = (F - c.tw*emgHigh( z - high_reach, c.s ))/(1.0 - c.tw);
          shp += c.w*(F - shape_gauss);
        }
        return amp*Math.min( 1.0, Math.max( 0.0, (1.0 - sum_skew)*phi(zeta) + sum_skew*shp ) );
      };
      break;
    }

    default: // 'NoSkew' (or an unknown skew type)
      answer.cdf = (x) => amp*phi( t(x) );
  }//switch( peak.skewType )

  return answer;
};//SpectrumChartD3.prototype._peakShape

/** Returns a function giving the ROI continuum counts below energy x (up to a constant), or null if the ROI
 has no continuum.
 */
SpectrumChartD3.prototype._continuumCdf = function( roi ){
  const type = roi.type ? roi.type : '';

  if( (type === 'External') || type.includes('Step') )
  {
    // Defined by per-channel counts: cumulative counts are linear within each channel, and past the ends we
    //  extrapolate using the edge channels' densities (`energies` has one more entry than `counts`).
    const energies = roi.continuumEnergies, counts = roi.continuumCounts;
    const n = Math.min( energies ? (energies.length - 1) : 0, counts ? counts.length : 0 );
    if( n < 1 )
      return null;

    const cum = new Float64Array( n + 1 );
    for( let i = 0; i < n; ++i )
      cum[i+1] = cum[i] + counts[i];
    const density = (i) => counts[i] / (energies[i+1] - energies[i]);

    return function( x ){
      const i = Math.min( n - 1, Math.max( 0, d3.bisectRight( energies, x ) - 1 ) );
      return cum[i] + (x - energies[i])*density(i);
    };
  }//if( continuum is defined by channel counts )

  const coeffs = roi.coeffs, ref = roi.referenceEnergy;
  if( (type === 'NoOffset') || !coeffs || !coeffs.length )
    return null;

  return function( x ){
    const dx = x - ref;
    let answer = 0.0, dxpow = dx;
    for( let i = 0; i < coeffs.length; ++i, dxpow *= dx )
      answer += coeffs[i]*dxpow/(i + 1);
    return answer;
  };
};//SpectrumChartD3.prototype._continuumCdf

/** Continuum counts of `roi` between energies x0 and x1. */
SpectrumChartD3.prototype.offset_integral = function( roi, x0, x1 ){
  const cdf = this._continuumCdf( roi );
  return cdf ? Math.max( cdf(x1) - cdf(x0), 0.0 ) : 0.0;
};

/** -------------- Peak ROI / Label Rendering -------------- */
SpectrumChartD3.prototype.drawPeaks = function() {
  const self = this;

  self.peakVis.selectAll("*").remove();
  self.peakPaths = [];

  if( !this.rawData || !this.rawData.spectra )
    return;

  const minx = self.xScale.domain()[0], maxx = self.xScale.domain()[1];
  const pxPerKeV = (self.xScale(maxx) - self.xScale(minx)) / (maxx - minx);
  const kSamplePx = 1.0;  // Approximate spacing, in pixels, of the points peaks are drawn with
  const showlabels = (self.options.showUserLabels || self.options.showPeakLabels || self.options.showNuclideNames);
  const labelinfo = [];

  const ybottom = Math.max.apply( null, self.yScale.range() );
  const ypx = function( y ){ const v = self.yScale(y); return isFinite(v) ? v : ybottom; };
  const toPx = (v) => Math.round( 100*v ) / 100;  // Plenty of precision, while keeping path strings short

  /* Content of histogram `pts` (bin k spans [pts[k].x, pts[k+1].x), with content yOf(k), by default pts[k].y)
     at energy x, linearly interpolated between bin centers: the bins content at its center, without stair-steps. */
  function histAt( pts, x, yOf ){
    const n = pts.length, y = yOf ? yOf : ((k) => pts[k].y);
    if( n < 2 )
      return n ? y(0) : 0;
    const center = (k) => (k + 1 < n) ? 0.5*(pts[k].x + pts[k+1].x) : (1.5*pts[k].x - 0.5*pts[k-1].x);
    let k = Math.min( n - 1, Math.max( 0, self._bisecX.right( pts, x ) - 1 ) );
    if( (k > 0) && (x < center(k)) )
      --k;
    if( k === (n - 1) )
      return y(k);
    const frac = Math.min( 1, Math.max( 0, (x - center(k)) / (center(k+1) - center(k)) ) );
    return y(k) + frac*(y(k+1) - y(k));
  }//function histAt( pts, x, yOf )

  /* Returns the drawing geometry of a ROI ({fills, outlines, cont, labels, yRangePx}), or null.

     The curves give, at each energy x, the counts a displayed (possibly rebinned) channel centered on x
     would contain: F(x + w/2) - F(x - w/2), with F the cumulative continuum (plus peak) counts, and w the
     displayed channel width.  This passes exactly through the tops of the spectrum's histogram bars at
     channel centers, and is smooth between them.  Channels much wider than the peaks are only sampled at
     their center (between centers the curve would rise above the bars), so zoomed out, the curves are the
     lines through the tops of the bars - and a narrow peak still shows, as tall as the bars it is in.
   */
  function roiGeometry( roi, points, scaleFactor, background ){
    const npts = points ? points.length : 0;
    const xa = Math.max( roi.lowerEnergy, minx ), xb = Math.min( roi.upperEnergy, maxx );
    if( (npts < 2) || !(xb > xa) )
      return null;

    const shapes = roi.peaks.map( (pk) => self._peakShape( pk ) );

    // Samples are spaced about every kSamplePx pixels, but no closer than sigma/16 of the narrowest peak in view
    //  (straight segments then deviate < 0.1% from the curve), so deep zooms don't evaluate thousands of points.
    let minSigma = Infinity;
    shapes.forEach( (s, j) => { if( s && (s.lo <= xb) && (s.hi >= xa) ) minSigma = Math.min( minSigma, roi.peaks[j].Width[0] ); } );
    const stepPx = Math.min( 20, Math.max( kSamplePx, minSigma*pxPerKeV/16 ) );

    // Sample energies `xs`, and the displayed channel width at each, `ws`.  Samples are anchored to channels
    //  (so panning doesn't make the curves wiggle), with an odd number per channel so its center is always
    //  included.  The curves are the lines through the samples, clipped to [xa, xb], so we also keep the last
    //  sample at or before xa, and the first at or after xb (or use xa or xb themselves, if there is none).
    const binWidth = (k) => (k + 1 < npts) ? (points[k+1].x - points[k].x) : (points[k].x - points[k-1].x);
    const binAt = (x) => Math.min( npts - 1, Math.max( 0, self._bisecX.right( points, x ) - 1 ) );
    const xs = [xa], ws = [binWidth( binAt(xa) )];
    let done = false;
    for( let k = Math.max( 0, binAt(xa) - 1 ); !done && (k < npts); ++k ){
      const lo = points[k].x, w = binWidth(k);
      if( !(w > 0) )
        continue;
      // (just the center, for channels too wide to resolve the peaks)
      const m = (w > 2*minSigma) ? 1 : (1 + 2*Math.floor( 0.5*w*pxPerKeV/stepPx )), dx = w/m;
      for( let s = Math.min( m - 1, Math.max( 0, Math.floor( (xa - lo)/dx - 0.5 ) ) ); !done && (s < m); ++s ){
        const x = lo + (s + 0.5)*dx;
        if( x <= xa ){
          xs[0] = x;
          ws[0] = w;
        }else{
          xs.push( x );
          ws.push( w );
          done = (x >= xb);
        }
      }
    }//for( loop over displayed channels )
    if( !done ){
      xs.push( xb );
      ws.push( binWidth( binAt(xb) ) );
    }
    const n = xs.length;

    // Each samples window, [x - w/2, x + w/2], as indexes into `ends`, the energies CDFs are evaluated at; a
    //  samples upper end is usually the lower end of the same sample in the next channel, so is shared.
    const ends = [], iLo = new Int32Array( n ), iHi = new Int32Array( n ), eps = 1e-6/pxPerKeV;
    for( let a = 0, b = 0; (a < n) || (b < n); ){
      const takeLo = (b >= n) || ((a < n) && ((xs[a] - 0.5*ws[a]) <= (xs[b] + 0.5*ws[b])));
      const e = takeLo ? (xs[a] - 0.5*ws[a]) : (xs[b] + 0.5*ws[b]);
      if( !ends.length || (Math.abs( e - ends[ends.length - 1] ) > eps) )
        ends.push( e );
      if( takeLo ) iLo[a++] = ends.length - 1; else iHi[b++] = ends.length - 1;
    }

    // The end samples get moved onto xa and xb, along the line to their neighbor.
    const fa = (xa - xs[0])/(xs[1] - xs[0]), fb = (xb - xs[n-2])/(xs[n-1] - xs[n-2]);
    xs[0] = xa;
    xs[n-1] = xb;

    // Sets out[d0..d1] to the counts in each samples window, by `cdf`, for samples i0 through i1 (zero for the
    //  others), with the end samples moved onto xa and xb.  Each CDF value is computed once (cached by `tag`).
    let tag = 0;
    const Fv = new Float64Array( ends.length ), Ftag = new Int32Array( ends.length );
    const cdfAt = function( cdf, j ){
      if( Ftag[j] !== tag ){
        Ftag[j] = tag;
        Fv[j] = cdf( ends[j] );
      }
      return Fv[j];
    };
    const windowCounts = function( cdf, out, d0, d1, i0, i1 ){
      ++tag;
      for( let i = d0; i <= d1; ++i )
        out[i] = ((i < i0) || (i > i1)) ? 0.0 : scaleFactor*Math.max( 0, cdfAt( cdf, iHi[i] ) - cdfAt( cdf, iLo[i] ) );
      const v0 = out[0], v1 = out[1], vm = out[n-2], vn = out[n-1];
      if( d0 === 0 )
        out[0] = v0 + fa*(v1 - v0);
      if( d1 === (n - 1) )
        out[n-1] = vm + fb*(vn - vm);
    };

    // When subtracting the background, it is taken at channel centers the same way the displayed bars do.
    const bgPts = (self.options.backgroundSubtract && background && background.points && (background.points.length > 1))
                  ? background.points : null;
    const bgOfBin = (k) => { const b = bgPts[self._bisecX.left( bgPts, points[k].x )]; return b ? b.y : 0; };
    const bgAt = (x) => bgPts ? histAt( points, x, bgOfBin ) : 0.0;

    // Counts of the continuum `c`, and of it plus the peaks so far `stack`, and their pixel y-values.
    const c = new Float64Array( n ), stack = new Float64Array( n ), p = new Float64Array( n );
    const cPx = new Float64Array( n ), stackPx = new Float64Array( n ), topPx = new Float64Array( n );
    const contCdf = self._continuumCdf( roi );
    if( contCdf )
      windowCounts( contCdf, c, 0, n - 1, 0, n - 1 );
    for( let i = 0; i < n; ++i ){
      stack[i] = (c[i] -= bgAt( xs[i] ));
      stackPx[i] = cPx[i] = ypx( c[i] );
    }

    const xpx = xs.map( (x) => self.xScale(x) );

    // Returns " x,y x,y ..." for samples i0 through i1 (reversed, if `rev`), with `yPx(i)` their pixel y.  Samples
    //  within 0.1px (vertically) of the line drawn past them are left out; `lo` and `hi` bound the slopes from
    //  the last point drawn that keep every left out sample within this.
    const coords = function( yPx, i0, i1, rev ){
      let s = "", ax = 0, ay = 0, lo = 0, hi = 0, px = 0, py = 0;
      const draw = function( x, y ){
        s += " " + toPx(x) + "," + toPx(y);
        ax = x; ay = y; lo = -Infinity; hi = Infinity;
      };
      for( let j = i0; j <= i1; ++j ){
        const i = rev ? (i0 + i1 - j) : j, x = xpx[i], y = yPx(i);
        if( j === i0 ){
          draw( x, y );
        }else{
          let u = Math.abs( x - ax );
          const slope = (y - ay)/u;
          if( !(slope >= lo) || !(slope <= hi) ){
            draw( px, py );
            u = Math.abs( x - ax );
          }
          if( u > 0 ){
            lo = Math.max( lo, (y - ay - 0.1)/u );
            hi = Math.min( hi, (y - ay + 0.1)/u );
          }
        }
        px = x;
        py = y;
      }
      if( i1 > i0 )
        draw( px, py );
      return s;
    };

    // First sample index for which `pred(i)` is true, for a `pred` that is false and then true.
    const firstTrue = function( pred ){
      let lo = 0, hi = n;
      while( lo < hi ){
        const mid = (lo + hi) >> 1;
        if( pred(mid) ) hi = mid; else lo = mid + 1;
      }
      return lo;
    };

    const geom = { fills: [], outlines: [], cont: null, labels: [], yRangePx: null };
    const npeaks = roi.peaks.length;

    // Peaks are stacked in ROI order: each peaks fill sits on top of the continuum plus the earlier peaks,
    //  while (in multi-peak ROIs) each peaks outline sits on just the continuum.
    roi.peaks.forEach( function( peak, j ){
      const shape = shapes[j];
      if( !shape && (peak.type !== 'DataDefined') )
        return;

      // The samples whose windows the peak is in (the only peak in a ROI is drawn across the whole ROI); its
      //  fill and outline span one more sample each side, to start and end on what it sits on.
      const whole = !shape || (npeaks === 1);
      const i0 = whole ? 0 : firstTrue( (i) => (ends[iHi[i]] >= shape.lo) );
      const i1 = whole ? (n - 1) : (firstTrue( (i) => (ends[iLo[i]] > shape.hi) ) - 1);
      if( i0 > i1 )
        return;
      const d0 = Math.max( 0, i0 - 1 ), d1 = Math.min( n - 1, i1 + 1 );

      if( shape )
        windowCounts( shape.cdf, p, d0, d1, i0, i1 );
      else  // A 'DataDefined' peak is the data above the continuum
        for( let i = 0; i < n; ++i )
          p[i] = Math.max( 0, histAt(points, xs[i]) - bgAt(xs[i]) - c[i] );

      for( let i = d0; i <= d1; ++i )
        topPx[i] = ypx( stack[i] + p[i] );

      geom.fills.push( { peak: peak,
        d: "M" + coords( (i) => stackPx[i], d0, d1, false ) + coords( (i) => topPx[i], d0, d1, true ) + "Z" } );

      if( npeaks > 1 )
        geom.outlines.push( { peak: peak, d: "M" + coords( (i) => cPx[i], d0, d0 )
                               + coords( (i) => ypx(c[i] + p[i]), d0, d1 ) + coords( (i) => cPx[i], d1, d1 ) } );

      for( let i = d0; i <= d1; ++i ){
        stack[i] += p[i];
        stackPx[i] = topPx[i];
      }

      const mean = peak.Centroid ? peak.Centroid[0] : NaN;
      if( showlabels && (mean >= xa) && (mean <= xb) ){
        // The continuum, and continuum plus this peak, at the centroid along the drawn lines
        const i = Math.max( 1, firstTrue( (i) => (xs[i] >= mean) ) ), f = (mean - xs[i-1])/(xs[i] - xs[i-1]);
        const cont = c[i-1] + f*(c[i] - c[i-1]), pk = p[i-1] + f*(p[i] - p[i-1]);
        geom.labels.push( { roi: roi, peak: peak, energy: mean, userLabel: peak.userLabel,
                            centroidXPx: self.xScale(mean), centroidMinYPx: ypx(cont), centroidMaxYPx: ypx(cont + pk) } );
      }
    } );//roi.peaks.forEach

    geom.cont = "M" + coords( (i) => cPx[i], 0, n - 1 );

    let minypx = Infinity, maxypx = -Infinity;
    for( let i = 0; i < n; ++i ){
      minypx = Math.min( minypx, cPx[i], stackPx[i] );
      maxypx = Math.max( maxypx, cPx[i], stackPx[i] );
    }
    geom.yRangePx = [minypx, maxypx];

    return geom;
  }//function roiGeometry(...)

  function draw_roi( roi, specindex, spectrum ){
    if( !roi || !roi.peaks || !roi.peaks.length || (roi.lowerEnergy > maxx) || (roi.upperEnergy < minx)
        || (self.yScale.domain()[0] === self.yScale.domain()[1]) )
      return;

    const scaleFactor = ((spectrum.type !== self.spectrumTypes.FOREGROUND) && (typeof spectrum.yScaleFactor === 'number'))
                        ? spectrum.yScaleFactor : 1.0;
    const geom = roiGeometry( roi, spectrum.points, scaleFactor, self.getSpectrumByID(spectrum.backgroundID) );
    if( !geom )
      return;

    Array.prototype.push.apply( labelinfo, geom.labels );

    const multi = (roi.peaks.length > 1);
    const xRangePx = [self.xScale(roi.lowerEnergy), self.xScale(roi.upperEnergy)];

    // Adds a peak's path, returning its color.  The paths responding to the mouse are those with `isOutline`
    //  set: a single-peak ROI's fill, or a multi-peak ROI's per-peak outlines.
    const addPath = function( item, isFill ){
      const peak = item.peak;
      const color = (peak.lineColor && peak.lineColor.length) ? peak.lineColor : spectrum.peakColor;
      const path = self.peakVis.append("path")
        .attr("d", item.d )
        .attr("class", (isFill ? "peakFill" : "peakOutline") )
        .attr("stroke-width", 1)
        .attr("stroke", color )
        .attr("fill-opacity", (isFill ? 0.6 : 0.0) )
        .attr("data-energy", (peak.Centroid ? peak.Centroid[0].toFixed(2) : 0) );
      if( isFill )
        path.style("fill", color );

      const info = {
        path: path,
        roi: roi,
        lowerEnergy: roi.lowerEnergy,
        upperEnergy: roi.upperEnergy,
        yRangePx: geom.yRangePx,
        xRangePx: xRangePx,
        color: color,
        isOutline: (!isFill || !multi),
        isFill: isFill,
        peak: peak,
        spectrumIndex: specindex
      };
      self.peakPaths.push( info );

      path.node().__peakInfo = info;  // for the hover/context-menu handlers delegated to peakVis
      return color;
    };//addPath

    const fillColors = geom.fills.map( (item) => addPath(item, true) );
    geom.outlines.forEach( (item) => addPath(item, false) );

    // The continuum line, for the foreground only - unless a single peak's fill outline already draws it.
    if( (spectrum.type === self.spectrumTypes.FOREGROUND)
        && (multi || (fillColors.length !== 1) || (fillColors[0] !== spectrum.peakColor)) ){
      self.peakVis.append("path")
        .attr("d", geom.cont )
        .attr("stroke-width", 1)
        .attr("fill-opacity", 0)
        .attr("stroke", spectrum.peakColor );
    }
  };//function draw_roi(roi,specindex,spectrum)

  for (let i = 0; i < this.rawData.spectra.length; i++) {
    const spectrum = this.rawData.spectra[i];
    if( !self._peaksDrawnFor( spectrum ) )
      continue;

    const peaks = spectrum.peaks;
    if( peaks ){
      peaks.forEach( function(roi){
        // Draw the original ROI until the server returns an updated one, so it doesn't vanish on mousedown
        if( self.roiIsBeingDragged && self.roiBeingDrugUpdate && roi == self.roiBeingDragged.roi )
          draw_roi(self.roiBeingDrugUpdate,i,spectrum);
        else
          draw_roi(roi,i,spectrum);
      });
    }//if( this.rawData.spectra[i].peaks )

    if( (self.leftDragMode === 'fitPeak') && self.roiBeingDrugUpdate && (this.rawData.spectra[i].type === self.spectrumTypes.FOREGROUND) )
      draw_roi(self.roiBeingDrugUpdate,i,spectrum);
  }

  self.drawPeakLabels( labelinfo );
}

/* Whether the peaks of `spectrum` are drawn (background peaks are hidden while subtracting it). */
SpectrumChartD3.prototype._peaksDrawnFor = function( spectrum ){
  const t = spectrum.type, T = this.spectrumTypes, show = this.options.drawPeaksFor;
  if( (t === T.FOREGROUND) || (t === T.SECONDARY) )
    return !!show[t];
  if( t === T.BACKGROUND )
    return !!show[t] && !this.options.backgroundSubtract;
  return true;
};

SpectrumChartD3.prototype.drawPeakLabels = function( labelinfos ) {
  const self = this;

  // Label positions are adjusted after a short idle (the timeout at the bottom of this function)
  window.clearTimeout( self.adjustLabelTimeout );
  self.adjustLabelTimeout = null;

  if( !labelinfos || !labelinfos.length )
    return;

  if( !self.options.showUserLabels && !self.options.showPeakLabels && !self.options.showNuclideNames )
    return;

  const chart = self.peakVis;
  const chartBox = self.plot.node().getBBox();

  // Chart not set up yet
  if( !chartBox.width || !chartBox.height )
    return;

  // labelinfos: [{roi, peak, energy, userLabel, centroidXPx, centroidMinYPx, centroidMaxYPx}] (see roiGeometry)
  const fontSize = self.options.peakLabelSize;
  const rotationAngle = self.options.peakLabelRotation;
  
  // The labels added, with their data and positions
  let label_array = [];

  const visCtm = self.peakVis.node().getCTM();  // label positions are relative to peakVis

  // Rotates `label`, keeping it roughly centered over the peak, `dy` above the peak top
  function setLabelTransform(label,roiInfo,dx,dy){
    const peak_x = roiInfo.centroidXPx;
    const peak_uy = roiInfo.centroidMaxYPx;
    const lbb = label.node().getBBox();
    
    // Rotate about the bottom-left corner, but keep the label naively centered on the peak in x
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
    
    if( peak_uy===null || Number.isNaN(peak_uy) || typeof(peak_uy)==='undefined' ){
      continue;
    }
    
    let labelRows = [];
    if( self.options.showUserLabels && info.userLabel && info.userLabel.length )
      labelRows.push( info.userLabel );
    
    if( self.options.showPeakLabels )
      labelRows.push( info.energy.toFixed(2) + " keV" );
    
    if( self.options.showNuclideNames && nuclide ) {
      let txt = nuclide.name;
      if( self.options.showNuclideEnergies ) // only shown along with the nuclide name
      txt += ", " + nuclide.energy.toFixed(2).toString() + " keV" + (nuclide.type ? " " + nuclide.type : "");
      labelRows.push( txt );
    }//if( show nuclide name and we have a nuclide )
    
    if( !labelRows.length )
      continue;

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
        .attr("dy", (i===0) ? ("-" + (labelRows.length-1) + ".2em") : "1em" ) // the extra 0.2em vertically centers the text
        .attr("x", 0)
        .attr("font-size", fontSize)
        .attr("alignment-baseline","baseline")
        .text(labelRows[i]);
    }

    // Bold on mouse-over
    label.on("mouseover", function(){ self.highlightLabel(this,false); } )
      .on("mouseout",  function(){ self.unHighlightLabel(true); } );
    
    if( self.isTouchDevice() )
      label.on("touchstart", function(){ self.highlightLabel(this,false); } );
    
    // Initial naive position; record the label's corners in peakVis coordinates
    setLabelTransform( label, info, 0, -10 );

    const svgCtm = label.node().getCTM();
    const labelBBox = label.node().getBBox();
      
    const visCtmInv = visCtm.inverse();
    const toVis = function(x,y){ return self._txtToVisCoords( svgCtm, visCtmInv, x, y ); };

    let data = {};
    data.initialPos = [ toVis( 0, 0 ), //bottom-left of text
      toVis( labelBBox.width, 0 ), //bottom right of text
      toVis( labelBBox.width, -labelBBox.height ), //top-right of text
      toVis( 0, -labelBBox.height ) //top-left of text
    ];
    
    data.offset = [0, -10];
    data.labelInfo = info;
    data.label = label;
    data.nominalX = peak_x;
    data.nominalY = peak_uy - 10;
    
    label_array.push( data );
  }//labelinfos.foreach(...)

  const chartw = chartBox.width;

  /* Places labels by simulated annealing (force layout didn't work out); idea from
     https://github.com/tinker10/D3-Labeler, though our needs differ enough not to use it directly. */
  function adjustLabels(){

    // Penalty for a label's placement: its offset from the default, and overlap with other labels
    function weightFcn( index ) {
      const label = label_array[index];
      const dx = label.offset[0], dy = label.offset[1] + 10;
        
      // Moving in x is okay, but not closer to the peak in y
      let penalty = 0.5 * Math.abs(dx) * Math.sqrt(Math.abs(dx));
      penalty += ((dy > 10) ? 20 : ((dy > 0) ? 4 : -1)) * dy;
      
      // Overlaps are computed with the rectangles rotated (about the origin) to be axis-aligned
      function rotatePoint(p) {
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
            
        let margin_overlap = overlap_area;
        for( let y = 0; y < 2; ++y ){
          ll2.x -= 3; ll2.y -= 3;
          ur2.x += 3; ur2.y += 3;
          
          const this_overlap = overlappingArea(ll1, ur1, ll2, ur2);
          console.assert( this_overlap >= margin_overlap );

          // Penalty for being close to another label
          penalty += 0.5*(2 - y)*(this_overlap - margin_overlap);
          margin_overlap = this_overlap;
        }
      }
        
      return penalty;
    };//function weightFcn
      
    // Randomly moves a label, keeping the move according to the current temperature
    function tryNewMonteCarloPosition( currentTemperature ){
      const i = Math.floor(Math.random() * label_array.length);
      let label = label_array[i];
        
      const prev_x = label.offset[0], prev_y = label.offset[1];
        
      let new_dx = prev_x + (Math.random() - 0.5) * 15.0;
      let new_dy = prev_y + (Math.random() - 0.5) * 7.5;
        
      // Don't go off the chart, or into the peak
      for( let posIndex = 0; posIndex < label.initialPos.length; ++posIndex ){
        const ip = label.initialPos[posIndex];
        if( (ip.x + new_dx) > chartw )
          new_dx = prev_x;
        if( (ip.x + new_dx) < 0 )
          new_dx = prev_x;
            
        // TODO: use the overlap with the ROI outline paths, rather than centroidMaxYPx, as the floor
        //       (see https://gist.github.com/mbostock/8027637, or https://github.com/d3/d3-polygon)
        if( (ip.y + new_dy) < label.labelInfo.centroidMaxYPx )
          new_dy = prev_y;
      }
        
      // Reject moving further above the chart
      const initialTop = Math.min(label.initialPos[0].y, label.initialPos[1].y, label.initialPos[2].y, label.initialPos[3].y);
      if( ((initialTop + new_dy) < 0) && (new_dy < prev_y) )
        new_dy = prev_y;
        
      // Keep the labels in the same x order
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
        
      if( threshold >= prob ){
        label.offset[0] = prev_x;
        label.offset[1] = prev_y;
        return false;
      }
        
      return true;
    };//function tryNewMonteCarloPosition

    function monteCarloFindLabelPos( numIterations ){
      let currentTemperature = 1.0;
      for( let i = 0; i < numIterations; ++i ){
        for( let j = 0; j < label_array.length; ++j ){
          tryNewMonteCarloPosition( currentTemperature );
        }
        currentTemperature = currentTemperature - (1.0 / numIterations); // linear cooling
      }
    };//function monteCarloFindLabelPos

    monteCarloFindLabelPos( 1000 );

    for( let i = 0; i < label_array.length; ++i ){
      setLabelTransform( label_array[i].label, label_array[i].labelInfo, label_array[i].offset[0], label_array[i].offset[1] );
    }
  }; //adjustLabels function
  
  // Placing labels is expensive, so only once they haven't been updated for 50ms
  self.adjustLabelTimeout = window.setTimeout( adjustLabels, 50 );
}//SpectrumChartD3.prototype.drawPeakLabels = ...

/* Sets whether peaks are drawn for a spectrum type ('FOREGROUND', 'BACKGROUND', 'SECONDARY') */
SpectrumChartD3.prototype.setShowPeaks = function(spectrum,show) {
  this.options.drawPeaksFor[spectrum] = show;
  this.redraw()();
}

SpectrumChartD3.prototype.setShowUserLabels = function(d) {
  this.options.showUserLabels = d;
  this.redraw()();
}

SpectrumChartD3.prototype.setShowPeakLabels = function(d) {
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

/** Handles a peak-fit ("define ROI") drag: a Ctrl-left-mouse drag, or a two-finger rightward swipe (which
 also draws two vertical guide lines).
 TODO: number keys to force the number of peaks; keys to choose the continuum polynomial order; return a
       zero-amplitude peak when the C++ fit fails; name the temporary ROI consistently with handleRoiDrag
       and updateRoiBeingDragged.
 */
SpectrumChartD3.prototype.handleMouseMovePeakFit = function() {
  const self = this;

  if( !self.hasSpectrumData()
      || self.rawData.spectra[0].y.length < 10 ) {
    return;
  }

  d3.select('body').style("cursor", "ew-resize");

  self.peakFitMouseMove = d3.mouse(self.vis[0][0]);
  self.peakFitTouchMove = d3.touches(self.vis[0][0]);

  let leftpospx, rightpospx;
  if( self.peakFitTouchMove.length > 0 ) {
    const t = self.peakFitTouchMove;
    const startTouchs = self.createPeaksStartTouches;

    /* A finger lifted mid-fit, or a stray single touch: tear down */
    if( (t.length !== 2) || !startTouchs || (startTouchs.length !== 2) ) {
      self.handleCancelTouchPeakFit();
      return;
    }

    /* Latch the mode: the touch recognizer keeps selecting peak-fit from here on */
    self.leftDragMode = 'fitPeak';

    /* Undo any zooming from jitter before fit-peak mode was recognized */
    if( self.origdomain ){
      const currX = self.xScale.domain(), prevX = self.origdomain;
      if( (currX[0] != prevX[0]) || (currX[1] != prevX[1]) ){
        self.xScale.domain( prevX );
        self.redraw()();
      }
    }

    leftpospx = Math.min( startTouchs[0][0], startTouchs[1][0] );
    rightpospx = Math.min( Math.max( t[0][0], t[1][0] ), self.xScale.range()[1] );

    self._updateTouchPeakFitLines( leftpospx, rightpospx );
  } else {
    if( !self.leftMouseDown || !self.peakFitMouseMove ) {
      return;
    }

    leftpospx = self.leftMouseDown[0];
    rightpospx = self.peakFitMouseMove[0];
    if( rightpospx < leftpospx )
      leftpospx = [rightpospx, rightpospx=leftpospx][0];
  }

  const lowerEnergy = self.xScale.invert(leftpospx);
  const upperEnergy = self.xScale.invert(rightpospx);
  
  self.zooming_plot = false;

  
  if( !self.origdomain )
    self.origdomain = self.xScale.domain();

  // The x-domain doesn't change while fitting
  self.xScale.domain( self.origdomain );

  let pageX = d3.event.pageX;
  let pageY = d3.event.pageY;

  // Emit the current range, at most every 2.5 seconds
  self._throttledDragEmit( 2500, function(){
    self.WtEmit(self.chart.id, {name: 'fitRoiDrag'},
                lowerEnergy, upperEnergy, -1, false, pageX, pageY );
  });
}//handleMouseMovePeakFit

/* Draws/updates the two vertical guide lines + label shown during a two-finger peak-fit gesture (for the
   mouse, the fitted ROI itself is the feedback). */
SpectrumChartD3.prototype._updateTouchPeakFitLines = function( leftPx, rightPx ){
  const self = this;
  let currentLine = self.vis.select("#createPeakTouchCurrentLine"),
      touchText = self.vis.select("#createPeakTouchText");

  /* The leftmost line marks the gesture start, so it is only ever created, not moved */
  if( self.vis.select("#createPeakTouchStartLine").empty() ) {
    self.vis.append("line")
      .attr("id", "createPeakTouchStartLine")
      .attr("class", "createPeakMouseLine")
      .attr("x1", leftPx)
      .attr("x2", leftPx)
      .attr("y1", 0)
      .attr("y2", self.size.height);
  }

  if( currentLine.empty() ) {
    currentLine = self.vis.append("line")
      .attr("id", "createPeakTouchCurrentLine")
      .attr("class", "createPeakMouseLine")
      .attr("y1", 0)
      .attr("y2", self.size.height);
  }
  currentLine.attr("x1", rightPx).attr("x2", rightPx);

  if( touchText.empty() )
    touchText = self.vis.append("text")
      .attr("id", "createPeakTouchText")
      .attr("class", "createPeakTouchText")
      .attr("y", 10)
      .text( self.options.txt.touchDefineRoi );
  touchText.attr("x", 0.5*(leftPx + rightPx - touchText.node().getBoundingClientRect().width) );
};

SpectrumChartD3.prototype.handleCancelTouchPeakFit = function() {
  const self = this;

  const touchStartLine = self.vis.select("#createPeakTouchStartLine");
  if( (self.leftDragMode !== 'fitPeak') && touchStartLine.empty() )
    return;

  self.handleMouseUpPeakFit();

  touchStartLine.remove();
  self.vis.select("#createPeakTouchCurrentLine").remove();
  self.vis.select("#createPeakTouchText").remove();

  self.leftDragMode = 'none';
}

/* Completes a peak-fit drag (mouse up, or fingers lifted). */
SpectrumChartD3.prototype.handleMouseUpPeakFit = function() {
  var self = this;

  const roi = self.roiBeingDrugUpdate;
  if( (self.leftDragMode !== 'fitPeak') || !self.rawData || !self.rawData.spectra || !roi )
    return;

  self.leftDragMode = 'none';
  
  self.redraw()();
  self.handleCancelRoiDrag();
  
  if( self.leftMouseDown ) {
    const pageX = d3.event.pageX;
    const pageY = d3.event.pageY;
    // Emit the ROI as currently shown, rather than the final mouse position (which may differ)
    self.WtEmit( self.chart.id, {name: 'fitRoiDrag'},
                 roi.lowerEnergy, roi.upperEnergy, roi.peaks.length, true, pageX, pageY );
  }else if( self.peakFitTouchMove ) {
    // C++ positions the "keep peak?" menu at these page coords; peakFitTouchMove is in vis coords, so
    // prefer the lifted finger's page coords.
    const ct = (d3.event && d3.event.changedTouches && d3.event.changedTouches.length) ? d3.event.changedTouches[0] : null;
    const pageX = ct ? ct.pageX : self.peakFitTouchMove[0][0];
    const pageY = ct ? ct.pageY : self.peakFitTouchMove[0][1];

    self.WtEmit( self.chart.id, {name: 'fitRoiDrag'},
                 roi.lowerEnergy, roi.upperEnergy, roi.peaks.length, true, pageX, pageY );
  }
}

/** Updates the Escape/Sum/Compton feature markers to the current mouse/touch position, removing them all if
 there is no position.

 @arg mouseDownEnergy Only used for "Sum Peak": the energy the user clicked (or previously tapped)
 */
SpectrumChartD3.prototype.updateFeatureMarkers = function( mouseDownEnergy ) {
  const self = this;
  
  let m;
  try{ m = d3.mouse(self.vis[0][0]) }catch(e){}

  try{
    const t = d3.touches(self.vis[0][0]);
    if( t && t.length > 0 )
      m = t[0];
  }catch (e) {
  }

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
  
  /* Deletes a feature-marker's line + measurement label (+ optional text) by id prefix. */
  function deleteMarker( prefix, hasText ) {
    self.deleteAndNullifyElements( prefix, prefix + 'Meas' );
    if( hasText )
      self.deleteAndNullifyElements( prefix + 'Text' );
  }

  const escapeMarkerSuffix = { single: 'singleEscape', double: 'doubleEscape',
                               singleForward: 'singleEscapeForward', doubleForward: 'doubleEscapeForward' };
  function deleteEscapePeakMarker( peakType ) {
    if( escapeMarkerSuffix[peakType] )
      deleteMarker( escapeMarkerSuffix[peakType], true );
  }

  function deleteComptonPeakLine(){ deleteMarker( 'comptonPeak', true ); }
  function deleteClickedSumPeakMarker(){ deleteMarker( 'clickedSumPeak', false ); }
  function deleteSumPeakMarker(){ deleteMarker( 'sumPeak', true ); }
  function deleteLeftSumPeakMarker(){ deleteMarker( 'leftSumPeak', true ); }
  
  if( !m )
  {
    removeMouseEdgeFromChart();
    deleteEscapePeakMarker('single');
    deleteEscapePeakMarker('double');
    deleteEscapePeakMarker('singleForward');
    deleteEscapePeakMarker('doubleForward');
    deleteComptonPeakLine();
    deleteMarker( 'comptonEdge', true );
    deleteClickedSumPeakMarker();
    deleteSumPeakMarker();
    deleteLeftSumPeakMarker();
    if( self.sumPeakHelpText ){
      self.sumPeakHelpText.remove();
      self.sumPeakHelpText = null;
    }
    return;
  }//if( !m )

  const energy = self.xScale.invert(m[0]),
      xmax = self.size.width,
      ymax = self.size.height;
      
  if( self.legdown || isNaN(energy) || self.currentlyAdjustingSpectrumScale )
    return;

  const cursorIsOutOfBounds = (m[0] < 0  || m[0] > xmax || m[1] < 0 || m[1] > ymax);

  let linehspace = 13;  // spacing between lines of text

  // The mouse edge is removed when all feature options are off, the cursor is out of bounds, or dragging
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
    if (deleteMouseEdge())
      return;
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

    /* Creates or updates one of the four escape-peak markers: a vertical line plus name and keV labels,
       stored on `self` as `<keyPrefix>`, `<keyPrefix>Text`, `<keyPrefix>Meas`.  `strokeWidth` (optional)
       is only set on creation. */
    function createOrUpdateEscape(keyPrefix, energyVal, pix, label, lineClass, strokeWidth) {
      var textKey = keyPrefix + 'Text';
      var measKey = keyPrefix + 'Meas';

      if (!self[keyPrefix] && energyVal >= 0) {
        self[keyPrefix] = self.vis.append("line")
          .attr("class", lineClass);
        if (strokeWidth !== undefined && strokeWidth !== null)
          self[keyPrefix].attr("stroke-width", strokeWidth);
        self[keyPrefix]
          .attr("x1", pix)
          .attr("x2", pix)
          .attr("y1", 0)
          .attr("y2", self.size.height);
        self[textKey] = self.vis.append("text")
          .attr("class", "peakText")
          .attr( "x", pix + xmax/200 )
          .attr( "y", self.size.height/5.3)
          .text( label );
        self[measKey] = self.vis.append("text")
          .attr("class", "peakText")
          .attr( "x", pix + xmax/125 )
          .attr( "y", self.size.height/5.3 + linehspace)
          .text( energyVal.toFixed(1) + " keV" );
      } else if (energyVal < 0 && self[keyPrefix] && self[textKey] && self[measKey]) {
        self.deleteAndNullifyElements( keyPrefix, textKey, measKey );
      } else if (self[keyPrefix]) {
        self[keyPrefix]
          .attr("y2", self.size.height)
          .attr("x1", pix)
          .attr("x2", pix);
        self[textKey]
          .attr( "y", self.size.height/5.3)
          .attr( "x", pix + xmax/200 );
        self[measKey]
          .attr( "y", self.size.height/5.3 + linehspace)
          .attr( "x", pix + xmax/125 )
          .text( energyVal.toFixed(1) + " keV" );
      }
    }

    if (!singleEscapeForwardOutOfBounds)
      createOrUpdateEscape('singleEscapeForward', singleEscapeForwardEnergy, singleEscapeForwardPix,
                           self.options.txt.singleEscape, 'escapeLineForward', null);

    if (!doubleEscapeForwardOutOfBounds)
      createOrUpdateEscape('doubleEscapeForward', doubleEscapeForwardEnergy, doubleEscapeForwardPix,
                           self.options.txt.doubleEscape, 'escapeLineForward', null);

    if ( singleEscapeOutOfBounds )
      return;

    createOrUpdateEscape('singleEscape', singleEscapeEnergy, singleEscapePix,
                         self.options.txt.singleEscape, 'peakLine', null);

    if (doubleEscapeOutOfBounds)
      return;

    createOrUpdateEscape('doubleEscape', doubleEscapeEnergy, doubleEscapePix,
                         self.options.txt.doubleEscape, 'peakLine', self.options.featureLineWidth);
  }

  /* Creates (on first call) and updates a feature marker: a vertical line plus 2 text labels.
     opts fields:
       textLabel       text for the upper <text> (the marker name)
       measText        text for the lower <text> (typically the keV value)
       textY           y-coord of the upper text (lower text is `textY + linehspace`)
       textOnUpdate    if true, re-sets textLabel on every update (e.g. the Compton peak's angle can change)
       textXOffset     x-offset from `pix` for the upper text; default xmax/200
       measXOffset     x-offset from `pix` for the lower text; default xmax/125 */
  function ensureLineMarker(keyPrefix, pix, opts) {
    var textKey = keyPrefix + 'Text';
    var measKey = keyPrefix + 'Meas';
    var textXOffset = (opts.textXOffset !== undefined) ? opts.textXOffset : xmax/200;
    var measXOffset = (opts.measXOffset !== undefined) ? opts.measXOffset : xmax/125;

    if (!self[keyPrefix]) {
      self[keyPrefix] = self.vis.append("line")
        .attr("class", "peakLine")
        .attr("stroke-width", self.options.featureLineWidth);
      self[textKey] = self.vis.append("text").attr("class", "peakText");
      self[measKey] = self.vis.append("text").attr("class", "peakText");
      if (!opts.textOnUpdate) self[textKey].text(opts.textLabel);
    }

    self[keyPrefix]
      .attr("x1", pix).attr("x2", pix)
      .attr("y1", 0).attr("y2", self.size.height);
    self[textKey]
      .attr("x", pix + textXOffset)
      .attr("y", opts.textY);
    if (opts.textOnUpdate) self[textKey].text(opts.textLabel);
    self[measKey]
      .attr("x", pix + measXOffset)
      .attr("y", opts.textY + linehspace)
      .text(opts.measText);
  }

  function updateComptonPeaks() {
    var compAngleRad = self.options.comptonPeakAngle * (3.14159265/180.0);
    var comptonPeakEnergy = energy / (1 + ((energy/510.99891)*(1-Math.cos(compAngleRad))));
    var comptonPeakPix = self.xScale(comptonPeakEnergy);

    if (shouldDeleteMouseEdge())
      deleteMouseEdge(true);

    var comptonPeakOutOfBounds = comptonPeakPix < 0 || comptonPeakPix > xmax;

    if( !self.options.showComptonPeaks || cursorIsOutOfBounds || comptonPeakOutOfBounds || self.dragging_plot ) {
      deleteComptonPeakLine();
      updateMouseEdge();
      return;
    }

    ensureLineMarker('comptonPeak', comptonPeakPix, {
      textLabel: self.options.txt.comptonPeakAngle.replace("{1}", String(self.options.comptonPeakAngle)),
      measText: comptonPeakEnergy.toFixed(1) + " keV",
      textY: self.size.height/10,
      textOnUpdate: true
    });

    updateMouseEdge();
  }

  function updateComptonEdge() {
    var compedge = energy - (energy / (1 + (2*(energy/510.99891))));
    var compEdgePix = self.xScale(compedge);

    if ( shouldDeleteMouseEdge() )
      deleteMouseEdge(true);

    var comptonEdgeOutOfBounds = compEdgePix < 0  || compEdgePix > xmax ;

    if( !self.options.showComptonEdge || cursorIsOutOfBounds || comptonEdgeOutOfBounds || self.dragging_plot ) {
      self.deleteAndNullifyElements( 'comptonEdge', 'comptonEdgeText', 'comptonEdgeMeas' );
      updateMouseEdge();
      return;
    }

    ensureLineMarker('comptonEdge', compEdgePix, {
      textLabel: self.options.txt.comptonEdge,
      measText: compedge.toFixed(1) + " keV",
      textY: self.size.height/22
    });

    updateMouseEdge();
  }

  function updateSumPeaks( clickedEnergy ) {
    if( shouldDeleteMouseEdge() )
      deleteMouseEdge(true);

    if( !self.options.showSumPeaks || cursorIsOutOfBounds || self.dragging_plot ) {
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
      // shouldUpdateSumPeak stays true, so we get past the early return below to show the help text
      shouldUpdateLeftSumPeak = shouldUpdateClickedSumPeak = false;
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
        self.sumPeakHelpText = self.vis.append("text")
            .attr("class", "peakText")
            .attr("fill", "red")
            .text( self.options.txt.firstEnergyClick );
      }
      
      self.sumPeakHelpText
          .attr( "x", m[0] + xmax/125 )
          .attr( "y", self.size.height/3.5)
    } else {
        if ( self.sumPeakHelpText ) {
          self.sumPeakHelpText.remove(); 
          self.sumPeakHelpText = null;
        }
    }

    if ( shouldUpdateLeftSumPeak && energy < clickedEnergy ) {
      var leftSumEnergy = clickedEnergy - energy,
          leftSumPix = self.xScale( leftSumEnergy  ),
          leftSumOutOfBounds = leftSumPix < 0 || leftSumPix > xmax;

      ensureLineMarker('leftSumPeak', leftSumPix, {
        textLabel: self.options.txt.clickedPeak,
        measText: energy.toFixed(1) + "+" + leftSumEnergy.toFixed(1) + "=" + clickedEnergy.toFixed(1) + " keV",
        textY: self.size.height/3.4,
        textXOffset: xmax/125
      });

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

      ensureLineMarker('sumPeak', sumPix, {
        textLabel: self.options.txt.sumPeak,
        measText: clickedEnergy.toFixed(1) + "+" + energy.toFixed(1) + "=" + sumEnergy.toFixed(1) + " keV",
        textY: self.size.height/4,
        textXOffset: xmax/125
      });
    }
  }

  updateEscapePeaks();
  updateComptonPeaks();
  updateComptonEdge();
  updateSumPeaks( mouseDownEnergy );
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
    self.searchEnergyWindows.sort( function(l,r){ return l.energy - r.energy; }  )
  }

  self.redraw()();
}

// Sets regions to draw in a solid fill, e.g. [{lowerEnergy: 90, upperEnergy: 112, fill: 'rgba(23,53,12,0.1)', hash: 123112319}, ...]
SpectrumChartD3.prototype.setHighlightRegions = function(ranges) {
  var self = this;

  if( !Array.isArray(ranges) || ranges.length===0 ){
    self.highlightRegions = null;
    self.vis.selectAll("g.highlight").remove(); // drawHighlightRegions() won't, with no regions
  } else {
    // TODO: validate the regions
    self.highlightRegions = ranges;
    self.highlightRegions.sort( function(l,r){ return l.lowerEnergy - r.lowerEnergy; }  )
  }
  
  self.redraw()();
}

/** -------------- Chart Animation (zoom animations, via requestAnimationFrame) -------------- */
SpectrumChartD3.prototype.redrawZoomXAnimation = function(targetDomain) {
  var self = this;

  if( !self.options.showAnimation )
    return;

  return function() {
    if( self.currentDomain === null || targetDomain === null
        || (self.currentDomain[0] == targetDomain[0] && self.currentDomain[1] == targetDomain[1]) ) {
      self.handleCancelAnimationZoom();
      return;
    }

    var animationFractionTimeElapsed = Math.min( Math.max((Math.floor(Date.now()) - self.startAnimationZoomTime) / self.options.animationDuration, 0), 1 );

    if( animationFractionTimeElapsed >= 0.999 ){
      self.handleCancelAnimationZoom();
      self.setXAxisRange( targetDomain[0], targetDomain[1], true, true );  //do emit range change
      self.redraw()();
      self.updateFeatureMarkers(-1);
      return;
    }
    
    self.setXAxisRange(
      Math.min( self.savedDomain[0] + (animationFractionTimeElapsed * (targetDomain[0] - self.savedDomain[0])), targetDomain[0] ),
      Math.max( self.savedDomain[1] - (animationFractionTimeElapsed * (self.savedDomain[1] - targetDomain[1])), targetDomain[1] ),
      false /* dont emit x-range change. */,
      true
    );
    self.currentDomain = self.xScale.domain();

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
    const now = Math.floor( Date.now() );
    const fractionTimeElapsed = Math.min( Math.max((now - self.startAnimationZoomTime) / self.options.animationDuration, 0), 1 );
    
    if( ( fractionTimeElapsed >= 0.999 )
        || (self.currentDomain == null)
        || (roundTo3Dec(self.currentDomain[0]) == roundTo3Dec(targetDomain[0])
            && roundTo3Dec(self.currentDomain[1]) == roundTo3Dec(targetDomain[1]))) {
      self.handleCancelAnimationZoom();
      self.yScale.domain( targetDomain );
      redraw();
      return;
    }// we're done here

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
    this.yScale.domain( yrange ? yrange : this.getYAxisDomain() );
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

  if (self.zoomAnimationID != null) {
    cancelAnimationFrame(self.zoomAnimationID);
  }

  self.zoomAnimationID = null;
  self.currentDomain = null;
  self.savedDomain = null;
  self.startAnimationZoomTime = null;
}

/** -------------- X-axis Zoom -------------- */
SpectrumChartD3.prototype.handleMouseMoveZoomX = function () {
  var self = this;

  var zoomInXBox = self.vis.select("#zoomInXBox"),
      zoomInXText = self.vis.select("#zoomInXText");

  if( !self.leftMouseDown )
    return;

  self._clampMouseXToVis();
  self.zooming_plot = true;

  console.assert( self.lastMouseMovePos.length === 4, "self.lastMouseMovePos should have 4 elements: ", self.lastMouseMovePos );
  console.assert( self.leftMouseDown.length === 4, "self.leftMouseDown should have 4 elements: ", self.leftMouseDown );
  
  /* Left of the starting position zooms out.  This uses svg coordinates (element 2), not vis
     coordinates (element 0), since the y-axis width can change while zooming out, which made the vis
     positions swing, toggling between zooming in and out. */
  if( self.lastMouseMovePos[2] < self.leftMouseDown[2] ) {
    self.handleCancelAnimationZoom();

    zoomInXText.remove();
    zoomInXBox.remove();
    self.zoominaltereddomain = true;

    var bounds = self.min_max_x_values();
    var xaxismin = bounds[0],
        xaxismax = bounds[1];
        
    var frac = 4 * (self.leftMouseDown[2] - self.lastMouseMovePos[2]) / self.leftMouseDown[0];

    if( !isFinite(frac) || isNaN(frac) || frac < 0.0 )
      frac = 0;

    var origdx = self.origdomain[1] - self.origdomain[0],
        maxdx = xaxismax - xaxismin,
        newdx = origdx + frac*(maxdx - origdx);

    const deltadx = newdx - origdx;
    const clamped = self._clampXRangeToData( self.origdomain[0] - 0.5*deltadx,
                                             self.origdomain[1] + 0.5*deltadx,
                                             xaxismin, xaxismax );

    // TODO: periodically emit xrangechanged while zooming out (now only on mouse up)
    self.setXAxisRange(clamped[0],clamped[1],false,true);
    self.redraw()();
  } else {
    if (zoomInXBox.empty()) {
      zoomInXBox = self.vis.append("rect")
      .attr("id", "zoomInXBox")
      .attr("class","leftbuttonzoombox")
      .attr("y", 0)
      .attr("pointer-events", "none");
    }
    
    if( self.zoominaltereddomain ) {
      // Went from zooming out to zooming in: restore the original domain
      self.setXAxisRange( self.origdomain[0], self.origdomain[1], true, true );
      self.zoominaltereddomain = false;
      self.redraw()();
    }

    zoomInXBox
      .attr("x", self.leftMouseDown[0])
      .attr("height", self.size.height)
      .attr("width", self.lastMouseMovePos[0] - self.leftMouseDown[0] );

    // "Zoom In" text once the box is wide enough to zoom
    if (self.lastMouseMovePos[0] - self.leftMouseDown[0] > 7) {
      if( zoomInXText.empty() ){
        zoomInXText = self.vis.append("text")
          .attr("id", "zoomInXText")
          .attr("class", "chartLineText")
          .attr("y", Number(zoomInXBox.attr("height"))/2)
          .text( self.options.txt.zoomIn );
      }

      const bb = zoomInXText.node().getBoundingClientRect();
      zoomInXText.attr("x", (0.5*(self.leftMouseDown[0] + self.lastMouseMovePos[0])) - 0.5*bb.width);
    }else if( !zoomInXText.empty() ){
      zoomInXText.remove();
    }//if( draw "Zoom In" text ) / else
  }//if( zoom out ) / else ( zoom in )
}//SpectrumChartD3.prototype.handleMouseMoveZoomX

SpectrumChartD3.prototype.handleMouseUpZoomX = function () {
  var self = this;

  if (!self.hasSpectrumData() || !self.zooming_plot)
    return;

  if( self.zooming_plot && self.lastMouseMovePos ) {
    const foreground = self.rawData.spectra[0];

    let m = self.getMousePos();

    console.assert( m.length === 4, "Expected getMousePos to return array with length 4.", m );
    console.assert( self.leftMouseDown.length === 4, "Expected leftMouseDown to be array length 4,", self.leftMouseDown );
    
    if( m[2] < self.leftMouseDown[2] ) {
      // Zoomed out — x-axis limits are already where we want them.
    } else {
      // Assumes the y-axis area width and x-domain are unchanged since drag start.
      m[0] = m[0] < 0 ? 0 : m[0];

      var oldXScale = self.xScale.domain();

      // From the mousedown position to the current one
      var x0 = self.xScale.invert(self.leftMouseDown[0]),
          x1 = self.xScale.invert(m[0]);

      // Require the mouse to have moved more than 7 pixels
      if( m[0] - self.leftMouseDown[0] > 7 && self.rawData ) {
        var bounds = self.min_max_x_values();
        var mindatax = bounds[0], 
            maxdatax = bounds[1];
        
        // A sub-bin selection shows exactly that bin, inset 1% on each edge
        var rawbi = self._bisecRaw;
        var lbin = rawbi.left(foreground.x, x0+(x1-x0));
        if( lbin > 1 && lbin < (foreground.x.length-1) && lbin === rawbi.left(foreground.x,x1+(x1-x0)) ) {
          var corx0 = foreground.x[lbin-1];
          var corx1 = foreground.x[lbin];
          var p = 0.01*(corx1-corx0);
          corx0 += p;
          corx1 -= p;

          x0 = Math.max(corx0, mindatax);
          x1 = Math.min(corx1, maxdatax);
        }
        
        if( self.options.showAnimation ) {
          self.handleCancelAnimationZoom();

          // 'xrangechanged' is emitted when the animation completes
          self.currentDomain = self.savedDomain = oldXScale;
          self.zoomAnimationID = requestAnimationFrame(self.redrawZoomXAnimation([x0,x1]));
          self.startAnimationZoomTime = Math.floor(Date.now());
        } else {
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
  this.zooming_plot = false;
  this.vis.select("#zoomInXBox").remove();
  this.vis.select("#zoomInXText").remove();
}//SpectrumChartD3.prototype.handleCancelMouseZoomInX

/** -------------- Y-axis Zoom -------------- */
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

    self.drawTemplates();
    self.update();
  }
}

// See also setYAxisRangeAnimated([min,max])
SpectrumChartD3.prototype.setYAxisRange = function( minimum, maximum ) {
  this.yScale.domain([maximum,minimum]);
  this.redrawYAxis()();
}

SpectrumChartD3.prototype.setYAxisMinimum = function( minimum ) {
  this.setYAxisRange( minimum, this.yScale.domain()[0] );
}

SpectrumChartD3.prototype.setYAxisMaximum = function( maximum ) {
  this.setYAxisRange( this.yScale.domain()[1], maximum );
}

SpectrumChartD3.prototype.handleMouseMoveZoomY = function () {
  var self = this;

  var zoomInYBox = self.vis.select("#zoomInYBox"),
      zoomInYText = self.vis.select("#zoomInYText");

  self.zoomingYPlot = true;

  // Start of the zoom box (clamped, in case the drag started out of bounds)
  if( !self.zoomInYMouse )
    self.zoomInYMouse = self.lastMouseMovePos;

  if (self.zoomInYMouse[1] < 0)
    self.zoomInYMouse[1] = 0;
  else if (self.zoomInYMouse[1] > self.size.height)
    self.zoomInYMouse[1] = self.size.height;

  if (self.zoomInYMouse[0] < 0 || self.zoomInYMouse[0] > self.size.width) {
    self.handleCancelMouseZoomY();
    return;
  }

  var height = self.lastMouseMovePos[1] - self.zoomInYMouse[1];

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
    zoomInYBox.attr("y", Math.min(self.zoomInYMouse[1], self.lastMouseMovePos[1]))
      .attr("height", Math.abs(height));
  }

  if (Math.abs(self.lastMouseMovePos[1] - self.zoomInYMouse[1]) > 10) {   // text once the box is tall enough to zoom
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

    const textBB = zoomInYText.node().getBoundingClientRect();
    zoomInYText.attr("y", Number(zoomInYBox.attr("y")) + (Number(zoomInYBox.attr("height"))/2) + 0.5*textBB.height );
    zoomInYText.attr("x", 0.5*self.size.width - 0.5*textBB.width );
  } else if (!zoomInYText.empty()) {
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
    // Zoom in
    self.setYAxisRangeAnimated( [start_counts, end_counts] );
  } else {
    // Zoom out: fully, or by x2 or x4 (depending how far the user dragged)
    if (zoomInYText.text().endsWith("full")) {
      self.setYAxisRangeAnimated( self.getYAxisDomain() );
    } else {
      const mult = (zoomInYText.text().endsWith("x2") ? 2 : 4);

      // Current y-domain is [max,min]; expand about its center, within the full auto range
      const domain = self.yScale.domain();
      const oldRange = Math.abs(domain[0] - domain[1]);
      const centroid = domain[1] + 0.5*oldRange;

      const ydomain = self.getYAxisDomain();
      const minY = ydomain[1], maxY = ydomain[0];

      let newY0 = centroid - 0.5*mult*oldRange;
      let newY1 = newY0 + mult*oldRange;

      if( newY0 < minY )
        newY1 += (minY - newY0);
      if( newY1 > maxY )
        newY0 -= (newY1 - maxY);

      self.setYAxisRangeAnimated( [Math.min(newY1, maxY), Math.max(newY0, minY)] );
    }//if( zoom all the way out ) / else (zoom to partial y-range )
  }//if( zooming in ) / else ( zoom out )

  self.handleCancelMouseZoomY();
}//SpectrumChartD3.prototype.handleMouseUpZoomY

SpectrumChartD3.prototype.handleCancelMouseZoomY = function() {
  this.vis.select("#zoomInYBox").remove();
  this.vis.select("#zoomInYText").remove();
  this.zoomingYPlot = false;
  this.zoomInYMouse = null;
}//SpectrumChartD3.prototype.handleCancelMouseZoomY

SpectrumChartD3.prototype.handleTouchMoveZoomInX = function() {
  const self = this;

  if (!self.touchesOnChart)
    return;

  var t = d3.touches(self.vis[0][0]);
  var keys = Object.keys(self.touchesOnChart);

  if( (keys.length !== 2) || (t.length !== 2) || !self.touchZoomStartEnergies )
    return;

  var x1 = t[0][0],  x2 = t[1][0];
  if( x1 > x2 )
    x2 = [x1, x1 = x2][0];

  var cur_e1 = self.xScale.invert(x1);
  var cur_e2 = self.xScale.invert(x2);
  
  // Adjust the range so the fingers stay on their original energies: with energy = a + b*x_px,
  //  b is the current keV per pixel, and we solve for the new a and b.
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

  // Don't zoom in too far, or past the data range
  var minx, maxx, bounds, foreground;
  if( !self.hasSpectrumData() ){
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

    // Span at least ~3 channels
    if( xrange < 4*avrg_channel_width ){
      var rawbi = self._bisecRaw;
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
  }
}//handleTouchMoveZoomInX

SpectrumChartD3.prototype.handleTouchMoveZoomY = function() {
  const self = this;
  if( !this.touchesOnChart )
    return;

  const t = d3.touches(this.vis[0][0]);
  const keys = Object.keys(this.touchesOnChart);

  if( (keys.length !== 2) || (t.length !== 2) )
    return;

  let zoomInYTopLine = this.vis.select("#zoomInYTopLine");
  let zoomInYBottomLine = this.vis.select("#zoomInYBottomLine");
  let zoomInYText = this.vis.select("#zoomInYText");

  /* Guide lines follow the fingers (vis coords); touch-end reads the zoom range back from them. */
  const topY = Math.min( t[0][1], t[1][1] );
  const bottomY = Math.max( t[0][1], t[1][1] );

  /* Spread apart zooms in, pinch together zooms out.  Start positions are page coords and d3.touches
     vis coords, but separations compare directly. */
  const touch1 = this.touchesOnChart[keys[0]];
  const touch2 = this.touchesOnChart[keys[1]];
  const startSep = Math.abs( touch1.startY - touch2.startY );
  const nowSep = bottomY - topY;

  if( zoomInYTopLine.empty() ) {
    zoomInYTopLine = this.vis.append("line")
      .attr("id", "zoomInYTopLine")
      .attr("class", "mouseLine")
      .attr("x1", 0)
      .attr("x2", this.size.width);
  }
  zoomInYTopLine.attr("y1", topY).attr("y2", topY);

  if( zoomInYBottomLine.empty() ) {
    zoomInYBottomLine = this.vis.append("line")
      .attr("id", "zoomInYBottomLine")
      .attr("class", "mouseLine")
      .attr("x1", 0)
      .attr("x2", this.size.width);
  }
  zoomInYBottomLine.attr("y1", bottomY).attr("y2", bottomY);

  if( zoomInYText.empty() ) {
    zoomInYText = this.vis.append("text")
      .attr("id", "zoomInYText")
      .attr("class", "mouseLineText");
  }

  /* +-10 px deadband (the pinch recognizer requires a >20 px change, so the text is never blank) */
  zoomInYText.text( (nowSep > startSep + 10) ? self.options.txt.zoomInY
                    : ((nowSep < startSep - 10) ? self.options.txt.zoomOutY : "") );

  const textBB = zoomInYText.node().getBoundingClientRect();
  zoomInYText.attr("x", 0.5*this.size.width - 0.5*textBB.width );
  zoomInYText.attr("y", 0.5*(topY + bottomY + textBB.height) );
}//SpectrumChartD3.prototype.handleTouchMoveZoomY

SpectrumChartD3.prototype.handleTouchEndZoomY = function() {
  const self = this;
  const zoomInYTopLine = this.vis.select("#zoomInYTopLine");
  const zoomInYBottomLine = this.vis.select("#zoomInYBottomLine");
  const zoomInYText = this.vis.select("#zoomInYText");

  if( zoomInYTopLine.empty() || zoomInYBottomLine.empty() ){
    this.handleTouchCancelZoomY();
    return;
  }

  const ypix1 = Number(zoomInYTopLine.attr("y1"));
  const ypix2 = Number(zoomInYBottomLine.attr("y1"));

  const y1 = this.yScale.invert(ypix1);
  const y2 = this.yScale.invert(ypix2);

  this.handleTouchCancelZoomY();

  if( Math.abs(ypix2 - ypix1) > 10 ) {
    if (zoomInYText.text() == self.options.txt.zoomInY ) {
      this.setYAxisRangeAnimated( [y1,y2] );
    }else if( zoomInYText.text() == self.options.txt.zoomOutY ) {
      this.setYAxisRangeAnimated( this.getYAxisDomain() );
    }
  }
}//SpectrumChartD3.prototype.handleTouchEndZoomY

SpectrumChartD3.prototype.handleTouchCancelZoomY = function() {
  this.vis.select("#zoomInYTopLine").remove();
  this.vis.select("#zoomInYBottomLine").remove();
  this.vis.select("#zoomInYText").remove();
}

/** -------------- Energy Recalibration (Alt+Ctrl drag) -------------- */
SpectrumChartD3.prototype.handleMouseMoveRecalibration = function() {
  var self = this;

  if (!self.leftMouseDown)
    return;
  if (!self.hasSpectrumData())
    return;

  self._clampMouseXToVis();

  var recalibrationStartLine = self.vis.select("#recalibrationStartLine"),
      recalibrationText = self.vis.select("#recalibrationText"),
      recalibrationMousePosLines = self.vis.select("#recalibrationMousePosLines");

  var recalibrationG = self.vis.select("#recalibrationG");
  var recalibrationPeakVis = self.vis.select("#recalibrationPeakVis");

  // Line where the drag started
  if (recalibrationStartLine.empty()) {
    recalibrationStartLine = self.vis.append("line")
      .attr("id", "recalibrationStartLine")
      .attr("class", "mouseLine")
      .attr("x1", self.leftMouseDown[0])
      .attr("x2", self.leftMouseDown[0])
      .attr("y1", 0)
      .attr("y2", self.size.height);
  }

  const start = Number( recalibrationStartLine.attr("x1") );
  const now = self.lastMouseMovePos[0];

  const txt = self.options.txt.recalFromTo.replace("{1}",String(self.xScale.invert(start).toFixed(2))).replace("{2}",self.xScale.invert(now).toFixed(2) );
  
  if (recalibrationText.empty()) {
    recalibrationText = self.vis.append("text")
      .attr("id", "recalibrationText")
      .attr("class", "mouseLineText")
      .attr("x", self.leftMouseDown[0] + 5 )
      .attr("y", 15 );
  }

  recalibrationText.text( txt )

  // Line at the mouse position
  if (recalibrationMousePosLines.empty())
    recalibrationMousePosLines = self.vis.append("line")
      .attr("id", "recalibrationMousePosLines")
      .attr("y1", 0)
      .attr("y2", self.size.height)
      .attr("class", "recalibrationMousePosLines")
      .style("opacity", 0.75)
      .attr("stroke-width",0.5);

  recalibrationMousePosLines.attr("x1", self.lastMouseMovePos[0])
    .attr("x2", self.lastMouseMovePos[0]);

  // Copies of the spectra and peaks, shifted with the mouse
  if (recalibrationG.empty()) {
    recalibrationG = self.vis.append("g")
      .attr("id", "recalibrationG")
      .attr("clip-path", "url(#clip" + this.chart.id + ")");
  }

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
      .attr("clip-path", "url(#clip" + this.chart.id + ")");

    self.peakVis.selectAll("path").each(function() {
      const path = d3.select(this);
      recalibrationPeakVis.append("path")
        .attr("class", path.attr("class"))
        .attr("d", path.attr("d"))
        .attr("fill-opacity", 0.4)
        .style("fill", path.style("fill"))
        ;
    });
  }

  recalibrationPeakVis.attr("transform", "translate(" + (self.lastMouseMovePos[0] - self.leftMouseDown[0]) + ",0)");

  for (var i = 0; i < ((self.rawData && self.rawData.spectra) ? self.rawData.spectra.length : 0); ++i) {
    var recalibrationLine = self.vis.select("#recalibrationLine"+i);

    if (!recalibrationLine.empty())
      recalibrationLine.attr("transform", "translate(" + (self.lastMouseMovePos[0] - self.leftMouseDown[0]) + ",0)");
  }
}

SpectrumChartD3.prototype.handleMouseUpRecalibration = function() {
  var self = this;

  if (self.leftMouseDown) {
    if (self.leftDragMode === 'recalibrate') {
      self.WtEmit(self.chart.id, {name: 'rightmousedragged'}, self.recalibrationStartEnergy, self.xScale.invert(self.lastMouseMovePos[0]));
    }

    self.handleCancelMouseRecalibration();
  }

  if (self.leftDragMode === 'recalibrate')
    self.leftMouseDown = null;
}

SpectrumChartD3.prototype.handleCancelMouseRecalibration = function() {
  var self = this;

  var recalibrationStartLine = self.vis.select("#recalibrationStartLine"),
      recalibrationText = self.vis.select("#recalibrationText"),
      recalibrationMousePosLines = self.vis.select("#recalibrationMousePosLines");

  var recalibrationG = self.vis.select("#recalibrationG");
  var recalibrationPeakVis = self.vis.select("#recalibrationPeakVis");

  recalibrationStartLine.remove();
  recalibrationText.remove()
  recalibrationPeakVis.remove();
  recalibrationMousePosLines.remove();
  recalibrationG.remove();
}

/** -------------- Delete Peaks (Shift drag) -------------- */
SpectrumChartD3.prototype.handleMouseMoveDeletePeak = function() {
  var self = this;

  SpectrumChartD3.stopEvent();

  if (!self.leftMouseDown)
    return;

  self._clampMouseXToVis();

  const left = self.leftMouseDown[0], right = self.lastMouseMovePos[0];
  self._drawDeletePeakBox( Math.min(left,right), Math.abs(left-right), false, false );
}

/* Returns [minEnergy, maxEnergy] spanned by a drag box (read from its x / width attrs). */
SpectrumChartD3.prototype._boxEnergyRange = function( boxSel ){
  const x = Number( boxSel.attr("x") );
  const w = Number( boxSel.attr("width") );
  const e0 = this.xScale.invert( x ), e1 = this.xScale.invert( x + w );
  return [ Math.min(e0,e1), Math.max(e0,e1) ];
};

/* Clamp the cached mouse x-position to the plot's x-range [0, size.width]. */
SpectrumChartD3.prototype._clampMouseXToVis = function(){
  if( this.lastMouseMovePos[0] < 0 )
    this.lastMouseMovePos[0] = 0;
  else if( this.lastMouseMovePos[0] > this.size.width )
    this.lastMouseMovePos[0] = this.size.width;
};

SpectrumChartD3.prototype.processDeletePeakRange = function() {
  var self = this;

  try {
    // attr access can (seldom) throw; harmless, just skip the emit.
    const r = self._boxEnergyRange( self.vis.select("#deletePeaksBox") );
    self.WtEmit(self.chart.id, {name: 'shiftkeydragged'}, r[0], r[1]);
    return true;
  } catch (e) {
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

SpectrumChartD3.prototype.cancelDeletePeak = function() {
  var self = this;

  var deletePeaksBox = self.vis.select("#deletePeaksBox"),
      deletePeaksText = self.vis.select("#deletePeaksText");

  deletePeaksBox.remove();
  deletePeaksText.remove();
};

SpectrumChartD3.prototype.handleCancelMouseDeletePeak = function() {
  this.cancelDeletePeak();
}

/* Draws/updates the delete-peaks range box + label, for mouse and touch.  centerLabel: center the label
   on the box (touch), vs a fixed -40px offset (mouse); blankWhenEmpty: no label for a zero-width box. */
SpectrumChartD3.prototype._drawDeletePeakBox = function( boxX, boxWidth, centerLabel, blankWhenEmpty ){
  const self = this;
  let box = self.vis.select("#deletePeaksBox"),
      text = self.vis.select("#deletePeaksText");

  if( box.empty() ){
    box = self.vis.append("rect")
      .attr("id", "deletePeaksBox")
      .attr("class", "deletePeaksBox")
      .attr("height", self.size.height)
      .attr("y", 0);
    text = self.vis.append("text")
      .attr("id", "deletePeaksText")
      .attr("class", "deletePeaksText")
      .attr("y", Number(box.attr("height"))/2);
  }

  box.attr("x", boxX).attr("width", boxWidth);

  text.text( (blankWhenEmpty && !(boxWidth > 0)) ? "" : self.options.txt.eraseInRange );

  const labelOffset = centerLabel ? (0.5*text.node().getBoundingClientRect().width) : 40;
  text.attr("x", Number(box.attr("x")) + (Number(box.attr("width"))/2) - labelOffset );
};

SpectrumChartD3.prototype.handleTouchMoveDeletePeak = function(t) {
  const self = this;

  if (t.length !== 2) {
    self.handleCancelTouchDeletePeak();
    return;
  }

  var leftTouch = t[0][0] < t[1][0] ? t[0] : t[1],
      rightTouch = leftTouch === t[0] ? t[1] : t[0];

  const leftTouchX = Math.min(leftTouch[0], self.xScale.range()[1]);
  const rightTouchX = Math.min(rightTouch[0], self.xScale.range()[1]);

  self._drawDeletePeakBox( leftTouchX, Math.abs(rightTouchX - leftTouchX), true, true );
}

SpectrumChartD3.prototype.handleTouchEndDeletePeak = function() {
  return this.handleMouseUpDeletePeak();
}

SpectrumChartD3.prototype.handleCancelTouchDeletePeak = function() {
  this.cancelDeletePeak();
}

/** -------------- Count Gammas (Alt+Shift drag) -------------- */
SpectrumChartD3.prototype.gammaIntegral = function(spectrum, lowerX, upperX) {
  let self = this;
  var sum = 0.0;

  // Need at least two channel edges for a bin width
  if( !spectrum || !spectrum.x || !spectrum.y || (spectrum.x.length < 2) )
    return sum;
  
  var bounds = self.min_max_x_values();
  var maxX = bounds[1];
  var minX = bounds[0];
  
  lowerX = Math.min( maxX, Math.max(lowerX, minX) );
  upperX = Math.max( minX, Math.min(upperX, maxX) );
  
  if (lowerX == upperX)
    return sum;
  
  if (lowerX > upperX) {
    upperX = [lowerX, lowerX = upperX][0];
  }
  
  var maxChannel = spectrum.x.length - 1;
  var lowerChannel = self._bisecRaw.left(spectrum.x,lowerX,1) - 1;
  var upperChannel = self._bisecRaw.left(spectrum.x,upperX,1) - 1;
  
  var lowerLowEdge = spectrum.x[lowerChannel];
  var lowerBinWidth = lowerChannel < maxChannel ? spectrum.x[lowerChannel+1] - spectrum.x[lowerChannel]
                                                : spectrum.x[lowerChannel] - spectrum.x[lowerChannel-1];
  var lowerUpEdge = lowerLowEdge + lowerBinWidth;
  
  if (lowerChannel === upperChannel) {
    var frac = (upperX - lowerX) / lowerBinWidth;
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
  return this.handleMouseUpCountGammas();
}

/** Updates the count-gammas box and sums as the mouse (Alt+Shift drag) or two vertically stacked fingers move. */
SpectrumChartD3.prototype.updateGammaSum = function() {
  var self = this

  const pos = self.getMousePos();
  if( !pos ){
    return;
  }
  
  if( !self.hasSpectrumData() )
    return;
  
  let isMouseEvent = true;
  if( d3.event ){
    SpectrumChartD3.stopEvent();
    const t = d3.event.changedTouches;
    const vt = t ? d3.touches(this.vis[0][0], t) : null;
    if( vt )
    isMouseEvent = false;
  }else if( !self.lastMouseMovePos && this.lastTapEvent ){
    isMouseEvent = false;
  }
  
  let startx_px, nowx_px;
  if( isMouseEvent ){
    if (!self.leftMouseDown || !self.lastMouseMovePos )
    return;
    
    startx_px = self.leftMouseDown[0];
    nowx_px = self.lastMouseMovePos[0];
    if( startx_px > nowx_px )
      [startx_px, nowx_px] = [nowx_px, startx_px];
  }else{
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

  if (countGammasBox.empty()) {
    countGammasBox = self.vis.append("rect")
    .attr("id", "countGammasBox")
    .attr("width", Math.abs( startx_px - nowx_px ))
    .attr("height", self.size.height)
    .attr("y", 0);
  } else {
    countGammasBox.attr("width", Math.abs( startx_px - nowx_px ));
  }
  
  if (countGammasText.empty()) {
    countGammasText = self.vis.append("text")
    .attr("id", "countGammasText")
    .attr("class", "countGammasText")
    .attr("y", Number(countGammasBox.attr("height"))/2)
    .text( self.options.txt.gammaCounts );
  }
  
  var ypos = Number(countGammasBox.attr("height"))/2 + 15;   // y-position of the next text line
  
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
  
  countGammasText.attr("x", Number(countGammasBox.attr("x")) + (Number(countGammasBox.attr("width"))/2) - 30 );
  
  // Counts for each spectrum
  var nforeground, nbackground, backSF;
  var nsigma = 0, isneg;
  var asterickText = "";
  var rightPadding = 50;
  var specialScaleSpectras = [];
  self.rawData.spectra.forEach(function(spectrum, i) {
    if (!spectrum)
    return;
    
    var spectrumSelector = 'Spectrum-' + spectrum.id;
    var spectrumCountsText = self.vis.select("#" + spectrumSelector + "CountsText");
    var spectrumScaleFactor = spectrum.yScaleFactor;
    var nspectrum = self.gammaIntegral(spectrum, sumEnergyRange[0], sumEnergyRange[1]);
    var spectrumGammaCount = Number((spectrumScaleFactor * nspectrum).toFixed(2));
    var countsText;
    
    // Foreground and background counts, for the sigma comparison
    if (spectrum.type === self.spectrumTypes.FOREGROUND)
    nforeground = nspectrum;
    else if (spectrum.type === self.spectrumTypes.BACKGROUND) {
      nbackground = nspectrum;
      backSF = spectrumScaleFactor;
    }
    
    const title = spectrum.title ? spectrum.title : ("Spectrum " + (i+1));
    if (spectrumScaleFactor != null && spectrumScaleFactor !== -1)
      countsText = title + ": " + spectrumGammaCount.toFixed(2);
    // No "scaled by" note for a missing or sentinel (-1) scale factor
    if ((typeof spectrumScaleFactor === "number") && (spectrumScaleFactor !== -1) && (spectrumScaleFactor !== 1)) {
      asterickText += "*";
      if (countsText)
      countsText += asterickText;
      specialScaleSpectras.push( asterickText + self.options.txt.afterScalingBy + spectrumScaleFactor.toFixed(3) );
    }
    
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

  // How many sigma the foreground is from the (scaled) background
  if (nforeground && nbackground && backSF) {
    const backSigma = backSF * Math.sqrt(nbackground);
    const sigma = Math.sqrt( backSigma*backSigma + nforeground );  // the foreground is never scaled
    nsigma = backSigma == 0 ? 0 : (Number((Math.abs(nforeground - backSF*nbackground) / sigma).toFixed(3)));
    isneg = ((backSF*nbackground) > nforeground);
  }
  
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

  // The asterisk notes for scaled spectra
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
  
  // Hide the text while the box is empty
  self.vis.selectAll(".countGammasText").attr("fill-opacity", (Number(countGammasBox.attr("width")) == 0) ? 0 : 1);
}//SpectrumChartD3.prototype.updateGammaSum

SpectrumChartD3.prototype.handleMouseUpCountGammas = function() {
  var self = this;

  try {
    // attr access can (seldom) throw; harmless, just skip the emit.
    const r = self._boxEnergyRange( self.vis.select("#countGammasBox") );
    self.WtEmit(self.chart.id, {name: 'shiftaltkeydragged'}, r[0], r[1]);
  } catch (e) {
    return;
  }

  self.handleCancelMouseCountGammas();
}

SpectrumChartD3.prototype.handleCancelMouseCountGammas = function() {
  var self = this;

  var countGammasBox = self.vis.select("#countGammasBox");
  countGammasBox.remove();
  self.vis.selectAll(".countGammasText").remove();
}

SpectrumChartD3.prototype.handleCancelTouchCountGammas = function() {
  this.handleCancelMouseCountGammas();
}

/** -------------- Peak Info and Highlighting -------------- */
SpectrumChartD3.prototype.handleMouseOutPeak = function(peakElem, highlightedPeak) {
  /* Returns true if a node is a descendant (or is) of a parent node. */
  function isElementDescendantOf(parent, node) {
    while (node != null) {
      if (node == parent) return true;
      node = node.parentNode;
    }
    return false;
  }
  
  if (this.peakInfo && isElementDescendantOf(this.peakInfo.node(), d3.event.relatedTarget)) {
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

      const shouldMovePeakInfoLeft = x >= box.x && x <= box.x + box.width;

      self.peakInfo.attr("transform","translate(" 
        + (shouldMovePeakInfoLeft ? (self.padding.leftComputed + 115) : self.size.width) 
        + "," + (self.size.height - 40) + ")");
    }
  }
}

SpectrumChartD3.prototype.getPeakInfoObject = function(roi, energy, spectrumIndex) {
  var self = this;

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

  // nuclide: {name: "Eu152", decayParent: "Eu152", decayChild: "Sm152", energy: 1408.01}
  // xray: {name: "Uranium", energy: 114.8440 }
  // reaction: {name: "Fe(n,g)", energy: 4996.23, type: "D.E."}
  let nuc = null;
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
  
  // contArea is only shown when the C++ doesn't provide roiCounts (it has since 20201129)
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

SpectrumChartD3.prototype.updatePeakInfo = function( posOverride ) {
  const self = this;
  
  if (!this.hasSpectrumData())
    return;
  
  // posOverride: from deferred callers, which have no d3.event for getMousePos()
  const energy = this.xScale.invert( (posOverride ? posOverride : this.getMousePos())[0] );
  let resultROI, spectrumIndex;
  
  // Prefer a highlighted peak, else a ROI under the mouse
  if( this.highlightedPeak && this.peakPaths ){
    for( let i = 0; !resultROI && (i < this.peakPaths.length); ++i ){
      if( this.peakPaths[i].path[0][0] === this.highlightedPeak ){
        resultROI = this.peakPaths[i].roi;
        spectrumIndex = this.peakPaths[i].spectrumIndex;
      }
    }
  }//if( this.highlightedPeak && this.peakPaths )

  this.rawData.spectra.forEach(function(spectrum, i) {
    if( resultROI || !spectrum.peaks || !self._peaksDrawnFor(spectrum) )
      return;

    spectrum.peaks.forEach(function(peak, j) {
      if (!resultROI && peak.lowerEnergy <= energy && energy <= peak.upperEnergy) {
        resultROI = peak;
        spectrumIndex = i;
      }
    });
  });
  
  if( !resultROI ) {
    this.hidePeakInfo();
  }else{
    const info = this.getPeakInfoObject(resultROI, energy, spectrumIndex);
    let clientX;
    if( posOverride && self.svg && self.svg[0] && self.svg[0][0] ){
      const rect = self.svg[0][0].getBoundingClientRect();
      clientX = rect.left + self.padding.leftComputed + posOverride[0];
    }
    this.displayPeakInfo(info, clientX);
  }
}

SpectrumChartD3.prototype.displayPeakInfo = function(info, clientX) {
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

  // The cursor x decides which side the box goes on: on touch, clientX is in changedTouches, and
  //  deferred callers (with no d3.event) pass clientX.
  let x = clientX;
  const evt = d3.event;
  if( (typeof x !== 'number') && evt )
    x = (typeof evt.clientX === 'number') ? evt.clientX
        : ((evt.changedTouches && evt.changedTouches.length) ? evt.changedTouches[0].clientX : undefined);

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
    createPeakInfoText(text, self.options.txt.spectrum, self.rawData.spectra[info.spectrumIndex].title || ("Spectrum " + (info.spectrumIndex + 1)));
    
  const width = text.node().getBoundingClientRect().width + 10; // + 10 for right padding
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

/** Highlights a peak path (the `peakElem` node), and if `highlightLabelTo`, its label. */
SpectrumChartD3.prototype.highlightPeak = function( peakElem, highlightLabelTo ) {
  var self = this;

  if( self.zooming_plot || !peakElem || self.leftMouseDown || self.rightClickDown )
    return;

  if( self.highlightedPeak )
    self.unhighlightPeak(null);
    
  var peak = d3.select(peakElem);
  if( peak )
    peak.attr("stroke-width",2);
  
  self.highlightedPeak = peakElem;
  
  if( !highlightLabelTo )
    return;
  
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

/** Highlights the peak at the given energy, as if moused over. */
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

/* Highlights a peak's label: bolds it, and draws a line from the label to the peak.

 @param labelEl The label's <text> element.
 @param isFromPeakBeingHighlighted True when the peak is being moused over: the line then goes to the
        closer of the peak's top or bottom.  Otherwise (the label is moused over), the line goes to the
        middle of the peak, and the peak is highlighted too.
 */
SpectrumChartD3.prototype.highlightLabel = function( labelEl, isFromPeakBeingHighlighted ) {
  const self = this;
  
  if( self.highlightedLabel === labelEl )
    return;
    
  if( self.highlightedLabel )
    self.unHighlightLabel(true);
  
  if( self.dragging_plot )
    return;
  
  self.highlightedLabel = labelEl;
  
  let thislabel = d3.select(labelEl);
  thislabel.attr("class", "peaklabel peakLabelBold");
  
  // The line goes in peakVis, so map the (possibly rotated) label's corners into peakVis coordinates
  const svgCtm = thislabel.node().getCTM();
  const visCtm = self.peakVis.node().getCTM();
  
  const visCtmInv = visCtm.inverse();
  const toVis = function(x,y){ return self._txtToVisCoords( svgCtm, visCtmInv, x, y ); };
  
  const labelbbox = thislabel.node().getBBox(); // untranslated/unrotated
  
  const labelEndpoints = [ toVis( 0, 0 ), //bottom-left of text
    toVis( 0, -0.5*labelbbox.height ), //mid-height-left of text
    toVis( 0.5*labelbbox.width, 0 ), //bottom, middle-x of text
    toVis( labelbbox.width, 0 ), //bottom right of text
    toVis( labelbbox.width, -0.5*labelbbox.height ), //mid-height-right of text
    toVis( labelbbox.width, -labelbbox.height ), //top-right of text
    toVis( 0.5*labelbbox.width, -labelbbox.height ), //top middle-x of text
    toVis( 0, -labelbbox.height ) //top-left of text
  ];
  
  const visPt = labelEndpoints[0];

  const x2 = labelEl.dataset.peakXPx;  //attribute 'data-peak-x-px' value
  let y2 = 0.5*(parseFloat(labelEl.dataset.peakLowerYPx) + parseFloat(labelEl.dataset.peakUpperYPx));
  if( isFromPeakBeingHighlighted )
    y2 = (visPt.y > labelEl.dataset.peakLowerYPx) ? labelEl.dataset.peakLowerYPx : labelEl.dataset.peakUpperYPx;
  
  // The label point (x1,y1) closest to the peak point (x2,y2)
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
  
  // Only draw the line if it is at least 10 pixels long
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
  
  self.peakVis.select('path[data-energy="' + labelEl.dataset.peakEnergy + '"].peakFill').each( function(){
    self.highlightPeak(this,false);
  });
}//function highlightLabel()

/** Un-highlights the highlighted label (self.highlightedLabel), and if `unHighlightPeakTo`, its peak. */
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
  
  d3.select(self.highlightedLabel).attr('class', 'peaklabel');

  if( self.peakLabelLine ) {
    self.peakLabelLine.remove();
    self.peakLabelLine = null;
  }
  
  self.highlightedLabel = null;
}//function unHighlightLabel(...)

/** -------------- Background Subtract -------------- */
SpectrumChartD3.prototype.setBackgroundSubtract = function( subtract ) {
  this.options.backgroundSubtract = Boolean(subtract);
  this.redraw()();
}

SpectrumChartD3.prototype.setAllowDragRoiExtent = function( allow ){
  this.options.allowDragRoiExtent = Boolean(allow);
}

SpectrumChartD3.prototype.rebinForBackgroundSubtract = function() {
  var self = this;

  if (!self.hasSpectrumData())
    return;
  if (!self.options.backgroundSubtract)
    return;

  var bisector = self._bisecX;

  self.rawData.spectra.forEach(function(spectrum) {

    if (spectrum.type === self.spectrumTypes.BACKGROUND) return;

    const background = self.getSpectrumByID(spectrum.backgroundID);

    if (!background){
      if( 'bgsubtractpoints' in spectrum )
        delete spectrum.bgsubtractpoints;
      return;
    }

    /* do_rebin replaces the points arrays when anything changes, so unchanged arrays mean the
       subtraction is still current. */
    if( spectrum.bgsubtractpoints && (spectrum.__bgSubSrc === spectrum.points)
        && (spectrum.__bgSubBkg === background.points) )
      return;
    spectrum.__bgSubSrc = spectrum.points;
    spectrum.__bgSubBkg = background.points;

    // Subtract the nearest background point
    spectrum.bgsubtractpoints = spectrum.points.map(function(point) {
      const x = point.x;
      const y = point.y;
      const bpoint = background.points[bisector.left(background.points, x)];
      if (!bpoint) return { x: x, y: 0 };
      return { x: x, y: Math.max(0, y - bpoint.y) };
    });
  });
}

/** -------------- Helpers -------------- */
/** Returns true if the browser has touch capabilities (https://stackoverflow.com/questions/4817029). */
SpectrumChartD3.prototype.isTouchDevice = function() {
  return 'ontouchstart' in window
      || (navigator && navigator.maxTouchPoints);
}

SpectrumChartD3.prototype.areMultipleSpectrumPeaksShown = function() {
  var self = this;

  if (!self.hasSpectrumData())
    return false;

  let numberOfSpectraPeaks = 0;

  self.rawData.spectra.forEach(function(spectrum) {
    if (spectrum.peaks && spectrum.peaks.length && self._peaksDrawnFor(spectrum))
      numberOfSpectraPeaks++;
  });

  return numberOfSpectraPeaks > 1;
}

/** Returns the (first) spectrum with the given ID, or null. */
SpectrumChartD3.prototype.getSpectrumByID = function(id) {
  var self = this;

  if (!self.hasSpectrumData())
    return;

  for (let i = 0; i < self.rawData.spectra.length; i++) {
    if (self.rawData.spectra[i].id === id)
      return self.rawData.spectra[i];
  }

  return null;
}

/** Returns the spectra titles. */
SpectrumChartD3.prototype.getSpectrumTitles = function() {
  var self = this;

  if (!self.hasSpectrumData())
    return;

  var result = [];
  self.rawData.spectra.forEach(function(spectrum, i) {
    if (spectrum.title)
      result.push(spectrum.title);
    else
      result.push("Spectrum " + (i + 1));
  });
  return result;
}

/* Returns the data y-range for the currently viewed x-range, as [min, max, smallest non-zero y]. */
SpectrumChartD3.prototype.getYAxisDataDomain = function(){
  var self = this;

  if( !self.hasAnyData() )
    return [0, 3000, self.options.logYAxisMin];

  var y0 = null, y1 = null, minNonZeroY0 = self.options.logYAxisMin;
  const hasSpectra = !!(self.rawData.spectra && self.rawData.spectra.length);

  if( hasSpectra ){
    var foreground = self.rawData.spectra[0];
    var firstData = self.displayed_start(foreground);
    var lastData = self.displayed_end(foreground);

    if( firstData >= 0 ){
      const forkey = self.options.backgroundSubtract && ('bgsubtractpoints' in foreground) ? 'bgsubtractpoints' : 'points';
      y0 = y1 = foreground[forkey][firstData].y;
      if( y0 > 0 ) minNonZeroY0 = y0;

      self.rawData.spectra.forEach(function(spectrum) {
        if (self.options.backgroundSubtract && spectrum.type === self.spectrumTypes.BACKGROUND) return;
        firstData = self.displayed_start(spectrum);
        lastData = self.displayed_end(spectrum);
        var speckey = self.options.backgroundSubtract && ('bgsubtractpoints' in spectrum) ? 'bgsubtractpoints' : 'points';

        for (var i = firstData; i < lastData; i++) {
          if (spectrum[speckey][i]) {
            const y = spectrum[speckey][i].y;
            y0 = Math.min( y0, y );
            y1 = Math.max( y1, y );
            if( y > 0 ) minNonZeroY0 = Math.min( minNonZeroY0, y );
          }
        }
      });
    }
  }

  // Include the top of the template stack (probed at each template's own points; it only pushes y1 up)
  const templates = (self.rawData && self.rawData.templates) ? self.rawData.templates : [];
  if( templates.length > 0 ){
    const xMin = self.xScale.domain()[0];
    const xMax = self.xScale.domain()[1];
    if( y0 === null ){ y0 = 0; y1 = 0; }

    for( let ti = 0; ti < templates.length; ++ti ){
      const t = templates[ti];
      if( !t || !t.points || !t.points.length ) continue;
      for( let pi = 0; pi < t.points.length; ++pi ){
        const xv = t.points[pi].x;
        if( xv < xMin || xv > xMax ) continue;
        const stackTop = self._templateBaselineAt(templates, xv);
        if( stackTop > y1 ) y1 = stackTop;
        if( stackTop > 0 && stackTop < minNonZeroY0 ) minNonZeroY0 = stackTop;
      }
    }
  }

  if( y0 === null || y1 === null ){ y0 = 0; y1 = 3000; }
  if( y0 > y1 ) { y1 = [y0, y0 = y1][0]; }
  if( y0 == y1 ){ y0 -=1; y1 += 1; }

  return [y0, y1, minNonZeroY0];
}

/** Returns the y-axis domain, [max, min], for the displayed data plus the padding options. */
SpectrumChartD3.prototype.getYAxisDomain = function(){
  var self = this;

  if( !self.hasAnyData() )
    return [3000,0];
    
  let yrange, y0, y1;
  yrange = self.getYAxisDataDomain();
  y0 = yrange[0];
  y1 = yrange[1];

  if( self.options.yscale == "log" ) {
    // The (approx) fractions of the chart the scale extends past the data
    var yfractop = self.options.logYFracTop, yfracbottom = self.options.logYFracBottom;

    var y0Intitial = ((y0<=0.0) ? self.options.logYAxisMin : y0);
    
    // If the data doesn't reach 1.0, base the minimum on the smallest non-zero counts
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
