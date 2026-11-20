#pragma once

#include <cstdint>
#include <mcu-max-position.h>

namespace litch_max::classic_chess::internal {

// Rules identity ignores moved flags except a home pawn's double-step state.
inline uint8_t CanonicalPiece(const uint8_t piece, const uint8_t square) {
    const uint8_t type = piece & 7U;
    const bool homePawn = (type == MCUMAX_PAWN_UPSTREAM && (square >> 4U) == 6U) ||
                          (type == MCUMAX_PAWN_DOWNSTREAM && (square >> 4U) == 1U);
    return homePawn ? piece : piece & 31U;
}

inline uint8_t CastlingRights(const mcumax_position& position) {
    uint8_t rights = 0U;
    if (position.board[116] == (MCUMAX_KING | MCUMAX_BOARD_WHITE)) {
        if (position.board[119] == (MCUMAX_ROOK | MCUMAX_BOARD_WHITE)) rights |= 1U;
        if (position.board[112] == (MCUMAX_ROOK | MCUMAX_BOARD_WHITE)) rights |= 2U;
    }
    if (position.board[4] == (MCUMAX_KING | MCUMAX_BOARD_BLACK)) {
        if (position.board[7] == (MCUMAX_ROOK | MCUMAX_BOARD_BLACK)) rights |= 4U;
        if (position.board[0] == (MCUMAX_ROOK | MCUMAX_BOARD_BLACK)) rights |= 8U;
    }
    return rights;
}

// A legal capture probe restores the supplied position before returning.
uint8_t EffectiveEnPassant(mcumax_position& position);

} // namespace litch_max::classic_chess::internal
