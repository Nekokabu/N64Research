#!/usr/bin/env python3
"""v40: python3 compare.py HWFILE [ARESFILE] -- per case: the VI slowdown factor (ratio to VI off) on the console and in ares,
for each VI mode and displayed buffer, plus console/ares of the VI-off value."""
import sys
def load(p):
    d={}
    for l in open(p,errors='replace'):
        f=l.rstrip('\n').split('\t')
        if f[0]=='V' and len(f)>=11: d[(f[1],f[2],f[3])]=(int(f[8]),float(f[9]),float(f[10]))
    return d
hw=load(sys.argv[1]); ar=load(sys.argv[2] if len(sys.argv)>2 else 'ares_reference.txt')
cases=[];modes=[];origs=[]
for k in hw:
    if k[0] not in cases: cases.append(k[0])
    if k[1] not in modes: modes.append(k[1])
    if k[2] not in origs: origs.append(k[2])
for c in cases:
    k0=(c,'vi_off','console')
    if k0 not in hw: continue
    print(f"== {c}: VI off console {hw[k0][1]:.1f} clk/span, ares {ar[k0][1] if k0 in ar else 0:.1f} (console/ares {hw[k0][1]/ar[k0][1] if k0 in ar and ar[k0][1] else 0:.3f})")
    print('   %-18s'%'mode'+' '.join('%22s'%o for o in origs))
    for m in modes:
        if m=='vi_off': continue
        row=[]
        for o in origs:
            k=(c,m,o)
            if k in hw: row.append('%22s'%('hw x%.3f ares x%.3f'%(hw[k][2], ar[k][2] if k in ar else 0)))
            else: row.append('%22s'%'-')
        print('   %-18s'%m+' '.join(row))
