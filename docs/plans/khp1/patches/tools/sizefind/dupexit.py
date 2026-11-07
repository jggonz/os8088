import sys,re,collections
sys.path.insert(0,sys.argv[2])
from insns import parse
I=parse(sys.argv[1])
# group exits: sequence of pops/popf/pop es.. ending in ret/retf, preceded by an unconditional transfer or label
by=collections.defaultdict(list)
i=0
n=len(I)
while i<n:
    x=I[i]
    j=i; seq=[]
    while j<n and I[j]['sect']==x['sect'] and re.match(r'^(pop\s+\w+|popf|clc|stc)$',I[j]['code']) and (j==i or not I[j]['labs']):
        seq.append(I[j]['code']); j+=1
    if j<n and seq and re.match(r'^(ret|retf|iret)$',I[j]['code']) and (j==i or not I[j]['labs']) and I[j]['sect']==x['sect']:
        seq.append(I[j]['code'])
        nb=sum(len(I[k]['hx'])//2 for k in range(i,j+1))
        by[(x['sect'],x['glob'])].append((x['addr'],tuple(seq),nb))
        i=j+1; continue
    i+=1
for (sect,g),L in by.items():
    c=collections.Counter(s for a,s,nb in L)
    for s,k in c.items():
        if k>1:
            nb=[b for a,ss,b in L if ss==s][0]
            if nb>=3:
                addrs=[hex(a) for a,ss,b in L if ss==s]
                print(sect,g,k,nb,'save~%d'%((k-1)*(nb-2)),' '.join(s),addrs)
