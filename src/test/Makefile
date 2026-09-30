
CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O0 -g
INCLUDES = -I.. -I. -I../../scenario-runtime/include

# Build all src/*.c except main.c
SRC_C = \
  ../brz_agent.c \
  ../brz_dsl.c \
  ../brz_kinds.c \
  ../brz_land.c \
  ../brz_parser.c \
  ../brz_settlement.c \
  ../brz_sim.c \
  ../brz_state.c \
  ../brz_util.c \
  ../brz_vec.c \
  ../brz_world.c \
  ../brz_port.c \
  ../../scenario-runtime/src/scenario_runtime.c

TEST_C = \
  test_common.c \
  test_main.c \
  test_util.c \
  test_vec.c \
  test_kinds.c \
  test_land.c \
  test_parser.c \
  test_dsl.c \
  test_sim.c

OBJS = $(SRC_C:.c=.o) $(TEST_C:.c=.o)

all: tests

tests: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f tests $(OBJS)

run: tests
	./tests

.PHONY: all clean run
