/**
 * Responsibility: prove slow detector does not amplify queue latency (in-flight ≤ 1).
 */

import { test, expect } from '@playwright/test';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { generateAllFakeCameraFixtures } from '../../scripts/generate-fake-camera.mjs';
import {
  applyCpuThrottling,
  launchDemoWithFakeCamera,
  readPipelineSummary,
  REPOSITORY_ROOT,
  waitForWarmUp,
} from './harness.mjs';

const FIXTURE_DIRECTORY = path.join(REPOSITORY_ROOT, 'test', 'fixtures', 'fake-camera');
const CPU_THROTTLE_RATE = 8;
const MAX_SUBMITTED_COMPLETED_DELTA = 2;

test('under CPU throttle, dropped/coalesced frames keep detect from queueing unbounded', async () => {
  if (!existsSync(path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m'))) {
    generateAllFakeCameraFixtures(FIXTURE_DIRECTORY);
  }

  const session = await launchDemoWithFakeCamera({
    y4mAbsolutePath: path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m'),
  });
  try {
    await applyCpuThrottling(session.page, CPU_THROTTLE_RATE);
    await waitForWarmUp(session.page, 3000);
    await session.page.evaluate(() => window.__kiboPipelineMetrics.reset());
    await waitForWarmUp(session.page, 7000);
    const summary = await readPipelineSummary(session.page, 0);

    // submitted should stay close to completed (no growing backlog); extras are skipped in-flight.
    const submitted = summary.counters.framesSubmitted;
    const completed = summary.counters.framesCompleted;
    expect(Math.abs(submitted - completed)).toBeLessThanOrEqual(MAX_SUBMITTED_COMPLETED_DELTA);
    expect(summary.counters.detectInFlightSkips).toBeGreaterThan(0);
  } finally {
    await session.close();
  }
});
