#ifndef TT_H__
#define TT_H__

#include <stddef.h>
#include <stdint.h>

#include "board.h"

typedef enum {
    TT_EXACT = 0,
    TT_LOWERBOUND = 1,
    TT_UPPERBOUND = 2,
} TTFlag;

#define TT_MATE_THRESHOLD 49000

void tt_init(size_t mb);
void tt_free(void);
void tt_clear(void);
size_t tt_size_mb(void);

int tt_probe_score(uint64_t key, int depth, int ply, int alpha, int beta, int* out_score);
int tt_probe_move(uint64_t key, Move* out_move);

void tt_store(uint64_t key, int depth, int ply, int score, TTFlag flag, Move best);

#endif  // TT_H__
