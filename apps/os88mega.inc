; =============================================================================
; os88mega.inc - a package's KERN_MEGA ARM (docs/plans/KERN-MEGA-PLAN.md 14.66)
; =============================================================================
; kern_mega is kern_big switched into 286 protected mode. Most packages run on
; it unchanged, because the kernel completes what real mode allowed and
; protected mode does not (a store through CS, KERN-MEGA-PLAN 14.16). Some
; carry code that exists ONLY for it - a second copy of an ISR that writes
; through a data selector, a decoder assembled to write nothing through CS, a
; ring placed above 1MB, a run of selectors read in place. That code is
; assembled under PKG_MEGA and nowhere else:
;
;   * the SHIPPED build (every apps disk, every geometry) is built WITHOUT
;     PKG_MEGA, so an 8088's disk carries none of it - the owner's rule, "mega
;     specific work that more than trivially increases the disk size is gated
;     to mega builds only";
;   * `make mega` builds the same source WITH -DPKG_MEGA into build/megak/apps/
;     and puts those builds on kern_mega's own apps disks, build/megaapps.img
;     and build/megaapps120.img, and on its system disks - the KERN_SMALL
;     precedent of `make smallapps` (SPEC.md 27.16), one source built twice;
;   * tests/unit/t_megapkg.py is the gate: no SHIPPED build may assemble a
;     kern_mega marker (CPU_F_PM, the kern_mega-only cells) outside what
;     tests/megapkg.txt allows, nor carry a part assembled for protected mode.
;
; A SHIPPED BUILD MUST STILL BE CORRECT ON kern_mega, because nothing stops a
; user putting the ordinary apps disk in a kern_mega machine. Each package
; decides how, and says so where it does: it runs LOW and plain (its claims
; conventional, its class byte without OS88_HIGH/OS88_HIGHMEM), it refuses
; one tier (the speaker library, apps/os88spk.inc, refuses PCM under
; CPU_F_PM), or it refuses to START - OS88_MEGA_REFUSE below, for a package
; whose every path assumes real mode (the Video Player's LOADALL borrows).
; =============================================================================

%ifndef OS88MEGA_INC
%define OS88MEGA_INC

; OS88_MEGA_REFUSE - the first lines of an ENTRY PROC (ES = KERNEL_SEG, DS =
; ours): built without PKG_MEGA and running under CPU_F_PM, a toast and CF=1,
; which tears the launch down before anything is claimed or read. Built WITH
; PKG_MEGA it is nothing at all. AX clobbered (OSAPI_CPU_INFO's), every other
; register kept. Its length is EVEN, so whatever follows it keeps the parity it
; had before it was added (tools/os88rdiff.py --hot)
%macro OS88_MEGA_REFUSE 0
%ifndef PKG_MEGA
%%b:
    call OSAPI_CPU_INFO             ; AH = CPU_F_*; AX only
    test ah, CPU_F_PM
    jz %%ok
    push es
    push ds
    pop es
    mov si, %%s
    xor cx, cx
    call OSAPI_TOAST                ; (24 characters reach the bar: 22 here)
    pop es
    stc
    ret
%%s: db 'Use the MEGA apps disk', 0
    times (($ - %%b) & 1) nop
%%ok:
%endif
%endmacro

%endif ; OS88MEGA_INC
