#include "perft.h"

long long perft(Board* b, int depth) {
    if (depth == 0)
        return 1;
    Move list[256];
    int n = gen_moves(b, list);
    long long nodes = 0;
    for (int i = 0; i < n; i++) {
        Undo u;
        if (!make_move(b, list[i], &u))
            continue;
        nodes += perft(b, depth - 1);
        unmake_move(b, list[i], &u);
    }
    return nodes;
}

long long perft_divide(Board* b, int depth, void (*report)(const char* move_str, long long nodes)) {
    if (depth <= 0)
        return perft(b, depth);
    Move list[256];
    int n = gen_moves(b, list);
    long long total = 0;
    for (int i = 0; i < n; i++) {
        Undo u;
        if (!make_move(b, list[i], &u))
            continue;
        long long nodes = perft(b, depth - 1);
        unmake_move(b, list[i], &u);
        total += nodes;
        if (report) {
            char buf[8];
            buf[0] = 'a' + (list[i].from % 8);
            buf[1] = '1' + (list[i].from / 8);
            buf[2] = 'a' + (list[i].to % 8);
            buf[3] = '1' + (list[i].to / 8);
            int len = 4;
            if (list[i].promo) {
                char c = 'q';
                if (list[i].promo == WN)
                    c = 'n';
                else if (list[i].promo == WB)
                    c = 'b';
                else if (list[i].promo == WR)
                    c = 'r';
                buf[len++] = c;
            }
            buf[len] = '\0';
            report(buf, nodes);
        }
    }
    return total;
}
