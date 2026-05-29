/*
 * LinRTOS - Test case registration via linker section.
 *
 * Each test file registers a test_case_t descriptor in .test_cases.1.
 * test_runner.c iterates the section and runs every registered test.
 */

#ifndef TEST_CASE_H
#define TEST_CASE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    bool (*fn)(void);
} test_case_t;

#define TEST_CASE_REGISTER(_name, _fn)                                 \
    static const test_case_t _test_case_##_name = {                    \
        .name = #_name,                                                \
        .fn   = _fn,                                                   \
    };                                                                 \
    static const test_case_t *const _test_case_ptr_##_name             \
        __attribute__((used, section(".test_cases.1"))) =              \
            &_test_case_##_name

extern const test_case_t *const _test_cases_start[];
extern const test_case_t *const _test_cases_end[];

#define FOR_EACH_TEST_CASE(_tc)                                        \
    for (const test_case_t *const *_pp = _test_cases_start;            \
         _pp < (const test_case_t *const *)_test_cases_end; _pp++)     \
        if (((_tc) = *_pp) != NULL)

#ifdef __cplusplus
}
#endif

#endif /* TEST_CASE_H */
