#pragma once

#include <cstddef>
#include <cstdint>

#include <mcu-max-position.h>

namespace litch_max::classic_chess {

inline constexpr uint8_t OPENING_BOOK_MAX_PLIES = 16U;
inline constexpr uint8_t OPENING_BOOK_MIN_OFFLINE_DEPTH = 5U;

// Supply the complete move sequence from the standard Classic starting position.
// An imported FEN, truncated history, or another variant is not such a prefix.
struct OpeningBookHistory {
    const mcumax_position_move* initialMoves = nullptr;
    uint16_t halfmoveClock = 0U;
    uint16_t fullmoveNumber = 1U;
    uint8_t moveCount = 0U;
    bool fromCanonicalStart = false;
};

struct BookMove {
    mcumax_position_move move{MCUMAX_SQUARE_INVALID, MCUMAX_SQUARE_INVALID, 0U};
    uint64_t positionKey = 0U;
    // Baseline engine score from the side to move, not a user-facing value.
    int16_t offlineScore = 0;
    uint16_t sourceLine = 0U;
    // Completed ordinary plies in offline Full search, never live work.
    uint8_t offlineDepth = 0U;
    uint8_t openingPly = 0U;
};

struct OpeningBookMetadata {
    const char* baselineIdentity;
    const char* baselineInputsSha256;
    const char* seedSha256;
    const char* recordsSha256;
    size_t entryCount;
    size_t sourceLineCount;
    size_t storageBytes;
};

// \brief Returns a legal offline-validated move for a bounded played opening.
// \param position Current pure Classic position; never changed by this query.
// \param history Complete initial move prefix and its actual FEN counters.
// \param outMove Receives offline provenance on success; unchanged on a miss.
// \return True only after exact state, history, clocks and legality checks.
// No singleton access, allocation, search, visibility or publication occurs.
bool TryOpeningBook(const mcumax_position& position,
                    const OpeningBookHistory& history, BookMove& outMove);

// \brief Identifies the immutable generated data and its actual storage size.
const OpeningBookMetadata& GetOpeningBookMetadata();

} // namespace litch_max::classic_chess
