#ifndef TEST_ATAGJS_DETECTOR_H
#define TEST_ATAGJS_DETECTOR_H

int atagjs_test_group_teardown(void **state);

void when_detect_called_before_init_returns_error_json(void);
void when_detect_called_on_empty_image_returns_empty_json_array(void);
void when_set_tag_size_given_invalid_tag_id_returns_error(void);

#endif
