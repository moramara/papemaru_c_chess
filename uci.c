#include "uci.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "perft.h"
#include "search.h"
#include "tt.h"

#define TT_DEFAULT_MB 32
#define TT_MIN_MB 1
#define TT_MAX_MB 1024

volatile int stop_flag = 0;

static pthread_t search_thread;
static volatile int search_active = 0;

typedef struct {
    Board board;
    int depth;
    int movetime_ms;
} SearchJob;

static SearchJob g_search_job;

static void move_to_str(Move m, char* buf);

static void* search_thread_main(void* arg) {
    (void)arg;
    Move best = search_root(&g_search_job.board, g_search_job.depth, g_search_job.movetime_ms);
    char best_str[8];
    move_to_str(best, best_str);
    printf("bestmove %s\n", best_str);
    fflush(stdout);
    search_active = 0;
    return NULL;
}

static void stop_and_join_search(void) {
    if (search_active) {
        stop_flag = 1;
        pthread_join(search_thread, NULL);
        search_active = 0;
    }
}

static Move parse_uci_move(const char* s) {
    Move m;
    m.from = 0;
    m.to = 0;
    m.promo = 0;
    if (strlen(s) < 4)
        return m;
    int ff = s[0] - 'a';
    int fr = s[1] - '1';
    int tf = s[2] - 'a';
    int tr = s[3] - '1';
    m.from = fr * 8 + ff;
    m.to = tr * 8 + tf;
    if (s[4]) {
        if (s[4] == 'q')
            m.promo = WQ;
        else if (s[4] == 'r')
            m.promo = WR;
        else if (s[4] == 'b')
            m.promo = WB;
        else if (s[4] == 'n')
            m.promo = WN;
    }
    return m;
}

static void move_to_str(Move m, char* buf) {
    if (m.from == 0 && m.to == 0) {
        strcpy(buf, "0000");
        return;
    }
    buf[0] = 'a' + (m.from % 8);
    buf[1] = '1' + (m.from / 8);
    buf[2] = 'a' + (m.to % 8);
    buf[3] = '1' + (m.to / 8);
    int len = 4;
    if (m.promo) {
        char c = 'q';
        if (m.promo == WN)
            c = 'n';
        else if (m.promo == WB)
            c = 'b';
        else if (m.promo == WR)
            c = 'r';
        buf[len++] = c;
    }
    buf[len] = '\0';
}

static void uci_parse_position(char* line, Board* b) {
    char* ptr;
    if ((ptr = strstr(line, "startpos"))) {
        board_startpos(b);
        ptr = strstr(line, "moves");
    } else if ((ptr = strstr(line, "fen"))) {
        char* fen_start = ptr + 4;
        char* moves_ptr = strstr(line, " moves");
        char fen_str[512];
        if (moves_ptr) {
            size_t len = moves_ptr - fen_start;
            strncpy(fen_str, fen_start, len);
            fen_str[len] = '\0';
        } else {
            strncpy(fen_str, fen_start, 511);
            fen_str[511] = '\0';
        }
        fen_str[strcspn(fen_str, "\r\n")] = '\0';
        board_set_fen(b, fen_str);
        ptr = moves_ptr;
    } else
        return;

    if (ptr && (ptr = strstr(ptr, "moves"))) {
        ptr += 5;
        while (*ptr) {
            while (*ptr == ' ')
                ptr++;
            if (*ptr == '\0' || *ptr == '\n')
                break;
            char ms[8];
            int i = 0;
            while (*ptr && *ptr != ' ' && *ptr != '\n' && i < 5)
                ms[i++] = *ptr++;
            ms[i] = '\0';
            if (strlen(ms) < 4)
                continue;
            Move m = parse_uci_move(ms);
            Undo u;
            make_move(b, m, &u);
        }
    }
}

static void print_divide_line(const char* move_str, long long nodes) {
    printf("%s: %lld\n", move_str, nodes);
}

static void uci_parse_go(char* line, Board* b) {
    if (strstr(line, "divide")) {
        int d = 1;
        if (sscanf(line, "go perft divide %d", &d) != 1)
            if (sscanf(line, "perft divide %d", &d) != 1)
                if (sscanf(line, "divide %d", &d) != 1)
                    d = 1;
        if (d <= 0)
            d = 1;
        long long total = perft_divide(b, d, print_divide_line);
        printf("info string perft divide %d total = %lld\n", d, total);
        fflush(stdout);
        return;
    }
    if (strstr(line, "perft")) {
        int d = 5;
        sscanf(line, "go perft %d", &d);
        if (d <= 0)
            sscanf(line, "perft %d", &d);
        if (d <= 0)
            d = 5;
        for (int i = 1; i <= d; i++) {
            long long nodes = perft(b, i);
            printf("info string perft %d = %lld\n", i, nodes);
        }
        fflush(stdout);
        return;
    }

    int depth = 4;
    int depth_specified = 0;
    int movetime_ms = 0;
    char buf[8192];
    strcpy(buf, line);
    char* tok = strtok(buf, " \n");
    while (tok) {
        if (strcmp(tok, "depth") == 0) {
            tok = strtok(NULL, " \n");
            if (tok) {
                depth = atoi(tok);
                depth_specified = 1;
            }
        } else if (strcmp(tok, "movetime") == 0) {
            tok = strtok(NULL, " \n");
            if (tok)
                movetime_ms = atoi(tok);
        }
        tok = strtok(NULL, " \n");
    }

    if (movetime_ms > 0 && !depth_specified)
        depth = 64;

    stop_and_join_search();

    g_search_job.board = *b;
    g_search_job.depth = depth;
    g_search_job.movetime_ms = movetime_ms;

    stop_flag = 0;
    search_active = 1;
    if (pthread_create(&search_thread, NULL, search_thread_main, NULL) != 0) {
        search_active = 0;
        Move best = search_root(&g_search_job.board, depth, movetime_ms);
        char best_str[8];
        move_to_str(best, best_str);
        printf("bestmove %s\n", best_str);
        fflush(stdout);
    }
}

static void uci_parse_setoption(char* line) {
    char name[64] = {0};
    char value[64] = {0};
    if (sscanf(line, "setoption name %63s value %63s", name, value) != 2)
        return;

    if (strcmp(name, "Hash") == 0) {
        int mb = atoi(value);
        if (mb < TT_MIN_MB)
            mb = TT_MIN_MB;
        if (mb > TT_MAX_MB)
            mb = TT_MAX_MB;
        stop_and_join_search();
        tt_init((size_t)mb);
    }
}

void uci_loop(void) {
    Board board;
    board_startpos(&board);
    tt_init(TT_DEFAULT_MB);
    char line[8192];

    while (1) {
        if (!fgets(line, sizeof(line), stdin))
            continue;
        if (line[0] == '\n')
            continue;
        if (strncmp(line, "ucinewgame", 10) == 0) {
            stop_and_join_search();
            board_startpos(&board);
            tt_clear();
        } else if (strncmp(line, "uci", 3) == 0) {
            printf("id name PapemaruCChess_v20260902\n");
            printf("id author Papemaru\n");
            printf("option name Hash type spin default %d min %d max %d\n", TT_DEFAULT_MB, TT_MIN_MB, TT_MAX_MB);
            printf("uciok\n");
        } else if (strncmp(line, "isready", 7) == 0) {
            printf("readyok\n");
        } else if (strncmp(line, "setoption", 9) == 0) {
            uci_parse_setoption(line);
        } else if (strncmp(line, "position", 8) == 0) {
            uci_parse_position(line, &board);
        } else if (strncmp(line, "go", 2) == 0) {
            uci_parse_go(line, &board);
        } else if (strncmp(line, "stop", 4) == 0) {
            stop_and_join_search();
        } else if (strncmp(line, "perft", 5) == 0) {
            uci_parse_go(line, &board);
        } else if (strncmp(line, "divide", 6) == 0) {
            uci_parse_go(line, &board);
        } else if (strncmp(line, "d", 1) == 0) {
            board_print(&board);
        } else if (strncmp(line, "quit", 4) == 0) {
            stop_and_join_search();
            break;
        }
        fflush(stdout);
    }
}