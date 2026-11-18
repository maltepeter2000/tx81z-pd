import json
import study as S

def kc(kw, key=60, extra=(), t_img=8.9):
    v, a = S.voice(**kw)
    w = S.run(v, a, notes=[(8.5, [0x90, key, 100])], end=t_img + 0.05, extra=extra)
    ch = [x & 7 for t, r, x in w if t >= 8.5 and r == 0x08]
    ch = ch[0] if ch else 0
    img = S.P.image(w, t_img)
    return img.get(0x28 + ch), img.get(0x30 + ch)

jobs = []
for trps in (0, 12, 23, 24, 25, 36, 48):
    for key in range(0, 128, 1):
        jobs.append(("trps", dict(trps=trps), key, ()))
for mt in range(0, 128, 1):
    jobs.append(("mtune", {}, 60, ((8.2, "syx", [0xF0, 0x43, 0x10, 0x10, 0x7B, 0x00, mt, 0xF7]),)))
for pbr in (0, 1, 2, 7, 12):
    for bend in range(0, 16384, 256):
        jobs.append(("bend", dict(pbr=pbr), 60, ((8.6, "midi", [0xE0, bend & 0x7F, bend >> 7]),)))
    jobs.append(("bend", dict(pbr=pbr), 60, ((8.6, "midi", [0xE0, 0x7F, 0x7F]),)))
res = S.pmap(lambda j: (j[0], j[1], j[2], [list(e[2]) if e[1] == "midi" else e[2] for e in j[3]], kc(j[1], j[2], j[3])), jobs)
json.dump(res, open("pitch.json", "w"))
print(len(res))
