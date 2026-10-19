; =============================================================================
; apps/os88ub.inc - a 386's segment base, set where it is wanted
; (SPEC.md 41.15, docs/plans/VIDEO-386-PLAN.md)
;
; THE 386'S ANSWER TO THE 286'S LOADALL (apps/os88la.inc). Every segment
; register has a HIDDEN descriptor cache - a 32-bit base, a limit and an
; access byte - and the CPU addresses through the cache, not the selector. In
; real mode a load sets the base to `selector x 16`; in protected mode it
; copies a descriptor out of the GDT. So a moment of protected mode - CR0.PE
; set, DS loaded from a descriptor whose base is 0x2A0000, CR0.PE clear again
; - leaves `DS:SI` reaching 2.6 MB in real mode, with no reset and no
; borrowed table, until the next load of DS.
;
; WHY THIS AND NOT UNREAL MODE: unreal mode (SPEC.md 41.4) widens a LIMIT, so
; reaching extended memory takes 32-bit addressing - an `a32` prefix on every
; string instruction and an ESI the 16-bit code around it does not keep. A
; loaded BASE keeps the 16-bit code exactly as it is: the Video Player's
; decoder runs over a record in XMS unchanged, as it does on the 286 under
; LOADALL (SPEC.md 98.3.18.11). The limit stays 0FFFFh and the access byte
; 93h - a real-mode segment's own - so the first ordinary load afterwards
; (`push cs / pop ds`) leaves the register EXACTLY as real mode makes it,
; with no 4 GB limit left behind for anything to trip over.
;
; WRITTEN ONCE, ASSEMBLED BY EACH CALLER, os88la.inc's shape: XMEM.DRV's
; tier-2 copy (41.5), the Video Player's decode in place (98.3.18.11) and
; HDD.DRV's read straight into XMS (52.1.4).
;
; TIER 2 ONLY, and the CALLER tests: every instruction below is a 386's, in a
; `cpu 386` island closed by `cpu 8086` (SPEC.md 41.9 rule 2).
;
; THE BASE ENDS AT ANY SEGMENT LOAD, and every ISR pushes and pops DS and ES,
; so between ub_set and the caller's own reload interrupts stay off - the
; same rule as a LOADALL borrow. The caller's data is reached through CS:.
;
; THE TRANSITION ITSELF is xm_arm's (SPEC.md 41.4), which has run on every
; tier-2 attach since XMEM.DRV was written: NMI masked at the CMOS index
; port across it (a real-mode IVT is not a protected-mode IDT, so an NMI
; taken with PE set would be a triple fault), no far jump (CS keeps its
; real-mode cache, which is what keeps the next instruction reachable, and
; the CPL is 0 throughout), the GDT's linear base computed from CS at every
; call because a package region and a driver image both MOVE (SPEC.md 66).
; About 6 us on a 16 MHz 386SX, most of it the four CMOS port accesses:
; against ~150 us for a LOADALL borrow on the Victory 286.
;
; A20 MUST BE OPEN, as for LOADALL: the base goes out on the bus as it is,
; and a shut gate aliases it 1 MB down without a fault. XMEM.DRV opens the
; gate at attach and no tier-2 path ever calls the BIOS's block move, which
; is the only thing in the tree that might close it.
;
; THE GDT IS 24 BYTES THE CALLER PLACES: UB_DATA lays it out, initialised,
; wherever the caller puts the macro - in its image, because ub_set writes
; it through DS.
; =============================================================================

UB_SEL_DS   equ 0x08                ; ub_gdt's entry 1: DS's descriptor
UB_SEL_ES   equ 0x10                ; ...entry 2: ES's

; The GDT, three entries: the null descriptor's eight bytes double as the
; pseudo-descriptor lgdt reads (limit, then the 32-bit linear base), which
; the CPU never reads as a descriptor. Each of the other two: limit 0FFFFh,
; base patched per call, access 93h (present, ring 0, data, writable,
; accessed - set already, so the CPU never writes the GDT back), byte
; granular, 16-bit. A real-mode segment, in other words, at any base.
%macro UB_DATA 0
ub_gdt:     dw 23                   ; lgdt's limit: three entries
            dd 0                    ; ...its base, linear (ub_set writes it)
            dw 0
            dw 0xFFFF, 0            ; UB_SEL_DS
            db 0, 0x93, 0x00, 0
            dw 0xFFFF, 0            ; UB_SEL_ES
            db 0, 0x93, 0x00, 0
%endmacro

; -----------------------------------------------------------------------------
; ub_set - load DS's and/or ES's hidden base, through a moment of protected
; mode. TIER 2 ONLY, IF = 0 (the caller's pushf / cli, held until it has
; reloaded every register it based)
;
; WHICH REGISTERS IS THE INCLUDER'S, at assembly time: `%define UB_DS` and/or
; `%define UB_ES` before the %include. XMEM.DRV's copy wants both, the
; Video Player's decode DS alone, HDD.DRV's read ES alone - and a run-time
; mask was twenty bytes of every image for a choice no caller makes twice.
;
; in:       DS = the caller's own image (where ub_gdt is - la_borrow's rule);
;           UB_DS: DS's base = EAX; UB_ES: ES's base = EBX (linear)
; out:      the register(s) addressing from those bases, limit 0FFFFh, their
;           visible selectors UB_SEL_DS / UB_SEL_ES. So with UB_DS the
;           caller's data is reached through CS: from here on
; clobbers: the high half of EAX, and flags (the GDTR is left on ub_gdt,
;           which real mode never reads - xm_arm leaves it on its own)
; -----------------------------------------------------------------------------
%ifndef UB_DS
%ifndef UB_ES
%error "os88ub.inc: define UB_DS and/or UB_ES - which bases ub_set loads"
%endif
%endif
cpu 386
ub_set:
    push ax
    push si
%ifdef UB_DS
    mov si, ub_gdt + UB_SEL_DS
    call .base                      ; DS's descriptor at EAX
%endif
%ifdef UB_ES
    mov si, ub_gdt + UB_SEL_ES
    mov eax, ebx
    call .base                      ; ES's at EBX
%endif
    xor eax, eax
    mov ax, ds                      ; the GDT's linear base: DS x 16 + its
    shl eax, 4                      ; offset, on EVERY call (the image moves)
    add eax, ub_gdt
    mov [ub_gdt + 2], eax
    mov al, 0x80
    out 0x70, al                    ; NMI off (index 0, harmless)
    in al, 0x71
    lgdt [ub_gdt]
    mov eax, cr0
    or al, 1
    mov cr0, eax                    ; PE: protected mode, for a few
    jmp short .pm                   ; instructions (the prefetch flushed)
.pm:
%ifdef UB_DS
    mov si, UB_SEL_DS
    mov ds, si                      ; the caches loaded from the descriptors
%endif
%ifdef UB_ES
    mov si, UB_SEL_ES
    mov es, si
%endif
    and al, 0xFE
    mov cr0, eax                    ; real mode again: the caches stay
    jmp short .rm
.rm:
    xor al, al
    out 0x70, al                    ; NMI back on
    in al, 0x71
    pop si
    pop ax
    ret
.base:                              ; EAX into the descriptor at DS:SI
    mov [si + 2], ax                ; base 0-15
    shr eax, 16
    mov [si + 4], al                ; ...16-23
    mov [si + 7], ah                ; ...24-31
    ret
cpu 8086
