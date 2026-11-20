/*
 * Owned position/rules extracted from mcu-max, (C) 2022-2024 Gissio.
 * Based on micro-Max 4.8 by H.G. Muller. License: MIT.
 */
#ifndef MCU_MAX_POSITION_H
#define MCU_MAX_POSITION_H

#include "mcu-max.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure chess state. Squares use 0x88 coordinates; piece/color/moved bits
 * retain the mcu-max representation. Off-board bytes are zero. */
typedef struct mcumax_position {
    uint8_t board[128];
    uint8_t side;
    uint8_t en_passant;
    uint8_t king_square[2]; /* White, Black. */
} mcumax_position;

typedef struct mcumax_position_move {
    uint8_t from;
    uint8_t to;
    /* Zero for ordinary moves/default Queen, otherwise MCUMAX_QUEEN,
     * MCUMAX_ROOK, MCUMAX_BISHOP or MCUMAX_KNIGHT on a promotion. */
    uint8_t promotion;
} mcumax_position_move;

/* At most four changed squares (castling). Does not snapshot the board. */
typedef struct mcumax_position_undo {
    uint8_t square[4];
    uint8_t piece[4];
    uint8_t count;
    uint8_t side;
    uint8_t en_passant;
    uint8_t king_square[2];
} mcumax_position_undo;

/* Fixed streaming cursor. Restore the position before requesting its next
 * move. Each promotion is yielded separately; no move arrays are allocated. */
typedef struct mcumax_position_iterator {
    uint8_t from;
    uint8_t direction;
    uint8_t distance;
    uint8_t promotion_index;
    uint8_t promotion_to;
} mcumax_position_iterator;

/* Imports once from the owner's snapshot, without reading the global engine.
 * Returns false for invalid side/pieces/kings/EP metadata. Output is unchanged
 * on failure. Legacy evaluation tags and positional-score bytes are stripped. */
bool mcumax_position_import(mcumax_position* position,
                            const mcumax_struct* source);
void mcumax_position_iterator_init(mcumax_position_iterator* iterator);
bool mcumax_position_iterator_next(const mcumax_position* position,
                                   mcumax_position_iterator* iterator,
                                   mcumax_position_move* move);
/* Same ordered pseudo-legal captures, EP and explicit promotions as the
 * ordinary iterator with quiet nonpromotions filtered out. Cursor storage and
 * caller make/unmake ownership are unchanged; use one selection per scan. */
bool mcumax_position_iterator_next_tactical(const mcumax_position* position,
                                            mcumax_position_iterator* iterator,
                                            mcumax_position_move* move);
bool mcumax_position_iterator_next_legal(mcumax_position* position,
                                         mcumax_position_iterator* iterator,
                                         mcumax_position_move* move);

/* Validates movement, promotion, king safety and castling. Neither kings nor
 * friendly pieces can be captured. Invalid moves leave position unchanged.
 * A successful move advances side and updates real pawn EP metadata only. */
bool mcumax_position_make(mcumax_position* position,
                          mcumax_position_move move,
                          mcumax_position_undo* undo);
/* Only for an exact move yielded by an initialized ordinary/tactical iterator
 * for this same parent position, restored after any intervening child search.
 * Reuses that iterator's movement/promotion evidence, but still validates both
 * kings, castling-through-check and final mover safety. Preferred, cached,
 * killer and external moves must use the strict make above. */
bool mcumax_position_make_generated(mcumax_position* position,
                                    mcumax_position_move move,
                                    mcumax_position_undo* undo);
void mcumax_position_unmake(mcumax_position* position,
                            const mcumax_position_undo* undo);
/* Invalid/missing cached king is treated as unsafe (true). */
bool mcumax_position_is_check(const mcumax_position* position, uint8_t side);
bool mcumax_position_any_legal(mcumax_position* position);

#ifdef __cplusplus
}
#endif
#endif
