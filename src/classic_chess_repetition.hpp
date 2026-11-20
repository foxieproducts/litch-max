#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mcu-max-position.h>

namespace litch_max::classic_chess::internal {

// This transient search identity never replaces the public/book FNV key or
// ordinary transposition-cache indexing. Auxiliary rules state is retained so
// make/unmake need only hash the undo record's changed squares.
struct RepetitionIdentity {
    uint64_t key = 0U;
    uint64_t auxiliary = 0U;
};

RepetitionIdentity ComputeRepetitionIdentity(mcumax_position& position);
RepetitionIdentity AdvanceRepetitionIdentity(const RepetitionIdentity& parent,
    mcumax_position& child, const mcumax_position_undo& undo);

template<size_t Capacity>
class RepetitionPath {
public:
    void Reset(mcumax_position& root) {
        m_positions[0U] = ComputeRepetitionIdentity(root);
    }
    void MadeChild(mcumax_position& child, const mcumax_position_undo& undo, const uint8_t ply) {
        m_positions[ply + 1U] = AdvanceRepetitionIdentity(m_positions[ply], child, undo);
    }
    void MadeNullChild(mcumax_position& child, const uint8_t ply) {
        m_positions[ply + 1U] = ComputeRepetitionIdentity(child);
    }
    uint64_t Key(const uint8_t ply) const { return m_positions[ply].key; }
    bool Repeated(const uint8_t ply, const uint8_t first) const {
        uint8_t count = 0U;
        for (int previous = static_cast<int>(ply) - 2; previous >= first; previous -= 2) {
            if (m_positions[previous].key == m_positions[ply].key && ++count == 2U) return true;
        }
        return false;
    }
private:
    std::array<RepetitionIdentity, Capacity> m_positions{};
};

} // namespace litch_max::classic_chess::internal
