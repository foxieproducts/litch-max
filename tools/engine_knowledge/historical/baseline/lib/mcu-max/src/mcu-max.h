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

#if !defined(MCU_MAX_H)
#define MCU_MAX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MCUMAX_ID "mcu-max 1.0.6"
#define MCUMAX_AUTHOR "Gissio"

#define MCUMAX_SQUARE_INVALID 0x80

#define MCUMAX_BOARD_WHITE 0x8
#define MCUMAX_BOARD_BLACK 0x10
#define MCUMAX_PIECE_MOVED 0x20

#define MCUMAX_MOVE_INVALID                          \
    (mcumax_move) {                                  \
        MCUMAX_SQUARE_INVALID, MCUMAX_SQUARE_INVALID \
    }

typedef uint8_t mcumax_square;
typedef uint8_t mcumax_piece;

typedef struct
{
    mcumax_square from;
    mcumax_square to;
} mcumax_move;

#define MCUMAX_ROOT_CANDIDATE_CAPACITY 3

typedef struct
{
    mcumax_move move;
} mcumax_root_candidate;

typedef void (*mcumax_callback)(void*);

/**
 * Piece types
 */
enum {
    // Bits 0-2: piece
    MCUMAX_EMPTY,
    MCUMAX_PAWN_UPSTREAM,
    MCUMAX_PAWN_DOWNSTREAM,
    MCUMAX_KNIGHT,
    MCUMAX_KING,
    MCUMAX_BISHOP,
    MCUMAX_ROOK,
    MCUMAX_QUEEN,

    // Bits 3: color
    MCUMAX_BLACK = 0x8,
};

/**
 * @brief Resets the engine state.
 */
void mcumax_init(void);

/**
 * @brief Sets position from a FEN string.
 *
 * @param value The FEN string.
 */
void mcumax_set_fen_position(const char* value);

/**
 * @brief Returns the piece at the specified square.
 *
 * @param square A square coded as 0xRF, R: rank (0-7), F: file (0-7).
 * @return The piece.
 */
mcumax_piece mcumax_get_piece(mcumax_square square);

/**
 * @brief Returns the current side.
 */
mcumax_piece mcumax_get_current_side(void);

/**
 * @brief Searches valid moves.
 *
 * @param buffer A buffer for storing valid moves.
 * @param buffer_size The buffer size for storing valid moves.
 *
 * @return The number of valid moves.
 */
uint32_t mcumax_search_valid_moves(mcumax_move* buffer, uint32_t buffer_size);

// Diagnostic / test helpers: compare the old recursive valid-move generator with
// the shared pure position-rules implementation. Fast generation changes only
// valid_moves_* output fields; it does not enter/reset recursive search.
uint32_t mcumax_search_valid_moves_fast(mcumax_move* buffer, uint32_t buffer_size);
uint32_t mcumax_search_valid_moves_legacy(mcumax_move* buffer, uint32_t buffer_size);
// Returns pseudo-legal moves (does not filter out moves that leave own king in check).
uint32_t mcumax_search_pseudo_legal_moves(mcumax_move* buffer, uint32_t buffer_size);

/**
 * @brief Searches the best move.
 *
 * @param node_max The maximum number of nodes to search.
 * @param depth_max The maximum depth to search.
 *
 * @return The legal move from a completed root iteration, or MCUMAX_MOVE_INVALID
 * if no usable result completed. Invalid results carry no score/candidate summary.
 */
mcumax_move mcumax_search_best_move(uint32_t node_max, uint32_t depth_max);

/**
 * @brief Copies legal root candidates from the last fully completed iteration.
 *
 * Candidates are ordered by the root search's deterministic move preference
 * and bounded by MCUMAX_ROOT_CANDIDATE_CAPACITY. Only best_score is a usable
 * evaluation; alpha-beta bounds for the alternatives are intentionally not
 * exposed as comparable scores. The returned count never exceeds buffer_size.
 */
uint8_t mcumax_get_last_root_candidates(mcumax_root_candidate* buffer,
                                        uint8_t buffer_size);

// Score-only search helper.
// Returns the evaluation score for the current side-to-move.
// This is intended for callers that want to evaluate a position without
// selecting a best move (e.g., a caller evaluating child nodes independently).
int32_t mcumax_search_score(uint32_t node_max, uint32_t depth_max);

/**
 * @brief Plays a move.
 *
 * @param move The move.
 * @return The move was played.
 */
bool mcumax_play_move(mcumax_move move);

// Pure strict legality check using the shared position rules.
// Validates movement, king safety, castling, EP and default Queen promotion.
// Does not mutate engine state, counters, callbacks or search controls.
bool mcumax_is_move_legal_strict(mcumax_move move);

/**
 * @brief Sets the user callback, which is called periodically during search.
 */
void mcumax_set_callback(mcumax_callback callback, void* userdata);

/* Called at a completed best-move root iteration, with the root board restored.
 * Observers may copy committed results; they must not mutate or reenter search. */
void mcumax_set_root_completed_callback(mcumax_callback callback, void* userdata);

/**
 * @brief Stops the current search. To be called from the user callback.
 */
void mcumax_stop_search(void);

/**
 * Checks if the king of the given side is in check.
 */
bool mcumax_is_in_check(uint8_t side);

/**
 * Checks if the king of the given side is in checkmate.
 */
bool mcumax_is_in_checkmate(uint8_t side);

/**
 * Checks if the king of the given side is in stalemate.
 */
bool mcumax_is_stalemate(uint8_t side);

/**
 * Exports the current position in FEN format.
 */
void mcumax_get_fen(char* fen_buffer, size_t buffer_size);

typedef struct mcumax_struct {
    uint8_t board[0x80 + 1];
    uint8_t current_side;
    int32_t score;
    uint8_t en_passant_square;
    int32_t non_pawn_material;
#ifdef MCUMAX_HASHING_ENABLED
    uint32_t hash_key;
    uint32_t hash_key2;
#endif
    uint8_t square_from;
    uint8_t square_to;

    // Best move found so far during MCUMAX_SEARCH_BEST_MOVE. This is updated at
    // iteration boundaries in the root search so that callers can still get a
    // reasonable move when the search aborts early (e.g., due to node budget).
    uint8_t best_square_from;
    uint8_t best_square_to;
    int32_t best_score;
    bool best_move_valid;

    mcumax_root_candidate root_candidates[MCUMAX_ROOT_CANDIDATE_CAPACITY];
    mcumax_root_candidate root_candidates_work[MCUMAX_ROOT_CANDIDATE_CAPACITY];
    int16_t root_candidate_work_scores[MCUMAX_ROOT_CANDIDATE_CAPACITY];
    uint8_t root_candidate_count;
    uint8_t root_candidate_work_count;

    uint32_t node_count;
    uint32_t node_max;
    uint32_t depth_max;
    volatile bool stop_search;
    // True if stop_search was triggered by node budget enforcement.
    volatile bool stop_due_to_node_budget;
    // Structural search horizon; zero selects the default 20-ply bound.
    // A reached horizon uses a static evaluation, not a terminal/draw claim.
    uint8_t recursion_limit;
    uint8_t recursion_depth;
    uint32_t recursion_horizon_nodes;
    uint8_t completed_root_depth;
    mcumax_callback user_callback;
    void* user_data;
    mcumax_callback root_completed_callback;
    void* root_completed_data;
    mcumax_move* valid_moves_buffer;
    uint32_t valid_moves_buffer_size;
    uint32_t valid_moves_num;

    // When true, valid-move generation uses a fast legality filter instead of
    // running the full recursive search per candidate move.
    bool valid_moves_fast_enabled;

    // When true, valid-move generation filters out moves that leave own king
    // in check. When false, valid-move generation returns pseudo-legal moves.
    bool valid_moves_filter_king_safety;
} mcumax_struct;

extern mcumax_struct mcumax;

#ifdef MCUMAX_HASHING_ENABLED
// Determinism hook for callers: this seed is used when initializing the hash scramble table.
// Default is 1 for fully deterministic behavior.
extern uint32_t mcumax_hash_seed;
#endif

#ifdef __cplusplus
}
#endif

#endif
