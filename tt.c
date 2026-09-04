#include "tt.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t key;
    Move best_move;
    int32_t score;
    int16_t depth;
    uint8_t flag;
    uint8_t generation;
    uint8_t used;
} TTEntry;

#define TT_CLUSTER_SIZE 4

typedef struct {
    TTEntry entry[TT_CLUSTER_SIZE];
} TTCluster;

static TTCluster* table = NULL;
static size_t table_clusters = 0;
static uint64_t table_mask = 0;
static uint8_t current_generation = 0;

#define TT_AGE_WEIGHT 8

static size_t prev_pow2(size_t x) {
    size_t p = 1;
    while ((p << 1) <= x)
        p <<= 1;
    return p;
}

void tt_free(void) {
    free(table);
    table = NULL;
    table_clusters = 0;
    table_mask = 0;
}

void tt_init(size_t mb) {
    tt_free();

    if (mb < 1)
        mb = 1;

    size_t bytes = mb * 1024ULL * 1024ULL;
    size_t clusters = bytes / sizeof(TTCluster);
    if (clusters < 1024)
        clusters = 1024;
    clusters = prev_pow2(clusters);

    table = (TTCluster*)calloc(clusters, sizeof(TTCluster));
    while (!table && clusters > 1024) {
        clusters >>= 1;
        table = (TTCluster*)calloc(clusters, sizeof(TTCluster));
    }

    table_clusters = table ? clusters : 0;
    table_mask = table_clusters ? (uint64_t)(table_clusters - 1) : 0;
    current_generation = 0;
}

void tt_clear(void) {
    if (table)
        memset(table, 0, table_clusters * sizeof(TTCluster));
    current_generation = 0;
}

size_t tt_size_mb(void) {
    return (table_clusters * sizeof(TTCluster)) / (1024 * 1024);
}

void tt_new_search(void) {
    current_generation++;
}

static TTCluster* tt_cluster_for(uint64_t key) {
    if (!table)
        return NULL;
    return &table[key & table_mask];
}

static TTEntry* tt_find(uint64_t key) {
    TTCluster* c = tt_cluster_for(key);
    if (!c)
        return NULL;
    for (int i = 0; i < TT_CLUSTER_SIZE; i++) {
        TTEntry* e = &c->entry[i];
        if (e->used && e->key == key)
            return e;
    }
    return NULL;
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

static int replacement_score(const TTEntry* e) {
    int age = (int)(uint8_t)(current_generation - e->generation);
    return (int)e->depth - age * TT_AGE_WEIGHT;
}

void tt_store(uint64_t key, int depth, int ply, int score, TTFlag flag, Move best) {
    TTCluster* c = tt_cluster_for(key);
    if (!c)
        return;

    TTEntry* victim = NULL;

    for (int i = 0; i < TT_CLUSTER_SIZE; i++) {
        TTEntry* e = &c->entry[i];

        if (!e->used) {
            victim = e;
            break;
        }
        if (e->key == key) {
            victim = e;
            break;
        }
        if (!victim || replacement_score(e) < replacement_score(victim))
            victim = e;
    }

    if (!victim)
        return;

    victim->used = 1;
    victim->key = key;
    victim->best_move = best;
    victim->score = score_to_tt(score, ply);
    victim->depth = (int16_t)depth;
    victim->flag = (uint8_t)flag;
    victim->generation = current_generation;
}
