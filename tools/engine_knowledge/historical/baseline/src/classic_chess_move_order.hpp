#pragma once

#include <array>
#include <cstdint>

#include <mcu-max-position.h>

namespace modules::classic_chess::internal {

constexpr mcumax_position_move INVALID_MOVE{MCUMAX_SQUARE_INVALID, MCUMAX_SQUARE_INVALID, 0U};

bool IsCapture(const mcumax_position& position, mcumax_position_move move);

// Conservative screen for a quiet move with an initially unchecked opponent.
// Positive results still need make/check; it never excludes a real new check.
bool MayCreateQuietCheck(const mcumax_position& position, mcumax_position_move move);
enum class MoveSelection : uint8_t { All, Tactical };


// A bounded prefix orders the best captures; the remaining generator streams
// every move. Ordering never substitutes for legal generation or search.
class MoveOrder {
public:
    void Begin(mcumax_position_move preferred = INVALID_MOVE);
    bool Next(const mcumax_position& position, mcumax_position_move& move,
              MoveSelection selection = MoveSelection::All);
    void RememberKiller(mcumax_position_move move);

private:
    bool CaptureBefore(const mcumax_position& position, mcumax_position_move a,
                       mcumax_position_move b) const;
    void PrepareCaptures(const mcumax_position& position);
    bool InCapturePrefix(mcumax_position_move move) const;

    mcumax_position_iterator m_iterator{};
    mcumax_position_move m_preferred = INVALID_MOVE;
    mcumax_position_move m_killers[2]{INVALID_MOVE, INVALID_MOVE};
    std::array<mcumax_position_move, 4> m_capturePrefix{};
    uint8_t m_stage = 0U;
    uint8_t m_captureCount = 0U;
    uint8_t m_captureIndex = 0U;
    enum class CaptureState : uint8_t { Unprepared, Complete, Overflow };
    CaptureState m_captureState = CaptureState::Unprepared;
};

} // namespace modules::classic_chess::internal
