#include <stdint.h>

#include "mcu-max.h"

#include "upstream_test.h"

static uint32_t stop_after_nodes;

static void stop_callback(void* userdata)
{
    (void)userdata;
    if (mcumax.node_count >= stop_after_nodes)
        mcumax_stop_search();
}

static bool same_move(const mcumax_move lhs, const mcumax_move rhs)
{
    return lhs.from == rhs.from &&
           (lhs.to & 0x77u) == (rhs.to & 0x77u);
}

int main(void)
{
    mcumax_init();

    mcumax_root_candidate candidates[MCUMAX_ROOT_CANDIDATE_CAPACITY];
    MCUMAX_TEST_CHECK(mcumax_get_last_root_candidates(
                          candidates, MCUMAX_ROOT_CANDIDATE_CAPACITY) == 0u);
    MCUMAX_TEST_CHECK(mcumax_get_last_root_candidates(NULL, 1u) == 0u);

    const mcumax_move best = mcumax_search_best_move(20000u, 8u);
    const uint8_t count = mcumax_get_last_root_candidates(
        candidates, MCUMAX_ROOT_CANDIDATE_CAPACITY);

    MCUMAX_TEST_CHECK(best.from != MCUMAX_SQUARE_INVALID);
    MCUMAX_TEST_CHECK(count == MCUMAX_ROOT_CANDIDATE_CAPACITY);
    MCUMAX_TEST_CHECK(same_move(candidates[0].move, best));
    for (uint8_t i = 0; i < count; ++i)
        MCUMAX_TEST_CHECK(mcumax_is_move_legal_strict(candidates[i].move));

    const int32_t best_score_before_query = mcumax.best_score;
    mcumax_move legal_moves[256];
    MCUMAX_TEST_CHECK(mcumax_search_valid_moves(legal_moves, 256u) > 0u);
    MCUMAX_TEST_CHECK(mcumax.best_move_valid);
    MCUMAX_TEST_CHECK(mcumax.best_score == best_score_before_query);
    MCUMAX_TEST_CHECK(mcumax_get_last_root_candidates(
                          candidates, MCUMAX_ROOT_CANDIDATE_CAPACITY) == count);

    MCUMAX_TEST_CHECK(mcumax_play_move(best));
    MCUMAX_TEST_CHECK(mcumax_get_last_root_candidates(
                          candidates, MCUMAX_ROOT_CANDIDATE_CAPACITY) == 0u);

    // A pinned/castling position previously let an illegal e1g1 candidate
    // consume one of the three slots before the accessor filtered it out.
    mcumax_set_fen_position(
        "4r1k1/8/8/8/8/2N5/3Q4/4K2R w K - 0 1");
    const mcumax_move pinned_best = mcumax_search_best_move(20000u, 8u);
    const uint8_t pinned_count = mcumax_get_last_root_candidates(
        candidates, MCUMAX_ROOT_CANDIDATE_CAPACITY);
    MCUMAX_TEST_CHECK(pinned_count == MCUMAX_ROOT_CANDIDATE_CAPACITY);
    MCUMAX_TEST_CHECK(same_move(candidates[0].move, pinned_best));
    for (uint8_t i = 0; i < pinned_count; ++i)
        MCUMAX_TEST_CHECK(mcumax_is_move_legal_strict(candidates[i].move));

    // A callback stop may interrupt an iteration. The candidate set must still
    // describe the same last completed iteration as the returned best move.
    mcumax_init();
    mcumax_set_callback(NULL, NULL);
    const mcumax_move budget_best = mcumax_search_best_move(5120u, 14u);
    mcumax_root_candidate budget_candidates[MCUMAX_ROOT_CANDIDATE_CAPACITY];
    const uint8_t budget_count = mcumax_get_last_root_candidates(
        budget_candidates, MCUMAX_ROOT_CANDIDATE_CAPACITY);
    const int32_t budget_score = mcumax.best_score;

    mcumax_init();
    stop_after_nodes = 5120u;
    mcumax_set_callback(stop_callback, NULL);
    const mcumax_move stopped_best = mcumax_search_best_move(100000u, 14u);
    mcumax_set_callback(NULL, NULL);
    const uint8_t stopped_count = mcumax_get_last_root_candidates(
        candidates, MCUMAX_ROOT_CANDIDATE_CAPACITY);
    MCUMAX_TEST_CHECK(stopped_best.from != MCUMAX_SQUARE_INVALID);
    MCUMAX_TEST_CHECK(same_move(stopped_best, budget_best));
    MCUMAX_TEST_CHECK(stopped_count == budget_count);
    MCUMAX_TEST_CHECK(mcumax.best_score == budget_score);
    for (uint8_t i = 0; i < stopped_count; ++i)
        MCUMAX_TEST_CHECK(same_move(candidates[i].move,
                                    budget_candidates[i].move));

    // Exhaustion before a complete root returns invalid, never a legal move
    // with fabricated score or candidate provenance.
    mcumax_init();
    const mcumax_move unavailable = mcumax_search_best_move(1u, 2u);
    MCUMAX_TEST_CHECK(unavailable.from == MCUMAX_SQUARE_INVALID &&
                      unavailable.to == MCUMAX_SQUARE_INVALID && !mcumax.best_move_valid);
    MCUMAX_TEST_CHECK(mcumax_get_last_root_candidates(
                          candidates, MCUMAX_ROOT_CANDIDATE_CAPACITY) == 0u);

    // A checked position must return genuinely searched legal work, or an
    // explicit invalid result with cleared provenance. An arbitrary legal
    // replacement for a rejected pseudo-legal root is never sufficient.
    mcumax_set_fen_position(
        "r5nr/p2b1kpp/2p1p2B/1P1pqp2/PbNPn1PN/8/RPQ1PPBP/4K2R w K - 0 1");
    const mcumax_move checked_best =
        mcumax_search_best_move(20000u, 8u);
    if (checked_best.from == MCUMAX_SQUARE_INVALID) {
        MCUMAX_TEST_CHECK(!mcumax.best_move_valid);
        MCUMAX_TEST_CHECK(mcumax_get_last_root_candidates(
                              candidates, MCUMAX_ROOT_CANDIDATE_CAPACITY) == 0u);
    } else {
        MCUMAX_TEST_CHECK(mcumax_is_move_legal_strict(checked_best));
        MCUMAX_TEST_CHECK(mcumax.best_move_valid);
        MCUMAX_TEST_CHECK(same_move(checked_best, (mcumax_move){
            mcumax.best_square_from, mcumax.best_square_to}));
    }

    mcumax_set_fen_position(
        "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    MCUMAX_TEST_CHECK(mcumax_get_last_root_candidates(
                          candidates, MCUMAX_ROOT_CANDIDATE_CAPACITY) == 0u);

    return mcumax_test_finish();
}
