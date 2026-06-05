Japi Base -- keyboard layouts
=============================

Japi Base has US QWERTY (QWERTY_US) built in, so a US-QWERTY keyboard
needs nothing from this folder. To use another layout, take the matching
.kbd file from here and put it -- together with a small config.sys file
-- on an SD card.

How to use a layout
-------------------

  1. Copy the .kbd file for your keyboard to the root of an SD card
     (not inside a folder).
  2. In the same place, make a plain text file named config.sys with a
     single line:

         KEYBOARD MAPPING = <NAME>

     where <NAME> is the file's name without the .kbd ending. For a
     Belgian AZERTY keyboard, for example, copy AZERTY_BE.kbd and write:

         KEYBOARD MAPPING = AZERTY_BE

  3. Put the card in Japi Base and switch it on.

The SD card (drive A:, the removable media) overrides whatever layout is
on the built-in media (drive C:). The same two files may instead be
placed on the built-in media from a program if you would rather not keep
a card in the slot.

Available layouts
-----------------

    AZERTY_BE   Belgian AZERTY
    AZERTY_FR   French AZERTY
    QWERTY_BE   Belgian QWERTY
    QWERTY_UK   British QWERTY
    QWERTY_US   US QWERTY (also the built-in default)
    QWERTZ_DE   German QWERTZ

See Chapter 7 of the Japi Base manual for the full explanation. If you
build a working layout for a keyboard not covered here, send it to us
and we will add it to this set so other people can use it too.
