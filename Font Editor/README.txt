Font Editor - Japi Base 8x12 CP437 Font
========================================

1. Convert the C header to BDF format:

	python3 font_h_to_bdf.py "../Japi Base Pico 2/font_8x12.h" japi_8x12.bdf

2. Edit the font in gbdfed (install: sudo apt install gbdfed):

	gbdfed japi_8x12.bdf

3. Convert the edited BDF back to C header:

	python3 bdf_to_font_h.py japi_8x12.bdf "../Japi Base Pico 2/font_8x12.h"

4. Rebuild and flash:

	cd "../Japi Base Pico 2/build"
	ninja


Files
-----
font_h_to_bdf.py	Converts C header to BDF (step 1)
bdf_to_font_h.py	Converts BDF back to C header (step 3)
japi_8x12.bdf		Font in BDF format for gbdfed
font_8x12.h		Backup copy of the C header

The font is 8x12 pixels, 256 glyphs, CP437 character set.
MSB (0x80) is the leftmost pixel in each row byte.
