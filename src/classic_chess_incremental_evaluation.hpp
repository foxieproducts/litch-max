#pragma once

#include <cstdint>

#include <mcu-max-position.h>

namespace litch_max::classic_chess::internal {

inline constexpr int PIECE_PHASE[8] = {0, 0, 0, 1, 0, 1, 2, 4};

// Attacks, mobility, king distance and pawn relationships still read the board.
struct EvaluationTerms {
    int middle[2] = {};
    int end[2] = {};
    uint8_t pawnRanks[2][8] = {};
    uint8_t bishops[2] = {};
    uint8_t queens[2] = {};
    int16_t phase = 0; // At most 62 promoted Queens: 248 units.
};
static_assert(sizeof(EvaluationTerms) == 40U, "Keep Classic state within its 4 KiB allocation");

int EvaluateIncremental(const mcumax_position& position, const EvaluationTerms& terms);

// Search owns one accumulator, independent of the pure position ABI. Null
// moves change only rules metadata and need no piece-term update.
class EvaluationState {
public:
    void Reset(const mcumax_position& position);
    void MadeChild(const mcumax_position& child, const mcumax_position_undo& undo);
    void UnmakingChild(const mcumax_position& child, const mcumax_position_undo& undo);
    int Evaluate(const mcumax_position& position) const;
private:
    EvaluationTerms m_terms;
};

} // namespace litch_max::classic_chess::internal
