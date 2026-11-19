#!/usr/bin/env python3
"""Turn a play session's build-port/motion.log + oam.log (LAGOON_MOTION_LOG / LAGOON_OAM_LOG)
into replay input for tools/smooth-tests/replay.c.  Usage (from the project folder):
  python3 tools/smooth-tests/make_replay.py build-port /tmp/replay.txt
  gcc -O2 -std=gnu11 -w -I src tools/smooth-tests/replay.c src/lagoon_smooth.c -lm -o /tmp/replay
  /tmp/replay /tmp/replay.txt | tail -8
The replay feeds every recorded field through lagoon_smooth.c at 180 pictures/s with half
pixels and reports, per room: camera movement per picture, characters whose pieces split
apart, and jumps larger than half a pixel."""
import re,sys,os
src,dst=sys.argv[1],sys.argv[2]
mot={};oam={}
for l in open(os.path.join(src,'motion.log'),errors='ignore'):
    if not l.startswith('F '):continue
    f=int(l.split()[1])
    c=re.search(r' c=(\w+),(\w+)',l); s=re.search(r' s=(\w+),(\w+)',l); p=re.search(r' p=(\w+),(\w+)',l)
    r=re.search(r' r=(\w+)',l); t=re.search(r' t=(\w+)',l); a=re.search(r' a=(\S*)',l); acts=[]
    if a and a[1]:
        for e in a[1].split(';'):
            try: sl,v=e.split(':'); x,y,ty=v.split(','); acts.append((int(sl),int(x,16),int(y,16),int(ty,16)))
            except Exception: pass
    mot[f]=dict(cx=int(c[1],16),cy=int(c[2],16),sx=int(s[1],16),sy=int(s[2],16),px=int(p[1],16),py=int(p[2],16),
                r=int(r[1],16),t=int(t[1],16),a=acts)
for l in open(os.path.join(src,'oam.log'),errors='ignore'):
    if not l.startswith('F '):continue
    q=l.split(); f=int(q[1]); pcs=[]
    for e in q[3:]:
        try: i,v=e.split(':'); x,y,tile,attr,big=v.split(','); pcs.append((int(i),int(x),int(y),int(tile,16)|(int(attr,16)<<8)))
        except Exception: pass
    oam[f]=pcs
def wrap(d):
    d&=1023
    return d-1024 if d>=512 else d
with open(dst,'w') as o:
    for f in sorted(mot):
        if f not in oam: continue
        m=mot[f]
        lx=wrap(m['cx']-m['sx']); ly=wrap(m['cy']-m['sy'])      # scroll trailing the camera
        if abs(lx)>8 or abs(ly)>8: lx=ly=0
        wx=m['cx']-lx; wy=m['cy']+1-ly                          # packet origin = camera - (0,-1)
        acts=[(0,m['px'],m['py'],0)]+m['a']
        pcs=[p for p in oam[f] if p[2]<224 or p[2]>=240]
        o.write(f"F {f} {m['t']} {m['cx']} {m['cy']} {wx} {wy} {m['r']} {len(acts)} "+
                " ".join(f"{a} {x} {y} {t}" for a,x,y,t in acts)+f" {len(pcs)} "+
                " ".join(f"{i} {x} {y&255} {t}" for i,x,y,t in pcs)+"\n")
