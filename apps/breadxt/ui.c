/* Breadboard XT native workbench. Original implementation, SPEC.md 130.
 * Numerical/file work runs in lock-free wakes. Paint sees committed snapshots.
 * Small row/plot bands are composed by draw.inc; no screen framebuffer.
 */
#define BU_SELECT 0
#define BU_WIRE 1
#define BU_PROBE 2
#define BU_PART 3
#define BU_MOVE 4
#define BU_BOARD 0
#define BU_PALETTE 1
#define BU_INSPECT 2
#define BU_SCOPE 3
#define BU_HELP 4
#define BU_EXAMPLES 5
#define BU_VALUE 6
#define BU_ABOUT 7
#define BU_ALL 1
#define BU_BOARD_DIRTY 2
#define BU_INFO_DIRTY 4
#define BU_SCOPE_DIRTY 8
#define BU_BAR_DIRTY 16
#define BU_INFO_FULL 32
#define BU_VALUE_DIRTY 64
#define BU_UIKEY 128      /* a user event or a command: re-key every section */
#define BU_OP_NEW 1
#define BU_OP_LOAD 2
#define BU_OP_SAVE 3
#define BU_OP_EXAMPLE 4
#define BU_OP_PART 5
#define BU_OP_WIRE 6
#define BU_OP_DELETE 7
#define BU_OP_UNDO 8
#define BU_OP_REDO 9
#define BU_OP_RUN 10
#define BU_OP_STEP 11
#define BU_OP_RESET 12
#define BU_ACT_NEW 1
#define BU_ACT_OPEN 2
#define BU_ACT_SAVE 3
#define BU_ACT_SAVEAS 4
#define BU_ACT_EXAMPLE 5
#define BU_ACT_CLOSE 6

void bb_timefmt(char *out,unsigned lo,unsigned hi);
unsigned bb_muldiv(unsigned a,unsigned b,unsigned denominator);
int bb_ask(const char *message,void *window,int buttons);
void bb_alertdone(int button);

static void *bu_win;
static struct os88_pt bu_org;
static struct os88_size bu_size;
static struct os88_video bu_video;
static struct os88_rect bu_damage;
static struct os88_place bu_doc_place,bu_choice_place,bu_launch_place;
static char bu_name[13],bu_choice[13],bu_title[64];
static char bu_line[80],bu_num[8],bu_edit[24],bu_msg[64];
static char bu_title_scope[48];
static unsigned bu_artseg,bu_io_example,bu_bandseg;
static struct os88_place bu_pkg_place;
/* UI callbacks are serialized. A file operation completes before any draw
 * in its wake; no retained bitmap points into this workspace. */
static BBByte bu_workbits[BB_FILEMAX];
#define bu_filebits bu_workbits
#define BU_BANDP 1024                   /* paragraphs: 16KB */
void bx_setup(unsigned seg,void *band,unsigned cap,unsigned artseg,int bpp);
extern unsigned bx_bseg;
static void bu_bxsetup(int planes)
{
    if(bu_bandseg)bx_setup(bu_bandseg,0,BU_BANDP*16,bu_artseg,planes);
    else bx_setup(0,bu_workbits,BB_FILEMAX,bu_artseg,planes);
}
/* The compactor moved a claim: the band is the only one this package keeps
 * (the art is a part, which the kernel tracks). Assignments only (66.3). */
void os88_onmove(unsigned was,unsigned now)
{
    if(bu_bandseg&&bu_bandseg==was){bu_bandseg=now;bx_bseg=now;}
}
static BBPart bu_part;
static BBWire bu_wire;
static BBWord bu_tool,bu_type,bu_mode,bu_channel,bu_netshow;
static BBWord bu_selected,bu_selected_kind,bu_hole,bu_selected_pin;
static BBWord bu_first,bu_second,bu_third,bu_ready,bu_wire_n,bu_color;
static BBWord bu_op,bu_arg,bu_after,bu_after_arg,bu_asking,bu_saveas_wait;
static BBWord bu_io_as,bu_io_hi,bu_io_size,bu_io_association;
static BBWord bu_active,bu_step_once,bu_flags,bu_initialized,bu_timer_ok;
static BBWord bu_scope_on,bu_help_page,bu_edit_param,bu_restore_place,bu_capture_reset;
static BBWord bu_lastdraw,bu_speed_tick,bu_speed_lo,bu_speed_hi,bu_speed;
static BBWord bu_last_scope,bu_diag_stale,bu_cache_ok,bu_paint_region,bu_closing;
static BBWord bu_title_dirty=BB_NONE;
static char bu_title_name[13],bu_timebuf[20];
static int bu_edit_index,bu_edit_len;
static int bu_x,bu_y,bu_w,bu_h,bu_compact,bu_pitch;
static int bu_bx,bu_by,bu_bw,bu_bh,bu_gx,bu_gy,bu_cols,bu_rows;
static int bu_scope_y,bu_scope_h;
static int bu_dirty_top,bu_dirty_bottom;
static void bu_result_changed(void);
static int ovl_bu_damage_span(int top,int bottom);
static int ovl_bu_damage_selection(void);
static int ovl_bu_locate_error(void);
static const char *bu_files[]={"New","Open...","Save","Save As...","Examples...","Close"};
static const char *bu_edits[]={"Undo","Redo","Delete","Rotate","Move part","Reroute wire","Wire color","Value / parameter"};
static const char *bu_parts[]={"Resistor","LED","Capacitor","Switch","Power","Ground","Potentiometer","All parts..."};
static const char *bu_sims[]={"Run","Pause","Step","Reset","Smaller timestep","Larger timestep"};
static const char *bu_views[]={"Breadboard","Parts palette","Inspector","Oscilloscope","Zoom","Net highlight","Help","Examples","Net list"};
static struct os88_menuset bu_menus={"BreadXT",0,5,
    {{"File",bu_files,6},{"Edit",bu_edits,8},{"Parts",bu_parts,8},
     {"Simulate",bu_sims,6},{"View",bu_views,9}}};
static const char *bu_about[]={"Breadboard XT 1.0","Educational DC / logic / RC simulator",
    "Ideal pin-level NE555 timer","Original native os8088 implementation",
    "Workflow inspiration: David Shah","Breadboard Simulator (2014-2016)",0};

static void bu_append(const char *s)
{
    unsigned n;
    n=os88_strlen(bu_line);
    if(n<sizeof(bu_line)-1) os88_strcpy(bu_line+n,s,sizeof(bu_line)-n);
}
static void bu_number(unsigned n)
{
    os88_utoa(n,bu_num); bu_append(bu_num);
}
static void bu_begin(const char *s){os88_strcpy(bu_line,s,sizeof(bu_line));}
static int bu_min(int a,int b){return a<b?a:b;}
static int bu_abs(int a){return a<0?-a:a;}
static void bu_note(const char *s)
{
    os88_strcpy(bu_msg,s,sizeof(bu_msg)); bu_flags|=BU_INFO_DIRTY;
}
static void bu_error(void){bu_note(bb_diag_text(bb_error));}
static int bu_part_index(unsigned id)
{
    unsigned i; for(i=0;i<bb_doc.np;i++)if(bb_doc.part[i].id==id)return i;return -1;
}
static int bu_wire_index(unsigned id)
{
    unsigned i;for(i=0;i<bb_doc.nw;i++)if(bb_doc.wire[i].id==id)return i;return -1;
}
static int bu_pick_index(void)
{
    if(bu_selected_kind==BB_PART)return bu_part_index(bu_selected);
    if(bu_selected_kind==BB_WIRE)return bu_wire_index(bu_selected);return -1;
}
static void bu_hole_name(unsigned h)
{
    static char row[4];
    if(h>=BB_HOLES){bu_append("--");return;}
    if(h/30>=2&&h/30<=11){row[0]='a'+h/30-2;row[1]=0;bu_append(row);}
    else{if(h/30<2)bu_append("T");else bu_append("B");bu_append((h/30==0||h/30==12)?"+":"-");}
    bu_number(h%30+1);
}
static const char *bu_prefix(unsigned type)
{
    if(type==BB_RES)return "R";if(type==BB_CAP)return "C";if(type==BB_LED)return "D";
    if(type==BB_SWITCH)return "S";if(type==BB_POT)return "P";if(type==BB_POWER)return "V";
    if(type==BB_GROUND)return "G";if(type==BB_INPUT)return "IN";if(type==BB_CLOCK)return "CK";
    if(type==BB_INDICATOR)return "L";return "U";
}
static void bu_ident(const BBPart *part){bu_append(bu_prefix(part->type));bu_number(part->id);}
static int ovl_bu_probe_mv(unsigned channel)
{
    BBWord h;int i;
    if(!bb_valid||bb_result.diag)return BB_MV_FAILED;
    h=bb_doc.probe[channel];
    if(bb_doc.probe_kind[channel]==BB_PROBE_BRANCH)return BB_MV_FAILED;
    if(bb_doc.probe_kind[channel]==BB_PROBE_PIN){
        i=bu_part_index(bb_doc.probe_id[channel]);if(i<0)return BB_MV_FAILED;
        h=bb_pin(&bb_doc.part[i],bb_doc.probe_pin[channel]);
    }
    if(h>=BB_HOLES||bb_hnet[h]>=BB_NETS)return BB_MV_FLOAT;
    return bb_result.mv[bb_hnet[h]];
}
static const char *bu_timer_pins[]={"","GND","TRIG","OUT","RESET","CONT","THRES","DISCH","VCC"};
static const BBByte bu_quad_gate[]={1,1,1,2,2,2,0,3,3,3,4,4,4,0};
static const char bu_quad_role[]="ABYABYGYABYABV";
static int ovl_bu_pin_caption(unsigned type,unsigned pin)
{
    static char role[2];
    if(type==BB_555&&pin>=1&&pin<=8){bu_append(bu_timer_pins[pin]);return 1;}
    if(type>=BB_HC00&&type<=BB_HC86){
        if(pin==7)bu_append("GND");else if(pin==14)bu_append("VCC");
        else if(type==BB_HC04){bu_number(pin<7?(pin+1)/2:(pin+2)/2-1);bu_append((pin&1)?"A":"Y");}
        else if(pin>=1&&pin<=14){bu_number(bu_quad_gate[pin-1]);role[0]=bu_quad_role[pin-1];role[1]=0;bu_append(role);}
    }else if(type==BB_LED)bu_append(pin==1?"Anode":"Cathode");
    else if(type==BB_CAP||type==BB_POWER)bu_append(pin==1?"+":"-");
    else if(type==BB_POT)bu_append(pin==3?"Wiper":"End");
    return 1;
}
#include "face.c"
static int ovl_bu_help_next(void)
{bu_help_page++;bu_flags|=BU_ALL;return 1;}
static void bu_redraw_locked(void *win)
{
    bu_paint_region=0;if(os88_wm_clip_set(win)==0)bu_render(win,0);
}
static void bu_wake(void){os88_wm_wake(bu_win);}
static int ovl_bu_view_changed(void)
{bb_view_changed();bu_flags|=BU_INFO_DIRTY;bu_wake();return 1;}
static int ovl_bu_speed_reset(void)
{bu_speed_tick=os88_ticks();bu_speed_lo=bb_result.tlo;bu_speed_hi=bb_result.thi;bu_speed=0;bu_diag_stale=0;return 1;}
static int ovl_bu_locate_error(void)
{
    int idx,col,row;unsigned i;BBWord h;
    if(bb_recompile||bu_diag_stale){bu_note("Run/Step to update");return 1;}
    h=BB_NONE;idx=bu_part_index(bb_result.part);ovl_bu_damage_selection();
    if(idx>=0){bu_selected=bb_doc.part[idx].id;bu_selected_kind=BB_PART;bu_selected_pin=1;h=bb_pin(&bb_doc.part[idx],1);}
    else if(bb_result.net<bb_nnet){for(i=0;i<BB_HOLES;i++)if(bb_hnet[i]==bb_result.net){h=i;break;}}
    if(h>=BB_HOLES){bu_note("Error has no physical location");os88_toast("Error has no location",36);return 1;}
    bu_hole=h;col=h%30;row=h/30;
    if(col<(int)bb_doc.panx||col>=(int)bb_doc.panx+bu_cols||!bu_visible(h)){
        bb_doc.panx=col>1?col-1:0;bb_doc.pany=row>1?row-1:0;ovl_bu_view_changed();bu_flags|=BU_ALL;
    }else ovl_bu_damage_selection();
    if(bu_mode!=BU_BOARD||bu_view!=BU_VBOARD)bu_flags|=BU_ALL;     /* from the scope or net list too */
    bu_mode=BU_BOARD;bu_view=BU_VBOARD;bu_flags|=BU_INFO_DIRTY|BU_INFO_FULL;bu_note("Located diagnostic; inspect selected pin");return 1;
}
static void bu_queue(unsigned op,unsigned arg)
{
    if(bu_op){bu_note("Busy: finish pending edit");return;}
    bu_op=op;bu_arg=arg;bu_wake();
}
static int ovl_bu_title_update(void)
{
    int i,same;same=1;
    for(i=0;i<13;i++)if(bu_title_name[i]!=bu_name[i])same=0;
    if(same&&bu_title_dirty==bb_dirty)return 1;
    bu_title_dirty=bb_dirty;os88_strcpy(bu_title_name,bu_name,sizeof(bu_title_name));
    os88_strcpy(bu_title,bu_name[0]?bu_name:"UNTITLED.BBX",sizeof(bu_title));
    os88_strcpy(bu_title+os88_strlen(bu_title),bb_dirty?" * - Breadboard XT":" - Breadboard XT",sizeof(bu_title)-os88_strlen(bu_title));
    os88_wm_title(bu_win,bu_title);

    return 1;
}
static void bu_reset_tools(void)
{
    bu_first=BB_NONE;bu_second=BB_NONE;bu_third=BB_NONE;bu_ready=0;bu_wire_n=0;bu_edit_index=-1;
}
static void bu_user_begin(void)
{
    if(bu_saveas_wait){bu_saveas_wait=0;bu_after=0;bu_after_arg=0;bu_restore_place=1;bu_wake();}
    bu_msg[0]=0;bu_flags|=BU_UIKEY;
}
static int ovl_bu_save(int as)
{
    if(as||!bu_name[0]){
        bu_saveas_wait=1;bu_io_as=1;
        if(os88_file_dlg(OS88_FDLG_SAVE,bu_win,bu_name[0]?bu_name:"CIRCUIT.BBX")!=0){bu_saveas_wait=0;bu_after=0;bu_note("File dialog unavailable");}
    }else{bu_io_as=0;os88_strcpy(bu_choice,bu_name,sizeof(bu_choice));bu_queue(BU_OP_SAVE,0);}

    return 1;
}
static int ovl_bu_action_now(unsigned action,unsigned arg)
{
    if(action==BU_ACT_NEW)bu_queue(BU_OP_NEW,0);
    else if(action==BU_ACT_OPEN){if(os88_file_dlg(OS88_FDLG_OPEN,bu_win,0)!=0)bu_note("File dialog unavailable");}
    else if(action==BU_ACT_EXAMPLE)bu_queue(BU_OP_EXAMPLE,arg);
    else if(action==BU_ACT_SAVE)ovl_bu_save(0);
    else if(action==BU_ACT_SAVEAS)ovl_bu_save(1);
    else if(action==BU_ACT_CLOSE){bu_closing=1;os88_wm_close(bu_win);}

    return 1;
}
static int ovl_bu_request(unsigned action,unsigned arg)
{
    if(bb_dirty&&(action==BU_ACT_NEW||action==BU_ACT_OPEN||action==BU_ACT_EXAMPLE||action==BU_ACT_CLOSE)){
        bu_after=action;bu_after_arg=arg;
        if(bu_asking)return 1;
        if(bb_ask("Save changes to circuit?",bu_win,2)==0)bu_asking=1;
        else{bu_after=0;bu_note("Save question unavailable");}
        return 1;
    }
    ovl_bu_action_now(action,arg);

    return 1;
}
void bb_alertdone(int button)
{
    unsigned action,arg;bu_asking=0;action=bu_after;arg=bu_after_arg;
    if(button==0){ovl_bu_save(0);return;}
    bu_after=0;bu_after_arg=0;
    if(button==1)ovl_bu_action_now(action,arg);
    else bu_note("Canceled");
    if(bu_closing)return;
    bu_flags|=BU_ALL;bu_redraw_locked(bu_win);
}
static int ovl_bu_after_save(void)
{
    unsigned action,arg;action=bu_after;arg=bu_after_arg;bu_after=0;bu_after_arg=0;
    if(action){os88_gfx_lock();ovl_bu_action_now(action,arg);os88_gfx_unlock();}

    return 1;
}
static int ovl_bu_samename(const char *a)
{
    unsigned i;for(i=0;a[i]&&bu_name[i]==a[i];i++){}return !a[i]&&!bu_name[i];
}
static int ovl_bu_io(unsigned op)
{
    unsigned n;int ok;
    os88_file_here(&bu_choice_place);
    if(op==BU_OP_LOAD){
        if(bu_io_association){
            if(os88_file_goto_q_mark(bu_launch_place.clus,bu_launch_place.vol)!=0){bb_error=BB_EIO;bu_error();return 1;}
            os88_file_here(&bu_choice_place);bu_io_association=0;
        }
        if(bu_io_example&&os88_file_goto_q_mark(bu_pkg_place.clus,bu_pkg_place.vol)!=0){bu_io_example=0;bb_error=BB_EIO;bu_error();return 1;}
        if(bu_io_hi||bu_io_size>BB_FILEMAX){bb_error=BB_ECAPACITY;bu_error();return 1;}
        n=os88_file_read(bu_choice,bu_filebits,BB_FILEMAX);
        if(os88_ferr()!=OS88_FERR_OK){bb_error=BB_EIO;bu_error();ok=0;}
        else ok=bb_decode(bu_filebits,n,&bb_stage);
        if(!ok){bu_io_example=0;bu_error();os88_file_goto_q_mark(bu_doc_place.clus,bu_doc_place.vol);return 1;}
        bb_copy(&bb_doc,&bb_stage,sizeof(bb_doc));bb_history_clear();bb_mark_saved();bb_invalidate();bb_dirty=0;
        bb_sim_reset();ovl_bu_speed_reset();bu_active=0;bu_step_once=0;bu_selected=0;bu_selected_kind=0;bu_hole=BB_NONE;bu_reset_tools();
        os88_strcpy(bu_name,bu_choice,sizeof(bu_name));
        if(bu_io_example){bu_example=bu_io_example;bu_io_example=0;bu_name[0]=0;bb_dirty=1;os88_file_goto_q_mark(bu_doc_place.clus,bu_doc_place.vol);}
        else{bb_copy(&bu_doc_place,&bu_choice_place,sizeof(bu_doc_place));for(bu_example=8;bu_example&&!ovl_bu_samename(bb_example_name(bu_example));bu_example--);}
        bu_mode=BU_BOARD;bu_view=BU_VBOARD;bu_note(bu_example&&!bu_name[0]?"Example is an untitled copy; Space runs":"Opened editable BBX project");bu_result_changed();bu_flags|=BU_ALL;
    }else{
        if(!bu_io_as&&os88_file_goto_q_mark(bu_doc_place.clus,bu_doc_place.vol)!=0){bb_error=BB_EIO;bu_error();bu_after=0;return 1;}
        n=bb_encode(&bb_doc,bu_filebits,BB_FILEMAX);
        if(!n){bu_error();bu_after=0;return 1;}
        if(os88_file_write(bu_choice,bu_filebits,n)!=0){bb_error=BB_EIO;bu_error();bu_after=0;os88_file_goto_q_mark(bu_doc_place.clus,bu_doc_place.vol);return 1;}
        os88_strcpy(bu_name,bu_choice,sizeof(bu_name));os88_file_here(&bu_doc_place);bb_mark_saved();
        bu_note("Saved complete BBX project");ovl_bu_after_save();
    }
    return 1;
}
static void ovl_bu_edited(void)
{
    bu_active=0;bu_step_once=0;bu_diag_stale=1;bu_note("Edited; results stale");bu_result_changed();bu_flags|=BU_INFO_DIRTY|BU_INFO_FULL|BU_BAR_DIRTY|BU_SCOPE_DIRTY;
}
static int ovl_bu_execute(unsigned op,unsigned arg)
{
    int ok,idx,shape;unsigned n;
    if(op==BU_OP_LOAD||op==BU_OP_SAVE)return ovl_bu_io(op);
    if(op==BU_OP_NEW||op==BU_OP_EXAMPLE){
        if(op==BU_OP_EXAMPLE){bu_io_example=arg;os88_strcpy(bu_choice,bb_example_name(arg),sizeof(bu_choice));
            bu_io_hi=0;bu_io_size=0;bu_io_association=0;return ovl_bu_io(BU_OP_LOAD);}
        bb_clear(&bb_stage);
        n=bb_encode(&bb_stage,bu_filebits,BB_FILEMAX);
        if(!n||!bb_decode(bu_filebits,n,&bb_stage)){bu_error();return 1;}
        bb_copy(&bb_doc,&bb_stage,sizeof(bb_doc));bb_history_clear();bb_invalidate();bb_sim_reset();ovl_bu_speed_reset();
        bu_active=0;bu_step_once=0;bu_selected=0;bu_selected_kind=0;bu_hole=BB_NONE;bu_reset_tools();
        bu_name[0]=0;bu_example=op==BU_OP_EXAMPLE?arg:0;bu_view=BU_VBOARD;if(op==BU_OP_NEW)bb_mark_saved();else bb_dirty=1;bu_mode=BU_BOARD;bu_result_changed();bu_flags|=BU_ALL;
        bu_note(op==BU_OP_NEW?"Choose Parts; click pins; Enter":"Example is editable; Space to run");return 1;
    }
    if(op==BU_OP_RUN||op==BU_OP_STEP){
        if(bb_sim_start(0)){
            ovl_bu_speed_reset();bu_active=1;bu_step_once=op==BU_OP_STEP;bb_running=op==BU_OP_RUN;bu_msg[0]=0;
            bu_flags|=BU_BAR_DIRTY|BU_INFO_DIRTY|BU_SCOPE_DIRTY;
        }else{bu_active=0;bb_running=0;bu_error();bu_result_changed();bu_flags|=BU_INFO_DIRTY|BU_BAR_DIRTY|BU_SCOPE_DIRTY;}
        return 1;
    }
    if(op==BU_OP_RESET){bb_running=0;bb_sim_reset();ovl_bu_speed_reset();bu_active=0;bu_step_once=0;bu_note("Reset to initial conditions");bu_result_changed();bu_flags|=BU_SCOPE_DIRTY|BU_INFO_DIRTY|BU_BAR_DIRTY;return 1;}
    ok=0;shape=1;
    if(op==BU_OP_PART&&bu_edit_index>=0){
        idx=bu_edit_index;shape=bb_doc.part[idx].type!=bu_part.type||bb_doc.part[idx].flags!=bu_part.flags||bu_part.type==BB_INPUT;
        for(n=0;n<3;n++)if(bb_doc.part[idx].pin[n]!=bu_part.pin[n])shape=1;
        if(shape)ovl_bu_damage_part(&bb_doc.part[idx]);
    }
    if(op==BU_OP_PART&&!shape)ovl_bu_damage_net();else ovl_bu_damage_selection();
    if(op==BU_OP_UNDO||op==BU_OP_REDO)for(idx=0;idx<2;idx++){
        n=bb_history_object(op==BU_OP_REDO,idx,&bu_part,&bu_wire);
        if(n==BB_PART)ovl_bu_damage_part(&bu_part);else if(n==BB_WIRE)ovl_bu_damage_wire(&bu_wire);
    }
    if(op==BU_OP_WIRE&&bu_edit_index>=0)ovl_bu_damage_wire(&bb_doc.wire[bu_edit_index]);
    if(op==BU_OP_DELETE){idx=bu_pick_index();if(idx>=0){if(bu_selected_kind==BB_PART)ovl_bu_damage_part(&bb_doc.part[idx]);else ovl_bu_damage_wire(&bb_doc.wire[idx]);}}
    if(op==BU_OP_PART){ok=bb_edit_part(bu_edit_index,&bu_part);if(ok){bu_selected_kind=BB_PART;bu_selected=bu_edit_index<0?bb_doc.part[bb_doc.np-1].id:bb_doc.part[bu_edit_index].id;}}
    else if(op==BU_OP_WIRE){ok=bb_edit_wire(bu_edit_index,&bu_wire);if(ok){bu_selected_kind=BB_WIRE;bu_selected=bu_edit_index<0?bb_doc.wire[bb_doc.nw-1].id:bb_doc.wire[bu_edit_index].id;}}
    else if(op==BU_OP_DELETE){ok=bb_delete(bu_selected_kind,bu_selected);if(ok){bu_selected=0;bu_selected_kind=0;}}
    else if(op==BU_OP_UNDO)ok=bb_undo(0);
    else if(op==BU_OP_REDO)ok=bb_undo(1);
    if(ok){
        if(op==BU_OP_PART&&shape){idx=bu_part_index(bu_selected);if(idx>=0)ovl_bu_damage_part(&bb_doc.part[idx]);}
        if(op==BU_OP_WIRE){idx=bu_wire_index(bu_selected);if(idx>=0)ovl_bu_damage_wire(&bb_doc.wire[idx]);}
        bu_reset_tools();if(op!=BU_OP_PART||shape)ovl_bu_damage_selection();ovl_bu_edited();}else bu_error();
    return 1;
}
static int ovl_bu_tool(unsigned tool)
{
    unsigned old;old=bu_mode;ovl_bu_damage_selection();bu_tool=tool;bu_mode=BU_BOARD;bu_reset_tools();
    if(tool==BU_WIRE)bb_zero(&bu_wire,sizeof(bu_wire));
    bu_flags|=old==BU_BOARD?BU_BAR_DIRTY|BU_INFO_DIRTY:BU_ALL;return 1;
}
static int ovl_bu_choose_type(unsigned type)
{
    if(bu_mode!=BU_BOARD||bu_view!=BU_VBOARD)bu_flags|=BU_ALL;
    ovl_bu_damage_selection();bu_type=type;bu_tool=BU_PART;bu_mode=BU_BOARD;bu_view=BU_VBOARD;bu_reset_tools();
    bu_note("Click each physical pin; Enter commits");

    return 1;
}
static int ovl_bu_default_part(void)
{
    bb_zero(&bu_part,sizeof(bu_part));bu_part.type=bu_type;
    bu_part.pin[0]=BB_NONE;bu_part.pin[1]=BB_NONE;bu_part.pin[2]=BB_NONE;
    if(bu_type==BB_RES)bu_part.value=33;
    else if(bu_type==BB_CAP)bu_part.value=10000;
    else if(bu_type==BB_POT){bu_part.value=1000;bu_part.param=500;}
    else if(bu_type==BB_POWER)bu_part.value=5000;
    else if(bu_type==BB_CLOCK){bu_part.value=10000;bu_part.param=500;}
    else if(bu_type==BB_SWITCH)bu_part.flags=BB_CLOSED;

    return 1;
}
static int ovl_bu_wire_preview(unsigned endpoint)
{
    unsigned col,row;
    bu_wire.a=bu_first;bu_wire.b=endpoint;bu_wire.color=bu_color;bu_wire.nbend=bu_wire_n;
    if(bu_wire_n<2){bu_wire.bend[2]=0;bu_wire.bend[3]=0;}
    if(!bu_wire_n){bu_wire.bend[0]=0;bu_wire.bend[1]=0;}
    if(!bu_wire_n&&bu_first%30!=endpoint%30&&bu_first/30!=endpoint/30){
        bu_wire.nbend=1;bu_wire.bend[0]=endpoint%30;bu_wire.bend[1]=bu_first/30;
    }
    bu_second=endpoint;bu_ready=1;ovl_bu_damage_wire(&bu_wire);bu_flags|=BU_INFO_DIRTY;

    return 1;
}
static int ovl_bu_place_click(unsigned h)
{
    int need;
    if(bu_ready){ovl_bu_damage_part(&bu_part);bu_reset_tools();}
    if(bu_first==BB_NONE){ovl_bu_default_part();bu_first=h;bu_part.pin[0]=h;bu_edit_index=-1;}
    else if(bu_second==BB_NONE){bu_second=h;bu_part.pin[1]=h;}
    else{bu_third=h;bu_part.pin[2]=h;}
    need=bb_pincount(bu_type);
    if(bu_type>=BB_HC00&&bu_type<=BB_HC86||bu_type==BB_555){
        if(h/30!=7||h%30+need/2>30){bu_reset_tools();bu_note("DIP anchor must be row f; fit all pins");return 1;}
        bu_ready=1;
    }else if(need==1||need==2&&bu_second!=BB_NONE||need==3&&bu_third!=BB_NONE)bu_ready=1;
    if(bu_ready)ovl_bu_damage_part(&bu_part);bu_flags|=BU_INFO_DIRTY;

    return 1;
}
static int ovl_bu_select_at(unsigned h,int x,int y)
{
    unsigned i,p,pins;int l,t,r,b,px,py;BBWord ph;
    bu_selected=0;bu_selected_kind=0;bu_selected_pin=0;
    for(i=0;i<bb_doc.np;i++){
        pins=bb_pincount(bb_doc.part[i].type);l=32767;t=32767;r=-32767;b=-32767;
        for(p=1;p<=pins;p++){
            ph=bb_pin(&bb_doc.part[i],p);px=bu_hx(ph);py=bu_hy(ph);
            if(ph==h){bu_selected=bb_doc.part[i].id;bu_selected_kind=BB_PART;bu_selected_pin=p;return 1;}
            if(px<l)l=px;if(px>r)r=px;if(py<t)t=py;if(py>b)b=py;
        }
        if(x>=l-7&&x<=r+7&&y>=t-7&&y<=b+7){bu_selected=bb_doc.part[i].id;bu_selected_kind=BB_PART;return 1;}
    }
    for(i=0;i<bb_doc.nw;i++){
        if(bb_doc.wire[i].a==h||bb_doc.wire[i].b==h){bu_selected=bb_doc.wire[i].id;bu_selected_kind=BB_WIRE;bu_selected_pin=bb_doc.wire[i].a==h?1:2;return 1;}
        px=bu_hx(bb_doc.wire[i].a);py=bu_hy(bb_doc.wire[i].a);
        for(p=0;p<=bb_doc.wire[i].nbend;p++){
            ph=p<bb_doc.wire[i].nbend?bb_doc.wire[i].bend[p*2+1]*30+bb_doc.wire[i].bend[p*2]:bb_doc.wire[i].b;
            l=bu_hx(ph);t=bu_hy(ph);
            if(px==l&&bu_abs(x-px)<=3&&y>=bu_min(py,t)-3&&y<=(py>t?py:t)+3||py==t&&bu_abs(y-py)<=3&&x>=bu_min(px,l)-3&&x<=(px>l?px:l)+3){bu_selected=bb_doc.wire[i].id;bu_selected_kind=BB_WIRE;return 1;}
            px=l;py=t;
        }
    }

    return 1;
}
static int ovl_bu_probe_bind(unsigned h,int x,int y)
{
    ovl_bu_select_at(h,x,y);
    bb_doc.probe[bu_channel]=BB_NONE;bb_doc.probe_id[bu_channel]=0;bb_doc.probe_pin[bu_channel]=0;bb_doc.probe_kind[bu_channel]=BB_PROBE_HOLE;
    if(bu_selected_kind==BB_PART){
        bb_doc.probe_id[bu_channel]=bu_selected;
        if(bu_selected_pin){bb_doc.probe_kind[bu_channel]=BB_PROBE_PIN;bb_doc.probe_pin[bu_channel]=bu_selected_pin;}
        else{bb_doc.probe_kind[bu_channel]=BB_PROBE_BRANCH;bb_doc.probe_pin[bu_channel]=1;}
    }else bb_doc.probe[bu_channel]=h;
    bu_capture_reset=1;ovl_bu_view_changed();ovl_bu_damage_selection();bu_flags|=BU_INFO_DIRTY|BU_SCOPE_DIRTY;

    return 1;
}
static int ovl_bu_move_click(unsigned h)
{
    int idx,dx,dy,i,col,row;BBWord old;
    idx=bu_part_index(bu_selected);if(idx<0){bu_tool=BU_SELECT;return 1;}
    bb_copy(&bu_part,&bb_doc.part[idx],sizeof(bu_part));old=bb_pin(&bu_part,1);
    dx=(int)(h%30)-(int)(old%30);dy=(int)(h/30)-(int)(old/30);
    for(i=0;i<3;i++)if(bu_part.pin[i]!=BB_NONE){
        col=(int)(bu_part.pin[i]%30)+dx;row=(int)(bu_part.pin[i]/30)+dy;
        if(col<0||col>=30||row<0||row>=14){bu_note("Move would leave board");return 1;}bu_part.pin[i]=row*30+col;
    }
    bu_edit_index=idx;bu_ready=1;bu_tool=BU_PART;bu_type=bu_part.type;ovl_bu_damage_part(&bu_part);bu_note("Move preview: Enter or Esc");

    return 1;
}
static int ovl_bu_rotate(void)
{
    int idx;idx=bu_selected_kind==BB_PART?bu_part_index(bu_selected):-1;
    if(idx<0){bu_note("Select a part to rotate");return 1;}
    bb_copy(&bu_part,&bb_doc.part[idx],sizeof(bu_part));bu_part.flags^=BB_ROT;bu_edit_index=idx;bu_queue(BU_OP_PART,0);

    return 1;
}
static int ovl_bu_parameter(int param)
{
    int idx;idx=bu_selected_kind==BB_PART?bu_part_index(bu_selected):-1;
    if(idx<0){bu_note("Select a part first");return 1;}
    if(param){if(bb_doc.part[idx].type!=BB_CAP&&bb_doc.part[idx].type!=BB_POT&&bb_doc.part[idx].type!=BB_INPUT&&bb_doc.part[idx].type!=BB_CLOCK){bu_note("Part has no editable parameter");return 1;}}
    else if(bb_doc.part[idx].type!=BB_RES&&bb_doc.part[idx].type!=BB_POT&&bb_doc.part[idx].type!=BB_CAP&&bb_doc.part[idx].type!=BB_POWER&&bb_doc.part[idx].type!=BB_CLOCK){bu_note("Part has no editable value");return 1;}
    bu_edit_param=param;bu_edit[0]=0;bu_edit_len=0;bu_mode=BU_VALUE;bu_flags|=bu_compact?BU_ALL:BU_INFO_DIRTY|BU_INFO_FULL;

    return 1;
}
static int ovl_bu_parse_unsigned(const char *s,BBWord *out)
{
    unsigned value,d;value=0;if(!*s)return 0;
    while(*s){if(*s<'0'||*s>'9')return 0;d=*s++-'0';if(value>6553||(value==6553&&d>5))return 0;value=(value<<3)+(value<<1)+d;}*out=value;return 1;
}
static int ovl_bu_value_accept(void)
{
    static BBWord parsed;
    int idx;idx=bu_part_index(bu_selected);if(idx<0)return 1;
    bb_copy(&bu_part,&bb_doc.part[idx],sizeof(bu_part));
    if(bu_edit_param){
        if(!ovl_bu_parse_unsigned(bu_edit,&parsed)){bu_note("Use a nonnegative integer parameter");return 1;}
        bu_part.param=parsed;
    }else if(!bb_parse_value(bu_part.type,bu_edit,&bu_part.value)){bu_error();return 1;}
    bu_edit_index=idx;bu_queue(BU_OP_PART,0);bu_mode=BU_BOARD;bu_flags|=bu_compact?BU_ALL:BU_INFO_DIRTY|BU_INFO_FULL;

    return 1;
}
static int ovl_bu_toggle(void)
{
    int idx;idx=bu_selected_kind==BB_PART?bu_part_index(bu_selected):-1;
    if(idx>=0&&(bb_doc.part[idx].type==BB_SWITCH||bb_doc.part[idx].type==BB_INPUT)){
        bb_copy(&bu_part,&bb_doc.part[idx],sizeof(bu_part));
        if(bu_part.type==BB_SWITCH)bu_part.flags^=BB_CLOSED;else bu_part.param=bu_part.param==1?0:1;
        bu_edit_index=idx;bu_queue(BU_OP_PART,0);
    }else if(bb_running){bb_running=0;bu_flags|=BU_BAR_DIRTY|BU_INFO_DIRTY|BU_SCOPE_DIRTY;bu_note("Paused; state retained");}
    else bu_queue(BU_OP_RUN,0);

    return 1;
}
static void ovl_bu_pan(int dx,int dy)
{
    int col,row;col=(int)bb_doc.panx+dx;row=(int)bb_doc.pany+dy;
    if(col<0)col=0;if(col>29)col=29;if(row<0)row=0;if(row>13)row=13;
    if(bb_doc.panx==col&&bb_doc.pany==row)return;bb_doc.panx=col;bb_doc.pany=row;ovl_bu_view_changed();bu_flags|=BU_ALL;
}
void os88_paint(void *win)
{
    unsigned flags,cache;int top,bottom;
    flags=bu_flags;cache=bu_cache_ok;top=bu_dirty_top;bottom=bu_dirty_bottom;
    bu_paint_region=os88_wm_damage(win,&bu_damage)==0;bu_render(win,1);
    if(bu_paint_region){bu_flags|=flags;
        bu_dirty_top=top;bu_dirty_bottom=bottom;bu_cache_ok=cache;if(bu_flags)bu_wake();}
    bu_paint_region=0;
}
void os88_onresize(int w,int h,void *win)
{
    (void)w;(void)h;(void)win;bu_flags|=BU_ALL;bu_cache_ok=0;
}
void os88_onmouseup(int x,int y,void *win){(void)x;(void)y;(void)win;}
void os88_onwheel(int notches,void *win)
{
    if(bu_op||bu_asking||bu_mode==BU_ABOUT)return;bu_user_begin();ovl_bu_pan(0,notches>0?-3:3);bu_redraw_locked(win);
}
static int bu_hit(int s,int x,int y){return bu_r1[s]<=bu_r3[s]&&x>=bu_r1[s]&&x<=bu_r3[s]&&y>=bu_r2[s]&&y<=bu_r4[s];}
/* The inspector's spinner (SPEC.md 130.6.5): the next value up or down a
 * preferred series, committed as one ordinary undoable part edit. */
static const unsigned bu_decade[]={1,10,100,1000};
static unsigned bu_cand(unsigned type,int k)
{
    static const BBByte e12[]={10,12,15,18,22,27,33,39,47,56,68,82};
    static const unsigned capv[]={1,2,5,10,22,47,100,220,470,1000,2200,4700,10000,22000,47000u};
    static const unsigned ckv[]={2,5,10,20,50,100,200,500,1000,2000,5000,10000,20000,50000u};
    unsigned m;
    if(type==BB_CAP)return k<15?capv[k]:0;
    if(type==BB_CLOCK)return k<14?ckv[k]:0;
    if(k>=48)return 0;m=e12[k%12];k/=12;
    if(k==3&&m>65)return 0;
    return m*bu_decade[k];
}
static int ovl_bu_spin(int up)
{
    int idx,k;unsigned v,c,best;BBPart *part;
    idx=bu_selected_kind==BB_PART?bu_part_index(bu_selected):-1;if(idx<0)return 1;
    part=&bb_doc.part[idx];v=part->value;best=v;
    if(part->type==BB_POWER){if(up&&v<=5900)best=v+100;else if(!up&&v>=4600)best=v-100;}
    else for(k=0;(c=bu_cand(part->type,k))!=0;k++){
        if(up&&c>v&&(best==v||c<best))best=c;
        if(!up&&c<v&&(best==v||c>best))best=c;
    }
    if(best==v){bu_note("Value is at the end of its range");return 1;}
    bb_copy(&bu_part,part,sizeof(bu_part));bu_part.value=best;bu_edit_index=idx;bu_queue(BU_OP_PART,0);
    return 1;
}
static int ovl_bu_onclick(int x,int y,void *win){
    int i,type,n;BBWord hole;
    bu_user_begin();if(!bu_layout(win))return 1;
    if(bu_mode==BU_ABOUT){bu_mode=BU_BOARD;bu_flags|=BU_ALL;bu_redraw_locked(win);return 1;}
    for(i=0;i<9;i++)if(bu_hit(S_BTN+i,x,y)){
        if(i==0)ovl_bu_tool(BU_SELECT);
        else if(i==1)ovl_bu_tool(BU_WIRE);
        else if(i==2)ovl_bu_tool(BU_PROBE);
        else if(i==3)ovl_bu_rotate();else if(i==4)bu_queue(BU_OP_UNDO,0);
        else if(i==5)bu_queue(BU_OP_RUN,0);
        else if(i==6){bb_running=0;bu_flags|=BU_INFO_DIRTY|BU_BAR_DIRTY|BU_SCOPE_DIRTY;}
        else if(i==7)bu_queue(BU_OP_STEP,0);else bu_queue(BU_OP_RESET,0);
        bu_redraw_locked(win);return 1;
    }
    if(bu_hit(S_STATE,x,y)||bu_hit(S_STATUS,x,y)){
        if(bu_hit(S_STATUS,x,y)&&x>bu_r3[S_SMSG]){bu_mode=BU_HELP;bu_help_page=0;bu_flags|=BU_ALL;}
        else if(bb_result.diag&&!bb_recompile&&!bu_diag_stale)ovl_bu_locate_error();
        bu_redraw_locked(win);return 1;
    }
    if(bu_hit(S_CTABS,x,y)){
        i=(x-bu_x-4)/80;
        if(i==1)bu_mode=BU_PALETTE;else if(i==2)bu_mode=BU_INSPECT;
        else if(i<5){bu_mode=BU_BOARD;bu_view=i==3?BU_VSCOPE:i==4?BU_VNETS:BU_VBOARD;}
        bu_flags|=BU_ALL;bu_redraw_locked(win);return 1;
    }
    if(bu_hit(S_LEFT,x,y)){
        n=bu_lcol2?bu_lcol2:bu_r3[S_LEFT];
        if(x<n&&y>=bu_lcat&&y<=bu_lcat+bu_th+5)bu_cat=(bu_cat+1)%3;
        else if(x<n&&(i=bu_lhit(x,y,bu_litem,bu_lrow,bu_r1[S_LEFT],n-6))>=0){
            type=bu_cat_items[bu_cat][i];
            if(type==0)ovl_bu_tool(BU_WIRE);else if(type!=255)ovl_bu_choose_type(type);
        }else if((i=bu_lhit(x,y,bu_lex,bu_lexrow,bu_lcol2?bu_lcol2-2:bu_r1[S_LEFT],bu_r3[S_LEFT]-4))>=0&&i<bu_lexn){
            if(bu_lexn<8&&i==bu_lexn-1){bu_mode=BU_EXAMPLES;bu_flags|=BU_ALL;}else ovl_bu_request(BU_ACT_EXAMPLE,i+1);}
        bu_redraw_locked(win);return 1;
    }
    if(bu_hit(S_TABS,x,y)){
        n=bu_lay==BU_MID?3:2;
        for(i=0;i<n;i++)if(x>=bu_tabx[i]&&x<bu_tabx[i+1]){bu_view=i;bu_mode=BU_BOARD;bu_flags|=BU_ALL;}
        bu_redraw_locked(win);return 1;
    }
    if(bu_hit(S_SHEAD,x,y)){
        if(x>=bu_r3[S_SHEAD]-bu_th-4){bu_scope_on^=1;bu_flags|=BU_ALL;}
        bu_redraw_locked(win);return 1;
    }
    if(bu_hit(S_INSP,x,y)){
        i=bu_selected_kind==BB_PART?bu_part_index(bu_selected):-1;
        if(i>=0&&bu_mode!=BU_VALUE){
            type=bb_doc.part[i].type;
            if(y>=bu_ival&&y<=bu_ival+bu_th+4){
                if(bu_has_value(type)&&x>=bu_r3[S_INSP]-18)ovl_bu_spin(y<bu_ival+(bu_th+4)/2);
                else ovl_bu_parameter(0);
            }else if(y>=bu_ibtn&&y<=bu_ibtn+bu_th+4){
                if(bu_toggles(type)&&x>=(bu_r1[S_INSP]+bu_r3[S_INSP])/2)ovl_bu_toggle();else ovl_bu_rotate();
            }
        }
        bu_redraw_locked(win);return 1;
    }
    if(bu_hit(S_PROBE,x,y)){
        if(y>=bu_pbox&&y<=bu_pbox+bu_th+5){bu_channel^=1;bu_flags|=BU_INFO_DIRTY;}
        bu_redraw_locked(win);return 1;
    }
    if(!bu_hit(S_VIEW,x,y)){bu_redraw_locked(win);return 1;}
    if(bu_mode==BU_HELP){ovl_bu_help_next();bu_redraw_locked(win);return 1;}
    if(bu_mode==BU_EXAMPLES){
        i=(y-bu_r2[S_VIEW]-8)/(bu_th+3)-1;
        if(i>=0&&i<8)ovl_bu_request(BU_ACT_EXAMPLE,i+1);
        return 1;
    }
    if(bu_mode!=BU_BOARD||bu_view!=BU_VBOARD)return 1;
    ovl_bu_damage_selection();hole=bu_hit_hole(x,y);if(hole!=BB_NONE)bu_hole=hole;
    if(bu_tool==BU_PART&&hole!=BB_NONE)ovl_bu_place_click(hole);
    else if(bu_tool==BU_MOVE&&hole!=BB_NONE)ovl_bu_move_click(hole);
    else if(bu_tool==BU_WIRE&&hole!=BB_NONE){
        if(bu_first==BB_NONE){bu_first=hole;bu_ready=0;bu_note("Choose wire end; Enter commits");}
        else ovl_bu_wire_preview(hole);
        if(bu_ready)ovl_bu_damage_wire(&bu_wire);
    }else if(bu_tool==BU_PROBE)ovl_bu_probe_bind(hole,x,y);
    else{ovl_bu_select_at(hole,x,y);ovl_bu_damage_selection();bu_flags|=BU_INFO_DIRTY|BU_INFO_FULL;}
    bu_redraw_locked(win);

    return 1;
}
void os88_onclick(int x,int y,void *win){if(bu_op||bu_asking)return;ovl_bu_onclick(x,y,win);}

static int ovl_bu_key(int ascii,int scan,void *win){
    int idx;
    if(ascii==27)ovl_bu_tool(BU_SELECT);
    else if(bu_mode==BU_EXAMPLES&&ascii>='1'&&ascii<='8')ovl_bu_request(BU_ACT_EXAMPLE,ascii-'0');
    else if(ascii==14)ovl_bu_request(BU_ACT_NEW,0);
    else if(ascii==15)ovl_bu_request(BU_ACT_OPEN,0);
    else if(ascii==19)ovl_bu_request(BU_ACT_SAVE,0);
    else if(ascii==26)bu_queue(BU_OP_UNDO,0);
    else if(ascii==25)bu_queue(BU_OP_REDO,0);
    else if(ascii==13){
        if(bu_mode==BU_HELP){ovl_bu_help_next();}
        else if(bu_ready){if(bu_tool==BU_PART)bu_queue(BU_OP_PART,0);else if(bu_tool==BU_WIRE)bu_queue(BU_OP_WIRE,0);}
    }else if(ascii==9){if(bu_mode==BU_PALETTE)bu_cat=(bu_cat+1)%3;else{bu_channel^=1;bu_flags|=BU_INFO_DIRTY;}}
    else if(ascii==127||scan==0x53)bu_queue(BU_OP_DELETE,0);
    else if(ascii=='['||ascii==']'){idx=bu_selected_kind==BB_PART?bu_part_index(bu_selected):-1;if(idx>=0){if(ascii==']'){bu_selected_pin++;if(bu_selected_pin>bb_pincount(bb_doc.part[idx].type))bu_selected_pin=1;}else if(bu_selected_pin>1)bu_selected_pin--;else bu_selected_pin=bb_pincount(bb_doc.part[idx].type);bu_flags|=BU_INFO_DIRTY;}}
    else if(ascii=='v'||ascii=='V')ovl_bu_tool(BU_SELECT);
    else if(ascii=='w'||ascii=='W')ovl_bu_tool(BU_WIRE);
    else if(ascii=='p'||ascii=='P')ovl_bu_tool(BU_PROBE);
    else if(ascii=='r'||ascii=='R')ovl_bu_rotate();
    else if(ascii=='m'||ascii=='M'){bu_tool=BU_MOVE;bu_reset_tools();bu_note("Click destination for selected pin1");}
    else if(ascii=='e'||ascii=='E')ovl_bu_parameter(0);
    else if(ascii=='t'||ascii=='T')ovl_bu_parameter(1);
    else if(ascii==' ')ovl_bu_toggle();
    else if(ascii=='.')bu_queue(BU_OP_STEP,0);
    else if((ascii=='b'||ascii=='B')&&bu_tool==BU_WIRE&&bu_second!=BB_NONE&&bu_wire_n<2){
        ovl_bu_damage_wire(&bu_wire);bu_wire.bend[bu_wire_n*2]=bu_second%30;bu_wire.bend[bu_wire_n*2+1]=bu_second/30;bu_wire_n++;bu_second=BB_NONE;bu_ready=0;bu_note("Bend held; click next end");
    }else if(ascii=='+'||ascii=='='){
        if(bb_doc.scope_div<30000)bb_doc.scope_div=bb_doc.scope_div<<1;else bb_doc.scope_div=60000u;ovl_bu_view_changed();bu_flags|=BU_SCOPE_DIRTY;
    }else if(ascii=='-'){
        if(bb_doc.scope_div>20)bb_doc.scope_div/=2;else bb_doc.scope_div=10;ovl_bu_view_changed();bu_flags|=BU_SCOPE_DIRTY;
    }else if(ascii=='g'||ascii=='G'){bb_doc.trigger=(bb_doc.trigger+1)%3;ovl_bu_view_changed();bu_capture_reset=1;bu_wake();bu_flags|=BU_SCOPE_DIRTY;}
    else if(!ascii&&scan==0x3b){bu_mode=BU_HELP;bu_help_page=0;bu_flags|=BU_ALL;}
    else if(!ascii&&scan==0x3c){if(bu_compact){bu_mode=BU_PALETTE;bu_flags|=BU_ALL;}else bu_cat=(bu_cat+1)%3;}
    else if(!ascii&&scan==0x3d){bu_mode=bu_compact?BU_INSPECT:BU_BOARD;bu_view=BU_VBOARD;bu_flags|=BU_ALL;}
    else if(!ascii&&scan==0x3e){bu_mode=BU_BOARD;if(bu_lay==BU_FULL)bu_scope_on^=1;else bu_view=bu_view==BU_VSCOPE?BU_VBOARD:BU_VSCOPE;bu_flags|=BU_ALL;}
    else if(!ascii&&scan==0x4b)ovl_bu_pan(-1,0);
    else if(!ascii&&scan==0x4d)ovl_bu_pan(1,0);
    else if(!ascii&&scan==0x48)ovl_bu_pan(0,-1);
    else if(!ascii&&scan==0x50)ovl_bu_pan(0,1);
    else if(!ascii&&scan==0x49){if(bu_view==BU_VNETS){bu_netscroll=bu_netscroll>8?bu_netscroll-8:0;}else ovl_bu_pan(0,-4);}
    else if(!ascii&&scan==0x51){if(bu_view==BU_VNETS)bu_netscroll+=8;else ovl_bu_pan(0,4);}
    bu_redraw_locked(win);

    return 1;
}
void os88_onkey(int ascii,int scan,void *win)
{
    if(bu_asking)return;
    if(bu_op){if(ascii==27&&(bu_op==BU_OP_PART||bu_op==BU_OP_WIRE||bu_op==BU_OP_DELETE||bu_op==BU_OP_UNDO||bu_op==BU_OP_REDO)){
        bu_op=0;bu_mode=BU_BOARD;ovl_bu_damage_selection();bu_reset_tools();bu_flags|=BU_INFO_DIRTY|BU_INFO_FULL;bu_redraw_locked(win);}return;}
    bu_user_begin();
    if(bu_mode==BU_ABOUT){bu_mode=BU_BOARD;bu_flags|=BU_ALL;bu_redraw_locked(win);return;}
    if(bu_mode==BU_VALUE){
        if(ascii==27){bu_mode=BU_BOARD;bu_flags|=bu_compact?BU_ALL:BU_INFO_DIRTY|BU_INFO_FULL;}
        else if(ascii==13)ovl_bu_value_accept();
        else if(ascii==8){if(bu_edit_len)bu_edit[--bu_edit_len]=0;bu_flags|=BU_VALUE_DIRTY|BU_INFO_DIRTY;}
        else if(ascii>=32&&ascii<127&&bu_edit_len<23){bu_edit[bu_edit_len++]=ascii;bu_edit[bu_edit_len]=0;bu_flags|=BU_VALUE_DIRTY|BU_INFO_DIRTY;}
        if(!bu_op)bu_redraw_locked(win);return;
    }
    ovl_bu_key(ascii,scan,win);
}

static int ovl_bu_oncmd(int item,int menu,void *win){
    int idx;bu_user_begin();
    if(menu==0){
        if(item==0)ovl_bu_request(BU_ACT_NEW,0);else if(item==1)ovl_bu_request(BU_ACT_OPEN,0);
        else if(item==2)ovl_bu_request(BU_ACT_SAVE,0);else if(item==3)ovl_bu_request(BU_ACT_SAVEAS,0);
        else if(item==4){bu_mode=BU_EXAMPLES;bu_flags|=BU_ALL;}else if(item==5){ovl_bu_request(BU_ACT_CLOSE,0);return 1;}
    }else if(menu==1){
        if(item==0)bu_queue(BU_OP_UNDO,0);else if(item==1)bu_queue(BU_OP_REDO,0);
        else if(item==2)bu_queue(BU_OP_DELETE,0);else if(item==3)ovl_bu_rotate();
        else if(item==4){bu_tool=BU_MOVE;bu_reset_tools();bu_note("Click destination for selected pin1");}
        else if(item==5){idx=bu_selected_kind==BB_WIRE?bu_wire_index(bu_selected):-1;
            if(idx<0)bu_note("Select wire first");else{bb_copy(&bu_wire,&bb_doc.wire[idx],sizeof(bu_wire));bu_tool=BU_WIRE;bu_first=bu_selected_pin==1?bu_wire.b:bu_wire.a;bu_second=BB_NONE;bu_wire_n=0;bu_ready=0;bu_edit_index=idx;bu_note("Click new endpoint; Enter reroutes");}}
        else if(item==6){bu_color=(bu_color+1)%15;if(!bu_color)bu_color=1;idx=bu_selected_kind==BB_WIRE?bu_wire_index(bu_selected):-1;
            if(idx>=0){bb_copy(&bu_wire,&bb_doc.wire[idx],sizeof(bu_wire));bu_wire.color=bu_color;bu_edit_index=idx;bu_queue(BU_OP_WIRE,0);}}
        else if(item==7)ovl_bu_parameter(0);
    }else if(menu==2){
        if(item==0)ovl_bu_choose_type(BB_RES);else if(item==1)ovl_bu_choose_type(BB_LED);else if(item==2)ovl_bu_choose_type(BB_CAP);
        else if(item==3)ovl_bu_choose_type(BB_SWITCH);else if(item==4)ovl_bu_choose_type(BB_POWER);else if(item==5)ovl_bu_choose_type(BB_GROUND);
        else if(item==6)ovl_bu_choose_type(BB_POT);else if(bu_compact){bu_mode=BU_PALETTE;bu_flags|=BU_ALL;}else bu_cat=(bu_cat+1)%3;
    }else if(menu==3){
        if(item==0)bu_queue(BU_OP_RUN,0);else if(item==1){bb_running=0;bu_flags|=BU_BAR_DIRTY|BU_INFO_DIRTY|BU_SCOPE_DIRTY;}
        else if(item==2)bu_queue(BU_OP_STEP,0);else if(item==3)bu_queue(BU_OP_RESET,0);
        else{if(item==4){if(bb_doc.dt>1)bb_doc.dt/=2;}else if(bb_doc.dt<500)bb_doc.dt=bb_doc.dt<<1;else bb_doc.dt=1000;
            bb_invalidate();bu_diag_stale=1;bu_active=0;bu_begin("Timestep ");bu_number(bb_doc.dt);bu_append(" x100us; Reset before comparing");bu_note(bu_line);bu_flags|=BU_INFO_DIRTY|BU_BAR_DIRTY|BU_SCOPE_DIRTY;}
    }else if(menu==4){
        if(item==0){bu_mode=BU_BOARD;bu_view=BU_VBOARD;}
        else if(item==1){if(bu_compact)bu_mode=BU_PALETTE;else bu_cat=(bu_cat+1)%3;}
        else if(item==2)bu_mode=bu_compact?BU_INSPECT:BU_BOARD;
        else if(item==3){bu_mode=BU_BOARD;if(bu_lay==BU_FULL)bu_scope_on^=1;else bu_view=bu_view==BU_VSCOPE?BU_VBOARD:BU_VSCOPE;}
        else if(item==8){bu_mode=BU_BOARD;bu_view=bu_view==BU_VNETS?BU_VBOARD:BU_VNETS;}
        else if(item==4){bb_doc.zoom=bb_doc.zoom==2?1:2;ovl_bu_view_changed();}
        else if(item==5)bu_netshow^=1;
        else if(item==6){bu_mode=BU_HELP;bu_help_page=0;}else if(item==7)bu_mode=BU_EXAMPLES;
        bu_flags|=BU_ALL;
    }
    bu_redraw_locked(win);

    return 1;
}
void os88_oncmd(int item,int menu,void *win){if(bu_op||bu_asking)return;if(bu_mode==BU_ABOUT){bu_mode=BU_BOARD;bu_flags|=BU_ALL;}ovl_bu_oncmd(item,menu,win);}

void os88_about(void *win)
{
    if(bu_op||bu_asking)return;
    bu_mode=BU_ABOUT;bu_flags=0;bu_cache_ok=0;os88_about_card(win,bu_about);
}
int os88_onclose(void *win)
{
    (void)win;if(bu_op||bu_asking)return 0;if(!bb_dirty)return 1;ovl_bu_request(BU_ACT_CLOSE,0);return 0;
}
void os88_onfile(int mode,const char *name,unsigned size_lo,unsigned size_hi,void *win)
{
    (void)win;if(bu_op||bu_asking){bu_note("Busy: choose the file again");return;}bu_saveas_wait=0;
    if(mode==OS88_FDLG_OPEN&&(size_hi||size_lo>BB_FILEMAX)){bb_error=BB_ECAPACITY;bu_error();bu_after=0;bu_flags|=BU_ALL;return;}
    os88_strcpy(bu_choice,name,sizeof(bu_choice));bu_io_size=size_lo;bu_io_hi=size_hi;bu_io_association=0;
    bu_io_as=mode==OS88_FDLG_SAVE;bu_queue(mode==OS88_FDLG_SAVE?BU_OP_SAVE:BU_OP_LOAD,0);bu_flags|=BU_ALL;
}
void os88_ontimer(void *win)
{
    if(!bu_initialized){bu_initialized=1;bu_wake();}
    if(bb_running||bu_step_once||bu_op||bu_flags)os88_wm_wake(win);
    if(os88_wm_timer(win,2)!=0)bu_timer_ok=0;
}
static BBInt bu_led_shadow[BB_PARTS];
static void bu_result_changed(void)
{
    unsigned i;int key,top,bottom,changed;BBWord h;
    changed=0;top=bu_by+bu_bh;bottom=bu_by;
    for(i=0;i<bb_doc.np;i++)if(bb_doc.part[i].type==BB_LED||bb_doc.part[i].type==BB_INDICATOR){
        if(bb_doc.part[i].type==BB_LED){key=0;if(bb_valid&&bb_result.ua[i]>100){key=1;if(bb_result.ua[i]>=2000)key=2;if(bb_result.ua[i]>=8000)key=3;}}
        else{h=bb_pin(&bb_doc.part[i],1);key=!bb_valid?4:bb_hnet[h]<BB_NETS?bb_result.logic[bb_hnet[h]]:BB_Z;}
        if(key!=bu_led_shadow[i]){
            bu_led_shadow[i]=key;changed=1;h=bb_pin(&bb_doc.part[i],1);
            if(bu_hy(h)-12<top)top=bu_hy(h)-12;if(bu_hy(h)+12>bottom)bottom=bu_hy(h)+12;
            if(bb_pincount(bb_doc.part[i].type)>1){h=bb_pin(&bb_doc.part[i],2);if(bu_hy(h)-12<top)top=bu_hy(h)-12;if(bu_hy(h)+12>bottom)bottom=bu_hy(h)+12;}
        }
    }
    if(changed)ovl_bu_damage_span(top,bottom);
    bu_flags|=BU_INFO_DIRTY;
    if((unsigned)(os88_ticks()-bu_last_scope)>=4){bu_last_scope=os88_ticks();bu_flags|=BU_SCOPE_DIRTY;}
}
void os88_onwake(void *win)
{
    unsigned op,arg,now;int result;
    if(bu_capture_reset){bu_capture_reset=0;bb_scope_reset();}
    if(bu_restore_place){bu_restore_place=0;os88_file_goto_q_mark(bu_doc_place.clus,bu_doc_place.vol);}
    op=bu_op;arg=bu_arg;bu_op=0;
    if(op){bu_flags|=BU_UIKEY;ovl_bu_execute(op,arg);}
    if(bu_closing)return;
    if(bb_running||bu_step_once){
        if(!bu_active){if(bb_sim_start(0)){ovl_bu_speed_reset();bu_active=1;}else{bb_running=0;bu_step_once=0;bu_error();bu_result_changed();bu_flags|=BU_BAR_DIRTY|BU_SCOPE_DIRTY;}}
        if(bu_active){
            result=bb_sim_slice(16);
            if(result==1){bu_result_changed();if(bu_step_once){bu_step_once=0;bb_running=0;bu_flags|=BU_BAR_DIRTY|BU_SCOPE_DIRTY|BU_UIKEY;}}
            else if(result<0){bu_diag_stale=0;bu_active=0;bu_step_once=0;bb_running=0;bu_result_changed();bu_flags|=BU_INFO_DIRTY|BU_BAR_DIRTY|BU_SCOPE_DIRTY|BU_UIKEY;}
        }
    }
    now=os88_ticks();
    if(bu_flags&&(op||!bb_running||(unsigned)(now-bu_lastdraw)>=2)){
        os88_gfx_lock();ovl_bu_title_update();bu_paint_region=0;if(os88_wm_clip_set(win)==0)bu_render(win,0);os88_gfx_unlock();bu_lastdraw=now;
    }
    if(bb_running||bu_step_once||bu_op)os88_wm_wake(win);
}
static struct os88_class bu_class={OS88_CLASS_LEN,OS88_WF_SIZABLE|OS88_WF_OWNBG,
    os88_t_paint,os88_t_onkey,os88_t_onclick,os88_t_about,os88_t_onwake,
    os88_t_onclose,os88_t_onmouseup,os88_t_onresize,0,0,os88_t_ontimer,0,os88_t_onwheel};
void *os88_main(void)
{
    os88_file_here(&bu_pkg_place);
    bb_clear(&bb_doc);bb_history_clear();bb_mark_saved();
    bu_type=BB_RES;bu_tool=BU_SELECT;bu_hole=BB_NONE;bu_scope_on=1;bu_color=OS88_RED;
    bu_reset_tools();bu_flags=BU_ALL;bu_netshow=1;
    os88_video(&bu_video);
    /* The face's art is a lazy part: fetched when it fits, else the plain
     * face (SPEC.md 130.6.3) - never a refused launch. */
    bu_plain=1;if(os88_part_lazyok(1)&&os88_part_fetch(1)==0){bu_plain=0;bu_artseg=os88_part_seg(1);}
    /* The band in a heap claim of its own: 51 rows a strip at 640 wide,
     * against the file buffer's 9, and every strip re-walks the face's
     * sections. Refused, the file buffer stands in (SPEC.md 130.6.2). */
    bu_bandseg=os88_mem_claim_p(BU_BANDP);if(bu_bandseg)os88_mem_movable(bu_bandseg,1);
    bu_bxsetup(bu_video.bpp>=4?4:1);
    os88_strcpy(bu_title,"UNTITLED.BBX - Breadboard XT",sizeof(bu_title));
    bu_win=os88_wm_create(0,OS88_MBAR_H,bu_video.w,bu_video.dock_top-OS88_MBAR_H,bu_title,&bu_class);
    if(!bu_win)return 0;
    os88_wm_minsize(bu_win,320,128);os88_menu_set(bu_win,&bu_menus);
    os88_file_here(&bu_doc_place);
    bu_timer_ok=os88_wm_timer(bu_win,2)==0;
    if(os88_arg_file(bu_choice,&bu_launch_place)==0){bu_io_association=1;bu_io_hi=0;bu_io_size=0;bu_op=BU_OP_LOAD;}
    bu_note(bu_plain?"Plain face: low memory":"Pick a part or an example; F1 Help");
    return bu_win;
}
