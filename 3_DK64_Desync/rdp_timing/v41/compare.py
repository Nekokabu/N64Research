#!/usr/bin/env python3
"""v41: python3 compare.py HWFILE [ARESFILE] -- per list: each variant relative to pad1k (console and ares), and console/ares."""
import sys
def load(p):
    d={}
    for l in open(p,errors='replace'):
        f=l.rstrip('\n').split('\t')
        if f[0]=='X' and len(f)>=6 and f[4].isdigit(): d[(f[1],f[2])]=int(f[4])
    return d
hw=load(sys.argv[1]); ar=load(sys.argv[2] if len(sys.argv)>2 else 'ares_reference.txt')
vs=['full','pad1k','xbus1k','xbus512','fifo1k','fifo512','fifo256']
lists=[]
for k in hw:
    if k[0] not in lists: lists.append(k[0])
print('%-16s'%'list'+''.join('%26s'%v for v in vs))
print('%-16s'%''+''.join('%26s'%'hw/pad1k ares/pad1k hw/ares' for v in vs))
for L in lists:
    b=hw.get((L,'pad1k')); ab=ar.get((L,'pad1k'))
    row='%-16s'%L
    for v in vs:
        k=(L,v)
        if k in hw and b and k in ar and ab: row+='%26s'%('%.3f  %.3f  %.3f'%(hw[k]/b, ar[k]/ab, hw[k]/ar[k]))
        else: row+='%26s'%'-'
    print(row)
