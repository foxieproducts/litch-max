#include "litch_max/classic_chess_search.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <type_traits>
#include <new>

#include "litch_max/classic_chess_evaluation.hpp"
#include "litch_max/classic_chess_kpk.hpp"
#include "classic_chess_move_order.hpp"
#include "classic_chess_endgame_evaluation.hpp"
#include "classic_chess_repetition.hpp"
#include "classic_chess_quiescence_cache.hpp"
#include "duck_chess_search_workspace.hpp"

namespace litch_max::classic_chess {
namespace {
constexpr uint8_t MAX_PLY = 32U;
static_assert(MAX_PLY <= 32U, "Tactical cache tags retain five ply bits");
constexpr int MATE = 30000;
constexpr int INFINITY_SCORE = 31000;
constexpr mcumax_position_move INVALID{MCUMAX_SQUARE_INVALID, MCUMAX_SQUARE_INVALID, 0U};

using internal::IsCapture;

char PromotionChar(const uint8_t piece) {
    switch (piece) {
        case MCUMAX_QUEEN: return 'q';
        case MCUMAX_ROOK: return 'r';
        case MCUMAX_BISHOP: return 'b';
        case MCUMAX_KNIGHT: return 'n';
        default: return '\0';
    }
}

enum class Bound : uint8_t { None, Exact, Lower, Upper };
struct CacheEntry {
    uint64_t key = 0U;
    int16_t score = 0;
    mcumax_position_move move = INVALID;
    uint8_t depth = 0U;
    uint8_t ply = 0U;
    Bound bound = Bound::None;
};
static_assert(sizeof(CacheEntry) == 16U, "Classic cache entries must stay compact");
// Leave internal RAM for BLE discovery and persistence during an active search.
constexpr size_t CACHE_CAPACITY = 512U;
struct Frame {
    mcumax_position_undo undo{};
    mcumax_position_move move = INVALID;
    mcumax_position_move best = INVALID;
    internal::MoveOrder order{};
};
} // namespace

struct ClassicState {
    mcumax_position position{};
    SearchConfig config{};
    SearchResult completed{};
    std::array<Frame, MAX_PLY> frames{};
    internal::RepetitionPath<MAX_PLY> repetition;
    std::array<uint16_t, MAX_PLY> halfmoves{};
    std::array<CacheEntry, CACHE_CAPACITY> cache{};
    internal::QuiescenceCache quiescenceCache;
    internal::EvaluationState evaluation;
    uint32_t nodes = 0U;
    uint32_t horizonNodes = 0U;
    bool stopped = false;
    // Nonzero only inside the complete hypothetical-pass subtree. Bounds,
    // move-cache traffic and killer writes are disabled until it unwinds.
    uint8_t hypotheticalRoot = 0U;

    CacheEntry& CacheAt(const uint64_t key) {
        return cache[key & (CACHE_CAPACITY - 1U)];
    }

    bool StopRequested() {
        if (config.shouldStop != nullptr && config.shouldStop(config.stopContext)) stopped = true;
        return stopped;
    }

    bool Visit() {
        if (stopped || (config.maxNodes != 0U && nodes >= config.maxNodes &&
            (completed.completedDepth != 0U || nodes >= config.firstRootNodeReserve))) {
            stopped = true;
            return false;
        }
        ++nodes;
        if (config.cooperate != nullptr) config.cooperate(config.cooperateContext, nodes);
        if ((nodes & 127U) == 0U) {
            config.progressObserver.Publish(nodes);
            StopRequested();
        }
        return !stopped;
    }

    void BeginMoves(Frame& frame, const mcumax_position_move preferred = INVALID) {
        frame.order.Begin(preferred);
        frame.best = INVALID;
    }

    bool MakeChild(Frame& frame, const uint8_t ply) {
        const uint8_t type = position.board[frame.move.from] & 7U;
        const bool zeroing = IsCapture(position, frame.move) ||
            type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM;
        if (!(frame.order.LastMoveWasGenerated()
            ? mcumax_position_make_generated(&position, frame.move, &frame.undo)
            : mcumax_position_make(&position, frame.move, &frame.undo))) return false;
        repetition.MadeChild(position, frame.undo, ply);
        evaluation.MadeChild(position, frame.undo);
        halfmoves[ply + 1U] = zeroing ? 0U : halfmoves[ply] == KPK_HALFMOVE_UNKNOWN
            ? KPK_HALFMOVE_UNKNOWN : std::min<unsigned>(100U, halfmoves[ply] + 1U);
        return true;
    }

    int LeafScore(const uint8_t ply) const {
        return hypotheticalRoot != 0U ? evaluation.Evaluate(position) : internal::EvaluateEndgame(position, halfmoves[ply], evaluation);
    }

    void UnmakeChild(const Frame& frame) {
        evaluation.UnmakingChild(position, frame.undo);
        mcumax_position_unmake(&position, &frame.undo);
    }

    bool FiftyMoveDraw(const uint8_t ply, const bool checked) {
        // A mating move takes precedence over the reversible-clock draw.
        return halfmoves[ply] != KPK_HALFMOVE_UNKNOWN && halfmoves[ply] >= 100U &&
            (!checked || mcumax_position_any_legal(&position));
    }

    int Quiescence(int alpha, const int beta, const uint8_t ply, const uint8_t quietCheckDepth) {
        if (!Visit()) return 0;
        if (repetition.Repeated(ply, hypotheticalRoot) || internal::DeadMaterial(position)) return 0;
        const bool checked = mcumax_position_is_check(&position, position.side);
        if (FiftyMoveDraw(ply, checked)) return 0;
        if (ply == MAX_PLY - 1U) {
            if (!mcumax_position_any_legal(&position)) return checked ? -MATE + ply : 0;
            ++horizonNodes;
            return LeafScore(ply);
        }
        const bool cacheable = config.quiescenceCache && hypotheticalRoot == 0U && halfmoves[ply] == 0U &&
                               config.cacheMode == CacheMode::Bounds;
        const auto key = repetition.Key(ply);
        const int originalAlpha = alpha;
        int cachedScore = 0;
        if (cacheable && quiescenceCache.Probe(key, ply, quietCheckDepth == 0U, alpha, beta, cachedScore)) return cachedScore;
        const auto finish = [&](const int score) {
            if (cacheable) quiescenceCache.Store(key, ply, quietCheckDepth == 0U, score, originalAlpha, beta);
            return score;
        };
        int best = checked ? -INFINITY_SCORE : LeafScore(ply);
        if (!checked) {
            if (best >= beta) return finish(mcumax_position_any_legal(&position) ? best : 0);
            alpha = std::max(alpha, best);
        }
        Frame& frame = frames[ply];
        const auto recapture = config.recaptureOrdering && ply != 0U && hypotheticalRoot == 0U
            ? internal::RecapturePreference(position, frames[ply - 1U].undo, frames[ply - 1U].move)
            : INVALID;
        BeginMoves(frame, recapture);
        bool legal = false;
        const auto selection = !checked && quietCheckDepth != 0U
            ? internal::MoveSelection::Tactical : internal::MoveSelection::All;
        // Imported roots and hypothetical-pass branches need not establish
        // the legal-game invariant that the previous mover is unchecked.
        const bool opponentChecked = !checked && quietCheckDepth == 0U &&
            mcumax_position_is_check(&position, position.side ^ 24U);
        while (frame.order.Next(position, frame.move, selection)) {
            const bool tactical = IsCapture(position, frame.move) || frame.move.promotion != 0U;
            if (!checked && !tactical && quietCheckDepth != 0U) continue;
            if (!checked && !tactical && !opponentChecked &&
                !internal::MayCreateQuietCheck(position, frame.move)) continue;
            if (!MakeChild(frame, ply)) continue;
            legal = true;
            if (!checked && !tactical && !mcumax_position_is_check(&position, position.side)) {
                UnmakeChild(frame);
                continue;
            }
            const int score = -Quiescence(-beta, -alpha, ply + 1U, quietCheckDepth + 1U);
            UnmakeChild(frame);
            if (stopped) return 0;
            best = std::max(best, score);
            alpha = std::max(alpha, score);
            if (alpha >= beta) break;
        }
        // Made moves prove legality; excluded quiet moves can still need a probe.
        if (!legal && (checked || !mcumax_position_any_legal(&position)))
            return finish(checked ? -MATE + ply : 0);
        return finish(best);
    }

    // Include a reply without capture extensions; keep its heuristic leaves
    // out of the Full search cache and ordering.
    int Preliminary(int alpha, const int beta, const uint8_t ply, const uint8_t limit) {
        if (!Visit() || repetition.Repeated(ply, hypotheticalRoot) || internal::DeadMaterial(position)) return 0;
        const bool checked = mcumax_position_is_check(&position, position.side);
        if (FiftyMoveDraw(ply, checked)) return 0;
        if (ply == limit) {
            if (!mcumax_position_any_legal(&position)) return checked ? -MATE + ply : 0;
            ++horizonNodes;
            return LeafScore(ply);
        }
        Frame& frame = frames[ply];
        BeginMoves(frame, ply == 0U ? config.rootPreferred : INVALID);
        int best = -INFINITY_SCORE;
        bool legal = false;
        while (frame.order.Next(position, frame.move)) {
            if (StopRequested()) return 0;
            if (!MakeChild(frame, ply)) continue;
            const int score = -Preliminary(-beta, -alpha, ply + 1U, limit);
            UnmakeChild(frame);
            if (StopRequested()) return 0;
            legal = true;
            if (score > best) { best = score; frame.best = frame.move; }
            alpha = std::max(alpha, score);
            if (alpha >= beta) break;
        }
        return legal ? best : (checked ? -MATE + ply : 0);
    }

    void Store(const uint64_t key, const Frame& frame, const int score,
               const int alpha, const int beta, const uint8_t depth,
               const uint8_t ply, const bool independentHistory) {
        if (hypotheticalRoot != 0U || config.cacheMode == CacheMode::Disabled || frame.best.from == MCUMAX_SQUARE_INVALID) return;
        auto& entry = CacheAt(key);
        entry.key = key;
        entry.move = frame.best;
        entry.depth = depth;
        entry.ply = ply;
        entry.score = static_cast<int16_t>(score);
        // A previous reversible path can change a future repetition draw.
        // Cache bounds only after a pawn move/capture makes that path unreachable
        // and resets its clock to0. At root the copied clock is unchanged across
        // iterations; the entire cache is cleared for every Run request.
        entry.bound = independentHistory ? (score <= alpha ? Bound::Upper :
            (score >= beta ? Bound::Lower : Bound::Exact)) : Bound::None;
    }

    bool NullMaterialEligible() const {
        uint8_t nonPawnPieces = 0U;
        bool pawn = false;
        for (uint8_t square = 0U; square < 128U; ++square) {
            if ((square & 0x88U) != 0U) continue;
            const uint8_t piece = position.board[square];
            if ((piece & 24U) != position.side) continue;
            const uint8_t type = piece & 7U;
            if (type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM) pawn = true;
            else if (type != 0U && type != MCUMAX_KING) ++nonPawnPieces;
        }
        return pawn && nonPawnPieces >= 2U;
    }

    bool TryNullCutoff(const int alpha, const int beta, const uint8_t depth,
                       const uint8_t ply, const bool checked) {
        if (!config.nullMovePruning || hypotheticalRoot != 0U || ply == 0U ||
            beta != alpha + 1 || depth < 3U || checked ||
            std::abs(beta) >= MATE - MAX_PLY || !NullMaterialEligible() ||
            evaluation.Evaluate(position) < beta || !mcumax_position_any_legal(&position)) return false;

        const uint8_t oldSide = position.side;
        const uint8_t oldEp = position.en_passant;
        position.side ^= 24U;
        position.en_passant = MCUMAX_SQUARE_INVALID;
        repetition.MadeNullChild(position, ply);
        // A pass is not part of legal history. Post-pass repetition cannot
        // compare with the real prefix, and no nested pass is allowed here.
        hypotheticalRoot = ply + 1U;
        halfmoves[ply + 1U] = KPK_HALFMOVE_UNKNOWN;
        constexpr uint8_t REDUCTION = 2U;
        const int score = -Search(-beta, -beta + 1, depth - 1U - REDUCTION, ply + 1U, false);
        hypotheticalRoot = 0U;
        position.side = oldSide;
        position.en_passant = oldEp;
        if (stopped) return false;
        // A hypothetical mate score never becomes an advertised result.
        // Only the finite lower bound beta may escape this selective probe.
        return score >= beta && std::abs(score) < MATE - MAX_PLY;
    }

    int Search(int alpha, const int beta, const uint8_t depth, const uint8_t ply,
               const bool independentHistory) {
        if (depth == 0U) return Quiescence(alpha, beta, ply, 0U);
        if (!Visit()) return 0;
        if (repetition.Repeated(ply, hypotheticalRoot) || internal::DeadMaterial(position)) return 0;
        const bool checked = mcumax_position_is_check(&position, position.side);
        if (FiftyMoveDraw(ply, checked)) return 0;
        if (ply == MAX_PLY - 1U) {
            ++horizonNodes;
            if (!mcumax_position_any_legal(&position)) return checked ? -MATE + ply : 0;
            return LeafScore(ply);
        }
        Frame& frame = frames[ply];
        const uint64_t key = hypotheticalRoot == 0U ? PositionKey(position) : 0U;
        const CacheEntry entry = hypotheticalRoot == 0U ? CacheAt(key) : CacheEntry{};
        const bool cached = hypotheticalRoot == 0U && config.cacheMode != CacheMode::Disabled && entry.key == key;
        const auto preferred = ply == 0U && completed.completedDepth != 0U && !completed.preliminaryOnly ? completed.move :
            (cached ? entry.move : (ply == 0U ? config.rootPreferred : INVALID));
        BeginMoves(frame, preferred);
        if (cached) ++completed.cacheOrderHits;
        if (config.cacheMode == CacheMode::Bounds && cached && !independentHistory &&
            entry.depth == depth && entry.ply == ply && entry.bound != Bound::None) {
            ++completed.reversibleBoundRefusals;
        }
        if (config.cacheMode == CacheMode::Bounds && independentHistory && cached &&
            entry.depth == depth && entry.ply == ply &&
            (entry.bound == Bound::Exact || (entry.bound == Bound::Lower && entry.score >= beta) ||
             (entry.bound == Bound::Upper && entry.score <= alpha))) {
            frame.best = entry.move;
            ++completed.cacheCutoffs;
            return entry.score;
        }
        if (TryNullCutoff(alpha, beta, depth, ply, checked)) return beta;
        if (stopped) return 0;
        const int originalAlpha = alpha;
        int best = -INFINITY_SCORE;
        bool legal = false;
        uint16_t searchedMoves = 0U;
        while (frame.order.Next(position, frame.move)) {
            const bool capture = IsCapture(position, frame.move);
            const bool quiet = !capture && frame.move.promotion == 0U;
            const uint8_t piece = position.board[frame.move.from] & 7U;
            const bool irreversible = capture || piece == MCUMAX_PAWN_UPSTREAM || piece == MCUMAX_PAWN_DOWNSTREAM;
            if (!MakeChild(frame, ply)) continue;
            ++searchedMoves;
            const bool reduce = config.lateQuietReduction && ply != 0U && depth >= 3U &&
                searchedMoves > 4U && quiet && !checked && !mcumax_position_is_check(&position, position.side) &&
                (piece != MCUMAX_QUEEN ||
                 !internal::EscapesCheaperCapture(position, frame.move, frame.undo));
            int score = alpha + 1;
            if (reduce) {
                score = -Search(-alpha - 1, -alpha, depth - 2U, ply + 1U, irreversible);
                if (stopped) {
                    UnmakeChild(frame);
                    return 0;
                }
            }
            if (reduce && score <= alpha) {
                // A reduced fail-low stays selective. Alpha improvement must
                // repeat this same made child at its full requested depth.
            } else if (config.principalVariationSearch && legal && beta > alpha + 1) {
                // A scout proves whether this alternative can improve alpha.
                // An in-window improvement needs its full search before it
                // can own the root result. Both calls share one made position.
                ++completed.scoutSearches;
                score = -Search(-alpha - 1, -alpha, depth - 1U, ply + 1U, irreversible);
                if (stopped) ++completed.scoutInterruptions;
                if (!stopped && score > alpha && score < beta) {
                    ++completed.pvResearches;
                    score = -Search(-beta, -alpha, depth - 1U, ply + 1U, irreversible);
                    if (stopped) ++completed.pvResearchInterruptions;
                }
            } else {
                score = -Search(-beta, -alpha, depth - 1U, ply + 1U, irreversible);
            }
            legal = true;
            UnmakeChild(frame);
            if (stopped) return 0;
            if (score > best) {
                best = score;
                frame.best = frame.move;
            }
            alpha = std::max(alpha, score);
            if (alpha >= beta) {
                if (hypotheticalRoot == 0U && quiet) frame.order.RememberKiller(frame.move);
                break;
            }
        }
        if (legal) Store(key, frame, best, originalAlpha, beta, depth, ply, independentHistory);
        return legal ? best : (checked ? -MATE + ply : 0);
    }

    SearchResult Run(const mcumax_position& root, const SearchConfig& request) {
        position = root;
        config = request;
        evaluation.Reset(position);
        completed = SearchResult{};
        frames.fill(Frame{});
        halfmoves.fill(KPK_HALFMOVE_UNKNOWN);
        halfmoves[0] = request.reversibleHalfmoves;
        repetition.Reset(position);
        nodes = 0U;
        horizonNodes = 0U;
        stopped = false;
        hypotheticalRoot = 0U;
        const uint8_t depthLimit = std::min<uint8_t>(request.maxDepth, MAX_PLY - 1U);
        const uint8_t preliminaryDepth = std::min<uint8_t>(2U, depthLimit);
        bool preliminaryRoot = (request.staticLeafOnly || request.preliminaryRoot || request.firstRootNodeReserve != 0U) &&
            request.minimumDepth <= 1U && depthLimit != 0U;
        for (uint8_t depth = 1U; depth <= depthLimit && !StopRequested();) {
            const int score = preliminaryRoot ? Preliminary(-INFINITY_SCORE, INFINITY_SCORE, 0U, preliminaryDepth) :
                Search(-INFINITY_SCORE, INFINITY_SCORE, depth, 0U, true);
            if (stopped || (preliminaryRoot && StopRequested())) break;
            if (frames[0].best.from == MCUMAX_SQUARE_INVALID) {
                completed.terminal = true;
                completed.score = static_cast<int16_t>(score);
                break;
            }
            completed.move = frames[0].best;
            completed.score = static_cast<int16_t>(score);
            completed.completedDepth = preliminaryRoot ? preliminaryDepth : depth;
            completed.preliminaryOnly = preliminaryRoot;
            completed.available = depth >= request.minimumDepth;
            config.progressObserver.Publish(nodes);
            {
                ThinkCandidate candidate;
                candidate.fromSquare = (completed.move.from >> 4U) * 8U + (completed.move.from & 7U);
                candidate.toSquare = (completed.move.to >> 4U) * 8U + (completed.move.to & 7U);
                candidate.promotion = PromotionChar(completed.move.promotion);
                candidate.completedDepth = completed.completedDepth;
                candidate.rootKind = preliminaryRoot ? ThinkRootKind::ClassicPreliminary : ThinkRootKind::ClassicFull;
                candidate.score = completed.score;
                candidate.scoreValid = true;
                candidate.scoreUnits = ThinkScoreUnits::ClassicPawn100;
                candidate.scorePerspective = root.side == MCUMAX_BOARD_WHITE ? SideToMove::White : SideToMove::Black;
                candidate.completedNodes = nodes;
                config.candidateObserver.RootCompleted(candidate);
                if (completed.available) config.candidateObserver.Publish(candidate);
            }
            if (request.staticLeafOnly || StopRequested() ||
                (completed.available && std::abs(score) >= MATE - MAX_PLY)) break;
            if (preliminaryRoot) preliminaryRoot = false;
            else ++depth;
        }
        if (!completed.available) completed.move = INVALID;
        completed.nodes = nodes;
        completed.horizonNodes = horizonNodes;
        completed.interrupted = stopped;
        config.progressObserver.Publish(nodes);
        return completed;
    }
};

SearchWorkspace::SearchWorkspace() = default;
SearchWorkspace::~SearchWorkspace() = default;

SearchResult SearchWorkspace::Run(const mcumax_position& position, const SearchConfig& config) {
    static_assert(sizeof(ClassicState) <= sizeof(m_storage));
    static_assert(alignof(ClassicState) <= alignof(SearchWorkspace));
    static_assert(std::is_trivially_destructible_v<ClassicState>);
    auto* state = ::new (m_storage) ClassicState{};
    return state->Run(position, config);
}

bool SearchWorkspace::RunDuck(const char* fen, uint8_t duck, const DuckChessSearchConfig& config,
                              DuckChessTurn& outTurn, DuckChessSearchStats& stats,
                              bool (*cancel)(void*), void* cancelContext) {
    using duck_chess_search_internal::SearchState;
    static_assert(sizeof(SearchState) <= sizeof(m_storage));
    static_assert(alignof(SearchState) <= alignof(SearchWorkspace));
    return duck_chess_search_internal::FindTurn(m_storage, fen, duck, config, outTurn, stats,
                                               cancel, cancelContext);
}

bool SearchWorkspace::RunDuckPlacement(const char* fen, uint8_t duck,
                                       const DuckChessSearchConfig& config, uint8_t& destination,
                                       DuckChessSearchStats& stats, bool (*cancel)(void*), void* cancelContext) {
    return FindBestDuckPlacement(fen, duck, config, destination, stats, cancel, cancelContext);
}

size_t SearchWorkspace::RequiredBytes() { return sizeof(ClassicState); }
size_t SearchWorkspace::DuckRequiredBytes() {
    return sizeof(duck_chess_search_internal::SearchState);
}

} // namespace litch_max::classic_chess
