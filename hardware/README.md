# Japi Base — the PCB (revision A)

Everything you need to have a Japi Base board made and to finish building it.

![Assembled Japi Base, revision A](Japi_Base-RevA-assembled.jpg)

*A finished revision-A board: Pico 2, VGA, PS/2 and audio fitted.*

![Revision A with only the SMD parts fitted](Japi_Base-RevA-smd-only.jpg)

*The same board with only the SMD parts soldered — this is how a JLCPCB-assembled
board (or one of the spare boards) arrives. You add the through-hole connectors.*

## What's in this folder

| File | What it is |
|------|------------|
| `Japi_Base-RevA-Gerbers.zip` | Gerber + drill files. Upload this to JLCPCB (or any board house) to order bare boards. |
| `Japi_Base-RevA-BOM-SMD.csv` | The **SMD** parts (with LCSC numbers), for JLCPCB's assembly service. |
| `Japi_Base-RevA-CPL-SMD.csv` | The **SMD** placement (pick-and-place) for that assembly. |

## Ordering the board

1. Upload `Japi_Base-RevA-Gerbers.zip` to **JLCPCB**.
2. (Recommended) Turn on **PCB assembly** and supply the BOM + CPL above. JLCPCB
   then solders every surface-mount part for you: the resistors and capacitors,
   the micro-SD socket, the reset button, and the TXS0102 level shifter.

## Parts you fit yourself (through-hole)

The SMD parts arrive already soldered. The connectors, the two headers and the
Pico are **through-hole** — source and solder these yourself:

| Ref | Part | LCSC |
|-----|------|------|
| A1 | Raspberry Pi Pico 2 (RP2350) | — (any RPi reseller) |
| J1 | VGA connector — D-SUB DE-15, through-hole | C138387 |
| J2 | PS/2 connector — mini-DIN 6-pin | C7428703 |
| J4 | 3.5 mm stereo audio jack — PJ-307C | C16684 |
| J5 | 3-pin header, 2.54 mm — VCCB 5V / 3V3 select | generic |
| J6 | 16-pin header, 2.54 mm — GPIO breakout | generic |
| H1–H4 | 4× M3 mounting hole (screws / standoffs optional) | — |

## Soldering the connectors (revision A)

Revision A has fairly small copper rings around the through-holes, so the
connectors want a steady hand. What works well:

- **Use plenty of flux** on the pads before you start.
- **Run the iron at about 350 °C** if your station lets you set it.
- **Clamp the board** in a vise or holder so you can press the iron against the
  pin and the copper ring at the same time, and heat both together.

Done that way it solders fine — it just isn't a first-ever-solder board.
A revision B with larger pads is being tested.

## Good to know
- Flash the firmware from the project's
  [**Releases**](https://github.com/JanFromBelgium/japi-base/releases) page.
- **Licence:** BSD-3-Clause (same as the firmware). Open hardware — build it,
  change it, share it; a mention that it is based on Japi Base is appreciated.
