#pragma once

#include <cstdint>

#include "modules/duck_chess_rules.hpp"
#include "modules/engine.hpp"

namespace modules {

namespace classic_chess { class SearchWorkspace; }

struct DuckChessSearchConfig {
    uint32_t maxNodes = 4096U;
    uint32_t rngSeed = 1U;
    uint8_t difficulty = 1U;
    ThinkProgressObserver progressObserver{};
    ThinkCandidateObserver candidateObserver{};
    bool continuousAnalysis = false;
    classic_chess::SearchWorkspace* workspace = nullptr;
    // Zero retains the direct helper's shallow low-level / two-turn high-level
    // policy. Engine calls supply the selected bounded depth profile.
    uint8_t maxDepth = 0U;
};

struct DuckChessSearchStats {
    uint32_t nodesVisited = 0U;
    uint32_t candidateTurns = 0U;
    bool completed = false;
    bool canceled = false;
    uint8_t completedDepth = 0U;
    uint32_t workspaceBytes = 0U;
    bool depthLimitReached = false;
    bool allocationFailed = false;
};

struct DuckChessTurn {
    UciMove chessMove;
    uint8_t duckDestination = DUCK_CHESS_INVALID_SQUARE;
    int32_t score = 0;
    bool capturedKing = false;
};

using DuckChessSearchCancelCallback = bool (*)(void* context);

bool FindBestDuckChessTurn(
    const char* fen,
    uint8_t duckSquare,
    const DuckChessSearchConfig& config,
    DuckChessTurn& outTurn,
    DuckChessSearchStats& outStats,
    DuckChessSearchCancelCallback cancel = nullptr,
    void* cancelContext = nullptr);

bool FindBestDuckPlacement(
    const char* afterChessFen,
    uint8_t duckSquare,
    const DuckChessSearchConfig& config,
    uint8_t& outDuckDestination,
    DuckChessSearchStats& outStats,
    DuckChessSearchCancelCallback cancel = nullptr,
    void* cancelContext = nullptr);

} // namespace modules
