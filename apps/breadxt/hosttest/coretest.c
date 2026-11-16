/* Independent host verification of Breadboard XT's SHIPPING portable core.
 * The native-double adapter tests the C model/stamps/state machine, not the
 * 8088 assembly arithmetic. BBNUM independently tests the actual target ABI.
 * Fixtures state logical nets first and assign physical holes independently.
 */
#define BB_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "../bb.h"
static unsigned vx_checks,vx_failures,vx_num_calls,vx_inject,vx_slice_max;
static const char *vx_negative;
static int vx_negative_hit;
static double vx_get(const BBNum *n){double v;memcpy(&v,n,sizeof(v));return v;}
static int vx_finite(double v){return isfinite(v)&&(v==0||(fabs(v)>=0x1p-64&&fabs(v)<0x1p40));}
static void vx_put(BBNum *n,double v){memcpy(n,&v,sizeof(v));}
static int vx_fault(void){vx_num_calls++;if(vx_inject&&vx_num_calls==vx_inject){bb_num_error=BB_ENUMERIC;return 1;}return 0;}
void bb_copy(void *to,const void *from,unsigned n){memmove(to,from,n);}
void bb_zero(void *to,unsigned n){memset(to,0,n);}
void bb_nset(BBNum *n,int v){if(vx_fault())return;if(v < -32768||v>32767){bb_num_error=BB_ENUMERIC;return;}vx_put(n,v);}
void bb_nuint(BBNum *n,unsigned v){if(vx_fault())return;if(v>65535){bb_num_error=BB_ENUMERIC;return;}vx_put(n,v);}
int bb_nop(BBNum *n,const BBNum *a,const BBNum *b,unsigned op){double x=vx_get(a),y=vx_get(b),z;if(vx_fault())return 0;if(!vx_finite(x)||!vx_finite(y))goto bad;switch(op){case BB_NADD:z=x+y;break;case BB_NSUB:z=x-y;break;case BB_NMUL:z=x*y;break;case BB_NDIV:if(!y)goto bad;z=x/y;break;default:goto bad;}if(!vx_finite(z))goto bad;vx_put(n,z);return 1;bad:bb_num_error=BB_ENUMERIC;return 0;}
int bb_ncmp(const BBNum *a,const BBNum *b){double x=vx_get(a),y=vx_get(b);if(vx_fault())return 0;if(!vx_finite(x)||!vx_finite(y)){bb_num_error=BB_ENUMERIC;return 0;}return (x>y)-(x<y);}
int bb_nround(const BBNum *n){double v=round(vx_get(n));if(vx_fault())return 0;if(!vx_finite(v)||v < -32768||v>32767){bb_num_error=BB_ENUMERIC;return 0;}return (int)v;}
unsigned bb_umuldiv(unsigned a,unsigned b,unsigned d){uint32_t q;if(!d){bb_num_error=BB_ENUMERIC;return 0;}q=(uint32_t)a*b/d;if(q>65535){bb_num_error=BB_ENUMERIC;return 0;}return q;}
#include "../breadxt.c"

static void vx_check(int yes,const char *family,const char *what,int line){vx_checks++;if(vx_negative&&!vx_negative_hit&&!strcmp(family,vx_negative)){yes=!yes;vx_negative_hit=1;}if(!yes){vx_failures++;fprintf(stderr,"FAIL %s:%d: %s (diag=%u id=%u net=%u)\n",family,line,what,bb_error,bb_error_part,bb_error_net);}}
#define CK(f,e) vx_check(!!(e),f,#e,__LINE__)
static BBWord vx_h(unsigned row,unsigned col){return (BBWord)(row*30+col);}
static unsigned vx_physical_strip(unsigned h){unsigned r=h/30,c=h%30;if(r>=2&&r<=6)return c;if(r>=7&&r<=11)return c+30;if(r<2)return 60+2*r+(c>=15);return 64+2*(r-12)+(c>=15);}
static void vx_fresh(void){vx_inject=0;bb_clear(&bb_doc);bb_history_clear();bb_sim_reset();bb_mark_saved();bb_recompile=1;bb_error=0;}
static unsigned vx_part(unsigned type,unsigned a,unsigned b,unsigned c,unsigned value,unsigned param,unsigned flags){BBPart *p;unsigned i=bb_doc.np++;p=&bb_doc.part[i];memset(p,0,sizeof(*p));p->id=bb_doc.next_id++;p->type=type;p->pin[0]=a;p->pin[1]=b;p->pin[2]=c;p->value=value;p->param=param;p->flags=flags;return i;}
static unsigned vx_wire(unsigned a,unsigned b){BBWire *w;unsigned i=bb_doc.nw++;w=&bb_doc.wire[i];memset(w,0,sizeof(*w));w->id=bb_doc.next_id++;w->a=a;w->b=b;w->color=2;if(a/30!=b/30&&a%30!=b%30){w->nbend=1;w->bend[0]=b%30;w->bend[1]=a/30;}return i;}
static unsigned vx_net(unsigned part,unsigned pin){BBWord h=bb_pin(&bb_doc.part[part],pin);return h<BB_HOLES?bb_hnet[h]:BB_NNET;}
static int vx_mv(unsigned part,unsigned pin){unsigned n=vx_net(part,pin);return n<BB_NETS?bb_result.mv[n]:BB_MV_FLOAT;}
static int vx_step(void){unsigned guard=0,before,count;int r;do{before=vx_num_calls;r=bb_sim_slice(16);count=vx_num_calls-before;if(count>vx_slice_max)vx_slice_max=count;if(count>16){fprintf(stderr,"FAIL slice exceeded16 numeric operations: %u\n",count);vx_failures++;return -2;}if(++guard>100000){fprintf(stderr,"FAIL solver failed to terminate\n");vx_failures++;return -2;}}while(r==0);return r;}
static int vx_start(void){int r=bb_sim_start(0);if(!r)return -1;return vx_step();}
static int vx_example(unsigned n){vx_fresh();if(!bb_example(n))return 0;memcpy(&bb_doc,&bb_stage,sizeof(bb_doc));bb_recompile=1;return bb_validate(&bb_doc);}
static int vx_run(unsigned steps){unsigned i;for(i=0;i<steps;i++)if(vx_step()!=1)return 0;return 1;}

static void vx_topology(void){unsigned h,k,r,c,s;BBPart p;BBDoc before;BBResult res;BBScope scope;BBByte oldnets[BB_HOLES];
 CK("topology",sizeof(BBWord)==2&&sizeof(BBInt)==2&&sizeof(BBNum)==8&&sizeof(BBPart)==16&&sizeof(BBWire)==16);
 for(h=0;h<420;h++)CK("topology",bb_strip(h)==(int)vx_physical_strip(h));
 CK("topology",bb_strip(420)==-1);CK("topology",bb_strip(BB_NONE)==-1);
 for(c=0;c<30;c++){for(r=2;r<=6;r++)CK("topology",bb_strip(vx_h(r,c))==bb_strip(vx_h(2,c)));for(r=7;r<=11;r++)CK("topology",bb_strip(vx_h(r,c))==bb_strip(vx_h(7,c)));CK("topology",bb_strip(vx_h(6,c))!=bb_strip(vx_h(7,c)));}
 for(r=0;r<14;r++)if(r<2||r>=12){CK("topology",bb_strip(vx_h(r,0))==bb_strip(vx_h(r,14)));CK("topology",bb_strip(vx_h(r,14))!=bb_strip(vx_h(r,15)));CK("topology",bb_strip(vx_h(r,15))==bb_strip(vx_h(r,29)));}
 memset(&p,0,sizeof(p));p.type=BB_HC00;p.pin[0]=vx_h(7,10);p.pin[1]=p.pin[2]=BB_NONE;
 for(s=0;s<2;s++){p.flags=s;for(k=1;k<=14;k++){unsigned row=k<=7?7:6,col=k<=7?k-1:14-k;if(s){row=13-row;col=6-col;}CK("topology",bb_pin(&p,k)==vx_h(row,10+col));}}
 p.type=BB_555;for(s=0;s<2;s++){p.flags=s;for(k=1;k<=8;k++){unsigned row=k<=4?7:6,col=k<=4?k-1:8-k;if(s){row=13-row;col=3-col;}CK("topology",bb_pin(&p,k)==vx_h(row,10+col));}}
 vx_fresh();vx_part(BB_RES,vx_h(2,1),vx_h(2,3),BB_NONE,1000,0,0);vx_part(BB_RES,vx_h(7,1),vx_h(7,3),BB_NONE,1000,0,0);vx_wire(vx_h(3,1),vx_h(8,3));vx_wire(vx_h(3,3),vx_h(8,1));CK("topology",bb_validate(&bb_doc)&&bb_compile());CK("topology",vx_net(0,1)==vx_net(1,2));CK("topology",vx_net(0,2)==vx_net(1,1));CK("topology",vx_net(0,1)!=vx_net(0,2));memcpy(oldnets,bb_hnet,sizeof(oldnets));{BBWire q=bb_doc.wire[0];bb_doc.wire[0]=bb_doc.wire[1];bb_doc.wire[1]=q;}CK("topology",bb_compile()&&!memcmp(oldnets,bb_hnet,sizeof(oldnets)));
 vx_fresh();memset(&p,0,sizeof(p));p.type=BB_RES;p.pin[0]=vx_h(2,1);p.pin[1]=vx_h(2,2);p.pin[2]=BB_NONE;p.value=1000;CK("topology",bb_edit_part(-1,&p));bb_mark_saved();memcpy(&before,&bb_doc,sizeof(before));res=bb_result;scope=bb_scope;bb_running=1;bb_valid=1;bb_recompile=0;CK("topology",!bb_edit_part(-1,&p));CK("topology",!memcmp(&before,&bb_doc,sizeof(before))&&bb_running==1&&bb_valid==1&&bb_recompile==0&&bb_dirty==0&&!memcmp(&res,&bb_result,sizeof(res))&&!memcmp(&scope,&bb_scope,sizeof(scope)));
 for(k=0;k<20;k++){p.value=1001+k;CK("topology",bb_edit_part(0,&p));}for(k=0;k<16;k++)CK("topology",bb_undo(0));CK("topology",!bb_undo(0));for(k=0;k<16;k++)CK("topology",bb_undo(1));CK("topology",bb_undo(0));p.value=999;CK("topology",bb_edit_part(0,&p));CK("topology",!bb_undo(1));
 vx_fresh();for(k=0;k<64;k++)vx_part(BB_INDICATOR,k<30?vx_h(2,k):k<60?vx_h(7,k-30):vx_h((k-60)/2,(k&1)?15:0),BB_NONE,BB_NONE,0,0,0);CK("topology",bb_validate(&bb_doc)&&bb_compile()&&bb_nnet==64);CK("topology",!bb_edit_part(-1,&p)&&bb_error==BB_ECAPACITY);memcpy(oldnets,bb_hnet,sizeof(oldnets));vx_wire(vx_h(12,0),vx_h(12,15));CK("topology",bb_validate(&bb_doc)&&!bb_compile()&&bb_error==BB_ECAPACITY&&!memcmp(oldnets,bb_hnet,sizeof(oldnets)));
 vx_fresh();bb_doc.next_id=65534;memset(&p,0,sizeof(p));p.type=BB_RES;p.pin[0]=vx_h(2,1);p.pin[1]=vx_h(2,2);p.pin[2]=BB_NONE;p.value=1000;CK("topology",bb_edit_part(-1,&p)&&bb_doc.part[0].id==65534&&bb_doc.next_id==65535);CK("topology",!bb_edit_part(-1,&p)&&bb_error==BB_ECAPACITY);CK("topology",bb_undo(0)&&bb_doc.next_id==65535&&!bb_edit_part(-1,&p));
 vx_fresh();CK("topology",bb_edit_part(-1,&p));bb_doc.probe[0]=BB_NONE;bb_doc.probe_kind[0]=BB_PROBE_PIN;bb_doc.probe_id[0]=bb_doc.part[0].id;bb_doc.probe_pin[0]=1;bb_mark_saved();CK("topology",bb_delete(BB_PART,bb_doc.part[0].id)&&bb_doc.probe_kind[0]==BB_PROBE_HOLE&&bb_doc.probe[0]==BB_NONE);CK("topology",bb_undo(0)&&bb_doc.probe_kind[0]==BB_PROBE_PIN&&bb_doc.probe_id[0]==bb_doc.part[0].id&&!bb_dirty);
 vx_fresh();for(k=0;k<128;k++)vx_wire(2*k,2*k+1);CK("topology",bb_validate(&bb_doc));{BBWire w;memset(&w,0,sizeof(w));w.a=256;w.b=257;CK("topology",!bb_edit_wire(-1,&w)&&bb_error==BB_ECAPACITY);}
}

/* Independent logical resistor graph and physical hole allocator. */
static unsigned vx_used[32],vx_nodehole[32];
static unsigned vx_take(unsigned node){unsigned use=vx_used[node]++,h;if(node<2){h=vx_h(node,use);}else{h=vx_h(2+use,node-2);}if(use>=(node<2?15:5)){fprintf(stderr,"fixture occupancy exhausted\n");exit(2);}if(!use)vx_nodehole[node]=h;return h;}
static void vx_graph_begin(void){vx_fresh();memset(vx_used,0,sizeof(vx_used));vx_part(BB_POWER,vx_take(0),vx_take(1),BB_NONE,5000,0,0);vx_part(BB_GROUND,vx_take(1),BB_NONE,BB_NONE,0,0,0);}
static unsigned vx_res(unsigned a,unsigned b,unsigned encoded){return vx_part(BB_RES,vx_take(a),vx_take(b),BB_NONE,encoded,0,0);}
static int vx_node_mv(unsigned node){unsigned n=bb_hnet[vx_nodehole[node]];return n<BB_NETS?bb_result.mv[n]:BB_MV_FLOAT;}
static void vx_dc(void){unsigned i,n;int v;BBPart p;
 vx_graph_begin();vx_res(0,2,1000);vx_res(2,1,1000);CK("numeric",bb_validate(&bb_doc)&&vx_start()==1);CK("numeric",vx_node_mv(2)==2500&&bb_result.ua[2]==250&&bb_result.ua[3]==250&&bb_result.ua[0]==-250);
 vx_graph_begin();vx_res(0,1,100);vx_res(0,1,100);CK("numeric",vx_start()==1);CK("numeric",bb_result.ua[2]==5000&&bb_result.ua[3]==5000&&bb_result.ua[0]==-10000);
 CK("numeric",vx_example(1)&&vx_start()==1);CK("numeric",abs(bb_result.ua[3]-8824)<=1);p=bb_doc.part[3];p.flags=BB_ROT;CK("numeric",bb_edit_part(3,&p)&&vx_start()==1);CK("numeric",bb_result.ua[3]==0);
 CK("numeric",vx_example(4));p=bb_doc.part[3];p.flags=0;CK("numeric",bb_edit_part(3,&p)&&vx_start()==1);CK("numeric",bb_result.ua[2]==0&&bb_result.ua[4]==0);CK("regression-led",vx_mv(4,1)==BB_MV_FLOAT);
 CK("numeric",vx_example(6)&&vx_start()==-1&&bb_result.diag==BB_ELEDLIMIT);
 for(i=0;i<3;i++){CK("numeric",vx_example(3));p=bb_doc.part[2];p.param=i*500;CK("numeric",bb_edit_part(2,&p)&&vx_start()==1);v=vx_mv(2,3);CK("numeric",abs(v-(int)(5000-i*2500))<=1);}
 vx_graph_begin();vx_res(0,1,10);CK("numeric",vx_start()==-1&&bb_result.diag==BB_ENUMERIC&&!bb_valid);
 vx_graph_begin();vx_wire(vx_take(0),vx_take(1));CK("numeric",vx_start()==-1&&bb_result.diag==BB_ESHORT);
 vx_graph_begin();vx_res(2,3,1000);CK("numeric",vx_start()==-1&&(bb_result.diag==BB_EFLOAT||bb_result.diag==BB_ESINGULAR));
 vx_fresh();vx_part(BB_RES,60,61,BB_NONE,1000,0,0);CK("numeric",vx_start()==-1&&bb_result.diag==BB_ENOGROUND);
 for(n=24;n<=25;n++){vx_graph_begin();for(i=0;i<=n;i++)vx_res(i==0?0:i+1,i==n?1:i+2,1000);CK("numeric",bb_validate(&bb_doc));if(n==24){CK("numeric",vx_start()==1);CK("numeric",abs(vx_node_mv(2)-4800)<=1);}else CK("numeric",vx_start()==-1&&bb_result.diag==BB_ECAPACITY);}
}

static BBWord vx_dip_hole(unsigned pin,unsigned rot){unsigned r=pin<=7?7:6,c=pin<=7?pin-1:14-pin;if(rot){r=13-r;c=6-c;}return vx_h(r,11+c);}
static unsigned vx_digital(unsigned type,unsigned rot,unsigned a,unsigned b,unsigned va,unsigned vb){unsigned u;BBWord hp,hg,ha,hb;vx_fresh();hp=vx_dip_hole(14,rot);hg=vx_dip_hole(7,rot);u=vx_part(type,vx_h(7,11),BB_NONE,BB_NONE,0,0,rot);vx_part(BB_POWER,vx_h(hp/30==6?2:8,hp%30),vx_h(1,0),BB_NONE,5000,0,0);vx_part(BB_GROUND,vx_h(1,1),BB_NONE,BB_NONE,0,0,0);vx_wire(vx_h(hg/30==6?3:9,hg%30),vx_h(1,2));ha=vx_dip_hole(a,rot);vx_part(BB_INPUT,vx_h(ha/30==6?4:10,ha%30),BB_NONE,BB_NONE,0,va,0);if(b){hb=vx_dip_hole(b,rot);vx_part(BB_INPUT,vx_h(hb/30==6?4:10,hb%30),BB_NONE,BB_NONE,0,vb,0);}return u;}
static int vx_truth(unsigned type,int a,int b){if(type==BB_HC04)return a<2?!a:BB_X;if(type==BB_HC00||type==BB_HC08){int v=a==0||b==0?0:a==1&&b==1?1:BB_X;return type==BB_HC00&&v<2?!v:v;}if(type==BB_HC32)return a==1||b==1?1:a==0&&b==0?0:BB_X;return a<2&&b<2?a^b:BB_X;}
static void vx_logic(void){static const unsigned quad[4][3]={{1,2,3},{4,5,6},{9,10,8},{12,13,11}};static const unsigned inv[6][2]={{1,2},{3,4},{5,6},{9,8},{11,10},{13,12}};unsigned type,rot,g,a,b,u,out;int want,r;BBPart p;
 for(type=BB_HC00;type<=BB_HC86;type++)for(rot=0;rot<2;rot++)for(g=0;g<(type==BB_HC04?6:4);g++)for(a=0;a<4;a++)for(b=0;b<(type==BB_HC04?1:4);b++){
  u=vx_digital(type,rot,type==BB_HC04?inv[g][0]:quad[g][0],type==BB_HC04?0:quad[g][1],a,b);out=type==BB_HC04?inv[g][1]:quad[g][2];want=vx_truth(type,a,b);r=vx_start();CK("digital",r==1);if(r==1){CK("digital",bb_result.logic[vx_net(u,out)]==want);CK("digital",want<2?vx_mv(u,out)==want*5000:vx_mv(u,out)==BB_MV_UNKNOWN);}}
 vx_digital(BB_HC08,0,1,2,0,3);CK("digital",vx_start()==1&&bb_result.logic[vx_net(0,3)]==BB_LOW);CK("digital",bb_result.logic[vx_net(0,4)]==BB_Z&&bb_result.logic[vx_net(0,6)]==BB_X);
 vx_digital(BB_HC04,0,1,0,1,0);p=bb_doc.part[1];p.pin[0]=vx_h(2,0);CK("digital",bb_edit_part(1,&p)&&vx_start()==-1&&bb_result.diag==BB_EPOWER);
 vx_graph_begin();vx_part(BB_INPUT,vx_take(2),BB_NONE,BB_NONE,0,BB_HIGH,0);vx_part(BB_INPUT,vx_take(2),BB_NONE,BB_NONE,0,BB_LOW,0);CK("digital",vx_start()==-1&&bb_result.diag==BB_ECONTENTION);
 vx_graph_begin();vx_part(BB_INPUT,vx_take(2),BB_NONE,BB_NONE,0,BB_HIGH,0);vx_part(BB_INPUT,vx_take(2),BB_NONE,BB_NONE,0,BB_HIGH,0);CK("digital",vx_start()==1&&vx_node_mv(2)==5000);
 vx_digital(BB_HC04,0,1,0,BB_LOW,0);vx_part(BB_INPUT,vx_h(10,13),BB_NONE,BB_NONE,0,BB_HIGH,0);vx_wire(vx_h(9,12),vx_h(9,14));CK("digital",vx_start()==-1&&bb_result.diag==BB_ECONTENTION);
 vx_digital(BB_HC04,0,1,0,BB_LOW,0);vx_part(BB_INPUT,vx_h(10,13),BB_NONE,BB_NONE,0,BB_LOW,0);vx_wire(vx_h(9,12),vx_h(9,14));CK("digital",vx_start()==1&&vx_mv(0,2)==5000&&vx_mv(0,4)==5000);
 vx_digital(BB_HC04,0,1,0,BB_LOW,0);CK("digital",bb_delete(BB_WIRE,bb_doc.wire[0].id)&&vx_start()==-1&&bb_result.diag==BB_EPOWER);
 vx_digital(BB_HC04,0,1,0,BB_LOW,0);vx_part(BB_RES,vx_h(8,12),vx_h(8,20),BB_NONE,33,0,0);u=vx_part(BB_LED,vx_h(9,20),vx_h(1,3),BB_NONE,0,0,0);CK("digital",vx_start()==1&&abs(bb_result.ua[u]-8824)<=1);
 vx_digital(BB_HC04,0,1,0,BB_Z,0);vx_part(BB_RES,vx_h(8,11),vx_h(8,12),BB_NONE,1000,0,0);CK("digital",vx_start()==-1&&bb_result.diag==BB_EBRIDGE);
 vx_digital(BB_HC04,0,1,0,BB_Z,0);vx_wire(vx_h(8,11),vx_h(8,12));r=vx_start();CK("digital",r==-1?bb_result.diag==BB_EOSCILLATE:r==1&&bb_result.logic[vx_net(0,2)]==BB_X);
}

/* Feed-forward analog-to-HC bridge: no logical source drives the input. */
static void vx_analog_hc(unsigned upper,unsigned lower)
{
    vx_digital(BB_HC04,0,1,0,BB_Z,0);
    bb_doc.np--; /* remove last Z input fixture object, retaining monotonic ID */
    vx_part(BB_RES,vx_h(3,11),vx_h(10,11),BB_NONE,upper,0,0);
    vx_part(BB_RES,vx_h(9,11),vx_h(1,3),BB_NONE,lower,0,0);
}
static void vx_bridges(void)
{
    unsigned k;
    static const unsigned upper[]={1000,2000,3000,1000};
    static const unsigned lower[]={1000,1000,1000,3000};
    static const unsigned want[]={BB_X,BB_X,BB_HIGH,BB_LOW};
    for(k=0;k<4;k++){
        vx_analog_hc(upper[k],lower[k]);
        CK("digital",bb_validate(&bb_doc)&&vx_start()==1);
        CK("digital",abs(vx_mv(0,1)-(int)lround(5000.0*lower[k]/(upper[k]+lower[k])))<=1);
        CK("digital",bb_result.logic[vx_net(0,2)]==want[k]);
    }
}

static void vx_transient(void){unsigned i,slow,fast,period1,period2;BBPart p;BBResult prior;BBScope capture;int r;
 CK("transient",vx_example(7)&&vx_start()==1&&vx_run(99));slow=vx_mv(3,1);CK("transient",abs((int)slow-(int)lround(5000*(1-pow(1.0/1.01,100))))<=1);
 CK("transient",vx_example(7));bb_doc.dt=5;CK("transient",vx_start()==1&&vx_run(199));fast=vx_mv(3,1);CK("transient",abs((int)fast-(int)lround(5000*(1-pow(1.0/1.005,200))))<=1);CK("transient",fabs(fast-5000*(1-exp(-1.0)))<fabs(slow-5000*(1-exp(-1.0))));
 CK("transient",vx_example(7));p=bb_doc.part[3];p.param=2000;CK("transient",bb_edit_part(3,&p)&&vx_start()==1);CK("transient",abs(vx_mv(3,1)-2030)<=1);bb_sim_reset();CK("transient",bb_scope.count==0&&bb_result.tlo==0&&bb_result.thi==0);CK("transient",vx_start()==1&&abs(vx_mv(3,1)-2030)<=1);
 CK("transient",vx_example(8)&&vx_start()==1&&vx_run(7999));period1=bb_scope.period_lo;CK("transient",bb_scope.period_hi==0&&abs((int)period1-10120)<=203);CK("transient",fabs((double)bb_scope.high_lo/period1-78.0/146.0)<0.02);CK("scope",bb_scope.count==256&&bb_scope.cycles>=5&&bb_result.thi==1);
 for(i=1;i<bb_scope.count;i++){unsigned prev=(bb_scope.head+i-1)&255,cur=(bb_scope.head+i)&255;uint32_t ta=((uint32_t)bb_scope.hi[prev]<<16)|bb_scope.lo[prev],tb=((uint32_t)bb_scope.hi[cur]<<16)|bb_scope.lo[cur];CK("scope",tb>ta);}
 prior=bb_result;capture=bb_scope;bb_running=0;CK("transient",!memcmp(&prior,&bb_result,sizeof(prior))&&!memcmp(&capture,&bb_scope,sizeof(capture)));
 p=bb_doc.part[4];p.value=10000;CK("transient",bb_edit_part(4,&p)&&vx_start()==1&&vx_run(9999));period2=bb_scope.period_lo;CK("transient",period2>period1&&abs((int)period2-14556)<=292);
 CK("transient",vx_example(8));{BBWire w=bb_doc.wire[3];w.b=vx_h(13,13);w.nbend=1;w.bend[0]=13;w.bend[1]=8;w.bend[2]=w.bend[3]=0;CK("transient",bb_edit_wire(3,&w)&&vx_start()==1&&vx_run(99));CK("transient",vx_mv(2,3)==0);}
 CK("transient",vx_example(8));CK("transient",bb_delete(BB_WIRE,bb_doc.wire[6].id)&&vx_start()==1&&vx_run(5999));CK("transient",bb_scope.cycles==0&&vx_mv(2,3)==5000);
 CK("scope",vx_example(2)&&vx_start()==1&&vx_run(5999));CK("scope",bb_scope.count==256);for(i=0;i<256;i++)CK("scope",bb_scope.mv[0][i]==2500&&bb_scope.mv[1][i]==5000);
 bb_scope_reset();bb_result.tlo=65530;bb_result.thi=3;r=vx_step();CK("scope",r==1&&bb_result.tlo==4&&bb_result.thi==4);
 CK("scope",vx_example(8));bb_doc.trigger=1;CK("scope",vx_start()==1&&vx_run(3999));CK("scope",bb_scope.triggered==2&&bb_scope.count>=128);capture=bb_scope;CK("scope",vx_run(100)&&bb_scope.head==capture.head&&bb_scope.count==capture.count&&!memcmp(bb_scope.mv,capture.mv,sizeof(capture.mv)));
 CK("transient",vx_example(7)&&vx_start()==1&&vx_run(9));prior=bb_result;capture=bb_scope;vx_num_calls=0;vx_inject=20;r=vx_step();vx_inject=0;CK("transient",r==-1&&bb_result.diag==BB_ENUMERIC&&bb_result.tlo==prior.tlo&&bb_result.thi==prior.thi&&!memcmp(bb_scope.mv,capture.mv,sizeof(capture.mv)));
}

static void vx_additional(void)
{
    unsigned i,wire,part;BBPart p;BBWire w;BBResult prior;BBScope scope;int r;
    /* Resistor-to-ground with a charged capacitor: exact BE discharge oracle. */
    vx_graph_begin();vx_res(2,1,1000);
    part=vx_part(BB_CAP,vx_take(2),vx_take(1),BB_NONE,10000,5000,0);
    CK("transient",vx_start()==1&&vx_run(99));
    CK("transient",abs(vx_mv(part,1)-(int)lround(5000*pow(1/1.01,100)))<=1);CK("transient",bb_result.ua[part]<0);
    /* A rejected edit/load while a new RC step is partial must not poison it. */
    CK("transient",vx_example(7)&&vx_start()==1);prior=bb_result;
    CK("transient",bb_sim_slice(1)==0);
    p=bb_doc.part[2];p.pin[0]=bb_doc.part[0].pin[0];
    CK("transient",!bb_edit_part(2,&p));
    CK("transient",bb_result.tlo==prior.tlo&&bb_result.thi==prior.thi);
    CK("transient",vx_step()==1&&abs(vx_mv(3,1)-99)<=1);
    /* 555 threshold high while trigger remains below1/3: trigger wins. */
    CK("transient",vx_example(8));
    CK("transient",bb_delete(BB_WIRE,bb_doc.wire[7].id));
    vx_part(BB_INPUT,vx_h(5,13),BB_NONE,BB_NONE,0,BB_HIGH,0);
    CK("transient",bb_validate(&bb_doc)&&vx_start()==1);
    CK("transient",vx_mv(2,2)<1667&&vx_mv(2,6)==5000&&vx_mv(2,3)==5000);
    /* Reset dominates the same contradictory comparator inputs. */
    w=bb_doc.wire[3];w.b=vx_h(13,13);w.nbend=1;w.bend[0]=13;w.bend[1]=8;w.bend[2]=w.bend[3]=0;
    CK("transient",bb_edit_wire(3,&w)&&vx_start()==1&&vx_mv(2,3)==0);
    /* Powered supply exists, but actual pin8 loses it. */
    CK("transient",vx_example(8));wire=4;
    CK("transient",bb_delete(BB_WIRE,bb_doc.wire[wire].id));
    CK("transient",vx_start()==-1&&bb_result.diag==BB_EPOWER);
    /* Unsupported external CONT resistor is explicit, never silently ignored. */
    CK("transient",vx_example(8));
    vx_part(BB_RES,vx_h(3,14),vx_h(0,3),BB_NONE,1000,0,0);
    CK("transient",bb_validate(&bb_doc)&&vx_start()==-1&&bb_result.diag==BB_ECONTROL);
    /* Move timing capacitor: behavior must stop or cease the1Hz waveform. */
    CK("transient",vx_example(8));p=bb_doc.part[5];p.pin[0]=vx_h(10,27);
    CK("transient",bb_edit_part(5,&p));r=vx_start();if(r==1)r=vx_run(2999)?1:-1;
    CK("transient",r==-1||bb_scope.cycles==0||abs((int)bb_scope.period_lo-10120)>203);
    /* Clock and timestamp cadence are simulated, not host time. */
    vx_graph_begin();part=vx_part(BB_CLOCK,vx_take(2),BB_NONE,BB_NONE,1000,250,0);
    bb_doc.probe[0]=vx_nodehole[2];CK("scope",vx_start()==1&&vx_run(999));
    CK("scope",bb_scope.period_hi==0&&bb_scope.period_lo==1000&&bb_scope.high_lo==250&&bb_scope.cycles>=8);
    CK("scope",bb_scope.undersampled==0);
    bb_doc.trigger=2;bb_scope_reset();CK("scope",vx_run(3000));
    CK("scope",bb_scope.triggered==2);
    scope=bb_scope;prior=bb_result;bb_running=0;
    for(i=0;i<3;i++)CK("scope",!memcmp(&scope,&bb_scope,sizeof(scope))&&!memcmp(&prior,&bb_result,sizeof(prior)));
    /* Actual UINT32 epoch carry, including wrap: no signed16 arithmetic. */
    bb_result.tlo=65530;bb_result.thi=65535;CK("scope",vx_step()==1&&bb_result.tlo==4&&bb_result.thi==0);
}

/* Independent implementation-review regressions: expected physical behavior. */
static void vx_review_regressions(void)
{
    unsigned i,led,clock,chip;int have_low=0,have_high=0;BBPart p;BBWord saved;
    /* Uniform passive sign: I1->2 enters a source's positive terminal.
     * A supplying source is negative; capacitor back-drive makes it positive. */
    vx_graph_begin();vx_part(BB_CAP,vx_take(0),vx_take(1),BB_NONE,100,6000,0);
    CK("regression-source",vx_start()==1);
    CK("regression-source",bb_result.ua[2]==-100&&bb_result.ua[0]==100);
    /* Channel A is DC. Channel B must preserve or disclose its20ms clock. */
    vx_graph_begin();clock=vx_part(BB_CLOCK,vx_take(2),BB_NONE,BB_NONE,200,500,0);
    bb_doc.probe[0]=vx_nodehole[0];bb_doc.probe[1]=vx_nodehole[2];
    CK("regression-scope",vx_start()==1&&vx_run(499));
    for(i=0;i<bb_scope.count;i++){
        if(bb_scope.mv[1][i]==0)have_low=1;
        if(bb_scope.mv[1][i]==5000)have_high=1;
    }
    CK("regression-scope",(have_low&&have_high)||bb_scope.undersampled);
    CK("regression-scope",bb_scope.cycles==0&&vx_mv(clock,1)>=0);
    /* Dominant RESET is independent of unwired TRIG and THRES comparators. */
    vx_fresh();chip=vx_part(BB_555,vx_h(7,11),BB_NONE,BB_NONE,0,0,0);
    vx_part(BB_POWER,vx_h(2,11),vx_h(1,0),BB_NONE,5000,0,0);
    vx_part(BB_GROUND,vx_h(1,1),BB_NONE,BB_NONE,0,0,0);
    vx_wire(vx_h(8,11),vx_h(1,2)); /* actualpin1ground */
    vx_wire(vx_h(8,14),vx_h(1,3)); /* actualpin4resetLOW */
    CK("regression-reset",bb_validate(&bb_doc)&&vx_start()==1);
    CK("regression-reset",vx_mv(chip,3)==0&&vx_mv(chip,7)==0&&vx_mv(chip,2)==BB_MV_FLOAT&&vx_mv(chip,6)==BB_MV_FLOAT);
    /* An isolated zero-current LED cannot assign its own2V reference. */
    vx_graph_begin();led=vx_part(BB_LED,vx_take(2),vx_take(1),BB_NONE,0,0,0);
    CK("regression-led",vx_start()==1);
    CK("regression-led",bb_result.ua[led]==0&&vx_mv(led,1)==BB_MV_FLOAT);
    /* A genuine resistor reference retains meaningful0V. */
    vx_graph_begin();vx_res(2,1,1000);led=vx_part(BB_LED,vx_take(2),vx_take(1),BB_NONE,0,0,0);
    CK("regression-led",vx_start()==1&&bb_result.ua[led]==0&&vx_mv(led,1)==0);
    /* A capacitor's initialized1V is also a genuine transient reference. */
    vx_graph_begin();vx_part(BB_CAP,vx_take(2),vx_take(1),BB_NONE,10000,1000,0);
    led=vx_part(BB_LED,vx_take(2),vx_take(1),BB_NONE,0,0,0);
    CK("regression-led",vx_start()==1&&bb_result.ua[led]==0&&vx_mv(led,1)==1000);
    /* Two conducting series LEDs have a valid inter-LED node despite no
     * separate R/C path at that node. R330+2*R10 gives1V/350ohm. */
    vx_graph_begin();vx_res(0,2,33);
    vx_part(BB_LED,vx_take(2),vx_take(3),BB_NONE,0,0,0);
    led=vx_part(BB_LED,vx_take(3),vx_take(1),BB_NONE,0,0,0);
    CK("regression-led",vx_start()==1&&abs(bb_result.ua[led]-2857)<=1&&abs(vx_mv(led,1)-2029)<=1);
    /* Clock-driven series LEDs must recover after a genuinely dark phase;
     * the middle diode node is allowed to float while no current flows. */
    vx_graph_begin();vx_part(BB_CLOCK,vx_take(2),BB_NONE,BB_NONE,2000,500,0);
    vx_res(2,3,33);vx_part(BB_LED,vx_take(3),vx_take(4),BB_NONE,0,0,0);
    led=vx_part(BB_LED,vx_take(4),vx_take(1),BB_NONE,0,0,0);
    CK("regression-led",vx_start()==1&&abs(bb_result.ua[led]-2857)<=1);
    CK("regression-led",vx_run(99)&&bb_result.ua[led]==0);
    CK("regression-led",vx_run(100)&&abs(bb_result.ua[led]-2857)<=1);
    /* Weak genuine forward drive must not oscillate an equality tolerance.
     * The unloaded divider is about2.00006V; loaded LED current rounds0uA. */
    vx_graph_begin();vx_res(0,2,30000);vx_res(2,1,20001);
    led=vx_part(BB_LED,vx_take(2),vx_take(1),BB_NONE,0,0,0);
    CK("regression-led",vx_start()==1&&vx_mv(led,1)==2000&&bb_result.ua[led]==0);
    vx_graph_begin();vx_res(0,2,65535);led=vx_part(BB_LED,vx_take(2),vx_take(1),BB_NONE,0,0,0);
    CK("regression-led",vx_start()==1&&vx_mv(led,1)==2000&&bb_result.ua[led]==5);
    /* Diode on-resistance is not an external limiter: two forward LEDs
     * directly across4.5V conduct25mA, below the current range guard. */
    vx_graph_begin();bb_doc.part[0].value=4500;
    vx_part(BB_LED,vx_take(0),vx_take(2),BB_NONE,0,0,0);
    vx_part(BB_LED,vx_take(2),vx_take(1),BB_NONE,0,0,0);
    CK("regression-limiter",vx_start()==-1&&bb_result.diag==BB_ELEDLIMIT);
    /* A parallel resistor does not limit that diode-only path. */
    vx_graph_begin();bb_doc.part[0].value=4500;
    vx_part(BB_LED,vx_take(0),vx_take(2),BB_NONE,0,0,0);
    vx_part(BB_LED,vx_take(2),vx_take(1),BB_NONE,0,0,0);vx_res(0,1,1000);
    CK("regression-limiter",vx_start()==-1&&bb_result.diag==BB_ELEDLIMIT);
    /* A real series100ohm resistor limits the same stack to0.5V/120ohm. */
    vx_graph_begin();bb_doc.part[0].value=4500;vx_res(0,2,10);
    vx_part(BB_LED,vx_take(2),vx_take(3),BB_NONE,0,0,0);
    led=vx_part(BB_LED,vx_take(3),vx_take(1),BB_NONE,0,0,0);
    CK("regression-limiter",vx_start()==1&&abs(bb_result.ua[led]-4167)<=1);
    /* The model explicitly supports ONE supply; never borrow a second VCC. */
    vx_digital(BB_HC04,0,1,0,BB_LOW,0);
    vx_part(BB_POWER,vx_h(0,0),vx_h(1,4),BB_NONE,6000,0,0);
    CK("regression-supply",bb_validate(&bb_doc)&&vx_start()==-1&&bb_result.diag==BB_ESUPPLIES);
    /* Clock LOW first primes NAND output HIGH; next HIGH-clock step creates
     * actual known zero-delay feedback and must exhaust the bounded queue. */
    vx_digital(BB_HC00,0,1,2,BB_Z,BB_Z);bb_doc.np-=2;
    vx_part(BB_CLOCK,vx_h(10,11),BB_NONE,BB_NONE,0+20,500,0);
    vx_wire(vx_h(9,13),vx_h(9,12));
    CK("regression-events",bb_validate(&bb_doc)&&vx_start()==1&&bb_result.logic[vx_net(0,3)]==BB_HIGH);
    CK("regression-events",vx_step()==-1&&bb_result.diag==BB_EOSCILLATE);
    /* Persisted view/probe edits affect saved-source dirty state only. */
    CK("regression-dirty",vx_example(2)&&vx_start()==1);bb_mark_saved();
    bb_running=1;saved=bb_doc.probe_pin[0];bb_doc.probe_pin[0]=2;bb_view_changed();
    CK("regression-dirty",bb_dirty&&bb_valid&&bb_running&&!bb_recompile);
    bb_doc.probe_pin[0]=saved;bb_view_changed();
    CK("regression-dirty",!bb_dirty&&bb_valid&&bb_running&&!bb_recompile);
    saved=bb_doc.panx;bb_doc.panx=1;bb_view_changed();CK("regression-dirty",bb_dirty);
    bb_doc.panx=saved;bb_view_changed();CK("regression-dirty",!bb_dirty);
    vx_fresh();memset(&p,0,sizeof(p));p.type=BB_RES;p.pin[0]=60;p.pin[1]=61;p.pin[2]=BB_NONE;p.value=1000;
    CK("regression-dirty",bb_edit_part(-1,&p)&&bb_dirty&&bb_undo(0)&&bb_doc.np==0&&!bb_dirty);
}

static void vx_history_readout(void)
{
    BBPart p,out;BBWire wire;BBDoc before;unsigned kind;
    vx_fresh();memset(&p,0,sizeof(p));p.type=BB_RES;p.pin[0]=60;p.pin[1]=61;p.pin[2]=BB_NONE;p.value=1000;
    CK("regression-history",bb_history_object(0,0,&out,&wire)==0&&bb_history_object(1,0,&out,&wire)==0);
    CK("regression-history",bb_edit_part(-1,&p));before=bb_doc;
    CK("regression-history",bb_history_object(0,0,&out,&wire)==BB_PART&&out.id==bb_doc.part[0].id&&out.value==1000);
    CK("regression-history",bb_history_object(0,1,&out,&wire)==0&&!memcmp(&before,&bb_doc,sizeof(before)));
    CK("regression-history",bb_undo(0));
    CK("regression-history",bb_history_object(1,0,&out,&wire)==0&&bb_history_object(1,1,&out,&wire)==BB_PART&&out.value==1000);
    CK("regression-history",bb_undo(1)&&bb_delete(BB_PART,bb_doc.part[0].id));before=bb_doc;
    kind=bb_history_object(0,0,&out,&wire);
    CK("regression-history",kind==0&&bb_history_object(0,1,&out,&wire)==BB_PART&&out.value==1000&&!memcmp(&before,&bb_doc,sizeof(before)));
    CK("regression-history",bb_undo(0)&&bb_history_object(1,0,&out,&wire)==BB_PART&&bb_history_object(1,1,&out,&wire)==0);
}

static void vx_codec(void){static BBByte data[BB_FILEMAX],copy[BB_FILEMAX];BBDoc out,live;BBResult prior;BBScope scope;unsigned n,i,m;BBWord v;
 CK("codec",vx_example(8));n=bb_encode(&bb_doc,data,sizeof(data));CK("codec",n==352&&data[6]==48&&data[30]==1);memcpy(&live,&bb_doc,sizeof(live));prior=bb_result;scope=bb_scope;
 for(i=0;i<n;i++){CK("codec",!bb_decode(data,i,&out));CK("codec",!memcmp(&live,&bb_doc,sizeof(live))&&!memcmp(&prior,&bb_result,sizeof(prior))&&!memcmp(&scope,&bb_scope,sizeof(scope)));}
 for(i=0;i<48;i++){memcpy(copy,data,n);copy[i]^=128;m=bb_decode(copy,n,&out);if(i==8||i==10||i==30||i>=44)CK("codec",!m);}
 CK("codec",bb_decode(data,n,&out)&&bb_encode(&out,copy,sizeof(copy))==n&&!memcmp(data,copy,n));memcpy(copy,data,n);copy[48+16]=copy[48];copy[48+17]=copy[49];CK("codec",!bb_decode(copy,n,&out));memcpy(copy,data,n);copy[48+4]=255;copy[48+5]=255;CK("codec",!bb_decode(copy,n,&out));
 CK("codec",bb_parse_value(BB_RES,"655350ohm",&v)&&v==65535);CK("codec",bb_parse_value(BB_RES,"655.35k",&v)&&v==65535);CK("codec",bb_parse_value(BB_RES,"330",&v)&&v==33);CK("codec",bb_parse_value(BB_CAP,"10uF",&v)&&v==10000);CK("codec",bb_parse_value(BB_CAP,"0.1uF",&v)&&v==100);CK("codec",bb_parse_value(BB_POWER,"4.5V",&v)&&v==4500);v=1234;CK("codec",!bb_parse_value(BB_RES,"333ohm",&v)&&v==1234);CK("codec",!bb_parse_value(BB_RES,"-10k",&v)&&v==1234);CK("codec",!bb_parse_value(BB_RES,"1e4",&v)&&v==1234);CK("codec",!bb_parse_value(BB_RES,"999999999999999999",&v)&&v==1234);
}

static int vx_network(void)
{
    unsigned n,m,i,a,b,ohm;
    if(scanf("%u %u",&n,&m)!=2||n<1||n>20||m+2>BB_PARTS)return 2;
    vx_graph_begin();
    for(i=0;i<m;i++){
        if(scanf("%u %u %u",&a,&b,&ohm)!=3||a>=n+2||b>=n+2||ohm<100||ohm>655350||ohm%10)return 2;
        vx_res(a,b,ohm/10);
    }
    if(!bb_validate(&bb_doc)||vx_start()!=1){fprintf(stderr,"network rejected diag %u\n",bb_error);return 1;}
    printf("{\"mv\":[5000,0");
    for(i=2;i<n+2;i++)printf(",%d",vx_node_mv(i));
    printf("],\"ua\":[");
    for(i=0;i<m;i++)printf("%s%d",i?",":"",bb_result.ua[i+2]);
    printf("],\"source_ua\":%d}\n",bb_result.ua[0]);
    return 0;
}
static void vx_fixture_probe(unsigned channel,unsigned part,unsigned pin,unsigned kind)
{
    bb_doc.probe_kind[channel]=kind;bb_doc.probe[channel]=BB_NONE;
    bb_doc.probe_id[channel]=bb_doc.part[part].id;bb_doc.probe_pin[channel]=pin;
}
static int vx_emit(const char *folder)
{
    static const char *names[]={"HCMID.BBX","HCLOW.BBX","HCHIGH.BBX","BCLK.BBX","RESET.BBX","FLOATLED.BBX","MAX24.BBX"};
    static const unsigned upper[]={1000,3000,1000},lower[]={1000,1000,3000};
    BBByte bytes[BB_FILEMAX];char path[1024];unsigned i,j,n;FILE *f;
    for(i=0;i<7;i++){
        if(i<3){vx_analog_hc(upper[i],lower[i]);vx_fixture_probe(0,0,1,BB_PROBE_PIN);vx_fixture_probe(1,0,2,BB_PROBE_PIN);}
        else if(i==3){vx_graph_begin();vx_part(BB_CLOCK,vx_take(2),BB_NONE,BB_NONE,200,500,0);bb_doc.scope_div=100;vx_fixture_probe(0,0,1,BB_PROBE_PIN);vx_fixture_probe(1,2,1,BB_PROBE_PIN);}
        else if(i==4){
            vx_fresh();vx_part(BB_555,vx_h(7,11),BB_NONE,BB_NONE,0,0,0);
            vx_part(BB_POWER,vx_h(2,11),vx_h(1,0),BB_NONE,5000,0,0);
            vx_part(BB_GROUND,vx_h(1,1),BB_NONE,BB_NONE,0,0,0);
            vx_wire(vx_h(8,11),vx_h(1,2));vx_wire(vx_h(8,14),vx_h(1,3));
            vx_fixture_probe(0,0,3,BB_PROBE_PIN);vx_fixture_probe(1,0,7,BB_PROBE_PIN);
        }else if(i==5){
            vx_graph_begin();vx_part(BB_LED,vx_take(2),vx_take(1),BB_NONE,0,0,0);
            vx_fixture_probe(0,2,1,BB_PROBE_PIN);vx_fixture_probe(1,2,1,BB_PROBE_BRANCH);
        }else{
            vx_graph_begin();for(j=0;j<=24;j++)vx_res(j==0?0:j+1,j==24?1:j+2,1000);
            vx_fixture_probe(0,2,2,BB_PROBE_PIN);vx_fixture_probe(1,26,1,BB_PROBE_PIN);
        }
        n=bb_encode(&bb_doc,bytes,sizeof(bytes));if(!n)return 1;
        snprintf(path,sizeof(path),"%s/%s",folder,names[i]);f=fopen(path,"wb");if(!f)return 1;
        if(fwrite(bytes,1,n,f)!=n||fclose(f))return 1;
    }
    return 0;
}

int main(int argc,char **argv){if(argc==2&&!strcmp(argv[1],"--network"))return vx_network();if(argc==3&&!strcmp(argv[1],"--emit"))return vx_emit(argv[2]);if(argc==3&&!strcmp(argv[1],"--negative"))vx_negative=argv[2];else if(argc!=1){fprintf(stderr,"usage: coretest [--negative FAMILY]\n");return 2;}vx_topology();vx_dc();vx_logic();vx_bridges();vx_transient();vx_additional();vx_review_regressions();vx_history_readout();vx_codec();if(vx_negative&&!vx_negative_hit){fprintf(stderr,"negative family not exercised\n");return 2;}printf("breadxt core: %u checks, %u failures%s; max numeric ops/slice %u\n",vx_checks,vx_failures,vx_negative?" (negative control)":"",vx_slice_max);return vx_failures?1:0;}
