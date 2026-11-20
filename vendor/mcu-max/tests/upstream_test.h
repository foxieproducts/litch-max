#ifndef MCUMAX_UPSTREAM_TEST_H
#define MCUMAX_UPSTREAM_TEST_H

#include <stdio.h>

static int mcumax_test_failures;

#define MCUMAX_TEST_CHECK(condition)                                      \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n",                \
                    __FILE__, __LINE__, #condition);                      \
            mcumax_test_failures++;                                       \
        }                                                                 \
    } while (0)

static int mcumax_test_finish(void)
{
    if (mcumax_test_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", mcumax_test_failures);
        return 1;
    }

    return 0;
}

#endif
