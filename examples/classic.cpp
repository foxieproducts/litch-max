#include <iostream>

#include "litch_max/chess_notation.hpp"
#include "litch_max/classic_chess_search.hpp"
#include "litch_max/classic_game.hpp"

int main() {
    using namespace litch_max;

    ClassicGame game;
    game.Reset();
    if (!game.ApplyUciMove("e2e4")) {
        std::cerr << "White's opening move was rejected.\n";
        return 1;
    }

    // Import an owned position so the search cannot change the live game.
    ClassicPosition position{};
    if (!game.Import(position)) {
        std::cerr << "Could not copy the position after e2e4.\n";
        return 1;
    }

    classic_chess::SearchConfig config;
    config.maxDepth = 3;  // One ply is one player's move.
    config.maxNodes = 4096;
    config.reversibleHalfmoves = 0;  // White just moved a pawn.
    // Published squares use 0=a8 .. 63=h1, ready for TryFormatUciMove.
    // The lower-level result.move uses mcu-max's 0x88 coordinates.
    ThinkCandidate reply;
    config.candidateObserver.context = &reply;
    config.candidateObserver.publish = [](void* context, const ThinkCandidate& candidate) {
        *static_cast<ThinkCandidate*>(context) = candidate;
    };

    static classic_chess::SearchWorkspace workspace;
    const auto result = workspace.Run(position, config);
    if (result.terminal) {
        std::cout << "The position is terminal.\n";
        return 0;
    }
    if (!result.available) {
        std::cerr << "The search stopped before it could choose a reply.\n";
        return 1;
    }

    UciMove move;
    if (!TryFormatUciMove(reply.fromSquare, reply.toSquare, reply.promotion, move) ||
        !game.ApplyUciMove(move.CStr())) {
        std::cerr << "Could not apply the engine reply.\n";
        return 1;
    }
    std::cout << "e2e4 " << move.CStr() << "  (depth " << static_cast<unsigned>(result.completedDepth)
              << ", " << result.nodes << " nodes)\n";
    if (result.interrupted) {
        std::cout << "The budget expired during a deeper search. Played the last completed reply.\n";
    }
}
