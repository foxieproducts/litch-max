#include "litch_max/classic_chess_search.hpp"
#include "litch_max/classic_chess_evaluation.hpp"
#include <limits>

namespace litch_max::classic_chess {
namespace {
char PromotionChar(const uint8_t piece) {
    switch (piece) {
        case MCUMAX_QUEEN: return 'q';
        case MCUMAX_ROOK: return 'r';
        case MCUMAX_BISHOP: return 'b';
        case MCUMAX_KNIGHT: return 'n';
        default: return '\0';
    }
}

} // namespace

bool FindImmediateCandidate(mcumax_position root, ThinkCandidate& out) {
    out = ThinkCandidate{};
    mcumax_position_iterator iterator{};
    mcumax_position_iterator_init(&iterator);
    mcumax_position_move move{};
    int bestScore = std::numeric_limits<int>::min();
    while (mcumax_position_iterator_next(&root, &iterator, &move)) {
        mcumax_position_undo undo{};
        if (!mcumax_position_make_generated(&root, move, &undo)) continue;
        const int score = -classic_chess::Evaluate(root);
        mcumax_position_unmake(&root, &undo);
        if (out.IsAvailable() && score <= bestScore) continue;
        bestScore = score;
        out.fromSquare = (move.from >> 4U) * 8U + (move.from & 7U);
        out.toSquare = (move.to >> 4U) * 8U + (move.to & 7U);
        out.promotion = PromotionChar(move.promotion);
        out.source = ThinkCandidateSource::Seed;
        out.rootKind = ThinkRootKind::Seed;
        out.score = score;
        out.scoreValid = true;
        out.scoreUnits = ThinkScoreUnits::ClassicPawn100;
        out.scorePerspective = root.side == MCUMAX_BOARD_WHITE ? SideToMove::White : SideToMove::Black;
    }
    return out.IsAvailable();
}

} // namespace litch_max::classic_chess
