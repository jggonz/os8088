; =============================================================================
; os8088 - apps/xtmp3/x3gran.inc - THE GRANULE RECORD AND THE BACKEND ABI
;                                   (SPEC.md 121.3, pinned)
;
; One file, included by every side of the decoder: the FRONT part
; (x3front.asm) and the frame-level parser (x3pars.inc, in the primary) that
; WRITE the record, the two reconstruction backends - STANDARD (x3std.asm)
; and XT (x3xt.asm) - that READ it, and the engine (x3eng.inc) that calls
; them. It holds EQUATES ONLY: no code, no data, no section switch, so it may
; be %included anywhere, any number of times.
;
; The split it encodes is SPEC.md 121.3's: everything tools/x3sim.py does
; BEFORE requant_i (frame sync, ID3, Xing/VBRI, side information, the
; reservoir, scalefactors, Huffman, the requantisation exponents and the
; stereo plan) is the FRONT END, written once; everything from requant_i on is
; a BACKEND's. A backend never parses a bit of the stream.
;
; Contents:
;   1. X3G_*   the granule record (what the front end hands a backend)
;   2. X3K_*   a backend part's header and verb table, X3V_* verb numbers
;   3. the verbs' register contracts, X3C_* caps, X3O_* open, X3Z_* stats,
;      X3E_* errors
;   4. X3I_*   the stream-info record (x3p_open's answer), X3R_* the
;      compressed-input ring, and the FRONT END's API (x3pars.inc's procs)
;   5. X3P_*   the FRONT part's header and its fixed data layout
; =============================================================================

%ifndef X3GRAN_INC
%define X3GRAN_INC

X3G_ABI     equ 1                   ; bumped by any change to sections 1-3

; =============================================================================
; 1. THE GRANULE RECORD (X3G_*) - one granule, both channels
; =============================================================================
;
; It lives in the FRONT part's data segment (section 5) and a backend is
; handed it by far pointer (GRANULE's ES:SI). It is WRITTEN by x3p_gran and
; READ by a backend's GRANULE; nothing else writes it, and a backend never
; writes it. Little-endian words; every line array is in BITSTREAM order
; (a short block's three windows NOT interleaved - the backend reorders, from
; X3G_SFBS below, so it carries no band table of its own).
;
; THE HEADER
X3G_VER     equ 0       ; db  1, 2 or 25 (MPEG-1, -2, -2.5)
X3G_SRI     equ 1       ; db  sample-rate index 0..2 of that version
X3G_NCH     equ 2       ; db  THIS FRAME's channels, 1 or 2 (it may differ
                        ;     from the stream's first frame's: the backend
                        ;     ADAPTS - a mono frame feeds both outputs, a
                        ;     stereo one is downmixed for mono output)
X3G_MODE    equ 3       ; db  0 stereo, 1 joint, 2 dual, 3 mono
X3G_MEXT    equ 4       ; db  mode extension: bit 1 M/S, bit 0 intensity
X3G_NGR     equ 5       ; db  granules a frame: 2 (MPEG-1) or 1
X3G_GR      equ 6       ; db  this granule's index in its frame
X3G_PLAN    equ 7       ; db  the stereo processing (x3sim Decoder.granule):
X3G_PLAN_LR equ 0       ;       none - two independent channels (also every
                        ;       granule in which a channel is not ok)
X3G_PLAN_MS equ 1       ;       every line M/S (joint, M/S, no intensity)
X3G_PLAN_PL equ 2       ;       X3G_PL below, line by line (intensity)
X3G_ISSC    equ 8       ; db  MPEG-2 intensity_scale (the right channel's
                        ;     scalefac_compress bit 0); 0 on MPEG-1
X3G_MS      equ 9       ; db  1 = the frame is M/S (mode 1, mext bit 1): an
                        ;     intensity line's factors are then x sqrt 2,
                        ;     and every q below already carries its -2
X3G_RATE    equ 10      ; dw  the sample rate, Hz
X3G_FRAME   equ 12      ; dd  frames staged since x3p_start (diagnostics)
X3G_SFBS    equ 16      ; 13 db  the short scalefactor-band WIDTHS (lines a
                        ;     window) of this version and rate - what a
                        ;     backend reorders a short block with:
                        ;       first = 3 if mixed else 0, start = 36 if
                        ;       mixed else 0, sc[] their running sums;
                        ;       subband-order line start + 3(sc[s]-sc[first])
                        ;       + w*width[s] + i  <-  the next bitstream line
                        ;     for s = first..12, i < width[s], w = 0..2
                        ;     (x3sim.reorder_index, exactly)
X3G_HDR     equ 32      ; header bytes; the channels follow
;
; PER CHANNEL: X3G_CH0 and X3G_CH0 + X3G_CSZ
X3G_C_OK    equ 0       ; db  1 = decoded. 0 = this channel's granule is
                        ;     SILENCE (a dropped frame, a short reservoir, a
                        ;     bad granule): a zero spectrum through the
                        ;     filterbank with the history kept, its block
                        ;     the previous granule's continuation (x3sim
                        ;     _silent_gi). Every other field is then stale
X3G_C_BT    equ 1       ; db  block_type 0..3
X3G_C_MIX   equ 2       ; db  mixed_block_flag (0 without window switching)
X3G_C_WS    equ 3       ; db  window_switching_flag
X3G_C_NZ    equ 4       ; dw  the last NON-ZERO line + 1 (bitstream order):
                        ;     every ix at or past it is 0, and q is defined
                        ;     only below it
X3G_C_IX    equ 6       ; 576 dw  the quantised values, signed, |ix| <= 8206
X3G_C_Q     equ 6 + 1152 ; 576 dw  the requantisation exponent of each line
                        ;     in QUARTER powers of two, signed, exactly
                        ;     x3sim.quarter_exponents(): global_gain - 210
                        ;     - 8 subblock_gain - (2|4)(sf + pretab), less 2
                        ;     more when X3G_MS (the 1/sqrt 2 of M/S)
X3G_CSZ     equ 6 + 2304 ; 2310 bytes a channel
X3G_CH0     equ X3G_HDR
X3G_CH1     equ X3G_CH0 + X3G_CSZ
;
; THE STEREO PLAN (meaningful only when X3G_PLAN = X3G_PLAN_PL)
X3G_PL      equ X3G_CH1 + X3G_CSZ   ; 576 db, bitstream order, decided from
                        ;     the RIGHT channel (x3sim.stereo_plan):
X3G_PL_LR   equ 0       ;       left/right as decoded
X3G_PL_MS   equ 1       ;       M/S: L' = L + R, R' = L - R
X3G_PL_IS   equ 0x80    ;       80h + p: intensity, position code p (MPEG-1
                        ;       is_pos 0..6; MPEG-2 is_pos 0..15, with
                        ;       X3G_ISSC): L' = L kl, R' = L kr (x3tables'
                        ;       IS tables, x sqrt 2 when X3G_MS)
X3G_SIZE    equ X3G_PL + 576        ; 5,228 bytes

; =============================================================================
; 2. A BACKEND PART: ITS HEADER AND VERB TABLE (X3K_*)
; =============================================================================
;
; A backend is an OP_SEG part (SPEC.md 20.12) whose first 18 bytes are:
X3K_SIG     equ 0       ; db 'X3'
X3K_ABI     equ 2       ; db X3G_ABI
X3K_KIND    equ 3       ; db X3K_STD / X3K_XT (X3K_FRONT is the FRONT part,
X3K_STD     equ 0       ;    section 5)
X3K_XT      equ 1
X3K_FRONT   equ 2
X3K_NV      equ 4       ; db verbs in the table (X3V_COUNT)
X3K_FLAGS   equ 5       ; db 0
X3K_VEC     equ 6       ; dw near offsets INSIDE THE PART, one per verb, in
X3K_SIZE    equ 18      ;    X3V_* order; each verb ends in RETF
X3V_CAPS    equ 0
X3V_OPEN    equ 1
X3V_RESET   equ 2
X3V_GRANULE equ 3
X3V_STATS   equ 4
X3V_CLOSE   equ 5
X3V_COUNT   equ 6
X3K_SIGVAL  equ 0x3358  ; 'X','3' as a word
;
; CALLING A VERB (the engine does this; apps/pixel's px_pcall is the shape):
;   1. AX = op_seg(part)  - the part's DATA segment; check [X3K_SIG],
;      [X3K_ABI] = X3G_ABI, [X3K_NV] >= the verb, before the first call;
;   2. the far vector = (word [X3K_VEC + 2 * verb] of that segment,
;      op_cseg(part)) - the CODE selector (op_cseg, OP_WANT_CSEG: on every
;      real-mode kernel the same word as op_seg; under kern_mega an alias);
;   3. DS = the part's DATA segment (step 1) for the call, then the registers
;      below, then `call far`. Restore your own DS after.
;
; EVERY VERB:
;   in   DS = the backend's own data segment (op_seg of its part)
;   out  CF = 0 success; CF = 1 and AX = X3E_* failure
;   keeps every register but AX, the documented outputs and the flags -
;        BX CX DX SI DI BP DS ES SS - and DF clear (a verb may cld and leaves
;        DF clear)
;   ...and it: NEVER calls the kernel (a part may not call the ES-fenced
;        slots, SPEC.md 20.12.5, and GRANULE runs on the WORKER, which may
;        touch neither a file nor the heap, SPEC.md 20.6 rule 7); NEVER
;        masks interrupts; uses at most X3_VSTACK bytes of the CALLER's stack,
;        the far call's own four included (a worker's stack is 384 bytes and
;        interrupts take up to ~80 of them, docs/KERNEL-MEMORY.md "Task
;        stacks"); and keeps NO far pointer across calls except the work
;        segments OPEN was handed (the package region MOVES, SPEC.md 66)
X3_VSTACK   equ 64
X3_MAXSEG   equ 6       ; work segments a backend may ask for

; =============================================================================
; 3. THE VERBS
; =============================================================================
;
; CAPS     what a profile needs. UI task.
;   in   AL = profile (STANDARD: 0 only; XT: X3XP_* 0 Standard, 1 Fast,
;        2 Turbo, 3 Speech), AH = divisor 1/2/4, CL = output channels 1/2,
;        ES:DI -> an X3C_SIZE caps record (the caller's)
;   out  CF = 0: the record filled. CF = 1 AX = X3E_PROFILE / X3E_DIV / X3E_CHAN
X3C_OFFER   equ 0       ; db  bit n = profile n is offered by this part
X3C_NSEG    equ 1       ; db  work segments wanted, 0..X3_MAXSEG
X3C_PARA    equ 2       ; 6 dw  paragraphs of each (1..4096): the caller
                        ;     claims each as its own block (OSAPI_MEM_CLAIM,
                        ;     in KB rounded up) and passes the segments to OPEN
X3C_TKB     equ 14      ; dw  KB of those that are EXPANDED TABLES (the rest
                        ;     is state) - for the Status line and 121.10's sum
X3C_PARTKB  equ 16      ; dw  KB of the part itself, as claimed (its tables
                        ;     and state when it carries them inside)
X3C_SIZE    equ 18
;
; OPEN     start a stream on this backend. UI task (expanding a table pack
;          and checking it may take ~1 s on an 8088, SPEC.md 121.10).
;   in   ES:DI -> an X3O_SIZE open record (the caller's)
;   out  CF = 0 ready, history clear (OPEN implies RESET). CF = 1 AX =
;        X3E_PROFILE / X3E_DIV / X3E_CHAN / X3E_FMT / X3E_MEM (a segment
;        missing or the count short) / X3E_TABLE (a table's check failed) /
;        X3E_RECORD (version or rate index out of range)
X3O_PROF    equ 0       ; db  profile, as CAPS
X3O_DIV     equ 1       ; db  rate divisor 1, 2 or 4: subbands at and above
                        ;     32/div are not reconstructed (SPEC.md 121.4)
X3O_OCH     equ 2       ; db  OUTPUT channels: 1 or 2. 1 on a stereo source
                        ;     is the MONO DOWNMIX - one synthesis of the
                        ;     averaged subband samples (x3sim mono=True); 2
                        ;     on a mono source is refused (X3E_CHAN): the
                        ;     feed duplicates, the decoder does not
X3O_FMT     equ 3       ; db  PCM format GRANULE writes:
X3_FMT_S16  equ 0       ;       int16 little-endian, signed, Q15 - the
                        ;       model's own output, interleaved L R
X3_FMT_U8   equ 1       ;       8-bit unsigned: the int16's HIGH BYTE XOR
                        ;       80h (truncation, never rounded), interleaved
X3O_SCH     equ 4       ; db  the source's channels (its first frame's)
X3O_VER     equ 5       ; db  1, 2 or 25
X3O_SRI     equ 6       ; db  rate index 0..2
X3O_RES1    equ 7       ; db  0
X3O_GAIN    equ 8       ; dw  0 = unity, the bit-exact model; otherwise a Q8
                        ;     gain (256 = unity) applied to each int16 sample
                        ;     AFTER the clip, saturating (the knob's digital
                        ;     gain where there is no mixer, SPEC.md 121.8);
                        ;     it moves no clip count
X3O_NSEG    equ 10      ; db  segments below (CAPS' X3C_NSEG)
X3O_RES2    equ 11      ; db  0
X3O_SEG     equ 12      ; 6 dw  the work segments, in CAPS' order
X3O_SIZE    equ 24
;
; RESET    clear every history: overlap, synthesis ring, the block-type
;          continuation. The FRONT END's reservoir is x3p_reset's (section 4).
;          Used after a seek, a calibration probe or a rung change.
;   in   -   out CF = 0
;
; GRANULE  ONE granule of the record into PCM. Any task (the worker's).
;   in   ES:SI -> the granule record (x3p_gran's DX:AX)
;        DX:DI -> the PCM destination: 576/div sample FRAMES x X3O_OCH x
;                 (2 bytes S16 / 1 byte U8); it must not cross DX's 64 KB
;   out  CF = 0: CX = sample frames written (576 / div), AX = output samples
;        CLIPPED in this granule (counted before any gain). CF = 1 AX =
;        X3E_NOTOPEN / X3E_RECORD (a record this OPEN cannot take: another
;        version or rate). The record is not written; DI is not advanced
;
; STATS    the counters since OPEN.
;   in   ES:DI -> an X3Z_SIZE record (the caller's)   out CF = 0, filled
X3Z_GRAN    equ 0       ; dd  granules decoded
X3Z_CLIP    equ 4       ; dd  output samples clipped (x3sim OPS.clip)
X3Z_SAT     equ 8       ; dd  saturations, every stage (sum of OPS.sat)
X3Z_STAGE   equ 12      ; 8 dw  saturations by stage, saturating at FFFFh:
                        ;     requant stereo alias scale imdct mix dct window
                        ;     (x3sim STAGES' order; an XT backend may report
                        ;     its own stages here and says which)
X3Z_SILENT  equ 28      ; dd  channel-granules decoded as silence (ok = 0)
X3Z_SIZE    equ 32
;
; CLOSE    nothing to free: the caller owns every claim.   in -  out CF = 0
;
X3E_PROFILE equ 1       ; the profile is not offered
X3E_DIV     equ 2       ; the divisor is not 1, 2 or 4 (or not this profile's)
X3E_CHAN    equ 3       ; the output channels cannot be made
X3E_FMT     equ 4       ; the PCM format is not offered
X3E_MEM     equ 5       ; a work segment is missing or too small
X3E_TABLE   equ 6       ; a table's check failed (a damaged package)
X3E_RECORD  equ 7       ; a record field out of range
X3E_NOTOPEN equ 8       ; GRANULE before OPEN
X3E_ABI     equ 9       ; the part's header is not this X3G_ABI's

; =============================================================================
; 4. THE FRONT END'S API (x3pars.inc: near procs IN THE PRIMARY)
; =============================================================================
;
; The engine calls these with DS = the package's own segment, like any other
; proc of the primary. They keep every register but their outputs, AX and the
; flags, unless they say otherwise. Frame-level work runs here; granule-level
; work (scalefactors, Huffman, exponents, the plan) is the FRONT part's
; (section 5), which x3p_gran far-calls.
;
; --- the stream-info record: x3p_open's answer (in the caller's segment) ---
X3I_FSIZE   equ 0       ; dd  the file's bytes - the CALLER sets it
X3I_WANT    equ 4       ; dd  x3p_open's request: the file offset it needs...
X3I_WANTN   equ 8       ; dw  ...and at least this many bytes from there
                        ;     (fewer only where the file ends)
X3I_STAGE   equ 10      ; db  (x3p_open's own)
X3I_REF     equ 11      ; db  0, or the REFUSAL, by name (SPEC.md 121.2):
X3REF_FREE  equ 1       ;       'FREE FORMAT'
X3REF_L1    equ 2       ;       'LAYER I'
X3REF_L2    equ 3       ;       'LAYER II'
X3REF_NOSYN equ 4       ;       'NO MPEG AUDIO'  (x3p_reftext: AL -> SI)
X3I_START   equ 12      ; dd  the first byte after the ID3v2 tags
X3I_AEND    equ 16      ; dd  the end of the audio: the file's end, or the
                        ;     ID3v1 tag's start
X3I_FIRST   equ 20      ; dd  the first frame (the Xing/Info/VBRI frame when
                        ;     X3IF_TAGF)
X3I_AUDIO   equ 24      ; dd  the first AUDIO frame: where x3p_start's ring
                        ;     must begin
X3I_HDR     equ 28      ; 4 db the first frame's header, as in the file
X3I_VER     equ 32      ; db  1, 2, 25
X3I_SRI     equ 33      ; db  rate index
X3I_NCH     equ 34      ; db  channels of the first frame
X3I_FLAGS   equ 35      ; db  X3IF_*
X3IF_EXACT  equ 0x01    ;       the duration is exact (a frame count)
X3IF_TAGF   equ 0x02    ;       the first frame is metadata, not audio
X3IF_TOC    equ 0x04    ;       X3I_TOC holds a Xing table of contents
X3IF_LAME   equ 0x08    ;       X3I_DELAY / X3I_PAD are a LAME tag's
X3IF_V1     equ 0x10    ;       an ID3v1 tag ends the file
X3IF_VBRI   equ 0x20    ;       the tag frame is VBRI (else Xing/Info)
X3I_RATE    equ 36      ; dw  Hz
X3I_KBPS    equ 38      ; dw  the first frame's bitrate
X3I_FRAMES  equ 40      ; dd  audio frames (Xing/Info/VBRI), 0 = unknown
X3I_BYTES   equ 44      ; dd  Xing's byte count, 0 = absent
X3I_DELAY   equ 48      ; dw  encoder delay (LAME tag / VBRI)
X3I_PAD     equ 50      ; dw  padding (LAME tag)
X3I_DURMS   equ 52      ; dd  the duration in milliseconds (floor): frames x
                        ;     576 x granules / rate, or (AEND - FIRST) x 8 /
                        ;     kbps - x3sim's two formulas
X3I_DUR     equ 56      ; dw  ...in seconds, rounded half up (the queue's)
X3I_SKIP    equ 58      ; dd  bytes skipped before the first frame
X3I_SCANP   equ 62      ; dd  (x3p_open's own)
X3I_NTAG    equ 66      ; db  (x3p_open's own)
X3I_RES     equ 67      ; db  0
X3I_TITLE   equ 68      ; 41 db  TIT2/TT2, else ID3v1's: printable ASCII,
X3I_ARTIST  equ 109     ; 41 db  ...TPE1/TP1; NUL-ended, '' when none
X3I_TOC     equ 150     ; 100 db  the Xing TOC (X3IF_TOC)
X3I_SIZE    equ 256
;
X3_FIRST_READ equ 4096  ; ID3v2 titles are looked for in the file's first
X3_TITLE    equ 40      ; 4,096 bytes only; a title is at most 40 chars
X3_RESERVOIR equ 4096   ; main data held, at most (only the last 511 can be
                        ; reached: main_data_begin is 9 bits)
;
; x3p_inew   zero an info record and set its size. Any task.
;   in   DI -> X3I record, DX:AX = the file's bytes
;
; x3p_open   THE OPEN, as a state machine that never reads a file itself:
;            the caller reads, the machine asks. Pure (no FRONT state), so it
;            may run on the UI task while another stream plays.
;   in   DI -> the X3I record (x3p_inew first, then every answer)
;        ES:BX -> file bytes, CX = how many (<= 32768), DX:AX = the file
;        offset of ES:BX. The first call must give offset 0 and at least
;        min(file, X3_FIRST_READ) bytes; later calls must cover the request
;        [X3I_WANT, + X3I_WANTN) (a buffer that does not is asked again)
;   out  CF = 0 AX = X3P_DONE: the record is complete - or X3I_REF says why
;        the stream is refused; AX = X3P_NEED: read [X3I_WANT, + X3I_WANTN)
;        and call again
X3P_DONE    equ 0
X3P_NEED    equ 1
X3P_MORE    equ 2       ; (x3p_next)
X3P_END     equ 3       ; (x3p_next)
;
; x3p_probe  title and duration for the QUEUE: x3p_open over the file's
;            bytes. UI task only (it reads the file and claims a transient
;            buffer of max(4 KB, 2 clusters), freed before it returns; it
;            moves the instance and puts it back).
;   in   SI -> a NUL 8.3 name, BL = its volume, DX = its folder's cluster,
;        DI -> an X3I record
;   out  CF = 0 the record complete (X3I_REF may refuse); CF = 1 AX =
;        FERR_* or X3E_MEM: nothing could be read
;
; x3p_start  begin DECODING an opened stream: the FRONT part's stream state
;            from the record, the reservoir and the counters cleared.
;   in   DI -> the X3I record (not refused), SI -> the X3R ring, whose
;        producer must deliver the file from X3I_AUDIO onward
;   out  CF = 0
;
; x3p_next   stage the NEXT FRAME from the ring: sync, header, side
;            information, the reservoir, the verdict. Any task (the worker's).
;   in   SI -> the X3R ring
;   out  CF = 0: AL = its granules (1 or 2), AH = its verdict:
X3V_OK      equ 0       ;       decoded
X3V_RES     equ 1       ;       reservoir: main_data_begin past the bytes held
                        ;       (the first frames after an open or a seek):
                        ;       its granules are silence
X3V_BAD     equ 2       ;       badside: dropped, its granules are silence
;        CF = 1: AX = X3P_MORE - not enough bytes and no X3R_EOF: feed the
;                ring and call again (nothing was lost; a scan in progress
;                is remembered); X3P_END - the stream is over (the ring is at
;                its EOF with no frame left, or the last frame is TRUNCATED,
;                which is counted and produces nothing)
;
; x3p_gran   the record for granule AL (0, then 1 on MPEG-1) of the frame
;            x3p_next staged - in that order, once each. Any task.
;   in   AL = the granule   out  CF = 0, DX:AX = the record (DX = the FRONT
;        part's data segment, which the backend's GRANULE takes as ES:SI)
;
; x3p_reset  after a SEEK: the reservoir emptied, the next x3p_next scans
;            from the ring's read position for an agreeing pair (not counted
;            as a resync). Keeps the stream's identity.
;   in   SI -> the X3R ring, already positioned and refilled by the engine
;
; x3p_seekpos where to start reading for a time.
;   in   DI -> the X3I record, DX:AX = milliseconds from the start
;   out  DX:AX = the file offset (an X3I_AUDIO .. X3I_AEND byte; x3p_reset
;        then finds the frame), CX = the frame index there (saturating).
;        The Xing TOC when X3IF_TOC, frame count interpolation when only a
;        count is known, CBR arithmetic otherwise
;
; x3p_counts the verdict counters (X3PC_*) since x3p_start.
;   in   ES:DI -> X3PC_SIZE bytes (the caller's)
;
; x3p_reftext  AL = X3REF_* -> SI = its NUL name (in the primary), CF = 1
;            for 0
;
; --- THE COMPRESSED-INPUT RING (the engine's; in the caller's segment) ---
X3R_SEG     equ 0       ; dw  the ring buffer's segment
X3R_MASK    equ 2       ; dw  its size - 1; the size is a power of two,
                        ;     4,096 .. 32,768 (a frame needs at most 1,445)
X3R_RD      equ 4       ; dw  bytes CONSUMED, free-running mod 65536: the
                        ;     parser's, and only the parser writes it
X3R_WR      equ 6       ; dw  bytes DELIVERED, free-running: the producer
                        ;     writes the bytes first and this word LAST
                        ;     (never more than the size ahead of X3R_RD)
X3R_EOF     equ 8       ; db  1 = the producer has delivered the audio's
                        ;     last byte (X3I_AEND) and will deliver no more
X3R_RES     equ 9       ; db  0
X3R_POS     equ 10      ; dd  the file offset of the byte at X3R_RD: the
                        ;     parser advances it with X3R_RD
X3R_SIZE    equ 14
;
; --- THE VERDICT COUNTERS (x3p_counts; x3sim Stream.counts) ---
X3PC_FRAMES equ 0       ; dd  frames staged (truncated not included)
X3PC_SHORT  equ 4       ; dd  'reservoir'
X3PC_BAD    equ 8       ; dd  'badside'
X3PC_TRUNC  equ 12      ; dd  'truncated'
X3PC_CHG    equ 16      ; dd  'changed' (channel count differs)
X3PC_BADGR  equ 20      ; dd  'badgranule' (granules with a channel bad)
X3PC_RESYNC equ 24      ; dd  'resync'
X3PC_SKIP   equ 28      ; dd  'skipped' bytes (garbage, before and between)
X3PC_REACH  equ 32      ; dw  'maxreach': the largest main_data_begin used
X3PC_SIZE   equ 34

; =============================================================================
; 5. THE FRONT PART (x3front.asm): its header and its FIXED data layout
; =============================================================================
;
; An OP_SEG part, loaded eagerly, assembled at org 0. x3pars.inc (the
; primary) runs its frame-level code with DS = the package's segment and ES =
; THIS part's data segment, and reads and writes the fields below at these
; offsets, so they are fixed here (x3front.asm reserves them with `times`
; before its code). The record (section 1) is at X3P_REC. The FRONT part's
; own verbs (GRAN, CHECK) run with DS = this part's data segment.
X3P_SIG     equ 0       ; db 'X3', X3G_ABI, X3K_FRONT, verbs, 0 - as X3K_*
X3P_VGRAN   equ X3K_VEC ; dw  GRAN: the granule-level decode (below)
X3P_VCHECK  equ X3K_VEC + 2 ; dw  CHECK: the part's tables against their CRC
X3P_VNEXT   equ X3K_VEC + 4 ; dw  NEXT: x3p_next's frame work (x3pnext.inc)
X3P_NV      equ 3
X3P_DATA    equ 0x0010  ; the data block starts here
X3P_REC     equ X3P_DATA                    ; the granule record
X3P_CNT     equ X3P_REC + X3G_SIZE          ; the verdict counters
X3P_ST      equ X3P_CNT + X3PC_SIZE         ; THE STAGED FRAME:
X3P_S_VER   equ X3P_ST + 0  ; db  version of the stream (the lock)
X3P_S_LAYER equ X3P_ST + 1  ; db  3
X3P_S_SRI   equ X3P_ST + 2  ; db  rate index
X3P_S_NCH0  equ X3P_ST + 3  ; db  the first frame's channels
X3P_S_NCH   equ X3P_ST + 4  ; db  THIS frame's channels
X3P_S_MODE  equ X3P_ST + 5  ; db
X3P_S_MEXT  equ X3P_ST + 6  ; db
X3P_S_CRC   equ X3P_ST + 7  ; db  1 = a CRC word follows the header
X3P_S_NGR   equ X3P_ST + 8  ; db
X3P_S_VERD  equ X3P_ST + 9  ; db  X3V_* of the staged frame
X3P_S_SCAN  equ X3P_ST + 10 ; db  1 = a resync scan is in progress
X3P_S_SEEK  equ X3P_ST + 11 ; db  1 = the next frame is found by a scan
X3P_S_RATE  equ X3P_ST + 12 ; dw
X3P_S_SIZE  equ X3P_ST + 14 ; dw  this frame's bytes
X3P_S_MDB   equ X3P_ST + 16 ; dw  main_data_begin
X3P_S_MAIN  equ X3P_ST + 18 ; dw  this frame's main-data bytes
X3P_S_HELD  equ X3P_ST + 20 ; dw  reservoir bytes held (<= X3_RESERVOIR)
X3P_S_TAIL  equ X3P_ST + 22 ; dw  of them, in X3P_MD's tail (<= 511)
X3P_S_BITP  equ X3P_ST + 24 ; dw  the bit reader: position in the md, bits
X3P_S_BITN  equ X3P_ST + 26 ; dw  ...and the md's length, bits
X3P_S_MD0   equ X3P_ST + 28 ; dw  the md's first byte (an X3P_MD offset)
X3P_S_FRAME equ X3P_ST + 30 ; dd  frames staged (X3G_FRAME)
X3P_S_SCFSI equ X3P_ST + 34 ; 2 db  scfsi bits, a channel
X3P_S_GRNXT equ X3P_ST + 36 ; db  the granule x3p_gran expects next
X3P_S_RES   equ X3P_ST + 37 ; db
X3P_SIDE    equ X3P_ST + 38 ; THE SIDE INFORMATION: [granule][channel]
X3P_SI_P23  equ 0       ; dw  part2_3_length
X3P_SI_BV   equ 2       ; dw  big_values
X3P_SI_SFC  equ 4       ; dw  scalefac_compress (4 or 9 bits)
X3P_SI_GG   equ 6       ; db  global_gain
X3P_SI_WS   equ 7       ; db  window_switching
X3P_SI_BT   equ 8       ; db  block_type
X3P_SI_MIX  equ 9       ; db  mixed
X3P_SI_TAB  equ 10      ; 3 db  table_select
X3P_SI_SBG  equ 13      ; 3 db  subblock_gain
X3P_SI_R0   equ 16      ; db  region0_count (8 / 7 with window switching)
X3P_SI_R1   equ 17      ; db  region1_count (36 with window switching)
X3P_SI_PRE  equ 18      ; db  preflag
X3P_SI_SCL  equ 19      ; db  scalefac_scale
X3P_SI_C1   equ 20      ; db  count1table_select
X3P_SI_RES  equ 21      ; db
X3P_SI_SZ   equ 22
X3P_SCF     equ X3P_SIDE + 4 * X3P_SI_SZ   ; SCALEFACTORS, a channel:
X3P_SF_LONG equ 0       ; 22 db
X3P_SF_SHRT equ 22      ; 39 db  [band 0..12][window 0..2]
X3P_SF_ILL  equ 61      ; 22 + 39 db  the MPEG-2 intensity 'illegal' flags,
X3P_SF_SZ   equ 122     ;     in the same order
X3P_MD      equ X3P_SCF + 2 * X3P_SF_SZ    ; THE MAIN DATA: 512 bytes of the
X3P_MD_TAIL equ 512     ;     reservoir's tail, then this frame's main data
X3P_MD_SZ   equ 512 + 1452 + 4  ; ...and 4 zero bytes past its end
X3P_FRM     equ X3P_MD + X3P_MD_SZ         ; the staged frame's bytes
X3P_FRM_SZ  equ 1448    ; (the largest Layer III frame is 1,441)
X3P_DEND    equ X3P_FRM + X3P_FRM_SZ       ; the FRONT part's code follows
;
; GRAN (the FRONT part's verb; x3p_gran's, internal to the front end)
;   in   DS = the FRONT part's data segment, AL = the granule
;   out  CF = 0, the record at X3P_REC filled; keeps every register but AX
;
; CHECK  the part's Huffman tables against their CRC-32 (x3std.inc's).
;   in   DS = the FRONT part's data segment   out CF = 0 / CF = 1 X3E_TABLE
;
; NEXT   x3p_next's work: in DS = the FRONT part's data segment, ES:SI = the
;        X3R descriptor; out as x3p_next (section 4)

%endif ; X3GRAN_INC
