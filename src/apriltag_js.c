/** @file apriltag_js.c
 *  @brief Apriltag detection compiled with emscripten for browser WASM use.
 */

#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "apriltag.h"
#include "apriltag_pose.h"
#include "tag36h11.h"
#include "aruco/tagAruco4x4_100.h"
#include "common/image_u8.h"
#include "common/zarray.h"
#ifdef __EMSCRIPTEN__
#include "emscripten.h"
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include "apriltag_js.h"
#include "str_json.h"

typedef enum {
    TAG_FAMILY_KIND_TAG36H11,
    TAG_FAMILY_KIND_ARUCO_4X4_100,
} tag_family_kind_t;

static apriltag_family_t *g_tag_family = NULL;
static apriltag_detector_t *g_tag_detector = NULL;
static tag_family_kind_t g_active_tag_family_kind = TAG_FAMILY_KIND_TAG36H11;
static int g_active_family_bits_corrected = 1;

static int g_image_width = 0;
static int g_image_height = 0;
static int g_image_stride = 0;
static uint8_t *g_image_buffer = NULL;

static t_str_json g_detection_json = STR_JSON_INITIALIZER;

static const double DEFAULT_TAG_SIZE_METERS = 0.15;
static const int MIN_POSE_ALTERNATIVE_SOLUTION_JSON_BYTES = 100;
static const size_t ERROR_JSON_ALLOC_BYTES = 256;

static int g_max_detections = 0;
static int g_return_pose = 1;
static int g_return_solutions = 0;

static double g_tag_size_tag36h11[MAX_TAG_ID];
static double g_tag_size_aruco_4x4_100[ARUCO_4X4_100_TAG_COUNT];

static apriltag_detection_info_t g_detection_pose_info = {
    .cx = 636.9118,
    .cy = 360.5100,
    .fx = 997.2827,
    .fy = 997.2827,
};

static double estimate_tag_pose_with_solution(
    apriltag_detection_info_t *detection_info,
    apriltag_pose_t *pose,
    char *alternative_solution_json,
    int alternative_solution_json_size);
static double tag_size_meters_from_id(int tag_id);
static int image_buffer_required_byte_count(int width, int height, int stride, size_t *out_byte_count);
static t_str_json *make_error_json(const char *message);
static apriltag_family_t *tag_family_create_by_name(const char *family_name, tag_family_kind_t *out_kind);
static void tag_family_destroy(apriltag_family_t *tag_family, tag_family_kind_t family_kind);
static int tag_family_bits_corrected_is_valid(int bits_corrected);
static void reset_all_tag_size_tables_to_default(void);
static int tag_id_is_valid_for_active_family(int tag_id);
static int apriltag_pose_has_valid_matrices(const apriltag_pose_t *pose);
static void apriltag_pose_clear(apriltag_pose_t *pose);
static void apriltag_pose_destroy_matrices(apriltag_pose_t *pose);
static void append_alternative_solution_json(
    char *alternative_solution_json,
    int alternative_solution_json_size,
    const apriltag_pose_t *alternative_pose,
    double alternative_error,
    int unique_solution);
static void format_detection_point_json(
    char *detection_fragment,
    const apriltag_detection_t *detection,
    const char *family_name);
static void format_detection_point_pose_json(
    char *detection_fragment,
    const apriltag_detection_t *detection,
    const char *family_name,
    double tag_size_meters,
    const apriltag_pose_t *pose,
    double pose_error,
    const char *alternative_solution_json);

const char fmt_error[] = "{ \"result\": \"%s\" }";
const char fmt_det_point[] =
    "{\"id\":%d, \"family\":\"%s\", \"hamming\":%d, \"decision_margin\":%.2f, "
    "\"corners\": [{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f}], "
    "\"center\": {\"x\":%.2f,\"y\":%.2f} }";
const char fmt_det_point_pose[] =
    "{\"id\":%d, \"family\":\"%s\", \"hamming\":%d, \"decision_margin\":%.2f, "
    "\"corners\": [{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f}], "
    "\"center\": {\"x\":%.2f,\"y\":%.2f}, \"pose\": { \"size\":%.2f, \"R\": [[%f,%f,%f],[%f,%f,%f],[%f,%f,%f]], "
    "\"t\": [%f,%f,%f], \"e\": %f %s } }";

static int tag_family_bits_corrected_is_valid(int bits_corrected)
{
    if (bits_corrected < TAG_FAMILY_BITS_CORRECTED_MIN) {
        return 0;
    }
    if (bits_corrected > TAG_FAMILY_BITS_CORRECTED_MAX) {
        return 0;
    }
    return 1;
}

static void reset_all_tag_size_tables_to_default(void)
{
    for (int tag_index = 0; tag_index < MAX_TAG_ID; tag_index++) {
        g_tag_size_tag36h11[tag_index] = DEFAULT_TAG_SIZE_METERS;
    }
    for (int tag_index = 0; tag_index < ARUCO_4X4_100_TAG_COUNT; tag_index++) {
        g_tag_size_aruco_4x4_100[tag_index] = DEFAULT_TAG_SIZE_METERS;
    }
}

static apriltag_family_t *tag_family_create_by_name(const char *family_name, tag_family_kind_t *out_kind)
{
    if (family_name == NULL || out_kind == NULL) {
        return NULL;
    }

    if (strcmp(family_name, "tag36h11") == 0) {
        *out_kind = TAG_FAMILY_KIND_TAG36H11;
        return tag36h11_create();
    }

    if (strcmp(family_name, "tagAruco4x4_100") == 0 || strcmp(family_name, "DICT_4X4_100") == 0) {
        *out_kind = TAG_FAMILY_KIND_ARUCO_4X4_100;
        return tagAruco4x4_100_create();
    }

    return NULL;
}

static void tag_family_destroy(apriltag_family_t *tag_family, tag_family_kind_t family_kind)
{
    if (tag_family == NULL) {
        return;
    }

    switch (family_kind) {
        case TAG_FAMILY_KIND_TAG36H11:
            tag36h11_destroy(tag_family);
            break;
        case TAG_FAMILY_KIND_ARUCO_4X4_100:
            tagAruco4x4_100_destroy(tag_family);
            break;
        default:
            break;
    }
}

static int tag_id_is_valid_for_active_family(int tag_id)
{
    if (tag_id < 0) {
        return 0;
    }

    if (g_active_tag_family_kind == TAG_FAMILY_KIND_ARUCO_4X4_100) {
        return tag_id < ARUCO_4X4_100_TAG_COUNT;
    }

    return tag_id < MAX_TAG_ID;
}

static int apriltag_pose_has_valid_matrices(const apriltag_pose_t *pose)
{
    if (pose == NULL) {
        return 0;
    }
    if (pose->R == NULL || pose->t == NULL) {
        return 0;
    }
    return 1;
}

static void apriltag_pose_clear(apriltag_pose_t *pose)
{
    if (pose == NULL) {
        return;
    }
    pose->R = NULL;
    pose->t = NULL;
}

static void apriltag_pose_destroy_matrices(apriltag_pose_t *pose)
{
    if (pose == NULL) {
        return;
    }
    if (pose->R != NULL) {
        matd_destroy(pose->R);
        pose->R = NULL;
    }
    if (pose->t != NULL) {
        matd_destroy(pose->t);
        pose->t = NULL;
    }
}

static void append_alternative_solution_json(
    char *alternative_solution_json,
    int alternative_solution_json_size,
    const apriltag_pose_t *alternative_pose,
    double alternative_error,
    int unique_solution)
{
    if (alternative_solution_json == NULL) {
        return;
    }
    if (alternative_solution_json_size <= MIN_POSE_ALTERNATIVE_SOLUTION_JSON_BYTES) {
        return;
    }
    if (!apriltag_pose_has_valid_matrices(alternative_pose)) {
        return;
    }

    snprintf(
        alternative_solution_json,
        alternative_solution_json_size,
        ", \"asol\": {\"R\": [[%f,%f,%f],[%f,%f,%f],[%f,%f,%f]], \"t\": [%f,%f,%f], \"e\": %f, \"uniquesol\": %s }",
        matd_get(alternative_pose->R, 0, 0),
        matd_get(alternative_pose->R, 1, 0),
        matd_get(alternative_pose->R, 2, 0),
        matd_get(alternative_pose->R, 0, 1),
        matd_get(alternative_pose->R, 1, 1),
        matd_get(alternative_pose->R, 2, 1),
        matd_get(alternative_pose->R, 0, 2),
        matd_get(alternative_pose->R, 1, 2),
        matd_get(alternative_pose->R, 2, 2),
        matd_get(alternative_pose->t, 0, 0),
        matd_get(alternative_pose->t, 1, 0),
        matd_get(alternative_pose->t, 2, 0),
        alternative_error,
        unique_solution ? "true" : "false");
}

static void format_detection_point_json(
    char *detection_fragment,
    const apriltag_detection_t *detection,
    const char *family_name)
{
    snprintf(
        detection_fragment,
        STR_DET_LEN,
        fmt_det_point,
        detection->id,
        family_name,
        detection->hamming,
        detection->decision_margin,
        detection->p[0][0],
        detection->p[0][1],
        detection->p[1][0],
        detection->p[1][1],
        detection->p[2][0],
        detection->p[2][1],
        detection->p[3][0],
        detection->p[3][1],
        detection->c[0],
        detection->c[1]);
}

static void format_detection_point_pose_json(
    char *detection_fragment,
    const apriltag_detection_t *detection,
    const char *family_name,
    double tag_size_meters,
    const apriltag_pose_t *pose,
    double pose_error,
    const char *alternative_solution_json)
{
    const char *alternative_solution_suffix =
        alternative_solution_json != NULL ? alternative_solution_json : "";

    snprintf(
        detection_fragment,
        STR_DET_LEN,
        fmt_det_point_pose,
        detection->id,
        family_name,
        detection->hamming,
        detection->decision_margin,
        detection->p[0][0],
        detection->p[0][1],
        detection->p[1][0],
        detection->p[1][1],
        detection->p[2][0],
        detection->p[2][1],
        detection->p[3][0],
        detection->p[3][1],
        detection->c[0],
        detection->c[1],
        tag_size_meters,
        matd_get(pose->R, 0, 0),
        matd_get(pose->R, 1, 0),
        matd_get(pose->R, 2, 0),
        matd_get(pose->R, 0, 1),
        matd_get(pose->R, 1, 1),
        matd_get(pose->R, 2, 1),
        matd_get(pose->R, 0, 2),
        matd_get(pose->R, 1, 2),
        matd_get(pose->R, 2, 2),
        matd_get(pose->t, 0, 0),
        matd_get(pose->t, 1, 0),
        matd_get(pose->t, 2, 0),
        pose_error,
        alternative_solution_suffix);
}

EMSCRIPTEN_KEEPALIVE
int atagjs_init()
{
    if (g_tag_detector != NULL || g_tag_family != NULL) {
        return -1;
    }

    g_tag_family = tag36h11_create();
    if (g_tag_family == NULL) {
        printf("Error initializing tag family.");
        return -1;
    }

    g_tag_detector = apriltag_detector_create();
    if (g_tag_detector == NULL) {
        printf("Error initializing detector.");
        tag36h11_destroy(g_tag_family);
        g_tag_family = NULL;
        return -1;
    }

    g_active_tag_family_kind = TAG_FAMILY_KIND_TAG36H11;
    g_active_family_bits_corrected = 1;
    apriltag_detector_add_family_bits(g_tag_detector, g_tag_family, g_active_family_bits_corrected);

    g_tag_detector->quad_decimate = 2.0;
    g_tag_detector->quad_sigma = 0.0;
    g_tag_detector->nthreads = 1;
    g_tag_detector->debug = false;
    g_tag_detector->refine_edges = true;
    g_return_pose = 1;
    g_return_solutions = 0;
    g_max_detections = 0;
    reset_all_tag_size_tables_to_default();

    return 0;
}

EMSCRIPTEN_KEEPALIVE
int atagjs_destroy()
{
    if (g_tag_detector != NULL) {
        apriltag_detector_destroy(g_tag_detector);
        g_tag_detector = NULL;
    }

    if (g_tag_family != NULL) {
        tag_family_destroy(g_tag_family, g_active_tag_family_kind);
        g_tag_family = NULL;
    }

    if (g_image_buffer != NULL) {
        free(g_image_buffer);
        g_image_buffer = NULL;
    }

    g_image_width = 0;
    g_image_height = 0;
    g_image_stride = 0;
    g_max_detections = 0;
    g_return_pose = 0;
    g_return_solutions = 0;
    g_active_tag_family_kind = TAG_FAMILY_KIND_TAG36H11;
    g_active_family_bits_corrected = 1;

    str_json_destroy(&g_detection_json);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int atagjs_set_tag_family(const char *family_name, int bits_corrected)
{
    tag_family_kind_t new_family_kind;
    apriltag_family_t *new_tag_family = NULL;
    apriltag_family_t *previous_tag_family = NULL;
    tag_family_kind_t previous_family_kind = g_active_tag_family_kind;

    if (g_tag_detector == NULL || g_tag_family == NULL) {
        return -1;
    }

    if (!tag_family_bits_corrected_is_valid(bits_corrected)) {
        return -1;
    }

    new_tag_family = tag_family_create_by_name(family_name, &new_family_kind);
    if (new_tag_family == NULL) {
        return -1;
    }

    apriltag_detector_clear_families(g_tag_detector);
    apriltag_detector_add_family_bits(g_tag_detector, new_tag_family, bits_corrected);

    previous_tag_family = g_tag_family;
    g_tag_family = new_tag_family;
    g_active_tag_family_kind = new_family_kind;
    g_active_family_bits_corrected = bits_corrected;

    tag_family_destroy(previous_tag_family, previous_family_kind);

    return 0;
}

EMSCRIPTEN_KEEPALIVE
int atagjs_set_detector_options(
    float decimate,
    float sigma,
    int nthreads,
    int refine_edges,
    int max_detections,
    int return_pose,
    int return_solutions)
{
    if (g_tag_detector == NULL) {
        return -1;
    }

    g_tag_detector->quad_decimate = decimate;
    g_tag_detector->quad_sigma = sigma;
    g_tag_detector->nthreads = nthreads;
    g_tag_detector->refine_edges = refine_edges != 0;
    g_max_detections = max_detections;
    g_return_pose = return_pose;
    g_return_solutions = return_solutions;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int atagjs_set_pose_info(double fx, double fy, double cx, double cy)
{
    g_detection_pose_info.fx = fx;
    g_detection_pose_info.fy = fy;
    g_detection_pose_info.cx = cx;
    g_detection_pose_info.cy = cy;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
uint8_t *atagjs_set_img_buffer(int width, int height, int stride)
{
    size_t buffer_byte_count = 0;

    if (image_buffer_required_byte_count(width, height, stride, &buffer_byte_count) != 0) {
        return NULL;
    }

    if (g_image_buffer != NULL
        && g_image_width == width
        && g_image_height == height
        && g_image_stride == stride) {
        return g_image_buffer;
    }

    uint8_t *new_image_buffer = (uint8_t *)calloc(buffer_byte_count, sizeof(uint8_t));
    if (new_image_buffer == NULL) {
        return NULL;
    }

    if (g_image_buffer != NULL) {
        free(g_image_buffer);
        g_image_buffer = NULL;
    }

    g_image_width = width;
    g_image_height = height;
    g_image_stride = stride;
    g_image_buffer = new_image_buffer;
    return g_image_buffer;
}

EMSCRIPTEN_KEEPALIVE
int atagjs_set_tag_size(int tag_id, double size_meters)
{
    if (!tag_id_is_valid_for_active_family(tag_id)) {
        return -1;
    }

    if (g_active_tag_family_kind == TAG_FAMILY_KIND_ARUCO_4X4_100) {
        g_tag_size_aruco_4x4_100[tag_id] = size_meters;
        return 0;
    }

    g_tag_size_tag36h11[tag_id] = size_meters;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
t_str_json *atagjs_detect()
{
    char detection_fragment[STR_DET_LEN + 1];

    str_json_destroy(&g_detection_json);

    if (g_tag_family == NULL || g_tag_detector == NULL || g_image_buffer == NULL) {
        return make_error_json("Detector not initialized. (did you call init and set_img_buffer?)");
    }

    image_u8_t image = {
        .width = g_image_width,
        .height = g_image_height,
        .stride = g_image_stride,
        .buf = g_image_buffer,
    };

    zarray_t *detections = apriltag_detector_detect(g_tag_detector, &image);
    int detection_count = zarray_size(detections);

    if (detection_count <= 0) {
        apriltag_detections_destroy(detections);
        if (str_json_create(&g_detection_json, 50) != 0) {
            return make_error_json("Could not allocate memory for empty detection result");
        }
        str_json_printf(&g_detection_json, "[ ]");
        return &g_detection_json;
    }

    if (g_max_detections > 0 && g_max_detections < detection_count) {
        detection_count = g_max_detections;
    }

    if (str_json_create(&g_detection_json, (size_t)detection_count * STR_DET_LEN) != 0) {
        apriltag_detections_destroy(detections);
        return make_error_json("Could not allocate memory for detections");
    }

    str_json_concat(&g_detection_json, "[ ");

    for (int detection_index = 0; detection_index < detection_count; detection_index++) {
        apriltag_detection_t *detection = NULL;
        const char *family_name = "unknown";

        zarray_get(detections, detection_index, &detection);
        if (detection->family != NULL && detection->family->name != NULL) {
            family_name = detection->family->name;
        }

        if (g_return_pose == 0) {
            format_detection_point_json(detection_fragment, detection, family_name);
        } else {
            double tag_size_meters = tag_size_meters_from_id(detection->id);
            apriltag_pose_t pose = { .R = NULL, .t = NULL };
            double pose_error = 0.0;
            char *alternative_solution_json = NULL;
            int alternative_solution_json_size = 0;

            g_detection_pose_info.det = detection;
            g_detection_pose_info.tagsize = tag_size_meters;

            if (g_return_solutions != 0) {
                alternative_solution_json = malloc(STR_DET_LEN);
                if (alternative_solution_json != NULL) {
                    alternative_solution_json[0] = '\0';
                    alternative_solution_json_size = STR_DET_LEN;
                }
            }

            pose_error = estimate_tag_pose_with_solution(
                &g_detection_pose_info,
                &pose,
                alternative_solution_json,
                alternative_solution_json_size);

            if (apriltag_pose_has_valid_matrices(&pose)) {
                format_detection_point_pose_json(
                    detection_fragment,
                    detection,
                    family_name,
                    tag_size_meters,
                    &pose,
                    pose_error,
                    alternative_solution_json);
                apriltag_pose_destroy_matrices(&pose);
            } else {
                format_detection_point_json(detection_fragment, detection, family_name);
            }

            if (alternative_solution_json != NULL) {
                free(alternative_solution_json);
            }
        }

        if (detection_index > 0) {
            str_json_concat(&g_detection_json, ", ");
        }
        str_json_concat(&g_detection_json, detection_fragment);
    }

    str_json_concat(&g_detection_json, " ]");
    apriltag_detections_destroy(detections);
    return &g_detection_json;
}

static double estimate_tag_pose_with_solution(
    apriltag_detection_info_t *detection_info,
    apriltag_pose_t *pose,
    char *alternative_solution_json,
    int alternative_solution_json_size)
{
    double error_first = 0.0;
    double error_second = 0.0;
    apriltag_pose_t pose_first = { .R = NULL, .t = NULL };
    apriltag_pose_t pose_second = { .R = NULL, .t = NULL };
    int first_pose_valid = 0;
    int second_pose_valid = 0;

    apriltag_pose_clear(pose);

    estimate_tag_pose_orthogonal_iteration(
        detection_info,
        &error_first,
        &pose_first,
        &error_second,
        &pose_second,
        50);

    first_pose_valid = apriltag_pose_has_valid_matrices(&pose_first);
    second_pose_valid = apriltag_pose_has_valid_matrices(&pose_second);

    if (first_pose_valid && (!second_pose_valid || error_first <= error_second)) {
        pose->R = pose_first.R;
        pose->t = pose_first.t;
        if (second_pose_valid) {
            append_alternative_solution_json(
                alternative_solution_json,
                alternative_solution_json_size,
                &pose_second,
                error_second,
                1);
        } else {
            append_alternative_solution_json(
                alternative_solution_json,
                alternative_solution_json_size,
                &pose_first,
                error_first,
                0);
        }
        apriltag_pose_destroy_matrices(&pose_second);
        return error_first;
    }

    if (second_pose_valid) {
        pose->R = pose_second.R;
        pose->t = pose_second.t;
        if (first_pose_valid) {
            append_alternative_solution_json(
                alternative_solution_json,
                alternative_solution_json_size,
                &pose_first,
                error_first,
                1);
        }
        apriltag_pose_destroy_matrices(&pose_first);
        return error_second;
    }

    if (first_pose_valid) {
        pose->R = pose_first.R;
        pose->t = pose_first.t;
        apriltag_pose_destroy_matrices(&pose_second);
        return error_first;
    }

    apriltag_pose_destroy_matrices(&pose_first);
    apriltag_pose_destroy_matrices(&pose_second);
    return error_first;
}

static double tag_size_meters_from_id(int tag_id)
{
    if (!tag_id_is_valid_for_active_family(tag_id)) {
        return DEFAULT_TAG_SIZE_METERS;
    }

    if (g_active_tag_family_kind == TAG_FAMILY_KIND_ARUCO_4X4_100) {
        return g_tag_size_aruco_4x4_100[tag_id];
    }

    return g_tag_size_tag36h11[tag_id];
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
    str_json_destroy(&g_detection_json);
    if (str_json_create(&g_detection_json, ERROR_JSON_ALLOC_BYTES) != 0) {
        return &g_detection_json;
    }
    str_json_printf(&g_detection_json, fmt_error, message);
    return &g_detection_json;
}
