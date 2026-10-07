1942 sampled effects use CC0 1.0 material, retrieved 2026-09-28:

- `shot.wav`: kurt, [Gunshots](https://opengameart.org/content/gunshots),
  `22 Magnum.wav`. Source SHA-256:
  `132a491a1e52c30042cac867ff461fc895c1a31b5420dfff24f5af0f2185d5cc`.
- `explosion.wav` and `loss.wav`: Spring Spring,
  [Mechanical Explosion](https://opengameart.org/content/mechanical-explosion),
  `mechanical_explosion.wav`. Source SHA-256:
  `13e9f713bd0d5921b223b01d29d68bb7625a1a1fbf1894af35ebfd5ef72c8632`.

License: [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/).
These audio assets remain CC0; they are not covered by the code's license.

Edits: stereo averaged to mono, box-filtered from 96 kHz to 8 kHz,
trimmed to one attack, DC removed, normalized, and faded at both ends.
Player loss slows the explosion to 75% speed. Converted to unsigned 8-bit
PCM. The exact crop, gain, length, speed and output hashes are recorded in
`samples.json`. Only these short derivatives ship; the original downloads
are not needed to build. `tools/1942sfx.py` validates and packs the WAVs.
