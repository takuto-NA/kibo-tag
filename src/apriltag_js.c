/** @file apriltag_js.c
 *  @brief Apriltag detection to be compile with emscripten
 *  @see documentation in apriltag_js.h
 *
 *  Uses the apriltag library; exposes a simple interface for a web app to
 *  use apriltags once it is compiled to WASM using emscripten
 *
 *  Copyright (C) Wiselab CMU.
 *  @date Nov, 2019
 */

#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <inttypes.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include "apriltag.h"
#include "apriltag_pose.h"
#include "tag36h11.h"
#include "tag25h9.h"
#include "tag16h5.h"
#include "tagCircle21h7.h"
#include "tagStandard41h12.h"
#include "common/getopt.h"
#include "common/image_u8.h"
#include "common/image_u8x4.h"
#include "common/pjpeg.h"
#include "common/zarray.h"
#ifdef __EMSCRIPTEN__
#include "emscripten.h"
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include "apriltag_js.h"
#include "str_json.h"

// global pointers to the tag family and detector
static apriltag_family_t *g_tf = NULL;
static apriltag_detector_t *g_td;

// size and stride f the image to process
static int g_width;
static int g_height;
static int g_stride;

// return structure for a json string we reuse in each detect() call
static t_str_json g_det_json = STR_JSON_INITIALIZER;

// pointer to the image grayscale pixels
static uint8_t *g_img_buf = NULL;

// default physical tag edge length in meters when tag id is unknown
static const double DEFAULT_TAG_SIZE_METERS = 0.15;

// minimum bytes for optional alternative pose solution JSON in detect output
static const int MIN_POSE_ALTERNATIVE_SOLUTION_JSON_BYTES = 100;

// allocation size for detector error JSON payloads
static const size_t ERROR_JSON_ALLOC_BYTES = 256;

// max number of detections returned (0=no max)
static int g_max_detections = 0;

// if we are returning pose (=0 does not output; output otherwise)
static int g_return_pose = 1;

// if we are returning details about both solutions (see estimate_tag_pose_with_solution; =0 does not output; output otherwise)
static int g_return_solutions = 0;

// store known tag sizes
static double g_tag_size[MAX_TAG_ID];

// apriltag_detection_info
static apriltag_detection_info_t g_det_pose_info = {.cx=636.9118, .cy=360.5100, .fx=997.2827, .fy=997.2827};

// declare static calls, implemented at the end of this file
static double estimate_tag_pose_with_solution(apriltag_detection_info_t *info, apriltag_pose_t *pose, char *s, int ssize);
static double tagsize_from_id(int tagid);
static int image_buffer_required_byte_count(int width, int height, int stride, size_t *out_byte_count);
static t_str_json *make_error_json(const char *message);

// json format string for errors
const char fmt_error[] = "{ \"result\": \"%s\" }";

// json format string for the detection corners
const char fmt_det_point[] = "{\"id\":%d, \"corners\": [{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f}], \"center\": {\"x\":%.2f,\"y\":%.2f} }";
// json format string for the detection with pose
const char fmt_det_point_pose[] = "{\"id\":%d, \"corners\": [{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f}], \"center\": {\"x\":%.2f,\"y\":%.2f}, \"pose\": { \"size\":%.2f, \"R\": [[%f,%f,%f],[%f,%f,%f],[%f,%f,%f]], \"t\": [%f,%f,%f], \"e\": %f %s } }";

// see documentation in .h
EMSCRIPTEN_KEEPALIVE
int atagjs_init()
{
    if (g_td != NULL || g_tf != NULL) {
        return -1;
    }
    g_tf = tag36h11_create();
    if (g_tf == NULL)
    {
        printf("Error initializing tag family.");
        return -1;
    }
    g_td = apriltag_detector_create();
    if (g_td == NULL)
    {
        printf("Error initializing detector.");
        tag36h11_destroy(g_tf);
        g_tf = NULL;
        return -1;
    }
    apriltag_detector_add_family_bits(g_td, g_tf, 1);
    g_td->quad_decimate = 2.0;
    g_td->quad_sigma = 0.0;
    g_td->nthreads = 1;
    g_td->debug = 0; // Enable debugging output (slow)
    g_td->refine_edges = 1;
    g_return_pose = 1;
    g_return_solutions = 0;
    g_max_detections = 0;

    for (int i = 0; i < MAX_TAG_ID; i++) {
        g_tag_size[i] = DEFAULT_TAG_SIZE_METERS;
    }

    return 0;
}

// see documentation in .h
EMSCRIPTEN_KEEPALIVE
int atagjs_destroy()
{
    if (g_td != NULL) {
        apriltag_detector_destroy(g_td);
        g_td = NULL;
    }
    if (g_tf != NULL) {
        tag36h11_destroy(g_tf);
        g_tf = NULL;
    }
    if (g_img_buf != NULL) {
        free(g_img_buf);
        g_img_buf = NULL;
    }

    g_width = 0;
    g_height = 0;
    g_stride = 0;
    g_max_detections = 0;
    g_return_pose = 0;
    g_return_solutions = 0;

    str_json_destroy(&g_det_json);

    return 0;
}

// see documentation in .h
EMSCRIPTEN_KEEPALIVE
int atagjs_set_detector_options(float decimate, float sigma, int nthreads, int refine_edges, int max_detections, int return_pose, int return_solutions)
{
    if (g_td == NULL) {
        return -1;
    }
    g_td->quad_decimate = decimate;
    g_td->quad_sigma = sigma;
    g_td->nthreads = nthreads;
    g_td->refine_edges = refine_edges;
    g_max_detections = max_detections;
    g_return_pose = return_pose;
    g_return_solutions = return_solutions;
    return 0;
}

// see documentation in .h
EMSCRIPTEN_KEEPALIVE
int atagjs_set_pose_info(double fx, double fy, double cx, double cy)
{
    g_det_pose_info.fx = fx;
    g_det_pose_info.fy = fy;
    g_det_pose_info.cx = cx;
    g_det_pose_info.cy = cy;
    return 0;
}

// see documentation in .h
EMSCRIPTEN_KEEPALIVE
uint8_t *atagjs_set_img_buffer(int width, int height, int stride)
{
    size_t buffer_byte_count = 0;

    if (image_buffer_required_byte_count(width, height, stride, &buffer_byte_count) != 0) {
        return NULL;
    }

    if (g_img_buf != NULL
        && g_width == width
        && g_height == height
        && g_stride == stride) {
        return g_img_buf;
    }

    uint8_t *new_image_buffer = (uint8_t *)calloc(buffer_byte_count, sizeof(uint8_t));
    if (new_image_buffer == NULL) {
        return NULL;
    }

    if (g_img_buf != NULL) {
        free(g_img_buf);
        g_img_buf = NULL;
    }

    g_width = width;
    g_height = height;
    g_stride = stride;
    g_img_buf = new_image_buffer;
    return g_img_buf;
}

// see documentation in .h
EMSCRIPTEN_KEEPALIVE
int atagjs_set_tag_size(int tagid, double size)
{
  if (tagid < 0 || tagid >= MAX_TAG_ID) return -1;
  g_tag_size[tagid] = size;
  return 0;
}

// see documentation in .h
EMSCRIPTEN_KEEPALIVE
t_str_json *atagjs_detect()
{
    char str_tmp_det[STR_DET_LEN+1];

    // clear the json string
    str_json_destroy(&g_det_json); // IMPORTANT: make sure g_det_json is initialized properly with: t_str_json g_det_json = STR_JSON_INITIALIZER;

    if (g_tf == NULL || g_td == NULL || g_img_buf == NULL)
    {
        return make_error_json("Detector not initialized. (did you call init and set_img_buffer?)");
    }

    image_u8_t im = {
        .width = g_width,
        .height = g_height,
        .stride = g_stride,
        .buf = g_img_buf};

    zarray_t *detections = apriltag_detector_detect(g_td, &im);

    int n = zarray_size(detections);

    if (n <= 0) {
      apriltag_detections_destroy(detections);
      if (str_json_create(&g_det_json, 50) != 0) {
        return make_error_json("Could not allocate memory for empty detection result");
      }
      str_json_printf(&g_det_json, "[ ]");
      return &g_det_json;
    }

    // limit detections returned according to g_max_detections
    if (g_max_detections > 0 && g_max_detections < n) n = g_max_detections;

    // start the json array
    if (str_json_create(&g_det_json, n*STR_DET_LEN) != 0) {
      apriltag_detections_destroy(detections);
      return make_error_json("Could not allocate memory for detections");
    }
    str_json_concat(&g_det_json, "[ ");

    for (int i = 0; i < n; i++)
    {
        apriltag_detection_t *det;
        zarray_get(detections, i, &det);

        if (g_return_pose == 0)
        {
            snprintf(str_tmp_det, STR_DET_LEN, fmt_det_point, det->id, det->p[0][0], det->p[0][1], det->p[1][0], det->p[1][1], det->p[2][0], det->p[2][1], det->p[3][0], det->p[3][1], det->c[0], det->c[1]);
        }
        else
        {
            // return pose ..
            double tagsize = tagsize_from_id(det->id); // size of the tag is determined from its id
            apriltag_pose_t pose;
            double pose_err;
            g_det_pose_info.det = det;
            g_det_pose_info.tagsize = tagsize;
            char *alternative_solution_json = NULL;
            int alternative_solution_json_size = 0;
            if (g_return_solutions != 0) {
                alternative_solution_json = malloc(STR_DET_LEN);
                if (alternative_solution_json != NULL) {
                    alternative_solution_json[0] = '\0';
                    alternative_solution_json_size = STR_DET_LEN;
                }
            }
            pose_err = estimate_tag_pose_with_solution(
                &g_det_pose_info,
                &pose,
                alternative_solution_json,
                alternative_solution_json_size);
            // column major R:
            snprintf(str_tmp_det, STR_DET_LEN, fmt_det_point_pose, det->id, det->p[0][0], det->p[0][1], det->p[1][0], det->p[1][1], det->p[2][0], det->p[2][1], det->p[3][0], det->p[3][1], det->c[0], det->c[1], tagsize, matd_get(pose.R, 0, 0), matd_get(pose.R, 1, 0), matd_get(pose.R, 2, 0), matd_get(pose.R, 0, 1), matd_get(pose.R, 1, 1), matd_get(pose.R, 2, 1), matd_get(pose.R, 0, 2), matd_get(pose.R, 1, 2), matd_get(pose.R, 2, 2), matd_get(pose.t, 0, 0), matd_get(pose.t, 1, 0), matd_get(pose.t, 2, 0), pose_err, alternative_solution_json != NULL ? alternative_solution_json : "");
            matd_destroy(pose.R);
            matd_destroy(pose.t);
            if (alternative_solution_json != NULL) {
                free(alternative_solution_json);
            }
        }
        if (i > 0) str_json_concat(&g_det_json, ", ");
        str_json_concat(&g_det_json, str_tmp_det);
    }

    str_json_concat(&g_det_json, " ]");

    apriltag_detections_destroy(detections);

    return &g_det_json;
}

/**
 * Our implementation of estimate tag pose to return the solution selected (1=homography method; 2=potential second local minima; see: apriltag/apriltag_pose.h)
 * Writes JSON-formatted pose solution(s) into a user supplied string
 *
 * @param info detection info
 * @param pose where to return the pose estimation
 * @param s user allocated string where to write the output json
 * @param ssize size of the given user allocated string s
 *
 * return the object-space error of the pose estimation
 */
static double estimate_tag_pose_with_solution(apriltag_detection_info_t *info, apriltag_pose_t *pose, char *s, int ssize)
{
    double err1, err2;
    apriltag_pose_t pose1, pose2;
    estimate_tag_pose_orthogonal_iteration(info, &err1, &pose1, &err2, &pose2, 50);

    if (err1 <= err2)
    {
        pose->R = pose1.R;
        pose->t = pose1.t;
        if (s != NULL && ssize > MIN_POSE_ALTERNATIVE_SOLUTION_JSON_BYTES) {
            if (pose2.R != NULL && pose2.t !=  NULL) {
                // return other alternative solution; uniquesol indicates if there are multiple solutions
                snprintf(s, ssize, ", \"asol\": {\"R\": [[%f,%f,%f],[%f,%f,%f],[%f,%f,%f]], \"t\": [%f,%f,%f], \"e\": %f, \"uniquesol\": true }",
                    matd_get(pose2.R, 0, 0), matd_get(pose2.R, 1, 0), matd_get(pose2.R, 2, 0), matd_get(pose2.R, 0, 1), matd_get(pose2.R, 1, 1), matd_get(pose2.R, 2, 1), matd_get(pose2.R, 0, 2), matd_get(pose2.R, 1, 2), matd_get(pose2.R, 2, 2), matd_get(pose2.t, 0, 0), matd_get(pose2.t, 1, 0), matd_get(pose2.t, 2, 0), err2);
            } else snprintf(s, ssize, ", \"asol\": {\"R\": [[%f,%f,%f],[%f,%f,%f],[%f,%f,%f]], \"t\": [%f,%f,%f], \"e\": %f, \"uniquesol\": false }", // return the same solution
                    matd_get(pose1.R, 0, 0), matd_get(pose1.R, 1, 0), matd_get(pose1.R, 2, 0), matd_get(pose1.R, 0, 1), matd_get(pose1.R, 1, 1), matd_get(pose1.R, 2, 1), matd_get(pose1.R, 0, 2), matd_get(pose1.R, 1, 2), matd_get(pose1.R, 2, 2), matd_get(pose1.t, 0, 0), matd_get(pose1.t, 1, 0), matd_get(pose1.t, 2, 0), err1);
        }
        if (pose2.R)
        {
            matd_destroy(pose2.t);
        }
        matd_destroy(pose2.R);
        return err1;
    }
    else
    {
        pose->R = pose2.R;
        pose->t = pose2.t;
        if (s != NULL && ssize > MIN_POSE_ALTERNATIVE_SOLUTION_JSON_BYTES) {
            // return other alternative solution; uniquesol indicates if there are multiple solutions
            snprintf(s, ssize, ", \"asol\": {\"R\": [[%f,%f,%f],[%f,%f,%f],[%f,%f,%f]], \"t\": [%f,%f,%f], \"e\": %f, \"uniquesol\": true }",
                 matd_get(pose1.R, 0, 0), matd_get(pose1.R, 1, 0), matd_get(pose1.R, 2, 0), matd_get(pose1.R, 0, 1), matd_get(pose1.R, 1, 1), matd_get(pose1.R, 2, 1), matd_get(pose1.R, 0, 2), matd_get(pose1.R, 1, 2), matd_get(pose1.R, 2, 2), matd_get(pose1.t, 0, 0), matd_get(pose1.t, 1, 0), matd_get(pose1.t, 2, 0), err1);
        }
        matd_destroy(pose1.R);
        matd_destroy(pose1.t);
        return err2;
    }
}

/**
 * @brief Determine size of the tag from its id
 *        if tag is known return that size, otherwise return 0.15 meters
 *
 * @param tagid tag id
 *
 * return the tag size, in meters
 */
static double tagsize_from_id(int tagid) {
  if (tagid < 0 || tagid >= MAX_TAG_ID) {
    return DEFAULT_TAG_SIZE_METERS;
  }
  return g_tag_size[tagid];
}

static int image_buffer_required_byte_count(int width, int height, int stride, size_t *out_byte_count)
{
    if (width <= 0 || height <= 0 || stride <= 0 || stride < width) {
        return -1;
    }
    if (height > INT_MAX / stride) {
        return -1;
    }

    size_t row_byte_count = (size_t)stride;
    size_t total_byte_count = row_byte_count * (size_t)height;
    if (out_byte_count != NULL) {
        *out_byte_count = total_byte_count;
    }
    return 0;
}

static t_str_json *make_error_json(const char *message)
{
    str_json_destroy(&g_det_json);
    if (str_json_create(&g_det_json, ERROR_JSON_ALLOC_BYTES) != 0) {
        return &g_det_json;
    }
    str_json_printf(&g_det_json, fmt_error, message);
    return &g_det_json;
}
