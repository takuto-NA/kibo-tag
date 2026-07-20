/**
 * Responsibility: prove fake-camera Y4M generation is deterministic (SHA + metadata).
 */

import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { generateAllFakeCameraFixtures } from '../../scripts/generate-fake-camera.mjs';

const REPOSITORY_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

function assertDeterministicGeneration() {
  const firstDirectory = mkdtempSync(path.join(os.tmpdir(), 'kibo-fake-camera-a-'));
  const secondDirectory = mkdtempSync(path.join(os.tmpdir(), 'kibo-fake-camera-b-'));

  try {
    // Determinism contract covers the static clip used by reproduction/CI.
    // Full multi-profile generation remains available via generate-fake-camera.mjs.
    const firstGeneration = generateAllFakeCameraFixtures(firstDirectory);
    const secondGeneration = generateAllFakeCameraFixtures(secondDirectory);
    const profileName = 'static-tag36h11-0';

    assert.equal(
      firstGeneration[profileName].y4mSha256,
      secondGeneration[profileName].y4mSha256,
      `${profileName} Y4M SHA must be stable across runs`);

    const firstManifest = JSON.parse(
      readFileSync(firstGeneration[profileName].outputManifest, 'utf8'));
    const secondManifest = JSON.parse(
      readFileSync(secondGeneration[profileName].outputManifest, 'utf8'));

    assert.equal(firstManifest.generatorVersion, '1');
    assert.equal(firstManifest.frameCount, secondManifest.frameCount);
    assert.equal(firstManifest.framesPerSecond, 30);
    assert.equal(firstManifest.y4mSha256, secondManifest.y4mSha256);
    assert.ok(Array.isArray(firstManifest.cornerTrajectory));
    assert.equal(
      firstManifest.cornerTrajectory.length,
      firstManifest.frameCount);
  } finally {
    rmSync(firstDirectory, { recursive: true, force: true });
    rmSync(secondDirectory, { recursive: true, force: true });
  }
}

assertDeterministicGeneration();
console.log('generate-fake-camera determinism: PASS');
console.log(`repositoryRoot=${REPOSITORY_ROOT}`);
