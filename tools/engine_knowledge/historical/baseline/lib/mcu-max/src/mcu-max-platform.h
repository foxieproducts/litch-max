#ifndef MCU_MAX_PLATFORM_H
#define MCU_MAX_PLATFORM_H

#include <stdint.h>

#include "mcu-max-downstream-config.h"

#ifndef MCUMAX_PLATFORM_COOPERATE
#define MCUMAX_PLATFORM_COOPERATE(node_count) ((void)(node_count))
#endif

#endif
