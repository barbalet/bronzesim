CC ?= cc

.PHONY: all test clean

all:
	$(MAKE) -C src

test:
	$(MAKE) -C src/test run

clean:
	$(MAKE) -C src clean
	$(MAKE) -C src/test clean
