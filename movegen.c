#include "board.h"

#define SQ(r, f) (((r) * 8) + (f))

int king_square(const Board* b, int color) {
    Bitboard kb = b->pieces[color][KING];
    if (kb == 0)
        return -1;
    return bb_lsb(kb);
}

int is_attacked(const Board* b, int sq, int by_color) {
    if (pawn_attacks[1 - by_color][sq] & b->pieces[by_color][PAWN])
        return 1;
    if (knight_attacks[sq] & b->pieces[by_color][KNIGHT])
        return 1;
    if (king_attacks[sq] & b->pieces[by_color][KING])
        return 1;

    Bitboard bishops_queens = b->pieces[by_color][BISHOP] | b->pieces[by_color][QUEEN];
    if (bishop_attacks(sq, b->all) & bishops_queens)
        return 1;

    Bitboard rooks_queens = b->pieces[by_color][ROOK] | b->pieces[by_color][QUEEN];
    if (rook_attacks(sq, b->all) & rooks_queens)
        return 1;

    return 0;
}

int is_in_check(const Board* b, int color) {
    int ks = king_square(b, color);
    if (ks == -1)
        return 0;
    return is_attacked(b, ks, 1 - color);
}

static int add_move(Move* list, int n, int from, int to, int promo) {
    list[n].from = from;
    list[n].to = to;
    list[n].promo = promo;
    return n + 1;
}

static int add_pawn_move(Move* list, int n, int from, int to, int promo_rank_hit) {
    if (promo_rank_hit) {
        n = add_move(list, n, from, to, WQ);
        n = add_move(list, n, from, to, WR);
        n = add_move(list, n, from, to, WB);
        n = add_move(list, n, from, to, WN);
    } else {
        n = add_move(list, n, from, to, 0);
    }
    return n;
}

static int gen_pawn_moves(Board* b, Move* list, int n) {
    int us = b->side;
    int them = 1 - us;
    Bitboard pawns = b->pieces[us][PAWN];
    Bitboard empty = ~b->all;
    Bitboard enemy = b->occupied[them];

    if (us == WHITE) {
        Bitboard single = (pawns << 8) & empty;
        Bitboard doubleStart = single & RANK_3;
        Bitboard dbl = (doubleStart << 8) & empty;
        Bitboard capL = (pawns & ~FILE_A) << 7 & enemy;
        Bitboard capR = (pawns & ~FILE_H) << 9 & enemy;

        Bitboard bb = single;
        while (bb) {
            int to = pop_lsb(&bb);
            n = add_pawn_move(list, n, to - 8, to, (to >= 56));
        }
        bb = dbl;
        while (bb) {
            int to = pop_lsb(&bb);
            n = add_move(list, n, to - 16, to, 0);
        }
        bb = capL;
        while (bb) {
            int to = pop_lsb(&bb);
            n = add_pawn_move(list, n, to - 7, to, (to >= 56));
        }
        bb = capR;
        while (bb) {
            int to = pop_lsb(&bb);
            n = add_pawn_move(list, n, to - 9, to, (to >= 56));
        }
    } else {
        Bitboard single = (pawns >> 8) & empty;
        Bitboard doubleStart = single & RANK_6;
        Bitboard dbl = (doubleStart >> 8) & empty;
        Bitboard capL = (pawns & ~FILE_A) >> 9 & enemy;
        Bitboard capR = (pawns & ~FILE_H) >> 7 & enemy;

        Bitboard bb = single;
        while (bb) {
            int to = pop_lsb(&bb);
            n = add_pawn_move(list, n, to + 8, to, (to < 8));
        }
        bb = dbl;
        while (bb) {
            int to = pop_lsb(&bb);
            n = add_move(list, n, to + 16, to, 0);
        }
        bb = capL;
        while (bb) {
            int to = pop_lsb(&bb);
            n = add_pawn_move(list, n, to + 9, to, (to < 8));
        }
        bb = capR;
        while (bb) {
            int to = pop_lsb(&bb);
            n = add_pawn_move(list, n, to + 7, to, (to < 8));
        }
    }

    if (b->enpas != -1) {
        Bitboard attackers = pawn_attacks[them][b->enpas] & pawns;
        while (attackers) {
            int from = pop_lsb(&attackers);
            n = add_move(list, n, from, b->enpas, 0);
        }
    }

    return n;
}

static int gen_knight_moves(Board* b, Move* list, int n) {
    int us = b->side;
    Bitboard bb = b->pieces[us][KNIGHT];
    while (bb) {
        int from = pop_lsb(&bb);
        Bitboard targets = knight_attacks[from] & ~b->occupied[us];
        while (targets) {
            int to = pop_lsb(&targets);
            n = add_move(list, n, from, to, 0);
        }
    }
    return n;
}

static int gen_sliding_moves(Board* b, Move* list, int n, int piece_type, int is_bishop_like, int is_rook_like) {
    int us = b->side;
    Bitboard bb = b->pieces[us][piece_type];
    while (bb) {
        int from = pop_lsb(&bb);
        Bitboard attacks = 0;
        if (is_bishop_like)
            attacks |= bishop_attacks(from, b->all);
        if (is_rook_like)
            attacks |= rook_attacks(from, b->all);
        Bitboard targets = attacks & ~b->occupied[us];
        while (targets) {
            int to = pop_lsb(&targets);
            n = add_move(list, n, from, to, 0);
        }
    }
    return n;
}

static int gen_king_moves(Board* b, Move* list, int n) {
    int us = b->side;
    int them = 1 - us;
    Bitboard bb = b->pieces[us][KING];
    if (bb == 0)
        return n;
    int from = bb_lsb(bb);

    Bitboard targets = king_attacks[from] & ~b->occupied[us];
    while (targets) {
        int to = pop_lsb(&targets);
        n = add_move(list, n, from, to, 0);
    }

    if (us == WHITE) {
        if ((b->castle & WK_CASTLE) && !(b->all & (bb_bit(5) | bb_bit(6))) && !is_in_check(b, WHITE) &&
            !is_attacked(b, 5, them) && !is_attacked(b, 6, them))
            n = add_move(list, n, 4, 6, 0);
        if ((b->castle & WQ_CASTLE) && !(b->all & (bb_bit(1) | bb_bit(2) | bb_bit(3))) && !is_in_check(b, WHITE) &&
            !is_attacked(b, 3, them) && !is_attacked(b, 2, them))
            n = add_move(list, n, 4, 2, 0);
    } else {
        if ((b->castle & BK_CASTLE) && !(b->all & (bb_bit(61) | bb_bit(62))) && !is_in_check(b, BLACK) &&
            !is_attacked(b, 61, them) && !is_attacked(b, 62, them))
            n = add_move(list, n, 60, 62, 0);
        if ((b->castle & BQ_CASTLE) && !(b->all & (bb_bit(57) | bb_bit(58) | bb_bit(59))) && !is_in_check(b, BLACK) &&
            !is_attacked(b, 59, them) && !is_attacked(b, 58, them))
            n = add_move(list, n, 60, 58, 0);
    }

    return n;
}

int gen_moves(Board* b, Move* list) {
    int n = 0;
    n = gen_pawn_moves(b, list, n);
    n = gen_knight_moves(b, list, n);
    n = gen_sliding_moves(b, list, n, BISHOP, 1, 0);
    n = gen_sliding_moves(b, list, n, ROOK, 0, 1);
    n = gen_sliding_moves(b, list, n, QUEEN, 1, 1);
    n = gen_king_moves(b, list, n);
    return n;
}

int gen_captures(Board* b, Move* list) {
    Move all[256];
    int n_all = gen_moves(b, all);
    int n = 0;
    for (int i = 0; i < n_all; i++) {
        int to = all[i].to;
        if ((b->all & bb_bit(to)) || to == b->enpas || all[i].promo)
            list[n++] = all[i];
    }
    return n;
}

#undef SQ
