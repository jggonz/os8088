tests/pdfany/wrt - PDWRT's any-PDF fixtures (PAGEDESK-SPEC 20.22.4.6)

Nothing is committed here on purpose: `make pdwrt-enc` builds every fixture
from the committed samples (apps/pagedesk/samples/MARKUP.PDF and PAGES.PDF)
with qpdf (12.4.2 when this was written), so a fixture and its provenance
cannot disagree. The commands (hosttest/pdw_encrun.py, FIX and FOREIGN):

  qpdf --object-streams=generate --stream-data=uncompress  MARKUP.PDF plain.pdf
  qpdf --object-streams=generate --stream-data=uncompress \
       --encrypt "" owner 128 --use-aes=y --               MARKUP.PDF aes128.pdf
  ... --encrypt u1 owner 128 --use-aes=y --                (user password "u1")
  qpdf --object-streams=disable ... --encrypt "" owner 128 --use-aes=y --   (classic)
  ... --encrypt "" owner 256 --                            (AES-256, R6)
  qpdf --allow-weak-crypto ... --encrypt "" owner 128 --use-aes=n --  (RC4-128)
  qpdf --allow-weak-crypto --object-streams=disable ... --encrypt "" owner 40 --   (RC4-40)
  foreign file: the same on PAGES.PDF, plain and AES-128 with user password "fp"

(The streams are left uncompressed so the stand-in's reader, which is qpdf,
is not the thing under test.)
