#ifndef ZOBRIST_H__
#define ZOBRIST_H__

#include <stdint.h>

#include "bitboard.h"

extern uint64_t zobrist_piece[2][PIECE_TYPE_NB][64];

extern uint64_t zobrist_side;

extern uint64_t zobrist_castle[16];

extern uint64_t zobrist_ep_file[8];

void zobrist_init_tables(void);

#endif  // ZOBRIST_H__
