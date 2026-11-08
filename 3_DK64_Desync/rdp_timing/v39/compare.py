#!/usr/bin/env python3
"""v39: python3 compare.py HWFILE [ARESFILE]  -- clk/span per case and width (console), console/ares, and the console's
position between its own all-pass and all-fail cases (0 = like pass, 1 = like fail) for each pattern."""
import sys
def load(p):
    d={}
    for l in open(p,errors='replace'):
        f=l.rstrip('\n').split('\t')
        if f[0]=='Y' and len(f)>=10: d[(f[1],int(f[2]))]=(int(f[6]),float(f[8]))
    return d
hw=load(sys.argv[1]); ar=load(sys.argv[2] if len(sys.argv)>2 else 'ares_reference.txt')
cases=[]
for k in hw:
    if k[0] not in cases: cases.append(k[0])
widths=sorted({k[1] for k in hw})
print('clk/span (console) ; console/ares ; position between pass (0) and fail (1) of the same family')
print('%-14s'%'case',' '.join('%16s'%('w%d'%w) for w in widths))
for c in cases:
    fam='_'.join(c.split('_')[:-1]) if c.count('_')>=1 else c
    if c.startswith('bar_h1'): fam='bar_h1'
    elif c.startswith('bar'): fam='bar'
    else: fam='dk'
    row=[]
    for w in widths:
        k=(c,w)
        if k not in hw: row.append('%16s'%'-'); continue
        h=hw[k][1]; a=ar[k][1] if k in ar else 0
        kp=(fam+'_pass',w); kf=(fam+'_fail',w)
        pos=''
        if kp in hw and kf in hw and hw[kp][1]!=hw[kf][1]: pos='%5.2f'%((hw[kp][1]-h)/(hw[kp][1]-hw[kf][1]))
        row.append('%6.1f %4.2f %5s'%(h, h/a if a else 0, pos))
    print('%-14s'%c,' '.join(row))
