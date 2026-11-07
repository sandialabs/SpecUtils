// Regression tests for assorted defects fixed during the 2026-11 cleanup pass.
const { test, expect } = require('../lib/harness');

// Adds a second chart (#chart2) to the page, with copies of the harness spectra.
async function addSecondChart( page ){
  await page.evaluate( () => {
    const d = document.createElement('div');
    d.id = 'chart2';
    d.style.width = '700px';
    d.style.height = '320px';
    document.body.appendChild( d );
    const spectra = window.graph.rawData.spectra.map( s => ({ x: s.x, y: s.y, type: s.type, title: s.title, id: s.id,
                                                             backgroundID: s.backgroundID, lineColor: s.lineColor,
                                                             peaks: s.peaks }) );
    window.graph2 = new SpectrumChartD3( 'chart2', {} );
    window.graph2.setData( { spectra: spectra }, true );
  } );
}

test.describe('cleanup-pass fixes', () => {
  test('slider grip lines are per chart: redrawing one chart keeps the other\'s', async ({ chart, page }) => {
    await addSecondChart( page );
    await page.evaluate( () => { window.graph2.setShowXAxisSliderChart( true ); window.graph.setShowXAxisSliderChart( true ); } );
    const count = () => page.evaluate( () => document.querySelectorAll('#chart2 .sliderDragRegionLine').length );
    const n = await count();
    expect( n ).toBeGreaterThan( 0 );
    await page.evaluate( () => window.graph.redraw()() );
    expect( await count() ).toBe( n );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('zoom-Y box follows a drag that goes above its start, then back below', async ({ chart, page }) => {
    const a = await chart.client( 1000, { yFrac: 0.5 } );
    const up = await chart.client( 1000, { yFrac: 0.3 } );
    const down = await chart.client( 1000, { yFrac: 0.75 } );
    await page.keyboard.down( 'Meta' );
    await page.mouse.move( a.x, a.y );
    await page.mouse.down();
    await page.mouse.move( up.x, up.y, { steps: 5 } );
    await page.mouse.move( down.x, down.y, { steps: 8 } );
    const box = await chart.boxRect( '#zoomInYBox' );
    await page.mouse.up();
    await page.keyboard.up( 'Meta' );
    expect( box ).not.toBeNull();
    expect( Math.abs( box.y - a.y ), 'box top stays at the drag start' ).toBeLessThan( 2 );
    expect( Math.abs( box.bottom - down.y ), 'box bottom at the pointer' ).toBeLessThan( 2 );
  });

  test('a scale-factor drag released over the plot emits yscaled and resets the slider', async ({ chart, page }) => {
    await page.evaluate( () => window.graph.setShowSpectrumScaleFactorWidget( true ) );
    await page.waitForTimeout( 50 );
    const c = await page.evaluate( () => { const b = document.querySelector('circle.scalertoggle').getBoundingClientRect();
                                           return { x: b.x + b.width/2, y: b.y + b.height/2 }; } );
    await page.mouse.move( c.x, c.y );
    await page.mouse.down();
    await page.mouse.move( c.x, c.y - 50, { steps: 5 } );
    await page.mouse.move( c.x - 300, c.y - 50, { steps: 8 } );   // over the plot area
    await page.mouse.up();
    await page.waitForTimeout( 80 );
    expect( (await chart.emits( 'yscaled' )).length ).toBeGreaterThan( 0 );
    const txtShown = await page.evaluate( () => Array.from( document.querySelectorAll('text.scalertxt') )
                                                .some( t => t.style.display !== 'none' ) );
    expect( txtShown, 'scale-factor text hidden once the drag ends' ).toBe( false );
  });

  test('turning feature markers off without a pointer event removes them', async ({ chart, page }) => {
    await page.evaluate( () => { window.graph.setComptonEdge( true ); window.graph.setSumPeaks( true ); } );
    const p = await chart.client( 1000, { yFrac: 0.5 } );
    await page.mouse.move( p.x, p.y, { steps: 3 } );
    expect( await page.evaluate( () => !!window.graph.comptonEdge && !!window.graph.sumPeakHelpText ) ).toBe( true );
    await page.evaluate( () => { window.graph.setComptonEdge( false ); window.graph.setSumPeaks( false ); } );
    expect( await page.evaluate( () => [!!window.graph.comptonEdge, !!window.graph.sumPeakHelpText] ) ).toEqual( [false, false] );
  });

  test('touch map resync keeps the active touches (stale entry, partial move / lift)', async ({ chart, page }) => {
    const res = await page.evaluate( () => {
      const g = window.graph;
      const t0 = { identifier: 0, pageX: 100, pageY: 100 }, t1 = { identifier: 1, pageX: 200, pageY: 100 };
      const out = [];
      const state = () => Object.keys( g.touchesOnChart ).sort().join(',') + '|' + Object.values( g.touchesOnChart ).every( v => !!v );

      g.touchesOnChart = null;
      g.updateTouchesOnChart( { type: 'touchstart', touches: [t0, t1], changedTouches: [t0, t1] } );
      g.touchesOnChart[999] = { identifier: 999, pageX: 0, pageY: 0, startX: 0, startY: 0 };   // e.g. a dropped touchend
      const t0m = { identifier: 0, pageX: 110, pageY: 100 };
      g.updateTouchesOnChart( { type: 'touchmove', touches: [t0m, t1], changedTouches: [t0m] } );
      out.push( state() );

      g.touchesOnChart[999] = { identifier: 999, pageX: 0, pageY: 0, startX: 0, startY: 0 };
      g.updateTouchesOnChart( { type: 'touchend', touches: [t0m], changedTouches: [t1] } );
      out.push( state() );
      g.touchesOnChart = null;
      return out;
    } );
    expect( res ).toEqual( ['0,1|true', '0|true'] );
  });

  test('a Ctrl-drag peak fit released outside the plot area still completes', async ({ chart, page }) => {
    await page.evaluate( () => { window.graph.setXAxisRange( 300, 420 ); window.graph.redraw()(); } );
    const a = await chart.client( 340, { yFrac: 0.5 } ), b = await chart.client( 370, { yFrac: 0.5 } );
    const out = await page.evaluate( () => { const r = document.getElementById('chart1').getBoundingClientRect(); return { x: r.right - 4, y: r.top + 0.6*r.height }; } );
    await page.keyboard.down( 'Control' );
    await page.mouse.move( a.x, a.y );
    await page.mouse.down();
    await page.mouse.move( b.x, b.y, { steps: 6 } );
    await chart.setFakeFitRoi( 340, 370, 1 );
    await page.mouse.move( out.x, out.y, { steps: 6 } );
    await page.mouse.up();
    await page.keyboard.up( 'Control' );
    await page.waitForTimeout( 80 );
    const finals = (await chart.emits( 'fitRoiDrag' )).filter( e => e.args[3] === true );
    expect( finals.length, 'final fitRoiDrag emitted' ).toBe( 1 );
    expect( await chart.dragMode() ).toBe( 'none' );
  });

  test('an x-axis drag that ends outside the chart emits xrangechanged', async ({ chart, page }) => {
    await page.evaluate( () => { window.graph.setXAxisRange( 300, 600 ); window.graph.redraw()(); } );
    await chart.reset();
    const lbl = await page.evaluate( () => { for( const t of document.querySelectorAll('.xaxis g.tick text') ){
      if( t.textContent ){ const b = t.getBoundingClientRect(); if( b.width > 0 ) return { x: b.x + b.width/2, y: b.y + b.height/2 }; } } return null; } );
    await page.mouse.move( lbl.x, lbl.y );
    await page.mouse.down();
    await page.mouse.move( lbl.x + 60, lbl.y, { steps: 6 } );
    await page.mouse.move( lbl.x + 60, lbl.y + 200, { steps: 4 } );   // below the chart
    await page.mouse.up();
    await page.waitForTimeout( 80 );
    const dom = await chart.xDomain();
    const ev = await chart.lastEmit( 'xrangechanged' );
    expect( ev, 'xrangechanged emitted' ).not.toBeNull();
    expect( Math.abs( ev.args[0] - dom[0] ) ).toBeLessThan( 1e-6 );
    expect( Math.abs( ev.args[1] - dom[1] ) ).toBeLessThan( 1e-6 );
  });

  test('peak info ignores background peaks hidden by background subtraction', async ({ chart, page }) => {
    await page.evaluate( () => {
      const g = window.graph;
      const roi = JSON.parse( JSON.stringify( g.rawData.spectra[0].peaks[0] ) );   // 270-282 keV ROI
      roi.lowerEnergy += 230; roi.upperEnergy += 230; roi.referenceEnergy += 230;
      roi.peaks[0].Centroid[0] += 230;
      g.setRoiData( [roi], 'BACKGROUND' );
      g.setBackgroundSubtract( true );
      g.setXAxisRange( 480, 520 );
      g.redraw()();
    } );
    const p = await chart.client( 506.2, { yFrac: 0.5 } );
    await page.mouse.move( p.x, p.y, { steps: 3 } );
    await page.waitForTimeout( 50 );
    expect( await page.evaluate( () => !!document.querySelector('#chart1 .peakInfo') ) ).toBe( false );
  });

  test('construction without options, and missing ids/titles/localizations get defaults', async ({ chart, page }) => {
    const r = await page.evaluate( () => {
      const out = {};
      const d = document.createElement('div');
      d.id = 'chart3'; d.style.width = '500px'; d.style.height = '300px';
      document.body.appendChild( d );
      try { new SpectrumChartD3( 'chart3' ); out.noOptions = 'ok'; } catch( e ){ out.noOptions = String(e); }

      const g = window.graph;
      out.candidates = typeof g.options.txt.candidates + ',' + typeof g.options.txt.useArrowsToSelect;

      const s = { x: g.rawData.spectra[0].x, y: g.rawData.spectra[0].y, title: 'Sec2' };
      g.setSpectrumData( { spectra: [s] }, false, 'SECONDARY' );
      out.idType = typeof s.id;

      g.setXAxisTitle( '' );
      g.setLocalizations( { xAxisTitle: 'Energy 2' } );
      const t = document.querySelector('#chart1 .xaxistitle');
      out.xtitle = t ? t.textContent : null;
      return out;
    } );
    expect( r ).toEqual( { noOptions: 'ok', candidates: 'string,string', idType: 'number', xtitle: 'Energy 2' } );
  });

  test('reference lines draw even when the first group has no lines', async ({ chart, page }) => {
    const n = await page.evaluate( () => {
      window.graph.setReferenceLines( [ { parent: 'Empty', color: 'red', lines: [] },
                                        { parent: 'Ba133', color: 'blue', lines: [ { e: 356.0, h: 1, particle: 'gamma' } ] } ] );
      return document.querySelectorAll('#chart1 g.ref').length;
    } );
    expect( n ).toBe( 1 );
  });

  test('the width reserved for y-scale sliders matches the sliders drawn', async ({ chart, page }) => {
    const r = await page.evaluate( () => {
      const g = window.graph, s0 = g.rawData.spectra[0];
      const mk = (type) => ({ x: s0.x, y: s0.y, type: type, title: 'T' + type, yScaleFactor: 1 });
      g.setShowSpectrumScaleFactorWidget( true );
      // No foreground (the first spectrum gets no slider), and an untyped spectrum (sliders identify by type)
      return [ [mk('SECONDARY'), mk('BACKGROUND')], [mk('FOREGROUND'), mk(undefined), mk('BACKGROUND')] ].map( (spectra) => {
        g.setData( { spectra: spectra }, true );
        return [ g.size.nYScalers, document.querySelectorAll('#chart1 .scalerwidget g[id$="SliderArea"]').length ];
      } );
    } );
    expect( r ).toEqual( [ [1, 1], [1, 1] ] );
  });

  test('a zoom-in drag within one channel shows that channel; one across a channel edge shows the selection', async ({ chart, page }) => {
    // Channels [a,b] and [b,c], each ~100 px wide
    const [a, b, c] = await page.evaluate( () => {
      const x = window.graph.rawData.spectra[0].x, k = Math.floor( x.length/3 );
      window.graph.setXAxisRange( x[k-3], x[k+3], false, false );
      window.graph.redraw()();
      return [ x[k-1], x[k], x[k+1] ];
    } );
    const w = b - a;
    await chart.mouseDrag( a + 0.55*w, a + 0.8*w );  // x1 + (x1 - x0) is past b
    let dom = await chart.xDomain();
    expect( Math.abs( dom[0] - (a + 0.01*w) ) ).toBeLessThan( 1e-6*w );
    expect( Math.abs( dom[1] - (b - 0.01*w) ) ).toBeLessThan( 1e-6*w );

    await page.evaluate( (r) => { window.graph.setXAxisRange( r[0], r[1], false, false ); window.graph.redraw()(); }, [a - 2*w, c + 2*w] );
    await chart.mouseDrag( a + 0.8*w, b + 0.1*w );   // spans the a|b edge
    dom = await chart.xDomain();
    expect( Math.abs( dom[0] - (a + 0.8*w) ) ).toBeLessThan( 0.03*w );
    expect( Math.abs( dom[1] - (b + 0.1*w) ) ).toBeLessThan( 0.03*w );
  });

  test('turning zoom animation off part way through a zoom still finishes it', async ({ chart, page }) => {
    await page.evaluate( () => { window.graph.setAnimationDuration( 400 ); window.graph.setShowAnimation( true ); } );
    await chart.mouseDrag( 600, 900 );   // returns ~80 ms after mouse-up, mid-animation
    await page.evaluate( () => window.graph.setShowAnimation( false ) );
    await page.waitForTimeout( 600 );
    const dom = await chart.xDomain();
    const ev = await chart.lastEmit( 'xrangechanged' );
    expect( ev, 'xrangechanged emitted at the end' ).not.toBeNull();
    expect( Math.abs( ev.args[0] - dom[0] ) ).toBeLessThan( 1e-6 );
    expect( Math.abs( ev.args[1] - dom[1] ) ).toBeLessThan( 1e-6 );
    expect( Math.abs( dom[1] - dom[0] - 300 ) ).toBeLessThan( 15 );
    expect( await chart.errors() ).toEqual( [] );
  });
});
