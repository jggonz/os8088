/* Original editable teaching documents. SPEC.md130; physical holes, no demo
 * behavior or filename dispatch in the simulator. All figures derive from
 * manufacturer pin maps and independent logical graphs in FORMAT.md. */
#ifdef BB_HOST
/* The example builders run on the HOST only: the build writes the eight
 * .BBX files from them (hosttest/export.c), and the package opens those
 * files from its own folder like any other document (SPEC.md 130.6). */
static unsigned bb_exhole(unsigned row,unsigned col)
{
    return (row<<5)-(row<<1)+col;
}
static unsigned bb_expart(unsigned type,unsigned a,unsigned b,unsigned c,
                          unsigned value,unsigned param,unsigned flags)
{
    BBPart *p;
    unsigned n;
    n=bb_stage.np++;
    p=&bb_stage.part[n];
    p->id=bb_stage.next_id++;p->type=type;
    p->pin[0]=a;p->pin[1]=b;p->pin[2]=c;
    p->value=value;p->param=param;p->flags=flags;
    return n;
}
static void bb_exwire(unsigned a,unsigned b,unsigned color)
{
    BBWire *w;
    w=&bb_stage.wire[bb_stage.nw++];
    w->id=bb_stage.next_id++;w->a=a;w->b=b;w->color=(BBByte)color;
    if(a%30!=b%30 && a/30!=b/30) {
        w->nbend=1;w->bend[0]=b%30;w->bend[1]=a/30;
    }
}
static void bb_exprobe(unsigned channel,unsigned part,unsigned pin)
{
    bb_stage.probe[channel]=BB_NONE;
    bb_stage.probe_kind[channel]=BB_PROBE_PIN;
    bb_stage.probe_id[channel]=bb_stage.part[part].id;
    bb_stage.probe_pin[channel]=pin;
}
static void bb_exsupply(unsigned positive,unsigned negative)
{
    bb_expart(BB_POWER,bb_exhole(7,positive),bb_exhole(7,negative),BB_NONE,5000,0,0);
    bb_expart(BB_GROUND,bb_exhole(8,negative),BB_NONE,BB_NONE,0,0,0);
}
static int ovl_bb_example(unsigned which)
{
    unsigned r,c,u;
    if(which>8) return bb_ffail(BB_EVALUE);
    bb_clear(&bb_stage);
    if(which==0) return bb_validate(&bb_stage);
    if(which==1) {
        bb_exsupply(3,20);
        bb_expart(BB_RES,bb_exhole(8,3),bb_exhole(8,10),BB_NONE,33,0,0);
        r=bb_expart(BB_LED,bb_exhole(9,10),bb_exhole(9,20),BB_NONE,0,0,0);
        bb_exprobe(0,r,1);bb_exprobe(1,0,1);
    } else if(which==2) {
        bb_exsupply(3,23);
        bb_expart(BB_RES,bb_exhole(8,3),bb_exhole(8,12),BB_NONE,1000,0,0);
        r=bb_expart(BB_RES,bb_exhole(9,12),bb_exhole(9,23),BB_NONE,1000,0,0);
        bb_exprobe(0,r,1);bb_exprobe(1,0,1);
    } else if(which==3) {
        bb_exsupply(3,23);
        r=bb_expart(BB_POT,bb_exhole(8,3),bb_exhole(9,23),bb_exhole(8,12),1000,500,0);
        bb_exprobe(0,r,3);bb_exprobe(1,0,1);
    } else if(which==4) {
        bb_exsupply(3,25);
        bb_expart(BB_RES,bb_exhole(8,3),bb_exhole(8,9),BB_NONE,33,0,0);
        bb_expart(BB_SWITCH,bb_exhole(9,9),bb_exhole(8,15),BB_NONE,0,0,BB_CLOSED);
        r=bb_expart(BB_LED,bb_exhole(9,15),bb_exhole(9,25),BB_NONE,0,0,0);
        bb_exprobe(0,r,1);bb_exprobe(1,0,1);
    } else if(which==5) {
        bb_expart(BB_POWER,bb_exhole(0,0),bb_exhole(1,0),BB_NONE,5000,0,0);
        bb_expart(BB_GROUND,bb_exhole(1,1),BB_NONE,BB_NONE,0,0,0);
        u=bb_expart(BB_HC00,bb_exhole(7,11),BB_NONE,BB_NONE,0,0,0);
        /* The inputs and the indicator stand clear of the chip and of each
         * other, wired to pins 1, 2 and 3: a single-pin part draws upward
         * and its tag is wider than a column, so three of them in g12-g14
         * piled onto the chip and onto each other's tags. */
        bb_expart(BB_INPUT,bb_exhole(8,4),BB_NONE,BB_NONE,0,BB_HIGH,0);
        bb_expart(BB_INPUT,bb_exhole(8,8),BB_NONE,BB_NONE,0,BB_HIGH,0);
        bb_expart(BB_INDICATOR,bb_exhole(8,21),BB_NONE,BB_NONE,0,0,0);
        bb_exwire(bb_exhole(2,11),bb_exhole(0,11),4);
        bb_exwire(bb_exhole(8,17),bb_exhole(1,10),1);
        bb_exwire(bb_exhole(11,11),bb_exhole(11,4),2);
        bb_exwire(bb_exhole(10,12),bb_exhole(10,8),2);
        bb_exwire(bb_exhole(11,13),bb_exhole(11,21),5);
        bb_exprobe(0,u,3);bb_exprobe(1,u,1);
    } else if(which==6) {
        bb_exsupply(3,23);
        r=bb_expart(BB_LED,bb_exhole(8,3),bb_exhole(9,23),BB_NONE,0,0,0);
        bb_exprobe(0,r,1);bb_exprobe(1,r,2);
    } else if(which==7) {
        bb_exsupply(3,23);
        bb_expart(BB_RES,bb_exhole(8,3),bb_exhole(8,12),BB_NONE,1000,0,0);
        c=bb_expart(BB_CAP,bb_exhole(9,12),bb_exhole(9,23),BB_NONE,10000,0,0);
        bb_exprobe(0,c,1);bb_exprobe(1,0,1);
    } else {
        /* 555 astable, actual pins. U anchor gives1=f12,2=f13,3=f14,
         *4=f15,5=e15,6=e14,7=e13,8=e12 (one-based labels). */
        bb_expart(BB_POWER,bb_exhole(0,0),bb_exhole(1,0),BB_NONE,5000,0,0);
        bb_expart(BB_GROUND,bb_exhole(1,1),BB_NONE,BB_NONE,0,0,0);
        u=bb_expart(BB_555,bb_exhole(7,11),BB_NONE,BB_NONE,0,0,0);
        bb_expart(BB_RES,bb_exhole(2,7),bb_exhole(2,12),BB_NONE,1000,0,0);
        bb_expart(BB_RES,bb_exhole(3,12),bb_exhole(3,17),BB_NONE,6800,0,0);
        c=bb_expart(BB_CAP,bb_exhole(10,12),bb_exhole(13,12),BB_NONE,10000,0,0);
        bb_expart(BB_CAP,bb_exhole(4,14),bb_exhole(4,20),BB_NONE,10,0,0);
        bb_expart(BB_RES,bb_exhole(8,13),bb_exhole(8,22),BB_NONE,33,0,0);
        bb_expart(BB_LED,bb_exhole(9,22),bb_exhole(9,25),BB_NONE,0,0,0);
        /* Rails: supplynegative(top-) tobottom-; explicit bridge ofbottom
         * railbreak at15/16. Wires consume their own neighboring holes. */
        bb_exwire(bb_exhole(1,2),bb_exhole(13,2),1);
        bb_exwire(bb_exhole(13,14),bb_exhole(13,15),1);
        /* Pin1ground; pin4reset and pin8power toVCC. */
        bb_exwire(bb_exhole(8,11),bb_exhole(13,11),1);
        bb_exwire(bb_exhole(8,14),bb_exhole(0,14),4);
        bb_exwire(bb_exhole(2,11),bb_exhole(0,11),4);
        /* RA fromVCC todischarge; RB fromdischarge tojoint2/6. */
        bb_exwire(bb_exhole(3,7),bb_exhole(0,7),4);
        bb_exwire(bb_exhole(4,17),bb_exhole(9,12),2);
        bb_exwire(bb_exhole(5,13),bb_exhole(8,12),2);
        /* CONT bypass andLED cathode return, through actualnodes. */
        bb_exwire(bb_exhole(5,20),bb_exhole(13,20),1);
        bb_exwire(bb_exhole(10,25),bb_exhole(13,25),1);
        bb_exprobe(0,u,3);bb_exprobe(1,c,1);
    }
    return bb_validate(&bb_stage);
}
int bb_example(unsigned which)
{
    return ovl_bb_example(which);
}
#endif
const char *bb_example_name(unsigned which)
{
    switch(which) {
    case 0:return "UNTITLED.BBX";
    case 1:return "LEDTEST.BBX";
    case 2:return "DIVIDER.BBX";
    case 3:return "POT.BBX";
    case 4:return "SWITCH.BBX";
    case 5:return "LOGIC.BBX";
    case 6:return "BROKEN.BBX";
    case 7:return "RC.BBX";
    case 8:return "FLASHER.BBX";
    }
    return "UNTITLED.BBX";
}
