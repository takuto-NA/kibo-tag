/** @file apriltag_js.h
*  @brief Definitions for the apriltag detector
*
*  Use apriltag library to implement a detector that runs in the browser
*  using WASM
*
* Copyright (C) Wiselab CMU.
* @date July, 2020
*/

#ifndef _APRILTAG_JS_
#define _APRILTAG_JS_

#include "apriltag.h"
#include "apriltag_pose.h"
#include "str_json.h"

#define STR_DET_LEN 1500

#define MAX_TAG_ID 600

#define ARUCO_4X4_100_TAG_COUNT 100

#define TAG_FAMILY_BITS_CORRECTED_MIN 0
#define TAG_FAMILY_BITS_CORRECTED_MAX 2

/**
 * @brief Init the apriltag detector with tag36h11 and default options
 *
 * @return 0=success; -1 if already initialized or on failure
 */
int atagjs_init();

/**
 * @brief Releases resources (safe to call when not initialized)
 *
 * @return 0=success
 */
int atagjs_destroy();

/**
 * @brief Switch the active tag family (one family at a time)
 *
 * @param family_name tag36h11, tagAruco4x4_100, or DICT_4X4_100
 * @param bits_corrected hamming bits to correct (0..2)
 *
 * @return 0=success; -1 if detector not initialized, unknown family, or invalid bits_corrected
 */
int atagjs_set_tag_family(const char *family_name, int bits_corrected);

/**
 * @brief Sets the given detector options
 *
 * @return 0=success; -1 if detector is not initialized
 */
int atagjs_set_detector_options(float decimate, float sigma, int nthreads, int refine_edges, int max_detections, int return_pose, int return_solutions);

/**
 * @brief Sets camera intrinsics (in pixels) for tag pose estimation
 *
 * May be called before atagjs_init().
 */
int atagjs_set_pose_info(double fx, double fy, double cx, double cy);

/**
 * @brief Creates/changes size of the image buffer
 *
 * @return pointer to the image buffer; NULL on invalid dimensions or allocation failure
 */
uint8_t *atagjs_set_img_buffer(int width, int height, int stride);

/**
 * @brief Set tag size for the active family (meters)
 *
 * @return 0=success; -1 if tagid is out of range for the active family
 */
int atagjs_set_tag_size(int tagid, double size);

/**
 * @brief Detect tags in image stored in the buffer
 */
t_str_json *atagjs_detect();

#endif
