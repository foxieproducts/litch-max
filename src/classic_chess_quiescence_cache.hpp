#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace litch_max::classic_chess::internal {

// Separate from ordinary move ordering. Callers admit only real positions
// immediately after an irreversible move, and check terminal/history first.
class QuiescenceCache {
public:
    bool Probe(const uint64_t key, const uint8_t ply, const bool quietChecks,
               const int alpha, const int beta, int& score) const {
        const std::size_t slot = key & (CAPACITY - 1U);
        const uint8_t metadata = m_metadata[slot];
        if (m_keys[slot] != key || (metadata & CONTEXT_MASK) != Context(ply, quietChecks)) return false;
        const uint8_t bound = metadata & BOUND_MASK;
        const int value = m_scores[slot];
        if (bound != EXACT && !(bound == LOWER && value >= beta) &&
            !(bound == UPPER && value <= alpha)) return false;
        score = value;
        return true;
    }

    void Store(const uint64_t key, const uint8_t ply, const bool quietChecks,
               const int score, const int alpha, const int beta) {
        const std::size_t slot = key & (CAPACITY - 1U);
        m_keys[slot] = key;
        m_scores[slot] = static_cast<int16_t>(score);
        const uint8_t bound = score <= alpha ? UPPER : score >= beta ? LOWER : EXACT;
        m_metadata[slot] = Context(ply, quietChecks) | bound;
    }

private:
    static constexpr std::size_t CAPACITY = 128U;
    static constexpr uint8_t EXACT = 1U, LOWER = 2U, UPPER = 3U;
    static constexpr uint8_t BOUND_MASK = 3U, CONTEXT_MASK = 252U;

    static uint8_t Context(const uint8_t ply, const bool quietChecks) {
        return static_cast<uint8_t>((ply << 3U) | (quietChecks ? 4U : 0U));
    }

    // Split storage avoids padding between entries in the fixed workspace.
    std::array<uint64_t, CAPACITY> m_keys{};
    std::array<int16_t, CAPACITY> m_scores{};
    std::array<uint8_t, CAPACITY> m_metadata{};
};
static_assert(sizeof(QuiescenceCache) == 1408U);

} // namespace litch_max::classic_chess::internal
