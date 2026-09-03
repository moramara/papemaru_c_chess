CC	= gcc
OPTIONS	= -Wall -Wextra -std=gnu17 -lpthread
LDOPTIONS	= -lm
SRCS	=	\
	main.c	\
	uci.c	\
	board.c	\
	bitboard.c	\
	zobrist.c	\
	tt.c	\
	movegen.c	\
	perft.c	\
	eval.c	\
	search.c

PROGRAM	= PapemaruCChess

all:
	$(CC) -o $(PROGRAM) $(SRCS) $(OPTIONS) $(LDOPTIONS)

clean:
	rm -f $(PROGRAM)

.PHONY: all clean
