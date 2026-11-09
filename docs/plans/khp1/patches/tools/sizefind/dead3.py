import sys,re,collections
sys.path.insert(0,sys.argv[2])
from insns import parse
exec(open(sys.argv[2]+'/dead2.py').read().split('# routines')[0].split('I=parse(sys.argv[1])',1)[1])
I=parse(sys.argv[1])
LAD=['es','bp','di','si','dx','cx','bx','ax']
# label index
lab={}
for idx,x in enumerate(I):
    for l in x['labs']:
        key=(x['glob'],l) if l.startswith('.') else (None,l)
        lab[key]=idx
def target(idx,name):
    g=I[idx]['glob']
    if name.startswith('.'): return lab.get((g,name))
    if '.' in name:
        return lab.get((None,name.split('.')[0])) if False else lab.get((name.split('.')[0],'.'+name.split('.',1)[1]))
    return lab.get((None,name))
def live_from(j,reg,seen,budget):
    while j<len(I) and budget[0]>0:
        budget[0]-=1
        if j in seen: return False
        seen.add(j)
        c=I[j]['code'].lower(); t=toks(c); op=t[0] if t else ''
        rw=reads_writes(c,reg)
        if rw=='W': return False
        if rw in ('R','?'): return True
        if op in ('ret','retf','iret'): return True
        if op=='jmp':
            m=re.match(r'^jmp\s+(?:short\s+|near\s+)?(kretc?|kretm|kretfc?)_(es|bp|di|si|dx|cx)$',c)
            if m: return reg not in LAD[LAD.index(m.group(2)):]
            m=re.match(r'^jmp\s+(?:short\s+|near\s+)?([.\w]+)$',I[j]['code'])
            if not m: return True
            k=target(j,m.group(1))
            if k is None: return True
            j=k; continue
        if (op.startswith('j') and op!='jmp') or op.startswith('loop') or op=='jcxz':
            m=re.match(r'^\w+\s+(?:short\s+|near\s+)?([.\w]+)$',I[j]['code'])
            if not m: return True
            k=target(j,m.group(1))
            if k is None: return True
            if live_from(k,reg,seen,budget): return True
            j+=1; continue
        if re.match(r'^(db|dw)\b',c): return True
        j+=1
    return True
glob_first={}
for idx,x in enumerate(I):
    g=x['glob']
    if g not in glob_first and any(l==g for l in x['labs']): glob_first[g]=idx
refs=collections.defaultdict(list)
for idx,x in enumerate(I):
    t=toks(x['code'])
    for tok in t[1:]:
        if tok in glob_first: refs[tok].append((idx,t[0]))
TERM=re.compile(r'^(ret|retf|iret|jmp\b)')
for g,fi in glob_first.items():
    x=I[fi]
    if x['sect'] not in ('.text','.cold'): continue
    # fall-through entry?
    if fi>0 and I[fi-1]['sect']==x['sect'] and not TERM.match(I[fi-1]['code'].lower()) and not re.match(r'^(db|dw|times|resb|resw)',I[fi-1]['code'].lower()):
        ft=True
    else: ft=False
    pushes=[]; k=fi; pre=[]
    while k<len(I) and k<fi+6 and (k==fi or not I[k]['labs']):
        c=I[k]['code'].lower()
        m=re.match(r'^push (ax|bx|cx|dx|si|di|bp)$',c)
        if m: pushes.append((m.group(1),list(pre))); k+=1; continue
        if pushes: break
        pre.append(c); k+=1
    if not pushes: continue
    R=refs.get(g,[])
    if not R or any(op!='call' for _,op in R): continue
    for reg,before in pushes:
        if any(reads_writes(c,reg) not in (None,) for c in before): continue
        if all(not live_from(ci+1,reg,set(),[200]) for ci,_ in R):
            print(x['sect'],hex(x['addr']),g,reg,len(R),'FALLTHRU' if ft else '')
