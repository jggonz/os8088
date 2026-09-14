/* Independent host assertions for the native parser and VM. The target's
 * packed doubles are backed by host doubles here; QEMU tests the assembly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../sbasic.h"
static unsigned char heap[1048576];
static unsigned alloc_seg[64],alloc_kb[64];
static unsigned clock_ticks;
static char output[8192];
static int output_len,pixels,lines;
static int sbs_lw=320,sbs_lh=200,sbs_gseg=0x800,sbs_bios_mode=3;
unsigned os88_mem_claim(int kb)
{
    unsigned s,end;int i,slot,again;
    slot=-1;for(i=0;i<64;++i)if(!alloc_kb[i]){slot=i;break;}
    if(slot<0||kb<1)return 0;
    s=0x1000;
    do{again=0;end=s+kb*64;if(end>0xe000)return 0;
        for(i=0;i<64;++i)if(alloc_kb[i]&&s<alloc_seg[i]+alloc_kb[i]*64&&end>alloc_seg[i]){s=alloc_seg[i]+alloc_kb[i]*64;again=1;break;}
    }while(again);
    alloc_seg[slot]=s;alloc_kb[slot]=kb;return s;
}
int os88_mem_free(unsigned seg){int i;for(i=0;i<64;++i)if(alloc_kb[i]&&alloc_seg[i]==seg){alloc_kb[i]=0;return 0;}return -1;}
int os88_peek(unsigned seg,unsigned off){return heap[((seg&65535)*16+(off&65535))&1048575];}
void os88_poke(unsigned seg,unsigned off,int value){heap[((seg&65535)*16+(off&65535))&1048575]=value;}
void sb_memzero(unsigned seg,unsigned n){memset(heap+seg*16,0,n);}
void sbf_init(void){}
void sbf_integer(void *p,int value){double n=(short)value;memcpy(p,&n,8);}
void sbf_parse(void *p,const char *s){double n=strtod(s,0);memcpy(p,&n,8);}
void sbf_format(void *p,char *s){double n;memcpy(&n,p,8);sprintf(s,"%.9g",n);}
int sbf_word(void *p){double n;memcpy(&n,p,8);return (short)(unsigned short)(long long)n;}
int sbf_op(int op,void *p,void *q)
{
    double a,b;memcpy(&a,p,8);memcpy(&b,q,8);
    if(op==5)return a<b?-1:a>b?1:0;
    if(op==1)a+=b;else if(op==2)a-=b;else if(op==3)a*=b;else if(op==4)a/=b;
    else if(op==6)a=floor(a);else if(op==7)a=trunc(a);else if(op==8)a=sqrt(a);
    memcpy(p,&a,8);return 0;
}
static int sbs_scale_x(int x){return x<0||x>=sbs_lw?-1:sbs_lw==640?x/2:x;}
static int sbs_scale_y(int y){return y<0||y>=sbs_lh?-1:sbs_lh==350?y*4/7:y;}
static void h_mode(int m,int w,int h){(void)m;sbs_lw=w;sbs_lh=h;}
static void h_clear(int c){memset(heap+sbs_gseg*16,(c&15)*17,32000);output_len=0;output[0]=0;}
static void h_put(int r,int c,int ch,int fg,int bg){(void)r;(void)c;(void)fg;(void)bg;if(output_len<8191)output[output_len++]=(char)ch;output[output_len]=0;}
static void h_cursor(int r,int c){(void)r;(void)c;}
static void h_pixel(int x,int y,int color)
{
    unsigned p;int b;x=sbs_scale_x(x);y=sbs_scale_y(y);if(x<0||y<0)return;++pixels;
    p=y*160+x/2;b=os88_peek(sbs_gseg,p);os88_poke(sbs_gseg,p,x&1?(b&240)|(color&15):(b&15)|((color&15)<<4));
}
static void h_line(int x,int y,int xx,int yy,int c)
{
    int dx=abs(xx-x),dy=abs(yy-y),sx=x<xx?1:-1,sy=y<yy?1:-1,e=dx-dy,t;++lines;
    for(;;){h_pixel(x,y,c);if(x==xx&&y==yy)break;t=e*2;if(t>-dy){e-=dy;x+=sx;}if(t<dx){e+=dx;y+=sy;}}
}
static void h_sound(int hz,int ticks){(void)hz;(void)ticks;}
static unsigned h_ticks(void){return clock_ticks;}
static void h_changed(void){}
static int sbs_mem_read(unsigned seg,unsigned off){return os88_peek(seg,off);}
static void sbs_mem_write(unsigned seg,unsigned off,int v){os88_poke(seg,off,v);}
static void sbs_out(int port,int value){(void)port;(void)value;}
static void sbs_video_mode(int mode){sbs_bios_mode=mode;h_mode(mode!=3,mode==9?640:320,mode==9?350:200);h_clear(0);}
#include "../sbcore.c"
static struct sb_host host={h_mode,h_clear,h_put,h_cursor,h_pixel,h_line,h_sound,h_ticks,h_changed};
static int failures;
static void check(const char *name,const char *source,const char *want,int expected)
{
    int guard=0;sb_load(source,(unsigned)strlen(source));
    while(sb_state()!=SB_STATE_DONE&&sb_state()!=SB_STATE_ERROR&&guard++<10000){++clock_ticks;sb_run_slice(32);if(sb_state()==SB_STATE_WAITING&&!sbc_wait_time)break;}
    if(sb_state()!=expected||(want&&!strstr(output,want))){fprintf(stderr,"FAIL %s: state %d line %d error=%s output=%s\n",name,sb_state(),sb_line(),sb_error(),output);++failures;}
}
int main(int argc,char **argv)
{
    sb_init(&host);
    if(argc>1){static char source[32768];FILE *f=fopen(argv[1],"rb");int n;if(!f)return 2;n=fread(source,1,sizeof(source)-1,f);fclose(f);source[n]=0;check(argv[1],source,0,SB_STATE_DONE);return failures!=0;}
    check("fractional arithmetic","CLS\nPRINT 2.5*4;\"/\";INT(-1.2);\"/\";65536/2\n","10/-2/ 32768",4);
    check("negative fractional floor","IF INT(-.5)<>-1 THEN bad=1/0\nIF INT(.5)<>0 THEN bad=1/0\nIF INT(-2)<>-2 THEN bad=1/0\n",0,4);
    check("arrays/data","DIM a(2,2)\nFOR i=0 TO 2: READ a(i,1): NEXT\nDATA 7,11,13\nPRINT a(0,1)+a(1,1)+a(2,1)\n","31",4);
    check("branches","x=0\nIF 0 THEN x=99: x=88 ELSE x=1\nIF x=0 THEN\nx=99\nELSEIF x=1 THEN\nx=2\nELSE\nx=88\nEND IF\nPRINT x\n","2",4);
    check("loops","x=0\nFOR i=3 TO 1: x=99: NEXT\nDO\nx=x+1\nLOOP UNTIL x=4\nWHILE x<7\nx=x+1\nWEND\nPRINT x\n","7",4);
    check("call by reference","x%=4\nCALL Add(x%)\nPRINT x%\nEND\nSUB Add(n%)\nn%=n%+3\nEND SUB\n","7",4);
    check("call by value","x%=4\nCALL Add((x%))\nPRINT x%\nEND\nSUB Add(n%)\nn%=n%+3\nEND SUB\n","4",4);
    check("functions","DEF FNDouble(x)=x*2\nPRINT FNDouble(2.5)\n","5",4);
    check("strings/select","s$=UCASE$(MID$(\"abcdef\",2,3))\nSELECT CASE s$\nCASE \"BAD\": PRINT \"FAIL\"\nCASE \"BCD\": PRINT INSTR(s$,\"C\")\nCASE ELSE: PRINT \"FAIL\"\nEND SELECT\n","2",4);
    check("input wait","LINE INPUT s$\nPRINT s$\n",0,3);
    sb_key('o',0);sb_key('k',0);sb_key(13,0);sb_run_slice(32);
    if(sb_state()!=4||!strstr(output,"ok")){fprintf(stderr,"FAIL input resume\n");++failures;}
    pixels=lines=0;
    check("graphics","SCREEN 7\nLINE (2,3)-(8,7),5,BF\nPSET (12,13),9\n",0,4);
    if(lines!=5||sbg_get(5,5)!=5||sbg_get(12,13)!=9){fprintf(stderr,"FAIL graphics pixels\n");++failures;}
    check("image restore","SCREEN 7\nDIM image%(100)\nLINE (2,3)-(8,7),5,BF\nGET (2,3)-(8,7),image%\nCLS\nPUT (12,13),image%,PSET\n",0,4);
    if(sbg_get(15,15)!=5){fprintf(stderr,"FAIL GET/PUT pixels\n");++failures;}
    check("bounds","DIM a(2)\na(3)=7\n",0,5);
    check("reset after error","PRINT \"RESET OK\"\n","RESET OK",4);
    check("integer overflow promotion","DEFINT A-Z\nPRINT 30000+10000;\"/\";-30000-10000\n","40000/-40000",4);
    check("atan reduction","IF ABS(ATN(-2)+1.1071487178)>.000001 THEN a=1/0\nIF ABS(ATN(1)-.7853981634)>.000001 THEN a=1/0\n",0,4);
    check("print using","PRINT USING \"## \"; 3;\n"," 3 ",4);
    check("scaled flood fill","SCREEN 9\nLINE (10,10)-(90,90),4,B\nPAINT (40,40),5,4\n",0,4);
    if(sbg_get(40,80)!=5||sbg_get(4,4)!=0){fprintf(stderr,"FAIL scaled PAINT pixels\n");++failures;}
    if(!failures)puts("speedybasic core: 19 checks passed");
    return failures!=0;
}
