#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "mcu-max-position.h"
#include "upstream_test.h"

static unsigned callback_calls;
static void counted_callback(void* context) {
    (void)context;
    ++callback_calls;
}

static mcumax_position load(const char* fen) {
    mcumax_position position = {{0}, 0, 0, {0, 0}};
    mcumax_set_fen_position(fen);
    MCUMAX_TEST_CHECK(mcumax_position_import(&position, &mcumax));
    return position;
}

static unsigned tactical_positions;
static unsigned tactical_moves;

static void check_tactical_iterator(const mcumax_position* position) {
    mcumax_position_iterator all, tactical;
    mcumax_position_move expected, actual;
    const mcumax_position before = *position;
    mcumax_position_iterator_init(&all);
    mcumax_position_iterator_init(&tactical);
    ++tactical_positions;
    while (mcumax_position_iterator_next(position, &all, &expected)) {
        const uint8_t type = position->board[expected.from] & 7U;
        const bool ep = type < 3 && expected.to == position->en_passant &&
                        (expected.from & 7U) != (expected.to & 7U);
        if (!position->board[expected.to] && !expected.promotion && !ep) continue;
        MCUMAX_TEST_CHECK(mcumax_position_iterator_next_tactical(position, &tactical, &actual));
        MCUMAX_TEST_CHECK(expected.from == actual.from && expected.to == actual.to &&
                          expected.promotion == actual.promotion);
        ++tactical_moves;
    }
    MCUMAX_TEST_CHECK(!mcumax_position_iterator_next_tactical(position, &tactical, &actual));
    MCUMAX_TEST_CHECK(memcmp(position, &before, sizeof(before)) == 0);
}

/* Strict make remains the geometry oracle. Every input here comes from the
 * actual iterator, including pseudo-legal moves rejected by king safety. */
static bool check_generated_make(mcumax_position* position,
                                  mcumax_position_move move,
                                  mcumax_position_undo* generated_undo) {
    const mcumax_position before = *position;
    mcumax_position strict = before;
    mcumax_position_undo strict_undo;
    memset(&strict_undo, 0xa5, sizeof(strict_undo));
    memset(generated_undo, 0xa5, sizeof(*generated_undo));
    const bool strict_ok = mcumax_position_make(&strict, move, &strict_undo);
    const bool generated_ok = mcumax_position_make_generated(position, move, generated_undo);
    MCUMAX_TEST_CHECK(strict_ok == generated_ok);
    MCUMAX_TEST_CHECK(memcmp(&strict, position, sizeof(strict)) == 0);
    MCUMAX_TEST_CHECK(memcmp(&strict_undo, generated_undo, sizeof(strict_undo)) == 0);
    if (strict_ok) mcumax_position_unmake(&strict, &strict_undo);
    MCUMAX_TEST_CHECK(memcmp(&strict, &before, sizeof(before)) == 0);
    return generated_ok;
}

static uint64_t perft(mcumax_position* position, unsigned depth) {
    if (!depth) return 1;
    check_tactical_iterator(position);
    const mcumax_position before = *position;
    mcumax_position_iterator iterator;
    mcumax_position_move move;
    uint64_t nodes = 0;
    mcumax_position_iterator_init(&iterator);
    while (mcumax_position_iterator_next(position, &iterator, &move)) {
        mcumax_position_undo undo;
        if (check_generated_make(position, move, &undo)) {
            nodes += perft(position, depth - 1);
            mcumax_position_unmake(position, &undo);
        }
        MCUMAX_TEST_CHECK(memcmp(position, &before, sizeof(before)) == 0);
    }
    return nodes;
}

static void print_move(mcumax_position_move move) {
    printf("%c%c%c%c", 'a' + (move.from & 7), '8' - (move.from >> 4),
           'a' + (move.to & 7), '8' - (move.to >> 4));
    if (move.promotion) {
        const char suffix = move.promotion == MCUMAX_QUEEN ? 'q' :
                            move.promotion == MCUMAX_ROOK ? 'r' :
                            move.promotion == MCUMAX_BISHOP ? 'b' : 'n';
        putchar(suffix);
    }
}

static bool has_move(mcumax_position* position, uint8_t from, uint8_t to,
                     uint8_t promotion) {
    mcumax_position_iterator iterator;
    mcumax_position_move move;
    mcumax_position_iterator_init(&iterator);
    while (mcumax_position_iterator_next_legal(position, &iterator, &move))
        if (move.from == from && move.to == to && move.promotion == promotion)
            return true;
    return false;
}

static void test_tactical_pawns(void) {
    static const uint8_t promotions[] = {
        MCUMAX_QUEEN, MCUMAX_ROOK, MCUMAX_BISHOP, MCUMAX_KNIGHT
    };
    static const struct {
        const char* name;
        const char* fen;
        uint8_t from, to, count;
    } cases[] = {
        {"white_promotion", "7k/P7/8/8/8/8/8/7K w - - 0 1", 16, 0, 4},
        {"black_promotion", "7k/8/8/8/8/8/p7/7K b - - 0 1", 96, 112, 4},
        {"white_capture", "7k/8/8/2p5/3P4/8/8/7K w - - 0 1", 67, 50, 1},
        {"black_capture", "7k/8/8/3p4/2P5/8/8/7K b - - 0 1", 51, 66, 1},
        {"white_ep", "7k/8/8/3pP3/8/8/8/7K w - d6 0 1", 52, 35, 1},
        {"black_ep", "7k/8/8/8/3Pp3/8/8/7K b - d3 0 1", 68, 83, 1},
        {"white_home", "7k/8/8/8/8/8/3P4/7K w - - 0 1", 99, 128, 0},
        {"black_home", "7k/3p4/8/8/8/8/8/7K b - - 0 1", 19, 128, 0}
    };
    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        mcumax_position position = load(cases[index].fen);
        const mcumax_position before = position;
        if (cases[index].count != 4) {
            const int step = position.side == MCUMAX_BOARD_WHITE ? -16 : 16;
            MCUMAX_TEST_CHECK(has_move(&position, cases[index].from,
                                      cases[index].from + step, 0));
            if (!cases[index].count)
                MCUMAX_TEST_CHECK(has_move(&position, cases[index].from,
                                          cases[index].from + 2 * step, 0));
        }
        mcumax_position_iterator iterator;
        mcumax_position_move move = {MCUMAX_SQUARE_INVALID, MCUMAX_SQUARE_INVALID, 0};
        mcumax_position_iterator_init(&iterator);
        for (unsigned choice = 0; choice < cases[index].count; ++choice) {
            MCUMAX_TEST_CHECK(mcumax_position_iterator_next_tactical(&position, &iterator, &move));
            MCUMAX_TEST_CHECK(move.from == cases[index].from && move.to == cases[index].to &&
                              move.promotion == (cases[index].count == 4 ? promotions[choice] : 0));
        }
        MCUMAX_TEST_CHECK(!mcumax_position_iterator_next_tactical(&position, &iterator, &move));
        check_tactical_iterator(&position);
        MCUMAX_TEST_CHECK(memcmp(&position, &before, sizeof(before)) == 0);
        printf("tactical pawns %s choices=%u\n", cases[index].name, cases[index].count);
    }
}

static void expect_rejected(mcumax_position* position, mcumax_position_move move) {
    const mcumax_position before = *position;
    mcumax_position_undo undo;
    MCUMAX_TEST_CHECK(!mcumax_position_make(position, move, &undo));
    MCUMAX_TEST_CHECK(memcmp(position, &before, sizeof(before)) == 0);
}

static void test_edge_cases(void) {
    mcumax_position position = load("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1");
    expect_rejected(&position, (mcumax_position_move){100, 4, 0}); // King capture.
    MCUMAX_TEST_CHECK(!has_move(&position, 100, 4, 0));
    expect_rejected(&position, (mcumax_position_move){100, 83, 0}); // Rook diagonal.
    expect_rejected(&position, (mcumax_position_move){128, 0, 0});
    expect_rejected(&position, (mcumax_position_move){100, 84, MCUMAX_KNIGHT});
    position = load("8/8/8/r4pPK/8/8/8/4k3 w - f6 0 1");
    expect_rejected(&position, (mcumax_position_move){54, 37, 0});
    MCUMAX_TEST_CHECK(!has_move(&position, 54, 37, 0)); // Pinned EP.
    position = load("r3k2r/8/8/8/2b5/8/8/R3K2R w KQkq - 0 1");
    expect_rejected(&position, (mcumax_position_move){116, 118, 0});
    MCUMAX_TEST_CHECK(has_move(&position, 116, 114, 0));
    position = load("r3k2r/8/8/8/8/8/4r3/R3K2R w KQkq - 0 1");
    expect_rejected(&position, (mcumax_position_move){116, 118, 0});
    expect_rejected(&position, (mcumax_position_move){116, 114, 0});
    position = load("1r5k/P7/8/8/8/8/8/7K w - - 0 1");
    const uint8_t promotions[] = {MCUMAX_QUEEN, MCUMAX_ROOK, MCUMAX_BISHOP, MCUMAX_KNIGHT};
    for (unsigned index = 0; index < 4; ++index) {
        MCUMAX_TEST_CHECK(has_move(&position, 16, 0, promotions[index]));
        MCUMAX_TEST_CHECK(has_move(&position, 16, 1, promotions[index]));
    }
    expect_rejected(&position, (mcumax_position_move){16, 0, MCUMAX_KING});
    mcumax_position_undo undo;
    const mcumax_position before_promotion = position;
    MCUMAX_TEST_CHECK(mcumax_position_make(&position, (mcumax_position_move){16, 0, 0}, &undo));
    MCUMAX_TEST_CHECK((position.board[0] & 7U) == MCUMAX_QUEEN);
    mcumax_position_unmake(&position, &undo);
    MCUMAX_TEST_CHECK(memcmp(&position, &before_promotion, sizeof(position)) == 0);

    position = load("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
    const mcumax_position before_rights = position;
    mcumax_position_undo rights_undo[4];
    MCUMAX_TEST_CHECK(mcumax_position_make(&position, (mcumax_position_move){119, 103, 0}, &rights_undo[0]));
    MCUMAX_TEST_CHECK(mcumax_position_make(&position, (mcumax_position_move){4, 5, 0}, &rights_undo[1]));
    MCUMAX_TEST_CHECK(mcumax_position_make(&position, (mcumax_position_move){103, 119, 0}, &rights_undo[2]));
    MCUMAX_TEST_CHECK((position.board[119] & MCUMAX_PIECE_MOVED) != 0);
    MCUMAX_TEST_CHECK(mcumax_position_make(&position, (mcumax_position_move){5, 4, 0}, &rights_undo[3]));
    MCUMAX_TEST_CHECK(!has_move(&position, 116, 118, 0));
    for (int index = 3; index >= 0; --index) mcumax_position_unmake(&position, &rights_undo[index]);
    MCUMAX_TEST_CHECK(memcmp(&position, &before_rights, sizeof(position)) == 0);
    MCUMAX_TEST_CHECK(has_move(&position, 116, 118, 0));

    position = load("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
    MCUMAX_TEST_CHECK(mcumax_position_make(&position, (mcumax_position_move){100, 68, 0}, &undo));
    MCUMAX_TEST_CHECK(position.en_passant == 84);
    MCUMAX_TEST_CHECK(mcumax_position_make(&position, (mcumax_position_move){4, 5, 0}, &rights_undo[0]));
    MCUMAX_TEST_CHECK(position.en_passant == MCUMAX_SQUARE_INVALID);
}

static void test_import_and_legacy_purity(void) {
    mcumax_init();
    mcumax_position destination;
    memset(&destination, 0xa5, sizeof(destination));
    const mcumax_position before = destination;
    mcumax_struct invalid = mcumax;
    invalid.board[4] = 0;
    MCUMAX_TEST_CHECK(!mcumax_position_import(&destination, &invalid));
    MCUMAX_TEST_CHECK(memcmp(&destination, &before, sizeof(before)) == 0);
    invalid = mcumax;
    invalid.board[3] = MCUMAX_BOARD_BLACK | MCUMAX_KING;
    MCUMAX_TEST_CHECK(!mcumax_position_import(&destination, &invalid));
    invalid = mcumax;
    invalid.en_passant_square = 36;
    MCUMAX_TEST_CHECK(!mcumax_position_import(&destination, &invalid));
    MCUMAX_TEST_CHECK(!mcumax_position_import(&destination, NULL));
    MCUMAX_TEST_CHECK(!mcumax_position_import(NULL, &mcumax));

    mcumax.node_count = 123456;
    mcumax.stop_search = true;
    mcumax.stop_due_to_node_budget = true;
    mcumax_set_callback(counted_callback, NULL);
    const mcumax_struct saved = mcumax;
    MCUMAX_TEST_CHECK(mcumax_is_move_legal_strict((mcumax_move){100, 68}));
    MCUMAX_TEST_CHECK(!mcumax_is_move_legal_strict((mcumax_move){113, 81}));
    MCUMAX_TEST_CHECK(!mcumax_is_in_check(MCUMAX_BOARD_WHITE));
    MCUMAX_TEST_CHECK(!mcumax_is_in_checkmate(MCUMAX_BOARD_WHITE));
    MCUMAX_TEST_CHECK(!mcumax_is_stalemate(MCUMAX_BOARD_WHITE));
    MCUMAX_TEST_CHECK(memcmp(&mcumax, &saved, sizeof(saved)) == 0);
    mcumax_move buffer[3] = {{128, 128}, {128, 128}, {77, 88}};
    MCUMAX_TEST_CHECK(mcumax_search_valid_moves_fast(buffer, 2) == 20);
    MCUMAX_TEST_CHECK(buffer[2].from == 77 && buffer[2].to == 88);
    MCUMAX_TEST_CHECK(mcumax.node_count == 123456 && mcumax.stop_search && mcumax.stop_due_to_node_budget);
    MCUMAX_TEST_CHECK(callback_calls == 0);
    MCUMAX_TEST_CHECK(mcumax_search_valid_moves_fast(NULL, 0) == 20);
    mcumax_set_callback(NULL, NULL);
}

int main(int argc, char** argv) {
    MCUMAX_TEST_CHECK(sizeof(mcumax_position) == 132);
    MCUMAX_TEST_CHECK(sizeof(mcumax_position_move) == 3);
    MCUMAX_TEST_CHECK(sizeof(mcumax_position_undo) == 13);
    MCUMAX_TEST_CHECK(sizeof(mcumax_position_iterator) == 5);
    if (argc == 3) {
        mcumax_position position = load(argv[1]);
        const uint64_t nodes = perft(&position, strtoul(argv[2], NULL, 10));
        printf("{\"nodes\":%" PRIu64 ",\"moves\":[", nodes);
        mcumax_position_iterator iterator;
        mcumax_position_move move;
        bool first = true;
        mcumax_position_iterator_init(&iterator);
        while (mcumax_position_iterator_next_legal(&position, &iterator, &move)) {
            printf("%s\"", first ? "" : ","); print_move(move); putchar('"'); first = false;
        }
        printf("]}\n");
        return mcumax_test_finish();
    }
    // Independent python-chess1.11.2 counts; retained reference receipt records
    // complete root move sets as well as these tree counts.
    static const struct { const char* name; const char* fen; unsigned depth; uint64_t nodes; } cases[] = {
        {"initial", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862},
        {"rook_ep_endgame", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238},
        {"promotions_and_castling", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 9467},
        {"ep_discovered_check", "8/8/8/r4pPK/8/8/8/4k3 w - f6 0 1", 3, 316},
        {"castle_through_check", "r3k2r/8/8/8/2b5/8/8/R3K2R w KQkq - 0 1", 3, 16883},
        {"castle_out_of_check", "r3k2r/8/8/8/8/8/4r3/R3K2R w KQkq - 0 1", 3, 2060},
        {"all_promotions", "1r5k/P7/8/8/8/8/8/7K w - - 0 1", 3, 1034}
    };
    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        mcumax_position position = load(cases[index].fen);
        const mcumax_struct source = mcumax;
        const uint64_t actual = perft(&position, cases[index].depth);
        printf("perft %s depth=%u nodes=%" PRIu64 " expected=%" PRIu64 "\n",
               cases[index].name, cases[index].depth, actual, cases[index].nodes);
        MCUMAX_TEST_CHECK(actual == cases[index].nodes);
        MCUMAX_TEST_CHECK(memcmp(&mcumax, &source, sizeof(source)) == 0);
    }
    test_tactical_pawns();
    test_edge_cases();
    test_import_and_legacy_purity();
    printf("tactical parity positions=%u moves=%u\n", tactical_positions, tactical_moves);
    return mcumax_test_finish();
}
