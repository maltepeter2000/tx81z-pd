#!/usr/bin/env python3
"""TX81Z voice records (78 bytes: VMEM 0..66 + VMEM 73..83, as the ROM presets and the internal bank
in battery RAM store them) -> VCED (93) + ACED (23). Reference for src/native/txvoice.h."""

ROM_PRESETS = 0x51C0      # ROM file offset (CPU bank 0): banks A-D, 128 records
NVRAM_INTERNAL = 0x0001   # battery RAM offset: internal bank, 32 records
RECORD = 78


def unpack(rec):
    v = [0] * 93
    a = [0] * 23
    for oi in range(4):
        o = rec[oi * 10:oi * 10 + 10]
        b = oi * 13
        v[b + 0], v[b + 1], v[b + 2], v[b + 3], v[b + 4], v[b + 5] = o[0], o[1], o[2], o[3], o[4], o[5]
        v[b + 8] = (o[6] >> 6) & 1          # AME
        v[b + 7] = (o[6] >> 3) & 7          # EBS
        v[b + 9] = o[6] & 7                 # KVS
        v[b + 10] = o[7]                    # OUT
        v[b + 11] = o[8]                    # CRS
        v[b + 6] = (o[9] >> 3) & 3          # RS
        v[b + 12] = o[9] & 7                # DET
        x = rec[67 + oi * 2:69 + oi * 2]
        a[oi * 5 + 4] = (x[0] >> 4) & 3     # EG shift
        a[oi * 5 + 0] = (x[0] >> 3) & 1     # FIX
        a[oi * 5 + 1] = x[0] & 7            # FIX range
        a[oi * 5 + 3] = (x[1] >> 4) & 7     # OSW
        a[oi * 5 + 2] = x[1] & 15           # FINE
    c = rec[40]
    v[58], v[53], v[52] = (c >> 6) & 1, (c >> 3) & 7, c & 7
    v[54], v[55], v[56], v[57] = rec[41], rec[42], rec[43], rec[44]
    c = rec[45]
    v[60], v[61], v[59] = (c >> 4) & 7, (c >> 2) & 3, c & 3
    v[62], v[64] = rec[46], rec[47]
    c = rec[48]
    v[70], v[63], v[68], v[69], v[65] = (c >> 4) & 1, (c >> 3) & 1, (c >> 2) & 1, (c >> 1) & 1, c & 1
    v[66], v[67] = rec[49], rec[50]
    v[71:77] = rec[51:57]
    v[77:87] = rec[57:67]
    v[87:93] = [99, 99, 99, 50, 50, 50]     # pitch EG (DX21 only)
    a[20], a[21], a[22] = rec[75], rec[76], rec[77]
    return v, a


def records(data, offset, n):
    return [data[offset + i * RECORD:offset + (i + 1) * RECORD] for i in range(n)]
