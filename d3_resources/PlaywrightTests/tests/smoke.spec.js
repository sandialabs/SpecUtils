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
});
