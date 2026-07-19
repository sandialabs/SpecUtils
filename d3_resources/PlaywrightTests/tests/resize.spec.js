// PERF-03: handleResize() no longer runs the full setData teardown/rebuild - a pure
// geometry change re-renders through redraw() and must REUSE the existing spectrum
// path nodes.  Driven the way the app drives it: the C++ ResizeObserver resizes the
// container then calls chart.handleResize().
const { test, expect } = require('../lib/harness');

async function resizeChart( page, w, h ){
  await page.evaluate( a => {
    const el = document.getElementById('chart1');
    el.style.width = a.w + 'px';
    el.style.height = a.h + 'px';
    window.graph.handleResize();
  }, { w, h } );
  await page.waitForTimeout( 100 );
}

test.describe('resize', () => {
  test('container resize re-renders without re-creating spectrum paths', async ({ chart, page }) => {
    const before = await page.evaluate( () => {
      const el = document.getElementById('spectrumline0');
      el.__perf03tag = 1;                       // node-identity marker
      return { w: window.graph.size.width, d: el.getAttribute('d') };
    } );

    await resizeChart( page, 760, 430 );

    const after = await page.evaluate( () => {
      const el = document.getElementById('spectrumline0');
      return { w: window.graph.size.width, d: el.getAttribute('d'),
               sameNode: el.__perf03tag === 1,
               nPaths: document.querySelectorAll('.speclinepath').length };
    } );

    expect( after.w, 'chart width should shrink' ).toBeLessThan( before.w );
    expect( after.d, 'spectrum path should re-render' ).not.toBe( before.d );
    expect( after.sameNode, 'path node must be reused, not re-created' ).toBeTruthy();
    expect( after.nPaths ).toBe( 3 );           // foreground + background + secondary
    // Energy<->pixel mapping still consistent at the new size.
    const e = await chart.energyAt( (await chart.client( 661, {} )).x );
    expect( Math.abs(e - 661) ).toBeLessThan( 2 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('resize with the slider chart shown keeps slider lines + box', async ({ chart, page }) => {
    await page.evaluate( () => window.graph.setShowXAxisSliderChart( true ) );
    await page.waitForTimeout( 50 );
    expect( await chart.boxRect( '#sliderLine0' ), 'slider line before' ).not.toBeNull();

    await resizeChart( page, 820, 470 );

    expect( await chart.boxRect( '#sliderLine0' ), 'slider line after resize' ).not.toBeNull();
    expect( await chart.boxRect( '.sliderBox' ), 'slider box after resize' ).not.toBeNull();
    expect( await chart.errors() ).toEqual( [] );
  });

  test('setLocalizations refreshes legend text (was piggybacking on setData)', async ({ chart, page }) => {
    await page.evaluate( () => window.graph.setShowLegend( true ) );
    await page.waitForTimeout( 50 );
    const legendHas = t => page.evaluate(
      s => { const el = document.querySelector('.legend'); return !!el && el.textContent.indexOf(s) >= 0; }, t );
    expect( await legendHas('Live Time'), 'default legend label' ).toBeTruthy();

    await page.evaluate( () => window.graph.setLocalizations( { liveTime: 'LT-X' } ) );
    await page.waitForTimeout( 100 );

    expect( await legendHas('LT-X'), 'localized legend label' ).toBeTruthy();
    expect( await chart.errors() ).toEqual( [] );
  });
});
