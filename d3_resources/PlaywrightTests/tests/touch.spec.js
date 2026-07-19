const { test, expect } = require('../lib/harness');

// Touch / phone suite. All multi-touch is synthesized through the CDP (genuine TouchEvents).
// Gestures are recognized by finger count + direction + movement thresholds (see
// handleVisTouchMove in SpectrumChartD3.js). We assert emits where a handler reports to the
// server, and axis BOUNDS where it doesn't (touch pan/zoom mutate the domain locally without
// emitting an xrangechanged).

test.describe('touch: one finger', () => {
  test('pan shifts the x-domain (bounds), no error', async ({ chart }) => {
    // Zoom in first so there's room to pan.
    await chart.pinch( 1000, 60, 320, 'x' );
    await chart.reset();
    const before = await chart.xDomain();

    // One finger drags left->right (content pans right, domain shifts down in energy).
    await chart.oneFingerDrag( 1100, 800, { yFrac: 0.4 } );
    const after = await chart.xDomain();

    const shifted = Math.abs(after[0] - before[0]) > 1 || Math.abs(after[1] - before[1]) > 1;
    expect( shifted, 'pan should move the x-domain' ).toBeTruthy();
    // Width preserved (pan, not zoom).
    expect( (after[1]-after[0]) ).toBeCloseTo( (before[1]-before[0]), 0 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('tap emits leftclicked', async ({ chart }) => {
    await chart.tap( 661, { yFrac: 0.5 } );
    expect( (await chart.emits('leftclicked')).length ).toBeGreaterThan( 0 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('double-tap emits doubleclicked', async ({ chart }) => {
    await chart.doubleTap( 661, { yFrac: 0.5 } );
    expect( (await chart.emits('doubleclicked')).length ).toBeGreaterThan( 0 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('long-press emits rightclicked', async ({ chart }) => {
    await chart.longPress( 661, { yFrac: 0.5 } );
    const rc = await chart.emits('rightclicked');
    expect( rc.length, 'long-press should emit rightclicked' ).toBeGreaterThan( 0 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('BUG-09: taps do not leak pageX/pageY to global scope', async ({ chart }) => {
    await chart.tap( 500, { yFrac: 0.5 } );
    await chart.tap( 800, { yFrac: 0.5 } );
    expect( await chart.globalLeaked('pageX') ).toBe( false );
    expect( await chart.globalLeaked('pageY') ).toBe( false );
  });
});

test.describe('touch: two-finger zoom (bounds)', () => {
  test('horizontal spread zooms x in (narrower domain)', async ({ chart }) => {
    const before = await chart.xDomain();
    await chart.pinch( 1000, 80, 360, 'x' );      // spread apart -> zoom in
    const after = await chart.xDomain();
    expect( (after[1]-after[0]), 'x-domain should narrow' ).toBeLessThan( before[1]-before[0] );
    expect( await chart.errors() ).toEqual( [] );
  });

  // BUG-25: the guide lines must track the fingers, so the committed zoom range is the
  // FINAL finger span (pre-fix they froze at the initial positions), and the in/out
  // decision must not mix page- and vis-coordinates.
  test('BUG-25: vertical spread zooms y to the final finger span', async ({ chart }) => {
    const before = await chart.yDomain();
    // pinch() ends with the fingers 320px apart around the yFrac=0.45 row: find the
    // counts at those final rows with the PRE-zoom scale; after the zoom they should
    // sit at the top/bottom edges of the plot.
    const c = await chart.client( 1000, { yFrac: 0.45 } );
    const expTop = await chart.countAt( c.y - 160 );
    const expBot = await chart.countAt( c.y + 160 );
    await chart.pinch( 1000, 60, 320, 'y' );
    const after = await chart.yDomain();
    const changed = Math.abs(after[0]-before[0]) > 1e-6 || Math.abs(after[1]-before[1]) > 1e-6;
    expect( changed, 'y-domain should change' ).toBeTruthy();
    const edges = await chart.page.evaluate(
      v => [ window.graph.yScale(v[0]), window.graph.yScale(v[1]), window.graph.size.height ],
      [expTop, expBot] );
    expect( Math.abs(edges[0]), 'final top-finger count at plot top' ).toBeLessThan( 8 );
    expect( Math.abs(edges[1] - edges[2]), 'final bottom-finger count at plot bottom' ).toBeLessThan( 8 );
    expect( await chart.errors() ).toEqual( [] );
  });

  test('BUG-25: vertical pinch-together zooms y back out to the full range', async ({ chart }) => {
    await chart.pinch( 1000, 60, 320, 'y' );        // spread -> zoom in first
    const zoomed = await chart.yDomain();
    await chart.pinch( 1000, 320, 60, 'y' );        // pinch together -> zoom out
    const after = await chart.yDomain();
    const full = await chart.page.evaluate( () => window.graph.getYAxisDomain() );
    expect( Math.abs(after[0]-zoomed[0]) > 1e-6 || Math.abs(after[1]-zoomed[1]) > 1e-6,
            'pinch-together should change the y-domain' ).toBeTruthy();
    expect( Math.abs(after[0]-full[0]) / Math.max(1, Math.abs(full[0])),
            'zoom-out should restore the full y range (max)' ).toBeLessThan( 0.01 );
    expect( Math.abs(after[1]-full[1]) / Math.max(1e-6, Math.abs(full[1])),
            'zoom-out should restore the full y range (min)' ).toBeLessThan( 0.01 );
    expect( await chart.errors() ).toEqual( [] );
  });
});

test.describe('touch: two-finger peak fit (BUG-04)', () => {
  test('two fingers swiping right emit fitRoiDrag (live); mode resets after lift', async ({ chart }) => {
    // Both fingers move right ~200 keV (~60 px), staying level, constant separation.
    await chart.twoFingerDrag(
      { fromEnergy: 800, toEnergy: 1000 },
      { fromEnergy: 920, toEnergy: 1120 },     // ~120 keV apart, constant
      { yFrac: 0.45, steps: 12 }
    );
    const fit = await chart.emits('fitRoiDrag');
    expect( fit.length, 'two-finger swipe should emit fitRoiDrag' ).toBeGreaterThan( 0 );
    // Live drag emits carry isFinal=false at arg index 3.
    expect( fit.some( e => e.args[3] === false ) ).toBeTruthy();
    // Not left stuck on 'fitPeak' after the fingers lift.
    expect( await chart.dragMode() ).toBe( 'none' );
    expect( await chart.errors() ).toEqual( [] );
  });

  // BUG-04: the keep-peak menu position. The completion emit (handleMouseUpPeakFit) only runs
  // once the server has answered the live drag by setting roiBeingDrugUpdate; this server-less
  // harness simulates that response mid-gesture, then lifts the fingers. The fix takes the page
  // coords from the lifted finger's changedTouches[0] (not the vis-relative peakFitTouchMove).
  test('BUG-04: completion fitRoiDrag carries the lifted finger page coords', async ({ chart }) => {
    const x1000 = (await chart.client( 1000, { yFrac: 0.45 } )).x;
    const x1120 = (await chart.client( 1120, { yFrac: 0.45 } )).x;

    await chart.twoFingerDragWithMid(
      { fromEnergy: 800, toEnergy: 1000 },
      { fromEnergy: 920, toEnergy: 1120 },
      { yFrac: 0.45, steps: 12 },
      async () => {
        expect( await chart.dragMode(), 'gesture should be in fitPeak mode before lift' ).toBe( 'fitPeak' );
        await chart.setFakeFitRoi( 970, 1090, 1 );   // "server" defines the ROI
      }
    );

    const complete = (await chart.emits('fitRoiDrag')).filter( e => e.args.length >= 6 && e.args[3] === true );
    expect( complete.length, 'a final fitRoiDrag should fire once the ROI is defined' ).toBeGreaterThan( 0 );
    const last = complete[complete.length - 1];
    const pageX = last.args[4], pageY = last.args[5];
    // pageX must be a real page coordinate near where a finger lifted (~1000..1120 keV),
    // NOT a small vis-relative value (the pre-fix behavior).
    expect( pageX ).toBeGreaterThan( Math.min(x1000, x1120) - 30 );
    expect( pageX ).toBeLessThan( Math.max(x1000, x1120) + 30 );
    expect( pageY ).toBeGreaterThan( 0 );
    expect( await chart.errors() ).toEqual( [] );
  });
});

test.describe('touch: two-finger delete-peak swipe', () => {
  test('vertical two-finger swipe emits shiftkeydragged', async ({ chart }) => {
    // Two fingers level, ~150 keV apart in x, swipe UP ~50 px together (x held constant).
    await chart.twoFingerDrag(
      { fromEnergy: 300, toEnergy: 300 },
      { fromEnergy: 450, toEnergy: 450 },
      { yFrac: 0.55, dyPx: -55, steps: 12 }
    );
    const del = await chart.emits('shiftkeydragged');
    expect( del.length, 'vertical two-finger swipe should emit shiftkeydragged' ).toBeGreaterThan( 0 );
    expect( await chart.errors() ).toEqual( [] );
  });
});

test.describe('touch: two-finger count-gammas swipe', () => {
  // The reachable gesture is two fingers STACKED VERTICALLY (~same energy/x, one above the
  // other) swiping right. The vertical separation pushes each finger's y far from the two-finger
  // average, which disqualifies controlDragSwipe (peak fit) and lets isAltShiftSwipe win.
  test('vertically-stacked fingers swiping right emit shiftaltkeydragged', async ({ chart }) => {
    await chart.twoFingerDrag(
      { fromEnergy: 600, toEnergy: 780 },
      { fromEnergy: 600, toEnergy: 780 },   // same energies -> same x as finger 1
      { yFrac: 0.20, f2YOffsetPx: 150, steps: 14 }   // finger 2 sits ~150 px below finger 1
    );
    const cg = await chart.emits('shiftaltkeydragged');
    expect( cg.length, 'count-gammas swipe should emit shiftaltkeydragged' ).toBeGreaterThan( 0 );
    // It must NOT be misread as a peak fit.
    expect( (await chart.emits('fitRoiDrag')).length ).toBe( 0 );
    // args [e0,e1] ~ the swept energy range.
    const last = cg[cg.length - 1];
    expect( last.args[0] ).toBeGreaterThan( 560 );
    expect( last.args[0] ).toBeLessThan( 640 );
    expect( last.args[1] ).toBeGreaterThan( 740 );
    expect( last.args[1] ).toBeLessThan( 820 );
    expect( await chart.errors() ).toEqual( [] );
  });
});

test.describe('touch: one-finger pan amount (bounds)', () => {
  test('pan shifts the domain by the dragged energy, width preserved', async ({ chart }) => {
    await chart.pinch( 1000, 60, 360, 'x' );    // zoom in so the pan is precise
    await chart.reset();
    const before = await chart.xDomain();

    // Drag the finger from where 1000 keV sits to where 950 keV sits (50 keV of leftward travel).
    await chart.oneFingerDrag( 1000, 950, { yFrac: 0.35 } );
    const after = await chart.xDomain();

    expect( (after[1]-after[0]) ).toBeCloseTo( (before[1]-before[0]), 0 );  // width preserved
    // Dragging content left shows higher energies: domain shifts up by ~the dragged amount.
    expect( after[0] - before[0] ).toBeGreaterThan( 0 );
    expect( Math.abs( (after[0] - before[0]) - 50 ) ).toBeLessThan( 8 );
    expect( await chart.errors() ).toEqual( [] );
  });
});
