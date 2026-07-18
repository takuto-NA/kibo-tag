/**
 * Responsibility: persist and restore a saved detection frame in localStorage for the demo.
 */

import * as Base64 from "./base64.js";

export function saveNextDetectionToLocalStorage(canvasContext, detections, onSaved) {
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
