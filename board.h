#ifndef BOARD_H__
#define BOARD_H__

#include <stdint.h>

#include "bitboard.h"

#define WHITE 0
#define BLACK 1
#define EMPTY 0

enum {
    WP = 1,
    WN = 2,
    WB = 3,
    WR = 4,
    WQ = 5,
    WK = 6,
    BP = -1,
    BN = -2,
    BB = -3,
    BR = -4,
    BQ = -5,
    BK = -6,
};

#define WK_CASTLE 1
#define WQ_CASTLE 2
#define BK_CASTLE 4
#define BQ_CASTLE 8

typedef struct {
    int from;
    int to;
    int promo;
} Move;

typedef struct {
    Bitboard pieces[2][PIECE_TYPE_NB];
    Bitboard occupied[2];
    Bitboard all;

    int side;
    int enpas;
    int castle;

    uint64_t hash;
} Board;

typedef struct {
    int captured_type;
    int captured_sq;
    int prev_castle;
    int prev_enpas;
    uint64_t prev_hash;
} Undo;

void board_init(Board* b);
void board_startpos(Board* b);

int gen_moves(Board* b, Move* list);
int gen_capture_moves(Board* b, Move* list);

int make_move(Board* b, Move m, Undo* u);

void unmake_move(Board* b, Move m, const Undo* u);

void make_null_move(Board* b, Undo* u);
void unmake_null_move(Board* b, const Undo* u);

int is_attacked(const Board* b, int sq, int by_color);
int is_in_check(const Board* b, int color);
int king_square(const Board* b, int color);

int piece_at(const Board* b, int sq);

uint64_t board_compute_hash(const Board* b);

void board_assert_consistent(const Board* b);

int board_set_fen(Board* b, const char* fen);
void board_print(Board* b);

#endif  // BOARD_H__
