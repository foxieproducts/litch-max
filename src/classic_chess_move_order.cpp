#include "classic_chess_move_order.hpp"

#include <initializer_list>

#include "litch_max/classic_chess_evaluation.hpp"

namespace litch_max::classic_chess::internal {
namespace {
bool SameMove(const mcumax_position_move a, const mcumax_position_move b) {
    return a.from == b.from && a.to == b.to && a.promotion == b.promotion;
}

bool GoodCapture(const mcumax_position& position, const mcumax_position_move move) {
    if (move.promotion != 0U) return true;
    const int victim = position.board[move.to] == 0U ? 100 : PieceValue(position.board[move.to] & 7U);
    return victim >= PieceValue(position.board[move.from] & 7U);
}

bool PreviousMoveCaptured(const mcumax_position& current,
                          const mcumax_position_undo& undo,
                          const mcumax_position_move move) {
    constexpr uint8_t COLORS = MCUMAX_BOARD_WHITE | MCUMAX_BOARD_BLACK;
    if ((current.side != MCUMAX_BOARD_WHITE && current.side != MCUMAX_BOARD_BLACK) ||
        undo.side != (current.side ^ COLORS) || undo.count < 2U || undo.count > 4U ||
        (move.from & 0x88U) != 0U || (move.to & 0x88U) != 0U || move.from == move.to ||
        current.board[move.from] != 0U) return false;
    const uint8_t target = current.board[move.to];
    if ((target & COLORS) != undo.side || (target & 7U) == 0U ||
        (target & 7U) == MCUMAX_KING) return false;
    uint8_t source = 0U, victim = 0U, epVictim = 0U;
    bool hasSource = false, hasTarget = false;
    for (uint8_t index = 0U; index < undo.count; ++index) {
        if (undo.square[index] == move.from) { source = undo.piece[index]; hasSource = true; }
        if (undo.square[index] == move.to) { victim = undo.piece[index]; hasTarget = true; }
        if (undo.square[index] == (move.to ^ 16U)) epVictim = undo.piece[index];
    }
    if (!hasSource || !hasTarget || (source & COLORS) != undo.side) return false;
    const uint8_t type = source & 7U;
    const bool pawn = type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM;
    const bool promoted = pawn && (move.to & 0x70U) == (undo.side == MCUMAX_BOARD_WHITE ? 0U : 0x70U);
    const uint8_t expected = promoted ? undo.side | MCUMAX_PIECE_MOVED |
        (move.promotion != 0U ? move.promotion : static_cast<uint8_t>(MCUMAX_QUEEN)) : source | MCUMAX_PIECE_MOVED;
    if (target != expected || (!promoted && move.promotion != 0U)) return false;
    if ((victim & COLORS) == current.side && (victim & 7U) != 0U &&
        (victim & 7U) != MCUMAX_KING) return true;
    const int step = undo.side == MCUMAX_BOARD_WHITE ? -16 : 16;
    const int delta = static_cast<int>(move.to) - move.from;
    return pawn && victim == 0U && undo.en_passant == move.to &&
        (delta == step - 1 || delta == step + 1) &&
        (epVictim & COLORS) == current.side &&
        (epVictim & 7U) == (current.side == MCUMAX_BOARD_WHITE ? MCUMAX_PAWN_UPSTREAM : MCUMAX_PAWN_DOWNSTREAM) &&
        current.board[move.to ^ 16U] == 0U;
}

} // namespace

mcumax_position_move RecapturePreference(mcumax_position& current,
                                         const mcumax_position_undo& previousUndo,
                                         const mcumax_position_move previousMove) {
    if (!PreviousMoveCaptured(current, previousUndo, previousMove)) return INVALID_MOVE;
    const int target = previousMove.to;
    const auto legalFrom = [&current, target](const int from, const uint8_t type) {
        if (from < 0 || from >= 128 || (from & 0x88) != 0 ||
            (current.board[from] & 24U) != current.side ||
            (current.board[from] & 7U) != type) return INVALID_MOVE;
        const bool promotion = type < 3U &&
            (target & 0x70) == (current.side == MCUMAX_BOARD_WHITE ? 0 : 0x70);
        const mcumax_position_move move{static_cast<uint8_t>(from), static_cast<uint8_t>(target),
            static_cast<uint8_t>(promotion ? static_cast<uint8_t>(MCUMAX_QUEEN) : 0U)};
        mcumax_position_undo undo{};
        if (!mcumax_position_make(&current, move, &undo)) return INVALID_MOVE;
        mcumax_position_unmake(&current, &undo);
        return move;
    };
    // Current material values rank Pawn, Knight, Bishop, Rook, Queen; kings
    // are considered last, independently of their zero exchange value.
    const int pawnStep = current.side == MCUMAX_BOARD_WHITE ? -16 : 16;
    const uint8_t pawn = current.side == MCUMAX_BOARD_WHITE ? MCUMAX_PAWN_UPSTREAM : MCUMAX_PAWN_DOWNSTREAM;
    for (const int file : {-1, 1}) {
        const auto move = legalFrom(target - pawnStep + file, pawn);
        if (move.from != MCUMAX_SQUARE_INVALID) return move;
    }
    static constexpr int8_t KNIGHT_STEPS[8] = {14, 18, 31, 33, -14, -18, -31, -33};
    for (const int step : KNIGHT_STEPS) {
        const auto move = legalFrom(target + step, MCUMAX_KNIGHT);
        if (move.from != MCUMAX_SQUARE_INVALID) return move;
    }
    static constexpr int8_t RAY_STEPS[8] = {1, -1, 16, -16, 15, -15, 17, -17};
    for (const uint8_t type : {MCUMAX_BISHOP, MCUMAX_ROOK, MCUMAX_QUEEN}) {
        const uint8_t begin = type == MCUMAX_BISHOP ? 4U : 0U;
        const uint8_t end = type == MCUMAX_ROOK ? 4U : 8U;
        for (uint8_t direction = begin; direction < end; ++direction) {
            int from = target + RAY_STEPS[direction];
            while (from >= 0 && from < 128 && (from & 0x88) == 0 && current.board[from] == 0U)
                from += RAY_STEPS[direction];
            const auto move = legalFrom(from, type);
            if (move.from != MCUMAX_SQUARE_INVALID) return move;
        }
    }
    for (const int step : RAY_STEPS) {
        const auto move = legalFrom(target + step, MCUMAX_KING);
        if (move.from != MCUMAX_SQUARE_INVALID) return move;
    }
    return INVALID_MOVE;
}

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

bool EscapesCheaperCapture(const mcumax_position& child, const mcumax_position_move move,
                          const mcumax_position_undo& undo) {
    if ((move.to & 0x88U) != 0U || (child.board[move.to] & 7U) != MCUMAX_QUEEN)
        return false;
    // Stateless local probes preserve the live position, history, and move order.
    auto probe = child;
    const auto capturable = [&probe](const uint8_t queen) {
        for (uint8_t from = 0U; from < 128U; ++from) {
            if ((from & 0x88U) != 0U) { from += 7U; continue; }
            const uint8_t piece = probe.board[from], type = piece & 7U;
            if ((piece & (MCUMAX_BOARD_WHITE | MCUMAX_BOARD_BLACK)) != probe.side ||
                type == 0U || type == MCUMAX_KING || type == MCUMAX_QUEEN) continue;
            mcumax_position_undo captureUndo{};
            if (!mcumax_position_make(&probe, {from, queen, 0U}, &captureUndo)) continue;
            mcumax_position_unmake(&probe, &captureUndo);
            return true;
        }
        return false;
    };
    mcumax_position_unmake(&probe, &undo);
    probe.side = child.side;
    probe.en_passant = MCUMAX_SQUARE_INVALID;
    if (!capturable(move.from)) return false;
    probe = child;
    return !capturable(move.to);
}

void MoveOrder::Begin(const mcumax_position_move preferred) {
    m_stage = 0U;
    m_captureCount = 0U;
    m_captureIndex = 0U;
    m_captureState = CaptureState::Unprepared;
    m_hasBadCaptures = false;
    m_lastMoveGenerated = false;
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
    // The first pass already establishes whether the losing-capture stage is empty.
    if (m_stage == 5U && !m_hasBadCaptures) return;
    mcumax_position_move move;
    while (mcumax_position_iterator_next_tactical(&position, &m_iterator, &move)) {
        if (SameMove(move, m_preferred) ||
            (!IsCapture(position, move) && move.promotion == 0U)) continue;
        const bool good = GoodCapture(position, move);
        if (!good) m_hasBadCaptures = true;
        if (good != (m_stage == 1U)) continue;
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
    m_lastMoveGenerated = false;
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
                m_lastMoveGenerated = true;
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
            if ((m_stage == 1U && capture && GoodCapture(position, move)) ||
                (m_stage == 4U && !capture &&
                 !SameMove(move, m_killers[0]) && !SameMove(move, m_killers[1])) ||
                (m_stage == 5U && capture && !GoodCapture(position, move))) {
                m_lastMoveGenerated = true;
                return true;
            }
        }
        ++m_stage;
        m_captureState = CaptureState::Unprepared;
        mcumax_position_iterator_init(&m_iterator);
    }
    return false;
}

} // namespace litch_max::classic_chess::internal
