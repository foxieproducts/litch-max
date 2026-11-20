#include <stdint.h>

#include "mcu-max.h"

#include "upstream_test.h"

uint32_t mcumax_test_platform_hook_calls;
uint32_t mcumax_test_platform_hook_last_node;

int main(void)
{
    mcumax_init();
    (void)mcumax_search_best_move(64, 2);

    MCUMAX_TEST_CHECK(mcumax.node_count != 0);
    MCUMAX_TEST_CHECK(
        mcumax_test_platform_hook_calls == mcumax.node_count);
    MCUMAX_TEST_CHECK(
        mcumax_test_platform_hook_last_node == mcumax.node_count);

    return mcumax_test_finish();
}
