import json
import study as S

def note_image(kw, key=60, vel=127):
    v, a = S.voice(**kw)
    w = S.run(v, a, notes=[(8.5, [0x90, key, vel])], end=8.95)
    ch = [x & 7 for t, r, x in w if t >= 8.5 and r == 0x08]
    ch = ch[0] if ch else None
    img = S.P.image(w, 8.94)
    return ch, {f"{r:02x}": img.get(r) for r in range(0x00, 0x100)}

if __name__ == "__main__":
  jobs = []
  for out in range(100):
      jobs.append(("out", dict(out1=out, kvs1=0, ls1=0), 60, 127))
  for kvs in range(8):
      for vel in list(range(1, 128, 6)) + [127]:
          jobs.append(("kvs", dict(out1=99, kvs1=kvs, ls1=0), 60, vel))
  for ls in (0, 1, 10, 25, 50, 75, 99):
      for key in range(12, 120, 3):
          jobs.append(("ls", dict(out1=99, kvs1=0, ls1=ls), key, 127))
  for key in range(0, 128, 1):
      jobs.append(("key", dict(out1=99, kvs1=0, ls1=0), key, 127))
  res = S.pmap(lambda j: (j[0], j[1], j[2], j[3], note_image(j[1], j[2], j[3])), jobs)
  json.dump(res, open("notes_tl.json", "w"))
  print(len(res))
