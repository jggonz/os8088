# parse a nasm listing into a list of (sect, addr, hexbytes, code, glabel, llabels_before)
import re,sys
rx=re.compile(r'^\s*(\d+)\s+([0-9A-F]{8})\s+([0-9A-F()\[\]]+)\-?\s+(<\d+>)?\s*(.*)$')
rxs=re.compile(r'^\s*(\d+)\s+(<\d+>)?\s*(.*)$')
def parse(lst):
    out=[]; cur=None; sect='.text'; labs=[]
    for ln in open(lst,errors='replace'):
        ln=ln.rstrip('\n')
        m=rx.match(ln)
        if m:
            src=m.group(5); addr=int(m.group(2),16); hx=m.group(3)
        else:
            m2=rxs.match(ln)
            if not m2: continue
            src=m2.group(3); addr=None; hx=''
        code=src.split(';')[0].strip()
        if not code: continue
        while True:
            lm=re.match(r'^(\.{0,2}[A-Za-z_$?@][\w.$?@]*):\s*',code)
            if not lm: break
            name=lm.group(1)
            if not name.startswith('.'): cur=name
            labs.append(name)
            code=code[lm.end():]
        if not code: continue
        sm=re.match(r'^(section|segment)\s+(\S+)',code)
        if sm: sect=sm.group(2); continue
        if addr is None: continue
        if hx.startswith('<'): continue
        out.append(dict(sect=sect,addr=addr,hx=hx,code=code,glob=cur,labs=labs))
        labs=[]
    return out
