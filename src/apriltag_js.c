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

typedef apriltag_family_t *(*tag_family_create_fn)(void);
typedef void (*tag_family_destroy_fn)(apriltag_family_t *);

typedef struct {
    const char *canonical_name;
    const char *alias_name;
    int max_tag_id;
    tag_family_create_fn create;
    tag_family_destroy_fn destroy;
    double *size_meters_by_id;
} tag_family_descriptor_t;

static double g_tag_size_tag36h11[MAX_TAG_ID];
static double g_tag_size_aruco_4x4_100[ARUCO_4X4_100_TAG_COUNT];

static tag_family_descriptor_t g_tag_family_descriptors[] = {
    {
        .canonical_name = "tag36h11",
        .alias_name = NULL,
        .max_tag_id = MAX_TAG_ID,
        .create = tag36h11_create,
        .destroy = tag36h11_destroy,
        .size_meters_by_id = g_tag_size_tag36h11,
    },
    {
        .canonical_name = "tagAruco4x4_100",
        .alias_name = "DICT_4X4_100",
        .max_tag_id = ARUCO_4X4_100_TAG_COUNT,
        .create = tagAruco4x4_100_create,
        .destroy = tagAruco4x4_100_destroy,
        .size_meters_by_id = g_tag_size_aruco_4x4_100,
    },
};

static const int TAG_FAMILY_DESCRIPTOR_COUNT =
    (int)(sizeof(g_tag_family_descriptors) / sizeof(g_tag_family_descriptors[0]));
static const int DEFAULT_TAG_FAMILY_DESCRIPTOR_INDEX = 0;

static apriltag_family_t *g_tag_family = NULL;
static apriltag_detector_t *g_tag_detector = NULL;
static const tag_family_descriptor_t *g_active_tag_family_descriptor = NULL;
static int g_active_family_bits_corrected = 1;

static int g_image_width = 0;
static int g_image_height = 0;
static int g_image_stride = 0;
static uint8_t *g_image_buffer = NULL;

static t_str_json g_detection_json = STR_JSON_INITIALIZER;

static const double DEFAULT_TAG_SIZE_METERS = 0.15;
static const int MIN_POSE_ALTERNATIVE_SOLUTION_JSON_BYTES = 100;
static const size_t ERROR_JSON_ALLOC_BYTES = 256;
static const int POSE_ORTHOGONAL_ITERATION_COUNT = 50;
static const size_t MAT3_JSON_BUFFER_BYTES = 256;
static const size_t VEC3_JSON_BUFFER_BYTES = 128;
static const size_t CORNERS_CENTER_JSON_BUFFER_BYTES = 256;

static int g_max_detections = 0;
static int g_return_pose = 1;
static int g_return_solutions = 0;

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
static const tag_family_descriptor_t *tag_family_descriptor_for_name(const char *family_name);
static int tag_family_bits_corrected_is_valid(int bits_corrected);
static void reset_all_tag_size_tables_to_default(void);
static int tag_id_is_valid_for_active_family(int tag_id);
static int apriltag_pose_has_valid_matrices(const apriltag_pose_t *pose);
static void apriltag_pose_clear(apriltag_pose_t *pose);
static void apriltag_pose_destroy_matrices(apriltag_pose_t *pose);
static void format_mat3x3_column_major_json(char *output_buffer, size_t output_buffer_bytes, const matd_t *matrix);
static void format_vec3_json(char *output_buffer, size_t output_buffer_bytes, const matd_t *vector);
static void format_corners_and_center_json(
    char *corners_json,
    size_t corners_json_bytes,
    char *center_json,
    size_t center_json_bytes,
    const apriltag_detection_t *detection);
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
    "\"corners\": %s, \"center\": %s }";
const char fmt_det_point_pose[] =
    "{\"id\":%d, \"family\":\"%s\", \"hamming\":%d, \"decision_margin\":%.2f, "
    "\"corners\": %s, \"center\": %s, \"pose\": { \"size\":%.2f, \"R\": %s, "
    "\"t\": %s, \"e\": %f %s } }";

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

static const tag_family_descriptor_t *tag_family_descriptor_for_name(const char *family_name)
{
    if (family_name == NULL) {
        return NULL;
    }

    for (int descriptor_index = 0; descriptor_index < TAG_FAMILY_DESCRIPTOR_COUNT; descriptor_index++) {
        const tag_family_descriptor_t *descriptor = &g_tag_family_descriptors[descriptor_index];
        if (strcmp(family_name, descriptor->canonical_name) == 0) {
            return descriptor;
        }
        if (descriptor->alias_name != NULL && strcmp(family_name, descriptor->alias_name) == 0) {
            return descriptor;
        }
    }

    return NULL;
}

static void reset_tag_size_table(double *size_meters_by_id, int max_tag_id)
{
    for (int tag_index = 0; tag_index < max_tag_id; tag_index++) {
        size_meters_by_id[tag_index] = DEFAULT_TAG_SIZE_METERS;
    }
}

static void reset_all_tag_size_tables_to_default(void)
{
    for (int descriptor_index = 0; descriptor_index < TAG_FAMILY_DESCRIPTOR_COUNT; descriptor_index++) {
        const tag_family_descriptor_t *descriptor = &g_tag_family_descriptors[descriptor_index];
        reset_tag_size_table(descriptor->size_meters_by_id, descriptor->max_tag_id);
    }
}

static int tag_id_is_valid_for_active_family(int tag_id)
{
    if (g_active_tag_family_descriptor == NULL) {
        return 0;
    }
    if (tag_id < 0) {
        return 0;
    }
    return tag_id < g_active_tag_family_descriptor->max_tag_id;
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

static void format_mat3x3_column_major_json(char *output_buffer, size_t output_buffer_bytes, const matd_t *matrix)
{
    snprintf(
        output_buffer,
        output_buffer_bytes,
        "[[%f,%f,%f],[%f,%f,%f],[%f,%f,%f]]",
        matd_get(matrix, 0, 0),
        matd_get(matrix, 1, 0),
        matd_get(matrix, 2, 0),
        matd_get(matrix, 0, 1),
        matd_get(matrix, 1, 1),
        matd_get(matrix, 2, 1),
        matd_get(matrix, 0, 2),
        matd_get(matrix, 1, 2),
        matd_get(matrix, 2, 2));
}

static void format_vec3_json(char *output_buffer, size_t output_buffer_bytes, const matd_t *vector)
{
    snprintf(
        output_buffer,
        output_buffer_bytes,
        "[%f,%f,%f]",
        matd_get(vector, 0, 0),
        matd_get(vector, 1, 0),
        matd_get(vector, 2, 0));
}

static void format_corners_and_center_json(
    char *corners_json,
    size_t corners_json_bytes,
    char *center_json,
    size_t center_json_bytes,
    const apriltag_detection_t *detection)
{
    snprintf(
        corners_json,
        corners_json_bytes,
        "[{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f},{\"x\":%.2f,\"y\":%.2f}]",
        detection->p[0][0],
        detection->p[0][1],
        detection->p[1][0],
        detection->p[1][1],
        detection->p[2][0],
        detection->p[2][1],
        detection->p[3][0],
        detection->p[3][1]);
    snprintf(
        center_json,
        center_json_bytes,
        "{\"x\":%.2f,\"y\":%.2f}",
        detection->c[0],
        detection->c[1]);
}

static void append_alternative_solution_json(
    char *alternative_solution_json,
    int alternative_solution_json_size,
    const apriltag_pose_t *alternative_pose,
    double alternative_error,
    int unique_solution)
{
    char rotation_json[MAT3_JSON_BUFFER_BYTES];
    char translation_json[VEC3_JSON_BUFFER_BYTES];

    if (alternative_solution_json == NULL) {
        return;
    }
    if (alternative_solution_json_size <= MIN_POSE_ALTERNATIVE_SOLUTION_JSON_BYTES) {
        return;
    }
    if (!apriltag_pose_has_valid_matrices(alternative_pose)) {
        return;
    }

    format_mat3x3_column_major_json(rotation_json, sizeof(rotation_json), alternative_pose->R);
    format_vec3_json(translation_json, sizeof(translation_json), alternative_pose->t);
    snprintf(
        alternative_solution_json,
        (size_t)alternative_solution_json_size,
        ", \"asol\": {\"R\": %s, \"t\": %s, \"e\": %f, \"uniquesol\": %s }",
        rotation_json,
        translation_json,
        alternative_error,
        unique_solution ? "true" : "false");
}

static void format_detection_point_json(
    char *detection_fragment,
    const apriltag_detection_t *detection,
    const char *family_name)
{
    char corners_json[CORNERS_CENTER_JSON_BUFFER_BYTES];
    char center_json[VEC3_JSON_BUFFER_BYTES];

    format_corners_and_center_json(
        corners_json,
        sizeof(corners_json),
        center_json,
        sizeof(center_json),
        detection);
    snprintf(
        detection_fragment,
        STR_DET_LEN,
        fmt_det_point,
        detection->id,
        family_name,
        detection->hamming,
        detection->decision_margin,
        corners_json,
        center_json);
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
    char corners_json[CORNERS_CENTER_JSON_BUFFER_BYTES];
    char center_json[VEC3_JSON_BUFFER_BYTES];
    char rotation_json[MAT3_JSON_BUFFER_BYTES];
    char translation_json[VEC3_JSON_BUFFER_BYTES];
    const char *alternative_solution_suffix =
        alternative_solution_json != NULL ? alternative_solution_json : "";

    format_corners_and_center_json(
        corners_json,
        sizeof(corners_json),
        center_json,
        sizeof(center_json),
        detection);
    format_mat3x3_column_major_json(rotation_json, sizeof(rotation_json), pose->R);
    format_vec3_json(translation_json, sizeof(translation_json), pose->t);

    snprintf(
        detection_fragment,
        STR_DET_LEN,
        fmt_det_point_pose,
        detection->id,
        family_name,
        detection->hamming,
        detection->decision_margin,
        corners_json,
        center_json,
        tag_size_meters,
        rotation_json,
        translation_json,
        pose_error,
        alternative_solution_suffix);
}

EMSCRIPTEN_KEEPALIVE
int atagjs_init()
{
    const tag_family_descriptor_t *default_descriptor =
        &g_tag_family_descriptors[DEFAULT_TAG_FAMILY_DESCRIPTOR_INDEX];

    if (g_tag_detector != NULL || g_tag_family != NULL) {
        return -1;
    }

    g_tag_family = default_descriptor->create();
    if (g_tag_family == NULL) {
        printf("Error initializing tag family.");
        return -1;
    }

    g_tag_detector = apriltag_detector_create();
    if (g_tag_detector == NULL) {
        printf("Error initializing detector.");
        default_descriptor->destroy(g_tag_family);
        g_tag_family = NULL;
        return -1;
    }

    g_active_tag_family_descriptor = default_descriptor;
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

    if (g_tag_family != NULL && g_active_tag_family_descriptor != NULL) {
        g_active_tag_family_descriptor->destroy(g_tag_family);
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
    g_active_tag_family_descriptor = NULL;
    g_active_family_bits_corrected = 1;

    str_json_destroy(&g_detection_json);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int atagjs_set_tag_family(const char *family_name, int bits_corrected)
{
    const tag_family_descriptor_t *new_descriptor = NULL;
    const tag_family_descriptor_t *previous_descriptor = NULL;
    apriltag_family_t *new_tag_family = NULL;
    apriltag_family_t *previous_tag_family = NULL;

    if (g_tag_detector == NULL || g_tag_family == NULL || g_active_tag_family_descriptor == NULL) {
        return -1;
    }

    if (!tag_family_bits_corrected_is_valid(bits_corrected)) {
        return -1;
    }

    new_descriptor = tag_family_descriptor_for_name(family_name);
    if (new_descriptor == NULL) {
        return -1;
    }

    new_tag_family = new_descriptor->create();
    if (new_tag_family == NULL) {
        return -1;
    }

    apriltag_detector_clear_families(g_tag_detector);
    apriltag_detector_add_family_bits(g_tag_detector, new_tag_family, bits_corrected);

    previous_tag_family = g_tag_family;
    previous_descriptor = g_active_tag_family_descriptor;
    g_tag_family = new_tag_family;
    g_active_tag_family_descriptor = new_descriptor;
    g_active_family_bits_corrected = bits_corrected;

    previous_descriptor->destroy(previous_tag_family);

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

    g_active_tag_family_descriptor->size_meters_by_id[tag_id] = size_meters;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int atagjs_set_default_tag_size(double size_meters)
{
    if (g_active_tag_family_descriptor == NULL) {
        return -1;
    }

    for (int tag_index = 0; tag_index < g_active_tag_family_descriptor->max_tag_id; tag_index++) {
        g_active_tag_family_descriptor->size_meters_by_id[tag_index] = size_meters;
    }
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

static void select_best_pose_and_alternative(
    apriltag_pose_t *pose_first,
    double error_first,
    int first_pose_valid,
    apriltag_pose_t *pose_second,
    double error_second,
    int second_pose_valid,
    apriltag_pose_t *out_primary_pose,
    double *out_primary_error,
    apriltag_pose_t **out_alternative_pose,
    double *out_alternative_error,
    int *out_unique_solution)
{
    apriltag_pose_clear(out_primary_pose);
    *out_alternative_pose = NULL;
    *out_alternative_error = 0.0;
    *out_unique_solution = 0;

    if (first_pose_valid && (!second_pose_valid || error_first <= error_second)) {
        out_primary_pose->R = pose_first->R;
        out_primary_pose->t = pose_first->t;
        *out_primary_error = error_first;
        if (second_pose_valid) {
            *out_alternative_pose = pose_second;
            *out_alternative_error = error_second;
            *out_unique_solution = 1;
        } else {
            *out_alternative_pose = pose_first;
            *out_alternative_error = error_first;
            *out_unique_solution = 0;
        }
        return;
    }

    if (second_pose_valid) {
        out_primary_pose->R = pose_second->R;
        out_primary_pose->t = pose_second->t;
        *out_primary_error = error_second;
        if (first_pose_valid) {
            *out_alternative_pose = pose_first;
            *out_alternative_error = error_first;
            *out_unique_solution = 1;
        }
        return;
    }

    if (first_pose_valid) {
        out_primary_pose->R = pose_first->R;
        out_primary_pose->t = pose_first->t;
        *out_primary_error = error_first;
    }
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
    apriltag_pose_t *alternative_pose = NULL;
    double alternative_error = 0.0;
    int unique_solution = 0;
    int first_pose_valid = 0;
    int second_pose_valid = 0;
    double primary_error = 0.0;

    apriltag_pose_clear(pose);

    estimate_tag_pose_orthogonal_iteration(
        detection_info,
        &error_first,
        &pose_first,
        &error_second,
        &pose_second,
        POSE_ORTHOGONAL_ITERATION_COUNT);

    first_pose_valid = apriltag_pose_has_valid_matrices(&pose_first);
    second_pose_valid = apriltag_pose_has_valid_matrices(&pose_second);

    select_best_pose_and_alternative(
        &pose_first,
        error_first,
        first_pose_valid,
        &pose_second,
        error_second,
        second_pose_valid,
        pose,
        &primary_error,
        &alternative_pose,
        &alternative_error,
        &unique_solution);

    if (alternative_pose != NULL) {
        append_alternative_solution_json(
            alternative_solution_json,
            alternative_solution_json_size,
            alternative_pose,
            alternative_error,
            unique_solution);
    }

    // Destroy the pose matrices that were not transferred to the primary output.
    if (pose->R != pose_first.R || pose->t != pose_first.t) {
        apriltag_pose_destroy_matrices(&pose_first);
    } else {
        apriltag_pose_clear(&pose_first);
    }
    if (pose->R != pose_second.R || pose->t != pose_second.t) {
        apriltag_pose_destroy_matrices(&pose_second);
    } else {
        apriltag_pose_clear(&pose_second);
    }

    return primary_error;
}

static double tag_size_meters_from_id(int tag_id)
{
    if (!tag_id_is_valid_for_active_family(tag_id)) {
        return DEFAULT_TAG_SIZE_METERS;
    }

    return g_active_tag_family_descriptor->size_meters_by_id[tag_id];
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
