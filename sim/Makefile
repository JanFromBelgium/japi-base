# Japi Base host simulator — dependency-free host build (gcc only).
CC      = gcc
JAPIINC = ../Japi Base Pico 2
CFLAGS  = -std=c11 -Wall -Wextra -O2 -I"$(JAPIINC)" -Ishim

sim_demo: japi_sim.c sim_demo.c japi_sim.h shim/ff.h shim/lfs.h
	$(CC) $(CFLAGS) -o sim_demo japi_sim.c sim_demo.c

sim_test: japi_sim.c sim_test.c japi_sim.h shim/ff.h shim/lfs.h
	$(CC) $(CFLAGS) -o sim_test japi_sim.c sim_test.c

test: sim_test
	./sim_test

clean:
	rm -f sim_demo sim_test

.PHONY: clean test
