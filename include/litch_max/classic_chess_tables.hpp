#pragma once

#include <array>
#include <cstdint>

#include <mcu-max.h>

namespace litch_max::classic_chess::internal
{
struct SquareScore
{
    int16_t middle = 0;
    int16_t end = 0;
};

struct AttackMap
{
    uint32_t low = 0U;
    uint32_t high = 0U;
};

struct LeaperTargetView
{
    const uint8_t* squares;
    uint8_t capacity;
};

inline constexpr uint8_t LEAPER_TARGET_INVALID = MCUMAX_SQUARE_INVALID;
// Packed valid 0x88 destinations followed by INVALID slots. Lists avoid
// constructing a bitmask only to enumerate it again in evaluation loops.
extern const std::array<std::array<uint8_t, 8U>, 2U * 64U> LEAPER_TARGETS;
extern const std::array<std::array<uint8_t, 2U>, 2U * 64U> PAWN_TARGETS;
extern const std::array<AttackMap, 2U * 64U> PAWN_ATTACKS;
// Center square and adjacent king destinations, independent of occupancy.
extern const std::array<AttackMap, 64U> KING_RINGS;
// Existing color-relative scoring arithmetic, generated without new weights.
extern const std::array<SquareScore, 8U * 64U> PIECE_SQUARE_SCORES;

inline SquareScore SquareValue(uint8_t type, int square, int side)
{
    const unsigned rank = side == 0 ? 7U - (square >> 4) : square >> 4;
    return PIECE_SQUARE_SCORES[type * 64U + rank * 8U + (square & 7)];
}

inline LeaperTargetView LeaperTargets(uint8_t type, int square, int side)
{
    const unsigned index = ((square & 0x70) >> 1) | (square & 7);
    if (type == MCUMAX_KNIGHT || type == MCUMAX_KING)
    {
        const unsigned group = type == MCUMAX_KNIGHT ? 0U : 1U;
        return {LEAPER_TARGETS[group * 64U + index].data(), 8U};
    }
    return {PAWN_TARGETS[side * 64U + index].data(), 2U};
}

inline AttackMap KingRing(int square)
{
    if (square < 0 || square >= 128 || (square & 0x88) != 0) return {};
    const unsigned index = ((square & 0x70) >> 1) | (square & 7);
    return KING_RINGS[index];
}

inline AttackMap PawnAttacks(int square, int side)
{
    const unsigned index = ((square & 0x70) >> 1) | (square & 7);
    return PAWN_ATTACKS[side * 64U + index];
}

}
