/** @file test_atagjs_detector.c
 *  @brief Integration tests for the public atagjs_* detector API (cmocka).
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <cmocka.h>

#include "apriltag.h"
#include "tag36h11.h"
#include "aruco/tagAruco4x4_100.h"
#include "common/image_u8.h"
#include "apriltag_js.h"

static const int TEST_IMAGE_WIDTH_PIXELS = 64;
static const int TEST_IMAGE_HEIGHT_PIXELS = 64;
static const int TEST_TAG_RENDER_BORDER_PIXELS = 24;
static const int TEST_ARUCO_TAG_ID = 23;
static const int TEST_TAG36H11_TAG_ID = 0;
static const int TEST_ARUCO_REPRESENTATIVE_TAG_IDS[] = {0, 1, 23, 99};
static const int TEST_ARUCO_REPRESENTATIVE_TAG_ID_COUNT = 4;
static const float TEST_DETECTOR_QUAD_DECIMATE_FOR_SYNTHETIC_TAGS = 1.0f;
static const int TEST_NOISY_FRAME_WIDTH_PIXELS = 320;
static const int TEST_NOISY_FRAME_HEIGHT_PIXELS = 240;
static const int TEST_NOISY_DETECT_REPEAT_COUNT = 32;
static const float TEST_BROWSER_DEMO_QUAD_DECIMATE = 2.0f;
static const int TEST_STRESS_PROFILE_MAX_DETECTIONS = 0;
static const int TEST_STRESS_PROFILE_RETURN_SOLUTIONS = 1;

static const char EXPECTED_ERROR_NOT_INITIALIZED[] = "Detector not initialized";
static const char EXPECTED_EMPTY_DETECTIONS_JSON[] = "[ ]";
static const char EXPECTED_ARUCO_FAMILY_NAME[] = "tagAruco4x4_100";

int atagjs_test_group_teardown(void **state)
{
    (void)state;
    atagjs_destroy();
    return 0;
}

static void configure_detector_for_synthetic_tag_images(void)
{
    int set_options_result = atagjs_set_detector_options(
        TEST_DETECTOR_QUAD_DECIMATE_FOR_SYNTHETIC_TAGS,
        0.0f,
        1,
        1,
        0,
        0,
        0);
    assert_int_equal(set_options_result, 0);
}

typedef apriltag_family_t *(*test_tag_family_create_fn)(void);
typedef void (*test_tag_family_destroy_fn)(apriltag_family_t *);

static int render_tag_into_detector_buffer(apriltag_family_t *tag_family, uint32_t tag_id)
{
    image_u8_t *tag_image = apriltag_to_image(tag_family, tag_id);
    assert_non_null(tag_image);

    int canvas_width = tag_image->width + (2 * TEST_TAG_RENDER_BORDER_PIXELS);
    int canvas_height = tag_image->height + (2 * TEST_TAG_RENDER_BORDER_PIXELS);
    uint8_t *image_buffer = atagjs_set_img_buffer(canvas_width, canvas_height, canvas_width);

    assert_non_null(image_buffer);
    memset(image_buffer, 255, (size_t)canvas_width * (size_t)canvas_height);

    for (int row = 0; row < tag_image->height; row++) {
        uint8_t *destination_row = image_buffer
            + ((row + TEST_TAG_RENDER_BORDER_PIXELS) * canvas_width)
            + TEST_TAG_RENDER_BORDER_PIXELS;
        memcpy(
            destination_row,
            tag_image->buf + (row * tag_image->stride),
            (size_t)tag_image->width);
    }

    image_u8_destroy(tag_image);
    return 0;
}

static t_str_json *detect_rendered_family_tag(
    test_tag_family_create_fn create_family,
    test_tag_family_destroy_fn destroy_family,
    uint32_t tag_id)
{
    apriltag_family_t *tag_family = create_family();
    assert_non_null(tag_family);
    render_tag_into_detector_buffer(tag_family, tag_id);
    destroy_family(tag_family);
    return atagjs_detect();
}

static void assert_detection_json_contains_tag_id(t_str_json *detection_json, int expected_tag_id)
{
    char expected_id_fragment[32];

    assert_non_null(detection_json);
    assert_non_null(detection_json->str);
    snprintf(expected_id_fragment, sizeof(expected_id_fragment), "\"id\":%d", expected_tag_id);
    assert_true(strstr(detection_json->str, expected_id_fragment) != NULL);
    assert_null(strstr(detection_json->str, EXPECTED_ERROR_NOT_INITIALIZED));
}

static void assert_detection_json_does_not_contain_tag_id(t_str_json *detection_json, int unexpected_tag_id)
{
    char unexpected_id_fragment[32];

    assert_non_null(detection_json);
    assert_non_null(detection_json->str);
    snprintf(unexpected_id_fragment, sizeof(unexpected_id_fragment), "\"id\":%d", unexpected_tag_id);
    assert_null(strstr(detection_json->str, unexpected_id_fragment));
}

static void assert_detect_returns_safe_json(t_str_json *detection_json)
{
    assert_non_null(detection_json);
    assert_non_null(detection_json->str);
    assert_true(detection_json->len > 0);
    assert_true(detection_json->str[0] == '[' || detection_json->str[0] == '{');
    assert_null(strstr(detection_json->str, EXPECTED_ERROR_NOT_INITIALIZED));
}

static void configure_detector_for_pose_stress_profile(void)
{
    int set_options_result = atagjs_set_detector_options(
        TEST_BROWSER_DEMO_QUAD_DECIMATE,
        0.0f,
        1,
        1,
        TEST_STRESS_PROFILE_MAX_DETECTIONS,
        1,
        TEST_STRESS_PROFILE_RETURN_SOLUTIONS);
    assert_int_equal(set_options_result, 0);
}

static void fill_image_buffer_with_low_confidence_noise(
    uint8_t *image_buffer,
    int width,
    int height,
    int stride,
    unsigned int noise_seed)
{
    for (int row = 0; row < height; row++) {
        uint8_t *row_pointer = image_buffer + (row * stride);
        for (int column = 0; column < width; column++) {
            unsigned int pixel_index = (unsigned int)row * (unsigned int)width + (unsigned int)column;
            unsigned int mixed_value = noise_seed + (pixel_index * 1103515245U);
            uint8_t gray_value = (uint8_t)((mixed_value >> 16) & 0xFF);
            if ((row / 8 + column / 8) % 2 == 0) {
                gray_value = (uint8_t)(255 - gray_value);
            }
            row_pointer[column] = gray_value;
        }
    }
}

void when_detect_called_before_init_returns_error_json(void **state)
{
    (void)state;
    t_str_json *detection_json = atagjs_detect();

    assert_non_null(detection_json);
    assert_non_null(detection_json->str);
    assert_true(strstr(detection_json->str, EXPECTED_ERROR_NOT_INITIALIZED) != NULL);
}

void when_detect_called_on_empty_image_returns_empty_json_array(void **state)
{
    (void)state;
    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);

    uint8_t *image_buffer = atagjs_set_img_buffer(
        TEST_IMAGE_WIDTH_PIXELS,
        TEST_IMAGE_HEIGHT_PIXELS,
        TEST_IMAGE_WIDTH_PIXELS);
    assert_non_null(image_buffer);

    t_str_json *detection_json = atagjs_detect();

    assert_non_null(detection_json);
    assert_non_null(detection_json->str);
    assert_string_equal(detection_json->str, EXPECTED_EMPTY_DETECTIONS_JSON);
}

void when_set_tag_size_given_invalid_tag_id_returns_error(void **state)
{
    (void)state;
    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);

    int set_tag_size_result = atagjs_set_tag_size(MAX_TAG_ID, 0.1);
    assert_int_equal(set_tag_size_result, -1);
}

void when_set_tag_size_given_negative_tag_id_returns_error(void **state)
{
    (void)state;
    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);

    int set_tag_size_result = atagjs_set_tag_size(-1, 0.1);
    assert_int_equal(set_tag_size_result, -1);
}

void when_set_detector_options_called_before_init_returns_error(void **state)
{
    (void)state;
    int set_options_result = atagjs_set_detector_options(
        2.0f, 0.0f, 1, 1, 0, 1, 1);
    assert_int_equal(set_options_result, -1);
}

void when_set_img_buffer_given_invalid_dimensions_returns_null(void **state)
{
    (void)state;
    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);

    uint8_t *valid_image_buffer = atagjs_set_img_buffer(
        TEST_IMAGE_WIDTH_PIXELS,
        TEST_IMAGE_HEIGHT_PIXELS,
        TEST_IMAGE_WIDTH_PIXELS);
    assert_non_null(valid_image_buffer);

    uint8_t *invalid_width_buffer = atagjs_set_img_buffer(
        0,
        TEST_IMAGE_HEIGHT_PIXELS,
        TEST_IMAGE_WIDTH_PIXELS);
    assert_null(invalid_width_buffer);

    uint8_t *invalid_stride_buffer = atagjs_set_img_buffer(
        TEST_IMAGE_WIDTH_PIXELS,
        TEST_IMAGE_HEIGHT_PIXELS,
        TEST_IMAGE_WIDTH_PIXELS - 1);
    assert_null(invalid_stride_buffer);

    uint8_t *image_buffer_after_invalid_call = atagjs_set_img_buffer(
        TEST_IMAGE_WIDTH_PIXELS,
        TEST_IMAGE_HEIGHT_PIXELS,
        TEST_IMAGE_WIDTH_PIXELS);
    assert_ptr_equal(image_buffer_after_invalid_call, valid_image_buffer);
}

void when_detect_called_after_init_without_image_buffer_returns_error_json(void **state)
{
    (void)state;
    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);

    t_str_json *detection_json = atagjs_detect();

    assert_non_null(detection_json);
    assert_non_null(detection_json->str);
    assert_true(strstr(detection_json->str, EXPECTED_ERROR_NOT_INITIALIZED) != NULL);
}

void when_destroy_called_without_init_returns_success(void **state)
{
    (void)state;
    int destroy_result = atagjs_destroy();
    assert_int_equal(destroy_result, 0);
}

void when_destroy_called_twice_returns_success(void **state)
{
    (void)state;
    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);

    int first_destroy_result = atagjs_destroy();
    assert_int_equal(first_destroy_result, 0);

    int second_destroy_result = atagjs_destroy();
    assert_int_equal(second_destroy_result, 0);
}

void when_init_defaults_to_tag36h11_family(void **state)
{
    (void)state;

    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);
    configure_detector_for_synthetic_tag_images();

    t_str_json *detection_json = detect_rendered_family_tag(
        tag36h11_create,
        tag36h11_destroy,
        TEST_TAG36H11_TAG_ID);
    assert_detection_json_contains_tag_id(detection_json, TEST_TAG36H11_TAG_ID);
}

void when_set_tag_family_dict_4x4_100_detects_expected_aruco_id(void **state)
{
    (void)state;

    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);
    configure_detector_for_synthetic_tag_images();

    int set_family_result = atagjs_set_tag_family("DICT_4X4_100", 1);
    assert_int_equal(set_family_result, 0);

    t_str_json *detection_json = detect_rendered_family_tag(
        tagAruco4x4_100_create,
        tagAruco4x4_100_destroy,
        TEST_ARUCO_TAG_ID);
    assert_detection_json_contains_tag_id(detection_json, TEST_ARUCO_TAG_ID);
    assert_true(strstr(detection_json->str, EXPECTED_ARUCO_FAMILY_NAME) != NULL);
    assert_true(strstr(detection_json->str, "\"hamming\":") != NULL);
    assert_true(strstr(detection_json->str, "\"decision_margin\":") != NULL);
}

void when_set_tag_family_switches_back_to_tag36h11(void **state)
{
    (void)state;

    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);
    configure_detector_for_synthetic_tag_images();

    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 1), 0);
    (void)detect_rendered_family_tag(
        tagAruco4x4_100_create,
        tagAruco4x4_100_destroy,
        TEST_ARUCO_TAG_ID);

    assert_int_equal(atagjs_set_tag_family("tag36h11", 1), 0);
    t_str_json *detection_json_after_switch = atagjs_detect();
    assert_detection_json_does_not_contain_tag_id(detection_json_after_switch, TEST_ARUCO_TAG_ID);

    t_str_json *tag36h11_detection_json = detect_rendered_family_tag(
        tag36h11_create,
        tag36h11_destroy,
        TEST_TAG36H11_TAG_ID);
    assert_detection_json_contains_tag_id(tag36h11_detection_json, TEST_TAG36H11_TAG_ID);
}

void when_set_tag_family_given_unknown_name_returns_error(void **state)
{
    (void)state;

    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);
    configure_detector_for_synthetic_tag_images();

    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 1), 0);
    (void)detect_rendered_family_tag(
        tagAruco4x4_100_create,
        tagAruco4x4_100_destroy,
        TEST_ARUCO_TAG_ID);

    int unknown_family_result = atagjs_set_tag_family("unknown_family", 1);
    assert_int_equal(unknown_family_result, -1);

    t_str_json *detection_json = atagjs_detect();
    assert_detection_json_contains_tag_id(detection_json, TEST_ARUCO_TAG_ID);
}

void when_set_tag_family_called_before_init_returns_error(void **state)
{
    (void)state;
    int set_family_result = atagjs_set_tag_family("DICT_4X4_100", 1);
    assert_int_equal(set_family_result, -1);
}

void when_set_tag_family_given_invalid_bits_corrected_returns_error(void **state)
{
    (void)state;
    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);

    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", -1), -1);
    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 3), -1);
}

void when_set_tag_family_dict_4x4_100_detects_representative_aruco_ids(void **state)
{
    apriltag_family_t *aruco_family = NULL;
    (void)state;

    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);
    configure_detector_for_synthetic_tag_images();

    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 1), 0);
    aruco_family = tagAruco4x4_100_create();
    assert_non_null(aruco_family);

    for (int tag_index = 0; tag_index < TEST_ARUCO_REPRESENTATIVE_TAG_ID_COUNT; tag_index++) {
        int expected_tag_id = TEST_ARUCO_REPRESENTATIVE_TAG_IDS[tag_index];
        render_tag_into_detector_buffer(aruco_family, (uint32_t)expected_tag_id);
        t_str_json *detection_json = atagjs_detect();
        assert_detection_json_contains_tag_id(detection_json, expected_tag_id);
    }

    tagAruco4x4_100_destroy(aruco_family);
}

void when_tag_size_is_isolated_per_family(void **state)
{
    const double aruco_tag_size_meters = 0.42;
    const double tag36h11_tag_size_meters = 0.77;
    apriltag_family_t *aruco_family = NULL;
    apriltag_family_t *tag36h11_family = NULL;
    char expected_aruco_size_fragment[32];
    char expected_tag36h11_size_fragment[32];
    (void)state;

    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);

    assert_int_equal(
        atagjs_set_detector_options(1.0f, 0.0f, 1, 1, 0, 1, 0),
        0);

    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 1), 0);
    assert_int_equal(atagjs_set_tag_size(TEST_ARUCO_TAG_ID, aruco_tag_size_meters), 0);

    aruco_family = tagAruco4x4_100_create();
    render_tag_into_detector_buffer(aruco_family, TEST_ARUCO_TAG_ID);
    tagAruco4x4_100_destroy(aruco_family);

    snprintf(
        expected_aruco_size_fragment,
        sizeof(expected_aruco_size_fragment),
        "\"size\":%.2f",
        aruco_tag_size_meters);
    t_str_json *aruco_detection_json = atagjs_detect();
    assert_true(strstr(aruco_detection_json->str, expected_aruco_size_fragment) != NULL);

    assert_int_equal(atagjs_set_tag_family("tag36h11", 1), 0);
    assert_int_equal(atagjs_set_tag_size(TEST_TAG36H11_TAG_ID, tag36h11_tag_size_meters), 0);

    tag36h11_family = tag36h11_create();
    render_tag_into_detector_buffer(tag36h11_family, TEST_TAG36H11_TAG_ID);
    tag36h11_destroy(tag36h11_family);

    snprintf(
        expected_tag36h11_size_fragment,
        sizeof(expected_tag36h11_size_fragment),
        "\"size\":%.2f",
        tag36h11_tag_size_meters);
    t_str_json *tag36h11_detection_json = atagjs_detect();
    assert_true(strstr(tag36h11_detection_json->str, expected_tag36h11_size_fragment) != NULL);
    assert_null(strstr(tag36h11_detection_json->str, expected_aruco_size_fragment));

    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 1), 0);

    aruco_family = tagAruco4x4_100_create();
    render_tag_into_detector_buffer(aruco_family, TEST_ARUCO_TAG_ID);
    tagAruco4x4_100_destroy(aruco_family);

    t_str_json *aruco_detection_json_after_switch_back = atagjs_detect();
    assert_true(
        strstr(aruco_detection_json_after_switch_back->str, expected_aruco_size_fragment) != NULL);
}

void when_detecting_noisy_aruco_frames_with_pose_enabled_returns_safe_json(void **state)
{
    uint8_t *image_buffer = NULL;
    (void)state;

    assert_int_equal(atagjs_init(), 0);
    configure_detector_for_pose_stress_profile();
    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 0), 0);

    image_buffer = atagjs_set_img_buffer(
        TEST_NOISY_FRAME_WIDTH_PIXELS,
        TEST_NOISY_FRAME_HEIGHT_PIXELS,
        TEST_NOISY_FRAME_WIDTH_PIXELS);
    assert_non_null(image_buffer);

    for (int detect_index = 0; detect_index < TEST_NOISY_DETECT_REPEAT_COUNT; detect_index++) {
        fill_image_buffer_with_low_confidence_noise(
            image_buffer,
            TEST_NOISY_FRAME_WIDTH_PIXELS,
            TEST_NOISY_FRAME_HEIGHT_PIXELS,
            TEST_NOISY_FRAME_WIDTH_PIXELS,
            (unsigned int)detect_index);
        assert_detect_returns_safe_json(atagjs_detect());
    }
}

static void assert_detection_json_has_pose_nested_size_contract(t_str_json *detection_json)
{
    const char *pose_key = NULL;
    const char *size_after_pose = NULL;

    assert_non_null(detection_json);
    assert_non_null(detection_json->str);
    pose_key = strstr(detection_json->str, "\"pose\"");
    assert_non_null(pose_key);
    size_after_pose = strstr(pose_key, "\"size\"");
    assert_non_null(size_after_pose);
    assert_non_null(strstr(pose_key, "\"R\""));
    assert_non_null(strstr(pose_key, "\"t\""));
}

static void assert_detection_json_has_core_detection_fields(t_str_json *detection_json)
{
    assert_non_null(detection_json);
    assert_non_null(detection_json->str);
    assert_non_null(strstr(detection_json->str, "\"id\":"));
    assert_non_null(strstr(detection_json->str, "\"family\":"));
    assert_non_null(strstr(detection_json->str, "\"hamming\":"));
    assert_non_null(strstr(detection_json->str, "\"decision_margin\":"));
    assert_non_null(strstr(detection_json->str, "\"corners\":"));
    assert_non_null(strstr(detection_json->str, "\"center\":"));
}

void when_pose_detection_puts_size_under_pose_object(void **state)
{
    const double expected_tag_size_meters = 0.33;
    char expected_pose_size_fragment[64];
    (void)state;

    assert_int_equal(atagjs_init(), 0);
    assert_int_equal(
        atagjs_set_detector_options(1.0f, 0.0f, 1, 1, 0, 1, 0),
        0);
    assert_int_equal(atagjs_set_tag_size(TEST_TAG36H11_TAG_ID, expected_tag_size_meters), 0);

    t_str_json *detection_json = detect_rendered_family_tag(
        tag36h11_create,
        tag36h11_destroy,
        TEST_TAG36H11_TAG_ID);
    assert_detection_json_has_core_detection_fields(detection_json);
    assert_detection_json_has_pose_nested_size_contract(detection_json);

    snprintf(
        expected_pose_size_fragment,
        sizeof(expected_pose_size_fragment),
        "\"pose\": { \"size\":%.2f",
        expected_tag_size_meters);
    assert_non_null(strstr(detection_json->str, expected_pose_size_fragment));
}

void when_init_default_max_detections_does_not_truncate_multiple_synthetic_tags(void **state)
{
    const int first_tag_id = 0;
    const int second_tag_id = 1;
    apriltag_family_t *tag36h11_family = NULL;
    image_u8_t *first_tag_image = NULL;
    image_u8_t *second_tag_image = NULL;
    uint8_t *image_buffer = NULL;
    int canvas_width = 0;
    int canvas_height = 0;
    int tag_width = 0;
    int tag_height = 0;
    (void)state;

    assert_int_equal(atagjs_init(), 0);
    // Guard: leave max_detections at init default (0 = return all). Only tune decimate for synthetic tags.
    assert_int_equal(
        atagjs_set_detector_options(1.0f, 0.0f, 1, 1, 0, 0, 0),
        0);

    tag36h11_family = tag36h11_create();
    assert_non_null(tag36h11_family);
    first_tag_image = apriltag_to_image(tag36h11_family, first_tag_id);
    second_tag_image = apriltag_to_image(tag36h11_family, second_tag_id);
    assert_non_null(first_tag_image);
    assert_non_null(second_tag_image);
    assert_int_equal(first_tag_image->width, second_tag_image->width);
    assert_int_equal(first_tag_image->height, second_tag_image->height);

    tag_width = first_tag_image->width;
    tag_height = first_tag_image->height;
    canvas_width = (tag_width * 2) + (3 * TEST_TAG_RENDER_BORDER_PIXELS);
    canvas_height = tag_height + (2 * TEST_TAG_RENDER_BORDER_PIXELS);
    image_buffer = atagjs_set_img_buffer(canvas_width, canvas_height, canvas_width);
    assert_non_null(image_buffer);
    memset(image_buffer, 255, (size_t)canvas_width * (size_t)canvas_height);

    for (int row = 0; row < tag_height; row++) {
        uint8_t *first_destination_row = image_buffer
            + ((row + TEST_TAG_RENDER_BORDER_PIXELS) * canvas_width)
            + TEST_TAG_RENDER_BORDER_PIXELS;
        uint8_t *second_destination_row = image_buffer
            + ((row + TEST_TAG_RENDER_BORDER_PIXELS) * canvas_width)
            + (2 * TEST_TAG_RENDER_BORDER_PIXELS)
            + tag_width;
        memcpy(
            first_destination_row,
            first_tag_image->buf + (row * first_tag_image->stride),
            (size_t)tag_width);
        memcpy(
            second_destination_row,
            second_tag_image->buf + (row * second_tag_image->stride),
            (size_t)tag_width);
    }

    image_u8_destroy(first_tag_image);
    image_u8_destroy(second_tag_image);
    tag36h11_destroy(tag36h11_family);

    t_str_json *detection_json_all = atagjs_detect();
    assert_detection_json_contains_tag_id(detection_json_all, first_tag_id);
    assert_detection_json_contains_tag_id(detection_json_all, second_tag_id);

    assert_int_equal(
        atagjs_set_detector_options(1.0f, 0.0f, 1, 1, 1, 0, 0),
        0);
    t_str_json *detection_json_truncated = atagjs_detect();
    assert_detection_json_contains_tag_id(detection_json_truncated, first_tag_id);
    assert_detection_json_does_not_contain_tag_id(detection_json_truncated, second_tag_id);
}

void when_set_all_tag_sizes_updates_endpoint_ids_for_active_family_only(void **state)
{
    const double aruco_all_tag_sizes_meters = 0.55;
    const double tag36h11_all_tag_sizes_meters = 0.66;
    const int aruco_first_tag_id = 0;
    const int aruco_last_tag_id = ARUCO_4X4_100_TAG_COUNT - 1;
    char expected_aruco_size_fragment[64];
    char expected_tag36h11_size_fragment[64];
    (void)state;

    assert_int_equal(atagjs_init(), 0);
    assert_int_equal(
        atagjs_set_detector_options(1.0f, 0.0f, 1, 1, 0, 1, 0),
        0);

    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 1), 0);
    assert_int_equal(atagjs_set_all_tag_sizes(aruco_all_tag_sizes_meters), 0);

    snprintf(
        expected_aruco_size_fragment,
        sizeof(expected_aruco_size_fragment),
        "\"pose\": { \"size\":%.2f",
        aruco_all_tag_sizes_meters);

    t_str_json *aruco_first_detection_json = detect_rendered_family_tag(
        tagAruco4x4_100_create,
        tagAruco4x4_100_destroy,
        (uint32_t)aruco_first_tag_id);
    assert_non_null(strstr(aruco_first_detection_json->str, expected_aruco_size_fragment));

    t_str_json *aruco_last_detection_json = detect_rendered_family_tag(
        tagAruco4x4_100_create,
        tagAruco4x4_100_destroy,
        (uint32_t)aruco_last_tag_id);
    assert_non_null(strstr(aruco_last_detection_json->str, expected_aruco_size_fragment));

    assert_int_equal(atagjs_set_tag_family("tag36h11", 1), 0);
    assert_int_equal(atagjs_set_all_tag_sizes(tag36h11_all_tag_sizes_meters), 0);
    snprintf(
        expected_tag36h11_size_fragment,
        sizeof(expected_tag36h11_size_fragment),
        "\"pose\": { \"size\":%.2f",
        tag36h11_all_tag_sizes_meters);

    t_str_json *tag36h11_detection_json = detect_rendered_family_tag(
        tag36h11_create,
        tag36h11_destroy,
        TEST_TAG36H11_TAG_ID);
    assert_non_null(strstr(tag36h11_detection_json->str, expected_tag36h11_size_fragment));
    assert_null(strstr(tag36h11_detection_json->str, expected_aruco_size_fragment));

    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 1), 0);
    t_str_json *aruco_after_switch_json = detect_rendered_family_tag(
        tagAruco4x4_100_create,
        tagAruco4x4_100_destroy,
        (uint32_t)aruco_first_tag_id);
    assert_non_null(strstr(aruco_after_switch_json->str, expected_aruco_size_fragment));
}

void when_set_all_tag_sizes_called_before_init_returns_error(void **state)
{
    (void)state;
    assert_int_equal(atagjs_set_all_tag_sizes(0.2), -1);
}

void when_both_families_return_core_detection_fields(void **state)
{
    (void)state;

    assert_int_equal(atagjs_init(), 0);
    configure_detector_for_synthetic_tag_images();

    t_str_json *tag36h11_detection_json = detect_rendered_family_tag(
        tag36h11_create,
        tag36h11_destroy,
        TEST_TAG36H11_TAG_ID);
    assert_detection_json_has_core_detection_fields(tag36h11_detection_json);
    assert_detection_json_contains_tag_id(tag36h11_detection_json, TEST_TAG36H11_TAG_ID);

    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 1), 0);
    t_str_json *aruco_detection_json = detect_rendered_family_tag(
        tagAruco4x4_100_create,
        tagAruco4x4_100_destroy,
        TEST_ARUCO_TAG_ID);
    assert_detection_json_has_core_detection_fields(aruco_detection_json);
    assert_detection_json_contains_tag_id(aruco_detection_json, TEST_ARUCO_TAG_ID);
    assert_non_null(strstr(aruco_detection_json->str, EXPECTED_ARUCO_FAMILY_NAME));
}
