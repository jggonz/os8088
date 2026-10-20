#!/usr/bin/env python3
"""Offline artwork import; Pillow is needed only when revising the PNG masters.
Normal builds use the committed indexed files and need only Python's stdlib.
"""
import json
from pathlib import Path
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'apps/1942/art'

def main():
    atlas = Image.open(ART/'aircraft.png').convert('RGBA')
    terrain = Image.open(ART/'terrain.png').convert('RGB')
    colors = Image.new('RGB',(512,1024))
    colors.paste(terrain.resize((512,512)),(0,0))
    colors.paste(atlas.resize((512,512)),(0,512),atlas.resize((512,512)))
    quant = colors.quantize(colors=56)
    fixed = [[0,0,0],[8,30,64],[38,111,151],[77,175,183],
             [255,246,207],[224,91,42],[255,199,74],[151,43,28]]
    rgb = fixed + [quant.getpalette()[i:i+3] for i in range(0,168,3)]
    pal = Image.new('P',(1,1)); pal.putpalette(sum(rgb,[])+[0]*(768-len(rgb)*3))
    def indexed(img):
        return list(img.convert('RGB').quantize(palette=pal,dither=Image.Dither.NONE).getdata())
    sprites=[]
    names=['player','bankleft','bankright','roll','enemy','elite','blue','boss',
           'blast0','blast1','blast2','blast3','carrier','ship','pick','island']
    sizes=[(24,24)]*7+[(40,40)]+[(24,24)]*4+[(32,64),(24,56),(12,12),(40,40)]
    for i,(name,(w,h)) in enumerate(zip(names,sizes)):
        cell=atlas.crop((i%4*atlas.width//4,i//4*atlas.height//4,
                         (i%4+1)*atlas.width//4,(i//4+1)*atlas.height//4))
        alpha=cell.getchannel('A'); box=alpha.point(lambda a:255 if a>=128 else 0).getbbox()
        cell=cell.crop(box); cell.thumbnail((w,h),Image.Resampling.LANCZOS)
        target=Image.new('RGBA',(w,h));target.paste(cell,((w-cell.width)//2,(h-cell.height)//2))
        data=indexed(target); mask=list(target.getchannel('A').getdata())
        data=[max(v,1) if a>=100 else 0 for v,a in zip(data,mask)]
        sprites.append(dict(name=name,w=w,h=h,pixels=bytes(data).hex()))
    # Small, sharp effects are authored directly at their target resolution.
    for name,rows in [('shot',['44','44','66','66','55','55']),
                      ('bullet',['0660','6446','6446','0660']),
                      ('wake',['00222200','00022000'])]:
        sprites.append(dict(name=name,w=len(rows[0]),h=len(rows),pixels=bytes(int(c,16) for r in rows for c in r).hex()))
    # Original 5x7 stencil alphabet; a blank final row gives an 8px cell.
    chars='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-:/'
    glyphs=['0E11131519110E','040C040404040E','0E11010204081F','1E01010E01011E','02060A121F0202','1F10101E01011E','0E10101E11110E','1F010204080808','0E11110E11110E','0E11110F01010E',
    '0E11111F111111','1E11111E11111E','0F10101010100F','1E11111111111E','1F10101E10101F','1F10101E101010','0F10101711110F','1111111F111111','0E04040404040E','0702020212120C','11121418141211','1010101010101F','111B1515111111','11191915131311','0E11111111110E','1E11111E101010','0E11111115120D','1E11111E141211','0F10100E01011E','1F040404040404','1111111111110E','11111111110A04','11111115151B11','11110A040A1111','11110A04040404','1F01020408101F','0000001F000000','00040000040000','01010204081010']
    for c in range(32,91):
        rows=bytes.fromhex(glyphs[chars.index(chr(c))]) if chr(c) in chars else bytes(7)
        pix=[4 if x<5 and y<7 and rows[y]&(16>>x) else 0 for y in range(8) for x in range(8)]
        sprites.append(dict(name='font%d'%c,w=8,h=8,pixels=bytes(pix).hex()))
    (ART/'sprites.json').write_text(json.dumps(sprites,indent=2)+'\n')
    for i,name in enumerate(('SEA','REEF','PORT')):
        panel=terrain.crop((i*terrain.width//3,0,(i+1)*terrain.width//3,terrain.height))
        data=bytearray(indexed(panel.resize((256,240),Image.Resampling.LANCZOS)))
        data[:256*16]=bytes([1])*(256*16)
        (ART/(name.lower()+'.idx')).write_bytes(data)
    # Four-color art is packed against one map; C changes real CGA registers.
    cmap=[]
    for r,g,b in rgb:
        light=(r*3+g*6+b)/10
        cmap.append(3 if light>177 else 2 if r>g*1.18 and r>80 else 1 if light>73 else 0)
    cmap[4]=3;cmap[5]=2;cmap[6]=3
    out={'rgb':rgb,'cga_map':cmap,'cga':[{'name':'Ocean','register':49},
         {'name':'Jungle','register':17},{'name':'Night','register':32}]}
    (ART.parent/'palette.json').write_text(json.dumps(out,indent=2)+'\n')
    print('Imported 3 stage backdrops and %d sprite frames, %d VGA colors'%(len(sprites),len(rgb)))
if __name__=='__main__':main()
