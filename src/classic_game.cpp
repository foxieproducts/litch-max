#include "litch_max/classic_game.hpp"

#include <array>
#include <cstring>

#include "classic_chess_fen.hpp"

namespace litch_max {
namespace {
void ApplyFixedEngineOptions() {
    mcumax.valid_moves_fast_enabled = true;
    mcumax.valid_moves_filter_king_safety = true;
}
} // namespace

void ClassicGame::SetHashSeed(const uint32_t seed) {
#ifdef MCUMAX_HASHING_ENABLED
    mcumax_hash_seed = seed;
#else
    (void)seed;
#endif
}

void ClassicGame::Reset(const uint32_t hashSeed) {
    SetHashSeed(hashSeed);
    mcumax_init();
    ApplyFixedEngineOptions();
}

void ClassicGame::PrepareSearch() { ApplyFixedEngineOptions(); }

bool ClassicGame::Import(ClassicPosition& position) const {
    return mcumax_position_import(&position, &mcumax);
}

bool ClassicGame::SetFen(const char* fen, const uint32_t hashSeed) {
    mcumax_struct state{};
    if (!classic_chess::internal::TryParseFen(fen, state)) {
        return false;
    }

    // FEN validation is complete before resetting hash/search state. Publish
    // the validated chess state without reparsing through the legacy decoder.
    Reset(hashSeed);
    for (uint8_t square = 0U; square < 128U; ++square) {
        if ((square & 0x88U) == 0U) mcumax.board[square] = state.board[square];
    }
    mcumax.current_side = state.current_side;
    mcumax.en_passant_square = state.en_passant_square;
    return true;
}

bool ClassicGame::GetFen(char* outFen, const size_t outSize) const {
    if (outFen == nullptr || outSize == 0) {
        return false;
    }

    std::memset(outFen, 0, outSize);
    mcumax_get_fen(outFen, outSize);
    return outFen[0] != '\0';
}

SideToMove ClassicGame::GetSideToMove() const {
    const mcumax_piece side = mcumax_get_current_side();
    return (side == MCUMAX_BOARD_BLACK) ? SideToMove::Black : SideToMove::White;
}

ClassicGame::PieceSide ClassicGame::GetPieceSideAtSquare(const uint8_t square) const {
    if (square >= 64U) {
        return PieceSide::None;
    }

    const uint8_t rankNibble = static_cast<uint8_t>(square / 8U);
    const uint8_t file = static_cast<uint8_t>(square % 8U);
    const mcumax_square sq = static_cast<mcumax_square>((rankNibble << 4) | file);

    const mcumax_piece p = mcumax_get_piece(sq);
    // mcu-max encodes pieces with a color bit, and also encodes empty squares as
    // (MCUMAX_EMPTY ^ MCUMAX_BLACK) == 0x8. Do not treat that as a black piece.
    const mcumax_piece type = static_cast<mcumax_piece>(p & 0x7);
    if (type == MCUMAX_EMPTY) {
        return PieceSide::None;
    }

    return ((p & MCUMAX_BLACK) != 0) ? PieceSide::Black : PieceSide::White;
}

bool ClassicGame::IsInCheck(const SideToMove side) const {
    const uint8_t s = (side == SideToMove::White) ? MCUMAX_BOARD_WHITE : MCUMAX_BOARD_BLACK;
    return mcumax_is_in_check(s);
}

bool ClassicGame::IsInCheckmate(const SideToMove side) const {
    const uint8_t s = (side == SideToMove::White) ? MCUMAX_BOARD_WHITE : MCUMAX_BOARD_BLACK;
    return mcumax_is_in_checkmate(s);
}

bool ClassicGame::IsInStalemate(const SideToMove side) const {
    const uint8_t s = (side == SideToMove::White) ? MCUMAX_BOARD_WHITE : MCUMAX_BOARD_BLACK;
    return mcumax_is_stalemate(s);
}

bool ClassicGame::GetLegalDestinationsMask(const uint8_t fromSquare, uint64_t& outDestMask64) {
    outDestMask64 = 0;

    if (fromSquare >= 64U) {
        return false;
    }

    ApplyFixedEngineOptions();

    const uint8_t rankNibble = static_cast<uint8_t>(fromSquare / 8U);
    const uint8_t file = static_cast<uint8_t>(fromSquare % 8U);
    const mcumax_square from = static_cast<mcumax_square>((rankNibble << 4) | file);

    std::array<mcumax_move, 256> moves;
    const uint32_t moveCount = mcumax_search_valid_moves(moves.data(), static_cast<uint32_t>(moves.size()));
    const uint32_t storedCount = (moveCount > moves.size()) ? static_cast<uint32_t>(moves.size()) : moveCount;

    for (uint32_t i = 0; i < storedCount; ++i) {
        const mcumax_move mv = moves[i];
        if (mv.from != from) {
            continue;
        }

        if (mv.to == MCUMAX_SQUARE_INVALID) {
            continue;
        }

        const mcumax_square toSanitized =
            static_cast<mcumax_square>(
                mv.to & static_cast<mcumax_square>(~0x88U));

        const uint8_t toFile = static_cast<uint8_t>(toSanitized & 0x0F);
        const uint8_t toRankNibble = static_cast<uint8_t>((toSanitized >> 4) & 0x0F);
        if (toFile >= 8U || toRankNibble >= 8U) {
            continue;
        }

        const uint8_t toSquare = static_cast<uint8_t>(toRankNibble * 8U + toFile);
        outDestMask64 |= (1ULL << toSquare);
    }

    return true;
}

} // namespace litch_max
