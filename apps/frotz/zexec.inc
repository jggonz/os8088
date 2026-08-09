; =============================================================================
; os8088 - apps/frotz/zexec.inc  (STUB - not yet implemented)
; =============================================================================
zx_worker:
.spin:
    call OSAPI_TASK_ALIVE
    mov ax, 18
    call OSAPI_TASK_SLEEP
    jmp .spin
