/* Breadboard XT: original physical model and editor transactions.
 * SPEC.md 130; docs/breadxt/CONTRACT.md. No BreadboardSim implementation is
 * incorporated. This file is included by breadxt.c, never built separately.
 * All scratch is static; the UI serializes model calls on its own task.
 */

/* A history record is 128 bytes on the 16-bit target. The unused tail keeps
 * indexing a shift rather than a non-power-of-two multiply in SmallerC. */
typedef struct {
    BBWord kind,index,before_exists,after_exists;
    BBPart before_part,after_part;
    BBWire before_wire,after_wire;
    BBWord before_next,after_next;
    BBWord before_lo,before_hi,after_lo,after_hi;
    BBWord before_probe[8],after_probe[8];
    BBWord spare[6];
} BBMHistory;

static BBMHistory bb_mhistory[BB_HISTORY],bb_mpending;
static BBWord bb_mhcount,bb_mhpos;
static BBWord bb_mrevision_lo,bb_mrevision_hi,bb_mserial_lo,bb_mserial_hi;
static BBWord bb_msaved_lo,bb_msaved_hi,bb_msaved_known;
static BBWord bb_msaved_view[14];
static BBPart bb_mpart;
static BBWire bb_mwire;
static BBByte bb_moccupied[BB_HOLES];
static BBWord bb_mids[BB_PARTS+BB_WIRES];
static BBByte bb_mparent[68],bb_mused[68],bb_mmap[68];

static int ovl_bb_mfail(unsigned code,unsigned id,unsigned net)
{
    bb_error=code;
    bb_error_part=id;
    bb_error_net=net;
    return 0;
}

static void ovl_bb_mok(void)
{
    bb_error=BB_OK;
    bb_error_part=BB_NONE;
    bb_error_net=BB_NONE;
}

static int ovl_bb_mequal(const void *a,const void *b,unsigned n)
{
    const BBByte *x;
    const BBByte *y;
    unsigned i;
    x=(const BBByte *)a;
    y=(const BBByte *)b;
    for(i=0;i<n;i++) if(x[i]!=y[i]) return 0;
    return 1;
}

static BBWord bb_mhole(unsigned row,unsigned col)
{
    return (row<<5)-(row<<1)+col;
}

int bb_pincount(unsigned type)
{
    if(type>=BB_HC00 && type<=BB_HC86) return 14;
    if(type==BB_555) return 8;
    if(type==BB_POT) return 3;
    if(type==BB_RES || type==BB_LED || type==BB_SWITCH ||
       type==BB_POWER || type==BB_CAP) return 2;
    if(type==BB_GROUND || type==BB_INPUT || type==BB_INDICATOR ||
       type==BB_CLOCK) return 1;
    return 0;
}

BBWord bb_pin(const BBPart *part,unsigned pin)
{
    unsigned n,half,row,col,pos;
    n=bb_pincount(part->type);
    if(pin==0 || pin>n) return BB_NONE;
    if(n>3) {
        if(part->pin[0]>=BB_HOLES || part->pin[0]/30!=7) return BB_NONE;
        half=n>>1;
        col=part->pin[0]%30;
        if(col+half>30) return BB_NONE;
        if(pin<=half) { row=7; pos=pin-1; }
        else { row=6; pos=n-pin; }
        if(part->flags&BB_ROT) { row=13-row; pos=half-1-pos; }
        return bb_mhole(row,col+pos);
    }
    if((part->flags&BB_ROT) && n>=2 && pin<3) pin=3-pin;
    return part->pin[pin-1];
}

int bb_strip(unsigned hole)
{
    unsigned row,col;
    if(hole>=BB_HOLES) return -1;
    row=hole/30;
    col=hole%30;
    if(row>=2 && row<=6) return col;
    if(row>=7 && row<=11) return 30+col;
    if(row<2) return 60+(row<<1)+(col>=15);
    return 64+((row-12)<<1)+(col>=15);
}

void bb_clear(BBDoc *doc)
{
    bb_zero(doc,sizeof(BBDoc));
    doc->next_id=1;
    doc->probe[0]=BB_NONE;
    doc->probe[1]=BB_NONE;
    doc->zoom=1;
    doc->dt=10;
    doc->scope_div=5000;
}

static int ovl_bb_mpart_fields(const BBPart *part)
{
    unsigned type,n,i,allowed;
    type=part->type;
    n=bb_pincount(type);
    if(n==0) return ovl_bb_mfail(BB_EVALUE,part->id,BB_NONE);
    allowed=BB_ROT;
    if(type==BB_SWITCH) allowed|=BB_CLOSED;
    if(part->flags&~allowed) return ovl_bb_mfail(BB_EVALUE,part->id,BB_NONE);
    if(n>3) {
        if(part->pin[1]!=BB_NONE || part->pin[2]!=BB_NONE)
            return ovl_bb_mfail(BB_EFORMAT,part->id,BB_NONE);
    } else {
        for(i=n;i<3;i++) if(part->pin[i]!=BB_NONE)
            return ovl_bb_mfail(BB_EFORMAT,part->id,BB_NONE);
    }
    if(type==BB_RES || type==BB_POT) {
        if(part->value<10) return ovl_bb_mfail(BB_EVALUE,part->id,BB_NONE);
        if(type==BB_POT) {
            if(part->param>1000) return ovl_bb_mfail(BB_EVALUE,part->id,BB_NONE);
        } else if(part->param!=0) return ovl_bb_mfail(BB_EVALUE,part->id,BB_NONE);
    } else if(type==BB_POWER) {
        if(part->value<4500 || part->value>6000 || part->param!=0)
            return ovl_bb_mfail(BB_EVALUE,part->id,BB_NONE);
    } else if(type==BB_CAP) {
        if(part->value==0 || part->value>60000u || part->param>6000)
            return ovl_bb_mfail(BB_EVALUE,part->id,BB_NONE);
    } else if(type==BB_CLOCK) {
        if(part->value<2 || part->value>60000u ||
           part->param==0 || part->param>=1000)
            return ovl_bb_mfail(BB_EVALUE,part->id,BB_NONE);
    } else if(type==BB_INPUT) {
        if(part->value!=0 || part->param>BB_Z)
            return ovl_bb_mfail(BB_EVALUE,part->id,BB_NONE);
    } else if(part->value!=0 || part->param!=0)
        return ovl_bb_mfail(BB_EVALUE,part->id,BB_NONE);
    return 1;
}

static int ovl_bb_mwire_fields(const BBWire *wire)
{
    unsigned i,x,y,nx,ny;
    if(wire->a>=BB_HOLES || wire->b>=BB_HOLES || wire->a==wire->b)
        return ovl_bb_mfail(BB_EHOLE,wire->id,BB_NONE);
    if(wire->color>15 || wire->nbend>2)
        return ovl_bb_mfail(BB_EVALUE,wire->id,BB_NONE);
    x=wire->a%30;
    y=wire->a/30;
    for(i=0;i<=wire->nbend;i++) {
        if(i==wire->nbend) { nx=wire->b%30; ny=wire->b/30; }
        else { nx=wire->bend[i<<1]; ny=wire->bend[(i<<1)+1]; }
        if(nx>=30 || ny>=14 || (x!=nx && y!=ny) || (x==nx && y==ny))
            return ovl_bb_mfail(BB_EVALUE,wire->id,BB_NONE);
        x=nx;
        y=ny;
    }
    for(i=(unsigned)wire->nbend<<1;i<4;i++) if(wire->bend[i]!=0)
        return ovl_bb_mfail(BB_EFORMAT,wire->id,BB_NONE);
    if(wire->nbend==2 && wire->bend[0]==wire->bend[2] &&
       wire->bend[1]==wire->bend[3])
        return ovl_bb_mfail(BB_EVALUE,wire->id,BB_NONE);
    return 1;
}

static int ovl_bb_mid_add(unsigned id,unsigned count,unsigned next)
{
    unsigned i;
    if(id==0 || id==BB_NONE || id>=next)
        return ovl_bb_mfail(BB_EVALUE,id,BB_NONE);
    for(i=0;i<count;i++) if(bb_mids[i]==id)
        return ovl_bb_mfail(BB_EDUPLICATE,id,BB_NONE);
    bb_mids[count]=id;
    return 1;
}

static int ovl_bb_minsert_lead(unsigned hole,unsigned id)
{
    if(hole>=BB_HOLES) return ovl_bb_mfail(BB_EHOLE,id,BB_NONE);
    if(bb_moccupied[hole]) return ovl_bb_mfail(BB_EOCCUPIED,id,BB_NONE);
    bb_moccupied[hole]=1;
    return 1;
}

static int ovl_bb_mfind_part(const BBDoc *doc,unsigned id)
{
    unsigned i;
    for(i=0;i<doc->np;i++) if(doc->part[i].id==id) return i;
    return -1;
}

static int ovl_bb_mprobe_valid(const BBDoc *doc,unsigned channel)
{
    int i;
    unsigned kind;
    kind=doc->probe_kind[channel];
    if(kind==BB_PROBE_HOLE) {
        if(doc->probe[channel]!=BB_NONE && doc->probe[channel]>=BB_HOLES)
            return ovl_bb_mfail(BB_EHOLE,BB_NONE,BB_NONE);
        if(doc->probe_id[channel]!=0 || doc->probe_pin[channel]!=0)
            return ovl_bb_mfail(BB_EFORMAT,BB_NONE,BB_NONE);
        return 1;
    }
    if(kind!=BB_PROBE_PIN && kind!=BB_PROBE_BRANCH)
        return ovl_bb_mfail(BB_EVALUE,BB_NONE,BB_NONE);
    if(doc->probe[channel]!=BB_NONE)
        return ovl_bb_mfail(BB_EFORMAT,doc->probe_id[channel],BB_NONE);
    i=ovl_bb_mfind_part(doc,doc->probe_id[channel]);
    if(i<0) return ovl_bb_mfail(BB_EDISCONNECTED,doc->probe_id[channel],BB_NONE);
    if(kind==BB_PROBE_PIN) {
        if(doc->probe_pin[channel]==0 ||
           doc->probe_pin[channel]>bb_pincount(doc->part[i].type))
            return ovl_bb_mfail(BB_EVALUE,doc->probe_id[channel],BB_NONE);
    } else if(doc->probe_pin[channel]!=1)
        return ovl_bb_mfail(BB_EFORMAT,doc->probe_id[channel],BB_NONE);
    return 1;
}

int ovl_bb_validate(BBDoc *doc)
{
    unsigned i,j,n,count;
    ovl_bb_mok();
    if(doc->np>BB_PARTS || doc->nw>BB_WIRES)
        return ovl_bb_mfail(BB_ECAPACITY,BB_NONE,BB_NONE);
    if(doc->next_id==0 || doc->panx>29 || doc->pany>13 ||
       (doc->zoom!=1 && doc->zoom!=2) || doc->dt==0 || doc->dt>1000 ||
       doc->scope_div<10 || doc->scope_div>60000u || doc->trigger>2)
        return ovl_bb_mfail(BB_EVALUE,BB_NONE,BB_NONE);
    bb_zero(bb_moccupied,sizeof(bb_moccupied));
    count=0;
    for(i=0;i<doc->np;i++) {
        if(!ovl_bb_mid_add(doc->part[i].id,count,doc->next_id)) return 0;
        count++;
        if(!ovl_bb_mpart_fields(&doc->part[i])) return 0;
        n=bb_pincount(doc->part[i].type);
        for(j=1;j<=n;j++)
            if(!ovl_bb_minsert_lead(bb_pin(&doc->part[i],j),doc->part[i].id)) return 0;
    }
    for(i=0;i<doc->nw;i++) {
        if(!ovl_bb_mid_add(doc->wire[i].id,count,doc->next_id)) return 0;
        count++;
        if(!ovl_bb_mwire_fields(&doc->wire[i])) return 0;
        for(j=0;j<i;j++) {
            if(doc->wire[i].a==doc->wire[j].a) {
                if(doc->wire[i].b==doc->wire[j].b)
                    return ovl_bb_mfail(BB_EDUPLICATE,doc->wire[i].id,BB_NONE);
            }
            if(doc->wire[i].a==doc->wire[j].b) {
                if(doc->wire[i].b==doc->wire[j].a)
                    return ovl_bb_mfail(BB_EDUPLICATE,doc->wire[i].id,BB_NONE);
            }
        }
        if(!ovl_bb_minsert_lead(doc->wire[i].a,doc->wire[i].id) ||
           !ovl_bb_minsert_lead(doc->wire[i].b,doc->wire[i].id)) return 0;
    }
    if(!ovl_bb_mprobe_valid(doc,0) || !ovl_bb_mprobe_valid(doc,1)) return 0;
    return 1;
}

void bb_invalidate(void)
{
    bb_running=0;
    bb_valid=0;
    bb_recompile=1;
    bb_dirty=1;
}

void bb_history_clear(void)
{
    bb_mhcount=0;
    bb_mhpos=0;
    bb_mrevision_lo=0;
    bb_mrevision_hi=0;
    bb_mserial_lo=0;
    bb_mserial_hi=0;
    bb_msaved_known=0;
}

void bb_mark_saved(void)
{
    bb_msaved_lo=bb_mrevision_lo;
    bb_msaved_hi=bb_mrevision_hi;
    bb_msaved_known=1;
    /* All fourteen words after next_id are persisted probe/view settings.
     * Keep these as well as the editor revision: view/probe changes need not
     * be object-history commands, but cannot make Undo falsely look saved. */
    bb_copy(bb_msaved_view,bb_doc.probe,sizeof(bb_msaved_view));
    bb_dirty=0;
}

static void ovl_bb_mdirty(void)
{
    bb_dirty=!bb_msaved_known || bb_mrevision_lo!=bb_msaved_lo ||
             bb_mrevision_hi!=bb_msaved_hi ||
             !ovl_bb_mequal(bb_msaved_view,bb_doc.probe,sizeof(bb_msaved_view));
}

static void ovl_bb_mprobe_store(const BBDoc *doc,BBWord *out)
{
    unsigned i,k;
    for(i=0;i<2;i++) {
        k=i<<2;
        out[k]=doc->probe[i];
        out[k+1]=doc->probe_kind[i];
        out[k+2]=doc->probe_id[i];
        out[k+3]=doc->probe_pin[i];
    }
}

static void ovl_bb_mprobe_restore(BBDoc *doc,const BBWord *to,const BBWord *from)
{
    unsigned i,k;
    for(i=0;i<2;i++) {
        k=i<<2;
        /* An unrelated later probe selection survives ordinary value Undo.
         * Only commands that changed a binding (e.g. deletion) restore it. */
        if(!ovl_bb_mequal(to+k,from+k,8)) {
            doc->probe[i]=to[k];
            doc->probe_kind[i]=to[k+1];
            doc->probe_id[i]=to[k+2];
            doc->probe_pin[i]=to[k+3];
        }
    }
}

static void ovl_bb_mprobe_unbind(BBDoc *doc,unsigned id)
{
    unsigned i;
    for(i=0;i<2;i++) if(doc->probe_kind[i]!=BB_PROBE_HOLE &&
                        doc->probe_id[i]==id) {
        doc->probe[i]=BB_NONE;
        doc->probe_kind[i]=BB_PROBE_HOLE;
        doc->probe_id[i]=0;
        doc->probe_pin[i]=0;
    }
}

static int ovl_bb_mcommit(unsigned kind,unsigned index,int before,int after)
{
    unsigned i;
    if(!ovl_bb_validate(&bb_stage)) return 0;
    bb_zero(&bb_mpending,sizeof(BBMHistory));
    bb_mpending.kind=kind;
    bb_mpending.index=index;
    bb_mpending.before_exists=before;
    bb_mpending.after_exists=after;
    if(kind==BB_PART) {
        if(before) bb_copy(&bb_mpending.before_part,&bb_doc.part[index],sizeof(BBPart));
        if(after) bb_copy(&bb_mpending.after_part,&bb_stage.part[index],sizeof(BBPart));
    } else {
        if(before) bb_copy(&bb_mpending.before_wire,&bb_doc.wire[index],sizeof(BBWire));
        if(after) bb_copy(&bb_mpending.after_wire,&bb_stage.wire[index],sizeof(BBWire));
    }
    bb_mpending.before_next=bb_doc.next_id;
    bb_mpending.after_next=bb_stage.next_id;
    bb_mpending.before_lo=bb_mrevision_lo;
    bb_mpending.before_hi=bb_mrevision_hi;
    bb_mserial_lo=(BBWord)(bb_mserial_lo+1);
    if(bb_mserial_lo==0) bb_mserial_hi=(BBWord)(bb_mserial_hi+1);
    bb_mpending.after_lo=bb_mserial_lo;
    bb_mpending.after_hi=bb_mserial_hi;
    ovl_bb_mprobe_store(&bb_doc,bb_mpending.before_probe);
    ovl_bb_mprobe_store(&bb_stage,bb_mpending.after_probe);
    bb_mhcount=bb_mhpos;              /* discard the redo branch as a whole */
    if(bb_mhcount==BB_HISTORY) {
        for(i=1;i<BB_HISTORY;i++)
            bb_copy(&bb_mhistory[i-1],&bb_mhistory[i],sizeof(BBMHistory));
        bb_mhcount--;
        bb_mhpos--;
    }
    bb_copy(&bb_mhistory[bb_mhcount],&bb_mpending,sizeof(BBMHistory));
    bb_mhcount++;
    bb_mhpos=bb_mhcount;
    bb_copy(&bb_doc,&bb_stage,sizeof(BBDoc));
    bb_mrevision_lo=bb_mpending.after_lo;
    bb_mrevision_hi=bb_mpending.after_hi;
    bb_invalidate();
    ovl_bb_mdirty();
    ovl_bb_mok();
    return 1;
}

int ovl_bb_edit_part(int index,const BBPart *part)
{
    unsigned i,n;
    ovl_bb_mok();
    if(part==0 || index<-1 || (index>=0 && (unsigned)index>=bb_doc.np))
        return ovl_bb_mfail(BB_EVALUE,BB_NONE,BB_NONE);
    if(index<0 && (bb_doc.np==BB_PARTS || bb_doc.next_id==BB_NONE))
        return ovl_bb_mfail(BB_ECAPACITY,BB_NONE,BB_NONE);
    bb_copy(&bb_mpart,part,sizeof(BBPart)); /* accepts a caller alias in stage */
    n=bb_pincount(bb_mpart.type);
    if(n==0) return ovl_bb_mfail(BB_EVALUE,BB_NONE,BB_NONE);
    if(n>3) n=1;
    for(i=n;i<3;i++) bb_mpart.pin[i]=BB_NONE;
    bb_copy(&bb_stage,&bb_doc,sizeof(BBDoc));
    if(index<0) {
        index=bb_stage.np;
        bb_stage.np++;
        bb_mpart.id=bb_stage.next_id;
        bb_stage.next_id++;
    } else bb_mpart.id=bb_doc.part[index].id;
    if((unsigned)index<bb_doc.np &&
       ovl_bb_mequal(&bb_doc.part[index],&bb_mpart,sizeof(BBPart))) return 1;
    bb_copy(&bb_stage.part[index],&bb_mpart,sizeof(BBPart));
    return ovl_bb_mcommit(BB_PART,index,(unsigned)index<bb_doc.np,1);
}

int ovl_bb_edit_wire(int index,const BBWire *wire)
{
    unsigned i;
    ovl_bb_mok();
    if(wire==0 || index<-1 || (index>=0 && (unsigned)index>=bb_doc.nw))
        return ovl_bb_mfail(BB_EVALUE,BB_NONE,BB_NONE);
    if(index<0 && (bb_doc.nw==BB_WIRES || bb_doc.next_id==BB_NONE))
        return ovl_bb_mfail(BB_ECAPACITY,BB_NONE,BB_NONE);
    bb_copy(&bb_mwire,wire,sizeof(BBWire));
    if(bb_mwire.nbend>2) return ovl_bb_mfail(BB_EVALUE,BB_NONE,BB_NONE);
    for(i=(unsigned)bb_mwire.nbend<<1;i<4;i++) bb_mwire.bend[i]=0;
    bb_copy(&bb_stage,&bb_doc,sizeof(BBDoc));
    if(index<0) {
        index=bb_stage.nw;
        bb_stage.nw++;
        bb_mwire.id=bb_stage.next_id;
        bb_stage.next_id++;
    } else bb_mwire.id=bb_doc.wire[index].id;
    if((unsigned)index<bb_doc.nw &&
       ovl_bb_mequal(&bb_doc.wire[index],&bb_mwire,sizeof(BBWire))) return 1;
    bb_copy(&bb_stage.wire[index],&bb_mwire,sizeof(BBWire));
    return ovl_bb_mcommit(BB_WIRE,index,(unsigned)index<bb_doc.nw,1);
}

static void ovl_bb_mremove(BBDoc *doc,unsigned kind,unsigned index)
{
    unsigned i;
    if(kind==BB_PART) {
        for(i=index+1;i<doc->np;i++)
            bb_copy(&doc->part[i-1],&doc->part[i],sizeof(BBPart));
        doc->np--;
        bb_zero(&doc->part[doc->np],sizeof(BBPart));
    } else {
        for(i=index+1;i<doc->nw;i++)
            bb_copy(&doc->wire[i-1],&doc->wire[i],sizeof(BBWire));
        doc->nw--;
        bb_zero(&doc->wire[doc->nw],sizeof(BBWire));
    }
}

int ovl_bb_delete(unsigned kind,unsigned id)
{
    unsigned i,count;
    ovl_bb_mok();
    if(kind!=BB_PART && kind!=BB_WIRE) return ovl_bb_mfail(BB_EVALUE,id,BB_NONE);
    count=kind==BB_PART?bb_doc.np:bb_doc.nw;
    for(i=0;i<count;i++) {
        if((kind==BB_PART?bb_doc.part[i].id:bb_doc.wire[i].id)==id) break;
    }
    if(i==count) return ovl_bb_mfail(BB_EVALUE,id,BB_NONE);
    bb_copy(&bb_stage,&bb_doc,sizeof(BBDoc));
    if(kind==BB_PART) ovl_bb_mprobe_unbind(&bb_stage,id);
    ovl_bb_mremove(&bb_stage,kind,i);
    return ovl_bb_mcommit(kind,i,1,0);
}

static int ovl_bb_mhistory_apply(const BBMHistory *h,int after)
{
    unsigned i,was,now,index;
    const BBPart *part;
    const BBWire *wire;
    was=after?h->before_exists:h->after_exists;
    now=after?h->after_exists:h->before_exists;
    index=h->index;
    part=after?&h->after_part:&h->before_part;
    wire=after?&h->after_wire:&h->before_wire;
    if(now) {
        if(!was) {
            if(h->kind==BB_PART) {
                if(bb_stage.np==BB_PARTS || index>bb_stage.np)
                    return ovl_bb_mfail(BB_ECAPACITY,part->id,BB_NONE);
                for(i=bb_stage.np;i>index;i--)
                    bb_copy(&bb_stage.part[i],&bb_stage.part[i-1],sizeof(BBPart));
                bb_stage.np++;
            } else {
                if(bb_stage.nw==BB_WIRES || index>bb_stage.nw)
                    return ovl_bb_mfail(BB_ECAPACITY,wire->id,BB_NONE);
                for(i=bb_stage.nw;i>index;i--)
                    bb_copy(&bb_stage.wire[i],&bb_stage.wire[i-1],sizeof(BBWire));
                bb_stage.nw++;
            }
        }
        if(h->kind==BB_PART) bb_copy(&bb_stage.part[index],part,sizeof(BBPart));
        else bb_copy(&bb_stage.wire[index],wire,sizeof(BBWire));
    } else ovl_bb_mremove(&bb_stage,h->kind,index);
    if(after) ovl_bb_mprobe_restore(&bb_stage,h->after_probe,h->before_probe);
    else ovl_bb_mprobe_restore(&bb_stage,h->before_probe,h->after_probe);
    /* next_id is an allocator high-water, never rewound by Undo. A new edit
     * after Undo cannot reuse the stable ID of an abandoned command. */
    return ovl_bb_validate(&bb_stage);
}

int ovl_bb_undo(int redo)
{
    const BBMHistory *h;
    ovl_bb_mok();
    if(redo) {
        if(bb_mhpos==bb_mhcount) return 0;
        h=&bb_mhistory[bb_mhpos];
    } else {
        if(bb_mhpos==0) return 0;
        h=&bb_mhistory[bb_mhpos-1];
    }
    bb_copy(&bb_stage,&bb_doc,sizeof(BBDoc));
    if(!ovl_bb_mhistory_apply(h,redo!=0)) return 0;
    bb_copy(&bb_doc,&bb_stage,sizeof(BBDoc));
    if(redo) {
        bb_mhpos++;
        bb_mrevision_lo=h->after_lo;
        bb_mrevision_hi=h->after_hi;
    } else {
        bb_mhpos--;
        bb_mrevision_lo=h->before_lo;
        bb_mrevision_hi=h->before_hi;
    }
    bb_invalidate();
    ovl_bb_mdirty();
    ovl_bb_mok();
    return 1;
}

static unsigned ovl_bb_mroot(unsigned strip)
{
    unsigned root,next;
    root=strip;
    while(bb_mparent[root]!=root) root=bb_mparent[root];
    while(bb_mparent[strip]!=strip) {
        next=bb_mparent[strip];
        bb_mparent[strip]=(BBByte)root;
        strip=next;
    }
    return root;
}

static void ovl_bb_mjoin(unsigned a,unsigned b)
{
    a=ovl_bb_mroot(a);
    b=ovl_bb_mroot(b);
    if(a<b) bb_mparent[b]=(BBByte)a;
    else if(b<a) bb_mparent[a]=(BBByte)b;
}

int ovl_bb_compile(void)
{
    unsigned i,j,n,a,b,root,count;
    if(!ovl_bb_validate(&bb_doc)) return 0;
    for(i=0;i<68;i++) { bb_mparent[i]=(BBByte)i; bb_mused[i]=0; bb_mmap[i]=BB_NNET; }
    for(i=0;i<bb_doc.np;i++) {
        n=bb_pincount(bb_doc.part[i].type);
        for(j=1;j<=n;j++) bb_mused[bb_strip(bb_pin(&bb_doc.part[i],j))]=1;
    }
    for(i=0;i<bb_doc.nw;i++) {
        a=bb_strip(bb_doc.wire[i].a);
        b=bb_strip(bb_doc.wire[i].b);
        bb_mused[a]=1;
        bb_mused[b]=1;
        ovl_bb_mjoin(a,b);
    }
    for(i=0;i<bb_doc.np;i++) {
        if(bb_doc.part[i].type==BB_SWITCH && (bb_doc.part[i].flags&BB_CLOSED))
            ovl_bb_mjoin(bb_strip(bb_pin(&bb_doc.part[i],1)),bb_strip(bb_pin(&bb_doc.part[i],2)));
        else if(bb_doc.part[i].type==BB_POT) {
            if(bb_doc.part[i].param==0)
                ovl_bb_mjoin(bb_strip(bb_pin(&bb_doc.part[i],1)),bb_strip(bb_pin(&bb_doc.part[i],3)));
            else if(bb_doc.part[i].param==1000)
                ovl_bb_mjoin(bb_strip(bb_pin(&bb_doc.part[i],2)),bb_strip(bb_pin(&bb_doc.part[i],3)));
        }
    }
    /* Mark canonical representatives, then enumerate in physical order. */
    for(i=0;i<68;i++) if(bb_mused[i]) bb_mmap[ovl_bb_mroot(i)]=0;
    count=0;
    for(i=0;i<68;i++) if(bb_mmap[i]!=BB_NNET) {
        if(count==BB_NETS) return ovl_bb_mfail(BB_ECAPACITY,BB_NONE,BB_NONE);
        bb_mmap[i]=(BBByte)count;
        count++;
    }
    /* Nothing public changes before every capacity check has passed. */
    for(i=0;i<BB_HOLES;i++) {
        root=ovl_bb_mroot(bb_strip(i));
        bb_hnet[i]=bb_mmap[root];
    }
    bb_nnet=count;
    bb_recompile=0;
    ovl_bb_mok();
    return 1;
}

const char *bb_part_name(unsigned type)
{
    switch(type) {
    case BB_RES:return "Resistor";
    case BB_LED:return "LED (A,K)";
    case BB_SWITCH:return "Switch";
    case BB_POWER:return "DC supply";
    case BB_GROUND:return "Ground";
    case BB_POT:return "Potentiometer";
    case BB_INPUT:return "Logic input";
    case BB_INDICATOR:return "Logic lamp";
    case BB_CLOCK:return "Clock";
    case BB_HC00:return "74HC00 NAND";
    case BB_HC04:return "74HC04 NOT";
    case BB_HC08:return "74HC08 AND";
    case BB_HC32:return "74HC32 OR";
    case BB_HC86:return "74HC86 XOR";
    case BB_CAP:return "Capacitor";
    case BB_555:return "NE555 timer";
    }
    return "Unknown part";
}

const char *bb_diag_text(unsigned code)
{
    switch(code) {
    case BB_OK:return "Ready";
    case BB_EHOLE:return "Choose a valid hole";
    case BB_EOCCUPIED:return "Hole occupied: use a neighbor";
    case BB_ECAPACITY:return "Circuit capacity reached";
    case BB_EVALUE:return "Value or placement out of range";
    case BB_EDUPLICATE:return "Duplicate ID or wire";
    case BB_EFORMAT:return "Malformed project record";
    case BB_EVERSION:return "Unsupported project version";
    case BB_ENOGROUND:return "Add a ground reference";
    case BB_EFLOAT:return "Floating analog network";
    case BB_EDISCONNECTED:return "Disconnected node or probe";
    case BB_ECONFLICT:return "Conflicting fixed voltages";
    case BB_ESHORT:return "Supply is shorted to ground";
    case BB_ESINGULAR:return "Singular circuit: check wires";
    case BB_ENUMERIC:return "Numeric range exceeded";
    case BB_ECONVERGE:return "Circuit did not converge";
    case BB_EPOWER:return "Check IC power pins";
    case BB_ECONTENTION:return "Conflicting output drivers";
    case BB_EOSCILLATE:return "Logic event limit: feedback";
    case BB_EBRIDGE:return "Unsupported mixed feedback";
    case BB_ECONTROL:return "555 CONT: use ground bypass";
    case BB_ELEDLIMIT:return "LED needs a series resistor";
    case BB_EIO:return "File operation failed";
    case BB_ESUPPLIES:return "Use one DC supply";
    }
    return "Circuit error";
}

/* Public resident command doors. Init, pin access and paint names remain
 * resident; validated document commands run only from lock-free wakes. */
int bb_validate(BBDoc *doc) { return ovl_bb_validate(doc); }
int bb_edit_part(int index,const BBPart *part) { return ovl_bb_edit_part(index,part); }
int bb_edit_wire(int index,const BBWire *wire) { return ovl_bb_edit_wire(index,wire); }
int bb_delete(unsigned kind,unsigned id) { return ovl_bb_delete(kind,id); }
int bb_undo(int redo) { return ovl_bb_undo(redo); }
int bb_compile(void) { return ovl_bb_compile(); }

void bb_view_changed(void) { ovl_bb_mdirty(); }

static unsigned ovl_bb_history_object(int redo,int after,BBPart *part,BBWire *wire)
{
    const BBMHistory *h;
    unsigned side,exists;
    if(redo) { if(bb_mhpos>=bb_mhcount) return 0; h=&bb_mhistory[bb_mhpos]; }
    else { if(bb_mhpos==0) return 0; h=&bb_mhistory[bb_mhpos-1]; }
    side=redo?(after!=0):(after==0);
    exists=side?h->after_exists:h->before_exists;
    if(!exists) return 0;
    if(h->kind==BB_PART) {
        if(part==0) return 0;
        bb_copy(part,side?&h->after_part:&h->before_part,sizeof(BBPart));
    } else {
        if(wire==0) return 0;
        bb_copy(wire,side?&h->after_wire:&h->before_wire,sizeof(BBWire));
    }
    return h->kind;
}
unsigned bb_history_object(int redo,int after,BBPart *part,BBWire *wire)
{ return ovl_bb_history_object(redo,after,part,wire); }
