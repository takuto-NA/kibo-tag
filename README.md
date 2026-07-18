# Apriltag WASM Detector Standalone Repo

Apriltag detector using the apriltag C library at [https://github.com/AprilRobotics/apriltag](https://github.com/AprilRobotics/apriltag), and compiled to WASM using emscripten.

This is the main WASM apriltag detector source, with additional tests and a browser camera demo. The live demo for this repository is on [GitHub Pages](https://takuto-na.github.io/kibo-tag/). It extends the original [ARENA standalone application](https://arenaxr.github.io/apriltag-js-standalone/) and can be integrated into the main [ARENA-core source](https://github.com/arenaxr/arena-web-core).

**Apriltags in the browser**
![Apriltag detection in the browser](html/example_screenshot.png)

## Contents

- **apriltag**: submodule of the apriltag library source repository ([https://github.com/AprilRobotics/apriltag](https://github.com/AprilRobotics/apriltag))
- **bin**: where the resulting binaries are placed
- **docs**: doxygen documentation of the detector C source; [see the docs](https://takuto-na.github.io/kibo-tag/docs/files.html) (also build locally with `make docs`).
- **html**: browser camera demo and WASM wrapper; live [here](https://takuto-na.github.io/kibo-tag/).
- **log**: where valgrind logs are placed
- **src**: the detector source
- **test**: cmocka tests
- **test/tag-imgs**: test input images

## Quick Start

### Docker (recommended on Windows)

Install [Docker Desktop](https://www.docker.com/products/docker-desktop/) only. You do not need WSL toolchains, MSYS2, or a local `make` install for tests and WASM builds.

Clone with submodules (required for apriltag sources):

```bash
git clone --recurse-submodules https://github.com/takuto-NA/kibo-tag.git
cd kibo-tag
# or after clone without submodules:
git submodule update --init --recursive
```

From the repository root (bash or PowerShell):

```bash
docker build -t kibo-tag-dev .
docker run --rm -v "${PWD}:/workspace" -w /workspace kibo-tag-dev make tests
docker run --rm -v "${PWD}:/workspace" -w /workspace kibo-tag-dev make apriltag_wasm.js
docker run --rm -v "${PWD}:/workspace" -w /workspace kibo-tag-dev make clean   # optional
```

The repository is bind-mounted; test binaries land in `bin/`, WASM artifacts in `html/`. The [Dockerfile](Dockerfile) pins a Linux toolchain (gcc, cmocka, Emscripten) and does not copy source into the image.

### Browser Camera Demo

Pushes to `master` run CI (`make tests`, WASM build) and deploy `html/` to GitHub Pages at [https://takuto-na.github.io/kibo-tag/](https://takuto-na.github.io/kibo-tag/). Rebuild WASM locally before serving if you changed detector code.

Build the WASM bundle first:

```bash
docker run --rm -v "${PWD}:/workspace" -w /workspace kibo-tag-dev make apriltag_wasm.js
```

Serve the `html/` directory over HTTP; browser camera APIs and web workers are not reliable from `file://` URLs. One simple option from the repository root is:

```bash
python -m http.server 8000 --directory html
```

Open [http://localhost:8000](http://localhost:8000), allow camera access, then choose the detector family in the page:

- Use `tag36h11` for the default AprilTag demo.
- Use `DICT_4X4_100` for OpenCV ArUco 4x4 tags with ids `0..99`.
- Selecting `DICT_4X4_100` in the page applies recommended defaults: `bitsCorrected = 0` and minimum decision margin `50`. You can tune margin higher (for example `80`–`100`) if false positives persist.
- Set the tag size in meters if you need pose estimates to match your printed tag.

Point the camera at a printed tag from the selected family. The canvas shows the live camera frame with corners and tag id overlaid. Detections below the configured minimum decision margin are hidden in the UI (they are still computed in WASM).

The demo draws the **previous frame's** detection boxes on the **current** video frame while WASM detection runs, so overlays can look slightly behind fast motion. Detection runs in a Web Worker at camera resolution (see the camera parameters textarea, default `1280x720`).

Closing the tab releases the camera (`pagehide` stops tracks and the detection loop).

### Native Linux

Install make, gcc, [emscripten](https://emscripten.org/docs/getting_started/downloads.html), [cmocka](https://cmocka.org/), [valgrind](https://www.valgrind.org/downloads/?src=www.discoversdk.com), and [doxygen](https://www.doxygen.nl/manual/install.html).  Cmocka and valgrind are only necessary to run the tests and memory checks. Doxygen is needed if you want to build the documentation.

To compile and run tests:

```bash
make <target>
```

The Makefile has the following targets:

- **all**: Builds the example binary (atagjs_example) and the WASM files (apriltag_wasm.js).
- **atagjs_example** (default): Creates a binary (at bin/atagjs_example) of an example program that get the detector output by giving it image files. The image files are indicated as arguments to the program (requires gcc).
- **apriltag_wasm.js**: Builds the WASM detector (requires emscripten). The resulting files (**apriltag_wasm.js** and **apriltag_wasm.wasm**) are placed under the [html](html) folder so they are run with the javascript example there.
- **tests**: Builds the cmocka test runner as executes it (requires cmocka).
- **valgrind**: Runs the test program under valgrind for several input images in [test/tag-imgs](test/tag-imgs) (requires valgrind).
- **clean**: Cleans non-source files.
- **help**: outputs description of targets.

## Detector Details

The detector defaults to the [tag36h11](http://ptolemy.berkeley.edu/ptolemyII/ptII11.0/ptII/doc/codeDoc/edu/umich/eecs/april/tag/Tag36h11.html) family ([pre-generated tags](https://github.com/arenaxr/apriltag-gen)). ArUco dictionaries from upstream AprilRobotics/apriltag are supported via ```set_tag_family()```.

Supported family names (initial set):

| OpenCV dictionary | AprilRobotics/apriltag family |
|-------------------|-------------------------------|
| `DICT_4X4_100` | `tagAruco4x4_100` |

Only one active family is loaded at a time. ```bitsCorrected``` must be between `0` and `2` (default `1`).

For tag pose estimation, tag sizes must be known. Use ```set_tag_size(tagid, size)``` for the **active** family. Unknown sizes default to 150 mm.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for ArUco/OpenCV license attribution.

See pre-generated tags here: https://github.com/arenaxr/apriltag-gen

## Detector API

The C detector documentation is [here](https://takuto-na.github.io/kibo-tag/docs/files.html). The detector calls are documented in [apriltag_js.h](https://takuto-na.github.io/kibo-tag/docs/apriltag__js_8h.html). A usage example can be found at [atagjs_example](src/atagjs_example.c).

When running in a browser, the C code is compiled to WASM and wrapped by the javascript class [Apriltag](html/apriltag.js) using emscripten's [cwrap()](https://emscripten.org/docs/api_reference/preamble.js.html#cwrap). The detector C calls are private to the **[Apriltag](html/apriltag.js)** class, which exposes the following calls:

- Apriltag() constructor. Accepts a callback that will be called when the detector code is fully loaded:

```javascript
let apriltag = Apriltag(() => {
  console.log("Apriltag detector ready.");
});
```

- The ```detect()``` call receives a grayscale image (```grayscaleImg```) with dimensions given by the arguments ```imgWidth``` and ```imgHeight``` in pixels:

```javascript
apriltag.detect(grayscaleImg, imgWidth, imgHeight)
```

> ```detect()``` returns an array of JSON objects on success. On failure (invalid dimensions, detector not ready, or C-side error JSON) it throws an ```Error``` with a message string.
>
> Example detection (with `return_pose = 1` and `return_solutions = 1`):
>
> ```json
> [
> {
>  "id": 151,
>  "family": "tag36h11",
>  "hamming": 0,
>  "decision_margin": 82.40,
>  "corners": [
>    { "x": 777.52, "y": 735.39},
>    { "x": 766.05, "y": 546.94},
>    { "x": 578.36, "y": 587.88},
>    { "x": 598, "y": 793.42}
>  ],
>  "center": { "x": 684.52, "y": 666.51 },
>  "pose": {
>    "size": 0.1,
>    "R": [
>      [ 0.91576, -0.385813, 0.111941 ],
>      [ -0.335306, -0.887549, -0.315954 ],
>      [ -0.221252, -0.251803, 0.942148 ] ],
>    "t": [ 0.873393, 0.188183, 0.080928 ],
>    "e": 0.000058,
>    "asol":{
>       "R":[
>          [ 0.892863, -0.092986, -0.440623 ],
>          [ 0.077304, 0.995574, -0.053454 ],
>          [ 0.443644, 0.013666, 0.896099 ] ],
>       "t":[ 0.040853, -0.032423, 1.790318 ],
>       "e":0.000078
>    }
>  }
> }
> ]
> ```
>
> Where:
>
> * *id* is the tag id,
> * *family*, *hamming*, and *decision_margin* describe the decode result
> * *corners* are x and y corners of the tag (in fractional pixel coordinates)
> * *center* is the center of the tag (in fractional pixel coordinates)
> * *pose*: pose estimation, only when ```return_pose = 1``` and the solver succeeds; otherwise corners and ```decision_margin``` are still returned without ```pose```
>   * *size* is the tag size in meters used for pose (based on the tag id / active family)
>   * *R* is the rotation matrix (**column major**)
>   * *t* is the translation
>   * *e* is the object-space error of the pose estimation
>   * *asol* is the alternative solution candidate, only when ```return_solutions = 1``` (see: [apriltag_pose.h](https://github.com/AprilRobotics/apriltag/blob/master/apriltag_pose.h))

- Use ```set_tag_size(tagid, size)``` to tell the detector about the size of a known tag. This size is used when computing the tag's pose and should be set before calling ```detect()```,  where
  * *tagid* is the id of the apriltag
  * *size* is the size of the tag in meters

```javascript
apriltag.set_tag_size(5, 0.1); // set the size of tag with id 5 to 0.1 meters
```

- Use ```set_all_tag_sizes(size)``` to overwrite every id size in the **active** family (preferred when all printed tags share one size). This is last-write-wins versus ```set_tag_size```:

```javascript
apriltag.set_all_tag_sizes(0.15); // meters; overwrites all ids in the active family
```

- Use ```set_camera_info(fx, fy, cx, cy)``` to tell the detector the camera parameters used when computing the tag's pose. The camera parameters should be set before calling ```detect()```,  where
  * *fx*, *fy* is the focal length, in pixels
  * *cx*, *cy* is the principal point offset, in pixels

```javascript
apriltag.set_camera_info(997.28, 997.28, 636.91, 360.51);
```

- Switch tag family (AprilTag or ArUco):

```javascript
// ArUco 4x4: prefer bitsCorrected 0 in live scenes (fewer false positives)
apriltag.set_tag_family("DICT_4X4_100", 0);
// alias: apriltag.set_tag_family("tagAruco4x4_100", 0);
apriltag.set_tag_family("tag36h11", 1);
```

Detection objects include ```family```, ```hamming```, and ```decision_margin``` in addition to ```id```, ```corners```, ```center```, and optional ```pose```.

- Set the detector maximum number of detections, if it should return pose estimates and details about alternative solutions with ```set_max_detections(maxDetections)```, ```set_return_pose(returnPose)``` and ```set_return_solutions(returnSolutions)```, where
  * *maxDetections* is the maximum number of detections (0=return all)
  * *returnPose* indicates if pose estimates are returned, (0=do not return; 1=return)
  * *returnSolutions* indicates if the alternative pose estimates solution is returned, (0=do not return; 1=return)

```javascript
// Override browser demo defaults (see Defaults section) when embedding the wrapper yourself:
apriltag.set_max_detections(0);   // 0 = return all detections
apriltag.set_return_pose(1);
apriltag.set_return_solutions(1); // includes alternative pose solutions; heavier
```

### Javascript example

This is an example javascript code snippet that shows how to call ```detect()```, using a video frame already in an html canvas. Before this code, we also need to assign an instance of the [Apriltag](html/apriltag.js) class to the ```apriltag``` variable used in the code and, if we are getting the pose from the detector, we would also need to call ```apriltag.set_camera_info(fx, fy, cx, cy)``` to set the correct camera parameters.

```javascript
// get the video frame
let ctx = canvas.getContext("2d"); // canvas is an html canvas with the video frame
let imageData = ctx.getImageData(0, 0, ctx.canvas.width, ctx.canvas.height);
let imageDataPixels = imageData.data;

// this is the grayscale image we will pass to the detector
let grayscalePixels = new Uint8Array(ctx.canvas.width * ctx.canvas.height);

// convert to grayscale
for (var i = 0, j = 0; i < imageDataPixels.length; i += 4, j++) {
  let grayscale = Math.round((imageDataPixels[i] + imageDataPixels[i + 1] + imageDataPixels[i + 2]) / 3);
  grayscalePixels[j] = grayscale; // single grayscale value
}

// call detect() passing the grayscale image in grayscalePixels. NOTE: **apriltag** is a previously created instance of ```Apriltag```
detections = await apriltag.detect(grayscalePixels, ctx.canvas.width, ctx.canvas.height); // Important: pass a width and height matching the grayscalePixels array size

// do something with the detections returned by detect() ...
```

See the full camera demo in the [html](html) folder ([video_process.js](html/video_process.js), [apriltag.js](html/apriltag.js)), live at [https://takuto-na.github.io/kibo-tag/](https://takuto-na.github.io/kibo-tag/).


## Detector Options

- Change detector options with ```set_max_detections(maxDetections)```, ```set_return_pose(returnPose)``` and ```set_return_solutions(returnSolutions)```. See [Detector API](#detector-api) for details.

### Defaults

The [Apriltag](html/apriltag.js) wrapper matches C ```atagjs_init()``` defaults after WASM init:

- `max_detections = 0` (return all)
- `return_pose = 1`
- `return_solutions = 0`

The **browser camera demo** (`html/detector_settings.js`) then applies a conservative profile after the detector is ready (`max_detections = 32`, pose on, alternative solutions off). Embedders that use `apriltag.js` directly do **not** get those demo limits unless they call the setters themselves.

Use ```set_max_detections```, ```set_return_pose```, and ```set_return_solutions``` to override at runtime. For example, ```set_return_solutions(1)``` includes alternative pose solutions in JSON (heavier).
