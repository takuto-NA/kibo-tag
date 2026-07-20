/**
 * Responsibility: blur non-regression smoke (full AUC matrix is nightly).
 */

import { test, expect } from '@playwright/test';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { generateAllFakeCameraFixtures } from '../../scripts/generate-fake-camera.mjs';
import {
  launchDemoWithFakeCamera,
  REPOSITORY_ROOT,
  waitForWarmUp,
} from './harness.mjs';

const FIXTURE_DIRECTORY = path.join(REPOSITORY_ROOT, 'test', 'fixtures', 'fake-camera');

test('zero-blur static fixture still detects tag id 0 after remediation', async () => {
  if (!existsSync(path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m'))) {
    generateAllFakeCameraFixtures(FIXTURE_DIRECTORY);
  }

  const session = await launchDemoWithFakeCamera({
    y4mAbsolutePath: path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m'),
  });
  try {
    await waitForWarmUp(session.page, 4000);
    await session.page.waitForFunction(() => {
      const ids = window.__kiboLastDetectionIds;
      return Array.isArray(ids) && ids.includes(0);
    }, null, { timeout: 20000 });
    const ids = await session.page.evaluate(() => window.__kiboLastDetectionIds);
    expect(ids).toContain(0);
  } finally {
    await session.close();
  }
});
