#ifndef TEST_ATAGJS_DETECTOR_H
#define TEST_ATAGJS_DETECTOR_H

int atagjs_test_group_teardown(void **state);

void when_detect_called_before_init_returns_error_json(void **state);
void when_detect_called_on_empty_image_returns_empty_json_array(void **state);
void when_set_tag_size_given_invalid_tag_id_returns_error(void **state);
void when_set_tag_size_given_negative_tag_id_returns_error(void **state);
void when_set_detector_options_called_before_init_returns_error(void **state);
void when_set_img_buffer_given_invalid_dimensions_returns_null(void **state);
void when_detect_called_after_init_without_image_buffer_returns_error_json(void **state);
void when_destroy_called_without_init_returns_success(void **state);
void when_destroy_called_twice_returns_success(void **state);

#endif
