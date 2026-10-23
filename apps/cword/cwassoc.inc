; =============================================================================
; os8088 - apps/cword/cwassoc.inc
;
; CWORD's association block (SPEC.md 54.6, 73.12): "a document wearing .RTF is
; MINE". The BRACKET is apps/cc/crt0.asm's - the OS88_ASSOC16 /
; OS88_ASSOC16_END pair and its offset assertions - so this file writes only
; the count byte and the extension, as apps/weave/wvassoc.inc does.
;
; WHY IT IS DECLARED AND NOT ONLY CLAIMED. os88_main() makes the same claim
; at run time with os88_assoc_set("RTF", "CWORD") - but that only happens once
; the program has RUN, and the disk's warm ASSOC.DAT is written by
; tools/os88disk.py from the header alone. Without this block the CWORD row it
; wrote carried no extension, so on a fresh boot a double-click on
; WELCOME.RTF found no program and answered "not a valid package".
; =============================================================================
    db 1                            ; the count byte is the author's to emit
    OS88_ASSOC_EXT 'RTF'            ; Rich Text Format, the file format
