#include "litch_max/classic_chess_search.hpp"
#include "classic_chess_fen.hpp"
#include "duck_chess_rules_internal.hpp"

namespace litch_max::classic_chess::internal {
namespace {
bool ValidMetadata(const duck_chess_rules_internal::DuckPosition& position) {
    using namespace duck_chess_rules_internal;
    uint8_t whiteKings = 0U;
    uint8_t blackKings = 0U;
    for (const char piece : position.board) {
        if (piece == 'K' && ++whiteKings > 1U) return false;
        if (piece == 'k' && ++blackKings > 1U) return false;
    }
    for (size_t i = 0U; i < CHESS_CASTLING_PATTERNS.size(); ++i) {
        if ((position.castlingRights & (1U << i)) == 0U) continue;
        const auto& pattern = CHESS_CASTLING_PATTERNS[i];
        const bool white = i < 2U;
        if (position.board[pattern.kingFrom] != (white ? 'K' : 'k') ||
            position.board[pattern.rookFrom] != (white ? 'R' : 'r')) return false;
    }
    const uint8_t target = position.enPassantSquare;
    if (target == DUCK_CHESS_INVALID_SQUARE) return true;
    const bool white = position.sideToMove == SideToMove::White;
    if (target / 8U != (white ? 2U : 5U) || position.board[target] != '\0' ||
        position.halfmoveClock != 0U) return false;
    // FEN records a double push even without a legal capturer. Verify the
    // opposing pawn and its vacated source, without imposing game reachability.
    return position.board[target + (white ? 8 : -8)] == (white ? 'p' : 'P') &&
        position.board[target + (white ? -8 : 8)] == '\0';
}
} // namespace

bool TryParseFen(const char* fen, mcumax_struct& outState) {
    // Storage also serves setup and Duck terminal states; only Classic callers
    // import the decoded snapshot into stricter, playable rules state.
    using namespace duck_chess_rules_internal;
    DuckPosition parsed;
    if (!ParseDuckPosition(fen, parsed) || !ValidMetadata(parsed)) return false;
    mcumax_struct snapshot{};
    snapshot.current_side = parsed.sideToMove == SideToMove::White
        ? MCUMAX_BOARD_WHITE : MCUMAX_BOARD_BLACK;
    snapshot.en_passant_square = parsed.enPassantSquare < 64U
        ? uint8_t((parsed.enPassantSquare / 8U) * 16U + parsed.enPassantSquare % 8U)
        : MCUMAX_SQUARE_INVALID;
    for (uint8_t square = 0U; square < 64U; ++square) {
        const char raw = parsed.board[square];
        if (raw == '\0') continue;
        const bool white = IsWhitePiece(raw);
        uint8_t type = 0U;
        switch (LowerPiece(raw)) {
            case 'p': type = white ? MCUMAX_PAWN_UPSTREAM : MCUMAX_PAWN_DOWNSTREAM; break;
            case 'n': type = MCUMAX_KNIGHT; break;
            case 'b': type = MCUMAX_BISHOP; break;
            case 'r': type = MCUMAX_ROOK; break;
            case 'q': type = MCUMAX_QUEEN; break;
            case 'k': type = MCUMAX_KING; break;
            default: return false;
        }
        const uint8_t rights = parsed.castlingRights;
        const bool homePawn = type < 3U && square / 8U == (white ? 6U : 1U);
        const bool unmovedKing = type == MCUMAX_KING &&
            ((white && square == 60U && (rights & (WhiteKingSide | WhiteQueenSide))) ||
             (!white && square == 4U && (rights & (BlackKingSide | BlackQueenSide))));
        const bool unmovedRook = type == MCUMAX_ROOK &&
            ((square == 63U && white && (rights & WhiteKingSide)) ||
             (square == 56U && white && (rights & WhiteQueenSide)) ||
             (square == 7U && !white && (rights & BlackKingSide)) ||
             (square == 0U && !white && (rights & BlackQueenSide)));
        const uint8_t moved = homePawn || unmovedKing || unmovedRook ? 0U : MCUMAX_PIECE_MOVED;
        snapshot.board[(square / 8U) * 16U + square % 8U] =
            type | (white ? MCUMAX_BOARD_WHITE : MCUMAX_BOARD_BLACK) | moved;
    }
    outState = snapshot;
    return true;
}

} // namespace litch_max::classic_chess::internal

namespace litch_max::classic_chess {
bool TryPositionKey(const char* fen, uint64_t& outKey) {
    mcumax_struct snapshot{};
    mcumax_position position{};
    if (!internal::TryParseFen(fen, snapshot) ||
        !mcumax_position_import(&position, &snapshot)) return false;
    outKey = litch_max::classic_chess::PositionKey(position);
    return true;
}

} // namespace litch_max::classic_chess
