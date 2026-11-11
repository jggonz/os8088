; =============================================================================
; Browser 1.7 images (SPEC.md 71.26): what the primary and part 1 both read.
; =============================================================================

BR_IMGTMAX  equ 32                  ; image table entries: INDEX 0..31
BR_ITSZ     equ 16                  ; bytes an entry

IT_ST       equ 0                   ; byte: IS_*
IT_FLG      equ 1                   ; byte: IF_* bits, the border << 3
IT_SRC      equ 2                   ; word: the src's offset in the link arena,
                                    ; 0FFFFh = none kept
IT_W        equ 4                   ; word: declared width in pixels, 0 = none
IT_H        equ 6                   ; word: ...and height
IT_DOC      equ 8                   ; dword: its D_IMG byte in the document
IT_PARA     equ 12                  ; word: its ready copy, paragraphs in
IT_COLS     equ 14                  ; byte: the box the copy was made for
IT_ROWS     equ 15                  ; byte

IS_WAIT     equ 0
IS_READY    equ 1
IS_NONE     equ 2
IS_EMS      equ 3                   ; ready, its copy in br_imgar (EMS)
IS_DISK     equ 4                   ; ready, its copy in the cache file (71.27)

ICS_FILE    equ 1                   ; br_icstate: the page has a cache file
ICS_OFF     equ 2                   ; ...the cache is off until the next page
BR_IDBUF    equ 8192                ; the paint's read buffer, the store's first bytes
BR_IDWIN    equ BR_IDBUF / 2        ; ...as two windows, each read on its own
BR_ICRING   equ 8                   ; pages kept on a fixed volume (71.28)

IF_NOW      equ 1                   ; no width given
IF_NOH      equ 2                   ; no height given
IF_LAID     equ 4                   ; the last layout drew it as a box
IF_BSHIFT   equ 3                   ; the border, in pixels, from bit 3

IE_OFF      equ 0                   ; br_ie: the engine (SPEC.md 71.26)
IE_START    equ 1                   ; UI: fetch part 1, claim the store
IE_NEXT     equ 2                   ; worker: the next image
IE_OPEN     equ 3
IE_WAIT     equ 4
IE_SEND     equ 5
IE_HEAD     equ 6
IE_BODY     equ 7
IE_GROW     equ 8                   ; UI: grow the staging
IE_PREP     equ 9                   ; UI: size, lay out, grow for the decode
IE_DEC      equ 10                  ; worker: decode
IE_SHOW     equ 11                  ; UI: shrink, draw
IE_END      equ 12                  ; UI: drop the part
IE_CACHED   equ 13                  ; UI: a kept copy, from the file (71.28)
IE_AEND     equ 14                  ; UI: every animation still: let them go
                                    ; (14 was IE_APREP: the second fetch is gone)
IE_ASET     equ 15                  ; worker: its plans and master
IE_ANIM     equ 16                  ; worker: the frames, when due
IE_AFRAME   equ 17                  ; UI: a frame drawn
IE_AGO      equ 18                  ; UI: the LZW table, then IE_ANIM

BR_IFMAX    equ 60000               ; a GIF staged whole, at most
BR_ISCR     equ 23040               ; LZW table (18 KB) and the decode scratch
BR_IRMAX    equ 65520               ; a ready copy, at most
BR_IHOSTMAX equ 64                  ; an image's host, as br_host
BR_IGROW    equ 256                 ; staging grows by at least 4 KB (paras)

; Part 1's far entry table (brimage_part.asm leads with it) as byte offsets
; into br_im_vec, four bytes an entry
BRIM_STEP   equ 0                   ; the worker's step (br_im_step)
BRIM_WAKE   equ 4                   ; the UI task's half of a hand-over
BRIM_N      equ 2
BRIM_SELF   equ BRIM_N * 2          ; ...then a word: the part's DATA segment

; Part 1's working state: in the PRIMARY's bss (br_iwork), because every
; part runs with DS = the primary and the socket verbs want their buffers
; there (SPEC.md 72). The primary only reserves it
bri_host    equ br_iwork + 0        ; BR_IHOSTMAX: the image's host
bri_port    equ br_iwork + 64       ; word
bri_sent    equ br_iwork + 66       ; word: request bytes sent
bri_reqn    equ br_iwork + 68       ; word: ...of these
bri_rx      equ br_iwork + 70       ; word: body bytes staged
bri_code    equ br_iwork + 72       ; word: the status code
bri_clen    equ br_iwork + 74       ; word: Content-Length, saturating
bri_hcol    equ br_iwork + 76       ; byte: column in the header line
bri_hst     equ br_iwork + 77       ; byte: 0 the status line, 1 the fields
bri_hm      equ br_iwork + 78       ; byte: content-length: 0 matching, 1 its
                                    ; digits, 2 read, FFh not this line
bri_hsp     equ br_iwork + 79       ; byte: spaces in the status line
bri_hasc    equ br_iwork + 80       ; byte: a Content-Length was given
bri_mono    equ br_iwork + 81       ; byte: the copy is 1bpp
bri_gw      equ br_iwork + 82       ; word: the logical screen
bri_gh      equ br_iwork + 84
bri_fl      equ br_iwork + 86       ; word: the first frame's place and size
bri_ft      equ br_iwork + 88
bri_fw      equ br_iwork + 90
bri_fh      equ br_iwork + 92
bri_pal     equ br_iwork + 94       ; word: its palette's offset in the file
bri_npal    equ br_iwork + 96       ; word: ...and entries
bri_trans   equ br_iwork + 98       ; word: the transparent index, FFFFh none
bri_data    equ br_iwork + 100      ; word: its first sub-block's length byte
bri_bgi     equ br_iwork + 102      ; byte: the background index
bri_ilace   equ br_iwork + 103      ; byte: interlaced
bri_min     equ br_iwork + 104      ; byte: the LZW minimum code size
bri_bord    equ br_iwork + 105      ; byte: the border, pixels
bri_tw      equ br_iwork + 106      ; word: the picture's size in the box
bri_th      equ br_iwork + 108
bri_bc      equ br_iwork + 110      ; word: the box's COLS (a word: the
                                    ; plane step)
bri_br      equ br_iwork + 112      ; byte: ...ROWS
bri_allbg   equ br_iwork + 113      ; byte: the row is the background's
bri_lzwp    equ br_iwork + 114      ; word: the LZW table, paragraphs in
bri_newp    equ br_iwork + 116      ; word: the new copy, paragraphs in
bri_rbytes  equ br_iwork + 118      ; word: its bytes
bri_stride  equ br_iwork + 120      ; word: ...and a row's
bri_stgs    equ br_iwork + 122      ; word: the staged file's segment
bri_lzws    equ br_iwork + 124      ; word: the table's
bri_news    equ br_iwork + 126      ; word: the new copy's
bri_fpos    equ br_iwork + 128      ; word: the code bytes' cursor
bri_blk     equ br_iwork + 130      ; byte: left in this sub-block
bri_bend    equ br_iwork + 131      ; byte: the blocks have ended
bri_fcol    equ br_iwork + 132      ; word: the frame row's next column
bri_frow    equ br_iwork + 134      ; word: frame rows done
bri_fy      equ br_iwork + 136      ; word: the frame row being filled
bri_pass    equ br_iwork + 138      ; byte: the interlace pass
bri_ret     equ br_iwork + 140      ; word: a row's answer to the LZW
bri_tx      equ br_iwork + 142      ; word: the pixel loop's column
bri_ty      equ br_iwork + 144      ; word: ...and target row
bri_ty1     equ br_iwork + 146      ; word: one past the rows a frame row fills
bri_lzw     equ br_iwork + 148      ; LZW_BSSSZ (26): os88lzw.inc's state
bri_dbyt    equ br_iwork + 174      ; dword: the disk spill: the copies' bytes
bri_doff    equ br_iwork + 178      ; dword: ...the next piece's place
bri_dclus   equ br_iwork + 182      ; word: ...the volume's cluster in bytes
bri_slot    equ br_iwork + 184      ; byte: the kept copy's slot, FFh none
bri_cond    equ br_iwork + 185      ; byte: the request asks again (71.28)
bri_hmask   equ br_iwork + 186      ; byte: the header names still matching
bri_vk      equ br_iwork + 187      ; byte: the validator read: 1 LM, 2 ETag
bri_vn      equ br_iwork + 188      ; byte: ...its bytes so far
bri_rh      equ br_iwork + 190      ; dword: the request's hash
bri_dfree   equ br_iwork + 194      ; dword: a kept volume's bytes left
bri_dcut    equ br_iwork + 198      ; word: the disk spill's cut
bri_dbase   equ br_iwork + 200      ; word: ...and the new [br_ibase]
bri_apend   equ br_iwork + 202      ; byte: the image in hand will animate
bri_anum    equ br_iwork + 203      ; byte: animations set up
bri_amode   equ br_iwork + 204      ; byte: rows go to the master
bri_aident  equ br_iwork + 205      ; byte: an index is itself
bri_acur    equ br_iwork + 206      ; byte: the record in hand
bri_xdisp   equ br_iwork + 207      ; byte: the GCE's disposal
bri_xdelay  equ br_iwork + 208      ; word: ...its delay, hundredths
bri_xloop   equ br_iwork + 210      ; word: NETSCAPE2.0's count, FFFFh none
bri_amask   equ br_iwork + 212      ; dword: images found animated
bri_atab    equ br_iwork + 216      ; word: the frame's table in the file
bri_atabn   equ br_iwork + 218      ; word: ...its entries
bri_amseg   equ br_iwork + 220      ; word: the master's segment
bri_asseg   equ br_iwork + 222      ; word: the saved rect's
bri_ab      equ br_iwork + 224      ; byte: B, the background index
bri_ahas3   equ br_iwork + 225      ; byte: a frame has disposal 3
bri_apar    equ br_iwork + 226      ; word: the block's paragraphs
bri_ax1     equ br_iwork + 228      ; word: a rect to fill, save or restore
bri_ay1     equ br_iwork + 230
bri_ax2     equ br_iwork + 232
bri_ay2     equ br_iwork + 234
bri_dx1     equ br_iwork + 236      ; word: the dirty rect, FFFFh none
bri_dy1     equ br_iwork + 238
bri_dx2     equ br_iwork + 240
bri_dy2     equ br_iwork + 242
bri_anew    equ br_iwork + 244      ; word: past the frame's data, FFFFh end
bri_apal    equ br_iwork + 246      ; word: frame 0's palette in the file
bri_apn     equ br_iwork + 248      ; word: ...its entries
bri_cr      equ br_iwork + 250      ; byte: a colour being matched
bri_cg      equ br_iwork + 251
bri_cb      equ br_iwork + 252
bri_bi      equ br_iwork + 253      ; byte: ...the best entry so far
bri_best    equ br_iwork + 254      ; dword: ...and its distance
bri_lsy     equ br_iwork + 258      ; word: the master row in bri_rowbuf
bri_at0     equ br_iwork + 260      ; word: frame 0's key, FFFFh none
BR_IWORK    equ 262

; an animation's record in part 1 (SPEC.md 71.29), BRI_AMAX of them
BRI_AMAX    equ 4
A_ON        equ 0                   ; byte: 0 free, 1 plays, 2 stopped, 3 paused
A_K         equ 1                   ; byte: the image
A_BLK       equ 2                   ; word: its block: the file, paragraphs in
A_FLEN      equ 4                   ; word: the file's bytes
A_MST       equ 6                   ; word: the master, paragraphs in
A_SAV       equ 8                   ; word: the saved rect's, 0 none
A_PLN       equ 10                  ; word: the plans'
A_GW        equ 12                  ; word: the logical screen
A_GH        equ 14
A_B         equ 16                  ; byte: B
A_G0        equ 17                  ; byte: frame 0 used the global table
A_GPAL      equ 18                  ; word: the global table, 0 none
A_GN        equ 20                  ; word: ...its entries
A_P0PAL     equ 22                  ; word: frame 0's table
A_P0N       equ 24
A_P1        equ 26                  ; word: past frame 0's data, FFFFh none
A_I0        equ 28                  ; word: frame 0's code bytes
A_LOOP      equ 30                  ; word: the count, FFFFh plays once
A_LEFT      equ 32                  ; word: passes left
A_POS       equ 34                  ; word: the next frame's blocks, FFFFh end
A_PX1       equ 36                  ; word: the last frame's rect, FFFFh none
A_PY1       equ 38
A_PX2       equ 40
A_PY2       equ 42
A_PDISP     equ 44                  ; byte: ...and its disposal
A_F0IL      equ 45                  ; byte: frame 0 interlaced
A_F0L       equ 46                  ; word: frame 0's place and size
A_F0T       equ 48
A_F0W       equ 50
A_F0H       equ 52
A_F0MIN     equ 54                  ; byte: its minimum code size
A_DISP0     equ 55                  ; byte: its disposal
A_T0        equ 56                  ; word: its transparent index, FFFFh none
A_D0        equ 58                  ; word: its delay, ticks
A_DUE       equ 60                  ; word: the next frame's tick
A_TW        equ 62                  ; word: the picture in its box
A_TH        equ 64
A_BORD      equ 66                  ; byte
A_SIZE      equ 68

; the kept page's index (SPEC.md 71.28): the file's first BRI_IXSZ bytes
BRI_IXSZ    equ 3072
IXH_MAGIC   equ 0                   ; 'BRC1'
IXH_HASH    equ 4                   ; dword: the page's hash
IXH_END     equ 8                   ; dword: the file's allocated end
IXH_SIZE    equ 64                  ; ...then BRI_SLOTS slots
BRI_SLOTS   equ 32
S_HASH      equ 0                   ; dword: the request's hash, 0 = free
S_GW        equ 4                   ; word: the picture's logical screen
S_GH        equ 6
S_COLS      equ 8                   ; byte: the box the copy was made for
S_ROWS      equ 9
S_MONO      equ 10                  ; byte: 1 = a 1bpp copy
S_ANIM      equ 11                  ; byte: non-zero = animated (71.29)
S_PARA      equ 12                  ; word: the copy, paragraphs into the file
S_LM        equ 14                  ; BRI_LMMAX: Last-Modified, NUL-ended
S_ETAG      equ 44                  ; BRI_ETMAX: ETag, NUL-ended
S_SIZE      equ 80
BRI_LMMAX   equ 30
BRI_ETMAX   equ 36
