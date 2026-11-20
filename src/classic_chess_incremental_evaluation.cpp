#include "classic_chess_incremental_evaluation.hpp"

#include "litch_max/classic_chess_tables.hpp"

namespace litch_max::classic_chess::internal {
namespace {

void UpdatePiece(EvaluationTerms& terms, const uint8_t piece, const uint8_t square, const int delta) {
    if (piece == 0U) return;
    const uint8_t type = piece & 7U;
    const unsigned side = (piece & 24U) == MCUMAX_BOARD_WHITE ? 0U : 1U;
    const auto value = SquareValue(type, square, side);
    terms.middle[side] += delta * value.middle;
    terms.end[side] += delta * value.end;
    terms.phase += delta * PIECE_PHASE[type];
    if (type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM) {
        const unsigned file = square & 7U;
        if (type == (side == 0U ? MCUMAX_PAWN_UPSTREAM : MCUMAX_PAWN_DOWNSTREAM)) {
            const uint8_t bit = 1U << (square >> 4U);
            if (delta > 0) terms.pawnRanks[side][file] |= bit;
            else terms.pawnRanks[side][file] &= static_cast<uint8_t>(~bit);
        }
    }
    if (type == MCUMAX_BISHOP) terms.bishops[side] += delta;
    if (type == MCUMAX_QUEEN) terms.queens[side] += delta;
}

void UpdateChangedSquares(EvaluationTerms& terms, const mcumax_position& child,
                          const mcumax_position_undo& undo, const int delta) {
    for (uint8_t index = 0U; index < undo.count; ++index) {
        const uint8_t square = undo.square[index];
        const uint8_t before = undo.piece[index];
        const uint8_t after = child.board[square];
        // Moved flags affect legal moves, but none of these evaluation terms.
        if ((before & 31U) == (after & 31U)) continue;
        UpdatePiece(terms, before, square, -delta);
        UpdatePiece(terms, after, square, delta);
    }
}

} // namespace

void EvaluationState::Reset(const mcumax_position& position) {
    m_terms = EvaluationTerms{};
    for (int square = 0; square < 128; square = (square + 9) & ~8)
        UpdatePiece(m_terms, position.board[square], square, 1);
}

void EvaluationState::MadeChild(const mcumax_position& child, const mcumax_position_undo& undo) {
    UpdateChangedSquares(m_terms, child, undo, 1);
}

void EvaluationState::UnmakingChild(const mcumax_position& child, const mcumax_position_undo& undo) {
    UpdateChangedSquares(m_terms, child, undo, -1);
}

int EvaluationState::Evaluate(const mcumax_position& position) const {
    return EvaluateIncremental(position, m_terms);
}

} // namespace litch_max::classic_chess::internal
