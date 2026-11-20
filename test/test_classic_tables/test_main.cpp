#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstdlib>

#include "litch_max/classic_chess_tables.hpp"

namespace
{
using litch_max::classic_chess::internal::AttackMap;
int g_failures = 0;

void Check(bool condition, const char* label)
{
    if (!condition) { ++g_failures; std::printf("FAIL %s\n", label); }
}

// Frozen pre-LUT arithmetic provides the independent equivalence oracle.
litch_max::classic_chess::internal::SquareScore OriginalSquareScore(int square, int side, uint8_t type)
{
    constexpr int VALUES[8] = {0,100,100,320,0,330,500,900};
    const int rank = side == 0 ? 7 - (square >> 4) : square >> 4;
    const int fileCenter = 7 - std::abs(2 * (square & 7) - 7);
    const int center = fileCenter + 7 - std::abs(2 * (square >> 4) - 7);
    int middle = VALUES[type];
    int end = VALUES[type];
    switch (type)
    {
    case MCUMAX_PAWN_UPSTREAM: case MCUMAX_PAWN_DOWNSTREAM:
        middle += (rank - 1) * 5 + fileCenter * 2; end += (rank - 1) * 8; break;
    case MCUMAX_KNIGHT: middle += center * 3 - (rank == 0 ? 10 : 0); end += center * 2; break;
    case MCUMAX_BISHOP: middle += center * 2 + rank * 2; end += center * 2; break;
    case MCUMAX_ROOK: middle += rank * 2; end += center; break;
    case MCUMAX_QUEEN: middle += center; end += center * 2; break;
    case MCUMAX_KING: middle -= center * 2 + rank * 8; end += center * 4; break;
    default: break;
    }
    return {static_cast<int16_t>(middle), static_cast<int16_t>(end)};
}

bool OnBoard(int square) { return square >= 0 && square < 128 && (square & 0x88) == 0; }

AttackMap OriginalAttacks(uint8_t type, int square, int side)
{
    constexpr int KNIGHT[8] = {-33,-31,-18,-14,14,18,31,33};
    constexpr int KING[8] = {-17,-15,15,17,-16,-1,1,16};
    AttackMap result;
    const int count = type == MCUMAX_KNIGHT || type == MCUMAX_KING ? 8 : 2;
    for (int step = 0; step < count; ++step)
    {
        const int target = square + (type == MCUMAX_KNIGHT ? KNIGHT[step] :
            type == MCUMAX_KING ? KING[step] : (side == 0 ? -16 : 16) + (step == 0 ? -1 : 1));
        if (!OnBoard(target)) continue;
        const unsigned logical = (target >> 4) * 8 + (target & 7);
        (logical < 32U ? result.low : result.high) |= 1U << (logical & 31U);
    }
    return result;
}
}

int main()
{
    using namespace litch_max::classic_chess::internal;
    for (int square = 0; square < 128; ++square)
    {
        if (!OnBoard(square)) continue;
        auto expectedRing = OriginalAttacks(MCUMAX_KING, square, 0);
        const unsigned center = (square >> 4) * 8 + (square & 7);
        (center < 32U ? expectedRing.low : expectedRing.high) |= 1U << (center & 31U);
        const auto ring = KingRing(square);
        Check(ring.low == expectedRing.low && ring.high == expectedRing.high,
              "king ring includes center and independent original king offsets");
        for (int side = 0; side < 2; ++side)
        {
            for (uint8_t type = 0U; type < 8U; ++type)
            {
                const auto original = OriginalSquareScore(square, side, type);
                const auto actual = SquareValue(type, square, side);
                Check(actual.middle == original.middle && actual.end == original.end, "square score parity");
            }
            for (const uint8_t type : {MCUMAX_KNIGHT, MCUMAX_KING, MCUMAX_PAWN_UPSTREAM, MCUMAX_PAWN_DOWNSTREAM})
            {
                const auto expected = OriginalAttacks(type, square, side);
                const auto list = LeaperTargets(type, square, side);
                Check(list.capacity == (type == MCUMAX_KNIGHT || type == MCUMAX_KING ? 8U : 2U), "target list capacity");
                AttackMap listed;
                bool ended = false;
                int previous = -1;
                for (uint8_t slot = 0U; slot < list.capacity; ++slot)
                {
                    const uint8_t target = list.squares[slot];
                    if (target == LEAPER_TARGET_INVALID) { ended = true; continue; }
                    Check(!ended, "valid prefix precedes only INVALID sentinel tail");
                    Check(OnBoard(target) && target > previous, "targets are unique ascending legal 0x88 squares");
                    previous = target;
                    const unsigned logical = (target >> 4U) * 8U + (target & 7U);
                    (logical < 32U ? listed.low : listed.high) |= 1U << (logical & 31U);
                }
                Check(listed.low == expected.low && listed.high == expected.high, "target list equals independent original offsets");
                if (type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM)
                {
                    const auto mask = PawnAttacks(square, side);
                    Check(mask.low == expected.low && mask.high == expected.high, "first-pass pawn mask preserves signed/asymmetric direction");
                }
            }
        }
    }
    for (int square = -128; square <= 255; ++square)
    {
        if (OnBoard(square)) continue;
        const auto ring = KingRing(square);
        Check(ring.low == 0U && ring.high == 0U, "invalid king square has empty ring");
    }
    for (const int square : {INT_MIN, INT_MAX})
    {
        const auto ring = KingRing(square);
        Check(ring.low == 0U && ring.high == 0U, "extreme invalid king square has empty ring");
    }
    std::printf("[CLASSIC_TABLES] failures=%d scores_bytes=%zu attacks_bytes=%zu\n",g_failures,
        sizeof(PIECE_SQUARE_SCORES),sizeof(LEAPER_TARGETS) + sizeof(PAWN_TARGETS) + sizeof(PAWN_ATTACKS) + sizeof(KING_RINGS));
    return g_failures == 0 ? 0 : 1;
}
