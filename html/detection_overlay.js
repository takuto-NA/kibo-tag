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
