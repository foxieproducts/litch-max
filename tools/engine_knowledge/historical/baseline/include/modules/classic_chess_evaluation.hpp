#pragma once

#include <cstdint>

struct mcumax_position;

namespace modules::classic_chess
{
/** \brief Returns material value in centipawns; kings have no exchange value.
 * Accepts either the low-three-bit piece type or a raw mcu-max piece byte.
 */
int PieceValue(uint8_t type);

/** \brief Evaluates a valid pure Classic position for the side to move.
 * Positive scores favor that side. Reads the complete board, without relying
 * on legacy incremental scores, phase tags, moved flags, or shared engine state.
 * Terminal results and repetition belong to the search, not this evaluator.
 */
int Evaluate(const mcumax_position& position);
}
