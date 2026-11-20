#pragma once

#include <cstdint>

struct mcumax_position;

namespace litch_max::classic_chess
{
enum class KpkOutcome : uint8_t { Unavailable, Draw, PawnWin };

struct KpkResult
{
    KpkOutcome outcome = KpkOutcome::Unavailable;
    // mcu-max board color (8 White, 16 Black); zero for unsupported input.
    uint8_t pawnSide = 0U;
};

constexpr uint16_t KPK_HALFMOVE_UNKNOWN = UINT16_MAX;
constexpr unsigned KPK_BITBASE_BYTES = 24576U;

/** \brief Probes exact king-and-pawn versus king material without mutation.
 * Infinite-rule WDL is stored in const flash. PawnWin is declined for an
 * unknown clock or insufficient margin to the certified next zeroing move.
 * It means a winning strategy exists, not mate distance or progress ordering.
 * Terminal and history-aware repetition checks belong to the caller. A prior
 * reversible history can invalidate PawnWin as an exact search cutoff, even
 * when the current position has not yet repeated three times. Draw stays safe.
 * Unsupported/illegal material or metadata returns Unavailable.
 */
KpkResult TryProbeKpk(const mcumax_position& position,
                      uint16_t reversibleHalfmoves = KPK_HALFMOVE_UNKNOWN);
}
