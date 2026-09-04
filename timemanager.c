#include "timemanager.h"

#include <math.h>
#include <string.h>
#include <time.h>

#define TIME_CHECK_INTERVAL 256ULL
#define MIN_TIME_RESERVE_MS 8LL
#define MAX_TIME_RESERVE_MS 150LL
#define DEFAULT_MOVES_TO_GO 30
#define MIN_SOFT_MS 1LL
#define MIN_HARD_MS 2LL

/* Fallback effective branching factor used before any real per-depth timing
 * history exists (roughly typical for an ID search with pruning/reductions). */
#define TM_FALLBACK_EBF 3.0
#define TM_EBF_MIN 1.05
#define TM_EBF_MAX 12.0
#define TM_EBF_SMOOTH_ALPHA 0.5

#define TM_SCORE_EWMA_ALPHA 0.4
#define TM_ASPIRATION_OVERHEAD_ALPHA 0.5

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int64_t clamp_i64(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static double clamp_d(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int64_t reserve_for_clock(int64_t remaining, int64_t overhead) {
    int64_t reserve = remaining / 25;
    reserve = clamp_i64(reserve, MIN_TIME_RESERVE_MS, MAX_TIME_RESERVE_MS);
    reserve += overhead > 0 ? overhead : 0;
    return reserve;
}

void tm_init(TimeManager* tm, const SearchLimits* limits, int side) {
    memset(tm, 0, sizeof(*tm));
    tm->soft_limit_ms = -1;
    tm->hard_limit_ms = -1;
    tm->base_soft_ms = -1;
    tm->base_hard_ms = -1;
    tm->start_ms = now_ms();
    tm->next_time_check = TIME_CHECK_INTERVAL;
    tm->node_limit = limits->nodes;
    tm->side = side;
    tm->move_overhead_ms = limits->move_overhead_ms > 0 ? limits->move_overhead_ms : 0;
    tm->slow_mover = limits->slow_mover > 0 ? limits->slow_mover : 100;
    tm->pondering = limits->ponder ? 1 : 0;
    tm->ebf_estimate = 0.0; /* unknown; predictions fall back until real data arrives */

    if (limits->nodes > 0 && limits->movetime_ms <= 0 && limits->wtime_ms <= 0 && limits->btime_ms <= 0)
        return;

    if (limits->movetime_ms > 0) {
        int64_t move_time = limits->movetime_ms;
        int64_t reserve = reserve_for_clock(move_time, tm->move_overhead_ms);
        int64_t budget = move_time - reserve;
        if (budget < 0) budget = 0;

        int64_t hard = budget;
        int64_t soft = budget * 75 / 100;
        soft = soft * tm->slow_mover / 100;
        soft = clamp_i64(soft, 0, hard);

        tm->base_soft_ms = soft;
        tm->base_hard_ms = hard;
        tm->soft_limit_ms = soft;
        tm->hard_limit_ms = hard;
        if (tm->pondering) {
            tm->soft_limit_ms = -1;
            tm->hard_limit_ms = -1;
        }
        return;
    }

    if (limits->infinite || limits->ponder)
        return;

    int64_t remaining = (side == WHITE) ? limits->wtime_ms : limits->btime_ms;
    int64_t increment = (side == WHITE) ? limits->winc_ms : limits->binc_ms;
    tm->remaining_ms = remaining > 0 ? remaining : 0;
    tm->increment_ms = increment > 0 ? increment : 0;
    if (remaining <= 0)
        return;

    int mtg = limits->movestogo > 0 ? limits->movestogo : DEFAULT_MOVES_TO_GO;
    int64_t reserve = reserve_for_clock(remaining, tm->move_overhead_ms);
    int64_t available = remaining - reserve;
    if (available < 1) available = 1;

    /* Base allocation: spend more than a naive 1/moves-to-go fraction when an
     * increment exists, but cap the hard allocation to keep a large reserve. */
    int64_t base = available / mtg;
    int64_t soft = base + increment * 75 / 100;
    int64_t hard = base * 3 / 2 + increment;

    soft -= tm->move_overhead_ms;
    hard -= tm->move_overhead_ms;
    if (soft < MIN_SOFT_MS) soft = MIN_SOFT_MS;
    if (hard < MIN_HARD_MS) hard = MIN_HARD_MS;

    int64_t max_hard = available / 2;
    if (max_hard < MIN_HARD_MS) max_hard = MIN_HARD_MS;
    hard = clamp_i64(hard, MIN_HARD_MS, max_hard);
    soft = clamp_i64(soft * tm->slow_mover / 100, MIN_SOFT_MS, hard);

    tm->base_soft_ms = soft;
    tm->base_hard_ms = hard;
    tm->soft_limit_ms = soft;
    tm->hard_limit_ms = hard;
}

void tm_set_tactical_root(TimeManager* tm, int is_tactical) {
    tm->tactical_root = is_tactical ? 1 : 0;
}

void tm_ponderhit(TimeManager* tm) {
    if (!tm->pondering)
        return;
    tm->pondering = 0;

    int64_t new_start = now_ms();
    int64_t ponder_duration = new_start - tm->start_ms;
    if (ponder_duration < 0) ponder_duration = 0;
    tm->start_ms = new_start;

    /* Shift the in-flight depth's start marker back by the pondering
     * duration rather than resetting it. Per-depth timing history, the EBF
     * estimate, bestmove stability counters, score volatility statistics,
     * fail-high/low totals, and the mate/tactical signal are all left
     * untouched: they were derived from real search effort spent on this
     * exact position while pondering, and remain the best information
     * available the instant the clock starts for real. Discarding them (as
     * a naive "reset everything" ponderhit would) would throw away exactly
     * the context that matters most right after ponderhit. */
    tm->depth_start_ms -= ponder_duration;
    tm->next_time_check = tm->nodes + TIME_CHECK_INTERVAL;

    /* soft/hard were -1 (unlimited) while pondering unless a fixed movetime
     * was given; only fall back to the base budget when no concrete limit
     * had already been established. */
    if (tm->soft_limit_ms < 0) tm->soft_limit_ms = tm->base_soft_ms;
    if (tm->hard_limit_ms < 0) tm->hard_limit_ms = tm->base_hard_ms;
}

void tm_begin_iteration(TimeManager* tm) {
    tm->depth_start_ms = now_ms() - tm->start_ms;
    tm->fail_high_count_iter = 0;
    tm->fail_low_count_iter = 0;
    tm->aspiration_research_ms_iter = 0;
}

static int is_mate_score_delta_sane(int mate_distance) {
    return mate_distance != 0;
}

void tm_iteration_feedback(TimeManager* tm, int depth, int score, int best_move_changed, int mate_distance) {
    tm->iterations_completed++;

    /* --- bestmove stability --- */
    if (tm->have_prev_best_move_flag) {
        if (best_move_changed) {
            tm->bestmove_change_count++;
            tm->stability_streak = 0;
        } else {
            tm->stability_streak++;
        }
    }
    tm->have_prev_best_move_flag = 1;

    /* --- quantitative score volatility (EWMA + running mean/variance) --- */
    int score_delta_abs = 0;
    if (tm->have_prev_score) {
        score_delta_abs = score > tm->prev_score ? score - tm->prev_score : tm->prev_score - score;
        tm->score_delta_ewma = (tm->score_delta_samples == 0)
                                    ? (double)score_delta_abs
                                    : TM_SCORE_EWMA_ALPHA * (double)score_delta_abs +
                                          (1.0 - TM_SCORE_EWMA_ALPHA) * tm->score_delta_ewma;
        tm->score_delta_samples++;
        /* Welford's online algorithm for running mean/variance of |delta|. */
        double x = (double)score_delta_abs;
        double delta1 = x - tm->score_delta_mean;
        tm->score_delta_mean += delta1 / (double)tm->score_delta_samples;
        double delta2 = x - tm->score_delta_mean;
        tm->score_delta_m2 += delta1 * delta2;
    }
    tm->prev_score = score;
    tm->have_prev_score = 1;

    /* --- per-depth timing / EBF / next-iteration time prediction --- */
    int64_t now_elapsed = now_ms() - tm->start_ms;
    int64_t duration = now_elapsed - tm->depth_start_ms;
    if (duration < 0) duration = 0;
    if (depth >= 0 && depth < TM_MAX_TRACKED_DEPTH)
        tm->depth_duration_ms[depth] = duration;

    if (tm->last_completed_depth > 0 && tm->last_completed_depth < TM_MAX_TRACKED_DEPTH) {
        int64_t prev_duration = tm->depth_duration_ms[tm->last_completed_depth];
        if (prev_duration > 0 && duration > 0) {
            double inst_ebf = (double)duration / (double)prev_duration;
            inst_ebf = clamp_d(inst_ebf, TM_EBF_MIN, TM_EBF_MAX);
            tm->ebf_estimate = (tm->ebf_estimate <= 0.0)
                                    ? inst_ebf
                                    : TM_EBF_SMOOTH_ALPHA * inst_ebf + (1.0 - TM_EBF_SMOOTH_ALPHA) * tm->ebf_estimate;
        }
    }
    tm->last_completed_depth = depth;

    /* Aspiration re-search overhead observed during this just-finished
     * iteration, expressed as a fraction of the iteration's total time, fed
     * into a smoothed ratio so it also inflates the *next* prediction (a
     * position that needed re-searches once is likely to need them again). */
    if (duration > 0) {
        double ratio = (double)tm->aspiration_research_ms_iter / (double)duration;
        ratio = clamp_d(ratio, 0.0, 3.0);
        tm->aspiration_overhead_ratio = (tm->aspiration_research_ms_iter == 0 && tm->aspiration_overhead_ratio == 0.0)
                                             ? 0.0
                                             : TM_ASPIRATION_OVERHEAD_ALPHA * ratio +
                                                   (1.0 - TM_ASPIRATION_OVERHEAD_ALPHA) * tm->aspiration_overhead_ratio;
    }

    double ebf = tm->ebf_estimate > 0.0 ? tm->ebf_estimate : TM_FALLBACK_EBF;
    double predicted = (double)duration * ebf * (1.0 + tm->aspiration_overhead_ratio);
    tm->predicted_next_iteration_ms = (int64_t)predicted;

    /* --- mate signal stability --- */
    int mate_unstable = 0;
    if (is_mate_score_delta_sane(mate_distance)) {
        if (tm->last_mate_distance == 0 || tm->last_mate_distance != mate_distance)
            mate_unstable = 1;
        tm->last_mate_distance = mate_distance;
    } else {
        tm->last_mate_distance = 0;
    }
    tm->mate_unstable_last_iteration = mate_unstable;

    /* Ponder search and fixed/no time control have nothing to extend. */
    if (tm->pondering || tm->base_hard_ms < 0)
        return;

    /* --- combine pressure sources into a single extension percentage --- */
    int pressure = 0;
    if (best_move_changed) pressure += 45;

    /* Continuous, EWMA-based volatility contribution instead of fixed
     * score-delta buckets: a sustained ~80cp swing contributes ~40 points,
     * scaling smoothly rather than jumping between hardcoded thresholds. */
    int volatility_pressure = (int)(tm->score_delta_ewma / 2.0);
    if (volatility_pressure > 40) volatility_pressure = 40;
    if (volatility_pressure < 0) volatility_pressure = 0;
    pressure += volatility_pressure;

    /* fail-high/low activity seen during this iteration. */
    int fail_events = tm->fail_high_count_iter + tm->fail_low_count_iter;
    int fail_pressure = fail_events * 15;
    if (fail_pressure > 30) fail_pressure = 30;
    pressure += fail_pressure;

    /* An unstable mate signal (first sighting, or the reported mate distance
     * changed from the previous iteration) strongly suggests the line has
     * not yet been verified deeply enough to trust; force a large extension
     * so the search keeps going instead of committing to a possibly-false
     * mate score. */
    if (mate_distance != 0 && mate_unstable) pressure += 100;

    if (depth <= 3) pressure /= 2;

    /* --- caps widen for tactical root positions and mate lines --- */
    int64_t soft_cap = tm->base_hard_ms * 90 / 100;
    int64_t hard_cap = tm->base_hard_ms * 125 / 100;
    if (tm->tactical_root) {
        soft_cap = tm->base_hard_ms * 110 / 100;
        hard_cap = tm->base_hard_ms * 150 / 100;
    }
    if (mate_distance != 0) {
        soft_cap = tm->base_hard_ms * 160 / 100;
        hard_cap = tm->base_hard_ms * 220 / 100;
    }
    if (soft_cap < tm->base_soft_ms) soft_cap = tm->base_soft_ms;
    if (hard_cap < tm->base_hard_ms) hard_cap = tm->base_hard_ms;

    if (pressure <= 0)
        return;

    int64_t soft = tm->soft_limit_ms;
    int64_t hard = tm->hard_limit_ms;
    soft += soft * pressure / 100;
    hard += hard * pressure / 100;
    if (soft > soft_cap) soft = soft_cap;
    if (hard > hard_cap) hard = hard_cap;
    if (soft > hard) soft = hard;

    tm->soft_limit_ms = soft;
    tm->hard_limit_ms = hard;
}

void tm_notify_aspiration_fail(TimeManager* tm, int fail_high) {
    if (fail_high) {
        tm->fail_high_count_iter++;
        tm->fail_high_count_total++;
    } else {
        tm->fail_low_count_iter++;
        tm->fail_low_count_total++;
    }

    if (tm->pondering || tm->base_hard_ms < 0)
        return;

    /* Grant an immediate, modest extension so the re-search that is about to
     * run is not starved by a soft/hard deadline that was sized for the
     * (apparently wrong) previous aspiration window. This is on top of the
     * end-of-iteration pressure applied once the iteration eventually
     * completes. */
    int64_t soft_cap = tm->base_hard_ms * 130 / 100;
    int64_t hard_cap = tm->base_hard_ms * 160 / 100;
    if (tm->tactical_root) hard_cap = tm->base_hard_ms * 200 / 100;

    int64_t bump = tm->base_hard_ms * 12 / 100;
    if (bump < 5) bump = 5;

    int64_t soft = tm->soft_limit_ms + bump;
    int64_t hard = tm->hard_limit_ms + bump;
    if (soft > soft_cap) soft = soft_cap;
    if (hard > hard_cap) hard = hard_cap;
    if (soft > hard) soft = hard;

    tm->soft_limit_ms = soft;
    tm->hard_limit_ms = hard;
}

void tm_notify_aspiration_research(TimeManager* tm, int64_t research_ms) {
    if (research_ms < 0) research_ms = 0;
    tm->aspiration_research_ms_iter += research_ms;
    tm->aspiration_research_ms_total += research_ms;
    tm->aspiration_research_count_total++;
}

int tm_should_start_iteration(const TimeManager* tm, int next_depth) {
    if (tm->pondering || tm->hard_limit_ms < 0)
        return 1;
    if (next_depth <= 2)
        return 1;
    if (tm->predicted_next_iteration_ms <= 0)
        return 1;

    int64_t elapsed = now_ms() - tm->start_ms;
    int64_t remaining_hard = tm->hard_limit_ms - elapsed;
    if (remaining_hard <= 0)
        return 0;

    /* Require headroom beyond the raw prediction: starting an iteration we
     * cannot finish wastes the time already spent on it, since an aborted
     * iteration's result is discarded by the caller. */
    int64_t required = tm->predicted_next_iteration_ms + tm->predicted_next_iteration_ms / 4;
    return required <= remaining_hard;
}

void tm_tick(TimeManager* tm) {
    tm->nodes++;
    if (tm->nodes >= tm->next_time_check)
        tm->next_time_check = tm->nodes + TIME_CHECK_INTERVAL;
}

int tm_node_limit_reached(const TimeManager* tm) {
    return tm->node_limit != 0 && tm->nodes >= tm->node_limit;
}

int tm_hard_expired(const TimeManager* tm) {
    if (tm->hard_limit_ms < 0 || tm->nodes < tm->next_time_check)
        return 0;
    return now_ms() - tm->start_ms >= tm->hard_limit_ms;
}

int tm_hard_expired_now(const TimeManager* tm) {
    if (tm->hard_limit_ms < 0 || tm->pondering)
        return 0;
    return now_ms() - tm->start_ms >= tm->hard_limit_ms;
}

int tm_soft_expired(const TimeManager* tm) {
    if (tm->soft_limit_ms < 0 || tm->pondering)
        return 0;
    return now_ms() - tm->start_ms >= tm->soft_limit_ms;
}

int64_t tm_start_ms(const TimeManager* tm) { return tm->start_ms; }
int64_t tm_soft_limit(const TimeManager* tm) { return tm->soft_limit_ms; }
int64_t tm_hard_limit(const TimeManager* tm) { return tm->hard_limit_ms; }
uint64_t tm_nodes(const TimeManager* tm) { return tm->nodes; }
int64_t tm_elapsed_ms(const TimeManager* tm) { return now_ms() - tm->start_ms; }

int tm_bestmove_change_count(const TimeManager* tm) { return tm->bestmove_change_count; }
int tm_stability_streak(const TimeManager* tm) { return tm->stability_streak; }

double tm_score_volatility_stddev(const TimeManager* tm) {
    if (tm->score_delta_samples < 2)
        return 0.0;
    return sqrt(tm->score_delta_m2 / (double)(tm->score_delta_samples - 1));
}

double tm_score_volatility_ewma(const TimeManager* tm) { return tm->score_delta_ewma; }
double tm_ebf(const TimeManager* tm) { return tm->ebf_estimate > 0.0 ? tm->ebf_estimate : TM_FALLBACK_EBF; }
int64_t tm_predicted_next_iteration_ms(const TimeManager* tm) { return tm->predicted_next_iteration_ms; }
int tm_fail_high_total(const TimeManager* tm) { return tm->fail_high_count_total; }
int tm_fail_low_total(const TimeManager* tm) { return tm->fail_low_count_total; }
int tm_aspiration_research_count(const TimeManager* tm) { return tm->aspiration_research_count_total; }
int64_t tm_aspiration_research_ms_total(const TimeManager* tm) { return tm->aspiration_research_ms_total; }
int tm_is_tactical_root(const TimeManager* tm) { return tm->tactical_root; }
int tm_last_mate_distance(const TimeManager* tm) { return tm->last_mate_distance; }
