#include <string.h>
#include "mcu-max.h"
#include "upstream_test.h"
static char root_fen[180];
static unsigned publications;
static bool coherent;
static mcumax_move published;
static unsigned peak_recursion;
static void record_peak(void* context) {
    (void)context;
    if (mcumax.recursion_depth > peak_recursion)
        peak_recursion = mcumax.recursion_depth;
}
static void check_quiescence_mates(void) {
    // A quiet mate must search the checked response; a checking capture must
    // not be discarded by futility after an apparently free queen capture.
    const char* positions[] = {
        "5k2/8/8/8/8/5q1b/5P1P/6K1 b - - 0 1",
        "kr6/8/8/8/1Q4q1/5b2/5PPP/6K1 b - - 0 1",
    };
    for (unsigned i = 0U; i < 2U; ++i) {
        mcumax_set_fen_position(positions[i]);
        mcumax.recursion_limit = 0U;
        const mcumax_move move = mcumax_search_best_move(100000U, 0U);
        MCUMAX_TEST_CHECK(mcumax.best_move_valid);
        MCUMAX_TEST_CHECK(mcumax_play_move(move));
        MCUMAX_TEST_CHECK(mcumax_is_in_checkmate(mcumax.current_side));
        MCUMAX_TEST_CHECK(mcumax.recursion_depth == 0U);
    }
}
static void node_stop(void* context) {
    (void)context;
    if (mcumax.node_count >= 20000U) mcumax_stop_search();
}
static void completed(void* context) {
    char fen[180];
    (void)context;
    mcumax_get_fen(fen, sizeof(fen));
    coherent = coherent && strcmp(root_fen, fen) == 0;
    published = (mcumax_move){mcumax.best_square_from, mcumax.best_square_to};
    coherent = coherent && mcumax.best_move_valid && mcumax_is_move_legal_strict(published);
    ++publications;
    if (publications == 3U) mcumax_stop_search();
}
int main(void) {
    mcumax_init();
    mcumax_get_fen(root_fen, sizeof(root_fen));
    coherent = true;
    mcumax_set_root_completed_callback(completed, NULL);
    const mcumax_move best = mcumax_search_best_move(0xffffffffU, 96U);
    mcumax_set_root_completed_callback(NULL, NULL);
    MCUMAX_TEST_CHECK(coherent && publications == 3U);
    MCUMAX_TEST_CHECK(best.from == published.from && best.to == published.to);
    MCUMAX_TEST_CHECK(mcumax.recursion_depth == 0U);
    char fen[180]; mcumax_get_fen(fen, sizeof(fen));
    MCUMAX_TEST_CHECK(strcmp(root_fen, fen) == 0);
    mcumax.recursion_limit = 4U;
    mcumax_set_callback(node_stop, NULL);
    const mcumax_move horizon_best = mcumax_search_best_move(0xffffffffU, 96U);
    mcumax_set_callback(NULL, NULL);
    MCUMAX_TEST_CHECK(mcumax.recursion_horizon_nodes > 0U);
    MCUMAX_TEST_CHECK(mcumax.node_count >= 20000U);
    MCUMAX_TEST_CHECK(mcumax.best_move_valid && mcumax_is_move_legal_strict(horizon_best));
    MCUMAX_TEST_CHECK(mcumax.recursion_depth == 0U);
    mcumax_get_fen(fen, sizeof(fen));
    MCUMAX_TEST_CHECK(strcmp(root_fen, fen) == 0);
    // Bound extensions in checked positions for either side; leaf scores must
    // retain negamax sign/king legality and never mutate the root position.
    const char* checks[] = {
        "4r1k1/8/8/8/8/2N5/3Q4/4K2R w K - 0 1",
        "4k2r/3q4/2n5/8/8/8/8/4R1K1 b k - 0 1",
    };
    for (unsigned index = 0U; index < 2U; ++index) {
        mcumax_set_fen_position(checks[index]);
        mcumax_get_fen(root_fen, sizeof(root_fen));
        mcumax_set_callback(node_stop, NULL);
        const mcumax_move checked_best = mcumax_search_best_move(0xffffffffU, 96U);
        mcumax_set_callback(NULL, NULL);
        MCUMAX_TEST_CHECK(mcumax_is_move_legal_strict(checked_best));
        MCUMAX_TEST_CHECK(mcumax.recursion_depth == 0U);
        mcumax_get_fen(fen, sizeof(fen));
        MCUMAX_TEST_CHECK(strcmp(root_fen, fen) == 0);
    }
    // Default zero means the finite policy bound, including forcing checks.
    mcumax_set_fen_position(
        "r1b2rk1/ppp2ppp/8/1NbBp3/2QnPn2/8/PPPP1PqP/1RB1K2R w K - 0 12");
    mcumax_get_fen(root_fen, sizeof(root_fen));
    mcumax.recursion_limit = 0U;
    peak_recursion = 0U;
    mcumax_set_callback(record_peak, NULL);
    const mcumax_move bounded_best = mcumax_search_best_move(225000U, 8U);
    mcumax_set_callback(NULL, NULL);
    MCUMAX_TEST_CHECK(peak_recursion <= 20U);
    MCUMAX_TEST_CHECK(mcumax.recursion_horizon_nodes > 0U);
    MCUMAX_TEST_CHECK(mcumax.node_count <= 225001U);
    MCUMAX_TEST_CHECK(mcumax.recursion_depth == 0U);
    MCUMAX_TEST_CHECK(mcumax_is_move_legal_strict(bounded_best));
    mcumax_get_fen(fen, sizeof(fen));
    MCUMAX_TEST_CHECK(strcmp(root_fen, fen) == 0);
    check_quiescence_mates();
    return mcumax_test_finish();
}
