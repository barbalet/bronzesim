CC=cc
CFLAGS=-O2 -std=c11 -Wall -Wextra -Wpedantic

OBJS=main.o brz_util.o brz_parser.o brz_world.o brz_cache.o brz_sim.o brz_dsl.o

all: bronzesim

bronzesim: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o bronzesim
