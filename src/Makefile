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


# Regenerate documentation sections that are derived from the parser grammar.
.PHONY: docs

docs:
	python3 ../tools/extract_dsl_grammar.py
	python3 ../tools/update_docs.py


.PHONY: verify-docs
verify-docs:
	python3 ../tools/verify_docs.py
