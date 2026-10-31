; =============================================================================
; os8088 - apps/win16/w16dos.inc
;
; WHERE WIN16'S DOS LIVES (SPEC.md 124.21.1): one claim, read by two
; assemblies - the image, which makes the claim and far-calls into it, and
; apps/win16/w16dosh.asm, the host part that sits in it above the core.
;
;   0                 nothing (the core's CORE_ORG clears a package header)
;   CORE_ORG          build/doscore.bin, the core DOS.O88 ships
;   CORE_BSS_AT       its bss, CORE_BSS_SIZE bytes, zero
;   W16DH_ORG         the host part: the table below, then its code
;   W16DH_ORG+W16DH_MAX
;
; Needs apps/dos/doscall.inc first.
; =============================================================================

W16DH_ORG   equ (CORE_BSS_AT + CORE_BSS_SIZE + 15) & 0xFFF0
W16DH_MAX   equ 1024                ; the host part's budget, asserted there
W16DH_KB    equ (W16DH_ORG + W16DH_MAX + 1023) / 1024   ; the claim

W16DH_INIT  equ W16DH_ORG + 0       ; far: AX = the PSP's segment, BL = the
                                    ;   volume, DX = the folder's cluster,
                                    ;   CX = the window's segment, SI = its
                                    ;   paragraphs; on the UI task's stack.
                                    ;   out CF=1 = the window will not do
W16DH_I21   equ W16DH_ORG + 3       ; far, as an `int`: the core's int 21h
W16DH_DONE  equ W16DH_ORG + 6       ; far: every handle the program left open
                                    ;   closed, as DOS does at an exit
W16DH_HSS   equ W16DH_ORG + 9       ; word: the stack the core runs a call on,
W16DH_HSP   equ W16DH_ORG + 11      ;   written by the image before each one
W16DH_WKB   equ W16DH_ORG + 13      ; byte: the read window's floor in KB,
                                    ;   DOS's DOS_WKB, read before the claim
