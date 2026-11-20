#include <algorithm>
#include <vector>
#include <cstdio>

#include "../../src/duck_chess_search_workspace.hpp"
#include <cstdlib>

#include "litch_max/chess_notation.hpp"
#include "litch_max/chess_strings.hpp"
#include "litch_max/duck_chess_analysis.hpp"
#include "litch_max/initial_fen.hpp"

namespace {
using namespace litch_max;
void Check(bool okay, const char* message) {
    if (!okay) { std::printf("FAIL: %s\n", message); std::exit(1); }
}
uint8_t Square(const char* name) {
    uint8_t square = 64U;
    Check(TryParseUciSquare(name, square), "square parses");
    return square;
}
struct Observe {
    const char* fen;
    uint8_t duck;
    DuckChessSearchStats* stats;
    ThinkCandidate last{};
    ThinkCandidate completed{};
    uint32_t publications = 0U;
    uint32_t lastNodes = 0U;
    uint32_t completedNodes = 0U;
    uint8_t targetDepth = 1U;
    uint32_t extraNodes = 0U;
    static void Candidate(void* context, const ThinkCandidate& candidate) {
        auto& self = *static_cast<Observe*>(context);
        UciMove move;
        DuckChessMoveResult result;
        Check(candidate.isDuck && TryFormatUciMove(candidate.fromSquare, candidate.toSquare,
                  candidate.promotion, move) && TryApplyDuckMove(self.fen, self.duck, move.CStr(), result),
              "every published candidate is a real legal root move");
        Check(result.capturedKing == candidate.capturedKing, "terminal flag matches applied move");
        char piece = '\0';
        Check(candidate.capturedKing ? candidate.duckDestination == 255U :
                  candidate.duckDestination < 64U && candidate.duckDestination != self.duck &&
                  !TryGetFenPieceAtSquare(result.resultingFen.CStr(), candidate.duckDestination, piece),
              "published compound turn has legal relocation or terminal no-duck");
        self.last = candidate;
        self.lastNodes = self.stats->nodesVisited;
        ++self.publications;
        if (self.stats->completedDepth == self.targetDepth && self.completedNodes == 0U) {
            self.completed = candidate;
            self.completedNodes = self.stats->nodesVisited;
        }
    }
    static bool Stop(void* context) {
        const auto& self = *static_cast<Observe*>(context);
        return self.completedNodes != 0U && self.stats->nodesVisited >= self.completedNodes + self.extraNodes;
    }
};
bool Same(const ThinkCandidate& candidate, const DuckChessTurn& turn) {
    UciMove move;
    return TryFormatUciMove(candidate.fromSquare, candidate.toSquare, candidate.promotion, move) &&
           move.View() == turn.chessMove.View() && candidate.duckDestination == turn.duckDestination;
}
void TestInitialAndInterruptedDepth(DuckChessAnalysisWorkspace& workspace) {
    DuckChessSearchConfig config;
    config.continuousAnalysis = true;
    config.workspace = &workspace;
    config.maxNodes = 1U;
    DuckChessSearchStats stats;
    DuckChessTurn best;
    Observe observed{INITIAL_FEN, Square("a4"), &stats};
    config.candidateObserver = {&Observe::Candidate, &observed};
    // A complete Hint needs the whole root iteration; stopping at two nodes
    // must not publish a partial fallback.
    config.maxNodes = 2U;
    Check(!FindBestDuckChessTurn(INITIAL_FEN, observed.duck, config, best, stats),
          "incomplete initial scan has no Hint result");
    Check(observed.publications == 0U && stats.completedDepth == 0U &&
              best.chessMove.Empty(),
          "partial initial scan never publishes a compound Hint");
    config.maxNodes = 100000U;
    observed = Observe{INITIAL_FEN, Square("a4"), &stats};
    observed.extraNodes = 30U;
    Check(FindBestDuckChessTurn(INITIAL_FEN, observed.duck, config, best, stats, &Observe::Stop, &observed),
          "interruption during depth2 retains fallback");
    Check(stats.canceled && stats.completedDepth == 1U && observed.completedNodes > 0U &&
              stats.nodesVisited == observed.completedNodes + observed.extraNodes && Same(observed.completed, best),
          "unfinished deeper comparison cannot replace previous completed depth");
    std::printf("initial depth=%u nodes=%u scratch=%u\n", stats.completedDepth, stats.nodesVisited, stats.workspaceBytes);
    Check(stats.workspaceBytes == DuckChessAnalysisWorkspace::DuckRequiredBytes() && stats.workspaceBytes <= 24U * 1024U && !stats.depthLimitReached && !stats.allocationFailed,
          "Duck owns proportional bounded scratch only for its current search");
}
void TestPersistedInitialPositionWithCenterDuck(
    DuckChessAnalysisWorkspace& workspace) {
    DuckChessSearchConfig config;
    config.continuousAnalysis = true;
    config.workspace = &workspace;
    config.maxNodes = 10000U;
    DuckChessSearchStats stats;
    DuckChessTurn best;
    Observe observed{INITIAL_FEN, Square("d5"), &stats};
    config.candidateObserver = {&Observe::Candidate, &observed};
    Check(FindBestDuckChessTurn(INITIAL_FEN, observed.duck, config, best,
                                stats),
          "persisted initial position with d5 Duck completes its root");
    Check(observed.publications > 0U && stats.completedDepth != 0U &&
              Same(observed.last, best),
          "d5 Duck Hint is published only after a complete root iteration");
}
void TestGenuineDepth(DuckChessAnalysisWorkspace& workspace) {
    constexpr char FEN[] = "7k/8/8/8/8/8/8/K7 w - - 0 1";
    DuckChessSearchStats stats;
    DuckChessTurn best;
    Observe observed{FEN, Square("d4"), &stats};
    observed.targetDepth = 2U;
    observed.extraNodes = 17U;
    DuckChessSearchConfig config;
    config.continuousAnalysis = true;
    config.workspace = &workspace;
    config.maxNodes = 1000000U;
    config.candidateObserver = {&Observe::Candidate, &observed};
    Check(FindBestDuckChessTurn(FEN, observed.duck, config, best, stats, &Observe::Stop, &observed),
          "search progresses through genuinely deeper compound turns");
    Check(stats.completedDepth == 2U && stats.canceled && Same(observed.completed, best) &&
              stats.nodesVisited == observed.completedNodes + 17U,
          "depth3 interruption preserves completed depth2");
    std::printf("deeper depth=%u nodes=%u move=%s duck=%u\n", stats.completedDepth, stats.nodesVisited,
                best.chessMove.CStr(), best.duckDestination);
}
void TestTacticalReply(DuckChessAnalysisWorkspace& workspace) {
    constexpr char FEN[] = "3k4/8/5n2/3r4/8/8/8/3Q3K w - - 0 1";
    DuckChessSearchStats stats;
    DuckChessTurn best;
    Observe observed{FEN, Square("a4"), &stats};
    observed.targetDepth = 2U;
    DuckChessSearchConfig config;
    config.continuousAnalysis = true;
    config.workspace = &workspace;
    config.maxNodes = 1000000U;
    config.candidateObserver = {&Observe::Candidate, &observed};
    Check(FindBestDuckChessTurn(FEN, observed.duck, config, best, stats, &Observe::Stop, &observed),
          "tactical search returns real compound turn");
    Check(stats.completedDepth >= 2U, "tactical reply horizon completes");
    DuckChessMoveResult applied;
    Check(TryApplyDuckMove(FEN, observed.duck, best.chessMove.CStr(), applied), "tactical result applies");
    for (uint8_t from = 0U; from < 64U; ++from) {
        uint64_t destinations = 0ULL;
        Check(GetDuckLegalDestinationsMask(applied.resultingFen.CStr(), best.duckDestination, from, destinations),
              "tactical oracle enumerates opponent replies");
        for (uint8_t to = 0U; to < 64U; ++to) {
            char piece = '\0';
            Check((destinations & (1ULL << to)) == 0ULL ||
                      !TryGetFenPieceAtSquare(applied.resultingFen.CStr(), to, piece) || piece != 'Q',
                  "deeper best protects queen from poisoned rook capture");
        }
    }
    std::printf("tactical depth=%u nodes=%u move=%s duck=%u\n", stats.completedDepth,
                stats.nodesVisited, best.chessMove.CStr(), best.duckDestination);
}
void TestEveryPromotion(DuckChessAnalysisWorkspace& workspace) {
    // Three king moves and four a7-a8 promotions, each followed by sixty
    // different legal duck placements. A queen-only generator visits240
    // instead of420 compound turns and fails this boundary check.
    constexpr char FEN[] = "7k/P7/8/8/8/8/8/K7 w - - 0 1";
    DuckChessSearchStats stats;
    DuckChessTurn best;
    Observe observed{FEN, Square("d4"), &stats};
    DuckChessSearchConfig config;
    config.continuousAnalysis = true;
    config.workspace = &workspace;
    config.maxNodes = 10000U;
    config.candidateObserver = {&Observe::Candidate, &observed};
    Check(FindBestDuckChessTurn(FEN, observed.duck, config, best, stats, &Observe::Stop, &observed),
          "promotion position yields a completed root");
    Check(stats.completedDepth == 1U && stats.candidateTurns == 420U && stats.nodesVisited == 420U,
          "initial root covers all four promotions and all compound placements");
}
void TestPreferredCompoundCoverage() {
    using litch_max::duck_chess_search_internal::CompoundMove;
    using litch_max::duck_chess_search_internal::Frame;
    struct Case { const char* fen; uint8_t duck; CompoundMove preferred; };
    const Case cases[] = {
        {"7k/P7/8/8/8/8/8/K7 w - - 0 1", Square("d4"), {Square("a7"), Square("a8"), 'n', Square("d5")}},
        {"4k3/8/8/8/8/8/4Q3/4K3 w - - 0 1", Square("a4"), {Square("e2"), Square("e8"), '\0', 255U, true}},
        {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", Square("a4"), {Square("e5"), Square("d6"), '\0', Square("e4")}},
        {"4kr2/8/8/8/8/8/8/R3K2R w KQ - 0 1", Square("a4"), {Square("e1"), Square("g1"), '\0', Square("d5")}},
    };
    const auto keys = [&](const Case& test, CompoundMove preference) {
        litch_max::duck_chess_rules_internal::DuckPosition root;
        Check(litch_max::duck_chess_rules_internal::ParseDuckPosition(test.fen, root), "coverage root parses");
        Frame frame;
        frame.Begin(root, test.duck, 1U, -1000001, 1000001, preference);
        std::vector<uint32_t> result;
        while (frame.Next()) {
            const auto& candidate = frame.candidate;
            UciMove move;
            DuckChessMoveResult applied;
            Check(TryFormatUciMove(candidate.from, candidate.to, candidate.promotion, move) &&
                  TryApplyDuckMove(test.fen, test.duck, move.CStr(), applied), "each streamed preferred/ordinary chess half uses production Duck legality");
            char occupied = '\0';
            Check(candidate.capturedKing == applied.capturedKing && (candidate.capturedKing ? candidate.duck == 255U :
                  candidate.duck < 64U && candidate.duck != test.duck &&
                  !TryGetFenPieceAtSquare(applied.resultingFen.CStr(), candidate.duck, occupied)), "every streamed tuple has a legal mandatory placement or terminal no-duck");
            const uint32_t promotion = candidate.promotion == 'q' ? 4U : candidate.promotion == 'r' ? 3U :
                candidate.promotion == 'b' ? 2U : candidate.promotion == 'n' ? 1U : 0U;
            result.push_back(candidate.from | (candidate.to << 6U) | (promotion << 12U) |
                             ((candidate.duck < 64U ? candidate.duck : 64U) << 15U));
        }
        std::sort(result.begin(), result.end());
        Check(std::adjacent_find(result.begin(), result.end()) == result.end(), "cached preferences never duplicate a compound choice");
        return result;
    };
    for (const auto& test : cases) {
        const auto full = keys(test, {});
        Check(keys(test, test.preferred) == full, "preferred promotion/king capture/EP/castle neither omits nor adds any compound choice");
        auto invalidPlacement = test.preferred;
        invalidPlacement.duck = test.duck;
        Check(keys(test, invalidPlacement) == full, "a different cached blocker is validated before emission and does not lose coverage");
    }
}

void TestTerminalAndFowled(DuckChessAnalysisWorkspace& workspace) {
    constexpr char FEN[] = "4k3/8/8/8/8/8/4Q3/4K3 w - - 0 1";
    DuckChessSearchStats stats;
    DuckChessTurn best;
    Observe observed{FEN, Square("a4"), &stats};
    DuckChessSearchConfig config;
    config.continuousAnalysis = true;
    config.workspace = &workspace;
    config.maxNodes = 1U;
    config.candidateObserver = {&Observe::Candidate, &observed};
    Check(FindBestDuckChessTurn(FEN, observed.duck, config, best, stats) && stats.completed &&
              best.capturedKing && best.duckDestination == 255U && observed.publications == 1U,
          "immediate king capture is published even at minimum budget");
    config.candidateObserver = {};
    config.maxNodes = 10000U;
    Check(!FindBestDuckChessTurn("8/8/8/8/8/8/8/7k w - - 0 1", Square("a4"), config, best, stats) && stats.completed,
          "fowled side has no invented move");
}
} // namespace
int main() {
    DuckChessAnalysisWorkspace workspace;
    TestInitialAndInterruptedDepth(workspace);
    TestPersistedInitialPositionWithCenterDuck(workspace);
    TestGenuineDepth(workspace);
    TestTacticalReply(workspace);
    TestEveryPromotion(workspace);
    TestPreferredCompoundCoverage();
    TestTerminalAndFowled(workspace);
    std::puts("continuous Duck production search passed");
    return 0;
}
