#include <cstdint>
#include <iostream>

#include "litch_max/chess_notation.hpp"
#include "litch_max/duck_chess_search.hpp"
#include "litch_max/initial_fen.hpp"

namespace {
using namespace litch_max;

// The six-field FEN describes the chess pieces. Keep the neutral blocker beside it.
struct Position {
    DuckChessFen fen;
    uint8_t duckSquare;
};

bool PlayTurn(Position& position) {
    DuckChessSearchConfig config;
    config.maxDepth = 1;  // One complete chess move and Duck relocation.
    config.maxNodes = 4096;

    DuckChessTurn turn;
    DuckChessSearchStats stats;
    if (!FindBestDuckChessTurn(position.fen.CStr(), position.duckSquare, config, turn, stats)) {
        if (stats.completed) {
            std::cerr << "The side to move has no chess move and wins under Duck rules.\n";
        } else {
            std::cerr << "No complete turn available. Check the position and search budget.\n";
        }
        return false;
    }

    DuckChessMoveResult afterChess;
    if (!TryApplyDuckMove(position.fen.CStr(), position.duckSquare, turn.chessMove.CStr(), afterChess)) {
        std::cerr << "The chess move was rejected.\n";
        return false;
    }

    if (afterChess.capturedKing) {
        position.fen = afterChess.resultingFen;
        std::cout << turn.chessMove.CStr() << " captures the king. Game over.\n";
        return true;
    }

    char destination[3]{};
    if (!TryFormatUciSquare(turn.duckDestination, destination, sizeof(destination))) {
        std::cerr << "A nonterminal turn needs a Duck destination.\n";
        return false;
    }
    // The search chose a different square that is empty after the chess move.
    // Commit the advanced FEN and the new blocker together.
    position = {afterChess.resultingFen, turn.duckDestination};
    std::cout << turn.chessMove.CStr() << ", Duck to " << destination << "  ("
              << stats.nodesVisited << " nodes)\n" << position.fen.CStr() << '\n';
    return true;
}
} // namespace

int main() {
    uint8_t duckSquare = 0;
    if (!litch_max::TryParseUciSquare("d4", duckSquare)) return 1;

    Position opening{litch_max::DuckChessFen(litch_max::INITIAL_FEN), duckSquare};
    if (!PlayTurn(opening)) return 1;

    // In this position White's queen can take the king on e8 immediately.
    Position ending{litch_max::DuckChessFen("4k3/8/8/8/8/8/4Q3/4K3 w - - 0 1"), duckSquare};
    return PlayTurn(ending) ? 0 : 1;
}
