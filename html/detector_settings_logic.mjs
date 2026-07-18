/**
 * Pure detector-settings helpers for the browser camera demo (no DOM).
 * Importable from Node for unit tests and from the demo for runtime use.
 */

export const DEFAULT_TAG_FAMILY_NAME = 'tag36h11';
export const ARUCO_4X4_100_FAMILY_NAME = 'DICT_4X4_100';
export const TAG36H11_BITS_CORRECTED = 1;
export const ARUCO_4X4_100_BITS_CORRECTED = 0;
export const DEFAULT_TAG_SIZE_METERS = 0.15;
export const TAG36H11_MINIMUM_DECISION_MARGIN = 0;
export const ARUCO_4X4_100_MINIMUM_DECISION_MARGIN = 50;
export const MIN_BITS_CORRECTED = 0;
export const MAX_BITS_CORRECTED = 2;
export const MIN_TAG_SIZE_METERS = 0.01;
export const MIN_DECISION_MARGIN = 0;

export const DETECTOR_FAMILY_SETTINGS = {
  [DEFAULT_TAG_FAMILY_NAME]: {
    familyName: DEFAULT_TAG_FAMILY_NAME,
    bitsCorrected: TAG36H11_BITS_CORRECTED,
    minimumDecisionMargin: TAG36H11_MINIMUM_DECISION_MARGIN,
  },
  [ARUCO_4X4_100_FAMILY_NAME]: {
    familyName: ARUCO_4X4_100_FAMILY_NAME,
    bitsCorrected: ARUCO_4X4_100_BITS_CORRECTED,
    minimumDecisionMargin: ARUCO_4X4_100_MINIMUM_DECISION_MARGIN,
  },
};

export const BROWSER_DEMO_MAX_DETECTIONS = 32;
export const BROWSER_DEMO_RETURN_POSE = 1;
export const BROWSER_DEMO_RETURN_SOLUTIONS = 0;

export function detectorFamilySettingsFor(familyName) {
  return DETECTOR_FAMILY_SETTINGS[familyName] || DETECTOR_FAMILY_SETTINGS[DEFAULT_TAG_FAMILY_NAME];
}

export function detectorSettingsDefaults() {
  const defaultFamilySettings = detectorFamilySettingsFor(DEFAULT_TAG_FAMILY_NAME);
  return {
    familyName: defaultFamilySettings.familyName,
    bitsCorrected: defaultFamilySettings.bitsCorrected,
    tagSizeMeters: DEFAULT_TAG_SIZE_METERS,
    minimumDecisionMargin: defaultFamilySettings.minimumDecisionMargin,
  };
}

export function numberFromRawValue(rawValue, defaultValue) {
  if (rawValue === null || rawValue === undefined) {
    return defaultValue;
  }

  const trimmedValue = String(rawValue).trim();
  if (trimmedValue === '') {
    return defaultValue;
  }

  const parsedValue = Number(trimmedValue);
  if (!Number.isFinite(parsedValue)) {
    return defaultValue;
  }

  return parsedValue;
}

export function bitsCorrectedFromRawValue(rawValue, defaultValue) {
  const parsedBitsCorrected = Math.trunc(numberFromRawValue(rawValue, defaultValue));
  if (parsedBitsCorrected < MIN_BITS_CORRECTED || parsedBitsCorrected > MAX_BITS_CORRECTED) {
    return defaultValue;
  }
  return parsedBitsCorrected;
}

export function tagSizeMetersFromRawValue(rawValue, defaultValue) {
  const parsedTagSizeMeters = numberFromRawValue(rawValue, defaultValue);
  if (parsedTagSizeMeters < MIN_TAG_SIZE_METERS) {
    return defaultValue;
  }
  return parsedTagSizeMeters;
}

export function minimumDecisionMarginFromRawValue(rawValue, defaultValue) {
  const parsedMinimumDecisionMargin = numberFromRawValue(rawValue, defaultValue);
  if (parsedMinimumDecisionMargin < MIN_DECISION_MARGIN) {
    return defaultValue;
  }
  return parsedMinimumDecisionMargin;
}

export function filterDetectionsByDecisionMargin(rawDetections, minimumDecisionMargin) {
  return rawDetections.filter((detection) => (
    typeof detection.decision_margin !== 'number'
    || detection.decision_margin >= minimumDecisionMargin
  ));
}

export function recommendedControlsForFamily(familyName) {
  const recommendedSettings = detectorFamilySettingsFor(familyName);
  return {
    bitsCorrected: recommendedSettings.bitsCorrected,
    minimumDecisionMargin: recommendedSettings.minimumDecisionMargin,
  };
}
