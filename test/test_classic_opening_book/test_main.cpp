#include <chrono>
#include <cstdio>
#include <cstring>

#include <mcu-max.h>

#include "litch_max/classic_chess_opening_book.hpp"
#include "litch_max/classic_chess_search.hpp"

namespace {
using litch_max::classic_chess::BookMove;
using litch_max::classic_chess::OpeningBookHistory;
using litch_max::classic_chess::OpeningBookMetadata;
using litch_max::classic_chess::TryOpeningBook;

struct BookRecord {
    uint64_t key;
    int16_t score;
    uint16_t move;
    uint16_t line;
    uint8_t depth;
    uint8_t ply;
};
static_assert(sizeof(BookRecord) == 16U, "Fixture observes the actual record layout");
#include "../../src/classic_chess_opening_book_data.inc"

int g_failures = 0;

void Check(const bool condition, const char* message) {
    if (!condition) {
        std::printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

mcumax_position InitialPosition() {
    mcumax_init();
    mcumax_position position{};
    Check(mcumax_position_import(&position, &mcumax), "real canonical engine snapshot imports");
    return position;
}

mcumax_position_move DecodeMove(const uint16_t packed) {
    const auto square = [](const uint16_t logical) {
        return static_cast<uint8_t>((logical / 8U) * 16U + logical % 8U);
    };
    return {square(packed & 63U), square((packed >> 6U) & 63U),
            static_cast<uint8_t>((packed >> 12U) & 7U)};
}

bool SameMove(const mcumax_position_move lhs, const mcumax_position_move rhs) {
    return lhs.from == rhs.from && lhs.to == rhs.to && lhs.promotion == rhs.promotion;
}

struct PlayedOpening {
    mcumax_position position = InitialPosition();
    mcumax_position_move moves[litch_max::classic_chess::OPENING_BOOK_MAX_PLIES]{};
    OpeningBookHistory history{moves, 0U, 1U, 0U, true};

    void Apply(const mcumax_position_move move) {
        if (history.moveCount >= litch_max::classic_chess::OPENING_BOOK_MAX_PLIES) {
            Check(false, "fixture stays within the initial-prefix bound");
            return;
        }
        const uint8_t type = position.board[move.from] & 7U;
        const bool reset = type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM ||
                           position.board[move.to] != 0U;
        mcumax_position_undo undo{};
        if (!mcumax_position_make(&position, move, &undo)) {
            Check(false, "every fixture prefix is legal through production rules");
            return;
        }
        moves[history.moveCount++] = move;
        history.halfmoveClock = reset ? 0U : history.halfmoveClock + 1U;
        history.fullmoveNumber = 1U + history.moveCount / 2U;
    }

    void Replay(const BookRecord& record) {
        for (uint8_t ply = 0U; ply < record.ply; ++ply)
            Apply(DecodeMove(BOOK_LINE_MOVES[BOOK_LINE_OFFSETS[record.line] + ply]));
    }
};

void CheckMiss(const mcumax_position& position, const OpeningBookHistory& history,
               const char* message) {
    const auto before = position;
    const auto singleton = mcumax;
    BookMove output;
    output.positionKey = 12345U;
    output.offlineScore = -42;
    output.offlineDepth = 19U;
    const auto previous = output;
    Check(!TryOpeningBook(position, history, output), message);
    Check(std::memcmp(&output, &previous, sizeof(output)) == 0, "miss leaves the output untouched");
    Check(std::memcmp(&position, &before, sizeof(position)) == 0, "miss preserves every caller byte");
    Check(std::memcmp(&mcumax, &singleton, sizeof(mcumax)) == 0, "miss never enters the singleton");
}

void CheckEveryStoredOpening() {
    size_t white = 0U, black = 0U, castled = 0U, epMetadata = 0U;
    for (size_t index = 0U; index < BOOK_ENTRY_COUNT; ++index) {
        const auto& record = BOOK_RECORDS[index];
        Check(index == 0U || BOOK_RECORDS[index - 1U].key <= record.key,
              "generated records are ordered for binary lookup");
        Check(record.line < BOOK_LINE_COUNT && record.depth >= 5U && record.ply <= 16U,
              "each record carries bounded reachability and genuine offline depth");
        PlayedOpening played;
        played.Replay(record);
        const auto before = played.position;
        const auto singleton = mcumax;
        BookMove output;
        Check(TryOpeningBook(played.position, played.history, output), "every stored opening is reachable");
        Check(output.positionKey == record.key && output.offlineScore == record.score &&
                  output.offlineDepth == record.depth && output.openingPly == record.ply &&
                  SameMove(output.move, DecodeMove(record.move)),
              "lookup returns the validated move with its own offline provenance");
        mcumax_position_undo undo{};
        Check(mcumax_position_make(&played.position, output.move, &undo),
              "every returned move is actually executable from the current opening");
        mcumax_position_unmake(&played.position, &undo);
        Check(std::memcmp(&played.position, &before, sizeof(before)) == 0,
              "lookup and subsequent legality probe restore all position bytes");
        Check(std::memcmp(&mcumax, &singleton, sizeof(mcumax)) == 0,
              "book lookup never reads or mutates the global engine");
        // GPC reloads its authoritative FEN while retaining the actual prefix.
        // FEN reconstruction marks harmless minor-piece moved bits differently.
        for (uint8_t square = 0U; square < 128U; ++square)
            mcumax.board[square] = before.board[square];
        mcumax.current_side = before.side;
        mcumax.en_passant_square = before.en_passant;
        char fen[128]{};
        mcumax_get_fen(fen, sizeof(fen));
        mcumax_set_fen_position(fen);
        mcumax_position reloaded{};
        Check(mcumax_position_import(&reloaded, &mcumax), "real FEN projection reimports every opening");
        const auto reloadedBefore = reloaded;
        const auto reloadedSingleton = mcumax;
        BookMove afterReload;
        Check(TryOpeningBook(reloaded, played.history, afterReload) &&
                  SameMove(afterReload.move, output.move) && afterReload.offlineScore == output.offlineScore,
              "trusted prefix survives production FEN normalization without changing the Book result");
        Check(std::memcmp(&reloaded, &reloadedBefore, sizeof(reloaded)) == 0 &&
                  std::memcmp(&mcumax, &reloadedSingleton, sizeof(mcumax)) == 0,
              "book lookup after a real FEN reload remains pure");
        if (before.side == MCUMAX_BOARD_WHITE) ++white;
        else ++black;
        if (before.king_square[0] != 116U || before.king_square[1] != 4U) ++castled;
        if (before.en_passant != MCUMAX_SQUARE_INVALID) ++epMetadata;
    }
    Check(white != 0U && black != 0U && castled != 0U && epMetadata != 0U,
          "coverage includes both sides, moved kings and actual en-passant metadata");
    std::printf("[BOOK] verified_entries=%zu white=%zu black=%zu moved_king=%zu ep_metadata=%zu\n",
                BOOK_ENTRY_COUNT, white, black, castled, epMetadata);
}

void CheckHistoryAndExactStateGuards() {
    PlayedOpening played;
    BookMove output;
    Check(TryOpeningBook(played.position, played.history, output), "acknowledged canonical start is covered");
    auto history = played.history;
    history.fromCanonicalStart = false;
    CheckMiss(played.position, history, "imported initial FEN cannot impersonate a played prefix");
    history = played.history;
    history.halfmoveClock = 99U;
    CheckMiss(played.position, history, "same key with an inconsistent halfmove clock misses");
    history = played.history;
    history.fullmoveNumber = 9U;
    CheckMiss(played.position, history, "same key with a late fullmove number misses");
    history = played.history;
    history.moveCount = 17U;
    history.initialMoves = nullptr;
    CheckMiss(played.position, history, "out-of-bound prefix is rejected before reading its pointer");
    history.moveCount = 1U;
    CheckMiss(played.position, history, "a missing nonempty prefix fails closed");

    auto malformed = played.position;
    malformed.king_square[0] = 115U;
    Check(litch_max::classic_chess::PositionKey(malformed) == output.positionKey,
          "damaged cached king is invisible to the identity hash");
    CheckMiss(malformed, played.history, "exact matching catches damaged king metadata despite the same key");
    malformed = played.position;
    malformed.board[8] = MCUMAX_QUEEN | MCUMAX_BOARD_WHITE;
    Check(litch_max::classic_chess::PositionKey(malformed) == output.positionKey,
          "off-board bytes are invisible to the identity hash");
    CheckMiss(malformed, played.history, "exact matching rejects nonzero off-board bytes despite the same key");
    malformed = played.position;
    malformed.board[68] = MCUMAX_PIECE_MOVED;
    CheckMiss(malformed, played.history, "moved-bit empty pieces are malformed rather than normalized away");
    malformed = played.position;
    malformed.board[100] |= MCUMAX_PIECE_MOVED;
    CheckMiss(malformed, played.history, "home pawn double-step eligibility is part of the exact state");
    malformed = played.position;
    malformed.board[116] |= MCUMAX_PIECE_MOVED;
    CheckMiss(malformed, played.history, "changed castling rights miss despite unchanged placement");
    malformed = played.position;
    malformed.side = MCUMAX_BOARD_BLACK;
    CheckMiss(malformed, played.history, "wrong side cannot match the prefix");

    played.Apply({100U, 68U, 0U}); // e2e4
    Check(TryOpeningBook(played.position, played.history, output), "ordinary first pawn move is covered");
    malformed = played.position;
    malformed.en_passant = MCUMAX_SQUARE_INVALID;
    Check(litch_max::classic_chess::PositionKey(malformed) == output.positionKey,
          "ineffective EP metadata shares the canonical repetition key");
    CheckMiss(malformed, played.history, "strict current-prefix EP matching rejects an altered state");
    history = played.history;
    history.moveCount = 0U;
    history.fullmoveNumber = 1U;
    CheckMiss(played.position, history, "truncated prefix cannot establish the current board");
    mcumax_position_move illegal[]{{100U, 52U, 0U}}; // e2e5
    history = {illegal, 0U, 1U, 1U, true};
    CheckMiss(played.position, history, "illegal opening history cannot reach a record");

    PlayedOpening absent;
    absent.Apply({96U, 80U, 0U}); // a2a3, deliberately outside this corpus
    CheckMiss(absent.position, absent.history, "legal uncovered opening returns no invented fallback");
    PlayedOpening repeated;
    repeated.Apply({118U, 85U, 0U}); // Nf3
    repeated.Apply({6U, 37U, 0U}); // Nf6
    repeated.Apply({85U, 118U, 0U}); // Ng1
    repeated.Apply({37U, 6U, 0U}); // Ng8
    CheckMiss(repeated.position, repeated.history, "repeated early position never consumes history-free knowledge");
}

void MeasureLookup() {
    const auto& metadata = litch_max::classic_chess::GetOpeningBookMetadata();
    Check(metadata.entryCount == BOOK_ENTRY_COUNT && metadata.sourceLineCount == BOOK_LINE_COUNT &&
              metadata.storageBytes < 10240U && std::strlen(metadata.seedSha256) == 64U &&
              std::strlen(metadata.baselineInputsSha256) == 64U &&
              std::strlen(metadata.recordsSha256) == 64U,
          "metadata identifies real compact data and reproducible provenance");
    PlayedOpening played;
    const BookRecord* deepest = &BOOK_RECORDS[0];
    for (const auto& record : BOOK_RECORDS)
        if (record.ply > deepest->ply) deepest = &record;
    played.Replay(*deepest);
    constexpr unsigned SAMPLES = 10000U;
    const auto start = std::chrono::steady_clock::now();
    unsigned hits = 0U;
    BookMove result;
    for (unsigned sample = 0U; sample < SAMPLES; ++sample)
        if (TryOpeningBook(played.position, played.history, result)) ++hits;
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    Check(hits == SAMPLES, "repeated read-only lookup remains deterministic");
    std::printf("[BOOK] entry_bytes=%zu storage_bytes=%zu lines=%zu measured_ply=%u "
                "native_lookup_mean_ns=%lld samples=%u (host measurement)\n",
                sizeof(BookRecord), metadata.storageBytes, metadata.sourceLineCount,
                deepest->ply, static_cast<long long>(elapsed / SAMPLES), SAMPLES);
}
} // namespace

int main() {
    CheckEveryStoredOpening();
    CheckHistoryAndExactStateGuards();
    MeasureLookup();
    if (g_failures != 0) {
        std::printf("[FAIL] classic_opening_book failures=%d\n", g_failures);
        return 1;
    }
    std::printf("[PASS] classic_opening_book\n");
    return 0;
}
