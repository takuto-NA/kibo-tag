/** @file test_atagjs_detector.c
 *  @brief Integration tests for the public atagjs_* detector API (cmocka).
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "apriltag_js.h"

static const int TEST_IMAGE_WIDTH_PIXELS = 64;
static const int TEST_IMAGE_HEIGHT_PIXELS = 64;

static const char EXPECTED_ERROR_NOT_INITIALIZED[] = "Detector not initialized";
static const char EXPECTED_EMPTY_DETECTIONS_JSON[] = "[ ]";

int atagjs_test_group_teardown(void **state)
{
    (void)state;
    atagjs_destroy();
    return 0;
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
