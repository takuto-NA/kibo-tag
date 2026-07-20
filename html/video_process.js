/**
 * Responsibility: wire the browser camera demo — init detector, rAF loop, and pagehide stop.
 */

import * as Comlink from "https://unpkg.com/comlink/dist/esm/comlink.mjs";

import {
  getCurrentDetectorSettings,
  initializeDetectorSettingsForDemo,
  isDetectorSettingsApplyInProgress,
} from './detector_settings.js';
import {
  detectTagsInGrayscaleFrame,
  rgbaPixelsToGrayscale,
} from './frame_pipeline.js';
import { drawDetectionOverlays } from './detection_overlay.js';
import {
  loadSavedDetectionIntoPage,
  saveNextDetectionToLocalStorage,
} from './demo_storage.js';
import { installPipelineMetricsOnWindow } from './pipeline_metrics.js';

const SCHEDULING_MODE_DECOUPLED = 'decoupled';
const SCHEDULING_MODE_GATED = 'gated';

var detections = [];
var imgSaveRequested = 0;
var videoProcessingActive = true;
const pipelineMetrics = installPipelineMetricsOnWindow(window);
let latestPresentedMediaTimeSeconds = 0;
let sourceFrameCallbackHandle = null;
let detectInFlight = false;

function getSchedulingMode() {
  // Production default is decoupled. Tests may force gated to re-capture P0 symptom RED.
  if (window.__kiboPipelineSchedulingMode === SCHEDULING_MODE_GATED) {
    return SCHEDULING_MODE_GATED;
  }
  return SCHEDULING_MODE_DECOUPLED;
}

function registerVideoProcessingReleaseOnPageExit() {
  window.addEventListener('pagehide', function(pageHideEvent) {
    // Guard: bfcache restore keeps the page alive; the detection loop may resume with the page.
    if (pageHideEvent.persisted) {
      return;
    }
    videoProcessingActive = false;
    if (sourceFrameCallbackHandle !== null && typeof video.cancelVideoFrameCallback === 'function') {
      video.cancelVideoFrameCallback(sourceFrameCallbackHandle);
      sourceFrameCallbackHandle = null;
    }
  });
}

registerVideoProcessingReleaseOnPageExit();

window.onload = () => {
  init();
  loadSavedDetectionIntoPage('saved_det');
};

function readCameraInfoFromWindow() {
  if (typeof window.cameraInfo === 'undefined' || window.cameraInfo === null) {
    return null;
  }
  const cameraMatrix = window.cameraInfo.camera_matrix;
  if (!cameraMatrix) {
    return null;
  }
  return {
    focalLengthX: cameraMatrix[0][0],
    focalLengthY: cameraMatrix[1][1],
    principalPointX: cameraMatrix[0][2],
    principalPointY: cameraMatrix[1][2],
  };
}

async function applyCameraInfoToApriltagDetector() {
  const cameraInfo = readCameraInfoFromWindow();
  if (cameraInfo === null || typeof window.apriltag === 'undefined') {
    return;
  }
  await window.apriltag.set_camera_info(
    cameraInfo.focalLengthX,
    cameraInfo.focalLengthY,
    cameraInfo.principalPointX,
    cameraInfo.principalPointY);
}

function registerCameraInfoChangeListener() {
  const cameraInfoTextArea = document.getElementById('camera_info');
  if (cameraInfoTextArea === null) {
    return;
  }
  cameraInfoTextArea.addEventListener('change', function() {
    applyCameraInfoToApriltagDetector().catch((cameraInfoError) => {
      console.log(cameraInfoError);
    });
  });
}

function registerSourceFramePresentationObserver() {
  // Guard: requestVideoFrameCallback is required for source-video FPS; fallback keeps detect loop alive.
  if (typeof video.requestVideoFrameCallback !== 'function') {
    return;
  }

  const onPresentedFrame = (_now, metadata) => {
    if (!videoProcessingActive) {
      return;
    }
    latestPresentedMediaTimeSeconds = metadata.mediaTime;
    pipelineMetrics.markSourceVideoFramePresented(metadata.mediaTime, performance.now());
    sourceFrameCallbackHandle = video.requestVideoFrameCallback(onPresentedFrame);
  };
  sourceFrameCallbackHandle = video.requestVideoFrameCallback(onPresentedFrame);
}

async function init() {
  const Apriltag = Comlink.wrap(new Worker("apriltag.js"));
  let resolveDetectorReady;
  const detectorReadyPromise = new Promise((resolve) => {
    resolveDetectorReady = resolve;
  });

  window.apriltag = await new Apriltag(Comlink.proxy(resolveDetectorReady));
  await detectorReadyPromise;
  await applyCameraInfoToApriltagDetector();
  registerCameraInfoChangeListener();
  await initializeDetectorSettingsForDemo(() => {
    detections = [];
  });
  registerSourceFramePresentationObserver();
  window.requestAnimationFrame(process_frame);
}

async function runDetectionForFrame(grayscalePixels, frameWidth, frameHeight, scheduleNextFrameAfterDetect) {
  const currentSettings = getCurrentDetectorSettings();
  pipelineMetrics.markFrameSubmitted();
  const detectStartedAtMilliseconds = performance.now();
  try {
    const nextDetections = await detectTagsInGrayscaleFrame(
      apriltag,
      grayscalePixels,
      frameWidth,
      frameHeight,
      currentSettings.minimumDecisionMargin);
    const detectWallMilliseconds = performance.now() - detectStartedAtMilliseconds;
    pipelineMetrics.markStageDuration('detectWallMilliseconds', detectWallMilliseconds);
    pipelineMetrics.markDetectionCompleted({
      tagCount: nextDetections.length,
      detectWallMilliseconds,
      nowMilliseconds: performance.now(),
    });
    detections = nextDetections;
    window.__kiboLastDetectionIds = detections.map((detection) => detection.id);
    window.__kiboLastDetections = detections;

    if (imgSaveRequested && detections.length > 0) {
      const ctx = canvas.getContext('2d');
      saveNextDetectionToLocalStorage(ctx, detections, () => {
        buttonToggle();
        loadSavedDetectionIntoPage('saved_det');
      });
    }
  } catch (detectionError) {
    console.log(detectionError);
    detections = [];
    pipelineMetrics.markFrameDropped();
  } finally {
    detectInFlight = false;
    if (scheduleNextFrameAfterDetect && videoProcessingActive) {
      window.requestAnimationFrame(process_frame);
    }
  }
}

function presentOverlayOnly(ctx) {
  const overlayStartedAtMilliseconds = performance.now();
  drawDetectionOverlays(ctx, detections);
  pipelineMetrics.markStageDuration(
    'overlayMilliseconds',
    performance.now() - overlayStartedAtMilliseconds);
}

async function process_frame() {
  if (!videoProcessingActive) {
    return;
  }

  const schedulingMode = getSchedulingMode();
  // Guard: assigning canvas.width/height clears the bitmap; only resize on dimension changes.
  if (canvas.width !== video.videoWidth) {
    canvas.width = video.videoWidth;
  }
  if (canvas.height !== video.videoHeight) {
    canvas.height = video.videoHeight;
  }
  let ctx = canvas.getContext("2d");

  const drawReadbackStartedAtMilliseconds = performance.now();
  try {
    ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
  } catch (err) {
    console.log("Failed to get video frame. Video not started ?");
    setTimeout(process_frame, 500);
    return;
  }

  if (schedulingMode === SCHEDULING_MODE_DECOUPLED) {
    // While detect is in flight, keep canvas-only presentation: drawImage + overlay only
    // (no getImageData/grayscale). The <video> element stays display:none.
    if (detectInFlight || isDetectorSettingsApplyInProgress()) {
      pipelineMetrics.markStageDuration(
        'drawReadbackMilliseconds',
        performance.now() - drawReadbackStartedAtMilliseconds);
      pipelineMetrics.markCanvasFramePresented(performance.now());
      presentOverlayOnly(ctx);
      if (detectInFlight) {
        pipelineMetrics.markDetectInFlightSkip();
      }
      window.__kiboPresentationLoopCount = (window.__kiboPresentationLoopCount || 0) + 1;
      window.requestAnimationFrame(process_frame);
      return;
    }
  }

  let imageData;
  try {
    imageData = ctx.getImageData(0, 0, ctx.canvas.width, ctx.canvas.height);
  } catch (err) {
    console.log("Failed to get video frame. Video not started ?");
    setTimeout(process_frame, 500);
    return;
  }
  pipelineMetrics.markStageDuration(
    'drawReadbackMilliseconds',
    performance.now() - drawReadbackStartedAtMilliseconds);
  // Canvas is the user-visible presentation surface (video element is display:none).
  pipelineMetrics.markCanvasFramePresented(performance.now());
  window.__kiboPresentationLoopCount = (window.__kiboPresentationLoopCount || 0) + 1;

  const rootCauseExperimentsEnabled = window.__kiboEnableRootCauseExperiments === true;
  // Guard: root-cause A/B hooks stay off unless the experiment harness opts in.
  if (rootCauseExperimentsEnabled && window.__kiboExperimentSkipGrayscaleAndDetect) {
    window.requestAnimationFrame(process_frame);
    return;
  }

  const grayscaleStartedAtMilliseconds = performance.now();
  const grayscalePixels = rgbaPixelsToGrayscale(
    imageData.data,
    ctx.canvas.width * ctx.canvas.height);
  pipelineMetrics.markStageDuration(
    'grayscaleMilliseconds',
    performance.now() - grayscaleStartedAtMilliseconds);

  if (isDetectorSettingsApplyInProgress()) {
    window.requestAnimationFrame(process_frame);
    return;
  }

  presentOverlayOnly(ctx);

  if (rootCauseExperimentsEnabled && window.__kiboExperimentSkipDetect) {
    window.requestAnimationFrame(process_frame);
    return;
  }

  if (schedulingMode === SCHEDULING_MODE_GATED) {
    // P0 / baseline behavior: next presentation waits for detect completion.
    detectInFlight = true;
    await runDetectionForFrame(
      grayscalePixels,
      ctx.canvas.width,
      ctx.canvas.height,
      true);
    return;
  }

  // Decoupled latest-frame scheduling: next presentation is not gated on detect completion.
  window.requestAnimationFrame(process_frame);
  detectInFlight = true;
  runDetectionForFrame(
    grayscalePixels,
    ctx.canvas.width,
    ctx.canvas.height,
    false);
}

var button = document.getElementById('req_save');
button.addEventListener('click', function() {
  buttonToggle();
});

function buttonToggle() {
  if (!imgSaveRequested) {
    button.innerHTML = 'Saving next detection...';
    imgSaveRequested = 1;
    return;
  }
  button.innerHTML = 'Save next detection (local storage)';
  imgSaveRequested = 0;
}

window.__kiboLatestPresentedMediaTimeSeconds = () => latestPresentedMediaTimeSeconds;
window.__kiboGetPipelineSchedulingMode = getSchedulingMode;
