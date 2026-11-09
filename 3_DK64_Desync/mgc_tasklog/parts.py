#!/usr/bin/env python3
"""parts.py: part-by-part comparison of the MGC 2011 demo between the console task logs (a: parts 1-2, b: parts 3-4, 001: parts 4-6)
and the same three logs taken in ares.  usage: parts.py TAG ares_a ares_b ares_c [TAG ares_a ares_b ares_c ...]"""
import re, sys, statistics
def load(p):
    out={}
    for l in open(p):
        if re.match(r'^\s*\d+\s+\d+',l):
            r=l.split(); out[int(r[0])]=(int(r[1]),float(r[2]),float(r[6]),float(r[7]))
    return out
H={'a':load('hw_a_frames.txt'),'b':load('hw_b_frames.txt'),'c':load('hw_001_frames.txt')}
# segments: (log, first frame, last frame, label)
segs=[('a',0,1232,'part 1 (a 0-1231)'),('a',1232,2130,'part 2 (a 1232-2129, 82%)'),('b',0,1345,'part 3 (b 0-1344)'),('c',0,1217,'part 4 (c 0-1216)'),('c',1217,1968,'part 5 (c 1217-1967)'),('c',1968,2233,'logo (c 1968-2232)')]
args=sys.argv[1:]
sets=[(args[i],{'a':load(args[i+1]),'b':load(args[i+2]),'c':load(args[i+3])}) for i in range(0,len(args),4)]
print('%-24s %8s'%('segment','console')+''.join(' %18s'%t for t,_ in sets))
tot_h=0; tot={t:0 for t,_ in sets}
for lg,f0,f1,lab in segs:
    dh=H[lg][f1][0]-H[lg][f0][0]; tot_h+=dh
    row='%-24s %8d'%(lab,dh)
    for t,A in sets:
        d=A[lg][f1][0]-A[lg][f0][0]; tot[t]+=d
        row+=' %8d (%+5.1f%%)'%(d,100*(d/dh-1))
    print(row)
row='%-24s %8d'%('sum',tot_h)
for t,_ in sets: row+=' %8d (%+5.1f%%)'%(tot[t],100*(tot[t]/tot_h-1))
print(row)
print('\nthe "@" frame (c 2233) : console VI %d'%H['c'][2233][0]+''.join('   %s %d (%+d)'%(t,A['c'][2233][0],A['c'][2233][0]-H['c'][2233][0]) for t,A in sets))
print('start of part 4 (c 0)   : console VI %d'%H['c'][0][0]+''.join('   %s %d (%+d)'%(t,A['c'][0][0],A['c'][0][0]-H['c'][0][0]) for t,A in sets))
print('first task (a 0)        : console VI %d'%H['a'][0][0]+''.join('   %s %d'%(t,A['a'][0][0]) for t,A in sets))
