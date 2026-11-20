#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <mcu-max-position.h>

#include "modules/engine.hpp"

namespace modules {
struct DuckChessSearchConfig;
struct DuckChessTurn;
struct DuckChessSearchStats;
}

namespace modules::classic_chess {

// Canonical rules identity for repetition and cache lookup. Temporary legal
// en-passant probes restore the supplied position before returning.
uint64_t PositionKey(mcumax_position& position);

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
    // Opt-in reply-aware preliminary root; allowance lasts until this first
    // complete root. All subsequent Full work obeys the cap. Zero is strict.
    uint16_t firstRootNodeReserve = 0U;
    bool (*shouldStop)(void* context) = nullptr;
    void* stopContext = nullptr;
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
};

// One owner, reused by the existing engine worker. No allocation occurs per
// node. Search never mutates the caller's position or the mcu-max singleton.
class SearchWorkspace {
public:
    SearchWorkspace();
    ~SearchWorkspace();
    SearchResult Run(const mcumax_position& position, const SearchConfig& config);
    // Both variants reuse one bounded allocation. Duck depth counts complete
    // chess-move/duck-placement turns and uses its own production rules.
    bool RunDuck(const char* fen, uint8_t duck, const DuckChessSearchConfig& config,
                 DuckChessTurn& outTurn, DuckChessSearchStats& stats,
                 bool (*cancel)(void*), void* cancelContext);
    bool RunDuckPlacement(const char* fen, uint8_t duck, const DuckChessSearchConfig& config,
                          uint8_t& destination, DuckChessSearchStats& stats,
                          bool (*cancel)(void*), void* cancelContext);
    static size_t RequiredBytes();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace modules::classic_chess
