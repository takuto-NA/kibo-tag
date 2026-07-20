/**
 * Responsibility: thin npm wrapper that runs the dual Playwright symptom gates
 * (P0 RED + candidate clearance) which write the locked contract artifacts.
 */

import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  PRIMARY_SYMPTOM_NAME,
} from '../test/browser/performance-contract.mjs';

const REPOSITORY_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const LOCKED_ARTIFACT_DIRECTORY = path.join(
  REPOSITORY_ROOT,
  'artifacts',
  'symptom-reproduction');

const playwrightResult = spawnSync(
  process.platform === 'win32' ? 'npx.cmd' : 'npx',
  ['playwright', 'test', 'test/browser/symptom-reproduction.spec.mjs', '--reporter=line'],
  {
    cwd: REPOSITORY_ROOT,
    encoding: 'utf8',
    stdio: 'inherit',
  });

console.log(JSON.stringify({
  exitCode: playwrightResult.status,
  primarySymptom: PRIMARY_SYMPTOM_NAME,
  contractVersion: 1,
  artifactDirectory: LOCKED_ARTIFACT_DIRECTORY,
  p0Artifact: path.join(LOCKED_ARTIFACT_DIRECTORY, 'contract-v1-runs.json'),
  candidateArtifact: path.join(LOCKED_ARTIFACT_DIRECTORY, 'contract-v1-candidate-runs.json'),
}, null, 2));

process.exit(playwrightResult.status ?? 1);
