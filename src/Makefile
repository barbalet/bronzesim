CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -I../scenario-runtime/include

OBJS = main.o brz_vec.o brz_util.o brz_kinds.o brz_dsl.o brz_parser.o brz_sim.o brz_state.o brz_observe.o brz_world.o brz_land.o brz_settlement.o brz_agent.o brz_port.o scenario_runtime.o

all: bronzesim

bronzesim: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

scenario_runtime.o: ../scenario-runtime/src/scenario_runtime.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) bronzesim

docs:
	python3 ../tools/extract_dsl_grammar.py
	python3 ../tools/update_docs.py

.PHONY: all clean docs
