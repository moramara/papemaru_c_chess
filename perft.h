#ifndef PERFT_H__
#define PERFT_H__

#include "board.h"

long long perft(Board* b, int depth);

long long perft_divide(Board* b, int depth, void (*report)(const char* move_str, long long nodes));

#endif  // PERFT_H__
