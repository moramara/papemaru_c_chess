#include "eval.h"

#include <stdlib.h>

#include "bitboard.h"

static int tables_ready = 0;

static Bitboard file_mask[8];
static Bitboard adjacent_files_mask[8];

static Bitboard ahead_ranks_mask[2][8];
static Bitboard passed_pawn_mask[2][64];
static Bitboard outpost_mask[2][64];

void eval_init_tables(void) {
    if (tables_ready)
        return;
    tables_ready = 1;

    static const Bitboard files[8] = {FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H};
    static const Bitboard ranks[8] = {RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8};

    for (int f = 0; f < 8; f++)
        file_mask[f] = files[f];

    for (int f = 0; f < 8; f++) {
        Bitboard m = 0;
        if (f > 0)
            m |= files[f - 1];
        if (f < 7)
            m |= files[f + 1];
        adjacent_files_mask[f] = m;
    }

    for (int r = 0; r < 8; r++) {
        Bitboard white_ahead = 0;
        for (int rr = r + 1; rr < 8; rr++)
            white_ahead |= ranks[rr];
        ahead_ranks_mask[WHITE][r] = white_ahead;

        Bitboard black_ahead = 0;
        for (int rr = r - 1; rr >= 0; rr--)
            black_ahead |= ranks[rr];
        ahead_ranks_mask[BLACK][r] = black_ahead;
    }

    for (int sq = 0; sq < 64; sq++) {
        int r = sq / 8;
        int f = sq % 8;
        Bitboard own_and_adjacent_files = file_mask[f] | adjacent_files_mask[f];

        passed_pawn_mask[WHITE][sq] = own_and_adjacent_files & ahead_ranks_mask[WHITE][r];
        passed_pawn_mask[BLACK][sq] = own_and_adjacent_files & ahead_ranks_mask[BLACK][r];

        outpost_mask[WHITE][sq] = adjacent_files_mask[f] & ahead_ranks_mask[WHITE][r];
        outpost_mask[BLACK][sq] = adjacent_files_mask[f] & ahead_ranks_mask[BLACK][r];
    }
}

static Bitboard pawn_defenders(const Board* b, int color, int sq) {
    return pawn_attacks[1 - color][sq] & b->pieces[color][PAWN];
}

static const int passed_pawn_bonus[8] = {0, 5, 10, 20, 35, 60, 100, 0};

static int eval_passed_pawns(const Board* b, int color) {
    int score = 0;
    int enemy = 1 - color;
    Bitboard enemy_pawns = b->pieces[enemy][PAWN];

    Bitboard pawns = b->pieces[color][PAWN];
    while (pawns) {
        int sq = pop_lsb(&pawns);
        if (enemy_pawns & passed_pawn_mask[color][sq])
            continue;
        int r = sq / 8;
        int rank_from_own_side = (color == WHITE) ? r : 7 - r;
        score += passed_pawn_bonus[rank_from_own_side];
    }
    return score;
}

static const int mobility_weight[PIECE_TYPE_NB] = {
    [KNIGHT] = 4,
    [BISHOP] = 4,
    [ROOK] = 2,
    [QUEEN] = 1,
};

static int eval_mobility(const Board* b, int color) {
    int score = 0;
    Bitboard own = b->occupied[color];

    Bitboard knights = b->pieces[color][KNIGHT];
    while (knights) {
        int sq = pop_lsb(&knights);
        score += mobility_weight[KNIGHT] * popcount(knight_attacks[sq] & ~own);
    }

    Bitboard bishops = b->pieces[color][BISHOP];
    while (bishops) {
        int sq = pop_lsb(&bishops);
        score += mobility_weight[BISHOP] * popcount(bishop_attacks(sq, b->all) & ~own);
    }

    Bitboard rooks = b->pieces[color][ROOK];
    while (rooks) {
        int sq = pop_lsb(&rooks);
        score += mobility_weight[ROOK] * popcount(rook_attacks(sq, b->all) & ~own);
    }

    Bitboard queens = b->pieces[color][QUEEN];
    while (queens) {
        int sq = pop_lsb(&queens);
        score += mobility_weight[QUEEN] * popcount(queen_attacks(sq, b->all) & ~own);
    }

    return score;
}

#define PAWN_SHIELD_NEAR_BONUS 12
#define PAWN_SHIELD_FAR_BONUS 6

static int eval_pawn_shield(const Board* b, int color) {
    Bitboard king_bb = b->pieces[color][KING];
    if (!king_bb)
        return 0;
    int ksq = bb_lsb(king_bb);
    int kf = ksq % 8;
    int kr = ksq / 8;

    if (kf >= 2 && kf <= 5)
        return 0;

    Bitboard shield_files = file_mask[kf];
    if (kf > 0)
        shield_files |= file_mask[kf - 1];
    if (kf < 7)
        shield_files |= file_mask[kf + 1];

    Bitboard own_pawns = b->pieces[color][PAWN];
    int score = 0;

    int near_rank = (color == WHITE) ? kr + 1 : kr - 1;
    int far_rank = (color == WHITE) ? kr + 2 : kr - 2;

    if (near_rank >= 0 && near_rank < 8) {
        Bitboard near_mask = shield_files & (Bitboard)(0xFFULL << (near_rank * 8));
        score += PAWN_SHIELD_NEAR_BONUS * popcount(own_pawns & near_mask);
    }
    if (far_rank >= 0 && far_rank < 8) {
        Bitboard far_mask = shield_files & (Bitboard)(0xFFULL << (far_rank * 8));
        score += PAWN_SHIELD_FAR_BONUS * popcount(own_pawns & far_mask);
    }

    return score;
}

#define ISOLATED_PAWN_PENALTY 15
#define DOUBLED_PAWN_PENALTY 12

static int eval_pawn_structure(const Board* b, int color) {
    int score = 0;
    Bitboard pawns = b->pieces[color][PAWN];

    for (int f = 0; f < 8; f++) {
        int count = popcount(pawns & file_mask[f]);
        if (count > 1)
            score -= DOUBLED_PAWN_PENALTY * (count - 1);
    }

    Bitboard bb = pawns;
    while (bb) {
        int sq = pop_lsb(&bb);
        int f = sq % 8;
        if ((pawns & adjacent_files_mask[f]) == 0)
            score -= ISOLATED_PAWN_PENALTY;
    }

    return score;
}

#define BISHOP_PAIR_BONUS 30

static int eval_bishop_pair(const Board* b, int color) {
    return (popcount(b->pieces[color][BISHOP]) >= 2) ? BISHOP_PAIR_BONUS : 0;
}

#define ROOK_OPEN_FILE_BONUS 20
#define ROOK_SEMI_OPEN_FILE_BONUS 10

static int eval_rook_open_file(const Board* b, int color) {
    int score = 0;
    int enemy = 1 - color;
    Bitboard own_pawns = b->pieces[color][PAWN];
    Bitboard enemy_pawns = b->pieces[enemy][PAWN];

    Bitboard rooks = b->pieces[color][ROOK];
    while (rooks) {
        int sq = pop_lsb(&rooks);
        int f = sq % 8;
        int has_own_pawn = (own_pawns & file_mask[f]) != 0;
        int has_enemy_pawn = (enemy_pawns & file_mask[f]) != 0;
        if (!has_own_pawn && !has_enemy_pawn)
            score += ROOK_OPEN_FILE_BONUS;
        else if (!has_own_pawn)
            score += ROOK_SEMI_OPEN_FILE_BONUS;
    }
    return score;
}

#define KNIGHT_OUTPOST_BONUS 25

static int eval_knight_outposts(const Board* b, int color) {
    int score = 0;
    int enemy = 1 - color;
    Bitboard enemy_pawns = b->pieces[enemy][PAWN];

    Bitboard knights = b->pieces[color][KNIGHT];
    while (knights) {
        int sq = pop_lsb(&knights);
        int r = sq / 8;

        int advanced = (color == WHITE) ? (r >= 3 && r <= 6) : (r <= 4 && r >= 1);
        if (!advanced)
            continue;

        if (enemy_pawns & outpost_mask[color][sq])
            continue;

        if (!pawn_defenders(b, color, sq))
            continue;

        score += KNIGHT_OUTPOST_BONUS;
    }
    return score;
}

#define ROOK_SEVENTH_BONUS 20

static int eval_rook_seventh(const Board* b, int color) {
    int enemy = 1 - color;
    Bitboard seventh = (color == WHITE) ? RANK_7 : RANK_2;
    Bitboard back_rank = (color == WHITE) ? RANK_8 : RANK_1;

    Bitboard rooks_on_seventh = b->pieces[color][ROOK] & seventh;
    if (!rooks_on_seventh)
        return 0;

    int enemy_king_trapped = (b->pieces[enemy][KING] & back_rank) != 0;
    int enemy_pawns_on_seventh = (b->pieces[enemy][PAWN] & seventh) != 0;

    if (!enemy_king_trapped && !enemy_pawns_on_seventh)
        return 0;

    return ROOK_SEVENTH_BONUS * popcount(rooks_on_seventh);
}

static Bitboard passed_pawns_bb(const Board* b, int color) {
    Bitboard result = 0;
    int enemy = 1 - color;
    Bitboard enemy_pawns = b->pieces[enemy][PAWN];
    Bitboard pawns = b->pieces[color][PAWN];
    while (pawns) {
        int sq = pop_lsb(&pawns);
        if (!(enemy_pawns & passed_pawn_mask[color][sq]))
            result |= bb_bit(sq);
    }
    return result;
}

#define CONNECTED_PASSED_BONUS 15

static int eval_connected_passed_pawns(const Board* b, int color) {
    Bitboard passed = passed_pawns_bb(b, color);
    int score = 0;

    Bitboard bb = passed;
    while (bb) {
        int sq = pop_lsb(&bb);
        int r = sq / 8;
        int f = sq % 8;

        Bitboard neighbors = passed & adjacent_files_mask[f];
        while (neighbors) {
            int osq = pop_lsb(&neighbors);
            int neighbor_rank = osq / 8;
            if (abs(neighbor_rank - r) <= 1) {
                score += CONNECTED_PASSED_BONUS;
                break;
            }
        }
    }
    return score;
}

#define CANDIDATE_PASSED_BONUS 10

static int eval_candidate_passed_pawns(const Board* b, int color) {
    int enemy = 1 - color;
    Bitboard own_pawns = b->pieces[color][PAWN];
    Bitboard enemy_pawns = b->pieces[enemy][PAWN];
    int score = 0;

    Bitboard bb = own_pawns;
    while (bb) {
        int sq = pop_lsb(&bb);
        int r = sq / 8;
        int f = sq % 8;

        Bitboard file_ahead = file_mask[f] & ahead_ranks_mask[color][r];
        if (enemy_pawns & file_ahead)
            continue;

        if (!(enemy_pawns & passed_pawn_mask[color][sq]))
            continue;

        Bitboard enemy_opposers = enemy_pawns & adjacent_files_mask[f] & ahead_ranks_mask[color][r];
        Bitboard own_supporters = own_pawns & adjacent_files_mask[f] & ~ahead_ranks_mask[color][r];

        if (popcount(own_supporters) >= popcount(enemy_opposers))
            score += CANDIDATE_PASSED_BONUS;
    }
    return score;
}

#define BACKWARD_PAWN_PENALTY 12

static int eval_backward_pawns(const Board* b, int color) {
    int enemy = 1 - color;
    Bitboard own_pawns = b->pieces[color][PAWN];
    Bitboard enemy_pawns = b->pieces[enemy][PAWN];
    int score = 0;

    Bitboard bb = own_pawns;
    while (bb) {
        int sq = pop_lsb(&bb);
        int r = sq / 8;
        int f = sq % 8;

        Bitboard support_zone = adjacent_files_mask[f] & ~ahead_ranks_mask[color][r];
        if (own_pawns & support_zone)
            continue;

        int front_sq = (color == WHITE) ? sq + 8 : sq - 8;
        if (front_sq < 0 || front_sq >= 64)
            continue;

        if (pawn_attacks[color][front_sq] & enemy_pawns)
            score -= BACKWARD_PAWN_PENALTY;
    }
    return score;
}

#define BISHOP_OUTPOST_BONUS 15

static int eval_bishop_outposts(const Board* b, int color) {
    int score = 0;
    int enemy = 1 - color;
    Bitboard enemy_pawns = b->pieces[enemy][PAWN];

    Bitboard bishops = b->pieces[color][BISHOP];
    while (bishops) {
        int sq = pop_lsb(&bishops);
        int r = sq / 8;

        int advanced = (color == WHITE) ? (r >= 3 && r <= 6) : (r <= 4 && r >= 1);
        if (!advanced)
            continue;

        if (enemy_pawns & outpost_mask[color][sq])
            continue;

        if (!pawn_defenders(b, color, sq))
            continue;

        score += BISHOP_OUTPOST_BONUS;
    }
    return score;
}

#define SPACE_UNIT_BONUS 1
#define SPACE_CENTER_FILES (FILE_C | FILE_D | FILE_E | FILE_F)

static int eval_space(const Board* b, int color) {
    int enemy = 1 - color;
    Bitboard enemy_pawns = b->pieces[enemy][PAWN];
    Bitboard zone_ranks = (color == WHITE) ? (RANK_2 | RANK_3 | RANK_4) : (RANK_5 | RANK_6 | RANK_7);
    Bitboard zone = zone_ranks & SPACE_CENTER_FILES & ~b->all;

    int count = 0;
    Bitboard bb = zone;
    while (bb) {
        int sq = pop_lsb(&bb);
        if (!(pawn_attacks[color][sq] & enemy_pawns))
            count++;
    }
    return count * SPACE_UNIT_BONUS;
}

#define TRAPPED_BISHOP_PENALTY 40

static int eval_trapped_bishops(const Board* b, int color) {
    int enemy = 1 - color;
    Bitboard enemy_pawns = b->pieces[enemy][PAWN];
    Bitboard own = b->occupied[color];
    int score = 0;

    Bitboard bishops = b->pieces[color][BISHOP];
    while (bishops) {
        int sq = pop_lsb(&bishops);
        int r = sq / 8;

        int deep = (color == WHITE) ? (r >= 5) : (r <= 2);
        if (!deep)
            continue;

        Bitboard moves = bishop_attacks(sq, b->all) & ~own;
        int has_safe_square = 0;
        Bitboard mv = moves;
        while (mv) {
            int t = pop_lsb(&mv);
            if (!(pawn_attacks[color][t] & enemy_pawns)) {
                has_safe_square = 1;
                break;
            }
        }

        if (!has_safe_square)
            score -= TRAPPED_BISHOP_PENALTY;
    }
    return score;
}

#define KS_KNIGHT_UNIT 2
#define KS_BISHOP_UNIT 2
#define KS_ROOK_UNIT 3
#define KS_QUEEN_UNIT 5
#define KS_OPEN_FILE_UNIT 3
#define KS_SEMI_OPEN_FILE_UNIT 2
#define KS_MAX_PENALTY 500

static int king_danger_score(int units) {
    int score = (units * units) / 4;
    if (score > KS_MAX_PENALTY)
        score = KS_MAX_PENALTY;
    return score;
}

static int eval_king_safety_raw(const Board* b, int color) {
    int enemy = 1 - color;
    int ksq = king_square(b, color);
    if (ksq < 0)
        return 0;

    Bitboard zone = king_attacks[ksq];
    int units = 0;

    Bitboard knights = b->pieces[enemy][KNIGHT];
    while (knights) {
        int sq = pop_lsb(&knights);
        if (knight_attacks[sq] & zone)
            units += KS_KNIGHT_UNIT;
    }

    Bitboard bishops = b->pieces[enemy][BISHOP];
    while (bishops) {
        int sq = pop_lsb(&bishops);
        if (bishop_attacks(sq, b->all) & zone)
            units += KS_BISHOP_UNIT;
    }

    Bitboard rooks = b->pieces[enemy][ROOK];
    while (rooks) {
        int sq = pop_lsb(&rooks);
        if (rook_attacks(sq, b->all) & zone)
            units += KS_ROOK_UNIT;
    }

    Bitboard queens = b->pieces[enemy][QUEEN];
    while (queens) {
        int sq = pop_lsb(&queens);
        if (queen_attacks(sq, b->all) & zone)
            units += KS_QUEEN_UNIT;
    }

    int kf = ksq % 8;
    Bitboard enemy_rq_on_kfile = (b->pieces[enemy][ROOK] | b->pieces[enemy][QUEEN]) & file_mask[kf];
    if (enemy_rq_on_kfile) {
        int has_own_pawn = (b->pieces[color][PAWN] & file_mask[kf]) != 0;
        int has_enemy_pawn = (b->pieces[enemy][PAWN] & file_mask[kf]) != 0;
        if (!has_own_pawn && !has_enemy_pawn)
            units += KS_OPEN_FILE_UNIT;
        else if (!has_own_pawn)
            units += KS_SEMI_OPEN_FILE_UNIT;
    }

    if (units == 0)
        return 0;
    return king_danger_score(units);
}

static int eval_king_safety(const Board* b, int color, int phase, int max_phase) {
    int raw = eval_king_safety_raw(b, color);
    if (raw == 0 || max_phase <= 0)
        return raw;
    return (raw * phase) / max_phase;
}

int eval_extra(const Board* b, int phase, int max_phase) {
    if (!tables_ready)
        eval_init_tables();

    int score = 0;
    for (int color = WHITE; color <= BLACK; color++) {
        int sign = (color == WHITE) ? 1 : -1;

        score += sign * eval_passed_pawns(b, color);
        score += sign * eval_mobility(b, color);
        score += sign * eval_pawn_shield(b, color);
        score += sign * eval_pawn_structure(b, color);
        score += sign * eval_bishop_pair(b, color);
        score += sign * eval_rook_open_file(b, color);
        score += sign * eval_knight_outposts(b, color);

        score += sign * eval_rook_seventh(b, color);
        score += sign * eval_connected_passed_pawns(b, color);
        score += sign * eval_candidate_passed_pawns(b, color);
        score += sign * eval_backward_pawns(b, color);
        score += sign * eval_bishop_outposts(b, color);
        score += sign * eval_space(b, color);
        score += sign * eval_trapped_bishops(b, color);

        score -= sign * eval_king_safety(b, color, phase, max_phase);
    }

    return score;
}
