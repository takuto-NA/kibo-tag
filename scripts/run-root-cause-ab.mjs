/**
 * Responsibility: one-variable A/B experiments to isolate the dominant FPS stage.
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
const WARM_UP_MILLISECONDS = Number(process.env.KIBO_PERF_WARMUP_MS || 5000);
const SAMPLE_MILLISECONDS = Number(process.env.KIBO_PERF_SAMPLE_MS || 15000);
const CPU_THROTTLE_RATE = Number(process.env.KIBO_PERF_CPU_THROTTLE || 8);
const DEMO_PORT = 8765;

const EXPERIMENTS = [
  {
    name: 'baseline-full-pipeline',
    mutatePage: null,
  },
  {
    name: 'skip-detect-after-grayscale',
    mutatePage: async (page) => {
      await page.evaluate(() => {
        window.__kiboEnableRootCauseExperiments = true;
        window.__kiboExperimentSkipDetect = true;
      });
    },
  },
  {
    name: 'skip-grayscale-and-detect',
    mutatePage: async (page) => {
      await page.evaluate(() => {
        window.__kiboEnableRootCauseExperiments = true;
        window.__kiboExperimentSkipGrayscaleAndDetect = true;
      });
    },
  },
];

async function measureExperiment(experiment) {
  const y4mAbsolutePath = path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m');
  const session = await launchDemoWithFakeCamera({ y4mAbsolutePath });
  try {
    if (experiment.mutatePage) {
      await experiment.mutatePage(session.page);
    }
    await applyCpuThrottling(session.page, CPU_THROTTLE_RATE);
    await waitForWarmUp(session.page, WARM_UP_MILLISECONDS);
    await session.page.evaluate(() => window.__kiboPipelineMetrics.reset());
    await waitForWarmUp(session.page, SAMPLE_MILLISECONDS);
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
    'root-cause',
    new Date().toISOString().replaceAll(':', '').replaceAll('.', ''));
  mkdirSync(artifactDirectory, { recursive: true });

  const results = [];
  for (const experiment of EXPERIMENTS) {
    const summary = await measureExperiment(experiment);
    results.push({ experiment: experiment.name, summary });
    console.log(JSON.stringify({
      experiment: experiment.name,
      canvasPresentedFps: summary.canvasPresentedFps,
      detectThroughputFps: summary.detectThroughputFps,
      stageTotalsMilliseconds: summary.stageTotalsMilliseconds,
    }));
  }

  const baseline = results.find((entry) => entry.experiment === 'baseline-full-pipeline').summary;
  const skipDetect = results.find((entry) => entry.experiment === 'skip-detect-after-grayscale').summary;
  const skipGray = results.find((entry) => entry.experiment === 'skip-grayscale-and-detect').summary;

  const report = {
    protocol: {
      warmUpMilliseconds: WARM_UP_MILLISECONDS,
      sampleMilliseconds: SAMPLE_MILLISECONDS,
      cpuThrottleRate: CPU_THROTTLE_RATE,
    },
    results,
    interpretation: {
      detectContributionHint:
        skipDetect.canvasPresentedFps - baseline.canvasPresentedFps,
      grayscalePlusDetectContributionHint:
        skipGray.canvasPresentedFps - baseline.canvasPresentedFps,
      dominantObservation:
        'If skip-detect raises canvas FPS little while skip-grayscale raises it a lot, main-thread prep dominates; '
        + 'if skip-detect raises it a lot, await-detect coupling/WASM dominates.',
    },
  };

  writeFileSync(path.join(artifactDirectory, 'report.json'), `${JSON.stringify(report, null, 2)}\n`);
  console.log(`wrote ${path.join(artifactDirectory, 'report.json')}`);
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
