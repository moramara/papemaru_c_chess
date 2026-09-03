#include "zobrist.h"

uint64_t zobrist_piece[2][PIECE_TYPE_NB][64];
uint64_t zobrist_side;
uint64_t zobrist_castle[16];
uint64_t zobrist_ep_file[8];

static int tables_initialized = 0;

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;

static uint64_t next_rand(void) {
    rng_state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = rng_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void zobrist_init_tables(void) {
    if (tables_initialized)
        return;
    tables_initialized = 1;

    for (int color = 0; color < 2; color++)
        for (int pt = 0; pt < PIECE_TYPE_NB; pt++)
            for (int sq = 0; sq < 64; sq++)
                zobrist_piece[color][pt][sq] = next_rand();

    zobrist_side = next_rand();

    for (int i = 0; i < 16; i++)
        zobrist_castle[i] = next_rand();

    for (int i = 0; i < 8; i++)
        zobrist_ep_file[i] = next_rand();
}
