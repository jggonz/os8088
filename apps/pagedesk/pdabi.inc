; ============================================================================
; os8088 - apps/pagedesk/pdabi.inc          GENERATED - DO NOT EDIT
;
; The assembly mirror of apps/pagedesk/pdabi.h: every integer #define and
; every struct field offset and size.  Written by
;     python3 tools/os88pdabi.py --write
; and held to the header by `python3 tools/os88pdabi.py --check`, which every
; `make` runs.  PDXB_<STRUCT>_<FIELD> is a byte offset; arrays also get
; _N (outer count) and _ESZ (bytes per outer element).
; ============================================================================
%ifndef PDABI_INC
%define PDABI_INC 1

; ---- #define ---------------------------------------------------------------
PD_MAXTOK                equ 1024
PD_MAXSTR                equ 4096
PD_MAXNAME               equ 127
PD_MAXDEPTH              equ 32
PD_MAXITEMS              equ 4096
PD_MAXREV                equ 8
PD_MAXSUBSEC             equ 256
PD_MAXPAGES              equ 1024
PD_MAXFILTERS            equ 4
PD_MAXGSTACK             equ 16
PD_MAXFORMDEPTH          equ 4
PD_MAXINLINEIMG          equ 4096
PD_BANDROWS              equ 16
PD_SLICE_OPS             equ 256
PD_THUMBS                equ 12
PD_MAXEDITS              equ 256
PD_MAXTEXT               equ 1024
PD_MAXINKPTS             equ 512
PD_MAXSTROKES            equ 16
PD_UNDO                  equ 64
PD_MAXPT                 equ 32767
PDE_OK                   equ 0
PDE_SYNTAX               equ 1
PDE_XREF                 equ 2
PDE_TRUNC                equ 3
PDE_CYCLE                equ 4
PDE_DEPTH                equ 5
PDE_FLATE                equ 16
PDE_FILTER               equ 17
PDE_FONT                 equ 18
PDE_GRAPHICS             equ 19
PDE_ENCRYPT              equ 20
PDE_XREFSTM              equ 21
PDE_SIGNED               equ 22
PDE_PASSWORD             equ 23
PDE_NOSLOT               equ 24
PDE_RANGE                equ 32
PDE_LIMIT                equ 33
PDE_NOMEM                equ 34
PDE_STACK                equ 35
PDE_IO                   equ 48
PDE_DISKFULL             equ 49
PDE_WPROT                equ 50
PDE_EXISTS               equ 51
PDE_CHANGED              equ 52
PDE_NOTFOUND             equ 53
PDE_CANCEL               equ 64
PDE_BUSY                 equ 65
PDE_NEEDMOD              equ 66
PDE_NEEDMEM              equ 67
PDF_INC_FONT             equ 0x0001
PDF_INC_FILTER           equ 0x0002
PDF_INC_IMAGE            equ 0x0004
PDF_INC_COLOUR           equ 0x0008
PDF_INC_SHADE            equ 0x0010
PDF_INC_TRANSP           equ 0x0020
PDF_INC_OPER             equ 0x0040
PDF_INC_LIMIT            equ 0x0080
PDF_INC_RANGE            equ 0x0100
PDF_INC_MODULE           equ 0x0200
PDF_INC_APPROX           equ 0x0400
PD_DSTK_BYTES            equ 2048
PD_DSTK_UISP             equ 2040
PD_DSTK_UISS             equ 2042
PD_DSTK_SELF             equ 2044
PD_DSTK_MAG              equ 2046
PD_DSTK_MAGIC            equ 0x4B53
PD_DSTK_CAN0             equ 0x5350
PD_DSTK_CAN1             equ 0x4B44
PD_DSTK_FILL             equ 0xCC
PDM_ABI                  equ 2
PDM_MAGIC_ENG            equ 0x4550
PDM_MAGIC_REND           equ 0x5250
PDM_MAGIC_WRT            equ 0x5750
PDM_MAGIC_KEY            equ 0x4B50
PDM_MAGIC_GFX            equ 0x4750
PDM_MAGIC_FNT            equ 0x4650
PDM_MAGIC_IMG            equ 0x4950
PDM_MAGIC_JPX            equ 0x4A50
PDX_NONE                 equ 0
PDX_KEY                  equ 1
PDX_GFX                  equ 2
PDX_FNT                  equ 3
PDX_IMG                  equ 4
PDX_JPX                  equ 5
PDX_COUNT                equ 6
PDMEM_XIDX               equ 1
PDMEM_OSW                equ 2
PDMEM_XC                 equ 3
PDMEM_X8                 equ 4
PDM_ENTRY                equ 8
PDV_ENG_INIT             equ 0x00
PDV_ENG_OPEN             equ 0x01
PDV_ENG_CLOSE            equ 0x02
PDV_ENG_PAGEINFO         equ 0x03
PDV_ENG_OBJ_FIND         equ 0x04
PDV_ENG_DICT_GET         equ 0x05
PDV_ENG_STRM_OPEN        equ 0x06
PDV_ENG_STRM_READ        equ 0x07
PDV_ENG_STRM_CLOSE       equ 0x08
PDV_ENG_TOKEN            equ 0x09
PDV_ENG_RES_LOOKUP       equ 0x0A
PDV_ENG_RAW_COPY         equ 0x0B
PDV_ENG_TRAILER          equ 0x0C
PDV_ENG_OUTLINE          equ 0x0D
PDV_ENG_ANNOTS           equ 0x0E
PDV_ENG_SEARCH           equ 0x0F
PDV_ENG_FINGERPRINT      equ 0x10
PDV_ENG_ARENA_MARK       equ 0x11
PDV_ENG_ARENA_REL        equ 0x12
PDV_ENG_CRYPTINFO        equ 0x13
PDV_ENG_SETKEY           equ 0x14
PDV_ENG_CRYPT            equ 0x15
PDV_ENG_SLOT             equ 0x16
PDV_REND_INIT            equ 0x40
PDV_REND_BEGIN           equ 0x41
PDV_REND_STEP            equ 0x42
PDV_REND_ABORT           equ 0x43
PDV_REND_TEXTMAP         equ 0x44
PDV_REND_THUMB           equ 0x45
PDV_REND_STATUS          equ 0x46
PDV_REND_FONTBANK        equ 0x47
PDV_REND_SLOT            equ 0x48
PDV_WRT_INIT             equ 0x80
PDV_WRT_SAVE             equ 0x81
PDV_WRT_NEWDOC           equ 0x82
PDV_WRT_EXTRACT          equ 0x83
PDV_WRT_ABORT            equ 0x84
PDV_X_INIT               equ 0xC0
PDV_KEY_AUTH             equ 0xC1
PDV_KEY_COST             equ 0xC2
PDV_IMG_OPEN             equ 0xE8
PDV_IMG_ROW              equ 0xE9
PDV_IMG_CLOSE            equ 0xEA
PD_XB_BYTES              equ 1024
PDXI_FLATE               equ 0x0001
PDO_READONLY             equ 0x0001
PDO_OUTLINE              equ 0x0002
PDO_ACROFORM             equ 0x0004
PDO_TAGGED               equ 0x0008
PDO_ENCRYPTED            equ 0x0010
PDO_XREFSTM              equ 0x0020
PDO_OBJSTM               equ 0x0040
PDO_REPAIRED             equ 0x0080
PDP_NOCONTENT            equ 0x0001
PDP_HASLINKS             equ 0x0002
PDK_NULL                 equ 0
PDK_BOOL                 equ 1
PDK_INT                  equ 2
PDK_REAL                 equ 3
PDK_STR                  equ 4
PDK_NAME                 equ 5
PDK_ARR                  equ 6
PDK_DICT                 equ 7
PDK_STRM                 equ 8
PDK_REF                  equ 9
PDFL_HEX                 equ 0x0001
PDFL_RLE                 equ 0x0002
PDFL_FLATE               equ 0x0004
PDFL_A85                 equ 0x0008
PDFL_LZW                 equ 0x0010
PDFL_CRYPT               equ 0x0020
PDSA_CODEC               equ 0x0001
PDCD_NONE                equ 0
PDCD_DCT                 equ 1
PDCD_CCITT               equ 2
PDCD_JBIG2               equ 3
PDCD_JPX                 equ 4
PDT_EOF                  equ 0
PDT_NUM                  equ 1
PDT_STR                  equ 2
PDT_NAME                 equ 3
PDT_OP                   equ 4
PDT_ARRB                 equ 5
PDT_ARRE                 equ 6
PDT_DICTB                equ 7
PDT_DICTE                equ 8
PDT_TRUE                 equ 9
PDT_FALSE                equ 10
PDT_NULL                 equ 11
PDT_REF                  equ 12
PDRC_EXACT               equ 0
PDRC_PLAIN               equ 1
PDTR_XREFSTM             equ 0x0001
PDTR_ENCDIRECT           equ 0x0002
PDS_NONE                 equ 0
PDS_COMPLETED            equ 1
PDRF_ANNOTS              equ 0x0001
PDRF_EDITS               equ 0x0002
PDRF_NOEDITID            equ 0x0004
PDSV_INCREMENTAL         equ 0
PDSV_FRESH               equ 1
PDSV_EXTRACT             equ 2
PDC_BLACK                equ 0
PDC_BLUE                 equ 1
PDC_GREEN                equ 2
PDC_CYAN                 equ 3
PDC_RED                  equ 4
PDC_MAGENTA              equ 5
PDC_BROWN                equ 6
PDC_LGRAY                equ 7
PDC_DGRAY                equ 8
PDC_LBLUE                equ 9
PDC_LGREEN               equ 10
PDC_LCYAN                equ 11
PDC_LRED                 equ 12
PDC_LMAGENTA             equ 13
PDC_YELLOW               equ 14
PDC_WHITE                equ 15
PDA_NOTE                 equ 1
PDA_HIGHLIGHT            equ 2
PDA_AREA                 equ 3
PDA_TEXT                 equ 4
PDA_INK                  equ 5
PDA_LINE                 equ 6
PDA_RECT                 equ 7
PDA_ELLIPSE              equ 8
PDA_SIGN                 equ 9
PDA_PAGEOP               equ 16
PDEF_DELETED             equ 0x01
PDEF_MOVED               equ 0x02
PDEF_DIRTY               equ 0x04
PDEF_IRTSRC              equ 0x08
PDCM_NONE                equ 0
PDCM_RC4                 equ 1
PDCM_AESV2               equ 2
PDCM_AESV3               equ 3
PDCM_IDENTITY            equ 4
PDCA_NONE                equ 0
PDCA_USER                equ 1
PDCA_OWNER               equ 2
PDIMG_MASK               equ 0x0001
PDIMG_INVERT             equ 0x0002
PDIM_GREY8               equ 0
PDIM_BITS1               equ 1
PDIM_N8                  equ 2
CCF_EOL                  equ 1
CCF_ALIGN                equ 2
CCF_EOB                  equ 4
CCF_BLACK1               equ 8

; ---- struct pd_u32 (4 bytes) ----------------------------------------------
PDXB_U32_LO                    equ 0
PDXB_U32_HI                    equ 2
PDXB_U32_BYTES                 equ 4

; ---- struct pd_fx (4 bytes) -----------------------------------------------
PDXB_FX_LO                     equ 0
PDXB_FX_HI                     equ 2
PDXB_FX_BYTES                  equ 4

; ---- struct pd_mat (24 bytes) ----------------------------------------------
PDXB_MAT_A                     equ 0
PDXB_MAT_B                     equ 4
PDXB_MAT_C                     equ 8
PDXB_MAT_D                     equ 12
PDXB_MAT_E                     equ 16
PDXB_MAT_F                     equ 20
PDXB_MAT_BYTES                 equ 24

; ---- struct pd_rect (8 bytes) ---------------------------------------------
PDXB_RECT_X0                   equ 0
PDXB_RECT_Y0                   equ 2
PDXB_RECT_X1                   equ 4
PDXB_RECT_Y1                   equ 6
PDXB_RECT_BYTES                equ 8

; ---- struct pd_prect (8 bytes) --------------------------------------------
PDXB_PRECT_X0                  equ 0
PDXB_PRECT_Y0                  equ 2
PDXB_PRECT_X1                  equ 4
PDXB_PRECT_Y1                  equ 6
PDXB_PRECT_BYTES               equ 8

; ---- struct pd_view (28 bytes) ---------------------------------------------
PDXB_VIEW_BOX                  equ 0
PDXB_VIEW_ROT                  equ 8
PDXB_VIEW_ZOOM                 equ 10
PDXB_VIEW_ASP_NUM              equ 12
PDXB_VIEW_ASP_DEN              equ 14
PDXB_VIEW_OX                   equ 16
PDXB_VIEW_OY                   equ 18
PDXB_VIEW_CLIP_X0              equ 20
PDXB_VIEW_CLIP_Y0              equ 22
PDXB_VIEW_CLIP_X1              equ 24
PDXB_VIEW_CLIP_Y1              equ 26
PDXB_VIEW_BYTES                equ 28

; ---- struct pdxb_init (74 bytes) -------------------------------------------
PDXB_INIT_XB_SEG               equ 0
PDXB_INIT_ENG_OFF              equ 2
PDXB_INIT_ENG_SEG              equ 4
PDXB_INIT_CACHE_SEG            equ 6
PDXB_INIT_CACHE_KB             equ 8
PDXB_INIT_ARENA_SEG            equ 10
PDXB_INIT_ARENA_KB             equ 12
PDXB_INIT_TABLES_SEG           equ 14
PDXB_INIT_TABLES_KB            equ 16
PDXB_INIT_DL_SEG               equ 18
PDXB_INIT_DL_KB                equ 20
PDXB_INIT_BAND_SEG             equ 22
PDXB_INIT_BAND_BYTES           equ 24
PDXB_INIT_GLYPH_SEG            equ 26
PDXB_INIT_GLYPH_KB             equ 28
PDXB_INIT_BANK_SEG             equ 30
PDXB_INIT_BANK_KB              equ 32
PDXB_INIT_WBUF_SEG             equ 34
PDXB_INIT_WBUF_KB              equ 36
PDXB_INIT_ADAPTER              equ 38
PDXB_INIT_BPP                  equ 40
PDXB_INIT_FLAGS                equ 42
PDXB_INIT_FLATE_SEG            equ 44
PDXB_INIT_FLATE_KB             equ 46
PDXB_INIT_SLOT_SEG             equ 48
PDXB_INIT_SLOT_KB              equ 50
PDXB_INIT_XIDX_SEG             equ 52
PDXB_INIT_XIDX_KB              equ 54
PDXB_INIT_OSW_SEG              equ 56
PDXB_INIT_OSW_KB               equ 58
PDXB_INIT_XC_SEG               equ 60
PDXB_INIT_XC_KB                equ 62
PDXB_INIT_X8_SEG               equ 64
PDXB_INIT_X8_KB                equ 66
PDXB_INIT_CPU                  equ 68
PDXB_INIT_REND_OFF             equ 70
PDXB_INIT_REND_SEG             equ 72
PDXB_INIT_BYTES                equ 74

; ---- struct pdxb_open (32 bytes) -------------------------------------------
PDXB_OPEN_NAME                 equ 0
PDXB_OPEN_NAME_N               equ 14
PDXB_OPEN_NAME_ESZ             equ 1
PDXB_OPEN_STATE                equ 14
PDXB_OPEN_NPAGES               equ 16
PDXB_OPEN_NREV                 equ 18
PDXB_OPEN_VERSION              equ 20
PDXB_OPEN_FLAGS                equ 22
PDXB_OPEN_SIZE                 equ 24
PDXB_OPEN_ERR_OBJ              equ 28
PDXB_OPEN_ERR_DETAIL           equ 30
PDXB_OPEN_BYTES                equ 32

; ---- struct pdxb_pageinfo (26 bytes) ---------------------------------------
PDXB_PAGEINFO_OBJNUM           equ 0
PDXB_PAGEINFO_GEN              equ 2
PDXB_PAGEINFO_MEDIA            equ 4
PDXB_PAGEINFO_CROP             equ 12
PDXB_PAGEINFO_ROTATE           equ 20
PDXB_PAGEINFO_NANNOTS          equ 22
PDXB_PAGEINFO_FLAGS            equ 24
PDXB_PAGEINFO_BYTES            equ 26

; ---- struct pdxb_objref (4 bytes) -----------------------------------------
PDXB_OBJREF_OBJNUM             equ 0
PDXB_OBJREF_GEN                equ 2
PDXB_OBJREF_BYTES              equ 4

; ---- struct pdxb_objinfo (14 bytes) ----------------------------------------
PDXB_OBJINFO_OBJNUM            equ 0
PDXB_OBJINFO_GEN               equ 2
PDXB_OBJINFO_KIND              equ 4
PDXB_OBJINFO_OFFSET            equ 6
PDXB_OBJINFO_LENGTH            equ 10
PDXB_OBJINFO_BYTES             equ 14

; ---- struct pdxb_dictget (242 bytes) ----------------------------------------
PDXB_DICTGET_OBJNUM            equ 0
PDXB_DICTGET_GEN               equ 2
PDXB_DICTGET_KEY               equ 4
PDXB_DICTGET_KEY_N             equ 3
PDXB_DICTGET_KEY_ESZ           equ 32
PDXB_DICTGET_KIND              equ 100
PDXB_DICTGET_NUM               equ 102
PDXB_DICTGET_REFNUM            equ 106
PDXB_DICTGET_REFGEN            equ 108
PDXB_DICTGET_LEN               equ 110
PDXB_DICTGET_TEXT              equ 112
PDXB_DICTGET_TEXT_N            equ 128
PDXB_DICTGET_TEXT_ESZ          equ 1
PDXB_DICTGET_COUNT             equ 240
PDXB_DICTGET_BYTES             equ 242

; ---- struct pdxb_strm (18 bytes) -------------------------------------------
PDXB_STRM_OBJNUM               equ 0
PDXB_STRM_GEN                  equ 2
PDXB_STRM_INDEX                equ 4
PDXB_STRM_PAGE                 equ 6
PDXB_STRM_DECLEN               equ 8
PDXB_STRM_FILTERS              equ 12
PDXB_STRM_ACCEPT               equ 14
PDXB_STRM_CODEC                equ 16
PDXB_STRM_BYTES                equ 18

; ---- struct pdxb_token (266 bytes) ------------------------------------------
PDXB_TOKEN_KIND                equ 0
PDXB_TOKEN_NUM                 equ 2
PDXB_TOKEN_LEN                 equ 6
PDXB_TOKEN_TEXT                equ 8
PDXB_TOKEN_TEXT_N              equ 256
PDXB_TOKEN_TEXT_ESZ            equ 1
PDXB_TOKEN_MORE                equ 264
PDXB_TOKEN_BYTES               equ 266

; ---- struct pdxb_reslookup (60 bytes) --------------------------------------
PDXB_RESLOOKUP_PAGE            equ 0
PDXB_RESLOOKUP_RESOBJ          equ 2
PDXB_RESLOOKUP_RESGEN          equ 4
PDXB_RESLOOKUP_CATEGORY        equ 6
PDXB_RESLOOKUP_CATEGORY_N      equ 16
PDXB_RESLOOKUP_CATEGORY_ESZ    equ 1
PDXB_RESLOOKUP_NAME            equ 22
PDXB_RESLOOKUP_NAME_N          equ 32
PDXB_RESLOOKUP_NAME_ESZ        equ 1
PDXB_RESLOOKUP_OBJNUM          equ 54
PDXB_RESLOOKUP_GEN             equ 56
PDXB_RESLOOKUP_KIND            equ 58
PDXB_RESLOOKUP_BYTES           equ 60

; ---- struct pdxb_rawcopy (18 bytes) ----------------------------------------
PDXB_RAWCOPY_OBJNUM            equ 0
PDXB_RAWCOPY_GEN               equ 2
PDXB_RAWCOPY_DONE              equ 4
PDXB_RAWCOPY_DSEG              equ 8
PDXB_RAWCOPY_DOFF              equ 10
PDXB_RAWCOPY_DMAX              equ 12
PDXB_RAWCOPY_GOT               equ 14
PDXB_RAWCOPY_MODE              equ 16
PDXB_RAWCOPY_BYTES             equ 18

; ---- struct pdxb_trailer (56 bytes) ----------------------------------------
PDXB_TRAILER_SIZE              equ 0
PDXB_TRAILER_ROOT_NUM          equ 2
PDXB_TRAILER_ROOT_GEN          equ 4
PDXB_TRAILER_INFO_NUM          equ 6
PDXB_TRAILER_INFO_GEN          equ 8
PDXB_TRAILER_STARTXREF         equ 10
PDXB_TRAILER_ID0               equ 14
PDXB_TRAILER_ID0_N             equ 16
PDXB_TRAILER_ID0_ESZ           equ 1
PDXB_TRAILER_ID0_LEN           equ 30
PDXB_TRAILER_VERSION           equ 32
PDXB_TRAILER_ID0_HI            equ 34
PDXB_TRAILER_ID0_HI_N          equ 16
PDXB_TRAILER_ID0_HI_ESZ        equ 1
PDXB_TRAILER_ENCRYPT_NUM       equ 50
PDXB_TRAILER_ENCRYPT_GEN       equ 52
PDXB_TRAILER_TFLAGS            equ 54
PDXB_TRAILER_BYTES             equ 56

; ---- struct pdxb_outline (80 bytes) ----------------------------------------
PDXB_OUTLINE_OBJNUM            equ 0
PDXB_OUTLINE_GEN               equ 2
PDXB_OUTLINE_NEXT_NUM          equ 4
PDXB_OUTLINE_NEXT_GEN          equ 6
PDXB_OUTLINE_FIRST_NUM         equ 8
PDXB_OUTLINE_FIRST_GEN         equ 10
PDXB_OUTLINE_COUNT             equ 12
PDXB_OUTLINE_DEST_PAGE         equ 14
PDXB_OUTLINE_TITLE             equ 16
PDXB_OUTLINE_TITLE_N           equ 64
PDXB_OUTLINE_TITLE_ESZ         equ 1
PDXB_OUTLINE_BYTES             equ 80

; ---- struct pdxb_annot (222 bytes) ------------------------------------------
PDXB_ANNOT_OBJNUM              equ 0
PDXB_ANNOT_GEN                 equ 2
PDXB_ANNOT_SUBTYPE             equ 4
PDXB_ANNOT_SUBTYPE_N           equ 16
PDXB_ANNOT_SUBTYPE_ESZ         equ 1
PDXB_ANNOT_RECT                equ 20
PDXB_ANNOT_NM                  equ 28
PDXB_ANNOT_NM_N                equ 24
PDXB_ANNOT_NM_ESZ              equ 1
PDXB_ANNOT_FLAGS               equ 52
PDXB_ANNOT_HAS_AP              equ 54
PDXB_ANNOT_IRT_NUM             equ 56
PDXB_ANNOT_STATE               equ 58
PDXB_ANNOT_CONTENTS_LEN        equ 60
PDXB_ANNOT_CONTENTS            equ 62
PDXB_ANNOT_CONTENTS_N          equ 160
PDXB_ANNOT_CONTENTS_ESZ        equ 1
PDXB_ANNOT_BYTES               equ 222

; ---- struct pdxb_search (76 bytes) -----------------------------------------
PDXB_SEARCH_NEEDLE             equ 0
PDXB_SEARCH_NEEDLE_N           equ 64
PDXB_SEARCH_NEEDLE_ESZ         equ 1
PDXB_SEARCH_PAGE               equ 64
PDXB_SEARCH_GLYPH              equ 66
PDXB_SEARCH_LAST_PAGE          equ 68
PDXB_SEARCH_STATE              equ 70
PDXB_SEARCH_FOUND              equ 72
PDXB_SEARCH_GLYPH_END          equ 74
PDXB_SEARCH_BYTES              equ 76

; ---- struct pdxb_fingerprint (8 bytes) ------------------------------------
PDXB_FINGERPRINT_SIZE          equ 0
PDXB_FINGERPRINT_CRC_HEAD      equ 4
PDXB_FINGERPRINT_CRC_TAIL      equ 6
PDXB_FINGERPRINT_BYTES         equ 8

; ---- struct pdxb_render (42 bytes) -----------------------------------------
PDXB_RENDER_VIEW               equ 0
PDXB_RENDER_PAGE               equ 28
PDXB_RENDER_WIN_OFF            equ 30
PDXB_RENDER_MONO_MARK          equ 32
PDXB_RENDER_EDITS_SEG          equ 34
PDXB_RENDER_EDITS_COUNT        equ 36
PDXB_RENDER_RFLAGS             equ 38
PDXB_RENDER_HIDE               equ 40
PDXB_RENDER_BYTES              equ 42

; ---- struct pdxb_textmap (42 bytes) ----------------------------------------
PDXB_TEXTMAP_VIEW              equ 0
PDXB_TEXTMAP_PAGE              equ 28
PDXB_TEXTMAP_STATE             equ 30
PDXB_TEXTMAP_DSEG              equ 32
PDXB_TEXTMAP_DOFF              equ 34
PDXB_TEXTMAP_DMAX              equ 36
PDXB_TEXTMAP_COUNT             equ 38
PDXB_TEXTMAP_DONE              equ 40
PDXB_TEXTMAP_BYTES             equ 42

; ---- struct pdtm_glyph (12 bytes) ------------------------------------------
PDXB_PDTM_GLYPH_CODE           equ 0
PDXB_PDTM_GLYPH_FLAGS          equ 1
PDXB_PDTM_GLYPH_X0             equ 2
PDXB_PDTM_GLYPH_Y0             equ 4
PDXB_PDTM_GLYPH_X1             equ 6
PDXB_PDTM_GLYPH_Y1             equ 8
PDXB_PDTM_GLYPH_RUN            equ 10
PDXB_PDTM_GLYPH_BYTES          equ 12

; ---- struct pdxb_thumb (12 bytes) ------------------------------------------
PDXB_THUMB_PAGE                equ 0
PDXB_THUMB_W                   equ 2
PDXB_THUMB_H                   equ 4
PDXB_THUMB_DSEG                equ 6
PDXB_THUMB_DOFF                equ 8
PDXB_THUMB_STATE               equ 10
PDXB_THUMB_BYTES               equ 12

; ---- struct pdxb_rstatus (42 bytes) ----------------------------------------
PDXB_RSTATUS_INCOMPLETE        equ 0
PDXB_RSTATUS_FIRST             equ 2
PDXB_RSTATUS_FIRST_N           equ 32
PDXB_RSTATUS_FIRST_ESZ         equ 1
PDXB_RSTATUS_OPS               equ 34
PDXB_RSTATUS_PASSES            equ 36
PDXB_RSTATUS_MODS              equ 38
PDXB_RSTATUS_SWAPS             equ 40
PDXB_RSTATUS_BYTES             equ 42

; ---- struct pdxb_fontbank (26 bytes) ---------------------------------------
PDXB_FONTBANK_PKGNAME          equ 0
PDXB_FONTBANK_PKGNAME_N        equ 14
PDXB_FONTBANK_PKGNAME_ESZ      equ 1
PDXB_FONTBANK_PLACE_CLUS       equ 14
PDXB_FONTBANK_PLACE_VOL        equ 16
PDXB_FONTBANK_PART_OFF         equ 18
PDXB_FONTBANK_INDEX_SEG        equ 22
PDXB_FONTBANK_INDEX_LEN        equ 24
PDXB_FONTBANK_BYTES            equ 26

; ---- struct pdxb_save (126 bytes) -------------------------------------------
PDXB_SAVE_TARGET               equ 0
PDXB_SAVE_TARGET_N             equ 14
PDXB_SAVE_TARGET_ESZ           equ 1
PDXB_SAVE_MODE                 equ 14
PDXB_SAVE_STATE                equ 16
PDXB_SAVE_EDITS_SEG            equ 18
PDXB_SAVE_EDITS_COUNT          equ 20
PDXB_SAVE_PAGES_SEG            equ 22
PDXB_SAVE_PAGES_COUNT          equ 24
PDXB_SAVE_WRITTEN              equ 26
PDXB_SAVE_TOTAL                equ 30
PDXB_SAVE_ERR_DETAIL           equ 34
PDXB_SAVE_SOURCE               equ 36
PDXB_SAVE_SOURCE_N             equ 14
PDXB_SAVE_SOURCE_ESZ           equ 1
PDXB_SAVE_DATE                 equ 50
PDXB_SAVE_DATE_N               equ 24
PDXB_SAVE_DATE_ESZ             equ 1
PDXB_SAVE_AUTHOR               equ 74
PDXB_SAVE_AUTHOR_N             equ 48
PDXB_SAVE_AUTHOR_ESZ           equ 1
PDXB_SAVE_SRC_CLUS             equ 122
PDXB_SAVE_SRC_VOL              equ 124
PDXB_SAVE_BYTES                equ 126

; ---- struct pd_newpage (10 bytes) ------------------------------------------
PDXB_NEWPAGE_W                 equ 0
PDXB_NEWPAGE_H                 equ 2
PDXB_NEWPAGE_ROTATE            equ 4
PDXB_NEWPAGE_CSEG              equ 6
PDXB_NEWPAGE_CLEN              equ 8
PDXB_NEWPAGE_BYTES             equ 10

; ---- struct pd_edit (48 bytes) ---------------------------------------------
PDXB_EDIT_KIND                 equ 0
PDXB_EDIT_FLAGS                equ 1
PDXB_EDIT_PAGE                 equ 2
PDXB_EDIT_RECT                 equ 4
PDXB_EDIT_SRC_NUM              equ 12
PDXB_EDIT_SRC_GEN              equ 14
PDXB_EDIT_ID                   equ 16
PDXB_EDIT_COLOUR               equ 18
PDXB_EDIT_STYLE                equ 19
PDXB_EDIT_SIZE                 equ 20
PDXB_EDIT_ALIGN                equ 21
PDXB_EDIT_TEXT_OFF             equ 22
PDXB_EDIT_TEXT_LEN             equ 24
PDXB_EDIT_PTS_OFF              equ 26
PDXB_EDIT_PTS_N                equ 28
PDXB_EDIT_IRT                  equ 30
PDXB_EDIT_STATE                equ 32
PDXB_EDIT_RESERVED             equ 34
PDXB_EDIT_RESERVED_N           equ 7
PDXB_EDIT_RESERVED_ESZ         equ 2
PDXB_EDIT_BYTES                equ 48

; ---- struct pdxb_crypt (402 bytes) ------------------------------------------
PDXB_CRYPT_V                   equ 0
PDXB_CRYPT_R                   equ 2
PDXB_CRYPT_LENGTH              equ 4
PDXB_CRYPT_P_LO                equ 6
PDXB_CRYPT_P_HI                equ 8
PDXB_CRYPT_STM                 equ 10
PDXB_CRYPT_STR                 equ 12
PDXB_CRYPT_EFF                 equ 14
PDXB_CRYPT_ENCMETA             equ 16
PDXB_CRYPT_O_LEN               equ 18
PDXB_CRYPT_U_LEN               equ 20
PDXB_CRYPT_O                   equ 22
PDXB_CRYPT_O_N                 equ 48
PDXB_CRYPT_O_ESZ               equ 1
PDXB_CRYPT_U                   equ 70
PDXB_CRYPT_U_N                 equ 48
PDXB_CRYPT_U_ESZ               equ 1
PDXB_CRYPT_OE                  equ 118
PDXB_CRYPT_OE_N                equ 32
PDXB_CRYPT_OE_ESZ              equ 1
PDXB_CRYPT_UE                  equ 150
PDXB_CRYPT_UE_N                equ 32
PDXB_CRYPT_UE_ESZ              equ 1
PDXB_CRYPT_PERMS               equ 182
PDXB_CRYPT_PERMS_N             equ 16
PDXB_CRYPT_PERMS_ESZ           equ 1
PDXB_CRYPT_ID0                 equ 198
PDXB_CRYPT_ID0_N               equ 32
PDXB_CRYPT_ID0_ESZ             equ 1
PDXB_CRYPT_ID0_LEN             equ 230
PDXB_CRYPT_PW_LEN              equ 232
PDXB_CRYPT_PW                  equ 234
PDXB_CRYPT_PW_N                equ 128
PDXB_CRYPT_PW_ESZ              equ 1
PDXB_CRYPT_KEY                 equ 362
PDXB_CRYPT_KEY_N               equ 32
PDXB_CRYPT_KEY_ESZ             equ 1
PDXB_CRYPT_KEY_LEN             equ 394
PDXB_CRYPT_AUTH                equ 396
PDXB_CRYPT_STATE               equ 398
PDXB_CRYPT_PROGRESS            equ 400
PDXB_CRYPT_BYTES               equ 402

; ---- struct pdxb_cryptop (344 bytes) ----------------------------------------
PDXB_CRYPTOP_KEY               equ 0
PDXB_CRYPTOP_KEY_N             equ 32
PDXB_CRYPTOP_KEY_ESZ           equ 1
PDXB_CRYPTOP_KEY_LEN           equ 32
PDXB_CRYPTOP_METHOD            equ 34
PDXB_CRYPTOP_OBJNUM            equ 36
PDXB_CRYPTOP_GEN               equ 38
PDXB_CRYPTOP_DIR               equ 40
PDXB_CRYPTOP_SEG               equ 42
PDXB_CRYPTOP_OFF               equ 44
PDXB_CRYPTOP_LEN               equ 46
PDXB_CRYPTOP_ROOM              equ 48
PDXB_CRYPTOP_PHASE             equ 50
PDXB_CRYPTOP_IV                equ 52
PDXB_CRYPTOP_IV_N              equ 16
PDXB_CRYPTOP_IV_ESZ            equ 1
PDXB_CRYPTOP_CARRY             equ 68
PDXB_CRYPTOP_CARRY_N           equ 16
PDXB_CRYPTOP_CARRY_ESZ         equ 1
PDXB_CRYPTOP_NCARRY            equ 84
PDXB_CRYPTOP_RC4               equ 86
PDXB_CRYPTOP_RC4_N             equ 258
PDXB_CRYPTOP_RC4_ESZ           equ 1
PDXB_CRYPTOP_BYTES             equ 344

; ---- struct pdxb_img (40 bytes) --------------------------------------------
PDXB_IMG_OBJNUM                equ 0
PDXB_IMG_GEN                   equ 2
PDXB_IMG_ISEG                  equ 4
PDXB_IMG_IOFF                  equ 6
PDXB_IMG_ILEN                  equ 8
PDXB_IMG_CODEC                 equ 10
PDXB_IMG_W                     equ 12
PDXB_IMG_H                     equ 14
PDXB_IMG_BPC                   equ 16
PDXB_IMG_NCOMP                 equ 18
PDXB_IMG_WANT_W                equ 20
PDXB_IMG_WANT_H                equ 22
PDXB_IMG_FLAGS                 equ 24
PDXB_IMG_OUT_W                 equ 26
PDXB_IMG_OUT_H                 equ 28
PDXB_IMG_OUT_MODE              equ 30
PDXB_IMG_OUT_NCOMP             equ 32
PDXB_IMG_ROWBYTES              equ 34
PDXB_IMG_SCALE                 equ 36
PDXB_IMG_STATE                 equ 38
PDXB_IMG_BYTES                 equ 40

; ---- struct pdxb_imgx (12 bytes) -------------------------------------------
PDXB_IMGX_K                    equ 0
PDXB_IMGX_COLS                 equ 2
PDXB_IMGX_ROWS                 equ 4
PDXB_IMGX_FLAGS                equ 6
PDXB_IMGX_GLOBALS              equ 8
PDXB_IMGX_RESERVED             equ 10
PDXB_IMGX_BYTES                equ 12

%endif ; PDABI_INC
