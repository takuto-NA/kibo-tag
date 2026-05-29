/** @file test_apriltag_js_contract.c
 *  @brief Contract tests for the browser Apriltag JS wrapper source.
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

static const char APRILTAG_JS_SOURCE_PATH[] = "html/apriltag.js";
static const char DEMO_INDEX_SOURCE_PATH[] = "html/index.html";
static const char DEMO_VIDEO_PROCESS_SOURCE_PATH[] = "html/video_process.js";
static const size_t APRILTAG_JS_SOURCE_MAX_BYTES = 65536;

static char *read_entire_file(const char *file_path, size_t *out_byte_count)
{
    FILE *source_file = fopen(file_path, "rb");
    char *file_buffer = NULL;
    size_t bytes_read = 0;

    if (source_file == NULL) {
        return NULL;
    }

    file_buffer = calloc(APRILTAG_JS_SOURCE_MAX_BYTES, sizeof(char));
    if (file_buffer == NULL) {
        fclose(source_file);
        return NULL;
    }

    bytes_read = fread(file_buffer, 1, APRILTAG_JS_SOURCE_MAX_BYTES - 1, source_file);
    fclose(source_file);
    file_buffer[bytes_read] = '\0';

    if (out_byte_count != NULL) {
        *out_byte_count = bytes_read;
    }

    return file_buffer;
}

void when_apriltag_js_exposes_set_tag_family_wrapper(void **state)
{
    size_t source_byte_count = 0;
    char *source_text = read_entire_file(APRILTAG_JS_SOURCE_PATH, &source_byte_count);

    (void)state;
    assert_non_null(source_text);
    assert_true(source_byte_count > 0);
    assert_non_null(strstr(source_text, "atagjs_set_tag_family"));
    assert_non_null(strstr(source_text, "set_tag_family"));
    free(source_text);
}

void when_camera_demo_exposes_aruco_family_configuration(void **state)
{
    size_t index_source_byte_count = 0;
    size_t video_process_source_byte_count = 0;
    char *index_source_text = read_entire_file(DEMO_INDEX_SOURCE_PATH, &index_source_byte_count);
    char *video_process_source_text =
        read_entire_file(DEMO_VIDEO_PROCESS_SOURCE_PATH, &video_process_source_byte_count);

    (void)state;
    assert_non_null(index_source_text);
    assert_true(index_source_byte_count > 0);
    assert_non_null(strstr(index_source_text, "DICT_4X4_100"));
    assert_non_null(strstr(index_source_text, "tag36h11"));

    assert_non_null(video_process_source_text);
    assert_true(video_process_source_byte_count > 0);
    assert_non_null(strstr(video_process_source_text, "readDetectorSettingsFromPage"));
    assert_non_null(strstr(video_process_source_text, "applyDetectorSettingsToApriltagDetector"));
    assert_non_null(strstr(video_process_source_text, "queueDetectorSettingsApply"));
    assert_non_null(strstr(video_process_source_text, "DETECTOR_FAMILY_SETTINGS"));
    assert_non_null(strstr(video_process_source_text, "detectorFamilySettingsFor"));
    assert_non_null(strstr(video_process_source_text, "ARUCO_4X4_100_BITS_CORRECTED = 0"));
    assert_non_null(strstr(video_process_source_text, "minimumDecisionMargin"));
    assert_non_null(strstr(video_process_source_text, "filterDetectionsByDecisionMargin"));
    assert_non_null(strstr(video_process_source_text, "set_tag_family"));

    free(index_source_text);
    free(video_process_source_text);
}
