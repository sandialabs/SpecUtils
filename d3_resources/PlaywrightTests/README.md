# SpectrumChartD3 Playwright tests

Automated regression tests for the **mouse and touch interactions** in
`d3_resources/SpectrumChartD3.js`, driven against the chart running **standalone** (no Wt
server / no InterSpec). They are the executable companion to the manual checklist in
`SpectrumChartD3_manual_test_guide.md`.

## What they check

Two complementary assertion surfaces:

1. **`Wt.emit` signals.** The harness installs a `window.Wt` stub that records every
   `WtEmit(...)` the chart makes. Each gesture is asserted to produce the right
   server-bound signal with sane arguments — `xrangechanged`, `fitRoiDrag`,
   `shiftkeydragged` (delete peaks), `shiftaltkeydragged` (count gammas),
   `rightmousedragged` (recalibrate), `leftclicked` / `rightclicked` / `doubleclicked`.
2. **Axis bounds + DOM geometry.** Where a gesture doesn't emit (touch pan/zoom mutate the
   x/y domain locally), we assert the resulting `xScale`/`yScale` domain. Transient drag
   boxes (`#deletePeaksBox`, `#countGammasBox`, `.leftbuttonzoombox`) are checked for
   presence and span. A clean uncaught-error list is asserted throughout (several recent
   fixes were "stop throwing").

Coverage maps to the recent change batch (`BUG-01`…`BUG-24`, the mouse/touch-unifying
`REF-*` refactors). Notably tested: **BUG-03** (75 ms anti-accidental-zoom debounce, both
directions), **BUG-01** (y-zoom with animation on/off), **BUG-04** (touch peak-fit keep-menu
page coords), **BUG-09** (no `pageX`/`pageY` leaked to global scope).

Also covered, beyond the bug fixes:
- **Right-click** (`rightclicked` with the clicked energy + page coords for the context menu).
- **The bottom-right coordinate readout** (`g.mouseInfo`) showing the energy under the cursor.
- **Amount/bounds correctness**: a drag-zoom lands on exactly the swept energy range; panning
  (mouse right-drag and one-finger touch) shifts the domain by the dragged energy and preserves
  the visible width.
- **Touch count-gammas**: two fingers stacked vertically (one above the other, ~same energy)
  swiping right → `shiftaltkeydragged`.

## Setup & run

One-time install (downloads the Chromium build Playwright drives):

```bash
cd external_libs/SpecUtils/d3_resources/PlaywrightTests
npm run setup            # = npm install + npx playwright install chromium
```

Then run:

```bash
npm test                 # everything (mouse + touch projects), headless
npm run test:mouse       # moused-device suite only
npm run test:touch       # touch/phone suite only

# focus / filter:
npx playwright test -g "BUG-04"             # only tests whose title matches
npx playwright test tests/mouse.spec.js     # one file
```

`npm test` auto-starts the bundled static server (`static-server.js`, serving the
`d3_resources/` directory) on port 8125 via Playwright's `webServer`; you don't start it
yourself. To poke the harness by hand instead: `npm run serve`, then open
<http://127.0.0.1:8125/PlaywrightTests/harness/harness.html> and drive the chart in your own
browser (the dev-tools console has `window.emits()`, `window.xDomain()`, etc.).

## Watching / inspecting the tests

Several ways, from most interactive to lightweight:

```bash
# 1. UI mode — the best way to watch & inspect: a time-travel browser where you can step
#    through each action, see DOM snapshots before/after, and re-run individual tests.
npx playwright test --ui

# 2. Headed — watch a real Chromium window perform the gestures in real time.
npx playwright test --headed
npx playwright test --headed --project=touch --workers=1     # one at a time, touch suite
PWDEBUG=1 npx playwright test -g "two-finger peak fit"        # headed + Playwright Inspector, pauses so you can step

# 3. Slow it down so the gestures are watchable by eye — add slowMo to the launch options
#    (one-off, no config edit needed):
#    in playwright.config.js, use: { launchOptions: { slowMo: 400 } }   // ms between actions

# 4. After a run: open the HTML report (includes a trace viewer for any failure).
npm run report
npx playwright show-trace test-results/<...>/trace.zip        # time-travel a specific run
```

`trace: 'retain-on-failure'` is already set, so any failing test records a full trace
(screenshots, DOM snapshots, console, network) viewable with the trace viewer. Use
`--trace on` to record traces for passing tests too.

## How it works

```
PlaywrightTests/
├── harness/harness.html   standalone page: d3 + SpectrumChartD3.js + the Ba-133 fixture,
│                          a Wt.emit stub, an error collector, and test helpers on window
├── lib/harness.js         Playwright `test` fixture + the `chart` driver (mouse + touch gestures)
├── lib/touch.js           CDP multi-touch primitives (Input.dispatchTouchEvent)
├── static-server.js       dependency-free static file server rooted at d3_resources/
├── playwright.config.js   `mouse` and `touch` projects + webServer
└── tests/                 smoke.spec.js, mouse.spec.js, touch.spec.js
```

Key design points:

- **Data fixture.** Loads the real `../../example_json_with_peaks.json` (Ba-133 FOREGROUND +
  BACKGROUND, real peaks/ROIs at known energies) and adds a SECONDARY clone, so legend/scaler
  paths have three spectra.
- **Energy-based coordinates.** Tests address the chart in **spectrum energies (keV)**, never
  raw pixels. `window.toClient(energy)` converts via the vis group's `getScreenCTM()` and the
  chart's own `xScale`/`yScale`, so tests are independent of chart size, margins and scroll.
- **Mouse** uses Playwright's native mouse + keyboard (modifier-key drags pick `leftDragMode`).
  The one exception is **BUG-03**: the 75 ms gate compares wall-clock `mousedown→mouseup`, which
  sequential Playwright actions can't hold under reliably, so that test dispatches the whole
  drag as native `MouseEvent`s in a single JS tick (`window.synthMouseDrag`, `holdMs:0`) for the
  suppressed case and with a real hold for the zoomed case.
- **Touch** is synthesized through the **Chrome DevTools Protocol** (`Input.dispatchTouchEvent`)
  because Playwright's built-in touchscreen is single-finger only. CDP produces genuine
  multi-touch `TouchEvent`s with per-finger identifiers and populated `changedTouches` — exactly
  what the chart's handlers and d3.v3's `d3.touches()` read.

## Known limitations / things the harness can't assert (do these manually)

- **Server round-trips.** The chart talks to C++ for the heavy lifting (actually fitting a
  peak, defining a ROI, recalibrating). Standalone, those responses never come back. The
  **peak-fit completion** emit (`handleMouseUpPeakFit`) only runs after the server sets
  `roiBeingDrugUpdate`; the BUG-04 test simulates that one server response in-page
  (`setFakeFitRoi`) so the fixed code path runs, but the real keep-peak menu / fitted peak can
  only be verified in the full app.
- **Touch count-gammas is gesture-sensitive.** In `handleVisTouchMove` the recognizers are
  checked in order `controlDragSwipe` (peak fit) → `deletePeakSwipe` → `altShiftSwipe` (count
  gammas). A two-finger rightward swipe with the fingers side-by-side (separated in x) is read
  as a **peak fit**; count-gammas only wins when the fingers are **stacked vertically** (one
  above the other, ~same x), which is what the test does. Worth keeping in mind if the gesture
  ever feels like it "does the wrong thing" — the vertical stacking is what disambiguates it.
- **Sub-500px / device-pixel-ratio phone layout** isn't the goal here (see the InterSpec
  `PlaywrightPhoneEmulation` harness and the CLAUDE.md "Mobile / phone UI testing" notes). This
  harness exercises chart *interaction logic*, not responsive CSS.

## Adding a test

Drive everything through the `chart` fixture (energies, not pixels):

```js
const { test, expect } = require('../lib/harness');

test('my gesture', async ({ chart }) => {
  await chart.mouseDrag( 500, 900, { yFrac: 0.5 } );        // or touch: chart.pinch / twoFingerDrag / tap …
  expect( (await chart.emits('xrangechanged')).length ).toBeGreaterThan( 0 );
  expect( await chart.errors() ).toEqual( [] );
});
```

See `lib/harness.js` for the full `chart` API:
- **mouse:** `mouseDrag`, `mouseDragV`, `mouseDragWithMid`, `synthDrag`, `click`, `dblclick`,
  `rightClick`, `rightDragPan`, `wheel`
- **touch:** `oneFingerDrag`, `twoFingerDrag`, `twoFingerDragWithMid`, `pinch`, `tap`,
  `doubleTap`, `longPress` (count-gammas = `twoFingerDrag` with `f2YOffsetPx`)
- **state / assertions:** `emits`, `lastEmit`, `errors`, `xDomain`, `yDomain`, `dragMode`,
  `boxRect`, `client`, `energyAt`, `peakEnergies`, `plotWidth`, `mouseCoordLines`,
  `readoutEnergy`, `globalLeaked`, `setFakeFitRoi`
