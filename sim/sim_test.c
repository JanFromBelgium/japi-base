/* Automated headless test for the simulator keyboard layer (step 4).
   Injects ASCII + a special key, verifies they come back in order via the
   platform API japi_has_char()/japi_get_char(). Exit 0 = pass. */
#include <stdio.h>
#include "japi_base.h"
#include "japi_sim.h"

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

int main(void) {
    japi_init();

    CHECK(!japi_has_char(), "buffer should start empty");
    CHECK(japi_get_char() == 0, "empty get returns 0");

    sim_type("Hi");
    sim_key_push(JAPI_KEY_LEFT);
    sim_key_push(JAPI_KEY_ESCAPE);

    CHECK(japi_has_char(), "has_char true after injection");
    CHECK(japi_get_char() == 'H',             "1st = 'H'");
    CHECK(japi_get_char() == 'i',             "2nd = 'i'");
    CHECK(japi_get_char() == JAPI_KEY_LEFT,   "3rd = JAPI_KEY_LEFT");
    CHECK(japi_get_char() == JAPI_KEY_ESCAPE, "4th = JAPI_KEY_ESCAPE");
    CHECK(!japi_has_char(), "buffer empty again");
    CHECK(japi_get_char() == 0, "drained get returns 0");

    /* FIFO order across a wrap is the ring-buffer's job; spot-check order */
    for (int i = 0; i < 10; i++) sim_key_push((uint16_t)('0' + i));
    int ok = 1;
    for (int i = 0; i < 10; i++) ok &= (japi_get_char() == (uint16_t)('0' + i));
    CHECK(ok, "FIFO order preserved for 10 keys");

    /* --- File I/O (step 6): write, exists, size, read back, remove --- */
    japi_file_t fw;
    CHECK(japi_fopen(&fw, "A:simtest.txt", JAPI_WRITE), "open A: for write");
    CHECK(japi_fwrite(&fw, "hello", 5) == 5, "wrote 5 bytes");
    japi_fclose(&fw);
    CHECK(japi_exists("A:simtest.txt"), "file exists after write");

    japi_file_t fr;
    CHECK(japi_fopen(&fr, "A:simtest.txt", JAPI_READ), "open A: for read");
    CHECK(japi_fsize(&fr) == 5, "fsize == 5");
    char rb[16] = {0};
    CHECK(japi_fread(&fr, rb, 15) == 5, "read 5 bytes back");
    CHECK(rb[0]=='h' && rb[4]=='o' && rb[5]==0, "content round-trips");
    japi_fclose(&fr);

    CHECK(japi_remove("A:simtest.txt"), "remove file");
    CHECK(!japi_exists("A:simtest.txt"), "gone after remove");
    CHECK(!japi_fopen(&fr, "B:nope.txt", JAPI_READ), "unknown drive fails");

    if (fails == 0) { printf("PASS: keyboard + file I/O (steps 4-6)\n"); return 0; }
    printf("%d check(s) failed\n", fails);
    return 1;
}
