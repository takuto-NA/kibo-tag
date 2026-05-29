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
    apriltag_family_t *tag36h11_family = NULL;
    (void)state;

    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);
    configure_detector_for_synthetic_tag_images();

    tag36h11_family = tag36h11_create();
    assert_non_null(tag36h11_family);
    render_tag_into_detector_buffer(tag36h11_family, TEST_TAG36H11_TAG_ID);
    tag36h11_destroy(tag36h11_family);

    t_str_json *detection_json = atagjs_detect();
    assert_detection_json_contains_tag_id(detection_json, TEST_TAG36H11_TAG_ID);
}

void when_set_tag_family_dict_4x4_100_detects_expected_aruco_id(void **state)
{
    apriltag_family_t *aruco_family = NULL;
    (void)state;

    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);
    configure_detector_for_synthetic_tag_images();

    int set_family_result = atagjs_set_tag_family("DICT_4X4_100", 1);
    assert_int_equal(set_family_result, 0);

    aruco_family = tagAruco4x4_100_create();
    assert_non_null(aruco_family);
    render_tag_into_detector_buffer(aruco_family, TEST_ARUCO_TAG_ID);
    tagAruco4x4_100_destroy(aruco_family);

    t_str_json *detection_json = atagjs_detect();
    assert_detection_json_contains_tag_id(detection_json, TEST_ARUCO_TAG_ID);
    assert_true(strstr(detection_json->str, EXPECTED_ARUCO_FAMILY_NAME) != NULL);
    assert_true(strstr(detection_json->str, "\"hamming\":") != NULL);
    assert_true(strstr(detection_json->str, "\"decision_margin\":") != NULL);
}

void when_set_tag_family_switches_back_to_tag36h11(void **state)
{
    apriltag_family_t *aruco_family = NULL;
    apriltag_family_t *tag36h11_family = NULL;
    (void)state;

    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);
    configure_detector_for_synthetic_tag_images();

    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 1), 0);
    aruco_family = tagAruco4x4_100_create();
    render_tag_into_detector_buffer(aruco_family, TEST_ARUCO_TAG_ID);
    tagAruco4x4_100_destroy(aruco_family);

    assert_int_equal(atagjs_set_tag_family("tag36h11", 1), 0);
    t_str_json *detection_json_after_switch = atagjs_detect();
    assert_detection_json_does_not_contain_tag_id(detection_json_after_switch, TEST_ARUCO_TAG_ID);

    tag36h11_family = tag36h11_create();
    render_tag_into_detector_buffer(tag36h11_family, TEST_TAG36H11_TAG_ID);
    tag36h11_destroy(tag36h11_family);

    t_str_json *tag36h11_detection_json = atagjs_detect();
    assert_detection_json_contains_tag_id(tag36h11_detection_json, TEST_TAG36H11_TAG_ID);
}

void when_set_tag_family_given_unknown_name_returns_error(void **state)
{
    apriltag_family_t *aruco_family = NULL;
    (void)state;

    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);
    configure_detector_for_synthetic_tag_images();

    assert_int_equal(atagjs_set_tag_family("DICT_4X4_100", 1), 0);
    aruco_family = tagAruco4x4_100_create();
    render_tag_into_detector_buffer(aruco_family, TEST_ARUCO_TAG_ID);
    tagAruco4x4_100_destroy(aruco_family);

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
