#include "board.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "eval.h"
#include "zobrist.h"

#define SQ(r, f) (((r) * 8) + (f))

static void board_refresh_occupancy(Board* b) {
    Bitboard w = 0, bl = 0;
    for (int pt = 0; pt < PIECE_TYPE_NB; pt++) {
        w |= b->pieces[WHITE][pt];
        bl |= b->pieces[BLACK][pt];
    }
    b->occupied[WHITE] = w;
    b->occupied[BLACK] = bl;
    b->all = w | bl;
}

void board_assert_consistent(const Board* b) {
#ifndef NDEBUG
    Bitboard w = 0, bl = 0;
    for (int pt = 0; pt < PIECE_TYPE_NB; pt++) {
        assert((b->pieces[WHITE][pt] & b->pieces[BLACK][pt]) == 0);
        w |= b->pieces[WHITE][pt];
        bl |= b->pieces[BLACK][pt];
    }
    assert((w & bl) == 0);
    assert(w == b->occupied[WHITE]);
    assert(bl == b->occupied[BLACK]);
    assert((w | bl) == b->all);
    assert(b->hash == board_compute_hash(b));
#else
    (void)b;
#endif
}

int piece_at(const Board* b, int sq) {
    Bitboard m = bb_bit(sq);
    if (b->occupied[WHITE] & m) {
        for (int pt = 0; pt < PIECE_TYPE_NB; pt++)
            if (b->pieces[WHITE][pt] & m)
                return pt + 1;
    } else if (b->occupied[BLACK] & m) {
        for (int pt = 0; pt < PIECE_TYPE_NB; pt++)
            if (b->pieces[BLACK][pt] & m)
                return -(pt + 1);
    }
    return EMPTY;
}

static int piece_type_at(const Board* b, int color, int sq) {
    Bitboard m = bb_bit(sq);
    for (int pt = 0; pt < PIECE_TYPE_NB; pt++)
        if (b->pieces[color][pt] & m)
            return pt;
    return -1;
}

uint64_t board_compute_hash(const Board* b) {
    uint64_t h = 0;
    for (int color = WHITE; color <= BLACK; color++) {
        for (int pt = 0; pt < PIECE_TYPE_NB; pt++) {
            Bitboard bb = b->pieces[color][pt];
            while (bb) {
                int sq = pop_lsb(&bb);
                h ^= zobrist_piece[color][pt][sq];
            }
        }
    }
    if (b->side == BLACK)
        h ^= zobrist_side;
    h ^= zobrist_castle[b->castle & 0xF];
    if (b->enpas != -1)
        h ^= zobrist_ep_file[b->enpas % 8];
    return h;
}

void board_init(Board* b) {
    bitboard_init_tables();
    zobrist_init_tables();
    eval_init_tables();
    memset(b->pieces, 0, sizeof(b->pieces));
    b->occupied[WHITE] = 0;
    b->occupied[BLACK] = 0;
    b->all = 0;
    b->side = WHITE;
    b->enpas = -1;
    b->castle = 0;
    b->hash = 0;
}

void board_startpos(Board* b) {
    board_init(b);

    b->pieces[WHITE][ROOK] = bb_bit(0) | bb_bit(7);
    b->pieces[WHITE][KNIGHT] = bb_bit(1) | bb_bit(6);
    b->pieces[WHITE][BISHOP] = bb_bit(2) | bb_bit(5);
    b->pieces[WHITE][QUEEN] = bb_bit(3);
    b->pieces[WHITE][KING] = bb_bit(4);
    b->pieces[WHITE][PAWN] = RANK_2;

    b->pieces[BLACK][ROOK] = bb_bit(56) | bb_bit(63);
    b->pieces[BLACK][KNIGHT] = bb_bit(57) | bb_bit(62);
    b->pieces[BLACK][BISHOP] = bb_bit(58) | bb_bit(61);
    b->pieces[BLACK][QUEEN] = bb_bit(59);
    b->pieces[BLACK][KING] = bb_bit(60);
    b->pieces[BLACK][PAWN] = RANK_7;

    board_refresh_occupancy(b);

    b->castle = WK_CASTLE | WQ_CASTLE | BK_CASTLE | BQ_CASTLE;
    b->side = WHITE;
    b->enpas = -1;

    b->hash = board_compute_hash(b);
}

static void apply_move_bits(Board* b, Move m, Undo* u) {
    int us = b->side;
    int them = 1 - us;
    int from = m.from;
    int to = m.to;
    int mt = piece_type_at(b, us, from);

    u->prev_castle = b->castle;
    u->prev_enpas = b->enpas;
    u->prev_hash = b->hash;
    u->captured_type = -1;
    u->captured_sq = to;

    uint64_t hash = b->hash;
    hash ^= zobrist_piece[us][mt][from];

    int is_ep = (mt == PAWN) && (to == b->enpas);
    if (is_ep) {
        int cap_sq = (us == WHITE) ? to - 8 : to + 8;
        u->captured_sq = cap_sq;
        u->captured_type = PAWN;
        b->pieces[them][PAWN] &= ~bb_bit(cap_sq);
        hash ^= zobrist_piece[them][PAWN][cap_sq];
    } else {
        int ct = piece_type_at(b, them, to);
        if (ct != -1) {
            u->captured_type = ct;
            b->pieces[them][ct] &= ~bb_bit(to);
            hash ^= zobrist_piece[them][ct][to];
        }
    }

    b->pieces[us][mt] &= ~bb_bit(from);
    if (m.promo) {
        int promo_type = m.promo - 1;
        b->pieces[us][promo_type] |= bb_bit(to);
        hash ^= zobrist_piece[us][promo_type][to];
    } else {
        b->pieces[us][mt] |= bb_bit(to);
        hash ^= zobrist_piece[us][mt][to];
    }

    if (mt == KING && (to - from == 2 || from - to == 2)) {
        if (to > from) {
            int rf = (us == WHITE) ? 7 : 63;
            int rt = (us == WHITE) ? 5 : 61;
            b->pieces[us][ROOK] &= ~bb_bit(rf);
            b->pieces[us][ROOK] |= bb_bit(rt);
            hash ^= zobrist_piece[us][ROOK][rf];
            hash ^= zobrist_piece[us][ROOK][rt];
        } else {
            int rf = (us == WHITE) ? 0 : 56;
            int rt = (us == WHITE) ? 3 : 59;
            b->pieces[us][ROOK] &= ~bb_bit(rf);
            b->pieces[us][ROOK] |= bb_bit(rt);
            hash ^= zobrist_piece[us][ROOK][rf];
            hash ^= zobrist_piece[us][ROOK][rt];
        }
    }

    if (b->enpas != -1)
        hash ^= zobrist_ep_file[b->enpas % 8];

    b->enpas = -1;
    if (mt == PAWN && (to - from == 16 || from - to == 16)) {
        b->enpas = (from + to) / 2;
        hash ^= zobrist_ep_file[b->enpas % 8];
    }

    int old_castle = b->castle;
    if (mt == KING) {
        if (us == WHITE)
            b->castle &= ~(WK_CASTLE | WQ_CASTLE);
        else
            b->castle &= ~(BK_CASTLE | BQ_CASTLE);
    }
    if (from == 0 || to == 0)
        b->castle &= ~WQ_CASTLE;
    if (from == 7 || to == 7)
        b->castle &= ~WK_CASTLE;
    if (from == 56 || to == 56)
        b->castle &= ~BQ_CASTLE;
    if (from == 63 || to == 63)
        b->castle &= ~BK_CASTLE;
    if (b->castle != old_castle) {
        hash ^= zobrist_castle[old_castle];
        hash ^= zobrist_castle[b->castle];
    }

    hash ^= zobrist_side;
    b->hash = hash;

    board_refresh_occupancy(b);
}

static void revert_move_bits(Board* b, int mover, Move m, const Undo* u) {
    int us = mover;
    int them = 1 - us;
    int from = m.from;
    int to = m.to;

    if (m.promo) {
        int promo_type = m.promo - 1;
        b->pieces[us][promo_type] &= ~bb_bit(to);
        b->pieces[us][PAWN] |= bb_bit(from);
    } else {
        int mt = piece_type_at(b, us, to);
        b->pieces[us][mt] &= ~bb_bit(to);
        b->pieces[us][mt] |= bb_bit(from);
    }

    if (!m.promo && (to - from == 2 || from - to == 2)) {
        int mt_at_to = piece_type_at(b, us, from);
        if (mt_at_to == KING) {
            if (to > from) {
                int rf = (us == WHITE) ? 7 : 63;
                int rt = (us == WHITE) ? 5 : 61;
                b->pieces[us][ROOK] &= ~bb_bit(rt);
                b->pieces[us][ROOK] |= bb_bit(rf);
            } else {
                int rf = (us == WHITE) ? 0 : 56;
                int rt = (us == WHITE) ? 3 : 59;
                b->pieces[us][ROOK] &= ~bb_bit(rt);
                b->pieces[us][ROOK] |= bb_bit(rf);
            }
        }
    }

    if (u->captured_type != -1) {
        b->pieces[them][u->captured_type] |= bb_bit(u->captured_sq);
    }

    b->castle = u->prev_castle;
    b->enpas = u->prev_enpas;
    b->hash = u->prev_hash;

    board_refresh_occupancy(b);
}

int make_move(Board* b, Move m, Undo* u) {
    int us = b->side;
    int them = 1 - us;

    apply_move_bits(b, m, u);

    int ks = king_square(b, us);
    if (ks != -1 && is_attacked(b, ks, them)) {
        revert_move_bits(b, us, m, u);
        return 0;
    }

    b->side = them;
    board_assert_consistent(b);
    return 1;
}

void unmake_move(Board* b, Move m, const Undo* u) {
    int us = 1 - b->side;
    revert_move_bits(b, us, m, u);
    b->side = us;
    board_assert_consistent(b);
}

void make_null_move(Board* b, Undo* u) {
    u->captured_type = -1;
    u->captured_sq = -1;
    u->prev_castle = b->castle;
    u->prev_enpas = b->enpas;
    u->prev_hash = b->hash;

    uint64_t hash = b->hash;
    if (b->enpas != -1)
        hash ^= zobrist_ep_file[b->enpas % 8];
    hash ^= zobrist_side;

    b->enpas = -1;
    b->side = 1 - b->side;
    b->hash = hash;
}

void unmake_null_move(Board* b, const Undo* u) {
    b->side = 1 - b->side;
    b->enpas = u->prev_enpas;
    b->castle = u->prev_castle;
    b->hash = u->prev_hash;
}

int board_set_fen(Board* b, const char* fen) {
    board_init(b);
    int r = 7;
    int f = 0;
    const char* p = fen;
    while (*p && *p != ' ') {
        if (*p == '/') {
            r--;
            f = 0;
        } else if (*p >= '1' && *p <= '8') {
            f += *p - '0';
        } else {
            int color = -1;
            int type = -1;
            switch (*p) {
                case 'P':
                    color = WHITE;
                    type = PAWN;
                    break;
                case 'N':
                    color = WHITE;
                    type = KNIGHT;
                    break;
                case 'B':
                    color = WHITE;
                    type = BISHOP;
                    break;
                case 'R':
                    color = WHITE;
                    type = ROOK;
                    break;
                case 'Q':
                    color = WHITE;
                    type = QUEEN;
                    break;
                case 'K':
                    color = WHITE;
                    type = KING;
                    break;
                case 'p':
                    color = BLACK;
                    type = PAWN;
                    break;
                case 'n':
                    color = BLACK;
                    type = KNIGHT;
                    break;
                case 'b':
                    color = BLACK;
                    type = BISHOP;
                    break;
                case 'r':
                    color = BLACK;
                    type = ROOK;
                    break;
                case 'q':
                    color = BLACK;
                    type = QUEEN;
                    break;
                case 'k':
                    color = BLACK;
                    type = KING;
                    break;
                default:
                    return 0;
            }
            if (r >= 0 && r < 8 && f >= 0 && f < 8)
                b->pieces[color][type] |= bb_bit(SQ(r, f));
            f++;
        }
        p++;
    }
    board_refresh_occupancy(b);

    while (*p == ' ')
        p++;
    b->side = (*p == 'b') ? BLACK : WHITE;
    p++;
    while (*p == ' ')
        p++;
    b->castle = 0;
    if (*p != '-') {
        while (*p && *p != ' ') {
            if (*p == 'K')
                b->castle |= WK_CASTLE;
            if (*p == 'Q')
                b->castle |= WQ_CASTLE;
            if (*p == 'k')
                b->castle |= BK_CASTLE;
            if (*p == 'q')
                b->castle |= BQ_CASTLE;
            p++;
        }
    } else
        p++;
    while (*p == ' ')
        p++;
    b->enpas = -1;
    if (*p != '-' && p[0] >= 'a' && p[0] <= 'h' && p[1] >= '1' && p[1] <= '8') {
        b->enpas = SQ(p[1] - '1', p[0] - 'a');
    }

    b->hash = board_compute_hash(b);

    board_assert_consistent(b);
    return 1;
}

void board_print(Board* b) {
    for (int r = 7; r >= 0; r--) {
        printf("%d ", r + 1);
        for (int f = 0; f < 8; f++) {
            int p = piece_at(b, SQ(r, f));
            char c = '.';
            switch (p) {
                case WP:
                    c = 'P';
                    break;
                case WN:
                    c = 'N';
                    break;
                case WB:
                    c = 'B';
                    break;
                case WR:
                    c = 'R';
                    break;
                case WQ:
                    c = 'Q';
                    break;
                case WK:
                    c = 'K';
                    break;
                case BP:
                    c = 'p';
                    break;
                case BN:
                    c = 'n';
                    break;
                case BB:
                    c = 'b';
                    break;
                case BR:
                    c = 'r';
                    break;
                case BQ:
                    c = 'q';
                    break;
                case BK:
                    c = 'k';
                    break;
                default:
                    c = '.';
                    break;
            }
            printf("%c ", c);
        }
        printf("\n");
    }
    printf(" a b c d e f g h\n");
    printf("side:%s castle:%d enpas:%d\n", b->side == WHITE ? "w" : "b", b->castle, b->enpas);
}
