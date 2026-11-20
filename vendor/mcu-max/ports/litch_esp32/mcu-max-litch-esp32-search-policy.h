#ifndef MCU_MAX_LITCH_ESP32_SEARCH_POLICY_H
#define MCU_MAX_LITCH_ESP32_SEARCH_POLICY_H

#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if defined(LITCH_ESP32S3)
#define MCUMAX_LITCH_ESP32_YIELD_NODE_MASK 0x1ffu
#else
#define MCUMAX_LITCH_ESP32_YIELD_NODE_MASK 0x1fffu
#endif

static inline void mcumax_litch_esp32_apply_search_policy(
    const uint32_t node_count) {
    if ((node_count & MCUMAX_LITCH_ESP32_YIELD_NODE_MASK) == 0u) {
        /* Let the IDLE task feed TWDT and keep board tasks responsive. */
        vTaskDelay(1);
    }
}

#define MCUMAX_PLATFORM_COOPERATE(node_count) \
    mcumax_litch_esp32_apply_search_policy((uint32_t)(node_count))

#endif
