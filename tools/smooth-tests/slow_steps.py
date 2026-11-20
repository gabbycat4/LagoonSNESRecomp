#!/usr/bin/env python3
"""Count the original game's slowdown in a play session (build-port/motion.log).
The game takes one step every 2 fields (tick $0408 changes); a step that takes
3 or more fields is the slowdown. Usage: python3 tools/smooth-tests/slow_steps.py [build-port]
Prints per room: steps, slow steps, slow share, and the worst 2-second burst."""
import re,sys,os
from collections import defaultdict
path=os.path.join(sys.argv[1] if len(sys.argv)>1 else 'build-port','motion.log')
last_tick=None;last_f=None;steps=defaultdict(int);slow=defaultdict(int);slow_at=defaultdict(list)
for l in open(path,errors='ignore'):
    if not l.startswith('F '):continue
    q=l.split();f=int(q[1])
    t=re.search(r' t=(\w+)',l);r=re.search(r' r=(\w+)',l);m=re.search(r' m=(\w+)',l)
    if not t:continue
    tick=int(t[1],16);room=int(r[1],16)
    if last_tick is not None and tick!=last_tick:
        gap=f-last_f
        if gap<=8:   # longer gaps are menus, loads, fades
            steps[room]+=1
            if gap>=3: slow[room]+=1; slow_at[room].append(f)
        last_f=f
    elif last_tick is None: last_f=f
    last_tick=tick
tot_s=sum(steps.values());tot_w=sum(slow.values())
for room in sorted(steps):
    a=slow_at[room];worst=0;j=0
    for i in range(len(a)):
        while a[i]-a[j]>120:j+=1
        worst=max(worst,i-j+1)
    print(f"room {room:x}: steps {steps[room]}, slow steps {slow[room]} ({100*slow[room]/max(1,steps[room]):.1f}%), worst 2 s: {worst}")
print(f"all: steps {tot_s}, slow steps {tot_w} ({100*tot_w/max(1,tot_s):.1f}%)")
