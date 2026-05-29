#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "test_str_json.h"
#include "test_atagjs_detector.h"
#include "test_apriltag_js_contract.h"

int main(void) {

    const struct CMUnitTest atagjs_detector_tests[] = {
        cmocka_unit_test_setup_teardown(
            when_detect_called_before_init_returns_error_json, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_detect_called_on_empty_image_returns_empty_json_array, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_set_tag_size_given_invalid_tag_id_returns_error, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_set_tag_size_given_negative_tag_id_returns_error, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_set_detector_options_called_before_init_returns_error, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_set_img_buffer_given_invalid_dimensions_returns_null, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_detect_called_after_init_without_image_buffer_returns_error_json, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_destroy_called_without_init_returns_success, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_destroy_called_twice_returns_success, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_init_defaults_to_tag36h11_family, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_set_tag_family_dict_4x4_100_detects_expected_aruco_id, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_set_tag_family_switches_back_to_tag36h11, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_set_tag_family_given_unknown_name_returns_error, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_set_tag_family_called_before_init_returns_error, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_set_tag_family_given_invalid_bits_corrected_returns_error, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_set_tag_family_dict_4x4_100_detects_representative_aruco_ids, NULL, atagjs_test_group_teardown),
        cmocka_unit_test_setup_teardown(
            when_tag_size_is_isolated_per_family, NULL, atagjs_test_group_teardown),
    };

    const struct CMUnitTest apriltag_js_contract_tests[] = {
        cmocka_unit_test(when_apriltag_js_exposes_set_tag_family_wrapper),
        cmocka_unit_test(when_camera_demo_exposes_aruco_family_configuration),
    };

    const struct CMUnitTest str_json_tests[] = {
        cmocka_unit_test(when_called_str_json_create_returns_a_valid_string),
        cmocka_unit_test(when_called_on_zero_len_str_json_create_returns_error),
        cmocka_unit_test(when_called_repeatedly_str_json_create_returns_error),
        cmocka_unit_test(when_called_str_json_destroy_destroys),
        cmocka_unit_test(when_called_str_clear_returns_an_empty_string),
        cmocka_unit_test(when_given_empty_inputs_str_json_concat_returns_a_valid_result),
        cmocka_unit_test(when_given_too_large_inputs_str_json_concat_truncates_at_alloc_size),
        cmocka_unit_test(when_concat_given_null_source_returns_zero_length),
        cmocka_unit_test(when_clear_and_concat_called_in_sequence_return_a_valid_result),
        cmocka_unit_test(when_destroy_and_concat_called_in_sequence_return_a_valid_result),
        cmocka_unit_test(when_str_json_printf_called_returns_a_well_formatted_result),
        cmocka_unit_test(when_given_too_large_inputs_str_json_printf_truncates_at_alloc_size)
    };

    int atagjs_result = cmocka_run_group_tests(atagjs_detector_tests, NULL, NULL);
    if (atagjs_result != 0) {
        return atagjs_result;
    }

    int contract_result = cmocka_run_group_tests(apriltag_js_contract_tests, NULL, NULL);
    if (contract_result != 0) {
        return contract_result;
    }

    return cmocka_run_group_tests(str_json_tests, NULL, NULL);
}
