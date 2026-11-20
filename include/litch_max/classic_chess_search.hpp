#pragma once

#include <cstddef>
#include <cstdint>

#include <mcu-max-position.h>

#include "litch_max/types.hpp"

namespace litch_max {
struct DuckChessSearchConfig;
struct DuckChessTurn;
struct DuckChessSearchStats;
}

namespace litch_max::classic_chess {

// Stable public/book and ordinary-cache rules identity. Live search repetition
// uses a private incremental fingerprint with the same canonical features.
// Temporary legal en-passant probes restore the position before returning.
uint64_t PositionKey(mcumax_position& position);
// Parse without touching the shared game, including effective en-passant identity.
bool TryPositionKey(const char* fen, uint64_t& outKey);

// Heap-free legal seed ranking; does not claim a completed searched root.
bool FindImmediateCandidate(mcumax_position position, ThinkCandidate& out);

enum class CacheMode : uint8_t { Disabled, MoveOrder, Bounds };

// Depth counts ordinary chess plies. Quiescence work is included in nodes.
struct SearchConfig {
    uint32_t maxNodes = 0U;
    uint8_t maxDepth = 32U;
    uint8_t minimumDepth = 1U;
    CacheMode cacheMode = CacheMode::Bounds;
    bool principalVariationSearch = true;
    // Selective search controls permit full-width regression comparisons.
    bool nullMovePruning = true;
    bool lateQuietReduction = true;
    // Disable only tactical-result reuse for equivalent-policy comparisons.
    bool quiescenceCache = true;
    // Human Hint enables this in its resolver; CPU/default ordering is retained.
    bool recaptureOrdering = false;
    // Novice CPU: complete a fixed static horizon without Full/quiescence work.
    // One ply chooses an immediate move; two plies also consider a reply.
    bool staticLeafOnly = false;
    // Complete a reply-aware static root before potentially expensive Full work.
    bool preliminaryRoot = false;
    // Opt-in reply-aware preliminary root; allowance lasts until this first
    // complete root. All subsequent Full work obeys the cap. Zero is strict.
    uint16_t firstRootNodeReserve = 0U;
    // Caller owns the real clock; search tracks only its made child positions.
    uint16_t reversibleHalfmoves = UINT16_MAX;
    // Legal offline book move orders CPU search; it never substitutes for it.
    mcumax_position_move rootPreferred{MCUMAX_SQUARE_INVALID, MCUMAX_SQUARE_INVALID, 0U};
    bool (*shouldStop)(void* context) = nullptr;
    void* stopContext = nullptr;
    // The caller owns platform scheduling; invoked at the existing per-node seam.
    void (*cooperate)(void* context, uint32_t nodes) = nullptr;
    void* cooperateContext = nullptr;
    ThinkProgressObserver progressObserver{};
    ThinkCandidateObserver candidateObserver{};
};

struct SearchResult {
    mcumax_position_move move{MCUMAX_SQUARE_INVALID, MCUMAX_SQUARE_INVALID, 0U};
    int16_t score = 0;
    uint8_t completedDepth = 0U;
    uint32_t nodes = 0U;
    uint32_t horizonNodes = 0U;
    uint32_t cacheOrderHits = 0U;
    uint32_t cacheCutoffs = 0U;
    uint32_t reversibleBoundRefusals = 0U;
    uint32_t scoutSearches = 0U;
    uint32_t pvResearches = 0U;
    uint32_t scoutInterruptions = 0U;
    uint32_t pvResearchInterruptions = 0U;
    bool available = false;
    bool interrupted = false;
    bool terminal = false;
    // Complete preliminary root with static leaves; no Full root completed.
    bool preliminaryOnly = false;
    // Retained for API compatibility. Caller-owned scratch needs no allocation.
    uint16_t allocationFailureBytes = 0U;
    uint8_t allocationFailureOrdinal = 0U;
};

// One caller-owned buffer serves one search at a time. Keep it outside small
// task stacks. Concurrent searches must have distinct workspace owners.
// Search never mutates the caller's position or the mcu-max singleton.
class SearchWorkspace {
public:
    SearchWorkspace();
    ~SearchWorkspace();
    SearchWorkspace(const SearchWorkspace&) = delete;
    SearchWorkspace& operator=(const SearchWorkspace&) = delete;
    SearchResult Run(const mcumax_position& position, const SearchConfig& config);
    // Only the current variant owns scratch. Duck depth counts complete
    // chess-move/duck-placement turns and uses its own production rules.
    bool RunDuck(const char* fen, uint8_t duck, const DuckChessSearchConfig& config,
                 DuckChessTurn& outTurn, DuckChessSearchStats& stats,
                 bool (*cancel)(void*), void* cancelContext);
    bool RunDuckPlacement(const char* fen, uint8_t duck, const DuckChessSearchConfig& config,
                          uint8_t& destination, DuckChessSearchStats& stats,
                          bool (*cancel)(void*), void* cancelContext);
    // Bytes used by each variant within the shared reservation.
    static size_t RequiredBytes();
    static size_t DuckRequiredBytes();

private:
    // 64-bit hosts have wider callback pointers. Both layouts are checked
    // against the production state types in the implementation.
    alignas(8) std::byte m_storage[sizeof(void*) == 4 ? 12112U : 12160U];
};

} // namespace litch_max::classic_chess
