#pragma once

#include <algorithm>
#include <cstdlib>

#include "litch_max/classic_chess_evaluation.hpp"
#include "litch_max/classic_chess_kpk.hpp"
#include "classic_chess_incremental_evaluation.hpp"

namespace litch_max::classic_chess::internal {
// These subsets cannot mate by any legal sequence. Two knights, opposite-
// color bishops and mixed minors are deliberately not declared dead.
inline bool DeadMaterial(const mcumax_position& position) {
    uint8_t knights = 0U;
    uint8_t bishops = 0U;
    uint8_t bishopColors = 0U;
    for (uint8_t square = 0U; square < 128U; ++square) {
        if ((square & 0x88U) != 0U) continue;
        const uint8_t type = position.board[square] & 7U;
        if (type == 0U || type == MCUMAX_KING) continue;
        if (type == MCUMAX_KNIGHT) ++knights;
        else if (type == MCUMAX_BISHOP) {
            ++bishops;
            bishopColors |= 1U << (((square >> 4U) + (square & 7U)) & 1U);
        } else return false;
    }
    return (bishops == 0U && knights <= 1U) ||
        (knights == 0U && bishopColors != 3U);
}


// Pure leaf knowledge: callers own terminal, history and draw-clock checks.
inline int EvaluateEndgame(const mcumax_position& position, uint16_t halfmoves, const EvaluationState& evaluation) {
    const auto knowledge = TryProbeKpk(position, halfmoves);
    if (knowledge.outcome == KpkOutcome::Draw) return 0;
    const int score = evaluation.Evaluate(position);
    if (knowledge.outcome != KpkOutcome::PawnWin) return score;
    const int owner = knowledge.pawnSide == MCUMAX_BOARD_WHITE ? 0 : 1;
    const int king = position.king_square[owner];
    int pawn = 0;
    for (int square = 0; square < 128; ++square) {
        if ((square & 0x88) == 0 && ((position.board[square] & 7U) == MCUMAX_PAWN_UPSTREAM ||
            (position.board[square] & 7U) == MCUMAX_PAWN_DOWNSTREAM)) { pawn = square; break; }
    }
    const auto rank = [owner](int square) { return owner == 0 ? 7 - (square >> 4) : square >> 4; };
    const int distance = std::max(std::abs((king & 7) - (pawn & 7)),
        std::abs(rank(king) - std::min(7, rank(pawn) + 2)));
    const int ownerScore = position.side == knowledge.pawnSide ? score : -score;
    // A WDL bit alone cannot rank progress. King support toward the pawn's key
    // squares breaks centralization ties without pretending to be mate distance.
    // Keep this entire heuristic below normal Rook/Queen conversion values.
    const int winScore = 200 + std::clamp(ownerScore / 8 + 4 * rank(king) - 4 * distance, -50, 50);
    return position.side == knowledge.pawnSide ? winScore : -winScore;
}
} // namespace litch_max::classic_chess::internal
