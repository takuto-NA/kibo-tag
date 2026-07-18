/**
 * Responsibility: read/write detector settings from the demo DOM, keep an in-memory
 * current settings object, and apply family/size/options to the WASM detector.
 */

import {
  BROWSER_DEMO_MAX_DETECTIONS,
  BROWSER_DEMO_RETURN_POSE,
  BROWSER_DEMO_RETURN_SOLUTIONS,
  DEFAULT_TAG_FAMILY_NAME,
  bitsCorrectedFromRawValue,
  detectorFamilySettingsFor,
  detectorSettingsDefaults,
  minimumDecisionMarginFromRawValue,
  recommendedControlsForFamily,
  tagSizeMetersFromRawValue,
} from './detector_settings_logic.mjs';

const DETECTOR_FAMILY_SELECT_ID = 'detector_family';
const DETECTOR_BITS_CORRECTED_SELECT_ID = 'detector_bits_corrected';
const DETECTOR_TAG_SIZE_INPUT_ID = 'detector_tag_size_meters';
const DETECTOR_MIN_DECISION_MARGIN_INPUT_ID = 'detector_min_decision_margin';
const DETECTOR_STATUS_ID = 'detector_status';

let currentDetectorSettings = detectorSettingsDefaults();
let detectorSettingsApplyChain = Promise.resolve();
let detectorSettingsApplyInProgress = false;

export function getCurrentDetectorSettings() {
  return { ...currentDetectorSettings };
}

export function isDetectorSettingsApplyInProgress() {
  return detectorSettingsApplyInProgress;
}

function updateDetectorStatus(message) {
  const detectorStatus = document.getElementById(DETECTOR_STATUS_ID);
  if (detectorStatus === null) {
    return;
  }
  detectorStatus.textContent = message;
}

function readDetectorSettingsFromPage() {
  const settings = detectorSettingsDefaults();
  const familySelect = document.getElementById(DETECTOR_FAMILY_SELECT_ID);
  const bitsCorrectedSelect = document.getElementById(DETECTOR_BITS_CORRECTED_SELECT_ID);
  const tagSizeInput = document.getElementById(DETECTOR_TAG_SIZE_INPUT_ID);
  const minimumDecisionMarginInput = document.getElementById(DETECTOR_MIN_DECISION_MARGIN_INPUT_ID);

  if (familySelect !== null && familySelect.value !== '') {
    const familySettings = detectorFamilySettingsFor(familySelect.value);
    settings.familyName = familySettings.familyName;
    settings.bitsCorrected = familySettings.bitsCorrected;
    settings.minimumDecisionMargin = familySettings.minimumDecisionMargin;
  }

  settings.bitsCorrected = bitsCorrectedFromRawValue(
    bitsCorrectedSelect !== null ? bitsCorrectedSelect.value : null,
    settings.bitsCorrected);
  settings.tagSizeMeters = tagSizeMetersFromRawValue(
    tagSizeInput !== null ? tagSizeInput.value : null,
    settings.tagSizeMeters);
  settings.minimumDecisionMargin = minimumDecisionMarginFromRawValue(
    minimumDecisionMarginInput !== null ? minimumDecisionMarginInput.value : null,
    settings.minimumDecisionMargin);

  return settings;
}

function applyRecommendedControlsForSelectedFamily() {
  const familySelect = document.getElementById(DETECTOR_FAMILY_SELECT_ID);
  const bitsCorrectedSelect = document.getElementById(DETECTOR_BITS_CORRECTED_SELECT_ID);
  const minimumDecisionMarginInput = document.getElementById(DETECTOR_MIN_DECISION_MARGIN_INPUT_ID);
  const selectedFamilyName = familySelect !== null ? familySelect.value : DEFAULT_TAG_FAMILY_NAME;
  const recommendedSettings = recommendedControlsForFamily(selectedFamilyName);

  if (bitsCorrectedSelect !== null) {
    bitsCorrectedSelect.value = String(recommendedSettings.bitsCorrected);
  }
  if (minimumDecisionMarginInput !== null) {
    minimumDecisionMarginInput.value = String(recommendedSettings.minimumDecisionMargin);
  }
}

async function applyBrowserDemoConservativeDetectorOptions() {
  await window.apriltag.set_max_detections(BROWSER_DEMO_MAX_DETECTIONS);
  await window.apriltag.set_return_pose(BROWSER_DEMO_RETURN_POSE);
  await window.apriltag.set_return_solutions(BROWSER_DEMO_RETURN_SOLUTIONS);
}

async function applyDetectorSettingsToApriltagDetector() {
  // Guard: detector settings can be changed before the worker-backed detector is ready.
  if (typeof window.apriltag === 'undefined') {
    return;
  }

  const settings = readDetectorSettingsFromPage();
  currentDetectorSettings = settings;
  await window.apriltag.set_tag_family(settings.familyName, settings.bitsCorrected);
  await window.apriltag.set_all_tag_sizes(settings.tagSizeMeters);
  updateDetectorStatus(
    `Detecting ${settings.familyName} with ${settings.bitsCorrected} corrected bit(s); tag size ${settings.tagSizeMeters} m; minimum decision margin ${settings.minimumDecisionMargin}.`);
}

export function queueDetectorSettingsApply(onSettingsApplied) {
  detectorSettingsApplyChain = detectorSettingsApplyChain
    .catch((previousConfigurationError) => {
      console.log(previousConfigurationError);
    })
    .then(async () => {
      detectorSettingsApplyInProgress = true;
      try {
        await applyDetectorSettingsToApriltagDetector();
        if (typeof onSettingsApplied === 'function') {
          onSettingsApplied();
        }
      } finally {
        detectorSettingsApplyInProgress = false;
      }
    });

  return detectorSettingsApplyChain;
}

export function registerDetectorSettingsChangeListener(onSettingsApplied) {
  const familySelect = document.getElementById(DETECTOR_FAMILY_SELECT_ID);
  const detectorSettingElementIds = [
    DETECTOR_BITS_CORRECTED_SELECT_ID,
    DETECTOR_TAG_SIZE_INPUT_ID,
    DETECTOR_MIN_DECISION_MARGIN_INPUT_ID,
  ];

  if (familySelect !== null) {
    familySelect.addEventListener('change', function() {
      applyRecommendedControlsForSelectedFamily();
      queueDetectorSettingsApply(onSettingsApplied).catch((configurationError) => {
        console.log(configurationError);
        updateDetectorStatus(configurationError.message);
      });
    });
  }

  detectorSettingElementIds.forEach((elementId) => {
    const detectorSettingElement = document.getElementById(elementId);
    if (detectorSettingElement === null) {
      return;
    }

    detectorSettingElement.addEventListener('change', function() {
      queueDetectorSettingsApply(onSettingsApplied).catch((configurationError) => {
        console.log(configurationError);
        updateDetectorStatus(configurationError.message);
      });
    });
  });
}

export async function initializeDetectorSettingsForDemo(onSettingsApplied) {
  applyRecommendedControlsForSelectedFamily();
  await applyBrowserDemoConservativeDetectorOptions();
  await queueDetectorSettingsApply(onSettingsApplied);
  registerDetectorSettingsChangeListener(onSettingsApplied);
}

