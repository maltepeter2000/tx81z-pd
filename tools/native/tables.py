import json
import study as S

def regs(kw, keys):
    v, a = S.voice(**kw)
    lw = S.load_writes(S.run(v, a, end=8.6))
    return {f"{k:02x}": lw.get(k) for k in keys}

jobs = []
for crs in range(64):
    for det in range(7):
        jobs.append(("crsdet", dict(crs1=crs, det1=det), [0x58, 0x98, 0xD8]))
for fixr in range(8):
    for crs in range(64):
        jobs.append(("fixed", dict(fix1=1, fixr1=fixr, crs1=crs), [0x58, 0x98, 0xD8]))
for fin in range(16):
    jobs.append(("fixfin", dict(fix1=1, fixr1=3, crs1=10, fin1=fin), [0x58, 0x98, 0xD8]))
    jobs.append(("ratiofin", dict(crs1=10, fin1=fin), [0x58, 0x98, 0xD8]))
for x in range(100):
    jobs.append(("lfs", dict(lfs=x), [0x18]))
    jobs.append(("bceb", dict(bceb=x), [0x00]))
for w in range(4):
    for s in range(2):
        jobs.append(("lfw", dict(lfw=w, sync=s), [0x18, 0x1B]))
res = S.pmap(lambda j: (j[0], j[1], regs(j[1], j[2])), jobs)
json.dump(res, open("tables.json", "w"))
print(len(res), "probes")
