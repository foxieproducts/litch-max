#include "modules/classic_chess_search.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <new>

#include <mcu-max-platform.h>

#include "modules/classic_chess_evaluation.hpp"
#include "classic_chess_move_order.hpp"
#include "duck_chess_search_workspace.hpp"

namespace modules::classic_chess {
namespace {
constexpr uint8_t MAX_PLY = 32U;
constexpr int MATE = 30000;
constexpr int INFINITY_SCORE = 31000;
constexpr mcumax_position_move INVALID{MCUMAX_SQUARE_INVALID, MCUMAX_SQUARE_INVALID, 0U};

using internal::IsCapture;

// These subsets cannot mate by any legal sequence. Two knights, opposite-
// color bishops and mixed minors are deliberately not declared dead.
bool DeadMaterial(const mcumax_position& position) {
    uint8_t knights = 0U;
    uint8_t bishops = 0U;
    uint8_t bishopColors = 0U;
    for (uint8_t square = 0U; square < 128U; ++square) {
        if ((square & 0x88U) != 0U) continue;
        const uint8_t type = position.board[square] & 7U;
        if (type == 0U || type == MCUMAX_KING) continue;
        if (type == MCUMAX_KNIGHT) ++knights;
        else if (type == MCUMAX_BISHOP) {
            ++bishops;
            bishopColors |= 1U << (((square >> 4U) + (square & 7U)) & 1U);
        } else return false;
    }
    return (bishops == 0U && knights <= 1U) ||
        (knights == 0U && bishopColors != 3U);
}

uint8_t EffectiveEnPassant(mcumax_position& position) {
    if (position.en_passant == MCUMAX_SQUARE_INVALID) return MCUMAX_SQUARE_INVALID;
    const int behind = position.en_passant + (position.side == MCUMAX_BOARD_WHITE ? 16 : -16);
    for (const int offset : {-1, 1}) {
        const int from = behind + offset;
        if (from < 0 || from >= 128 || (from & 0x88) != 0) continue;
        const uint8_t type = position.board[from] & 7U;
        if (type != MCUMAX_PAWN_UPSTREAM && type != MCUMAX_PAWN_DOWNSTREAM) continue;
        mcumax_position_undo undo{};
        const mcumax_position_move move{static_cast<uint8_t>(from), position.en_passant, 0U};
        if (mcumax_position_make(&position, move, &undo)) {
            mcumax_position_unmake(&position, &undo);
            return position.en_passant;
        }
    }
    return MCUMAX_SQUARE_INVALID;
}

} // namespace

// Royal moved flags matter only through the four actual castling rights.
// Home-pawn double-step state and effective en-passant remain part of identity.
uint64_t PositionKey(mcumax_position& position) {
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
    return (key ^ EffectiveEnPassant(position)) * 1099511628211ULL;
}

namespace {
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
constexpr size_t CACHE_CAPACITY = 1024U;

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
    std::array<uint64_t, MAX_PLY> path{};
    std::array<CacheEntry, CACHE_CAPACITY> cache{};
    uint32_t nodes = 0U;
    uint32_t horizonNodes = 0U;
    bool stopped = false;
    // Nonzero only inside the complete hypothetical-pass subtree. Bounds,
    // move-cache traffic and killer writes are disabled until it unwinds.
    uint8_t hypotheticalRoot = 0U;

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
        MCUMAX_PLATFORM_COOPERATE(nodes);
        if ((nodes & 127U) == 0U) {
            config.progressObserver.Publish(nodes);
            StopRequested();
        }
        return !stopped;
    }

    bool Repeated(const uint8_t ply) {
        path[ply] = PositionKey(position);
        uint8_t count = 0U;
        for (int previous = static_cast<int>(ply) - 2; previous >= hypotheticalRoot; previous -= 2) {
            if (path[previous] == path[ply] && ++count == 2U) return true;
        }
        return false;
    }

    void BeginMoves(Frame& frame, const mcumax_position_move preferred = INVALID) {
        frame.order.Begin(preferred);
        frame.best = INVALID;
    }

    int Quiescence(int alpha, const int beta, const uint8_t ply, const uint8_t quietCheckDepth) {
        if (!Visit()) return 0;
        if (Repeated(ply) || DeadMaterial(position)) return 0;
        const bool checked = mcumax_position_is_check(&position, position.side);
        if (!mcumax_position_any_legal(&position)) return checked ? -MATE + ply : 0;
        if (ply == MAX_PLY - 1U) {
            ++horizonNodes;
            return Evaluate(position);
        }
        int best = checked ? -INFINITY_SCORE : Evaluate(position);
        if (!checked) {
            if (best >= beta) return best;
            alpha = std::max(alpha, best);
        }
        Frame& frame = frames[ply];
        BeginMoves(frame);
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
            if (!mcumax_position_make(&position, frame.move, &frame.undo)) continue;
            if (!checked && !tactical && !mcumax_position_is_check(&position, position.side)) {
                mcumax_position_unmake(&position, &frame.undo);
                continue;
            }
            const int score = -Quiescence(-beta, -alpha, ply + 1U, quietCheckDepth + 1U);
            mcumax_position_unmake(&position, &frame.undo);
            if (stopped) return 0;
            best = std::max(best, score);
            alpha = std::max(alpha, score);
            if (alpha >= beta) break;
        }
        return best;
    }

    // Include a reply without capture extensions; keep its heuristic leaves
    // out of the Full search cache and ordering.
    int Preliminary(int alpha, const int beta, const uint8_t ply, const uint8_t limit) {
        if (!Visit() || Repeated(ply) || DeadMaterial(position)) return 0;
        const bool checked = mcumax_position_is_check(&position, position.side);
        if (ply == limit) {
            if (!mcumax_position_any_legal(&position)) return checked ? -MATE + ply : 0;
            ++horizonNodes;
            return Evaluate(position);
        }
        Frame& frame = frames[ply];
        BeginMoves(frame);
        int best = -INFINITY_SCORE;
        bool legal = false;
        while (frame.order.Next(position, frame.move)) {
            if (StopRequested()) return 0;
            if (!mcumax_position_make(&position, frame.move, &frame.undo)) continue;
            const int score = -Preliminary(-beta, -alpha, ply + 1U, limit);
            mcumax_position_unmake(&position, &frame.undo);
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
        auto& entry = cache[key & (CACHE_CAPACITY - 1U)];
        entry.key = key;
        entry.move = frame.best;
        entry.depth = depth;
        entry.ply = ply;
        entry.score = static_cast<int16_t>(score);
        // A previous reversible path can change a future repetition draw.
        // Cache bounds only after a pawn move/capture makes that path unreachable.
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
            Evaluate(position) < beta || !mcumax_position_any_legal(&position)) return false;

        const uint8_t oldSide = position.side;
        const uint8_t oldEp = position.en_passant;
        position.side ^= 24U;
        position.en_passant = MCUMAX_SQUARE_INVALID;
        // A pass is not part of legal history. Post-pass repetition cannot
        // compare with the real prefix, and no nested pass is allowed here.
        hypotheticalRoot = ply + 1U;
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
        if (Repeated(ply) || DeadMaterial(position)) return 0;
        const bool checked = mcumax_position_is_check(&position, position.side);
        if (ply == MAX_PLY - 1U) {
            ++horizonNodes;
            if (!mcumax_position_any_legal(&position)) return checked ? -MATE + ply : 0;
            return Evaluate(position);
        }
        Frame& frame = frames[ply];
        const uint64_t key = path[ply];
        const CacheEntry entry = hypotheticalRoot == 0U ? cache[key & (CACHE_CAPACITY - 1U)] : CacheEntry{};
        const bool cached = hypotheticalRoot == 0U && config.cacheMode != CacheMode::Disabled && entry.key == key;
        const auto preferred = ply == 0U && completed.completedDepth != 0U && !completed.preliminaryOnly ? completed.move :
            (cached ? entry.move : INVALID);
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
            if (!mcumax_position_make(&position, frame.move, &frame.undo)) continue;
            ++searchedMoves;
            const bool reduce = config.lateQuietReduction && ply != 0U && depth >= 3U &&
                searchedMoves > 4U && quiet && !checked && !mcumax_position_is_check(&position, position.side);
            int score = alpha + 1;
            if (reduce) {
                score = -Search(-alpha - 1, -alpha, depth - 2U, ply + 1U, irreversible);
                if (stopped) {
                    mcumax_position_unmake(&position, &frame.undo);
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
            mcumax_position_unmake(&position, &frame.undo);
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
        completed = SearchResult{};
        frames.fill(Frame{});
        cache.fill(CacheEntry{});
        nodes = 0U;
        horizonNodes = 0U;
        stopped = false;
        hypotheticalRoot = 0U;
        const uint8_t depthLimit = std::min<uint8_t>(request.maxDepth, MAX_PLY - 1U);
        const uint8_t preliminaryDepth = std::min<uint8_t>(2U, depthLimit);
        bool preliminaryRoot = request.firstRootNodeReserve != 0U && request.minimumDepth <= 1U && depthLimit != 0U;
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
            if (completed.available) {
                ThinkCandidate candidate;
                candidate.fromSquare = (completed.move.from >> 4U) * 8U + (completed.move.from & 7U);
                candidate.toSquare = (completed.move.to >> 4U) * 8U + (completed.move.to & 7U);
                candidate.promotion = PromotionChar(completed.move.promotion);
                candidate.completedDepth = completed.completedDepth;
                config.candidateObserver.Publish(candidate);
            }
            if (StopRequested() || (completed.available && std::abs(score) >= MATE - MAX_PLY)) break;
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

static_assert(sizeof(duck_chess_search_internal::SearchState) <= sizeof(ClassicState),
              "Duck traversal must reuse the existing Classic allocation capacity");

struct SearchWorkspace::Impl {
    enum class Variant : uint8_t { Empty, Classic, Duck };
    Variant variant = Variant::Empty;
    union {
        ClassicState classic;
        duck_chess_search_internal::SearchState duck;
    };
    Impl() {}
    ~Impl() { Clear(); }
    void Clear() {
        if (variant == Variant::Classic) classic.~ClassicState();
        if (variant == Variant::Duck) duck.~SearchState();
        variant = Variant::Empty;
    }
    void Select(Variant next) {
        if (variant == next) return;
        Clear();
        if (next == Variant::Classic) new (&classic) ClassicState{};
        else new (&duck) duck_chess_search_internal::SearchState{};
        variant = next;
    }
};

SearchWorkspace::SearchWorkspace() = default;
SearchWorkspace::~SearchWorkspace() = default;

SearchResult SearchWorkspace::Run(const mcumax_position& position, const SearchConfig& config) {
    if (!m_impl) m_impl.reset(new (std::nothrow) Impl{});
    if (!m_impl) return SearchResult{};
    m_impl->Select(Impl::Variant::Classic);
    return m_impl->classic.Run(position, config);
}

bool SearchWorkspace::RunDuck(const char* fen, uint8_t duck, const DuckChessSearchConfig& config,
                              DuckChessTurn& outTurn, DuckChessSearchStats& stats,
                              bool (*cancel)(void*), void* cancelContext) {
    outTurn = DuckChessTurn{};
    stats = DuckChessSearchStats{};
    duck_chess_rules_internal::DuckPosition root;
    if (!duck_chess_rules_internal::ParseDuckPosition(fen, root) ||
        !duck_chess_rules_internal::IsValidDuck(root, duck)) return false;
    const auto status = duck_chess_search_internal::InspectRoot(root, duck, config, outTurn, stats, cancel, cancelContext);
    if (status != duck_chess_search_internal::RootStatus::Ready) {
        return status == duck_chess_search_internal::RootStatus::KingCapture;
    }
    if (!m_impl) m_impl.reset(new (std::nothrow) Impl{});
    stats.workspaceBytes = sizeof(Impl);
    if (!m_impl) { stats.allocationFailed = true; return false; }
    m_impl->Select(Impl::Variant::Duck);
    return m_impl->duck.Run(root, duck, config, outTurn, stats, cancel, cancelContext);
}

bool SearchWorkspace::RunDuckPlacement(const char* fen, uint8_t duck,
                                       const DuckChessSearchConfig& config, uint8_t& destination,
                                       DuckChessSearchStats& stats, bool (*cancel)(void*), void* cancelContext) {
    destination = DUCK_CHESS_INVALID_SQUARE;
    stats = DuckChessSearchStats{};
    duck_chess_rules_internal::DuckPosition position;
    if (!duck_chess_rules_internal::ParseDuckPosition(fen, position) ||
        !duck_chess_rules_internal::IsValidDuck(position, duck)) return false;
    return duck_chess_search_internal::PlaceDuck(position, duck, config, destination, stats, cancel, cancelContext);
}

size_t SearchWorkspace::RequiredBytes() { return sizeof(Impl); }

} // namespace modules::classic_chess
