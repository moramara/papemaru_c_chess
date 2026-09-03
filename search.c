#include "search.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "eval.h"
#include "tt.h"

#define INF 1000000
#define MATE 50000

#define MAX_CHECK_EXT 8

#define MAX_PLY 256

static long long time_limit_ms = -1;
static long long search_start_ms = 0;
static long long node_count = 0;

#define TIME_CHECK_INTERVAL 2048

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void check_time(void) {
    if (time_limit_ms < 0)
        return;
    node_count++;
    if ((node_count & (TIME_CHECK_INTERVAL - 1)) != 0)
        return;
    if (now_ms() - search_start_ms >= time_limit_ms)
        stop_flag = 1;
}

static const int pawn_pst[64] = {
    // clang-format off
     0,   0,   0,   0,   0,   0,   0,   0,
    50,  50,  50,  50,  50,  50,  50,  50,
    10,  10,  20,  30,  30,  20,  10,  10,
     5,   5,  10,  25,  25,  10,   5,   5,
     0,   0,   0,  20,  20,   0,   0,   0,
     5,  -5, -10,   0,   0, -10,  -5,   5,
     5,  10,  10, -20, -20,  10,  10,   5,
     0,   0,   0,   0,   0,   0,   0,   0,
    // clang-format on
};

static const int knight_pst[64] = {
    // clang-format off
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20,   0,   0,   0,   0, -20, -40,
    -30,   0,  10,  15,  15,  10,   0, -30,
    -30,   5,  15,  20,  20,  15,   5, -30,
    -30,   0,  15,  20,  20,  15,   0, -30,
    -30,   5,  10,  15,  15,  10,   5, -30,
    -40, -20,   0,   5,   5,   0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50,
    // clang-format on
};

static const int bishop_pst[64] = {
    // clang-format off
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   5,  10,  10,   5,   0, -10,
    -10,   5,   5,  10,  10,   5,   5, -10,
    -10,   0,  10,  10,  10,  10,   0, -10,
    -10,  10,  10,  10,  10,  10,  10, -10,
    -10,   5,   0,   0,   0,   0,   5, -10,
    -20, -10, -10, -10, -10, -10, -10, -20,
    // clang-format on
};

static const int rook_pst[64] = {
    // clang-format off
      0,  0,  0,  0,  0,  0,  0,  0,
      5, 10, 10, 10, 10, 10, 10,  5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      0,  0,  0,  5,  5,  0,  0,  0,
    // clang-format on
};

static const int queen_pst[64] = {
    // clang-format off
    -20, -10, -10,  -5,  -5, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   5,   5,   5,   5,   0, -10,
     -5,   0,   5,   5,   5,   5,   0,  -5,
      0,   0,   5,   5,   5,   5,   0,  -5,
    -10,   5,   5,   5,   5,   5,   0, -10,
    -10,   0,   5,   0,   0,   0,   0, -10,
    -20, -10, -10,  -5,  -5, -10, -10, -20,
    // clang-format on
};

static const int king_pst[64] = {
    // clang-format off
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -10, -20, -20, -20, -20, -20, -20, -10,
     20,  20,   0,   0,   0,   0,  20,  20,
     20,  30,  10,   0,   0,  10,  30,  20,
    // clang-format on
};

static const int* pst_table_for(int type) {
    switch (type) {
        case PAWN:
            return pawn_pst;
        case KNIGHT:
            return knight_pst;
        case BISHOP:
            return bishop_pst;
        case ROOK:
            return rook_pst;
        case QUEEN:
            return queen_pst;
        case KING:
            return king_pst;
        default:
            return NULL;
    }
}

static int pst_score(int color, int type, int sq) {
    const int* table = pst_table_for(type);
    if (!table)
        return 0;
    return (color == WHITE) ? table[sq ^ 56] : -table[sq];
}

static const int piece_material[PIECE_TYPE_NB] = {100, 320, 330, 500, 900, 20000};

int evaluate(Board* b) {
    int score = 0;
    for (int color = WHITE; color <= BLACK; color++) {
        int sign = (color == WHITE) ? 1 : -1;
        for (int type = PAWN; type <= KING; type++) {
            Bitboard bb = b->pieces[color][type];
            while (bb) {
                int sq = pop_lsb(&bb);
                score += sign * piece_material[type];
                score += pst_score(color, type, sq);
            }
        }
    }
    score += eval_extra(b);
    return score;
}

static int piece_value(int p) {
    switch (p > 0 ? p : -p) {
        case WP:
            return 100;
        case WN:
            return 320;
        case WB:
            return 330;
        case WR:
            return 500;
        case WQ:
            return 900;
        case WK:
            return 20000;
        default:
            return 0;
    }
}

static int mvv_lva_score(Board* b, Move m) {
    int moving = piece_at(b, m.from);
    int attacker = piece_value(moving);
    int victim = piece_at(b, m.to);
    if (victim == EMPTY && m.to == b->enpas && (moving == WP || moving == BP))
        victim = WP;
    return piece_value(victim) * 10 - attacker;
}

static Bitboard attackers_to(const Board* b, int sq, Bitboard occ) {
    Bitboard att = 0;
    att |= pawn_attacks[BLACK][sq] & b->pieces[WHITE][PAWN];
    att |= pawn_attacks[WHITE][sq] & b->pieces[BLACK][PAWN];
    att |= knight_attacks[sq] & (b->pieces[WHITE][KNIGHT] | b->pieces[BLACK][KNIGHT]);
    att |= king_attacks[sq] & (b->pieces[WHITE][KING] | b->pieces[BLACK][KING]);
    Bitboard bishops_queens =
        b->pieces[WHITE][BISHOP] | b->pieces[WHITE][QUEEN] | b->pieces[BLACK][BISHOP] | b->pieces[BLACK][QUEEN];
    att |= bishop_attacks(sq, occ) & bishops_queens;
    Bitboard rooks_queens =
        b->pieces[WHITE][ROOK] | b->pieces[WHITE][QUEEN] | b->pieces[BLACK][ROOK] | b->pieces[BLACK][QUEEN];
    att |= rook_attacks(sq, occ) & rooks_queens;
    return att & occ;
}

static int least_valuable_attacker(const Board* b, Bitboard attackers, int side, int* out_sq, int* out_type) {
    for (int pt = PAWN; pt <= KING; pt++) {
        Bitboard bb = attackers & b->pieces[side][pt];
        if (bb) {
            *out_sq = bb_lsb(bb);
            *out_type = pt;
            return 1;
        }
    }
    return 0;
}

static int see(const Board* b, Move m) {
    int us = b->side;
    int them = 1 - us;
    int from = m.from;
    int to = m.to;

    int attacker_piece = piece_at(b, from);
    int attacker_type = (attacker_piece > 0 ? attacker_piece : -attacker_piece) - 1;

    int captured_sq = to;
    int captured_type;
    int is_ep = (attacker_type == PAWN) && (to == b->enpas) && piece_at(b, to) == EMPTY;
    if (is_ep) {
        captured_type = PAWN;
        captured_sq = (us == WHITE) ? to - 8 : to + 8;
    } else {
        int cp = piece_at(b, to);
        captured_type = (cp == EMPTY) ? -1 : ((cp > 0 ? cp : -cp) - 1);
    }

    int gain[32];
    int d = 0;
    gain[0] = (captured_type == -1) ? 0 : piece_material[captured_type];

    Bitboard occ = b->all & ~bb_bit(from);
    if (is_ep)
        occ &= ~bb_bit(captured_sq);

    int side = them;
    int cur_value = piece_material[attacker_type];

    while (d + 1 < 32) {
        Bitboard side_attackers = attackers_to(b, to, occ) & b->occupied[side];
        int sq, pt;
        if (!least_valuable_attacker(b, side_attackers, side, &sq, &pt))
            break;

        d++;
        gain[d] = cur_value - gain[d - 1];

        occ &= ~bb_bit(sq);
        cur_value = piece_material[pt];
        side = 1 - side;
    }

    while (d > 0) {
        int stop_here = -gain[d - 1];
        int continue_here = gain[d];
        gain[d - 1] = -(continue_here > stop_here ? continue_here : stop_here);
        d--;
    }
    return gain[0];
}

static int same_move(Move m1, Move m2) {
    return m1.from == m2.from && m1.to == m2.to && m1.promo == m2.promo;
}

static int is_capture_move(const Board* b, Move m) {
    return piece_at(b, m.to) != EMPTY || m.to == b->enpas;
}

static Move killer_moves[MAX_PLY][2];
static int history[2][64][64];

static void reset_ordering_heuristics(void) {
    memset(killer_moves, 0, sizeof(killer_moves));
    memset(history, 0, sizeof(history));
}

static void record_killer(int ply, Move m) {
    if (ply < 0 || ply >= MAX_PLY)
        return;
    if (same_move(killer_moves[ply][0], m))
        return;
    killer_moves[ply][1] = killer_moves[ply][0];
    killer_moves[ply][0] = m;
}

static void record_history(int side, Move m, int depth) {
    int bonus = depth * depth;
    int* slot = &history[side][m.from][m.to];
    *slot += bonus;
    if (*slot > 1000000) {
        for (int f = 0; f < 64; f++)
            for (int t = 0; t < 64; t++)
                history[side][f][t] /= 2;
    }
}

static void on_quiet_cutoff(Board* b, Move m, int depth, int ply) {
    record_killer(ply, m);
    record_history(b->side, m, depth);
}

static int is_killer(int ply, Move m) {
    if (ply < 0 || ply >= MAX_PLY)
        return 0;
    return same_move(killer_moves[ply][0], m) || same_move(killer_moves[ply][1], m);
}

static void order_moves(Board* b, Move* list, int n, const Move* tt_move, int ply) {
    int score[256];
    for (int i = 0; i < n; i++) {
        int is_capture = is_capture_move(b, list[i]);
        int s;
        if (tt_move && same_move(list[i], *tt_move)) {
            s = 2000000;
        } else if (is_capture) {
            int see_val = see(b, list[i]);
            if (see_val >= 0)
                s = 1000000 + mvv_lva_score(b, list[i]);
            else
                s = -1000000 + see_val;
        } else if (ply >= 0 && ply < MAX_PLY && same_move(list[i], killer_moves[ply][0])) {
            s = 900001;
        } else if (ply >= 0 && ply < MAX_PLY && same_move(list[i], killer_moves[ply][1])) {
            s = 900000;
        } else {
            s = history[b->side][list[i].from][list[i].to];
        }
        score[i] = s;
    }
    for (int i = 1; i < n; i++) {
        Move mv = list[i];
        int sc = score[i];
        int j = i - 1;
        while (j >= 0 && score[j] < sc) {
            list[j + 1] = list[j];
            score[j + 1] = score[j];
            j--;
        }
        list[j + 1] = mv;
        score[j + 1] = sc;
    }
}

#define QSEARCH_DELTA_MARGIN 200

static int quiescence(Board* b, int alpha, int beta, int check_ext, int ply) {
    check_time();
    if (stop_flag)
        return 0;

    int in_check = (check_ext < MAX_CHECK_EXT) && is_in_check(b, b->side);

    int stand_pat = 0;
    if (!in_check) {
        stand_pat = evaluate(b) * (b->side == WHITE ? 1 : -1);
        if (stand_pat >= beta)
            return beta;
        if (alpha < stand_pat)
            alpha = stand_pat;
    }

    Move list[256];
    int n;
    int see_val[256];
    if (in_check) {
        n = gen_moves(b, list);
        order_moves(b, list, n, NULL, ply);
    } else {
        n = gen_captures(b, list);
        for (int i = 0; i < n; i++)
            see_val[i] = see(b, list[i]);
        for (int i = 1; i < n; i++) {
            Move mv = list[i];
            int sc = see_val[i];
            int j = i - 1;
            while (j >= 0 && see_val[j] < sc) {
                list[j + 1] = list[j];
                see_val[j + 1] = see_val[j];
                j--;
            }
            list[j + 1] = mv;
            see_val[j + 1] = sc;
        }
    }

    int legal = 0;
    for (int i = 0; i < n; i++) {
        if (!in_check) {
            if (see_val[i] < 0)
                continue;
            if (!list[i].promo) {
                int captured = piece_at(b, list[i].to);
                int cap_value = (captured == EMPTY) ? piece_value(WP) : piece_value(captured);
                if (stand_pat + cap_value + QSEARCH_DELTA_MARGIN < alpha)
                    continue;
            }
        }

        Undo u;
        if (!make_move(b, list[i], &u))
            continue;
        legal++;
        int next_ext = in_check ? check_ext + 1 : check_ext;
        int score = -quiescence(b, -beta, -alpha, next_ext, ply + 1);
        unmake_move(b, list[i], &u);
        if (score >= beta)
            return beta;
        if (score > alpha)
            alpha = score;
    }

    if (in_check && legal == 0)
        return -MATE + ply;

    return alpha;
}

static int has_non_pawn_material(const Board* b, int side) {
    return (b->pieces[side][KNIGHT] | b->pieces[side][BISHOP] | b->pieces[side][ROOK] | b->pieces[side][QUEEN]) != 0;
}

#define NULL_MOVE_MIN_DEPTH 3

#define LMR_MIN_DEPTH 3
#define LMR_FULL_MOVES 3

static int lmr_reduction(int depth, int move_number) {
    if (depth < LMR_MIN_DEPTH || move_number <= LMR_FULL_MOVES)
        return 0;
    int r = 1;
    if (depth >= 6 && move_number > 6)
        r = 2;
    if (depth >= 10 && move_number > 12)
        r = 3;
    return r;
}

static int negamax(Board* b, int depth, int alpha, int beta, int ply, int allow_null) {
    check_time();
    if (stop_flag)
        return 0;
    if (depth == 0)
        return quiescence(b, alpha, beta, 0, ply);

    uint64_t key = b->hash;
    int alpha_orig = alpha;

    int tt_score;
    if (tt_probe_score(key, depth, ply, alpha, beta, &tt_score))
        return tt_score;

    int in_check = is_in_check(b, b->side);

    if (allow_null && !in_check && depth >= NULL_MOVE_MIN_DEPTH && ply > 0 && has_non_pawn_material(b, b->side)) {
        int R = (depth > 6) ? 3 : 2;
        Undo nu;
        make_null_move(b, &nu);
        int null_score = -negamax(b, depth - 1 - R, -beta, -beta + 1, ply + 1, 0);
        unmake_null_move(b, &nu);
        if (!stop_flag && null_score >= beta)
            return beta;
    }

    Move tt_move = {0, 0, 0};
    int have_tt_move = tt_probe_move(key, &tt_move);

    Move list[256];
    int n = gen_moves(b, list);
    order_moves(b, list, n, have_tt_move ? &tt_move : NULL, ply);
    int legal = 0;
    int best = -INF;
    Move best_move = {0, 0, 0};

    for (int i = 0; i < n; i++) {
        Move mv = list[i];

        int is_quiet = !is_capture_move(b, mv) && !mv.promo;

        Undo u;
        if (!make_move(b, mv, &u))
            continue;
        legal++;

        int gives_check = is_in_check(b, b->side);

        int score;
        if (legal == 1) {
            score = -negamax(b, depth - 1, -beta, -alpha, ply + 1, 1);
        } else {
            int reduction = 0;
            if (!in_check && is_quiet && !gives_check) {
                reduction = lmr_reduction(depth, legal);
                if (reduction > 0 && is_killer(ply, mv))
                    reduction--;
            }

            score = -negamax(b, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, 1);
            if (score > alpha && reduction > 0)
                score = -negamax(b, depth - 1, -alpha - 1, -alpha, ply + 1, 1);
            if (score > alpha && score < beta)
                score = -negamax(b, depth - 1, -beta, -alpha, ply + 1, 1);
        }

        unmake_move(b, mv, &u);
        if (score > best) {
            best = score;
            best_move = list[i];
        }
        if (score > alpha)
            alpha = score;
        if (alpha >= beta) {
            if (is_quiet)
                on_quiet_cutoff(b, mv, depth, ply);
            break;
        }
    }
    if (legal == 0) {
        if (in_check)
            return -MATE + ply;
        return 0;
    }

    if (!stop_flag) {
        TTFlag flag;
        if (best <= alpha_orig)
            flag = TT_UPPERBOUND;
        else if (best >= beta)
            flag = TT_LOWERBOUND;
        else
            flag = TT_EXACT;
        tt_store(key, depth, ply, best, flag, best_move);
    }

    return best;
}

Move search_root(Board* b, int depth, int movetime_ms) {
    node_count = 0;
    search_start_ms = now_ms();
    time_limit_ms = (movetime_ms > 0) ? movetime_ms : -1;
    reset_ordering_heuristics();

    Move list[256];
    int n = gen_moves(b, list);
    Move tt_move = {0, 0, 0};
    int have_tt_move = tt_probe_move(b->hash, &tt_move);
    order_moves(b, list, n, have_tt_move ? &tt_move : NULL, 0);
    Move bestMove = {0, 0, 0};
    int haveMove = 0;
    for (int i = 0; i < n; i++) {
        Undo u;
        if (make_move(b, list[i], &u)) {
            unmake_move(b, list[i], &u);
            bestMove = list[i];
            haveMove = 1;
            break;
        }
    }
    if (!haveMove)
        return bestMove;

    for (int d = 1; d <= depth; d++) {
        int alpha = -INF;
        int beta = INF;
        int iterBestScore = -INF;
        Move iterBestMove = {0, 0, 0};
        int iterHave = 0;

        for (int i = 0; i < n; i++) {
            if (same_move(list[i], bestMove)) {
                Move tmp = list[0];
                list[0] = list[i];
                list[i] = tmp;
                break;
            }
        }

        int move_num = 0;
        for (int i = 0; i < n; i++) {
            if (stop_flag && d > 1)
                break;
            Undo u;
            if (!make_move(b, list[i], &u))
                continue;
            move_num++;

            int score;
            if (move_num == 1) {
                score = -negamax(b, d - 1, -beta, -alpha, 1, 1);
            } else {
                score = -negamax(b, d - 1, -alpha - 1, -alpha, 1, 1);
                if (score > alpha && score < beta)
                    score = -negamax(b, d - 1, -beta, -alpha, 1, 1);
            }

            unmake_move(b, list[i], &u);
            if (stop_flag && d > 1)
                break;
            if (score > iterBestScore) {
                iterBestScore = score;
                iterBestMove = list[i];
                iterHave = 1;
            }
            if (score > alpha)
                alpha = score;
        }

        if (iterHave) {
            bestMove = iterBestMove;
            haveMove = 1;
            printf("info depth %d score cp %d\n", d, iterBestScore);
            fflush(stdout);
            if (!stop_flag)
                tt_store(b->hash, d, 0, iterBestScore, TT_EXACT, iterBestMove);
        }

        if (stop_flag)
            break;
    }

    return bestMove;
}