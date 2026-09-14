# Speedy BASIC demo corpus

These `.BAS` files are deterministic extracts of the built-in samples in the
Speedy BASIC web IDE (`src/features/samples/samples.ts` in
`github.com/jggonz/speedybasic`, extracted at commit
`1dc7d4d02d3ffb9f8097e0a5491b616416c6c540`). They are source programs, carried on the application
disk so every text, graphics, sound, hardware and editor demonstration can be
opened and run on os8088.

`MANIFEST.TXT` fixes their order. `tools/speedybasic_samples.py` checks this
vendored corpus byte for byte against `../speedybasic` when that checkout is
available, then materializes it under `build/speedybasic-demos/`. A standalone
os8088 clone uses the vendored copy and needs no sibling checkout.

The samples retain their own introductory comments and attributions. The
extractor normalizes line endings to LF and requires ASCII, matching the text
format consumed by the os8088 editor.
