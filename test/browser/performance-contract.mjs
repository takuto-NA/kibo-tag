/**
 * Responsibility: single source of truth for P0 symptom and candidate clearance predicates.
 */

export const PRESENTATION_DETECT_RATIO_MIN = 0.85;
export const PRESENTATION_DETECT_RATIO_MAX = 1.15;
export const MAX_CAMERA_PRESENTED_FPS_WHEN_GATED = 25;
/** Relative canvas lift vs same-host gated reference (10%). */
export const MIN_CANVAS_IMPROVEMENT_OVER_GATED = 1.1;
/** Max retries for a single-shot candidate measurement when host noise yields zero skips. */
export const MAX_CANDIDATE_MEASUREMENT_ATTEMPTS = 3;
export const REQUIRED_SYMPTOM_RUNS = 4;
export const REQUIRED_CLEARED_RUNS = 3;
export const TOTAL_SYMPTOM_RUNS = 5;
export const PRIMARY_SYMPTOM_NAME = 'detectGatedPresentationUnderLoad';
export const CANDIDATE_CLEARANCE_PREDICATE = (
  'detectInFlightSkips > 0 && canvasPresentedFps > gatedReference * 1.1'
);

export function hitsPrimarySymptom(summary) {
  const ratio = summary.presentationDetectRatio;
  const presentedFps = summary.cameraPresentedFps;
  const detectFps = summary.detectThroughputFps;
  return (
    ratio !== null
    && ratio >= PRESENTATION_DETECT_RATIO_MIN
    && ratio <= PRESENTATION_DETECT_RATIO_MAX
    && presentedFps < MAX_CAMERA_PRESENTED_FPS_WHEN_GATED
    && detectFps > 0
    && detectFps < MAX_CAMERA_PRESENTED_FPS_WHEN_GATED
  );
}

export function clearsPrimarySymptom(summary, gatedReferenceCanvasPresentedFps) {
  // Guard: gated reference must be positive or relative improvement is undefined.
  if (!(gatedReferenceCanvasPresentedFps > 0)) {
    return false;
  }
  const detectInFlightSkips = summary.counters?.detectInFlightSkips ?? 0;
  const canvasImproved = (
    summary.canvasPresentedFps
    > gatedReferenceCanvasPresentedFps * MIN_CANVAS_IMPROVEMENT_OVER_GATED
  );
  // Structural proof of decoupled latest-frame path plus user-visible canvas lift vs P0.
  return detectInFlightSkips > 0 && canvasImproved;
}
