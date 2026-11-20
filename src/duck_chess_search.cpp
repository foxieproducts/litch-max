#include "litch_max/duck_chess_search.hpp"
#include "litch_max/classic_chess_search.hpp"

#include "duck_chess_search_workspace.hpp"

namespace litch_max {

// The convenience call owns only Duck scratch. Keep its frame out of the
// supplied-workspace path used by the firmware worker.
static bool FindTurnWithLocalScratch(const char* fen, uint8_t duck,
                                     const DuckChessSearchConfig& config, DuckChessTurn& turn,
                                     DuckChessSearchStats& stats, DuckChessSearchCancelCallback cancel,
                                     void* cancelContext) {
    using duck_chess_search_internal::SearchState;
    alignas(SearchState) std::byte scratch[sizeof(SearchState)];
    return duck_chess_search_internal::FindTurn(scratch, fen, duck, config, turn, stats, cancel, cancelContext);
}

bool FindBestDuckChessTurn(const char* fen, uint8_t duck,
                           const DuckChessSearchConfig& config, DuckChessTurn& turn,
                           DuckChessSearchStats& stats, DuckChessSearchCancelCallback cancel,
                           void* cancelContext) {
    if (config.workspace != nullptr) return config.workspace->RunDuck(fen, duck, config, turn, stats, cancel, cancelContext);
    return FindTurnWithLocalScratch(fen, duck, config, turn, stats, cancel, cancelContext);
}

bool FindBestDuckPlacement(const char* fen, uint8_t duck,
                           const DuckChessSearchConfig& config, uint8_t& destination,
                           DuckChessSearchStats& stats, DuckChessSearchCancelCallback cancel,
                           void* cancelContext) {
    destination = DUCK_CHESS_INVALID_SQUARE;
    stats = DuckChessSearchStats{};
    duck_chess_rules_internal::DuckPosition position;
    if (!duck_chess_rules_internal::ParseDuckPosition(fen, position) ||
        !duck_chess_rules_internal::IsValidDuck(position, duck)) return false;
    return duck_chess_search_internal::PlaceDuck(position, duck, config, destination, stats, cancel, cancelContext);
}

} // namespace litch_max
