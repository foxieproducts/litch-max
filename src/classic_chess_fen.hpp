#pragma once

#include <mcu-max-position.h>

namespace litch_max::classic_chess::internal {

// Shared pure six-field FEN storage decoder, including partial setup and Duck
// terminal positions. Classic import owns playability checks. Rejection leaves
// output unchanged; clocks/history remain caller-owned.
bool TryParseFen(const char* fen, mcumax_struct& outState);

} // namespace litch_max::classic_chess::internal
