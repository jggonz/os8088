; =============================================================================
; os8088 - apps/dos/doscents.inc
;
; **THE CORE'S PUBLISHED SURFACE, AS ONE LIST** (SPEC.md 96.44.5). Every name
; a HOST reaches across the core seam, and nothing else - measured rather than
; guessed, which is what W9a's `%ifndef DOS_EXTCORE` marking made possible:
; classify every reference in `dos.asm`, `dosh.inc` and the window files by
; which side DEFINES the symbol and which side NAMES it, and this is what is
; left.
;
; **IT IS INCLUDED TWICE AND NEVER READ BY HAND.** `apps/dos/doscall.inc`
; defines DOSC_E and DOSC_D one way for the core - which EMITS the table - and
; another for a host, which turns each name into its slot. So the ORDER of
; these lines is an ABI: inserting one in the middle moves every entry after
; it, in a build where the two halves are assembled separately and nothing
; would say so. **APPEND, never insert**, and `tests/unit/t_doscore.py` is
; what notices.
;
; **`dos_keeph` CAME OFF THIS LIST** (SPEC.md 96.44.6) and that is not a
; breach of the rule above: nothing SHIPS an old core against a new host - the
; two halves are parts of one `DOS.O88` and `make` rebuilds both from this
; file - so what the rule protects against is a half-rebuild, which the
; Makefile cannot produce. What it was is a pure window routine marked core,
; whose only caller was in the window half all along, and it was carrying the
; last two `OSAPI_*` far calls in the core.
;
; THE TWO POPULATIONS ARE NOT THE SAME KIND OF THING. A DOSC_E is CODE and its
; slot is a `jmp near`, so a host calls it exactly as it did when the core was
; inline. A DOSC_D is DATA and its slot is a WORD holding the address, so
; `mov si, dsh_s_bad` becomes `mov si, [..]` - one byte more per site, and the
; only two that survived the cheaper answers (96.44.5): the reason codes moved
; to the container, the code page turned out to be the window's all along, and
; the prompt got its own three-byte CRLF.
; =============================================================================
    DOSC_E dos_be_goto
    DOSC_E dos_be_here
    DOSC_E dos_be_read
    DOSC_E dos_be_vkind
    DOSC_E dos_be_write
    DOSC_E dos_blaster_set
    DOSC_E dos_build_psp
    DOSC_E dos_date_init
    DOSC_E dos_drv_bank
    DOSC_E dos_err_line
    DOSC_E dos_exe_setup
    DOSC_E dos_fh_setup
    DOSC_E dos_hexd
    DOSC_E dos_hook_vectors
    DOSC_E dos_is_exe
    DOSC_E dos_load
    DOSC_E dos_prog_enter
    DOSC_E dos_save_machine
    DOSC_E dos_upc
    DOSC_E dos_walk_pbuf
    DOSC_E dsh_bank
    DOSC_E dsh_cmdword
    DOSC_E dsh_cpz
    DOSC_E dsh_dir_more
    DOSC_E dsh_dir_stop
    DOSC_E dsh_home
    DOSC_E dsh_nth
    DOSC_E dsh_run
    DOSC_E dsh_skipsp
    DOSC_E dsh_to11
    DOSC_D dsh_s_bad
    DOSC_D dsh_s_ver
; **APPENDED, never inserted** (SPEC.md 96.48.2): the path box's parser stands
; the machine on the drive a typed path names, because the walk follows
; `[dos_pvol]` now rather than `[dos_vol]`, and `dos_path_take` is on the
; WINDOW side of the seam. One entry, at the end, so no slot before it moves.
    DOSC_E dos_fh_stand
