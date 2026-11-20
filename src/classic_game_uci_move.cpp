#include "litch_max/classic_game.hpp"

#include <cstring>

#include "litch_max/chess_notation.hpp"

namespace litch_max {
namespace {
bool ParseSquare(const char* text, uint8_t& square) {
    if (text[0] < 'a' || text[0] > 'h' || text[1] < '1' || text[1] > '8') return false;
    square = static_cast<uint8_t>((('8' - text[1]) << 4) | (text[0] - 'a'));
    return true;
}

uint8_t ParsePromotionPiece(const char symbol) {
    switch (NormalizeUciPromotionSuffixLower(symbol)) {
        case 'q': return MCUMAX_QUEEN;
        case 'r': return MCUMAX_ROOK;
        case 'b': return MCUMAX_BISHOP;
        case 'n': return MCUMAX_KNIGHT;
        default: return 0U;
    }
}
} // namespace

bool ClassicGame::ApplyUciMove(const char* uciMove) {
    if (uciMove == nullptr) return false;
    const size_t length = std::strlen(uciMove);
    if (length != 4U && length != 5U) return false;
    mcumax_position_move move{};
    if (!ParseSquare(uciMove, move.from) || !ParseSquare(uciMove + 2, move.to)) return false;
    if (length == 5U) {
        move.promotion = ParsePromotionPiece(uciMove[4]);
        if (move.promotion == 0U) return false;
    }
    mcumax_position position{};
    mcumax_position_undo undo{};
    if (!mcumax_position_import(&position, &mcumax) ||
        !mcumax_position_make(&position, move, &undo)) return false;

    // The singleton remains the library's FEN/board owner. Copy only chess
    // state; legacy positional-table bytes are not part of the new position.
    for (uint8_t square = 0U; square < 128U; ++square) {
        if ((square & 0x88U) == 0U) mcumax.board[square] = position.board[square];
    }
    mcumax.current_side = position.side;
    mcumax.en_passant_square = position.en_passant;
    // Legacy incremental scores are intentionally not consumed by Classic
    // search. Its standalone reference path starts from an explicit FEN.
    mcumax.score = 0;
    mcumax.non_pawn_material = 0;
    mcumax.best_move_valid = false;
    mcumax.best_square_from = MCUMAX_SQUARE_INVALID;
    mcumax.best_square_to = MCUMAX_SQUARE_INVALID;
    mcumax.best_score = 0;
    mcumax.completed_root_depth = 0U;
    mcumax.root_candidate_count = 0U;
    mcumax.root_candidate_work_count = 0U;
    return true;
}
} // namespace litch_max
