/* Cold native menus. BORATYPE-SPEC3.1. Included by bt_face.c, shared band
 * primitives and hit registry; every function stays in the eager overlay.
 * No new UI state, audio calls, claims or scoring changes. */
static void tf_render(int x,int y,int w,int h,int kind,int item);
void ovl_bt_portrait_scene(int x,int y,int w,int h);
#ifndef BT_ART_BADGES
#define BT_ART_BADGES 264
#endif
static const char *tf_achnames[]={"First lesson","95% mastery","25-key streak","50-key streak","95% challenge","Perfect lesson","Personal best"};
static const char *tf_resultlabels[]={"SPEED","ACCURACY","ERRORS","BEST STREAK","TIME"};

static int ovl_tm_visible(int x,int y,int w,int h)
{return w>0&&h>0&&x<tf_sx+tf_sw&&x+w>tf_sx&&y<tf_sy+tf_sh&&y+h>tf_sy;}
static void ovl_tm_panel(int x,int y,int w,int h,const char *title)
{
    if(!ovl_tm_visible(x,y,w,h))return;
    gk_box(x,y,w,h,GK_SUNKEN2,tf_ground());
    if(*title){bx_fill(x+3,y+3,x+w-4,y+22,tf_mono?15:TF_BLUE);
        tf_text(x+8,y+5,w-16,title,TF_YELLOW,BT_F_TITLE);}
}
static void ovl_tm_bar(int x,int y,int w,int value,int total)
{
    int n;
    if(!ovl_tm_visible(x,y,w,10))return;
    if(value>total)value=total;if(value<0)value=0;
    gk_box(x,y,w,10,GK_SUNKEN,tf_ground());
    n=(w-4)*value/total;
    if(n)bx_fill(x+2,y+2,x+n+1,y+7,tf_ink(TF_GREEN));
}
static int ovl_tm_mastered(BTProfile *p)
{int i,n;n=0;for(i=0;i<BT_LESSONS;i++)if(p->lesson_best[i]>=95)n++;return n;}
static void ovl_tm_profile(int x,int y,int w,BTProfile *p)
{
    int n;
    n=tf_append(0,"Profile: ");n=tf_append(n,p->name);
    if(bt_readonly)tf_append(n,"  (read only)");
    tf_text(x,y,w,tf_buf,TF_YELLOW,tf_font);
}
static void ovl_tm_mascot(int x,int y,int w,int h)
{
    int sh;
    if(!ovl_tm_visible(x,y,w,h))return;
    ovl_tm_panel(x,y,w,h,"");sh=h>=168?120:48;
    ovl_bt_portrait_scene(x+4,y+5,w-8,sh);
    tf_append(tf_append(0,"Borat: "),bt_caption());
    ovl_tf_lines(x+8,y+sh+12,w-16,(h-sh-18)/(tf_th+2),tf_buf);
}
static int ovl_tm_has(const char *s,int c)
{while(*s){if(*s==c)return 1;s++;}return 0;}
static void ovl_tm_keys(int x,int y,int w,int h,const char *allowed)
{
    int i,kx,ky,kw,kh,c;
    kw=w/15;kh=h/4;
    if(kw<6||kh<6)return;
    for(i=0;i<47;i++){
        kx=x+tf_keys[i][3]*kw/4;ky=y+tf_keys[i][2]*kh;
        if(!ovl_tm_visible(kx,ky,kw-1,kh-1))continue;
        c=ovl_tm_has(allowed,tf_keys[i][0]);
        gk_box(kx,ky,kw-1,kh-1,GK_RAISED,tf_mono?15:c?tf_zones[tf_keys[i][5]]:7);
        if(kw>=11&&kh>=11)gk_textbox(kx,ky,kw-2,kh-2,tf_keylabel(i),0,BT_F_SMALL,GK_CENTRE|GK_FIT);
    }
}
static void ovl_tm_weak(int x,int y,int w,int h,BTProfile *p)
{
    int i,k,c,n;
    ovl_tm_panel(x,y,w,h,"TRY THESE KEYS");c=(w-32)/3;
    for(i=0;i<3;i++){
        k=bt_weak(i);if(!k)break;
        tf_char[0]=k;tf_char[1]=0;
        if(k==' ')tf_append(0,"SPACE");else tf_append(0,tf_char);
        if(ovl_tm_visible(x+8+i*(c+8),y+27,c,27)){
            gk_box(x+8+i*(c+8),y+27,c,27,GK_RAISED2,tf_mono?15:tf_zones[tf_keys[tf_keyid(k)][5]]);
            gk_textbox(x+8+i*(c+8),y+28,c,25,tf_buf,0,BT_F_TITLE,GK_CENTRE|GK_FIT);
        }
        n=tf_number(0,p->key_error[k-32]);n=tf_append(n,"/");tf_number(n,p->key_attempt[k-32]);
        if(h>=72)tf_text(x+8+i*(c+8),y+57,c,tf_buf,TF_WHITE,BT_F_SMALL);
    }
    if(!i)ovl_tf_lines(x+8,y+29,w-16,2,"20 samples per key needed");
}
static void ovl_tm_achievements(int x,int y,int w,int h,unsigned bits)
{
    int i,c,rh,xx,yy,n;
    c=(w-18)/4;rh=h/2;
    for(i=0;i<7;i++){
        xx=x+(i%4)*(c+6);yy=y+(i/4)*rh;
        if(!ovl_tm_visible(xx,yy,c,rh-4))continue;
        gk_box(xx,yy,c,rh-4,(bits&(1<<i))?GK_RAISED2:GK_SUNKEN,tf_ground());
        n=(bits&(1<<i))?TF_YELLOW:7;
        if((bits&(1<<i))&&!tf_mono&&rh>=30)bx_spr(xx+4,yy+2,BT_ART_BADGES+(i==0?0:i==2?1:i==3?2:i==6?4:3));
        else tf_text(xx+6,yy+3,18,(bits&(1<<i))?"*":"-",n,BT_F_TITLE);
        tf_text(xx+30,yy+5,c-34,tf_achnames[i],(bits&(1<<i))?TF_WHITE:7,BT_F_SMALL);
    }
}
static void ovl_tm_lessons(int x,int y,int w,int h,BTProfile *p,int rich)
{
    int i,n,c,rh,rows,first,right,ph;
    if(!rich){
        c=tf_compact?(tf_h<150?4:3):2;rows=(BT_LESSONS+c-1)/c;
        rh=(h-4)/rows;if(rh>28)rh=28;if(rh<10)rh=10;n=(w-8*(c-1))/c;
        for(i=0;i<BT_LESSONS;i++){
            tf_append(tf_append(0,p->lesson_best[i]>=95?"* ":p->lesson_done[i]?"+ ":""),bt_lessons[i].name);
            tf_button(x+(i/rows)*(n+8),y+(i%rows)*rh,n,rh-1,tf_buf,BT_A_LESSON_BASE+i,bt_selection==i);
        }return;
    }
    right=w*3/5;c=right-8;ph=h-36;rh=22;rows=(ph-52)/rh;if(rows>12)rows=12;if(rows<1)rows=1;
    first=(bt_selection/rows)*rows;
    ovl_tm_panel(x,y,c,ph,"");
    tf_text(x+7,y+7,c-112,"LESSON",TF_YELLOW,BT_F_TITLE);
    tf_text(x+c-107,y+9,48,"BEST",TF_WHITE,BT_F_SMALL);
    tf_text(x+c-55,y+9,49,"STATUS",TF_WHITE,BT_F_SMALL);
    for(i=first;i<first+rows&&i<BT_LESSONS;i++){
        n=y+27+(i-first)*rh;
        tf_button(x+4,n,c-8,rh-1,"",BT_A_LESSON_BASE+i,bt_selection==i);
        tf_text(x+10,n+4,c-122,bt_lessons[i].name,bt_selection==i?0:TF_WHITE,tf_font);
        if(p->lesson_done[i]){tf_number(0,p->lesson_best[i]);tf_append(tf_number(0,p->lesson_best[i]),"%");}
        else tf_append(0,"-");
        tf_text(x+c-107,n+4,48,tf_buf,bt_selection==i?0:TF_YELLOW,tf_font);
        tf_text(x+c-55,n+4,49,p->lesson_best[i]>=95?"*":p->lesson_done[i]?"+":"Ready",bt_selection==i?0:TF_GREEN,BT_F_SMALL);
    }
    n=y+ph-22;
    tf_button(x+6,n,54,17,"Prev",BT_A_LESSON_BASE+(first?first-1:0),0);
    tf_number(0,bt_selection+1);tf_append(tf_number(0,bt_selection+1)," / 24");
    tf_text(x+66,n+4,c-132,tf_buf,TF_YELLOW,BT_F_SMALL);
    tf_button(x+c-60,n,54,17,"Next",BT_A_LESSON_BASE+(first+rows<24?first+rows:23),0);
    i=bt_selection;if(i<0||i>=24)i=0;n=x+right;
    ovl_tm_panel(n,y,w-right,140,"");ovl_bt_portrait_scene(n+4,y+5,w-right-8,130);
    ovl_tm_panel(n,y+146,w-right,ph-146,bt_lessons[i].name);
    if(ph>=244){
        tf_text(n+8,y+175,w-right-16,bt_lessons[i].allowed,TF_YELLOW,tf_font);
        tf_text(n+8,y+192,w-right-16,bt_lessons[i].drill,TF_WHITE,BT_F_SMALL);
        ovl_tm_keys(n+8,y+212,w-right-16,ph-220,bt_lessons[i].allowed);
    }else ovl_tf_lines(n+8,y+175,w-right-16,(ph-182)/(tf_th+2),bt_lessons[i].instruction);
    c=(w-12)/3;n=y+h-30;
    tf_button(x,n,c,27,"Start lesson",BT_A_START,1);
    tf_button(x+c+6,n,c,27,"Practice",BT_A_PRACTICE,0);
    tf_button(x+2*(c+6),n,c,27,"Back",BT_A_BACK,0);
}
static void ovl_tm_records(int x,int y,int w,int h,BTProfile *p,int rich)
{
    int i,d,n,c,yy,rh,table,mastered,earned;
    if(!rich){
        rh=9;c=(w-40)/4;
        for(d=0;d<3;d++){
            n=tf_number(0,d==0?1:d==1?3:5);tf_append(n,"m");tf_text(x,y,38,tf_buf,TF_YELLOW,BT_F_SMALL);
            for(i=0;i<4;i++){tf_char[0]="HLDM"[i];tf_char[1]=' ';tf_char[2]=0;n=tf_append(0,tf_char);
                n=tf_number(n,p->record_wpm[d][i]);n=tf_append(n,"/");n=tf_number(n,p->record_accuracy[d][i]);tf_append(n,"%");
                tf_text(x+40+i*c,y,c,tf_buf,TF_WHITE,BT_F_SMALL);}y+=rh;
        }y+=2;
        for(i=0;i<7;i++){n=tf_append(0,(p->achievements&(1<<i))?"[*] ":"[ ] ");tf_append(n,tf_achnames[i]);tf_text(x+(i%2)*(w/2),y+(i/2)*rh,w/2,tf_buf,TF_YELLOW,BT_F_SMALL);}return;
    }
    table=w*3/5;c=(table-20)/3;
    for(i=0;i<3;i++){n=tf_number(0,i==0?1:i==1?3:5);tf_append(n," MIN");tf_button(x+i*(c+4),y,c,27,tf_buf,BT_A_DURATION_BASE+i,bt_ch_duration==i);}
    yy=y+34;ovl_tm_panel(x,yy,table-8,126,"");
    tf_text(x+8,yy+6,table/2-8,"TEST",TF_WHITE,BT_F_SMALL);
    tf_text(x+table/2,yy+6,table/4,"WPM",TF_WHITE,BT_F_SMALL);
    tf_text(x+3*table/4,yy+6,table/4-16,"ACCURACY",TF_WHITE,BT_F_SMALL);
    d=bt_ch_duration;
    for(i=0;i<4;i++){
        n=yy+23+i*24;tf_button(x+4,n,table-16,23,"",BT_A_DIFFICULTY_BASE+i,bt_ch_diff==i);
        tf_text(x+10,n+5,table/2-12,tf_group[i],bt_ch_diff==i?0:TF_WHITE,tf_font);
        tf_number(0,p->record_wpm[d][i]);tf_text(x+table/2,n+5,table/4,tf_buf,bt_ch_diff==i?0:TF_YELLOW,tf_font);
        tf_number(0,p->record_accuracy[d][i]);tf_append(tf_number(0,p->record_accuracy[d][i]),"%");tf_text(x+3*table/4,n+5,table/4-16,tf_buf,bt_ch_diff==i?0:TF_YELLOW,tf_font);
    }
    ovl_tm_mascot(x+table,y,w-table,160);
    yy=y+162;mastered=ovl_tm_mastered(p);earned=0;for(i=0;i<7;i++)if(p->achievements&(1<<i))earned++;
    rh=15;
    for(i=0;i<3;i++){
        n=tf_append(0,i==0?"Lessons mastered":i==1?"Best streak":"Achievements");
        tf_text(x,yy+i*rh,w/3,tf_buf,TF_WHITE,BT_F_SMALL);
        ovl_tm_bar(x+w/3,yy+i*rh+1,w/3,i==0?mastered:i==1?p->best_streak:earned,i==0?24:i==1?50:7);
        n=tf_number(0,i==0?mastered:i==1?p->best_streak:earned);n=tf_append(n," / ");tf_number(n,i==0?24:i==1?50:7);
        tf_text(x+2*w/3+10,yy+i*rh,w/3-10,tf_buf,TF_YELLOW,tf_font);
    }
    yy+=48;ovl_tm_achievements(x,yy,w,h-(yy-y)-34,p->achievements);
    c=(w-12)/3;yy=y+h-29;
    tf_button(x,yy,c,27,"Practice",BT_A_PRACTICE,0);tf_button(x+c+6,yy,c,27,"Continue",BT_A_START,1);tf_button(x+2*(c+6),yy,c,27,"Back",BT_A_BACK,0);
}
static void ovl_tf_resultcard(int x,int y,int w,BTProfile *p)
{
    int left,sx,sw,sy,rh,i,n,k,h,c;
    h=tf_bottom-y-36;left=w/3;sx=x+left+10;sw=w-left-10;
    ovl_tm_mascot(x,y,left,h);
    if(h>=230){
        sy=y+174;tf_text(x+8,sy,left-16,"New achievements:",TF_YELLOW,BT_F_SMALL);sy+=14;
        for(i=0;i<7&&sy+tf_th<y+h-5;i++)if(bt_session.achievement_new&(1<<i)){
            n=tf_append(0,"* ");tf_append(n,tf_achnames[i]);tf_line(x+8,sy,left-16,tf_buf);sy+=tf_th+2;
        }
    }
    tf_text(sx,y,sw,!bt_session.attempts?"No keys entered":bt_session.new_record?"PERSONAL BEST!":bt_session.ranked?"Completed":"Practice / unranked",TF_YELLOW,tf_font);
    sy=y+18;rh=h>=260?24:18;ovl_tm_panel(sx,sy,sw,5*rh+3,"");
    for(i=0;i<5;i++){
        tf_text(sx+7,sy+i*rh+3,sw/2-12,tf_resultlabels[i],TF_WHITE,tf_font);n=0;
        if(i==0){n=bt_session.seconds?tf_number(0,bt_session.wpm):tf_append(0,"-");tf_append(n," WPM");}
        else if(i==1){if(bt_session.attempts){n=tf_number(0,bt_session.accuracy);tf_append(n,"%");}else tf_append(0,"-");}
        else if(i==2)tf_number(0,bt_session.errors);else if(i==3)tf_number(0,bt_session.best_streak);
        else{n=tf_number(0,bt_session.seconds);tf_append(n," sec");}
        tf_text(sx+sw/2,sy+i*rh+3,sw/2-8,tf_buf,i==0?TF_GREEN:i==2?TF_RED:TF_YELLOW,BT_F_TITLE);
        if(i<4)bx_fill(sx+4,sy+(i+1)*rh-1,sx+sw-5,sy+(i+1)*rh-1,tf_ink(TF_CYAN));
    }
    sy+=5*rh+7;
    if(sy+72<=tf_bottom-35){ovl_tm_weak(sx,sy,sw,72,p);sy+=78;}
    else{n=tf_append(0,"Weak keys: ");for(i=0;i<3;i++){k=bt_weak(i);if(k){tf_char[0]=k;tf_char[1]=' ';tf_char[2]=0;n=tf_append(n,tf_char);}}tf_line(sx,sy,sw,tf_buf);sy+=tf_th+3;}
    n=(tf_bottom-36-sy)/(tf_th+2);if(n>2)n=2;
    ovl_tf_lines(sx,sy,sw,n,!bt_session.attempts?"No keys were entered. Try F and J, or Retry the test.":bt_session.accuracy>=95?"Great success! Continue, or practice the next keys.":"Aim for 95% accuracy. Practice these keys before adding speed.");sy+=n*(tf_th+2)+3;
    for(i=0;i<4&&sy+tf_th<tf_bottom-35;i++){
        if(!bt_reaction_history(i)[0])break;n=tf_append(0,"Borat: ");tf_append(n,bt_reaction_history(i));tf_line(sx,sy,sw,tf_buf);sy+=tf_th+2;
    }
    c=(w-12)/4;sy=tf_bottom-29;
    tf_button(x,sy,c,25,"Retry",BT_A_RETRY,1);tf_button(x+c+4,sy,c,25,"Next lesson",BT_A_NEXT,0);tf_button(x+2*(c+4),sy,c,25,"Weak keys",BT_A_WEAK,0);tf_button(x+3*(c+4),sy,c,25,"Menu",BT_A_BACK,0);
}
static void ovl_tm_settings(int x,int y,int w,int h,int rich)
{
    int c,row,bh,n,left,i,yy,a,on;const char *s;
    if(!rich){
        c=(w-8)/3;bh=tf_compact?(tf_h<140?13:15):25;row=bh+2;
        n=tf_append(0,"M Music: ");tf_append(n,bt_store.music?"ON":"OFF");tf_button(x,y,c,bh,tf_buf,BT_A_MUSIC,bt_store.music);
        n=tf_append(0,"E Effects: ");tf_append(n,bt_store.effects?"ON":"OFF");tf_button(x+c+4,y,c,bh,tf_buf,BT_A_EFFECTS,bt_store.effects);
        n=tf_append(0,"K Key ticks: ");tf_append(n,bt_store.keyclick?"ON":"OFF");tf_button(x+2*(c+4),y,c,bh,tf_buf,BT_A_KEYCLICK,bt_store.keyclick);y+=row;
        n=tf_append(0,"R Roast: ");tf_append(n,bt_store.roast==0?"Gentle":bt_store.roast==1?"Classic":"Extra");tf_button(x,y,c,bh,tf_buf,BT_A_ROAST,0);
        n=tf_append(0,"D Display: ");tf_append(n,bt_store.display?"CGA color":"Native");tf_button(x+c+4,y,c,bh,tf_buf,BT_A_DISPLAY,0);tf_button(x+2*(c+4),y,c,bh,"T Test music",BT_A_TEST_MUSIC,0);y+=row;
        tf_button(x,y,c,bh,bt_full?"F6 Leave full":"F6 Fullscreen",BT_A_FULL,0);tf_button(x+c+4,y,c,bh,"Save settings",BT_A_SAVE,0);tf_button(x+2*(c+4),y,c,bh,"Back",BT_A_BACK,0);y+=row;
    }else{
        left=w/2-4;ovl_tm_panel(x,y,left,h-37,"SOUND");ovl_tm_panel(x+left+8,y,w-left-8,h-37,"DISPLAY");
        bh=32;
        for(i=0;i<3;i++){
            a=i==0?BT_A_MUSIC:i==1?BT_A_EFFECTS:BT_A_KEYCLICK;on=i==0?bt_store.music:i==1?bt_store.effects:bt_store.keyclick;
            s=i==0?"M Music: ":i==1?"E Effects: ":"K Key ticks: ";yy=y+31+i*40;
            n=tf_append(0,s);tf_append(n,on?"ON":"OFF");tf_button(x+8,yy,left-16,bh,tf_buf,a,on);
        }
        yy=y+155;n=tf_append(0,"R Roast: ");tf_append(n,bt_store.roast==0?"Gentle":bt_store.roast==1?"Classic":"Extra");
        tf_button(x+8,yy,left-16,bh,tf_buf,BT_A_ROAST,1);
        if(h>=300){c=(left-28)/3;for(i=0;i<3;i++){
            gk_box(x+8+i*(c+6),yy+40,c,22,bt_store.roast==i?GK_RAISED2:GK_SUNKEN,tf_ground());
            gk_textbox(x+8+i*(c+6),yy+40,c,22,i==0?"Gentle":i==1?"Classic":"Extra",tf_ink(bt_store.roast==i?TF_YELLOW:TF_WHITE),BT_F_SMALL,GK_CENTRE|GK_FIT);}}
        c=x+left+16;n=tf_append(0,"D Display: ");tf_append(n,bt_store.display?"CGA color":"Native");
        tf_button(c,y+31,w-left-24,bh,tf_buf,BT_A_DISPLAY,0);
        tf_button(c,y+75,w-left-24,bh,bt_full?"F6 Leave full":"F6 Fullscreen",BT_A_FULL,bt_full);
        tf_button(c,y+119,w-left-24,bh,"T Test music",BT_A_TEST_MUSIC,0);
        if(h>=270)ovl_bt_portrait_scene(c,y+160,w-left-24,h-205);
        yy=y+h-29;c=(w-6)/2;tf_button(x,yy,c,26,"Save settings",BT_A_SAVE,1);tf_button(x+c+6,yy,c,26,"Back",BT_A_BACK,0);
        y+=h-58;x+=4;w=left-8;
    }
    tf_vsx=x+4;tf_vsy=y;tf_text(tf_vsx,y,w-8,tf_soundstatus(),TF_YELLOW,tf_font);
}
static void ovl_tm_setup(int x,int y,int w,int h,int rich,int challenge,BTProfile *p)
{
    int c,i,n,bh,side,yy;
    bh=tf_compact?(tf_h<140?13:15):27;side=rich?w/3:0;c=(w-side-8)/2;yy=y;
    if(challenge){
        c=(w-side-8)/3;
        for(i=0;i<3;i++){n=tf_number(0,i==0?1:i==1?3:5);tf_append(n," minute");tf_button(x+i*(c+4),yy,c,bh,tf_buf,BT_A_DURATION_BASE+i,bt_ch_duration==i);}yy+=bh+6;c=(w-side-4)/2;
    }
    for(i=0;i<4;i++)tf_button(x+(i%2)*(c+4),yy+(i/2)*(bh+4),c,bh,tf_group[i],(challenge?BT_A_DIFFICULTY_BASE:BT_A_PRACTICE_BASE)+i,!bt_practice_weak&&bt_ch_diff==i);
    yy+=2*(bh+4);
    if(rich){
        ovl_tm_mascot(x+w-side+8,y,side-8,h-36);
        if(challenge){
            n=tf_append(0,"Personal best: ");n=tf_number(n,p->record_wpm[bt_ch_duration][bt_ch_diff]);tf_append(n," WPM");tf_text(x,yy+3,w-side-8,tf_buf,TF_YELLOW,BT_F_TITLE);
            ovl_tf_lines(x,yy+31,w-side-8,3,"Three seconds to get ready. Pausing, losing focus or changing display makes a challenge unranked.");
        }else{ovl_tm_weak(x,yy+4,w-side-8,77,p);tf_button(x,yy+88,w-side-8,bh,"Practice weak keys",BT_A_WEAK,bt_practice_weak);}
        yy=y+h-29;c=(w-side-8)/2;
        tf_button(x,yy,c,26,challenge?"Start challenge":"Start practice",BT_A_START,1);tf_button(x+c+4,yy,c,26,"Back",BT_A_BACK,0);
    }else{
        if(!challenge)tf_button(x,yy,c,bh,"Practice weak keys",BT_A_WEAK,bt_practice_weak);
        tf_button(challenge?x:x+c+4,yy,challenge?w:c,bh,challenge?"Start challenge":"Start practice",BT_A_START,1);
    }
}
static void ovl_tm_home(int x,int y,int w,int h,BTProfile *p,int rich)
{
    int c,yy,i,n,bh,side;const char *s;
    side=rich?w/3:0;if(rich){ovl_tm_mascot(x,y,side,h);x+=side+12;w-=side+12;}
    ovl_tm_profile(x,y,w,p);y+=tf_compact?14:25;bh=tf_compact?15:30;c=(w-8)/2;
    for(i=0;i<6;i++){
        s=i==0?"Continue lesson":i==1?"Choose lesson":i==2?"Practice":i==3?"Timed challenge":i==4?"Profiles":"Records";
        yy=y+(i/2)*(bh+6);tf_button(x+(i%2)*(c+8),yy,c,bh,s,i==0?BT_A_START:i==1?BT_A_LESSONS:i==2?BT_A_PRACTICE:i==3?BT_A_CHALLENGE:i==4?BT_A_PROFILE:BT_A_RECORDS,i==0);
    }
    if(rich){
        y+=3*(bh+6)+16;tf_line(x,y,w,bt_lessons[bt_selection].name);y+=22;
        n=tf_append(0,"Lessons mastered: ");tf_number(n,ovl_tm_mastered(p));tf_text(x,y,w,tf_buf,TF_YELLOW,tf_font);y+=18;
        ovl_tm_bar(x,y,w,ovl_tm_mastered(p),24);y+=21;
        n=tf_append(0,"Personal best: ");n=tf_number(n,p->best_wpm);tf_append(n," WPM");tf_line(x,y,w,tf_buf);
    }
}
static void ovl_tm_other(int x,int y,int w,int h,int rich,BTProfile *p)
{
    static const char *help[]={"F/J bumps anchor your index fingers.","Correct keys advance. Wrong keys stay.","Backspace retracts; old errors stay.","WPM = accepted keys x 12 / seconds.","Shift uses the opposite hand.","Pause/focus/display makes unranked.","F1 Help F2 Lessons F3 Sound F4 Pause F6 Full", "Settings M/E/K/R/D/T; Practice 1-4."};
    int i,n,c,rh,bh,yy;
    bh=tf_compact?(tf_h<140?13:15):29;
    if(bt_screen==BT_S_PROFILE){
        c=rich?(w-8)/2:w;rh=rich?74:bh+3;
        for(i=0;i<4;i++){
            n=tf_number(0,i+1);n=tf_append(n,". ");tf_append(n,bt_store.profile[i].name);
            yy=y+(rich?i/2:i)*rh;
            tf_button(x+(rich?i%2:0)*(c+8),yy,c,rich?rh-6:bh,tf_buf,BT_A_PROFILE_BASE+i,i==bt_store.selected);
        }
        if(rich){y+=2*rh+10;ovl_tf_lines(x,y,w,2,bt_profile_edit?"Type name; Enter saves.":"Select a profile. First key replaces its name.");y+=38;ovl_tm_profile(x,y,w,p);}
    }else if(bt_screen==BT_S_PAUSE){
        if(rich){c=w/3;ovl_tm_mascot(x,y,c,h);x+=c+12;w-=c+12;}
        if(!tf_compact){tf_line(x,y,w,"Your passage and score are safe.");y+=24;}
        c=(w-8)/2;tf_button(x,y,c,bh,"Resume (F4)",BT_A_PAUSE,1);tf_button(x+c+8,y,c,bh,"Restart",BT_A_RETRY,0);y+=bh+5;
        tf_button(x,y,c,bh,"Display / Fullscreen",BT_A_FULL,0);tf_button(x+c+8,y,c,bh,"Exit to menu",BT_A_BACK,0);
    }else{
        rh=(h-3)/(rich?4:8);if(rh>64)rh=64;c=rich?(w-8)/2:w;
        for(i=0;i<8;i++){
            yy=y+(rich?i/2:i)*rh;n=x+(rich?i%2:0)*(c+8);
            if(rich)ovl_tm_panel(n,yy,c,rh-6,"");
            ovl_tf_lines(n+4,yy+4,c-8,rich?3:1,help[i]);
        }
    }
}
static void ovl_tf_drawbody(void)
{
    int x,y,w,h,rich,n,c,i;BTProfile *p;
    p=&bt_store.profile[bt_store.selected];tf_nhit=0;
    x=tf_x+12;y=tf_top+5;w=tf_w-24;rich=tf_w>=600&&tf_h>=360;
    tf_text(x,y,w,tf_screenname[bt_screen],TF_YELLOW,tf_title);
    if(rich&&bt_screen==BT_S_RECORDS){
        n=tf_append(0,p->name);n=tf_append(n,"  Practice: ");n=tf_number(n,p->practice_minutes);tf_append(n," min");
        tf_text(x+w/2,y+6,w/2,tf_buf,TF_WHITE,BT_F_SMALL);
    }
    y+=tf_compact?14:28;h=tf_bottom-y-4;
    if(bt_screen==BT_S_HOME)ovl_tm_home(x,y,w,h,p,rich);
    else if(bt_screen==BT_S_LESSONS)ovl_tm_lessons(x,y,w,h,p,rich);
    else if(bt_screen==BT_S_PRACTICE||bt_screen==BT_S_CHALLENGE)ovl_tm_setup(x,y,w,h,rich,bt_screen==BT_S_CHALLENGE,p);
    else if(bt_screen==BT_S_RECORDS)ovl_tm_records(x,y,w,h,p,rich);
    else if(bt_screen==BT_S_SETTINGS)ovl_tm_settings(x,y,w,h,rich);
    else if(bt_screen==BT_S_RESULTS){
        if(!tf_compact){ovl_tf_resultcard(x,y,w,p);return;}
        tf_line(x,y,w,!bt_session.attempts?"No keys entered":bt_session.ranked?"Completed":"Practice / unranked");y+=11;
        n=tf_append(0,"WPM ");n=bt_session.seconds?tf_number(n,bt_session.wpm):tf_append(n,"-");n=tf_append(n,"  Accuracy ");
        if(bt_session.attempts){n=tf_number(n,bt_session.accuracy);n=tf_append(n,"%");}else n=tf_append(n,"-");n=tf_append(n,"  Errors ");tf_number(n,bt_session.errors);tf_line(x,y,w,tf_buf);y+=11;
        n=tf_append(0,"Time ");n=tf_number(n,bt_session.seconds);n=tf_append(n,"s  Best streak ");tf_number(n,bt_session.best_streak);tf_line(x,y,w,tf_buf);y+=11;
        for(i=0;i<4&&y+21<tf_bottom;i++){n=tf_append(0,"Borat: ");tf_append(n,bt_reaction_history(i));tf_line(x,y,w,tf_buf);y+=10;}
        c=(w-12)/4;y=tf_bottom-17;
        tf_button(x,y,c,15,"Retry",BT_A_RETRY,1);tf_button(x+c+4,y,c,15,"Next lesson",BT_A_NEXT,0);tf_button(x+2*(c+4),y,c,15,"Weak keys",BT_A_WEAK,0);tf_button(x+3*(c+4),y,c,15,"Menu",BT_A_BACK,0);
    }else ovl_tm_other(x,y,w,h,rich,p);
}
static int ovl_tf_settings_delta(unsigned diff)
{
    int i,a,bit;
    for(i=0;i<tf_nhit;i++){a=tf_hit[i][4];bit=0;
        if(a==BT_A_MUSIC)bit=1;else if(a==BT_A_EFFECTS)bit=2;
        else if(a==BT_A_KEYCLICK)bit=8;else if(a==BT_A_ROAST)bit=48;else if(a==BT_A_DISPLAY)bit=64;else if(a==BT_A_FULL)bit=128;
        if(diff&bit)tf_render(tf_hit[i][0],tf_hit[i][1],tf_hit[i][2],a==BT_A_ROAST&&tf_w>=600&&tf_h>=360?78:tf_hit[i][3],3,0);
    }return 1;
}
