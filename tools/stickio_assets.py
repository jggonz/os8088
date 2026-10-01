#!/usr/bin/env python3
"""Compile original joint-based stick figures, monochrome tiles, 30 courses and music.
No rasterizer, mixer, random generator or asset IO runs on the guest.
"""
from pathlib import Path
import math, argparse, json, wave, struct
from stickio_courses import validate_course, authored_course, encode_map, preview_svg, MATERIALS
ROOT=Path(__file__).resolve().parents[1]

def line(p,a,b):
    x,y=a; xx,yy=b; dx=abs(xx-x); sx=1 if x<xx else -1
    dy=-abs(yy-y); sy=1 if y<yy else -1; e=dx+dy
    while True:
        if 0<=x<len(p[0]) and 0<=y<len(p): p[y][x]=0
        if (x,y)==(xx,yy): break
        v=2*e
        if v>=dy: e+=dy; x+=sx
        if v<=dx: e+=dx; y+=sy

def pose(i):
    p=[[1]*16 for _ in range(24)]
    # Eight gait samples: contact, recoil, passing, high point, opposite leg.
    legs=[((10,17),(13,22),(5,17),(2,22)),((10,18),(11,22),(4,16),(2,20)),
          ((8,18),(8,22),(6,15),(4,18)),((6,18),(5,22),(10,16),(9,19)),
          ((4,17),(2,22),(10,17),(13,22)),((5,16),(3,20),(10,18),(11,22)),
          ((8,15),(7,18),(8,18),(8,22)),((10,16),(11,19),(6,18),(5,22))]
    bob=[0,1,0,-1,0,1,0,-1][i%8] if i<8 else (2 if i==11 else 0)
    hip=(8,13+bob); neck=(8,7+bob)
    # The head stays above the pelvis; limbs bend at explicit elbows and knees.
    line(p,(7,1+bob),(9,1+bob)); line(p,(5,3+bob),(5,4+bob))
    line(p,(6,2+bob),(5,3+bob)); line(p,(9,1+bob),(11,3+bob))
    line(p,(11,3+bob),(11,4+bob)); line(p,(5,4+bob),(7,6+bob))
    line(p,(7,6+bob),(9,6+bob)); line(p,(9,6+bob),(11,4+bob))
    p[3+bob][10]=0
    if i<8:
        k1,f1,k2,f2=legs[i]; swing=[-3,-2,0,2,3,2,0,-2][i]
        arms=[((8-swing,10+bob),(8-swing-2,12+bob)),((8+swing,10+bob),(8+swing+2,8+bob))]
    elif i==8: # relaxed idle, weight on one leg
        k1,f1,k2,f2=(7,18),(6,22),(10,18),(11,22)
        arms=[((5,10),(6,13)),((11,10),(10,13))]
    elif i==9: # takeoff/rise: one arm reaching, rear knee flexed
        k1,f1,k2,f2=(10,17),(12,20),(5,16),(3,18)
        arms=[((5,8),(4,5)),((11,10),(13,8))]
    elif i==10: # falling, arms balance, feet prepare for contact
        k1,f1,k2,f2=(5,18),(4,22),(11,18),(12,22)
        arms=[((4,9),(2,7)),((12,9),(14,7))]
    else: # landing recoil: bent knees absorb the impact
        hip=(8,15); neck=(8,9); k1,f1,k2,f2=(4,18),(5,22),(12,18),(11,22)
        arms=[((4,12),(3,15)),((12,12),(13,15))]
    line(p,neck,hip)
    for elbow,hand in arms: line(p,neck,elbow); line(p,elbow,hand)
    for knee,foot in [(k1,f1),(k2,f2)]:
        line(p,hip,knee); line(p,knee,foot); line(p,foot,(min(15,foot[0]+2),foot[1]))
    return p

def pack(p):
    return bytes(sum((3 if bit else 0)<<(6-2*j) for j,bit in enumerate(row[x:x+4]))
                 for row in p for x in range(0,len(row),4))

def tiles():
    out=[]
    for t in range(10):
        p=[[1]*16 for _ in range(16)]
        if t==1: # soil: constant columns allow whole floor to stay cached in a scroll
            for y in (0,2,7,15): p[y]=[0]*16
        if t in (2,3):
            for y in (0,15): p[y]=[0]*16
            for y in range(16): p[y][0]=p[y][15]=0
            if t==2:
                line(p,(0,7),(15,7)); line(p,(7,0),(7,7)); line(p,(3,7),(3,15))
            else:
                for a,b in [((5,4),(10,4)),((10,4),(10,7)),((10,7),(7,9)),((7,9),(7,10)),((7,12),(7,12))]: line(p,a,b)
        if t==4:
            for a,b in [((7,3),(11,7)),((11,7),(7,12)),((7,12),(3,7)),((3,7),(7,3)),((7,5),(7,10))]: line(p,a,b)
        if t==5:
            for x in (0,8): line(p,(x,15),(x+4,4)); line(p,(x+4,4),(x+7,15))
        if t==6:
            for y in (5,14): line(p,(1,y),(14,y))
            for y in (7,10,12): line(p,(4,y),(11,y+1))
        if t in (7,8):
            line(p,(4,0),(4,15)); line(p,(4,1),(13,1)); line(p,(13,1),(10,6)); line(p,(10,6),(4,6))
            if t==8: line(p,(7,2),(9,4))
        if t==9: p[0]=[0]*16; p[2]=[0]*16
        out.append(p)
    return out

NAMES=['FIRST FOOTSTEPS','BRICK HOP','THE DITCH','SPRING STREET','MEADOW MARCH',
       'INK WORKS','OVER THE FOUNDRY','HOPPER HALL','CHECKPOINT CHASE','BLACK IRON',
       'PAPER HEIGHTS','CROSSWINDS','WINGED WALK','HIGH ROAD','SKYLINE',
       'DEEP LINES','LOW CEILING','SPIKE ALLEY','CAVERN SPRINGS','NIGHT SHIFT',
       'CLOCKWORK','STAIR RUN','DOUBLE TROUBLE','THE GAUNTLET','WHITE KNUCKLES',
       'LAST ASCENT','HOP AND GLIDE','NARROW ESCAPES','FINAL MILE','STICKIO SUMMIT']

def courses():
    levels=[]
    for n in range(30):
        width=64+(n//5)*16+(n%5)*4
        m=[[0]*8 for _ in range(width)]
        for col in m: col[6]=col[7]=1
        enemies=[]; difficulty=n//5
        # Authored motif grammar; each course has a distinct arrangement, no runtime RNG.
        for j,x in enumerate(range(12,width-10,12)):
            kind=(j+n)%6
            if kind==0 and n>=2:
                gap=2+(difficulty>=3)
                for xx in range(x,x+gap): m[xx][6]=m[xx][7]=0
                for xx in range(x-1,x+gap+1): m[xx][3]=4
            elif kind==1:
                height=1+(difficulty>=2)
                for xx in range(x,x+2):
                    for y in range(6-height,6): m[xx][y]=2
                m[x][3]=3
            elif kind==2:
                for xx in range(x,x+3): m[xx][3 if difficulty>=2 else 4]=9
                m[x+1][2 if difficulty>=2 else 3]=4
                if n>=3: m[x-1][5]=6
            elif kind==3 and n>=7:
                m[x][5]=5
                if difficulty>=3: m[x+1][5]=5
                for xx in range(x-1,x+3): m[xx][3]=4
            else:
                m[x][3]=3; m[x+2][4]=4
                if n>=1:
                    enemy_type=1 if n<7 else 1+(j+difficulty)%min(3,1+n//7)
                    # The old flyer ignored its y=72 record and used y=28..60.
                    # Make that actual patrol height explicit during migration.
                    enemies.append([x*16+32,28 if enemy_type==3 else 72,enemy_type,-1])
        # Protected spawn, checkpoint, and finish zones.
        mid=width//2
        for x in list(range(0,8))+list(range(mid-2,mid+3))+list(range(width-7,width)):
            m[x]=[0]*6+[1,1]
        m[mid][5]=8; m[width-4][5]=7
        enemies=[e for e in enemies if abs(e[0]//16-mid)>3][:20]
        levels.append(validate_course(dict(id='course-%02d'%(n+1), name=NAMES[n],width=width,
            map=m,enemies=enemies,world=n//5,music=n//5,environment='surface',room='main',
            spawn=[32,72],checkpoint=[mid*16,72],exit=[width-4,5])))
    return levels

# Original tunes: six 32-note phrases with contrasting contour and bass rhythm.
TUNES=[[0,4,7,12,7,4,2,5,9,5,2,-1,0,4,7,4,5,9,12,9,7,4,2,-1,0,7,4,2,0,-1,0,99],
       [0,3,7,10,7,3,0,99,2,5,9,12,9,5,2,99,3,7,10,14,12,10,7,3,2,5,7,5,3,2,0,99],
       [12,7,9,12,14,9,7,99,9,5,7,9,12,7,5,99,7,4,5,7,9,5,4,99,5,2,4,7,2,-1,0,99],
       [0,7,3,7,0,7,3,10,-2,5,2,5,-2,5,2,9,-4,3,0,3,-4,3,0,7,-2,5,2,9,7,3,0,99],
       [0,2,4,7,9,7,4,2,5,7,9,12,14,12,9,7,4,5,7,11,12,11,7,5,2,4,7,9,7,4,2,99],
       [0,7,12,16,14,12,7,4,5,12,17,16,14,12,9,5,7,14,19,17,16,14,11,7,12,7,9,4,5,2,0,99]]
def hz(n,base=262): return 0 if n==99 else round(base*2**(n/12))

def generate(out,preview=False,course=None):
    out.mkdir(parents=True,exist_ok=True)
    s=['; Generated by tools/stickio_assets.py. Original Stickio artwork and music.']
    def data(name,b):
        s.append(name+':')
        for i in range(0,len(b),24): s.append(' db '+','.join(str(v) for v in b[i:i+24]))
    sprites=[pose(i) for i in range(12)]
    enemy=[]
    for kind in range(3):
        for frame in range(2):
            p=[[1]*16 for _ in range(24)]
            if kind==0:
                for a,b in [((4,10),(11,10)),((11,10),(13,18)),((13,18),(2,18)),((2,18),(4,10)),((4,19),(2+frame*3,22)),((11,19),(13-frame*3,22))]: line(p,a,b)
                p[13][5]=p[13][10]=0
            elif kind==1:
                for a,b in [((7,5),(12,10)),((12,10),(10,17)),((10,17),(4,17)),((4,17),(2,10)),((2,10),(7,5)),((4,18),(1,20-frame*2)),((1,20-frame*2),(5,22)),((10,18),(14,20-frame*2)),((14,20-frame*2),(10,22))]: line(p,a,b)
                p[10][6]=p[10][9]=0
            else:
                for a,b in [((6,10),(10,10)),((10,10),(12,14)),((12,14),(8,18)),((8,18),(4,14)),((4,14),(6,10)),((5,13),(0,7+frame*12)),((11,13),(15,7+frame*12))]: line(p,a,b)
                p[12][7]=p[12][9]=0
            enemy.append(p)
    data('st_sprites',b''.join(pack(p) for p in sprites+[[row[::-1] for row in p] for p in sprites]+enemy))
    # Deduplicate each tile's four byte-columns. Empty and soil are phase invariant.
    cols=[]; keys=[]
    for p in tiles():
        for x in range(0,16,4):
            c=bytes(pack([row[x:x+4]])[0] for row in p)
            if c not in cols: cols.append(c)
            keys.append(cols.index(c))
    data('st_tilekeys',bytes(keys)); data('st_columns',b''.join(cols))
    levels=[authored_course(course)] if course else courses()
    fixtures=[authored_course(p) for p in sorted((ROOT/'apps/stickio/levels').glob('*.json'))]
    for fixture in fixtures:
        (out/(fixture['id']+'.svg')).write_text(preview_svg(fixture))
    (out/'authored-fixtures.json').write_text(json.dumps(fixtures,indent=2)+'\n')
    s.append('ST_LEVEL_COUNT equ %d'%len(levels))
    s.append('st_levels:'); s.extend(' dw st_level%d'%i for i in range(len(levels)))
    for i,l in enumerate(levels):
        s+=['st_level%d:'%i,' dw %d,st_map%d,st_enemy%d,st_name%d'%(l['width'],i,i,i),' db %d,%d'%(len(l['enemies']),l['world']),
            ' dw %d,%d,%d,%d'%(*l['spawn'],*l['checkpoint'])]
        encoded=encode_map(l['map'])
        data('st_map%d'%i,encoded)
        s.append('st_enemy%d:'%i)
        for x,y,t,d in l['enemies']: s+=[' dw %d,%d'%(x,y),' db %d,%d'%(t,d&255)]
        s+=['st_name%d: db "%s",0'%(i,l['name'])]
    s+=['st_tunes:']; s.extend(' dw st_tune%d'%i for i in range(6))
    for i,t in enumerate(TUNES):
        s+=['st_tune%d:'%i,' dw '+','.join(str(hz(n)) for n in t),
            ' dw '+','.join(str(hz([0,-5,-3,-5][(j//8)%4],131)) for j in range(32)),
            ' dw '+','.join(str(hz(n-12)) for n in t)]
    # Fixed samples: 0.256 seconds at 8 kHz, ready to DMA. No guest mixer.
    for i,(f0,f1) in enumerate([(280,800),(1100,1800),(190,65),(900,260),(500,1200)]):
        b=bytes(max(0,min(255,round(128+45*(1-j/2048)*math.sin(2*math.pi*(f0*j/8000+(f1-f0)*j*j/(2*2048*8000)))))) for j in range(2048))
        data('st_pcm%d'%i,b)
    # Tiny 5x7 alphabet expands at build time to two CGA bytes per row.
    font={}
    raw=['01110/10001/10011/10101/11001/10001/01110','00100/01100/00100/00100/00100/00100/01110','01110/10001/00001/00010/00100/01000/11111','11110/00001/00001/01110/00001/00001/11110','00010/00110/01010/10010/11111/00010/00010','11111/10000/10000/11110/00001/00001/11110','01110/10000/10000/11110/10001/10001/01110','11111/00001/00010/00100/01000/01000/01000','01110/10001/10001/01110/10001/10001/01110','01110/10001/10001/01111/00001/00001/01110']
    alpha=['01110/10001/10001/11111/10001/10001/10001','11110/10001/10001/11110/10001/10001/11110','01111/10000/10000/10000/10000/10000/01111','11110/10001/10001/10001/10001/10001/11110','11111/10000/10000/11110/10000/10000/11111','11111/10000/10000/11110/10000/10000/10000','01111/10000/10000/10111/10001/10001/01111','10001/10001/10001/11111/10001/10001/10001','01110/00100/00100/00100/00100/00100/01110','00111/00010/00010/00010/00010/10010/01100','10001/10010/10100/11000/10100/10010/10001','10000/10000/10000/10000/10000/10000/11111','10001/11011/10101/10101/10001/10001/10001','10001/11001/10101/10011/10001/10001/10001','01110/10001/10001/10001/10001/10001/01110','11110/10001/10001/11110/10000/10000/10000','01110/10001/10001/10001/10101/10010/01101','11110/10001/10001/11110/10100/10010/10001','01111/10000/10000/01110/00001/00001/11110','11111/00100/00100/00100/00100/00100/00100','10001/10001/10001/10001/10001/10001/01110','10001/10001/10001/10001/10001/01010/00100','10001/10001/10001/10101/10101/10101/01010','10001/10001/01010/00100/01010/10001/10001','10001/10001/01010/00100/00100/00100/00100','11111/00001/00010/00100/01000/10000/11111']
    for ch,g in zip('0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ',raw+alpha): font[ch]=g
    b=bytearray()
    for c in range(32,91):
        rows=font.get(chr(c),'00000/'*6+'00000').split('/')
        p=[[1]*8]+[[1]+[0 if v=='1' else 1 for v in row]+[1,1] for row in rows]
        b.extend(pack(p))
    data('st_font',b)
    (out/'assets.inc').write_text('\n'.join(s)+'\n')
    (out/'levels.json').write_text(json.dumps(levels,indent=2)+'\n')
    (out/'manifest.json').write_text(json.dumps(dict(version=1,materials=MATERIALS,
        limits=dict(map_bytes=1280,actors=20,nearby_actors=6,footprints=7),
        courses=[{key:l[key] for key in ('id','name','room','spawn','checkpoint','exit','objects','rewards')}
                 for l in levels]),indent=2)+'\n')
    if preview:
        from PIL import Image, ImageDraw
        im=Image.new('RGB',(12*80,240),'white'); dr=ImageDraw.Draw(im)
        for i,p in enumerate(sprites):
            for y,row in enumerate(p):
                for x,v in enumerate(row):
                    if not v: dr.rectangle((i*80+x*4,20+y*4,i*80+x*4+3,20+y*4+3),fill='black')
            dr.text((i*80,122),str(i),fill='black')
        im.save(out/'poses.png')
        frames=[]
        for i in range(8):
            fr=Image.new('RGB',(128,128),'white'); fd=ImageDraw.Draw(fr)
            for y,row in enumerate(sprites[i]):
                for x,v in enumerate(row):
                    if not v: fd.rectangle((32+x*4,14+y*4,35+x*4,17+y*4),fill='black')
            frames.append(fr)
        frames[0].save(out/'run.gif',save_all=True,append_images=frames[1:],duration=90,loop=0)
        # Host audition only; the guest consumes the note tables, never this WAV.
        for k,t in enumerate(TUNES):
            with wave.open(str(out/('theme%d.wav'%k)),'wb') as w:
                w.setparams((1,2,22050,0,'NONE','not compressed'))
                for j,n in enumerate(t):
                    f=hz(n); bass=hz([0,-5,-3,-5][(j//8)%4],131)
                    samples=[int(7000*(1-0.4*x/4096)*((1 if math.sin(2*math.pi*f*x/22050)>0 else -1) if f else 0)+2500*math.sin(2*math.pi*bass*x/22050)) for x in range(4096)]
                    w.writeframes(struct.pack('<%dh'%len(samples),*samples))
    print('Stickio: %d levels, 24 articulated poses, %d cached tile columns, 6 themes'%(len(levels),len(cols)))
if __name__=='__main__':
    a=argparse.ArgumentParser(); a.add_argument('out',type=Path); a.add_argument('--preview',action='store_true'); a.add_argument('--course',type=Path)
    ns=a.parse_args(); generate(ns.out,ns.preview,ns.course)
