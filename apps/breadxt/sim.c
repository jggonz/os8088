/* Breadboard XT educational DC/digital/transient engine. Original code.
 * SPEC.md 130; numerical values are opaque shared software records.
 * Internal units V, mS, mA; public units mV/uA/100us. No OS calls here.
 * One slice unit is one numeric operation, comparison, gate evaluation,
 * object visit or bounded scalar bookkeeping step. No scalar loop solves
 * an entire Gaussian row. Only a complete step publishes cap/latch state.
 */
#define BB_S_BRANCHES 128
#define BB_S_GATES 32 /* physical 30-column board holds <=4 fourteen-pin DIPs */
#define BB_S_IDLE 0
#define BB_S_SCAN 1
#define BB_S_G 2
#define BB_S_BRIDGE 3
#define BB_S_SEED 4
#define BB_S_GATE 5
#define BB_S_CLASSIFY 6
#define BB_S_STAMP 7
#define BB_S_SCALE 8
#define BB_S_PIVOT 9
#define BB_S_SWAP 10
#define BB_S_ELIM 11
#define BB_S_BACK 12
#define BB_S_VOLTS 13
#define BB_S_DEVICES 14
#define BB_S_CURRENT 15
#define BB_S_RESIDUAL 16
#define BB_S_PUBLISH 17
#define BB_S_BEGIN 18
#define BB_S_RHS 19
#define BB_S_FORWARD 20
#define BB_S_REARM 21

/* Numeric library scratch is addressed by numeric.inc, not by C. */
BBByte bb_fp_scratch[76];
unsigned bb_umuldiv(unsigned a,unsigned b,unsigned den);

typedef struct {
    BBNum g,e;
    BBWord a,b,owner,kind,value,scale,aux,reserved;
} BBSBranch; /* 32 target bytes, compiler-friendly indexing */
typedef struct {
    BBByte a,b,out,op,drive,reserved;
    BBWord owner;
} BBSGate; /* 8 target bytes */
static BBSBranch bb_sb[BB_S_BRANCHES];
static BBSGate bb_sg[BB_S_GATES];
static BBNum bb_sm[BB_UNKNOWNS * 25];
static BBNum bb_sv[BB_NETS],bb_scap[BB_PARTS],bb_scapnext[BB_PARTS];
static BBNum bb_skcl[BB_NETS],bb_sabs[BB_UNKNOWNS],bb_srows[BB_UNKNOWNS];
static BBNum bb_st0,bb_st1,bb_st2,bb_sfactor,bb_sbest,bb_ssum;
static BBNum bb_sctrl,bb_strig;
static BBResult bb_sw;
static BBInt bb_sbase[BB_NETS],bb_svm[BB_NETS];
static BBByte bb_sfixed[BB_NETS],bb_sanalog[BB_NETS],bb_slogic[BB_NETS],bb_smanual[BB_NETS],bb_sconn[BB_NETS],bb_sanchor[BB_NETS];
static BBByte bb_sunk[BB_NETS],bb_sunet[BB_UNKNOWNS];
static BBByte bb_slatch[BB_PARTS],bb_swlatch[BB_PARTS],bb_sled[BB_PARTS];
static BBWord bb_sclock[BB_PARTS],bb_swclock[BB_PARTS],bb_sclockhigh[BB_PARTS];
static BBByte bb_sq[64],bb_sinq[BB_S_GATES],bb_sperm[BB_UNKNOWNS];
static BBWord bb_sluready,bb_slun,bb_sreuse;
static BBByte bb_sadj[BB_NETS][8],bb_sdeps[BB_NETS][8];
static BBByte bb_sseen[128],bb_ssearch[128];
static BBWord bb_sphase,bb_snbranch,bb_sngate,bb_snu,bb_sready,bb_sg_after;
static BBWord bb_si,bb_sj,bb_sk,bb_sr,bb_ssub;
static BBWord bb_sqhead,bb_sqtail,bb_sqcount,bb_sevents,bb_sledpasses;
static BBWord bb_sground,bb_ssupply,bb_ssupplymv,bb_srebuild,bb_schanged;
static BBWord bb_sclocked,bb_sclockquantized,bb_snextlo,bb_snexthi,bb_sbridgegate;
static BBWord bb_sbh,bb_sbt,bb_sbe;
static BBWord bb_sdiag,bb_sdiagpart,bb_sdiagnet,bb_scheckstage;
static BBWord bb_shasled;
static BBWord bb_scope_lastlo,bb_scope_lasthi,bb_scope_lastedge,bb_scope_lastedge_b;
static BBWord bb_scope_riselo,bb_scope_risehi,bb_scope_falllo,bb_scope_fallhi;
static BBWord bb_scope_have_rise,bb_scope_have_fall,bb_scope_post,bb_scope_mode;

/* Exact IEEE binary64 constants, encoded as four little-endian words. */
static const BBNum bb_n0={{0,0,0,0}};
static const BBNum bb_n2={{0,0,0,0x4000}};
static const BBNum bb_n3={{0,0,0,0x4008}};
static const BBNum bb_n6={{0,0,0,0x4018}};
static const BBNum bb_nreset={{0x6666,0x6666,0x6666,0x3fe6}}; /* 0.7 V */
static const BBNum bb_n30={{0,0,0,0x403e}};
static const BBNum bb_n100={{0,0,0,0x4059}};
static const BBNum bb_n1000={{0,0,0x4000,0x408f}};
static const BBNum bb_npivot={{0xd695,0xe826,0x2e0b,0x3e11}}; /* 1e-9 mS */
static const BBNum bb_nrelpivot={{0xea11,0x812d,0x9799,0x3d71}}; /* 1e-12 */
static const BBNum bb_nresabs={{0xed8d,0xa0b5,0xc6f7,0x3eb0}}; /* 1e-6 mA */
static const BBNum bb_nresrel={{0xaf48,0x9abc,0xd7f2,0x3e7a}}; /* 1e-7 */
static const BBNum bb_nvolt_tol={{0xed8d,0xa0b5,0xc6f7,0x3eb0}}; /* 1uV */
static const BBByte bb_squad[12]={1,2,3,4,5,6,9,10,8,12,13,11};
static const BBByte bb_sinv[12]={1,2,3,4,5,6,9,8,11,10,13,12};

static BBNum *ovl_bb_scell(unsigned r,unsigned c)
{
    unsigned z;
    z=(r<<4)+(r<<3)+r;
    return &bb_sm[z+c];
}

static void ovl_bb_snc(BBNum *d,const BBNum *s)
{ bb_copy(d,s,sizeof(BBNum)); }

static void ovl_bb_snabs(BBNum *d,const BBNum *s)
{
    ovl_bb_snc(d,s);
    d->w[3]&=32767;
}

static unsigned ovl_bb_snet(const BBPart *p,unsigned pin)
{
    unsigned h;
    h=bb_pin(p,pin);
    if(h>=BB_HOLES) return BB_NNET;
    return bb_hnet[h];
}

static void ovl_bb_sfail(unsigned code,unsigned part,unsigned net)
{
    bb_sdiag=code; bb_sdiagpart=part; bb_sdiagnet=net;
}

static int ovl_bb_sfix(unsigned net,int mv,unsigned kind,unsigned id)
{
    if(net>=bb_nnet) { ovl_bb_sfail(BB_EDISCONNECTED,id,net); return 0; }
    if(bb_sbase[net]>=0 && bb_sbase[net]!=mv) {
        if((bb_sbase[net]==0 && kind==BB_POWER) ||
           (mv==0 && bb_sfixed[net]==BB_POWER))
            ovl_bb_sfail(BB_ESHORT,id,net);
        else if(kind==BB_POWER && bb_sfixed[net]==BB_POWER)
            ovl_bb_sfail(BB_ECONFLICT,id,net);
        else ovl_bb_sfail(BB_ECONTENTION,id,net);
        return 0;
    }
    bb_sbase[net]=mv; bb_sfixed[net]=(BBByte)kind;
    return 1;
}

static void ovl_bb_sqpush(unsigned g)
{
    if(g<bb_sngate && !bb_sinq[g]) {
        if(bb_sqcount==64) { ovl_bb_sfail(BB_EOSCILLATE,0,BB_NNET); return; }
        bb_sq[bb_sqtail]=(BBByte)g; bb_sqtail=(bb_sqtail+1)&63;
        ++bb_sqcount; bb_sinq[g]=1;
    }
}

static void ovl_bb_sqnet(unsigned net)
{
    unsigned i;
    for(i=0;i<bb_sngate;++i)
        if(bb_sg[i].a==net || bb_sg[i].b==net) ovl_bb_sqpush(i);
}

/* Resolve equal ideal drivers; X remains X, Z contributes no constraint. */
static int ovl_bb_sresolve(unsigned net)
{
    unsigned i,d,v,unknown;
    int old;
    old=bb_slogic[net]; v=BB_Z; unknown=bb_smanual[net]==BB_X;
    if(bb_sbase[net]>=0) v=(bb_sbase[net]>0)?BB_HIGH:BB_LOW;
    for(i=0;i<bb_sngate;++i) if(bb_sg[i].out==net) {
        d=bb_sg[i].drive;
        if(d==BB_Z) continue;
        if(d==BB_X) { unknown=1; continue; }
        if(v==BB_Z) v=d;
        else if(v!=d) {
            ovl_bb_sfail(BB_ECONTENTION,bb_doc.part[bb_sg[i].owner].id,net);
            return 0;
        }
    }
    if(unknown) v=BB_X;
    bb_slogic[net]=(BBByte)v;
    if(old!=(int)v) ovl_bb_sqnet(net);
    return 1;
}

static unsigned ovl_bb_sinput(unsigned net)
{
    int v;
    unsigned d;
    if(net>=bb_nnet) return BB_Z;
    d=bb_slogic[net];
    if(d!=BB_Z) return d;
    if(!bb_sanalog[net]) return BB_Z;
    v=bb_svm[net];
    if(v<0) return BB_X;
    if((unsigned)v<=bb_ssupplymv*3u/10) return BB_LOW;
    if((unsigned)v>=bb_ssupplymv*7u/10) return BB_HIGH;
    return BB_X;
}

static unsigned ovl_bb_sgatevalue(const BBSGate *g)
{
    unsigned a,b,d;
    a=ovl_bb_sinput(g->a); b=ovl_bb_sinput(g->b);
    if(g->op==BB_HC04) {
        if(a==BB_LOW) return BB_HIGH;
        if(a==BB_HIGH) return BB_LOW;
        return BB_X;
    }
    if(g->op==BB_HC00 || g->op==BB_HC08) {
        if(a==BB_LOW || b==BB_LOW) d=BB_LOW;
        else if(a==BB_HIGH && b==BB_HIGH) d=BB_HIGH;
        else d=BB_X;
        if(g->op==BB_HC00 && d<BB_X) d=1-d;
        return d;
    }
    if(g->op==BB_HC32) {
        if(a==BB_HIGH || b==BB_HIGH) return BB_HIGH;
        if(a==BB_LOW && b==BB_LOW) return BB_LOW;
        return BB_X;
    }
    if(a<BB_X && b<BB_X) return a^b;
    return BB_X;
}

static int ovl_bb_saddbranch(unsigned a,unsigned b,unsigned owner,unsigned kind,
                          unsigned value,unsigned scale)
{
    BBSBranch *x;
    if(bb_snbranch>=BB_S_BRANCHES) {
        ovl_bb_sfail(BB_ECAPACITY,bb_doc.part[owner].id,BB_NNET); return 0;
    }
    if(a>=bb_nnet || b>=bb_nnet) {
        ovl_bb_sfail(BB_EDISCONNECTED,bb_doc.part[owner].id,BB_NNET); return 0;
    }
    x=&bb_sb[bb_snbranch++];
    x->a=a; x->b=b; x->owner=owner; x->kind=kind;
    x->value=value; x->scale=scale; x->aux=0; x->reserved=0;
    ovl_bb_snc(&x->g,&bb_n0); ovl_bb_snc(&x->e,&bb_n0);
    return 1;
}

static int ovl_bb_saddgate(const BBPart *p,unsigned owner,unsigned nth)
{
    BBSGate *g;
    unsigned n;
    if(bb_sngate==BB_S_GATES) { ovl_bb_sfail(BB_ECAPACITY,p->id,0); return 0; }
    g=&bb_sg[bb_sngate++]; n=nth+nth;
    if(p->type==BB_HC04) {
        g->a=(BBByte)ovl_bb_snet(p,bb_sinv[n]); g->b=BB_NNET;
        g->out=(BBByte)ovl_bb_snet(p,bb_sinv[n+1]);
    } else {
        n=nth+nth+nth;
        g->a=(BBByte)ovl_bb_snet(p,bb_squad[n]);
        g->b=(BBByte)ovl_bb_snet(p,bb_squad[n+1]);
        g->out=(BBByte)ovl_bb_snet(p,bb_squad[n+2]);
    }
    g->op=(BBByte)p->type; g->owner=owner;
    g->drive=BB_Z; g->reserved=0;
    return 1;
}

static void ovl_bb_sedge(BBByte *adj,unsigned a,unsigned b)
{
    if(a<bb_nnet && b<bb_nnet)
        adj[(a<<3)+(b>>3)]|=(BBByte)(1u<<(b&7));
}

static int ovl_bb_sedgehas(const BBByte *adj,unsigned a,unsigned b)
{ return (adj[(a<<3)+(b>>3)]&(1u<<(b&7)))!=0; }

static void ovl_bb_sbeginstep(void)
{
    bb_sdiag=0; bb_sdiagpart=0; bb_sdiagnet=BB_NNET; bb_num_error=0;
    bb_snextlo=(BBWord)(bb_result.tlo+bb_doc.dt);
    bb_snexthi=bb_result.thi;
    if(bb_snextlo<bb_result.tlo) ++bb_snexthi;
    bb_sevents=0; bb_sledpasses=0; bb_sclocked=bb_sclockquantized;
    bb_si=0; bb_ssub=0; bb_sphase=BB_S_BEGIN;
}

static void ovl_bb_sbeginone(void)
{
    unsigned i;
    if(bb_si==bb_doc.np) {
        bb_si=0; bb_ssub=0; bb_sphase=BB_S_SEED; return;
    }
    i=bb_si++;
    bb_swlatch[i]=bb_slatch[i]; bb_swclock[i]=bb_sclock[i];
    /* Fresh candidates discover newly conducting coupled diode paths, e.g.
     * a capacitor charging into two series LEDs. No candidate is published
     * until the bounded active-set solve has rejected zero-current clamps. */
    if(bb_doc.part[i].type==BB_LED) bb_sled[i]=1;
    if(bb_doc.part[i].type==BB_CLOCK) {
        bb_swclock[i]=(BBWord)(bb_swclock[i]+bb_doc.dt);
        if(bb_swclock[i]>=bb_doc.part[i].value)
            bb_swclock[i]%=bb_doc.part[i].value;
    }
}

void bb_scope_reset(void)
{
    bb_zero(&bb_scope,sizeof(bb_scope));
    bb_scope_lastlo=0; bb_scope_lasthi=0; bb_scope_lastedge=BB_X;
    bb_scope_lastedge_b=BB_X;
    bb_scope_riselo=0; bb_scope_risehi=0; bb_scope_falllo=0;
    bb_scope_fallhi=0; bb_scope_have_rise=0; bb_scope_have_fall=0;
    bb_scope_post=0; bb_scope_mode=bb_doc.trigger;
}

void bb_sim_reset(void)
{
    unsigned i;
    bb_sphase=BB_S_IDLE; bb_sready=0; bb_shasled=0; bb_sclockquantized=0;
    bb_sluready=0; bb_slun=0; bb_sreuse=0;
    bb_zero(bb_scap,sizeof(bb_scap)); bb_zero(bb_scapnext,sizeof(bb_scapnext));
    bb_zero(bb_slatch,sizeof(bb_slatch));
    bb_zero(bb_sclock,sizeof(bb_sclock)); bb_zero(bb_sanalog,sizeof(bb_sanalog));
    bb_zero(&bb_result,sizeof(bb_result));
    for(i=0;i<BB_NETS;++i) {
        bb_result.mv[i]=BB_MV_FLOAT; bb_result.logic[i]=BB_Z;
        bb_svm[i]=BB_MV_FLOAT;
    }
    for(i=0;i<BB_PARTS;++i) {
        bb_result.ua[i]=BB_UA_INVALID;
        bb_sled[i]=1;
    }
    bb_result.net=BB_NNET; bb_valid=0; bb_num_error=0;
    bb_scope_reset();
}

int ovl_bb_sim_start(int reset)
{
    if(reset || bb_recompile || !bb_sready) {
        if(!bb_validate(&bb_doc)) return 0;
        if(!bb_compile()) return 0;
        bb_sim_reset();
        bb_recompile=0;
        bb_sphase=BB_S_SCAN; bb_si=0; bb_ssub=0;
        bb_snbranch=0; bb_sngate=0; bb_sground=BB_NNET;
        bb_ssupply=BB_NNET; bb_ssupplymv=5000;
        bb_sdiag=0; bb_sdiagpart=0; bb_sdiagnet=BB_NNET;
        bb_zero(bb_sadj,sizeof(bb_sadj)); bb_zero(bb_sdeps,sizeof(bb_sdeps));
        bb_shasled=0;
    } else if(bb_sphase==BB_S_IDLE) ovl_bb_sbeginstep();
    return 1;
}

/* First scan is integer-only, one object/gate per unit. Numeric parameters
 * are compiled later, one scalar operation per stage. */
static void ovl_bb_sscan(void)
{
    BBPart *p;
    unsigned a,b,c,t,g;
    if(bb_si==bb_doc.np) {
        if(bb_sground==BB_NNET) { ovl_bb_sfail(BB_ENOGROUND,0,BB_NNET); return; }
        if(bb_ssupply==BB_NNET) { ovl_bb_sfail(BB_EPOWER,0,BB_NNET); return; }
        bb_si=0; bb_ssub=0; bb_sphase=BB_S_G; return;
    }
    p=&bb_doc.part[bb_si]; t=p->type;
    a=ovl_bb_snet(p,1); b=ovl_bb_snet(p,2); c=ovl_bb_snet(p,3);
    if(t>=BB_HC00 && t<=BB_HC86) {
        g=(t==BB_HC04)?6:4;
        if(bb_ssub<g) { ovl_bb_saddgate(p,bb_si,bb_ssub++); return; }
        bb_ssub=0;
    } else if(t==BB_GROUND) bb_sground=a;
    else if(t==BB_POWER) {
        if(bb_ssupply!=BB_NNET) {
            ovl_bb_sfail(BB_ESUPPLIES,p->id,a); return;
        }
        bb_ssupply=a; bb_ssupplymv=p->value;
    }
    else if(t==BB_RES || t==BB_LED || t==BB_CAP) {
        if(t==BB_LED) bb_shasled=1;
        ovl_bb_saddbranch(a,b,bb_si,t,p->value,0);
    }
    else if(t==BB_POT) {
        if(p->param>0) ovl_bb_saddbranch(a,c,bb_si,BB_POT,p->value,p->param);
        if(p->param<1000) ovl_bb_saddbranch(c,b,bb_si,BB_POT,p->value,1000-p->param);
    } else if(t==BB_CLOCK) {
        bb_sclockhigh[bb_si]=bb_umuldiv(p->value,p->param,1000);
        if(bb_sclockhigh[bb_si]==0 || bb_sclockhigh[bb_si]>=p->value)
            bb_sclockquantized=1;
    }
    ++bb_si;
}

/* Compile branch G and initial cap state. V,mS,mA avoid wide integer units. */
static void ovl_bb_sconductance(void)
{
    BBSBranch *x;
    BBPart *p;
    unsigned a,b,t;
    if(bb_ssub>=100) {
        if(bb_ssub==100) {
            if(bb_si==bb_nnet) { bb_si=0; bb_ssub=101; return; }
            bb_sbase[bb_si++]=-1; return;
        }
        if(bb_ssub==101) {
            if(bb_si==bb_doc.np) { bb_si=0; bb_ssub=102; return; }
            p=&bb_doc.part[bb_si++];
            if(p->type==BB_GROUND) bb_sbase[ovl_bb_snet(p,1)]=0;
            if(p->type==BB_POWER) bb_sbase[ovl_bb_snet(p,1)]=p->value;
            return;
        }
        if(bb_ssub==102) {
            if(bb_si==bb_snbranch) { bb_si=0; bb_ssub=103; return; }
            a=bb_sb[bb_si].a; b=bb_sb[bb_si++].b;
            if(a!=b && bb_sbase[a]<0 && bb_sbase[b]<0) {
                ovl_bb_sedge(&bb_sadj[0][0],a,b); ovl_bb_sedge(&bb_sadj[0][0],b,a);
            }
            return;
        }
        if(bb_si==bb_sngate) {
            bb_sbridgegate=0; bb_ssub=0; bb_sphase=BB_S_BRIDGE; return;
        }
        ovl_bb_sedge(&bb_sdeps[0][0],bb_sg[bb_si].a,bb_sg[bb_si].out);
        ovl_bb_sedge(&bb_sdeps[0][0],bb_sg[bb_si].b,bb_sg[bb_si].out);
        ++bb_si; return;
    }
    if(bb_si==bb_snbranch) { bb_si=0; bb_ssub=100; return; }
    x=&bb_sb[bb_si]; p=&bb_doc.part[x->owner]; t=x->kind;
    if(t==BB_LED) {
        ovl_bb_snc(&x->g,&bb_n100); ovl_bb_snc(&x->e,&bb_n2);
        ++bb_si; bb_ssub=0; return;
    }
    if(bb_ssub==0) { bb_nuint(&bb_st0,x->value); ++bb_ssub; return; }
    if(t==BB_CAP) {
        if(bb_ssub==1) bb_nuint(&bb_st1,bb_doc.dt);
        else if(bb_ssub==2) bb_nop(&bb_st1,&bb_st1,&bb_n100,BB_NMUL);
        else if(bb_ssub==3) bb_nop(&x->g,&bb_st0,&bb_st1,BB_NDIV);
        else if(bb_ssub==4) bb_nuint(&bb_st0,p->param);
        else if(bb_ssub==5) bb_nop(&bb_scap[x->owner],&bb_st0,&bb_n1000,BB_NDIV);
        else { ++bb_si; bb_ssub=0; return; }
    } else {
        if(bb_ssub==1) bb_nop(&x->g,&bb_n100,&bb_st0,BB_NDIV);
        else if(t==BB_POT && bb_ssub==2) bb_nop(&x->g,&x->g,&bb_n1000,BB_NMUL);
        else if(t==BB_POT && bb_ssub==3) bb_nuint(&bb_st1,x->scale);
        else if(t==BB_POT && bb_ssub==4) bb_nop(&x->g,&x->g,&bb_st1,BB_NDIV);
        else { ++bb_si; bb_ssub=0; return; }
    }
    ++bb_ssub;
}

/* Reject a dependency cycle containing both a HC edge and passive edge.
 * The 555 feedback circuit is not a HC edge and is intentionally supported.
 * BFS state bit6 records that a nonideal analog edge has been traversed. */
static void ovl_bb_sbridge(void)
{
    unsigned x,n,k,j,pass;
    if(bb_sbridgegate==bb_sngate) {
        bb_sready=1; ovl_bb_sbeginstep(); return;
    }
    if(bb_ssub==0) {
        bb_zero(bb_sseen,sizeof(bb_sseen));
        x=bb_sg[bb_sbridgegate].out; bb_sseen[x]=1;
        bb_ssearch[0]=(BBByte)x; bb_sbh=0; bb_sbt=1; bb_sbe=0;
        bb_ssub=1; return;
    }
    if(bb_sbh==bb_sbt) { ++bb_sbridgegate; bb_ssub=0; return; }
    x=bb_ssearch[bb_sbh]; n=x&63; pass=x&64; j=bb_sbe++;
    if(bb_sbe==bb_nnet) { bb_sbe=0; ++bb_sbh; }
    if(ovl_bb_sedgehas(&bb_sadj[0][0],n,j)) k=j|64;
    else if(ovl_bb_sedgehas(&bb_sdeps[0][0],n,j)) k=j|pass;
    else return;
    if((k&64) && ((k&63)==bb_sg[bb_sbridgegate].a ||
        (k&63)==bb_sg[bb_sbridgegate].b)) {
        ovl_bb_sfail(BB_EBRIDGE,bb_doc.part[bb_sg[bb_sbridgegate].owner].id,k&63);
        return;
    }
    if(!bb_sseen[k]) {
        bb_sseen[k]=1;
        if(bb_sbt<128) bb_ssearch[bb_sbt++]=(BBByte)k;
    }
}

/* Seed known rails/inputs/timer pins. Each source conversion is separate. */
static void ovl_bb_sseed(void)
{
    BBPart *p;
    unsigned i,a,b,t,d;
    if(bb_ssub==0) {
        if(bb_si<bb_nnet) {
            i=bb_si++; bb_sbase[i]=-1; bb_sfixed[i]=0; bb_slogic[i]=BB_Z;
            bb_sunk[i]=BB_NNET; bb_smanual[i]=BB_Z;
            bb_sconn[i]=(BBByte)i; bb_sanchor[i]=0; return;
        }
        bb_sqhead=0; bb_sqtail=0; bb_sqcount=0;
        bb_zero(bb_sinq,sizeof(bb_sinq)); bb_si=0; bb_ssub=1; return;
    }
    if(bb_ssub==1) {
        if(bb_si==bb_doc.np) { bb_si=0; bb_ssub=2; return; }
        p=&bb_doc.part[bb_si++];
        if(p->type==BB_GROUND) ovl_bb_sfix(ovl_bb_snet(p,1),0,BB_GROUND,p->id);
        return;
    }
    if(bb_ssub==2) {
        if(bb_si==bb_doc.np) { bb_si=0; bb_ssub=3; return; }
        p=&bb_doc.part[bb_si++]; a=ovl_bb_snet(p,1); b=ovl_bb_snet(p,2);
        if(p->type==BB_POWER) {
            if(b>=bb_nnet || bb_sbase[b]!=0) ovl_bb_sfail(BB_ENOGROUND,p->id,b);
            else ovl_bb_sfix(a,p->value,BB_POWER,p->id);
        } else if(p->type==BB_INPUT) {
            if(p->param<BB_X) ovl_bb_sfix(a,p->param?bb_ssupplymv:0,BB_INPUT,p->id);
            else { bb_slogic[a]=(BBByte)p->param; bb_smanual[a]=(BBByte)p->param; }
        } else if(p->type==BB_CLOCK) {
            d=bb_swclock[bb_si-1]<bb_sclockhigh[bb_si-1];
            ovl_bb_sfix(a,d?bb_ssupplymv:0,BB_CLOCK,p->id);
            if(bb_doc.dt>=p->value || bb_doc.dt>bb_sclockhigh[bb_si-1] ||
               bb_doc.dt>p->value-bb_sclockhigh[bb_si-1]) bb_sclocked=1;
        }
        return;
    }
    if(bb_ssub==3) {
        if(bb_si==bb_doc.np) { bb_si=0; bb_ssub=4; return; }
        p=&bb_doc.part[bb_si]; t=p->type;
        if(t>=BB_HC00 && t<=BB_HC86) {
            a=ovl_bb_snet(p,14); b=ovl_bb_snet(p,7);
            if(bb_sbase[a]<4500 || bb_sbase[a]>6000 || bb_sbase[b]!=0)
                ovl_bb_sfail(BB_EPOWER,p->id,a);
        } else if(t==BB_555) {
            a=ovl_bb_snet(p,8); b=ovl_bb_snet(p,1);
            if(bb_sbase[a]<4500 || bb_sbase[a]>6000 || bb_sbase[b]!=0)
                ovl_bb_sfail(BB_EPOWER,p->id,a);
            else {
                ovl_bb_sfix(ovl_bb_snet(p,3),bb_swlatch[bb_si]?bb_sbase[a]:0,BB_555,p->id);
                if(!bb_swlatch[bb_si]) ovl_bb_sfix(ovl_bb_snet(p,7),0,BB_555,p->id);
                /* Ideal control reference, rounded only for fixed seed. */
                ovl_bb_sfix(ovl_bb_snet(p,5),(bb_sbase[a]*2)/3,BB_555,p->id);
            }
        }
        ++bb_si; return;
    }
    if(bb_ssub==4) {
        if(bb_si==bb_nnet) { bb_si=0; bb_ssub=5; return; }
        i=bb_si++;
        if(bb_sbase[i]>=0) bb_slogic[i]=(BBByte)(bb_sbase[i]?BB_HIGH:BB_LOW);
        ovl_bb_sresolve(i);
        return;
    }
    if(bb_ssub==5) {
        if(bb_si==bb_sngate) {
            bb_srebuild=0; bb_sg_after=0; bb_sphase=BB_S_GATE; return;
        }
        ovl_bb_sqpush(bb_si++); return;
    }
}

static void ovl_bb_sgates(void)
{
    unsigned g,d;
    if(!bb_sqcount) {
        bb_si=0; bb_ssub=0;
        if(bb_srebuild && bb_shasled) bb_sphase=BB_S_REARM;
        else if(bb_sg_after) {
            if(bb_srebuild) bb_sphase=BB_S_SEED;
            else bb_sphase=BB_S_CURRENT;
        } else bb_sphase=BB_S_CLASSIFY;
        return;
    }
    if(++bb_sevents>1024) { ovl_bb_sfail(BB_EOSCILLATE,0,BB_NNET); return; }
    g=bb_sq[bb_sqhead]; bb_sqhead=(bb_sqhead+1)&63;
    --bb_sqcount; bb_sinq[g]=0; d=ovl_bb_sgatevalue(&bb_sg[g]);
    if(d!=bb_sg[g].drive) {
        bb_sg[g].drive=(BBByte)d; bb_srebuild=1;
        ovl_bb_sresolve(bb_sg[g].out);
    }
}

/* A real ideal-driver change can start a previously dark series LED path.
 * Re-arm only between nonlinear/digital solves, one object per slice unit. */
static void ovl_bb_srearm(void)
{
    unsigned i;
    if(bb_si==bb_doc.np) {
        bb_si=0; bb_ssub=0; bb_sphase=BB_S_SEED; return;
    }
    i=bb_si++;
    if(bb_doc.part[i].type==BB_LED) bb_sled[i]=1;
}

/* Build unknown map after resolving all ideal drivers. X does not stamp0V. */
static unsigned ovl_bb_sroot(unsigned n)
{
    unsigned k,j;
    k=n;
    while(bb_sconn[k]!=k) k=bb_sconn[k];
    while(bb_sconn[n]!=n) { j=bb_sconn[n]; bb_sconn[n]=(BBByte)k; n=j; }
    return k;
}

/* The bridge search is compile-only. Reuse its first64 bytes as a separate
 * LED-only union forest while checking zero-current reference ambiguity. */
static unsigned ovl_bb_slroot(unsigned n)
{
    unsigned k,j;
    k=n;
    while(bb_ssearch[k]!=k) k=bb_ssearch[k];
    while(bb_ssearch[n]!=n) { j=bb_ssearch[n]; bb_ssearch[n]=(BBByte)k; n=j; }
    return k;
}

static void ovl_bb_sclassify(void)
{
    unsigned i,a,b,d;
    BBSBranch *x;
    if(bb_ssub==0) {
        if(bb_si==0) bb_sreuse=bb_sluready;
        if(bb_si==bb_sngate) { bb_si=0; bb_ssub=7; return; }
        i=bb_si++; a=bb_sg[i].out; d=bb_slogic[a];
        if(d<BB_X) {
            if(bb_sbase[a]>=0 && bb_sbase[a]!=(d?bb_ssupplymv:0))
                ovl_bb_sfail(BB_ECONTENTION,bb_doc.part[bb_sg[i].owner].id,a);
            else bb_sbase[a]=d?bb_ssupplymv:0;
        }
        return;
    }
    if(bb_ssub==7) {
        /* Only now discard participation from the last analog solution. */
        if(bb_si==bb_nnet) { bb_si=0; bb_ssub=bb_shasled?12:1; return; }
        i=bb_si++; bb_sanalog[i]=0;
        bb_sconn[i]=(BBByte)i; bb_sanchor[i]=0; return;
    }
    if(bb_ssub==12) {
        /* A genuine R/C reference reaches a fixed node without relying on
         * any LED boundary. Preserve these flags in compile-only scratch. */
        if(bb_si==bb_snbranch) { bb_si=0; bb_ssub=13; return; }
        x=&bb_sb[bb_si++];
        if(x->kind==BB_LED) return;
        a=ovl_bb_sroot(x->a); b=ovl_bb_sroot(x->b);
        if(a<b) bb_sconn[b]=(BBByte)a;
        else if(b<a) bb_sconn[a]=(BBByte)b;
        return;
    }
    if(bb_ssub==13) {
        if(bb_si==bb_nnet) { bb_si=0; bb_ssub=14; return; }
        i=bb_si++;
        if(bb_sbase[i]>=0) bb_sanchor[ovl_bb_sroot(i)]=1;
        return;
    }
    if(bb_ssub==14) {
        if(bb_si==bb_nnet) { bb_si=0; bb_ssub=15; return; }
        i=bb_si++; bb_sseen[i]=bb_sanchor[ovl_bb_sroot(i)]; return;
    }
    if(bb_ssub==15) {
        if(bb_si==bb_nnet) { bb_si=0; bb_ssub=8; return; }
        i=bb_si++; bb_sconn[i]=(BBByte)i; bb_sanchor[i]=0; return;
    }
    if(bb_ssub==8) {
        /* Fixed nodes are energy boundaries, never traversal shortcuts. A
         * component joined only to one rail cannot power an LED. Stored
         * capacitor conditions exempt the component from this pruning. */
        if(bb_si==bb_snbranch) { bb_si=0; bb_ssub=9; return; }
        x=&bb_sb[bb_si++];
        if((x->kind!=BB_LED || bb_sled[x->owner]) &&
           bb_sbase[x->a]<0 && bb_sbase[x->b]<0) {
            a=ovl_bb_sroot(x->a); b=ovl_bb_sroot(x->b);
            if(a<b) bb_sconn[b]=(BBByte)a;
            else if(b<a) bb_sconn[a]=(BBByte)b;
        }
        return;
    }
    if(bb_ssub==9) {
        if(bb_si==bb_snbranch) { bb_si=0; bb_ssub=10; bb_schanged=0; return; }
        x=&bb_sb[bb_si++];
        if(x->kind==BB_LED && !bb_sled[x->owner]) return;
        if(bb_sbase[x->a]<0) {
            a=ovl_bb_sroot(x->a);
            if(x->kind==BB_CAP) bb_sanchor[a]|=4;
            if(bb_sbase[x->b]>=0) bb_sanchor[a]|=bb_sbase[x->b]?2:1;
        }
        if(bb_sbase[x->b]<0) {
            b=ovl_bb_sroot(x->b);
            if(x->kind==BB_CAP) bb_sanchor[b]|=4;
            if(bb_sbase[x->a]>=0) bb_sanchor[b]|=bb_sbase[x->a]?2:1;
        }
        return;
    }
    if(bb_ssub==10) {
        if(bb_si==bb_snbranch) {
            bb_si=0; bb_ssub=11;
            if(bb_schanged) {
                if(++bb_sledpasses>12) ovl_bb_sfail(BB_ECONVERGE,0,BB_NNET);
                else { bb_ssub=0; bb_sphase=BB_S_SEED; }
            }
            return;
        }
        x=&bb_sb[bb_si++];
        if(x->kind==BB_LED && bb_sled[x->owner] &&
           (bb_sbase[x->a]<0 || bb_sbase[x->b]<0)) {
            a=ovl_bb_sroot(bb_sbase[x->a]<0?x->a:x->b);
            d=bb_sanchor[a];
            if(!(d&4) && (d&3)!=3) { bb_sled[x->owner]=0; bb_schanged=1; }
        }
        return;
    }
    if(bb_ssub==11) {
        if(bb_si==bb_nnet) { bb_si=0; bb_ssub=1; return; }
        i=bb_si++; bb_sconn[i]=(BBByte)i; bb_sanchor[i]=0; return;
    }
    if(bb_ssub==1) {
        if(bb_si==bb_snbranch) { bb_si=0; bb_ssub=4; bb_snu=0; return; }
        x=&bb_sb[bb_si++];
        if(x->kind!=BB_LED || bb_sled[x->owner]) {
            bb_sanalog[x->a]=1; bb_sanalog[x->b]=1;
            a=ovl_bb_sroot(x->a); b=ovl_bb_sroot(x->b);
            if(a<b) bb_sconn[b]=(BBByte)a;
            else if(b<a) bb_sconn[a]=(BBByte)b;
        }
        if(x->kind==BB_CAP) ovl_bb_snc(&x->e,&bb_scap[x->owner]);
        return;
    }
    if(bb_ssub==4) {
        if(bb_si==bb_nnet) { bb_si=0; bb_ssub=2; return; }
        i=bb_si++;
        if(bb_sbase[i]>=0) bb_sanchor[ovl_bb_sroot(i)]=1;
        return;
    }
    if(bb_ssub==2) {
        if(bb_si==bb_nnet) {
            if(bb_snu!=bb_slun) bb_sreuse=0;
            bb_si=0; bb_ssub=5; return;
        }
        i=bb_si++;
        if(bb_sanalog[i] && bb_sbase[i]<0) {
            a=ovl_bb_sroot(i);
            if(!(bb_sanchor[a]&1)) { ovl_bb_sfail(BB_EFLOAT,0,i); return; }
            if(bb_snu==BB_UNKNOWNS) { ovl_bb_sfail(BB_ECAPACITY,0,i); return; }
            if(bb_snu>=bb_slun || bb_sunet[bb_snu]!=i) bb_sreuse=0;
            bb_sunk[i]=(BBByte)bb_snu; bb_sunet[bb_snu++]=(BBByte)i;
        }
        if(bb_sbase[i]>=0) { bb_nset(&bb_sv[i],bb_sbase[i]); bb_ssub=3; }
        else if(!bb_sanalog[i]) ovl_bb_snc(&bb_sv[i],&bb_n0);
        return;
    }
    if(bb_ssub==5) {
        if(bb_si<bb_snbranch) {
            x=&bb_sb[bb_si++];
            if(x->kind==BB_LED) {
                if(x->aux!=bb_sled[x->owner]) bb_sreuse=0;
                x->aux=bb_sled[x->owner];
            }
            return;
        }
        bb_zero(bb_sabs,sizeof(bb_sabs)); /* RHS now; incident sums after solve */
        if(!bb_sreuse) {
            bb_zero(bb_sm,sizeof(bb_sm)); bb_zero(bb_srows,sizeof(bb_srows));
            bb_sluready=0;
        }
        bb_si=0; bb_ssub=6; return;
    }
    if(bb_ssub==6) {
        if(!bb_sreuse && bb_si<bb_snu) { bb_sperm[bb_si]=(BBByte)bb_si; ++bb_si; return; }
        bb_si=0; bb_ssub=0; bb_sphase=BB_S_STAMP; return;
    }
    /* The previous unit converted millivolts; normalize to volts. */
    i=bb_si-1; bb_nop(&bb_sv[i],&bb_sv[i],&bb_n1000,BB_NDIV); bb_ssub=2;
}

/* Stamp I(a->b)=G*(Va-Vb-E). Every state does <=one numeric op. */
static void ovl_bb_sstamp(void)
{
    BBSBranch *x;
    unsigned a,b,ua,ub;
    if(bb_si==bb_snbranch) {
        bb_si=0; bb_ssub=0; bb_sphase=BB_S_RHS; return;
    }
    x=&bb_sb[bb_si]; a=x->a; b=x->b;
    ua=bb_sunk[a]; ub=bb_sunk[b];
    if(x->kind==BB_LED && !bb_sled[x->owner]) { ++bb_si; bb_ssub=0; return; }
    if(a==b) { ++bb_si; bb_ssub=0; return; }
    if(bb_ssub==0) bb_nop(&bb_st0,&x->g,&x->e,BB_NMUL);
    else if(bb_ssub==1 && ua!=BB_NNET && !bb_sreuse)
        bb_nop(ovl_bb_scell(ua,ua),ovl_bb_scell(ua,ua),&x->g,BB_NADD);
    else if(bb_ssub==2 && ua!=BB_NNET && ub!=BB_NNET && !bb_sreuse)
        bb_nop(ovl_bb_scell(ua,ub),ovl_bb_scell(ua,ub),&x->g,BB_NSUB);
    else if(bb_ssub==3 && ua!=BB_NNET && ub==BB_NNET)
        bb_nop(&bb_st1,&x->g,&bb_sv[b],BB_NMUL);
    else if(bb_ssub==4 && ua!=BB_NNET && ub==BB_NNET)
        bb_nop(&bb_sabs[ua],&bb_sabs[ua],&bb_st1,BB_NADD);
    else if(bb_ssub==5 && ua!=BB_NNET)
        bb_nop(&bb_sabs[ua],&bb_sabs[ua],&bb_st0,BB_NADD);
    else if(bb_ssub==6 && ub!=BB_NNET && !bb_sreuse)
        bb_nop(ovl_bb_scell(ub,ub),ovl_bb_scell(ub,ub),&x->g,BB_NADD);
    else if(bb_ssub==7 && ub!=BB_NNET && ua!=BB_NNET && !bb_sreuse)
        bb_nop(ovl_bb_scell(ub,ua),ovl_bb_scell(ub,ua),&x->g,BB_NSUB);
    else if(bb_ssub==8 && ub!=BB_NNET && ua==BB_NNET)
        bb_nop(&bb_st1,&x->g,&bb_sv[a],BB_NMUL);
    else if(bb_ssub==9 && ub!=BB_NNET && ua==BB_NNET)
        bb_nop(&bb_sabs[ub],&bb_sabs[ub],&bb_st1,BB_NADD);
    else if(bb_ssub==10 && ub!=BB_NNET)
        bb_nop(&bb_sabs[ub],&bb_sabs[ub],&bb_st0,BB_NSUB);
    else if(bb_ssub==11) { ++bb_si; bb_ssub=0; return; }
    ++bb_ssub;
}

/* Reuse LU only when unknown-net identity and every nonlinear conductance
 * agree. Fixed source levels change RHS, not the coefficient matrix. */
static void ovl_bb_srhs(void)
{
    unsigned j;
    if(bb_si<bb_snu) {
        j=bb_sreuse?bb_sperm[bb_si]:bb_si;
        ovl_bb_snc(ovl_bb_scell(bb_si,bb_snu),&bb_sabs[j]); ++bb_si; return;
    }
    bb_si=0; bb_sj=0; bb_ssub=0;
    bb_sphase=bb_sreuse?BB_S_FORWARD:BB_S_SCALE;
}

static void ovl_bb_sforward(void)
{
    if(bb_si==bb_snu) {
        bb_si=bb_snu; bb_ssub=0; bb_sphase=BB_S_BACK; return;
    }
    if(bb_sj==bb_si) { ++bb_si; bb_sj=0; bb_ssub=0; return; }
    if(bb_ssub==0) {
        bb_nop(&bb_st0,ovl_bb_scell(bb_si,bb_sj),ovl_bb_scell(bb_sj,bb_snu),BB_NMUL);
        bb_ssub=1;
    } else {
        bb_nop(ovl_bb_scell(bb_si,bb_snu),ovl_bb_scell(bb_si,bb_snu),&bb_st0,BB_NSUB);
        ++bb_sj; bb_ssub=0;
    }
}

/* Retain original row maximum for relative pivot checking. */
static void ovl_bb_sscale(void)
{
    if(bb_si==bb_snu) {
        bb_sk=0; bb_si=0; bb_ssub=0; bb_sphase=BB_S_PIVOT; return;
    }
    ovl_bb_snabs(&bb_st0,ovl_bb_scell(bb_si,bb_ssub));
    if(bb_ncmp(&bb_st0,&bb_srows[bb_si])>0) ovl_bb_snc(&bb_srows[bb_si],&bb_st0);
    if(++bb_ssub==bb_snu) { ++bb_si; bb_ssub=0; }
}

static void ovl_bb_spivot(void)
{
    if(bb_sk==bb_snu) {
        bb_sluready=1; bb_slun=bb_snu;
        bb_si=bb_snu; bb_ssub=0; bb_sphase=BB_S_BACK; return;
    }
    if(bb_ssub==0) {
        bb_sr=bb_sk; bb_si=bb_sk;
        ovl_bb_snc(&bb_sbest,&bb_n0); bb_ssub=1; return;
    }
    if(bb_ssub==1) {
        if(bb_si==bb_snu) { bb_ssub=2; return; }
        ovl_bb_snabs(&bb_st0,ovl_bb_scell(bb_si,bb_sk));
        if(bb_ncmp(&bb_st0,&bb_sbest)>0) {
            ovl_bb_snc(&bb_sbest,&bb_st0); bb_sr=bb_si;
        }
        ++bb_si; return;
    }
    if(bb_ssub==2) {
        if(bb_ncmp(&bb_sbest,&bb_npivot)<=0)
            ovl_bb_sfail(BB_ESINGULAR,0,bb_sunet[bb_sk]);
        bb_ssub=3; return;
    }
    if(bb_ssub==3) {
        bb_nop(&bb_st0,&bb_srows[bb_sr],&bb_nrelpivot,BB_NMUL);
        bb_ssub=4; return;
    }
    if(bb_ncmp(&bb_sbest,&bb_st0)<=0) {
        ovl_bb_sfail(BB_ESINGULAR,0,bb_sunet[bb_sk]); return;
    }
    bb_si=0; bb_sphase=BB_S_SWAP; bb_ssub=0;
}

static void ovl_bb_sswap(void)
{
    if(bb_sr!=bb_sk && bb_si<=bb_snu) {
        ovl_bb_snc(&bb_st0,ovl_bb_scell(bb_sr,bb_si));
        ovl_bb_snc(ovl_bb_scell(bb_sr,bb_si),ovl_bb_scell(bb_sk,bb_si));
        ovl_bb_snc(ovl_bb_scell(bb_sk,bb_si),&bb_st0); ++bb_si; return;
    }
    if(bb_sr!=bb_sk) {
        bb_sj=bb_sperm[bb_sr]; bb_sperm[bb_sr]=bb_sperm[bb_sk];
        bb_sperm[bb_sk]=(BBByte)bb_sj;
        ovl_bb_snc(&bb_st0,&bb_srows[bb_sr]);
        ovl_bb_snc(&bb_srows[bb_sr],&bb_srows[bb_sk]);
        ovl_bb_snc(&bb_srows[bb_sk],&bb_st0);
    }
    bb_si=bb_sk+1; bb_sj=bb_sk+1; bb_ssub=0; bb_sphase=BB_S_ELIM;
}

static void ovl_bb_selim(void)
{
    if(bb_si==bb_snu) { ++bb_sk; bb_ssub=0; bb_sphase=BB_S_PIVOT; return; }
    if(bb_ssub==0) {
        bb_nop(&bb_sfactor,ovl_bb_scell(bb_si,bb_sk),ovl_bb_scell(bb_sk,bb_sk),BB_NDIV);
        ovl_bb_snc(ovl_bb_scell(bb_si,bb_sk),&bb_sfactor); bb_sj=bb_sk+1; bb_ssub=1; return;
    }
    if(bb_sj>bb_snu) { ++bb_si; bb_ssub=0; return; }
    if(bb_ssub==1) {
        bb_nop(&bb_st0,&bb_sfactor,ovl_bb_scell(bb_sk,bb_sj),BB_NMUL); bb_ssub=2;
    } else {
        bb_nop(ovl_bb_scell(bb_si,bb_sj),ovl_bb_scell(bb_si,bb_sj),&bb_st0,BB_NSUB);
        ++bb_sj; bb_ssub=1;
    }
}

static void ovl_bb_sback(void)
{
    if(bb_ssub==0) {
        if(bb_si==0) { bb_si=0; bb_ssub=0; bb_schanged=0;
            bb_scheckstage=0; bb_sphase=BB_S_DEVICES; return; }
        --bb_si; bb_sj=bb_si+1;
        ovl_bb_snc(&bb_ssum,ovl_bb_scell(bb_si,bb_snu)); bb_ssub=1; return;
    }
    if(bb_sj==bb_snu) {
        bb_nop(&bb_sv[bb_sunet[bb_si]],&bb_ssum,ovl_bb_scell(bb_si,bb_si),BB_NDIV);
        bb_ssub=0; return;
    }
    if(bb_ssub==1) {
        bb_nop(&bb_st0,ovl_bb_scell(bb_si,bb_sj),&bb_sv[bb_sunet[bb_sj]],BB_NMUL);
        bb_ssub=2;
    } else {
        bb_nop(&bb_ssum,&bb_ssum,&bb_st0,BB_NSUB); ++bb_sj; bb_ssub=1;
    }
}

static void ovl_bb_svolts(void)
{
    unsigned i;
    if(bb_si==bb_nnet) {
        bb_si=0; bb_ssub=0; bb_schanged=0; bb_scheckstage=1; bb_sphase=BB_S_DEVICES; return;
    }
    i=bb_si;
    if(bb_sbase[i]<0 && !bb_sanalog[i]) {
        bb_svm[i]=(bb_slogic[i]==BB_X)?BB_MV_UNKNOWN:BB_MV_FLOAT;
        ++bb_si; bb_ssub=0; return;
    }
    if(bb_ssub==0) {
        if(bb_ncmp(&bb_sv[i],&bb_n0)<0) {
            ovl_bb_snabs(&bb_st0,&bb_sv[i]); bb_ssub=1;
        } else bb_ssub=2;
        return;
    }
    if(bb_ssub==1) {
        if(bb_ncmp(&bb_st0,&bb_nvolt_tol)>0) ovl_bb_sfail(BB_ENUMERIC,0,i);
        else ovl_bb_snc(&bb_sv[i],&bb_n0);
        bb_ssub=2; return;
    }
    if(bb_ssub==2) {
        bb_nop(&bb_st0,&bb_sv[i],&bb_n6,BB_NSUB); bb_ssub=3; return;
    }
    if(bb_ssub==3) {
        if(bb_ncmp(&bb_st0,&bb_nvolt_tol)>0) ovl_bb_sfail(BB_ENUMERIC,0,i);
        else if((bb_st0.w[3]&32768u)==0) ovl_bb_snc(&bb_sv[i],&bb_n6);
        bb_ssub=4; return;
    }
    if(bb_ssub==4) { bb_nop(&bb_st0,&bb_sv[i],&bb_n1000,BB_NMUL); bb_ssub=5; return; }
    bb_svm[i]=(BBInt)bb_nround(&bb_st0); ++bb_si; bb_ssub=0;
}

static int ovl_bb_scontrolpart(unsigned timer,unsigned other)
{
    BBPart *p,*q;
    unsigned ctl,k,n,a,b;
    p=&bb_doc.part[timer]; q=&bb_doc.part[other]; ctl=ovl_bb_snet(p,5);
    n=bb_pincount(q->type);
    for(k=1;k<=n;++k) if(ovl_bb_snet(q,k)==ctl) {
        if(timer==other && k==5) continue;
        if(q->type!=BB_CAP) return 0;
        a=ovl_bb_snet(q,1); b=ovl_bb_snet(q,2);
        if(a==ctl && bb_sbase[b]==0) continue;
        if(b==ctl && bb_sbase[a]==0) continue;
        return 0;
    }
    return 1;
}

/* Stage0 resolves LED active sets before physical voltage range conversion.
 * Stage1 evaluates actual555 input voltages and the default control policy. */
static void ovl_bb_sdevices(void)
{
    BBPart *p;
    BBSBranch *x;
    unsigned a,b,t,now,i;
    if(bb_scheckstage==0) {
        if(bb_si==bb_doc.np) {
            if(bb_schanged) {
                if(++bb_sledpasses>12) { ovl_bb_sfail(BB_ECONVERGE,0,BB_NNET); return; }
                bb_ssub=0; bb_si=0; bb_sphase=BB_S_SEED; return;
            }
            bb_si=0; bb_ssub=0; bb_sphase=BB_S_VOLTS; return;
        }
        p=&bb_doc.part[bb_si];
        if(p->type!=BB_LED) { ++bb_si; bb_ssub=0; return; }
        a=ovl_bb_snet(p,1); b=ovl_bb_snet(p,2);
        if(bb_ssub==0) {
            /* An OFF diode cannot infer forward bias from an unreferenced
             * terminal's numerical scratch zero. Genuine R/C/fixed references
             * retain participation and can establish a physical voltage. */
            if(!bb_sled[bb_si] &&
               ((bb_sbase[a]<0 && !bb_sanalog[a]) ||
                (bb_sbase[b]<0 && !bb_sanalog[b]))) {
                ++bb_si; return;
            }
            bb_nop(&bb_st0,&bb_sv[a],&bb_sv[b],BB_NSUB); bb_ssub=1; return;
        }
        if(bb_ssub==1) {
            bb_nop(&bb_st0,&bb_st0,&bb_n2,BB_NSUB); bb_ssub=2; return;
        }
        if(bb_ssub==2) {
            ovl_bb_snabs(&bb_st1,&bb_st0);
            if(bb_ncmp(&bb_st1,&bb_nvolt_tol)>0) bb_ssub=7;
            else { bb_sj=0; bb_ssub=3; }
            return;
        }
        if(bb_ssub==3) {
            if(bb_sj==bb_nnet) { bb_sj=0; bb_ssub=4; return; }
            i=bb_sj++; bb_sconn[i]=(BBByte)i; bb_sanchor[i]=0;
            bb_ssearch[i]=(BBByte)i; return;
        }
        if(bb_ssub==4) {
            if(bb_sj==bb_snbranch) { bb_sj=0; bb_ssub=5; return; }
            x=&bb_sb[bb_sj++];
            if(x->kind==BB_LED) {
                if(x->owner==bb_si || !bb_sled[x->owner]) return;
                a=ovl_bb_slroot(x->a); b=ovl_bb_slroot(x->b);
                if(a<b) bb_ssearch[b]=(BBByte)a;
                else if(b<a) bb_ssearch[a]=(BBByte)b;
            }
            a=ovl_bb_sroot(x->a); b=ovl_bb_sroot(x->b);
            if(a<b) bb_sconn[b]=(BBByte)a;
            else if(b<a) bb_sconn[a]=(BBByte)b;
            return;
        }
        if(bb_ssub==5) {
            if(bb_sj==bb_nnet) { bb_ssub=6; return; }
            i=bb_sj++;
            if(bb_sbase[i]>=0) bb_sanchor[ovl_bb_sroot(i)]=1;
            return;
        }
        if(bb_ssub==6) {
            /* Near zero current, keep a genuinely referenced weak path.
             * A diode's own2V clamp must not be its terminal's only anchor. */
            now=bb_sled[bb_si];
            if(now && (!bb_sseen[a] || !bb_sseen[b]) &&
               ovl_bb_slroot(a)==ovl_bb_slroot(b)) {
                /* A near-zero diode-only cycle can keep its own arbitrary
                 * clamps alive. Refuse that ambiguous active set explicitly;
                 * general diode complementarity is outside this solver. */
                ovl_bb_sfail(BB_EFLOAT,p->id,!bb_sseen[a]?a:b); return;
            }
            if(!bb_sanchor[ovl_bb_sroot(a)] || !bb_sanchor[ovl_bb_sroot(b)]) now=0;
        } else now=bb_ncmp(&bb_st0,&bb_n0)>0;
        if(now!=bb_sled[bb_si]) { bb_sled[bb_si]=(BBByte)now; bb_schanged=1; }
        ++bb_si; bb_ssub=0; return;
    }
    if(bb_si==bb_doc.np) {
        bb_sqhead=0; bb_sqtail=0; bb_sqcount=0;
        bb_zero(bb_sinq,sizeof(bb_sinq));
        for(i=0;i<bb_sngate;++i) ovl_bb_sqpush(i);
        bb_srebuild=0; bb_sg_after=1; bb_sphase=BB_S_GATE; return;
    }
    p=&bb_doc.part[bb_si]; t=p->type;
    if(t!=BB_555) { ++bb_si; bb_ssub=0; return; }
    if(bb_ssub==0) { bb_sj=0; bb_ssub=1; return; }
    if(bb_ssub==1) {
        if(bb_sj==bb_doc.np) { bb_ssub=2; return; }
        if(!ovl_bb_scontrolpart(bb_si,bb_sj++))
            ovl_bb_sfail(BB_ECONTROL,p->id,ovl_bb_snet(p,5));
        return;
    }
    a=ovl_bb_snet(p,8); b=ovl_bb_snet(p,1);
    if(bb_ssub==2) {
        if(bb_svm[ovl_bb_snet(p,4)]<0) {
            ovl_bb_sfail(BB_EDISCONNECTED,p->id,ovl_bb_snet(p,4)); return;
        }
        bb_nset(&bb_sctrl,bb_sbase[a]); bb_ssub=3; return;
    }
    if(bb_ssub==3) { bb_nop(&bb_sctrl,&bb_sctrl,&bb_n1000,BB_NDIV); bb_ssub=4; return; }
    if(bb_ssub==4) { bb_nop(&bb_strig,&bb_sctrl,&bb_n3,BB_NDIV); bb_ssub=5; return; }
    if(bb_ssub==5) { bb_nop(&bb_sctrl,&bb_strig,&bb_n2,BB_NMUL); bb_ssub=6; return; }
    if(bb_ssub==6) {
        if(bb_ncmp(&bb_sv[ovl_bb_snet(p,4)],&bb_nreset)<0) {
            bb_schanged=bb_swlatch[bb_si]!=0; bb_swlatch[bb_si]=0; bb_ssub=9;
        } else bb_ssub=7;
        return;
    }
    if(bb_ssub==7) {
        if(bb_svm[ovl_bb_snet(p,2)]<0) {
            ovl_bb_sfail(BB_EDISCONNECTED,p->id,ovl_bb_snet(p,2)); return;
        }
        if(bb_ncmp(&bb_sv[ovl_bb_snet(p,2)],&bb_strig)<0) {
            bb_schanged=bb_swlatch[bb_si]!=1; bb_swlatch[bb_si]=1; bb_ssub=9;
        } else bb_ssub=8;
        return;
    }
    if(bb_ssub==8) {
        if(bb_svm[ovl_bb_snet(p,6)]<0) {
            ovl_bb_sfail(BB_EDISCONNECTED,p->id,ovl_bb_snet(p,6)); return;
        }
        if(bb_ncmp(&bb_sv[ovl_bb_snet(p,6)],&bb_sctrl)>0) {
            bb_schanged=bb_swlatch[bb_si]!=0; bb_swlatch[bb_si]=0;
        } else bb_schanged=0;
        bb_ssub=9; return;
    }
    if(bb_schanged) {
        if(++bb_sevents>1024) { ovl_bb_sfail(BB_EOSCILLATE,p->id,a); return; }
        bb_ssub=0; bb_si=0; bb_sphase=bb_shasled?BB_S_REARM:BB_S_SEED;
    } else { ++bb_si; bb_ssub=0; }
}

static void ovl_bb_scurrent(void)
{
    BBSBranch *x;
    unsigned a,b,u,i;
    if(bb_ssub==0) {
        if(bb_si<bb_sngate) {
            i=bb_si++;
            if(bb_slogic[bb_sg[i].out]==BB_X && bb_sanalog[bb_sg[i].out])
                ovl_bb_sfail(BB_EBRIDGE,bb_doc.part[bb_sg[i].owner].id,bb_sg[i].out);
            return;
        }
        bb_si=0; bb_ssub=bb_shasled?20:23; return;
    }
    if(bb_ssub==20) {
        if(bb_si==bb_nnet) { bb_si=0; bb_ssub=21; return; }
        i=bb_si++; bb_sconn[i]=(BBByte)i; bb_sanchor[i]=0; bb_ssearch[i]=0; return;
    }
    if(bb_ssub==21) {
        /* LED regularization is not a physical current limiter. An active
         * all-LED path between unequal ideal rails is unsupported even if
         * its10ohm models happen to predict less than30mA. A series R/C
         * breaks this path; a parallel resistor does not. */
        if(bb_si==bb_snbranch) { bb_si=0; bb_ssub=22; return; }
        x=&bb_sb[bb_si++];
        if(x->kind!=BB_LED || !bb_sled[x->owner]) return;
        a=ovl_bb_sroot(x->a); b=ovl_bb_sroot(x->b);
        if(a<b) bb_sconn[b]=(BBByte)a;
        else if(b<a) { bb_sconn[a]=(BBByte)b; a=b; }
        bb_sanchor[a]=(BBByte)(x->owner+1); return;
    }
    if(bb_ssub==22) {
        if(bb_si==bb_nnet) { bb_si=0; bb_ssub=23; return; }
        i=bb_si++;
        if(bb_sbase[i]<0) return;
        a=ovl_bb_sroot(i);
        if(!bb_sanchor[a]) return;
        b=bb_ssearch[a];
        if(b && bb_sbase[b-1]!=bb_sbase[i]) {
            ovl_bb_sfail(BB_ELEDLIMIT,bb_doc.part[bb_sanchor[a]-1].id,i); return;
        }
        bb_ssearch[a]=(BBByte)(i+1); return;
    }
    if(bb_ssub==23) {
        bb_zero(bb_skcl,sizeof(bb_skcl)); bb_zero(bb_sabs,sizeof(bb_sabs));
        bb_si=0; bb_ssub=12; return;
    }
    if(bb_ssub==12) {
        if(bb_si<BB_PARTS) { bb_sw.ua[bb_si++]=BB_UA_INVALID; return; }
        bb_si=0; bb_ssub=1; return;
    }
    if(bb_si==bb_snbranch) {
        bb_si=0; bb_ssub=0; bb_sphase=BB_S_RESIDUAL; return;
    }
    x=&bb_sb[bb_si]; a=x->a; b=x->b;
    if(x->kind==BB_LED && !bb_sled[x->owner]) {
        bb_sw.ua[x->owner]=0; ++bb_si; bb_ssub=1; return;
    }
    if(bb_ssub==1) bb_nop(&bb_st0,&bb_sv[a],&bb_sv[b],BB_NSUB);
    else if(bb_ssub==2) bb_nop(&bb_st0,&bb_st0,&x->e,BB_NSUB);
    else if(bb_ssub==3) bb_nop(&bb_st0,&bb_st0,&x->g,BB_NMUL);
    else if(bb_ssub==4) {
        ovl_bb_snabs(&bb_st1,&bb_st0);
        if(bb_ncmp(&bb_st1,&bb_n30)>0) {
            ovl_bb_sfail(x->kind==BB_LED && bb_sbase[a]>=0 && bb_sbase[b]>=0?
                     BB_ELEDLIMIT:BB_ENUMERIC,bb_doc.part[x->owner].id,a); return;
        }
    } else if(bb_ssub==5) bb_nop(&bb_skcl[a],&bb_skcl[a],&bb_st0,BB_NADD);
    else if(bb_ssub==6) bb_nop(&bb_skcl[b],&bb_skcl[b],&bb_st0,BB_NSUB);
    else if(bb_ssub==7) {
        u=bb_sunk[a];
        if(u!=BB_NNET) bb_nop(&bb_sabs[u],&bb_sabs[u],&bb_st1,BB_NADD);
    } else if(bb_ssub==8) {
        u=bb_sunk[b];
        if(u!=BB_NNET) bb_nop(&bb_sabs[u],&bb_sabs[u],&bb_st1,BB_NADD);
    } else if(bb_ssub==9) bb_nop(&bb_st2,&bb_st0,&bb_n1000,BB_NMUL);
    else if(bb_ssub==10) {
        if(x->kind!=BB_POT) bb_sw.ua[x->owner]=(BBInt)bb_nround(&bb_st2);
    } else { ++bb_si; bb_ssub=1; return; }
    ++bb_ssub;
}

static void ovl_bb_sresidual(void)
{
    unsigned net,i;
    BBPart *p;
    if(bb_si<bb_snu) {
        net=bb_sunet[bb_si];
        if(bb_ssub==0) {
            /* A roundoff-scale incident current must not underflow the
             * guarded working envelope while constructing its tolerance.
             * Below1e-6mA the omitted relative term is <=1e-13mA. */
            if(bb_ncmp(&bb_sabs[bb_si],&bb_nresabs)<=0) {
                ovl_bb_snc(&bb_st0,&bb_nresabs); bb_ssub=2;
            } else bb_ssub=3;
        } else if(bb_ssub==3) {
            bb_nop(&bb_st0,&bb_sabs[bb_si],&bb_nresrel,BB_NMUL); bb_ssub=1;
        } else if(bb_ssub==1) {
            bb_nop(&bb_st0,&bb_st0,&bb_nresabs,BB_NADD); bb_ssub=2;
        } else {
            ovl_bb_snabs(&bb_st1,&bb_skcl[net]);
            if(bb_ncmp(&bb_st1,&bb_st0)>0) ovl_bb_sfail(BB_ECONVERGE,0,net);
            ++bb_si; bb_ssub=0;
        }
        return;
    }
    i=bb_si-bb_snu;
    if(i==bb_doc.np) {
        bb_si=0; bb_ssub=0; bb_sphase=BB_S_PUBLISH; return;
    }
    p=&bb_doc.part[i];
    if(p->type!=BB_POWER) { ++bb_si; return; }
    net=ovl_bb_snet(p,1);
    if(bb_ssub==0) {
        ovl_bb_snabs(&bb_st0,&bb_skcl[net]);
        if(bb_ncmp(&bb_st0,&bb_n30)>0) ovl_bb_sfail(BB_ENUMERIC,p->id,net);
        bb_ssub=1; return;
    }
    if(bb_ssub==1) {
        /* KCL is current delivered OUT of pin1; the source's internal
         * pin1-to-pin2 reference is opposite, positive when absorbing. */
        bb_nop(&bb_st0,&bb_n0,&bb_skcl[net],BB_NSUB); bb_ssub=2; return;
    }
    if(bb_ssub==2) {
        bb_nop(&bb_st0,&bb_st0,&bb_n1000,BB_NMUL); bb_ssub=3; return;
    }
    bb_sw.ua[i]=(BBInt)bb_nround(&bb_st0); ++bb_si; bb_ssub=0;
}

/* Scope captures voltage probes only; branch probes are explicitly N/A here.
 * Inspector branch current remains pin1->pin2 for supported two-pin parts. */
static int ovl_bb_sprobe(unsigned channel)
{
    unsigned h,i;
    if(bb_doc.probe_kind[channel]==BB_PROBE_BRANCH) return BB_MV_UNKNOWN;
    h=bb_doc.probe[channel];
    if(bb_doc.probe_kind[channel]==BB_PROBE_PIN) {
        h=BB_NONE;
        for(i=0;i<bb_doc.np;++i) if(bb_doc.part[i].id==bb_doc.probe_id[channel]) {
            h=bb_pin(&bb_doc.part[i],bb_doc.probe_pin[channel]); break;
        }
    }
    if(h>=BB_HOLES || bb_hnet[h]>=bb_nnet) return BB_MV_FLOAT;
    return bb_result.mv[bb_hnet[h]];
}

/* Pair subtraction modulo2^32, interval must be <2^31 ticks. */
static void ovl_bb_stimediff(unsigned alo,unsigned ahi,unsigned blo,unsigned bhi,
                         BBWord *lo,BBWord *hi)
{
    *lo=(BBWord)(alo-blo); *hi=(BBWord)(ahi-bhi-(alo<blo));
}

void ovl_bb_scope_sample(void)
{
    static BBWord dlo,dhi,plo,phi;
    int v0,v1;
    unsigned edge,now,edge_b,now_b,sample,i;
    if(!bb_valid || bb_result.diag) return;
    if(bb_scope_mode!=bb_doc.trigger) bb_scope_reset();
    v0=ovl_bb_sprobe(0); v1=ovl_bb_sprobe(1); edge=0;
    now=BB_X;
    if(v0>=0) now=((unsigned)v0>=bb_ssupplymv/2)?BB_HIGH:BB_LOW;
    if(now<BB_X && bb_scope_lastedge<BB_X && now!=bb_scope_lastedge) {
        edge=now==BB_HIGH?1:2;
        if(now==BB_HIGH) {
            if(bb_scope_have_rise) {
                ovl_bb_stimediff(bb_result.tlo,bb_result.thi,bb_scope_riselo,
                             bb_scope_risehi,&plo,&phi);
                if(phi<32768u && (plo || phi)) {
                    bb_scope.period_lo=plo; bb_scope.period_hi=phi;
                    if(bb_scope_have_fall) {
                        ovl_bb_stimediff(bb_scope_falllo,bb_scope_fallhi,
                                     bb_scope_riselo,bb_scope_risehi,&dlo,&dhi);
                        if(dhi<32768u) {
                            bb_scope.high_lo=dlo; bb_scope.high_hi=dhi;
                            if(bb_scope.cycles<65535u) ++bb_scope.cycles;
                        }
                    }
                } else {
                    bb_scope.cycles=0; bb_scope_have_fall=0;
                }
            }
            bb_scope_riselo=bb_result.tlo; bb_scope_risehi=bb_result.thi;
            bb_scope_have_rise=1; bb_scope_have_fall=0;
        } else if(bb_scope_have_rise) {
            bb_scope_falllo=bb_result.tlo; bb_scope_fallhi=bb_result.thi;
            bb_scope_have_fall=1;
        }
    }
    bb_scope_lastedge=now;
    /* Both channels preserve resolved digital edges. Channel A alone owns
     * trigger/period measurement, but B must never alias into a flat trace. */
    now_b=BB_X; edge_b=0;
    if(v1>=0) now_b=((unsigned)v1>=bb_ssupplymv/2)?BB_HIGH:BB_LOW;
    if(now_b<BB_X && bb_scope_lastedge_b<BB_X && now_b!=bb_scope_lastedge_b)
        edge_b=1;
    bb_scope_lastedge_b=now_b;
    ovl_bb_stimediff(bb_result.tlo,bb_result.thi,bb_scope_lastlo,
                 bb_scope_lasthi,&dlo,&dhi);
    sample=(bb_scope.count==0 || dhi!=0 || dlo>=200 || edge!=0 || edge_b!=0);
    if(bb_sclocked) bb_scope.undersampled=1;
    if(bb_doc.trigger && bb_scope.triggered==0 && edge==bb_doc.trigger) {
        bb_scope.triggered=1; bb_scope.trigger_at=bb_scope.head; bb_scope_post=128;
    }
    if(bb_doc.trigger && bb_scope.triggered==2) return;
    if(!sample) return;
    i=bb_scope.head;
    bb_scope.mv[0][i]=(BBInt)v0; bb_scope.mv[1][i]=(BBInt)v1;
    bb_scope.lo[i]=bb_result.tlo; bb_scope.hi[i]=bb_result.thi;
    bb_scope.head=(i+1)&255;
    if(bb_scope.count<256) ++bb_scope.count;
    bb_scope_lastlo=bb_result.tlo; bb_scope_lasthi=bb_result.thi;
    if(bb_scope.triggered==1 && bb_scope_post) {
        if(--bb_scope_post==0) bb_scope.triggered=2;
    }
}

static void ovl_bb_spublish(void)
{
    unsigned i,a,b;
    BBPart *p;
    if(bb_ssub==0) {
        if(bb_si==bb_doc.np) { bb_si=0; bb_ssub=1; return; }
        i=bb_si++; p=&bb_doc.part[i];
        if(p->type==BB_CAP) {
            a=ovl_bb_snet(p,1); b=ovl_bb_snet(p,2);
            bb_nop(&bb_scapnext[i],&bb_sv[a],&bb_sv[b],BB_NSUB);
        }
        return;
    }
    if(bb_si<BB_NETS) {
        i=bb_si++;
        if(i<bb_nnet) { bb_sw.mv[i]=bb_svm[i]; bb_sw.logic[i]=(BBByte)ovl_bb_sinput(i); }
        else { bb_sw.mv[i]=BB_MV_FLOAT; bb_sw.logic[i]=BB_Z; }
        return;
    }
    /* Fixed-size assembly copies, no scheduler/OS call, are one atomic commit. */
    bb_sw.tlo=bb_snextlo; bb_sw.thi=bb_snexthi;
    bb_sw.diag=0; bb_sw.part=0; bb_sw.net=BB_NNET;
    bb_copy(bb_scap,bb_scapnext,sizeof(bb_scap));
    bb_copy(bb_slatch,bb_swlatch,sizeof(bb_slatch));
    bb_copy(bb_sclock,bb_swclock,sizeof(bb_sclock));
    bb_copy(&bb_result,&bb_sw,sizeof(bb_result));
    bb_valid=1; bb_sphase=BB_S_IDLE; ovl_bb_scope_sample();
}

int ovl_bb_sim_slice(unsigned budget)
{
    if(bb_sphase==BB_S_IDLE) {
        if(!bb_sready) return -1;
        ovl_bb_sbeginstep();
    }
    if(budget>16) budget=16;
    while(budget--) {
        if(bb_sphase==BB_S_BEGIN) ovl_bb_sbeginone();
        else if(bb_sphase==BB_S_REARM) ovl_bb_srearm();
        else if(bb_sphase==BB_S_SCAN) ovl_bb_sscan();
        else if(bb_sphase==BB_S_G) ovl_bb_sconductance();
        else if(bb_sphase==BB_S_BRIDGE) ovl_bb_sbridge();
        else if(bb_sphase==BB_S_SEED) ovl_bb_sseed();
        else if(bb_sphase==BB_S_GATE) ovl_bb_sgates();
        else if(bb_sphase==BB_S_CLASSIFY) ovl_bb_sclassify();
        else if(bb_sphase==BB_S_STAMP) ovl_bb_sstamp();
        else if(bb_sphase==BB_S_RHS) ovl_bb_srhs();
        else if(bb_sphase==BB_S_FORWARD) ovl_bb_sforward();
        else if(bb_sphase==BB_S_SCALE) ovl_bb_sscale();
        else if(bb_sphase==BB_S_PIVOT) ovl_bb_spivot();
        else if(bb_sphase==BB_S_SWAP) ovl_bb_sswap();
        else if(bb_sphase==BB_S_ELIM) ovl_bb_selim();
        else if(bb_sphase==BB_S_BACK) ovl_bb_sback();
        else if(bb_sphase==BB_S_VOLTS) ovl_bb_svolts();
        else if(bb_sphase==BB_S_DEVICES) ovl_bb_sdevices();
        else if(bb_sphase==BB_S_CURRENT) ovl_bb_scurrent();
        else if(bb_sphase==BB_S_RESIDUAL) ovl_bb_sresidual();
        else if(bb_sphase==BB_S_PUBLISH) ovl_bb_spublish();
        else return -1;
        if(bb_num_error && !bb_sdiag) ovl_bb_sfail(BB_ENUMERIC,0,BB_NNET);
        if(bb_sdiag) {
            bb_result.diag=bb_sdiag; bb_result.part=bb_sdiagpart;
            bb_result.net=bb_sdiagnet; bb_valid=0; bb_running=0;
            bb_error=bb_sdiag; bb_error_part=bb_sdiagpart; bb_error_net=bb_sdiagnet;
            bb_sluready=0; bb_sphase=BB_S_IDLE; return -1;
        }
        if(bb_sphase==BB_S_IDLE) return 1;
    }
    return 0;
}

/* Public resident entry points: called only from lock-free wakes. The overlay
 * is eager inside BREADXT.O88; the runtime reports a refused load as0. */
int bb_sim_start(int reset) { return ovl_bb_sim_start(reset); }
int bb_sim_slice(unsigned budget) { return ovl_bb_sim_slice(budget); }

void bb_scope_sample(void) { ovl_bb_scope_sample(); }
