/*
 * mcu-max
 * Chess game engine for low-resource MCUs
 *
 * (C) 2022-2024 Gissio
 *
 * License: MIT
 *
 * Based on micro-Max 4.8 by H.G. Muller.
 * Compliant with FIDE laws (except for underpromotion).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mcu-max.h"
#include "mcu-max-position.h"
#include "mcu-max-platform.h"

// Configuration
// #define MCUMAX_HASHING_ENABLED

// Constants
#define MCUMAX_BOARD_MASK 0x88
#define MCUMAX_BOARD_WHITE 0x8
#define MCUMAX_BOARD_BLACK 0x10
#define MCUMAX_PIECE_MOVED 0x20
#define MCUMAX_SCORE_MAX 8000
#define MCUMAX_DEPTH_MAX 99

// NOTE: This fork keeps best-move selection deterministic and strictly
// score-maximizing. We do not do randomized tie-breaking at the root.

enum mcumax_mode {
    MCUMAX_INTERNAL_NODE,
    MCUMAX_SEARCH_VALID_MOVES,
    MCUMAX_SEARCH_BEST_MOVE,
    MCUMAX_PLAY_MOVE,
};

mcumax_struct mcumax;

static int16_t mcumax_clamp_root_score(const int32_t score) {
    if (score > INT16_MAX)
        return INT16_MAX;
    if (score < INT16_MIN)
        return INT16_MIN;
    return (int16_t)score;
}

static bool mcumax_same_root_move(const mcumax_move lhs,
                                  const mcumax_move rhs) {
    return lhs.from == rhs.from &&
           (lhs.to & ~MCUMAX_BOARD_MASK) == (rhs.to & ~MCUMAX_BOARD_MASK);
}

static void mcumax_consider_root_candidate(const mcumax_move move,
                                           const int32_t score) {
    mcumax_root_candidate candidate = {
        {move.from, move.to & ~MCUMAX_BOARD_MASK},
    };
    const int16_t candidate_score = mcumax_clamp_root_score(score);
    uint8_t count = mcumax.root_candidate_work_count;

    for (uint8_t i = 0; i < count; ++i) {
        if (!mcumax_same_root_move(
                mcumax.root_candidates_work[i].move, candidate.move))
            continue;

        if (candidate_score <= mcumax.root_candidate_work_scores[i])
            return;

        for (uint8_t j = i; (j + 1U) < count; ++j) {
            mcumax.root_candidates_work[j] =
                mcumax.root_candidates_work[j + 1U];
            mcumax.root_candidate_work_scores[j] =
                mcumax.root_candidate_work_scores[j + 1U];
        }
        count--;
        break;
    }

    uint8_t insert_at = 0;
    while ((insert_at < count) &&
           (mcumax.root_candidate_work_scores[insert_at] >= candidate_score))
        insert_at++;

    if (insert_at >= MCUMAX_ROOT_CANDIDATE_CAPACITY)
        return;

    if (count < MCUMAX_ROOT_CANDIDATE_CAPACITY)
        count++;

    for (uint8_t i = count - 1U; i > insert_at; --i) {
        mcumax.root_candidates_work[i] =
            mcumax.root_candidates_work[i - 1U];
        mcumax.root_candidate_work_scores[i] =
            mcumax.root_candidate_work_scores[i - 1U];
    }

    mcumax.root_candidates_work[insert_at] = candidate;
    mcumax.root_candidate_work_scores[insert_at] = candidate_score;
    mcumax.root_candidate_work_count = count;
}

static void mcumax_commit_root_candidates(void) {
    mcumax.root_candidate_count = mcumax.root_candidate_work_count;
    for (uint8_t i = 0; i < mcumax.root_candidate_count; ++i)
        mcumax.root_candidates[i] = mcumax.root_candidates_work[i];
}

static const int8_t mcumax_capture_values[] = {
    0, 2, 2, 7, -1, 8, 12, 23};

static const int8_t mcumax_step_vectors_indices[] = {
    0, 7, -1, 11, 6, 8, 3, 6};

static const int8_t mcumax_step_vectors[] = {
    // Upstream pawn
    -16, -15, -17, 0,
    // Rook
    1, 16, 0,
    // King, queen
    1, 16, 15, 17, 0,
    // Knight
    14, 18, 31, 33, 0};

static const int8_t mcumax_board_setup[] = {
    MCUMAX_ROOK,
    MCUMAX_KNIGHT,
    MCUMAX_BISHOP,
    MCUMAX_QUEEN,
    MCUMAX_KING,
    MCUMAX_BISHOP,
    MCUMAX_KNIGHT,
    MCUMAX_ROOK,
};

#ifdef MCUMAX_HASHING_ENABLED

#define MCUMAX_HASH_SCRAMBLE_TABLE_SIZE 1035
#define MCUMAX_HASH_TABLE_SIZE (1 << 24)

#define HashScramble(A, B)               \
    *(uint32_t*)(mcumax_scramble_table + \
                 A + (B & 8) + MCUMAX_SQUARE_INVALID * (B & 0b111))
#define Hash(A)                                                      \
    HashScramble(step_square_to + A, mcumax.board[step_square_to]) - \
        HashScramble(scan_square_from + A, scan_piece) -             \
        HashScramble(capture_square + A, capture_piece)

static uint8_t mcumax_scramble_table[MCUMAX_HASH_SCRAMBLE_TABLE_SIZE]; /* hash translation table */

struct HashEntry {
    uint32_t key2;
    int32_t score;
    uint8_t square_from;
    uint8_t square_to;
    uint8_t depth;
};

static struct HashEntry mcumax_hash_table[MCUMAX_HASH_TABLE_SIZE];

#endif

typedef bool (*mcumax_move_callback)(mcumax_move move);

static int32_t mcumax_search(int32_t alpha,
                             int32_t beta,
                             int32_t score,
                             uint8_t en_passant_square,
                             uint8_t depth,
                             enum mcumax_mode mode);

// Recursive minimax search
// (alpha,beta)=window, score=current evaluation score, en_passant_square=e.p. sqr.
// depth=depth, in_root=in_root; returns score
static int32_t mcumax_search(int32_t alpha,
                             int32_t beta,
                             int32_t score,
                             uint8_t en_passant_square,
                             uint8_t depth,
                             enum mcumax_mode mode) {
    if (mcumax.stop_search)
        return score;

    if (mode == MCUMAX_INTERNAL_NODE &&
        mcumax_is_in_check(mcumax.current_side ^ 0x18U)) {
        ++mcumax.node_count;
        MCUMAX_PLATFORM_COOPERATE(mcumax.node_count);
        if (mcumax.user_callback && ((mcumax.node_count & 0xFFu) == 0u))
            mcumax.user_callback(mcumax.user_data);
        if (mcumax.node_max != 0U && mcumax.node_count >= mcumax.node_max) {
            mcumax.stop_search = true;
            mcumax.stop_due_to_node_budget = true;
        }
        return MCUMAX_SCORE_MAX;
    }

    if (mcumax.recursion_depth >= (mcumax.recursion_limit != 0U ? mcumax.recursion_limit : 20U)) {
        // A bounded static leaf, not a global abort. The incoming incremental
        // score is already relative to this side to move (recursive callers
        // negate both input and output). Preserve the king-capture legality
        // sentinel: an attacked previous mover's king makes its move illegal.
        ++mcumax.recursion_horizon_nodes;
        ++mcumax.node_count;
        MCUMAX_PLATFORM_COOPERATE(mcumax.node_count);
        if (mcumax.user_callback && ((mcumax.node_count & 0xFFu) == 0u))
            mcumax.user_callback(mcumax.user_data);
        if (mcumax.node_max != 0U && mcumax.node_count >= mcumax.node_max) {
            mcumax.stop_search = true;
            mcumax.stop_due_to_node_budget = true;
        }
        if (mcumax.stop_search)
            return score;
        return mcumax_is_in_check(mcumax.current_side ^ 0x18U)
                   ? MCUMAX_SCORE_MAX : score;
    }
    ++mcumax.recursion_depth;

    // The engine calls this callback primarily for cancellation.
    // Calling it on every node is expensive on MCUs; sample it periodically.
    // Using node_count keeps this deterministic and amortizes call overhead.
    if (mcumax.user_callback && ((mcumax.node_count & 0xFFu) == 0u)) {
        mcumax.user_callback(mcumax.user_data);
    }

    uint8_t iter_depth;
    int32_t iter_score;
    uint8_t iter_square_from;
    uint8_t iter_square_to;

#ifdef MCUMAX_HASHING_ENABLED
    int32_t hash_key;
    int32_t hash_key2;
#endif

    uint8_t square_start;

    uint8_t square_from;
    uint8_t square_to;

    uint8_t replay_move;

    uint8_t scan_piece;
    uint8_t scan_piece_type;

    int8_t step_vector;
    int8_t step_vector_index;

    uint8_t castling_skip_square;
    uint8_t castling_rook_square;

    uint8_t capture_square;
    uint8_t capture_piece;
    int32_t capture_piece_value;

    uint8_t step_depth;
    int32_t step_alpha;
    int32_t step_score;
    int32_t step_score_new;

    // Root iterative-deepening: track whether this iteration actually updated
    // the best move, so we only commit a completed iteration's result.
    bool iter_best_valid;

    // Adj. window: delay bonus
    alpha -= alpha < score;
    beta -= beta <= score;

#ifdef MCUMAX_HASHING_ENABLED
    // Lookup pos. in hash table
    struct HashEntry* hash_entry = mcumax_hash_table +
                                   ((mcumax.hash_key +
                                     mcumax.current_side * en_passant_square) &
                                    MCUMAX_HASH_TABLE_SIZE - 1);

    iter_depth = hash_entry->depth;
    iter_score = hash_entry->score;
    iter_square_from = hash_entry->square_from;
    iter_square_to = hash_entry->square_to;

    // Resume at stored depth
    if ((hash_entry->key2 != mcumax.hash_key2) ||
        (mode != MCUMAX_INTERNAL_NODE) || // Miss: other pos. or empty
        !(((iter_score <= alpha) ||
           (iter_square_from & 0x8)) &&
          ((iter_score >= beta) ||
           (iter_square_from & MCUMAX_SQUARE_INVALID)))) // Or window incompatible
    {
        // Start iteration from scratch
        iter_depth =
            iter_square_to = 0;
    }

    // Start at best-move hint
    iter_square_from &= ~MCUMAX_BOARD_MASK;

    hash_key = mcumax.hash_key;
    hash_key2 = mcumax.hash_key2;
#else
    iter_depth =
        iter_score =
            iter_square_from =
                iter_square_to = 0;
#endif

    // Check state governs evasions, castling legality and terminal scoring.
    const bool side_in_check_before = mcumax_is_in_check(mcumax.current_side);

    // Min depth = 2 iterative deepening loop
    // root: deepen upto time
    // time's up: go do best
    while ((iter_depth++ < depth) ||
           (iter_depth < 3) ||
           ((mode != MCUMAX_INTERNAL_NODE) &&
            (mcumax.square_from == MCUMAX_SQUARE_INVALID) &&
            (((mcumax.node_count < mcumax.node_max) &&
              (iter_depth <= mcumax.depth_max)) ||
             (mcumax.square_from = iter_square_from,
              mcumax.square_to = iter_square_to & ~MCUMAX_BOARD_MASK,
              iter_depth = 3)))) {
        if (mcumax.stop_search)
            break;

        // Node budget: only stop *between* completed iterations.
        //
        // Cutting off in the middle of an iteration can yield a partially
        // evaluated best move (bad). We prefer the last fully-completed
        // iteration's move (stored in mcumax.best_*).
        if ((mode == MCUMAX_SEARCH_BEST_MOVE) &&
            (mcumax.square_from == MCUMAX_SQUARE_INVALID) &&
            (mcumax.node_max != 0) &&
            (mcumax.node_count >= mcumax.node_max)) {
            break;
        }

        iter_best_valid = false;

        if ((mode == MCUMAX_SEARCH_BEST_MOVE) &&
            (mcumax.square_from == MCUMAX_SQUARE_INVALID))
            mcumax.root_candidate_work_count = 0;

        // Start scan at previous best
        square_from =
            square_start = (mode != MCUMAX_SEARCH_VALID_MOVES)
                               ? iter_square_from
                               : 0;

        // Request try noncastling first
        // Exhaustive enumeration starts at square zero, not the previous best origin.
        replay_move = (mode == MCUMAX_SEARCH_VALID_MOVES)
                          ? 0
                          : (iter_square_to & MCUMAX_SQUARE_INVALID);

        // No null-move pruning: true check state owns check and terminal semantics.
        iter_score = ((iter_depth != 2) || side_in_check_before)
                         ? -MCUMAX_SCORE_MAX : score;

        // Node count (for timing)
        mcumax.node_count++;
        MCUMAX_PLATFORM_COOPERATE(mcumax.node_count);

        // Tight node budget enforcement.
        //
        // Stopping only at iteration boundaries can overshoot node_max by large
        // multiples. Instead, once we hit the budget, set a stop flag that
        // causes recursive calls to unwind quickly.
        //
        // IMPORTANT: we do not treat any partially evaluated iteration as a
        // valid "best move". The caller will use mcumax.best_* which is only
        // updated at completed iteration boundaries.
        if ((mcumax.node_max != 0) &&
            (mcumax.square_from == MCUMAX_SQUARE_INVALID) &&
            (mcumax.node_count >= mcumax.node_max)) {
            mcumax.stop_search = true;
            mcumax.stop_due_to_node_budget = true;
            goto cutoff;
        }

        do {
            // Scan board looking for
            scan_piece = mcumax.board[square_from];

            // Own piece (inefficient!)
            if (scan_piece & mcumax.current_side) {
                // p = piece type (set r>0)
                step_vector = scan_piece_type = (scan_piece & 0b111);

                // First step vector for piece
                step_vector_index = mcumax_step_vectors_indices[scan_piece_type];

                // Loop over directions o[]
                while ((step_vector = ((scan_piece_type > 2) &&
                                       (step_vector < 0))
                                          ? -step_vector
                                          : -mcumax_step_vectors[++step_vector_index])) {
                replay:
                    // Resume normal after best
                    square_to = square_from;

                    castling_skip_square =
                        castling_rook_square = MCUMAX_SQUARE_INVALID;

                    // y traverses ray, or:
                    do {
                        // Sneak in previous best move
                        capture_square =
                            square_to =
                                replay_move
                                    ? (iter_square_to ^ replay_move)
                                    : (square_to + step_vector);

                        // Board edge hit
                        if (square_to & MCUMAX_BOARD_MASK)
                            break;

                        // Bad castling
                        if ((en_passant_square != MCUMAX_SQUARE_INVALID) &&
                            mcumax.board[en_passant_square] &&
                            ((square_to - en_passant_square) < 2) &&
                            ((en_passant_square - square_to) < 2))
                            iter_score = MCUMAX_SCORE_MAX;

                        // Shift capture square if en-passant
                        if ((scan_piece_type < 3) &&
                            (square_to == en_passant_square))
                            capture_square ^= 16;

                        capture_piece = mcumax.board[capture_square];

                        // Capture own, bad pawn mode
                        if ((capture_piece & mcumax.current_side) ||
                            ((scan_piece_type < 3) &&
                             !((square_to - square_from) & 0b111) - !capture_piece))
                            break;

                        // Value of captured piece
                        capture_piece_value = 37 * mcumax_capture_values[capture_piece & 0b111] +
                                              (capture_piece & 0xc0);

                        // King capture
                        if (capture_piece_value < 0) {
                            iter_score = MCUMAX_SCORE_MAX;
                            iter_depth = MCUMAX_DEPTH_MAX - 1;
                        }

                        // Abort on fail high
                        if ((iter_score >= beta) &&
                            (iter_depth > 1))
                            goto cutoff;

                        // MVV/LVA scoring if depth == 1
                        step_score = (iter_depth != 1)
                                         ? score
                                         : capture_piece_value - scan_piece_type;

                        // Quiescence includes captures and legal check evasions.
                        // Only its first requested ply may add quiet checks.
                        if ((iter_depth - !capture_piece) > 1 ||
                            (iter_depth == 2 && (side_in_check_before || depth >= 2))) {
                            // Center positional score
                            step_score = (scan_piece_type < 6)
                                             ? mcumax.board[square_from + 0x8] -
                                                   mcumax.board[square_to + 0x8]
                                             : 0;

                            mcumax.board[castling_rook_square] =
                                mcumax.board[capture_square] =
                                    mcumax.board[square_from] = 0;

                            // Do move, set non-virgin
                            mcumax.board[square_to] = scan_piece | MCUMAX_PIECE_MOVED;

                            // Castling: put rook & score
                            if (!(castling_rook_square & MCUMAX_BOARD_MASK)) {
                                mcumax.board[castling_skip_square] = mcumax.current_side + 6;
                                step_score += 50;
                            }

                            // Freeze king in mid-game
                            step_score -= ((scan_piece_type != 4) ||
                                           (mcumax.non_pawn_material > 30))
                                              ? 0
                                              : 20;

                            // Pawns
                            if (scan_piece_type < 3) {
                                step_score -=
                                    9 * ((((square_from - 2) & MCUMAX_BOARD_MASK) ||
                                          mcumax.board[square_from - 2] - scan_piece) +
                                         // Structure, undefended
                                         (((square_from + 2) & MCUMAX_BOARD_MASK) ||
                                          mcumax.board[square_from + 2] - scan_piece) -
                                         1 +
                                         // Squares plus bias
                                         (mcumax.board[square_from ^ 0x10] ==
                                          (mcumax.current_side + 36))) // Cling to magnetic king
                                    - (mcumax.non_pawn_material >> 2); // End-game Pawn-push bonus

                                // Promotion / passer bonus
                                capture_piece_value +=
                                    step_alpha =
                                        (square_to + step_vector + 1) & MCUMAX_SQUARE_INVALID
                                            ? (647 - scan_piece_type)
                                            : 2 * (scan_piece & (square_to + 0x10) & 0x20);

                                // Upgrade pawn or convert to queen
                                mcumax.board[square_to] += step_alpha;
                            }

                            // En-passant target square is only valid immediately after a pawn
                            // double-step. For non-pawn moves, micro-Max uses the same parameter
                            // slot for castling bookkeeping; keep that behavior.
                            mcumax_square step_en_passant_square = castling_skip_square;
                            if (scan_piece_type < 3) {
                                step_en_passant_square = MCUMAX_SQUARE_INVALID;

                                const bool is_straight_pawn_push = (((square_to - square_from) & 0b111) == 0);
                                const bool is_double_step = ((int16_t)square_to - (int16_t)square_from) == (int16_t)(2 * step_vector);
                                const bool is_start_rank =
                                    (scan_piece_type == MCUMAX_PAWN_UPSTREAM)
                                        ? ((square_from & 0x70) == 0x60)
                                        : ((square_from & 0x70) == 0x10);

                                if (is_straight_pawn_push &&
                                    is_double_step &&
                                    is_start_rank &&
                                    !(scan_piece & MCUMAX_PIECE_MOVED)) {
                                    step_en_passant_square = (mcumax_square)(square_from + step_vector);
                                }
                            }

#ifdef MCUMAX_HASHING_ENABLED
                            mcumax.hash_key += Hash(0);
                            mcumax.hash_key2 += Hash(8) + castling_rook_square - MCUMAX_SQUARE_INVALID;
#endif

                            if (mode == MCUMAX_PLAY_MOVE) {
                                // Strict move legality for playing a specific move:
                                // reject moves that leave own king in check. Additionally,
                                // enforce castling "not out of check" and "not through check".
                                bool illegal = mcumax_is_in_check(mcumax.current_side);

                                // Castling is identified by a king move where the rook was moved.
                                if (!illegal && (scan_piece_type == MCUMAX_KING) &&
                                    !(castling_rook_square & MCUMAX_BOARD_MASK)) {
                                    if (side_in_check_before)
                                        illegal = true;
                                    else {
                                        const uint8_t king_from = square_from;
                                        const uint8_t king_to = square_to;
                                        const int8_t delta = (int8_t)(king_to - king_from);
                                        if (delta == 2 || delta == -2) {
                                            const uint8_t king_mid = (uint8_t)(king_from + (delta / 2));

                                            // Evaluate check on the intermediate square in a
                                            // pre-castle intermediate position (king moved to mid,
                                            // rook still on its original square).
                                            const uint8_t king_piece = mcumax.board[king_to];
                                            const uint8_t rook_piece = mcumax.board[castling_skip_square];

                                            // Convert current post-castle position -> pre-castle.
                                            mcumax.board[king_to] = 0;
                                            mcumax.board[castling_skip_square] = 0;
                                            mcumax.board[king_from] = king_piece;
                                            mcumax.board[castling_rook_square] = rook_piece;

                                            // Move king to mid square (rook remains unmoved).
                                            mcumax.board[king_from] = 0;
                                            mcumax.board[king_mid] = king_piece;
                                            if (mcumax_is_in_check(mcumax.current_side))
                                                illegal = true;

                                            // Restore post-castle position.
                                            mcumax.board[king_mid] = 0;
                                            mcumax.board[castling_rook_square] = 0;
                                            mcumax.board[king_from] = 0;
                                            mcumax.board[king_to] = king_piece;
                                            mcumax.board[castling_skip_square] = rook_piece;
                                        }
                                    }
                                }

                                step_score = illegal ? -MCUMAX_SCORE_MAX : score;
                            } else if ((mode == MCUMAX_SEARCH_VALID_MOVES) && mcumax.valid_moves_fast_enabled) {
                                // Fast legality filter:
                                // Legal if our own king is not left in check after the move.
                                bool illegal = mcumax.valid_moves_filter_king_safety
                                                   ? mcumax_is_in_check(mcumax.current_side)
                                                   : false;

                                // Castling additional rules (when king-safety filtering is enabled):
                                // - Not allowed out of check
                                // - King may not pass through check (intermediate square)
                                if (!illegal &&
                                    mcumax.valid_moves_filter_king_safety &&
                                    (scan_piece_type == MCUMAX_KING) &&
                                    !(castling_rook_square & MCUMAX_BOARD_MASK)) {
                                    if (side_in_check_before)
                                        illegal = true;
                                    else {
                                        const uint8_t king_from = square_from;
                                        const uint8_t king_to = square_to;
                                        const int8_t delta = (int8_t)(king_to - king_from);
                                        if (delta == 2 || delta == -2) {
                                            const uint8_t king_mid = (uint8_t)(king_from + (delta / 2));

                                            // Evaluate check on the intermediate square in a
                                            // pre-castle intermediate position (king moved to mid,
                                            // rook still on its original square).
                                            const uint8_t king_piece = mcumax.board[king_to];
                                            const uint8_t rook_piece = mcumax.board[castling_skip_square];

                                            // Convert current post-castle position -> pre-castle.
                                            mcumax.board[king_to] = 0;
                                            mcumax.board[castling_skip_square] = 0;
                                            mcumax.board[king_from] = king_piece;
                                            mcumax.board[castling_rook_square] = rook_piece;

                                            // Move king to mid square (rook remains unmoved).
                                            mcumax.board[king_from] = 0;
                                            mcumax.board[king_mid] = king_piece;
                                            if (mcumax_is_in_check(mcumax.current_side))
                                                illegal = true;

                                            // Restore post-castle position.
                                            mcumax.board[king_mid] = 0;
                                            mcumax.board[castling_rook_square] = 0;
                                            mcumax.board[king_from] = 0;
                                            mcumax.board[king_to] = king_piece;
                                            mcumax.board[castling_skip_square] = rook_piece;
                                        }
                                    }
                                }

                                step_score = illegal ? -MCUMAX_SCORE_MAX : score;
                            } else if (side_in_check_before &&
                                       !(castling_rook_square & MCUMAX_BOARD_MASK)) {
                                step_score = -MCUMAX_SCORE_MAX;
                            } else if (iter_depth == 2 && !capture_piece &&
                                       !side_in_check_before &&
                                       !mcumax_is_in_check(mcumax.current_side ^ 0x18U)) {
                                // At the first quiescence ply, a quiet move is searched
                                // only when it gives check. Later capture/evasion plies
                                // have requested depth1, so they cannot add quiet checks.
                                step_score = -MCUMAX_SCORE_MAX;
                            } else {
                                // New score & alpha
                                step_score += score + capture_piece_value;
                                step_alpha = iter_score > alpha
                                                 ? iter_score
                                                 : alpha;

                                // New depth, reduce non-capture
                                step_depth = iter_depth - 1 -
                                             ((iter_depth > 5) &&
                                              (scan_piece_type > 2) &&
                                              !capture_piece &&
                                              !replay_move);

                                // Extend 1 ply if in check
                                if (!((mcumax.non_pawn_material > 30) ||
                                      !side_in_check_before ||
                                      (iter_depth < 3) ||
                                      (capture_piece &&
                                       (scan_piece_type != 4))))
                                    step_depth = iter_depth;

                                // Futility, recursive evaluation of reply
                                do {
                                    // Change side
                                    mcumax.current_side ^= 0x18;

                                    step_score_new = ((mode == MCUMAX_SEARCH_VALID_MOVES) ||
                                                      side_in_check_before ||
                                                      (iter_depth == 2 && !capture_piece && depth >= 2) ||
                                                      (step_depth > 2) ||
                                                      (step_score > step_alpha) ||
                                                      mcumax_is_in_check(mcumax.current_side))
                                                         ? -mcumax_search(-beta,
                                                                          -step_alpha,
                                                                          -step_score,
                                                                          step_en_passant_square,
                                                                          step_depth,
                                                                          MCUMAX_INTERNAL_NODE)
                                                         : step_score;

                                    // Change side
                                    mcumax.current_side ^= 0x18;
                                } while ((step_score_new > alpha) &&
                                         (++step_depth < iter_depth));

                                // No fail: re-search unreduced
                                step_score = step_score_new;
                            }

                            if ((mode == MCUMAX_PLAY_MOVE) &&
                                (step_score != -MCUMAX_SCORE_MAX) &&
                                (square_from == mcumax.square_from) &&
                                (square_to == mcumax.square_to)) {
                                // Playing move
                                mcumax.score = -score - capture_piece_value;
                                // The recursive search reuses the en-passant
                                // parameter for castling bookkeeping. Only a
                                // pawn double-step may publish that value as
                                // the next position's en-passant target.
                                mcumax.en_passant_square =
                                    (scan_piece_type < 3)
                                        ? step_en_passant_square
                                        : MCUMAX_SQUARE_INVALID;

#ifdef MCUMAX_HASHING_ENABLED
                                // Lock game in hash as draw
                                hash_entry->depth = MCUMAX_DEPTH_MAX;
                                hash_entry->score = 0;
#endif

                                // Total captured material
                                mcumax.non_pawn_material += capture_piece_value >> 7;

                                // Change side
                                mcumax.current_side ^= 0x18;

                                // Captured non-pawn material
                                --mcumax.recursion_depth;
                                return beta;
                            }

#ifdef MCUMAX_HASHING_ENABLED
                            mcumax.hash_key = hash_key;
                            mcumax.hash_key2 = hash_key2;
#endif

                            // Undo move
                            mcumax.board[castling_rook_square] = mcumax.current_side + 6;
                            mcumax.board[castling_skip_square] = mcumax.board[square_to] = 0;
                            mcumax.board[square_from] = scan_piece;
                            mcumax.board[capture_square] = capture_piece;

                            if ((mode == MCUMAX_SEARCH_BEST_MOVE) &&
                                (mcumax.square_from == MCUMAX_SQUARE_INVALID) &&
                                (step_score != -MCUMAX_SCORE_MAX)) {
                                const mcumax_move candidate = {square_from, square_to};
                                if (mcumax_is_move_legal_strict(candidate))
                                    mcumax_consider_root_candidate(candidate, step_score);
                            }

                            if ((mode == MCUMAX_SEARCH_BEST_MOVE) &&
                                (step_score != -MCUMAX_SCORE_MAX) &&
                                (square_from == mcumax.square_from) &&
                                (square_to == mcumax.square_to)) {
                                // Searching best move
                                --mcumax.recursion_depth;
                                return beta;
                            }

                            if ((mode == MCUMAX_SEARCH_VALID_MOVES) &&
                                (step_score != -MCUMAX_SCORE_MAX) &&
                                (mcumax.square_from == MCUMAX_SQUARE_INVALID) &&
                                (iter_depth == 3) &&
                                !replay_move) {
                                // Searching valid moves
                                mcumax_move move = {square_from, square_to};

                                if (mcumax.valid_moves_num < mcumax.valid_moves_buffer_size)
                                    mcumax.valid_moves_buffer[mcumax.valid_moves_num] = move;

                                mcumax.valid_moves_num++;
                            }
                        }

                        // Only a strict improvement replaces the incumbent. An equal
                        // fail-low bound does not establish an equal move value.
                        if (step_score > iter_score) {
                            // Mark non-double
                            iter_score = step_score;
                            iter_square_from = square_from;
                            iter_square_to = square_to |
                                             (castling_skip_square & MCUMAX_SQUARE_INVALID);
                            iter_best_valid = true;

                        }

                        if (replay_move) {
                            // Redo after doing old best
                            replay_move = 0;

                            goto replay;
                        }

                        // Not first step, moved before
                        if ((square_from + step_vector - square_to) ||
                            (scan_piece & MCUMAX_PIECE_MOVED) ||
                            // No pawn and no lateral king move
                            ((scan_piece_type > 2) &&
                             (((scan_piece_type != 4) ||
                               (step_vector_index != 7) ||
                               // No virgin rook in corner
                               (mcumax.board[castling_rook_square =
                                                 ((square_from + 3) ^
                                                  ((step_vector >> 1) & 0b111))] -
                                mcumax.current_side - 6) ||
                               // No two empty squares next to rook
                               mcumax.board[castling_rook_square ^ 1] ||
                               mcumax.board[castling_rook_square ^ 2]))))
                            // Fake capture for nonsliding
                            capture_piece += (scan_piece_type < 5);
                        else
                            // Enable en-passant
                            castling_skip_square = square_to;

                        // If no capture, continue ray
                    } while (!capture_piece);
                }
            }

            // Next square of board, wrap
        } while ((square_from = ((square_from + 9) &
                                 ~MCUMAX_BOARD_MASK)) != square_start);

    cutoff:
        // No legal moves is stalemate only when the side is not in check.
        if ((iter_score == -MCUMAX_SCORE_MAX) &&
            !side_in_check_before)
            iter_score = 0;

#ifdef MCUMAX_HASHING_ENABLED
        // Protect game history
        if (hash_entry->depth < MCUMAX_DEPTH_MAX) {
            hash_entry->key2 = mcumax.hash_key2;
            hash_entry->score = iter_score;
            hash_entry->depth = iter_depth;

            // Move, type (bound/exact)
            hash_entry->square_from = iter_square_from |
                                      8 * (iter_score > alpha) |
                                      MCUMAX_SQUARE_INVALID * (iter_score < beta);
            hash_entry->square_to = iter_square_to;
        }
#endif

        // Root-only: commit best move for this completed iteration.
        // This is what we'll return if we stop due to node budgeting.
        if ((mode == MCUMAX_SEARCH_BEST_MOVE) &&
            (mcumax.square_from == MCUMAX_SQUARE_INVALID) &&
            iter_best_valid &&
            !mcumax.stop_search &&
            !mcumax.stop_due_to_node_budget &&
            (iter_score != -MCUMAX_SCORE_MAX)) {
            mcumax.best_square_from = iter_square_from;
            mcumax.best_square_to = iter_square_to & ~MCUMAX_BOARD_MASK;
            mcumax.best_score = iter_score;
            mcumax.best_move_valid = true;
            mcumax.completed_root_depth = iter_depth;
            mcumax_commit_root_candidates();
            if (mcumax.root_completed_callback)
                mcumax.root_completed_callback(mcumax.root_completed_data);
        }

        // Kibitz
        // if (in_root)
        //     printf("%2d %6d %10d %c%c%c%c\n",
        //         iter_depth - 2,
        //         iter_score,
        //         mcumax.node_count,
        //         'a' + (iter_square_from & 0b111),
        //         '8' - (iter_square_from >> 4),
        //         'a' + (iter_square_to & 0b111),
        //         '8' - (iter_square_to >> 4 & 0b111));
    }

    // Delayed-loss bonus
    --mcumax.recursion_depth;
    return iter_score += iter_score < score;
}

/***************************************************************************/

void mcumax_init() {
    for (uint32_t x = 0; x < 8; x++) {
        // Setup pieces (left side)
        mcumax.board[0x10 * 0 + x] = MCUMAX_BOARD_BLACK | mcumax_board_setup[x];
        mcumax.board[0x10 * 1 + x] = MCUMAX_BOARD_BLACK | MCUMAX_PAWN_DOWNSTREAM;
        for (uint32_t y = 2; y < 6; y++)
            mcumax.board[0x10 * y + x] = MCUMAX_EMPTY;
        mcumax.board[0x10 * 6 + x] = MCUMAX_BOARD_WHITE | MCUMAX_PAWN_UPSTREAM;
        mcumax.board[0x10 * 7 + x] = MCUMAX_BOARD_WHITE | mcumax_board_setup[x];

        // Setup weights (right side)
        for (uint32_t y = 0; y < 8; y++)
            mcumax.board[16 * y + x + 8] = (x - 4) * (x - 4) + (y - 4) * (y - 3);
    }
    mcumax.current_side = MCUMAX_BOARD_WHITE;

    mcumax.score = 0;
    mcumax.en_passant_square = MCUMAX_SQUARE_INVALID;
    mcumax.non_pawn_material = 0;
    mcumax.best_square_from = MCUMAX_SQUARE_INVALID;
    mcumax.best_square_to = MCUMAX_SQUARE_INVALID;
    mcumax.best_score = 0;
    mcumax.best_move_valid = false;
    mcumax.root_candidate_count = 0;
    mcumax.root_candidate_work_count = 0;

    // Default to the optimized valid-move generator.
    mcumax.valid_moves_fast_enabled = true;
    mcumax.valid_moves_filter_king_safety = true;

#ifdef MCUMAX_HASHING_ENABLED
    uint32_t mcumax_hash_seed = 1;

    mcumax.hash_key = 0;
    mcumax.hash_key2 = 0;

    memset(mcumax_hash_table, 0, sizeof(mcumax_hash_table));

    bool illegal = mcumax.valid_moves_filter_king_safety
                       ? mcumax_is_in_check(mcumax.current_side)
                       : false;
    //     mcumax_scramble_table[i] = rand() & 0xff;

    srand(mcumax_hash_seed);
    for (uint32_t i = 0; i < 1035; i++)
        mcumax_scramble_table[i] =
            ((rand() & 0xff) << 0) |
            ((rand() & 0xff) << 8) |
            ((rand() & 0xff) << 16) |
            ((rand() & 0xff) << 24);
#endif
}

static mcumax_square mcumax_set_piece(mcumax_square square, mcumax_piece piece) {
    if (square & MCUMAX_BOARD_MASK)
        return square;

    mcumax.board[square] = piece ? (piece | MCUMAX_PIECE_MOVED) : piece;

    return square + 1;
}

mcumax_piece mcumax_get_piece(mcumax_square square) {
    if (square & MCUMAX_BOARD_MASK)
        return MCUMAX_EMPTY;

    return (mcumax.board[square] & 0xf) ^ MCUMAX_BLACK;
}

void mcumax_set_fen_position(const char* fen_string) {
    mcumax_init();

    uint32_t field_index = 0;
    uint32_t board_index = 0;

    char c;
    while ((c = *fen_string++)) {
        if (c == ' ') {
            if (field_index < 4)
                field_index++;

            continue;
        }

        switch (field_index) {
            case 0:
                if (board_index < 0x80) {
                    switch (c) {
                        case '8':
                        case '7':
                        case '6':
                        case '5':
                        case '4':
                        case '3':
                        case '2':
                        case '1':
                            for (int32_t i = 0; i < (c - '0'); i++)
                                board_index = mcumax_set_piece(board_index, MCUMAX_EMPTY);

                            break;

                        case 'P':
                            board_index = mcumax_set_piece(board_index, MCUMAX_PAWN_UPSTREAM | MCUMAX_BOARD_WHITE);

                            break;

                        case 'N':
                            board_index = mcumax_set_piece(board_index, MCUMAX_KNIGHT | MCUMAX_BOARD_WHITE);

                            break;

                        case 'B':
                            board_index = mcumax_set_piece(board_index, MCUMAX_BISHOP | MCUMAX_BOARD_WHITE);

                            break;

                        case 'R':
                            board_index = mcumax_set_piece(board_index, MCUMAX_ROOK | MCUMAX_BOARD_WHITE);

                            break;

                        case 'Q':
                            board_index = mcumax_set_piece(board_index, MCUMAX_QUEEN | MCUMAX_BOARD_WHITE);

                            break;

                        case 'K':
                            board_index = mcumax_set_piece(board_index, MCUMAX_KING | MCUMAX_BOARD_WHITE);

                            break;

                        case 'p':
                            board_index = mcumax_set_piece(board_index, MCUMAX_PAWN_DOWNSTREAM | MCUMAX_BOARD_BLACK);

                            break;

                        case 'n':
                            board_index = mcumax_set_piece(board_index, MCUMAX_KNIGHT | MCUMAX_BOARD_BLACK);

                            break;

                        case 'b':
                            board_index = mcumax_set_piece(board_index, MCUMAX_BISHOP | MCUMAX_BOARD_BLACK);

                            break;

                        case 'r':
                            board_index = mcumax_set_piece(board_index, MCUMAX_ROOK | MCUMAX_BOARD_BLACK);

                            break;

                        case 'q':
                            board_index = mcumax_set_piece(board_index, MCUMAX_QUEEN | MCUMAX_BOARD_BLACK);

                            break;

                        case 'k':
                            board_index = mcumax_set_piece(board_index, MCUMAX_KING | MCUMAX_BOARD_BLACK);

                            break;

                        case '/':
                            board_index = (board_index < 0x80) ? (board_index & 0xf0) + 0x10 : board_index;

                            break;
                    }
                }
                break;

            case 1:
                switch (c) {
                    case 'w':
                        mcumax.current_side = MCUMAX_BOARD_WHITE;

                        break;

                    case 'b':
                        mcumax.current_side = MCUMAX_BOARD_BLACK;

                        break;
                }
                break;

            case 2:
                switch (c) {
                    case 'K':
                        mcumax.board[0x74] &= ~MCUMAX_PIECE_MOVED;
                        mcumax.board[0x77] &= ~MCUMAX_PIECE_MOVED;

                        break;

                    case 'Q':
                        mcumax.board[0x74] &= ~MCUMAX_PIECE_MOVED;
                        mcumax.board[0x70] &= ~MCUMAX_PIECE_MOVED;

                        break;

                    case 'k':
                        mcumax.board[0x04] &= ~MCUMAX_PIECE_MOVED;
                        mcumax.board[0x07] &= ~MCUMAX_PIECE_MOVED;

                        break;

                    case 'q':
                        mcumax.board[0x04] &= ~MCUMAX_PIECE_MOVED;
                        mcumax.board[0x00] &= ~MCUMAX_PIECE_MOVED;

                        break;
                }

                break;

            case 3:
                switch (c) {
                    case 'a':
                    case 'b':
                    case 'c':
                    case 'd':
                    case 'e':
                    case 'f':
                    case 'g':
                    case 'h':
                        mcumax.en_passant_square &= 0x7f;
                        mcumax.en_passant_square |= (c - 'a');

                        break;

                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                        mcumax.en_passant_square &= 0x7f;
                        mcumax.en_passant_square |= 16 * ('8' - c);

                        break;
                }

                break;
        }
    }

    // FEN does not encode per-piece virginity; however, pawns can never return
    // to their starting rank. If a pawn is on its initial rank, it is
    // definitely eligible for a double-step.
    for (uint32_t x = 0; x < 8; x++) {
        const mcumax_square blackPawnSq = (mcumax_square)(0x10 * 1 + x);
        const mcumax_square whitePawnSq = (mcumax_square)(0x10 * 6 + x);

        const uint8_t blackPiece = mcumax.board[blackPawnSq];
        if (((blackPiece & 0b111) == MCUMAX_PAWN_DOWNSTREAM) &&
            ((blackPiece & MCUMAX_BOARD_BLACK) != 0)) {
            mcumax.board[blackPawnSq] &= (uint8_t)~MCUMAX_PIECE_MOVED;
        }

        const uint8_t whitePiece = mcumax.board[whitePawnSq];
        if (((whitePiece & 0b111) == MCUMAX_PAWN_UPSTREAM) &&
            ((whitePiece & MCUMAX_BOARD_WHITE) != 0)) {
            mcumax.board[whitePawnSq] &= (uint8_t)~MCUMAX_PIECE_MOVED;
        }
    }
}

mcumax_piece mcumax_get_current_side(void) {
    return mcumax.current_side;
}

static int32_t mcumax_start_search(enum mcumax_mode mode,
                                   mcumax_move move,
                                   uint32_t depth_max,
                                   uint32_t node_max) {
    mcumax.square_from = move.from;
    mcumax.square_to = move.to;

    if (mode == MCUMAX_SEARCH_BEST_MOVE) {
        mcumax.best_square_from = MCUMAX_SQUARE_INVALID;
        mcumax.best_square_to = MCUMAX_SQUARE_INVALID;
        mcumax.best_score = 0;
        mcumax.best_move_valid = false;
        mcumax.root_candidate_count = 0;
        mcumax.root_candidate_work_count = 0;
    }

    mcumax.node_max = node_max;
    mcumax.node_count = 0;
    mcumax.depth_max = depth_max;

    mcumax.stop_search = false;
    mcumax.stop_due_to_node_budget = false;
    if (mode == MCUMAX_SEARCH_BEST_MOVE) {
        mcumax.recursion_horizon_nodes = 0U;
        mcumax.completed_root_depth = 0U;
    }
    mcumax.recursion_depth = 0U;

    return mcumax_search(-MCUMAX_SCORE_MAX,
                         MCUMAX_SCORE_MAX,
                         mcumax.score,
                         mcumax.en_passant_square,
                         3,
                         mode);
}

static uint32_t mcumax_collect_position_moves(mcumax_move* buffer,
                                               uint32_t buffer_size,
                                               bool filter_king_safety) {
    mcumax_position position;
    mcumax_position_iterator iterator;
    mcumax_position_move move;
    uint32_t count = 0;
    mcumax.valid_moves_buffer = buffer;
    mcumax.valid_moves_buffer_size = buffer_size;
    mcumax.valid_moves_num = 0;
    if (!mcumax_position_import(&position, &mcumax)) return 0;
    mcumax_position_iterator_init(&iterator);
    while (filter_king_safety
               ? mcumax_position_iterator_next_legal(&position, &iterator, &move)
               : mcumax_position_iterator_next(&position, &iterator, &move)) {
        // The legacy two-byte API exposes default Queen promotion only.
        if (move.promotion && move.promotion != MCUMAX_QUEEN) continue;
        if (buffer && count < buffer_size)
            buffer[count] = (mcumax_move){move.from, move.to};
        ++count;
    }
    mcumax.valid_moves_num = count;
    return count;
}

uint32_t mcumax_search_valid_moves(mcumax_move* buffer, uint32_t buffer_size) {
    if (mcumax.valid_moves_fast_enabled)
        return mcumax_collect_position_moves(buffer, buffer_size,
                                              mcumax.valid_moves_filter_king_safety);
    mcumax.valid_moves_num = 0;
    mcumax.valid_moves_buffer = buffer;
    mcumax.valid_moves_buffer_size = buffer_size;
    mcumax_start_search(MCUMAX_SEARCH_VALID_MOVES, MCUMAX_MOVE_INVALID, 0, 0);
    return mcumax.valid_moves_num;
}

uint32_t mcumax_search_valid_moves_fast(mcumax_move* buffer, uint32_t buffer_size) {
    return mcumax_collect_position_moves(buffer, buffer_size,
                                          mcumax.valid_moves_filter_king_safety);
}

uint32_t mcumax_search_valid_moves_legacy(mcumax_move* buffer, uint32_t buffer_size) {
    const bool previous = mcumax.valid_moves_fast_enabled;
    mcumax.valid_moves_fast_enabled = false;
    const uint32_t count = mcumax_search_valid_moves(buffer, buffer_size);
    mcumax.valid_moves_fast_enabled = previous;
    return count;
}

uint32_t mcumax_search_pseudo_legal_moves(mcumax_move* buffer, uint32_t buffer_size) {
    return mcumax_collect_position_moves(buffer, buffer_size, false);
}

mcumax_move mcumax_search_best_move(uint32_t node_max, uint32_t depth_max) {
    (void)mcumax_start_search(MCUMAX_SEARCH_BEST_MOVE,
                              MCUMAX_MOVE_INVALID, depth_max + 3, node_max);

    // Preferred path: return the best move from the last fully-completed
    // iteration. This remains valid even if the search aborted early.
    mcumax_move chosen = MCUMAX_MOVE_INVALID;
    if (mcumax.best_move_valid)
        chosen = (mcumax_move){mcumax.best_square_from, mcumax.best_square_to};

    // Safety: ensure we never surface an illegal move to UCI callers.
    // (micro-Max search can consider pseudo-legal moves; when the side to move
    // is in check, this can yield a "best" move that does not resolve check.)
    if (chosen.from != MCUMAX_SQUARE_INVALID) {
        if (mcumax_is_move_legal_strict(chosen)) {
            return chosen;
        }
    }

    // The completed root result is not usable when its selected move is
    // illegal (or when no iteration completed). Do not leave a score/candidate
    // summary associated with an unavailable result.
    mcumax.best_square_from = MCUMAX_SQUARE_INVALID;
    mcumax.best_square_to = MCUMAX_SQUARE_INVALID;
    mcumax.best_score = 0;
    mcumax.best_move_valid = false;
    mcumax.root_candidate_count = 0;
    mcumax.root_candidate_work_count = 0;

    return MCUMAX_MOVE_INVALID;
}

uint8_t mcumax_get_last_root_candidates(mcumax_root_candidate* buffer,
                                        uint8_t buffer_size) {
    if ((buffer == NULL) || (buffer_size == 0U))
        return 0;

    uint8_t copied = 0;
    for (uint8_t i = 0;
         (i < mcumax.root_candidate_count) && (copied < buffer_size);
         ++i) {
        buffer[copied++] = mcumax.root_candidates[i];
    }
    return copied;
}

int32_t mcumax_search_score(uint32_t node_max, uint32_t depth_max) {
    // Budgeted score-only search.
    //
    // IMPORTANT: MCUMAX_INTERNAL_NODE mode is used for internal recursive calls
    // and intentionally bypasses the root iterative-deepening loop (it keys off
    // the mcumax_search() "depth" parameter rather than mcumax.depth_max).
    //
    // For a top-level entry point, we want the same behavior as the best-move
    // search: deepen up to depth_max and/or node_max, then return the resulting
    // evaluation score.
    const int32_t score = mcumax_start_search(MCUMAX_SEARCH_BEST_MOVE,
                                              MCUMAX_MOVE_INVALID,
                                              depth_max + 3,
                                              node_max);
    if (mcumax.best_move_valid) {
        return mcumax.best_score;
    }
    return score;
}

bool mcumax_play_move(mcumax_move move) {
    // Shared rules reject invalid geometry/king capture before legacy play.
    // Legacy play remains responsible for its incremental score bookkeeping.
    if (!mcumax_is_move_legal_strict(move)) return false;
    // Strict legality enforcement wrapper.
    //
    // The underlying engine applies the move during MCUMAX_PLAY_MOVE search.
    // We snapshot the full engine state so we can validate legality and
    // restore on failure.
    const mcumax_struct before = mcumax;
    const uint8_t side_before = before.current_side;

    const bool applied = (mcumax_start_search(MCUMAX_PLAY_MOVE, move, 0, 0) == MCUMAX_SCORE_MAX);
    if (!applied) {
        mcumax = before;
        return false;
    }

    const mcumax_struct after = mcumax;

    // Rule: you may not make a move that leaves your own king in check.
    if (mcumax_is_in_check(side_before)) {
        mcumax = before;
        return false;
    }

    // Castling special rules:
    // - Not allowed out of check
    // - King may not pass through check (intermediate square)
    //
    // Detect castling by a 2-file king move.
    const uint8_t moved_piece = before.board[move.from];
    const uint8_t moved_type = moved_piece & 0b111;
    const int8_t delta = (int8_t)(move.to - move.from);
    if ((moved_type == MCUMAX_KING) && (delta == 2 || delta == -2)) {
        // Check "not out of check" on the pre-move position.
        mcumax = before;
        if (mcumax_is_in_check(side_before)) {
            mcumax = before;
            return false;
        }

        // Check intermediate square is not attacked.
        const mcumax_square king_from = move.from;
        const mcumax_square king_mid = (mcumax_square)(move.from + (delta / 2));

        // Temporarily place king on the intermediate square (rook remains on
        // its original square in the pre-move position).
        const uint8_t from_piece = mcumax.board[king_from];
        const uint8_t mid_piece = mcumax.board[king_mid];
        mcumax.board[king_from] = 0;
        mcumax.board[king_mid] = from_piece;
        const bool mid_in_check = mcumax_is_in_check(side_before);
        mcumax.board[king_mid] = mid_piece;
        mcumax.board[king_from] = from_piece;

        if (mid_in_check) {
            mcumax = before;
            return false;
        }

        // Restore moved position (already validated above).
        mcumax = after;
    } else {
        // Restore moved position.
        mcumax = after;
    }

    mcumax.best_square_from = MCUMAX_SQUARE_INVALID;
    mcumax.best_square_to = MCUMAX_SQUARE_INVALID;
    mcumax.best_score = 0;
    mcumax.best_move_valid = false;
    mcumax.root_candidate_count = 0;
    mcumax.root_candidate_work_count = 0;

    return true;
}

bool mcumax_is_move_legal_strict(mcumax_move move) {
    mcumax_position position;
    mcumax_position_undo undo;
    return mcumax_position_import(&position, &mcumax) &&
           mcumax_position_make(&position,
                                (mcumax_position_move){move.from, move.to, 0},
                                &undo);
}

void mcumax_set_callback(mcumax_callback callback, void* userdata) {
    mcumax.user_callback = callback;
    mcumax.user_data = userdata;
}

void mcumax_set_root_completed_callback(mcumax_callback callback, void* userdata) {
    mcumax.root_completed_callback = callback;
    mcumax.root_completed_data = userdata;
}

void mcumax_stop_search(void) {
    mcumax.stop_search = true;
}

static bool mcumax_import_query_position(mcumax_position* position,
                                          uint8_t side, bool use_en_passant) {
    // During legacy recursion the global EP field describes the root position,
    // not the current node. Check detection needs no EP metadata. Queries for
    // the other side likewise cannot use this turn's EP opportunity.
    mcumax_struct source = mcumax;
    source.current_side = side;
    if (!use_en_passant || side != mcumax.current_side)
        source.en_passant_square = MCUMAX_SQUARE_INVALID;
    return mcumax_position_import(position, &source);
}

bool mcumax_is_in_check(uint8_t side) {
    mcumax_position position;
    return mcumax_import_query_position(&position, side, false) &&
           mcumax_position_is_check(&position, side);
}

bool mcumax_is_in_checkmate(uint8_t side) {
    mcumax_position position;
    return mcumax_import_query_position(&position, side, true) &&
           mcumax_position_is_check(&position, side) &&
           !mcumax_position_any_legal(&position);
}

bool mcumax_is_stalemate(uint8_t side) {
    mcumax_position position;
    return mcumax_import_query_position(&position, side, true) &&
           !mcumax_position_is_check(&position, side) &&
           !mcumax_position_any_legal(&position);
}

static bool mcumax_has_unmoved_piece(const mcumax_square square,
                                     const uint8_t side,
                                     const uint8_t piece_type) {
    const uint8_t piece = mcumax.board[square];
    const uint8_t color = piece & (MCUMAX_BOARD_WHITE | MCUMAX_BOARD_BLACK);

    return (color == side) &&
           ((piece & 0b111) == piece_type) &&
           !(piece & MCUMAX_PIECE_MOVED);
}

void mcumax_get_fen(char* fen_buffer, size_t buffer_size) {
    if (!fen_buffer || buffer_size < 100) {
        return; // Buffer too small for a complete FEN
    }

    char* ptr = fen_buffer;
    size_t remaining = buffer_size - 1; // Leave space for '\0'

    // 1. Piece positions (8 ranks separated by '/')
    for (int rank = 0; rank < 8; rank++) {
        int empty_count = 0;

        for (int file = 0; file < 8; file++) {
            mcumax_square square = rank * 16 + file;
            uint8_t piece_raw = mcumax.board[square];

            if (piece_raw == MCUMAX_EMPTY) {
                empty_count++;
            } else {
                // Write the number of empty squares if needed
                if (empty_count > 0) {
                    if (remaining < 1)
                        return;
                    *ptr++ = '0' + empty_count;
                    remaining--;
                    empty_count = 0;
                }

                // Convert piece to FEN symbol
                char piece_char = '?';
                int piece_type = piece_raw & 0b111;
                bool is_white = (piece_raw & MCUMAX_BOARD_WHITE) != 0;

                switch (piece_type) {
                    case MCUMAX_PAWN_UPSTREAM:
                    case MCUMAX_PAWN_DOWNSTREAM:
                        piece_char = is_white ? 'P' : 'p';
                        break;
                    case MCUMAX_KNIGHT:
                        piece_char = is_white ? 'N' : 'n';
                        break;
                    case MCUMAX_KING:
                        piece_char = is_white ? 'K' : 'k';
                        break;
                    case MCUMAX_BISHOP:
                        piece_char = is_white ? 'B' : 'b';
                        break;
                    case MCUMAX_ROOK:
                        piece_char = is_white ? 'R' : 'r';
                        break;
                    case MCUMAX_QUEEN:
                        piece_char = is_white ? 'Q' : 'q';
                        break;
                }

                if (remaining < 1)
                    return;
                *ptr++ = piece_char;
                remaining--;
            }
        }

        // Write the number of empty squares at the end of the rank if needed
        if (empty_count > 0) {
            if (remaining < 1)
                return;
            *ptr++ = '0' + empty_count;
            remaining--;
        }

        // Add '/' except for the last rank
        if (rank < 7) {
            if (remaining < 1)
                return;
            *ptr++ = '/';
            remaining--;
        }
    }

    // 2. Side to move
    if (remaining < 2)
        return;
    *ptr++ = ' ';
    *ptr++ = (mcumax.current_side == MCUMAX_BOARD_WHITE) ? 'w' : 'b';
    remaining -= 2;

    // 3. Castling rights
    if (remaining < 2)
        return;
    *ptr++ = ' ';
    remaining--;

    bool has_castling = false;

    // White king-side castling (K)
    if (mcumax_has_unmoved_piece(0x74, MCUMAX_BOARD_WHITE, MCUMAX_KING) &&
        mcumax_has_unmoved_piece(0x77, MCUMAX_BOARD_WHITE, MCUMAX_ROOK)) {
        if (remaining < 1)
            return;
        *ptr++ = 'K';
        remaining--;
        has_castling = true;
    }

    // White queen-side castling (Q)
    if (mcumax_has_unmoved_piece(0x74, MCUMAX_BOARD_WHITE, MCUMAX_KING) &&
        mcumax_has_unmoved_piece(0x70, MCUMAX_BOARD_WHITE, MCUMAX_ROOK)) {
        if (remaining < 1)
            return;
        *ptr++ = 'Q';
        remaining--;
        has_castling = true;
    }

    // Black king-side castling (k)
    if (mcumax_has_unmoved_piece(0x04, MCUMAX_BOARD_BLACK, MCUMAX_KING) &&
        mcumax_has_unmoved_piece(0x07, MCUMAX_BOARD_BLACK, MCUMAX_ROOK)) {
        if (remaining < 1)
            return;
        *ptr++ = 'k';
        remaining--;
        has_castling = true;
    }

    // Black queen-side castling (q)
    if (mcumax_has_unmoved_piece(0x04, MCUMAX_BOARD_BLACK, MCUMAX_KING) &&
        mcumax_has_unmoved_piece(0x00, MCUMAX_BOARD_BLACK, MCUMAX_ROOK)) {
        if (remaining < 1)
            return;
        *ptr++ = 'q';
        remaining--;
        has_castling = true;
    }

    // If no castling possible
    if (!has_castling) {
        if (remaining < 1)
            return;
        *ptr++ = '-';
        remaining--;
    }

    // 4. En passant square
    if (remaining < 2)
        return;
    *ptr++ = ' ';
    remaining--;

    if (mcumax.en_passant_square == MCUMAX_SQUARE_INVALID) {
        if (remaining < 1)
            return;
        *ptr++ = '-';
        remaining--;
    } else {
        // Convert square to algebraic notation
        int file = mcumax.en_passant_square & 0x0F;
        int rank = (mcumax.en_passant_square & 0xF0) >> 4;

        if (remaining < 2)
            return;
        *ptr++ = 'a' + file;
        *ptr++ = '8' - rank;
        remaining -= 2;
    }

    // 5. Halfmove clock (50-move rule) - Simplified to 0 as not tracked by engine
    if (remaining < 3)
        return;
    *ptr++ = ' ';
    *ptr++ = '0';
    remaining -= 2;

    // 6. Move number - Simplified to 1 as not tracked by engine
    if (remaining < 3)
        return;
    *ptr++ = ' ';
    *ptr++ = '1';
    remaining -= 2;

    // End the string
    *ptr = '\0';
}
