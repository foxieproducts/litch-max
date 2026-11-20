#pragma once

#include <cstdint>

#include "foxie_embedded/fixed_string.hpp"
#include "litch_max/fen_constants.hpp"

namespace litch_max {

inline constexpr uint8_t DUCK_CHESS_INVALID_SQUARE = 255U;

using DuckChessFen = fxe::FixedString<FEN_BUFFER_CAPACITY>;

struct DuckChessMoveResult {
    DuckChessFen resultingFen;
    bool capturedPieceValid = false;
    char capturedPieceFen = '\0';
    bool captureSquareValid = false;
    uint8_t captureSquare = DUCK_CHESS_INVALID_SQUARE;
    bool capturedKing = false;
};

// Returns pseudo-legal destinations for the FEN side-to-move. The duck is a
// neutral, non-capturable blocker and is deliberately absent from the FEN.
bool GetDuckLegalDestinationsMask(const char* fen,
                                  uint8_t duckSquare,
                                  uint8_t fromSquare,
                                  uint64_t& outMask);

// True only when this side-to-move piece has no destination with the Duck in
// place but has at least one destination when that neutral blocker is removed.
// This is the narrow admission rule for the one-time opening relocation.
bool IsDuckSoleBlockerForPiece(const char* fen,
                               uint8_t duckSquare,
                               uint8_t fromSquare);

// Applies one pseudo-legal chess move while treating duckSquare as a neutral
// blocker. Promotion without a suffix defaults to Queen. The returned FEN has
// already advanced side/fullmove metadata, so a combined-turn coordinator must
// stage it until the mandatory duck relocation completes.
bool TryApplyDuckMove(const char* fen,
                      uint8_t duckSquare,
                      const char* uciMove,
                      DuckChessMoveResult& outResult);

// Fowling query: true when the FEN side-to-move has at least one pseudo-legal
// chess move with the duck fixed on duckSquare. Invalid input returns false.
bool HasAnyDuckChessMove(const char* fen, uint8_t duckSquare);

} // namespace litch_max
