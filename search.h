#ifndef SEARCH_H__
#define SEARCH_H__

#include "board.h"

extern volatile int stop_flag;

int evaluate(Board* b);
Move search_root(Board* b, int depth, int movetime_ms);

#endif  // SEARCH_H__