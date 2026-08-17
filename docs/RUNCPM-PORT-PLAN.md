# RUNCPM port plan — RunCPM 6.9 as an os8088 C package

The design record for SPEC.md §71, produced by `.claude/skills/port-to-os8088`'s scouting workflow (one source scout, three tree scouts, a planner, three adversarial reviewers, a reconciler) on 2026-08-17 and amended by the user's decisions below. `workflows/implement.js` reads this file one wave at a time. The reference source is https://github.com/MockbaTheBorg/RunCPM (MIT); it lives outside this repo and nothing from it is vendored (CONTRIBUTING.md §6).

## Summary

Port RunCPM 6.9 (CP/M 2.2 only) to os8088 as one C package, RUNCPM.O88 (+ RUNCPM.OVL if the measured size demands it), with the Z80 core and every Z80-RAM access hand-written in 8086 assembly inside the shim and everything else - _PatchCPM, BIOS, BDOS 0-40 + 230/231/248-254, the FCB/record layer, the console, the CCP glue - as C reimplemented from cpm.h/disk.h/console.h/globals.h/main.c. The structural change from RunCPM: NO worker task and NO blocking anywhere. The emulator runs on the UI task in bounded slices, re-entered through a new kernel wake event (OSAPI_WM_WAKE posts EVT_WAKE; the UI task dispatches W_ONWAKE WITHOUT the gfx lock; os88_onwake takes the lock only around the terminal flush), so file slots are always legal, the 1024-byte UI stack is the only stack, and a Z80 program waiting on a key costs zero CPU until os88_onkey pushes the key and kicks. Every blocking BDOS call (CONIN 1/6, BIOS CONIN, the line editor 10) is either 'retry the trap when a key exists' or a small state machine advanced by os88_onkey. The Z80 core's register plan, corrected from the draft: A/F in AL/AH (F in lahf layout with P/V and N fixups), BC in CX, DE in DX, HL in DI, PC in SI, Z80 SP in BP (ds: override on every stack access), BX = dispatch scratch (`xor bh,bh / mov bl,[si] / inc si / shl bx,1 / jmp [cs:bx+tab]`, no lodsb - it would clobber A), IX/IY/alt set/I/R/IFF in cs: statics, DS = the 64KB Z80 claim; DDCB/FDCB reuse the CB table through an operand pointer (documented r=6 forms only). The console is an 80x25 VT100-subset terminal model (RC_ROWS = 25, the reference geometry TE.COM is configured for) drawn damage-only through a glass shadow: gfx_scroll + one repainted row per line, a composed 1bpp band + one gfx_blit1 per changed row (font_run for short spans and as the blit1-refused fallback); in a framed window a 640-px screen shows 78 of 80 columns and CGA 17 of 25 rows, and the fullscreen latch (Decision 2) is the one mechanism that gives the full 80x25 on CGA. CP/M drives are folders A\0.. below the launch folder; the BDOS record model is whole-file-in-a-heap-claim with an 8-entry open-file table, a per-(drive,user) directory cache, write-back on F_CLOSE / warm boot; files above 65,535 bytes are refused (decided, not asked) and are not shipped on any geometry. The drive/user switch is NOT OSAPI_FILE_GOTO_Q as drafted (a package's next file call re-stands it in the instance folder via inst_vol_enter): it is a goto_q variant that also marks the instance, folded into the same wave-1 kernel work as the wake, with os88_file_goto as the fallback. The command processor is DRI's own CCP-DR.60K running on the Z80. Estimated Z80 speed is a number to MEASURE on the first core build (the draft's 0.25-0.35 MHz XT figure rested on an inner loop that could not be written); the banner prints the measured figure. Budget re-derived at cword's all-in ratio (5.9 bytes/line): 36-47KB image + ~11.5KB bss - at or past the 55,000 trigger, so the ovl_* candidates are named now and LESSONS 2's stub build happens before wave 2. Disk rules, the vm/386-runcpm machine and a debug .COM loader move to wave 2 so waves 2-4 can meet their own done_when; the CCP binary's provenance and the allapps fetch are the user's call (Decision 3).

## Decisions (the user's, 2026-08-17)

1. **Kernel change: yes, both** — add `OSAPI_WM_WAKE` / `OSAPI_WM_ONWAKE` (dispatched on the UI task WITHOUT the gfx lock; `CC_HAS_ONWAKE` in crt0; thunks) and the instance-marking variant of `OSAPI_FILE_GOTO_Q` (`os88_file_goto_q_mark`). Measured against 429 bytes left in the image rung; no budget constant is raised. The emulator has no worker.
2. **Fullscreen: yes** — thunk `os88_fullscreen` (SPEC.md §11.2's latch), bind **Alt+F** in both directions; SPEC.md §71.2 states the exception to §11.2.1 and why (a terminal owns F and Esc). The chord must be verified to reach the package with ascii 0 and a distinct scan code and not to be consumed by the kernel first.
3. **Disk content: fetch at build, ship all that fits** — nothing third-party is committed (CONTRIBUTING.md §6 rules the plan's 'commit the 2KB CCP' out); `tools/getruncpm.py` fetches RunCPM at a pinned commit (the `getstories.py` precedent), unpacks `DISK/A0.zip` and copies `CCP/CCP-DR.60K`; the runcpm disks and `apps-all.img` carry everything on the master disk that fits and is under 65,535 bytes, MBASIC and Z80ASM included; the three files above 65,535 bytes are never shipped and the disk's readme says so. `make allapps` acquires the fetch as a prerequisite (it already needs the C toolchain).
4. Intake: the user chose 'port it whole and grey what cannot carry'; the program name is inferred (RunCPM 6.9); the licence is MIT and the attribution goes into every file header carrying derived material, into the About box and into the PR body.

Everything else below was decided by the plan and stands unless a wave shows it wrong — and then this file and SPEC.md §71 are amended together.

## Authority table (surface → the RunCPM file that defines it)

- **boot banner: '  CP/M Emulator v6.9 by Marcelo Dantas', 'Built <pinned upstream commit date>' (stated deviation: __DATE__/__TIME__ would break byte-for-byte rebuilds), the dashes rule, 'CPU is 8086 native' (new CPU_IS - this core is not cpu1.h's 'Model 1'), '<n> T-states in <n> ms' (smaller magnitudes: a reduced burst, stated), 'Estimated Z80 clock speed: N.NN MHz' (stated deviation from cpu_mhz.h's integer %u, which would print '0 MHz' on the XT), 'BIOS at 0xFE00 - BDOS at 0xE400', 'BIOS/BDOS using interrupt handoff method', 'CCP CCP-DR.60K at 0xDC00', 'Unable to load CP/M CCP.' 'CPU halted.'** — RunCPM/main.c lines 66-105; RunCPM/cpu_mhz.h; RunCPM/cpu1.h:7 (CPU_IS); the three deviations pinned in the RunCPM SPEC section
- **CCPHEAD warm-boot header '\r\nRunCPM Version 6.9 (CCP-DR.60K) - CP/M 2.2\r\n', VERSION '6.9', VersionBCD 0x69, CCPname/VersionCCP/BatchFCB/CCPaddr per CCP_* block, TPASIZE 60, PAGESIZE/BIOSjmppage 0xFE00/BIOSpage 0xFF00/BDOSjmppage 0xE400/BDOSpage/DPBaddr/tmpFCB layout defines, BOOTONLY FALSE (AUTOEXEC.TXT on EVERY warm boot, from FILEBASE = the launch folder)** — RunCPM/globals.h lines 55-135, 240-270, 305; RunCPM/main.c 125-137
- **page zero, BIOS/BDOS jump pages, RST 08h/RST 10h stubs, DPB/DPH, block/extent constants (blockShift, numAllocBlocks, physicalExtentBytes, logicalExtentBytes 16*1024)** — RunCPM/cpm.h _PatchBIOS/_PatchCPM lines 158-296
- **BIOS entries BOOT..SECTRAN and their register answers (B_LIST 582 and B_AUXOUT 585 are EMPTY - no-ops here too)** — RunCPM/cpm.h _Bios lines 549-725
- **BDOS functions 0-40 and 230, 231, 248-254 (result plumbing HL -> A/B, DRV_ALLRESET, _CheckSUB), A_WRITE 4 / L_WRITE 5 -> A/0/PUN.TXT and A/0/LST.TXT, F_ATTRIB answers HL=0, and the C_READSTR line editor key map (^A ^B ^C ^E ^F ^G ^H/DEL ^J/^M ^K ^R ^U ^W ^X ^? and its help text, bell on refusal, printable 0x20-0x7E and ^Z)** — RunCPM/cpm.h _Bdos lines 738-2156, 795-818 (4/5), 1390 (F_ATTRIB), C_READSTR lines ~984-1170 (1035 DEL, 1120 ^?), F_SETMASK..F_CCPADDR lines 2038-2135; RunCPM/host.h F_BDOSCALL 231
- **console byte semantics: mask8bit 0x7F on every output byte, no translation of CR/LF/BS/BEL/TAB/ESC, _puthex8/16, _putdec** — RunCPM/console.h (133 lines)
- **BDOS disk error text '\r\nBdos Err on X: ' + 'R/O' | 'Select' | '\r\nCP/M ERR', wait for a key, warm boot** — RunCPM/disk.h _error lines 22-41
- **FCB (36 bytes) and directory entry (32 bytes) layouts; _FCBtoHostname/_HostnameToFCB name rules incl. NOSLASH; _mockupDirEntry extent/rc/al synthesis INCLUDING the multi-entry extent chain for files above one physical extent (one entry per extentsPerDirEntry, disk.h 229-247); _OpenFile/_CloseFile/_MakeFile/_SearchFirst/_SearchNext/_DeleteFile/_RenameFile/_ReadSeq/_WriteSeq/_ReadRand/_WriteRand/_GetFileSize/_SetRandom/_SetUser/_MakeDisk/_CheckSUB behaviours and return codes** — RunCPM/disk.h (894 lines); RunCPM/abstraction_posix.h typedef CPM_FCB/CPM_DIRENTRY, FOLDERCHAR, FILEBASE
- **drive/user folder layout A..P / 0..9,A..F, INFO.TXT, PUN.TXT/LST.TXT capture, AUTOEXEC.TXT, $$$.SUB reverse-record format** — RunCPM/disk.h, RunCPM/cpm.h A_WRITE/L_WRITE, RunCPM/ccp.h _ccp_readInput, RunCPM/globals.h BATCHA/AUTOEXEC
- **HALT: the shipping (non-DEBUG) build sets STATUS_EXIT silently - '::CPU HALTED::' is #ifdef DEBUG and is NOT printed here; this port toasts 'RunCPM: CPU halted' (an OS mechanism, stated as a deviation in SPEC) and self-closes** — RunCPM/cpu1.h case 0x76 lines 1687-1700 (DEBUG-guarded); RunCPM/main.c exit path
- **HostOS code (new value 0x08 for os8088, beside 0x00 Windows/0x01 Arduino/0x02 posix/0x03 DOS/0x04 Teensy/0x05 ESP32/0x06 STM32/0x07 Pico); the shipped INFO.COM maps only 0..7 and prints 'running on Unknown' for 0x08 - greyed as a fact** — RunCPM/abstraction_posix.h line 23, abstraction_windows.h line 19, abstraction_arduino.h lines 9-21; A0.zip INFO.Z80 lines 41-95
- **terminal geometry 80x25 ANSI/VT100 assumed; _clrscr = ESC[2J ESC[H; TE.COM's TE_CONF block at 0x127B says 25 rows x 80 cols** — RunCPM/abstraction_runvt.h _console_init/_clrscr; RunCPM/readme.md 'Limitations'; A0.zip TE.COM (TE_CONF)
- **CCP-DR.60K binary, its $$$.SUB FCB offset (+0x7AC), entry with C = DSKByte, load at BDOSjmppage-0x800** — RunCPM/CCP/CCP-DR.60K; RunCPM/globals.h CCP_DR block; RunCPM/main.c _RamLoad(CCPname, CCPaddr)
- **master disk contents (81 files, 875,602 bytes; the three above 65,535 bytes - Z80ASM.PDF 134,899, BDOS.ASM 67,301, ZCPR3.ASM 66,761 - are not shipped), 1STREAD.ME text, per-program credits, INFO.TXT 'Main RunCPM disk'** — RunCPM/DISK/A0.zip, RunCPM/DISK/1STREAD.ME
- **licence text and attribution ('Copyright (c) 2017 Mockba the Borg', file headers 'Copyright (c) 2016 - Marcelo Dantas')** — RunCPM/LICENSE; RunCPM/main.c, cpm.h headers
- **Z80 instruction semantics and flag results the assembly core must reproduce (the reference the ZEXDOC gate compares against)** — RunCPM/cpu1.h (4249 lines) - behaviour only, no line ports; A0.zip ZEXDOC.COM/ZEXALL.COM as the oracle
- **keyboard: '^? = Ctrl+-  and DEL = Ctrl+Backspace on this keyboard' (the help line advertises both and int 16h delivers chr 31 and 0x7F only that way); Ctrl-H/I/M fold into BS/Tab/CR (identical bytes to what CP/M expects)** — RunCPM/cpm.h 1035, 1120; kernel/keyboard scan-code path (verified by screendump in wave 3)

## Scope

### Ships

- Z80 core in 8086 assembly: every documented opcode incl. CB/ED/DD/FD/DDCB/FDCB (DDCB/FDCB through the CB table with a precomputed (IX+d) operand pointer), R register, HALT, IN/OUT as no-ops (IN answers 0), RST 08h/10h handoff at BIOSpage/BDOSpage, sliced execution with all state in package statics; register plan AL/AH=A/F, CX=BC, DX=DE, DI=HL, SI=PC, BP=Z80 SP (ds:), BX=dispatch scratch
- CP/M 2.2 memory image exactly as _PatchCPM builds it (60K layout: BDOS 0xE400, BIOS 0xFE00, CCP at 0xDC00), warm boot, DSKByte, IOByte
- BIOS: BOOT (-> STATUS_EXIT), WBOOT, CONST, CONIN, CONOUT, LIST and AUXOUT/PUNCH as no-ops (as cpm.h), READER (0x1A), HOME/SELDSK/SETTRK/SETSEC/SETDMA/READ/WRITE/LISTST/SECTRAN as in cpm.h
- BDOS 0-40 (console 1,2,6,7,8,9,10,11,12; 4/5 capture to A\0\PUN.TXT / LST.TXT; disk 13-40, F_ATTRIB answers 0 as upstream), plus RunCPM-private 230 (8-bit mask), 231 (F_BDOSCALL -> 0), 248 (uptime ms from ticks x55), 249 (MakeDisk = mkdir), 250 (HostOS = 0x08), 251/252/253, 254 (accepted, no-op)
- BDOS 10 line editor with RunCPM's full ^A..^X/^? key map and help text, as a state machine driven by os88_onkey; ^W recall buffer 256 bytes static
- Drives = folders A\0 .. P\F below the launch folder; user areas; SELDSK of an absent folder -> 'Bdos Err on X: Select'; USER n creates the folder on first write; FORMAT.COM/BDOS 249 creates a drive folder; the (drive,user) switch is one quiet goto that also moves the instance (api_gaps)
- FCB file model over whole files: 8-entry open-file table in heap claims, sequential and random read/write, extents/rc/s2 dirty bit, F_SFIRST/F_SNEXT from a 128-entry directory cache with '?' matching and the multi-entry extent chain for large files, ERA/REN/MAKE, $$$.SUB (SUBMIT.COM works), files up to 65,535 bytes
- Console: VT100/ANSI subset (CR LF BS BEL TAB FF; ESC[ H f A B C D J K m(0/7) L M s u ?25h/l) over an 80x25 model (RC_ROWS = 25; rows shown = what the window holds: 25 VGA/Hercules framed, 17 CGA framed, 25 CGA fullscreen), reverse-video attribute, cursor as an inverse cell, keys incl. arrows/Home/End/PgUp/PgDn/Del as VT sequences, ^C, 7-bit mask
- Damage-only glass: shadow, per-row diff, gfx_scroll per scrolled line, one composed band + gfx_blit1 per changed row (font_run for spans <= 8 cells and as the blit1-refused fallback), full repaint only on expose; os88_onwake holds the gfx lock only around the flush
- DRI CCP (CCP-DR.60K) as the command processor, loaded once at launch (before any folder move) into a 2KB claim and copied to 0xDC00 on every warm boot; CCPHEAD printed before each; AUTOEXEC.TXT read from the launch folder on every warm boot and poked at CCPaddr+7/+8
- EXIT.COM / BIOS BOOT: prints the RunCPM exit text, then a 10-line self-close worker (cword's File > Close idiom) closes the window; HALT toasts 'RunCPM: CPU halted' and does the same
- Boot banner with a measured 'Estimated Z80 clock speed: N.NN MHz' (16-bit tick arithmetic over a fixed burst), 'CPU is 8086 native', 'Built <pinned commit date>'
- About box (product, 6.9, MIT attribution to Mockba the Borg / Marcelo Dantas, what this port is, OK) <= 12 rows; kernel bar name 'RunCPM' via an empty menu set
- Fullscreen latch (Decision 2): a chord the Z80 side cannot want (Alt+F, scan code with ascii 0) enters/leaves WF_FULL - 80x25 on every adapter, stated in SPEC as the exception to 11.2.1 with its reason
- Disks: build/runcpm.img/720/360 (wave 2) carrying RUNCPM.O88 (+ .OVL if split) + CCP-DR.60K in the root beside A\0\; A\0 = the master-disk executables (.COM/.SUB/.TXT/.ME on 360KB; sources on 720KB/1.44MB as far as they fit, manifest listed; no file above 65,535 bytes on any geometry, the readme names the three left off); RUNCPM\ folder on apps-all.img; three geometries, each --verify'd
- vm/386-runcpm 86Box machine (wave 2); XT numbers measured on vm/xt640 (640KB) with fdd_02_fn hand-pointed for the session; xt-runcpm only if the measured speed justifies it
- Launch requirement defined by claims, not KB: os88_mem_claim(64) for Z80 RAM + the CCP claim + one file claim must succeed; the refusal sentence quotes what was asked and os88_mem_largest_kb()

### Present and greyed (SPEC.md §47 — the fact that greys it)

- **CPU throttling / BDOS 254 (F_CPUSPEED) and cpuDelayInstructions** — the emulated Z80 is slower than a real one on the target (measured MHz on the banner); there is nothing to throttle - accepted, ignored
- **files larger than 65,535 bytes (open, make, random write past 64KB)** — the os8088 file API a package reaches reads and writes whole files with a 16-bit count and no seek; BDOS answers 0xFF/error 6 and a toast names the file and '64KB'; DIR still lists such a file with its full extent chain; the three such files on the master disk are not shipped and the disk readme says so
- **more than 8 files open at once** — each open file is a heap claim; the 9th open flushes/evicts a clean entry or errors 'Bdos Err on X: CP/M ERR' with a toast 'RunCPM: 8 files already open'
- **terminal geometry in a FRAMED window: 78 of 80 columns on VGA and CGA (640 px minus the frame; Hercules' 720 px shows all 80), 17 of 25 rows on CGA (200 - bar - dock - title = 138 px)** — these are facts of the window frame, not of the display: the fullscreen latch (Decision 2) shows 80x25 on every adapter; if it is not built the greying reads 'fullscreen was not built because <the user's reason>'
- **ANSI bold/underline/colour (ESC[1m, 4m, 3x/4xm)** — one 8x8 face, no true bold, and 1bpp adapters round every colour to black/white/dither - only reverse video (7m) is rendered; others are parsed and ignored
- **host escape sequences not in the subset (VT52, DEC private modes other than ?25, scroll regions)** — RunCPM contains no terminal emulator; this port carries the subset TE/WordStar/CLS use; unknown sequences are swallowed
- **CP/M 3 (CPM3), banked memory (BANKS>1), F_MULTISEC, date stamps, DATE command** — off by default upstream and needing long/time(); this build is CP/M 2.2 (BDOS 12 answers 0x0022 as upstream does)
- **XMODEM.COM / serial transfer** — no serial path reaches a package in this OS; files are put on the floppy from a host that mounts FAT12 or copied in the Disk window
- **Z80estimateClock's uint64 method** — no 32/64-bit type; replaced by a 16-bit tick count over a fixed instruction burst - same two output lines (with two decimals and smaller T-state magnitudes, both stated), measured on this machine
- **INFO.COM's host name** — the shipped INFO.COM's table (INFO.Z80 41-95) predates HostOS 0x08 and prints 'running on Unknown' here; the binary is third-party and is not edited
- **HALT message** — '::CPU HALTED::' is a DEBUG-only string upstream; the shipping build is silent, and this port toasts the fact through the OS before closing

### Absent

- Internal C CCP (ccp.h) - host-blocking C; the DRI binary CCP is the command processor. Its extra commands (LDIR/COPY/DEL/ECHO/PAGE/POKE/VER/DUMP/VOL/CLS/?) do not exist at the prompt; PIP/DUMP/EXIT are .COM files on the master disk
- Debugger/disassembler/trace/logs (debug.h, DEBUG/iDEBUG/DEBUGLOG) - not in a shipping upstream build; ~10KB of strings
- Arduino GPIO BDOS 220-224/232, hardware IN/OUT, LED codes, SdFat, Serial console
- STREAMIO console redirection, RunVT embedding, getopt options
- ABDOS alternate BDOS
- cpu2/3/4 alternate cores - one 8086 core
- AUTOEXEC BOOTONLY variants beyond upstream's default (every warm boot); PROFILE builds
- Z80ASM.PDF, BDOS.ASM, ZCPR3.ASM (the three master-disk files above 65,535 bytes) - not shipped on any geometry; BDOS.SUB therefore cannot be run, and the disk readme says so

## Files

| file | holds | resident |
|---|---|---|
| `apps/runcpm/runcpm.c` | the one translation unit: MIT header, prototypes at top, #includes of the parts below (ALL created as stubs in wave 1 so the Makefile prerequisites are written once), os88_main (64KB Z80 claim + CCP claim loaded BEFORE any folder move, one ovl_* touched here if an overlay exists, window 640x(18+200) authored for 640x480 and clamped, wm_snap, empty menu set 'RunCPM', about_set, onwake install, launch place recorded), os88_paint (full redraw from the model on expose), os88_onkey (key ring push + line-editor advance + kick; Alt+F fullscreen chord), os88_onwake (the slice driver: run N instructions by CPU class, service the trap, drain the console, lock/flush/unlock, re-post), os88_oncmd stub, os88_about, os88_worker (self-close only), the boot state machine STATUS_RUNNING/EXIT/RESTART, banner and clock estimate, the always-compiled debug key that loads a named .COM from the launch folder to 0x100 (wave 2's loader) | yes |
| `apps/runcpm/rcterm.c` | terminal model (chars stride 80 via row<<6 + row<<4, attrs as a reverse-video BITMAP, 25 rows x 80 cols), VT100/ANSI parser, cursor, scroll count, glass shadow and the damage flush (rc_rowdiff -> font_run span or rc_band + gfx_blit1; gfx_scroll with fallback), key-to-sequence mapping table | yes |
| `apps/runcpm/rccpm.c` | _PatchCPM (ovl_ candidate), _Bios and _Bdos dispatchers minus CPM3/Arduino/DEBUG, C_READSTR as a state machine with the same key table, console helpers (mask8bit, puthex8/16, putdec), private calls 230/231/248-254; the keystroke-path functions (1,2,6,9,10,11, F_READ/F_WRITE) resident | yes |
| `apps/runcpm/rcfs.c` | REWRITTEN disk layer: place table (drive,user)->folder cluster resolved lazily by os88_file_find + the instance-marking quiet goto (fallback os88_file_goto); 128-entry directory cache per current (drive,user) with in-place updates on our own create/delete/rename; 8-entry open-file table over heap claims (open loads whole file with os88_file_read_seg, close writes back with os88_file_write_seg when dirty, regrow on growth); FCB<->name, match(), _mockupDirEntry with the multi-extent chain (two-word size math), seq/random record math in 16 bits, $$$.SUB, PUN/LST as ordinary OFT entries flushed at warm boot, _error path; F_MAKE/ERA/REN/MakeDisk/_error text are ovl_ candidates | yes |
| `apps/runcpm/rcabout.c` | the About panel (<= 12 rows, control y = 6+row*10) and its OK/Esc dismissal, shadow invalidation after it comes down; the first ovl_ candidate | overlay candidate |
| `apps/runcpm/rcz80.inc` | HAND-WRITTEN Z80 core: _rc_run(n) cdecl entry that saves DS/ES/BP, loads DS = Z80 claim, unpacks registers from cs: statics (AF->AX, BC->CX, DE->DX, HL->DI, PC->SI, SP->BP), fetch `xor bh,bh / mov bl,[si] / inc si / shl bx,1 / jmp [cs:bx+tab]`, lahf-layout F with OF->P/V and N fixups, per-prefix tables (base, CB, ED, DD, FD; DDCB/FDCB via the CB table with an operand pointer), RST 08/10 -> return with reason, HALT, slice count; ~2,500-3,500 lines | yes |
| `apps/runcpm/rcmem.inc` | the ES-loading movers, marked cc8086:allow: rc_rd(a) rc_rd16(a) rc_wr(a,v) (near, ~5us), rc_zcopy_in(zaddr, src, n), rc_zcopy_out(dst, zaddr, n), rc_zzcopy(seg,off -> zaddr / zaddr -> seg,off, n) for the 128-byte DMA and 36-byte FCB and CCP images, rc_zfill; tested by hosttest/rcmemtest (boot-sector harness, cwmovetest precedent) | yes |
| `apps/runcpm/rcband.inc` | rc_band(chars, attrbits, ncells) composes one 8-row 1bpp band from OSAPI_FONT_GLYPHS (8-aligned cells: byte copies, XOR 0xFF for reverse) into a static 80x8 band; rc_rowdiff(a, b, n) -> first/last differing cell; both called once per row | yes |
| `apps/runcpm/runcpm.asm` | the shim: CC_PKG_NAME 'RUNCPM', CC_HAS_ONKEY, CC_HAS_MENUS, CC_HAS_ABOUT, CC_HAS_ONWAKE (new), CC_HAS_WORKER, CC_HAS_OVL if split, CC_ICON, %include cc/crt0.asm, runcpm.gen.asm, runcpm/rcz80.inc, runcpm/rcmem.inc, runcpm/rcband.inc, CC_IMAGE_END | yes |
| `apps/runcpm/build.sh` | runs the host checks (rcuitest, rcfstest) before the 8086 build, cword's build.sh precedent; invoked by the make rule | yes |
| `apps/runcpm/hosttest/os88.h` | the stub header (second copy of apps/cc/os88.h's shapes without the poison) + prototypes/stubs of rc_* asm shims, os88_wm_wake/onwake, os88_file_goto_q_mark, os88_snd_tone, fullscreen; grows in every wave that adds an API | yes |
| `apps/runcpm/hosttest/rcuitest.c` | the glass model harness: cellch/pixline arrays, font_run/blit1/scroll modelled (not refused), stubs for the fs and the wake, feeds byte streams (banner, DIR listing, ESC sequences, a TE-style 25-row full-screen redraw) to the terminal, verifies model == glass, audits the shadow, prints the cost table (echo one char, one DIR row, one scrolled line, ESC[2J, full 25-row repaint, cursor-only move) | yes |
| `apps/runcpm/hosttest/rcfstest.c` | rcfs.c against a fake folder tree: FCB<->name, wildcard match, dirent synthesis for 0/127/128/16384/16385/65535/67301/134899-byte files (the last two: multi-entry chain listed, open refused), seq/random record math, OFT eviction, $$$.SUB order | yes |
| `apps/runcpm/hosttest/rcmemtest.asm + rcmemtest.sh` | boot-sector QEMU harness running rcmem.inc with SS != DS and negative controls (ES not restored) | yes |
| `tests/rczex.py + tests/rczex_ocr.py` | the Z80 core gate as top-level test drivers (the tests/*.py convention): boots build/runcpm.img in QEMU, launches RUNCPM by the two-press double-click, types ZEXDOC, reads the terminal rows off screendumps through an 8x8-glyph OCR harvested once from a known string, until 'Tests complete'; ZEXALL on demand; the measured ZEXDOC runtime written into the recipe | yes |
| `tools/getruncpm.py` | fetches MockbaTheBorg/RunCPM at a pinned commit into build/runcpm-src, unpacks DISK/A0.zip into build/runcpm-disk/A/0 (dropping the three files above 65,535 bytes, listing them), copies CCP/CCP-DR.60K unless it is committed (Decision 3); nothing third-party committed beyond that (tools/getstories.py precedent); a stamp file the disk rules and allapps depend on | yes |
| `kernel: events.inc (EVT_WAKE), ui.inc dispatch, wm.inc (W_ONWAKE word or install slot), kernel.asm API table (OSAPI_WM_WAKE, OSAPI_WM_ONWAKE, and the instance-marking variant of osapi_file_goto_q); apps/os88api.inc; apps/cc/crt0.asm (CC_HAS_ONWAKE trampoline); apps/cc/os88thunk.asm + os88.h (os88_wm_wake, os88_wm_onwake, os88_file_goto_q_mark, os88_fullscreen)` | the wake mechanism and the quiet-goto fix (Decision 1), the fullscreen thunk (Decision 2) and the dozen-line thunks; kernel headroom today: image rung 429 bytes left, footprint 1,024 spare (kernsize[big]) | yes |
| `vm/386-runcpm/86box.cfg; Makefile (CC_PACKAGE runcpm[,RUNCPM.OVL], RUNCPMSRC prereqs for every .c and .inc written once in wave 1, three disk rules + runcpm-src stamp, allapps lines, rczex, 386-runcpm target); SPEC.md new top-level section for RunCPM (cited from 70.12's neighbours); README` | machine, build, docs | yes |

## Budget

- image estimate: 42000
- bss estimate: 11500
- overlay estimate: 6000

cword all-in ratio: build/cword.bin 54,450 (35,886 resident + 18,564 .ovl) for ~8,046 compiled lines = ~5.9 bytes/line (the draft's 3.7 dropped the overlay). C: runcpm.c ~600 + rcterm.c ~600 + rccpm.c ~1,100 + rcfs.c ~1,100 + rcabout.c ~200 = ~3,600 lines = ~19-24KB. Assembly: rcz80.inc ~3,000 lines = ~7-10KB handlers (ALU handlers ~12 bytes each with the OF->P/V and N fixups) + 5 x 512-byte dispatch tables (base, CB, ED, DD, FD; DDCB/FDCB share CB) = 2.5KB -> 9.5-13KB; rcmem/rcband ~400 lines = ~1KB. crt0 runtime + thunks 5-6KB (ccsmoke alone is 3,406); string literals ~1.5KB (banner, CCPHEAD, C_READSTR help, error texts); icon 128 B. Total 36-47KB image; ~42KB is the planning figure. bss: terminal model chars 25x80 = 2,000 + attr bitmap 250; glass shadow 2,000 + 250; band 640; key ring 64 + line editor 128 + 256 recall; OFT 8 x 24 = 192; directory cache 128 x 16 = 2,048; place table 16x16 words = 512; scratch names/paths 128; About/misc ~500; DDCB operand pointer and core statics ~64 = ~11.5KB. Sum ~53.5KB of 61,440 - AT the 55,000 trigger, so LESSONS 2 applies: before wave 2, assemble the dispatch tables + stub handlers and compile stubbed rccpm/rcfs/rcterm to get os88pkg's size line and plan against it. ovl_* candidates named now (rcabout.c, banner + clock estimate, _PatchCPM, F_MAKE/ERA/REN/MakeDisk/_error text, name<->FCB conversion for search) ~6KB; the slice loop, terminal flush, BDOS 1/2/6/9/10/11 and F_READ/F_WRITE stay resident. If an overlay exists: CC_PACKAGE third argument, one ovl_ touched in os88_main before the first folder move (the .OVL resolves in the instance's folder, crt0.asm 745-747), and RUNCPM.OVL beside RUNCPM.O88 on every disk. Heap: 64KB Z80 RAM + 2KB CCP + overlay claim + up to 8 x <=64KB open files; launch = the three claims succeeding, refusal quotes os88_mem_largest_kb().

## API gaps

- **need:** a way for a package to be called back on the UI task without a user event - the emulator's slice loop, console flush and every disk-touching BDOS call run there (file slots are UI-task only; a worker's stack is 256 bytes)
  - **slot:** none exists (evq has only mouse events; no timer/tick callback; W_PAINT/W_ONKEY only on user action - and W_ONKEY is dispatched UNDER the gfx lock at kernel/ui.inc:107, so it cannot carry the wake)
  - **action:** add to the kernel: EVT_WAKE record {type, a = win ptr}; OSAPI_WM_WAKE (BX = win; evq_push, ISR/worker-safe, CF=1 if the queue was full) and OSAPI_WM_ONWAKE (install a near proc, like WM_ONMOUSEUP; one word in the window record or the instance record); the UI task's event loop dispatches it through the package dispatcher WITHOUT the gfx lock. crt0.asm CC_HAS_ONWAKE trampoline; os88.h/os88thunk.asm: void os88_wm_onwake(void *win); int os88_wm_wake(void *win). ~100-150 kernel bytes against a measured 429 left in the image rung and 1,024 footprint spare - fits without a rung crossing if it lands in .text. This is Decision 1
- **need:** switch between drive/user folders on every BDOS call without a remount, listing, sort or icon harvest - AND have the next file call resolve there
  - **slot:** OSAPI_FILE_GOTO_Q (os88api.inc:2227) moves the machine but deliberately not the instance (kernel.asm ~2977, SPEC 19.2.2), and every package file cell runs inst_vol_enter first (kernel.asm 2243/2328/2446), which re-stands the machine in the instance's folder - so goto_q alone is undone by the next read
  - **action:** fold into the wave-1 kernel work: a variant (a flag bit in BH, or a second slot) that does the quiet stand and then inst_vol_mark, thunked as os88_file_goto_q_mark(unsigned clus, int vol); FILE_FIND then fills the directory cache in the same folder. Fallback if the kernel change is refused: os88_file_goto (already thunked) once per (drive,user) change - a full dsk_chdir per switch, restated in the risks. Either way the CCP load and any overlay load happen before the first move, and the launch place is recorded
- **need:** the kernel 8x8 glyph table for composing a row band
  - **slot:** OSAPI_FONT_GLYPHS (os88api.inc:1222) - not thunked
  - **action:** used directly from rcband.inc in the shim's assembly (DX:SI); no os88.h change
- **need:** byte and block access to the 64KB Z80 RAM claim from C
  - **slot:** os88_peek/os88_poke exist but are far calls (~47us) - a program, not a peek
  - **action:** write it in the package: rcmem.inc near cdecl accessors and ES-loading movers marked cc8086:allow, tested by a boot-sector harness (cwmove precedent)
- **need:** the whole 80x25 on every adapter (CGA framed shows 17 rows, 640-px framed shows 78 columns)
  - **slot:** OSAPI_FULLSCREEN (os88api.inc:315, SPEC 11.2 - the WF_FULL window latch, not the 53 bracket) - not thunked
  - **action:** Decision 2: thunk int os88_fullscreen(void *win, int enter) (a dozen lines; caller holds the gfx lock, which a callback does) and bind a chord the Z80 side cannot want (Alt+F) in both directions, stated in SPEC as the exception to 11.2.1; or grey with the framed-window fact and the user's reason
- **need:** files larger than 65,535 bytes
  - **slot:** OSAPI_FILE_READ_AT (os88api.inc:2208) exists, not thunked; writes have no counterpart
  - **action:** decided: grey the feature (BDOS error + toast fact), ship no such file, list the extent chain in DIR; the read-side thunk is not added
- **need:** self-close on EXIT / BIOS BOOT / HALT
  - **slot:** none (no self-close slot)
  - **action:** write it in the package: the cword idiom - a worker that sleeps 4 ticks, os88_gfx_lock, os88_wm_destroy, unlock, os88_task_alive
- **need:** terminal bell (BEL)
  - **slot:** OSAPI_SND_TONE (wrapped)
  - **action:** os88_snd_tone(880, 2, 0x40)

## Waves

### Wave 1 — Window, terminal model, glass shadow, banner, harness, the wake mechanism and the quiet-goto fix

- SPEC section written first (its own top-level section, cited from 70.12's neighbours; every number 'measured: pending', no dangling references so checkdocs passes from the first commit): the RunCPM package, the wake event, the goto_q_mark variant, the terminal geometry (80x25 model; 78/78/80 columns framed on VGA/CGA/Hercules, 17 rows CGA framed), the file model, the three banner deviations, the keyboard note (^? = Ctrl+-, DEL = Ctrl+Backspace)
- kernel: EVT_WAKE + OSAPI_WM_WAKE + OSAPI_WM_ONWAKE + UI dispatch without the lock; the instance-marking goto_q variant; crt0 CC_HAS_ONWAKE; thunks os88_wm_wake/os88_wm_onwake/os88_file_goto_q_mark (+ os88_fullscreen if Decision 2 says yes); kernsize before/after quoted against the 429/1,024 headroom; a counter in the RunCPM window proves the round trip (a wake handler that re-posts N times and shows N)
- all five .c files created (stubs where empty) with MIT + Mockba the Borg / Marcelo Dantas / v6.9 headers, and the Makefile's RUNCPMSRC and .bin %include prerequisite lines written once for every .c and .inc; apps/runcpm/build.sh; a bare build/runcpm.img rule (RUNCPM.O88 only, --verify'd) for make test TESTAPPS=
- os88_main: claims (refusal sentence quoting largest_kb), window 640x(18+200) at (7,20), wm_snap, empty menu set 'RunCPM', about_set, onwake install; os88_paint = full redraw from the model with the shadow invalidated; os88_onwake locks only around rc_flush
- rcterm.c: 25-row model (stride 80, attr bitmap), VT100 subset parser, cursor, scroll counter, shadow, damage flush (font_run spans; gfx_scroll + vacated rows); rcband.inc + blit1 path with font_run fallback when blit1 answers -1
- banner text from main.c pushed through the terminal at first paint
- hosttest/os88.h + rcuitest.c: glass model, byte-stream driver, model==glass verify, cost table skeleton, a TE-style 25-row redraw script

**Files:** `apps/runcpm/runcpm.c`, `apps/runcpm/rcterm.c`, `apps/runcpm/rccpm.c (stub)`, `apps/runcpm/rcfs.c (stub)`, `apps/runcpm/rcabout.c (stub)`, `apps/runcpm/rcband.inc`, `apps/runcpm/runcpm.asm`, `apps/runcpm/build.sh`, `apps/runcpm/hosttest/os88.h`, `apps/runcpm/hosttest/rcuitest.c`, `kernel/events.inc`, `kernel/ui.inc`, `kernel/wm.inc`, `kernel/kernel.asm`, `apps/os88api.inc`, `apps/cc/crt0.asm`, `apps/cc/os88thunk.asm`, `apps/cc/os88.h`, `Makefile`, `SPEC.md`

**Done when:** make runcpm builds and os88pkg prints the size line; kernsize shows the wake + goto_q_mark bytes inside the 429/1,024 headroom (or names the crossing for Decision 1); make test TESTAPPS=build/runcpm.img shows the RunCPM window with the banner lines in the 8x8 face; a screendump cropped at zoom 4 shows the wake counter climbing without any key; a goto_q_mark to a subfolder followed by os88_file_read of a file that exists only there succeeds; the harness prints echo-one-char = 2 calls/3 cells, one scrolled DIR row = 2 calls (scroll + blit1) or 2 calls/78 cells on the fallback, full repaint = 25 calls; the VGA screendump shows 78 columns and 25 rows, VIDEO=cga shows 17 rows and 78 columns with nothing off the content box

### Wave 2 — The Z80 core, the slice loop, the disks, the machine and the core gate

- LESSONS 2 first: assemble the dispatch tables + stub handlers with the stubbed C and read os88pkg's size line; if it projects past 55,000 resident, the ovl_* names are applied now and CC_PACKAGE gets its third argument
- rcz80.inc: full documented Z80 instruction set on the corrected register plan (AL/AH, CX, DX, DI=HL, SI=PC, BP=SP with ds:, BX dispatch), lahf-layout flags with fixups, prefixes incl. DDCB/FDCB via the CB table, R, HALT, RST 08/10 handoff returning a reason code, slice count; registers unpacked/packed in cs: statics; DS = Z80 claim inside, restored on exit
- rcmem.inc movers + rcmemtest boot-sector harness with negative controls
- os88_onwake slice driver: N instructions by os88_cpu() class (~50 ms on the target), status machine, kick from paint/key/click so a dropped wake cannot stall the machine, re-post only while running and not waiting on a key
- a minimal BDOS 2/9 and CONIN-retry path plus the always-compiled debug key that os88_file_read_seg's a named .COM from the launch folder into a scratch claim, rc_zzcopy's it to 0x100 and runs it (the loader wave 3/4 do not yet provide)
- tools/getruncpm.py + build/runcpm-src stamp; disk rules build/runcpm.img/720/360 carrying RUNCPM.O88 (+ .OVL) + CCP-DR.60K + build/runcpm-disk/A/0 (no file > 65,535 bytes), each --verify'd; vm/386-runcpm (copy of vm/386-c-word with B: image and uuid changed) + Makefile target
- tests/rczex.py + rczex_ocr.py: glyph OCR harvested from a known screendump string; ZEXDOC timed once on the first working core in QEMU and the figure written into the recipe; the banner's measured clock estimate

**Files:** `apps/runcpm/rcz80.inc`, `apps/runcpm/rcmem.inc`, `apps/runcpm/runcpm.c`, `apps/runcpm/hosttest/rcmemtest.asm`, `apps/runcpm/hosttest/rcmemtest.sh`, `tests/rczex.py`, `tests/rczex_ocr.py`, `tools/getruncpm.py`, `vm/386-runcpm/86box.cfg`, `Makefile`, `SPEC.md`

**Done when:** a hand-assembled Z80 hello (a .COM on build/runcpm.img loaded by the debug key) prints through BDOS 9; ZEXDOC loaded the same way reports every group OK in QEMU (make rczex; if the measured run is over ~15 minutes the wave gate is the hello plus a per-opcode-group host check and the full ZEXDOC becomes wave 4's gate); the banner prints an 'Estimated Z80 clock speed' line, and the number read on vm/386-runcpm and on vm/xt640 (fdd_02_fn hand-pointed at build/runcpm360.img for the session, cfg git-checkout'ed after) is recorded in SPEC; the size line after the stub build is recorded and the resident/overlay decision written down

### Wave 3 — BIOS, BDOS console side, the line editor, warm boot and the DRI CCP

- rccpm.c (MIT header): _PatchCPM, _Bios (5/6 no-ops), _Bdos dispatch, console functions 1,2,6,7,8,9,10,11,12 with the retry/state-machine design, 230/231/248-254, mask8bit
- C_READSTR state machine with the cpm.h key map and help text; ^C at an empty line warm-boots
- CCP-DR.60K loaded into a 2KB claim at launch (from the launch folder, before any folder move), copied to 0xDC00 on each warm boot; CCPHEAD printed; AUTOEXEC.TXT read from the launch folder on every warm boot and poked
- key mapping (arrows/Home/End/PgUp/PgDn/Del -> VT sequences, ^C, BkSp=8, Ctrl+- = ^?, Ctrl+Backspace = DEL) and the BEL tone; Alt+F fullscreen chord if Decision 2 says yes
- EXIT (BIOS BOOT) -> exit text -> self-close worker; HALT -> toast -> the same
- Makefile prerequisites re-checked (rccpm.c already listed from wave 1); hosttest/os88.h grows with the tone/key stubs

**Files:** `apps/runcpm/rccpm.c`, `apps/runcpm/runcpm.c`, `apps/runcpm/rcterm.c`, `apps/runcpm/hosttest/rcuitest.c`, `apps/runcpm/hosttest/os88.h`, `Makefile`

**Done when:** 'A>' prompt appears under the CCPHEAD on build/runcpm.img; typing echoes at 2 calls per character; the editor keys behave per cpm.h (^U shows '#', Ctrl+- prints the help, Ctrl+Backspace deletes) - Ctrl-letters verified to arrive as 1..26 in a screendump; DIR/TYPE answer 'NO FILE' (fs stubbed); EXIT closes the window; a keystroke round trip on vm/386-runcpm is not visibly laggy

### Wave 4 — The file system: drives, directory cache, open-file table, records

- rcfs.c (MIT header): place table via os88_file_find over the launch folder and os88_file_goto_q_mark (fallback os88_file_goto); directory cache; F_SFIRST/F_SNEXT with _mockupDirEntry incl. the multi-extent chain; F_OPEN/F_MAKE/F_CLOSE over claims (read_seg/write_seg/regrow), sequential and random reads/writes, F_SIZE/F_RANDREC, ERA/REN, F_ATTRIB = 0, USER folder creation, BDOS 249 MakeDisk, 'Bdos Err on X: Select/R/O', $$$.SUB and _CheckSUB, PUN/LST capture entries, warm-boot flush
- the >64KB and >8-open refusals with their toasts
- rcfstest.c host test incl. 67,301- and 134,899-byte dirents
- full ZEXDOC gate if wave 2 deferred it

**Files:** `apps/runcpm/rcfs.c`, `apps/runcpm/rccpm.c`, `apps/runcpm/hosttest/rcfstest.c`, `apps/runcpm/hosttest/os88.h`, `apps/runcpm/build.sh`, `Makefile`

**Done when:** on build/runcpm.img: DIR lists A\0 (the first DIR after ~N int13 calls, later ones instant); MBASIC loads and runs a typed program and SAVE/LOAD round-trip a .BAS; TE edits and saves a file; PIP copies a file and the FAT12 image opened on the host holds the bytes (walked from the directory entry, not grepped); SUBMIT runs a .SUB; ZEXDOC passes from the A> prompt (make rczex); the size line is under 55,000 resident or the overlay is on

### Wave 5 — About, greying facts, polish of the console

- rcabout.c About panel (<= 12 rows) with MIT attribution (ovl_ if split); shadow invalidation after any panel/menu/fullscreen change
- greyed facts wired: 254 no-op, 64KB refusal toast, open-file cap toast, unknown escapes swallowed, mono attribute rule, INFO.COM 'Unknown' stated
- cost pass on the terminal: coalesced scrolls, cursor folded into row draws, blit1 vs font_run threshold measured in the harness
- full VIDEO=cga and Hercules (hercshot) look at the prompt, a DIR, TE's 25-row screen (framed and fullscreen) and the About box

**Files:** `apps/runcpm/rcabout.c`, `apps/runcpm/rcterm.c`, `apps/runcpm/runcpm.c`, `apps/runcpm/hosttest/rcuitest.c`, `apps/runcpm/hosttest/os88.h`, `Makefile`

**Done when:** About shows on VGA and CGA with OK inside the panel; the harness cost table shows: echo 2 calls, DIR row 2 calls, ESC[2J 1 fill + 0 cells, TE full redraw 25 blit1 calls; a 40KB TYPE scrolls without a full repaint anywhere (counter in the harness = 1 scroll per line); TE's status line is on the glass on VGA/Hercules and on CGA fullscreen

### Wave 6 — Icon, disk curation, docs, licence re-check

- icon.inc (16 rows mask + 16 data); 360KB curation (.COM/.SUB/.TXT/.ME) and the 720KB/1.44MB manifest of what fits, listed; 1STREAD.ME + a note naming the three files left off; RUNCPM\ folder on apps-all.img with the CCP beside it (Decision 3)
- XT measurement recorded; xt-runcpm only if the measured speed justifies it
- README/SPEC numbers re-measured at the end (image, bss, ovl, calls, MHz per machine); the MIT header re-check across every file, the About box and the PR body; 1STREAD.ME's per-program credits on the disk

**Files:** `apps/runcpm/icon.inc`, `tools/getruncpm.py`, `Makefile`, `SPEC.md`, `README.md`, `apps/runcpm/*.c headers`

**Done when:** make runcpmdisk produces runcpm.img/720/360 that verify with no file over 65,535 bytes; make allapps carries RUNCPM\ and it boots to A> from that disk; make 386-runcpm boots to the A> prompt on 86Box and the banner's MHz is in SPEC; checkdocs passes

## Verification

- Host harness apps/runcpm/hosttest/rcuitest.c (clang, run first by apps/runcpm/build.sh and the make rule): stub os88.h + glass model (cellch/pixline, blit1 and scroll MODELLED not refused, scroll vacated rows filled with garbage), feed byte streams and escape sequences, assert model == glass and shadow == glass after every step, print the cost table in calls/cells/us for: echo one char, one DIR row, one scrolled line, ESC[2J, cursor-only move, TE-style 25-row redraw, full expose repaint
- Host test rcfstest.c: FCB/name/wildcard/dirent/record math against a fake folder incl. 67,301- and 134,899-byte files (chain listed, open refused), OFT eviction, $$$.SUB
- Boot-sector harness rcmemtest (QEMU) for the ES-loading movers with SS != DS and negative controls
- LESSONS 2 stub build before wave 2: dispatch tables + stub handlers + stubbed C -> os88pkg size line, recorded in SPEC with the resident/overlay decision
- make rczex (tests/rczex.py): QEMU boots build/runcpm.img, launches RUNCPM by the two-press double-click driver, sends 'ZEXDOC' + Enter via tools/qmp.py sendkey (or the debug loader key in wave 2), polls tools/shot.py crops of the terminal rows through tests/rczex_ocr.py until 'Tests complete', fails on any 'ERROR' line; runtime measured once and written into the recipe; ZEXALL on demand overnight
- QMP recipe: tools/qmp.py build/qmp.sock quit; rm -f build/qmp.sock; make test TESTAPPS=build/runcpm.img; double-click Disk B then RUNCPM; sendkey the CP/M command; tools/shot.py --crop <terminal rows> --zoom 4 before believing anything; repeat with VIDEO=cga (--screen 640x200) and VIDEO=herc HERCSEG=0x7000 + tools/hercshot.py; save into a scratch copy of the image and verify a saved file by walking its FAT12 directory entry
- Wave 1 wake/goto proof: a screendump of the wake counter climbing with no key; a read after goto_q_mark of a file that exists only in the target folder
- 86Box: vm/386-runcpm (386, B: = build/runcpm.img) - read the banner's 'Estimated Z80 clock speed', time a DIR of 37 files first and second, time TYPE of a 20KB file, watch a TE full-screen redraw for flash; then the same 360KB disk on vm/xt640 (640KB, 4.77 MHz 8088; fdd_02_fn hand-pointed for the session, cfg git-checkout'ed after) to record the XT figure before any xt-runcpm target exists
- Counters: a counter in the terminal flush (calls, cells, scrolls) toasted on demand by a debug key during development, multiplied by the table (756us/call, 900us/cell); nothing repaints more than it changed - a full repaint per keystroke or per line is a defect
- Size: os88pkg's line tracked from the first commit; frame report on every build; 55,000 resident is the split trigger; kernsize before/after the wave-1 kernel change quoted against 429/1,024

## Risks

- The wake mechanism and the goto_q_mark variant are a kernel change: kernsize[big] today shows the image rung 57,344 with 429 bytes left and the footprint 1,024 spare (2 steps) - ~100-150 bytes fit in .text without a rung crossing, but it is measured, not assumed, in wave 1; if refused (Decision 1) the design falls back to a Frotz-style worker that draws under short lock holds and a RAM-drive with write-back only at UI-task moments, which loses unsaved files on the close box
- The event queue is 16 records and drops silently when full: a dropped wake stalls the emulator until the next paint/key/click kicks it - the design re-posts from every callback and never posts twice; verify by flooding clicks while ZEXDOC runs
- Z80 speed on the XT is NOT yet a number: the draft's 0.25-0.35 MHz rested on an inner loop that could not be written (lodsb clobbers A; no free base register); the corrected fetch is 5 instructions plus a cs: prefix and (HL) accesses through DI are free but stack ops through BP pay a ds: prefix. The first core build measures it on vm/xt640 and vm/386-runcpm and the banner and SPEC state what it means (MBASIC/TE usable? Z80ASM minutes? ZEXALL not for the XT?)
- Budget is at the trigger before a line is written (36-47KB image + 11.5KB bss at cword's all-in 5.9 bytes/line); the stub build before wave 2 decides the split, and if an overlay exists it must be loaded before the first folder move because the .OVL resolves in the instance's folder
- Fullscreen (Decision 2) deviates from SPEC 11.2.1's F/Esc binding by necessity - a terminal owns both keys; the Alt+F chord must be verified to arrive with ascii 0 and a distinct scan code through the BIOS keyboard path, and it must be checked that Alt+letter is not consumed by the kernel first
- First DIR/first open of a folder is O(files) int 13h calls (~400 ms each on the XT: 37 files ~15 s once) because os88_file_find re-walks by ordinal; with the os88_file_goto fallback every (drive,user) switch is a full dsk_chdir (listing, sort, icon harvest) on top of that - the directory cache makes later searches free, but the first one must be seen on hardware and stated
- Whole-file model: F_CLOSE writes the entire file (1-3 s on the XT for a 20KB file); random-access programs that keep files open and never close lose changes only if the machine dies before warm boot - the same as CP/M with a dirty buffer, but say so
- Typing latency is a full round trip (key -> ring -> Z80 program echo -> terminal): with 50 ms slices it is under 100 ms on the XT; a slice that is too long makes the machine feel dead - size N by os88_cpu() and measure
- The Z80 F byte in lahf layout: N and P/V(overflow) need per-op fixups and X/Y undocumented bits are ignored - ZEXDOC (documented flags) is the gate; ZEXALL (undocumented) may show differences we accept and state; ZEXDOC's own runtime in QEMU is unmeasured and may push the full gate to wave 4
- No printf: every banner/estimate/hex string is built with utoa/strcpy; every out-parameter and buffer static; the C_READSTR recall buffer static not calloc
- Keyboard: Ctrl-H/I/M fold into BS/Tab/CR (identical bytes, no loss); Ctrl+Space arrives as 32; Ctrl-letters must be verified to arrive as 1..26, and Ctrl+- (^?) and Ctrl+Backspace (DEL) must be verified to arrive at all, in a wave-3 screendump
- MBASIC/Z80ASM/others on A0.zip are not MIT-licensed; shipping them is the user's call (Decision 3); the tool fetches at build so nothing third-party is committed except, if chosen, the 2KB DRI CCP
- Only one hand in the translation unit at a time; make cannot see through #include - every .c and .inc is a written prerequisite from wave 1, and stubs exist from wave 1 so no later wave adds a file the Makefile does not name
- The 720KB/1.44MB disks cannot carry all of A0.zip (875,602 bytes over 81 files) beside the package; the manifest is curated in wave 6 and what is left off is listed
