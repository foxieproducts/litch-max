/*
 * Owned position/rules extracted from mcu-max, (C) 2022-2024 Gissio.
 * Based on micro-Max 4.8 by H.G. Muller. License: MIT.
 */
#include "mcu-max-position.h"

#include <string.h>

static const int8_t position_steps[8] = {1, -1, 16, -16, 15, -15, 17, -17};
static const int8_t position_knight_steps[8] = {14, 18, 31, 33, -14, -18, -31, -33};
static const uint8_t position_promotions[4] = {
    MCUMAX_QUEEN, MCUMAX_ROOK, MCUMAX_BISHOP, MCUMAX_KNIGHT};

static bool position_square_valid(int square) {
    return square >= 0 && square < 128 && !(square & 0x88);
}

static uint8_t position_color(uint8_t piece) {
    return piece & (MCUMAX_BOARD_WHITE | MCUMAX_BOARD_BLACK);
}

static unsigned position_side_index(uint8_t side) {
    return side == MCUMAX_BOARD_WHITE ? 0U : 1U;
}

static bool position_king_valid(const mcumax_position* position, uint8_t side) {
    const uint8_t square = position->king_square[position_side_index(side)];
    return position_square_valid(square) &&
           position_color(position->board[square]) == side &&
           (position->board[square] & 7U) == MCUMAX_KING;
}

bool mcumax_position_import(mcumax_position* position,
                            const mcumax_struct* source) {
    if (!position || !source ||
        (source->current_side != MCUMAX_BOARD_WHITE &&
         source->current_side != MCUMAX_BOARD_BLACK))
        return false;
    mcumax_position imported = {{0}, source->current_side,
                                source->en_passant_square, {128, 128}};
    for (unsigned square = 0; square < 128; ++square) {
        if (square & 0x88) { square += 7; continue; }
        const uint8_t piece = source->board[square] & 0x3fU;
        const uint8_t type = piece & 7U;
        if (!type) {
            if (piece) return false;
            continue;
        }
        const uint8_t color = position_color(piece);
        if (color != MCUMAX_BOARD_WHITE && color != MCUMAX_BOARD_BLACK)
            return false;
        if (type < 3 &&
            (type != (color == MCUMAX_BOARD_WHITE ? MCUMAX_PAWN_UPSTREAM
                                                 : MCUMAX_PAWN_DOWNSTREAM) ||
             (square & 0x70) == 0 || (square & 0x70) == 0x70))
            return false;
        imported.board[square] = piece;
        if (type == MCUMAX_KING) {
            const unsigned index = position_side_index(color);
            if (imported.king_square[index] != MCUMAX_SQUARE_INVALID)
                return false;
            imported.king_square[index] = square;
        }
    }
    if (!position_king_valid(&imported, MCUMAX_BOARD_WHITE) ||
        !position_king_valid(&imported, MCUMAX_BOARD_BLACK))
        return false;
    if (imported.en_passant != MCUMAX_SQUARE_INVALID) {
        const uint8_t target = imported.en_passant;
        if (!position_square_valid(target) || imported.board[target] ||
            (target & 0x70) != (imported.side == MCUMAX_BOARD_WHITE ? 0x20 : 0x50))
            return false;
        const uint8_t pawn = imported.board[target ^ 16U];
        const uint8_t enemy = imported.side ^ 0x18U;
        if (position_color(pawn) != enemy ||
            (pawn & 7U) != (enemy == MCUMAX_BOARD_WHITE ? MCUMAX_PAWN_UPSTREAM
                                                       : MCUMAX_PAWN_DOWNSTREAM))
            return false;
    }
    *position = imported;
    return true;
}

bool mcumax_position_is_check(const mcumax_position* position, uint8_t side) {
    if (!position || (side != MCUMAX_BOARD_WHITE && side != MCUMAX_BOARD_BLACK) ||
        !position_king_valid(position, side))
        return true;
    const int king = position->king_square[position_side_index(side)];
    const uint8_t enemy = side ^ 0x18U;
    for (unsigned direction = 0; direction < 8; ++direction) {
        unsigned distance = 1;
        for (int square = king + position_steps[direction];
             position_square_valid(square); square += position_steps[direction], ++distance) {
            const uint8_t piece = position->board[square];
            if (!piece) continue;
            if (position_color(piece) == enemy) {
                const uint8_t type = piece & 7U;
                if (type == MCUMAX_QUEEN ||
                    (direction < 4 && type == MCUMAX_ROOK) ||
                    (direction >= 4 && type == MCUMAX_BISHOP) ||
                    (distance == 1 && type == MCUMAX_KING))
                    return true;
            }
            break;
        }
    }
    for (unsigned direction = 0; direction < 8; ++direction) {
        const int square = king + position_knight_steps[direction];
        if (position_square_valid(square) &&
            position_color(position->board[square]) == enemy &&
            (position->board[square] & 7U) == MCUMAX_KNIGHT)
            return true;
    }
    const int enemy_direction = enemy == MCUMAX_BOARD_WHITE ? -16 : 16;
    for (int file = -1; file <= 1; file += 2) {
        const int square = king - enemy_direction + file;
        if (position_square_valid(square) &&
            position_color(position->board[square]) == enemy &&
            (position->board[square] & 7U) ==
                (enemy == MCUMAX_BOARD_WHITE ? MCUMAX_PAWN_UPSTREAM
                                             : MCUMAX_PAWN_DOWNSTREAM))
            return true;
    }
    return false;
}

static bool position_castle_geometry(const mcumax_position* position,
                                      mcumax_position_move move) {
    const uint8_t home = position->side == MCUMAX_BOARD_WHITE ? 116U : 4U;
    if (move.from != home || (move.to != home - 2 && move.to != home + 2) ||
        position->board[home] & MCUMAX_PIECE_MOVED)
        return false;
    const int step = move.to > home ? 1 : -1;
    const uint8_t rook_square = step > 0 ? home + 3 : home - 4;
    const uint8_t rook = position->board[rook_square];
    if (position_color(rook) != position->side || (rook & 7U) != MCUMAX_ROOK ||
        (rook & MCUMAX_PIECE_MOVED))
        return false;
    for (int square = home + step; square != rook_square; square += step)
        if (position->board[square]) return false;
    return true;
}

static bool position_pseudo_move(const mcumax_position* position,
                                 mcumax_position_move move) {
    if (!position_square_valid(move.from) || !position_square_valid(move.to) ||
        move.from == move.to)
        return false;
    const uint8_t piece = position->board[move.from];
    const uint8_t target = position->board[move.to];
    const uint8_t type = piece & 7U;
    if (!type || position_color(piece) != position->side ||
        (target && (position_color(target) == position->side ||
                    (target & 7U) == MCUMAX_KING)))
        return false;
    if (type < 3) {
        const int step = position->side == MCUMAX_BOARD_WHITE ? -16 : 16;
        const int delta = (int)move.to - move.from;
        const bool promotes = (move.to & 0x70) == (step < 0 ? 0 : 0x70);
        if (move.promotion &&
            (!promotes || (move.promotion != MCUMAX_QUEEN &&
                           move.promotion != MCUMAX_ROOK &&
                           move.promotion != MCUMAX_BISHOP &&
                           move.promotion != MCUMAX_KNIGHT)))
            return false;
        if (delta == step) return !target;
        if (delta == 2 * step)
            return !target && !(piece & MCUMAX_PIECE_MOVED) &&
                   (move.from & 0x70) == (step < 0 ? 0x60 : 0x10) &&
                   !position->board[move.from + step];
        if (delta != step - 1 && delta != step + 1) return false;
        if (target) return true;
        if (move.to != position->en_passant ||
            (move.to & 0x70) != (step < 0 ? 0x20 : 0x50))
            return false;
        const uint8_t captured = position->board[move.to ^ 16U];
        return position_color(captured) == (position->side ^ 0x18U) &&
               (captured & 7U) == (step < 0 ? MCUMAX_PAWN_DOWNSTREAM
                                           : MCUMAX_PAWN_UPSTREAM);
    }
    if (move.promotion) return false;
    const int delta = (int)move.to - move.from;
    if (type == MCUMAX_KING && (delta == -2 || delta == 2))
        return position_castle_geometry(position, move);
    const int rank_delta = (int)(move.to >> 4U) - (int)(move.from >> 4U);
    const int file_delta = (int)(move.to & 7U) - (int)(move.from & 7U);
    const unsigned ranks = rank_delta < 0 ? -rank_delta : rank_delta;
    const unsigned files = file_delta < 0 ? -file_delta : file_delta;
    if (type == MCUMAX_KNIGHT)
        return (ranks == 1U && files == 2U) || (ranks == 2U && files == 1U);
    if (type == MCUMAX_KING) return ranks <= 1U && files <= 1U;
    const bool straight = rank_delta == 0 || file_delta == 0;
    const bool diagonal = ranks == files;
    if ((type == MCUMAX_ROOK && !straight) ||
        (type == MCUMAX_BISHOP && !diagonal) ||
        (type == MCUMAX_QUEEN && !straight && !diagonal))
        return false;
    const int step = (rank_delta == 0 ? 0 : rank_delta > 0 ? 16 : -16) +
                     (file_delta == 0 ? 0 : file_delta > 0 ? 1 : -1);
    // Valid aligned endpoints imply every intermediate square is on-board.
    // Recheck only this ray, including for cached/killer moves passed to make.
    for (int square = move.from + step; square != move.to; square += step)
        if (position->board[square]) return false;
    return true;
}

void mcumax_position_iterator_init(mcumax_position_iterator* iterator) {
    if (iterator) *iterator = (mcumax_position_iterator){0, 0, 1, 0, 128};
}

static bool position_iterator_next(const mcumax_position* position,
                                   mcumax_position_iterator* iterator,
                                   mcumax_position_move* move, bool tactical) {
    if (!position || !iterator || !move) return false;
    if (iterator->promotion_index) {
        *move = (mcumax_position_move){iterator->from, iterator->promotion_to,
                                      position_promotions[iterator->promotion_index]};
        if (++iterator->promotion_index == 4) iterator->promotion_index = 0;
        return true;
    }
    while (iterator->from < 128) {
        const uint8_t from = iterator->from;
        if (from & 0x88) { iterator->from += 8; continue; }
        const uint8_t piece = position->board[from], type = piece & 7U;
        if (position_color(piece) != position->side || !type) {
            ++iterator->from; iterator->direction = 0; iterator->distance = 1; continue;
        }
        const unsigned count = type < 3 ? 4U : type == MCUMAX_KING ? 10U : 8U;
        if (iterator->direction >= count) {
            ++iterator->from; iterator->direction = 0; iterator->distance = 1; continue;
        }
        const unsigned direction = iterator->direction;
        if ((type == MCUMAX_BISHOP && direction < 4) ||
            (type == MCUMAX_ROOK && direction >= 4)) {
            ++iterator->direction; continue;
        }
        int to;
        if (type < 3) {
            const int step = position->side == MCUMAX_BOARD_WHITE ? -16 : 16;
            to = from + (direction == 0 ? step : direction == 1 ? 2 * step :
                         direction == 2 ? step - 1 : step + 1);
            ++iterator->direction;
        } else if (direction >= 8) {
            to = from + (direction == 8 ? -2 : 2);
            ++iterator->direction;
        } else {
            const int step = type == MCUMAX_KNIGHT ? position_knight_steps[direction]
                                                  : position_steps[direction];
            to = from + step * iterator->distance;
            if (tactical && type != MCUMAX_KING && type != MCUMAX_KNIGHT) {
                // Only the first ray blocker can be captured. Quiet squares
                // remain covered by the ordinary iterator, in the same order.
                while (position_square_valid(to) && !position->board[to]) to += step;
                ++iterator->direction;
                iterator->distance = 1;
            } else if (!position_square_valid(to) || position->board[to] ||
                type == MCUMAX_KING || type == MCUMAX_KNIGHT) {
                ++iterator->direction; iterator->distance = 1;
            } else {
                ++iterator->distance;
            }
        }
        if (!position_square_valid(to)) continue;
        *move = (mcumax_position_move){from, to, 0};
        if (type >= MCUMAX_KING && direction < 8U) {
            // This initialized cursor already walked every empty ray square.
            // Geometry need not be retraced; public make still validates it.
            const uint8_t target = position->board[to];
            if (target && (position_color(target) == position->side ||
                           (target & 7U) == MCUMAX_KING)) continue;
        } else if (!position_pseudo_move(position, *move)) {
            continue;
        }
        if (type < 3 && ((to & 0x70) == 0 || (to & 0x70) == 0x70)) {
            move->promotion = MCUMAX_QUEEN;
            iterator->promotion_index = 1;
            iterator->promotion_to = to;
        }
        if (tactical && !move->promotion && !position->board[to] &&
            !(type < 3 && to == position->en_passant &&
              (to & 7U) != (from & 7U))) continue;
        return true;
    }
    return false;
}

bool mcumax_position_iterator_next(const mcumax_position* position,
                                   mcumax_position_iterator* iterator,
                                   mcumax_position_move* move) {
    return position_iterator_next(position, iterator, move, false);
}

bool mcumax_position_iterator_next_tactical(const mcumax_position* position,
                                            mcumax_position_iterator* iterator,
                                            mcumax_position_move* move) {
    return position_iterator_next(position, iterator, move, true);
}

static void position_remember(mcumax_position* position,
                              mcumax_position_undo* undo, uint8_t square) {
    for (unsigned index = 0; index < undo->count; ++index)
        if (undo->square[index] == square) return;
    undo->square[undo->count] = square;
    undo->piece[undo->count++] = position->board[square];
}

void mcumax_position_unmake(mcumax_position* position,
                            const mcumax_position_undo* undo) {
    for (unsigned index = 0; index < undo->count; ++index)
        position->board[undo->square[index]] = undo->piece[index];
    position->side = undo->side;
    position->en_passant = undo->en_passant;
    memcpy(position->king_square, undo->king_square, 2);
}

bool mcumax_position_make(mcumax_position* position,
                          mcumax_position_move move,
                          mcumax_position_undo* undo) {
    if (!position || !undo ||
        (position->side != MCUMAX_BOARD_WHITE && position->side != MCUMAX_BOARD_BLACK) ||
        !position_king_valid(position, MCUMAX_BOARD_WHITE) ||
        !position_king_valid(position, MCUMAX_BOARD_BLACK) ||
        !position_pseudo_move(position, move))
        return false;
    const uint8_t side = position->side;
    const unsigned index = position_side_index(side);
    const uint8_t piece = position->board[move.from], type = piece & 7U;
    const int delta = (int)move.to - move.from;
    const bool castle = type == MCUMAX_KING && (delta == -2 || delta == 2);
    if (castle) {
        if (mcumax_position_is_check(position, side)) return false;
        const uint8_t middle = move.from + delta / 2;
        position->board[move.from] = 0;
        position->board[middle] = piece;
        position->king_square[index] = middle;
        const bool unsafe = mcumax_position_is_check(position, side);
        position->board[middle] = 0;
        position->board[move.from] = piece;
        position->king_square[index] = move.from;
        if (unsafe) return false;
    }
    *undo = (mcumax_position_undo){{0}, {0}, 0, side, position->en_passant,
                                  {position->king_square[0], position->king_square[1]}};
    position_remember(position, undo, move.from);
    position_remember(position, undo, move.to);
    if (type < 3 && move.to == position->en_passant && !position->board[move.to]) {
        const uint8_t captured = move.to ^ 16U;
        position_remember(position, undo, captured);
        position->board[captured] = 0;
    }
    position->board[move.from] = 0;
    position->board[move.to] = piece | MCUMAX_PIECE_MOVED;
    if (type < 3 && ((move.to & 0x70) == 0 || (move.to & 0x70) == 0x70))
        position->board[move.to] = side | MCUMAX_PIECE_MOVED |
                                 (move.promotion ? move.promotion : MCUMAX_QUEEN);
    if (type == MCUMAX_KING) position->king_square[index] = move.to;
    if (castle) {
        const uint8_t rook_from = delta > 0 ? move.from + 3 : move.from - 4;
        const uint8_t rook_to = delta > 0 ? move.to - 1 : move.to + 1;
        position_remember(position, undo, rook_from);
        position_remember(position, undo, rook_to);
        position->board[rook_to] = position->board[rook_from] | MCUMAX_PIECE_MOVED;
        position->board[rook_from] = 0;
    }
    position->en_passant = type < 3 && (delta == -32 || delta == 32)
                               ? move.from + delta / 2 : MCUMAX_SQUARE_INVALID;
    if (mcumax_position_is_check(position, side)) {
        mcumax_position_unmake(position, undo);
        return false;
    }
    position->side ^= 0x18U;
    return true;
}

bool mcumax_position_iterator_next_legal(mcumax_position* position,
                                         mcumax_position_iterator* iterator,
                                         mcumax_position_move* move) {
    mcumax_position_undo undo;
    while (mcumax_position_iterator_next(position, iterator, move)) {
        if (!mcumax_position_make(position, *move, &undo)) continue;
        mcumax_position_unmake(position, &undo);
        return true;
    }
    return false;
}

bool mcumax_position_any_legal(mcumax_position* position) {
    mcumax_position_iterator iterator;
    mcumax_position_move move;
    mcumax_position_iterator_init(&iterator);
    return mcumax_position_iterator_next_legal(position, &iterator, &move);
}
