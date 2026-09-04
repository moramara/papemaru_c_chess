#include "uci.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdatomic.h>

#include "board.h"
#include "perft.h"
#include "search.h"
#include "tt.h"

#define TT_DEFAULT_MB 32
#define TT_MIN_MB 1
#define TT_MAX_MB 1024

_Atomic int stop_flag = 0;

static pthread_t search_thread;
static _Atomic int search_active = 0;
static int search_thread_started = 0;

static int option_move_overhead_ms = 30;
static int option_slow_mover = 100;
static int option_ponder = 0;

typedef struct {
    Board board;
    SearchLimits limits;
} SearchJob;

static SearchJob g_search_job;

static void move_to_str(Move m, char* buf);

static void* search_thread_main(void* arg) {
    (void)arg;
    Move best = search_root(&g_search_job.board, &g_search_job.limits);
    char best_str[8];
    move_to_str(best, best_str);
    printf("bestmove %s\n", best_str);
    fflush(stdout);
    atomic_store_explicit(&search_active, 0, memory_order_release);
    return NULL;
}

static void stop_and_join_search(void) {
    if (!search_thread_started)
        return;
    atomic_store_explicit(&stop_flag, 1, memory_order_relaxed);
    pthread_join(search_thread, NULL);
    search_thread_started = 0;
    atomic_store_explicit(&search_active, 0, memory_order_release);
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

static int parse_int_token(const char* s) {
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0')
        return 0;
    if (v < 0) return 0;
    if (v > 2147483647L) return 2147483647;
    return (int)v;
}

static void uci_parse_go(char* line, Board* b) {
    if (strstr(line, "divide")) {
        int d = 1;
        if (sscanf(line, "go perft divide %d", &d) != 1)
            if (sscanf(line, "perft divide %d", &d) != 1)
                if (sscanf(line, "divide %d", &d) != 1)
                    d = 1;
        if (d <= 0) d = 1;
        long long total = perft_divide(b, d, print_divide_line);
        printf("info string perft divide %d total = %lld\n", d, total);
        fflush(stdout);
        return;
    }
    if (strstr(line, "perft")) {
        int d = 5;
        sscanf(line, "go perft %d", &d);
        if (d <= 0) sscanf(line, "perft %d", &d);
        if (d <= 0) d = 5;
        for (int i = 1; i <= d; i++) {
            long long nodes = perft(b, i);
            printf("info string perft %d = %lld\n", i, nodes);
        }
        fflush(stdout);
        return;
    }

    SearchLimits limits;
    memset(&limits, 0, sizeof(limits));
    limits.depth = 64;

    char buf[8192];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* tok = strtok(buf, " \t\r\n");
    if (tok && strcmp(tok, "go") == 0)
        tok = strtok(NULL, " \t\r\n");

    while (tok) {
        if (strcmp(tok, "searchmoves") == 0) {
            tok = strtok(NULL, " \t\r\n");
            while (tok && limits.searchmove_count < 256) {
                Move m = parse_uci_move(tok);
                limits.searchmoves[limits.searchmove_count++] = m;
                tok = strtok(NULL, " \t\r\n");
            }
            break;
        }
        if (strcmp(tok, "infinite") == 0) {
            limits.infinite = 1;
            tok = strtok(NULL, " \t\r\n");
            continue;
        }
        if (strcmp(tok, "ponder") == 0) {
            limits.ponder = option_ponder ? 1 : 0;
            tok = strtok(NULL, " \t\r\n");
            continue;
        }

        char* value = strtok(NULL, " \t\r\n");
        if (!value)
            break;
        if (strcmp(tok, "depth") == 0) limits.depth = parse_int_token(value);
        else if (strcmp(tok, "movetime") == 0) limits.movetime_ms = parse_int_token(value);
        else if (strcmp(tok, "wtime") == 0) limits.wtime_ms = parse_int_token(value);
        else if (strcmp(tok, "btime") == 0) limits.btime_ms = parse_int_token(value);
        else if (strcmp(tok, "winc") == 0) limits.winc_ms = parse_int_token(value);
        else if (strcmp(tok, "binc") == 0) limits.binc_ms = parse_int_token(value);
        else if (strcmp(tok, "movestogo") == 0) limits.movestogo = parse_int_token(value);
        else if (strcmp(tok, "nodes") == 0) {
            char* endptr = NULL;
            unsigned long long v = strtoull(value, &endptr, 10);
            if (endptr != value && *endptr == '\0')
                limits.nodes = (uint64_t)v;
        }
        tok = strtok(NULL, " \t\r\n");
    }

    limits.move_overhead_ms = option_move_overhead_ms;
    limits.slow_mover = option_slow_mover;

    stop_and_join_search();
    g_search_job.board = *b;
    g_search_job.limits = limits;

    atomic_store_explicit(&stop_flag, 0, memory_order_relaxed);
    atomic_store_explicit(&ponder_hit_flag, 0, memory_order_relaxed);
    atomic_store_explicit(&search_active, 1, memory_order_release);
    if (pthread_create(&search_thread, NULL, search_thread_main, NULL) != 0) {
        atomic_store_explicit(&search_active, 0, memory_order_release);
        Move best = search_root(&g_search_job.board, &g_search_job.limits);
        char best_str[8];
        move_to_str(best, best_str);
        printf("bestmove %s\n", best_str);
        fflush(stdout);
        search_thread_started = 0;
    } else {
        search_thread_started = 1;
    }
}

static void ponder_hit(void) {
    if (!search_thread_started)
        return;
    atomic_store_explicit(&ponder_hit_flag, 1, memory_order_release);
}

static void print_uci_help(void) {
    printf("info string PapemaruCChess UCI help\n");
    printf("info string Options: Hash <MB>, Clear Hash, Move Overhead <ms>, Slow Mover <10..500>, Ponder <true|false>\n");
    printf("info string go: depth N | movetime MS | wtime MS btime MS winc MS binc MS movestogo N | nodes N | infinite | ponder | searchmoves e2e4 ...\n");
    printf("info string Time management uses a soft deadline for efficient stopping and a hard deadline with a safety reserve to avoid flagging.\n");
    fflush(stdout);
}

static int extract_setoption(const char* line, char* name, size_t name_cap, char* value, size_t value_cap) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncasecmp(p, "setoption", 9) != 0 || (p[9] != ' ' && p[9] != '\t')) return 0;
    p += 9;
    while (*p == ' ' || *p == '\t') p++;
    if (strncasecmp(p, "name", 4) != 0 || (p[4] != ' ' && p[4] != '\t')) return 0;
    p += 4;
    while (*p == ' ' || *p == '\t') p++;

    const char* v = strstr(p, " value ");
    if (!v) v = strstr(p, " value\t");
    if (v) {
        size_t n = (size_t)(v - p);
        while (n && (p[n-1] == ' ' || p[n-1] == '\t')) n--;
        if (n >= name_cap) n = name_cap - 1;
        memcpy(name, p, n); name[n] = '\0';
        v += 6;
        while (*v == ' ' || *v == '\t') v++;
        strncpy(value, v, value_cap - 1); value[value_cap - 1] = '\0';
        value[strcspn(value, "\r\n")] = '\0';
    } else {
        strncpy(name, p, name_cap - 1); name[name_cap - 1] = '\0';
        name[strcspn(name, "\r\n")] = '\0';
        value[0] = '\0';
    }
    return name[0] != '\0';
}

static void uci_parse_setoption(char* line) {
    char name[128] = {0};
    char value[128] = {0};
    if (!extract_setoption(line, name, sizeof(name), value, sizeof(value)))
        return;

    if (strcasecmp(name, "Hash") == 0) {
        int mb = parse_int_token(value);
        if (mb < TT_MIN_MB) mb = TT_MIN_MB;
        if (mb > TT_MAX_MB) mb = TT_MAX_MB;
        stop_and_join_search();
        tt_init((size_t)mb);
    } else if (strcasecmp(name, "Clear Hash") == 0) {
        stop_and_join_search();
        tt_clear();
    } else if (strcasecmp(name, "Move Overhead") == 0) {
        int v = parse_int_token(value);
        if (v > 500) v = 500;
        option_move_overhead_ms = v;
    } else if (strcasecmp(name, "Slow Mover") == 0) {
        int v = parse_int_token(value);
        if (v < 10) v = 10;
        if (v > 500) v = 500;
        option_slow_mover = v;
    } else if (strcasecmp(name, "Ponder") == 0) {
        if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0)
            option_ponder = 1;
        else if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0)
            option_ponder = 0;
    }
}

void uci_loop(void) {
    Board board;
    board_startpos(&board);
    tt_init(TT_DEFAULT_MB);
    char line[8192];

    while (1) {
        if (!fgets(line, sizeof(line), stdin))
            break;
        if (line[0] == '\n')
            continue;
        if (strncmp(line, "ucinewgame", 10) == 0) {
            stop_and_join_search();
            board_startpos(&board);
            tt_clear();
        } else if (strncmp(line, "help", 4) == 0 || strncmp(line, "ucihelp", 7) == 0) {
            print_uci_help();
        } else if (strncmp(line, "uci", 3) == 0) {
            printf("id name PapemaruCChess_v20260902\n");
            printf("id author Papemaru\n");
            printf("option name Hash type spin default %d min %d max %d\n", TT_DEFAULT_MB, TT_MIN_MB, TT_MAX_MB);
            printf("option name Clear Hash type button\n");
            printf("option name Move Overhead type spin default 30 min 0 max 500\n");
            printf("option name Slow Mover type spin default 100 min 10 max 500\n");
            printf("option name Ponder type check default false\n");
            printf("info string PapemaruCChess UCI: time management supports movetime, wtime/btime, winc/binc, movestogo, depth, nodes and infinite\n");
            printf("info string Move Overhead is a future GUI-tuning hook; a built-in safety reserve is always applied.\n");
            printf("uciok\n");
        } else if (strncmp(line, "isready", 7) == 0) {
            printf("readyok\n");
        } else if (strncmp(line, "setoption", 9) == 0) {
            uci_parse_setoption(line);
        } else if (strncmp(line, "position", 8) == 0) {
            uci_parse_position(line, &board);
        } else if (strncmp(line, "go", 2) == 0) {
            uci_parse_go(line, &board);
        } else if (strncmp(line, "ponderhit", 9) == 0) {
            ponder_hit();
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