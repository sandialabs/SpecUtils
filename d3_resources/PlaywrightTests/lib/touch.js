// Low-level multi-touch driver built on the Chrome DevTools Protocol.
//
// Playwright's built-in `page.touchscreen` only does single-finger taps; the chart's
// interesting gestures are all two-finger. `Input.dispatchTouchEvent` produces genuine
// DOM TouchEvents (with populated touches / changedTouches and per-finger identifiers),
// which is exactly what SpectrumChartD3's touch handlers and d3.v3's d3.touches() read.
//
// Coordinates are CSS pixels relative to the viewport top-left — the same space the
// harness's window.toClient() returns (it uses the vis group's getScreenCTM()).

class MultiTouch {
  /** @param {import('@playwright/test').CDPSession} cdp */
  constructor( cdp ){ this.cdp = cdp; }

  _points( pts ){
    return pts.map( function(p){ return { x: Math.round(p.x), y: Math.round(p.y), id: p.id }; } );
  }

  async start( pts ){ await this.cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: this._points(pts) }); }
  async move( pts ){  await this.cdp.send('Input.dispatchTouchEvent', { type: 'touchMove',  touchPoints: this._points(pts) }); }

  // Release every active finger at once (touchend with the lifted points in changedTouches).
  async end(){ await this.cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); }
  async cancel(){ await this.cdp.send('Input.dispatchTouchEvent', { type: 'touchCancel', touchPoints: [] }); }
}

// Linear interpolation between two {x,y} points (0..1).
function lerp( a, b, t ){ return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t }; }

module.exports = { MultiTouch, lerp };
