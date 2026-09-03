#include "eval.h"

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

int eval_extra(const Board* b) {
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
    }
    return score;
}
