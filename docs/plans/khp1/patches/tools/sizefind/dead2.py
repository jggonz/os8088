import sys,re,collections
sys.path.insert(0,sys.argv[2])
from insns import parse
I=parse(sys.argv[1])
R16=['ax','bx','cx','dx','si','di','bp','es','ds']
SUB={'ax':['ax','al','ah'],'bx':['bx','bl','bh'],'cx':['cx','cl','ch'],'dx':['dx','dl','dh'],
     'si':['si'],'di':['di'],'bp':['bp'],'es':['es'],'ds':['ds']}
def toks(c):
    return re.findall(r'[a-z_.][\w.]*',c.lower())
def reads_writes(c,reg):
    """return 'R' if reads reg (or part), 'W' if fully writes w/o reading, None if not mentioned, '?' unknown"""
    cl=c.lower()
    t=toks(cl)
    names=SUB[reg]
    if not any(n in t for n in names): 
        # implicit uses
        op=t[0] if t else ''
        imp={'ax':['lodsb','lodsw','stosb','stosw','mul','imul','div','idiv','cbw','cwd','xlatb','in','out','scasb','scasw','aam','aad','daa','das','aaa','aas','lahf','sahf','int','call','retf','iret'],
             'cx':['rep','repe','repne','repz','repnz','loop','loope','loopne','loopz','loopnz','jcxz','int','call'],
             'si':['lodsb','lodsw','movsb','movsw','cmpsb','cmpsw','rep','int','call'],
             'di':['stosb','stosw','movsb','movsw','scasb','scasw','cmpsb','cmpsw','rep','int','call'],
             'dx':['cwd','mul','imul','div','idiv','in','out','int','call'],
             'bx':['xlatb','int','call'],'es':['stosb','stosw','movsb','movsw','scasb','scasw','cmpsb','cmpsw','int','call'],
             'ds':['lodsb','lodsw','movsb','movsw','int','call','xlatb'],'bp':['int','call']}
        if op in imp.get(reg,[]): return '?'
        if op in ('push','pop') : return None
        # memory addressing through reg e.g. [bx] covered by tokens
        return None
    op=t[0]
    # full writes
    if op=='mov' and cl.split(None,1)[1].split(',')[0].strip()==reg : 
        # mov reg, X  where X doesn't involve reg
        rest=cl.split(',',1)[1] if ',' in cl else ''
        if not any(n in toks(rest) for n in names): return 'W'
        return 'R'
    if op=='pop' and t[1]==reg: return 'W'
    if op in ('xor','sub') and len(t)>=3 and t[1]==reg and t[2]==reg: return 'W'
    if op=='lea' and t[1]==reg: 
        rest=cl.split(',',1)[1]
        if not any(n in toks(rest) for n in names): return 'W'
    if op in ('les','lds') and t[1]==reg:
        rest=cl.split(',',1)[1]
        if not any(n in toks(rest) for n in names): return 'W'
    return 'R'
# routines
glob_first={}
for idx,x in enumerate(I):
    g=x['glob']
    if g not in glob_first and any(l==g for l in x['labs']): glob_first[g]=idx
refs=collections.defaultdict(list)
for idx,x in enumerate(I):
    t=toks(x['code'])
    for tok in t[1:]:
        if tok in glob_first: refs[tok].append((idx,t[0]))
def live_after(idx,reg,depth=0):
    # scan forward from idx+1 in straight line
    j=idx+1
    steps=0
    while j<len(I) and steps<40:
        c=I[j]['code'].lower(); t=toks(c); op=t[0] if t else ''
        if I[j]['labs'] and steps>0:
            # a label: someone may jump here; still straight-line for this path - continue
            pass
        rw=reads_writes(c,reg)
        if rw=='W': return False
        if rw in ('R','?'): return True
        if op in ('ret','retf','iret'): return True
        if op=='jmp':
            m=re.match(r'^jmp\s+(?:short\s+|near\s+)?(kretc?|kretm|kretfc?)_(es|bp|di|si|dx|cx)$',c)
            if m:
                LAD=['es','bp','di','si','dx','cx','bx','ax']
                return reg not in LAD[LAD.index(m.group(2)):]
            return True
        if op.startswith('j') or op.startswith('loop'): return True
        j+=1; steps+=1
    return True
out=[]
for g,fi in glob_first.items():
    x=I[fi]
    if x['sect'] not in ('.text','.cold'): continue
    # prologue pushes
    pushes=[]
    k=fi
    while k<len(I) and re.match(r'^push (ax|bx|cx|dx|si|di|bp|es|ds)$',I[k]['code'].lower()) and (k==fi or not I[k]['labs']):
        pushes.append(I[k]['code'].split()[1].lower()); k+=1
    if not pushes: continue
    R=refs.get(g,[])
    if not R: continue
    if any(op!='call' for _,op in R): continue
    for reg in pushes:
        if reg in ('ds','es'): continue
        if all(not live_after(ci,reg) for ci,_ in R):
            out.append((x['sect'],hex(x['addr']),g,reg,len(R)))
for o in out: print(*o)
