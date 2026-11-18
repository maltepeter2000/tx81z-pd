import json, random
import study as S
from notes_tl import note_image
jobs = []
for kvs in range(1, 8):
    for vel in range(1, 128):
        jobs.append(("vel", dict(out1=99, kvs1=kvs, ls1=0), 60, vel))
for ls in (99, 50, 33, 7, 1, 2, 3):
    for key in range(24, 109):
        jobs.append(("ls", dict(out1=99, kvs1=0, ls1=ls), key, 127))
random.seed(1)
for i in range(60):
    kw = dict(out1=random.randint(0, 99), kvs1=random.randint(0, 7), ls1=random.randint(0, 99))
    jobs.append(("combo", kw, random.randint(24, 108), random.randint(1, 127)))
res = S.pmap(lambda j: (j[0], j[1], j[2], j[3], note_image(j[1], j[2], j[3])), jobs)
json.dump(res, open("notes_tl2.json", "w"))
print(len(res))
