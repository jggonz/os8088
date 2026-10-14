; tests/csprobe/harness.asm - REDLINE's chipset probes (apps/redline/csrows.inc)
; on their own, for tests/csprobe.py to run under Unicorn against a model of
; each board. Segment 0, org 100h; the test pokes the CPU tier and reads the
; state cs_identify leaves. OSAPI_CPU_INFO is a near stub answering [h_tier].
cpu 8086
bits 16
org 0x100
CPU_8086 equ 0
CPU_286  equ 1
CPU_386  equ 2
%define OSAPI_CPU_INFO h_cpuinfo
h_start:
    call cs_identify
    hlt
h_tier:     db 0
h_cpuinfo:
    mov al, [h_tier]
    ret
cs_ap_fe:                           ; the rows' apply procs (csapply.inc):
cs_ap_ne:                           ; not this harness's subject -
    stc                             ; feharness.asm's
    ret
%include "redline/csrows.inc"
