/**
 * Responsibility: generate deterministic fake-camera Y4M fixtures and manifests for Chromium.
 */

import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const SCRIPT_DIRECTORY = path.dirname(fileURLToPath(import.meta.url));
const REPOSITORY_ROOT = path.resolve(SCRIPT_DIRECTORY, '..');
const PYTHON_WRITER = path.join(SCRIPT_DIRECTORY, 'lib', 'y4m_writer.py');
const DEFAULT_OUTPUT_DIRECTORY = path.join(REPOSITORY_ROOT, 'test', 'fixtures', 'fake-camera');
const DEFAULT_SOURCE_JPEG = path.join(
  REPOSITORY_ROOT,
  'test',
  'tag-imgs',
  'single_tag_0_1.jpg');

const FIXTURE_PROFILES = {
  'static-tag36h11-0': {
    sourceJpeg: DEFAULT_SOURCE_JPEG,
    seed: 1,
    // Chromium fake-capture loops the file; keep clips short for fast generation and load.
    frameCount: 30,
    framesPerSecond: 30,
    motion: 'static',
    expectedFamily: 'tag36h11',
    expectedTagIds: '0',
  },
  'moving-tag36h11-0': {
    sourceJpeg: DEFAULT_SOURCE_JPEG,
    seed: 2,
    frameCount: 30,
    framesPerSecond: 30,
    motion: 'horizontal',
    expectedFamily: 'tag36h11',
    expectedTagIds: '0',
  },
  'empty-scene': {
    sourceJpeg: null,
    scene: 'empty',
    seed: 3,
    frameCount: 30,
    framesPerSecond: 30,
    motion: 'static',
    expectedFamily: 'tag36h11',
    expectedTagIds: '',
  },
};

function sha256Buffer(buffer) {
  return createHash('sha256').update(buffer).digest('hex');
}

function runPythonWriter(profileName, profile, outputDirectory) {
  const outputY4m = path.join(outputDirectory, `${profileName}.y4m`);
  const outputManifest = path.join(outputDirectory, `${profileName}.manifest.json`);
  const argumentsForWriter = [
    PYTHON_WRITER,
    '--output-y4m', outputY4m,
    '--output-manifest', outputManifest,
    '--seed', String(profile.seed),
    '--scene', profile.scene || 'tag',
    '--frame-count', String(profile.frameCount),
    '--frames-per-second', String(profile.framesPerSecond),
    '--motion', profile.motion,
    '--expected-family', profile.expectedFamily,
    '--expected-tag-ids', profile.expectedTagIds,
  ];
  if (profile.sourceJpeg) {
    argumentsForWriter.push('--source-jpeg', profile.sourceJpeg);
  }

  const pythonResult = spawnSync('python', argumentsForWriter, {
    cwd: REPOSITORY_ROOT,
    encoding: 'utf8',
  });
  if (pythonResult.status !== 0) {
    throw new Error(
      `Y4M writer failed for ${profileName}: ${pythonResult.stderr || pythonResult.stdout}`);
  }

  const y4mBytes = readFileSync(outputY4m);
  const manifest = JSON.parse(readFileSync(outputManifest, 'utf8'));
  return {
    profileName,
    outputY4m,
    outputManifest,
    y4mSha256: sha256Buffer(y4mBytes),
    manifestSha256: sha256Buffer(Buffer.from(JSON.stringify(manifest), 'utf8')),
    manifest,
  };
}

export function generateAllFakeCameraFixtures(outputDirectory = DEFAULT_OUTPUT_DIRECTORY) {
  mkdirSync(outputDirectory, { recursive: true });
  const generated = {};
  for (const [profileName, profile] of Object.entries(FIXTURE_PROFILES)) {
    generated[profileName] = runPythonWriter(profileName, profile, outputDirectory);
  }

  const indexPath = path.join(outputDirectory, 'index.json');
  const indexPayload = {
    generatorVersion: '1',
    fixtures: Object.fromEntries(
      Object.entries(generated).map(([name, value]) => [name, {
        y4m: path.relative(REPOSITORY_ROOT, value.outputY4m).replaceAll('\\', '/'),
        manifest: path.relative(REPOSITORY_ROOT, value.outputManifest).replaceAll('\\', '/'),
        y4mSha256: value.y4mSha256,
      }])),
  };
  writeFileSync(indexPath, `${JSON.stringify(indexPayload, null, 2)}\n`, 'utf8');
  return generated;
}

const isExecutedDirectly = process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isExecutedDirectly) {
  const generated = generateAllFakeCameraFixtures();
  for (const [name, value] of Object.entries(generated)) {
    console.log(`${name}: ${value.y4mSha256}`);
  }
}
