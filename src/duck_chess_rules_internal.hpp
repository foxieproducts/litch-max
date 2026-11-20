#pragma once

#include <array>
#include <cstdint>

#include "litch_max/duck_chess_rules.hpp"
#include "litch_max/types.hpp"
#include "litch_max/chess_notation.hpp"
#include "litch_max/chess_castling_patterns.hpp"

namespace litch_max::duck_chess_rules_internal {

enum CastlingRight : uint8_t {
    WhiteKingSide = 1U << 0U,
    WhiteQueenSide = 1U << 1U,
    BlackKingSide = 1U << 2U,
    BlackQueenSide = 1U << 3U,
};

struct DuckPosition {
    std::array<char, 64> board{};
    SideToMove sideToMove = SideToMove::White;
    uint8_t castlingRights = 0U;
    uint8_t enPassantSquare = DUCK_CHESS_INVALID_SQUARE;
    uint32_t halfmoveClock = 0U;
    uint32_t fullmoveNumber = 1U;
};

struct MoveCapture {
    uint8_t square = DUCK_CHESS_INVALID_SQUARE;
    char piece = '\0';
};
// The FEN-facing reducer and search share this exact movement/metadata owner.
// Rejected moves leave the supplied position unchanged; no FEN work per node.
bool ApplyMove(DuckPosition& position, uint8_t duck, const ParsedUciMove& move,
               MoveCapture& capture);

bool IsPiece(char c);
bool IsWhitePiece(char c);
bool IsPieceForSide(char c, SideToMove side);
char LowerPiece(char c);

bool ParseDuckPosition(const char* fen, DuckPosition& out);
bool BuildFen(const DuckPosition& position, DuckChessFen& outFen);
bool IsValidDuck(const DuckPosition& position, uint8_t duckSquare);
uint64_t GenerateDestinations(const DuckPosition& position,
                              uint8_t duckSquare,
                              uint8_t fromSquare);

const CastlingPattern* FindCastle(uint8_t from, uint8_t to);
void UpdateCastlingRights(DuckPosition& position,
                          char mover,
                          uint8_t from,
                          char captured,
                          uint8_t captureSquare);

} // namespace litch_max::duck_chess_rules_internal
