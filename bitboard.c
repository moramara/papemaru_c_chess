#include "bitboard.h"

Bitboard knight_attacks[64];
Bitboard king_attacks[64];
Bitboard pawn_attacks[2][64];

static int tables_initialized = 0;

static int on_board(int r, int f) {
    return r >= 0 && r < 8 && f >= 0 && f < 8;
}

void bitboard_init_tables(void) {
    if (tables_initialized)
        return;
    tables_initialized = 1;

    static const int knight_deltas[8][2] = {
        {-2, -1},
        {-2, 1},
        {-1, -2},
        {-1, 2},
        {1, -2},
        {1, 2},
        {2, -1},
        {2, 1},
    };
    static const int king_deltas[8][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
        {1, 1},
        {1, -1},
        {-1, 1},
        {-1, -1},
    };

    for (int sq = 0; sq < 64; sq++) {
        int r = sq / 8;
        int f = sq % 8;

        Bitboard n = 0;
        for (int i = 0; i < 8; i++) {
            int nr = r + knight_deltas[i][0];
            int nf = f + knight_deltas[i][1];
            if (on_board(nr, nf))
                n |= bb_bit(nr * 8 + nf);
        }
        knight_attacks[sq] = n;

        Bitboard k = 0;
        for (int i = 0; i < 8; i++) {
            int nr = r + king_deltas[i][0];
            int nf = f + king_deltas[i][1];
            if (on_board(nr, nf))
                k |= bb_bit(nr * 8 + nf);
        }
        king_attacks[sq] = k;

        Bitboard wp = 0;
        if (on_board(r + 1, f - 1))
            wp |= bb_bit((r + 1) * 8 + (f - 1));
        if (on_board(r + 1, f + 1))
            wp |= bb_bit((r + 1) * 8 + (f + 1));
        pawn_attacks[0][sq] = wp;

        Bitboard bp = 0;
        if (on_board(r - 1, f - 1))
            bp |= bb_bit((r - 1) * 8 + (f - 1));
        if (on_board(r - 1, f + 1))
            bp |= bb_bit((r - 1) * 8 + (f + 1));
        pawn_attacks[1][sq] = bp;
    }
}

static Bitboard sliding_attacks(int sq, Bitboard occupied, const int deltas[4][2]) {
    Bitboard attacks = 0;
    int r0 = sq / 8;
    int f0 = sq % 8;

    for (int d = 0; d < 4; d++) {
        int r = r0 + deltas[d][0];
        int f = f0 + deltas[d][1];
        while (on_board(r, f)) {
            int s = r * 8 + f;
            attacks |= bb_bit(s);
            if (occupied & bb_bit(s))
                break;
            r += deltas[d][0];
            f += deltas[d][1];
        }
    }
    return attacks;
}

Bitboard bishop_attacks(int sq, Bitboard occupied) {
    static const int deltas[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    return sliding_attacks(sq, occupied, deltas);
}

Bitboard rook_attacks(int sq, Bitboard occupied) {
    static const int deltas[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    return sliding_attacks(sq, occupied, deltas);
}
