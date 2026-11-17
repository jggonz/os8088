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

void bb_bytes(void *dst,unsigned n,unsigned byte);
void bb_timefmt(char *out,unsigned lo,unsigned hi);
void bb_board_row(void *bits,int stride,int cols,int pitch,const BBWord *marks);
void bb_scope_line(void *bits,int stride,int x1,int y1,int x2,int y2,int dash);
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
static char bu_shadow[16][48];
/* UI callbacks are serialized. A file operation completes before any draw
 * in its wake; no retained bitmap points into this workspace. */
static BBByte bu_workbits[BB_FILEMAX];
#define bu_rowbits bu_workbits
#define bu_scopebits bu_workbits
#define bu_filebits bu_workbits
static BBWord bu_marks[4];
static BBPart bu_part;
static BBWire bu_wire;
static BBWord bu_tool,bu_type,bu_mode,bu_palette_page,bu_channel,bu_netshow;
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
static int bu_scope_y,bu_scope_h,bu_right;
static int bu_dirty_top,bu_dirty_bottom;
static void bu_result_changed(void);
static int ovl_bu_damage_span(int top,int bottom);
static int ovl_bu_damage_selection(void);
static int ovl_bu_locate_error(void);
static const char *bu_files[]={"New","Open...","Save","Save As...","Examples...","Close"};
static const char *bu_edits[]={"Undo","Redo","Delete","Rotate","Move part","Reroute wire","Wire color","Value / parameter"};
static const char *bu_parts[]={"Resistor","LED","Capacitor","Switch","Power","Ground","Potentiometer","All parts..."};
static const char *bu_sims[]={"Run","Pause","Step","Reset","Smaller timestep","Larger timestep"};
static const char *bu_views[]={"Breadboard","Parts palette","Inspector","Oscilloscope","Zoom","Net highlight","Help","Examples"};
static struct os88_menuset bu_menus={"BreadXT",0,5,
    {{"File",bu_files,6},{"Edit",bu_edits,8},{"Parts",bu_parts,8},
     {"Simulate",bu_sims,6},{"View",bu_views,8}}};
static const char *bu_tools[]={"Select","Wire","Probe","Rotate","Undo","Run","Pause","Step","Reset"};
static const char *bu_smalltools[]={"Sel","W","P","Rot","U","Run","||",">","0"};
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
static void bu_signed(int n)
{
    if(n<0){bu_append("-"); n=-n;} bu_number(n);
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
static void bu_text(int slot,int x,int y,const char *s,int cells,int force)
{
    int i,first,last,n;
    static char text[64];
    if(cells>47)cells=47;
    if(cells<1||y<bu_y||y+7>=bu_y+bu_h)return;
    n=os88_strlen(s);if(n>cells)n=cells;
    for(i=0;i<cells;i++)text[i]=i<n?s[i]:' ';
    text[cells]=0;first=0;last=cells;
    if(!force&&bu_cache_ok){
        while(first<cells&&text[first]==bu_shadow[slot][first])first++;
        while(last>first&&text[last-1]==bu_shadow[slot][last-1])last--;
    }
    if(!bu_paint_region){for(i=0;i<cells;i++)bu_shadow[slot][i]=text[i];bu_shadow[slot][cells]=0;}
    if(first<last){text[last]=0;os88_font_run(x+first*8,y,text+first,OS88_BLACK,OS88_WHITE);}
}
static int bu_bounds(int x1,int y1,int x2,int y2)
{
    if(!bu_paint_region)return 1;
    return x2>=bu_damage.x1&&x1<=bu_damage.x2&&y2>=bu_damage.y1&&y1<=bu_damage.y2;
}
static void bu_label(int x,int y,const char *s)
{
    int n;static char text[80];
    if(y<bu_y||y+7>=bu_y+bu_h||x>=bu_x+bu_w||!bu_bounds(x,y,x+os88_strlen(s)*8-1,y+7))return;
    n=(bu_x+bu_w-x)/8;if(n>79)n=79;if(n<1)return;
    os88_strcpy(text,s,n+1);os88_font_run(x,y,text,OS88_BLACK,OS88_WHITE);
}
static void bu_layout(void *win)
{
    os88_wm_content(win,&bu_org);if(os88_wm_geom(win,&bu_size)!=0)return;
    os88_wm_display(win,&bu_video);
    bu_x=bu_org.x;bu_y=bu_org.y;bu_w=bu_size.w;bu_h=bu_size.h;
    bu_compact=bu_h<240||bu_w<560;bu_pitch=bb_doc.zoom==2?20:12;
    bu_scope_h=(!bu_compact&&bu_scope_on)?bu_min(104,bu_h/3):0;
    bu_scope_y=bu_y+bu_h-12-bu_scope_h;
    bu_bx=bu_x+(bu_compact?0:96);bu_by=bu_y+(bu_compact?36:24);
    bu_bw=bu_w-(bu_compact?0:224);bu_right=bu_x+bu_w-128;
    bu_bh=bu_scope_y-bu_by;
    bu_gx=(bu_bx+23)&~7;bu_gy=bu_by+16;
    bu_cols=(bu_bw-24)/bu_pitch;if(bu_cols>30)bu_cols=30;if(bu_cols<1)bu_cols=1;
    bu_rows=(bu_bh-16)/bu_pitch;if(bu_rows>14)bu_rows=14;if(bu_rows<1)bu_rows=1;
    if(bb_doc.panx>29)bb_doc.panx=29;
    if(bb_doc.pany>13)bb_doc.pany=13;
}
static int bu_hx(unsigned hole){return bu_gx+8+((int)(hole%30)-(int)bb_doc.panx)*bu_pitch;}
static int bu_hy(unsigned hole)
{
    int r,p;r=hole/30;p=bb_doc.pany;
    return bu_gy+(r-p)*bu_pitch+((r>=7)-(p>=7))*bu_pitch+5;
}
static int bu_visible(unsigned hole)
{
    int x,y;if(hole>=BB_HOLES)return 0;x=bu_hx(hole);y=bu_hy(hole);
    return x>=bu_gx&&x<bu_bx+bu_bw-12&&y>=bu_gy&&y<bu_by+bu_bh-12;
}
static BBWord bu_hit_hole(int x,int y)
{
    int col,row,hy;
    col=(x-bu_gx-8+bu_pitch/2)/bu_pitch+bb_doc.panx;
    if(col<0||col>=30)return BB_NONE;
    if(bu_abs(x-(bu_gx+8+(col-(int)bb_doc.panx)*bu_pitch))>bu_pitch/3)return BB_NONE;
    for(row=bb_doc.pany;row<14;row++){
        hy=bu_hy(row*30+col);
        if(hy>=bu_by+bu_bh)break;
        if(bu_abs(y-hy)<=bu_pitch/3)return (row<<5)-(row<<1)+col;
    }
    return BB_NONE;
}
static void bu_hole_name(unsigned h)
{
    static char row[4];
    if(h>=BB_HOLES){bu_append("--");return;}
    if(h/30>=2&&h/30<=11){row[0]='a'+h/30-2;row[1]=0;bu_append(row);}
    else{if(h/30<2)bu_append("T");else bu_append("B");bu_append((h/30==0||h/30==12)?"+":"-");}
    bu_number(h%30+1);
}
static void ovl_bu_volts(int mv)
{
    if(mv==BB_MV_FLOAT){bu_append("floating");return;}
    if(mv==BB_MV_UNKNOWN){bu_append("X / unknown");return;}
    if(mv<0){bu_append("failed");return;}
    bu_number(mv/1000);bu_append(".");
    if(mv%1000<100)bu_append("0");if(mv%1000<10)bu_append("0");
    bu_number(mv%1000);bu_append(" V");
}
static int ovl_bu_value(unsigned type,unsigned value)
{
    if(type==BB_RES||type==BB_POT){
        if(value<100){bu_number(value*10);bu_append(" ohm");}
        else{bu_number(value/100);if(value%100){bu_append(".");if(value%100<10)bu_append("0");bu_number(value%100);}bu_append("k ohm");}
    }else if(type==BB_CAP){
        if(value>=1000){bu_number(value/1000);if(value%1000){bu_append(".");if(value%1000<100)bu_append("0");if(value%1000<10)bu_append("0");bu_number(value%1000);}bu_append("uF");}
        else{bu_number(value);bu_append("nF");}
    }else if(type==BB_POWER)ovl_bu_volts(value);
    else if(type==BB_CLOCK){bu_number(value);bu_append(" x100us");}

    return 1;
}
static const char *bu_prefix(unsigned type)
{
    if(type==BB_RES)return "R";if(type==BB_CAP)return "C";if(type==BB_LED)return "D";
    if(type==BB_SWITCH)return "S";if(type==BB_POT)return "P";if(type==BB_POWER)return "V";
    if(type==BB_GROUND)return "G";if(type==BB_INPUT)return "IN";if(type==BB_CLOCK)return "CK";
    if(type==BB_INDICATOR)return "L";return "U";
}
static void bu_ident(const BBPart *part){bu_append(bu_prefix(part->type));bu_number(part->id);}
static int bu_overlap(int top,int bottom)
{
    return bottom>=bu_dirty_top&&top<=bu_dirty_bottom;
}
static void bu_canvas_hline(int x1,int x2,int y)
{
    int t;
    if(y<bu_gy||y>=bu_by+bu_bh||y<bu_dirty_top||y>bu_dirty_bottom)return;
    if(x1>x2){t=x1;x1=x2;x2=t;}
    if(x1<bu_gx)x1=bu_gx;if(x2>=bu_bx+bu_bw)x2=bu_bx+bu_bw-1;
    if(x1<=x2)os88_gfx_hline(x1,x2,y);
}
static void bu_canvas_vline(int x,int y1,int y2)
{
    int t;
    if(x<bu_gx||x>=bu_bx+bu_bw)return;
    if(y1>y2){t=y1;y1=y2;y2=t;}
    if(y1<bu_gy)y1=bu_gy;if(y1<bu_dirty_top)y1=bu_dirty_top;
    if(y2>=bu_by+bu_bh)y2=bu_by+bu_bh-1;if(y2>bu_dirty_bottom)y2=bu_dirty_bottom;
    if(y1<=y2)os88_gfx_vline(x,y1,y2);
}
static void bu_canvas_fill(int x1,int y1,int x2,int y2)
{
    if(x1<bu_gx)x1=bu_gx;if(x2>=bu_bx+bu_bw)x2=bu_bx+bu_bw-1;
    if(y1<bu_gy)y1=bu_gy;if(y1<bu_dirty_top)y1=bu_dirty_top;
    if(y2>=bu_by+bu_bh)y2=bu_by+bu_bh-1;if(y2>bu_dirty_bottom)y2=bu_dirty_bottom;
    if(x1<=x2&&y1<=y2)os88_gfx_fill(x1,y1,x2,y2);
}
static void bu_canvas_frame(int x1,int y1,int x2,int y2)
{
    bu_canvas_hline(x1,x2,y1);bu_canvas_hline(x1,x2,y2);
    bu_canvas_vline(x1,y1,y2);bu_canvas_vline(x2,y1,y2);
}
static void bu_canvas_text(int x,int y,const char *text,int ink,int paper)
{
    int skip,n;static char line[64];
    if(y<bu_gy||y+7>=bu_by+bu_bh||!bu_overlap(y,y+7))return;
    skip=x<bu_gx?(bu_gx-x+7)/8:0;n=os88_strlen(text);
    if(skip>=n)return;x+=skip*8;n-=skip;
    if(n>(bu_bx+bu_bw-x)/8)n=(bu_bx+bu_bw-x)/8;
    if(n>63)n=63;if(n<1)return;
    os88_strcpy(line,text+skip,n+1);os88_font_run(x,y,line,ink,paper);
}
static void bu_canvas_label(int x,int y,const char *text)
{bu_canvas_text(x,y,text,OS88_BLACK,OS88_WHITE);}
static void bu_line_xy(int x1,int y1,int x2,int y2)
{
    if(x1==x2)bu_canvas_vline(x1,y1,y2);
    else if(y1==y2)bu_canvas_hline(x1,x2,y1);
    else{bu_canvas_hline(x1,x2,y1);bu_canvas_vline(x2,y1,y2);}
}
static void bu_wire_draw(const BBWire *wire,int preview)
{
    int x,y,nx,ny,i,top,bottom;
    top=bu_min(bu_hy(wire->a),bu_hy(wire->b))-2;bottom=bu_hy(wire->a)>bu_hy(wire->b)?bu_hy(wire->a)+2:bu_hy(wire->b)+2;
    for(i=0;i<wire->nbend;i++){ny=bu_hy(wire->bend[i*2+1]*30+wire->bend[i*2]);if(ny<top)top=ny;if(ny>bottom)bottom=ny;}
    if(!bu_overlap(top,bottom))return;
    os88_set_color(preview?OS88_DGRAY:(bu_video.bpp>=4?wire->color:OS88_BLACK));
    if(preview)os88_gfx_pen(1);
    x=bu_hx(wire->a);y=bu_hy(wire->a);
    for(i=0;i<wire->nbend;i++){
        nx=bu_hx(wire->bend[i*2+1]*30+wire->bend[i*2]);ny=bu_hy(wire->bend[i*2+1]*30+wire->bend[i*2]);
        bu_line_xy(x,y,nx,ny);x=nx;y=ny;
    }
    bu_line_xy(x,y,bu_hx(wire->b),bu_hy(wire->b));
    if(preview)os88_gfx_pen(0);
    if(wire->id==bu_selected&&bu_selected_kind==BB_WIRE){
        os88_set_color(OS88_BLACK);bu_canvas_frame(bu_hx(wire->a)-3,bu_hy(wire->a)-3,bu_hx(wire->a)+3,bu_hy(wire->a)+3);
        bu_canvas_frame(bu_hx(wire->b)-3,bu_hy(wire->b)-3,bu_hx(wire->b)+3,bu_hy(wire->b)+3);
    }
}
static void bu_part_draw(const BBPart *part,int preview)
{
    int n,i,x1,y1,x2,y2,cx,cy,left,top,right,bottom,idx,on;
    BBWord h;
    n=bb_pincount(part->type);if(n<1)return;
    h=bb_pin(part,1);if(h>=BB_HOLES)return;
    x1=bu_hx(h);y1=bu_hy(h);x2=x1;y2=y1;
    for(i=2;i<=n;i++){
        h=bb_pin(part,i);if(h>=BB_HOLES)return;
        if(bu_hx(h)<x1)x1=bu_hx(h);if(bu_hx(h)>x2)x2=bu_hx(h);
        if(bu_hy(h)<y1)y1=bu_hy(h);if(bu_hy(h)>y2)y2=bu_hy(h);
    }
    top=y1-16;bottom=y2+16;if(x2+12<bu_gx||x1-12>=bu_bx+bu_bw||bottom<bu_gy||top>=bu_by+bu_bh||!bu_overlap(top,bottom))return;
    os88_set_color(OS88_BLACK);if(preview)os88_gfx_pen(1);
    if(part->type>=BB_HC00&&part->type<=BB_HC86||part->type==BB_555){
        left=x1-5;right=x2+5;top=y1+4;bottom=y2-4;
        if(bottom<top+10)bottom=top+10;
        for(i=1;i<=n;i++){h=bb_pin(part,i);bu_canvas_frame(bu_hx(h)-2,bu_hy(h)-2,bu_hx(h)+2,bu_hy(h)+2);}
        bu_canvas_fill(left,top,right,bottom);
        bu_canvas_text(left+2,(top+bottom-7)/2,part->type==BB_555?"555":bb_part_name(part->type),OS88_WHITE,OS88_BLACK);
        os88_set_color(OS88_WHITE);
        if(part->flags&BB_ROT)bu_canvas_vline(right-2,top+3,bottom-3);else bu_canvas_vline(left+2,top+3,bottom-3);
        os88_set_color(OS88_BLACK);
        h=bb_pin(part,1);bu_canvas_frame(bu_hx(h)-3,bu_hy(h)-3,bu_hx(h)+3,bu_hy(h)+3);
    }else{
        h=bb_pin(part,1);x1=bu_hx(h);y1=bu_hy(h);
        h=n>1?bb_pin(part,2):bb_pin(part,1);x2=bu_hx(h);y2=bu_hy(h);
        cx=(x1+x2)/2;cy=(y1+y2)/2;
        bu_line_xy(x1,y1,x2,y2);
        left=cx-10;right=cx+10;top=cy-4;bottom=cy+4;
        if(part->type==BB_LED){
            idx=bu_part_index(part->id);on=0;if(idx>=0&&bb_valid&&!bb_result.diag&&bb_result.ua[idx]>100){on=1;if(bb_result.ua[idx]>=2000)on=2;if(bb_result.ua[idx]>=8000)on=3;}
            os88_set_color(on&&bu_video.bpp>=4?(on==1?OS88_BROWN:on==2?OS88_RED:OS88_LRED):OS88_WHITE);bu_canvas_fill(cx-5,cy-5,cx+5,cy+4);
            os88_set_color(OS88_BLACK);bu_canvas_frame(cx-5,cy-5,cx+5,cy+4);
            bu_canvas_hline(cx-7,cx+7,cy+5);
            if(on){bu_canvas_vline(cx-8,cy-5,cy+2);bu_canvas_vline(cx+8,cy-5,cy+2);if(on>=2)bu_canvas_hline(cx-4,cx+4,cy-8);if(on==3){bu_canvas_vline(cx-10,cy-6,cy-3);bu_canvas_vline(cx+10,cy-6,cy-3);}}
            bu_canvas_label(x1-3,y1-10,"A");bu_canvas_label(x2-3,y2+4,"K");
        }else if(part->type==BB_CAP){
            os88_set_color(OS88_WHITE);bu_canvas_fill(cx-3,cy-7,cx+3,cy+7);
            os88_set_color(OS88_BLACK);bu_canvas_vline(cx-2,cy-7,cy+7);bu_canvas_vline(cx+2,cy-7,cy+7);
            bu_canvas_label(x1-3,y1-10,"+");
        }else if(part->type==BB_RES||part->type==BB_POT){
            os88_set_color(bu_video.bpp>=4?OS88_YELLOW:OS88_WHITE);bu_canvas_fill(left,top,right,bottom);
            os88_set_color(OS88_BLACK);bu_canvas_frame(left,top,right,bottom);
            bu_canvas_vline(cx-5,top,bottom);bu_canvas_vline(cx+4,top,bottom);
            if(part->type==BB_POT){h=bb_pin(part,3);bu_line_xy(bu_hx(h),bu_hy(h),cx,cy);bu_canvas_label(cx-3,cy-9,"v");}
        }else if(part->type==BB_SWITCH){
            os88_set_color(OS88_WHITE);bu_canvas_fill(left,top,right,bottom);os88_set_color(OS88_BLACK);
            bu_canvas_frame(left,top,right,bottom);bu_canvas_label(cx-7,cy-3,part->flags&BB_CLOSED?"ON":"--");
        }else if(part->type==BB_GROUND){
            bu_canvas_hline(cx-6,cx+6,cy+2);bu_canvas_hline(cx-4,cx+4,cy+5);bu_canvas_hline(cx-2,cx+2,cy+8);
        }else if(part->type==BB_POWER){
            bu_canvas_frame(left,top,right,bottom);bu_canvas_label(cx-7,cy-3,"+-");
        }else{
            os88_set_color(OS88_WHITE);bu_canvas_fill(cx-5,cy-5,cx+5,cy+5);os88_set_color(OS88_BLACK);bu_canvas_frame(cx-5,cy-5,cx+5,cy+5);
            bu_begin("");
            if(part->type==BB_INPUT){if(part->param==0)bu_append("0");else if(part->param==1)bu_append("1");else bu_append(part->param==2?"X":"Z");}
            else if(part->type==BB_CLOCK)bu_append("C");
            else{h=bb_pin(part,1);if(bb_valid&&bb_hnet[h]<BB_NETS){i=bb_result.logic[bb_hnet[h]];bu_append(i==0?"0":i==1?"1":i==2?"X":"Z");}else bu_append("?");}
            bu_canvas_label(cx-3,cy-3,bu_line);
        }
        bu_begin("");bu_ident(part);
        if(cx-8>=bu_bx&&cy+11<bu_by+bu_bh)bu_canvas_label(cx-8,cy+7,bu_line);
    }
    if(preview)os88_gfx_pen(0);
    if(part->id==bu_selected&&bu_selected_kind==BB_PART){
        os88_set_color(OS88_BLACK);bu_canvas_frame(bu_min(x1,x2)-7,bu_min(y1,y2)-7,(x1>x2?x1:x2)+7,(y1>y2?y1:y2)+7);
    }
}
static void bu_canvas_gap(int y1,int y2,int width)
{
    int top,bottom,end;
    if(y1>y2)return;
    top=bu_hy(180)-5+bu_pitch;bottom=top+bu_pitch-1;
    if(y1<top){end=y2<top?y2:top-1;os88_set_color(OS88_WHITE);os88_gfx_fill(bu_gx,y1,bu_gx+width-1,end);}
    if(y2>=top&&y1<=bottom){os88_set_color(OS88_BLACK);os88_gfx_fill_gray(bu_gx,y1>top?y1:top,bu_gx+width-1,y2<bottom?y2:bottom);}
    if(y2>bottom){os88_set_color(OS88_WHITE);os88_gfx_fill(bu_gx,y1>bottom?y1:bottom+1,bu_gx+width-1,y2);}
}
static void bu_canvas(int full)
{
    int row,col,y,h,stride,width,strip,net,top,bottom,cursor,gap;
    unsigned i,hole;
    if(bu_bw<32||bu_bh<24)return;
    if(full){bu_dirty_top=bu_by;bu_dirty_bottom=bu_by+bu_bh-1;if(bu_paint_region){if(bu_dirty_top<bu_damage.y1)bu_dirty_top=bu_damage.y1;if(bu_dirty_bottom>bu_damage.y2)bu_dirty_bottom=bu_damage.y2;}}
    if(!bu_bounds(bu_bx,bu_by,bu_bx+bu_bw-1,bu_by+bu_bh-1))return;
    if(bu_dirty_top<bu_by)bu_dirty_top=bu_by;
    if(bu_dirty_bottom>=bu_by+bu_bh)bu_dirty_bottom=bu_by+bu_bh-1;
    if(bu_dirty_top>bu_dirty_bottom)return;
    width=(bu_cols*bu_pitch+16+7)&~7;
    if(width>bu_bx+bu_bw-bu_gx)width=(bu_bx+bu_bw-bu_gx)&~7;
    if(width<8)return;stride=width/8;cursor=bu_dirty_top;
    strip=bu_hole<BB_HOLES?bb_strip(bu_hole):-1;
    net=bu_hole<BB_HOLES&&!bb_recompile?bb_hnet[bu_hole]:BB_NNET;
    for(row=bb_doc.pany;row<14;row++){
        y=bu_hy((row<<5)-(row<<1))-5;
        if(y>=bu_by+bu_bh-1)break;
        h=bu_min(bu_pitch,bu_by+bu_bh-y-1);
        if(h<1||!bu_overlap(y,y+h-1))continue;
        top=y>bu_dirty_top?y:bu_dirty_top;bottom=y+h-1;
        if(bottom>bu_dirty_bottom)bottom=bu_dirty_bottom;
        if(cursor<top)bu_canvas_gap(cursor,top-1,width);
        bu_marks[0]=0;bu_marks[1]=0;bu_marks[2]=0;bu_marks[3]=0;
        for(col=0;col<bu_cols;col++){
            hole=(row<<5)-(row<<1)+bb_doc.panx+col;
            if(hole>=BB_HOLES||bb_doc.panx+col>=30)break;
            if(strip>=0&&bb_strip(hole)==strip)bu_marks[col>>4]|=(1u<<(col&15));
            else if(bu_netshow&&net<BB_NETS&&bb_hnet[hole]==net)bu_marks[2+(col>>4)]|=(1u<<(col&15));
        }
        bb_board_row(bu_rowbits,stride,col,bu_pitch,bu_marks);
        if(os88_gfx_blit1(bu_rowbits+(top-y)*stride,stride,bu_gx,top,width,bottom-top+1)!=0){
            os88_set_color(OS88_WHITE);os88_gfx_fill(bu_gx,top,bu_gx+width-1,bottom);
            os88_set_color(OS88_BLACK);
            for(col=0;col<bu_cols&&bb_doc.panx+col<30;col++)bu_canvas_frame(bu_gx+7+col*bu_pitch,y+4,bu_gx+9+col*bu_pitch,y+6);
        }
        cursor=bottom+1;
        if(row<2||row>11){
            os88_set_color(bu_video.bpp>=4?(row==0||row==12?OS88_RED:OS88_BLUE):OS88_BLACK);
            gap=15-(int)bb_doc.panx;
            if(gap>0&&gap<bu_cols){bu_canvas_hline(bu_gx,bu_gx+gap*bu_pitch-2,y+1);bu_canvas_hline(bu_gx+gap*bu_pitch+4,bu_gx+width-1,y+1);}
            else bu_canvas_hline(bu_gx,bu_gx+width-1,y+1);
        }
        if(full){bu_begin("");bu_hole_name((row<<5)-(row<<1));bu_line[1]=0;
            if(row<2||row>11)bu_begin(row==0||row==12?"+":"-");bu_label(bu_bx+2,y+2,bu_line);}
    }
    if(cursor<=bu_dirty_bottom)bu_canvas_gap(cursor,bu_dirty_bottom,width);
    os88_set_color(OS88_WHITE);
    if(bu_gx+width<bu_bx+bu_bw)os88_gfx_fill(bu_gx+width,bu_dirty_top,bu_bx+bu_bw-1,bu_dirty_bottom);
    if(full&&bu_overlap(bu_by,bu_by+14)){
        for(col=0;col<bu_cols&&bb_doc.panx+col<30;col++){
            hole=bb_doc.panx+col;
            if(!hole||(hole+1)%5==0){bu_begin("");bu_number(hole+1);bu_label((bu_hx(hole)-4)&~7,bu_by+2,bu_line);}
        }
    }
    for(i=0;i<bb_doc.nw;i++)bu_wire_draw(&bb_doc.wire[i],0);
    for(i=0;i<bb_doc.np;i++)bu_part_draw(&bb_doc.part[i],0);
    if(bu_ready&&bu_tool==BU_PART)bu_part_draw(&bu_part,1);
    if(bu_ready&&bu_tool==BU_WIRE)bu_wire_draw(&bu_wire,1);
    if(bu_hole<BB_HOLES&&bu_visible(bu_hole)){
        os88_set_color(OS88_BLACK);bu_canvas_frame(bu_hx(bu_hole)-4,bu_hy(bu_hole)-4,bu_hx(bu_hole)+4,bu_hy(bu_hole)+4);
    }
}
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
static int ovl_bu_probe_logic(unsigned channel)
{
    BBWord h;int i;
    if(!bb_valid||bb_result.diag||bb_doc.probe_kind[channel]==BB_PROBE_BRANCH)return -1;
    h=bb_doc.probe[channel];
    if(bb_doc.probe_kind[channel]==BB_PROBE_PIN){i=bu_part_index(bb_doc.probe_id[channel]);if(i<0)return -1;h=bb_pin(&bb_doc.part[i],bb_doc.probe_pin[channel]);}
    if(h>=BB_HOLES||bb_hnet[h]>=BB_NETS)return BB_Z;
    return bb_result.logic[bb_hnet[h]];
}
static int ovl_bu_logic_label(unsigned channel)
{
    int state;state=ovl_bu_probe_logic(channel);
    bu_append(" L:");bu_append(state<0?"--":state==BB_LOW?"0":state==BB_HIGH?"1":state==BB_X?"X":"Z");return 1;
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
static int ovl_bu_compact_info(int full)
{
    int x,y,rx,cols,idx,pin;BBPart *part;BBWord h;
    x=bu_x+8;y=bu_y+42;rx=(bu_x+bu_w/2+7)&~7;cols=(bu_w/2-16)/8;
    if(full){os88_set_color(OS88_WHITE);os88_gfx_fill(bu_x,bu_y+36,bu_x+bu_w-1,bu_y+bu_h-13);}
    idx=bu_selected_kind==BB_PART?bu_part_index(bu_selected):-1;
    bu_begin("Inspector");if(idx>=0){part=&bb_doc.part[idx];bu_begin("");bu_ident(part);bu_append(" ");bu_append(bb_part_name(part->type));}
    bu_text(0,x,y,bu_line,cols,full);
    bu_begin("");if(idx>=0)ovl_bu_value(part->type,part->value);else bu_append("Select a part");bu_text(1,x,y+12,bu_line,cols,full);
    bu_begin("Param ");if(idx>=0)bu_number(part->param);else bu_append("--");bu_text(2,x,y+24,bu_line,cols,full);
    bu_begin("Pin ");if(idx>=0){pin=bu_selected_pin?bu_selected_pin:1;bu_number(pin);bu_append(" ");ovl_bu_pin_caption(part->type,pin);bu_append(" ");h=bb_pin(part,pin);bu_hole_name(h);}else bu_append("--");bu_text(3,x,y+36,bu_line,cols,full);
    bu_begin("I1->2 ");
    if(idx<0||part->type!=BB_RES&&part->type!=BB_LED&&part->type!=BB_CAP&&part->type!=BB_POWER)bu_append("N/A");
    else if(!bb_valid)bu_append("stale");else if(bb_result.diag||bb_result.ua[idx]==BB_UA_INVALID)bu_append("failed");else{bu_signed(bb_result.ua[idx]);bu_append("uA");}
    bu_text(4,x,y+48,bu_line,cols,full);
    bu_begin(bu_channel?"Probe B":"Probe A");bu_text(5,rx,y,bu_line,cols,full);
    bu_begin("");if(bb_doc.probe_kind[bu_channel]==BB_PROBE_HOLE)bu_hole_name(bb_doc.probe[bu_channel]);else{bu_append("Part ");bu_number(bb_doc.probe_id[bu_channel]);bu_append(" pin ");bu_number(bb_doc.probe_pin[bu_channel]);}bu_text(6,rx,y+12,bu_line,cols,full);
    bu_begin("");if(!bb_valid)bu_append("stale");else if(bb_doc.probe_kind[bu_channel]==BB_PROBE_BRANCH){idx=bu_part_index(bb_doc.probe_id[bu_channel]);if(idx<0||bb_result.ua[idx]==BB_UA_INVALID)bu_append("N/A");else{bu_signed(bb_result.ua[idx]);bu_append("uA 1->2");}}else{ovl_bu_volts(ovl_bu_probe_mv(bu_channel));ovl_bu_logic_label(bu_channel);}bu_text(7,rx,y+24,bu_line,cols,full);
    bu_begin("Parts ");bu_number(bb_doc.np);bu_append(" Nets ");bu_number(bb_nnet);bu_text(8,rx,y+36,bu_line,cols,full);
    if(full){bu_label(x,y+64,"E value  T param  [ ] pin");bu_label(rx,y+48,"Tab A/B; P bind; Esc board");}
    return 1;
}
static int ovl_bu_inspector(int full)
{
    int x,y,i,pin,cols;BBWord h;BBPart *part;
    if(bu_compact){ovl_bu_compact_info(full);return 1;}
    x=bu_right+8;y=bu_y+28;cols=15;
    if(full){os88_set_color(OS88_WHITE);os88_gfx_fill(x-4,y-4,bu_x+bu_w-1,bu_scope_y-1);}
    bu_begin("Inspector");bu_text(0,x,y,bu_line,cols,full);y+=16;
    i=bu_selected_kind==BB_PART?bu_part_index(bu_selected):-1;
    if(i>=0){
        part=&bb_doc.part[i];bu_begin("");bu_ident(part);bu_append(" ");bu_append(bb_part_name(part->type));
        bu_text(1,x,y,bu_line,cols,full);y+=12;
        bu_begin("");ovl_bu_value(part->type,part->value);bu_text(2,x,y,bu_line,cols,full);y+=12;
        bu_begin("Param ");bu_number(part->param);if(part->type==BB_SWITCH)bu_append(part->flags&BB_CLOSED?" ON":" OPEN");
        bu_text(3,x,y,bu_line,cols,full);y+=12;
        if(full){bu_label(x,y,"E Value  T Param");bu_label(x,y+12,"R Rotate M Move");bu_label(x,y+24,"Del   Space toggle");}y+=40;
        bu_begin("Pin ");pin=bu_selected_pin?bu_selected_pin:1;bu_number(pin);bu_append(" ");ovl_bu_pin_caption(part->type,pin);bu_append(": ");h=bb_pin(part,pin);bu_hole_name(h);
        bu_text(4,x,y,bu_line,cols,full);y+=12;
        bu_begin("I 1->2: ");
        if(part->type!=BB_RES&&part->type!=BB_LED&&part->type!=BB_CAP&&part->type!=BB_POWER)bu_append("N/A");
        else if(!bb_valid)bu_append("stale");else if(bb_result.diag||bb_result.ua[i]==BB_UA_INVALID)bu_append("failed");
        else{bu_signed(bb_result.ua[i]);bu_append(" uA");}
        bu_text(5,x,y,bu_line,cols,full);y+=16;
    }else{
        bu_begin(bu_selected_kind==BB_WIRE?"Wire ":"Select a part");if(bu_selected_kind==BB_WIRE)bu_number(bu_selected);
        bu_text(1,x,y,bu_line,cols,full);y+=12;
        if(full){bu_label(x,y,"Click holes to snap");bu_label(x,y+12,"W Wire  P Probe");bu_label(x,y+24,"Enter commits");}y+=40;
    }
    bu_begin(bu_channel?"Probe B: ":"Probe A: ");
    if(bb_doc.probe_kind[bu_channel]==BB_PROBE_HOLE)bu_hole_name(bb_doc.probe[bu_channel]);
    else{bu_append("part ");bu_number(bb_doc.probe_id[bu_channel]);if(bb_doc.probe_kind[bu_channel]==BB_PROBE_PIN){bu_append(".");bu_number(bb_doc.probe_pin[bu_channel]);}else bu_append(" current");}
    bu_text(6,x,y,bu_line,cols,full);y+=12;
    bu_begin("");
    if(!bb_valid)bu_append("stale");
    else if(bb_doc.probe_kind[bu_channel]==BB_PROBE_BRANCH){
        i=bu_part_index(bb_doc.probe_id[bu_channel]);
        if(i<0||bb_result.ua[i]==BB_UA_INVALID)bu_append("N/A");else{bu_signed(bb_result.ua[i]);bu_append(" uA 1->2");}
    }else{ovl_bu_volts(ovl_bu_probe_mv(bu_channel));ovl_bu_logic_label(bu_channel);}
    bu_text(7,x,y,bu_line,cols,full);y+=12;
    if(full)bu_label(x,y,"Tab: A/B  P: bind");y+=20;
    bu_begin("Parts ");bu_number(bb_doc.np);bu_append(" Wires ");bu_number(bb_doc.nw);
    bu_text(8,x,y,bu_line,cols,full);y+=12;
    bu_begin("Nets ");bu_number(bb_nnet);bu_text(9,x,y,bu_line,cols,full);
    return 1;
}
static int ovl_bu_palette(void)
{
    int i,type,x,y,cols,rows,index,capacity;
    if(bu_compact||bu_mode==BU_PALETTE){
        cols=bu_w/144;if(cols<1)cols=1;rows=(bu_h-56)/16;if(rows<1)rows=1;
        capacity=cols*rows;if(bu_palette_page*capacity>=16)bu_palette_page=0;
        for(i=0;i<capacity;i++){
            index=i+bu_palette_page*capacity;if(index>=16)break;type=index+1;
            x=bu_x+8+(i/rows)*144;y=bu_y+42+(i%rows)*16;
            if(x+128>=bu_x+bu_w)break;
            bu_begin("");bu_number(type);bu_append(" ");bu_append(bb_part_name(type));
            os88_font_run(x,y,bu_line,type==bu_type?OS88_WHITE:OS88_BLACK,type==bu_type?OS88_BLACK:OS88_WHITE);
        }
        bu_label(bu_x+8,bu_y+bu_h-24,"Click part; Tab changes page; Esc board");
    }else{
        bu_label(bu_x+8,bu_y+28,"Parts");
        for(i=0;i<8;i++){
            type=bu_palette_page*8+i+1;y=bu_y+44+i*16;
            if(y+8>=bu_scope_y)break;
            os88_font_run(bu_x+4,y,bb_part_name(type),type==bu_type?OS88_WHITE:OS88_BLACK,type==bu_type?OS88_BLACK:OS88_WHITE);
        }
        y=bu_y+44+8*16;
        if(y+8<bu_scope_y)bu_label(bu_x+8,y,"More >");
        if(y+40<bu_scope_y){bu_label(bu_x+8,y+24,"Examples");bu_label(bu_x+8,y+40,"F1 Help");}
    }

    return 1;
}
static void bu_status(int full)
{
    int n;unsigned lo,hi,delta,ticks;
    bu_begin("");if(bu_hole<BB_HOLES){bu_hole_name(bu_hole);bu_append(" ");}
    if(bb_result.diag&&!bb_recompile&&!bu_diag_stale){bu_append("Click error: ");bu_append(bb_diag_text(bb_result.diag));}
    else if(bu_msg[0])bu_append(bu_msg);
    else if(bu_ready)bu_append("Preview: Enter commit; Esc cancel");
    else if(bu_tool==BU_PART)bu_append("Place pins with clicks; Enter commit");
    else if(bu_tool==BU_WIRE)bu_append("Wire: endpoints, B bend, Enter commit");
    else if(bu_tool==BU_PROBE)bu_append("Probe: click hole/pin/part; Tab A/B");
    else bu_append(bb_valid?(bb_running?"Running ":"Paused "):"Stale ");
    n=(bu_w-(bu_w<560?128:200)-8)/8;if(n>47)n=47;bu_text(10,bu_x+4,bu_y+bu_h-10,bu_line,n,full);
    ticks=os88_ticks();delta=ticks-bu_speed_tick;
    if(delta>=18){
        lo=bb_result.tlo-bu_speed_lo;hi=bb_result.thi-bu_speed_hi-(bb_result.tlo<bu_speed_lo);
        bu_speed=hi?65535u:bb_muldiv(lo,18,delta);
        bu_speed_tick=ticks;bu_speed_lo=bb_result.tlo;bu_speed_hi=bb_result.thi;
    }
    bu_begin("t ");bb_timefmt(bu_timebuf,bb_result.tlo,bb_result.thi);bu_append(bu_timebuf);bu_append("s");
    if(bu_w>=560){bu_append(" ");bu_number(bu_speed/10);bu_append(".");bu_number(bu_speed%10);bu_append("ms/s");}
    bu_text(12,bu_x+bu_w-(bu_w<560?128:200),bu_y+bu_h-10,bu_line,bu_w<560?16:24,full);
}
static void bu_toolbar(int full)
{
    int i,x,w,state;
    w=(bu_w-4)/9;if(w>64)w=64;
    for(i=0;i<9;i++){
        x=bu_x+2+i*w;if(!bu_bounds(x,bu_y+2,x+w-3,bu_y+17))continue;state=(i==0&&bu_tool==BU_SELECT)||(i==1&&bu_tool==BU_WIRE)||(i==2&&bu_tool==BU_PROBE)||(i==5&&bb_running);
        if(full){os88_set_color(OS88_BLACK);os88_gfx_frame(x,bu_y+2,x+w-3,bu_y+17);}
        os88_font_run(x+3,bu_y+6,(w<54?bu_smalltools[i]:bu_tools[i]),state?OS88_WHITE:OS88_BLACK,state?OS88_BLACK:OS88_WHITE);
    }
    if(bu_compact&&full&&bu_mode<=BU_SCOPE){bu_label(bu_x+8,bu_y+24,"Board     Parts     Inspect     Scope");}
}
static void bu_scope_draw(int full)
{
    int x,y,w,h,stride,i,j,idx,px,py,ox,oy,ch,have;
    unsigned newest,lo,hi,spanlo,spanhi,distlo,disthi;
    int mv;
    if(bu_compact){x=(bu_x+23)&~7;y=bu_y+54;w=(bu_w-48)&~7;h=bu_h-82;}
    else{x=(bu_bx+23)&~7;y=bu_scope_y+18;w=(bu_w-(bu_bx-bu_x)-48)&~7;h=bu_scope_h-38;}
    if(w>384)w=384;if(h>64)h=64;if(w<32||h<12)return;
    if(!bu_bounds(x-16,y-16,x+w,y+h+12))return;stride=w/8;
    bu_begin("A solid / B dashed 0..6V ");bu_append(!bb_valid?"STALE":bb_running?"RUN":"PAUSE");bu_text(14,x-16,y-14,bu_line,bu_min((w+16)/8,47),full);
    bb_bytes(bu_scopebits,stride*h,0);
    for(i=0;i<=10;i++)bb_scope_line(bu_scopebits,stride,i*(w-1)/10,0,i*(w-1)/10,h-1,1);
    for(i=0;i<3;i++)bb_scope_line(bu_scopebits,stride,0,i*(h-1)/2,w-1,i*(h-1)/2,1);
    if(bb_scope.count){
        newest=(bb_scope.head+255)&255;lo=bb_scope.lo[newest];hi=bb_scope.hi[newest];
        spanlo=bb_doc.scope_div;spanhi=0;
        for(i=1;i<10;i++){spanlo+=bb_doc.scope_div;if(spanlo<bb_doc.scope_div)spanhi++;}
        for(ch=0;ch<2;ch++){
            have=0;
            for(j=0;j<bb_scope.count;j++){
                idx=(bb_scope.head+256-bb_scope.count+j)&255;
                distlo=lo-bb_scope.lo[idx];disthi=hi-bb_scope.hi[idx]-(lo<bb_scope.lo[idx]);
                if(disthi>spanhi||(disthi==spanhi&&distlo>spanlo)){have=0;continue;}
                {unsigned sl,sh,dl,dh;sl=spanlo;sh=spanhi;dl=distlo;dh=disthi;
                    while(sh){sl=(sl>>1)|(sh<<15);sh>>=1;dl=(dl>>1)|(dh<<15);dh>>=1;}
                    px=w-1-bb_muldiv(dl,w-1,sl?sl:1);
                }
                mv=bb_scope.mv[ch][idx];if(mv<0||mv>6000){have=0;continue;}
                py=h-1-bb_muldiv(mv,h-1,6000);
                if(have){bb_scope_line(bu_scopebits,stride,ox,oy,px,oy,ch);bb_scope_line(bu_scopebits,stride,px,oy,px,py,ch);}
                else bb_scope_line(bu_scopebits,stride,px,py,px,py,ch);
                ox=px;oy=py;have=1;
            }
        }
    }
    if(os88_gfx_blit1(bu_scopebits,stride,x,y,w,h)!=0){bu_label(x,y,"Scope needs band drawing");}
    bu_begin("");bu_number(bb_doc.scope_div/10);if(bb_doc.scope_div%10){bu_append(".");bu_number(bb_doc.scope_div%10);}bu_append("ms/div ");
    if(bb_scope.undersampled)bu_append("UNDERSAMPLED ");
    if(bb_scope.cycles&&bb_scope.period_hi==0&&bb_scope.period_lo){
        bu_number(10000/bb_scope.period_lo);bu_append(".");i=bb_muldiv(10000%bb_scope.period_lo,100,bb_scope.period_lo);if(i<10)bu_append("0");bu_number(i);bu_append("Hz ");
        if(bb_scope.high_hi==0){bu_number(bb_muldiv(bb_scope.high_lo,100,bb_scope.period_lo));bu_append("% ");}
    }else bu_append(bb_scope.period_hi?"long period ":"need cycles ");
    bu_append(bb_doc.trigger==1?"rise":bb_doc.trigger==2?"fall":"free");
    bu_text(11,x,y+h+3,bu_line,bu_min(w/8,63),full);
}
static const char *bu_help_lines[]={
    "1/5  Build a circuit", "Columns1-30; rowsa-j. a-e share",
    "a strip; f-j share another. Trench",
    "isolates them. Rails split15|16.",
    "Parts: choose, click each pin, Enter.",
    "Wire: click ends, Enter. B adds bend.",
    "Esc cancels. Crossing is not a joint.",
    "Enter next page; Esc breadboard.",
    "2/5  Edit and measure", "Select part/pin or wire. E value;",
    "T parameter; R rotate; M move part.",
    "Del removes; Ctrl-Z/Y undo/redo.",
    "Moving/removing parts leaves wires.",
    "P probe; Tab chooses A/B channel.",
    "Click pin: voltage; body: I1->2 uA.",
    "Enter next page; Esc breadboard.",
    "3/5  Simulation", "Space Run/Pause; . one timestep.",
    "Reset restores C initial voltage.",
    "Edits pause and stale old results.",
    "Pause holds state; Run resumes it.",
    "Simulation time is calculation time.",
    "An XT may calculate below real time.",
    "Enter next page; Esc breadboard.",
    "4/5  Models and units", "Educational models, not SPICE.",
    "R100..655350ohm; 1..60000nF C.",
    "5V default; allowed4.5..6V supply.",
    "LED2V+10ohm; no hidden limiter.",
    "Max30mA/branch. No heat/avalanche.",
    "HC inputs0.3/0.7VCC; X/Z explicit.",
    "Enter next page; Esc breadboard.",
    "5/5  Timer, scope and limits", "555:1GND2TRIG3OUT4RESET5CONT",
    "6THRES7DISCH8VCC. Reset wins.",
    "CONT only open/ground bypass cap.",
    "64parts128wires64nets24unknowns.",
    "1ms default; C backward Euler.",
    "ScopeA/B0..6V; +/- time; G trigger.",
    "Scope holds values between samples."
};
static int ovl_bu_help(void)
{
    int i,y,rows,start,total;
    rows=(bu_h-52)/12;if(rows<1)rows=1;if(rows>7)rows=7;
    total=(39/rows)+1;if(bu_help_page>=total)bu_help_page=0;
    start=bu_help_page*rows;y=bu_y+28;
    for(i=0;i<rows&&start+i<40;i++){bu_label(bu_x+8,y,bu_help_lines[start+i]);y+=12;}
    bu_label(bu_x+8,bu_y+bu_h-24,"Enter: next page   Esc: breadboard");return 1;
}
static int ovl_bu_help_next(void)
{int rows;rows=(bu_h-52)/12;if(rows<1)rows=1;if(rows>7)rows=7;bu_help_page=(bu_help_page+1)%((39/rows)+1);bu_flags|=BU_ALL;return 1;}
static int ovl_bu_examples(void)
{
    int i,x,y,rows;
    bu_label(bu_x+8,bu_y+27,"Examples: press1-8 or click; Esc returns");
    rows=(bu_h-52)/16;if(rows<1)rows=1;
    for(i=0;i<8;i++){
        x=bu_x+8+(i/rows)*224;y=bu_y+44+(i%rows)*16;
        bu_begin("");bu_number(i+1);bu_append(" ");bu_append(bb_example_name(i+1));bu_label(x,y,bu_line);
    }
    return 1;
}
static int bu_value_x(void){return bu_compact?bu_x+8:bu_right+8;}
static int bu_value_y(void){return bu_compact?bu_y+30:bu_y+28;}
static int bu_value_cells(void){return bu_compact?bu_min((bu_w-16)/8,40):14;}
static void bu_value_text(int full)
{
    int keep;keep=bu_value_cells()-5;if(keep<1)keep=1;
    bu_begin("[ ");bu_append(bu_edit+(bu_edit_len>keep?bu_edit_len-keep:0));bu_append("_ ]");
    bu_text(13,bu_value_x(),bu_value_y()+22,bu_line,bu_value_cells(),full);
}
static int ovl_bu_value_panel(void)
{
    int idx,x,y;BBPart *part;
    idx=bu_part_index(bu_selected);if(idx<0){bu_mode=BU_BOARD;return 1;}part=&bb_doc.part[idx];
    x=bu_value_x();y=bu_value_y();
    if(!bu_compact){os88_set_color(OS88_WHITE);os88_gfx_fill(bu_right+1,bu_y+22,bu_x+bu_w-1,bu_scope_y-1);}
    bu_begin(bu_edit_param?"Parameter ":"Value ");bu_ident(part);bu_label(x,y,bu_line);bu_value_text(1);
    if(bu_edit_param)bu_label(x,y+42,part->type==BB_CAP?"Initial mV":part->type==BB_POT?"Wiper 0..1000":part->type==BB_INPUT?"0/1/X=2/Z=3":"Duty 1..999");
    else bu_label(x,y+42,part->type==BB_CAP?"nF / uF":part->type==BB_POWER?"mV / V":part->type==BB_CLOCK?"us / ms / s":"ohm / k");
    bu_label(x,y+56,"Enter accepts");bu_label(x,y+68,"Esc cancels");return 1;
}
static void bu_render(void *win,int full)
{
    unsigned flags;
    bu_layout(win);flags=bu_flags;
    if(bu_mode==BU_ABOUT){if(full||flags&BU_ALL)os88_about_card_d(win,bu_about);bu_flags=0;return;}
    if(full||flags&BU_ALL){
        full=1;bu_cache_ok=0;
        os88_set_color(OS88_WHITE);os88_gfx_fill(bu_x,bu_y,bu_x+bu_w-1,bu_y+bu_h-1);bu_toolbar(1);
        if(bu_mode==BU_HELP){ovl_bu_help();bu_status(1);goto done;}
        if(bu_mode==BU_EXAMPLES){ovl_bu_examples();bu_status(1);goto done;}
        if(bu_mode==BU_VALUE&&bu_compact){ovl_bu_value_panel();bu_status(1);goto done;}
        if(bu_mode==BU_PALETTE){ovl_bu_palette();bu_status(1);goto done;}
        if(bu_compact&&bu_mode==BU_INSPECT){ovl_bu_inspector(1);bu_status(1);goto done;}
        if(bu_compact&&bu_mode==BU_SCOPE){bu_scope_draw(1);bu_status(1);goto done;}
        if(!bu_compact){
            os88_set_color(OS88_BLACK);os88_gfx_vline(bu_bx-1,bu_y+22,bu_y+bu_h-13);os88_gfx_vline(bu_right,bu_y+22,bu_scope_y-1);
            ovl_bu_palette();if(bu_bounds(bu_right,bu_y+22,bu_x+bu_w-1,bu_scope_y-1)){if(bu_mode==BU_VALUE)ovl_bu_value_panel();else ovl_bu_inspector(1);}
        }
        bu_canvas(1);if(bu_scope_h)bu_scope_draw(1);bu_status(1);
    }else{
        if(bu_mode==BU_VALUE){
            if(flags&BU_INFO_FULL)ovl_bu_value_panel();else if(flags&BU_VALUE_DIRTY)bu_value_text(0);
            if(bu_compact){if(flags&BU_INFO_DIRTY)bu_status(0);goto done;}
        }
        if(bu_mode==BU_HELP||bu_mode==BU_EXAMPLES||bu_mode==BU_PALETTE){if(flags&BU_INFO_DIRTY)bu_status(0);goto done;}
        if(flags&BU_BAR_DIRTY)bu_toolbar(0);
        if(flags&BU_BOARD_DIRTY&&(bu_mode==BU_BOARD||bu_mode==BU_VALUE&&!bu_compact))bu_canvas(0);
        if(flags&BU_INFO_DIRTY){if(bu_mode!=BU_VALUE&&(!bu_compact||bu_mode==BU_INSPECT))ovl_bu_inspector((flags&BU_INFO_FULL)!=0);bu_status(0);}
        if(flags&BU_SCOPE_DIRTY&&(bu_scope_h||bu_compact&&bu_mode==BU_SCOPE))bu_scope_draw(0);
    }
done:
    if(!bu_paint_region)bu_cache_ok=1;
    bu_flags=0;os88_wm_grow(win);
}
static void bu_redraw_locked(void *win)
{
    bu_paint_region=0;if(os88_wm_clip_set(win)==0)bu_render(win,0);
}
static void bu_wake(void){os88_wm_wake(bu_win);}
static int ovl_bu_damage_span(int top,int bottom)
{
    top-=8;bottom+=8;
    if(bottom<bu_gy||top>=bu_by+bu_bh)return 1;
    if(top<bu_gy)top=bu_gy;if(bottom>=bu_by+bu_bh)bottom=bu_by+bu_bh-1;
    if(!(bu_flags&BU_BOARD_DIRTY)){bu_dirty_top=top;bu_dirty_bottom=bottom;}
    else{if(top<bu_dirty_top)bu_dirty_top=top;if(bottom>bu_dirty_bottom)bu_dirty_bottom=bottom;}
    bu_flags|=BU_BOARD_DIRTY;return 1;
}
static int ovl_bu_damage_part(const BBPart *part)
{
    unsigned i,n;int top,bottom,y;BBWord h;
    top=32767;bottom=-32767;n=bb_pincount(part->type);
    for(i=1;i<=n;i++){h=bb_pin(part,i);if(h>=BB_HOLES)continue;y=bu_hy(h);if(y<top)top=y;if(y>bottom)bottom=y;}
    if(top<=bottom)ovl_bu_damage_span(top-16,bottom+16);return 1;
}
static int ovl_bu_damage_wire(const BBWire *wire)
{
    int top,bottom,y;unsigned i;
    top=bu_hy(wire->a);bottom=bu_hy(wire->b);if(top>bottom){y=top;top=bottom;bottom=y;}
    for(i=0;i<wire->nbend;i++){y=bu_hy(wire->bend[i*2+1]*30+wire->bend[i*2]);if(y<top)top=y;if(y>bottom)bottom=y;}
    ovl_bu_damage_span(top-5,bottom+5);return 1;
}
static int ovl_bu_damage_selection(void)
{
    unsigned i;int idx,strip,net;
    idx=bu_pick_index();if(idx>=0){if(bu_selected_kind==BB_PART)ovl_bu_damage_part(&bb_doc.part[idx]);else ovl_bu_damage_wire(&bb_doc.wire[idx]);}
    if(bu_ready){if(bu_tool==BU_PART)ovl_bu_damage_part(&bu_part);else if(bu_tool==BU_WIRE)ovl_bu_damage_wire(&bu_wire);}
    if(bu_hole<BB_HOLES){strip=bb_strip(bu_hole);net=!bb_recompile?bb_hnet[bu_hole]:BB_NNET;
        for(i=0;i<BB_HOLES;i++)if(bb_strip(i)==strip||(bu_netshow&&net<BB_NETS&&bb_hnet[i]==net))ovl_bu_damage_span(bu_hy(i)-5,bu_hy(i)+5);}
    return 1;
}
static int ovl_bu_damage_net(void)
{
    unsigned i;int net,strip;
    if(!bu_netshow||bb_recompile||bu_hole>=BB_HOLES)return 1;
    net=bb_hnet[bu_hole];strip=bb_strip(bu_hole);
    if(net<BB_NETS)for(i=0;i<BB_HOLES;i++)if(bb_hnet[i]==net&&bb_strip(i)!=strip)ovl_bu_damage_span(bu_hy(i)-5,bu_hy(i)+5);
    return 1;
}
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
    if(bu_mode!=BU_BOARD)bu_flags|=BU_ALL;
    bu_mode=BU_BOARD;bu_flags|=BU_INFO_DIRTY|BU_INFO_FULL;bu_note("Located diagnostic; inspect selected pin");return 1;
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
    bu_msg[0]=0;
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
static int ovl_bu_io(unsigned op)
{
    unsigned n;int ok;
    os88_file_here(&bu_choice_place);
    if(op==BU_OP_LOAD){
        if(bu_io_association){
            if(os88_file_goto_q_mark(bu_launch_place.clus,bu_launch_place.vol)!=0){bb_error=BB_EIO;bu_error();return 1;}
            os88_file_here(&bu_choice_place);bu_io_association=0;
        }
        if(bu_io_hi||bu_io_size>BB_FILEMAX){bb_error=BB_ECAPACITY;bu_error();return 1;}
        n=os88_file_read(bu_choice,bu_filebits,BB_FILEMAX);
        if(os88_ferr()!=OS88_FERR_OK){bb_error=BB_EIO;bu_error();ok=0;}
        else ok=bb_decode(bu_filebits,n,&bb_stage);
        if(!ok){bu_error();os88_file_goto_q_mark(bu_doc_place.clus,bu_doc_place.vol);return 1;}
        bb_copy(&bb_doc,&bb_stage,sizeof(bb_doc));bb_history_clear();bb_mark_saved();bb_invalidate();bb_dirty=0;
        bb_sim_reset();ovl_bu_speed_reset();bu_active=0;bu_step_once=0;bu_selected=0;bu_selected_kind=0;bu_hole=BB_NONE;bu_reset_tools();
        os88_strcpy(bu_name,bu_choice,sizeof(bu_name));bb_copy(&bu_doc_place,&bu_choice_place,sizeof(bu_doc_place));
        bu_mode=BU_BOARD;bu_note("Opened editable BBX project");bu_result_changed();bu_flags|=BU_ALL;
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
        if(op==BU_OP_NEW)bb_clear(&bb_stage);else if(!bb_example(arg)){bu_error();return 1;}
        n=bb_encode(&bb_stage,bu_filebits,BB_FILEMAX);
        if(!n||!bb_decode(bu_filebits,n,&bb_stage)){bu_error();return 1;}
        bb_copy(&bb_doc,&bb_stage,sizeof(bb_doc));bb_history_clear();bb_invalidate();bb_sim_reset();ovl_bu_speed_reset();
        bu_active=0;bu_step_once=0;bu_selected=0;bu_selected_kind=0;bu_hole=BB_NONE;bu_reset_tools();
        bu_name[0]=0;if(op==BU_OP_NEW)bb_mark_saved();else bb_dirty=1;bu_mode=BU_BOARD;bu_result_changed();bu_flags|=BU_ALL;
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
    bu_type=type;bu_tool=BU_PART;bu_mode=BU_BOARD;bu_reset_tools();bu_flags|=BU_ALL;
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
static int ovl_bu_onclick(int x,int y,void *win){
    int cell,idx,rows,col,row;BBWord hole;
    bu_user_begin();bu_layout(win);
    if(bu_mode==BU_ABOUT){bu_mode=BU_BOARD;bu_flags|=BU_ALL;bu_redraw_locked(win);return 1;}
    if(bb_result.diag&&!bb_recompile&&!bu_diag_stale&&y>=bu_y+bu_h-12){ovl_bu_locate_error();bu_redraw_locked(win);return 1;}
    if(y<bu_y+20){
        cell=(bu_w-4)/9;if(cell>64)cell=64;idx=(x-bu_x-2)/cell;
        if(idx==0)ovl_bu_tool(BU_SELECT);
        else if(idx==1)ovl_bu_tool(BU_WIRE);
        else if(idx==2)ovl_bu_tool(BU_PROBE);
        else if(idx==3)ovl_bu_rotate();else if(idx==4)bu_queue(BU_OP_UNDO,0);
        else if(idx==5)bu_queue(BU_OP_RUN,0);
        else if(idx==6){bb_running=0;bu_flags|=BU_INFO_DIRTY|BU_BAR_DIRTY|BU_SCOPE_DIRTY;}
        else if(idx==7)bu_queue(BU_OP_STEP,0);else if(idx==8)bu_queue(BU_OP_RESET,0);
        bu_redraw_locked(win);return 1;
    }
    if(bu_compact&&y<bu_y+36){idx=(x-bu_x)/80;if(idx>3)idx=3;bu_mode=idx;bu_flags|=BU_ALL;bu_redraw_locked(win);return 1;}
    if(bu_mode==BU_HELP){ovl_bu_help_next();bu_redraw_locked(win);return 1;}
    if(bu_mode==BU_EXAMPLES){
        rows=(bu_h-52)/16;if(rows<1)rows=1;idx=((x-bu_x-8)/224)*rows+(y-bu_y-44)/16;
        if(y>=bu_y+44&&idx>=0&&idx<8)ovl_bu_request(BU_ACT_EXAMPLE,idx+1);
        return 1;
    }
    if(bu_mode==BU_VALUE)return 1;
    if(bu_mode==BU_PALETTE){
        rows=(bu_h-56)/16;if(rows<1)rows=1;col=bu_w/144;if(col<1)col=1;
        idx=((x-bu_x-8)/144)*rows+(y-bu_y-42)/16+bu_palette_page*rows*col;
        if(y>=bu_y+42&&idx>=0&&idx<16)ovl_bu_choose_type(idx+1);bu_redraw_locked(win);return 1;
    }
    if(!bu_compact&&x<bu_bx){
        idx=(y-bu_y-44)/16;
        if(y>=bu_y+44&&idx>=0&&idx<8)ovl_bu_choose_type(bu_palette_page*8+idx+1);
        else if(y>=bu_y+44+8*16&&y<bu_y+64+8*16){bu_palette_page^=1;bu_flags|=BU_ALL;}
        else if(y>=bu_y+44+8*16+24){bu_mode=BU_EXAMPLES;bu_flags|=BU_ALL;}
        bu_redraw_locked(win);return 1;
    }
    if(bu_mode==BU_INSPECT||!bu_compact&&x>=bu_right){ovl_bu_parameter(0);bu_redraw_locked(win);return 1;}
    if(bu_mode==BU_SCOPE)return 1;
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
    }else if(ascii==9){if(bu_mode==BU_PALETTE){idx=(bu_h-56)/16;if(idx<1)idx=1;idx*=bu_w/144?bu_w/144:1;bu_palette_page=(bu_palette_page+1)%((15/idx)+1);bu_flags|=BU_ALL;}else{bu_channel^=1;bu_flags|=BU_INFO_DIRTY;}}
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
    else if(!ascii&&scan==0x3c){bu_mode=BU_PALETTE;bu_palette_page=0;bu_flags|=BU_ALL;}
    else if(!ascii&&scan==0x3d){bu_mode=bu_compact?BU_INSPECT:BU_BOARD;bu_flags|=BU_ALL;}
    else if(!ascii&&scan==0x3e){if(bu_compact)bu_mode=BU_SCOPE;else bu_scope_on^=1;bu_flags|=BU_ALL;}
    else if(!ascii&&scan==0x4b)ovl_bu_pan(-1,0);
    else if(!ascii&&scan==0x4d)ovl_bu_pan(1,0);
    else if(!ascii&&scan==0x48)ovl_bu_pan(0,-1);
    else if(!ascii&&scan==0x50)ovl_bu_pan(0,1);
    else if(!ascii&&scan==0x49)ovl_bu_pan(0,-4);
    else if(!ascii&&scan==0x51)ovl_bu_pan(0,4);
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
        else if(item==6)ovl_bu_choose_type(BB_POT);else{bu_mode=BU_PALETTE;bu_palette_page=0;bu_flags|=BU_ALL;}
    }else if(menu==3){
        if(item==0)bu_queue(BU_OP_RUN,0);else if(item==1){bb_running=0;bu_flags|=BU_BAR_DIRTY|BU_INFO_DIRTY|BU_SCOPE_DIRTY;}
        else if(item==2)bu_queue(BU_OP_STEP,0);else if(item==3)bu_queue(BU_OP_RESET,0);
        else{if(item==4){if(bb_doc.dt>1)bb_doc.dt/=2;}else if(bb_doc.dt<500)bb_doc.dt=bb_doc.dt<<1;else bb_doc.dt=1000;
            bb_invalidate();bu_diag_stale=1;bu_active=0;bu_begin("Timestep ");bu_number(bb_doc.dt);bu_append(" x100us; Reset before comparing");bu_note(bu_line);bu_flags|=BU_INFO_DIRTY|BU_BAR_DIRTY|BU_SCOPE_DIRTY;}
    }else if(menu==4){
        if(item<=3){if(item==3&&!bu_compact)bu_scope_on^=1;else bu_mode=item==2&&!bu_compact?BU_BOARD:item;if(item==1)bu_palette_page=0;}
        else if(item==4){bb_doc.zoom=bb_doc.zoom==2?1:2;ovl_bu_view_changed();}
        else if(item==5)bu_netshow^=1;
        else if(item==6){bu_mode=BU_HELP;bu_help_page=0;}else bu_mode=BU_EXAMPLES;
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
    if(op)ovl_bu_execute(op,arg);
    if(bu_closing)return;
    if(bb_running||bu_step_once){
        if(!bu_active){if(bb_sim_start(0)){ovl_bu_speed_reset();bu_active=1;}else{bb_running=0;bu_step_once=0;bu_error();bu_result_changed();bu_flags|=BU_BAR_DIRTY|BU_SCOPE_DIRTY;}}
        if(bu_active){
            result=bb_sim_slice(16);
            if(result==1){bu_result_changed();if(bu_step_once){bu_step_once=0;bb_running=0;bu_flags|=BU_BAR_DIRTY|BU_SCOPE_DIRTY;}}
            else if(result<0){bu_diag_stale=0;bu_active=0;bu_step_once=0;bb_running=0;bu_result_changed();bu_flags|=BU_INFO_DIRTY|BU_BAR_DIRTY|BU_SCOPE_DIRTY;}
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
    bb_clear(&bb_doc);bb_history_clear();bb_mark_saved();
    bu_type=BB_RES;bu_tool=BU_SELECT;bu_hole=BB_NONE;bu_scope_on=1;bu_color=OS88_RED;
    bu_reset_tools();bu_flags=BU_ALL;bu_netshow=1;
    os88_video(&bu_video);
    os88_strcpy(bu_title,"UNTITLED.BBX - Breadboard XT",sizeof(bu_title));
    bu_win=os88_wm_create(0,OS88_MBAR_H,bu_video.w,bu_video.dock_top-OS88_MBAR_H,bu_title,&bu_class);
    if(!bu_win)return 0;
    os88_wm_minsize(bu_win,320,128);os88_menu_set(bu_win,&bu_menus);
    os88_file_here(&bu_doc_place);
    bu_timer_ok=os88_wm_timer(bu_win,2)==0;
    if(os88_arg_file(bu_choice,&bu_launch_place)==0){bu_io_association=1;bu_io_hi=0;bu_io_size=0;bu_op=BU_OP_LOAD;}
    bu_note("Parts to place; W wire; Enter commit; F1 Help");
    return bu_win;
}
