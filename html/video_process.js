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
import {
  drawDetectionOverlays,
  loadSavedDetectionIntoPage,
  saveNextDetectionToLocalStorage,
} from './detection_overlay.js';

var detections = [];
var imgSaveRequested = 0;
var videoProcessingActive = true;

function registerVideoProcessingReleaseOnPageExit() {
  window.addEventListener('pagehide', function(pageHideEvent) {
    // Guard: bfcache restore keeps the page alive; the detection loop may resume with the page.
    if (pageHideEvent.persisted) {
      return;
    }
    videoProcessingActive = false;
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
  window.requestAnimationFrame(process_frame);
}

async function process_frame() {
  if (!videoProcessingActive) {
    return;
  }

  canvas.width = video.videoWidth;
  canvas.height = video.videoHeight;
  let ctx = canvas.getContext("2d");

  let imageData;
  try {
    ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
    imageData = ctx.getImageData(0, 0, ctx.canvas.width, ctx.canvas.height);
  } catch (err) {
    console.log("Failed to get video frame. Video not started ?");
    setTimeout(process_frame, 500);
    return;
  }

  const grayscalePixels = rgbaPixelsToGrayscale(
    imageData.data,
    ctx.canvas.width * ctx.canvas.height);
  ctx.putImageData(imageData, 0, 0);

  if (isDetectorSettingsApplyInProgress()) {
    window.requestAnimationFrame(process_frame);
    return;
  }

  drawDetectionOverlays(ctx, detections);

  try {
    const currentSettings = getCurrentDetectorSettings();
    detections = await detectTagsInGrayscaleFrame(
      apriltag,
      grayscalePixels,
      ctx.canvas.width,
      ctx.canvas.height,
      currentSettings.minimumDecisionMargin);
  } catch (detectionError) {
    console.log(detectionError);
    detections = [];
    window.requestAnimationFrame(process_frame);
    return;
  }

  if (imgSaveRequested && detections.length > 0) {
    saveNextDetectionToLocalStorage(ctx, detections, () => {
      buttonToggle();
      loadSavedDetectionIntoPage('saved_det');
    });
  }

  window.requestAnimationFrame(process_frame);
}

var button = document.getElementById('req_save');
button.addEventListener('click', function() {
  buttonToggle();
});

function buttonToggle() {
  if (imgSaveRequested == 0) {
    button.innerHTML = "Saving next detection... (press to cancel)";
    imgSaveRequested = 1;
    button.classList.add("active");
  } else {
    button.innerHTML = "Save next detection (local storage)";
    imgSaveRequested = 0;
    button.classList.remove("active");
  }
}
