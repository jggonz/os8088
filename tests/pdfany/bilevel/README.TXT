tests/pdfany/bilevel - fixtures for PDXI's CCITT and JBIG2 decoders (PAGEDESK-SPEC 20.22.12)

Everything here is made by `python3 tools/os88pdfx_bilevel.py --build tests/pdfany/bilevel`, which
is deterministic (random.Random(88)): re-running it must change nothing. The encoders are this
repository's own (tools/os88pdfx_bilevel.py); no JBIG2 encoder exists on the build host, so each
stream was also decoded by an independent reader before it was trusted:

  MuPDF 1.28.6 (mutool draw -r 72 -c gray) reads every .pdf here and draws the source image exactly
  (tools/os88pdbl_check.py mutool-jb2-generic / -text / -huff / -halftone / mutool-ccitt, the same
  encoders over random inputs). libtiff (via PIL) decodes the CCITT streams the encoder wraps in a
  TIFF (the tables and G3/G4 shapes). One known difference: MuPDF ignores a halftone region's
  HDEFPIXEL=1; the checks hold defpix 0 against it and defpix 1 against the model.

  ccitt_g4 / g3_1d / g3_2d / g4_align   200x48, /K -1, 0 (EOL), 4 (EOL), -1 with /EncodedByteAlign
  jb2_generic   96x64, one MQ generic region, template 0, TPGDON      jb2_mmr   the same bitmap, MMR
  jb2_text      symbol dictionary (.glob) + text region (.jb2)         jb2_huff  Huffman, MMR collective
  jb2_halftone  pattern dictionary (.glob) + halftone region           *.bin / *.jb2 / *.glob: coded bytes
