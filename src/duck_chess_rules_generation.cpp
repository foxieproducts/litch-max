#include "duck_chess_rules_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace litch_max::duck_chess_rules_internal {
namespace {

bool IsOpen(const DuckPosition& position,
            const uint8_t duckSquare,
            const uint8_t square) {
    return square != duckSquare && position.board[square] == '\0';
}

void AddStep(const DuckPosition& position,
             const uint8_t duckSquare,
             const int rank,
             const int file,
             uint64_t& mask) {
    if (rank < 0 || rank >= 8 || file < 0 || file >= 8) {
        return;
    }
    const uint8_t square = static_cast<uint8_t>(rank * 8 + file);
    if (square == duckSquare ||
        IsPieceForSide(position.board[square], position.sideToMove)) {
        return;
    }
    mask |= 1ULL << square;
}

void AddRay(const DuckPosition& position,
            const uint8_t duckSquare,
            const uint8_t fromSquare,
            const int rankStep,
            const int fileStep,
            uint64_t& mask) {
    int rank = static_cast<int>(fromSquare / 8U) + rankStep;
    int file = static_cast<int>(fromSquare % 8U) + fileStep;
    while (rank >= 0 && rank < 8 && file >= 0 && file < 8) {
        const uint8_t square = static_cast<uint8_t>(rank * 8 + file);
        if (square == duckSquare) {
            return;
        }
        const char target = position.board[square];
        if (target != '\0') {
            if (!IsPieceForSide(target, position.sideToMove)) {
                mask |= 1ULL << square;
            }
            return;
        }
        mask |= 1ULL << square;
        rank += rankStep;
        file += fileStep;
    }
}

uint8_t CastlingRightForPattern(const size_t index) {
    static constexpr uint8_t RIGHTS[] = {
        WhiteKingSide, WhiteQueenSide, BlackKingSide, BlackQueenSide};
    return RIGHTS[index];
}

bool CastlePathOpen(const DuckPosition& position,
                    const uint8_t duckSquare,
                    const CastlingPattern& pattern) {
    const int rank = static_cast<int>(pattern.kingFrom / 8U);
    const int kingFile = static_cast<int>(pattern.kingFrom % 8U);
    const int rookFile = static_cast<int>(pattern.rookFrom % 8U);
    const int begin = std::min(kingFile, rookFile) + 1;
    const int end = std::max(kingFile, rookFile);
    for (int file = begin; file < end; ++file) {
        const uint8_t square = static_cast<uint8_t>(rank * 8 + file);
        if (!IsOpen(position, duckSquare, square)) {
            return false;
        }
    }
    return true;
}

void AddCastling(const DuckPosition& position,
                 const uint8_t duckSquare,
                 const uint8_t fromSquare,
                 uint64_t& mask) {
    for (size_t i = 0U; i < CHESS_CASTLING_PATTERNS.size(); ++i) {
        const CastlingPattern& pattern = CHESS_CASTLING_PATTERNS[i];
        if (fromSquare != pattern.kingFrom ||
            (position.castlingRights & CastlingRightForPattern(i)) == 0U) {
            continue;
        }
        const char rook = position.sideToMove == SideToMove::White ? 'R' : 'r';
        if (position.board[pattern.rookFrom] == rook &&
            CastlePathOpen(position, duckSquare, pattern)) {
            mask |= 1ULL << pattern.kingTo;
        }
    }
}

void AddPawnAdvance(const DuckPosition& position,
                    const uint8_t duckSquare,
                    const uint8_t fromSquare,
                    const int direction,
                    uint64_t& mask) {
    const int rank = static_cast<int>(fromSquare / 8U);
    const int file = static_cast<int>(fromSquare % 8U);
    const int oneRank = rank + direction;
    if (oneRank < 0 || oneRank >= 8) {
        return;
    }
    const uint8_t one = static_cast<uint8_t>(oneRank * 8 + file);
    if (!IsOpen(position, duckSquare, one)) {
        return;
    }
    mask |= 1ULL << one;
    const int startRank = position.sideToMove == SideToMove::White ? 6 : 1;
    const int twoRank = rank + 2 * direction;
    if (rank != startRank || twoRank < 0 || twoRank >= 8) {
        return;
    }
    const uint8_t two = static_cast<uint8_t>(twoRank * 8 + file);
    if (IsOpen(position, duckSquare, two)) {
        mask |= 1ULL << two;
    }
}

void AddPawnCaptures(const DuckPosition& position,
                     const uint8_t duckSquare,
                     const uint8_t fromSquare,
                     const int direction,
                     uint64_t& mask) {
    const int rank = static_cast<int>(fromSquare / 8U) + direction;
    const int file = static_cast<int>(fromSquare % 8U);
    for (const int fileStep : {-1, 1}) {
        const int targetFile = file + fileStep;
        if (rank < 0 || rank >= 8 || targetFile < 0 || targetFile >= 8) {
            continue;
        }
        const uint8_t target = static_cast<uint8_t>(rank * 8 + targetFile);
        if (target == duckSquare) {
            continue;
        }
        const char targetPiece = position.board[target];
        if (targetPiece != '\0' &&
            !IsPieceForSide(targetPiece, position.sideToMove)) {
            mask |= 1ULL << target;
            continue;
        }
        if (target != position.enPassantSquare) {
            continue;
        }
        const uint8_t captured = static_cast<uint8_t>(target - direction * 8);
        const char expectedPawn = position.sideToMove == SideToMove::White ? 'p' : 'P';
        if (position.board[captured] == expectedPawn) {
            mask |= 1ULL << target;
        }
    }
}

void AddPawnMoves(const DuckPosition& position,
                  const uint8_t duckSquare,
                  const uint8_t fromSquare,
                  uint64_t& mask) {
    const int direction = position.sideToMove == SideToMove::White ? -1 : 1;
    AddPawnAdvance(position, duckSquare, fromSquare, direction, mask);
    AddPawnCaptures(position, duckSquare, fromSquare, direction, mask);
}

void AddKnightMoves(const DuckPosition& position,
                    const uint8_t duckSquare,
                    const uint8_t fromSquare,
                    uint64_t& mask) {
    static constexpr int DELTAS[8][2] = {
        {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}};
    const int rank = static_cast<int>(fromSquare / 8U);
    const int file = static_cast<int>(fromSquare % 8U);
    for (const auto& delta : DELTAS) {
        AddStep(position, duckSquare,
                rank + delta[0], file + delta[1], mask);
    }
}

void AddKingMoves(const DuckPosition& position,
                  const uint8_t duckSquare,
                  const uint8_t fromSquare,
                  uint64_t& mask) {
    const int rank = static_cast<int>(fromSquare / 8U);
    const int file = static_cast<int>(fromSquare % 8U);
    for (int rankStep = -1; rankStep <= 1; ++rankStep) {
        for (int fileStep = -1; fileStep <= 1; ++fileStep) {
            if (rankStep != 0 || fileStep != 0) {
                AddStep(position, duckSquare,
                        rank + rankStep, file + fileStep, mask);
            }
        }
    }
    AddCastling(position, duckSquare, fromSquare, mask);
}

void AddSlidingMoves(const DuckPosition& position,
                     const uint8_t duckSquare,
                     const uint8_t fromSquare,
                     const char piece,
                     uint64_t& mask) {
    static constexpr int DIRECTIONS[8][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    const size_t begin = piece == 'r' ? 4U : 0U;
    const size_t end = piece == 'b' ? 4U : 8U;
    for (size_t i = begin; i < end; ++i) {
        AddRay(position, duckSquare, fromSquare,
               DIRECTIONS[i][0], DIRECTIONS[i][1], mask);
    }
}

} // namespace

bool IsValidDuck(const DuckPosition& position, const uint8_t duckSquare) {
    return duckSquare < 64U && position.board[duckSquare] == '\0';
}

uint64_t GenerateDestinations(const DuckPosition& position,
                              const uint8_t duckSquare,
                              const uint8_t fromSquare) {
    if (fromSquare >= 64U || fromSquare == duckSquare ||
        !IsPieceForSide(position.board[fromSquare], position.sideToMove)) {
        return 0ULL;
    }
    const char piece = LowerPiece(position.board[fromSquare]);
    uint64_t mask = 0ULL;
    if (piece == 'p') {
        AddPawnMoves(position, duckSquare, fromSquare, mask);
    } else if (piece == 'n') {
        AddKnightMoves(position, duckSquare, fromSquare, mask);
    } else if (piece == 'k') {
        AddKingMoves(position, duckSquare, fromSquare, mask);
    } else {
        AddSlidingMoves(position, duckSquare, fromSquare, piece, mask);
    }
    return mask;
}

} // namespace litch_max::duck_chess_rules_internal
