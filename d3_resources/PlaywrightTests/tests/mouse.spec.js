const { test, expect } = require('../lib/harness');

// Moused-device suite. Each gesture is a modifier-key left-drag (leftDragMode is chosen at
// mousedown). We assert the server-bound emit (Wt.emit stub), the resulting axis bounds, the
// transient drag-box geometry, and a clean error list.
//
// Coordinates are spectrum energies; the harness converts to exact pixels via the chart scales.

test.describe('mouse: x-axis zoom (no modifier)', () => {
  test('drag-right zooms in, emits xrangechanged, narrows x-domain', async ({ chart }) => {
    const before = await chart.xDomain();
    await chart.mouseDrag( 500, 900, { yFrac: 0.5 } );

    const emit = await chart.lastEmit( 'xrangechanged' );
    expect( emit, 'xrangechanged should fire' ).not.toBeNull();
    // args: [x0, x1, width, height, userAction]
    expect( emit.args[0] ).toBeGreaterThan( before[0] );
    expect( emit.args[1] ).toBeLessThan( before[1] );
    expect( emit.args[0] ).toBeGreaterThanOrEqual( 480 );
    expect( emit.args[1] ).toBeLessThanOrEqual( 920 );
    expect( typeof emit.args[4] ).toBe( 'boolean' );   // userAction flag present

    const after = await chart.xDomain();
    expect( after[1] - after[0] ).toBeLessThan( before[1] - before[0] );
    expect( await chart.errors() ).toEqual( [] );
  });

  // BUG-03: the 75 ms anti-accidental-zoom debounce. Driven with synthetic events so the
  // mousedown->mouseup interval is controlled exactly: ~0 ms must NOT zoom, >75 ms must zoom.
  test('BUG-03: sub-75ms drag does NOT zoom, but a held drag does', async ({ chart }) => {
    const before = await chart.xDomain();

    await chart.synthDrag( 500, 900, { yFrac: 0.5, holdMs: 0 } );   // same tick -> suppressed
    const afterFast = await chart.xDomain();
    expect( Math.abs(afterFast[0] - before[0]) ).toBeLessThan( 1 );
    expect( Math.abs(afterFast[1] - before[1]) ).toBeLessThan( 1 );
    expect( await chart.emits('xrangechanged') ).toEqual( [] );
    expect( await chart.errors() ).toEqual( [] );

    await chart.reset();
    await chart.synthDrag( 500, 900, { yFrac: 0.5, holdMs: 130 } );  // held -> zooms
    const afterSlow = await chart.xDomain();
    expect( afterSlow[1] - afterSlow[0], 'held drag should zoom in' ).toBeLessThan( before[1] - before[0] );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('zoom box appears mid-drag spanning the swept range', async ({ chart }) => {
    const pLeft = await chart.client( 500, { yFrac: 0.5 } );
    const pRight = await chart.client( 900, { yFrac: 0.5 } );
    await chart.mouseDragWithMid( 500, 900, { yFrac: 0.5 }, async () => {
      const box = await chart.boxRect( '.leftbuttonzoombox' );
      expect( box, 'zoom box present during drag' ).not.toBeNull();
      // Box should roughly span from the start to the current x.
      expect( box.width ).toBeGreaterThan( 0.5 * (pRight.x - pLeft.x) );
    });
    // Box gone after release.
    expect( await chart.boxRect( '.leftbuttonzoombox' ) ).toBeNull();
  });
});

test.describe('mouse: y-axis zoom (Meta-drag)', () => {
  for( const anim of [false, true] ){
    test( `BUG-01: y-zoom changes y-domain without error (animation ${anim})`, async ({ chart, page }) => {
      await page.evaluate( on => window.graph.setShowAnimation( on ), anim );
      const before = await chart.yDomain();
      // Vertical Meta-drag spanning much of the plot height.
      await chart.mouseDragV( 661, 0.15, 0.85, { modifiers: ['Meta'], holdMs: 120 } );
      const after = await chart.yDomain();
      // Must complete without throwing (BUG-01) and leave a finite, changed y-domain.
      expect( Number.isFinite(after[0]) && Number.isFinite(after[1]) ).toBeTruthy();
      const changed = Math.abs(after[0]-before[0]) > 1e-6 || Math.abs(after[1]-before[1]) > 1e-6;
      expect( changed, 'y-domain should change on y-zoom' ).toBeTruthy();
      expect( await chart.errors() ).toEqual( [] );
    });
  }

  // BUG-26: partial zoom-out clamped to a hardcoded [0.1,3000] (dead rawData.y branch) and
  // centered on the domain MAX instead of the middle, shifting the window up a full range.
  test('BUG-26: x2 zoom-out doubles the range about the center, clamped to the data', async ({ chart, page }) => {
    // Zoom into a mid-plot band first so an x2 expansion has room on both sides.
    await chart.mouseDragV( 661, 0.30, 0.60, { modifiers: ['Meta'] } );
    const zoomed = await chart.yDomain();          // [max,min]

    const expected = await page.evaluate( () => {
      const d = window.graph.yScale.domain();      // [max,min]
      const full = window.graph.getYAxisDomain();  // [max,min]
      const R = Math.abs( d[0] - d[1] );
      const c = d[1] + 0.5*R;
      let y0 = c - R, y1 = y0 + 2*R;
      if( y0 < full[1] ) y1 += (full[1] - y0);
      if( y1 > full[0] ) y0 -= (y1 - full[0]);
      return [ Math.min(y1, full[0]), Math.max(y0, full[1]) ];
    });

    // A small upward Meta-drag (>10 px but <5% of the plot height) selects "x2".
    const H = await page.evaluate( () => window.graph.size.height );
    const dyFrac = Math.max( 12/H, 0.03 );
    await chart.mouseDragV( 661, 0.60, 0.60 - dyFrac, { modifiers: ['Meta'] } );
    const after = await chart.yDomain();

    expect( after[0], 'max should not shrink on zoom-out' ).toBeGreaterThanOrEqual( zoomed[0] - 1e-9 );
    expect( after[1], 'min should not grow on zoom-out' ).toBeLessThanOrEqual( zoomed[1] + 1e-9 );
    expect( Math.abs(after[0]-expected[0]) / Math.max(1, Math.abs(expected[0])) ).toBeLessThan( 0.02 );
    expect( Math.abs(after[1]-expected[1]) / Math.max(1, Math.abs(expected[1])) ).toBeLessThan( 0.02 );
    expect( await chart.errors() ).toEqual( [] );
  });
});

test.describe('mouse: peak fit (Ctrl-drag)', () => {
  test('fitRoiDrag emitted; leftDragMode resets after release', async ({ chart }) => {
    // Drag across the 356 keV Ba-133 peak region.
    await chart.mouseDrag( 345, 368, { modifiers: ['Control'], yFrac: 0.6, holdMs: 120 } );
    const fit = await chart.emits( 'fitRoiDrag' );
    expect( fit.length, 'a fit drag should be emitted' ).toBeGreaterThan( 0 );
    expect( await chart.dragMode() ).toBe( 'none' );  // not stuck on 'fitPeak'
    expect( await chart.errors() ).toEqual( [] );
  });
});

test.describe('mouse: delete peaks (Shift-drag)', () => {
  test('shiftkeydragged emitted; deletePeaksBox spans the range', async ({ chart }) => {
    const pa = await chart.client( 300, { yFrac: 0.5 } );
    const pb = await chart.client( 400, { yFrac: 0.5 } );
    await chart.mouseDragWithMid( 300, 400, { modifiers: ['Shift'], yFrac: 0.5, holdMs: 90 }, async () => {
      const box = await chart.boxRect( '#deletePeaksBox' );
      expect( box, 'deletePeaksBox present during shift-drag' ).not.toBeNull();
      expect( box.width ).toBeGreaterThan( 0.5 * (pb.x - pa.x) );
    });
    const emit = await chart.lastEmit( 'shiftkeydragged' );
    expect( emit, 'shiftkeydragged should fire' ).not.toBeNull();
    // args [e0,e1] roughly the swept energies.
    expect( emit.args[0] ).toBeGreaterThan( 270 );
    expect( emit.args[1] ).toBeLessThan( 430 );
    expect( await chart.errors() ).toEqual( [] );
  });
});

test.describe('mouse: count gammas (Shift+Alt-drag)', () => {
  test('shiftaltkeydragged emitted; countGammasBox present', async ({ chart }) => {
    await chart.mouseDragWithMid( 600, 720, { modifiers: ['Shift','Alt'], yFrac: 0.5, holdMs: 90 }, async () => {
      const box = await chart.boxRect( '#countGammasBox' );
      expect( box, 'countGammasBox present during shift+alt-drag' ).not.toBeNull();
    });
    const emit = await chart.lastEmit( 'shiftaltkeydragged' );
    expect( emit, 'shiftaltkeydragged should fire' ).not.toBeNull();
    expect( await chart.errors() ).toEqual( [] );
  });
});

test.describe('mouse: energy recalibrate (Ctrl+Alt-drag)', () => {
  test('rightmousedragged emitted', async ({ chart }) => {
    await chart.mouseDrag( 356, 380, { modifiers: ['Control','Alt'], yFrac: 0.5, holdMs: 120 } );
    const emit = await chart.emits( 'rightmousedragged' );
    expect( emit.length, 'recalibrate should emit rightmousedragged' ).toBeGreaterThan( 0 );
    expect( await chart.errors() ).toEqual( [] );
  });
});

test.describe('mouse: wheel + clicks', () => {
  test('wheel over plot changes x-range', async ({ chart }) => {
    const before = await chart.xDomain();
    await chart.wheel( 661, -300, { yFrac: 0.5 } );   // wheel up = zoom in
    const after = await chart.xDomain();
    expect( (after[1]-after[0]) ).not.toBeCloseTo( (before[1]-before[0]), 1 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('single click emits leftclicked', async ({ chart }) => {
    await chart.click( 661, { yFrac: 0.5 } );
    const emit = await chart.emits( 'leftclicked' );
    expect( emit.length ).toBeGreaterThan( 0 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('double click emits doubleclicked', async ({ chart }) => {
    await chart.dblclick( 661, { yFrac: 0.5 } );
    const emit = await chart.emits( 'doubleclicked' );
    expect( emit.length ).toBeGreaterThan( 0 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('right-click emits rightclicked at the clicked energy with page coords', async ({ chart }) => {
    // Zoom in first so 1 px is a fraction of a keV (full range is ~3.4 keV/px).
    await chart.synthDrag( 600, 720, { yFrac: 0.5, holdMs: 130 } );
    await chart.reset();

    const target = 661;
    const p = await chart.client( target, { yFrac: 0.5 } );
    await chart.rightClick( target, { yFrac: 0.5 } );

    const rc = await chart.lastEmit( 'rightclicked' );
    expect( rc, 'rightclicked should fire' ).not.toBeNull();
    // args: [energy, count, pageX, pageY, refLineInfo]
    expect( Math.abs( rc.args[0] - target ) ).toBeLessThan( 2 );
    expect( rc.args[2] ).toBeGreaterThan( 0 );          // pageX
    expect( rc.args[3] ).toBeGreaterThan( 0 );          // pageY
    expect( Math.abs( rc.args[2] - p.x ) ).toBeLessThan( 3 );  // menu anchored at the click
    expect( await chart.errors() ).toEqual( [] );
  });
});

test.describe('mouse: bounds / amount correctness', () => {
  test('drag-zoom selects the swept energy range precisely', async ({ chart }) => {
    await chart.mouseDrag( 400, 800, { yFrac: 0.5, holdMs: 130 } );
    const d = await chart.xDomain();
    expect( Math.abs( d[0] - 400 ) ).toBeLessThan( 3 );
    expect( Math.abs( d[1] - 800 ) ).toBeLessThan( 3 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('right-drag pan shifts by the dragged energy, width preserved', async ({ chart }) => {
    await chart.synthDrag( 400, 800, { yFrac: 0.5, holdMs: 130 } );  // zoom to ~[400,800]
    await chart.reset();
    const before = await chart.xDomain();

    // Grab at 600 keV and drag the cursor right to where 650 keV sits (+50 keV of travel).
    await chart.rightDragPan( 600, 650, { yFrac: 0.5 } );
    const after = await chart.xDomain();

    // Pan preserves the visible width.
    expect( (after[1]-after[0]) ).toBeCloseTo( (before[1]-before[0]), 1 );
    // Dragging content right shows lower energies: domain shifts down by ~the dragged amount.
    expect( after[0] - before[0] ).toBeLessThan( 0 );
    expect( Math.abs( (after[0] - before[0]) - (-50) ) ).toBeLessThan( 4 );
    expect( await chart.errors() ).toEqual( [] );
  });
});

test.describe('mouse: coordinate readout', () => {
  test('bottom-right readout shows the energy under the cursor', async ({ chart, page }) => {
    await chart.synthDrag( 600, 720, { yFrac: 0.5, holdMs: 130 } );   // zoom in for sub-keV precision
    await chart.reset();

    const target = 661;
    const p = await chart.client( target, { yFrac: 0.45 } );
    await page.mouse.move( p.x - 25, p.y );
    await page.mouse.move( p.x, p.y, { steps: 4 } );

    const shown = await chart.readoutEnergy();
    expect( shown, `readout should show a keV value (lines: ${JSON.stringify(await chart.mouseCoordLines())})` ).not.toBeNull();
    expect( Math.abs( shown - target ) ).toBeLessThan( 2 );
  });
});
