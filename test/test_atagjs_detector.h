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
void when_init_defaults_to_tag36h11_family(void **state);
void when_set_tag_family_dict_4x4_100_detects_expected_aruco_id(void **state);
void when_set_tag_family_switches_back_to_tag36h11(void **state);
void when_set_tag_family_given_unknown_name_returns_error(void **state);
void when_set_tag_family_called_before_init_returns_error(void **state);
void when_set_tag_family_given_invalid_bits_corrected_returns_error(void **state);
void when_set_tag_family_dict_4x4_100_detects_representative_aruco_ids(void **state);
void when_tag_size_is_isolated_per_family(void **state);
void when_detecting_noisy_aruco_frames_with_pose_enabled_returns_safe_json(void **state);
void when_pose_detection_puts_size_under_pose_object(void **state);
void when_init_default_max_detections_does_not_truncate_multiple_synthetic_tags(void **state);
void when_set_all_tag_sizes_updates_endpoint_ids_for_active_family_only(void **state);
void when_set_all_tag_sizes_called_before_init_returns_error(void **state);
void when_both_families_return_core_detection_fields(void **state);

#endif
