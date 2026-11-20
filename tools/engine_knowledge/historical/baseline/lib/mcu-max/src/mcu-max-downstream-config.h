#ifndef MCU_MAX_DOWNSTREAM_CONFIG_H
#define MCU_MAX_DOWNSTREAM_CONFIG_H

/*
 * Optional downstream configuration boundary.
 *
 * Portable mcu-max behavior must not depend on a downstream product. A vendor
 * integration may define the hooks consumed by mcu-max-platform.h here.
 */

#if defined(LITCH_ESP32) && !defined(MCUMAX_PLATFORM_COOPERATE)
#include "../ports/litch_esp32/mcu-max-litch-esp32-search-policy.h"
#endif

#endif
