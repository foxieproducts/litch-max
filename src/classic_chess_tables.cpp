#include "litch_max/classic_chess_tables.hpp"

namespace litch_max::classic_chess::internal
{
namespace
{
constexpr int VALUES[8] = {0, 100, 100, 320, 0, 330, 500, 900};

constexpr int Abs(int value) { return value < 0 ? -value : value; }

constexpr std::array<SquareScore, 8U * 64U> MakeSquareScores()
{
    std::array<SquareScore, 8U * 64U> scores{};
    for (unsigned type = 0U; type < 8U; ++type)
    {
        for (int square = 0; square < 64; ++square)
        {
            const int rank = square / 8;
            const int fileCenter = 7 - Abs(2 * (square % 8) - 7);
            const int center = fileCenter + 7 - Abs(2 * rank - 7);
            int middle = VALUES[type];
            int end = VALUES[type];
            switch (type)
            {
            case MCUMAX_PAWN_UPSTREAM:
            case MCUMAX_PAWN_DOWNSTREAM:
                middle += (rank - 1) * 5 + fileCenter * 2;
                end += (rank - 1) * 8;
                break;
            case MCUMAX_KNIGHT:
                middle += center * 3 - (rank == 0 ? 10 : 0);
                end += center * 2;
                break;
            case MCUMAX_BISHOP:
                middle += center * 2 + rank * 2;
                end += center * 2;
                break;
            case MCUMAX_ROOK:
                middle += rank * 2;
                end += center;
                break;
            case MCUMAX_QUEEN:
                middle += center;
                end += center * 2;
                break;
            case MCUMAX_KING:
                middle -= center * 2 + rank * 8;
                end += center * 4;
                break;
            default:
                break;
            }
            scores[type * 64U + square] = {static_cast<int16_t>(middle), static_cast<int16_t>(end)};
        }
    }
    return scores;
}

constexpr std::array<std::array<uint8_t, 8U>, 2U * 64U> MakeLeaperTargets()
{
    std::array<std::array<uint8_t, 8U>, 2U * 64U> targets{};
    for (unsigned group = 0U; group < 2U; ++group)
    {
        for (int square = 0; square < 64; ++square)
        {
            auto& list = targets[group * 64U + square];
            for (auto& target : list) target = LEAPER_TARGET_INVALID;
            unsigned count = 0U;
            for (int target = 0; target < 64; ++target)
            {
                const int ranks = Abs(target / 8 - square / 8);
                const int files = Abs(target % 8 - square % 8);
                const bool knight = group == 0U && ranks * files == 2;
                const bool king = group == 1U && ranks <= 1 && files <= 1 && (ranks != 0 || files != 0);
                if (knight || king) list[count++] = static_cast<uint8_t>((target / 8) * 16 + target % 8);
            }
        }
    }
    return targets;
}

constexpr std::array<std::array<uint8_t, 2U>, 2U * 64U> MakePawnTargets()
{
    std::array<std::array<uint8_t, 2U>, 2U * 64U> targets{};
    for (unsigned side = 0U; side < 2U; ++side)
    {
        for (int square = 0; square < 64; ++square)
        {
            auto& list = targets[side * 64U + square];
            for (auto& target : list) target = LEAPER_TARGET_INVALID;
            const int source = (square / 8) * 16 + square % 8;
            const int step = side == 0U ? -16 : 16;
            unsigned count = 0U;
            for (int file : {-1, 1})
            {
                const int target = source + step + file;
                if (target >= 0 && target < 128 && (target & 0x88) == 0)
                    list[count++] = static_cast<uint8_t>(target);
            }
        }
    }
    return targets;
}

constexpr std::array<AttackMap, 2U * 64U> MakePawnAttacks()
{
    std::array<AttackMap, 2U * 64U> attacks{};
    const auto targets = MakePawnTargets();
    for (unsigned square = 0U; square < targets.size(); ++square)
    {
        for (const uint8_t target : targets[square])
        {
            if (target == LEAPER_TARGET_INVALID) break;
            const unsigned logical = ((target & 0x70U) >> 1U) | (target & 7U);
            (logical < 32U ? attacks[square].low : attacks[square].high) |= 1U << (logical & 31U);
        }
    }
    return attacks;
}

constexpr std::array<AttackMap, 64U> MakeKingRings()
{
    std::array<AttackMap, 64U> rings{};
    const auto targets = MakeLeaperTargets();
    for (unsigned square = 0U; square < rings.size(); ++square)
    {
        (square < 32U ? rings[square].low : rings[square].high) |= 1U << (square & 31U);
        for (const uint8_t target : targets[64U + square])
        {
            if (target == LEAPER_TARGET_INVALID) break;
            const unsigned logical = ((target & 0x70U) >> 1U) | (target & 7U);
            (logical < 32U ? rings[square].low : rings[square].high) |= 1U << (logical & 31U);
        }
    }
    return rings;
}
}

const std::array<SquareScore, 8U * 64U> PIECE_SQUARE_SCORES = MakeSquareScores();
const std::array<std::array<uint8_t, 8U>, 2U * 64U> LEAPER_TARGETS = MakeLeaperTargets();
const std::array<std::array<uint8_t, 2U>, 2U * 64U> PAWN_TARGETS = MakePawnTargets();
const std::array<AttackMap, 2U * 64U> PAWN_ATTACKS = MakePawnAttacks();
const std::array<AttackMap, 64U> KING_RINGS = MakeKingRings();
static_assert(sizeof(PIECE_SQUARE_SCORES) == 2048U, "Piece-square scores must remain compact");
static_assert(sizeof(LEAPER_TARGETS) == 1024U, "Knight/King targets must remain compact");
static_assert(sizeof(PAWN_TARGETS) == 256U, "Pawn targets must remain compact");
static_assert(sizeof(PAWN_ATTACKS) == 1024U, "Pawn masks must remain compact");
static_assert(sizeof(KING_RINGS) == 512U, "King rings must remain compact");
}
