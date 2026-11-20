#include "classic_chess_repetition.hpp"
#include "classic_chess_identity.hpp"

namespace litch_max::classic_chess::internal {
namespace {

constexpr uint64_t Mix(uint64_t value) {
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

template<size_t Count>
constexpr std::array<uint64_t, Count> Keys(const uint64_t seed) {
    std::array<uint64_t, Count> keys{};
    for (size_t index = 0U; index < Count; ++index)
        keys[index] = Mix(seed + (index + 1U) * 0x9e3779b97f4a7c15ULL);
    return keys;
}

// Six piece kinds per side plus two moved-home-pawn identities. Compile-time
// tables occupy flash; they allocate no per-search or per-node RAM.
constexpr auto PIECES = Keys<14U * 64U>(0x6c69746368726570ULL);
constexpr auto RIGHTS = Keys<16U>(0x636173746c696e67ULL);
constexpr auto EP = Keys<129U>(0x656e70617373616eULL);
constexpr auto SIDES = Keys<2U>(0x73696465746f6d76ULL);

uint64_t PieceKey(const uint8_t raw, const uint8_t square) {
    const uint8_t piece = CanonicalPiece(raw, square);
    const uint8_t type = piece & 7U;
    if (type == 0U) return 0U;
    const unsigned side = (piece & 24U) == MCUMAX_BOARD_WHITE ? 0U : 1U;
    const unsigned kind = type < 3U ? 0U : type - 2U;
    const unsigned group = type < 3U && (piece & MCUMAX_PIECE_MOVED) != 0U
        ? 12U + side : side * 6U + kind;
    const unsigned logical = ((square & 0x70U) >> 1U) | (square & 7U);
    return PIECES[group * 64U + logical];
}

uint64_t Auxiliary(mcumax_position& position) {
    return RIGHTS[CastlingRights(position)] ^ EP[EffectiveEnPassant(position)] ^
        SIDES[position.side == MCUMAX_BOARD_WHITE ? 0U : 1U];
}

} // namespace

RepetitionIdentity ComputeRepetitionIdentity(mcumax_position& position) {
    RepetitionIdentity identity;
    identity.auxiliary = Auxiliary(position);
    identity.key = identity.auxiliary;
    for (uint8_t square = 0U; square < 128U; ++square) {
        if ((square & 0x88U) != 0U) { square += 7U; continue; }
        identity.key ^= PieceKey(position.board[square], square);
    }
    return identity;
}

RepetitionIdentity AdvanceRepetitionIdentity(const RepetitionIdentity& parent,
    mcumax_position& child, const mcumax_position_undo& undo) {
    RepetitionIdentity identity;
    identity.auxiliary = Auxiliary(child);
    identity.key = parent.key ^ parent.auxiliary ^ identity.auxiliary;
    for (uint8_t index = 0U; index < undo.count; ++index) {
        const uint8_t square = undo.square[index];
        identity.key ^= PieceKey(undo.piece[index], square) ^ PieceKey(child.board[square], square);
    }
    return identity;
}

} // namespace litch_max::classic_chess::internal
