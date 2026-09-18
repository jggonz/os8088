; =============================================================================
; kernel/hbstage.inc - THE STAGING AREA IN THE TEXT FRAMEBUFFER, ONCE
;
; SPEC.md 87.5's map, shared as SOURCE between `kernel/hiber.inc` - which
; stages it for an ordinary resume - and `kerndos/kdresume.inc`, which stages
; the SAME picture on a machine the kernel is no longer running on
; (SPEC.md 96.49). Two stagers, one layout, and neither can call the other.
;
; It is constants only: the stub's BODY is `kernel/hbstub.inc`, so that each
; host can put it in the section it belongs in - `.modh` over there, `.text`
; over here.
; =============================================================================
%ifndef HBSTAGE_INC
%define HBSTAGE_INC 1

; --- the staging area in the text framebuffer (SPEC.md 87.5) -----------------
; B800:0000 (or B000:0000 on a Hercules primary), which vid_reboot has just
; made a linear 16KB+ of RAM that no rung of the ladder owns. The stub's CS,
; DS and SS are all this segment, and every datum it reads is at one of
; these offsets - numeric, so the stub is position-independent.
HS_CODE     equ 0x0000          ; the stub, up to HS_CODE_MAX bytes
HS_CODE_MAX equ 0x0400
HS_UNIT     equ 0x0400          ; dw the int 13h drive
HS_SPT      equ 0x0402          ; dw sectors per track
HS_HEADS    equ 0x0404          ; dw heads
HS_NX       equ 0x0406          ; dw extents
HS_WAKE     equ 0x0408          ; dd hbm_wake, off:seg
HS_MASKM    equ 0x0410          ; db the master PIC's mask before the stub
HS_MASKS    equ 0x0411          ; db ...and the slave's
HS_CLK      equ 0x0412          ; db[10] clk_sec..clk_year of the fresh boot
HS_LBA      equ 0x0420          ; dd the run's first LBA (scratch)
HS_LEFT     equ 0x0424          ; dw sectors left in this extent
HS_K        equ 0x0426          ; dw the file sector index = linear / 512
HS_N        equ 0x0428          ; dw sectors in the call being issued
HS_CHS      equ 0x042A          ; dw CX, dw DX as int 13h wants them
HS_DOSCODE  equ 0x043A          ; dw the DOS program's exit code on a handoff
                                ; return, 0xFFFF = an ordinary resume. IT NEEDS
                                ; NO STUB CODE AT ALL: hbm_wake already reads
                                ; the staging area for HS_CLK, and the stub
                                ; never writes into that segment
HS_SLAVE    equ 0x042E          ; db 1 = there is a second 8259 to mask and
                                ; put back; 0 on an XT, whose port 0xA0 is
                                ; the NMI mask register (SPEC.md 9.9.2)
HS_IVT      equ 0x0500          ; db[1024] file sectors 0..1, the IVT
HS_EXT      equ 0x0A00          ; {dd lba, dw count} x HS_XMAX
HS_XMAX     equ 1280            ; one per sector of a 640KB image at worst
HS_XSTRIDE  equ 6                ; ...and one entry's bytes
HS_STACK    equ 0x3FF0          ; the stub's stack top: under a CGA's 16KB

; **AND THE KERNEL'S OWN SEGMENT, WHICH USED TO BE AN ASSEMBLY CONSTANT.**
; The stub ends `mov ax, KERNEL_SEG / mov ds, ax` before jumping to the wake
; address, and that was fine while there was one host: `KERNEL_SEG` meant the
; kernel. It is `KD_SEG` - 0x0060 - inside `kern_dos` (kerndos/kdlayout.inc),
; so a SHARED stub assembled there would hand the resumed kernel a DS of 96
; and nothing would say so. The value is staged instead, by whoever stages the
; rest: the kernel writes its own, and `kern_dos` writes the one the launch
; block carried over. It cannot be taken from HS_WAKE either - that segment is
; the hibernate MODULE's CS (`mov [hb_wakep + 2], cs`) and not the kernel's.
HS_KSEG     equ 0x0442          ; dw the resumed kernel's KERNEL_SEG. Above
                                ; every KDS_* cell (0x0430..0x0441) so the two
                                ; stubs' maps do not alias, though they never
                                ; coexist
HS_DOSAKB   equ 0x0444          ; dw the ARENA the DOS program was given, in
                                ; KB, on a handoff return (SPEC.md 96.41.1).
                                ; HS_DOSCODE's companion and staged by whoever
                                ; stages that: `kd_resume` on the live route,
                                ; `hbm_res` out of the BDA on the reboot one -
                                ; so `hbm_wake` reads ONE pair of cells and
                                ; neither route needs a second reader

; **AND THE BIOS TICK THE KERNEL STOPPED COUNTING AT** (SPEC.md 96.49.5).
; 0xFFFFFFFF = the ten bytes at HS_CLK are ALREADY CURRENT, which is what the
; reboot route stages: a fresh boot has just run the clock ladder, and adding
; an elapsed time on top of that would count the DOS session twice. Any other
; value is 0040:006C as it stood at the handover, and `hbm_wake` subtracts it
; from the live counter to find out how long the machine was somebody else's.
;
; THE BDA IS WHY THIS WORKS AT ALL: the stub puts the IVT back and never file
; sector 2, so 0040:006C is the ROM's own and has been counting throughout -
; and SPEC.md 8.1's PIT change is a MODE change, leaving the IRQ0 rate at
; 18.2065 Hz on both sides of the handover.
HS_TICK0    equ 0x0446          ; dd, above HS_DOSAKB and below HS_IVT

%endif                          ; HBSTAGE_INC
