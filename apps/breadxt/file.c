/* Breadboard XT original bounded BBX1 codec; SPEC.md 130.5.
 * Wire format is explicit little endian, never a native structure dump.
 * The caller supplies staged storage; the live document is never a load target.
 * No OS calls, solver calls, heap, C floating types or addressable automatics. */
static unsigned bb_fget(const BBByte *data,unsigned at)
{
    return (unsigned)data[at]|((unsigned)data[at+1]<<8);
}
static void bb_fput(BBByte *data,unsigned at,unsigned value)
{
    data[at]=(BBByte)value;
    data[at+1]=(BBByte)(value>>8);
}
static int bb_ffail(unsigned code)
{
    bb_error=code;bb_error_part=BB_NONE;bb_error_net=BB_NONE;
    return 0;
}
static int ovl_bb_decode(const BBByte *data,unsigned size,BBDoc *out)
{
    unsigned i,j,at,np,nw;
    BBPart *p;
    BBWire *w;
    if(!data || !out || out==&bb_doc || size<BB_HEADER_SIZE || size>BB_FILEMAX)
        return bb_ffail(BB_EFORMAT);
    if(data[0]!='B'||data[1]!='B'||data[2]!='X'||data[3]!='1')
        return bb_ffail(BB_EFORMAT);
    if(bb_fget(data,4)!=1) return bb_ffail(BB_EVERSION);
    if(bb_fget(data,6)!=BB_HEADER_SIZE || bb_fget(data,30)!=BB_BOARD_PROFILE ||
       bb_fget(data,44)!=0 || bb_fget(data,46)!=0)
        return bb_ffail(BB_EFORMAT);
    np=bb_fget(data,8);nw=bb_fget(data,10);
    if(np>BB_PARTS || nw>BB_WIRES) return bb_ffail(BB_ECAPACITY);
    if(size!=BB_HEADER_SIZE+((np+nw)<<4)) return bb_ffail(BB_EFORMAT);
    bb_clear(out);
    out->np=np;out->nw=nw;out->next_id=bb_fget(data,12);
    out->probe[0]=bb_fget(data,14);out->probe[1]=bb_fget(data,16);
    out->panx=bb_fget(data,18);out->pany=bb_fget(data,20);
    out->zoom=bb_fget(data,22);out->dt=bb_fget(data,24);
    out->scope_div=bb_fget(data,26);out->trigger=bb_fget(data,28);
    for(i=0;i<2;i++) {
        out->probe_kind[i]=bb_fget(data,32+(i<<1));
        out->probe_id[i]=bb_fget(data,36+(i<<1));
        out->probe_pin[i]=bb_fget(data,40+(i<<1));
    }
    at=BB_HEADER_SIZE;
    for(i=0;i<np;i++) {
        p=&out->part[i];p->id=bb_fget(data,at);p->type=bb_fget(data,at+2);
        for(j=0;j<3;j++) p->pin[j]=bb_fget(data,at+4+(j<<1));
        p->value=bb_fget(data,at+10);p->param=bb_fget(data,at+12);
        p->flags=bb_fget(data,at+14);at+=16;
    }
    for(i=0;i<nw;i++) {
        w=&out->wire[i];w->id=bb_fget(data,at);w->a=bb_fget(data,at+2);
        w->b=bb_fget(data,at+4);
        for(j=0;j<4;j++) w->bend[j]=bb_fget(data,at+6+(j<<1));
        w->color=data[at+14];w->nbend=data[at+15];at+=16;
    }
    return bb_validate(out);
}
int bb_decode(const BBByte *data,unsigned size,BBDoc *out)
{
    return ovl_bb_decode(data,size,out);
}
static int ovl_bb_encode(const BBDoc *doc,BBByte *data,unsigned cap)
{
    unsigned size,i,j,at;
    const BBPart *p;
    const BBWire *w;
    if(!doc || !data) return bb_ffail(BB_EFORMAT);
    if(!bb_validate((BBDoc *)doc)) return 0;
    size=BB_HEADER_SIZE+((doc->np+doc->nw)<<4);
    if(cap<size || size>BB_FILEMAX) return bb_ffail(BB_ECAPACITY);
    bb_zero(data,size);
    data[0]='B';data[1]='B';data[2]='X';data[3]='1';
    bb_fput(data,4,1);bb_fput(data,6,BB_HEADER_SIZE);
    bb_fput(data,8,doc->np);bb_fput(data,10,doc->nw);bb_fput(data,12,doc->next_id);
    bb_fput(data,14,doc->probe[0]);bb_fput(data,16,doc->probe[1]);
    bb_fput(data,18,doc->panx);bb_fput(data,20,doc->pany);
    bb_fput(data,22,doc->zoom);bb_fput(data,24,doc->dt);
    bb_fput(data,26,doc->scope_div);bb_fput(data,28,doc->trigger);
    bb_fput(data,30,BB_BOARD_PROFILE);
    for(i=0;i<2;i++) {
        bb_fput(data,32+(i<<1),doc->probe_kind[i]);
        bb_fput(data,36+(i<<1),doc->probe_id[i]);
        bb_fput(data,40+(i<<1),doc->probe_pin[i]);
    }
    at=BB_HEADER_SIZE;
    for(i=0;i<doc->np;i++) {
        p=&doc->part[i];bb_fput(data,at,p->id);bb_fput(data,at+2,p->type);
        for(j=0;j<3;j++) bb_fput(data,at+4+(j<<1),p->pin[j]);
        bb_fput(data,at+10,p->value);bb_fput(data,at+12,p->param);
        bb_fput(data,at+14,p->flags);at+=16;
    }
    for(i=0;i<doc->nw;i++) {
        w=&doc->wire[i];bb_fput(data,at,w->id);bb_fput(data,at+2,w->a);
        bb_fput(data,at+4,w->b);
        for(j=0;j<4;j++) bb_fput(data,at+6+(j<<1),w->bend[j]);
        data[at+14]=w->color;data[at+15]=w->nbend;at+=16;
    }
    bb_error=BB_OK;bb_error_part=BB_NONE;bb_error_net=BB_NONE;
    return size;
}
unsigned bb_encode(const BBDoc *doc,BBByte *data,unsigned cap)
{
    return ovl_bb_encode(doc,data,cap);
}

/* Parse decimal digits using a power-of-ten shift before accumulation.
 * This accepts e.g.655350ohm without ever forming655350 in a target int. */
static char bb_fdigits[12],bb_fsuffix[9];
static int bb_fsame(const char *a,const char *b)
{
    unsigned i;
    for(i=0;i<9;i++) {
        if(a[i]!=b[i]) return 0;
        if(a[i]==0) return 1;
    }
    return 0;
}
static int ovl_bb_parse(unsigned type,const char *s,BBWord *out)
{
    unsigned at,n,frac,dot,ndigit,i,c,value,ns;
    int power;
    if(!s || !out) return bb_ffail(BB_EVALUE);
    at=0;while(at<32 && s[at]==' ') at++;
    n=0;frac=0;dot=0;ndigit=0;
    while(at<32) {
        c=(BBByte)s[at];
        if(c>='0' && c<='9') {
            if(n==11) return bb_ffail(BB_EVALUE);
            bb_fdigits[n++]=(char)c;ndigit++;
            if(dot && ++frac>3) return bb_ffail(BB_EVALUE);
        } else if(c=='.' && !dot && ndigit) dot=1;
        else break;
        at++;
    }
    if(!ndigit || (dot && !frac)) return bb_ffail(BB_EVALUE);
    while(at<32 && s[at]==' ') at++;
    ns=0;
    while(at<32 && s[at] && s[at]!=' ') {
        if(ns==8) return bb_ffail(BB_EVALUE);
        bb_fsuffix[ns++]=s[at++];
    }
    bb_fsuffix[ns]=0;
    while(at<32 && s[at]==' ') at++;
    if(at>=32 || s[at]) return bb_ffail(BB_EVALUE);
    power=0;
    if(type==BB_RES || type==BB_POT) {
        power=-1;
        if(bb_fsame(bb_fsuffix,"") || bb_fsame(bb_fsuffix,"ohm") ||
           bb_fsame(bb_fsuffix,"Ohm") || bb_fsame(bb_fsuffix,"R")) power=-1;
        else if(bb_fsame(bb_fsuffix,"k") || bb_fsame(bb_fsuffix,"K") ||
                bb_fsame(bb_fsuffix,"kohm") || bb_fsame(bb_fsuffix,"kOhm")) power=2;
        else if(bb_fsame(bb_fsuffix,"M") || bb_fsame(bb_fsuffix,"Mohm") ||
                bb_fsame(bb_fsuffix,"MOhm")) power=5;
        else return bb_ffail(BB_EVALUE);
    } else if(type==BB_CAP) {
        if(bb_fsame(bb_fsuffix,"") || bb_fsame(bb_fsuffix,"n") ||
           bb_fsame(bb_fsuffix,"nF")) power=0;
        else if(bb_fsame(bb_fsuffix,"u") || bb_fsame(bb_fsuffix,"uF")) power=3;
        else return bb_ffail(BB_EVALUE);
    } else if(type==BB_POWER) {
        if(bb_fsame(bb_fsuffix,"") || bb_fsame(bb_fsuffix,"mV")) power=0;
        else if(bb_fsame(bb_fsuffix,"V")) power=3;
        else return bb_ffail(BB_EVALUE);
    } else if(type==BB_CLOCK) {
        if(bb_fsame(bb_fsuffix,"")) power=0;
        else if(bb_fsame(bb_fsuffix,"us")) power=-2;
        else if(bb_fsame(bb_fsuffix,"ms")) power=1;
        else if(bb_fsame(bb_fsuffix,"s")) power=4;
        else return bb_ffail(BB_EVALUE);
    } else return bb_ffail(BB_EVALUE);
    power-=(int)frac;
    while(power<0) {
        if(!n || bb_fdigits[n-1]!='0') return bb_ffail(BB_EVALUE);
        n--;power++;
    }
    value=0;
    for(i=0;i<n;i++) {
        c=(unsigned)(bb_fdigits[i]-'0');
        if(value>6553u || (value==6553u && c>5)) return bb_ffail(BB_EVALUE);
        value=(value<<3)+(value<<1);
        value+=c;
    }
    while(power>0) {
        if(value>6553u) return bb_ffail(BB_EVALUE);
        value=(value<<3)+(value<<1);power--;
    }
    if(type==BB_RES || type==BB_POT) { if(value<10) return bb_ffail(BB_EVALUE); }
    else if(type==BB_CAP) { if(!value || value>60000u) return bb_ffail(BB_EVALUE); }
    else if(type==BB_POWER) { if(value<4500 || value>6000) return bb_ffail(BB_EVALUE); }
    else if(value<2 || value>60000u) return bb_ffail(BB_EVALUE);
    *out=(BBWord)value;
    bb_error=BB_OK;bb_error_part=BB_NONE;bb_error_net=BB_NONE;
    return 1;
}
int bb_parse_value(unsigned type,const char *s,BBWord *out)
{
    return ovl_bb_parse(type,s,out);
}
