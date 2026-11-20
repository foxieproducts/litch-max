#pragma once
#include <cstdint>
namespace litch_max {
bool TryGetFenPieceAtSquare(const char* fen, uint8_t square, char& outPieceChar);
} // namespace litch_max
