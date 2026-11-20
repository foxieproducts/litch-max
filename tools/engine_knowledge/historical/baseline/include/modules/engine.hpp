#pragma once

#include <cstddef>
#include <cstdint>

#include "foxie_embedded/fixed_string.hpp"

namespace modules {

enum class SideToMove : uint8_t {
    White = 0,
    Black = 1,
};

// UCI move strings are 4 chars for normal moves and 5 with promotion (e7e8q).
// Keep a little slack for safety and a null terminator.
using UciMove = fxe::FixedString<8>;

enum class ThinkState : uint8_t {
    Idle = 0,
    Thinking = 1,
    Done = 2,
    Canceled = 3,
    Error = 4,
};

// Search-thread-only scalar publication. The receiver must outlive the search
// and its cleanup; this does not expose the mutable engine to observers.
struct ThinkProgressObserver {
    void (*publish)(void* context, uint32_t nodes) = nullptr;
    void* context = nullptr;

    void Publish(const uint32_t nodes) const {
        if (publish != nullptr) publish(context, nodes);
    }
};

// Immutable result of completed search work; logical squares are 0=a8 .. 63=h1.
struct ThinkCandidate {
    uint8_t fromSquare = 64U;
    uint8_t toSquare = 64U;
    char promotion = '\0';
    uint8_t duckDestination = 255U;
    bool isDuck = false;
    bool capturedKing = false;
    // Completed root depth; zero means unavailable. Classic CPU may publish
    // preliminary depth two before restarting at quiescence-inclusive depth one.
    uint8_t completedDepth = 0U;
};

struct ThinkCandidateObserver {
    void (*publish)(void* context, const ThinkCandidate& candidate) = nullptr;
    void* context = nullptr;

    void Publish(const ThinkCandidate& candidate) const {
        if (publish != nullptr) publish(context, candidate);
    }
};

struct ThinkConfig {
    // Difficulty is intentionally engine-defined. Higher means stronger.
    uint8_t difficulty = 0;

    // Optional limits; engines may ignore unsupported fields.
    uint32_t maxThinkMs = 0;
    uint32_t maxNodes = 0;
    uint32_t maxDepth = 0;

    // Determinism hook for engines that use randomness.
    uint32_t rngSeed = 0;

    ThinkProgressObserver progressObserver{};
    ThinkCandidateObserver candidateObserver{};
    // Hint-only sustained analysis; ordinary opponent difficulty is unchanged.
    bool continuousAnalysis = false;
    // Enables a reply-aware preliminary Classic root. Its bounded allowance
    // ends when that root completes; later Full work obeys maxNodes.
    uint16_t firstRootNodeReserve = 0U;
};

struct ThinkStats {
    uint32_t nodeCount = 0;
};

constexpr uint8_t CLASSIC_THINK_CANDIDATE_CAPACITY = 3U;

enum ClassicThinkBestFact : uint8_t {
    ClassicThinkOutcomeKnown = 1U << 0U,
    ClassicThinkGivesCheck = 1U << 1U,
    ClassicThinkGivesMate = 1U << 2U,
};

struct ClassicThinkCandidate {
    // Logical squares use the board-wide 0=a8 .. 63=h1 convention.
    uint8_t fromSquare = 64U;
    uint8_t toSquare = 64U;
    // Zero for an ordinary move; lowercase UCI promotion suffix otherwise.
    char promotion = '\0';
};

// Fixed-size summary of the last completed Classic root search. The first
// candidate is the move returned by PollThinkProgress; remaining candidates
// preserve engine order but do not claim comparable evaluations: alpha-beta
// searches may return bounds for non-best root moves. bestScore is valid only
// for the selected move from a fully completed iteration and is an
// engine-relative signal, not user-facing evaluation text.
struct ClassicThinkResult {
    ClassicThinkCandidate candidates[CLASSIC_THINK_CANDIDATE_CAPACITY];
    int16_t bestScore = 0;
    uint8_t candidateCount = 0U;
    uint8_t bestMoveFacts = 0U;
    bool bestScoreValid = false;
    // Complete preliminary comparison; the Full search did not finish a root.
    bool preliminaryOnly = false;
};

struct DuckThinkResult {
    UciMove chessMove;
    uint8_t duckDestination = 255U;
    int32_t score = 0;
    bool capturedKing = false;
};

class IEngine {
  public:
    virtual ~IEngine() = default;

    // \brief Resets engine to the initial position.
    virtual void Reset() = 0;

    // \brief Sets position from FEN.
    virtual bool SetFen(const char* fen) = 0;

    // \brief Writes current FEN into a provided buffer.
    virtual bool GetFen(char* outFen, size_t outSize) const = 0;

    // \brief Applies a move in UCI notation (e.g. "e2e4", "e7e8q").
    virtual bool ApplyUciMove(const char* uciMove) = 0;

    virtual SideToMove GetSideToMove() const = 0;

    // \brief Queries for check/game-over conditions.
    //
    // Notes:
    // - These are expected to be pure queries of current engine state.
    // - For legal positions, only the side-to-move can be in check/mate.
    virtual bool IsInCheck(SideToMove side) const = 0;
    virtual bool IsInCheckmate(SideToMove side) const = 0;
    virtual bool IsInStalemate(SideToMove side) const = 0;

    // \brief Returns a bitmask of legal destination squares for a given origin.
    //
    // Squares are indexed 0..63 with 0 = a8 and 63 = h1.
    //
    // Notes:
    // - The result is computed for the current side-to-move only.
    // - Returns true for valid inputs even if there are 0 legal destinations.
    // - Returns false if fromSquare is invalid or if the engine is busy.
    virtual bool GetLegalDestinationsMask(uint8_t fromSquare, uint64_t& outDestMask64) = 0;

    // \brief Starts VS-CPU thinking for the current side-to-move.
    // \brief Returns false if thinking could not be started.
    virtual bool StartThink(const ThinkConfig& cfg) = 0;

    // \brief Starts a variant-aware search over a complete Duck turn.
    //
    // Engines without Duck support may retain the default false result.
    virtual bool StartDuckThink(const char* fen,
                                uint8_t duckSquare,
                                const ThinkConfig& cfg) {
        (void)fen;
        (void)duckSquare;
        (void)cfg;
        return false;
    }

    // \brief Requests canceling an in-progress think.
    virtual void CancelThink() = 0;

    // \brief Advances/captures progress for an async engine.
    // \brief If state == Done, outBestMoveUci must contain the chosen move.
    // \brief Progress is 0..100.
    // \brief Polls progress for an in-progress think.
    // \brief If state == Done, outBestMoveUci must contain the chosen move.
    // \brief Progress is 0..100.
    virtual ThinkState PollThinkProgress(uint8_t& outProgressPercent,
                                         UciMove& outBestMoveUci) = 0;

    // rief Reports the most recent think statistics (if available).
    virtual bool GetLastThinkStats(ThinkStats& outStats) const = 0;

    // \brief Returns a bounded summary from the most recent Classic search.
    // This is a coordinator-owned result read, not a thread-safe observation:
    // an AsyncThinkWorker caller must require IsDone() && !IsActive() first.
    // Engines without root-candidate support retain the default false result.
    virtual bool GetLastClassicThinkResult(ClassicThinkResult& outResult) const {
        outResult = ClassicThinkResult{};
        return false;
    }

    // \brief Returns the compound result from the most recent Duck search.
    virtual bool GetLastDuckThinkResult(DuckThinkResult& outResult) const {
        outResult = DuckThinkResult{};
        return false;
    }
};

} // namespace modules
