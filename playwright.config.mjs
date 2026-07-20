/**
 * Responsibility: pin Chromium browser tests for the demo / fake-camera harness.
 */

import { defineConfig } from '@playwright/test';

const DEMO_PORT = 8765;
const DEMO_BASE_URL = `http://127.0.0.1:${DEMO_PORT}`;

export default defineConfig({
  testDir: 'test/browser',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  timeout: 180000,
  expect: {
    timeout: 30000,
  },
  use: {
    baseURL: DEMO_BASE_URL,
    headless: true,
    browserName: 'chromium',
    launchOptions: {
      args: [
        '--use-fake-ui-for-media-stream',
        '--use-fake-device-for-media-stream',
        '--auto-accept-camera-and-microphone-capture',
      ],
    },
    permissions: ['camera'],
  },
  webServer: {
    command: `python -m http.server ${DEMO_PORT} --directory html --bind 127.0.0.1`,
    url: DEMO_BASE_URL,
    reuseExistingServer: !process.env.CI,
    timeout: 120000,
  },
});
