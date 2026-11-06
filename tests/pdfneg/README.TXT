tests/pdfneg: the NEGATIVE CORPUS (PAGEDESK-SPEC 18.3, PD-28)

EVERY FILE GENERATED FROM THIS README IS DELIBERATELY INVALID OR UNSUPPORTED.
None of them is a sample. They are never shipped on a disk and never placed in
a folder that advertises valid PDFs. Regenerate with:

    python3 tools/os88pdfgen.py --negative build/pdfneg

The files are generated, not committed; only this README is. The generator
writes this exact text, and tests/unit/t_pdfix.py checks that it has not drifted.

Columns: file | layer | expected | why.
  layer    open   = PDE_* comes back from opening the file
           stream = PDV_ENG_STRM_OPEN of the page content (open itself succeeds)
           pageinfo = PDV_ENG_PAGEINFO (inherited boxes resolve on demand)
           render = the file opens (or the page is reached) and the refusal is
                    raised while drawing the page; the page is then shown as
                    Incomplete rendering (PAGEDESK-SPEC 10.6) where the code is
                    PDE_FONT/PDE_GRAPHICS/PDE_FLATE/PDE_FILTER
  expected the PDE_* code of PAGEDESK-SPEC 3.6 / apps/pagedesk/pdabi.h.
           PDE_OK means: no failure code, the bad object is skipped, nothing
           crashes, and the document stays editable.

These expectations are the fixture author's reading of PAGEDESK-SPEC 1.3 and 3.6;
where the engine's documented code differs, the engine's 20.2 wins and this table
is regenerated with it.

After EVERY case the test must also check recovery: the source file is
byte-identical, no claim is left open, and a following valid file opens.

trunc_header.pdf           open    PDE_TRUNC     file is only the five bytes "%PDF-"
bad_header.pdf             open    PDE_SYNTAX    no %PDF- header in the first 1024 bytes
trunc_xref.pdf             open    PDE_TRUNC     cut in the middle of the xref table; no trailer or startxref
trunc_startxref.pdf        open    PDE_TRUNC     startxref has no offset
xref_wrong_offset.pdf      open    PDE_XREF      startxref points inside the xref keyword, not at it
xref_swapped_entries.pdf   open    PDE_XREF      two xref entries swapped: "N 0 obj" is not at the offset
xref_size_huge.pdf         open    PDE_XREF      /Size claims 99,999,999 objects, the table has a handful
no_root.pdf                open    PDE_XREF      trailer has no /Root
stream_length_short.pdf    stream  PDE_SYNTAX    /Length 3 is shorter than the data; endstream is not where it says (file length unchanged)
stream_length_long.pdf     stream  PDE_SYNTAX    /Length overshoots endstream but stays inside the file
stream_length_past_eof.pdf stream  PDE_TRUNC     /Length runs past the end of the file
stream_length_negative.pdf stream  PDE_SYNTAX    negative /Length
stream_no_endstream.pdf    stream  PDE_SYNTAX    endstream keyword corrupted
cyclic_pagetree.pdf        open    PDE_CYCLE     /Pages lists itself among its own /Kids
cyclic_parent.pdf          open    PDE_CYCLE     /Parent chain loops (a page is its own parent)
deep_pagetree.pdf          open    PDE_DEPTH     page tree nested 120 levels deep
deep_array.pdf             open    PDE_DEPTH     array nested 5,000 levels in a page dictionary
deep_dict.pdf              open    PDE_DEPTH     dictionary nested 2,000 levels
cyclic_resources.pdf       render  PDE_CYCLE     a Form XObject draws itself; open succeeds, render refuses
form_nest_7.pdf            render  PDE_DEPTH     Form XObjects nested 7 deep (limit 4)
huge_mediabox_number.pdf   pageinfo PDE_RANGE     MediaBox width is a 11-digit integer
page_5000pt.pdf            pageinfo PDE_RANGE     page box outside +/-2047 pt
huge_operands.pdf          render  PDE_RANGE     operands beyond 32-bit and a 99,999,999 pt font size in a content stream
graphics_stack_1000.pdf    render  PDE_LIMIT     1,000 nested q operators (the stack is bounded)
string_100k.pdf            render  PDE_LIMIT     a 100,000 byte literal string in one operator
name_20k.pdf               open    PDE_LIMIT     a 20,000 byte name
xref_subsec_300.pdf        open    PDE_LIMIT     a VALID file whose xref has 301 subsections (one per object); the engine keeps at most 256 (PD_MAXSUBSEC)
objnum_70000.pdf           open    PDE_LIMIT     the page content is object 70000: an xref subsection and a reference above 65535 (DECISIONS D8)
runlength_bomb.pdf         render  PDE_LIMIT     80,000 bytes of RunLength that expand to about 5 MB
flate_unsupported.pdf      render  PDE_FLATE     FlateDecode content on the XT build
lzw_unsupported.pdf        render  PDE_FILTER    LZWDecode content
filter_chain_5.pdf         render  PDE_FILTER    a five-stage filter pipeline (limit 4)
prev_loop.pdf              open    PDE_XREF      /Prev points at the xref section it is in
prev_broken.pdf            open    PDE_XREF      /Prev points at an offset with no xref section
prev_past_eof.pdf          open    PDE_XREF      /Prev beyond the end of the file
prev_chain_10.pdf          open    PDE_LIMIT     ten revisions chained by /Prev (limit 8)
annot_no_rect.pdf          render  PDE_OK        open succeeds; the Square without /Rect is skipped, not drawn, no crash
annot_bad_rect.pdf         render  PDE_OK        /Rect has three numbers: skipped
annot_quads_odd.pdf        render  PDE_OK        /QuadPoints has 7 numbers, not a multiple of 8: skipped
annot_no_subtype.pdf       render  PDE_OK        no /Subtype: skipped
annots_not_array.pdf       render  PDE_OK        /Annots is the integer 42: ignored
annots_not_dicts.pdf       render  PDE_OK        /Annots holds a string, a dangling reference, an integer and null: all skipped
annot_ap_cycle.pdf         render  PDE_CYCLE     an /AP /N form that draws itself
annot_ap_not_stream.pdf    render  PDE_OK        /AP /N is an integer: the annotation is skipped
annot_launch_action.pdf    render  PDE_OK        a Link with a Launch action: shown as a link, never followed
missing_font.pdf           render  PDE_FONT      text selects /F9, which is not in /Resources: substitute face, Incomplete rendering
missing_image.pdf          render  PDE_GRAPHICS  /ImX is not in /Resources: nothing drawn, Incomplete rendering
shading_sh.pdf             render  PDE_GRAPHICS  the sh operator is outside the profile: Incomplete rendering
dct_image.pdf              render  PDE_FILTER    a DCTDecode (JPEG) image
zero_pages.pdf             open    PDE_NOTFOUND  a page tree with no pages
encrypted.pdf              open    PDE_ENCRYPT   /Encrypt in the trailer (the dictionary is a stub; the code must be PDE_ENCRYPT before anything is decrypted)
signed_perms.pdf           open    PDE_SIGNED    catalog carries /Perms: opens READ-ONLY with a label, not refused
xrefstm_objstm.pdf         open    PDE_XREFSTM   a PDF 1.5 file with a cross-reference STREAM and an object stream (valid, but outside the XT profile)

53 files.
