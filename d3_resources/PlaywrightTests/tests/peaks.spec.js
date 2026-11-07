// PERF-01a: peak-path hover/context-menu behavior.  Written against the per-path-listener
// code first; must stay green when the listeners are delegated to the peakVis group.
const { test, expect } = require('../lib/harness');

// Finds the last drawn peak path near `energy` (for multi-peak ROIs that's the outline,
// which is what carries the hover listeners) and returns points on/off it.
async function peakPathPoint( page, energy ){
  return page.evaluate( (e) => {
    let best = null;
    document.querySelectorAll('g.peakVis path[data-energy]').forEach( p => {
      if( Math.abs( Number(p.getAttribute('data-energy')) - e ) < 4 )
        best = p;
    } );
    if( !best ) return null;
    best.__peakTag = 1;
    const r = best.getBoundingClientRect();
    return { apex: { x: r.x + r.width/2, y: r.y + 2 },
             mid:  { x: r.x + r.width/2, y: r.y + r.height*0.5 },
             above:{ x: r.x + r.width/2, y: r.y - 90 } };
  }, energy );
}
const taggedStrokeWidth = page => page.evaluate( () => {
  let el = null;
  document.querySelectorAll('g.peakVis path').forEach( p => { if( p.__peakTag ) el = p; } );
  return el ? String( el.getAttribute('stroke-width') ) : null;
} );

test.describe('peak interactions', () => {
  test('hovering a peak highlights it (stroke-width 2); moving off unhighlights', async ({ chart, page }) => {
    const pts = await peakPathPoint( page, 356 );
    expect( pts, 'fixture should have a ~356 keV peak' ).not.toBeNull();

    await page.mouse.move( pts.apex.x, pts.apex.y, { steps: 3 } );
    await page.waitForTimeout( 150 );
    expect( await taggedStrokeWidth( page ), 'hovered peak gets stroke-width 2' ).toBe( '2' );

    await page.mouse.move( pts.above.x, pts.above.y, { steps: 3 } );
    await page.waitForTimeout( 150 );
    expect( await taggedStrokeWidth( page ), 'unhovered peak back to stroke-width 1' ).toBe( '1' );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('right-click on a peak emits rightclicked at the peak energy, no native-menu error', async ({ chart, page }) => {
    const pts = await peakPathPoint( page, 356 );
    await page.mouse.click( pts.mid.x, pts.mid.y, { button: 'right' } );
    await page.waitForTimeout( 200 );
    const emits = await chart.emits( 'rightclicked' );
    expect( emits.length ).toBeGreaterThan( 0 );
    expect( Math.abs( emits[emits.length-1].args[0] - 356 ) ).toBeLessThan( 8 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('left-click on a peak emits leftclicked and highlights the peak', async ({ chart, page }) => {
    const pts = await peakPathPoint( page, 356 );
    await page.mouse.click( pts.mid.x, pts.mid.y );
    await page.waitForTimeout( 550 );          // > doubleClickDelay, so the single-click fires
    expect( (await chart.emits('leftclicked')).length ).toBe( 1 );
    const nHighlighted = await page.evaluate( () => {
      let n = 0;
      document.querySelectorAll('g.peakVis path').forEach( p => { if( p.getAttribute('stroke-width') === '2' ) ++n; } );
      return n;
    } );
    expect( nHighlighted, 'clicked peak highlighted' ).toBeGreaterThan( 0 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('peakVis child count is stable across a small pan', async ({ chart, page }) => {
    await chart.mouseDrag( 240, 420, { yFrac: 0.5 } );      // zoom to a multi-peak window
    const n0 = await page.evaluate( () => document.querySelectorAll('g.peakVis > *').length );
    expect( n0 ).toBeGreaterThan( 0 );
    await chart.rightDragPan( 330, 327, { yFrac: 0.25 } );  // tiny pan; no ROI enters/leaves
    const n1 = await page.evaluate( () => document.querySelectorAll('g.peakVis > *').length );
    expect( n1, 'no duplicated/leaked peak nodes across redraw' ).toBe( n0 );
    expect( await chart.errors() ).toEqual( [] );
  });
});


// Peak drawing (drawPeaks/roiGeometry): curves show, at each energy, the counts a displayed channel centered
//  there would hold, computed from each peak's CDF (`_peakShape`) and the continuum's (`_continuumCdf`).
test.describe('peak drawing', () => {
  // Topmost pixel y at which the SVG paths `ds` cross pixel x (paths may leave out vertices along straight runs).
  function topAtPx( ds, x ){
    let top = Infinity;
    ds.forEach( (d) => {
      const v = d.replace( /[MLZ]/g, ' ' ).trim().split( /\s+/ ).map( (p) => p.split(',').map(Number) );
      for( let i = 1; i < v.length; ++i ){
        const [x0, y0] = v[i-1], [x1, y1] = v[i];
        if( (x0 !== x1) && (Math.min(x0, x1) <= x) && (x <= Math.max(x0, x1)) )
          top = Math.min( top, y0 + (y1 - y0)*(x - x0)/(x1 - x0) );
      }
    } );
    return top;
  }
  test.beforeEach( async ({ chart, page }) => {
    await page.addScriptTag( { content: 'window.topAtPx = ' + topAtPx.toString() + ';' } );
  });

  test('peak-shape CDFs are normalized, monotone, and match the integrated PDFs', async ({ chart, page }) => {
    const failures = await page.evaluate( () => {
      const erfc = SpectrumChartD3.erfc;
      const mean = 500, sigma = 1.2, amp = 1000;
      // Reference PDFs, in t = (x - mean)/sigma, written independently of the chart code.
      const gaus = (t) => Math.exp(-0.5*t*t)/Math.sqrt(2*Math.PI);
      const exg = (t, s) => (t > 30) ? 0 : (0.5/s)*Math.exp(t/s + 0.5/(s*s))*erfc( Math.SQRT1_2*(t + 1/s) ); // low-side Exp*Gauss
      const tail = (a, n, u) => Math.pow(n/a, n)*Math.exp(-0.5*a*a)*Math.pow(n/a - a - u, -n);     // power-law tail, u <= -a
      const cases = [
        { skewType: 'NoSkew', pdf: gaus },
        { skewType: 'ExGauss', skew: [0.8*sigma], pdf: (t) => exg(t, 0.8) },
        { skewType: 'GaussPlusExGauss', skew: [0.3, 1.5*sigma], pdf: (t) => 0.7*gaus(t) + 0.3*exg(t, 1.5) },
        { skewType: 'DoubleExGauss', skew: [0.5*sigma, 2*sigma, 0.25], pdf: (t) => 0.75*exg(t, 0.5) + 0.25*exg(t, 2.5) },
        { skewType: 'GaussExp', skew: [1.3], pdf: (t) => Math.exp( (t >= -1.3) ? -0.5*t*t : (0.5*1.3*1.3 + 1.3*t) ) },
        { skewType: 'ExpGaussExp', skew: [1.1, 2.2],
          pdf: (t) => Math.exp( (t > 2.2) ? (0.5*2.2*2.2 - 2.2*t) : ((t > -1.1) ? -0.5*t*t : (0.5*1.1*1.1 + 1.1*t)) ) },
        { skewType: 'CB', skew: [1.4, 6], pdf: (t) => (t > -1.4) ? Math.exp(-0.5*t*t) : tail(1.4, 6, t) },
        { skewType: 'DSCB', skew: [1.2, 6, 1.8, 8],
          pdf: (t) => (t < -1.2) ? tail(1.2, 6, t) : ((t > 1.8) ? tail(1.8, 8, -t) : Math.exp(-0.5*t*t)) },
        { skewType: 'VoigtPlusExGauss', skew: [0.3, 0.2, sigma] },
        { skewType: 'GadrasGeneric', skew: [4, 2, 0.5, 0.5, 0, 0] },
        { skewType: 'GadrasCZT', skew: [8, 1, 0, 0, 1, -1] },
      ];
      const integ = (f, a, b, n) => { let s = 0; const h = (b - a)/n; for( let i = 0; i < n; ++i ) s += f(a + (i + 0.5)*h); return s*h; };

      const failures = [];
      cases.forEach( (c) => {
        const peak = { type: 'GaussianDefined', skewType: c.skewType, Centroid: [mean, 0], Width: [sigma, 0], Amplitude: [amp, 0] };
        for( let i = 0; i < 6; ++i )
          peak['Skew' + i] = [(c.skew && (i < c.skew.length)) ? c.skew[i] : 0, 0];
        // Normalization of the reference PDF (the chart normalizes each shape itself, so no `DistNorm` is given)
        const area = c.pdf ? (integ(c.pdf, -300, -20, 20000) + integ(c.pdf, -20, 20, 80000) + integ(c.pdf, 20, 300, 20000)) : 1;

        const cdf = window.graph._peakShape( peak ).cdf, at = (t) => cdf( mean + t*sigma );
        const total = at(1E4) - at(-1E4);
        if( !(Math.abs(total - amp) < 2E-4*amp) )
          failures.push( `${c.skewType}: total ${total} != ${amp}` );
        for( let t = -40, prev = at(-40); t < 40; t += 0.02 ){
          const v = at(t + 0.02);
          if( !(v >= prev - 1E-9*amp) ){ failures.push( `${c.skewType}: not monotone at t=${t}` ); break; }
          prev = v;
        }
        if( c.pdf ){
          for( let k = -14; k < 14; ++k ){
            const want = amp*integ( c.pdf, 0.5*k, 0.5*(k + 1), 500 )/area, got = at(0.5*(k + 1)) - at(0.5*k);
            if( !(Math.abs(got - want) < 2E-5*amp) ){ failures.push( `${c.skewType}: t in [${0.5*k},${0.5*(k+1)}] got ${got}, want ${want}` ); break; }
          }
        }
      } );
      return failures;
    } );
    expect( failures ).toEqual( [] );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('curves equal the exact counts of the displayed channel at its center, and end on the ROI edges', async ({ chart, page }) => {
    const rows = await page.evaluate( () => {
      const g = window.graph, erf = SpectrumChartD3.erf;
      g.setXAxisRange( 70, 400, false, false );
      g.redraw()();
      const fg = g.rawData.spectra.find( s => s.type === 'FOREGROUND' ), pts = fg.points;
      const vertices = (d) => d.replace( /[MLZ]/g, ' ' ).trim().split( /\s+/ ).map( (p) => p.split(',').map(Number) );
      return fg.peaks.map( (roi) => {
        // Exact continuum + peaks counts in the displayed channel holding the ROIs last peak (the top of the stack)
        const m = roi.peaks[roi.peaks.length - 1].Centroid[0];
        const k = pts.findIndex( (p, i) => (p.x <= m) && (pts[i+1].x > m) );
        const x0 = pts[k].x, x1 = pts[k+1].x;
        let counts = 0;
        roi.coeffs.forEach( (c, i) => counts += c/(i + 1)*(Math.pow(x1 - roi.referenceEnergy, i + 1) - Math.pow(x0 - roi.referenceEnergy, i + 1)) );
        roi.peaks.forEach( (p) => counts += 0.5*p.Amplitude[0]*(erf((x1 - p.Centroid[0])/(Math.SQRT2*p.Width[0])) - erf((x0 - p.Centroid[0])/(Math.SQRT2*p.Width[0]))) );

        const fills = g.peakPaths.filter( (pp) => pp.isFill && (pp.roi === roi) ).map( (pp) => pp.path.attr('d') );
        const top = window.topAtPx( fills, g.xScale( 0.5*(x0 + x1) ) );
        let xmin = Infinity, xmax = -Infinity;
        fills.forEach( (d) => vertices( d ).forEach( ([x, y]) => { xmin = Math.min( xmin, x ); xmax = Math.max( xmax, x ); } ) );
        // A single peak's fill spans the whole ROI; in multi-peak ROIs each fill spans just its peak, within the ROI.
        const lpx = g.xScale(roi.lowerEnergy), upx = g.xScale(roi.upperEnergy), single = (roi.peaks.length === 1);
        return { energy: m, dy: Math.abs( top - g.yScale(counts) ),
                 dxLower: single ? Math.abs( xmin - lpx ) : Math.max( 0, lpx - xmin ),
                 dxUpper: single ? Math.abs( xmax - upx ) : Math.max( 0, xmax - upx ) };
      } );
    } );
    expect( rows.length ).toBe( 5 );
    rows.forEach( (r) => {
      expect( r.dy, `curve height at channel center, ${r.energy} keV` ).toBeLessThan( 0.15 );
      expect( r.dxLower, `ROI lower edge, ${r.energy} keV` ).toBeLessThan( 0.01 );
      expect( r.dxUpper, `ROI upper edge, ${r.energy} keV` ).toBeLessThan( 0.01 );
    } );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('zoomed out, a ROI narrower than a displayed channel is drawn, but no taller than the channel bars', async ({ chart, page }) => {
    const res = await page.evaluate( () => {
      const g = window.graph;
      const fg = g.rawData.spectra.find( s => s.type === 'FOREGROUND' );
      const roi = JSON.parse( JSON.stringify( fg.peaks.find( (r) => Math.abs(r.peaks[0].Centroid[0] - 355.7) < 1 ) ) );
      const b = g.min_max_x_values();
      g.setXAxisRange( b[0], b[1], false, false );
      g.redraw()();
      // Shrink the ROI to lie strictly inside the (rebinned) channel holding the peak - before 20261106 such a
      //  ROI wasn't drawn at all.
      const m = roi.peaks[0].Centroid[0], pts = fg.points;
      const k = pts.findIndex( (p, i) => (p.x <= m) && (pts[i+1].x > m) ), w = pts[k+1].x - pts[k].x;
      roi.lowerEnergy = Math.max( pts[k].x + 0.02*w, m - 0.6 );
      roi.upperEnergy = Math.min( pts[k+1].x - 0.02*w, m + 0.6 );
      g.setRoiData( [roi], 'FOREGROUND' );
      const fills = g.peakPaths.filter( (pp) => pp.isFill );
      const res = { rebin: g.rebinFactor, nfill: fills.length, height: (fills.length ? fills[0].path.node().getBBox().height : 0) };

      // A narrow ROI straddling a channel boundary puts about half of its peak in each channel, so must not be
      //  drawn as tall as the whole peak (it was, briefly, during 20261106 development).
      g.setLinearY();  // (changes the plot width, so the rebinning)
      g.redraw()();
      const lpts = fg.points, lk = lpts.findIndex( (p, i) => (p.x <= m) && (lpts[i+1].x > m) );
      const b0 = lpts[lk].x, phi = (z) => 0.5*SpectrumChartD3.erfc( -z*Math.SQRT1_2 );
      g.setRoiData( [ { type: 'Linear', lowerEnergy: b0 - 1, upperEnergy: b0 + 1, referenceEnergy: b0 - 1, coeffs: [100, 0],
        peaks: [ { type: 'GaussianDefined', skewType: 'NoSkew', Centroid: [b0, 0], Width: [0.3, 0], Amplitude: [1e5, 0], Chi2: [1, 0] } ] } ], 'FOREGROUND' );
      const bar = (j) => 1e5*(phi((lpts[j+1].x - b0)/0.3) - phi((lpts[j].x - b0)/0.3)) + 100*(lpts[j+1].x - lpts[j].x);
      const top = Math.min( ...g.peakPaths.filter( (pp) => pp.isFill ).map( (pp) => pp.path.node().getBBox().y ) );
      res.sameBinning = (fg.points === lpts);
      res.straddleRatio = g.yScale.invert( top ) / Math.max( bar(lk - 1), bar(lk) );
      return res;
    } );
    expect( res.rebin ).toBeGreaterThan( 5 );
    expect( res.nfill ).toBe( 1 );
    expect( res.height, 'peak drawn about as tall as its channel bar' ).toBeGreaterThan( 50 );
    expect( res.sameBinning ).toBe( true );
    expect( res.straddleRatio, 'peak straddling channels drawn as tall as their bars' ).toBeGreaterThan( 0.98 );
    expect( res.straddleRatio, 'peak straddling channels drawn no taller than their bars' ).toBeLessThan( 1.01 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('a lone peak is drawn across its whole ROI, out into its far tails', async ({ chart, page }) => {
    const rows = await page.evaluate( () => {
      const g = window.graph, mean = 356, sigma = 0.6, amp = 1e8, phi = (z) => 0.5*SpectrumChartD3.erfc( -z*Math.SQRT1_2 );
      g.setRoiData( [ { type: 'Linear', lowerEnergy: 345, upperEnergy: 367, referenceEnergy: 345, coeffs: [2, 0],
        peaks: [ { type: 'GaussianDefined', skewType: 'NoSkew', Centroid: [mean, 0], Width: [sigma, 0], Amplitude: [amp, 0], Chi2: [1, 0] } ] } ], 'FOREGROUND' );
      g.setXAxisRange( 345, 367, false, false );
      g.redraw()();
      g.setYAxisRange( 0.01, 1e9 );
      const pts = g.rawData.spectra.find( s => s.type === 'FOREGROUND' ).points;
      const fills = g.peakPaths.filter( (pp) => pp.isFill ).map( (pp) => pp.path.attr('d') );
      const rows = [];
      for( let k = 0; k + 1 < pts.length; ++k ){
        const x0 = pts[k].x, x1 = pts[k+1].x, z = (0.5*(x0 + x1) - mean)/sigma;
        if( (Math.abs(z) > 4.5) && (Math.abs(z) < 8) ){
          const want = amp*(phi((x1 - mean)/sigma) - phi((x0 - mean)/sigma)) + 2*(x1 - x0);
          rows.push( { z: z, dy: Math.abs( window.topAtPx( fills, g.xScale(0.5*(x0 + x1)) ) - g.yScale(want) ) } );
        }
      }
      return rows;
    } );
    expect( rows.length ).toBeGreaterThan( 10 );
    rows.forEach( (r) => expect( r.dy, `curve height at z=${r.z.toFixed(2)}` ).toBeLessThan( 0.15 ) );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('per-channel (step and external) continua are drawn through their channel counts', async ({ chart, page }) => {
    const rows = await page.evaluate( () => {
      const g = window.graph, fg = g.rawData.spectra.find( s => s.type === 'FOREGROUND' ), x = fg.x;
      const c0 = d3.bisectLeft( x, 350 ) - 3, c1 = d3.bisectLeft( x, 362 ) + 3;
      const energies = x.slice( c0, c1 + 2 ), counts = energies.slice( 0, -1 ).map( (e) => (e < 356) ? 300 : 100 );
      const rows = [];
      for( const type of ['FlatStep', 'External'] ){
        // Two peaks, so the continuum gets its own path
        g.setRoiData( [ { type: type, lowerEnergy: 350, upperEnergy: 362, referenceEnergy: 350, coeffs: [0, 0],
          continuumEnergies: energies, continuumCounts: counts,
          peaks: [ 356, 358 ].map( (m) => ({ type: 'GaussianDefined', skewType: 'NoSkew', Centroid: [m, 0], Width: [0.6, 0], Amplitude: [2000, 0], Chi2: [1, 0] }) ) } ], 'FOREGROUND' );
        g.setXAxisRange( 349, 363, false, false );
        g.redraw()();
        const cont = Array.from( document.querySelectorAll('g.peakVis path:not([class])') ).map( (p) => p.getAttribute('d') );
        const pts = fg.points;
        for( let k = 0; k + 1 < pts.length; ++k ){
          if( (pts[k].x < 350) || (pts[k+1].x > 362) )
            continue;
          let want = 0;  // the continuum channels within this displayed channel
          for( let i = 0; i < counts.length; ++i )
            if( (energies[i] >= pts[k].x - 1e-6) && (energies[i+1] <= pts[k+1].x + 1e-6) ) want += counts[i];
          rows.push( { type: type, e: pts[k].x, ncont: cont.length,
                       dy: Math.abs( window.topAtPx( cont, g.xScale(0.5*(pts[k].x + pts[k+1].x)) ) - g.yScale(want) ) } );
        }
      }
      return rows;
    } );
    expect( rows.length ).toBeGreaterThan( 20 );
    rows.forEach( (r) => {
      expect( r.ncont, r.type ).toBe( 1 );
      expect( r.dy, `${r.type} continuum at ${r.e.toFixed(2)} keV` ).toBeLessThan( 0.15 );
    } );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('background subtraction, and linear/sqrt y-axes, draw every peak with finite coordinates', async ({ chart, page }) => {
    const res = await page.evaluate( () => {
      const g = window.graph, out = [];
      g.setXAxisRange( 70, 400, false, false );
      [ () => g.setBackgroundSubtract(true), () => g.setLinearY(), () => g.setSqrtY(), () => g.setBackgroundSubtract(false) ].forEach( (f) => {
        f();
        g.redraw()();
        const d = Array.from( document.querySelectorAll('g.peakVis path') ).map( (p) => p.getAttribute('d') );
        out.push( { nfill: g.peakPaths.filter( (pp) => pp.isFill ).length, bad: d.filter( (s) => /NaN|Infinity|undefined/.test(s) ).length } );
      } );
      return out;
    } );
    res.forEach( (r) => { expect( r.nfill ).toBe( 6 ); expect( r.bad ).toBe( 0 ); } );
    expect( await chart.errors() ).toEqual( [] );
  });
});
