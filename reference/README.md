# Reference material

Saved 2026-09-26 for the Gorillas port. These are reference inputs, not runtime
assets. Preserve the originals; implementation notes belong alongside them.
Copyright and attribution remain with the original authors/publishers.

- [gorillas/gorilla.bas](gorillas/gorilla.bas) — User-supplied `gorilla.bas` from the repository root; byte-for-byte copy.
  SHA-256: `9926fc1f50c4b489ec4c1b0da5bd2c497ebf4282b3259c28a835a743e24699f7`

- [ibm/cga-technical-reference.pdf](ibm/cga-technical-reference.pdf) — IBM Technical Reference, Options and Adapters, Volume 2 (April 1984). Download: https://www.ibm-pc.se/manuals/ibm/options/Technical_Reference_Options_and_Adapters_Volume_2_Apr84.pdf
  SHA-256: `b5bf24ea3e63082d5c637db8b08469c6d4929b4b9f6b7b24c7a211338b42a15f`

- [ibm/xt-color-select-page-150.html](ibm/xt-color-select-page-150.html) — IBM Personal Computer XT Technical Reference, printed page 1-100 (color-select register). Saved HTML: https://www.manualslib.com/manual/4563968/Ibm-Personal-Computer-Xt.html?page=150
  SHA-256: `dfc0214beeee8019c7b2237840c5d1abdaebfd35c45885e8b9094237969213af`

The saved HTML has its embedded page token removed before check-in.

## Gorillas palette notes

The supplied BASIC's `SetScreen` remaps its EGA ink indices: 0 → 1 (blue
sky), 1 → 46 (orange objects), 2 → 44 (explosion), 3 → 54 (yellow sun),
5 → 7 (gray building), 6 → 4 (red building), 7 → 3 (cyan building), and
9 → 63 (white). These values are EGA six-bit colors, not desktop indices.
`MakeCityScape` uses ink 14 for most windows and ink 8 for unlit windows.
`DrawGorilla` adds brows, nostrils and chest curves; `DoSun` adds rays and a
smile. `tools/gorillas_art.py` translates this palette and creates native art.

The CGA color-select register is at port `3D9h`. Bits 0–3 select the
background from 16 colors, bit 4 selects foreground intensity, and bit 5
selects a foreground group. The two ordinary groups are green/red/brown
and cyan/magenta/white, with brighter variants. The three foreground slots
are not independently programmable RGB entries. The port uses `11h` for
blue background and bright green/red/yellow. This approximates the EGA
scheme; it is not a claim to reproduce its RGB values on CGA.

## Live repository references

Use the maintained versions, rather than duplicating interface contracts:

- [SPEC.md §53 and §98](../SPEC.md): exclusive display ownership and Gorillas.
- [SDK](../apps/os88api.inc): `FSX_RUN`, `FSX_MODE`, and drawing contracts.
- [Dot Delirium](../apps/dotdel/dotdel.asm): fullscreen lifecycle.
- [Tank rasterizer](../apps/tank/tkraster.inc): CGA color-select and VGA DAC examples.
- [Testing guide](../docs/TESTING.md) and [Hercules testing](../docs/HERCULES-TESTING.md).
