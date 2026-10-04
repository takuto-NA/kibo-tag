/**
 * Responsibility: demo wiring for ID-unconfirmed (decode-failed) quad overlays.
 */

import { test, expect } from '@playwright/test';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { generateAllFakeCameraFixtures } from '../../scripts/generate-fake-camera.mjs';
import {
  launchDemoWithFakeCamera,
  REPOSITORY_ROOT,
  waitForWarmUp,
} from './harness.mjs';

const FIXTURE_DIRECTORY = path.join(REPOSITORY_ROOT, 'test', 'fixtures', 'fake-camera');
const DEMO_PORT = 8876;
const DEMO_BASE_URL = `http://127.0.0.1:${DEMO_PORT}`;
const WARM_UP_MILLISECONDS = 4000;

let demoServer;

test.beforeAll(async () => {
  demoServer = spawn(
    'python',
    ['-m', 'http.server', String(DEMO_PORT), '--directory', 'html', '--bind', '127.0.0.1'],
    { cwd: REPOSITORY_ROOT, stdio: 'ignore' });
  const readyDeadline = Date.now() + 10000;
  while (Date.now() < readyDeadline) {
    try {
      const response = await fetch(DEMO_BASE_URL);
      if (response.ok) {
        return;
      }
    } catch {
      // Server is still starting.
    }
    await new Promise((resolve) => setTimeout(resolve, 200));
  }
  throw new Error(`Demo server did not start on ${DEMO_BASE_URL}`);
});

test.afterAll(() => {
  demoServer?.kill();
});

test('showing ID-unconfirmed quads keeps tag id 0 and exposes a failed-quad list', async () => {
  const fixturePath = path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m');
  if (!existsSync(fixturePath)) {
    generateAllFakeCameraFixtures(FIXTURE_DIRECTORY);
  }

  const session = await launchDemoWithFakeCamera({
    y4mAbsolutePath: fixturePath,
    baseURL: DEMO_BASE_URL,
  });
  try {
    await waitForWarmUp(session.page, WARM_UP_MILLISECONDS);
    await session.page.check('#show_failed_quads');
    await session.page.waitForFunction(() => {
      const statusText = document.getElementById('detector_status')?.textContent || '';
      return statusText.includes('near-miss ID-unconfirmed quads');
    }, null, { timeout: 20000 });
    await session.page.waitForFunction(() => {
      const ids = window.__kiboLastDetectionIds;
      return Array.isArray(ids) && ids.includes(0) && Array.isArray(window.__kiboLastFailedQuads);
    }, null, { timeout: 20000 });

    const failedQuads = await session.page.evaluate(() => window.__kiboLastFailedQuads);
    expect(Array.isArray(failedQuads)).toBe(true);
    for (const failedQuad of failedQuads) {
      expect(failedQuad.id).toBeUndefined();
      expect(failedQuad.corners).toHaveLength(4);
    }
  } finally {
    await session.close();
  }
});
