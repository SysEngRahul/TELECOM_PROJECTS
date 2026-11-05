/* Minimal test harness: no external framework needed. */
#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_failed++;                                                          \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b) CHECK((long long)(a) == (long long)(b))
#define CHECK_NEAR(a, b, eps) CHECK(fabs((double)(a) - (double)(b)) <= (eps))

#define RUN_TEST(fn)                                                             \
    do {                                                                         \
        int before = g_failed;                                                   \
        fn();                                                                    \
        printf("[%s] %s\n", (g_failed == before) ? " OK " : "FAIL", #fn);        \
    } while (0)

static inline int test_finish(const char *suite)
{
    printf("%s: %d checks, %d failed\n", suite, g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}

#endif
