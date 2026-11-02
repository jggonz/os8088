; =============================================================================
; os8088 - apps/video/vcold.inc
;
; THE kern_mega ARM's COLD PART (SPEC.md 98.3.24.1): part 3 of the arm's
; VIDEO.O88, apps/video/vcold.asm - code moved out of the image because a part
; is not counted against APP_MAX_SIZE and the arm's diag builds had outgrown
; it. It is a LINKED part, PiXEL's shape (SPEC.md 106.20): it reads and writes
; the player's own variables through DS, by the names tools/os88plink.py takes
; out of the player's map, and it reaches the player's routines back through
; the player's trampoline (vp_ptramp). video.asm's stubs far-call it:
;
;   +0 VK_SIG    dw VK_SIGVAL
;   +2 VK_IMG    dw the player's image size it was linked against: the player
;                refuses a part from another build rather than run it
;   +4 VK_NVEC   db VK_N
;   +6 VK_VEC    dw a near offset a vector, far-called with the part's CODE
;                alias for CS (in protected mode) and DS = the player's data,
;                every register its routine's contract:
;      VK_RDOOR  vp_rdoor   (98.3.24's door)
;      VK_RPOST  vp_rpost   (...and what it owes after OSAPI_FSX_RUN)
;      VK_RLINE  vp_rline   (the info card's ninth line)
;      VK_PRBEG  vp_prbeg   (VP_DIAG: the profile's claim, 98.3.22.3)
;      VK_PREND  vp_prend   (VP_DIAG: the profile written)
;      VK_RQGET  vp_rqget   (VP_RQ: the bracket's reflection counts)
; A vector a build does not carry is 0, and its stub is not assembled.
; =============================================================================
VK_SIGVAL   equ 0x4356              ; 'VC'
VK_SIG      equ 0
VK_IMG      equ 2
VK_NVEC     equ 4
VK_VEC      equ 6
VK_RDOOR    equ 0
VK_RPOST    equ 1
VK_RLINE    equ 2
VK_PRBEG    equ 3
VK_PREND    equ 4
VK_RQGET    equ 5
VK_N        equ 6
