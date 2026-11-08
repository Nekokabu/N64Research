#!/usr/bin/env python3
"""v32: python3 v32_cmp.py HWFILE [ARESFILE] -> per-span cost by width and per-triangle fixed cost"""
import sys,re
def load(p):
    d={}
    for l in open(p,errors='replace'):
        f=l.rstrip('\n').split('\t')
        if f[0]=='Y' and len(f)>9: d[(f[1],int(f[2]))]=(int(f[4]),int(f[5]),int(f[6]))  # tris, spans, clk
    return d
hw=load(sys.argv[1]); ar=load(sys.argv[2]) if len(sys.argv)>2 else {}
widths=[1,2,3,4,6,8,16]
cases=[]
for k in hw:
    if k[0] not in cases: cases.append(k[0])
print('clk per span (hw / ares):')
for c in cases:
    row=[]
    for w in widths:
        if (c,w) in hw:
            t,s,clk=hw[(c,w)]; a=ar.get((c,w))
            row.append(f"w{w}:{clk/s:5.1f}/{(a[2]/a[1]) if a else 0:5.1f}")
    print(f"{c:16s} "+'  '.join(row))
print('\nper-triangle: clk/tri = T + H*s  (from H=1,2,4,8,32 at each width, hw)')
for w in widths:
    pts=[]
    for c,H in (('wo_rect640_h1',1),('wo_rect640_h2',2),('wo_rect640_h4',4),('wo_rect640',8),('wo_rect640_h32',32)):
        if (c,w) in hw: t,s,clk=hw[(c,w)]; pts.append((H,clk/t))
    if len(pts)>=2:
        import numpy as np
        A=np.array([[1,h] for h,_ in pts],float); y=np.array([v for _,v in pts])
        T,sl=np.linalg.lstsq(A,y,rcond=None)[0]
        print(f"w{w:2d}: "+' '.join(f"H{h}:{v:6.1f}" for h,v in pts)+f"   fit T={T:5.1f} s={sl:5.1f}")
