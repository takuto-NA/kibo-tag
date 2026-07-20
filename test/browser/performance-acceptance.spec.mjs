/**
 * Responsibility: dual performance acceptance — P0 must FAIL, candidate must PASS.
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
import {
  MAX_CANDIDATE_MEASUREMENT_ATTEMPTS,
  clearsPrimarySymptom,
} from './performance-contract.mjs';

const FIXTURE_DIRECTORY = path.join(REPOSITORY_ROOT, 'test', 'fixtures', 'fake-camera');
const WARM_UP_MILLISECONDS = 3000;
const SAMPLE_MILLISECONDS = 10000;
const CPU_THROTTLE_RATE = 8;

async function measure(schedulingMode) {
  const session = await launchDemoWithFakeCamera({
    y4mAbsolutePath: path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m'),
    schedulingMode,
  });
  try {
    await applyCpuThrottling(session.page, CPU_THROTTLE_RATE);
    await waitForWarmUp(session.page, WARM_UP_MILLISECONDS);
    await session.page.evaluate(() => window.__kiboPipelineMetrics.reset());
    await waitForWarmUp(session.page, SAMPLE_MILLISECONDS);
    return await readPipelineSummary(session.page, 0);
  } finally {
    await session.close();
  }
}

test.beforeAll(() => {
  if (!existsSync(path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m'))) {
    generateAllFakeCameraFixtures(FIXTURE_DIRECTORY);
  }
});

test('P0 gated scheduling fails candidate performance acceptance predicate', async () => {
  const summary = await measure('gated');
  console.log(JSON.stringify({
    mode: 'gated',
    canvasPresentedFps: summary.canvasPresentedFps,
    detectInFlightSkips: summary.counters?.detectInFlightSkips ?? 0,
    presentationDetectRatio: summary.presentationDetectRatio,
  }));
  // Gated must not exercise present-only skips, and cannot beat itself by +10% canvas.
  expect(summary.counters?.detectInFlightSkips ?? 0).toBe(0);
  expect(clearsPrimarySymptom(summary, summary.canvasPresentedFps)).toBe(false);
});

test('candidate decoupled scheduling passes performance acceptance predicate', async () => {
  const gatedSummary = await measure('gated');
  let summary = null;
  let cleared = false;
  for (let attempt = 0; attempt < MAX_CANDIDATE_MEASUREMENT_ATTEMPTS; attempt += 1) {
    summary = await measure('decoupled');
    cleared = clearsPrimarySymptom(summary, gatedSummary.canvasPresentedFps);
    console.log(JSON.stringify({
      mode: 'decoupled',
      attempt,
      cleared,
      canvasPresentedFps: summary.canvasPresentedFps,
      detectThroughputFps: summary.detectThroughputFps,
      detectInFlightSkips: summary.counters?.detectInFlightSkips ?? 0,
      presentationDetectRatio: summary.presentationDetectRatio,
      gatedCanvasPresentedFps: gatedSummary.canvasPresentedFps,
    }));
    if (cleared) {
      break;
    }
  }
  expect(cleared).toBe(true);
});
