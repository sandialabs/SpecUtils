const { test, expect } = require('../lib/harness');

// Sanity: the standalone chart boots, loads the Ba-133 fixture, and exposes helpers.
test.describe('smoke', () => {
  test('harness loads chart + data with no errors', async ({ chart, page }) => {
    const loadError = await page.evaluate( () => window.__loadError );
    expect( loadError, 'fixture should load' ).toBeNull();

    expect( await chart.errors() ).toEqual( [] );

    const range = await chart.fullXRange();
    expect( range[0] ).toBeLessThan( range[1] );
    expect( range[1] ).toBeGreaterThan( 1000 );   // Ba-133 file spans ~0..3000 keV

    const peaks = await chart.peakEnergies();
    expect( peaks.length ).toBeGreaterThan( 0 );
    // Ba-133 276 keV peak should be in the list.
    expect( peaks.some( e => Math.abs(e - 276.2) < 1 ) ).toBeTruthy();
  });

  test('energy<->pixel mapping round-trips', async ({ chart }) => {
    const e0 = 661;
    const p = await chart.client( e0, { yFrac: 0.4 } );
    expect( p.x ).toBeGreaterThan( 0 );
    const back = await chart.energyAt( p.x );
    expect( Math.abs( back - e0 ) ).toBeLessThan( 1 );
  });

  // PERF-07: the grids update in place (no remove/re-insert); tick nodes are reused by
  // value, so minor/major classes are re-stamped and must never go stale or duplicate.
  test('grid updates in place across zooms with correct minor/major styling', async ({ chart, page }) => {
    await page.evaluate( () => { window.graph.setGridX( true ); window.graph.setGridY( true ); } );
    await page.waitForTimeout( 60 );
    await chart.mouseDrag( 300, 700, { yFrac: 0.5 } );    // zoom -> tick sets change, nodes reused
    const info = await page.evaluate( () => {
      const res = { bad: 0, major: 0, minor: 0, dupes: 0, majColor: null, minColor: null };
      ['xgrid','ygrid'].forEach( cls => {
        const seen = {};
        document.querySelectorAll( 'g.' + cls + ' > g' ).forEach( t => {
          const c = t.getAttribute('class');
          if( c === 'tick' ){ res.major++; res.majColor = getComputedStyle(t).stroke; }
          else if( c === 'tick minorgrid' ){ res.minor++; res.minColor = getComputedStyle(t).stroke; }
          else res.bad++;
          const key = cls + ':' + t.__data__;
          if( seen[key] ) res.dupes++;
          seen[key] = true;
        } );
      } );
      return res;
    } );
    expect( info.bad, 'every grid tick classed tick or tick minorgrid' ).toBe( 0 );
    expect( info.major ).toBeGreaterThan( 0 );
    expect( info.minor ).toBeGreaterThan( 0 );
    expect( info.dupes, 'no duplicated tick nodes after in-place updates' ).toBe( 0 );
    expect( info.minColor, 'minor color differs from major' ).not.toBe( info.majColor );
    expect( await chart.errors() ).toEqual( [] );
  });
});
