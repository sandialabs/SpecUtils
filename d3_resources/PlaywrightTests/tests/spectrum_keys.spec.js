// Holding B or S: double-click, Ctrl-drag peak fit, and Shift-drag erase act on the background or secondary.
const { test, expect } = require('../lib/harness');

test.describe('B/S spectrum keys', () => {
  test('double-click with B or S held reports the Alt or Shift bit', async ({ chart, page }) => {
    const lastMods = async () => { const e = await chart.lastEmit( 'doubleclicked' ); return e ? e.args[3] : null; };
    await chart.dblclick( 661 );
    expect( await lastMods() ).toBe( 0 );

    await page.keyboard.down( 'b' );
    await chart.dblclick( 661 );
    await page.keyboard.up( 'b' );
    expect( await lastMods() ).toBe( 0x04 );

    await page.keyboard.down( 's' );
    await chart.dblclick( 661 );
    await page.keyboard.up( 's' );
    expect( await lastMods() ).toBe( 0x01 );

    await page.waitForTimeout( 600 );   // past the double-click window
    await chart.dblclick( 661 );
    expect( await lastMods(), 'released keys no longer count' ).toBe( 0 );
  });

  test('Shift-drag with B held erases in the background, labelled so', async ({ chart, page }) => {
    let label = null;
    await page.keyboard.down( 'b' );
    await chart.mouseDragWithMid( 300, 400, { modifiers: ['Shift'] }, async () => {
      label = await page.evaluate( () => document.querySelector('#chart1 #deletePeaksText').textContent );
    } );
    await page.keyboard.up( 'b' );
    expect( label ).toBe( 'Will Erase Background Peaks In Range' );
    expect( (await chart.lastEmit( 'shiftkeydragged' )).args[2] ).toBe( 'BACKGROUND' );

    await chart.mouseDrag( 300, 400, { modifiers: ['Shift'] } );
    expect( (await chart.lastEmit( 'shiftkeydragged' )).args[2] ).toBe( 'FOREGROUND' );
  });

  test('S pressed during a Ctrl-drag fit targets the secondary, without reaching app shortcuts', async ({ chart, page }) => {
    await page.evaluate( () => {
      window.__ctrlS = 0;
      document.addEventListener( 'keydown', (e) => { if( e.ctrlKey && (e.key === 's') ) ++window.__ctrlS; }, true );
      window.graph.setXAxisRange( 300, 420 );
      window.graph.redraw()();
    } );
    const a = await chart.client( 340, { yFrac: 0.5 } ), b = await chart.client( 370, { yFrac: 0.5 } );
    await page.keyboard.down( 'Control' );
    await page.mouse.move( a.x, a.y );
    await page.mouse.down();
    await page.mouse.move( b.x - 20, b.y, { steps: 4 } );
    await page.keyboard.down( 's' );
    await page.mouse.move( b.x, b.y, { steps: 3 } );
    await chart.setFakeFitRoi( 340, 370, 1 );
    await page.mouse.up();
    await page.keyboard.up( 's' );
    await page.keyboard.up( 'Control' );
    await page.waitForTimeout( 80 );

    const final = (await chart.emits( 'fitRoiDrag' )).filter( (e) => e.args[3] === true );
    expect( final.length ).toBe( 1 );
    expect( final[0].args[6] ).toBe( 'SECONDARY' );
    expect( await page.evaluate( () => window.__ctrlS ), 'Ctrl+S mid-drag is not passed on' ).toBe( 0 );

    // Outside a drag, Ctrl+S is left alone (synthetic, so the browser takes no default action)
    await page.evaluate( () => document.body.dispatchEvent( new KeyboardEvent( 'keydown', { key: 's', ctrlKey: true, bubbles: true } ) ) );
    expect( await page.evaluate( () => window.__ctrlS ) ).toBe( 1 );
  });

  test('B typed into a text field does not count', async ({ chart, page }) => {
    await page.evaluate( () => { const i = document.createElement('input'); i.id = 'txt'; document.body.appendChild( i ); i.focus(); } );
    await page.keyboard.down( 'b' );
    const held = await page.evaluate( () => window.graph.heldSpectrumKey );
    await page.keyboard.up( 'b' );
    expect( held ).toBeNull();
  });
});
