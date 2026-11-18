import json
import study as S
from notes_tl import note_image
jobs = [(ls, 3 * g) for ls in range(1, 100) for g in range(8, 37)]
def f(j):
    ls, key = j
    ch, img = note_image(dict(out1=99, kvs1=0, ls1=ls), key, 127)
    return ls, key, (img[f"{0x78 + ch:02x}"] & 0x7F) - 8
res = S.pmap(f, jobs)
json.dump(res, open("ls_table.json", "w"))
print(len(res))
