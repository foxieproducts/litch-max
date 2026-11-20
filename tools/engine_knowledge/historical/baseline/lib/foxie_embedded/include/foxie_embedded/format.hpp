#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fxe {
namespace format {

inline void AppendChar(char* dst, const size_t cap, size_t& pos, const char c, bool& truncated) {
    if (!dst || cap == 0) {
        truncated = true;
        return;
    }
    if (pos + 1 < cap) {
        dst[pos++] = c;
        dst[pos] = '\0';
    } else {
        truncated = true;
    }
}

inline void AppendStr(char* dst, const size_t cap, size_t& pos, const char* s, bool& truncated) {
    if (!dst || cap == 0) {
        truncated = true;
        return;
    }
    if (!s) {
        s = "(null)";
    }
    while (*s && (pos + 1 < cap)) {
        dst[pos++] = *s++;
    }
    dst[pos] = '\0';
    if (*s != '\0') {
        truncated = true;
    }
}

inline size_t UnsignedToBuf(char* buf, const size_t bufCap, uint64_t value, const unsigned base, const bool upper) {
    if (!buf || bufCap == 0 || base < 2 || base > 16) {
        return 0;
    }
    static constexpr char kDigitsLower[] = "0123456789abcdef";
    static constexpr char kDigitsUpper[] = "0123456789ABCDEF";
    const char* digits = upper ? kDigitsUpper : kDigitsLower;
    size_t len = 0;
    do {
        if (len >= bufCap) {
            break;
        }
        buf[len++] = digits[value % base];
        value /= base;
    } while (value != 0);
    return len;
}

inline void AppendPadding(char* dst, const size_t cap, size_t& pos, const char pad, size_t count, bool& truncated) {
    while (count-- > 0) {
        AppendChar(dst, cap, pos, pad, truncated);
        if (truncated) {
            return;
        }
    }
}

inline void AppendUnsigned(char* dst,
                           const size_t cap,
                           size_t& pos,
                           const uint64_t value,
                           const unsigned base,
                           const bool upper,
                           const int width,
                           const bool leftJustify,
                           const bool zeroPad,
                           bool& truncated) {
    char buf[32];
    const size_t len = UnsignedToBuf(buf, sizeof(buf), value, base, upper);
    const size_t total = len;
    const size_t pad = (width > 0 && static_cast<size_t>(width) > total) ? (static_cast<size_t>(width) - total) : 0;

    if (!leftJustify && pad > 0) {
        AppendPadding(dst, cap, pos, zeroPad ? '0' : ' ', pad, truncated);
    }

    for (size_t i = 0; i < len; ++i) {
        AppendChar(dst, cap, pos, buf[len - 1 - i], truncated);
    }

    if (leftJustify && pad > 0) {
        AppendPadding(dst, cap, pos, ' ', pad, truncated);
    }
}

inline void AppendSigned(char* dst,
                         const size_t cap,
                         size_t& pos,
                         const int64_t value,
                         const unsigned base,
                         const bool upper,
                         const int width,
                         const bool leftJustify,
                         const bool zeroPad,
                         bool& truncated) {
    const bool negative = value < 0;
    const uint64_t mag = negative ? static_cast<uint64_t>(-(value + 1)) + 1ULL : static_cast<uint64_t>(value);

    char buf[32];
    const size_t len = UnsignedToBuf(buf, sizeof(buf), mag, base, upper);
    const size_t total = len + (negative ? 1U : 0U);
    const size_t pad = (width > 0 && static_cast<size_t>(width) > total) ? (static_cast<size_t>(width) - total) : 0;

    if (!leftJustify && !zeroPad && pad > 0) {
        AppendPadding(dst, cap, pos, ' ', pad, truncated);
    }

    if (negative) {
        AppendChar(dst, cap, pos, '-', truncated);
    }

    if (!leftJustify && zeroPad && pad > 0) {
        AppendPadding(dst, cap, pos, '0', pad, truncated);
    }

    for (size_t i = 0; i < len; ++i) {
        AppendChar(dst, cap, pos, buf[len - 1 - i], truncated);
    }

    if (leftJustify && pad > 0) {
        AppendPadding(dst, cap, pos, ' ', pad, truncated);
    }
}

inline void AppendFixedFloat(char* dst,
                             const size_t cap,
                             size_t& pos,
                             double value,
                             int precision,
                             bool& truncated) {
    if (precision < 0) {
        precision = 6;
    }
    if (precision > 6) {
        precision = 6;
    }
    if (value < 0) {
        AppendChar(dst, cap, pos, '-', truncated);
        value = -value;
    }
    static const uint64_t pow10[] = {1ULL, 10ULL, 100ULL, 1000ULL, 10000ULL, 100000ULL, 1000000ULL};
    const uint64_t scale = pow10[precision];
    const uint64_t whole = static_cast<uint64_t>(value);
    double fracVal = (value - static_cast<double>(whole)) * static_cast<double>(scale);
    uint64_t frac = static_cast<uint64_t>(fracVal + 0.5);
    if (frac >= scale) {
        frac = 0;
        AppendUnsigned(dst, cap, pos, whole + 1ULL, 10U, false, 0, false, false, truncated);
    } else {
        AppendUnsigned(dst, cap, pos, whole, 10U, false, 0, false, false, truncated);
    }
    if (precision > 0) {
        AppendChar(dst, cap, pos, '.', truncated);
        uint64_t div = scale / 10U;
        for (int i = 0; i < precision; ++i) {
            const unsigned digit = static_cast<unsigned>(frac / div);
            AppendChar(dst, cap, pos, static_cast<char>('0' + digit), truncated);
            frac %= div;
            if (div > 1U) {
                div /= 10U;
            }
        }
    }
}

inline bool FormatAppendV(char* dst, const size_t cap, size_t& pos, const char* fmt, va_list args) {
    if (!dst || cap == 0 || !fmt) {
        return false;
    }

    bool truncated = false;

    for (const char* p = fmt; *p; ++p) {
        if (*p != '%') {
            AppendChar(dst, cap, pos, *p, truncated);
            continue;
        }

        ++p;
        if (*p == '%') {
            AppendChar(dst, cap, pos, '%', truncated);
            continue;
        }

        bool leftJustify = false;
        bool zeroPad = false;
        int width = 0;
        int precision = -1;

        for (;; ++p) {
            if (*p == '-') {
                leftJustify = true;
                continue;
            }
            if (*p == '0') {
                zeroPad = true;
                continue;
            }
            break;
        }

        if (*p == '*') {
            width = va_arg(args, int);
            if (width < 0) {
                leftJustify = true;
                width = -width;
            }
            ++p;
        } else {
            while (*p >= '0' && *p <= '9') {
                width = (width * 10) + (*p - '0');
                ++p;
            }
        }

        if (*p == '.') {
            ++p;
            precision = 0;
            if (*p == '*') {
                precision = va_arg(args, int);
                ++p;
            } else {
                while (*p >= '0' && *p <= '9') {
                    precision = (precision * 10) + (*p - '0');
                    ++p;
                }
            }
        }

        enum class LengthMod {
            None,
            Long,
            LongLong,
            Size
        };
        LengthMod lengthMod = LengthMod::None;
        if (*p == 'l') {
            ++p;
            if (*p == 'l') {
                lengthMod = LengthMod::LongLong;
                ++p;
            } else {
                lengthMod = LengthMod::Long;
            }
        } else if (*p == 'z') {
            lengthMod = LengthMod::Size;
            ++p;
        }

        switch (*p) {
            case 'd':
            case 'i': {
                int64_t v = 0;
                if (lengthMod == LengthMod::LongLong) {
                    v = va_arg(args, long long);
                } else if (lengthMod == LengthMod::Long) {
                    v = va_arg(args, long);
                } else if (lengthMod == LengthMod::Size) {
                    v = static_cast<int64_t>(va_arg(args, size_t));
                } else {
                    v = va_arg(args, int);
                }
                AppendSigned(dst, cap, pos, v, 10U, false, width, leftJustify, zeroPad, truncated);
                break;
            }
            case 'u': {
                uint64_t v = 0;
                if (lengthMod == LengthMod::LongLong) {
                    v = va_arg(args, unsigned long long);
                } else if (lengthMod == LengthMod::Long) {
                    v = va_arg(args, unsigned long);
                } else if (lengthMod == LengthMod::Size) {
                    v = va_arg(args, size_t);
                } else {
                    v = va_arg(args, unsigned int);
                }
                AppendUnsigned(dst, cap, pos, v, 10U, false, width, leftJustify, zeroPad, truncated);
                break;
            }
            case 'x':
            case 'X': {
                const bool upper = (*p == 'X');
                uint64_t v = 0;
                if (lengthMod == LengthMod::LongLong) {
                    v = va_arg(args, unsigned long long);
                } else if (lengthMod == LengthMod::Long) {
                    v = va_arg(args, unsigned long);
                } else if (lengthMod == LengthMod::Size) {
                    v = va_arg(args, size_t);
                } else {
                    v = va_arg(args, unsigned int);
                }
                AppendUnsigned(dst, cap, pos, v, 16U, upper, width, leftJustify, zeroPad, truncated);
                break;
            }
            case 'p': {
                const void* ptr = va_arg(args, const void*);
                AppendStr(dst, cap, pos, "0x", truncated);
                AppendUnsigned(dst,
                               cap,
                               pos,
                               reinterpret_cast<uintptr_t>(ptr),
                               16U,
                               false,
                               0,
                               false,
                               false,
                               truncated);
                break;
            }
            case 'c': {
                AppendChar(dst, cap, pos, static_cast<char>(va_arg(args, int)), truncated);
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) {
                    s = "(null)";
                }
                const size_t len = std::strlen(s);
                const size_t pad = (width > 0 && static_cast<size_t>(width) > len) ? (static_cast<size_t>(width) - len) : 0;
                if (!leftJustify && pad > 0) {
                    AppendPadding(dst, cap, pos, ' ', pad, truncated);
                }
                AppendStr(dst, cap, pos, s, truncated);
                if (leftJustify && pad > 0) {
                    AppendPadding(dst, cap, pos, ' ', pad, truncated);
                }
                break;
            }
            case 'f': {
                AppendFixedFloat(dst, cap, pos, va_arg(args, double), precision, truncated);
                break;
            }
            default:
                AppendChar(dst, cap, pos, '%', truncated);
                AppendChar(dst, cap, pos, *p, truncated);
                break;
        }
    }

    if (cap > 0) {
        dst[(pos < cap) ? pos : (cap - 1)] = '\0';
    }
    return !truncated;
}

inline bool FormatToBufferV(char* dst, const size_t cap, const char* fmt, va_list args, size_t* outLen = nullptr) {
    if (!dst || cap == 0 || !fmt) {
        if (outLen) {
            *outLen = 0;
        }
        return false;
    }

    size_t pos = 0;
    const bool ok = FormatAppendV(dst, cap, pos, fmt, args);
    if (outLen) {
        *outLen = pos;
    }
    return ok;
}

} // namespace format
} // namespace fxe