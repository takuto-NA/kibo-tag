/**
 * Responsibility: characterization E2E for the real demo path (correctness before production fix).
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
const WARM_UP_MILLISECONDS = 4000;
const DETECTION_POLL_TIMEOUT_MILLISECONDS = 20000;

test.beforeAll(() => {
  // Fixtures are large; regenerate only when the static clip is missing.
  const staticFixturePath = path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m');
  if (!existsSync(staticFixturePath)) {
    generateAllFakeCameraFixtures(FIXTURE_DIRECTORY);
  }
});

async function waitForTagIds(page, expectedIds) {
  await page.waitForFunction((ids) => {
    const lastIds = window.__kiboLastDetectionIds;
    if (!Array.isArray(lastIds)) {
      return false;
    }
    if (lastIds.length !== ids.length) {
      return false;
    }
    const sortedLast = [...lastIds].sort((a, b) => a - b);
    const sortedExpected = [...ids].sort((a, b) => a - b);
    return sortedLast.every((value, index) => value === sortedExpected[index]);
  }, expectedIds, { timeout: DETECTION_POLL_TIMEOUT_MILLISECONDS });
}

test('static tag36h11 id 0 is detected through real getUserMedia fake-device path', async () => {
  const y4mAbsolutePath = path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m');
  const session = await launchDemoWithFakeCamera({ y4mAbsolutePath });
  try {
    await waitForWarmUp(session.page, WARM_UP_MILLISECONDS);
    await waitForTagIds(session.page, [0]);
    const detections = await session.page.evaluate(() => window.__kiboLastDetections);
    expect(detections.length).toBeGreaterThan(0);
    expect(detections[0].id).toBe(0);
    expect(detections[0].corners.length).toBe(4);
  } finally {
    await session.close();
  }
});

test('empty scene yields zero detections', async () => {
  const y4mAbsolutePath = path.join(FIXTURE_DIRECTORY, 'empty-scene.y4m');
  const session = await launchDemoWithFakeCamera({ y4mAbsolutePath });
  try {
    await waitForWarmUp(session.page, WARM_UP_MILLISECONDS);
    await session.page.waitForFunction(() => Array.isArray(window.__kiboLastDetectionIds), null, {
      timeout: DETECTION_POLL_TIMEOUT_MILLISECONDS,
    });
    await session.page.waitForTimeout(2000);
    const lastIds = await session.page.evaluate(() => window.__kiboLastDetectionIds);
    expect(lastIds).toEqual([]);
  } finally {
    await session.close();
  }
});

test('settings A→B→A race leaves only final config active without stale overlay ids from old family path', async () => {
  const y4mAbsolutePath = path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m');
  const session = await launchDemoWithFakeCamera({ y4mAbsolutePath });
  try {
    await waitForWarmUp(session.page, WARM_UP_MILLISECONDS);
    await waitForTagIds(session.page, [0]);

    await session.page.selectOption('#detector_family', 'DICT_4X4_100');
    await session.page.waitForTimeout(1500);
    await session.page.selectOption('#detector_family', 'tag36h11');
    await session.page.waitForTimeout(2000);

    await waitForTagIds(session.page, [0]);
    const statusText = await session.page.locator('#detector_status').innerText();
    expect(statusText.toLowerCase()).toContain('tag36h11');
  } finally {
    await session.close();
  }
});

test('pagehide stops camera tracks', async () => {
  const y4mAbsolutePath = path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m');
  const session = await launchDemoWithFakeCamera({ y4mAbsolutePath });
  try {
    await waitForWarmUp(session.page, 2000);
    const trackEnded = await session.page.evaluate(async () => {
      const stream = window.stream;
      if (!stream) {
        return false;
      }
      window.dispatchEvent(new PageTransitionEvent('pagehide', { persisted: false }));
      await new Promise((resolve) => setTimeout(resolve, 100));
      return stream.getTracks().every((track) => track.readyState === 'ended');
    });
    expect(trackEnded).toBe(true);
  } finally {
    await session.close();
  }
});

test('save next detection writes JSON into localStorage-backed textarea', async () => {
  const y4mAbsolutePath = path.join(FIXTURE_DIRECTORY, 'static-tag36h11-0.y4m');
  const session = await launchDemoWithFakeCamera({ y4mAbsolutePath });
  try {
    await waitForWarmUp(session.page, WARM_UP_MILLISECONDS);
    await waitForTagIds(session.page, [0]);
    await session.page.click('#req_save');
    await session.page.waitForFunction(() => {
      const text = document.getElementById('saved_det_data')?.value || '';
      return text.includes('"id"') || text.includes('"detections"');
    }, null, { timeout: DETECTION_POLL_TIMEOUT_MILLISECONDS });
    const savedText = await session.page.locator('#saved_det_data').inputValue();
    expect(savedText.length).toBeGreaterThan(20);
  } finally {
    await session.close();
  }
});
