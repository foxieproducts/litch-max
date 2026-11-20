#include "duck_chess_search_workspace.hpp"

#include <algorithm>
#include <limits>

namespace modules::duck_chess_search_internal {
namespace {
using duck_chess_rules_internal::DuckPosition;
using duck_chess_rules_internal::GenerateDestinations;
using duck_chess_rules_internal::IsPieceForSide;
using duck_chess_rules_internal::LowerPiece;
constexpr char PROMOTIONS[]{'q', 'r', 'b', 'n'};

bool Same(const CompoundMove& left, const CompoundMove& right) {
    return left.from == right.from && left.to == right.to &&
        left.promotion == right.promotion && left.duck == right.duck;
}

uint32_t TieKey(uint32_t seed, const CompoundMove& move) {
    uint32_t value = seed ^ 0x9E3779B9U;
    value ^= static_cast<uint32_t>(move.from) * 0x85EBCA6BU;
    value ^= static_cast<uint32_t>(move.to) * 0xC2B2AE35U;
    value ^= static_cast<uint32_t>(move.duck) * 0x27D4EB2FU;
    return value ^ (value >> 16U);
}

bool LoadMove(Frame& frame, const CompoundMove& move) {
    frame.afterChess = frame.position;
    ParsedUciMove parsed;
    parsed.fromSquare = move.from;
    parsed.toSquare = move.to;
    parsed.hasPromotion = move.promotion != '\0';
    parsed.promotionSuffixLower = move.promotion;
    duck_chess_rules_internal::MoveCapture capture;
    if (!duck_chess_rules_internal::ApplyMove(frame.afterChess, frame.duck, parsed, capture)) return false;
    frame.candidate = move;
    frame.candidate.capturedKing = LowerPiece(capture.piece) == 'k';
    if (frame.candidate.capturedKing) frame.candidate.duck = DUCK_CHESS_INVALID_SQUARE;
    return true;
}

bool FormatTurn(const CompoundMove& move, const int32_t score, DuckChessTurn& turn) {
    if (!TryFormatUciMove(move.from, move.to, move.promotion, turn.chessMove)) return false;
    turn.duckDestination = move.duck;
    turn.capturedKing = move.capturedKing;
    turn.score = score;
    return true;
}

void Publish(const DuckChessSearchConfig& config, const CompoundMove& move, uint8_t depth) {
    ThinkCandidate candidate;
    candidate.fromSquare = move.from;
    candidate.toSquare = move.to;
    candidate.promotion = move.promotion;
    candidate.duckDestination = move.duck;
    candidate.isDuck = true;
    candidate.capturedKing = move.capturedKing;
    candidate.completedDepth = depth;
    config.candidateObserver.Publish(candidate);
}

int32_t PieceValue(const char piece) {
    switch (LowerPiece(piece)) {
        case 'p': return 100;
        case 'n': return 320;
        case 'b': return 330;
        case 'r': return 500;
        case 'q': return 900;
        default: return 0;
    }
}

struct SideAnalysis {
    uint16_t moves = 0U;
    int32_t bestCapture = 0;
    bool kingCapture = false;
};

SideAnalysis Analyze(const DuckPosition& position, uint8_t duck) {
    SideAnalysis result;
    const char enemyKing = position.sideToMove == SideToMove::White ? 'k' : 'K';
    for (uint8_t from = 0U; from < 64U; ++from) {
        uint64_t destinations = GenerateDestinations(position, duck, from);
        while (destinations != 0ULL) {
            const uint8_t to = static_cast<uint8_t>(__builtin_ctzll(destinations));
            destinations &= destinations - 1ULL;
            ++result.moves;
            result.kingCapture = result.kingCapture || position.board[to] == enemyKing;
            result.bestCapture = std::max(result.bestCapture, PieceValue(position.board[to]));
        }
    }
    return result;
}

int32_t Evaluate(const DuckPosition& position, uint8_t duck) {
    const auto current = Analyze(position, duck);
    // No-move wins and available king captures are real Duck terminal facts.
    if (current.moves == 0U || current.kingCapture) return WIN;
    auto opposite = position;
    opposite.sideToMove = position.sideToMove == SideToMove::White ? SideToMove::Black : SideToMove::White;
    const auto other = Analyze(opposite, duck);
    int32_t material = 0;
    for (const char piece : position.board) {
        material += IsPieceForSide(piece, position.sideToMove) ? PieceValue(piece) : -PieceValue(piece);
    }
    return material * 10 + current.moves * 3 - other.moves * 4 +
        current.bestCapture - other.bestCapture * 2 - (other.kingCapture ? 10000 : 0);
}

} // namespace

RootStatus InspectRoot(const DuckPosition& position, uint8_t duck,
                       const DuckChessSearchConfig& config, DuckChessTurn& outTurn,
                       DuckChessSearchStats& stats, DuckChessSearchCancelCallback cancel,
                       void* cancelContext) {
    bool haveMove = false;
    const char king = position.sideToMove == SideToMove::White ? 'k' : 'K';
    for (uint8_t from = 0U; from < 64U; ++from) {
        config.progressObserver.Publish(stats.nodesVisited);
        if (cancel != nullptr && cancel(cancelContext)) {
            stats.canceled = true;
            return RootStatus::Canceled;
        }
        uint64_t destinations = GenerateDestinations(position, duck, from);
        haveMove = haveMove || destinations != 0ULL;
        while (destinations != 0ULL) {
            const auto to = static_cast<uint8_t>(__builtin_ctzll(destinations));
            destinations &= destinations - 1ULL;
            if (position.board[to] != king) continue;
            const char piece = LowerPiece(position.board[from]);
            CompoundMove move{from, to, piece == 'p' && (to < 8U || to >= 56U) ? 'q' : '\0',
                              DUCK_CHESS_INVALID_SQUARE, true};
            if (!FormatTurn(move, WIN, outTurn)) return RootStatus::NoMoves;
            stats.nodesVisited = stats.candidateTurns = 1U;
            stats.completedDepth = 1U;
            stats.completed = true;
            config.progressObserver.Publish(stats.nodesVisited);
            Publish(config, move, 1U);
            return RootStatus::KingCapture;
        }
    }
    if (!haveMove) stats.completed = true;
    return haveMove ? RootStatus::Ready : RootStatus::NoMoves;
}

void Frame::Begin(const DuckPosition& root, uint8_t blocker, uint8_t depth,
                  int32_t lower, int32_t upper, CompoundMove preference) {
    position = root;
    duck = blocker;
    remaining = depth;
    alpha = lower;
    beta = upper;
    score = -INFINITY_SCORE;
    preferred = preference;
    from = to = promotion = stage = 0U;
    nextDuck = 64U;
    fromLoaded = haveMove = haveBest = exhausted = preferredEmitted = false;
}

bool Frame::Next() {
    if (!preferredEmitted) {
        preferredEmitted = true;
        if (preferred.from < 64U && LoadMove(*this, preferred) &&
            (candidate.capturedKing || (preferred.duck < 64U && preferred.duck != duck &&
                                       afterChess.board[preferred.duck] == '\0'))) {
            haveMove = true;
            preferred = candidate; // Normalize a cached terminal turn to no-duck.
            return true;
        }
    }
    while (stage < 2U && !exhausted) {
        if (nextDuck < 64U) {
            candidate.duck = nextDuck++;
            if (candidate.duck == duck || afterChess.board[candidate.duck] != '\0' ||
                Same(candidate, preferred)) continue;
            return true;
        }
        if (from >= 64U) {
            ++stage;
            from = to = promotion = 0U;
            fromLoaded = false;
            continue;
        }
        if (!fromLoaded) {
            destinations = GenerateDestinations(position, duck, from);
            fromLoaded = true;
        }
        if (to >= 64U) {
            ++from;
            to = promotion = 0U;
            fromLoaded = false;
            continue;
        }
        if ((destinations & (1ULL << to)) == 0ULL) { ++to; continue; }
        const char piece = LowerPiece(position.board[from]);
        const bool promotes = piece == 'p' && (to < 8U || to >= 56U);
        const bool capture = position.board[to] != '\0' ||
            (piece == 'p' && (from % 8U) != (to % 8U));
        if ((capture || promotes) != (stage == 0U)) { ++to; continue; }
        const CompoundMove move{from, to, promotes ? PROMOTIONS[promotion] : '\0'};
        if (++promotion >= (promotes ? 4U : 1U)) { ++to; promotion = 0U; }
        if (!LoadMove(*this, move)) continue;
        haveMove = true;
        if (candidate.capturedKing) {
            if (!Same(candidate, preferred)) return true;
            continue;
        }
        nextDuck = 0U;
    }
    return false;
}

void Frame::Accept(int32_t value, uint32_t tie, bool exactRootTie) {
    if (!haveBest || value > score || (exactRootTie && value == score && tie < bestTie)) {
        score = value;
        best = candidate;
        bestTie = tie;
        haveBest = true;
    }
    alpha = std::max(alpha, value);
    exhausted = alpha >= beta;
}

void SearchState::BeginFrame(uint8_t ply, const DuckPosition& root, uint8_t duck,
                             uint8_t depth, int32_t alpha, int32_t beta, CompoundMove preferred) {
    uint64_t key = 1469598103934665603ULL;
    if (depth != 0U) {
        for (const auto piece : root.board) key = (key ^ static_cast<uint8_t>(piece)) * 1099511628211ULL;
        key = (key ^ static_cast<uint8_t>(root.sideToMove)) * 1099511628211ULL;
        key = (key ^ root.castlingRights) * 1099511628211ULL;
        key = (key ^ root.enPassantSquare) * 1099511628211ULL;
        const auto& entry = orderCache[key & (orderCache.size() - 1U)];
        if (preferred.from >= 64U && entry.key == key) preferred = entry.move;
    }
    // Intentionally share move ordering across different blocker positions.
    // No scores/bounds are cached. Next validates the compound choice against
    // the real blocker and still streams every other legal choice exactly once.
    frames[ply].Begin(root, duck, depth, alpha, beta, preferred);
    frames[ply].orderKey = key;
}

bool SearchState::Stopped() {
    config.progressObserver.Publish(stats->nodesVisited);
    if (cancel != nullptr && cancel(cancelContext)) stats->canceled = true;
    return stats->canceled || (config.maxNodes != 0U && stats->nodesVisited >= config.maxNodes);
}

bool SearchState::Run(const DuckPosition& root, uint8_t duck,
                      const DuckChessSearchConfig& request, DuckChessTurn& outTurn,
                      DuckChessSearchStats& outStats, DuckChessSearchCancelCallback stop,
                      void* stopContext) {
    config = request;
    orderCache.fill(OrderEntry{});
    stats = &outStats;
    cancel = stop;
    cancelContext = stopContext;
    const uint8_t requestedDepth = config.maxDepth != 0U ? config.maxDepth :
        (config.continuousAnalysis ? FRAME_COUNT - 1U : (config.difficulty < 8U ? 1U : 2U));
    const uint8_t depthLimit = std::min<uint8_t>(requestedDepth, FRAME_COUNT - 1U);
    bool havePublished = false;
    CompoundMove previous{};
    for (uint8_t depth = 1U; depth <= depthLimit; ++depth) {
        uint8_t ply = 0U;
        BeginFrame(0U, root, duck, depth, -INFINITY_SCORE, INFINITY_SCORE, previous);
        while (true) {
            if (Stopped()) return havePublished;
            Frame& frame = frames[ply];
            if (frame.remaining != 0U && frame.Next()) {
                ++stats->nodesVisited;
                if (ply == 0U) ++stats->candidateTurns;
                if (frame.candidate.capturedKing) {
                    frame.Accept(WIN, TieKey(config.rngSeed, frame.candidate), ply == 0U && depth == 1U);
                    continue;
                }
                BeginFrame(ply + 1U, frame.afterChess, frame.candidate.duck,
                           frame.remaining - 1U, -frame.beta, -frame.alpha);
                ++ply;
                continue;
            }
            const int32_t score = frame.remaining == 0U ? Evaluate(frame.position, frame.duck) :
                (frame.haveMove ? frame.score : WIN);
            if (frame.remaining != 0U && frame.haveBest) {
                orderCache[frame.orderKey & (orderCache.size() - 1U)] = {frame.orderKey, frame.best};
            }
            if (ply == 0U) break;
            Frame& parent = frames[--ply];
            // A fail-low bound is not an exact tie. Preserve the proven best;
            // seeded tie choice is sound only for fully evaluated depth1 leaves.
            parent.Accept(-score, TieKey(config.rngSeed, parent.candidate), ply == 0U && depth == 1U);
        }
        const Frame& complete = frames[0];
        if (!complete.haveBest) { stats->completed = true; return false; }
        if (!FormatTurn(complete.best, complete.score, outTurn)) return havePublished;
        previous = complete.best;
        stats->completedDepth = depth;
        config.progressObserver.Publish(stats->nodesVisited);
        Publish(config, complete.best, depth);
        havePublished = true;
        if (complete.score == WIN || complete.score == -WIN) { stats->completed = true; return true; }
    }
    stats->depthLimitReached = requestedDepth >= FRAME_COUNT - 1U;
    stats->completed = true;
    return havePublished;
}

bool PlaceDuck(const DuckPosition& position, uint8_t duck,
               const DuckChessSearchConfig& config, uint8_t& destination,
               DuckChessSearchStats& stats, DuckChessSearchCancelCallback cancel,
               void* cancelContext) {
    int32_t best = -INFINITY_SCORE;
    uint32_t bestTie = std::numeric_limits<uint32_t>::max();
    for (uint8_t square = 0U; square < 64U; ++square) {
        config.progressObserver.Publish(stats.nodesVisited);
        if (cancel != nullptr && cancel(cancelContext)) stats.canceled = true;
        if (stats.canceled || (config.maxNodes != 0U && stats.nodesVisited >= config.maxNodes)) break;
        if (square == duck || position.board[square] != '\0') continue;
        ++stats.nodesVisited;
        ++stats.candidateTurns;
        const int32_t score = -Evaluate(position, square);
        const uint32_t tie = TieKey(config.rngSeed, CompoundMove{0U, 0U, '\0', square});
        if (destination == DUCK_CHESS_INVALID_SQUARE || score > best || (score == best && tie < bestTie)) {
            destination = square;
            best = score;
            bestTie = tie;
        }
    }
    stats.completed = !stats.canceled && (config.maxNodes == 0U || stats.nodesVisited < config.maxNodes);
    config.progressObserver.Publish(stats.nodesVisited);
    return destination < 64U;
}

} // namespace modules::duck_chess_search_internal
