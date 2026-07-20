/**
 * Responsibility: short soak smoke for resource stability (full 30m soak is nightly).
 */

import { test, expect } from '@playwright/test';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { generateAllFakeCameraFixtures } from '../../scripts/generate-fake-camera.mjs';
import {
  launchDemoWithFakeCamera,
  readPipelineSummary,
  REPOSITORY_ROOT,
  waitForWarmUp,
} from './harness.mjs';

const FIXTURE_DIRECTORY = path.join(REPOSITORY_ROOT, 'test', 'fixtures', 'fake-camera');
const SOAK_MILLISECONDS = 20000;

test('short soak keeps completing detections without hard failure', async () => {
  if (!existsSync(path.join(FIXTURE_DIRECTORY, 'moving-tag36h11-0.y4m'))) {
    generateAllFakeCameraFixtures(FIXTURE_DIRECTORY);
  }

  const session = await launchDemoWithFakeCamera({
    y4mAbsolutePath: path.join(FIXTURE_DIRECTORY, 'moving-tag36h11-0.y4m'),
  });
  try {
    await waitForWarmUp(session.page, 3000);
    await session.page.evaluate(() => window.__kiboPipelineMetrics.reset());
    await waitForWarmUp(session.page, SOAK_MILLISECONDS);
    const summary = await readPipelineSummary(session.page, 0);
    expect(summary.counters.framesCompleted).toBeGreaterThan(50);
    expect(summary.detectThroughputFps).toBeGreaterThan(5);
  } finally {
    await session.close();
  }
});
