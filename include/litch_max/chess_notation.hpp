#pragma once

#include <cstddef>
#include <cstdint>

#include "litch_max/types.hpp"

namespace litch_max {

inline bool IsUciFileChar(const char c) {
    return c >= 'a' && c <= 'h';
}

inline bool IsUciRankChar(const char c) {
    return c >= '1' && c <= '8';
}

inline bool IsUciPromotionSuffix(const char c) {
    return c == 'q' || c == 'r' || c == 'b' || c == 'n' || c == 'Q' || c == 'R' || c == 'B' || c == 'N';
}

inline char NormalizeUciPromotionSuffixLower(const char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

// Converts a UCI square (e.g. "e2") to the project square index (0=a8 .. 63=h1).
// Only the first two characters are consumed.
inline bool TryParseUciSquare(const char* s, uint8_t& outSquare) {
    if (s == nullptr) {
        return false;
    }

    const char fileC = s[0];
    const char rankC = s[1];

    if (!IsUciFileChar(fileC) || !IsUciRankChar(rankC)) {
        return false;
    }

    const uint8_t file = static_cast<uint8_t>(fileC - 'a');
    const uint8_t rankFromTop = static_cast<uint8_t>('8' - rankC);

    if (file >= 8 || rankFromTop >= 8) {
        return false;
    }

    outSquare = static_cast<uint8_t>(rankFromTop * 8 + file);
    return true;
}

inline bool TryFormatUciSquare(const uint8_t square, char* out, const size_t outSize) {
    if (out == nullptr || outSize < 3) {
        return false;
    }
    if (square >= 64) {
        out[0] = '\0';
        return false;
    }

    const uint8_t file = static_cast<uint8_t>(square % 8);
    const uint8_t rankFromTop = static_cast<uint8_t>(square / 8);

    out[0] = static_cast<char>('a' + file);
    out[1] = static_cast<char>('8' - rankFromTop);
    out[2] = '\0';
    return true;
}

struct ParsedUciMove {
    uint8_t fromSquare = 0;
    uint8_t toSquare = 0;
    char promotionSuffixLower = '\0';
    bool hasPromotion = false;
};

// Parses "e2e4" or "e7e8q". Strict: only lengths 4 and 5 are accepted.
inline bool TryParseUciMove(const char* s, ParsedUciMove& out) {
    if (s == nullptr) {
        return false;
    }

    const char c0 = s[0];
    const char c1 = s[1];
    const char c2 = s[2];
    const char c3 = s[3];

    if (c0 == '\0' || c1 == '\0' || c2 == '\0' || c3 == '\0') {
        return false;
    }

    uint8_t from = 0;
    uint8_t to = 0;
    if (!TryParseUciSquare(s + 0, from)) {
        return false;
    }
    if (!TryParseUciSquare(s + 2, to)) {
        return false;
    }

    out.fromSquare = from;
    out.toSquare = to;
    out.hasPromotion = false;
    out.promotionSuffixLower = '\0';

    const char c4 = s[4];
    if (c4 == '\0') {
        return true;
    }

    const char c5 = s[5];
    if (c5 != '\0') {
        return false;
    }

    if (!IsUciPromotionSuffix(c4)) {
        return false;
    }

    out.hasPromotion = true;
    out.promotionSuffixLower = NormalizeUciPromotionSuffixLower(c4);
    return true;
}

inline bool TryFormatUciMove(const uint8_t fromSquare,
                             const uint8_t toSquare,
                             const char promotionSuffixOrNull,
                             UciMove& outUci) {
    if (fromSquare >= 64 || toSquare >= 64) {
        return false;
    }

    char fromBuf[3] = {0};
    char toBuf[3] = {0};

    if (!TryFormatUciSquare(fromSquare, fromBuf, sizeof(fromBuf))) {
        return false;
    }
    if (!TryFormatUciSquare(toSquare, toBuf, sizeof(toBuf))) {
        return false;
    }

    outUci.Clear();
    if (!outUci.Append(fromBuf)) {
        return false;
    }
    if (!outUci.Append(toBuf)) {
        return false;
    }

    if (promotionSuffixOrNull == '\0') {
        return true;
    }

    if (!IsUciPromotionSuffix(promotionSuffixOrNull)) {
        return false;
    }

    const char promoLower = NormalizeUciPromotionSuffixLower(promotionSuffixOrNull);
    char promoBuf[2] = {promoLower, '\0'};
    return outUci.Append(promoBuf);
}

} // namespace litch_max
