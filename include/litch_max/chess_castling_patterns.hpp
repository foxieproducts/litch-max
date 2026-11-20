#pragma once

#include <array>
#include <cstdint>

namespace litch_max {

struct CastlingPattern {
    uint8_t kingFrom = 0;
    uint8_t kingTo = 0;
    uint8_t rookFrom = 0;
    uint8_t rookTo = 0;
};

inline constexpr std::array<CastlingPattern, 4> CHESS_CASTLING_PATTERNS = {{
    {60, 62, 63, 61},
    {60, 58, 56, 59},
    {4, 6, 7, 5},
    {4, 2, 0, 3},
}};

} // namespace litch_max
