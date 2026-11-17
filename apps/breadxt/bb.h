/* Breadboard XT original native implementation. SPEC.md 130; no upstream code. */
#ifndef BREADXT_BB_H
#define BREADXT_BB_H
#ifdef BB_HOST
typedef unsigned short BBWord;
typedef short BBInt;
#else
typedef unsigned BBWord;
typedef int BBInt;
#endif
typedef unsigned char BBByte;
#define BB_PARTS 64
#define BB_WIRES 128
#define BB_HOLES 420
#define BB_NETS 64
#define BB_UNKNOWNS 24
#define BB_HISTORY 16
#define BB_SAMPLES 256
#define BB_FILEMAX 3120
#define BB_HEADER_SIZE 48
#define BB_BOARD_PROFILE 1
#define BB_MV_FLOAT (-1)
#define BB_MV_UNKNOWN (-2)
#define BB_MV_FAILED (-3)
#define BB_UA_INVALID (-32767 - 1)
#define BB_CURRENT_MAX 30000
#define BB_PROBE_HOLE 0
#define BB_PROBE_PIN 1
#define BB_PROBE_BRANCH 2
#define BB_NONE ((BBWord)65535u)
#define BB_NNET 255
#define BB_ROT 1
#define BB_CLOSED 2
#define BB_LOW 0
#define BB_HIGH 1
#define BB_X 2
#define BB_Z 3
#define BB_RES 1
#define BB_LED 2
#define BB_SWITCH 3
#define BB_POWER 4
#define BB_GROUND 5
#define BB_POT 6
#define BB_INPUT 7
#define BB_INDICATOR 8
#define BB_CLOCK 9
#define BB_HC00 10
#define BB_HC04 11
#define BB_HC08 12
#define BB_HC32 13
#define BB_HC86 14
#define BB_CAP 15
#define BB_555 16
#define BB_OK 0
#define BB_EHOLE 1
#define BB_EOCCUPIED 2
#define BB_ECAPACITY 3
#define BB_EVALUE 4
#define BB_EDUPLICATE 5
#define BB_EFORMAT 6
#define BB_EVERSION 7
#define BB_ENOGROUND 8
#define BB_EFLOAT 9
#define BB_EDISCONNECTED 10
#define BB_ECONFLICT 11
#define BB_ESHORT 12
#define BB_ESINGULAR 13
#define BB_ENUMERIC 14
#define BB_ECONVERGE 15
#define BB_EPOWER 16
#define BB_ECONTENTION 17
#define BB_EOSCILLATE 18
#define BB_EBRIDGE 19
#define BB_ECONTROL 20
#define BB_ELEDLIMIT 21
#define BB_EIO 22
#define BB_ESUPPLIES 23
#define BB_PART 1
#define BB_WIRE 2
#define BB_NADD 0
#define BB_NSUB 1
#define BB_NMUL 2
#define BB_NDIV 3

typedef struct { BBWord id,type,pin[3],value,param,flags; } BBPart;
typedef struct { BBWord id,a,b,bend[4]; BBByte color,nbend; } BBWire;
typedef struct {
    BBPart part[BB_PARTS]; BBWire wire[BB_WIRES];
    BBWord np,nw,next_id,probe[2],panx,pany,zoom,dt,scope_div,trigger;
    BBWord probe_kind[2],probe_id[2],probe_pin[2];
} BBDoc;
typedef struct { BBWord w[4]; } BBNum;
typedef struct {
    BBInt mv[BB_NETS],ua[BB_PARTS]; BBByte logic[BB_NETS];
    BBWord tlo,thi,diag,part,net;
} BBResult;
typedef struct {
    BBInt mv[2][BB_SAMPLES]; BBWord lo[BB_SAMPLES],hi[BB_SAMPLES];
    BBWord head,count,trigger_at,triggered,undersampled;
    BBWord period_lo,period_hi,high_lo,high_hi,cycles;
} BBScope;
extern BBDoc bb_doc,bb_stage;
extern BBResult bb_result;
extern BBScope bb_scope;
extern BBByte bb_hnet[BB_HOLES];
extern BBWord bb_nnet,bb_error,bb_error_part,bb_error_net,bb_num_error;
extern BBWord bb_dirty,bb_running,bb_valid,bb_recompile;
/* OS-free model/topology; 1 success,0 failure with bb_error, except as noted. */
void bb_clear(BBDoc *doc);
int bb_validate(BBDoc *doc);
int bb_pincount(unsigned type);
BBWord bb_pin(const BBPart *part,unsigned pin); /* physical pin number is 1-based */
int bb_strip(unsigned hole); /* physical strip0..67, -1 invalid */
int bb_edit_part(int index,const BBPart *part); /* -1 add; assigns new stable ID */
int bb_edit_wire(int index,const BBWire *wire);
int bb_delete(unsigned kind,unsigned id); /* stable id, NOT index */
int bb_undo(int redo);
void bb_history_clear(void);
void bb_mark_saved(void);
void bb_view_changed(void);
unsigned bb_history_object(int redo,int after,BBPart *part,BBWire *wire);
void bb_invalidate(void);
int bb_compile(void);
const char *bb_part_name(unsigned type);
const char *bb_diag_text(unsigned code);
/* Opaque shared software arithmetic; never C floating types on target. */
void bb_nset(BBNum *out,int value);
void bb_nuint(BBNum *out,unsigned value);
int bb_nop(BBNum *out,const BBNum *a,const BBNum *b,unsigned op);
int bb_ncmp(const BBNum *a,const BBNum *b);
int bb_nround(const BBNum *a); /* bb_num_error=BB_ENUMERIC on out-of-int-range */
/* Lock-free compiled simulation, publish one completed step atomically. */
int bb_sim_start(int reset);
int bb_sim_slice(unsigned budget); /* 0 pending,1 step committed,-1 stopped/error */
void bb_sim_reset(void);
void bb_scope_reset(void);
void bb_scope_sample(void);
/* OS-free binary codec validates into caller's staged doc only. */
int bb_decode(const BBByte *data,unsigned size,BBDoc *out);
unsigned bb_encode(const BBDoc *doc,BBByte *data,unsigned cap);
int bb_parse_value(unsigned type,const char *s,BBWord *out);
int bb_example(unsigned which); /* fills bb_stage, 0blank,1LED,2divider,3pot,
                                 4switch,5logic,6broken,7RC,8flasher */
const char *bb_example_name(unsigned which);
#ifdef BB_HOST
void bb_copy(void *dest,const void *src,unsigned n);
void bb_zero(void *dest,unsigned n);
#else
#define bb_copy os88_memcpy
#define bb_zero(p,n) os88_memset((p),0,(n))
#endif
#endif
