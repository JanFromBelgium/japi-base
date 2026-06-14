# Japi Base host simulator — the single canonical simulator (gcc only, no deps).
# JBB and the editor link japi_sim.c from here. -DJAPI_SIM makes japi_base.h take
# the host (shim) include path instead of the Pico SDK one.
CC      = gcc
JAPIINC = ../Japi Base Pico 2
CFLAGS  = -std=c11 -Wall -Wextra -O2 -DJAPI_SIM -I"$(JAPIINC)" -I. -Ishim

SIM = japi_sim.c

# Headless, byte-exact test of the bitmap API (no terminal needed).
sim_bitmap_test: $(SIM) sim_bitmap_test.c japi_sim.h shim/ff.h shim/lfs.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o sim_bitmap_test $(SIM) sim_bitmap_test.c

# Interactive visual demo: opens a window and draws. Run in a >=127x65 terminal.
sim_gfx_demo: $(SIM) sim_gfx_demo.c japi_sim.h shim/ff.h shim/lfs.h
	$(CC) $(CFLAGS) -o sim_gfx_demo $(SIM) sim_gfx_demo.c

test: sim_bitmap_test
	./sim_bitmap_test

clean:
	rm -f sim_bitmap_test sim_gfx_demo

.PHONY: clean test
