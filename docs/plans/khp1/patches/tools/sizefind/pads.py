import re,sys
sys.path.insert(0,sys.argv[3])
lst,mapf=sys.argv[1],sys.argv[2]
rx=re.compile(r'^\s*(\d+)\s+([0-9A-F]{8})\s+([0-9A-F]+)\s+(<\d+>)?\s*(.*)$')
sect='.text'; cur=None
pads=[]
lines=open(lst,errors='replace').read().split('\n')
for i,ln in enumerate(lines):
    m=rx.match(ln)
    src = m.group(5) if m else re.sub(r'^\s*\d+\s+(<\d+>)?\s*','',ln)
    code=src.split(';')[0].strip()
    sm=re.match(r'^(section|segment)\s+(\S+)',code)
    if sm: sect=sm.group(2)
    lm=re.match(r'^([A-Za-z_][\w]*):',code)
    if lm: cur=lm.group(1)
    if m and 'PARITY' in src.upper() and re.match(r'^(\w+:\s*)?(nop|db 0|db 90h|db 0x90)$',code):
        # continuation comment lines mention parity; also check next lines
        pads.append((sect,int(m.group(2),16),m.group(3),cur,code))
    elif m and re.match(r'^(\w+:\s*)?(nop|db 0|db 90h)$',code):
        # check following comment lines for PARITY
        ctx=' '.join(lines[i:i+2])
        if 'PARITY' in ctx.upper():
            pads.append((sect,int(m.group(2),16),m.group(3),cur,code+' (ctx)'))
for p in pads: print(p[0],hex(p[1]),p[2],p[3],p[4])
