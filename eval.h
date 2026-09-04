#ifndef EVAL_H__
#define EVAL_H__

#include "board.h"

void eval_init_tables(void);

int eval_extra(const Board* b, int phase, int max_phase);

#endif  // EVAL_H__
