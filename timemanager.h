#ifndef TIMEMANAGER_H__
#define TIMEMANAGER_H__

#include <stdint.h>

#include "search.h"

#define TM_MAX_TRACKED_DEPTH 128

typedef struct {
    int64_t start_ms;
    int64_t soft_limit_ms;
    int64_t hard_limit_ms;
    int64_t base_soft_ms;
    int64_t base_hard_ms;
    int64_t remaining_ms;
    int64_t increment_ms;
    int64_t move_overhead_ms;
    int slow_mover;
    int side;
    int pondering;
    uint64_t node_limit;
    uint64_t nodes;
    uint64_t next_time_check;

    /* --- root bestmove stability statistics --- */
    int iterations_completed;
    int bestmove_change_count;
    int stability_streak;
    int have_prev_best_move_flag; /* whether a previous iteration exists at all */

    /* --- quantitative score volatility --- */
    int have_prev_score;
    int prev_score;
    double score_delta_ewma;   /* smoothed |delta| between consecutive iterations */
    double score_delta_mean;   /* running mean of |delta|, Welford's method */
    double score_delta_m2;     /* running sum of squared deviations, Welford's method */
    int score_delta_samples;

    /* --- per-depth timing / effective branching factor / prediction --- */
    int64_t depth_start_ms;                          /* elapsed-ms marker for current depth's start */
    int64_t depth_duration_ms[TM_MAX_TRACKED_DEPTH];  /* wall time spent solely on depth d */
    int last_completed_depth;
    double ebf_estimate;
    int64_t predicted_next_iteration_ms;

    /* --- fail-high / fail-low tracking --- */
    int fail_high_count_iter;
    int fail_low_count_iter;
    int fail_high_count_total;
    int fail_low_count_total;

    /* --- aspiration re-search cost accounting --- */
    int64_t aspiration_research_ms_iter;
    int64_t aspiration_research_ms_total;
    int aspiration_research_count_total;
    double aspiration_overhead_ratio; /* smoothed fraction of iteration time spent re-searching */

    /* --- mate / tactical handling --- */
    int tactical_root;
    int last_mate_distance; /* 0 = no mate seen; >0 = mate for us in N; <0 = mate against us in N */
    int mate_unstable_last_iteration;
} TimeManager;

void tm_init(TimeManager* tm, const SearchLimits* limits, int side);
void tm_set_tactical_root(TimeManager* tm, int is_tactical);
void tm_tick(TimeManager* tm);
void tm_ponderhit(TimeManager* tm);

/* Call once at the start of each iterative-deepening depth, before searching it. */
void tm_begin_iteration(TimeManager* tm);

/* Call once a depth has finished searching (only when a usable result was
 * produced, i.e. the search was not aborted mid-iteration). Updates bestmove
 * stability stats, score volatility, per-depth timing / EBF / next-iteration
 * prediction, and applies time extensions for instability, fail-high/low
 * activity seen this iteration, and mate/tactical signals. */
void tm_iteration_feedback(TimeManager* tm, int depth, int score, int best_move_changed, int mate_distance);

/* Call immediately when an aspiration-window research is triggered (fail low
 * or fail high), before the re-search runs, so the extra time is available
 * to the in-flight re-search rather than only being granted after the fact. */
void tm_notify_aspiration_fail(TimeManager* tm, int fail_high);

/* Call after an aspiration pass that failed (low or high) completes, with
 * the wall-clock cost of that wasted pass, so TimeManager's cost model and
 * next-iteration prediction account for aspiration re-search overhead. */
void tm_notify_aspiration_research(TimeManager* tm, int64_t research_ms);

/* Decide whether it is worth starting `next_depth` at all, based on the
 * predicted cost of that iteration versus the remaining hard budget. This
 * avoids starting an iteration that has no realistic chance of completing,
 * since an aborted iteration's result is discarded and the time spent on it
 * is wasted. */
int tm_should_start_iteration(const TimeManager* tm, int next_depth);

int tm_hard_expired(const TimeManager* tm);
int tm_hard_expired_now(const TimeManager* tm);
int tm_soft_expired(const TimeManager* tm);
int tm_node_limit_reached(const TimeManager* tm);
int64_t tm_start_ms(const TimeManager* tm);
int64_t tm_soft_limit(const TimeManager* tm);
int64_t tm_hard_limit(const TimeManager* tm);
uint64_t tm_nodes(const TimeManager* tm);
int64_t tm_elapsed_ms(const TimeManager* tm);

/* --- statistics accessors, mainly for UCI diagnostic reporting --- */
int tm_bestmove_change_count(const TimeManager* tm);
int tm_stability_streak(const TimeManager* tm);
double tm_score_volatility_stddev(const TimeManager* tm);
double tm_score_volatility_ewma(const TimeManager* tm);
double tm_ebf(const TimeManager* tm);
int64_t tm_predicted_next_iteration_ms(const TimeManager* tm);
int tm_fail_high_total(const TimeManager* tm);
int tm_fail_low_total(const TimeManager* tm);
int tm_aspiration_research_count(const TimeManager* tm);
int64_t tm_aspiration_research_ms_total(const TimeManager* tm);
int tm_is_tactical_root(const TimeManager* tm);
int tm_last_mate_distance(const TimeManager* tm);

#endif  // TIMEMANAGER_H__
