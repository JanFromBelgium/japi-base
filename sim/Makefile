# Japi Base host simulator — dependency-free host build (gcc only).
CC      = gcc
JAPIINC = ../Japi Base Pico 2
CFLAGS  = -std=c11 -Wall -Wextra -O2 -I"$(JAPIINC)" -Ishim

sim_demo: japi_sim.c sim_demo.c japi_sim.h shim/ff.h shim/lfs.h
	$(CC) $(CFLAGS) -o sim_demo japi_sim.c sim_demo.c

sim_test: japi_sim.c sim_test.c japi_sim.h shim/ff.h shim/lfs.h
	$(CC) $(CFLAGS) -o sim_test japi_sim.c sim_test.c

# Seam proof: seam_app.c uses ONLY japi_base.h. Build 1 = run on the host
# backend. Build 2 = -fsyntax-only against the REAL platform header, proving
# the same source is accepted by the Pico-side API too (no SDK/link needed).
seam_app: japi_sim.c seam_app.c shim/ff.h shim/lfs.h
	$(CC) $(CFLAGS) -o seam_app japi_sim.c seam_app.c
	$(CC) -std=c11 -Wall -Wextra -fsyntax-only -I"$(JAPIINC)" \
	      -I"$(JAPIINC)/FatFs_SPI/ff15/source" \
	      -I"$(JAPIINC)/pico-lfs/libs/littlefs" seam_app.c

test: sim_test
	./sim_test

clean:
	rm -f sim_demo sim_test

.PHONY: clean test
