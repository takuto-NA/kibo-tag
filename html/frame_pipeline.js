/**
 * Responsibility: convert a video frame to grayscale and run WASM detection with margin filtering.
 */

import { filterDetectionsByDecisionMargin } from './detector_settings_logic.mjs';

export function rgbaPixelsToGrayscale(imageDataPixels, pixelCount) {
  // Detector-only luma buffer: do not mutate the source RGBA (canvas keeps color).
  const grayscalePixels = new Uint8Array(pixelCount);
  for (let rgbaIndex = 0, grayIndex = 0; rgbaIndex < imageDataPixels.length; rgbaIndex += 4, grayIndex++) {
    grayscalePixels[grayIndex] = Math.round(
      (imageDataPixels[rgbaIndex] + imageDataPixels[rgbaIndex + 1] + imageDataPixels[rgbaIndex + 2]) / 3);
  }
  return grayscalePixels;
}

export async function detectTagsInGrayscaleFrame(
  apriltagDetector,
  grayscalePixels,
  frameWidth,
  frameHeight,
  minimumDecisionMargin)
{
  const rawDetections = await apriltagDetector.detect(grayscalePixels, frameWidth, frameHeight);
  return filterDetectionsByDecisionMargin(rawDetections, minimumDecisionMargin);
}
