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
