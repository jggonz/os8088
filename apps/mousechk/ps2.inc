; =============================================================================
; os8088 - apps/mousechk/ps2.inc - PS/2: the boot record (SPEC.md 126.7)
;
; STUB from the foundation (wave 1); owner `ps2` replaces it. p2_canprobe and
; p2_probe are the shell's two hooks for the Probe PS/2 button (SPEC.md 126.3):
;   p2_canprobe  out CF = 0 may probe; CF = 1 refused, SI = a NUL reason in DS
;   p2_probe     runs the live probe, synchronously on the UI task; its trace
;                then appears through p2_rows
; =============================================================================
p2_init:
    ret

p2_rows:
    ret

p2_canprobe:
    mov si, p2_s_notyet
    stc
    ret

p2_probe:
    ret

p2_s_notyet: db 'Probe not built yet', 0
