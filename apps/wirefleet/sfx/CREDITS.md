WireFleet's sampled effects (SPEC.md 114.11) use CC0 1.0 material,
retrieved 2026-10-20:

- `launch.wav`: Kenney, [Sci-fi Sounds](https://opengameart.org/content/sci-fi-sounds)
  (`sci-fi_sounds.zip`, SHA-256
  `119340f351a5098ad814f78719438c0da355a9ce8a4c8a3af6a8d48aa3d49e04`),
  `Audio/thrusterFire_000.ogg`. Source SHA-256:
  `0fdc474d93f209debf1fc4ea77765a669dcb7676db31b4d8a0a509685bccf374`.
- `explode.wav`: the same pack, `Audio/lowFrequency_explosion_001.ogg`
  (SHA-256 `cdda50600a4928f2f1b8371224d53c171ac799c1cf2c30b11999508985250b3b`)
  mixed with `Audio/explosionCrunch_000.ogg`
  (SHA-256 `4b597d658d0ae101f0a030fbeea5fc3a4292ab85f017470a8254a8e7959cbd69`).
- `splash.wav`: Thimras, [Ocean splash](https://opengameart.org/content/ocean-splash),
  `cannon_miss.ogg`. Source SHA-256:
  `d91a1effc2964673033fa9f8d22093029fa0b500aea0bafb86724738c0e89fe3`.

License: [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/).
These audio assets remain CC0; they are not covered by the code's license.
Kenney asks for (and does not require) credit: www.kenney.nl.

Edits, all by `tools/wirefleet_sfxderive.py`: decoded to 16-bit WAV
(`afconvert -f WAVE -d LEI16`), channels averaged to mono, low-passed at
3.6 kHz with a windowed sinc and resampled to 8 kHz, cut to the first
4,096 samples (0.512 s), faded in and out, peak-normalised, softly
saturated and converted to unsigned 8-bit PCM. The explosion is 0.65 of
the low boom plus 0.45 of the crunch. Only these short derivatives ship;
the original downloads are not needed to build. `tools/wirefleet_sfx.py`
validates them against `samples.json` and packs the lazy part `WFLET.SFX`.
