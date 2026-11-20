#include <cstdint>

#include <mcu-max-position.h>

#include "litch_max/classic_chess_kpk.hpp"

namespace litch_max::classic_chess
{
namespace
{
#include "classic_chess_kpk_data.inc"
static_assert(sizeof(KPK_WIN_BITS) == KPK_BITBASE_BYTES, "KPK flash asset must stay 24KiB");
static_assert(KPK_MAX_ZEROING_PLIES < 100U, "KPK proof must fit the fifty-move window");

int Abs(int value)
{
    return value < 0 ? -value : value;
}

int ToSquare64(uint8_t square)
{
    return (7 - (square >> 4U)) * 8 + (square & 7U);
}

bool Adjacent(int lhs, int rhs)
{
    return Abs(lhs / 8 - rhs / 8) <= 1 && Abs(lhs % 8 - rhs % 8) <= 1;
}

struct Material
{
    uint8_t kings[2] = {MCUMAX_SQUARE_INVALID, MCUMAX_SQUARE_INVALID};
    uint8_t pawn = MCUMAX_SQUARE_INVALID;
    uint8_t pawnSide = 0U;
};

bool ReadMaterial(const mcumax_position& position, Material& material)
{
    if (position.side != MCUMAX_BOARD_WHITE && position.side != MCUMAX_BOARD_BLACK)
        return false;
    for (unsigned square = 0; square < 128U; ++square)
    {
        const uint8_t piece = position.board[square];
        if (piece == 0U) continue;
        if ((square & 0x88U) != 0U || (piece & 0xc0U) != 0U) return false;
        const uint8_t type = piece & 7U;
        const uint8_t side = piece & 24U;
        if (side != MCUMAX_BOARD_WHITE && side != MCUMAX_BOARD_BLACK) return false;
        if (type == MCUMAX_KING)
        {
            const unsigned index = side == MCUMAX_BOARD_WHITE ? 0U : 1U;
            if (material.kings[index] != MCUMAX_SQUARE_INVALID) return false;
            material.kings[index] = square;
        }
        else if (type == (side == MCUMAX_BOARD_WHITE ? MCUMAX_PAWN_UPSTREAM : MCUMAX_PAWN_DOWNSTREAM))
        {
            if (material.pawn != MCUMAX_SQUARE_INVALID || (square & 0x70U) == 0U ||
                (square & 0x70U) == 0x70U) return false;
            // A legal pawn cannot return to its starting rank. A synthetic
            // moved pawn there has fewer moves than this canonical bitbase.
            const uint8_t startRank = side == MCUMAX_BOARD_WHITE ? 0x60U : 0x10U;
            if ((square & 0x70U) == startRank && (piece & MCUMAX_PIECE_MOVED) != 0U)
                return false;
            material.pawn = square;
            material.pawnSide = side;
        }
        else return false;
    }
    if (material.pawn == MCUMAX_SQUARE_INVALID ||
        material.kings[0] == MCUMAX_SQUARE_INVALID || material.kings[1] == MCUMAX_SQUARE_INVALID ||
        material.kings[0] != position.king_square[0] || material.kings[1] != position.king_square[1])
        return false;
    if (position.en_passant != MCUMAX_SQUARE_INVALID)
    {
        const int step = material.pawnSide == MCUMAX_BOARD_WHITE ? -16 : 16;
        const uint8_t doubleRank = material.pawnSide == MCUMAX_BOARD_WHITE ? 0x40U : 0x30U;
        if (position.side == material.pawnSide || (material.pawn & 0x70U) != doubleRank ||
            int(position.en_passant) != material.pawn - step || position.board[position.en_passant] != 0U)
            return false;
    }
    return true;
}
}

KpkResult TryProbeKpk(const mcumax_position& position, uint16_t reversibleHalfmoves)
{
    Material material;
    if (!ReadMaterial(position, material)) return {};
    const unsigned owner = material.pawnSide == MCUMAX_BOARD_WHITE ? 0U : 1U;
    int pawn = ToSquare64(material.pawn);
    int ownerKing = ToSquare64(material.kings[owner]);
    int defenderKing = ToSquare64(material.kings[owner ^ 1U]);
    if (owner == 1U) { pawn ^= 56; ownerKing ^= 56; defenderKing ^= 56; }
    if ((pawn & 7) > 3) { pawn ^= 7; ownerKing ^= 7; defenderKing ^= 7; }
    if (Adjacent(ownerKing, defenderKing)) return {};
    const bool defenderTurn = position.side != material.pawnSide;
    if (!defenderTurn && defenderKing / 8 == pawn / 8 + 1 &&
        Abs(defenderKing % 8 - pawn % 8) == 1) return {};
    const unsigned pawnIndex = (pawn / 8 - 1) * 4 + pawn % 8;
    const unsigned index = (((pawnIndex * 64U + ownerKing) * 64U + defenderKing) * 2U) + defenderTurn;
    if ((KPK_WIN_BITS[index / 8U] & (1U << (index % 8U))) == 0U)
        return {KpkOutcome::Draw, material.pawnSide};
    // Count the zeroing move itself in the margin, deliberately declining
    // some wins near the claim boundary. No history or mate distance is stored.
    if (reversibleHalfmoves >= 100U - KPK_MAX_ZEROING_PLIES)
        return {KpkOutcome::Unavailable, material.pawnSide};
    return {KpkOutcome::PawnWin, material.pawnSide};
}
}
