#include "tt.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t key;
    Move best_move;
    int32_t score;
    int16_t depth;
    uint8_t flag;
    uint8_t used;
} TTEntry;

static TTEntry* table = NULL;
static size_t table_entries = 0;
static uint64_t table_mask = 0;

static size_t prev_pow2(size_t x) {
    size_t p = 1;
    while ((p << 1) <= x)
        p <<= 1;
    return p;
}

void tt_free(void) {
    free(table);
    table = NULL;
    table_entries = 0;
    table_mask = 0;
}

void tt_init(size_t mb) {
    tt_free();

    if (mb < 1)
        mb = 1;

    size_t bytes = mb * 1024ULL * 1024ULL;
    size_t entries = bytes / sizeof(TTEntry);
    if (entries < 1024)
        entries = 1024;
    entries = prev_pow2(entries);

    table = (TTEntry*)calloc(entries, sizeof(TTEntry));
    while (!table && entries > 1024) {
        entries >>= 1;
        table = (TTEntry*)calloc(entries, sizeof(TTEntry));
    }

    table_entries = table ? entries : 0;
    table_mask = table_entries ? (uint64_t)(table_entries - 1) : 0;
}

void tt_clear(void) {
    if (table)
        memset(table, 0, table_entries * sizeof(TTEntry));
}

size_t tt_size_mb(void) {
    return (table_entries * sizeof(TTEntry)) / (1024 * 1024);
}

static TTEntry* tt_find(uint64_t key) {
    if (!table)
        return NULL;
    TTEntry* e = &table[key & table_mask];
    if (!e->used || e->key != key)
        return NULL;
    return e;
}

static int score_to_tt(int score, int ply) {
    if (score >= TT_MATE_THRESHOLD)
        return score + ply;
    if (score <= -TT_MATE_THRESHOLD)
        return score - ply;
    return score;
}

static int score_from_tt(int score, int ply) {
    if (score >= TT_MATE_THRESHOLD)
        return score - ply;
    if (score <= -TT_MATE_THRESHOLD)
        return score + ply;
    return score;
}

int tt_probe_score(uint64_t key, int depth, int ply, int alpha, int beta, int* out_score) {
    TTEntry* e = tt_find(key);
    if (!e || e->depth < depth)
        return 0;

    int score = score_from_tt(e->score, ply);

    if (e->flag == TT_EXACT) {
        *out_score = score;
        return 1;
    }
    if (e->flag == TT_LOWERBOUND && score >= beta) {
        *out_score = score;
        return 1;
    }
    if (e->flag == TT_UPPERBOUND && score <= alpha) {
        *out_score = score;
        return 1;
    }
    return 0;
}

int tt_probe_move(uint64_t key, Move* out_move) {
    TTEntry* e = tt_find(key);
    if (!e)
        return 0;
    *out_move = e->best_move;
    return 1;
}

void tt_store(uint64_t key, int depth, int ply, int score, TTFlag flag, Move best) {
    if (!table)
        return;

    TTEntry* e = &table[key & table_mask];

    if (e->used && e->key == key && e->depth > depth)
        return;

    e->used = 1;
    e->key = key;
    e->best_move = best;
    e->score = score_to_tt(score, ply);
    e->depth = (int16_t)depth;
    e->flag = (uint8_t)flag;
}
