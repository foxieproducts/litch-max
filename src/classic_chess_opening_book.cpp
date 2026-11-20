#include "litch_max/classic_chess_opening_book.hpp"

#include "litch_max/classic_chess_search.hpp"

namespace litch_max::classic_chess {
namespace {

struct BookRecord {
    uint64_t key;
    int16_t score;
    uint16_t move;
    uint16_t line;
    uint8_t depth;
    uint8_t ply;
};
static_assert(sizeof(BookRecord) == 16U, "Opening book records must remain compact");

#include "classic_chess_opening_book_data.inc"

mcumax_position InitialPosition() {
    mcumax_position position{};
    constexpr uint8_t BACK_RANK[]{MCUMAX_ROOK, MCUMAX_KNIGHT, MCUMAX_BISHOP,
        MCUMAX_QUEEN, MCUMAX_KING, MCUMAX_BISHOP, MCUMAX_KNIGHT, MCUMAX_ROOK};
    for (uint8_t file = 0U; file < 8U; ++file) {
        position.board[file] = BACK_RANK[file] | MCUMAX_BOARD_BLACK;
        position.board[16U + file] = MCUMAX_PAWN_DOWNSTREAM | MCUMAX_BOARD_BLACK;
        position.board[96U + file] = MCUMAX_PAWN_UPSTREAM | MCUMAX_BOARD_WHITE;
        position.board[112U + file] = BACK_RANK[file] | MCUMAX_BOARD_WHITE;
    }
    position.side = MCUMAX_BOARD_WHITE;
    position.en_passant = MCUMAX_SQUARE_INVALID;
    position.king_square[0] = 116U;
    position.king_square[1] = 4U;
    return position;
}

mcumax_position_move DecodeMove(const uint16_t packed) {
    const auto square = [](const uint16_t logical) {
        return static_cast<uint8_t>((logical / 8U) * 16U + logical % 8U);
    };
    return {square(packed & 63U), square((packed >> 6U) & 63U),
            static_cast<uint8_t>((packed >> 12U) & 7U)};
}

uint8_t CastleRights(const mcumax_position& position) {
    uint8_t rights = 0U;
    if (position.board[116] == (MCUMAX_KING | MCUMAX_BOARD_WHITE)) {
        if (position.board[119] == (MCUMAX_ROOK | MCUMAX_BOARD_WHITE)) rights |= 1U;
        if (position.board[112] == (MCUMAX_ROOK | MCUMAX_BOARD_WHITE)) rights |= 2U;
    }
    if (position.board[4] == (MCUMAX_KING | MCUMAX_BOARD_BLACK)) {
        if (position.board[7] == (MCUMAX_ROOK | MCUMAX_BOARD_BLACK)) rights |= 4U;
        if (position.board[0] == (MCUMAX_ROOK | MCUMAX_BOARD_BLACK)) rights |= 8U;
    }
    return rights;
}

bool SameRulesState(const mcumax_position& actual, const mcumax_position& replay) {
    if (actual.side != replay.side || actual.en_passant != replay.en_passant ||
        actual.king_square[0] != replay.king_square[0] ||
        actual.king_square[1] != replay.king_square[1] ||
        CastleRights(actual) != CastleRights(replay)) return false;
    for (uint8_t square = 0U; square < 128U; ++square) {
        const uint8_t piece = actual.board[square];
        if ((square & 0x88U) != 0U) {
            if (piece != 0U) return false;
            continue;
        }
        if ((piece & 0xc0U) != 0U) return false;
        const uint8_t type = piece & 7U;
        if (type == 0U && piece != 0U) return false;
        const bool homePawn = (type == MCUMAX_PAWN_UPSTREAM && (square >> 4U) == 6U) ||
                              (type == MCUMAX_PAWN_DOWNSTREAM && (square >> 4U) == 1U);
        const uint8_t mask = homePawn ? 63U : 31U;
        if ((piece & mask) != (replay.board[square] & mask)) return false;
    }
    return true;
}

bool ReplayHistory(const OpeningBookHistory& history, mcumax_position& replay) {
    if (!history.fromCanonicalStart || history.moveCount > OPENING_BOOK_MAX_PLIES ||
        (history.moveCount != 0U && history.initialMoves == nullptr) ||
        history.fullmoveNumber != 1U + history.moveCount / 2U) return false;
    replay = InitialPosition();
    uint64_t seen[OPENING_BOOK_MAX_PLIES + 1U]{};
    seen[0] = PositionKey(replay);
    uint16_t halfmoveClock = 0U;
    for (uint8_t ply = 0U; ply < history.moveCount; ++ply) {
        const auto move = history.initialMoves[ply];
        if ((move.from & 0x88U) != 0U || (move.to & 0x88U) != 0U) return false;
        const uint8_t type = replay.board[move.from] & 7U;
        const bool resetClock = type == MCUMAX_PAWN_UPSTREAM ||
            type == MCUMAX_PAWN_DOWNSTREAM || replay.board[move.to] != 0U;
        mcumax_position_undo undo{};
        if (!mcumax_position_make(&replay, move, &undo)) return false;
        halfmoveClock = resetClock ? 0U : halfmoveClock + 1U;
        const uint64_t key = PositionKey(replay);
        // Opening-only knowledge never makes a claim about repetition history.
        for (uint8_t previous = 0U; previous <= ply; ++previous)
            if (seen[previous] == key) return false;
        seen[ply + 1U] = key;
    }
    return history.halfmoveClock == halfmoveClock;
}

bool ReplayRecord(const BookRecord& record, mcumax_position& replay) {
    if (record.line >= BOOK_LINE_COUNT || record.ply > OPENING_BOOK_MAX_PLIES ||
        record.ply > BOOK_LINE_OFFSETS[record.line + 1U] - BOOK_LINE_OFFSETS[record.line]) return false;
    replay = InitialPosition();
    for (uint8_t ply = 0U; ply < record.ply; ++ply) {
        mcumax_position_undo undo{};
        const uint16_t packed = BOOK_LINE_MOVES[BOOK_LINE_OFFSETS[record.line] + ply];
        if ((packed & 0x8000U) != 0U ||
            !mcumax_position_make(&replay, DecodeMove(packed), &undo)) return false;
    }
    return true;
}

} // namespace

bool TryOpeningBook(const mcumax_position& position,
                    const OpeningBookHistory& history, BookMove& outMove) {
    mcumax_position replay{};
    if (!ReplayHistory(history, replay) || !SameRulesState(position, replay)) return false;
    const uint64_t key = PositionKey(replay);
    size_t lower = 0U;
    size_t upper = BOOK_ENTRY_COUNT;
    while (lower < upper) {
        const size_t middle = lower + (upper - lower) / 2U;
        if (BOOK_RECORDS[middle].key < key) lower = middle + 1U;
        else upper = middle;
    }
    for (size_t index = lower; index < BOOK_ENTRY_COUNT && BOOK_RECORDS[index].key == key; ++index) {
        const auto& record = BOOK_RECORDS[index];
        if (record.ply != history.moveCount || record.depth < OPENING_BOOK_MIN_OFFLINE_DEPTH ||
            (record.move & 0x8000U) != 0U || !ReplayRecord(record, replay) ||
            !SameRulesState(position, replay)) continue;
        const auto move = DecodeMove(record.move);
        mcumax_position_undo undo{};
        if (!mcumax_position_make(&replay, move, &undo)) continue;
        mcumax_position_unmake(&replay, &undo);
        // Only the local replay is probed; callers and the engine lane stay pure.
        BookMove result;
        result.move = move;
        result.positionKey = key;
        result.offlineScore = record.score;
        result.sourceLine = record.line;
        result.offlineDepth = record.depth;
        result.openingPly = record.ply;
        outMove = result;
        return true;
    }
    return false;
}

const OpeningBookMetadata& GetOpeningBookMetadata() { return BOOK_METADATA; }

} // namespace litch_max::classic_chess
