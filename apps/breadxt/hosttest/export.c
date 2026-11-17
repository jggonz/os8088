/* Host example builder: uses the SHIPPING model/codec/example implementation.
 * No alternate parser, no simulator, no runtime arithmetic adapter required. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef BB_HOST
#define BB_HOST
#endif
#include "../bb.h"
BBDoc bb_doc,bb_stage;
BBResult bb_result;
BBScope bb_scope;
BBByte bb_hnet[BB_HOLES];
BBWord bb_nnet,bb_error,bb_error_part,bb_error_net,bb_num_error;
BBWord bb_dirty,bb_running,bb_valid,bb_recompile;
void bb_copy(void *dest,const void *src,unsigned n){memmove(dest,src,n);}
void bb_zero(void *dest,unsigned n){memset(dest,0,n);}
#include "../model.c"
#include "../file.c"
#include "../examples.c"
static BBDoc ex_loaded;
static BBByte ex_data[BB_FILEMAX],ex_again[BB_FILEMAX];
static char ex_path[1024];
static int ex_net(unsigned part,unsigned pin)
{
    return bb_hnet[bb_pin(&bb_doc.part[part],pin)];
}
static int ex_graph(unsigned which)
{
    /* Independent logical relationships, indexed by thedocument's own
     * explicit fixturepart order, not by the model's union representatives. */
    if(which==8) {
        if(ex_net(0,1)!=ex_net(2,8)||ex_net(0,1)!=ex_net(2,4)||
           ex_net(0,1)!=ex_net(3,1)) return 0;
        if(ex_net(0,2)!=ex_net(1,1)||ex_net(0,2)!=ex_net(2,1)||
           ex_net(0,2)!=ex_net(5,2)||ex_net(0,2)!=ex_net(6,2)||
           ex_net(0,2)!=ex_net(8,2)) return 0;
        if(ex_net(2,7)!=ex_net(3,2)||ex_net(2,7)!=ex_net(4,1)) return 0;
        if(ex_net(2,2)!=ex_net(2,6)||ex_net(2,2)!=ex_net(4,2)||
           ex_net(2,2)!=ex_net(5,1)) return 0;
        if(ex_net(2,5)!=ex_net(6,1)||ex_net(2,3)!=ex_net(7,1)||
           ex_net(7,2)!=ex_net(8,1)) return 0;
        if(bb_nnet!=7) return 0;
    } else if(which==5) {
        if(ex_net(2,14)!=ex_net(0,1)||ex_net(2,7)!=ex_net(0,2)||
           ex_net(2,1)!=ex_net(3,1)||ex_net(2,2)!=ex_net(4,1)||
           ex_net(2,3)!=ex_net(5,1)) return 0;
    } else if(which==1||which==2||which==7) {
        if(ex_net(0,1)!=ex_net(2,1)||ex_net(2,2)!=ex_net(3,1)||
           ex_net(3,2)!=ex_net(0,2)||ex_net(1,1)!=ex_net(0,2)) return 0;
        if(bb_nnet!=3) return 0;
    } else if(which==3) {
        if(ex_net(0,1)!=ex_net(2,1)||ex_net(0,2)!=ex_net(2,2)||
           ex_net(2,3)==ex_net(0,1)||ex_net(2,3)==ex_net(0,2)) return 0;
    } else if(which==4) {
        if(ex_net(0,1)!=ex_net(2,1)||ex_net(2,2)!=ex_net(3,1)||
           ex_net(3,2)!=ex_net(4,1)||ex_net(4,2)!=ex_net(0,2)) return 0;
    } else if(which==6) {
        if(ex_net(0,1)!=ex_net(2,1)||ex_net(0,2)!=ex_net(2,2)) return 0;
    }
    return 1;
}
int main(int argc,char **argv)
{
    unsigned i,n,m;
    FILE *f;
    if(argc!=2){fprintf(stderr,"usage: breadxt-export OUTPUT-DIRECTORY\n");return 2;}
    if(sizeof(BBPart)!=16||sizeof(BBWire)!=16||sizeof(BBNum)!=8){fprintf(stderr,"record size mismatch\n");return 1;}
    for(i=1;i<=8;i++) {
        if(!bb_example(i)){fprintf(stderr,"%s invalid: %s id%u\n",bb_example_name(i),bb_diag_text(bb_error),bb_error_part);return 1;}
        n=bb_encode(&bb_stage,ex_data,sizeof(ex_data));
        if(!n||!bb_decode(ex_data,n,&ex_loaded)){fprintf(stderr,"%s codec failed\n",bb_example_name(i));return 1;}
        m=bb_encode(&ex_loaded,ex_again,sizeof(ex_again));
        if(n!=m||memcmp(ex_data,ex_again,n)){fprintf(stderr,"%s roundtrip mismatch\n",bb_example_name(i));return 1;}
        bb_copy(&bb_doc,&ex_loaded,sizeof(bb_doc));
        if(!bb_compile()||!ex_graph(i)){fprintf(stderr,"%s graph failed: %s id%u\n",bb_example_name(i),bb_diag_text(bb_error),bb_error_part);return 1;}
        if(snprintf(ex_path,sizeof(ex_path),"%s/%s",argv[1],bb_example_name(i))>=(int)sizeof(ex_path))return 1;
        f=fopen(ex_path,"wb");if(!f){perror(ex_path);return 1;}
        if(fwrite(ex_data,1,n,f)!=n||fclose(f)){perror(ex_path);return 1;}
        printf("%s: %u bytes, %u parts, %u wires, %u nets; graph+roundtrip verified\n",bb_example_name(i),n,bb_doc.np,bb_doc.nw,bb_nnet);
    }
    return 0;
}
