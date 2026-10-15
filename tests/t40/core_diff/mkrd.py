import re,sys
src=open(sys.argv[1]).read().split('\n'); fns=open(sys.argv[2]).read().split(); out=sys.argv[3]
def body(name):
    for i,l in enumerate(src):
        if re.match(r'^[a-zA-Z].*\b'+name+r'\(.*\)\s*$',l) and i+1<len(src) and src[i+1].startswith('{'):
            j=i+1
            while not src[j].startswith('}'): j+=1
            return '\n'.join(src[i:j+1]), src[i]
    raise SystemExit('missing '+name)
bs=[];ps=[]
for f in fns:
    b,h=body(f); bs.append(b); ps.append(h.rstrip()+';')
open(out,'w').write('\n'.join(ps)+'\n\n'+'\n\n'.join(bs))
