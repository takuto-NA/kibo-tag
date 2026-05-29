/** @file test_str_json.c
 *  @brief Unit tests for the public str_json buffer API (cmocka).
 */

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "str_json.h"

static const size_t TEST_BUFFER_SMALL_BYTES = 10;
static const size_t TEST_BUFFER_MEDIUM_BYTES = 30;
static const size_t TEST_BUFFER_LARGE_BYTES = 100;
static const size_t TEST_BUFFER_DEFAULT_BYTES = 1000;
static const size_t TEST_BUFFER_SECOND_CREATE_BYTES = 500;
static const size_t TEST_BUFFER_AFTER_DESTROY_BYTES = 20;
static const size_t TEST_BUFFER_FINAL_CREATE_BYTES = 30;

void when_called_str_json_create_returns_a_valid_string(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    int create_result = str_json_create(&str_json, TEST_BUFFER_DEFAULT_BYTES);

    assert_int_equal(create_result, 0);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_DEFAULT_BYTES);
    assert_int_equal(str_json.len, 0);

    str_json_destroy(&str_json);
}

void when_called_on_zero_len_str_json_create_returns_error(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    int create_result = str_json_create(&str_json, 0);

    assert_int_equal(create_result, -1);

    str_json_destroy(&str_json);
}

void when_called_repeatedly_str_json_create_returns_error(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    int create_result = str_json_create(&str_json, TEST_BUFFER_DEFAULT_BYTES);

    assert_int_equal(create_result, 0);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_DEFAULT_BYTES);
    assert_int_equal(str_json.len, 0);
    assert_non_null(str_json.str);
    char *buffer_pointer = str_json.str;

    create_result = str_json_create(&str_json, TEST_BUFFER_SECOND_CREATE_BYTES);
    assert_int_equal(create_result, -1);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_DEFAULT_BYTES);
    assert_int_equal(str_json.len, 0);
    assert_ptr_equal(buffer_pointer, str_json.str);

    str_json_destroy(&str_json);
}

void when_called_str_json_destroy_destroys(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    str_json_create(&str_json, TEST_BUFFER_DEFAULT_BYTES);
    int destroy_result = str_json_destroy(&str_json);
    assert_int_equal(destroy_result, 0);
    assert_int_equal(str_json.alloc_size, 0);
    assert_int_equal(str_json.len, 0);
    assert_null(str_json.str);

    destroy_result = str_json_destroy(&str_json);
    assert_int_equal(destroy_result, -1);
}

void when_called_str_clear_returns_an_empty_string(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    str_json_create(&str_json, TEST_BUFFER_DEFAULT_BYTES);

    str_json_concat(&str_json, "A test string.");

    str_json_clear(&str_json);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_DEFAULT_BYTES);
    assert_int_equal(str_json.len, 0);
    assert_non_null(str_json.str);
    assert_string_equal(str_json.str, "");

    str_json_destroy(&str_json);
}

void when_given_empty_inputs_str_json_concat_returns_a_valid_result(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    str_json_create(&str_json, TEST_BUFFER_DEFAULT_BYTES);

    size_t concat_result = str_json_concat(&str_json, "");
    assert_int_equal(concat_result, 0);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_DEFAULT_BYTES);
    assert_int_equal(str_json.len, 0);
    assert_string_equal(str_json.str, "");

    concat_result = str_json_concat(&str_json, "Hello");
    assert_int_equal(concat_result, 5);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_DEFAULT_BYTES);
    assert_int_equal(str_json.len, 5);
    assert_string_equal(str_json.str, "Hello");

    str_json_destroy(&str_json);
}

void when_given_too_large_inputs_str_json_concat_truncates_at_alloc_size(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    t_str_json str_json_second = STR_JSON_INITIALIZER;
    str_json_create(&str_json, TEST_BUFFER_SMALL_BYTES);

    size_t concat_result = str_json_concat(&str_json, "A large input!");
    assert_int_equal(concat_result, TEST_BUFFER_SMALL_BYTES);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_SMALL_BYTES);
    assert_int_equal(str_json.len, TEST_BUFFER_SMALL_BYTES);
    assert_string_equal(str_json.str, "A large in");

    concat_result = str_json_concat(&str_json, "Lets try again!");
    assert_int_equal(concat_result, TEST_BUFFER_SMALL_BYTES);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_SMALL_BYTES);
    assert_int_equal(str_json.len, TEST_BUFFER_SMALL_BYTES);
    assert_string_equal(str_json.str, "A large in");

    str_json_create(&str_json_second, TEST_BUFFER_SMALL_BYTES);

    concat_result = str_json_concat(&str_json_second, "Exactly 10");
    assert_int_equal(concat_result, TEST_BUFFER_SMALL_BYTES);
    assert_int_equal(str_json_second.alloc_size, TEST_BUFFER_SMALL_BYTES);
    assert_int_equal(str_json_second.len, TEST_BUFFER_SMALL_BYTES);
    assert_string_equal(str_json_second.str, "Exactly 10");

    str_json_destroy(&str_json);
    str_json_destroy(&str_json_second);
}

void when_concat_given_null_source_returns_zero_length(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    str_json_create(&str_json, TEST_BUFFER_SMALL_BYTES);

    size_t concat_result = str_json_concat(&str_json, NULL);
    assert_int_equal(concat_result, 0);
    assert_int_equal(str_json.len, 0);
    assert_string_equal(str_json.str, "");

    str_json_destroy(&str_json);
}

void when_clear_and_concat_called_in_sequence_return_a_valid_result(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    str_json_create(&str_json, TEST_BUFFER_SMALL_BYTES);

    str_json_concat(&str_json, "One");
    str_json_concat(&str_json, "Two");
    str_json_concat(&str_json, "Three");
    assert_string_equal(str_json.str, "OneTwoThre");

    str_json_clear(&str_json);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_SMALL_BYTES);
    assert_int_equal(str_json.len, 0);
    assert_string_equal(str_json.str, "");

    str_json_concat(&str_json, "One");
    str_json_concat(&str_json, "Two");
    str_json_concat(&str_json, "Three");
    assert_string_equal(str_json.str, "OneTwoThre");
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_SMALL_BYTES);
    assert_int_equal(str_json.len, TEST_BUFFER_SMALL_BYTES);

    str_json_clear(&str_json);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_SMALL_BYTES);
    assert_int_equal(str_json.len, 0);
    assert_string_equal(str_json.str, "");

    str_json_destroy(&str_json);
}

void when_destroy_and_concat_called_in_sequence_return_a_valid_result(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    str_json_create(&str_json, TEST_BUFFER_SMALL_BYTES);

    str_json_concat(&str_json, "One");
    str_json_concat(&str_json, "Two");
    str_json_concat(&str_json, "Three");
    assert_string_equal(str_json.str, "OneTwoThre");

    str_json_destroy(&str_json);
    str_json_create(&str_json, TEST_BUFFER_AFTER_DESTROY_BYTES);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_AFTER_DESTROY_BYTES);
    assert_int_equal(str_json.len, 0);
    assert_string_equal(str_json.str, "");

    str_json_concat(&str_json, "One");
    str_json_concat(&str_json, "Two");
    str_json_concat(&str_json, "Three");
    assert_string_equal(str_json.str, "OneTwoThree");
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_AFTER_DESTROY_BYTES);
    assert_int_equal(str_json.len, 11);

    str_json_destroy(&str_json);
    str_json_create(&str_json, TEST_BUFFER_FINAL_CREATE_BYTES);
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_FINAL_CREATE_BYTES);
    assert_int_equal(str_json.len, 0);
    assert_string_equal(str_json.str, "");

    str_json_destroy(&str_json);
}

void when_str_json_printf_called_returns_a_well_formatted_result(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    str_json_create(&str_json, TEST_BUFFER_LARGE_BYTES);

    str_json_printf(&str_json, "A value: %d", 5);
    assert_string_equal(str_json.str, "A value: 5");
    assert_int_equal(str_json.alloc_size, TEST_BUFFER_LARGE_BYTES);
    assert_int_equal(str_json.len, 10);

    str_json_destroy(&str_json);
}

void when_given_too_large_inputs_str_json_printf_truncates_at_alloc_size(void)
{
    t_str_json str_json = STR_JSON_INITIALIZER;
    str_json_create(&str_json, TEST_BUFFER_MEDIUM_BYTES);

    str_json_printf(&str_json, "A value: %s", "this is a long string");
    assert_string_equal(str_json.str, "A value: this is a long strin");

    str_json_destroy(&str_json);
}
