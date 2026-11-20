#pragma once

#include <array>
#include <cstdint>

#include "duck_chess_rules_internal.hpp"
#include "modules/chess_notation.hpp"
#include "modules/duck_chess_search.hpp"

namespace modules::duck_chess_search_internal {

constexpr uint8_t FRAME_COUNT = 16U;
constexpr int32_t WIN = 1000000;
constexpr int32_t INFINITY_SCORE = WIN + 1;

struct CompoundMove {
    uint8_t from = 64U;
    uint8_t to = 64U;
    char promotion = '\0';
    uint8_t duck = DUCK_CHESS_INVALID_SQUARE;
    bool capturedKing = false;
};

enum class RootStatus : uint8_t { Ready, KingCapture, NoMoves, Canceled };
RootStatus InspectRoot(const duck_chess_rules_internal::DuckPosition& position,
                       uint8_t duck, const DuckChessSearchConfig& config,
                       DuckChessTurn& outTurn, DuckChessSearchStats& stats,
                       DuckChessSearchCancelCallback cancel, void* cancelContext);

struct Frame {
    duck_chess_rules_internal::DuckPosition position{};
    duck_chess_rules_internal::DuckPosition afterChess{};
    CompoundMove candidate{}, best{}, preferred{};
    uint64_t destinations = 0ULL;
    uint64_t orderKey = 0U;
    int32_t alpha = -INFINITY_SCORE;
    int32_t beta = INFINITY_SCORE;
    int32_t score = -INFINITY_SCORE;
    uint32_t bestTie = 0U;
    uint8_t duck = DUCK_CHESS_INVALID_SQUARE;
    uint8_t remaining = 0U;
    uint8_t from = 0U;
    uint8_t to = 0U;
    uint8_t promotion = 0U;
    uint8_t nextDuck = 64U;
    uint8_t stage = 0U;
    bool fromLoaded = false;
    bool haveMove = false;
    bool haveBest = false;
    bool exhausted = false;
    bool preferredEmitted = false;

    void Begin(const duck_chess_rules_internal::DuckPosition& root, uint8_t blocker,
               uint8_t depth, int32_t lower, int32_t upper,
               CompoundMove preference = {});
    bool Next();
    void Accept(int32_t value, uint32_t tie, bool exactRootTie);
};

// Iterative traversal keeps positions and cursors off the small worker stack.
// This variant occupies the same allocation as the Classic cache/frames.
struct OrderEntry {
    uint64_t key = 0U;
    CompoundMove move{};
};
static_assert(sizeof(OrderEntry) == 16U, "Duck ordering cache entries remain compact");

struct SearchState {
    std::array<Frame, FRAME_COUNT> frames{};
    std::array<OrderEntry, 128U> orderCache{};
    DuckChessSearchConfig config{};
    DuckChessSearchStats* stats = nullptr;
    DuckChessSearchCancelCallback cancel = nullptr;
    void* cancelContext = nullptr;

    bool Stopped();
    void BeginFrame(uint8_t ply, const duck_chess_rules_internal::DuckPosition& root,
                    uint8_t duck, uint8_t depth, int32_t alpha, int32_t beta,
                    CompoundMove preferred = {});
    bool Run(const duck_chess_rules_internal::DuckPosition& root, uint8_t duck,
             const DuckChessSearchConfig& request, DuckChessTurn& outTurn,
             DuckChessSearchStats& outStats, DuckChessSearchCancelCallback stop,
             void* stopContext);
};

bool PlaceDuck(const duck_chess_rules_internal::DuckPosition& position,
               uint8_t duck, const DuckChessSearchConfig& config,
               uint8_t& destination, DuckChessSearchStats& stats,
               DuckChessSearchCancelCallback cancel, void* cancelContext);

} // namespace modules::duck_chess_search_internal
