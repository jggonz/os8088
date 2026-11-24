; =============================================================================
; os8088 - drivers/joy/joypkg.inc - JOYSTICK.DRV's own header (SPEC.md 137)
;
; WHAT A PACKAGE NAMES IS NOT HERE. The class, the five verbs, the state
; record, the types and the directions (DRVC_JOY, JOYV_*, JOYS_*, JOYT_*,
; JOYD_*, JOYA_*, JOY_NONE) are in apps/os88api.inc, beside the class number,
; so a game includes apps/os88joy.inc and needs no driver header and no extra
; -I on its Makefile rule - EMS's precedent (EMSV_* live there too), and the
; one place they are written, so a mirror cannot drift.
;
; WHAT IS HERE is what the driver and its Control Panel page share (SPEC.md
; 137.5, 137.6): the settings blob's layout, the timing constants, and the
; documented names of the driver's state and routines that the page
; (drivers/joy/joyui.inc, J5) reads and calls from inside the same image.
; =============================================================================

JOY_VER      equ 0x0100         ; JOYV_INFO's DX: 1.0
JOY_PORT     equ 0x201          ; the IBM Game Control Adapter
JOY_TMO      equ 2700           ; a sample's timeout in PIT counts (838 ns):
                                ; ~2.26 ms, past a 100 kOhm stick's ~1.1 ms,
                                ; under the 4.17 ms an 8250 byte takes at
                                ; 2400 baud (SPEC.md 137.3)
JOY_TBLK     equ 48             ; PIT counts one sample block is assumed to
                                ; take when the counter reloaded inside it
                                ; and its own length cannot be known (a fast
                                ; channel-0 divisor: an FSX rate bracket, the
                                ; speaker's PCM - SPEC.md 137.10)
JOY_TSTEP    equ 512            ; ...and the longest one block can take on
                                ; any machine: a difference past it is a
                                ; reload, never time
JOY_HALF     equ 64             ; a direction is |position| >= this: half of
                                ; full travel (SPEC.md 137.3)
JOY_PROBEGAP equ 18             ; ticks between automatic re-probes while no
                                ; expected axis answers (~1 s, SPEC.md 137.10)

; --- THE SETTINGS BLOB (SPEC.md 137.5): SYSTEM.CFG key 'JS', JOYB_SZ bytes,
; resident in drv_cfg on every driver kernel (kernel/driver.inc's CFG_JBLOB).
; Read at attach and written only through OSAPI_DRV_CFG; the kernel moves the
; bytes and never reads them. A blob never written reads as zeros, so
; JOYB_VER = 0 is "the defaults", exactly as SPEC.md 51.9 intends
JOYB_VER     equ 0              ; db: JOYB_V, or the defaults stand
JOYB_TYPE    equ 1              ; db: JOYT_*
JOYB_FLAGS   equ 2              ; db: JOYF_*
JOYB_DZ      equ 3              ; db: the dead zone, percent of each half
                                ; (0..JOY_DZMAX)
JOYB_CAL     equ 4              ; JOYC_SZ bytes per axis, X1 Y1 X2 Y2:
JOYC_MIN     equ 0              ;   db: the minimum / 16 (REDUCED PRECISION:
JOYC_MAX     equ 1              ;   db: the maximum / 16  the ends only scale)
JOYC_CEN     equ 2              ;   dw: the centre, full precision; 0 = this
                                ;   axis is NOT calibrated
JOYC_SZ      equ 4
JOYB_SZ      equ JOYB_CAL + 4 * JOYC_SZ ; 20
JOYB_V       equ 1
JOYF_INVY    equ 1              ; Invert Y (both sticks' Y)
JOYF_SWAP    equ 2              ; Swap A and B: X1/Y1 and buttons 1-2 trade
                                ; places with X2/Y2 and buttons 3-4
JOY_DZDEF    equ 8              ; the default dead zone, percent
JOY_DZMAX    equ 30             ; ...and the page's ceiling

; --- THE RUNTIME CALIBRATION (joy_cal), one JCAL_SZ entry per axis ---------
JCAL_MIN     equ 0              ; dw: raw at full left / up
JCAL_CEN     equ 2              ; dw: raw at rest; 0 = none yet - the next
                                ; sample that answers becomes it, with MIN 0
                                ; and MAX twice it (SPEC.md 137.3)
JCAL_MAX     equ 4              ; dw: raw at full right / down
JCAL_DZL     equ 6              ; dw: the dead zone below the centre, counts
JCAL_DZH     equ 8              ; dw: ...and above it (joy_prep derives both)
JCAL_SZ      equ 10

; =============================================================================
; THE DRIVER'S STATE AND ROUTINES THE PAGE MAY USE (drivers/joy/joy.asm)
;
; All in the driver's own image (DS = CS on every entry), so the page reads
; and writes them as plain near data and near calls. Every routine below
; preserves every register but its documented outputs.
;
;   joy_blob   JOYB_SZ   the settings, as read at attach (defaults if none):
;                        edit JOYB_TYPE / JOYB_FLAGS / JOYB_DZ in place, then
;                        `call joy_apply`
;   joy_cal    4 x JCAL_SZ   the runtime calibration, X1 Y1 X2 Y2
;   joy_raw    4 dw      the last sample's raw counts, port order, JOY_NONE
;                        for an axis that did not fall or was not waited for
;   joy_btn    db        the last sample's buttons held, bits 0-3, port
;                        order and undecoded
;   joy_live   db        the axes answering (JOYA_*): rebuilt by a probe,
;                        an axis dropped when it times out
;   joy_st     JOYS_SIZE the last COOKED sample - apps/os88api.inc's record,
;                        what JOYV_STATE hands a package (JOYS_PRESS there
;                        is the live joy_press at the moment of the copy)
;   joy_press  db        presses not yet read, logical bits 0-3
;
;   joy_take   one sample of the expected axes (or the timed re-probe),
;              cooked into joy_st. Interrupts off throughout. No outputs
;   joy_probe  sample all four axes, full timeout: joy_live rebuilt and the
;              sample cooked. out AL = joy_live
;   joy_sample AH = the axes to wait for -> joy_raw, joy_btn; out AL = the
;              axes that fell, AH = buttons held. The bare timing, uncooked
;   joy_apply  joy_blob -> joy_cal and the expected mask; call after editing
;              joy_blob (a type, a flag, the dead zone, a reset)
;   joy_prep   re-derive one axis's dead zone: SI -> its joy_cal entry. Call
;              after writing JCAL_MIN/CEN/MAX yourself (a calibration step)
;   joy_save   joy_cal -> joy_blob's JOYB_CAL, then OSAPI_DRV_CFG writes the
;              blob (SYSTEM.CFG is written at the panel's close, 31.8)
;   joy_reset  forget the calibration: every JOYC_CEN 0, joy_apply. The
;              caller saves if it wants that kept
; =============================================================================
