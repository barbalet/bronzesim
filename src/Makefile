CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

OBJS = main.o brz_vec.o brz_util.o brz_kinds.o brz_dsl.o brz_parser.o brz_sim.o

all: bronzesim

bronzesim: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) bronzesim
