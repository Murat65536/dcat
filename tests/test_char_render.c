// Unit tests for the terminal character-rendering helpers. Run them through
// `meson test`: the truecolor expectations rely on stdio being pipes (non-TTY),
// which changes detect_truecolor_support()'s behavior vs. an interactive run.
#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif

#include "terminal/char_render.h"
#include "terminal/truecolor_characters.h"
#include <stdint.h>
#include <stdlib.h>
#include <unity.h>

static void set_env(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : ""); // an empty string removes the variable
#else
    if (value) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

// Pin every variable detect_truecolor_support() reads so the host terminal
// can't leak into the results. The process exits after the run, so nothing
// needs restoring.
void setUp(void) {
    set_env("TERM", "dumb");
    set_env("COLORTERM", NULL);
    set_env("WT_SESSION", NULL);
}

void tearDown(void) {}

// The returned string is exactly 3 bytes with no terminator, so compare with
// EQUAL_MEMORY; EQUAL_STRING would read into the adjacent table row.
static void assert_3digit(const char *expected, uint8_t v) {
    TEST_ASSERT_EQUAL_MEMORY(expected, char_u8_3digit(v), 3);
}

static void test_u8_3digit_covers_range(void) {
    assert_3digit("000", 0);
    assert_3digit("007", 7);
    assert_3digit("042", 42);
    assert_3digit("099", 99);
    assert_3digit("100", 100);
    assert_3digit("255", 255);
}

static void test_u8_3digit_stable_storage(void) {
    const char *first = char_u8_3digit(42);
    const char *second = char_u8_3digit(42);
    TEST_ASSERT_EQUAL_PTR(first, second);
    TEST_ASSERT_EQUAL_MEMORY("042", second, 3);
}

#ifdef _WIN32
static void test_truecolor_requires_tty(void) {
    // stdout is a pipe under meson test, so the isatty gate rejects before any
    // env inspection — even with COLORTERM set.
    set_env("COLORTERM", "truecolor");
    TEST_ASSERT_FALSE(detect_truecolor_support());
    set_env("COLORTERM", NULL);
    TEST_ASSERT_FALSE(detect_truecolor_support());
}
#else
static void test_truecolor_colorterm_values(void) {
    set_env("COLORTERM", "truecolor");
    TEST_ASSERT_TRUE(detect_truecolor_support());
    set_env("COLORTERM", "24bit");
    TEST_ASSERT_TRUE(detect_truecolor_support());
}

static void test_truecolor_rejects_without_hint(void) {
    // With COLORTERM unset or unrecognized and TERM=dumb, the DCS query
    // fallback bails on the non-TTY stdio, so the result is deterministic.
    TEST_ASSERT_FALSE(detect_truecolor_support());
    set_env("COLORTERM", "garbage");
    TEST_ASSERT_FALSE(detect_truecolor_support());
    set_env("COLORTERM", "TRUECOLOR"); // the comparison is case-sensitive
    TEST_ASSERT_FALSE(detect_truecolor_support());
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_u8_3digit_covers_range);
    RUN_TEST(test_u8_3digit_stable_storage);
#ifdef _WIN32
    RUN_TEST(test_truecolor_requires_tty);
#else
    RUN_TEST(test_truecolor_colorterm_values);
    RUN_TEST(test_truecolor_rejects_without_hint);
#endif
    return UNITY_END();
}
