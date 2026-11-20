#pragma once

#include <cstddef>
#include <cstdint>
#include <mcu-max-position.h>
#include "litch_max/types.hpp"

namespace litch_max {
using ClassicPosition = mcumax_position;
using ClassicMove = mcumax_position_move;

// Facade over the retained mcu-max singleton, not an independent game instance.
// One caller owns all reads/mutations; never access it while another search
// owner uses the shared game lane. Pure SearchWorkspace inputs are separate.
class ClassicGame {
public:
    enum class PieceSide : uint8_t { None = 0, White = 1, Black = 2 };
    void Reset(uint32_t hashSeed = 1U);
    void SetHashSeed(uint32_t hashSeed);
    void PrepareSearch();
    // Loads six-field FEN, including partial setup and Duck terminal positions.
    // Counters are strict uint32 decimals (fullmove >= 1). Malformed fields,
    // duplicate kings or inconsistent castling/EP metadata leave game/search
    // state unchanged. Import/Classic rules still require a playable position;
    // storage does not prove game reachability. Clocks/history stay caller-owned.
    bool SetFen(const char* fen, uint32_t hashSeed = 1U);
    // The retained engine projects counter fields as "0 1". Preserve the real
    // counters separately when exporting a game or configuring search.
    bool GetFen(char* outFen, size_t outSize) const;
    bool ApplyUciMove(const char* uciMove);
    SideToMove GetSideToMove() const;
    PieceSide GetPieceSideAtSquare(uint8_t square) const;
    bool IsInCheck(SideToMove side) const;
    bool IsInCheckmate(SideToMove side) const;
    bool IsInStalemate(SideToMove side) const;
    bool GetLegalDestinationsMask(uint8_t fromSquare, uint64_t& outDestMask64);
    bool Import(ClassicPosition& position) const;
};
} // namespace litch_max
