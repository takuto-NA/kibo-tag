/**
 * Responsibility: unit-test pure detector settings helpers without a browser DOM.
 */

import assert from 'node:assert/strict';
import {
  ARUCO_4X4_100_BITS_CORRECTED,
  ARUCO_4X4_100_MINIMUM_DECISION_MARGIN,
  DEFAULT_FAILED_QUAD_MIN_DECISION_MARGIN,
  DEFAULT_SHOW_FAILED_QUADS,
  DEFAULT_TAG_FAMILY_NAME,
  bitsCorrectedFromRawValue,
  detectorSettingsDefaults,
  filterDetectionsByDecisionMargin,
  filterFailedQuads,
  recommendedControlsForFamily,
  tagSizeMetersFromRawValue,
} from '../html/detector_settings_logic.mjs';

assert.equal(bitsCorrectedFromRawValue('2', 1), 2);
assert.equal(bitsCorrectedFromRawValue('9', 1), 1);
assert.equal(tagSizeMetersFromRawValue('0.2', 0.15), 0.2);
assert.equal(tagSizeMetersFromRawValue('0', 0.15), 0.15);

const recommendedAruco = recommendedControlsForFamily('DICT_4X4_100');
assert.equal(recommendedAruco.bitsCorrected, ARUCO_4X4_100_BITS_CORRECTED);
assert.equal(recommendedAruco.minimumDecisionMargin, ARUCO_4X4_100_MINIMUM_DECISION_MARGIN);

const recommendedDefault = recommendedControlsForFamily('unknown');
assert.equal(recommendedDefault.bitsCorrected, 1);
assert.equal(recommendedDefault.minimumDecisionMargin, 0);
assert.equal(DEFAULT_TAG_FAMILY_NAME, 'tag36h11');

const filtered = filterDetectionsByDecisionMargin(
  [
    { id: 1, decision_margin: 10 },
    { id: 2, decision_margin: 80 },
    { id: 3 },
  ],
  50);
assert.deepEqual(filtered.map((detection) => detection.id), [2, 3]);

const defaults = detectorSettingsDefaults();
assert.equal(defaults.showFailedQuads, DEFAULT_SHOW_FAILED_QUADS);
assert.equal(defaults.showFailedQuads, false);
assert.equal(defaults.failedQuadMinDecisionMargin, DEFAULT_FAILED_QUAD_MIN_DECISION_MARGIN);

const failedQuadSamples = [
  { decision_margin: -1, corners: [1] },
  { decision_margin: -5, corners: [2] },
  { decision_margin: -40, corners: [3] },
];
assert.deepEqual(
  filterFailedQuads(failedQuadSamples, false, -20).map((failedQuad) => failedQuad.decision_margin),
  [-5]);
assert.equal(filterFailedQuads(failedQuadSamples, true, -20).length, 3);

console.log('detector_settings_logic.test.mjs: PASS');
