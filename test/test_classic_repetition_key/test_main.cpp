#include <cstdio>
#include <cstring>
#include <initializer_list>

#include <mcu-max.h>
#include "litch_max/classic_chess_search.hpp"
#include "litch_max/classic_game.hpp"
#include "litch_max/fen_constants.hpp"
#include "litch_max/initial_fen.hpp"
#include "../../src/classic_chess_repetition.hpp"

namespace {
using litch_max::classic_chess::internal::ComputeRepetitionIdentity;
using litch_max::classic_chess::internal::AdvanceRepetitionIdentity;
int failures = 0;
unsigned positions = 0U, children = 0U;

void Check(bool condition, const char* label) {
    if (!condition) { ++failures; std::printf("[FAIL] %s\n", label); }
}

mcumax_position Position(const char* fen) {
    mcumax_init(); mcumax_set_fen_position(fen);
    mcumax_position position{};
    Check(mcumax_position_import(&position, &mcumax), "fixture imports through production rules");
    return position;
}

void CheckFenAdmission() {
    litch_max::ClassicGame game;
    game.Reset();
    Check(game.ApplyUciMove("e2e4"), "FEN rejection starts from a played game");
    mcumax.node_count = 41U;
    mcumax.best_score = 123;
    mcumax.completed_root_depth = 3U;
    mcumax_stop_search();
    const auto before = mcumax;
    const char* invalidFens[] = {nullptr, "", "4k3/8/8/8/8/8/8/3K3 w - - 0 1",
             "4k3/8/8/44/8/8/8/4K3 w - - 0 1",
             "4k3/8/8/8/8/8/8/4K3 x - - 0 1",
             "4k3/8/8/8/8/8/4K3/4K3 w - - 0 1",
             "4k3/8/8/8/8/8/8/4K2R w KK - 0 1",
             "4k3/8/8/8/8/8/8/4K3 w K - 0 1",
             "4k3/8/8/3p4/8/8/8/4K3 w - d3 0 9",
             "4k3/8/8/8/8/8/8/4K3 w - d6 0 9",
             "4k3/8/8/3p4/8/8/8/4K3 w - d6 17 9",
             "4k3/8/8/8/8/8/8/4K3 w - - -1 1",
             "4k3/8/8/8/8/8/8/4K3 w - - 4294967296 1",
             "4k3/8/8/8/8/8/8/4K3 w - - 0 nope",
             "4k3/8/8/8/8/8/8/4K3 w - - 0 0",
             "4k3/8/8/8/8/8/8/4K3 w - - 0 1 extra"};
    for (const char* invalid : invalidFens) {
        Check(!game.SetFen(invalid, 9876U), "malformed FEN rejects before live mutation");
        Check(std::memcmp(&before, &mcumax, sizeof(before)) == 0,
              "FEN rejection preserves the played board and all search state");
        uint64_t key = 123U;
        Check(!litch_max::classic_chess::TryPositionKey(invalid, key) && key == 123U &&
                  std::memcmp(&before, &mcumax, sizeof(before)) == 0,
              "pure FEN key shares rejection without changing output or game");
    }
    Check(game.ApplyUciMove("e7e5"), "played game continues after rejected FEN");

    struct Fixture { const char* fen; const char* projected; bool playable; };
    const Fixture fixtures[] = {
        {litch_max::INITIAL_FEN, litch_max::INITIAL_FEN, true},
        {"8/8/8/8/8/8/8/8 w - - 0 1", "8/8/8/8/8/8/8/8 w - - 0 1", false},
        {"4Q3/8/8/8/8/8/8/4K3 b - - 0 8", "4Q3/8/8/8/8/8/8/4K3 b - - 0 1", false},
        {"P3k3/8/8/8/8/8/8/4K3 w - - 0 1", "P3k3/8/8/8/8/8/8/4K3 w - - 0 1", false},
        {"r3k2r/8/8/8/8/8/8/R3K2R w qK - 12 34", "r3k2r/8/8/8/8/8/8/R3K2R w Kq - 0 1", true},
        {"4k3/8/8/3p4/8/8/8/4K3 w - d6 0 9", "4k3/8/8/3p4/8/8/8/4K3 w - d6 0 1", true},
        {"4k3/8/8/8/3P4/8/8/4K3 b - d3 0 9", "4k3/8/8/8/3P4/8/8/4K3 b - d3 0 1", true},
        {"7k/8/8/8/8/8/8/K7 b - - 4294967295 4294967295", "7k/8/8/8/8/8/8/K7 b - - 0 1", true},
    };
    for (const auto& fixture : fixtures) {
        Check(game.SetFen(fixture.fen), "supported full, partial or terminal FEN loads");
        char fen[litch_max::FEN_BUFFER_CAPACITY]{};
        Check(game.GetFen(fen, sizeof(fen)) && std::strcmp(fen, fixture.projected) == 0,
              "loaded placement, side, castling and EP survive; counters stay caller-owned");
        mcumax_position position{};
        Check(game.Import(position) == fixture.playable,
              "Classic import keeps stricter playability than FEN storage");
        const auto loaded = mcumax;
        uint64_t key = 123U;
        const bool keyed = litch_max::classic_chess::TryPositionKey(fixture.fen, key);
        Check(keyed == fixture.playable && (keyed
                  ? key == litch_max::classic_chess::PositionKey(position) : key == 123U),
              "pure Classic FEN key agrees with import and rules identity");
        Check(std::memcmp(&loaded, &mcumax, sizeof(loaded)) == 0,
              "pure FEN key preserves a successfully loaded game");
    }
}

// Frozen pre-optimization public/book identity. This deliberately does not use
// the new shared feature helpers or the incremental update implementation.
uint64_t OriginalKey(mcumax_position& position) {
    uint64_t key = 1469598103934665603ULL;
    for (uint8_t square = 0U; square < 128U; ++square) {
        if ((square & 0x88U) != 0U) continue;
        uint8_t piece = position.board[square];
        const uint8_t type = piece & 7U;
        const bool homePawn = (type == MCUMAX_PAWN_UPSTREAM && (square >> 4U) == 6U) ||
                              (type == MCUMAX_PAWN_DOWNSTREAM && (square >> 4U) == 1U);
        if (!homePawn) piece &= 31U;
        key = (key ^ piece) * 1099511628211ULL;
    }
    uint8_t rights = 0U;
    if (position.board[116] == (MCUMAX_KING | MCUMAX_BOARD_WHITE)) {
        if (position.board[119] == (MCUMAX_ROOK | MCUMAX_BOARD_WHITE)) rights |= 1U;
        if (position.board[112] == (MCUMAX_ROOK | MCUMAX_BOARD_WHITE)) rights |= 2U;
    }
    if (position.board[4] == (MCUMAX_KING | MCUMAX_BOARD_BLACK)) {
        if (position.board[7] == (MCUMAX_ROOK | MCUMAX_BOARD_BLACK)) rights |= 4U;
        if (position.board[0] == (MCUMAX_ROOK | MCUMAX_BOARD_BLACK)) rights |= 8U;
    }
    key = (key ^ rights) * 1099511628211ULL;
    key = (key ^ position.side) * 1099511628211ULL;
    uint8_t ep = MCUMAX_SQUARE_INVALID;
    if (position.en_passant != MCUMAX_SQUARE_INVALID) {
        const int behind = position.en_passant + (position.side == MCUMAX_BOARD_WHITE ? 16 : -16);
        for (const int offset : {-1, 1}) {
            const int from = behind + offset;
            if (from < 0 || from >= 128 || (from & 0x88) != 0) continue;
            const uint8_t type = position.board[from] & 7U;
            if (type != MCUMAX_PAWN_UPSTREAM && type != MCUMAX_PAWN_DOWNSTREAM) continue;
            mcumax_position_undo undo{};
            if (mcumax_position_make(&position, {static_cast<uint8_t>(from), position.en_passant, 0U}, &undo)) {
                mcumax_position_unmake(&position, &undo);
                ep = position.en_passant;
                break;
            }
        }
    }
    return (key ^ ep) * 1099511628211ULL;
}

void CheckIdentity(mcumax_position& position) {
    const auto before = position;
    Check(litch_max::classic_chess::PositionKey(position) == OriginalKey(position),
          "public/book FNV identity stays byte-for-byte compatible");
    (void)ComputeRepetitionIdentity(position);
    Check(std::memcmp(&before, &position, sizeof(position)) == 0,
          "all identity queries restore board, side, kings and EP");
}

uint32_t Random(uint32_t& seed) {
    seed ^= seed << 13U; seed ^= seed >> 17U; seed ^= seed << 5U;
    return seed;
}

bool CheckChildren(mcumax_position& position, uint32_t& seed) {
    ++positions;
    CheckIdentity(position);
    const auto before = position;
    const auto parent = ComputeRepetitionIdentity(position);
    mcumax_position_iterator iterator{}; mcumax_position_iterator_init(&iterator);
    mcumax_position_move move{}, chosen{};
    unsigned legal = 0U;
    while (mcumax_position_iterator_next(&position, &iterator, &move)) {
        mcumax_position_undo undo{};
        if (!mcumax_position_make(&position, move, &undo)) {
            Check(std::memcmp(&before, &position, sizeof(position)) == 0,
                  "rejected candidate leaves incremental parent authoritative");
            continue;
        }
        ++children;
        const auto incremental = AdvanceRepetitionIdentity(parent, position, undo);
        const auto complete = ComputeRepetitionIdentity(position);
        Check(incremental.key == complete.key && incremental.auxiliary == complete.auxiliary,
              "actual changed-square delta equals independent full-board recomputation");
        CheckIdentity(position);
        mcumax_position_unmake(&position, &undo);
        Check(std::memcmp(&before, &position, sizeof(position)) == 0 &&
                  ComputeRepetitionIdentity(position).key == parent.key,
              "every legal child unmake restores exact parent identity and state");
        if (Random(seed) % ++legal == 0U) chosen = move;
    }
    if (legal == 0U) return false;
    mcumax_position_undo undo{};
    Check(mcumax_position_make(&position, chosen, &undo), "selected legal walk move remains executable");
    return true;
}

void CheckEquivalenceClasses() {
    for (const char* fen : {litch_max::INITIAL_FEN,
             "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
             "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
             "4r1k1/8/8/3pP3/8/8/8/4K3 w - d6 0 1"}) {
        auto position = Position(fen);
        const auto identity = ComputeRepetitionIdentity(position);
        const auto publicKey = OriginalKey(position);
        for (uint8_t square = 0U; square < 128U; ++square) {
            if ((square & 0x88U) != 0U || position.board[square] == 0U) continue;
            auto changed = position;
            changed.board[square] ^= MCUMAX_PIECE_MOVED;
            Check((ComputeRepetitionIdentity(changed).key == identity.key) ==
                      (OriginalKey(changed) == publicKey),
                  "home pawn and actual castling rights preserve original moved-flag equivalence");
        }
        auto noEp = position; noEp.en_passant = MCUMAX_SQUARE_INVALID;
        Check((ComputeRepetitionIdentity(noEp).key == identity.key) ==
                  (OriginalKey(noEp) == publicKey),
              "legal versus pinned/unavailable EP preserves original identity equivalence");
        auto opposite = position; opposite.side ^= 24U; opposite.en_passant = MCUMAX_SQUARE_INVALID;
        Check(ComputeRepetitionIdentity(opposite).key != ComputeRepetitionIdentity(noEp).key,
              "side to move remains part of repetition identity");
    }
}

void CheckPath() {
    auto position = Position("4k1n1/8/8/8/8/8/8/4K1N1 w - - 0 1");
    litch_max::classic_chess::internal::RepetitionPath<32U> path;
    path.Reset(position);
    const mcumax_position_move cycle[] = {{118U, 85U, 0U}, {6U, 37U, 0U},
                                         {85U, 118U, 0U}, {37U, 6U, 0U}};
    for (uint8_t ply = 0U; ply < 8U; ++ply) {
        mcumax_position_undo undo{};
        Check(mcumax_position_make(&position, cycle[ply % 4U], &undo), "real reversible cycle applies");
        path.MadeChild(position, undo, ply);
        Check(path.Repeated(ply + 1U, 0U) == (ply == 7U),
              "path recognizes only the real third same-side occurrence");
    }
    Check(!path.Repeated(8U, 4U), "hypothetical subtree excludes earlier real prefix");
    path.Reset(position);
    Check(!path.Repeated(0U, 0U), "new request resets repetition authority");
}

void CheckNullPathAndSibling() {
    auto position = Position("4k1n1/8/8/8/8/8/8/4K1N1 w - - 0 1");
    const auto root = position;
    litch_max::classic_chess::internal::RepetitionPath<32U> path;
    uint64_t reference[32]{};
    reference[0U] = OriginalKey(position);
    path.Reset(position);
    mcumax_position_undo first{};
    Check(mcumax_position_make(&position, {118U, 85U, 0U}, &first), "real child precedes null search");
    path.MadeChild(position, first, 0U);
    reference[1U] = OriginalKey(position);
    const auto beforeNull = position;
    position.side ^= 24U;
    position.en_passant = MCUMAX_SQUARE_INVALID;
    path.MadeNullChild(position, 1U);
    reference[2U] = OriginalKey(position);
    const mcumax_position_move cycle[] = {{85U, 118U, 0U}, {6U, 37U, 0U},
                                         {118U, 85U, 0U}, {37U, 6U, 0U}};
    mcumax_position_undo undos[8]{};
    for (uint8_t ply = 2U; ply < 10U; ++ply) {
        Check(mcumax_position_make(&position, cycle[(ply - 2U) % 4U], &undos[ply - 2U]),
              "legal descendant follows the actual hypothetical pass");
        path.MadeChild(position, undos[ply - 2U], ply);
        reference[ply + 1U] = OriginalKey(position);
        unsigned matches = 0U;
        for (int previous = static_cast<int>(ply) - 1; previous >= 2; previous -= 2)
            if (reference[previous] == reference[ply + 1U]) ++matches;
        Check(path.Repeated(ply + 1U, 2U) == (matches >= 2U),
              "null subtree repetition agrees with frozen FNV path and excludes real prefix");
    }
    for (int index = 7; index >= 0; --index) mcumax_position_unmake(&position, &undos[index]);
    Check(OriginalKey(position) == reference[2U], "null descendants unwind to exact hypothetical parent");
    position.side = beforeNull.side;
    position.en_passant = beforeNull.en_passant;
    Check(std::memcmp(&position, &beforeNull, sizeof(position)) == 0,
          "null branch restores the complete real parent before its sibling");
    mcumax_position_undo sibling{};
    Check(mcumax_position_make(&position, {6U, 39U, 0U}, &sibling), "fresh real sibling is legal");
    path.MadeChild(position, sibling, 1U);
    Check(!path.Repeated(2U, 0U), "unwound null descendant keys cannot contaminate a real sibling");
    mcumax_position_unmake(&position, &sibling);
    mcumax_position_unmake(&position, &first);
    Check(std::memcmp(&position, &root, sizeof(root)) == 0, "hypothetical pass restores exact real root");
    path.Reset(position);
    Check(!path.Repeated(0U, 0U), "reused request cannot inherit a hypothetical path");
}
} // namespace

int main() {
    uint32_t seed = 20261027U;
    CheckFenAdmission();
    CheckEquivalenceClasses(); CheckPath(); CheckNullPathAndSibling();
    for (const char* fen : {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
             "4k3/P7/8/8/8/8/7p/4K3 w - - 0 1",
             "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
             "4r1k1/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
             "rnbqkb1r/p1ppp2p/5n2/5p1Q/4PP1p/2P5/P1PP2P1/1RB1KBNR b Kkq - 0 1"}) {
        auto position = Position(fen); (void)CheckChildren(position, seed);
    }
    for (unsigned game = 0U; game < 32U; ++game) {
        auto position = Position(litch_max::INITIAL_FEN);
        for (unsigned ply = 0U; ply < 96U && CheckChildren(position, seed); ++ply) {}
    }
    std::printf("[CLASSIC_REPETITION] failures=%d positions=%u children=%u path_bytes=%zu\n",
        failures, positions, children, sizeof(litch_max::classic_chess::internal::RepetitionPath<32U>));
    return failures == 0 ? 0 : 1;
}
