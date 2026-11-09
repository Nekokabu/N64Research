#!/usr/bin/env python3
"""v42: python3 compare.py HWFILE [ARESFILE] -- per list and variant: the slowdown of the list by each CPU / SP DMA load (relative to 'none'),
console and ares, VI off and in the title's VI mode."""
import sys
def load(p):
    d={}
    for l in open(p,errors='replace'):
        f=l.rstrip('\n').split('\t')
        if f[0]=='X' and len(f)>=13 and f[4].isdigit(): d[(f[1],f[2],f[3],f[10].split('(')[0])]=int(f[4])
    return d
hw=load(sys.argv[1]); ar=load(sys.argv[2] if len(sys.argv)>2 else 'ares_reference.txt')
lists=[];conds=[]
for k in hw:
    if k[0] not in lists: lists.append(k[0])
    if k[3] not in conds: conds.append(k[3])
for L in lists:
    for var in ('full','fifo1k'):
        for mode in ('vi_off','game'):
            b=hw.get((L,var,mode,'none')); ab=ar.get((L,var,mode,'none'))
            if not b: continue
            print('== %s %s %s: none hw %d ares %d (hw/ares %.3f)'%(L,var,mode,b,ab or 0,b/ab if ab else 0))
            for c in conds:
                k=(L,var,mode,c)
                if c=='none' or k not in hw or hw[k]==0: continue
                print('   %-22s hw x%.3f  ares x%.3f'%(c, hw[k]/b, ar[k]/ab if k in ar and ab else 0))
