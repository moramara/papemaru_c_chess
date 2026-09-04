#ifndef SEARCH_H__
#define SEARCH_H__

#include <stdatomic.h>
#include <stdint.h>

#include "board.h"

extern _Atomic int stop_flag;
extern _Atomic int ponder_hit_flag;

/* UCI search limits. A zero value means "not specified". */
typedef struct {
    int depth;
    int movetime_ms;
    int wtime_ms;
    int btime_ms;
    int winc_ms;
    int binc_ms;
    int movestogo;
    uint64_t nodes;
    int infinite;
    int ponder;
    int move_overhead_ms;
    int slow_mover;
    int searchmove_count;
    Move searchmoves[256];
} SearchLimits;

int evaluate(Board* b);
Move search_root(Board* b, const SearchLimits* limits);

#endif  // SEARCH_H__
