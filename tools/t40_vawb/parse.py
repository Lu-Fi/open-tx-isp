# parse sdk.dis into functions: name -> list of (addr, mnemonic, ops, relocs[(type,sym)])
import re
def load(path):
    funcs={}; cur=None; order=[]
    for l in open(path):
        m=re.match(r'^([0-9a-f]+) <(.*)>:$',l)
        if m:
            cur=m.group(2); funcs[cur]={'start':int(m.group(1),16),'ins':[]}; order.append(cur); continue
        if cur is None: continue
        r=re.match(r'\s+([0-9a-f]+): (R_MIPS_\w+)\s+(\S+)',l)
        if r:
            funcs[cur]['ins'][-1][3].append((r.group(2),r.group(3))); continue
        x=re.match(r'\s+([0-9a-f]+):\s+([0-9a-f]{8})\s+(\S+)\s*(.*)',l)
        if x:
            ops=x.group(4).split('<')[0].strip()
            funcs[cur]['ins'].append([int(x.group(1),16),x.group(3),ops,[],int(x.group(2),16)])
    return funcs
def loadsym(path):
    S={}
    for l in open(path):
        p=l.split()
        if len(p)>=5 and re.match(r'^[0-9a-f]{8}$',p[0]):
            S.setdefault(p[-3] if len(p)>=6 else p[-3],[])
            sec=p[-3]; S.setdefault(sec,[]).append((int(p[0],16),int(p[-2],16),p[-1],p[2] if len(p)>5 else ''))
    for s in S: S[s].sort()
    return S
