#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include <mcu-max-position.h>

#include "litch_max/classic_chess_kpk.hpp"

namespace {
using litch_max::classic_chess::KpkOutcome;
using litch_max::classic_chess::TryProbeKpk;
constexpr uint32_t KPK_STATES = 196608U;
constexpr uint32_t DRAW_CHILD = UINT32_MAX;
constexpr uint32_t WIN_CHILD = UINT32_MAX - 1U;
constexpr uint16_t UNKNOWN_DISTANCE = UINT16_MAX;
int g_failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) { std::printf("[FAIL] %s\n", message); ++g_failures; }
}

uint8_t Square88(unsigned square) {
    return (7U - square / 8U) * 16U + square % 8U;
}

unsigned Square64(uint8_t square) {
    return (7U - square / 16U) * 8U + square % 16U;
}

mcumax_position Position(unsigned ownerKing, unsigned defenderKing, unsigned pawn,
                         bool defenderTurn, unsigned transform = 0U) {
    const bool black = (transform & 2U) != 0U;
    const unsigned mirror = ((transform & 1U) != 0U ? 7U : 0U) ^ (black ? 56U : 0U);
    mcumax_position position{};
    position.side = defenderTurn != black ? MCUMAX_BOARD_BLACK : MCUMAX_BOARD_WHITE;
    position.en_passant = MCUMAX_SQUARE_INVALID;
    const uint8_t owner = black ? MCUMAX_BOARD_BLACK : MCUMAX_BOARD_WHITE;
    const uint8_t defender = owner ^ 24U;
    const uint8_t ownerSquare = Square88(ownerKing ^ mirror);
    const uint8_t defenderSquare = Square88(defenderKing ^ mirror);
    const uint8_t pawnSquare = Square88(pawn ^ mirror);
    position.board[ownerSquare] = owner | MCUMAX_KING | MCUMAX_PIECE_MOVED;
    position.board[defenderSquare] = defender | MCUMAX_KING | MCUMAX_PIECE_MOVED;
    position.board[pawnSquare] = owner | (black ? MCUMAX_PAWN_DOWNSTREAM : MCUMAX_PAWN_UPSTREAM);
    if (pawn / 8U != 1U) position.board[pawnSquare] |= MCUMAX_PIECE_MOVED;
    position.king_square[black ? 1U : 0U] = ownerSquare;
    position.king_square[black ? 0U : 1U] = defenderSquare;
    return position;
}

mcumax_position Decode(uint32_t index) {
    const bool defenderTurn = (index & 1U) != 0U;
    index /= 2U;
    const unsigned defender = index % 64U;
    index /= 64U;
    const unsigned owner = index % 64U;
    index /= 64U;
    const unsigned pawn = (index / 4U + 1U) * 8U + index % 4U;
    return Position(owner, defender, pawn, defenderTurn);
}

unsigned PawnSquare(const mcumax_position& position) {
    for (unsigned square = 0; square < 128U; ++square)
        if ((square & 0x88U) == 0U && (position.board[square] & 7U) == MCUMAX_PAWN_UPSTREAM)
            return square;
    return MCUMAX_SQUARE_INVALID;
}

uint32_t Encode(const mcumax_position& position) {
    const unsigned pawn = Square64(PawnSquare(position));
    return (((((pawn / 8U - 1U) * 4U + pawn % 8U) * 64U +
              Square64(position.king_square[0])) * 64U + Square64(position.king_square[1])) * 2U) +
           (position.side == MCUMAX_BOARD_BLACK);
}

bool CanonicalLegal(uint32_t index) {
    uint32_t decoded = index / 2U;
    const unsigned defender = decoded % 64U;
    decoded /= 64U;
    const unsigned owner = decoded % 64U;
    decoded /= 64U;
    const unsigned pawn = (decoded / 4U + 1U) * 8U + decoded % 4U;
    if (owner == defender || owner == pawn || defender == pawn) return false;
    const auto position = Decode(index);
    // The side which just moved cannot remain in check. This also rejects
    // adjacent kings using the real attack rules, independently of the generator.
    return !mcumax_position_is_check(&position, MCUMAX_BOARD_WHITE) &&
           ((index & 1U) != 0U || !mcumax_position_is_check(&position, MCUMAX_BOARD_BLACK));
}

bool MajorConversionWins(mcumax_position& position, uint8_t majorSquare) {
    const auto type = position.board[majorSquare] & 7U;
    if (type != MCUMAX_QUEEN && type != MCUMAX_ROOK) return false;
    mcumax_position_iterator iterator{};
    mcumax_position_iterator_init(&iterator);
    mcumax_position_move move{};
    bool any = false;
    while (mcumax_position_iterator_next_legal(&position, &iterator, &move)) {
        any = true;
        if (move.to == majorSquare) return false;
    }
    return any || mcumax_position_is_check(&position, position.side);
}

struct Edge {
    uint32_t child;
    bool zeroes;
};

struct Graph {
    std::vector<uint8_t> legal;
    std::vector<uint8_t> wins;
    std::vector<uint8_t> mate;
    std::vector<uint32_t> offsets;
    std::vector<Edge> edges;
};

Graph ProductionGraph() {
    Graph graph{std::vector<uint8_t>(KPK_STATES, 0U), std::vector<uint8_t>(KPK_STATES, 0U),
                std::vector<uint8_t>(KPK_STATES, 0U), std::vector<uint32_t>(KPK_STATES + 1U, 0U), {}};
    graph.edges.reserve(1200000U);
    for (uint32_t index = 0; index < KPK_STATES; ++index) {
        graph.offsets[index] = graph.edges.size();
        if (!CanonicalLegal(index)) continue;
        graph.legal[index] = 1U;
        auto position = Decode(index);
        const auto original = position;
        const unsigned pawn = PawnSquare(position);
        mcumax_position_iterator iterator{};
        mcumax_position_iterator_init(&iterator);
        mcumax_position_move move{};
        while (mcumax_position_iterator_next(&position, &iterator, &move)) {
            mcumax_position_undo undo{};
            if (!mcumax_position_make(&position, move, &undo)) continue;
            uint32_t child;
            if (move.to == pawn && move.from != pawn) child = DRAW_CHILD;
            else if (move.promotion != 0U)
                child = MajorConversionWins(position, move.to) ? WIN_CHILD : DRAW_CHILD;
            else child = Encode(position);
            graph.edges.push_back({child, move.from == pawn});
            mcumax_position_unmake(&position, &undo);
            Check(std::memcmp(&position, &original, sizeof(position)) == 0,
                  "production make/unmake restores the exhaustive position");
        }
        if (graph.edges.size() == graph.offsets[index] &&
            mcumax_position_is_check(&position, position.side)) graph.mate[index] = 1U;
    }
    graph.offsets[KPK_STATES] = graph.edges.size();
    return graph;
}

bool ChildWin(const Graph& graph, const Edge& edge) {
    return edge.child == WIN_CHILD || (edge.child < KPK_STATES && graph.wins[edge.child] != 0U);
}

void CheckProductionAttractor() {
    auto graph = ProductionGraph();
    unsigned changes;
    unsigned rounds = 0U;
    // Forward fixed-point iteration is deliberately different from the offline
    // generator's reverse predecessor queue. Cyclic draws never seed a win.
    do {
        changes = 0U;
        ++rounds;
        for (uint32_t index = 0; index < KPK_STATES; ++index) {
            if (!graph.legal[index] || graph.wins[index]) continue;
            const bool defender = (index & 1U) != 0U;
            bool any = false, all = graph.offsets[index] != graph.offsets[index + 1U];
            for (auto offset = graph.offsets[index]; offset < graph.offsets[index + 1U]; ++offset) {
                const bool win = ChildWin(graph, graph.edges[offset]);
                any |= win;
                all &= win;
            }
            if (graph.mate[index] || (defender ? all : any)) {
                graph.wins[index] = 1U;
                ++changes;
            }
        }
    } while (changes != 0U);

    std::vector<uint16_t> distance(KPK_STATES, UNKNOWN_DISTANCE);
    do {
        changes = 0U;
        for (uint32_t index = 0; index < KPK_STATES; ++index) {
            if (!graph.wins[index]) continue;
            const bool defender = (index & 1U) != 0U;
            uint16_t value = graph.mate[index] ? 0U : defender ? 0U : UNKNOWN_DISTANCE;
            bool missing = false;
            for (auto offset = graph.offsets[index]; offset < graph.offsets[index + 1U]; ++offset) {
                const auto edge = graph.edges[offset];
                if (!ChildWin(graph, edge)) continue;
                const uint16_t child = edge.zeroes ? 0U : distance[edge.child];
                if (child == UNKNOWN_DISTANCE) { missing = true; continue; }
                if (defender) value = std::max<uint16_t>(value, child + 1U);
                else value = std::min<uint16_t>(value, child + 1U);
            }
            if (defender && missing) continue;
            if (value < distance[index]) { distance[index] = value; ++changes; }
        }
    } while (changes != 0U);

    unsigned legal = 0U, wins = 0U, maximum = 0U;
    for (uint32_t index = 0; index < KPK_STATES; ++index) {
        const auto position = Decode(index);
        const auto result = TryProbeKpk(position, 0U);
        if (!graph.legal[index]) {
            Check(result.outcome == KpkOutcome::Unavailable, "all illegal canonical slots decline");
            continue;
        }
        ++legal;
        wins += graph.wins[index];
        Check(result.outcome == (graph.wins[index] ? KpkOutcome::PawnWin : KpkOutcome::Draw),
              "every flash bit matches independent production-rules retrograde");
        if (graph.wins[index]) {
            Check(distance[index] != UNKNOWN_DISTANCE, "every win has a production forced-progress certificate");
            maximum = std::max<unsigned>(maximum, distance[index]);
            Check(TryProbeKpk(position, 79U).outcome == KpkOutcome::PawnWin &&
                      TryProbeKpk(position, 80U).outcome == KpkOutcome::Unavailable &&
                      TryProbeKpk(position).outcome == KpkOutcome::Unavailable,
                  "all wins obey known-clock certified margin, including unknown clocks");
        } else {
            Check(TryProbeKpk(position, UINT16_MAX).outcome == KpkOutcome::Draw,
                  "unknown/high clocks do not revoke exact drawn material");
        }
        for (unsigned transform = 1U; transform < 4U; ++transform) {
            const unsigned pawn = Square64(PawnSquare(position));
            const auto mirrored = Position(Square64(position.king_square[0]),
                                           Square64(position.king_square[1]), pawn,
                                           (index & 1U) != 0U, transform);
            const auto mirroredResult = TryProbeKpk(mirrored, 0U);
            Check(mirroredResult.outcome == result.outcome &&
                      mirroredResult.pawnSide == ((transform & 2U) ? MCUMAX_BOARD_BLACK : MCUMAX_BOARD_WHITE),
                  "horizontal and color/vertical symmetry preserve every legal result");
        }
    }
    Check(legal == 165676U && wins == 111282U && maximum == 20U,
          "independent production counts and fifty-move progress bound match generated certificate");
    std::printf("[KPK_PRODUCTION] slots=%u legal=%u wins=%u edges=%zu rounds=%u max_zeroing_plies=%u\n",
                KPK_STATES, legal, wins, graph.edges.size(), rounds, maximum);
}

void CheckNamedPositions() {
    struct Fixture { const char* name; unsigned owner, defender, pawn; bool defenderTurn; KpkOutcome outcome; };
    const Fixture fixtures[] = {
        {"rook pawn held in corner", 41U, 56U, 40U, false, KpkOutcome::Draw}, // Kb6/Ka8/a6
        {"opposition lost with own pawn blocked", 28U, 44U, 20U, false, KpkOutcome::Draw},
        {"opposition won with defender to move", 28U, 44U, 20U, true, KpkOutcome::PawnWin},
        {"king on sixth rank in front of pawn", 44U, 60U, 36U, false, KpkOutcome::PawnWin},
        {"bare pawn can be captured", 0U, 20U, 12U, true, KpkOutcome::Draw},
        {"remote free promotion", 0U, 63U, 48U, false, KpkOutcome::PawnWin},
        {"promotion would immediately be captured", 7U, 51U, 50U, false, KpkOutcome::Draw},
        {"rook underpromotion avoids queen stalemate", 42U, 48U, 50U, false, KpkOutcome::PawnWin},
        {"actual rook-pawn stalemate", 40U, 56U, 48U, true, KpkOutcome::Draw},
    };
    for (const auto& fixture : fixtures) {
        const auto position = Position(fixture.owner, fixture.defender, fixture.pawn, fixture.defenderTurn);
        const auto original = position;
        const auto result = TryProbeKpk(position, 0U);
        Check(result.outcome == fixture.outcome, fixture.name);
        Check(std::memcmp(&position, &original, sizeof(position)) == 0, "probe preserves every position byte");
        std::printf("[KPK_FIXTURE] name=%s outcome=%u\n", fixture.name, unsigned(result.outcome));
    }
    // The old board-only evaluator gave both opposition positions +120cp for
    // the pawn owner. Preserve the behavioral distinction, not that old score.
    const auto drawn = Position(28U, 44U, 20U, false);
    const auto won = Position(28U, 44U, 20U, true);
    Check(TryProbeKpk(drawn, 0U).outcome == KpkOutcome::Draw &&
              TryProbeKpk(won, 0U).outcome == KpkOutcome::PawnWin,
          "side to move distinguishes drawn/won opposition through the production probe");
    auto promo = Position(42U, 48U, 50U, false);
    const uint8_t from = Square88(50U), to = Square88(58U);
    mcumax_position_undo undo{};
    Check(mcumax_position_make(&promo, {from, to, MCUMAX_QUEEN}, &undo) &&
              !mcumax_position_any_legal(&promo) && !mcumax_position_is_check(&promo, promo.side),
          "production queen promotion is actual stalemate");
    mcumax_position_unmake(&promo, &undo);
    Check(mcumax_position_make(&promo, {from, to, MCUMAX_ROOK}, &undo) &&
              mcumax_position_any_legal(&promo) && MajorConversionWins(promo, to),
          "production rook underpromotion remains a real winning conversion");
}

void CheckInvalidInputs() {
    const auto valid = Position(44U, 60U, 36U, false);
    auto bad = valid;
    bad.board[Square88(0U)] = MCUMAX_BOARD_WHITE | MCUMAX_ROOK;
    Check(TryProbeKpk(bad, 0U).outcome == KpkOutcome::Unavailable, "extra material declines");
    bad = valid; bad.king_square[0] = Square88(43U);
    Check(TryProbeKpk(bad, 0U).outcome == KpkOutcome::Unavailable, "stale king cache declines");
    bad = valid; bad.side = 24U;
    Check(TryProbeKpk(bad, 0U).outcome == KpkOutcome::Unavailable, "invalid side declines");
    bad = valid; bad.board[8] = 1U;
    Check(TryProbeKpk(bad, 0U).outcome == KpkOutcome::Unavailable, "offboard garbage declines");
    bad = valid; bad.en_passant = 255U;
    Check(TryProbeKpk(bad, 0U).outcome == KpkOutcome::Unavailable, "invalid EP safely declines without indexing it");
    bad = valid; bad.board[Square88(36U)] = MCUMAX_BOARD_WHITE | MCUMAX_PAWN_DOWNSTREAM;
    Check(TryProbeKpk(bad, 0U).outcome == KpkOutcome::Unavailable, "pawn direction must match side");
    bad = Position(44U, 60U, 12U, false); bad.board[Square88(12U)] |= MCUMAX_PIECE_MOVED;
    Check(TryProbeKpk(bad, 0U).outcome == KpkOutcome::Unavailable, "synthetic moved starting-rank pawn declines");
    bad = Position(4U, 60U, 28U, true); bad.en_passant = Square88(20U);
    Check(TryProbeKpk(bad, 0U).outcome != KpkOutcome::Unavailable,
          "real double-push EP metadata is harmless with no opposing pawn");
    Check(sizeof(litch_max::classic_chess::KpkResult) == 2U, "result uses two bytes");
}
constexpr uint32_t MAJOR_STATES = 64U * 64U * 64U * 2U;

mcumax_position MajorPosition(uint32_t index, uint8_t type) {
    const bool defenderTurn = index & 1U;
    index /= 2U;
    const unsigned defender = index % 64U;
    index /= 64U;
    const unsigned owner = index % 64U;
    const unsigned major = index / 64U;
    auto position = Position(owner, defender, major, defenderTurn);
    position.board[Square88(major)] = MCUMAX_BOARD_WHITE | MCUMAX_PIECE_MOVED | type;
    return position;
}

bool MajorLegal(uint32_t index, uint8_t type) {
    const auto decoded = index / 2U;
    const unsigned defender = decoded % 64U, owner = (decoded / 64U) % 64U, major = decoded / 4096U;
    if (owner == defender || owner == major || defender == major) return false;
    const auto position = MajorPosition(index, type);
    return !mcumax_position_is_check(&position, MCUMAX_BOARD_WHITE) &&
           ((index & 1U) != 0U || !mcumax_position_is_check(&position, MCUMAX_BOARD_BLACK));
}

uint32_t MajorIndex(const mcumax_position& position, uint8_t type) {
    unsigned major = MCUMAX_SQUARE_INVALID;
    for (unsigned square = 0; square < 128U; ++square)
        if ((square & 0x88U) == 0U && (position.board[square] & 7U) == type) {
            major = Square64(square);
            break;
        }
    if (major == MCUMAX_SQUARE_INVALID) return DRAW_CHILD;
    return (((major * 64U + Square64(position.king_square[0])) * 64U +
             Square64(position.king_square[1])) * 2U) + (position.side == MCUMAX_BOARD_BLACK);
}

void CheckConversion(uint8_t type) {
    std::vector<uint8_t> legal(MAJOR_STATES, 0U);
    std::vector<uint16_t> distance(MAJOR_STATES, UNKNOWN_DISTANCE);
    std::vector<uint32_t> offsets(MAJOR_STATES + 1U, 0U), edges;
    edges.reserve(8000000U);
    for (uint32_t index = 0; index < MAJOR_STATES; ++index) {
        offsets[index] = edges.size();
        if (!MajorLegal(index, type)) continue;
        legal[index] = 1U;
        auto position = MajorPosition(index, type);
        mcumax_position_iterator iterator{};
        mcumax_position_iterator_init(&iterator);
        mcumax_position_move move{};
        while (mcumax_position_iterator_next(&position, &iterator, &move)) {
            mcumax_position_undo undo{};
            if (!mcumax_position_make(&position, move, &undo)) continue;
            edges.push_back(MajorIndex(position, type));
            mcumax_position_unmake(&position, &undo);
        }
        if (offsets[index] == edges.size() && mcumax_position_is_check(&position, position.side))
            distance[index] = 0U;
    }
    offsets[MAJOR_STATES] = edges.size();
    unsigned changes;
    do {
        changes = 0U;
        for (uint32_t index = 0; index < MAJOR_STATES; ++index) {
            if (!legal[index] || offsets[index] == offsets[index + 1U]) continue;
            const bool defender = index & 1U;
            uint16_t value = defender ? 0U : UNKNOWN_DISTANCE;
            bool missing = false;
            for (auto offset = offsets[index]; offset < offsets[index + 1U]; ++offset) {
                const auto child = edges[offset] == DRAW_CHILD ? UNKNOWN_DISTANCE : distance[edges[offset]];
                if (child == UNKNOWN_DISTANCE) { missing = true; continue; }
                if (defender) value = std::max<uint16_t>(value, child + 1U);
                else value = std::min<uint16_t>(value, child + 1U);
            }
            if (defender && missing) continue;
            if (value < distance[index]) { distance[index] = value; ++changes; }
        }
    } while (changes != 0U);
    unsigned wins = 0U, maxMate = 0U, promotions = 0U;
    for (uint32_t index = 0; index < MAJOR_STATES; ++index) {
        if (!legal[index] || distance[index] == UNKNOWN_DISTANCE) continue;
        ++wins;
        maxMate = std::max<unsigned>(maxMate, distance[index]);
    }
    // Independently challenge every accepted major-promotion boundary from
    // every legal canonical seventh-rank KPK. Draw boundaries must stay draw.
    for (uint32_t index = KPK_STATES - 32768U; index < KPK_STATES; index += 2U) {
        if (!CanonicalLegal(index)) continue;
        auto position = Decode(index);
        const unsigned pawn = PawnSquare(position);
        const mcumax_position_move move{uint8_t(pawn), uint8_t(pawn - 16U), type};
        mcumax_position_undo undo{};
        if (!mcumax_position_make(&position, move, &undo)) continue;
        const bool expected = MajorConversionWins(position, move.to);
        const auto majorIndex = MajorIndex(position, type);
        const bool solved = distance[majorIndex] != UNKNOWN_DISTANCE;
        Check(expected == solved, "all promotion classifications match actual production mate attractor");
        if (expected) {
            ++promotions;
            Check(distance[majorIndex] < 100U, "promotion can force mate within the reset fifty-move clock");
        }
    }
    std::printf("[KPK_CONVERSION_PROOF] type=%u states=%u wins=%u edges=%zu max_mate_plies=%u accepted_promotions=%u\n",
                unsigned(type), MAJOR_STATES, wins, edges.size(), maxMate, promotions);
    Check(maxMate < 100U, "every solved major conversion meets fifty-move safety");
}
} // namespace

int main() {
    CheckNamedPositions();
    CheckInvalidInputs();
    CheckProductionAttractor();
    CheckConversion(MCUMAX_QUEEN);
    CheckConversion(MCUMAX_ROOK);
    std::printf("[KPK] failures=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
