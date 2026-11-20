#include "duck_chess_rules_internal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "litch_max/chess_notation.hpp"

namespace litch_max::duck_chess_rules_internal {
namespace {

bool SplitFenFields(const char* fen,
                    std::array<std::string_view, 6>& outFields) {
    if (fen == nullptr || fen[0] == '\0') {
        return false;
    }
    const std::string_view text(fen);
    size_t cursor = 0U;
    uint8_t count = 0U;
    while (cursor < text.size()) {
        while (cursor < text.size() && text[cursor] == ' ') {
            ++cursor;
        }
        if (cursor == text.size()) {
            break;
        }
        const size_t start = cursor;
        while (cursor < text.size() && text[cursor] != ' ') {
            ++cursor;
        }
        if (count >= outFields.size()) {
            return false;
        }
        outFields[count++] = text.substr(start, cursor - start);
    }
    return count == outFields.size();
}

bool ParsePlacement(const std::string_view field, DuckPosition& out) {
    uint8_t rank = 0U;
    uint8_t file = 0U;
    bool previousEmptyRun = false;
    for (const char c : field) {
        if (c == '/') {
            if (file != 8U || rank >= 7U) {
                return false;
            }
            ++rank;
            file = 0U;
            previousEmptyRun = false;
        } else if (c >= '1' && c <= '8') {
            if (previousEmptyRun) {
                return false;
            }
            previousEmptyRun = true;
            file = static_cast<uint8_t>(file + static_cast<uint8_t>(c - '0'));
            if (file > 8U) {
                return false;
            }
        } else {
            if (!IsPiece(c) || file >= 8U) {
                return false;
            }
            out.board[static_cast<size_t>(rank * 8U + file)] = c;
            ++file;
            previousEmptyRun = false;
        }
    }
    return rank == 7U && file == 8U;
}

bool ParseCastlingRights(const std::string_view field, uint8_t& outRights) {
    outRights = 0U;
    if (field == "-") {
        return true;
    }
    for (const char c : field) {
        uint8_t right = 0U;
        switch (c) {
            case 'K':
                right = WhiteKingSide;
                break;
            case 'Q':
                right = WhiteQueenSide;
                break;
            case 'k':
                right = BlackKingSide;
                break;
            case 'q':
                right = BlackQueenSide;
                break;
            default:
                return false;
        }
        if ((outRights & right) != 0U) {
            return false;
        }
        outRights = static_cast<uint8_t>(outRights | right);
    }
    return outRights != 0U;
}

bool ParseCounter(const std::string_view field, uint32_t& outValue) {
    if (field.empty()) {
        return false;
    }
    uint32_t value = 0U;
    for (const char c : field) {
        if (c < '0' || c > '9') {
            return false;
        }
        const uint32_t digit = static_cast<uint32_t>(c - '0');
        if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
    }
    outValue = value;
    return true;
}

bool ParseSideAndEnPassant(const std::array<std::string_view, 6>& fields,
                           DuckPosition& out) {
    if (fields[1] == "w") {
        out.sideToMove = SideToMove::White;
    } else if (fields[1] == "b") {
        out.sideToMove = SideToMove::Black;
    } else {
        return false;
    }
    if (fields[3] == "-") {
        return true;
    }
    if (fields[3].size() != 2U || !IsUciFileChar(fields[3][0]) ||
        !IsUciRankChar(fields[3][1])) {
        return false;
    }
    out.enPassantSquare = static_cast<uint8_t>(
        static_cast<uint8_t>('8' - fields[3][1]) * 8U +
        static_cast<uint8_t>(fields[3][0] - 'a'));
    return true;
}

void ClearRookRightAt(DuckPosition& position,
                      const char rook,
                      const uint8_t square) {
    if (rook == 'R' && square == 63U) {
        position.castlingRights &= static_cast<uint8_t>(~WhiteKingSide);
    } else if (rook == 'R' && square == 56U) {
        position.castlingRights &= static_cast<uint8_t>(~WhiteQueenSide);
    } else if (rook == 'r' && square == 7U) {
        position.castlingRights &= static_cast<uint8_t>(~BlackKingSide);
    } else if (rook == 'r' && square == 0U) {
        position.castlingRights &= static_cast<uint8_t>(~BlackQueenSide);
    }
}

bool AppendCounter(DuckChessFen& fen, const uint32_t value) {
    fxe::FixedString<16> text;
    return text.Format("%lu", static_cast<unsigned long>(value)) &&
           fen.Append(text.View());
}

bool AppendPlacement(const DuckPosition& position, DuckChessFen& outFen) {
    for (uint8_t rank = 0U; rank < 8U; ++rank) {
        uint8_t emptyCount = 0U;
        for (uint8_t file = 0U; file < 8U; ++file) {
            const char piece = position.board[static_cast<size_t>(rank * 8U + file)];
            if (piece == '\0') {
                ++emptyCount;
                continue;
            }
            if (emptyCount != 0U &&
                !outFen.PushBack(static_cast<char>('0' + emptyCount))) {
                return false;
            }
            emptyCount = 0U;
            if (!outFen.PushBack(piece)) {
                return false;
            }
        }
        if (emptyCount != 0U &&
            !outFen.PushBack(static_cast<char>('0' + emptyCount))) {
            return false;
        }
        if (rank != 7U && !outFen.PushBack('/')) {
            return false;
        }
    }
    return true;
}

bool AppendCastling(const DuckPosition& position, DuckChessFen& outFen) {
    if (position.castlingRights == 0U) {
        return outFen.PushBack('-');
    }
    static constexpr char SYMBOLS[] = {'K', 'Q', 'k', 'q'};
    for (uint8_t i = 0U; i < 4U; ++i) {
        if ((position.castlingRights & (1U << i)) != 0U &&
            !outFen.PushBack(SYMBOLS[i])) {
            return false;
        }
    }
    return true;
}

bool AppendEnPassant(const DuckPosition& position, DuckChessFen& outFen) {
    if (position.enPassantSquare >= 64U) {
        return outFen.PushBack('-');
    }
    char square[3] = {};
    return TryFormatUciSquare(position.enPassantSquare, square, sizeof(square)) &&
           outFen.Append(std::string_view(square, 2U));
}

} // namespace

bool IsPiece(const char c) {
    switch (c) {
        case 'P':
        case 'N':
        case 'B':
        case 'R':
        case 'Q':
        case 'K':
        case 'p':
        case 'n':
        case 'b':
        case 'r':
        case 'q':
        case 'k':
            return true;
        default:
            return false;
    }
}

bool IsWhitePiece(const char c) {
    return c >= 'A' && c <= 'Z';
}

bool IsPieceForSide(const char c, const SideToMove side) {
    return IsPiece(c) && (IsWhitePiece(c) == (side == SideToMove::White));
}

char LowerPiece(const char c) {
    return IsWhitePiece(c) ? static_cast<char>(c - 'A' + 'a') : c;
}

bool ParseDuckPosition(const char* fen, DuckPosition& out) {
    out = DuckPosition{};
    std::array<std::string_view, 6> fields{};
    if (!SplitFenFields(fen, fields) || !ParsePlacement(fields[0], out) ||
        !ParseSideAndEnPassant(fields, out) ||
        !ParseCastlingRights(fields[2], out.castlingRights) ||
        !ParseCounter(fields[4], out.halfmoveClock) ||
        !ParseCounter(fields[5], out.fullmoveNumber)) {
        return false;
    }
    return out.fullmoveNumber != 0U;
}

bool BuildFen(const DuckPosition& position, DuckChessFen& outFen) {
    outFen.Clear();
    if (!AppendPlacement(position, outFen) ||
        !outFen.Append(position.sideToMove == SideToMove::White ? " w " : " b ") ||
        !AppendCastling(position, outFen) || !outFen.PushBack(' ') ||
        !AppendEnPassant(position, outFen) || !outFen.PushBack(' ') ||
        !AppendCounter(outFen, position.halfmoveClock) || !outFen.PushBack(' ')) {
        return false;
    }
    return AppendCounter(outFen, position.fullmoveNumber);
}

const CastlingPattern* FindCastle(const uint8_t from,
                                           const uint8_t to) {
    for (const CastlingPattern& pattern : CHESS_CASTLING_PATTERNS) {
        if (pattern.kingFrom == from && pattern.kingTo == to) {
            return &pattern;
        }
    }
    return nullptr;
}

void UpdateCastlingRights(DuckPosition& position,
                          const char mover,
                          const uint8_t from,
                          const char captured,
                          const uint8_t captureSquare) {
    if (mover == 'K') {
        position.castlingRights &= static_cast<uint8_t>(~(WhiteKingSide | WhiteQueenSide));
    } else if (mover == 'k') {
        position.castlingRights &= static_cast<uint8_t>(~(BlackKingSide | BlackQueenSide));
    } else if (LowerPiece(mover) == 'r') {
        ClearRookRightAt(position, mover, from);
    }
    if (LowerPiece(captured) == 'r') {
        ClearRookRightAt(position, captured, captureSquare);
    } else if (captured == 'K') {
        position.castlingRights &= static_cast<uint8_t>(~(WhiteKingSide | WhiteQueenSide));
    } else if (captured == 'k') {
        position.castlingRights &= static_cast<uint8_t>(~(BlackKingSide | BlackQueenSide));
    }
}

} // namespace litch_max::duck_chess_rules_internal
