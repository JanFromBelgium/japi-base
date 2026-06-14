/* Simulator-only API — NOT part of the Japi Base platform.
 *
 * The editor/app code includes only japi_base.h and never this file. Test
 * harnesses (and step 5's live input) use these helpers to feed the keyboard
 * ring buffer that japi_has_char()/japi_get_char() read from.
 *
 * Codes are the same uint16_t space as the platform: ASCII < 256, special
 * keys are the JAPI_KEY_* constants from japi_base.h (>= 0x0101). The buffer
 * carries any 16-bit code, so future SHIFT/CTRL+SHIFT/ALT codes work too.
 */
#ifndef JAPI_SIM_H
#define JAPI_SIM_H
#include <stdint.h>

void sim_key_push(uint16_t code);     /* enqueue one key code */
void sim_type(const char *s);         /* enqueue each byte of an ASCII string */

#endif
