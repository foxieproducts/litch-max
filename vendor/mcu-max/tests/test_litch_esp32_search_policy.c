#include <stdint.h>

#include "mcu-max-platform.h"

#include "upstream_test.h"

static uint32_t delay_calls;
static uint32_t last_delay_ticks;

void vTaskDelay(const uint32_t ticks)
{
    delay_calls++;
    last_delay_ticks = ticks;
}

int main(void)
{
    const uint32_t interval = MCUMAX_LITCH_ESP32_YIELD_NODE_MASK + 1u;

    MCUMAX_PLATFORM_COOPERATE(interval - 1u);
    MCUMAX_TEST_CHECK(delay_calls == 0u);

    MCUMAX_PLATFORM_COOPERATE(interval);
    MCUMAX_TEST_CHECK(delay_calls == 1u);
    MCUMAX_TEST_CHECK(last_delay_ticks == 1u);

    MCUMAX_PLATFORM_COOPERATE(interval + 1u);
    MCUMAX_TEST_CHECK(delay_calls == 1u);

    MCUMAX_PLATFORM_COOPERATE(interval * 2u);
    MCUMAX_TEST_CHECK(delay_calls == 2u);
    MCUMAX_TEST_CHECK(last_delay_ticks == 1u);

#if defined(LITCH_ESP32S3)
    MCUMAX_TEST_CHECK(interval == 512u);
#else
    MCUMAX_TEST_CHECK(interval == 8192u);
#endif

    return mcumax_test_finish();
}
