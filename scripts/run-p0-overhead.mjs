/**
 * Responsibility: paired metrics ON/OFF overhead check for the P0 instrumentation gate.
 *
 * Uses always-on __kiboPresentationLoopCount so the OFF arm truly disables metric recording
 * while still measuring presentation cadence.
 */

import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { generateAllFakeCameraFixtures } from './generate-fake-camera.mjs';
import {
  applyCpuThrottling,
  launchDemoWithFakeCamera,
  REPOSITORY_ROOT,
  waitForWarmUp,
} from '../test/browser/harness.mjs';

const FIXTURE_DIRECTORY = path.join(REPOSITORY_ROOT, 'test', 'fixtures', 'fake-camera');
const WARM_UP_MILLISECONDS = Number(process.env.KIBO_PERF_WARMUP_MS || 3000);
const SAMPLE_MILLISECONDS = Number(process.env.KIBO_PERF_SAMPLE_MS || 10000);
const PAIR_COUNT = Number(process.env.KIBO_PERF_PAIRS || 2);
const CPU_THROTTLE_RATE = Number(process.env.KIBO_PERF_CPU_THROTTLE || 8);
const DEMO_PORT = 8765;
const EQUIVALENCE_BOUND_FRACTION = Number(process.env.KIBO_PERF_EQUIV_BOUND || 0.15);
const MILLISECONDS_PER_SECOND = 1000;

function median(values) {
  const sorted = [...values].sort((left, right) => left - right);
  const middle = Math.floor(sorted.length / 2);
  if (sorted.length % 2 === 0) {
    return (sorted[middle - 1] + sorted[middle]) / 2;
  }
  return sorted[middle];
}

function bootstrapRelativeMedianDeltaCi(samplesOff, samplesOn, iterations = 2000) {
  const deltas = [];
  for (let iteration = 0; iteration < iterations; iteration += 1) {
    const resampleOff = samplesOff.map(() => samplesOff[Math.floor(Math.random() * samplesOff.length)]);
    const resampleOn = samplesOn.map(() => samplesOn[Math.floor(Math.random() * samplesOn.length)]);
    const offMedian = median(resampleOff);
    const onMedian = median(resampleOn);
    deltas.push((onMedian - offMedian) / Math.max(offMedian, 1e-9));
  }
  deltas.sort((left, right) => left - right);
  return {
    relativeMedianDelta: (median(samplesOn) - median(samplesOff)) / Math.max(median(samplesOff), 1e-9),
    ci95: [deltas[Math.floor(0.025 * deltas.length)], deltas[Math.floor(0.975 * deltas.length)]],
  };
}

async function measurePresentationLoopFps(metricsEnabled) {
  const y4mAbsolutePath = path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m');
  const session = await launchDemoWithFakeCamera({ y4mAbsolutePath });
  try {
    await applyCpuThrottling(session.page, CPU_THROTTLE_RATE);
    await session.page.evaluate((enabled) => {
      window.__kiboPipelineMetrics.setMetricsRecordingEnabled(enabled);
      window.__kiboPresentationLoopCount = 0;
    }, metricsEnabled);
    await waitForWarmUp(session.page, WARM_UP_MILLISECONDS);
    await session.page.evaluate(() => {
      window.__kiboPresentationLoopCount = 0;
    });
    await waitForWarmUp(session.page, SAMPLE_MILLISECONDS);
    const loopCount = await session.page.evaluate(() => window.__kiboPresentationLoopCount || 0);
    return loopCount / (SAMPLE_MILLISECONDS / MILLISECONDS_PER_SECOND);
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

  const offSamples = [];
  const onSamples = [];
  for (let pairIndex = 0; pairIndex < PAIR_COUNT; pairIndex += 1) {
    const order = pairIndex % 2 === 0 ? ['off', 'on'] : ['on', 'off'];
    for (const label of order) {
      const value = await measurePresentationLoopFps(label === 'on');
      if (label === 'on') {
        onSamples.push(value);
      } else {
        offSamples.push(value);
      }
      console.log(JSON.stringify({ pairIndex, label, presentationLoopFps: value }));
    }
  }

  const stats = bootstrapRelativeMedianDeltaCi(offSamples, onSamples);
  const withinBand = (
    stats.ci95[0] >= -EQUIVALENCE_BOUND_FRACTION
    && stats.ci95[1] <= EQUIVALENCE_BOUND_FRACTION
  );

  const artifactDirectory = path.join(
    REPOSITORY_ROOT,
    'artifacts',
    'p0-overhead',
    new Date().toISOString().replaceAll(':', '').replaceAll('.', ''));
  mkdirSync(artifactDirectory, { recursive: true });
  const report = {
    protocol: {
      warmUpMilliseconds: WARM_UP_MILLISECONDS,
      sampleMilliseconds: SAMPLE_MILLISECONDS,
      pairCount: PAIR_COUNT,
      cpuThrottleRate: CPU_THROTTLE_RATE,
      equivalenceBoundFraction: EQUIVALENCE_BOUND_FRACTION,
      measurement: '__kiboPresentationLoopCount with metrics recording forced off/on for the full sample',
    },
    offSamples,
    onSamples,
    stats,
    withinBand,
  };
  writeFileSync(path.join(artifactDirectory, 'report.json'), `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify({ withinBand, stats, artifactDirectory }, null, 2));
  if (!withinBand) {
    process.exitCode = 2;
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
