#pragma once

#include <cstdint>
#include <foxie_embedded/fixed_string.hpp>

namespace litch_max {

enum class SideToMove : uint8_t {
    White = 0,
    Black = 1,
};

// UCI move strings are 4 chars for normal moves and 5 with promotion (e7e8q).
// Keep a little slack for safety and a null terminator.
using UciMove = fxe::FixedString<8>;

// Search-thread-only scalar publication. The receiver must outlive the search
// and its cleanup; this does not expose the mutable engine to observers.
struct ThinkProgressObserver {
    void (*publish)(void* context, uint32_t nodes) = nullptr;
    void* context = nullptr;

    void Publish(const uint32_t nodes) const {
        if (publish != nullptr) publish(context, nodes);
    }
};

enum class ThinkCandidateSource : uint8_t { Search, Book, Seed };
enum class ThinkRootKind : uint8_t { Unavailable, Book, ClassicPreliminary, ClassicFull, DuckCompound, Seed };
enum class ThinkScoreUnits : uint8_t { Unavailable, ClassicPawn100, DuckPawn1000 };
// Scalar limits only: zero requested limits may resolve to engine defaults.
// A reported ceiling is never a claim of completed depth or consumed work.
struct ThinkProfile {
    uint32_t maxThinkMs = 0U;
    uint32_t maxNodes = 0U;
    uint32_t maxDepth = 0U;
    uint16_t firstRootNodeReserve = 0U;
    uint8_t difficulty = 0U;
    uint8_t minimumDepth = 0U;

    template <typename Config>
    static ThinkProfile Requested(const Config& config) {
        return {config.maxThinkMs, config.maxNodes, config.maxDepth,
                config.firstRootNodeReserve, config.difficulty, 0U};
    }
};

// Immutable analysis; logical squares are 0=a8 .. 63=h1. Book depth describes
// completed offline work and never claims a completed live root.
struct ThinkCandidate {
    uint8_t fromSquare = 64U;
    uint8_t toSquare = 64U;
    char promotion = '\0';
    uint8_t duckDestination = 255U;
    bool isDuck = false;
    bool capturedKing = false;
    // Classic CPU may publish preliminary depth two before restarting at Full one.
    uint8_t completedDepth = 0U;
    ThinkCandidateSource source = ThinkCandidateSource::Search;
    uint8_t offlineDepth = 0U;
    // Completed-root evidence, from the search thread. Replayed retained
    // candidates remain useful but cannot count as new work in this request.
    ThinkRootKind rootKind = ThinkRootKind::Unavailable;
    ThinkScoreUnits scoreUnits = ThinkScoreUnits::Unavailable;
    SideToMove scorePerspective = SideToMove::White;
    int32_t score = 0;
    uint32_t completedNodes = 0U;
    bool scoreValid = false;
    bool replayed = false;
    uint8_t reservedBytes[2]{}; // Fully initialized word-copy representation.

    bool IsAvailable() const {
        return fromSquare < 64U && toSquare < 64U &&
            (source == ThinkCandidateSource::Seed ||
             (source == ThinkCandidateSource::Book ? offlineDepth != 0U : completedDepth != 0U));
    }
};

struct ThinkCandidateObserver {
    void (*publish)(void* context, const ThinkCandidate& candidate) = nullptr;
    void* context = nullptr;
    // Diagnostic callbacks include completed roots below a Hint publication
    // floor. They never change which candidate the publication owner accepts.
    void (*completedRoot)(void* context, const ThinkCandidate& candidate) = nullptr;
    // Opt-in declares complete-root reporting, including suppressed Hint roots.
    void (*effectiveProfile)(void* context, const ThinkProfile& profile) = nullptr;

    void RootCompleted(const ThinkCandidate& candidate) const {
        if (completedRoot != nullptr) completedRoot(context, candidate);
    }
    void ProfileResolved(const ThinkProfile& profile) const {
        if (effectiveProfile != nullptr) effectiveProfile(context, profile);
    }
    void Publish(const ThinkCandidate& candidate) const {
        if (publish != nullptr) publish(context, candidate);
    }
};

} // namespace litch_max
