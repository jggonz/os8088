# PageDesk any PDF: screenshots at dfe2bdba8

All shots are of the branch head dfe2bdba8 as built (`pagedesk.o88`, `PDFONTX.RES`, `os8088.img`, `os8088-720.img`, `os8088-360.img`). PageDesk ran from a B: disk holding that `pagedesk.o88` and the test corpus.

Where the shots come from:
- QEMU: kern_big, 640 KB.
- CGA: the tree's own `VIDEO=cga` kernel.
- EGA: a copy of the QA tree's `VIDEO=ega` kernel. Kernel source is unchanged across the branch, and the symbol reader verified it byte for byte against this head's source.
- Hercules: QEMU cannot show Hercules pixels, so those shots are MartyPC (`os8088_5150_herc_sb_720_gla`, an IBM 5150 with a real Hercules card at B000h, 640 KB). They are the card's VRAM, decoded.

| file | shows | emulator | adapter | file opened |
|---|---|---|---|---|
| 01-colour-vga.png | COLOUR.PDF page 1: 48 colour-space cells (Indexed, ICC, CalGray/CalRGB, Lab, Separation, DeviceN), whole window, status `No changes` | QEMU | VGA 640x480 | tests/pdfany/gfx/colour.pdf |
| 01-colour-cga.png | the same page in the CGA compact layout (dithered mono), status `No changes` | QEMU | CGA 640x200 | colour.pdf |
| 01-colour-ega.png | the same page in EGA's own layout, status `No changes` | QEMU | EGA 640x350 | colour.pdf |
| 01-colour-herc.png | the same page on Hercules, status `No changes`. It took 460.7 guest seconds to draw on the 4.77 MHz 8088 | MartyPC | Hercules 720x348 | colour.pdf |
| 02-fonts-vga.png | a real Quartz print of /etc/services, page 1 of 25, drawn with its own embedded font, `No changes` | QEMU | VGA | QZTEXT.PDF (cupsfilter of /etc/services, made here) |
| 02-fonts-cga.png | the same page in the CGA layout, `No changes` | QEMU | CGA | QZTEXT.PDF |
| 02-fonts-herc.png | PDFKit's Sample.pdf page 1 (subset TrueType with ToUnicode): `PDFKit / Sample PDF`, `No changes` | MartyPC | Hercules | PDFKIT.PDF (from /System's PDFKit) |
| 03-photo-vga.png | a baseline JPEG photo page, `No changes`. The colour photo is drawn as a grey dither on VGA | QEMU | VGA | gen/img/JPGBASE.PDF (made here) |
| 04-jbig2-vga.png | JBIG2 symbol-dictionary text with /JBIG2Globals: the symbols drawn, `No changes` | QEMU | VGA | tests/pdfany/bilevel/jb2_text.pdf |
| 04-jbig2-herc.png | the same JBIG2 page at 400%, `No changes` | MartyPC | Hercules | jb2_text.pdf |
| 05-password-vga.png | the password prompt (`Password: _` / `Enter: open  Esc: cancel`) on an RC4-40 file with a user password, status `Opening RC440PW.PDF` | QEMU | VGA | tests/pdfany/crypt/RC4_40_PW.PDF |
| 06-r6-vga.png | the R6 question on an 8088: `This file's encryption takes about 21 minutes on this machine. Open anyway?` It was answered Esc, and the status then read `Cancelled.` with no document open. QEMU never asks this, because its CPU is not an 8086 | MartyPC (`os8088_xt_vga`, an IBM 5160 at 4.77 MHz with a VGA, 640 KB) | VGA | tests/pdfany/crypt/AES_256_R6.PDF |
| 07-shade-vga.png | shade.pdf, shading types 1-7 (axial, radial, function, free-form and lattice Gouraud, Coons and tensor patches), cropped to the page and zoomed 2x | QEMU | VGA | gen/gfx/shade.pdf (tools/os88pdfx_gfx.py) |
| 08-notes-vga.png | tt-simple.pdf in Annotate mode, with a note typed (`Checked on the 5150 - fonts OK`) and kept. The note card is on the page, the Notes panel lists `Note - Page 1 / Checked on the 51...` (F32's text kept), and the status reads `1 change - Unsaved` | QEMU | VGA | tests/pdfany/fonts/tt-simple.pdf |
| 08-notes-cga.png | the same note on CGA, with the compact layout's Notes popover opened: `Note p1: Checked on the 51...` | QEMU | CGA | tt-simple.pdf |
