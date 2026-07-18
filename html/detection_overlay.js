/**
 * Responsibility: draw detection overlays and persist a saved detection to localStorage.
 */

import * as Base64 from "./base64.js";

export function drawDetectionOverlays(canvasContext, detections) {
  detections.forEach((detection) => {
    canvasContext.beginPath();
    canvasContext.lineWidth = "5";
    canvasContext.strokeStyle = "blue";
    canvasContext.moveTo(detection.corners[0].x, detection.corners[0].y);
    canvasContext.lineTo(detection.corners[1].x, detection.corners[1].y);
    canvasContext.lineTo(detection.corners[2].x, detection.corners[2].y);
    canvasContext.lineTo(detection.corners[3].x, detection.corners[3].y);
    canvasContext.lineTo(detection.corners[0].x, detection.corners[0].y);
    canvasContext.font = "bold 20px Arial";
    canvasContext.fillStyle = "blue";
    canvasContext.textAlign = "center";
    canvasContext.fillText("" + detection.id, detection.center.x, detection.center.y + 5);
    canvasContext.stroke();
  });
}

export function saveNextDetectionToLocalStorage(canvasContext, detections, onSaved) {
  if (detections.length <= 0) {
    return false;
  }

  const imageBytesBase64 = Base64.bytesToBase64(
    canvasContext.getImageData(0, 0, canvasContext.canvas.width, canvasContext.canvas.height).data);
  const detectionPayload = JSON.stringify({
    det_data: detections[0],
    img_data: LZString.compressToUTF16(imageBytesBase64),
    img_width: canvasContext.canvas.width,
    img_height: canvasContext.canvas.height
  });

  localStorage.setItem("detectData", detectionPayload);
  if (typeof onSaved === 'function') {
    onSaved();
  }
  return true;
}

export async function loadSavedDetectionIntoPage(targetHtmlElemId) {
  const detectData = localStorage.getItem('detectData');
  if (!detectData) {
    console.log("detectData not found");
    return;
  }

  const detectDataObj = JSON.parse(detectData);
  const savedPixels = Base64.base64ToBytes(LZString.decompressFromUTF16(detectDataObj.img_data));
  delete detectDataObj.img_data;

  const canvasSaved = document.getElementById(targetHtmlElemId + "_canvas");
  const canvasContext = canvasSaved.getContext("2d");
  canvasSaved.width = detectDataObj.img_width;
  canvasSaved.height = detectDataObj.img_height;
  const imageData = canvasContext.getImageData(0, 0, canvasContext.canvas.width, canvasContext.canvas.height);
  imageData.data.set(savedPixels);
  canvasContext.putImageData(imageData, 0, 0);

  const detDataSaved = document.getElementById(targetHtmlElemId + "_data");
  detDataSaved.value = JSON.stringify(detectDataObj, null, 2);
}
