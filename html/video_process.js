import * as Comlink from "https://unpkg.com/comlink/dist/esm/comlink.mjs";

import * as Base64 from "./base64.js";

const DEFAULT_TAG_FAMILY_NAME = 'tag36h11';
const ARUCO_4X4_100_FAMILY_NAME = 'DICT_4X4_100';
const TAG36H11_BITS_CORRECTED = 1;
const ARUCO_4X4_100_BITS_CORRECTED = 0;
const DEFAULT_TAG_SIZE_METERS = 0.15;
const TAG36H11_MINIMUM_DECISION_MARGIN = 0;
const ARUCO_4X4_100_MINIMUM_DECISION_MARGIN = 50;
const MIN_BITS_CORRECTED = 0;
const MAX_BITS_CORRECTED = 2;
const MIN_TAG_SIZE_METERS = 0.01;
const MIN_DECISION_MARGIN = 0;
const DETECTOR_FAMILY_SELECT_ID = 'detector_family';
const DETECTOR_BITS_CORRECTED_SELECT_ID = 'detector_bits_corrected';
const DETECTOR_TAG_SIZE_INPUT_ID = 'detector_tag_size_meters';
const DETECTOR_MIN_DECISION_MARGIN_INPUT_ID = 'detector_min_decision_margin';
const DETECTOR_STATUS_ID = 'detector_status';
const TAG36H11_DEMO_TAG_IDS = [5];
const ARUCO_4X4_100_TAG_COUNT = 100;
const ARUCO_4X4_100_TAG_IDS = Array.from({ length: ARUCO_4X4_100_TAG_COUNT }, (_, tagId) => tagId);
const DETECTOR_FAMILY_SETTINGS = {
  [DEFAULT_TAG_FAMILY_NAME]: {
    familyName: DEFAULT_TAG_FAMILY_NAME,
    bitsCorrected: TAG36H11_BITS_CORRECTED,
    minimumDecisionMargin: TAG36H11_MINIMUM_DECISION_MARGIN,
    tagIds: TAG36H11_DEMO_TAG_IDS,
  },
  [ARUCO_4X4_100_FAMILY_NAME]: {
    familyName: ARUCO_4X4_100_FAMILY_NAME,
    bitsCorrected: ARUCO_4X4_100_BITS_CORRECTED,
    minimumDecisionMargin: ARUCO_4X4_100_MINIMUM_DECISION_MARGIN,
    tagIds: ARUCO_4X4_100_TAG_IDS,
  },
};

var detections = [];
var imgSaveRequested = 0;
var detectorSettingsApplyChain = Promise.resolve();
var detectorSettingsApplyInProgress = false;
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

  loadImg('saved_det');
}

function detectorSettingsDefaults() {
  const defaultFamilySettings = detectorFamilySettingsFor(DEFAULT_TAG_FAMILY_NAME);
  return {
    familyName: defaultFamilySettings.familyName,
    bitsCorrected: defaultFamilySettings.bitsCorrected,
    tagSizeMeters: DEFAULT_TAG_SIZE_METERS,
    minimumDecisionMargin: defaultFamilySettings.minimumDecisionMargin,
  };
}

function detectorFamilySettingsFor(familyName) {
  return DETECTOR_FAMILY_SETTINGS[familyName] || DETECTOR_FAMILY_SETTINGS[DEFAULT_TAG_FAMILY_NAME];
}

function numberFromInputValue(inputElement, defaultValue) {
  if (inputElement === null) {
    return defaultValue;
  }

  if (inputElement.value.trim() === '') {
    return defaultValue;
  }

  const parsedValue = Number(inputElement.value);
  if (!Number.isFinite(parsedValue)) {
    return defaultValue;
  }

  return parsedValue;
}

function bitsCorrectedFromInput(inputElement, defaultValue) {
  const parsedBitsCorrected = Math.trunc(numberFromInputValue(inputElement, defaultValue));
  if (parsedBitsCorrected < MIN_BITS_CORRECTED || parsedBitsCorrected > MAX_BITS_CORRECTED) {
    return defaultValue;
  }

  return parsedBitsCorrected;
}

function tagSizeMetersFromInput(inputElement, defaultValue) {
  const parsedTagSizeMeters = numberFromInputValue(inputElement, defaultValue);
  if (parsedTagSizeMeters < MIN_TAG_SIZE_METERS) {
    return defaultValue;
  }

  return parsedTagSizeMeters;
}

function minimumDecisionMarginFromInput(inputElement, defaultValue) {
  const parsedMinimumDecisionMargin = numberFromInputValue(inputElement, defaultValue);
  if (parsedMinimumDecisionMargin < MIN_DECISION_MARGIN) {
    return defaultValue;
  }

  return parsedMinimumDecisionMargin;
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

  settings.bitsCorrected = bitsCorrectedFromInput(bitsCorrectedSelect, settings.bitsCorrected);
  settings.tagSizeMeters = tagSizeMetersFromInput(tagSizeInput, settings.tagSizeMeters);
  settings.minimumDecisionMargin = minimumDecisionMarginFromInput(
    minimumDecisionMarginInput,
    settings.minimumDecisionMargin);

  return settings;
}

function applyRecommendedControlsForSelectedFamily() {
  const familySelect = document.getElementById(DETECTOR_FAMILY_SELECT_ID);
  const bitsCorrectedSelect = document.getElementById(DETECTOR_BITS_CORRECTED_SELECT_ID);
  const minimumDecisionMarginInput = document.getElementById(DETECTOR_MIN_DECISION_MARGIN_INPUT_ID);
  const selectedFamilyName = familySelect !== null ? familySelect.value : DEFAULT_TAG_FAMILY_NAME;
  const recommendedSettings = detectorFamilySettingsFor(selectedFamilyName);

  if (bitsCorrectedSelect !== null) {
    bitsCorrectedSelect.value = String(recommendedSettings.bitsCorrected);
  }

  if (minimumDecisionMarginInput !== null) {
    minimumDecisionMarginInput.value = String(recommendedSettings.minimumDecisionMargin);
  }
}

function tagIdsForFamily(familyName) {
  return detectorFamilySettingsFor(familyName).tagIds;
}

function updateDetectorStatus(message) {
  const detectorStatus = document.getElementById(DETECTOR_STATUS_ID);
  if (detectorStatus === null) {
    return;
  }

  detectorStatus.textContent = message;
}

async function applyTagSizeToActiveFamily(settings) {
  const tagIds = tagIdsForFamily(settings.familyName);
  const tagSizePromises = tagIds.map((tagId) => (
    window.apriltag.set_tag_size(tagId, settings.tagSizeMeters)
  ));
  await Promise.all(tagSizePromises);
}

async function applyDetectorSettingsToApriltagDetector() {
  // Guard: detector settings can be changed before the worker-backed detector is ready.
  if (typeof window.apriltag === 'undefined') {
    return;
  }

  const settings = readDetectorSettingsFromPage();
  await window.apriltag.set_tag_family(settings.familyName, settings.bitsCorrected);
  await applyTagSizeToActiveFamily(settings);
  detections = [];
  updateDetectorStatus(
    `Detecting ${settings.familyName} with ${settings.bitsCorrected} corrected bit(s); tag size ${settings.tagSizeMeters} m; minimum decision margin ${settings.minimumDecisionMargin}.`);
}

function queueDetectorSettingsApply() {
  detectorSettingsApplyChain = detectorSettingsApplyChain
    .catch((previousConfigurationError) => {
      console.log(previousConfigurationError);
    })
    .then(async () => {
      detectorSettingsApplyInProgress = true;
      try {
        await applyDetectorSettingsToApriltagDetector();
      } finally {
        detectorSettingsApplyInProgress = false;
      }
    });

  return detectorSettingsApplyChain;
}

function registerDetectorSettingsChangeListener() {
  const familySelect = document.getElementById(DETECTOR_FAMILY_SELECT_ID);
  const detectorSettingElementIds = [
    DETECTOR_BITS_CORRECTED_SELECT_ID,
    DETECTOR_TAG_SIZE_INPUT_ID,
    DETECTOR_MIN_DECISION_MARGIN_INPUT_ID,
  ];

  if (familySelect !== null) {
    familySelect.addEventListener('change', function() {
      applyRecommendedControlsForSelectedFamily();
      queueDetectorSettingsApply().catch((configurationError) => {
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
      queueDetectorSettingsApply().catch((configurationError) => {
        console.log(configurationError);
        updateDetectorStatus(configurationError.message);
      });
    });
  });
}

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
  applyRecommendedControlsForSelectedFamily();
  await queueDetectorSettingsApply();
  registerDetectorSettingsChangeListener();
  window.requestAnimationFrame(process_frame);
}

function filterDetectionsByDecisionMargin(rawDetections) {
  const settings = readDetectorSettingsFromPage();
  return rawDetections.filter((detection) => (
    typeof detection.decision_margin !== 'number'
    || detection.decision_margin >= settings.minimumDecisionMargin
  ));
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
  let imageDataPixels = imageData.data;
  let grayscalePixels = new Uint8Array(ctx.canvas.width * ctx.canvas.height);

  for (var i = 0, j = 0; i < imageDataPixels.length; i += 4, j++) {
    let grayscale = Math.round((imageDataPixels[i] + imageDataPixels[i + 1] + imageDataPixels[i + 2]) / 3);
    grayscalePixels[j] = grayscale;
    imageDataPixels[i] = grayscale;
    imageDataPixels[i + 1] = grayscale;
    imageDataPixels[i + 2] = grayscale;
  }
  ctx.putImageData(imageData, 0, 0);

  if (detectorSettingsApplyInProgress) {
    window.requestAnimationFrame(process_frame);
    return;
  }

  detections.forEach(det => {
    ctx.beginPath();
      ctx.lineWidth = "5";
      ctx.strokeStyle = "blue";
      ctx.moveTo(det.corners[0].x, det.corners[0].y);
      ctx.lineTo(det.corners[1].x, det.corners[1].y);
      ctx.lineTo(det.corners[2].x, det.corners[2].y);
      ctx.lineTo(det.corners[3].x, det.corners[3].y);
      ctx.lineTo(det.corners[0].x, det.corners[0].y);
      ctx.font = "bold 20px Arial";
      var txt = ""+det.id;
      ctx.fillStyle = "blue";
      ctx.textAlign = "center";
      ctx.fillText(txt, det.center.x, det.center.y+5);
    ctx.stroke();
  });

  try {
    const rawDetections = await apriltag.detect(grayscalePixels, ctx.canvas.width, ctx.canvas.height);
    detections = filterDetectionsByDecisionMargin(rawDetections);
  } catch (detectionError) {
    console.log(detectionError);
    window.requestAnimationFrame(process_frame);
    return;
  }

  if (imgSaveRequested && detections.length > 0) {
      let savep = Base64.bytesToBase64(ctx.getImageData(0, 0, ctx.canvas.width, ctx.canvas.height).data);
      var det = JSON.stringify({
        det_data: detections[0],
        img_data: LZString.compressToUTF16(savep),
        img_width:  ctx.canvas.width,
        img_height: ctx.canvas.height
      });

      localStorage.setItem("detectData", det);
      buttonToggle();
      loadImg('saved_det');
  }

  window.requestAnimationFrame(process_frame);
}

async function loadImg(targetHtmlElemId) {
  var detectData = localStorage.getItem('detectData');
  if (detectData) {
     let detectDataObj = JSON.parse(detectData);
     let savedPixels = Base64.base64ToBytes(LZString.decompressFromUTF16(detectDataObj.img_data));
     delete detectDataObj.img_data;

     const canvasSaved = document.getElementById(targetHtmlElemId+"_canvas");
     let ctx = canvasSaved.getContext("2d");
     canvasSaved.width = detectDataObj.img_width;
     canvasSaved.height = detectDataObj.img_height;
     let imageData = ctx.getImageData(0, 0, ctx.canvas.width, ctx.canvas.height);
     imageData.data.set(savedPixels);
     ctx.putImageData(imageData, 0, 0);

     let detDataSaved = document.getElementById(targetHtmlElemId+"_data");
     detDataSaved.value=JSON.stringify(detectDataObj, null, 2);
  } else console.log("detectData not found");
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
