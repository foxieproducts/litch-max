#pragma once

#include "litch_max/classic_chess_search.hpp"
#include "litch_max/duck_chess_search.hpp"

namespace litch_max {
// Compatibility name for direct Duck analysis callers; firmware owns just one
// shared workspace across variants, not a second retained Duck allocation.
using DuckChessAnalysisWorkspace = classic_chess::SearchWorkspace;
} // namespace litch_max
