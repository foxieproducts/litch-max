// Offline driver linked only with a pinned original Litch engine snapshot.
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "modules/classic_chess_search.hpp"

namespace {
mcumax_position InitialPosition() {
    mcumax_position position{};
    constexpr uint8_t BACK_RANK[]{MCUMAX_ROOK, MCUMAX_KNIGHT, MCUMAX_BISHOP,
        MCUMAX_QUEEN, MCUMAX_KING, MCUMAX_BISHOP, MCUMAX_KNIGHT, MCUMAX_ROOK};
    for (uint8_t file = 0U; file < 8U; ++file) {
        position.board[file] = BACK_RANK[file] | MCUMAX_BOARD_BLACK;
        position.board[16U + file] = MCUMAX_PAWN_DOWNSTREAM | MCUMAX_BOARD_BLACK;
        position.board[96U + file] = MCUMAX_PAWN_UPSTREAM | MCUMAX_BOARD_WHITE;
        position.board[112U + file] = BACK_RANK[file] | MCUMAX_BOARD_WHITE;
    }
    position.side = MCUMAX_BOARD_WHITE;
    position.en_passant = MCUMAX_SQUARE_INVALID;
    position.king_square[0] = 116U;
    position.king_square[1] = 4U;
    return position;
}

bool ParseMove(const std::string& text, mcumax_position_move& move) {
    if (text.size() != 4U && text.size() != 5U) return false;
    for (unsigned index : {0U, 2U})
        if (text[index] < 'a' || text[index] > 'h' ||
            text[index + 1U] < '1' || text[index + 1U] > '8') return false;
    move = {static_cast<uint8_t>((8 - (text[1] - '0')) * 16 + text[0] - 'a'),
            static_cast<uint8_t>((8 - (text[3] - '0')) * 16 + text[2] - 'a'), 0U};
    if (text.size() == 5U) {
        switch (text[4]) {
            case 'q': move.promotion = MCUMAX_QUEEN; break;
            case 'r': move.promotion = MCUMAX_ROOK; break;
            case 'b': move.promotion = MCUMAX_BISHOP; break;
            case 'n': move.promotion = MCUMAX_KNIGHT; break;
            default: return false;
        }
    }
    return true;
}

char PieceChar(const uint8_t piece) {
    constexpr char TYPES[]{'.', 'p', 'p', 'n', 'k', 'b', 'r', 'q'};
    const char type = TYPES[piece & 7U];
    return type != '.' && (piece & 24U) == MCUMAX_BOARD_WHITE
        ? static_cast<char>(type - 'a' + 'A') : type;
}

uint8_t CastleRights(const mcumax_position& position) {
    uint8_t rights = 0U;
    if (position.board[116] == (MCUMAX_KING | MCUMAX_BOARD_WHITE)) {
        if (position.board[119] == (MCUMAX_ROOK | MCUMAX_BOARD_WHITE)) rights |= 1U;
        if (position.board[112] == (MCUMAX_ROOK | MCUMAX_BOARD_WHITE)) rights |= 2U;
    }
    if (position.board[4] == (MCUMAX_KING | MCUMAX_BOARD_BLACK)) {
        if (position.board[7] == (MCUMAX_ROOK | MCUMAX_BOARD_BLACK)) rights |= 4U;
        if (position.board[0] == (MCUMAX_ROOK | MCUMAX_BOARD_BLACK)) rights |= 8U;
    }
    return rights;
}
} // namespace

int main(int argc, char** argv) {
    const unsigned depth = argc == 2 ? static_cast<unsigned>(std::stoul(argv[1])) : 5U;
    if (depth < 5U || depth > 16U) return 2;
    std::string request;
    while (std::getline(std::cin, request)) {
        std::istringstream input(request);
        unsigned index = 0U, line = 0U, ply = 0U;
        if (!(input >> index >> line >> ply) || ply > 16U) return 3;
        auto position = InitialPosition();
        std::string text;
        for (unsigned previous = 0U; previous < ply; ++previous) {
            mcumax_position_move move{};
            mcumax_position_undo undo{};
            if (!(input >> text) || !ParseMove(text, move) ||
                !mcumax_position_make(&position, move, &undo)) return 4;
        }
        if (input >> text) return 5;
        const auto original = position;
        const uint64_t key = modules::classic_chess::PositionKey(position);
        modules::classic_chess::SearchWorkspace workspace;
        modules::classic_chess::SearchConfig config;
        config.maxDepth = static_cast<uint8_t>(depth);
        config.minimumDepth = static_cast<uint8_t>(depth);
        config.maxNodes = 0U;
        config.firstRootNodeReserve = 0U;
        const auto result = workspace.Run(position, config);
        mcumax_position_undo undo{};
        const bool legal = result.available && mcumax_position_make(&position, result.move, &undo);
        if (legal) mcumax_position_unmake(&position, &undo);
        const bool restored = std::memcmp(&original, &position, sizeof(position)) == 0;
        std::string board;
        for (uint8_t square = 0U; square < 128U; ++square)
            if ((square & 0x88U) == 0U) board += PieceChar(position.board[square]);
        std::printf("{\"index\":%u,\"line\":%u,\"ply\":%u,\"key\":\"%016llx\","
            "\"from\":%u,\"to\":%u,\"promotion\":%u,\"score\":%d,\"depth\":%u,"
            "\"nodes\":%u,\"available\":%s,\"preliminary\":%s,\"interrupted\":%s,"
            "\"legal\":%s,\"restored\":%s,\"board\":\"%s\",\"side\":%u,\"ep\":%u,"
            "\"castling\":%u}\n", index, line, ply, static_cast<unsigned long long>(key),
            result.move.from, result.move.to, result.move.promotion, result.score,
            result.completedDepth, result.nodes, result.available ? "true" : "false",
            result.preliminaryOnly ? "true" : "false", result.interrupted ? "true" : "false",
            legal ? "true" : "false", restored ? "true" : "false", board.c_str(),
            position.side, position.en_passant, CastleRights(position));
        std::fflush(stdout);
        if (!result.available || result.completedDepth < depth || result.preliminaryOnly ||
            result.interrupted || !legal || !restored) return 6;
    }
    return 0;
}
