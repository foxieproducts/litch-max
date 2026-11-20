#include "modules/duck_chess_rules.hpp"

#include <cstdint>
#include <limits>

#include "duck_chess_rules_internal.hpp"
#include "modules/chess_notation.hpp"

namespace modules {
namespace {

using duck_chess_rules_internal::DuckPosition;

char PromotedPiece(const SideToMove side, const char suffixLower) {
    if (side == SideToMove::Black) {
        return suffixLower;
    }
    return static_cast<char>(suffixLower - 'a' + 'A');
}

uint32_t IncrementSaturated(const uint32_t value) {
    return value == std::numeric_limits<uint32_t>::max() ? value : value + 1U;
}

void ReadCapture(const DuckPosition& position,
                 const ParsedUciMove& move,
                 const bool pawn,
                 uint8_t& outSquare,
                 char& outPiece) {
    outSquare = move.toSquare;
    outPiece = position.board[outSquare];
    if (!pawn || outPiece != '\0' ||
        (move.fromSquare % 8U) == (move.toSquare % 8U)) {
        return;
    }
    const int direction = position.sideToMove == SideToMove::White ? -1 : 1;
    outSquare = static_cast<uint8_t>(move.toSquare - direction * 8);
    outPiece = position.board[outSquare];
}

void ApplyBoardMove(DuckPosition& position,
                    const ParsedUciMove& move,
                    const char mover,
                    const bool promotion,
                    const uint8_t captureSquare,
                    const char captured) {
    position.board[move.fromSquare] = '\0';
    if (captured != '\0') {
        position.board[captureSquare] = '\0';
    }
    const runtime::CastlingPattern* castle =
        duck_chess_rules_internal::LowerPiece(mover) == 'k'
            ? duck_chess_rules_internal::FindCastle(move.fromSquare, move.toSquare)
            : nullptr;
    if (castle != nullptr) {
        position.board[castle->rookTo] = position.board[castle->rookFrom];
        position.board[castle->rookFrom] = '\0';
    }
    const char suffix = move.hasPromotion ? move.promotionSuffixLower : 'q';
    position.board[move.toSquare] = promotion
                                        ? PromotedPiece(position.sideToMove, suffix)
                                        : mover;
}

void UpdateFenMetadata(DuckPosition& position,
                       const ParsedUciMove& move,
                       const bool pawn,
                       const bool capture) {
    position.enPassantSquare = DUCK_CHESS_INVALID_SQUARE;
    const uint8_t distance = move.fromSquare > move.toSquare
                                 ? static_cast<uint8_t>(move.fromSquare - move.toSquare)
                                 : static_cast<uint8_t>(move.toSquare - move.fromSquare);
    if (pawn && distance == 16U) {
        position.enPassantSquare = static_cast<uint8_t>(
            (static_cast<uint16_t>(move.fromSquare) + move.toSquare) / 2U);
    }
    position.halfmoveClock = (pawn || capture)
                                 ? 0U
                                 : IncrementSaturated(position.halfmoveClock);
    if (position.sideToMove == SideToMove::Black) {
        position.fullmoveNumber = IncrementSaturated(position.fullmoveNumber);
    }
    position.sideToMove = position.sideToMove == SideToMove::White
                              ? SideToMove::Black
                              : SideToMove::White;
}

void SetCaptureResult(const uint8_t captureSquare,
                      const char captured,
                      DuckChessMoveResult& outResult) {
    outResult.capturedPieceValid = captured != '\0';
    outResult.capturedPieceFen = captured;
    outResult.captureSquareValid = captured != '\0';
    outResult.captureSquare = captured != '\0'
                                  ? captureSquare
                                  : DUCK_CHESS_INVALID_SQUARE;
    outResult.capturedKing = duck_chess_rules_internal::LowerPiece(captured) == 'k';
}

} // namespace

namespace duck_chess_rules_internal {
bool ApplyMove(DuckPosition& position, uint8_t duck, const ParsedUciMove& move,
               MoveCapture& capture) {
    capture = MoveCapture{};
    if (!IsValidDuck(position, duck) || move.fromSquare >= 64U || move.toSquare >= 64U ||
        (GenerateDestinations(position, duck, move.fromSquare) & (1ULL << move.toSquare)) == 0ULL) return false;
    const char mover = position.board[move.fromSquare];
    const bool pawn = LowerPiece(mover) == 'p';
    const bool promotion = pawn && (move.toSquare < 8U || move.toSquare >= 56U);
    const char suffix = move.promotionSuffixLower;
    if (move.hasPromotion && (!promotion || (suffix != 'q' && suffix != 'r' && suffix != 'b' && suffix != 'n'))) return false;
    ReadCapture(position, move, pawn, capture.square, capture.piece);
    UpdateCastlingRights(position, mover, move.fromSquare, capture.piece, capture.square);
    ApplyBoardMove(position, move, mover, promotion, capture.square, capture.piece);
    UpdateFenMetadata(position, move, pawn, capture.piece != '\0');
    return true;
}
} // namespace duck_chess_rules_internal

bool GetDuckLegalDestinationsMask(const char* fen,
                                  const uint8_t duckSquare,
                                  const uint8_t fromSquare,
                                  uint64_t& outMask) {
    outMask = 0ULL;
    DuckPosition position;
    if (!duck_chess_rules_internal::ParseDuckPosition(fen, position) ||
        !duck_chess_rules_internal::IsValidDuck(position, duckSquare)) {
        return false;
    }
    outMask = duck_chess_rules_internal::GenerateDestinations(
        position, duckSquare, fromSquare);
    return true;
}

bool IsDuckSoleBlockerForPiece(const char* fen,
                               const uint8_t duckSquare,
                               const uint8_t fromSquare) {
    DuckPosition position;
    if (!duck_chess_rules_internal::ParseDuckPosition(fen, position) ||
        !duck_chess_rules_internal::IsValidDuck(position, duckSquare)) {
        return false;
    }
    const uint64_t withDuck =
        duck_chess_rules_internal::GenerateDestinations(
            position, duckSquare, fromSquare);
    const uint64_t withoutDuck =
        duck_chess_rules_internal::GenerateDestinations(
            position, DUCK_CHESS_INVALID_SQUARE, fromSquare);
    return withDuck == 0ULL && withoutDuck != 0ULL;
}

bool TryApplyDuckMove(const char* fen,
                      const uint8_t duckSquare,
                      const char* uciMove,
                      DuckChessMoveResult& outResult) {
    outResult = DuckChessMoveResult{};
    DuckPosition position;
    ParsedUciMove move;
    if (!duck_chess_rules_internal::ParseDuckPosition(fen, position) ||
        !duck_chess_rules_internal::IsValidDuck(position, duckSquare) ||
        !TryParseUciMove(uciMove, move)) {
        return false;
    }
    duck_chess_rules_internal::MoveCapture capture;
    if (!duck_chess_rules_internal::ApplyMove(position, duckSquare, move, capture)) return false;
    SetCaptureResult(capture.square, capture.piece, outResult);
    return duck_chess_rules_internal::BuildFen(position, outResult.resultingFen);
}

bool HasAnyDuckChessMove(const char* fen, const uint8_t duckSquare) {
    DuckPosition position;
    if (!duck_chess_rules_internal::ParseDuckPosition(fen, position) ||
        !duck_chess_rules_internal::IsValidDuck(position, duckSquare)) {
        return false;
    }
    for (uint8_t square = 0U; square < 64U; ++square) {
        if (duck_chess_rules_internal::GenerateDestinations(
                position, duckSquare, square) != 0ULL) {
            return true;
        }
    }
    return false;
}

} // namespace modules
