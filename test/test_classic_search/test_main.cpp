#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <tuple>
#include <vector>

#include <mcu-max.h>
#include <mcu-max-position.h>

#include "litch_max/classic_chess_search.hpp"
#include "litch_max/classic_chess_evaluation.hpp"
#include "litch_max/initial_fen.hpp"
#include "litch_max/duck_chess_search.hpp"
#include "../../src/classic_chess_move_order.hpp"
#include "../support/allocation_probe.hpp"

namespace {

using litch_max::classic_chess::SearchConfig;
using litch_max::classic_chess::SearchResult;
using litch_max::classic_chess::SearchWorkspace;
using litch_max::classic_chess::CacheMode;

constexpr size_t MAX_WORKSPACE_BYTES = 32768U;
int g_failures = 0;

void Check(const bool condition, const char* message) {
    if (!condition) {
        std::printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

mcumax_position Position(const char* fen) {
    mcumax_init();
    mcumax_set_fen_position(fen);
    mcumax_position position{};
    if (!mcumax_position_import(&position, &mcumax)) {
        Check(false, "valid fixture imports through the real rules seam");
    }
    return position;
}

bool SamePosition(const mcumax_position& lhs, const mcumax_position& rhs) {
    return std::memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}

bool SameMove(const mcumax_position_move lhs, const mcumax_position_move rhs) {
    return lhs.from == rhs.from && lhs.to == rhs.to && lhs.promotion == rhs.promotion;
}

uint8_t Promotion(const char symbol) {
    switch (symbol) {
        case 'q': return MCUMAX_QUEEN;
        case 'r': return MCUMAX_ROOK;
        case 'b': return MCUMAX_BISHOP;
        case 'n': return MCUMAX_KNIGHT;
        default: return 0U;
    }
}

mcumax_position_move Move(const litch_max::ThinkCandidate& candidate) {
    return {
        static_cast<uint8_t>((candidate.fromSquare / 8U) * 16U + candidate.fromSquare % 8U),
        static_cast<uint8_t>((candidate.toSquare / 8U) * 16U + candidate.toSquare % 8U),
        Promotion(candidate.promotion),
    };
}

struct Observation {
    const mcumax_position* caller = nullptr;
    mcumax_position root{};
    mcumax_struct singleton{};
    litch_max::ThinkCandidate latest{};
    uint32_t nodes = 0U;
    uint32_t cooperationCalls = 0U;
    uint32_t lastCooperationNodes = 0U;
    uint32_t publications = 0U;
    uint32_t firstPublicationNodes = 0U;
    uint32_t latestPublicationNodes = 0U;
    uint32_t extraNodesBeforeCancel = 0U;
    uint8_t cancelAfterDepth = 0U;
    uint8_t minimumDepth = 1U;
    bool cancelImmediately = false;
    bool cancelRequested = false;

    explicit Observation(const mcumax_position& position)
        : caller(&position), root(position), singleton(mcumax) {}

    static void Cooperate(void* context, const uint32_t nodeCount) {
        auto& self = *static_cast<Observation*>(context);
        ++self.cooperationCalls;
        Check(nodeCount == self.cooperationCalls,
              "cooperation receives each visited node exactly once in order");
        self.lastCooperationNodes = nodeCount;
    }

    static void Progress(void* context, const uint32_t nodeCount) {
        auto& self = *static_cast<Observation*>(context);
        Check(nodeCount >= self.nodes, "observed search work never runs backwards");
        self.nodes = nodeCount;
        if (self.publications != 0U && self.extraNodesBeforeCancel != 0U &&
            self.latest.completedDepth >= self.cancelAfterDepth &&
            nodeCount >= self.latestPublicationNodes + self.extraNodesBeforeCancel) {
            self.cancelRequested = true;
        }
    }

    static void Candidate(void* context, const litch_max::ThinkCandidate& candidate) {
        auto& self = *static_cast<Observation*>(context);
        Check(SamePosition(*self.caller, self.root),
              "publication never exposes a changed caller position");
        Check(std::memcmp(&mcumax, &self.singleton, sizeof(mcumax)) == 0,
              "publication never enters or changes the legacy singleton");
        Check(candidate.completedDepth >= self.minimumDepth &&
                  candidate.completedDepth > self.latest.completedDepth,
              "only a complete genuinely deeper eligible root publishes");
        Check(!candidate.isDuck && candidate.duckDestination == 255U &&
                  candidate.fromSquare < 64U && candidate.toSquare < 64U,
              "Classic publication carries valid immutable move coordinates");
        auto applied = self.root;
        mcumax_position_undo undo{};
        const bool legal = mcumax_position_make(&applied, Move(candidate), &undo);
        Check(legal, "each published move is actually executable from the root");
        if (legal) {
            Check(!mcumax_position_is_check(&applied, self.root.side),
                  "each publication preserves the mover king's safety");
            mcumax_position_unmake(&applied, &undo);
            Check(SamePosition(applied, self.root),
                  "published-move make/unmake exactly restores root state");
        }
        if (self.publications == 0U) self.firstPublicationNodes = self.nodes;
        self.latestPublicationNodes = self.nodes;
        self.latest = candidate;
        ++self.publications;
    }

    static bool Stop(void* context) {
        const auto& self = *static_cast<Observation*>(context);
        return self.cancelImmediately || self.cancelRequested;
    }
};

SearchConfig Config(Observation& observed, const uint32_t nodes,
                    const uint8_t depth, const uint8_t minimumDepth = 1U,
                    const CacheMode cacheMode = CacheMode::Bounds,
                    const bool principalVariationSearch = true) {
    observed.minimumDepth = minimumDepth;
    SearchConfig config;
    config.maxNodes = nodes;
    config.maxDepth = depth;
    config.minimumDepth = minimumDepth;
    config.cacheMode = cacheMode;
    config.principalVariationSearch = principalVariationSearch;
    config.shouldStop = &Observation::Stop;
    config.stopContext = &observed;
    config.cooperate = &Observation::Cooperate;
    config.cooperateContext = &observed;
    config.progressObserver = {&Observation::Progress, &observed};
    config.candidateObserver = {&Observation::Candidate, &observed};
    return config;
}

void CheckRestored(const mcumax_position& position, const Observation& observed) {
    Check(SamePosition(position, observed.root),
          "every search exit exactly preserves caller board, turn, kings and EP");
    Check(std::memcmp(&mcumax, &observed.singleton, sizeof(mcumax)) == 0,
          "every search exit preserves all legacy engine state and callbacks");
}

void CheckResult(const SearchResult& result, const Observation& observed) {
    Check(observed.cooperationCalls == result.nodes &&
              observed.lastCooperationNodes == result.nodes,
          "cooperation call count and final node value match actual search work");
    Check(result.available && !result.terminal && observed.publications != 0U,
          "a usable result has a committed publication");
    if (!result.available || observed.publications == 0U) return;
    Check(result.completedDepth == observed.latest.completedDepth &&
              SameMove(result.move, Move(observed.latest)),
          "returned move/depth belong to the last complete published iteration");
}

// Independent whole-list ordering oracle: rank each capture class, retain its
// best four, then preserve generator order for overflow. This uses no staged
// cursor or production MoveOrder state.
std::vector<mcumax_position_move> ReferenceMoveOrder(
        const mcumax_position& position, const mcumax_position_move preferred,
        const std::array<mcumax_position_move, 2U>& killers) {
    using litch_max::classic_chess::PieceValue;
    using litch_max::classic_chess::internal::IsCapture;
    std::vector<mcumax_position_move> good, bad, quiet, expected;
    mcumax_position_iterator iterator{};
    mcumax_position_iterator_init(&iterator);
    mcumax_position_move move{};
    while (mcumax_position_iterator_next(&position, &iterator, &move)) {
        if (SameMove(move, preferred)) continue;
        if (!IsCapture(position, move) && move.promotion == 0U) {
            if (!SameMove(move, killers[0]) && !SameMove(move, killers[1])) quiet.push_back(move);
            continue;
        }
        const int victim = position.board[move.to] == 0U ? 100 : PieceValue(position.board[move.to]);
        (move.promotion != 0U || victim >= PieceValue(position.board[move.from]) ? good : bad).push_back(move);
    }
    const auto rank = [&position](const mcumax_position_move choice) {
        const int victim = position.board[choice.to] == 0U ?
            (IsCapture(position, choice) ? 100 : 0) : PieceValue(position.board[choice.to]);
        const int gain = victim + (choice.promotion != 0U ? PieceValue(choice.promotion) - 100 : 0);
        const uint8_t type = position.board[choice.from] & 7U;
        return std::make_tuple(-gain, type == MCUMAX_KING ? 32767 : PieceValue(type));
    };
    const auto appendCaptures = [&expected, &rank](const auto& choices) {
        auto ranked = choices;
        std::stable_sort(ranked.begin(), ranked.end(), [&rank](const auto a, const auto b) { return rank(a) < rank(b); });
        const size_t count = std::min<size_t>(4U, ranked.size());
        expected.insert(expected.end(), ranked.begin(), ranked.begin() + count);
        for (const auto choice : choices) {
            if (std::none_of(ranked.begin(), ranked.begin() + count,
                             [choice](const auto top) { return SameMove(choice, top); })) expected.push_back(choice);
        }
    };
    if (preferred.from != MCUMAX_SQUARE_INVALID) expected.push_back(preferred);
    appendCaptures(good);
    for (const auto killer : killers) {
        const uint8_t type = killer.from == MCUMAX_SQUARE_INVALID ? 0U : position.board[killer.from] & 7U;
        const bool implicitPromotion = (type == MCUMAX_PAWN_UPSTREAM && (killer.to >> 4U) == 0U) ||
            (type == MCUMAX_PAWN_DOWNSTREAM && (killer.to >> 4U) == 7U);
        if (killer.from != MCUMAX_SQUARE_INVALID && !implicitPromotion &&
            !SameMove(killer, preferred) && !IsCapture(position, killer)) expected.push_back(killer);
    }
    expected.insert(expected.end(), quiet.begin(), quiet.end());
    appendCaptures(bad);
    return expected;
}

void CheckOrderedMake(const mcumax_position& position, const mcumax_position_move move,
                      const bool generated) {
    auto strict = position, selected = position;
    mcumax_position_undo strictUndo{}, selectedUndo{};
    std::memset(&strictUndo, 0xa5, sizeof(strictUndo));
    std::memset(&selectedUndo, 0xa5, sizeof(selectedUndo));
    const bool strictOk = mcumax_position_make(&strict, move, &strictUndo);
    const bool selectedOk = generated ? mcumax_position_make_generated(&selected, move, &selectedUndo) :
        mcumax_position_make(&selected, move, &selectedUndo);
    Check(strictOk == selectedOk && SamePosition(strict, selected) &&
              std::memcmp(&strictUndo, &selectedUndo, sizeof(strictUndo)) == 0,
          "ordered provenance preserves strict legality, every child byte and compact undo");
    if (strictOk) mcumax_position_unmake(&strict, &strictUndo);
    if (selectedOk) mcumax_position_unmake(&selected, &selectedUndo);
    Check(SamePosition(strict, position) && SamePosition(selected, position),
          "both ordered make paths restore the same parent before the next iterator yield");
}

void CheckQuietGenerationScreensAt(const mcumax_position& position) {
    using litch_max::classic_chess::internal::IsCapture;
    using litch_max::classic_chess::internal::MayCreateQuietCheck;
    using litch_max::classic_chess::internal::MoveOrder;
    using litch_max::classic_chess::internal::MoveSelection;
    mcumax_position_move move{}, selected{};
    // Cached preference and sibling killers change the stream before qsearch.
    // Check both quiet and tactical preferred moves against the full order.
    mcumax_position_iterator preferredIterator{};
    mcumax_position_iterator_init(&preferredIterator);
    mcumax_position_move preferred = litch_max::classic_chess::internal::INVALID_MOVE;
    do {
        MoveOrder all, tactical;
        for (const auto killer : {mcumax_position_move{113U, 82U, 0U},
                                  mcumax_position_move{16U, 0U, 0U}}) {
            all.RememberKiller(killer); tactical.RememberKiller(killer);
        }
        all.Begin(preferred); tactical.Begin(preferred);
        const std::array<mcumax_position_move, 2U> killers{{{16U, 0U, 0U}, {113U, 82U, 0U}}};
        const auto expected = ReferenceMoveOrder(position, preferred, killers);
        size_t ordered = 0U;
        while (all.Next(position, move)) {
            Check(ordered < expected.size() && SameMove(move, expected[ordered]),
                  "every preferred, killer, prefix and overflow tuple preserves the whole-list order");
            ++ordered;
            const bool quiet = !IsCapture(position, move) && move.promotion == 0U;
            const bool generated = !SameMove(move, preferred) &&
                !(quiet && (SameMove(move, killers[0]) || SameMove(move, killers[1])));
            Check(all.LastMoveWasGenerated() == generated,
                  "only iterator and capture-prefix tuples carry generated movement evidence");
            CheckOrderedMake(position, move, all.LastMoveWasGenerated());
            if (quiet) continue;
            Check(tactical.Next(position, selected, MoveSelection::Tactical) && SameMove(move, selected) &&
                      tactical.LastMoveWasGenerated() == generated,
                  "tactical stream preserves capture/promotion order and provenance with cached preferences and killers");
        }
        Check(ordered == expected.size() && !all.LastMoveWasGenerated(),
              "exhausted full stream has exactly the reference tuples and no stale provenance");
        Check(!tactical.Next(position, selected, MoveSelection::Tactical) && !tactical.LastMoveWasGenerated(),
              "tactical stream emits no additional quiet or duplicate move and clears provenance");
    } while (mcumax_position_iterator_next(&position, &preferredIterator, &preferred));
    const bool alreadyChecked = mcumax_position_is_check(&position, position.side ^ 24U);
    mcumax_position_iterator iterator{};
    mcumax_position_iterator_init(&iterator);
    while (mcumax_position_iterator_next(&position, &iterator, &move)) {
        if (IsCapture(position, move) || move.promotion != 0U) continue;
        auto made = position;
        mcumax_position_undo undo{};
        if (!mcumax_position_make(&made, move, &undo)) continue;
        if (!alreadyChecked && mcumax_position_is_check(&made, made.side)) {
            Check(MayCreateQuietCheck(position, move),
                  "cheap quiet-check screen cannot exclude an actual executable check");
        }
        mcumax_position_unmake(&made, &undo);
        Check(SamePosition(made, position), "quiet-check verification restores every position byte");
    }
}

void CheckQuietGenerationScreens() {
    const char* cases[] = {
        litch_max::INITIAL_FEN,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r1bk1b1r/ppN2ppp/2n5/4p1q1/2B5/8/PPPP1PPP/R1BQK2R w KQ - 1 9",
        "1r5k/P7/8/8/8/8/8/7K w - - 0 1",
        "8/8/8/r4pPK/8/8/8/4k3 w - f6 0 1",
        "3k4/8/8/8/8/8/8/R3K2R w KQ - 0 1",
        "4k3/8/8/8/8/8/4B3/4R1K1 w - - 0 1",
        "8/7k/8/8/4N3/8/8/1B5K w - - 0 1",
        "8/7k/8/8/4P3/8/8/1B5K w - - 0 1",
        "4k3/8/8/8/8/8/4R3/1N2K3 w - - 0 1",
        // Capture-stage exhaustion at0/4/5/6 eligible tuples. The helper
        // also excludes each preferred move, crossing the4/5 threshold, and
        // checks all four promotion tuples with sibling killer preferences.
        "7k/8/8/8/8/8/8/7K w - - 0 1",
        "7k/P7/8/8/8/8/8/7K w - - 0 1",
        "7k/P7/8/2q5/4N3/8/8/7K w - - 0 1",
        "7k/P7/5q2/2q5/4N3/8/8/7K w - - 0 1",
        "7k/8/3p4/8/1p1Q1p2/8/3p4/7K w - - 0 1",
        "7k/8/3p1p2/8/1p1Q1p2/8/3p4/7K w - - 0 1",
        "7k/8/1p1p1p2/8/1p1Q1p2/8/3p4/7K w - - 0 1",
    };
    for (const auto fen : cases) {
        const auto root = Position(fen);
        CheckQuietGenerationScreensAt(root);
        mcumax_position_iterator iterator{};
        mcumax_position_move move{};
        mcumax_position_iterator_init(&iterator);
        while (mcumax_position_iterator_next(&root, &iterator, &move)) {
            auto child = root;
            mcumax_position_undo undo{};
            if (mcumax_position_make(&child, move, &undo)) CheckQuietGenerationScreensAt(child);
        }
    }
}

void CheckVariantWorkspaceReuse(SearchWorkspace& workspace) {
    const auto root = Position(litch_max::INITIAL_FEN);
    Observation before(root);
    const auto classic = workspace.Run(root, Config(before, 0U, 3U));
    litch_max::DuckChessSearchConfig config;
    config.maxNodes = 4096U;
    config.maxDepth = 2U;
    config.continuousAnalysis = true;
    for (const bool canceled : {false, true}) {
        litch_max::DuckChessTurn turn, referenceTurn;
        litch_max::DuckChessSearchStats stats, referenceStats;
        bool (*const stop)(void*) = [](void* context) { return static_cast<litch_max::DuckChessSearchStats*>(context)->nodesVisited >= 73U; };
        const bool found = workspace.RunDuck(litch_max::INITIAL_FEN, 32U, config, turn, stats,
                                             canceled ? stop : nullptr, &stats);
        SearchWorkspace fresh;
        const bool reference = fresh.RunDuck(litch_max::INITIAL_FEN, 32U, config, referenceTurn, referenceStats,
                                             canceled ? stop : nullptr, &referenceStats);
        Check(found == reference && turn.chessMove.View() == referenceTurn.chessMove.View() &&
                  turn.duckDestination == referenceTurn.duckDestination && turn.score == referenceTurn.score &&
                  stats.nodesVisited == referenceStats.nodesVisited && stats.completedDepth == referenceStats.completedDepth &&
                  stats.workspaceBytes == SearchWorkspace::DuckRequiredBytes(), "Classic-to-Duck reuse matches proportional scratch after normal or canceled work");
        CheckRestored(root, before);
        Observation after(root);
        const auto again = workspace.Run(root, Config(after, 0U, 3U));
        Check(SameMove(classic.move, again.move) && classic.score == again.score &&
                  classic.nodes == again.nodes && classic.completedDepth == again.completedDepth,
              "Duck-to-Classic reuse retires every variant cursor, cache, callback and stop flag");
        CheckRestored(root, after);
    }
}

void CheckFixedWorkspaceSearches() {
    const auto root = Position(litch_max::INITIAL_FEN);
    Observation observation(root);
    const auto config = Config(observation, 0U, 3U);
    const size_t before = search_allocation_probe::calls;
    SearchWorkspace workspace;
    const auto classic = workspace.Run(root, config);
    Check(classic.available && classic.completedDepth == 3U &&
              classic.allocationFailureBytes == 0U && search_allocation_probe::calls == before,
          "workspace construction and complete Classic search allocate nothing");
    CheckRestored(root, observation);

    litch_max::DuckChessSearchConfig duck;
    duck.maxDepth = 1U;
    for (const bool supplied : {true, false}) {
        duck.workspace = supplied ? &workspace : nullptr;
        litch_max::DuckChessTurn turn;
        litch_max::DuckChessSearchStats stats;
        const bool found = litch_max::FindBestDuckChessTurn(litch_max::INITIAL_FEN, 32U, duck, turn, stats);
        Check(found && stats.completedDepth == 1U && !stats.allocationFailed &&
                  stats.workspaceBytes == SearchWorkspace::DuckRequiredBytes() &&
                  search_allocation_probe::calls == before,
              "complete Duck turn allocates nothing with either scratch owner");
        uint8_t square = litch_max::DUCK_CHESS_INVALID_SQUARE;
        Check(litch_max::FindBestDuckPlacement(litch_max::INITIAL_FEN, 32U, duck, square, stats) &&
                  square < 64U && stats.workspaceBytes == 0U && search_allocation_probe::calls == before,
              "placement needs no search workspace or allocation");
    }
    CheckRestored(root, observation);
}

void CheckBudgetsAndEligibility(SearchWorkspace& workspace) {
    const auto position = Position(litch_max::INITIAL_FEN);
    Observation tiny(position);
    const auto tinyResult = workspace.Run(position, Config(tiny, 1U, 8U));
    Check(!tinyResult.available && !tinyResult.terminal && tinyResult.interrupted &&
              tiny.publications == 0U && tinyResult.nodes <= 1U,
          "one node cannot fabricate a complete initial-position result");
    CheckRestored(position, tiny);

    Observation bounded(position);
    const auto boundedResult = workspace.Run(position, Config(bounded, 97U, 8U));
    Check(boundedResult.nodes <= 97U && boundedResult.interrupted,
          "ordinary and quiescence work share the exact configured node cap");
    if (boundedResult.available) CheckResult(boundedResult, bounded);
    CheckRestored(position, bounded);

    Observation tooShallow(position);
    const auto shallowResult = workspace.Run(position, Config(tooShallow, 100000U, 1U, 2U));
    Check(!shallowResult.available && tooShallow.publications == 0U &&
              shallowResult.completedDepth <= 1U,
          "completed work below the configured Hint floor is not a usable Hint");
    CheckRestored(position, tooShallow);

    Observation complete(position);
    const auto completeResult = workspace.Run(position, Config(complete, 0U, 2U));
    Check(completeResult.available && !completeResult.interrupted &&
              completeResult.completedDepth == 2U && completeResult.nodes > 0U,
          "zero node cap means unlimited work but genuine two-ply depth still bounds search");
    CheckResult(completeResult, complete);
    CheckRestored(position, complete);
    std::printf("[CLASSIC_BOUNDS] tiny=%u bounded=%u complete=%u depth=%u workspace=%zu\n",
                tinyResult.nodes, boundedResult.nodes, completeResult.nodes,
                completeResult.completedDepth, SearchWorkspace::RequiredBytes());
}

void CheckCancelAndReuse(SearchWorkspace& workspace,
                        const CacheMode cacheMode = CacheMode::Bounds,
                        const bool principalVariationSearch = true) {
    const auto position = Position(litch_max::INITIAL_FEN);
    Observation beforeWork(position);
    beforeWork.cancelImmediately = true;
    const auto canceled = workspace.Run(position, Config(beforeWork, 100000U, 8U, 1U, cacheMode, principalVariationSearch));
    Check(canceled.interrupted && !canceled.available && !canceled.terminal &&
              canceled.nodes == 0U && beforeWork.publications == 0U,
          "cancellation before work returns no invented result or publication");
    CheckRestored(position, beforeWork);

    Observation duringDepth(position);
    duringDepth.extraNodesBeforeCancel = 17U;
    duringDepth.cancelAfterDepth = 2U;
    const auto interrupted = workspace.Run(position, Config(duringDepth, 100000U, 8U, 1U, cacheMode, principalVariationSearch));
    Check(duringDepth.cancelRequested && interrupted.interrupted && interrupted.available,
          "real observer-driven cancellation occurs after committed work and later search work");
    Check(interrupted.nodes >= duringDepth.latestPublicationNodes + 17U &&
              interrupted.completedDepth == 2U,
          "cancellation interrupts deeper work after the retained complete two-ply root");
    CheckResult(interrupted, duringDepth);
    CheckRestored(position, duringDepth);

    Observation reused(position);
    const auto fresh = workspace.Run(position, Config(reused, 0U, 2U, 1U, cacheMode, principalVariationSearch));
    Observation repeated(position);
    const auto repeat = workspace.Run(position, Config(repeated, 0U, 2U, 1U, cacheMode, principalVariationSearch));
    Check(fresh.available && !fresh.interrupted && repeat.available && !repeat.interrupted &&
              fresh.completedDepth == 2U && repeat.completedDepth == 2U &&
              SameMove(fresh.move, repeat.move) && fresh.score == repeat.score &&
              fresh.nodes == repeat.nodes,
          "workspace reuse clears cancellation and yields deterministic fresh search");
    CheckResult(fresh, reused);
    CheckResult(repeat, repeated);
    CheckRestored(position, reused);
    CheckRestored(position, repeated);
    std::printf("[CLASSIC_CANCEL] cache=%u pvs=%u nodes=%u retained_depth=%u publications=%u reuse_nodes=%u\n",
                static_cast<unsigned>(cacheMode), static_cast<unsigned>(principalVariationSearch),
                interrupted.nodes, interrupted.completedDepth,
                duringDepth.publications, fresh.nodes);
}

void CheckTerminalAndEvasion(SearchWorkspace& workspace) {
    // These reported fixtures contain a Queen, so neither is a dead-material
    // draw. The real selected move must end in checkmate for the responding side.
    for (const char* fen : {
             "7k/5Q2/6K1/8/8/8/8/8 w - - 0 1",
             "8/8/8/8/8/6k1/5q2/7K b - - 0 1",
         }) {
        const auto position = Position(fen);
        Observation observed(position);
        const auto result = workspace.Run(position, Config(observed, 20000U, 2U));
        CheckResult(result, observed);
        auto after = position;
        mcumax_position_undo undo{};
        const bool legal = result.available && mcumax_position_make(&after, result.move, &undo);
        Check(legal && mcumax_position_is_check(&after, after.side) &&
                  !mcumax_position_any_legal(&after),
              "mate-in-one search actually mates for both colors rather than stalemating");
        CheckRestored(position, observed);
    }

    for (const bool mate : {false, true}) {
        const auto position = Position(mate ? "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1"
                                            : "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        Observation observed(position);
        const auto result = workspace.Run(position, Config(observed, 2000U, 3U));
        Check(result.terminal && !result.available && !result.interrupted &&
                  observed.publications == 0U && (mate ? result.score < 0 : result.score == 0),
              "terminal mate and stalemate stay distinct and have no move publication");
        CheckRestored(position, observed);
    }

    const auto position = Position("7k/8/8/8/8/8/8/Kr6 w - - 0 1");
    Check(mcumax_position_is_check(&position, position.side),
          "check-evasion fixture starts with the real mover checked");
    Observation observed(position);
    const auto result = workspace.Run(position, Config(observed, 2000U, 1U));
    CheckResult(result, observed);
    auto after = position;
    mcumax_position_undo undo{};
    const bool legal = result.available && mcumax_position_make(&after, result.move, &undo);
    Check(legal && !mcumax_position_is_check(&after, position.side),
          "checked search returns a real evasion rather than a stand-pat result");
    CheckRestored(position, observed);
}

void CheckDeadMaterialBoundary(SearchWorkspace& workspace) {
    for (const char* fen : {
             "7k/8/8/8/8/8/8/K7 w - - 0 1",
             "7k/8/8/8/8/8/2N5/K7 w - - 0 1",
             "7k/8/8/8/8/8/2B5/K7 w - - 0 1",
             "5b1k/8/8/8/8/8/8/K1B5 w - - 0 1",
         }) {
        const auto position = Position(fen);
        Observation observed(position);
        const auto result = workspace.Run(position, Config(observed, 2000U, 2U));
        Check(result.terminal && !result.available && !result.interrupted &&
                  result.score == 0 && observed.publications == 0U,
              "conservative dead material is a terminal draw without a fabricated move");
        CheckRestored(position, observed);
    }
    for (const char* fen : {
             "7k/8/8/8/8/8/2NN4/K7 w - - 0 1",
             "7k/6n1/8/8/8/8/8/K1B5 w - - 0 1",
             "4b2k/8/8/8/8/8/8/K1B5 w - - 0 1",
         }) {
        const auto position = Position(fen);
        Observation observed(position);
        const auto result = workspace.Run(position, Config(observed, 2000U, 1U));
        Check(!result.terminal && result.available && observed.publications != 0U,
              "two Knights, mixed minors and opposite-color Bishops are not declared dead");
        CheckResult(result, observed);
        CheckRestored(position, observed);
    }
}

uint64_t KeyRestored(mcumax_position& position) {
    const auto before = position;
    const auto singleton = mcumax;
    const auto key = litch_max::classic_chess::PositionKey(position);
    Check(SamePosition(position, before) &&
              std::memcmp(&mcumax, &singleton, sizeof(mcumax)) == 0,
          "identity's legal-EP probes restore every caller byte and legacy state");
    return key;
}

void Apply(mcumax_position& position, const char* uci) {
    const mcumax_position_move move{
        static_cast<uint8_t>(('8' - uci[1]) * 16 + uci[0] - 'a'),
        static_cast<uint8_t>(('8' - uci[3]) * 16 + uci[2] - 'a'), 0U};
    mcumax_position_undo undo{};
    Check(mcumax_position_make(&position, move, &undo),
          "identity regression follows a real legal rule sequence");
}

void CheckPositionIdentity() {
    auto position = Position("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    const auto initial = KeyRestored(position);
    for (const char* move : {"e1f1", "e8f8", "f1e1", "f8e8"}) Apply(position, move);
    const auto a = KeyRestored(position);
    Check(initial != a, "lost castling rights change identity although occupancy is restored");
    for (const char* move : {"a1a2", "e8f8", "a2a1", "f8e8"}) Apply(position, move);
    const auto b = KeyRestored(position);
    for (const char* move : {"a1a2", "e8f8", "a2a1", "f8e8"}) Apply(position, move);
    const auto c = KeyRestored(position);
    Check(a == b && b == c,
          "irrelevant first-Rook-move history cannot delay the third occurrence after King rights are lost");

    auto rights = Position("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    const auto allRights = KeyRestored(rights);
    for (const char* fen : {
             "r3k2r/8/8/8/8/8/8/R3K2R w Qkq - 0 1",
             "r3k2r/8/8/8/8/8/8/R3K2R w Kkq - 0 1",
             "r3k2r/8/8/8/8/8/8/R3K2R w KQq - 0 1",
             "r3k2r/8/8/8/8/8/8/R3K2R w KQk - 0 1",
         }) {
        auto missingRight = Position(fen);
        Check(KeyRestored(missingRight) != allRights,
              "each of the four available castling rights remains meaningful in identity");
    }
    auto ordinary = Position("4k3/8/8/3p4/8/8/8/4K3 w - - 0 1");
    const auto withoutEp = KeyRestored(ordinary);
    auto inactiveEp = Position("4k3/8/8/3p4/8/8/8/4K3 w - d6 0 1");
    Check(KeyRestored(inactiveEp) == withoutEp,
          "an EP target without a legal capture cannot prevent repetition");
    auto legalEp = Position("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    const auto legalEpKey = KeyRestored(legalEp);
    auto noLegalEp = Position("4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1");
    Check(KeyRestored(noLegalEp) != legalEpKey,
          "an actually legal EP capture remains meaningful in identity");
    auto pinnedEp = Position("4r1k1/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    const auto pinnedEpKey = KeyRestored(pinnedEp);
    auto noPinnedEp = Position("4r1k1/8/8/3pP3/8/8/8/4K3 w - - 0 1");
    Check(KeyRestored(noPinnedEp) == pinnedEpKey,
          "a pinned EP capture does not create a legal identity distinction");
    std::printf("[CLASSIC_IDENTITY] rights_present=%llu rights_lost_A=%llu B=%llu C=%llu\n",
                static_cast<unsigned long long>(initial), static_cast<unsigned long long>(a),
                static_cast<unsigned long long>(b), static_cast<unsigned long long>(c));
}

void CheckPvsCancellation(SearchWorkspace& workspace) {
    // Real progress/stop callbacks cancel an active deeper scout. A bounded
    // exact-work sweep also catches a short in-window full re-search between
    // the first and second publications; it cannot hide in the128-node stop
    // polling quantum. Passive counters establish which call was interrupted.
    for (const auto mode : {CacheMode::Disabled, CacheMode::MoveOrder, CacheMode::Bounds}) {
        const auto position = Position(litch_max::INITIAL_FEN);
        Observation observed(position);
        observed.cancelAfterDepth = 2U;
        observed.extraNodesBeforeCancel = 17U;
        const auto canceled = workspace.Run(position, Config(observed, 50000U, 8U, 1U, mode, true));
        Check(observed.cancelRequested && canceled.available && canceled.interrupted &&
                  canceled.scoutInterruptions != 0U,
              "real stop-hook cancellation interrupts an active deeper scout after committed work");
        CheckResult(canceled, observed);
        CheckRestored(position, observed);
        std::printf("[CLASSIC_PVS_CANCEL] mode=%u nodes=%u retained_depth=%u "
                    "scout_interruptions=%u research_interruptions=%u\n",
                    static_cast<unsigned>(mode), canceled.nodes, canceled.completedDepth,
                    canceled.scoutInterruptions, canceled.pvResearchInterruptions);

        Observation first(position);
        const auto completeFirst = workspace.Run(position, Config(first, 0U, 1U, 1U, mode, true));
        Observation second(position);
        const auto completeSecond = workspace.Run(position, Config(second, 0U, 2U, 1U, mode, true));
        CheckResult(completeFirst, first);
        CheckResult(completeSecond, second);
        CheckRestored(position, first);
        CheckRestored(position, second);
        bool researchStopped = false;
        uint32_t attempts = 0U;
        for (uint32_t cap = completeFirst.nodes + 1U;
             cap < completeSecond.nodes && cap <= completeFirst.nodes + 256U;
             ++cap) {
            Observation partial(position);
            const auto result = workspace.Run(position, Config(partial, cap, 8U, 1U, mode, true));
            Check(result.available && result.interrupted && result.nodes <= cap,
                  "exact work bound preserves the prior completed root while deeper work is unfinished");
            CheckResult(result, partial);
            CheckRestored(position, partial);
            ++attempts;
            if (result.pvResearchInterruptions == 0U) continue;
            researchStopped = true;
            Check(result.completedDepth == completeFirst.completedDepth &&
                      result.score == completeFirst.score && partial.publications == 1U,
                  "aborted full re-search cannot publish or replace its earlier complete score");
            Observation hintFloor(position);
            const auto hint = workspace.Run(position, Config(hintFloor, cap, 8U, 2U, mode, true));
            Check(hint.interrupted && !hint.available && hintFloor.publications == 0U &&
                      hint.pvResearchInterruptions != 0U,
                  "the same aborted re-search cannot promote one-ply work to an eligible Hint");
            CheckRestored(position, hintFloor);
            std::printf("[CLASSIC_PVS_RESEARCH_ABORT] mode=%u cap=%u nodes=%u retained_depth=%u "
                        "publications=%u research_interruptions=%u hint_available=%u hint_publications=%u\n",
                        static_cast<unsigned>(mode), cap, result.nodes, result.completedDepth,
                        partial.publications, result.pvResearchInterruptions,
                        static_cast<unsigned>(hint.available), hintFloor.publications);
            break;
        }
        Check(researchStopped, "each cache mode really interrupts an in-window full re-search");
        std::printf("[CLASSIC_PVS_CANCEL_COVERAGE] mode=%u attempts=%u scout=%u research=%u\n",
                    static_cast<unsigned>(mode), attempts,
                    static_cast<unsigned>(canceled.scoutInterruptions != 0U),
                    static_cast<unsigned>(researchStopped));
    }
}

void CheckStaleKillerPromotions() {
    using litch_max::classic_chess::internal::MoveOrder;
    for (const bool black : {false, true}) {
        const auto previous = Position(black ? "4k3/8/8/8/8/8/2r5/4K3 b - - 0 1" :
                                              "4k3/2R5/8/8/8/8/8/4K3 w - - 0 1");
        MoveOrder order;
        order.Begin();
        mcumax_position_move move{};
        const uint8_t from = black ? 98U : 18U;
        const uint8_t to = black ? 114U : 2U;
        bool primed = false;
        while (order.Next(previous, move)) {
            if (move.from != from || move.to != to || move.promotion != 0U) continue;
            auto applied = previous;
            mcumax_position_undo undo{};
            if (!mcumax_position_make(&applied, move, &undo)) continue;
            order.RememberKiller(move);
            primed = true;
            break;
        }
        Check(primed, "a legal nonpawn sibling supplies the quiet back-rank killer");
        const auto position = Position(black ? "4k3/8/8/8/8/8/2p5/4K3 b - - 0 1" :
                                              "4k3/2P5/8/8/8/8/8/4K3 w - - 0 1");
        Observation observed(position);
        order.Begin();
        uint8_t promotions[8]{};
        uint8_t implicit = 0U;
        while (order.Next(position, move)) {
            auto applied = position;
            mcumax_position_undo undo{};
            if (!mcumax_position_make(&applied, move, &undo) || move.from != from || move.to != to) continue;
            if (move.promotion == 0U) ++implicit;
            else ++promotions[move.promotion];
        }
        Check(implicit == 0U && promotions[MCUMAX_QUEEN] == 1U && promotions[MCUMAX_ROOK] == 1U &&
                  promotions[MCUMAX_BISHOP] == 1U && promotions[MCUMAX_KNIGHT] == 1U,
              "a pawn sibling emits exactly four explicit promotions, without a duplicate quiet Queen");
        CheckRestored(position, observed);
    }
}

void CheckCacheEquivalence(SearchWorkspace& workspace) {
    struct Case {
        const char* name;
        const char* fen;
        uint8_t depth;
        bool requiresBoundReuse = false;
        bool requiresReversibleRefusal = false;
    };
    // Independent general rules/search fixtures, not the four strength witnesses.
    // True score ties may order differently; every mode must preserve full-search
    // root value, completed depth, terminal outcome and actual move legality.
    constexpr Case CASES[] = {
        {"initial", litch_max::INITIAL_FEN, 3U},
        {"reversible_knight_cycle", "4k1n1/8/8/8/8/8/8/4K1N1 w - - 0 1", 8U},
        {"legal_en_passant", "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", 4U},
        {"pinned_en_passant", "4r1k1/8/8/3pP3/8/8/8/4K3 w - d6 0 1", 4U},
        {"castling_rights", "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", 3U},
        {"quiet_promotion", "7k/P7/8/8/8/8/8/7K w - - 0 1", 3U},
        {"irreversible_pawn_interleaving", "7k/3pp3/8/8/8/8/3PP3/K7 w - - 0 1", 6U, true, true},
        {"mixed_reversible_interleaving", "4k1n1/3pp3/8/8/8/8/3PP3/4K1N1 w - - 0 1", 6U, true, true},
    };
    uint32_t researchCoverage[3]{};
    for (const auto& test : CASES) {
        const auto position = Position(test.fen);
        SearchResult control{};
        for (const auto mode : {CacheMode::Disabled, CacheMode::MoveOrder, CacheMode::Bounds}) {
            for (const bool pvs : {false, true}) {
                Observation observed(position);
                auto request = Config(observed, 0U, test.depth, 1U, mode, pvs);
                // Minimax equivalence isolates cache/PVS from the selective
                // null-pass and late-quiet policies used by production.
                request.nullMovePruning = false;
                request.lateQuietReduction = false;
                const auto result = workspace.Run(position, request);
                Check(result.available && !result.interrupted && result.completedDepth == test.depth,
                      "cache/PVS comparison completes the requested genuine fixed depth without a work cap");
                CheckResult(result, observed);
                CheckRestored(position, observed);
                if (mode == CacheMode::Disabled && !pvs) control = result;
                else Check(result.score == control.score && result.terminal == control.terminal &&
                               result.available == control.available && result.completedDepth == control.completedDepth,
                           "all cache and PVS modes preserve uncached plain full-depth score and outcome");
                if (mode == CacheMode::Disabled) {
                    Check(result.cacheOrderHits == 0U && result.cacheCutoffs == 0U &&
                              result.reversibleBoundRefusals == 0U,
                          "disabled cache cannot accidentally order from or reuse a cached result");
                } else if (mode == CacheMode::MoveOrder) {
                    Check(result.cacheOrderHits != 0U && result.cacheCutoffs == 0U &&
                              result.reversibleBoundRefusals == 0U,
                          "move-order control really uses stored moves without reusing any bound");
                } else {
                    if (test.requiresBoundReuse) {
                        Check(result.cacheCutoffs != 0U,
                              "convergent irreversible paths actually reuse a completed cached bound");
                    }
                    if (test.requiresReversibleRefusal) {
                        Check(result.reversibleBoundRefusals != 0U,
                              "a matching stored bound is refused after a reversible entry move");
                    }
                    if (std::strcmp(test.name, "reversible_knight_cycle") == 0) {
                        Check(result.cacheOrderHits != 0U && result.cacheCutoffs == 0U,
                              "reversible cycles may reorder moves but cannot reuse path-sensitive bounds");
                    }
                }
                if (pvs) {
                    Check(result.scoutSearches != 0U, "PVS control really uses narrow scouts");
                    researchCoverage[static_cast<unsigned>(mode)] += result.pvResearches;
                } else {
                    Check(result.scoutSearches == 0U && result.pvResearches == 0U &&
                              result.scoutInterruptions == 0U && result.pvResearchInterruptions == 0U,
                          "plain reference performs full searches without silently entering PVS");
                }
                std::printf("[CLASSIC_CACHE] case=%s mode=%u pvs=%u depth=%u score=%d nodes=%u horizon=%u "
                            "order_hits=%u cutoffs=%u reversible_refusals=%u scouts=%u researches=%u\n",
                            test.name, static_cast<unsigned>(mode), static_cast<unsigned>(pvs),
                            result.completedDepth, result.score, result.nodes, result.horizonNodes,
                            result.cacheOrderHits, result.cacheCutoffs, result.reversibleBoundRefusals,
                            result.scoutSearches, result.pvResearches);
            }
        }
    }

    for (const auto mode : {CacheMode::Disabled, CacheMode::MoveOrder, CacheMode::Bounds}) {
        Check(researchCoverage[static_cast<unsigned>(mode)] != 0U,
              "each PVS cache mode actually exercises an in-window full re-search");
        for (const bool pvs : {false, true}) {
            const auto position = Position("8/k1P5/2K5/8/8/8/8/8 w - - 0 1");
            Observation observed(position);
            const auto result = workspace.Run(position, Config(observed, 0U, 1U, 1U, mode, pvs));
            CheckResult(result, observed);
            Check(result.move.promotion == MCUMAX_ROOK,
                  "all cache/PVS modes prefer a live Rook promotion to immediate Queen stalemate");
            auto after = position;
            mcumax_position_undo undo{};
            Check(mcumax_position_make(&after, result.move, &undo) && mcumax_position_any_legal(&after),
                  "selected underpromotion leaves the responder a real legal move");
            CheckRestored(position, observed);
            CheckCancelAndReuse(workspace, mode, pvs);

            const auto reentered = Position(litch_max::INITIAL_FEN);
            Observation clean(reentered);
            const auto fresh = workspace.Run(reentered, Config(clean, 0U, 2U, 1U, mode, pvs));
            SearchWorkspace separate;
            Observation independent(reentered);
            const auto independentResult = separate.Run(reentered, Config(independent, 0U, 2U, 1U, mode, pvs));
            Check(fresh.available && independentResult.available && SameMove(fresh.move, independentResult.move) &&
                      fresh.score == independentResult.score && fresh.nodes == independentResult.nodes &&
                      fresh.completedDepth == independentResult.completedDepth,
                  "promotion, previous roots and canceled work cannot contaminate a new search cache");
            CheckResult(fresh, clean);
            CheckResult(independentResult, independent);
            CheckRestored(reentered, clean);
            CheckRestored(reentered, independent);
        }
    }
    CheckPvsCancellation(workspace);
}

} // namespace

int main() {
    g_failures = 0;
    Check(SearchWorkspace::RequiredBytes() > 0U &&
              SearchWorkspace::RequiredBytes() <= MAX_WORKSPACE_BYTES,
          "search scratch stays within the fixed workspace budget");
    SearchWorkspace workspace;
    CheckBudgetsAndEligibility(workspace);
    CheckCancelAndReuse(workspace);
    CheckTerminalAndEvasion(workspace);
    CheckDeadMaterialBoundary(workspace);
    CheckPositionIdentity();
    CheckStaleKillerPromotions();
    CheckQuietGenerationScreens();
    CheckVariantWorkspaceReuse(workspace);
    CheckFixedWorkspaceSearches();
    CheckCacheEquivalence(workspace);
    std::printf("[CLASSIC_SEARCH] failures=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
