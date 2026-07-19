// PERF-02: the x-axis slider chart's data lines are cached - ordinary pan/zoom redraws
// must NOT rebuild them (they always show the full data range), while changes to their
// real inputs (y-scale type, background subtract, scale factors, data, size) must.
const { test, expect } = require('../lib/harness');

async function showSlider( page ){
  await page.evaluate( () => window.graph.setShowXAxisSliderChart( true ) );
  await page.waitForTimeout( 60 );
}
const lineD = page => page.evaluate( () => {
  const el = document.getElementById('sliderLine0');
  return el ? el.getAttribute('d') : null;
} );

test.describe('x-axis slider chart', () => {
  test('PERF-02: redraws are cache hits - line node + path unchanged, box tracks view', async ({ chart, page }) => {
    await showSlider( page );
    const d0 = await lineD( page );
    expect( d0 ).not.toBeNull();
    await page.evaluate( () => { document.getElementById('sliderLine0').__cacheTag = 1; } );

    // Plain redraws (what every pan/zoom frame does) must not rebuild the slider lines.
    // Pre-fix, each of these ran two full-spectrum rebins and re-created every line node.
    await page.evaluate( () => { window.graph.redraw()(); window.graph.redraw()(); window.graph.redraw()(); } );

    const after = await page.evaluate( () => {
      const el = document.getElementById('sliderLine0');
      return { d: el.getAttribute('d'), tagged: el.__cacheTag === 1 };
    } );
    expect( after.tagged, 'slider line node reused across redraws' ).toBeTruthy();
    expect( after.d, 'slider line path unchanged across redraws' ).toBe( d0 );

    // The slider box still tracks the view.  (A zoom may legitimately rebuild the lines:
    // if the y-tick label width changes the plot width, the rebin that feeds them changes.)
    const box0 = await chart.boxRect( '.sliderBox' );
    await chart.mouseDrag( 400, 900, { yFrac: 0.4 } );
    const box1 = await chart.boxRect( '.sliderBox' );
    expect( Math.abs( box1.width - box0.width ) > 2 || Math.abs( box1.x - box0.x ) > 2,
            'slider box should track the view' ).toBeTruthy();
    expect( await chart.errors() ).toEqual( [] );
  });

  test('PERF-02: y-scale type, background subtract, scale factor and resize all invalidate', async ({ chart, page }) => {
    await showSlider( page );
    const d1 = await lineD( page );

    await page.evaluate( () => window.graph.setYAxisType('lin') );
    await page.waitForTimeout( 40 );
    const d2 = await lineD( page );
    expect( d2, 'lin y-scale rebuilds slider lines' ).not.toBe( d1 );

    await page.evaluate( () => window.graph.setBackgroundSubtract( true ) );
    await page.waitForTimeout( 40 );
    const d3 = await lineD( page );
    expect( d3, 'background subtract rebuilds slider lines' ).not.toBe( d2 );

    await page.evaluate( () => { window.graph.rawData.spectra[0].yScaleFactor = 2.0; window.graph.redraw()(); } );
    await page.waitForTimeout( 40 );
    const d4 = await lineD( page );
    expect( d4, 'scale factor rebuilds slider lines' ).not.toBe( d3 );

    await page.evaluate( () => {
      document.getElementById('chart1').style.width = '820px';
      window.graph.handleResize();
    } );
    await page.waitForTimeout( 80 );
    const d5 = await lineD( page );
    expect( d5, 'resize rebuilds slider lines' ).not.toBe( d4 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('slider box drag pans the chart: domain shifts, width preserved, emits', async ({ chart, page }) => {
    await chart.mouseDrag( 400, 900, { yFrac: 0.4 } );       // zoom in so the box is draggable
    await showSlider( page );
    await chart.reset();                                     // drop the zoom's emits
    const dom0 = await chart.xDomain();
    const box = await chart.boxRect( '.sliderBox' );
    const cy = box.y + box.height/2, cx = box.x + box.width/2;
    await page.mouse.move( cx, cy );
    await page.mouse.down();
    await page.mouse.move( cx + 120, cy, { steps: 8 } );
    await page.mouse.up();
    await page.waitForTimeout( 150 );
    const dom1 = await chart.xDomain();
    expect( dom1[0], 'domain should shift right' ).toBeGreaterThan( dom0[0] );
    expect( Math.abs( (dom1[1]-dom1[0]) - (dom0[1]-dom0[0]) ),
            'width preserved' ).toBeLessThan( 0.02*(dom0[1]-dom0[0]) );
    expect( (await chart.emits('xrangechanged')).length ).toBeGreaterThan( 0 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('toggling the slider off and on re-creates its lines', async ({ chart, page }) => {
    await showSlider( page );
    expect( await lineD( page ) ).not.toBeNull();
    await page.evaluate( () => window.graph.setShowXAxisSliderChart( false ) );
    await page.waitForTimeout( 60 );
    expect( await lineD( page ), 'lines removed with the slider' ).toBeNull();
    await showSlider( page );
    expect( await lineD( page ), 'lines rebuilt after re-show' ).not.toBeNull();
    expect( await chart.errors() ).toEqual( [] );
  });
});
