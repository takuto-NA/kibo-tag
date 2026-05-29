importScripts('apriltag_wasm.js');
importScripts("https://unpkg.com/comlink/dist/umd/comlink.js");

const BROWSER_DEMO_MAX_DETECTIONS = 32;
const BROWSER_DEMO_RETURN_POSE = 1;
const BROWSER_DEMO_RETURN_SOLUTIONS = 0;

/**
 * Wrapper around apriltag_wasm: loads the WASM module and exposes detector calls.
 * Default family is tag36h11. Use set_tag_family for ArUco dictionaries.
 */
class Apriltag {

  /**
   * @param {function} onDetectorReadyCallback Called when the detector is ready
   */
    constructor(onDetectorReadyCallback) {
        this.onDetectorReadyCallback = onDetectorReadyCallback;

        this._opt = {
          quad_decimate: 2.0,
          quad_sigma: 0.0,
          nthreads: 1,
          refine_edges: 1,
          max_detections: BROWSER_DEMO_MAX_DETECTIONS,
          return_pose: BROWSER_DEMO_RETURN_POSE,
          return_solutions: BROWSER_DEMO_RETURN_SOLUTIONS
        };

        let _this = this;
        AprilTagWasm().then(function (Module) {
            console.log("Apriltag WASM module loaded.");
            _this.onWasmInit(Module);
        }).catch(function (loadError) {
            console.error("Apriltag WASM failed to load.", loadError);
        });
    }

    onWasmInit(Module) {
        this._Module = Module;
        this._init = Module.cwrap('atagjs_init', 'number', []);
        this._destroy = Module.cwrap('atagjs_destroy', 'number', []);
        this._set_tag_family = Module.cwrap('atagjs_set_tag_family', 'number', ['string', 'number']);
        this._set_detector_options = Module.cwrap(
            'atagjs_set_detector_options',
            'number',
            ['number', 'number', 'number', 'number', 'number', 'number', 'number']);
        this._set_pose_info = Module.cwrap('atagjs_set_pose_info', 'number', ['number', 'number', 'number', 'number']);
        this._set_img_buffer = Module.cwrap('atagjs_set_img_buffer', 'number', ['number', 'number', 'number']);
        this._atagjs_set_tag_size = Module.cwrap('atagjs_set_tag_size', 'number', ['number', 'number']);
        this._detect = Module.cwrap('atagjs_detect', 'number', []);

        const init_result = this._init();
        if (init_result !== 0) {
            throw new Error('Apriltag detector init failed.');
        }

        this._applyDetectorOptions();
        this.onDetectorReadyCallback();
      }

    _applyDetectorOptions() {
        const set_options_result = this._set_detector_options(
          this._opt.quad_decimate,
          this._opt.quad_sigma,
          this._opt.nthreads,
          this._opt.refine_edges,
          this._opt.max_detections,
          this._opt.return_pose,
          this._opt.return_solutions);
        if (set_options_result !== 0) {
            throw new Error('Apriltag set_detector_options failed.');
        }
    }

    _detectionJsonStringFromPointer(str_json_pointer) {
        const json_string_length = this._Module.getValue(str_json_pointer, 'i32');
        const json_string_pointer = this._Module.getValue(str_json_pointer + 4, 'i32');
        if (json_string_length <= 0 || json_string_pointer === 0) {
            throw new Error('Apriltag detect returned an empty response.');
        }
        return this._Module.UTF8ToString(json_string_pointer, json_string_length);
    }

    /**
     * @param {Array} grayscaleImg grayscale image buffer
     * @param {Number} imgWidth image width in pixels
     * @param {Number} imgHeight image height in pixels
     * @return {Array} detection objects
     */
    detect(grayscaleImg, imgWidth, imgHeight) {
        const image_buffer_pointer = this._set_img_buffer(imgWidth, imgHeight, imgWidth);
        if (!image_buffer_pointer) {
            throw new Error('Invalid image dimensions for detector buffer.');
        }
        if (imgWidth * imgHeight !== grayscaleImg.length) {
            throw new Error('Grayscale image length must equal width times height.');
        }
        this._Module.HEAPU8.set(grayscaleImg, image_buffer_pointer);

        const str_json_pointer = this._detect();
        const detections_json_string = this._detectionJsonStringFromPointer(str_json_pointer);
        const detections = JSON.parse(detections_json_string);

        if (!Array.isArray(detections)) {
            if (detections && detections.result) {
                throw new Error(detections.result);
            }
            throw new Error('Apriltag detect returned an unexpected response.');
        }

        return detections;
    }

    set_tag_family(familyName, bitsCorrected = 1) {
        const set_family_result = this._set_tag_family(familyName, bitsCorrected);
        if (set_family_result !== 0) {
            throw new Error('Unsupported tag family or invalid bitsCorrected: ' + familyName);
        }
    }

    set_camera_info(fx, fy, cx, cy) {
        this._set_pose_info(fx, fy, cx, cy);
    }

    set_tag_size(tagid, size) {
        const set_tag_size_result = this._atagjs_set_tag_size(tagid, size);
        if (set_tag_size_result !== 0) {
            throw new Error('Invalid tag id for set_tag_size.');
        }
    }

    set_max_detections(maxDetections) {
        this._opt.max_detections = maxDetections;
        this._applyDetectorOptions();
    }

    set_return_pose(returnPose) {
        this._opt.return_pose = returnPose;
        this._applyDetectorOptions();
    }

    set_return_solutions(returnSolutions) {
        this._opt.return_solutions = returnSolutions;
        this._applyDetectorOptions();
    }

}

Comlink.expose(Apriltag);
