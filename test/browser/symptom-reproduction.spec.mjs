/**
 * Responsibility: dual gate for the locked primary symptom — P0 RED and candidate clearance.
 */

import { test, expect } from '@playwright/test';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
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
  CANDIDATE_CLEARANCE_PREDICATE,
  MIN_CANVAS_IMPROVEMENT_OVER_GATED,
  PRIMARY_SYMPTOM_NAME,
  REQUIRED_CLEARED_RUNS,
  REQUIRED_SYMPTOM_RUNS,
  TOTAL_SYMPTOM_RUNS,
  clearsPrimarySymptom,
  hitsPrimarySymptom,
} from './performance-contract.mjs';

const FIXTURE_DIRECTORY = path.join(REPOSITORY_ROOT, 'test', 'fixtures', 'fake-camera');
const ARTIFACT_DIRECTORY = path.join(REPOSITORY_ROOT, 'artifacts', 'symptom-reproduction');
const WARM_UP_MILLISECONDS = 3000;
const SAMPLE_MILLISECONDS = 7000;
const CANDIDATE_SAMPLE_MILLISECONDS = 10000;
const CPU_THROTTLE_RATE = 8;

test.describe.configure({ mode: 'serial' });

async function measureOnce(schedulingMode, sampleMilliseconds) {
  const session = await launchDemoWithFakeCamera({
    y4mAbsolutePath: path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m'),
    schedulingMode,
  });
  try {
    await applyCpuThrottling(session.page, CPU_THROTTLE_RATE);
    await waitForWarmUp(session.page, WARM_UP_MILLISECONDS);
    await session.page.evaluate(() => window.__kiboPipelineMetrics.reset());
    await waitForWarmUp(session.page, sampleMilliseconds);
    return await readPipelineSummary(session.page, 0);
  } finally {
    await session.close();
  }
}

async function runGatedCampaign() {
  let symptomHits = 0;
  const runSummaries = [];
  for (let runIndex = 0; runIndex < TOTAL_SYMPTOM_RUNS; runIndex += 1) {
    const summary = await measureOnce('gated', SAMPLE_MILLISECONDS);
    runSummaries.push(summary);
    const symptomPresent = hitsPrimarySymptom(summary);
    if (symptomPresent) {
      symptomHits += 1;
    }
    console.log(JSON.stringify({
      schedulingMode: 'gated',
      runIndex,
      symptomPresent,
      cameraPresentedFps: summary.cameraPresentedFps,
      detectThroughputFps: summary.detectThroughputFps,
      presentationDetectRatio: summary.presentationDetectRatio,
    }));
  }
  return { symptomHits, runSummaries };
}

test.beforeAll(() => {
  if (!existsSync(path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m'))) {
    generateAllFakeCameraFixtures(FIXTURE_DIRECTORY);
  }
  mkdirSync(ARTIFACT_DIRECTORY, { recursive: true });
});

test('P0 gated scheduling reproduces primary symptom in 4/5 runs', async () => {
  const { symptomHits, runSummaries } = await runGatedCampaign();
  writeFileSync(
    path.join(ARTIFACT_DIRECTORY, 'contract-v1-runs.json'),
    `${JSON.stringify({
      contractVersion: 1,
      primarySymptom: PRIMARY_SYMPTOM_NAME,
      mode: 'p0-gated',
      schedulingMode: 'gated',
      cpuThrottleRate: CPU_THROTTLE_RATE,
      requiredPassingRuns: REQUIRED_SYMPTOM_RUNS,
      totalRuns: TOTAL_SYMPTOM_RUNS,
      passingRuns: symptomHits,
      runSummaries,
    }, null, 2)}\n`);

  expect(symptomHits).toBeGreaterThanOrEqual(REQUIRED_SYMPTOM_RUNS);
});

test('candidate decoupled scheduling clears primary symptom in majority (≥3/5) runs', async () => {
  // Same-host gated reference removes host-load ambiguity from absolute ratio thresholds.
  const gatedReferenceSummary = await measureOnce('gated', CANDIDATE_SAMPLE_MILLISECONDS);
  const gatedReferenceCanvasPresentedFps = gatedReferenceSummary.canvasPresentedFps;

  let clearedRuns = 0;
  const runSummaries = [];
  for (let runIndex = 0; runIndex < TOTAL_SYMPTOM_RUNS; runIndex += 1) {
    const summary = await measureOnce('decoupled', CANDIDATE_SAMPLE_MILLISECONDS);
    runSummaries.push(summary);
    const cleared = clearsPrimarySymptom(summary, gatedReferenceCanvasPresentedFps);
    if (cleared) {
      clearedRuns += 1;
    }
    console.log(JSON.stringify({
      schedulingMode: 'decoupled',
      runIndex,
      cleared,
      cameraPresentedFps: summary.canvasPresentedFps,
      detectInFlightSkips: summary.counters?.detectInFlightSkips ?? 0,
      presentationDetectRatio: summary.presentationDetectRatio,
      gatedReferenceCanvasPresentedFps,
    }));
  }

  const canvasRates = runSummaries.map((summary) => summary.canvasPresentedFps).sort((a, b) => a - b);
  const medianCanvasPresentedFps = canvasRates[Math.floor(canvasRates.length / 2)];
  const medianCanvasImprovementRatio = (
    medianCanvasPresentedFps / gatedReferenceCanvasPresentedFps
  );

  writeFileSync(
    path.join(ARTIFACT_DIRECTORY, 'contract-v1-candidate-runs.json'),
    `${JSON.stringify({
      contractVersion: 1,
      primarySymptom: PRIMARY_SYMPTOM_NAME,
      mode: 'candidate-clears-symptom',
      schedulingMode: 'decoupled',
      cpuThrottleRate: CPU_THROTTLE_RATE,
      sampleMilliseconds: CANDIDATE_SAMPLE_MILLISECONDS,
      requiredClearedRuns: REQUIRED_CLEARED_RUNS,
      clearancePredicate: CANDIDATE_CLEARANCE_PREDICATE,
      gatedReferenceCanvasPresentedFps,
      totalRuns: TOTAL_SYMPTOM_RUNS,
      clearedRuns,
      medianCanvasPresentedFps,
      medianCanvasImprovementRatio,
      gatedReferenceSummary,
      runSummaries,
    }, null, 2)}\n`);

  expect(medianCanvasImprovementRatio).toBeGreaterThan(MIN_CANVAS_IMPROVEMENT_OVER_GATED);
  expect(clearedRuns).toBeGreaterThanOrEqual(REQUIRED_CLEARED_RUNS);
});
