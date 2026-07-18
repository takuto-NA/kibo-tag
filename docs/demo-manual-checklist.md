# Browser camera demo manual checklist

CI cannot exercise a real camera. After WASM/demo changes, verify locally:

1. Serve `html/` over HTTP and open the demo.
2. With `tag36h11` selected, a printed tag36h11 is detected (corners + id overlay).
3. Switch to `DICT_4X4_100`: recommended bits corrected becomes `0` and minimum decision margin becomes `50`.
4. An ArUco 4x4 tag in range `0..99` is detected under that family.
5. Changing tag size (meters) updates status text and uses the one-shot `set_all_tag_sizes` path (no per-id loop).
6. Closing/hiding the tab stops the camera tracks and the detection loop (`pagehide`).
7. Embedding note: without the demo bootstrap, `apriltag.js` keeps C defaults (`max_detections = 0`).
