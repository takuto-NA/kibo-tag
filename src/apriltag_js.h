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

// maximum size of string for each detection
#define STR_DET_LEN 1500

// max id: 36h11 tag ids are up to 586
#define MAX_TAG_ID 600

/**
 * @brief Init the apriltag detector with given family and default options
 * default options: quad_decimate=2.0; quad_sigma=0.0; nthreads=1; refine_edges=1; return_pose=1
 * @sa set_detector_options for meaning of options
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
 * @brief Sets the given detector options
 *
 * @param decimate Decimate input image by this factor
 * @param sigma Apply low-pass blur to input; negative sharpens
 * @param nthreads Use this many CPU threads
 * @param refine_edges Spend more time trying to align edges of tags
 * @param max_detections Maximum number of detections to return (0=no max)
 * @param return_pose Detect returns pose of detected tags (0=does not return pose; returns pose otherwise)
 * @param return_solutions Detect returns details about both solutions of the pose estimation, if available
 *
 * @return 0=success; -1 if detector is not initialized (call atagjs_init first)
 */
int atagjs_set_detector_options(float decimate, float sigma, int nthreads, int refine_edges, int max_detections, int return_pose, int return_solutions);

/**
 * @brief Sets camera intrinsics (in pixels) for tag pose estimation
 *
 * May be called before atagjs_init().
 *
 * @param fx x focal lenght in pixels
 * @param fy y focal lenght in pixels
 * @param cx x principal point in pixels
 * @param cy y principal point in pixels
 *
 * @return 0=success
 */
int atagjs_set_pose_info(double fx, double fy, double cx, double cy);

/**
 * @brief Creates/changes size of the image buffer where we receive the images to process
 *
 * @param width Width of the image
 * @param height Height of the image
 * @param stride How many pixels per row (=width typically)
 *
 * @return pointer to the image buffer; NULL if width, height, or stride are invalid or allocation fails
 *
 * @warning caller of detect is responsible for putting *grayscale* image pixels in this buffer
 * @warning invalid dimensions do not change the existing buffer
 */
uint8_t *atagjs_set_img_buffer(int width, int height, int stride);

/**
 * @brief Set the size of a known tag; This size will be used for pose computation later
 *
 * @param tagid the ID of the tag (0 <= tagid < MAX_TAG_ID)
 * @param size the size of the tag in meters
 *
 * @return 0=success; -1 if tagid is out of range
 *
 */
int atagjs_set_tag_size(int tagid, double size);

/**
 * @brief Detect tags in image stored in the buffer (g_img_buf)
 *
 * @return pointer to str_json structure. The data in this memory location must be consumed before the next call to detect()
 *
 * @warning caller is responsible for putting *grayscale* image pixels in the input buffer (g_img_buf)
 * @warning caller *should not* release return pointer (it's reused at every detect() call); data returned must be consumed before the next call to detect()
 */
t_str_json *atagjs_detect();

#endif
