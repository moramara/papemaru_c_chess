#ifndef BITBOARD_H__
#define BITBOARD_H__

#include <stdint.h>

typedef uint64_t Bitboard;

#define FILE_A 0x0101010101010101ULL
#define FILE_B 0x0202020202020202ULL
#define FILE_C 0x0404040404040404ULL
#define FILE_D 0x0808080808080808ULL
#define FILE_E 0x1010101010101010ULL
#define FILE_F 0x2020202020202020ULL
#define FILE_G 0x4040404040404040ULL
#define FILE_H 0x8080808080808080ULL

#define RANK_1 0x00000000000000FFULL
#define RANK_2 0x000000000000FF00ULL
#define RANK_3 0x0000000000FF0000ULL
#define RANK_4 0x00000000FF000000ULL
#define RANK_5 0x000000FF00000000ULL
#define RANK_6 0x0000FF0000000000ULL
#define RANK_7 0x00FF000000000000ULL
#define RANK_8 0xFF00000000000000ULL

enum {
    PAWN = 0,
    KNIGHT = 1,
    BISHOP = 2,
    ROOK = 3,
    QUEEN = 4,
    KING = 5,
    PIECE_TYPE_NB = 6,
};

static inline Bitboard bb_bit(int sq) {
    return (Bitboard)1ULL << sq;
}

static inline int pop_lsb(Bitboard* bb) {
    int sq = __builtin_ctzll(*bb);
    *bb &= *bb - 1;
    return sq;
}

static inline int bb_lsb(Bitboard bb) {
    return __builtin_ctzll(bb);
}

static inline int popcount(Bitboard bb) {
    return __builtin_popcountll(bb);
}

static inline int more_than_one(Bitboard bb) {
    return (bb & (bb - 1)) != 0;
}

extern Bitboard knight_attacks[64];
extern Bitboard king_attacks[64];
extern Bitboard pawn_attacks[2][64];

void bitboard_init_tables(void);

Bitboard bishop_attacks(int sq, Bitboard occupied);
Bitboard rook_attacks(int sq, Bitboard occupied);
static inline Bitboard queen_attacks(int sq, Bitboard occupied) {
    return bishop_attacks(sq, occupied) | rook_attacks(sq, occupied);
}

#endif  // BITBOARD_H__
