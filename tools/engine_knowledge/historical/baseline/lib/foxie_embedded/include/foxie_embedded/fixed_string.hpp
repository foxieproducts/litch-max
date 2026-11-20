#pragma once

#include <array>
#include <cstdarg>
#include <cstring>
#include <string_view>

#include "foxie_embedded/format.hpp"

namespace fxe {

template <size_t CAPACITY>
class FixedString final {
  public:
    static_assert(CAPACITY >= 1, "FixedString capacity must include space for null terminator");

    FixedString() {
        Clear();
    }

    explicit FixedString(const std::string_view s) {
        Clear();
        (void)Append(s);
    }

    void Clear() {
        m_size = 0;
        m_buf[0] = '\0';
    }

    [[nodiscard]] constexpr size_t Capacity() const {
        return CAPACITY;
    }

    [[nodiscard]] size_t Size() const {
        return m_size;
    }

    [[nodiscard]] bool Empty() const {
        return m_size == 0;
    }

    [[nodiscard]] const char* CStr() const {
        return m_buf.data();
    }

    [[nodiscard]] std::string_view View() const {
        return std::string_view(m_buf.data(), m_size);
    }

    [[nodiscard]] size_t Remaining() const {
        return (CAPACITY - 1) - m_size;
    }

    [[nodiscard]] bool Assign(const std::string_view s) {
        Clear();
        return Append(s);
    }

    [[nodiscard]] bool Append(const std::string_view s) {
        const size_t toCopy = (s.size() <= Remaining()) ? s.size() : Remaining();
        if (toCopy > 0) {
            std::memcpy(m_buf.data() + m_size, s.data(), toCopy);
            m_size += toCopy;
        }
        m_buf[m_size] = '\0';
        return toCopy == s.size();
    }

    [[nodiscard]] bool PushBack(const char c) {
        if (Remaining() == 0) {
            return false;
        }
        m_buf[m_size++] = c;
        m_buf[m_size] = '\0';
        return true;
    }

    [[nodiscard]] bool Equals(const std::string_view s) const {
        return View() == s;
    }

    [[nodiscard]] bool operator==(const std::string_view s) const {
        return Equals(s);
    }

    [[nodiscard]] bool operator==(const char* s) const {
        if (s == nullptr) {
            return Empty();
        }
        return Equals(std::string_view(s));
    }

    template <size_t OTHER_CAPACITY>
    [[nodiscard]] bool operator==(const FixedString<OTHER_CAPACITY>& other) const {
        return View() == other.View();
    }

    [[nodiscard]] bool operator!=(const std::string_view s) const {
        return !(*this == s);
    }

    [[nodiscard]] bool operator!=(const char* s) const {
        return !(*this == s);
    }

    template <size_t OTHER_CAPACITY>
    [[nodiscard]] bool operator!=(const FixedString<OTHER_CAPACITY>& other) const {
        return !(*this == other);
    }

    FixedString& operator+=(const char c) {
        (void)PushBack(c);
        return *this;
    }

    FixedString& operator+=(const std::string_view s) {
        (void)Append(s);
        return *this;
    }

    FixedString& operator+=(const char* s) {
        if (s != nullptr) {
            (void)Append(std::string_view(s));
        }
        return *this;
    }

    template <size_t OTHER_CAPACITY>
    FixedString& operator+=(const FixedString<OTHER_CAPACITY>& other) {
        (void)Append(other.View());
        return *this;
    }

    friend bool operator==(const std::string_view lhs, const FixedString& rhs) {
        return rhs == lhs;
    }

    friend bool operator==(const char* lhs, const FixedString& rhs) {
        return rhs == lhs;
    }

    friend bool operator!=(const std::string_view lhs, const FixedString& rhs) {
        return !(rhs == lhs);
    }

    friend bool operator!=(const char* lhs, const FixedString& rhs) {
        return !(rhs == lhs);
    }

    [[nodiscard]] bool StartsWith(const std::string_view prefix) const {
        const auto v = View();
        if (prefix.size() > v.size()) {
            return false;
        }
        return v.substr(0, prefix.size()) == prefix;
    }

    [[nodiscard]] bool VFormat(const char* fmt, va_list args) {
        if (!fmt) {
            Clear();
            return false;
        }

        size_t written = 0;
        va_list argsCopy;
        va_copy(argsCopy, args);
        const bool ok = fxe::format::FormatToBufferV(m_buf.data(), CAPACITY, fmt, argsCopy, &written);
        va_end(argsCopy);

        m_size = (written < CAPACITY) ? written : (CAPACITY - 1);
        m_buf[m_size] = '\0';
        return ok;
    }

    [[nodiscard]] bool Format(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        const bool ok = VFormat(fmt, args);
        va_end(args);
        return ok;
    }

  private:
    std::array<char, CAPACITY> m_buf{};
    size_t m_size = 0;
};

} // namespace fxe
