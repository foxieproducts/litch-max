#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "mcu-max.h"

#include "upstream_test.h"

typedef bool (*position_query)(uint8_t side);

static void count_callback(void* userdata)
{
    int* calls = userdata;
    (*calls)++;
}

static void expect_pure_query(const char* fen,
                              const uint8_t side,
                              position_query query,
                              const bool expected)
{
    int callback_calls = 0;
    mcumax_move sentinel_moves[] = {{1, 2}, {3, 4}};
    const mcumax_move sentinel_before[] = {{1, 2}, {3, 4}};

    mcumax_set_fen_position(fen);
    mcumax.node_count = 0x12345678u;
    mcumax.valid_moves_buffer = sentinel_moves;
    mcumax.valid_moves_buffer_size = 2;
    mcumax.valid_moves_num = 1;
    mcumax.valid_moves_fast_enabled = false;
    mcumax.valid_moves_filter_king_safety = false;
    mcumax_set_callback(count_callback, &callback_calls);

    const mcumax_struct before = mcumax;
    const bool actual = query(side);

    MCUMAX_TEST_CHECK(actual == expected);
    MCUMAX_TEST_CHECK(callback_calls == 0);
    MCUMAX_TEST_CHECK(memcmp(&mcumax, &before, sizeof(mcumax)) == 0);
    MCUMAX_TEST_CHECK(
        memcmp(sentinel_moves, sentinel_before, sizeof(sentinel_moves)) == 0);
}

int main(void)
{
    expect_pure_query(
        "7k/6Q1/7K/8/8/8/8/8 b - - 0 1",
        MCUMAX_BOARD_BLACK,
        mcumax_is_in_checkmate,
        true);
    expect_pure_query(
        "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1",
        MCUMAX_BOARD_BLACK,
        mcumax_is_stalemate,
        true);
    expect_pure_query(
        "4k3/8/8/8/8/8/8/4K3 w - - 0 1",
        MCUMAX_BOARD_WHITE,
        mcumax_is_stalemate,
        false);
    expect_pure_query(
        "7k/8/8/8/8/8/8/K6R b - - 0 1",
        MCUMAX_BOARD_BLACK,
        mcumax_is_in_checkmate,
        false);

    return mcumax_test_finish();
}
