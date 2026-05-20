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

# --- Japi Base Editor (JBE) MVP step 1 ---------------------------------
# Same JBE source builds for both backends; here we link with the host sim.
EDITORINC = -I../editor
JBE_SRC   = ../editor/jbe.c

jbe: japi_sim.c $(JBE_SRC) ../editor/jbe_main.c ../editor/jbe.h japi_sim.h
	$(CC) $(CFLAGS) $(EDITORINC) -o jbe japi_sim.c $(JBE_SRC) ../editor/jbe_main.c

jbe_test: japi_sim.c $(JBE_SRC) ../editor/jbe_test.c ../editor/jbe.h japi_sim.h
	$(CC) $(CFLAGS) $(EDITORINC) -I. -o jbe_test japi_sim.c $(JBE_SRC) ../editor/jbe_test.c

test: sim_test jbe_test
	./sim_test
	./jbe_test

# Quick interactive try-out: writes its own source into A:jbe.c then opens it.
demo: jbe
	mkdir -p simdisk_A
	cp ../editor/jbe.c simdisk_A/jbe.c
	./jbe A:jbe.c

clean:
	rm -f sim_demo sim_test seam_app jbe jbe_test

.PHONY: clean test
