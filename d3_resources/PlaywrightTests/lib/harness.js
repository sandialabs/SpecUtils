// Test fixtures + the `chart` driver shared by every spec.
//
//   const { test, expect } = require('../lib/harness');
//   test('...', async ({ chart }) => { ... });
//
// `chart` wraps page.evaluate() calls into the harness helpers (window.toClient, window.emits,
// window.boxRect, ...) and provides mouse + touch gesture builders. All coordinates are given
// as spectrum energies (keV); the harness converts them to exact viewport pixels via the chart's
// own xScale/yScale, so tests are independent of chart size and layout.

const base = require('@playwright/test');
const { MultiTouch, lerp } = require('./touch');

const HARNESS_URL = '/PlaywrightTests/harness/harness.html';

// SpectrumChartD3 timing constants the helpers must respect (see SpectrumChartD3.js):
const ZOOM_MIN_HOLD_MS  = 90;   // x-zoom needs the button down > 75 ms (BUG-03); use margin
const DOUBLE_CLICK_MS   = 500;  // options.doubleClickDelay — single click/tap emits after this
const TOUCH_HOLD_MS     = 700;  // long-press -> rightclicked
const SETTLE_MS         = 80;   // let throttled-emit trailing timers + redraw run

class ChartDriver {
  /** @param {import('@playwright/test').Page} page */
  constructor( page ){ this.page = page; this._touch = null; }

  // ---- lifecycle ---------------------------------------------------------
  async open(){
    await this.page.goto( HARNESS_URL );
    await this.page.waitForFunction( () => window.__ready === true, null, { timeout: 15000 } );
    await this.reset();
  }
  async reset(){ await this.page.evaluate( () => window.testReset() ); }
  async resetView(){ await this.page.evaluate( () => window.resetView() ); }

  // ---- state / assertions surface ---------------------------------------
  emits( name )    { return this.page.evaluate( n => window.emits(n), name ?? null ); }
  lastEmit( name ) { return this.page.evaluate( n => window.lastEmit(n), name ?? null ); }
  errors()         { return this.page.evaluate( () => window.errors() ); }
  xDomain()        { return this.page.evaluate( () => window.xDomain() ); }
  yDomain()        { return this.page.evaluate( () => window.yDomain() ); }
  fullXRange()     { return this.page.evaluate( () => window.fullXRange() ); }
  dragMode()       { return this.page.evaluate( () => window.dragMode() ); }
  peakEnergies()   { return this.page.evaluate( () => window.peakEnergies() ); }
  boxRect( sel )   { return this.page.evaluate( s => window.boxRect(s), sel ); }

  // Window globals must not be polluted (BUG-09 leaked pageX/pageY to global scope).
  globalLeaked( name ){ return this.page.evaluate( n => Object.prototype.hasOwnProperty.call(window, n) && typeof window[n] !== 'undefined' ? typeof window[n] : false, name ); }

  // energy[, {count|yFrac}] -> {x,y} viewport pixels
  client( energy, opts ){ return this.page.evaluate( a => window.toClient(a.e, a.o), { e: energy, o: opts || {} } ); }
  energyAt( clientX ){ return this.page.evaluate( x => window.clientToEnergy(x), clientX ); }

  // ---- mouse primitives (Playwright native) ------------------------------
  async _down( energy, opts ){
    opts = opts || {};
    const p = await this.client( energy, opts );
    for( const k of (opts.modifiers || []) ) await this.page.keyboard.down( k );
    await this.page.mouse.move( p.x, p.y );
    await this.page.mouse.down();
    return p;
  }
  async _moveTo( energy, opts ){
    const p = await this.client( energy, opts || {} );
    await this.page.mouse.move( p.x, p.y, { steps: (opts && opts.steps) || 8 } );
    return p;
  }
  async _up( opts ){
    await this.page.mouse.up();
    for( const k of ((opts && opts.modifiers) || []) ) await this.page.keyboard.up( k );
  }

  // High-level modifier drag from one energy to another. `modifiers` are Playwright key names
  // ('Control','Shift','Alt','Meta'). Holds > 75 ms so x-zoom isn't suppressed as accidental.
  async mouseDrag( fromEnergy, toEnergy, opts ){
    opts = opts || {};
    await this._down( fromEnergy, opts );
    await this._moveTo( toEnergy, opts );
    await this.page.waitForTimeout( opts.holdMs ?? ZOOM_MIN_HOLD_MS );
    await this._up( opts );
    await this.page.waitForTimeout( SETTLE_MS );
  }

  // Vertical drag at a fixed energy (x), from one plot y-fraction to another. For y-axis zoom
  // (Meta-drag) and other vertical gestures. 0=top of plot, 1=bottom.
  async mouseDragV( energy, fromYFrac, toYFrac, opts ){
    opts = opts || {};
    await this._down( energy, { ...opts, yFrac: fromYFrac } );
    await this._moveTo( energy, { ...opts, yFrac: toYFrac } );
    await this.page.waitForTimeout( opts.holdMs ?? ZOOM_MIN_HOLD_MS );
    await this._up( opts );
    await this.page.waitForTimeout( SETTLE_MS );
  }

  // Same as mouseDrag but invokes `midFn()` while the button is still down (for box-geometry
  // assertions), then releases.
  async mouseDragWithMid( fromEnergy, toEnergy, opts, midFn ){
    opts = opts || {};
    await this._down( fromEnergy, opts );
    await this._moveTo( toEnergy, opts );
    await this.page.waitForTimeout( opts.holdMs ?? ZOOM_MIN_HOLD_MS );
    if( midFn ) await midFn();
    await this._up( opts );
    await this.page.waitForTimeout( SETTLE_MS );
  }

  // Synthetic same-tick (or held) mouse drag via native events; used for deterministic timing
  // (BUG-03). opts: { yFrac, holdMs, ctrlKey, shiftKey, altKey, metaKey, steps }.
  async synthDrag( fromEnergy, toEnergy, opts ){
    await this.page.evaluate( a => window.synthMouseDrag( a.from, a.to, a.opts ), { from: fromEnergy, to: toEnergy, opts: opts || {} } );
    await this.page.waitForTimeout( SETTLE_MS );
  }

  async click( energy, opts ){
    const p = await this.client( energy, opts || {} );
    await this.page.mouse.click( p.x, p.y );
    await this.page.waitForTimeout( DOUBLE_CLICK_MS + 60 );  // let the single-click timer fire
  }
  async dblclick( energy, opts ){
    const p = await this.client( energy, opts || {} );
    await this.page.mouse.dblclick( p.x, p.y );
    await this.page.waitForTimeout( SETTLE_MS );
  }
  // Right-click (button 2) at an energy. Emits 'rightclicked' on mouse-up (no debounce).
  async rightClick( energy, opts ){
    const p = await this.client( energy, opts || {} );
    await this.page.mouse.move( p.x, p.y );
    await this.page.mouse.down( { button: 'right' } );
    await this.page.mouse.up( { button: 'right' } );
    await this.page.waitForTimeout( SETTLE_MS );
  }

  // Right-button drag = pan. Holds the right button while moving from one energy to another.
  async rightDragPan( fromEnergy, toEnergy, opts ){
    opts = opts || {};
    const a = await this.client( fromEnergy, opts );
    const b = await this.client( toEnergy, opts );
    await this.page.mouse.move( a.x, a.y );
    await this.page.mouse.down( { button: 'right' } );
    await this.page.mouse.move( b.x, b.y, { steps: opts.steps || 10 } );
    await this.page.mouse.up( { button: 'right' } );
    await this.page.waitForTimeout( SETTLE_MS );
  }

  // Lines of the bottom-right mouse-position readout (one per tspan), or [] if hidden.
  // Returned as separate lines so the energy value isn't concatenated with the counts value.
  mouseCoordLines(){ return this.page.evaluate( () => {
    const sp = document.querySelectorAll('.mouseInfo text tspan');
    return Array.from(sp).map( t => (t.textContent || '').replace(/\s+/g, ' ').trim() ).filter(Boolean);
  } ); }

  // The energy (keV) shown in the readout, or null. Reads the tspan that contains "keV".
  async readoutEnergy(){
    const lines = await this.mouseCoordLines();
    const line = lines.find( l => /keV/.test(l) );
    if( !line ) return null;
    const m = line.match( /([\d.]+)\s*keV/ );
    return m ? parseFloat( m[1] ) : null;
  }

  // Plot drawing width in px (graph.size.width) — for energy/pixel-rate assertions.
  plotWidth(){ return this.page.evaluate( () => window.graph.size.width ); }

  async wheel( energy, deltaY, opts ){
    const p = await this.client( energy, opts || {} );
    await this.page.mouse.move( p.x, p.y );
    await this.page.mouse.wheel( 0, deltaY );
    await this.page.waitForTimeout( 300 );  // wheeltimer cleanup is 250 ms
  }

  // ---- touch (CDP) -------------------------------------------------------
  async touchInit(){
    if( this._touch ) return this._touch;
    const cdp = await this.page.context().newCDPSession( this.page );
    this._touch = new MultiTouch( cdp );
    return this._touch;
  }

  async _runTouch( startPts, endPts, steps ){
    const mt = await this.touchInit();
    await mt.start( startPts );
    steps = steps || 10;
    for( let i = 1; i <= steps; ++i ){
      const t = i / steps;
      const pts = startPts.map( (s, idx) => ({ id: s.id, ...lerp(s, endPts[idx], t) }) );
      await mt.move( pts );
      await this.page.waitForTimeout( 8 );
    }
    await mt.end();
    await this.page.waitForTimeout( SETTLE_MS );
  }

  // One finger from energy A to energy B (pan).
  async oneFingerDrag( fromEnergy, toEnergy, opts ){
    opts = opts || {};
    const a = await this.client( fromEnergy, opts );
    const b = await this.client( toEnergy, opts );
    await this._runTouch( [{ id: 0, ...a }], [{ id: 0, ...b }], opts.steps );
  }

  // Two fingers, parameterised in energy + a pixel y. `f1`/`f2` each: {fromEnergy,toEnergy}.
  // dyPx shifts BOTH fingers vertically by the same pixel amount over the gesture (for the
  // vertical delete-peak swipe). f2YOffsetPx separates the fingers vertically at start if needed.
  async twoFingerDrag( f1, f2, opts ){
    opts = opts || {};
    const yFrac = (typeof opts.yFrac === 'number') ? opts.yFrac : 0.30;
    const a1 = await this.client( f1.fromEnergy, { yFrac } );
    const b1 = await this.client( f1.toEnergy,   { yFrac } );
    const a2 = await this.client( f2.fromEnergy, { yFrac } );
    const b2 = await this.client( f2.toEnergy,   { yFrac } );
    const dy = opts.dyPx || 0;
    const start = [ { id: 0, x: a1.x, y: a1.y }, { id: 1, x: a2.x, y: a2.y + (opts.f2YOffsetPx || 0) } ];
    const end   = [ { id: 0, x: b1.x, y: b1.y + dy }, { id: 1, x: b2.x, y: b2.y + dy + (opts.f2YOffsetPx || 0) } ];
    await this._runTouch( start, end, opts.steps );
  }

  // Like twoFingerDrag, but runs midFn() after the moves while both fingers are still down,
  // then lifts. Used to simulate the server's ROI response before the fingers lift, so the
  // peak-fit completion path (handleMouseUpPeakFit) can be exercised in this server-less harness.
  async twoFingerDragWithMid( f1, f2, opts, midFn ){
    opts = opts || {};
    const yFrac = (typeof opts.yFrac === 'number') ? opts.yFrac : 0.30;
    const a1 = await this.client( f1.fromEnergy, { yFrac } );
    const b1 = await this.client( f1.toEnergy,   { yFrac } );
    const a2 = await this.client( f2.fromEnergy, { yFrac } );
    const b2 = await this.client( f2.toEnergy,   { yFrac } );
    const dy = opts.dyPx || 0;
    const start = [ { id: 0, x: a1.x, y: a1.y }, { id: 1, x: a2.x, y: a2.y } ];
    const end   = [ { id: 0, x: b1.x, y: b1.y + dy }, { id: 1, x: b2.x, y: b2.y + dy } ];
    const mt = await this.touchInit();
    await mt.start( start );
    const steps = opts.steps || 10;
    for( let i = 1; i <= steps; ++i ){
      const t = i / steps;
      await mt.move( [ { id: 0, ...lerp(start[0], end[0], t) }, { id: 1, ...lerp(start[1], end[1], t) } ] );
      await this.page.waitForTimeout( 8 );
    }
    if( midFn ) await midFn();
    await mt.end();
    await this.page.waitForTimeout( SETTLE_MS );
  }

  // Inject a fake "server response" ROI so the peak-fit completion path has something to finalise.
  setFakeFitRoi( lowerEnergy, upperEnergy, nPeaks ){
    return this.page.evaluate( a => {
      window.graph.roiBeingDrugUpdate = { lowerEnergy: a.lo, upperEnergy: a.hi, peaks: new Array(a.n).fill({}) };
    }, { lo: lowerEnergy, hi: upperEnergy, n: nPeaks || 1 } );
  }

  // Pinch about a center energy: two fingers symmetric in x, gap goes from gapFrom->gapTo px.
  // axis 'x' keeps y constant (x-zoom); axis 'y' spreads vertically instead (y-zoom).
  async pinch( centerEnergy, gapFromPx, gapToPx, axis, opts ){
    opts = opts || {};
    const yFrac = (typeof opts.yFrac === 'number') ? opts.yFrac : 0.45;
    const c = await this.client( centerEnergy, { yFrac } );
    let start, end;
    if( axis === 'y' ){
      start = [ { id: 0, x: c.x, y: c.y - gapFromPx/2 }, { id: 1, x: c.x, y: c.y + gapFromPx/2 } ];
      end   = [ { id: 0, x: c.x, y: c.y - gapToPx/2 },   { id: 1, x: c.x, y: c.y + gapToPx/2 } ];
    } else {
      start = [ { id: 0, x: c.x - gapFromPx/2, y: c.y }, { id: 1, x: c.x + gapFromPx/2, y: c.y } ];
      end   = [ { id: 0, x: c.x - gapToPx/2, y: c.y },   { id: 1, x: c.x + gapToPx/2, y: c.y } ];
    }
    await this._runTouch( start, end, opts.steps );
  }

  async tap( energy, opts ){
    const mt = await this.touchInit();
    const p = await this.client( energy, opts || {} );
    await mt.start( [{ id: 0, ...p }] );
    await this.page.waitForTimeout( 20 );
    await mt.end();
    await this.page.waitForTimeout( DOUBLE_CLICK_MS + 60 );  // single-tap leftclicked timer
  }
  async doubleTap( energy, opts ){
    const mt = await this.touchInit();
    const p = await this.client( energy, opts || {} );
    await mt.start( [{ id: 0, ...p }] ); await this.page.waitForTimeout( 20 ); await mt.end();
    await this.page.waitForTimeout( 120 );  // well under doubleClickDelay
    await mt.start( [{ id: 0, ...p }] ); await this.page.waitForTimeout( 20 ); await mt.end();
    await this.page.waitForTimeout( SETTLE_MS );
  }
  async longPress( energy, opts ){
    const mt = await this.touchInit();
    const p = await this.client( energy, opts || {} );
    await mt.start( [{ id: 0, ...p }] );
    await this.page.waitForTimeout( TOUCH_HOLD_MS + 120 );  // exceed touchHoldTimeInterval
    await mt.end();
    await this.page.waitForTimeout( SETTLE_MS );
  }
}

// Extend Playwright's test with a `chart` fixture (opened + reset for every test) and a
// per-test guard that fails if the chart threw during the test.
const test = base.test.extend({
  chart: async ({ page }, use) => {
    const driver = new ChartDriver( page );
    // Surface real page errors loudly too.
    page.on( 'pageerror', e => driver.__pageError = String(e) );
    await driver.open();
    await use( driver );
  },
});

module.exports = { test, expect: base.expect, ChartDriver, HARNESS_URL };
