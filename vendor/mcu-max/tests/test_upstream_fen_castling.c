#include <string.h>

#include "mcu-max.h"

#include "upstream_test.h"

static void expect_fen(const char* input, const char* expected)
{
    char actual[128] = {0};

    mcumax_set_fen_position(input);
    mcumax_get_fen(actual, sizeof(actual));

    MCUMAX_TEST_CHECK(strcmp(actual, expected) == 0);
}

static void expect_move_fen(const char* input,
                            const mcumax_move move,
                            const char* expected)
{
    char actual[128] = {0};

    mcumax_set_fen_position(input);
    MCUMAX_TEST_CHECK(mcumax_play_move(move));
    mcumax_get_fen(actual, sizeof(actual));

    MCUMAX_TEST_CHECK(strcmp(actual, expected) == 0);
}

int main(void)
{
    expect_fen(
        "8/8/8/8/8/8/8/8 w - - 0 1",
        "8/8/8/8/8/8/8/8 w - - 0 1");
    expect_fen(
        "4k3/8/8/8/8/8/8/4K3 w - - 7 42",
        "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    expect_fen(
        "b3k2b/8/8/8/8/8/8/B3K2B w KQkq - 0 1",
        "b3k2b/8/8/8/8/8/8/B3K2B w - - 0 1");
    expect_fen(
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    expect_fen(
        "4k3/8/8/8/8/8/8/4K2R w K - 0 1",
        "4k3/8/8/8/8/8/8/4K2R w K - 0 1");
    expect_move_fen(
        "4k3/8/8/8/8/8/8/4K2R w K - 0 1",
        (mcumax_move){0x74, 0x76},
        "4k3/8/8/8/8/8/8/5RK1 b - - 0 1");
    expect_move_fen(
        "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1",
        (mcumax_move){0x64, 0x44},
        "4k3/8/8/8/4P3/8/8/4K3 b - e3 0 1");

    return mcumax_test_finish();
}
