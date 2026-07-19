// @ts-check
const { defineConfig, devices } = require('@playwright/test');

const PORT = 8125;

// Two projects share the same harness but differ in input mode:
//   - "mouse" runs the moused-device suite (modifier-key drags, wheel, clicks).
//   - "touch" runs the touch/phone suite (one- and two-finger gestures via CDP).
// Both run smoke.spec.js. All touch is synthesized through the Chrome DevTools
// Protocol (Input.dispatchTouchEvent), which produces genuine multi-touch
// TouchEvents — the only reliable way to drive the chart's two-finger gestures.
module.exports = defineConfig({
  testDir: './tests',
  fullyParallel: false,         // single shared chart per page; keep runs deterministic
  forbidOnly: !!process.env.CI,
  retries: 0,
  reporter: process.env.CI ? 'line' : [['list'], ['html', { open: 'never' }]],
  use: {
    baseURL: `http://127.0.0.1:${PORT}`,
    trace: 'retain-on-failure',
    video: 'off',
  },
  projects: [
    {
      name: 'mouse',
      testMatch: /(smoke|mouse|resize|slider|peaks)\.spec\.js/,
      use: { ...devices['Desktop Chrome'], hasTouch: false },
    },
    {
      name: 'touch',
      testMatch: /(touch)\.spec\.js/,
      // hasTouch enables TouchEvent support; we still dispatch via CDP for true multi-touch.
      use: { ...devices['Desktop Chrome'], hasTouch: true, isMobile: false },
    },
  ],
  webServer: {
    command: 'node static-server.js',
    port: PORT,
    reuseExistingServer: !process.env.CI,
    stdout: 'ignore',
    stderr: 'pipe',
  },
});
