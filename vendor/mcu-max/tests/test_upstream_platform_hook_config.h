#ifndef MCU_MAX_TEST_UPSTREAM_PLATFORM_HOOK_CONFIG_H
#define MCU_MAX_TEST_UPSTREAM_PLATFORM_HOOK_CONFIG_H

#include <stdint.h>

extern uint32_t mcumax_test_platform_hook_calls;
extern uint32_t mcumax_test_platform_hook_last_node;

#define MCUMAX_PLATFORM_COOPERATE(node_count)      \
    do {                                            \
        mcumax_test_platform_hook_calls++;          \
        mcumax_test_platform_hook_last_node =       \
            (uint32_t)(node_count);                 \
    } while (0)

#endif
