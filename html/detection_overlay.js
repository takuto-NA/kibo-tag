/**
 * Responsibility: draw detection overlays (corners and tag id) on a canvas.
 */

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

export function drawFailedQuadOverlays(canvasContext, failedQuads) {
  if (!Array.isArray(failedQuads) || failedQuads.length === 0) {
    return;
  }

  canvasContext.save();
  canvasContext.setLineDash([8, 6]);
  canvasContext.lineWidth = 2;
  canvasContext.strokeStyle = 'rgba(255, 200, 0, 0.9)';
  failedQuads.forEach((failedQuad) => {
    canvasContext.beginPath();
    canvasContext.moveTo(failedQuad.corners[0].x, failedQuad.corners[0].y);
    canvasContext.lineTo(failedQuad.corners[1].x, failedQuad.corners[1].y);
    canvasContext.lineTo(failedQuad.corners[2].x, failedQuad.corners[2].y);
    canvasContext.lineTo(failedQuad.corners[3].x, failedQuad.corners[3].y);
    canvasContext.closePath();
    canvasContext.stroke();
  });
  canvasContext.setLineDash([]);
  canvasContext.font = '14px Arial';
  canvasContext.fillStyle = 'rgba(255, 200, 0, 0.95)';
  canvasContext.textAlign = 'left';
  canvasContext.fillText(`ID-unconfirmed quads: ${failedQuads.length}`, 8, 20);
  canvasContext.restore();
}
