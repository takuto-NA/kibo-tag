/**
 * Responsibility: record a paired performance sample series for the current candidate build.
 *
 * This is not a two-binary compare yet; use symptom/performance dual-gate tests for gated vs
 * decoupled acceptance. Keep this script for longitudinal candidate sampling artifacts.
 */

import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { generateAllFakeCameraFixtures } from './generate-fake-camera.mjs';
import {
  applyCpuThrottling,
  launchDemoWithFakeCamera,
  readPipelineSummary,
  REPOSITORY_ROOT,
  waitForWarmUp,
} from '../test/browser/harness.mjs';

const FIXTURE_DIRECTORY = path.join(REPOSITORY_ROOT, 'test', 'fixtures', 'fake-camera');
const DEFAULT_WARM_UP_MILLISECONDS = Number(process.env.KIBO_PERF_WARMUP_MS || 10000);
const DEFAULT_SAMPLE_MILLISECONDS = Number(process.env.KIBO_PERF_SAMPLE_MS || 60000);
const DEFAULT_SAMPLE_COUNT = Number(process.env.KIBO_PERF_SAMPLES || 4);
const CPU_THROTTLE_RATE = Number(process.env.KIBO_PERF_CPU_THROTTLE || 8);
const DEMO_PORT = 8765;

function median(values) {
  const sorted = [...values].sort((left, right) => left - right);
  const middle = Math.floor(sorted.length / 2);
  if (sorted.length % 2 === 0) {
    return (sorted[middle - 1] + sorted[middle]) / 2;
  }
  return sorted[middle];
}

async function measureOnce() {
  const y4mAbsolutePath = path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m');
  const session = await launchDemoWithFakeCamera({ y4mAbsolutePath });
  try {
    await applyCpuThrottling(session.page, CPU_THROTTLE_RATE);
    await waitForWarmUp(session.page, DEFAULT_WARM_UP_MILLISECONDS);
    await session.page.evaluate(() => window.__kiboPipelineMetrics.reset());
    await waitForWarmUp(session.page, DEFAULT_SAMPLE_MILLISECONDS);
    return await readPipelineSummary(session.page, 0);
  } finally {
    await session.close();
  }
}

async function main() {
  generateAllFakeCameraFixtures(FIXTURE_DIRECTORY);
  const response = await fetch(`http://127.0.0.1:${DEMO_PORT}/`);
  if (!response.ok) {
    throw new Error(`Demo server not reachable on ${DEMO_PORT}`);
  }

  const artifactDirectory = path.join(
    REPOSITORY_ROOT,
    'artifacts',
    'perf-compare',
    new Date().toISOString().replaceAll(':', '').replaceAll('.', ''));
  mkdirSync(artifactDirectory, { recursive: true });

  const samples = [];
  for (let index = 0; index < DEFAULT_SAMPLE_COUNT; index += 1) {
    const summary = await measureOnce();
    samples.push(summary);
    console.log(JSON.stringify({
      index,
      canvasPresentedFps: summary.canvasPresentedFps,
      detectThroughputFps: summary.detectThroughputFps,
      presentationDetectRatio: summary.presentationDetectRatio,
    }));
  }

  const report = {
    protocol: {
      warmUpMilliseconds: DEFAULT_WARM_UP_MILLISECONDS,
      sampleMilliseconds: DEFAULT_SAMPLE_MILLISECONDS,
      sampleCount: DEFAULT_SAMPLE_COUNT,
      cpuThrottleRate: CPU_THROTTLE_RATE,
    },
    samples,
    canvasPresentedFpsMedian: median(samples.map((sample) => sample.canvasPresentedFps)),
  };
  writeFileSync(path.join(artifactDirectory, 'report.json'), `${JSON.stringify(report, null, 2)}\n`);
  console.log(`wrote ${path.join(artifactDirectory, 'report.json')}`);
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
