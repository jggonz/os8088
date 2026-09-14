/* Compile the exact shipping core as one host translation unit. */
#define SB_HOST_TEST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sbasic.h"

static const unsigned char *far_src; static unsigned far_len;
struct obs {int mode,w,h,modes,clears,clearc,pixel,pixelc,line,linec,point,circle,paint,get,put,peek,poke,ports,pal,sound;unsigned seg,off,ticks;} o;
int os88_peek(unsigned s,unsigned p){return s==0x2222u&&p<far_len?far_src[p]:0;}
static void mode(int m,int w,int h){o.mode=m;o.w=w;o.h=h;o.modes++;}
static void clear(int c){o.clearc=c;o.clears++;}
static void text(int r,int c,int ch,int f,int b){(void)r;(void)c;(void)ch;(void)f;(void)b;}
static void cursor(int r,int c){(void)r;(void)c;} static void scroll(void){}
static void pixel(int x,int y,int c){(void)x;(void)y;o.pixelc=c;o.pixel++;}
static void line(int a,int b,int c,int d,int e){(void)a;(void)b;(void)c;(void)d;o.linec=e;o.line++;}
static int point(int x,int y){(void)x;(void)y;o.point++;return 23;}
static void circle(int x,int y,int r,int c){(void)x;(void)y;(void)r;(void)c;o.circle++;}
static void paint(int x,int y,int c,int b){(void)x;(void)y;(void)c;(void)b;o.paint++;}
static unsigned gget(int a,int b,int c,int d,unsigned s,unsigned p){(void)a;(void)b;(void)c;(void)d;o.seg=s;o.off=p;o.get++;return 37;}
static void gput(int x,int y,unsigned s,unsigned p,int op){(void)x;(void)y;(void)op;o.seg=s;o.off=p;o.put++;}
static int vpeek(unsigned s,unsigned p){o.seg=s;o.off=p;o.peek++;return 77;}
static void vpoke(unsigned s,unsigned p,int v){(void)v;o.seg=s;o.off=p;o.poke++;}
static void pout(unsigned p,int v){(void)p;(void)v;o.ports++;}
static void palette(int i,int r,int g,int b){(void)i;(void)r;(void)g;(void)b;o.pal++;}
static void sound(int h,int t){(void)h;(void)t;o.sound++;} static unsigned ticks(void){return ++o.ticks;}
static void changed(void){}
#include "sbmem_host.c"
#include "sbnum_ref.c"
#include "../sbcore.c"

static struct sb_host host={mode,clear,text,cursor,scroll,pixel,line,point,circle,paint,gget,gput,vpeek,vpoke,pout,palette,sound,ticks,changed};
static int checks,fails;
static void ck(int x,const char *s){checks++;if(!x){fails++;fprintf(stderr,"FAIL: %s\n",s);}}
static int nval(const char *n){int s=sbc_find_symbol(n,0);ck(s>=0,n);return s<0?0:sbc_symbols[s].value;}
static const char *sval(const char *n){int s=sbc_find_symbol(n,0),p=-1;ck(s>=0,n);if(s>=0)p=sbc_symbols[s].strslot;ck(p>=0,"string slot");return p<0?"":sbc_strings[p];}

static void run(const char *name,const char *src,const char *keys)
{
 int i,k=0,state; memset(&o,0,sizeof(o)); ck(sb_init(&host)==0,"init"); ck(sb_load(src,(unsigned)strlen(src))==0,name);
 if(keys)while(keys[k])sb_key((unsigned char)keys[k++],0);
 for(i=0;i<20000;i++){state=sb_run_slice(32);if(state==SB_STATE_DONE||state==SB_STATE_ERROR||state==SB_STATE_STOPPED)break;if(state==SB_STATE_WAITING)sb_key(keys&&keys[k]?(unsigned char)keys[k++]:13,0);}
 if(sb_state()!=SB_STATE_DONE){fails++;fprintf(stderr,"FAIL: %s state=%d line=%d error=%s\n",name,sb_state(),sb_line(),sb_error());}
 checks++;if(sb_hosttest_ignored()){fails++;fprintf(stderr,"FAIL: %s ignored=%d line=%d\n",name,sb_hosttest_ignored(),sb_line());}
}

static void test_language(void)
{
 run("tokenizer/expression","A=2+3*4\nB=2^3^2\nC=&H10+10 MOD 4\nD=(5>=5) AND (2<>3)\nE=NOT 0\nF=17\\5\nG=18\\\\5\nEND\n",0);
 ck(nval("A")==14,"precedence");ck(nval("B")==512,"power");ck(nval("C")==18,"hex/MOD");ck(nval("D")&&nval("E"),"boolean");ck(nval("F")==3&&nval("G")==3,"integer division and vendored slash encoding");
 run("demo numeric model","A=CINT(.5*10)\nB=CINT(1.25E2)\nL&=40000\nM&=-50000\nN&=L&+10000\nI%=CINT(2.5)\nJ%=CINT(3.5)\nK=INT(-1.2)\nS=CINT(SIN(0)*1000)\nC=CINT(COS(0)*1000)\nQ=CINT(SQR(81))\nU%=3.6\nF!=1.5\nG&=40000\nH=CINT(F!*2+G&/10000)\nCMP=(F!<2) AND (G&>32767)\nEND\n",0);
 ck(nval("A")==5&&nval("B")==125,"fraction/exponent literals");ck(nval("L&")==40000&&nval("M&")==-50000&&nval("N&")==50000,"signed LONG arithmetic");ck(nval("I%")==2&&nval("J%")==4&&nval("U%")==4,"integer suffix/coercion");ck(nval("K")==-2,"INT floors negatives");ck(nval("S")==0&&nval("C")==1000&&nval("Q")==9,"SIN/COS/SQR/CINT");ck(nval("H")==7&&nval("CMP")!=0,"mixed fixed/long and comparisons");
 run("arrays/data","DIM A(2),B(1,1)\nDATA 5,6,7,8\nREAD A(0),A(1),A(2),B(1,1)\nRESTORE\nREAD R\nX=A(0)+A(1)+A(2)+B(1,1)+R\nEND\n",0);
 ck(nval("X")==31,"arrays/data/restore");
 run("control flow","X=0\nFOR I=1 TO 3\nX=X+I\nNEXT I\nFOR I=3 TO 1 STEP -1\nX=X+I\nNEXT I\nW=0\nWHILE W<2\nW=W+1\nWEND\nD=0\nDO\nD=D+1\nLOOP UNTIL D=2\nIF X=12 THEN\nQ=1\nELSEIF X=13 THEN\nQ=2\nELSE\nQ=3\nEND IF\nIF Q=1 THEN S=4 ELSE S=9\nSELECT CASE S\nCASE 1 TO 3\nT=1\nCASE 4,5\nT=2\nCASE ELSE\nT=3\nEND SELECT\nGOSUB INC\nGOTO FINISH\nINC:\nT=T+1\nRETURN\nFINISH:\nEND\n",0);
 ck(nval("X")==12,"for/next");ck(nval("W")==2&&nval("D")==2,"while/do");ck(nval("Q")==1&&nval("S")==4,"if forms");ck(nval("T")==3,"select/gosub/goto");
 run("GOSUB control unwind","FOR I=1 TO 70\nGOSUB EARLY\nNEXT I\nEND\nEARLY:\nIF 1 THEN\nRETURN\nEND IF\n",0);
 ck(nval("I")==71,"RETURN unwinds subroutine control frames");
 run("GOTO control unwind","FOR I=1 TO 70\nIF 1 THEN\nGOTO AFTER\nEND IF\nAFTER:\nNEXT I\nEND\n",0);
 ck(nval("I")==71,"GOTO reconciles IF frames");
 run("SUB/CALL scopes","A=99\nG=1\nCALL ADDER(2,3)\nCALL OUTER(4)\nEND\nSUB ADDER(A,B)\nLOCAL T\nT=A+B\nG=G+T\nEND SUB\nSUB OUTER(N)\nLOCAL A\nA=N*2\nCALL ADDER(A,1)\nEND SUB\n",0);
 ck(nval("A")==99,"local scope");ck(nval("G")==15,"call parameters/nesting");
 run("strings","A$=\"Abc\"\nB$=LEFT$(A$,2)+RIGHT$(A$,1)\nC$=MID$(A$,2,1)\nD$=CHR$(65)+SPACE$(2)+UCASE$(\"z\")\nN=LEN(B$)+ASC(C$)+VAL(\"12\")+INSTR(A$,\"bc\")\nEND\n",0);
 ck(!strcmp(sval("B$"),"Abc"),"left/right");ck(!strcmp(sval("C$"),"b"),"mid");ck(!strcmp(sval("D$"),"A  Z"),"string builtins");ck(nval("N")==115,"numeric string builtins");
 run("input","INPUT \"NUMBER\";N\nLINE INPUT S$\nK$=INKEY$\nEND\n","42\rhello world\rZ");
 ck(nval("N")==42,"input");ck(!strcmp(sval("S$"),"hello world"),"line input");ck(!strcmp(sval("K$"),"Z"),"inkey");
}

static void test_hooks(void)
{
 run("graphics hooks","SCREEN 13\nCLS 2\nPSET (3,4),5\nLINE (1,2)-(7,8),9\nLINE (1,1)-(3,3),6,B\nLINE (4,4)-(6,5),4,BF\nCIRCLE (10,11),4,12\nPAINT (13,14),2,3\nP=POINT(5,6)\nGET (1,2)-(3,4),BUF\nPUT (8,9),BUF,XOR\nEND\n",0);
 ck(o.modes&&o.mode==13&&o.w==320&&o.h==200,"screen hook");ck(o.clears&&o.clearc==2,"clear hook");ck(o.pixel&&o.pixelc==5&&o.line==7&&o.linec==4&&o.circle&&o.paint,"draw hooks and explicit colors");ck(o.point&&nval("P")==23,"point hook");ck(o.get&&o.put,"get/put hooks");
 run("low-level hooks","DEF SEG=&HA000\nPOKE 12,34\nV=PEEK(12)\nOUT &H3C8,4\nOUT &H3C9,1\nOUT &H3C9,2\nOUT &H3C9,3\nREG 1,&H13\nCALL INTERRUPT &H10\nSOUND 440,7\nBEEP\nEND\n",0);
 ck(o.poke&&o.peek&&nval("V")==77,"peek/poke hooks");ck(o.ports==4&&o.pal==1,"port/palette hooks");ck(o.modes&&o.mode==13,"interrupt hook");ck(o.sound==2,"sound hooks");
}

static void test_web_surface(void)
{
 run("mutation/type/exit","A=1\nB=2\nSWAP A,B\nINCR A,3\nDECR B\nDIM X(3)\nREDIM X(5)\nERASE X\nOPTION BASE 1\nDEFINT I-N\nDEFLNG A-C\nDEFSNG D-F\nDEFDBL G-H\nDEFSTR S\nFOR I=1 TO 3\nEXIT FOR\nNEXT I\nDO\nEXIT DO\nLOOP\nEND\n",0);
 run("ON branch and DEF FN","X=2\nON X GOTO L1,L2\nL1:\nA=1\nGOTO FIN\nL2:\nON 1 GOSUB S1,S2\nA=FNSQ(7)\nGOTO FIN\nS1:\nRETURN\nS2:\nRETURN\nDEF FNSQ(N)=N*N\nFIN:\nEND\n",0);ck(nval("A")==49,"ON/DEF FN");
 run("text/graphics surface","CLS\nCOLOR 7,1\nLOCATE 2,3\nWIDTH 80\nPRINT USING \"###\";12\nWRITE \"A\",2\nSCREEN 13\nVIEW (0,0)-(20,20),1,2\nWINDOW (0,0)-(100,100)\nPALETTE 1,4\nPRESET (1,2),3\nDRAW \"R4D4L4U4\"\nEND\n",0);
 run("timing/hardware surface","RANDOMIZE 7\nPLAY \"T120 O3 CDE\"\nDELAY .01\nWAIT &H3DA,8\nDEF SEG=&H1000\nMEMSET 0,16,7\nPEN ON\nSTRIG ON\nBLOAD \"X.BIN\",0\nBSAVE \"Y.BIN\",0,16\nIOCTL \"KBD\",\"X\"\nCALL ABSOLUTE(0)\nEND\n",0);
 run("key/error/runtime surface","KEY 1,\"X\"\nKEY(1) ON\nON KEY(1) GOSUB HIT\nON ERROR GOTO EH\nERROR 5\nRESUME NEXT\nCOMMON A\nRESET\nENVIRON \"A=B\"\nEND\nHIT:\nRETURN\nEH:\nRESUME NEXT\n",0);
 run("file/directory surface","OPEN \"T.DAT\" FOR OUTPUT AS #1\nPRINT #1,\"A\"\nWRITE #1,2\nCLOSE #1\nOPEN \"T.DAT\" FOR INPUT AS #1\nINPUT #1,A$\nLINE INPUT #1,B$\nSEEK #1,1\nCLOSE #1\nFILES \"*.DAT\"\nNAME \"T.DAT\" AS \"U.DAT\"\nKILL \"U.DAT\"\nMKDIR \"D\"\nCHDIR \"D\"\nCHDIR \"..\"\nRMDIR \"D\"\nEND\n",0);
 run("meta directives","$DYNAMIC\nDIM A(4)\n$STATIC\n$INLINE &H90\nEND\n",0);
 run("numeric builtin surface","A=FIX(-1.8)\nB=CEIL(1.2)\nC=CLNG(4.5)\nD=CINT(CSNG(1.25)*100)\nE=CINT(CDBL(2.5)*10)\nF=CINT(TAN(0)*100)\nG=CINT(ATN(1)*1000)\nH=CINT(EXP(0))\nI=CINT(EXP2(3))\nJ=CINT(EXP10(2))\nK=CINT(LOG(EXP(1))*100)\nL=CINT(LOG2(8))\nM=CINT(LOG10(100))\nEND\n",0);
 ck(nval("A")==-1&&nval("B")==2&&nval("C")==4,"FIX/CEIL/CLNG");ck(nval("D")==125&&nval("E")==25,"CSNG/CDBL");ck(nval("F")==0&&nval("H")==1&&nval("I")==8&&nval("J")==100,"trig/exp");ck(nval("G")>=784&&nval("G")<=786&&nval("K")==100&&nval("L")==3&&nval("M")==2,"atan/log");
 run("string builtin surface","A$=LCASE$(\"AB\")\nB$=STRING$(3,65)\nC$=HEX$(255)+OCT$(8)+BIN$(5)\nD$=MKI$(123)+MKL$(40000)+MKS$(1.5)+MKD$(2.5)\nEND\n",0);
 ck(!strcmp(sval("A$"),"ab")&&!strcmp(sval("B$"),"AAA"),"LCASE/STRING");ck(!strcmp(sval("C$"),"FF105"),"base strings");ck(strlen(sval("D$"))>0,"packing strings");
}

static unsigned char *readall(const char *p,unsigned *len)
{
 FILE *f=fopen(p,"rb");long n;unsigned char *s;if(!f)return 0;if(fseek(f,0,SEEK_END)||(n=ftell(f))<0||n>(long)SB_SOURCE_MAX||fseek(f,0,SEEK_SET)){fclose(f);return 0;}s=malloc((size_t)n+1);if(!s){fclose(f);return 0;}if(fread(s,1,(size_t)n,f)!=(size_t)n){free(s);fclose(f);return 0;}fclose(f);s[n]=0;*len=(unsigned)n;return s;
}
static void test_demos(const char *manifest,const char *dir)
{
 FILE *f=fopen(manifest,"r");char name[64],path[1024];unsigned char *s;unsigned len;int count=0,state,i;ck(f!=0,"manifest");if(!f)return;
 while(fgets(name,sizeof(name),f)){name[strcspn(name,"\r\n")]=0;if(!name[0]||name[0]=='#')continue;snprintf(path,sizeof(path),"%s/%s",dir,name);s=readall(path,&len);ck(s!=0,path);if(!s)continue;far_src=s;far_len=len;memset(&o,0,sizeof(o));sb_init(&host);ck(sb_load_seg(0x2222u,len)==0,name);
  for(i=0;i<2000;i++){state=sb_run_slice(64);if(state==SB_STATE_ERROR||state==SB_STATE_DONE||state==SB_STATE_STOPPED)break;if(state==SB_STATE_WAITING)sb_key(13,0);else if((i&31)==31)sb_key(27,0);}
  if(sb_state()==SB_STATE_ERROR){unsigned p=sb_hosttest_pos(),e=p;while(e<len&&s[e]!='\r'&&s[e]!='\n'&&e-p<80)e++;fails++;fprintf(stderr,"FAIL: %s line=%d pos=%u error=%s near=%.*s\n",name,sb_line(),p,sb_error(),(int)(e-p),s+p);}checks++;if(sb_hosttest_ignored()){fails++;fprintf(stderr,"FAIL: %s ignored=%d line=%d pos=%u\n",name,sb_hosttest_ignored(),sb_line(),sb_hosttest_pos());}
  if(!strcmp(name,"BOING.BAS")){ck(o.get>=3,"BOING reaches image capture");ck(o.put>=2,"BOING reaches masked sprite output");}
  if(!strcmp(name,"STARS3D.BAS")){ck(o.modes>=2,"STARS3D enters and leaves VGA mode");ck(o.ports>=49,"STARS3D programs DAC");ck(o.poke>0,"STARS3D reaches framebuffer POKE");}
  free(s);count++;}
 fclose(f);ck(count==29,"all 29 demos loaded and stepped");
}
int main(int ac,char **av){if(ac!=3)return 2;test_language();test_hooks();test_web_surface();test_demos(av[1],av[2]);if(fails){fprintf(stderr,"speedybasic core: %d/%d failed\n",fails,checks);return 1;}printf("speedybasic core: %d checks, 29 demos\n",checks);return 0;}
