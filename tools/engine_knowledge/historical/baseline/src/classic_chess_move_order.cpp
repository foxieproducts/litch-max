#include "classic_chess_move_order.hpp"

#include "modules/classic_chess_evaluation.hpp"

namespace modules::classic_chess::internal {
namespace {
bool SameMove(const mcumax_position_move a, const mcumax_position_move b) {
    return a.from == b.from && a.to == b.to && a.promotion == b.promotion;
}

bool GoodCapture(const mcumax_position& position, const mcumax_position_move move) {
    if (move.promotion != 0U) return true;
    const int victim = position.board[move.to] == 0U ? 100 : PieceValue(position.board[move.to] & 7U);
    return victim >= PieceValue(position.board[move.from] & 7U);
}

} // namespace

bool IsCapture(const mcumax_position& position, const mcumax_position_move move) {
    if ((move.from & 0x88U) != 0U || (move.to & 0x88U) != 0U) return false;
    const uint8_t type = position.board[move.from] & 7U;
    return position.board[move.to] != 0U ||
        ((type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM) &&
         move.to == position.en_passant && (move.to & 7U) != (move.from & 7U));
}

bool MayCreateQuietCheck(const mcumax_position& position, const mcumax_position_move move) {
    const uint8_t king = position.king_square[position.side == MCUMAX_BOARD_WHITE ? 1U : 0U];
    if ((move.from & 0x88U) != 0U || (move.to & 0x88U) != 0U ||
        (king & 0x88U) != 0U) return true;
    const auto distance = [](const int value) { return value < 0 ? -value : value; };
    const int fromRanks = distance(static_cast<int>(king >> 4U) - static_cast<int>(move.from >> 4U));
    const int fromFiles = distance(static_cast<int>(king & 7U) - static_cast<int>(move.from & 7U));
    // Leaving a king ray can reveal a different slider's attack.
    if (fromRanks == 0 || fromFiles == 0 || fromRanks == fromFiles) return true;
    const int ranks = static_cast<int>(king >> 4U) - static_cast<int>(move.to >> 4U);
    const int files = distance(static_cast<int>(king & 7U) - static_cast<int>(move.to & 7U));
    const int rankDistance = distance(ranks);
    const uint8_t type = position.board[move.from] & 7U;
    if (type == MCUMAX_KING) {
        // Castling also moves a rook, independently of the king's geometry.
        const int delta = static_cast<int>(move.to) - move.from;
        return delta == 2 || delta == -2 || (rankDistance <= 1 && files <= 1);
    }
    if (type == MCUMAX_KNIGHT)
        return (rankDistance == 1 && files == 2) || (rankDistance == 2 && files == 1);
    if (type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM)
        return ranks == (position.side == MCUMAX_BOARD_WHITE ? -1 : 1) && files == 1;
    if (type == MCUMAX_ROOK) return ranks == 0 || files == 0;
    if (type == MCUMAX_BISHOP) return rankDistance == files;
    if (type == MCUMAX_QUEEN) return ranks == 0 || files == 0 || rankDistance == files;
    return true;
}

void MoveOrder::Begin(const mcumax_position_move preferred) {
    m_stage = 0U;
    m_captureCount = 0U;
    m_captureIndex = 0U;
    m_captureState = CaptureState::Unprepared;
    m_preferred = preferred;
    mcumax_position_iterator_init(&m_iterator);
}

void MoveOrder::RememberKiller(const mcumax_position_move move) {
    if (SameMove(move, m_killers[0])) return;
    m_killers[1] = m_killers[0];
    m_killers[0] = move;
}

// Greatest victim/promotion gain first, then least attacker value.
bool MoveOrder::CaptureBefore(const mcumax_position& position, const mcumax_position_move a, const mcumax_position_move b) const {
    const auto victim = [&position](const mcumax_position_move move) {
        const int captured = position.board[move.to] == 0U ? (IsCapture(position, move) ? 100 : 0) :
            PieceValue(position.board[move.to] & 7U);
        return captured + (move.promotion != 0U ? PieceValue(move.promotion) - 100 : 0);
    };
    const int av = victim(a);
    const int bv = victim(b);
    if (av != bv) return av > bv;
    const auto attacker = [&position](const mcumax_position_move move) {
        const uint8_t piece = position.board[move.from] & 7U;
        // Kings have no exchange value; use the largest ordering rank.
        return piece == MCUMAX_KING ? 32767 : PieceValue(piece);
    };
    return attacker(a) < attacker(b);
}

void MoveOrder::PrepareCaptures(const mcumax_position& position) {
    m_captureState = CaptureState::Complete;
    m_captureCount = 0U;
    m_captureIndex = 0U;
    mcumax_position_move move;
    while (mcumax_position_iterator_next_tactical(&position, &m_iterator, &move)) {
        if (SameMove(move, m_preferred) ||
            (!IsCapture(position, move) && move.promotion == 0U) ||
            GoodCapture(position, move) != (m_stage == 1U)) continue;
        if (m_captureCount == m_capturePrefix.size()) m_captureState = CaptureState::Overflow;
        uint8_t index = 0U;
        while (index < m_captureCount && !CaptureBefore(position, move, m_capturePrefix[index])) ++index;
        if (index >= m_capturePrefix.size()) continue;
        if (m_captureCount < m_capturePrefix.size()) ++m_captureCount;
        for (uint8_t slot = m_captureCount - 1U; slot > index; --slot) {
            m_capturePrefix[slot] = m_capturePrefix[slot - 1U];
        }
        m_capturePrefix[index] = move;
    }
    mcumax_position_iterator_init(&m_iterator);
}

bool MoveOrder::InCapturePrefix(const mcumax_position_move move) const {
    for (uint8_t index = 0U; index < m_captureCount; ++index) {
        if (SameMove(move, m_capturePrefix[index])) return true;
    }
    return false;
}

// Streaming stages bound storage independently of the number of legal
// moves. Capture economics changes ordering only, never search coverage.
bool MoveOrder::Next(const mcumax_position& position, mcumax_position_move& move,
                     const MoveSelection selection) {
    while (m_stage < 6U) {
        if (selection == MoveSelection::Tactical && m_stage >= 2U && m_stage <= 4U) {
            m_stage = 5U;
            m_captureState = CaptureState::Unprepared;
            mcumax_position_iterator_init(&m_iterator);
        }
        if (m_stage == 0U) {
            ++m_stage;
            if (m_preferred.from != MCUMAX_SQUARE_INVALID &&
                (selection == MoveSelection::All || IsCapture(position, m_preferred) ||
                 m_preferred.promotion != 0U)) {
                move = m_preferred;
                return true;
            }
        }
        if (m_stage == 2U || m_stage == 3U) {
            const auto killer = m_killers[m_stage++ - 2U];
            const uint8_t type = killer.from == MCUMAX_SQUARE_INVALID ? 0U : position.board[killer.from] & 7U;
            const bool implicitPromotion =
                (type == MCUMAX_PAWN_UPSTREAM && (killer.to >> 4U) == 0U) ||
                (type == MCUMAX_PAWN_DOWNSTREAM && (killer.to >> 4U) == 7U);
            // A sibling's quiet killer can now be a pawn promotion. The
            // generator's four explicit promotions remain authoritative.
            if (killer.from != MCUMAX_SQUARE_INVALID && !implicitPromotion &&
                !SameMove(killer, m_preferred) && !IsCapture(position, killer)) {
                move = killer;
                return true;
            }
            continue;
        }
        if (m_stage == 1U || m_stage == 5U) {
            if (m_captureState == CaptureState::Unprepared) PrepareCaptures(position);
            if (m_captureIndex < m_captureCount) {
                move = m_capturePrefix[m_captureIndex++];
                return true;
            }
            // With no overflow, the prefix already contains this entire stage.
            // Only excess captures require another streaming generator pass.
            if (m_captureState == CaptureState::Complete) {
                ++m_stage;
                m_captureState = CaptureState::Unprepared;
                mcumax_position_iterator_init(&m_iterator);
                continue;
            }
        }
        const auto next = (m_stage == 1U || m_stage == 5U)
            ? mcumax_position_iterator_next_tactical : mcumax_position_iterator_next;
        while (next(&position, &m_iterator, &move)) {
            if (SameMove(move, m_preferred)) continue;
            const bool capture = IsCapture(position, move) || move.promotion != 0U;
            if ((m_stage == 1U || m_stage == 5U) && InCapturePrefix(move)) continue;
            if (m_stage == 1U && capture && GoodCapture(position, move)) return true;
            if (m_stage == 4U && !capture &&
                !SameMove(move, m_killers[0]) && !SameMove(move, m_killers[1])) return true;
            if (m_stage == 5U && capture && !GoodCapture(position, move)) return true;
        }
        ++m_stage;
        m_captureState = CaptureState::Unprepared;
        mcumax_position_iterator_init(&m_iterator);
    }
    return false;
}

} // namespace modules::classic_chess::internal
