/* Host-only UI glass/queue regression harness. It runs the shipping callbacks
 * with a clipped opaque drawing model, not a target/emulator substitute. */
#define main vx_core_unused_main
#include "coretest.c"
#undef main
#include "sdk-host.h"
#include "../ui.c"
#undef long
#undef float
#undef double

static unsigned char ug_glass[720*480],ug_before[720*480];
static int ug_width=640,ug_height=480,ug_lock,ug_ink,ug_disabled,ug_damage_on;
static int ug_x1,ug_y1,ug_x2=639,ug_y2=479,ug_w=640,ug_h=438;
static unsigned ug_ticks,ug_glyphs,ug_calls,ug_failures,ug_checks,ug_wakes,ug_diagonals,ug_scope_lines;
static int ug_negative;
static char ug_textlog[16384],ug_toast[128];
static unsigned ug_textn;
static BBByte ug_filebits[BB_FILEMAX];
static unsigned ug_filesize;
static struct os88_rect ug_damage;
static struct os88_place ug_place={0,1};
static char ug_token;
static void *ug_win=&ug_token;
static void ug_check(int yes,const char *what,int line){ug_checks++;if(!yes){ug_failures++;fprintf(stderr,"FAIL ui:%d %s\n",line,what);}}
#define UC(e) ug_check(!!(e),#e,__LINE__)
static void ug_pixel(int x,int y,int colour){if(x>=0&&x<ug_width&&y>=0&&y<ug_height&&x>=ug_x1&&x<=ug_x2&&y>=ug_y1&&y<=ug_y2)ug_glass[y*720+x]=(unsigned char)(ug_disabled&&((x+y)&1)?15:colour);}
static void ug_diff(const char *label){unsigned x,y,n=0,minx=720,miny=480,maxx=0,maxy=0;for(y=0;y<480;y++)for(x=0;x<720;x++)if(ug_before[y*720+x]!=ug_glass[y*720+x]){n++;if(x<minx)minx=x;if(x>maxx)maxx=x;if(y<miny)miny=y;if(y>maxy)maxy=y;}fprintf(stderr,"%s: %u differing pixels, bounds %u,%u..%u,%u\n",label,n,minx,miny,maxx,maxy);}
static void ug_clip_full(void){ug_x1=0;ug_y1=OS88_MBAR_H+OS88_TITLE_H;ug_x2=ug_w-1;ug_y2=OS88_MBAR_H+ug_h-1;}
static void ug_event_begin(void){ug_lock=1;ug_clip_full();}
static void ug_event_end(void){ug_lock=0;}
static void ug_key(int ascii,int scan){ug_event_begin();os88_onkey(ascii,scan,ug_win);ug_event_end();}
static void ug_cmd(int item,int menu){ug_event_begin();os88_oncmd(item,menu,ug_win);ug_event_end();}
static void ug_wake(void){ug_ticks+=2;os88_onwake(ug_win);}

void os88_gfx_lock(void){UC(ug_lock==0);ug_lock=1;}
void os88_gfx_unlock(void){UC(ug_lock==1);ug_lock=0;}
void os88_set_color(int c){ug_ink=c;}
void os88_gfx_pen(int disabled){ug_disabled=disabled;}
void os88_gfx_pixel(int x,int y){ug_calls++;ug_pixel(x,y,ug_ink);}
void os88_gfx_fill(int x1,int y1,int x2,int y2){int x,y;ug_calls++;for(y=y1;y<=y2;y++)for(x=x1;x<=x2;x++)ug_pixel(x,y,ug_ink);}
void os88_gfx_fill_gray(int x1,int y1,int x2,int y2){int x,y;ug_calls++;for(y=y1;y<=y2;y++)for(x=x1;x<=x2;x++)ug_pixel(x,y,(x+y)&1?0:15);}
void os88_gfx_hline(int x1,int x2,int y){int x;ug_calls++;for(x=x1;x<=x2;x++)ug_pixel(x,y,ug_ink);}
void os88_gfx_vline(int x,int y1,int y2){int y;ug_calls++;for(y=y1;y<=y2;y++)ug_pixel(x,y,ug_ink);}
void os88_gfx_frame(int x1,int y1,int x2,int y2){os88_gfx_hline(x1,x2,y1);os88_gfx_hline(x1,x2,y2);os88_gfx_vline(x1,y1,y2);os88_gfx_vline(x2,y1,y2);}
void os88_font_run(int x,int y,const char *s,int ink,int paper){unsigned i;int xx,yy;ug_calls++;for(i=0;s[i]&&ug_textn<sizeof(ug_textlog)-2;i++)ug_textlog[ug_textn++]=s[i];if(ug_textn<sizeof(ug_textlog)-1)ug_textlog[ug_textn++]='|';ug_textlog[ug_textn]=0;for(i=0;s[i];i++){ug_glyphs++;for(yy=0;yy<8;yy++)for(xx=0;xx<8;xx++)ug_pixel(x+8*i+xx,y+yy,s[i]==' '?paper:(((unsigned char)s[i]>>(xx%7))^(yy&1))&1?ink:paper);}}
int os88_gfx_blit1(const void *raw,int stride,int x,int y,int w,int rows){const unsigned char *p=raw;int xx,yy;ug_calls++;if(x%8||w%8||rows<1||rows>255)return -1;for(yy=0;yy<rows;yy++)for(xx=0;xx<w;xx++)ug_pixel(x+xx,y+yy,p[yy*stride+xx/8]&(128>>(xx&7))?15:0);return 0;}
void os88_memcpy(void *to,const void *from,unsigned n){memmove(to,from,n);}
void os88_memset(void *to,int value,unsigned n){memset(to,value,n);}
unsigned os88_strlen(const char *s){return (unsigned)strlen(s);}
void os88_strcpy(char *to,const char *from,unsigned cap){if(!cap)return;while(--cap&&*from)*to++=*from++;*to=0;}
char *os88_utoa(unsigned n,char *out){snprintf(out,8,"%u",n&65535u);return out;}
char *os88_itoa(int n,char *out){snprintf(out,8,"%d",n);return out;}
void os88_video(struct os88_video *v){memset(v,0,sizeof(*v));v->w=ug_width;v->h=ug_height;v->dock_top=ug_height-24;v->kind=ug_height==200?OS88_VID_CGA:OS88_VID_VGA;v->bpp=ug_height==200?1:4;}
void os88_wm_display(void *win,struct os88_video *v){(void)win;os88_video(v);}
void *os88_wm_create(int x,int y,int w,int h,const char *title,struct os88_class *cls){(void)x;(void)y;(void)title;(void)cls;ug_w=w;ug_h=h;return ug_win;}
void os88_wm_content(void *win,struct os88_pt *p){(void)win;p->x=0;p->y=OS88_MBAR_H+OS88_TITLE_H;}
int os88_wm_geom(void *win,struct os88_size *s){(void)win;s->w=ug_w;s->h=ug_h-OS88_TITLE_H-1;return 0;}
int os88_wm_clip_set(void *win){(void)win;ug_clip_full();return 0;}
int os88_wm_damage(void *win,struct os88_rect *r){(void)win;if(ug_damage_on){*r=ug_damage;return ug_negative?1:0;}return 1;}
void os88_wm_grow(void *win){(void)win;}
void os88_wm_minsize(void *win,int w,int h){(void)win;(void)w;(void)h;}
void os88_wm_title(void *win,const char *title){(void)win;(void)title;}
void os88_menu_set(void *win,struct os88_menuset *set){(void)win;(void)set;}
int os88_wm_wake(void *win){(void)win;ug_wakes++;return 0;}
int os88_wm_timer(void *win,int ticks){(void)win;(void)ticks;return 0;}
void os88_wm_close(void *win){(void)win;}
unsigned os88_ticks(void){return ug_ticks;}
void os88_file_here(struct os88_place *place){*place=ug_place;}
int os88_file_goto_q_mark(unsigned clus,int vol){ug_place.clus=clus;ug_place.vol=vol;return 0;}
int os88_arg_file(char *name,struct os88_place *place){(void)name;(void)place;return -1;}
int os88_file_dlg(int mode,void *win,const char *name){(void)mode;(void)win;(void)name;return 0;}
unsigned os88_file_read(const char *name,void *buf,unsigned cap){unsigned n;UC(ug_lock==0);if(!strcmp(name,"CACHE.BBX")&&ug_filesize){n=ug_filesize<cap?ug_filesize:cap;memmove(buf,ug_filebits,n);return n;}return 0;}
int os88_file_write(const char *name,const void *buf,unsigned n){(void)name;(void)buf;(void)n;UC(ug_lock==0);return -1;}
int os88_ferr(void){return OS88_FERR_OK;}
int os88_toast(const char *message,int ticks){(void)ticks;snprintf(ug_toast,sizeof(ug_toast),"%s",message);return 0;}
static void ug_about(void *win,const char **lines){unsigned i;(void)win;os88_set_color(15);os88_gfx_fill(32,52,ug_w-33,ug_h-20);os88_set_color(0);os88_gfx_frame(32,52,ug_w-33,ug_h-20);for(i=0;lines[i];i++)os88_font_run(40,60+i*10,lines[i],0,15);}
void os88_about_card(void *win,const char **lines){ug_about(win,lines);}
void os88_about_card_d(void *win,const char **lines){ug_about(win,lines);}
#define UG_TRAMP(name) void os88_t_##name(void){}
UG_TRAMP(paint) UG_TRAMP(onkey) UG_TRAMP(onclick) UG_TRAMP(about)
UG_TRAMP(onwake) UG_TRAMP(onclose) UG_TRAMP(onmouseup) UG_TRAMP(onresize)
UG_TRAMP(ontimer) UG_TRAMP(onwheel)
void bb_timefmt(char *out,unsigned lo,unsigned hi){uint32_t t=((uint32_t)hi<<16)|(lo&65535u);snprintf(out,20,"%u.%04u",t/10000,t%10000);}
void bb_bytes(void *p,unsigned n,unsigned byte){memset(p,byte,n);}
unsigned bb_muldiv(unsigned a,unsigned b,unsigned denominator){uint32_t n;if(!denominator)return 65535;n=(uint32_t)a*b/denominator;return n>65535?65535:n;}
int bb_ask(const char *message,void *win,int buttons){(void)message;(void)win;(void)buttons;return 0;}
static void ug_bit(void *raw,int stride,int x,int y,int black){unsigned char *p=raw;unsigned char mask=(unsigned char)(128>>(x&7));if(black)p[y*stride+x/8]&=(unsigned char)~mask;else p[y*stride+x/8]|=mask;}
void bb_board_row(void *raw,int stride,int cols,int pitch,const BBWord *marks){int c,x,y,mode;memset(raw,255,stride*pitch);for(c=0;c<cols;c++){x=8+c*pitch;for(y=4;y<=6;y++){ug_bit(raw,stride,x-1,y,1);ug_bit(raw,stride,x,y,1);ug_bit(raw,stride,x+1,y,1);}mode=(marks[c/16]>>(c&15))&1?2:(marks[2+c/16]>>(c&15))&1;for(y=0;mode&&y<pitch;y++)if(mode==2||!(y&1)){ug_bit(raw,stride,x-4,y,1);ug_bit(raw,stride,x+4,y,1);}}}
void bb_scope_line(void *raw,int stride,int x1,int y1,int x2,int y2,int dash){ug_scope_lines++;if(x1!=x2&&y1!=y2)ug_diagonals++;int dx=abs(x2-x1),sx=x1<x2?1:-1,dy=-abs(y2-y1),sy=y1<y2?1:-1,e=dx+dy,e2;for(;;){if(!dash||!((x1+y1)&2))ug_bit(raw,stride,x1,y1,0);if(x1==x2&&y1==y2)break;e2=2*e;if(e2>=dy){e+=dy;x1+=sx;}if(e2<=dx){e+=dx;y1+=sy;}}}

static void ug_paint(int partial){ug_event_begin();ug_damage_on=partial;if(partial){ug_x1=ug_damage.x1;ug_y1=ug_damage.y1;ug_x2=ug_damage.x2;ug_y2=ug_damage.y2;}os88_paint(ug_win);ug_damage_on=0;ug_event_end();}
static void ug_init(int height){ug_height=height;ug_width=640;memset(ug_glass,15,sizeof(ug_glass));ug_ticks=0;ug_lock=0;ug_calls=ug_glyphs=0;UC(os88_main()==ug_win);ug_paint(0);}
static void ug_load_example(unsigned which){unsigned guard;ug_cmd(4,0);ug_key('0'+which,0);for(guard=0;bu_op&&guard<10;guard++)ug_wake();UC(bb_doc.np>0);}
static void ug_step(void){unsigned guard;ug_cmd(2,3);for(guard=0;(bu_op||bu_step_once)&&guard<10000;guard++)ug_wake();}
static void ug_example(unsigned which){ug_load_example(which);ug_step();UC(bb_valid&&bb_result.diag==0);}
static void ug_select_resistor(void){bu_selected_kind=BB_PART;bu_selected=bb_doc.part[2].id;bu_selected_pin=1;}
static void ug_value_open(void){ug_select_resistor();ug_key('e',0);UC(bu_mode==BU_VALUE);}
static void ug_value_replace(const char *s){unsigned i;for(i=0;i<30;i++)ug_key(8,0);for(i=0;s[i];i++)ug_key(s[i],0);}
static void ug_queue_checks(void){unsigned count,value;BBWord flags;char name[13];ug_init(480);ug_example(2);ug_value_open();value=bb_doc.part[2].value;count=bb_doc.np;ug_value_replace("20k");ug_key(13,0);UC(bu_op==BU_OP_PART);ug_key(27,1);ug_wake();UC(bb_doc.np==count&&bb_doc.part[2].value==value);
 ug_select_resistor();flags=bb_doc.part[2].flags;ug_key('r',0);UC(bu_op==BU_OP_PART);ug_key('r',0);ug_wake();UC(bb_doc.part[2].flags==(flags^BB_ROT));
 ug_select_resistor();flags=bb_doc.part[2].flags;ug_key('r',0);
 {int x=(bu_hx(bb_pin(&bb_doc.part[3],1))+bu_hx(bb_pin(&bb_doc.part[3],2)))/2,y=(bu_hy(bb_pin(&bb_doc.part[3],1))+bu_hy(bb_pin(&bb_doc.part[3],2)))/2;BBWord other=bb_doc.part[3].flags;
  ug_event_begin();if(ug_negative)ovl_bu_onclick(x,y,ug_win);else os88_onclick(x,y,ug_win);ug_event_end();
  if(ug_negative){ug_event_begin();ovl_bu_key('r',0,ug_win);ug_event_end();}else ug_key('r',0);
  ug_wake();UC(bb_doc.part[2].flags==(flags^BB_ROT)&&bb_doc.part[3].flags==other);}
 ug_select_resistor();ug_key('r',0);memcpy(name,bu_choice,13);ug_event_begin();os88_onfile(OS88_FDLG_OPEN,"OTHER.BBX",112,0,ug_win);ug_event_end();UC(!memcmp(name,bu_choice,13));ug_key(27,1);ug_wake();}
static void ug_modal_checks(void){unsigned cells;ug_init(200);ug_example(2);ug_value_open();if(ug_negative)bu_cache_ok=0;ug_calls=ug_glyphs=0;ug_key('1',0);cells=ug_glyphs;UC(cells<=10);printf("UI appended value char: %u glyph cells, %u drawing calls\n",cells,ug_calls);ug_key(27,1);ug_cmd(2,4);UC(bu_mode==BU_INSPECT);ug_calls=ug_glyphs=0;bu_flags|=BU_INFO_DIRTY;ug_wake();UC(ug_glyphs==0);printf("UI unchanged inspector: %u glyph cells, %u drawing calls\n",ug_glyphs,ug_calls);
 ug_event_begin();os88_about(ug_win);ug_event_end();memcpy(ug_before,ug_glass,sizeof(ug_before));ug_event_begin();os88_ontimer(ug_win);ug_event_end();ug_wake();UC(!memcmp(ug_before,ug_glass,sizeof(ug_before)));ug_damage.x1=64;ug_damage.y1=72;ug_damage.x2=95;ug_damage.y2=79;ug_event_begin();os88_set_color(0);os88_gfx_fill(64,72,95,79);ug_event_end();ug_paint(1);ug_wake();UC(!memcmp(ug_before,ug_glass,sizeof(ug_before)));ug_key(27,1);UC(bu_mode!=BU_ABOUT);}
static void ug_damage_checks(void){unsigned net,last;int compare;ug_init(480);ug_example(2);net=bb_hnet[bb_pin(&bb_doc.part[3],1)];bb_result.mv[net]=2345;last=(bb_scope.head+255)&255;bb_scope.mv[0][last]=2345;bu_flags|=BU_INFO_DIRTY|BU_SCOPE_DIRTY;ug_damage.x1=bu_right+2;ug_damage.y1=bu_y+26;ug_damage.x2=bu_right+31;ug_damage.y2=bu_y+31;ug_paint(1);ug_wake();memcpy(ug_before,ug_glass,sizeof(ug_before));ug_paint(0);compare=memcmp(ug_before,ug_glass,sizeof(ug_before));UC(compare==0);if(compare)fprintf(stderr,"partial damage/delta differs from full repaint\n");}
static void ug_wide_checks(int dismiss)
{
    char edit[24];unsigned output,last;int compare;
    ug_init(480);ug_example(8);
    bu_selected_kind=BB_PART;bu_selected=bb_doc.part[4].id;bu_selected_pin=1;
    bu_flags|=BU_ALL;ug_wake(); /* establish the selected board before editing */
    ug_key('e',0);UC(bu_mode==BU_VALUE&&!bu_compact);memcpy(edit,bu_edit,sizeof(edit));
    /* Inputs to the renderer represent a newly committed flasher LOW phase.
     * This fixture asserts UI painting only, not electrical calculation. */
    output=bb_hnet[bb_pin(&bb_doc.part[2],3)];bb_result.mv[output]=0;
    bb_result.ua[8]=0;bb_result.tlo=(BBWord)(bb_result.tlo+10000);
    last=(bb_scope.head+255)&255;bb_scope.mv[0][last]=0;bb_scope.mv[1][last]=3000;
    bu_result_changed();bu_flags|=BU_SCOPE_DIRTY|BU_BAR_DIRTY;
    if(ug_negative)bu_flags&=~(BU_BOARD_DIRTY|BU_SCOPE_DIRTY|BU_BAR_DIRTY);
    ug_wake();UC(bu_mode==BU_VALUE&&!memcmp(edit,bu_edit,sizeof(edit)));
    if(dismiss)ug_key(27,1);
    memcpy(ug_before,ug_glass,sizeof(ug_before));ug_paint(0);
    compare=memcmp(ug_before,ug_glass,sizeof(ug_before));UC(compare==0);
    if(compare){fprintf(stderr,"wide value %s discarded underlying LED/scope/bar damage\n",dismiss?"dismiss":"flush");ug_diff("wide value");}
}
static void ug_locate_checks(int kind)
{
    unsigned beforex,beforey,target;int compare;
    ug_init(kind==1?200:480);ug_example(2);target=bb_doc.part[2].id;
    if(kind==1){bb_doc.pany=6;bb_view_changed();bu_flags|=BU_ALL;ug_wake();ug_cmd(3,4);UC(bu_mode==BU_SCOPE);}
    else {if(kind==2){bb_doc.panx=29;bb_doc.pany=13;bb_view_changed();bu_flags|=BU_ALL;ug_wake();}ug_cmd(6,4);UC(bu_mode==BU_HELP);}
    bb_result.diag=BB_ENUMERIC;bb_result.part=kind==3?0:target;bb_result.net=BB_NNET;bb_valid=0;
    beforex=bb_doc.panx;beforey=bb_doc.pany;ug_textn=0;ug_textlog[0]=0;ug_toast[0]=0;
    ug_event_begin();os88_onclick(bu_x+8,bu_y+bu_h-5,ug_win);ug_event_end();
    if(kind==3){UC(bu_mode==BU_HELP&&(strstr(ug_textlog,"no physical location")!=0||strstr(ug_toast,"no location")!=0));return;}
    UC(bu_mode==BU_BOARD&&bu_selected_kind==BB_PART&&bu_selected==target);
    if(kind<2)UC(bb_doc.panx==beforex&&bb_doc.pany==beforey);
    else UC(bb_doc.panx!=beforex||bb_doc.pany!=beforey);
    memcpy(ug_before,ug_glass,sizeof(ug_before));ug_paint(0);compare=memcmp(ug_before,ug_glass,sizeof(ug_before));UC(compare==0);
    if(compare){fprintf(stderr,"Locate from non-board view left glass unlike full board repaint\n");ug_diff("Locate");}
}
static void ug_stale_checks(void)
{
    unsigned target,px,py;
    ug_init(480);ug_load_example(6);ug_step();UC(bb_result.diag==BB_ELEDLIMIT);
    target=bb_doc.part[2].id;bu_selected_kind=BB_PART;bu_selected=target;bu_selected_pin=1;
    ug_key('r',0);ug_wake();UC(bb_recompile&&!bb_valid&&bb_result.diag==BB_ELEDLIMIT);
    ug_textn=0;ug_textlog[0]=0;bu_flags|=BU_INFO_DIRTY;ug_wake();UC(strstr(ug_textlog,"Click error:")==0);
    /* A public standalone recompile must not revive a diagnosis from the old
     * source revision even though numeric net IDs have become reusable. */
    UC(bb_compile());if(ug_negative)bu_diag_stale=0;ug_textn=0;ug_textlog[0]=0;bu_cache_ok=0;bu_flags|=BU_INFO_DIRTY;ug_wake();
    UC(strstr(ug_textlog,"Click error:")==0);
    ug_cmd(6,4);bu_selected=0;bu_selected_kind=0;px=bb_doc.panx;py=bb_doc.pany;
    ug_event_begin();os88_onclick(bu_x+8,bu_y+bu_h-5,ug_win);ug_event_end();
    UC(bu_mode==BU_HELP&&bu_selected==0&&bb_doc.panx==px&&bb_doc.pany==py);
    /* Restore the faulty forward LED through the actual edit callback. A new
     * failed solve is current and must still locate its physical part. */
    ug_key(27,1);bu_selected_kind=BB_PART;bu_selected=target;bu_selected_pin=1;ug_key('r',0);ug_wake();ug_step();
    UC(bb_result.diag==BB_ELEDLIMIT);ug_cmd(6,4);
    ug_event_begin();os88_onclick(bu_x+8,bu_y+bu_h-5,ug_win);ug_event_end();
    UC(bu_mode==BU_BOARD&&bu_selected_kind==BB_PART&&bu_selected==target);
}
static void ug_scope_hold_checks(void)
{
    unsigned i;ug_init(480);ug_example(8);bb_scope.head=3;bb_scope.count=3;
    for(i=0;i<3;i++){bb_scope.lo[i]=i*1000;bb_scope.hi[i]=0;}
    bb_scope.mv[0][0]=0;bb_scope.mv[0][1]=5000;bb_scope.mv[0][2]=0;
    bb_scope.mv[1][0]=1000;bb_scope.mv[1][1]=3000;bb_scope.mv[1][2]=2000;
    bb_running=1;ug_diagonals=ug_scope_lines=0;ug_textn=0;ug_textlog[0]=0;
    ug_event_begin();bu_flags|=BU_SCOPE_DIRTY;bu_render(ug_win,0);ug_event_end();
    /* A negative arm restores an interpolated edge through the same shipped
     * drawing primitive; the axis/corner rule must reject it. */
    if(ug_negative)bb_scope_line(bu_scopebits,48,10,10,20,20,0);
    UC(ug_scope_lines>=24&&ug_diagonals==0);UC(strstr(ug_textlog,"RUN")!=0);
    ug_textn=0;ug_textlog[0]=0;ug_cmd(1,3);
    UC(!bb_running&&strstr(ug_textlog,"PAUSE")!=0);
}
static void ug_led_body(unsigned char *pixels)
{
    int x,y,cx,cy,n=0;BBPart *led=&bb_doc.part[3];
    cx=(bu_hx(bb_pin(led,1))+bu_hx(bb_pin(led,2)))/2;
    cy=(bu_hy(bb_pin(led,1))+bu_hy(bb_pin(led,2)))/2;
    for(y=cy-2;y<=cy+2;y++)for(x=cx-2;x<=cx+2;x++)pixels[n++]=ug_glass[y*720+x];
}
static void ug_replacement_checks(void)
{
    unsigned i,guard,mode;unsigned char baseline[25],actual[25];
    ug_init(480);ug_example(1);ug_led_body(baseline);
    for(i=0;i<25;i++)UC(baseline[i]==OS88_LRED);
    ug_filesize=bb_encode(&bb_doc,ug_filebits,sizeof(ug_filebits));UC(ug_filesize>0);bb_mark_saved();
    for(mode=0;mode<2;mode++){
        if(mode==0){
            ug_cmd(0,0);for(guard=0;bu_op&&guard<20;guard++)ug_wake();UC(bb_doc.np==0);
            ug_event_begin();os88_onfile(OS88_FDLG_OPEN,"CACHE.BBX",ug_filesize,0,ug_win);ug_event_end();
            for(guard=0;bu_op&&guard<20;guard++)ug_wake();UC(bb_doc.np==4&&!bb_valid);
        }else{bb_mark_saved();ug_load_example(1);UC(bb_doc.np==4&&!bb_valid);}
        ug_paint(0);ug_led_body(actual);UC(actual[12]==OS88_WHITE);
        /* Restore the stale ON key after replacement to reproduce the old
         * cache bug while leaving the actual invalid full paint intact. */
        if(ug_negative)bu_led_shadow[3]=3;
        ug_step();UC(bb_valid&&bb_result.ua[3]==8824);
        ug_led_body(actual);UC(!memcmp(baseline,actual,sizeof(actual)));
        ug_paint(0);ug_led_body(actual);UC(!memcmp(baseline,actual,sizeof(actual)));
    }
    /* A genuine failed numerical slice invalidates a formerly powered result.
     * Its incremental body must agree with a clean paint of that failed state. */
    vx_num_calls=0;vx_inject=20;ug_step();vx_inject=0;
    UC(!bb_valid&&bb_result.diag==BB_ENUMERIC);ug_led_body(actual);UC(actual[12]==OS88_WHITE);
    ug_paint(0);ug_led_body(actual);UC(actual[12]==OS88_WHITE);
}
int main(int argc,char **argv){if(argc!=2&&argc!=3)return 2;ug_negative=argc==3&&!strcmp(argv[2],"negative");if(!strcmp(argv[1],"queue"))ug_queue_checks();else if(!strcmp(argv[1],"modal"))ug_modal_checks();else if(!strcmp(argv[1],"damage"))ug_damage_checks();else if(!strcmp(argv[1],"wide"))ug_wide_checks(0);else if(!strcmp(argv[1],"wide-dismiss"))ug_wide_checks(1);else if(!strcmp(argv[1],"locate-help"))ug_locate_checks(0);else if(!strcmp(argv[1],"locate-scope"))ug_locate_checks(1);else if(!strcmp(argv[1],"locate-offscreen"))ug_locate_checks(2);else if(!strcmp(argv[1],"locate-none"))ug_locate_checks(3);else if(!strcmp(argv[1],"stale"))ug_stale_checks();else if(!strcmp(argv[1],"scope-hold"))ug_scope_hold_checks();else if(!strcmp(argv[1],"replace-led"))ug_replacement_checks();else return 2;printf("breadxt UI host %s: %u checks, %u failures\n",argv[1],ug_checks,ug_failures);return ug_failures?1:0;}
