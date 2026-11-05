; =============================================================================
; os8088 - apps/mousechk/summary.inc - the Summary section (SPEC.md 126.5)
;
; STUB from the foundation (wave 1): one heading, so the shell has something
; to build. Owner `other` replaces it. Entry contract (SPEC.md 126.2): sum_init
; once at open, sum_rows on every rebuild with ES = KERNEL_SEG, DS = package.
; =============================================================================
sum_init:
    ret

sum_rows:
    mov si, sum_s_head
    mov al, LP_INFO
    call mc_head
    mov si, sum_s_stub
    call mc_row
    mov si, sum_s_stubv
    call mc_put
    ret

sum_s_head: db 'Summary', 0
sum_s_stub: db 'Status', 0
sum_s_stubv: db 'sections not built yet', 0
