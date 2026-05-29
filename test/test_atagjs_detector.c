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

static const char EXPECTED_ERROR_NOT_INITIALIZED[] = "Detector not initizalized";
static const char EXPECTED_EMPTY_DETECTIONS_JSON[] = "[ ]";

int atagjs_test_group_teardown(void **state)
{
    (void)state;
    atagjs_destroy();
    return 0;
}

void when_detect_called_before_init_returns_error_json(void)
{
    t_str_json *detection_json = atagjs_detect();

    assert_non_null(detection_json);
    assert_non_null(detection_json->str);
    assert_true(strstr(detection_json->str, EXPECTED_ERROR_NOT_INITIALIZED) != NULL);
}

void when_detect_called_on_empty_image_returns_empty_json_array(void)
{
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

void when_set_tag_size_given_invalid_tag_id_returns_error(void)
{
    int init_result = atagjs_init();
    assert_int_equal(init_result, 0);

    int set_tag_size_result = atagjs_set_tag_size(MAX_TAG_ID, 0.1);
    assert_int_equal(set_tag_size_result, -1);
}
