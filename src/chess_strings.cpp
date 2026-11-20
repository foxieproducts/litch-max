#include "litch_max/chess_strings.hpp"

#include "litch_max/chess_notation.hpp"

namespace litch_max {

bool TryGetFenPieceAtSquare(const char* fen, const uint8_t square, char& outPieceChar) {
    outPieceChar = '\0';
    if (fen == nullptr || fen[0] == '\0' || square >= 64U) {
        return false;
    }

    int rank = 0;
    int file = 0;
    for (const char* p = fen; *p != '\0' && *p != ' '; ++p) {
        const char c = *p;
        if (c == '/') {
            if (file != 8) {
                return false;
            }
            ++rank;
            file = 0;
            continue;
        }
        if (c >= '1' && c <= '8') {
            file += static_cast<int>(c - '0');
            if (file > 8) {
                return false;
            }
            continue;
        }

        const bool isPiece = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
        if (!isPiece || rank < 0 || rank >= 8 || file < 0 || file >= 8) {
            return false;
        }

        const uint8_t sq = static_cast<uint8_t>(rank * 8 + file);
        if (sq == square) {
            outPieceChar = c;
            return true;
        }

        ++file;
    }

    return false;
}

} // namespace litch_max
