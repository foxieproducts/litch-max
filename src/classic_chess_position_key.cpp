#include "litch_max/classic_chess_search.hpp"
#include "classic_chess_identity.hpp"

#include <initializer_list>

namespace litch_max::classic_chess {
namespace internal {
uint8_t EffectiveEnPassant(mcumax_position& position) {
    if (position.en_passant == MCUMAX_SQUARE_INVALID) return MCUMAX_SQUARE_INVALID;
    const int behind = position.en_passant + (position.side == MCUMAX_BOARD_WHITE ? 16 : -16);
    for (const int offset : {-1, 1}) {
        const int from = behind + offset;
        if (from < 0 || from >= 128 || (from & 0x88) != 0) continue;
        const uint8_t type = position.board[from] & 7U;
        if (type != MCUMAX_PAWN_UPSTREAM && type != MCUMAX_PAWN_DOWNSTREAM) continue;
        mcumax_position_undo undo{};
        const mcumax_position_move move{static_cast<uint8_t>(from), position.en_passant, 0U};
        if (mcumax_position_make(&position, move, &undo)) {
            mcumax_position_unmake(&position, &undo);
            return position.en_passant;
        }
    }
    return MCUMAX_SQUARE_INVALID;
}

} // namespace internal

// Royal moved flags matter only through the four actual castling rights.
// Home-pawn double-step state and effective en-passant remain part of identity.
uint64_t PositionKey(mcumax_position& position) {
    uint64_t key = 1469598103934665603ULL;
    for (uint8_t square = 0U; square < 128U; ++square) {
        if ((square & 0x88U) != 0U) continue;
        const uint8_t piece = internal::CanonicalPiece(position.board[square], square);
        key = (key ^ piece) * 1099511628211ULL;
    }
    const uint8_t rights = internal::CastlingRights(position);
    key = (key ^ rights) * 1099511628211ULL;
    key = (key ^ position.side) * 1099511628211ULL;
    return (key ^ internal::EffectiveEnPassant(position)) * 1099511628211ULL;
}

} // namespace litch_max::classic_chess
