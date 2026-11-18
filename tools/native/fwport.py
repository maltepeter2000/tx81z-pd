#!/usr/bin/env python3
"""Line-by-line Python ports of the firmware's periodic routines (single mode), working on a copy of
the CPU's RAM so they can be checked against RAM snapshots of the real firmware (test_port.py).
The C++ engine follows these ports. Addresses are CPU addresses; ROM reads use bank 1.

Dispatcher $8F1F (every 292 samples, phases alternate):
  phase A: $A390 bend / BC pitch bias, $AAF7 software LFO, $AA1A controller pitch depth + PM depth,
           $A538 portamento + key code
  phase B: $A64D LFO delay, $A68F channel volume + TL refresh, $A94B AM depth
"""
import romtables


class Ram:
    def __init__(self, image=None):
        self.m = bytearray(0x10000)
        rom = open(romtables.ROM, "rb").read()
        self.m[0x8000:0x10000] = rom[0x8000:0x10000]
        if image is not None:                       # (internal RAM 256 B, external RAM 8 KB)
            self.m[0:0x100] = image[0]
            self.m[0x6000:0x8000] = image[1]
        self.writes = []                            # OPZ register writes

    def r(self, a):
        return self.m[a & 0xFFFF]

    def w(self, a, v):
        self.m[a & 0xFFFF] = v & 0xFF

    def r16(self, a):
        return self.m[a & 0xFFFF] << 8 | self.m[(a + 1) & 0xFFFF]

    def w16(self, a, v):
        self.m[a & 0xFFFF] = (v >> 8) & 0xFF
        self.m[(a + 1) & 0xFFFF] = v & 0xFF

    def opz(self, reg, val):
        self.writes.append((reg & 0xFF, val & 0xFF))


def hi(a, b):
    return (a * b) >> 8


def sat(x):
    return 0xFF if x > 0xFF else x


MOD_ATT = 0xA82B


# ---- phase B -------------------------------------------------------------------------------------

def lfo_delay(m):
    """$A64D: two-stage LFO delay per LFO / instrument b = 0..7 (wait in $7796, ramp in $77A6)"""
    for b in range(8):
        rate = m.r16(0x780E + 2 * b)
        d = m.r16(0x7796 + 2 * b) + rate
        d = 0xFFFF if d > 0xFFFF else d
        m.w16(0x7796 + 2 * b, d)
        if d != 0xFFFF:
            continue
        d = m.r16(0x77A6 + 2 * b) + rate
        m.w16(0x77A6 + 2 * b, 0xFFFF if d > 0xFFFF else d)


def channel_volume(m, ch):
    """$A69B for one channel: EG bias amount, volume attenuation, register 00-07"""
    inst = m.r(0x793B + ch)
    if inst & 0x80:
        return
    br = m.r(0x7D5B + inst)
    m.w(0x7D23 + ch, m.r(0x7D33 + ch) - hi(m.r(0x7D33 + ch), br))
    fcv = m.r(0x7D1B + ch)
    m.w(0x7D2B + ch, hi(fcv, m.r(0x7D63 + inst)))
    a = ((fcv - m.r(0x7D2B + ch)) & 0xFF) + m.r(0x7D6B + inst)
    a = (~sat(a)) & 0xFF
    a = m.r(MOD_ATT + a) + m.r(MOD_ATT + ((~m.r(0x776D)) & 0xFF))
    if a > 0xFF:
        a = 0xFF
    elif m.r(0x776B) & 2:
        a = sat(a + m.r(MOD_ATT + ((~m.r(0x7753 + inst)) & 0xFF)))
    m.w(0x7D2B + ch, a)
    new = a + m.r(MOD_ATT + ((~m.r(0x7D23 + ch)) & 0xFF))     # 9 bit
    old = m.r16(0x7893 + 2 * ch)
    m.w16(0x7893 + 2 * ch, new)
    diff = (old - new) & 0xFFFF
    if diff == 0:
        return
    if old < new:
        diff = (new - old) & 0xFFFF
    val = 0 if diff >= 0x20 else m.r(0xA92B + diff)
    m.opz(ch, val)


def is_carrier(alg, oi):
    """$A7B1: algorithm -> does operator index oi (0 = OP4 .. 3 = OP1) get the volume attenuation"""
    b = (alg & 7) - 4
    if b < 0:
        return oi == 3
    if b == 0:
        return bool(oi & 2)
    if b < 3:
        return oi != 0
    return True


def tl_refresh(m):
    """$A769: TL of 4 channels x 4 operators, the half alternates ($91 / $92)"""
    if m.r(0x91) == 0:
        m.w(0x91, 0xFF)
        m.w(0x92, 0)
    else:
        m.w(0x91, 0)
        m.w(0x92, 4)
    half = m.r(0x92)
    for oi in (3, 2, 1, 0):
        for c in (3, 2, 1, 0):
            ch = c + half
            s = oi * 8 + c + half
            a = (~hi(m.r(0x7853 + s), m.r(0x7D23 + ch))) & 0xFF
            a = m.r(MOD_ATT + a)
            if is_carrier(m.r(0x7913 + ch), oi):
                a = sat(a + m.r(0x7D2B + ch))
            m.w(0x7873 + s, a)
            t = sat(a + m.r(0x78A3 + s))
            bit7 = 0 if (m.r(0x776B) & 2 and m.r(0x7767)) else 0x80
            m.opz(0x60 + s, bit7 | (t >> 1))


def volume_and_tl(m):
    """$A68F"""
    start = 4 if m.r(0x91) else 0
    for ch in range(start, start + 4):
        channel_volume(m, ch)
    tl_refresh(m)


def lfo_pick(m, k):
    """instrument whose controllers feed LFO k (0 / 1)"""
    b = m.r(0x7850)
    if b == 0 or b == 0xFF:
        return m.r(0x784E if k == 0 else 0x784F)
    return m.r(0x784F if k == 0 else 0x784E)


def am_depth(m):
    """$A94B: AM depth of LFO 1 (19) or 2 (17), bit 7 clear"""
    k = 1 if m.r(0x91) else 0
    b = lfo_pick(m, k)
    f2 = hi(m.r(0x7D15 + k), m.r(0x7D53 + b))
    f3 = hi(m.r(0x7D17 + k), m.r(0x7D5B + b))
    f5 = hi(m.r(0x7D19 + k), m.r(0x7D63 + b))
    s = f2 + f3
    s = 0xFF if s > 0xFF else sat(s + f5)
    sel = m.r(0x7850 + k)
    dly = 0 if sel & 0x80 else m.r(0x77A6 + sel)
    a = sat(hi(m.r(0x7D13 + k), dly) + s)
    a = m.r(MOD_ATT + ((~a) & 0xFF))
    if not sel & 0x80:
        bias = m.r(0x7873 + sel)
        a = (sat(a + bias) - bias) & 0xFF
    m.opz(0x19 if k == 0 else 0x17, a >> 1)


def phase_b(m):
    lfo_delay(m)
    volume_and_tl(m)
    am_depth(m)


# ---- phase A -------------------------------------------------------------------------------------

def s8(x):
    return x - 256 if x & 0x80 else x


def bend(m):
    """$A390: pitch bend and breath pitch bias per channel -> $78C3 (16 bit, 1/256 semitone)"""
    for ch in range(7, -1, -1):
        if m.r(0x793B + ch) & 0x80:
            m.w16(0x78C3 + 2 * ch, 0)
            continue
        # pitch bend
        b = m.r(0x7D83 + m.r(0x793B + ch))   # bend value per instrument
        d = 0
        if b != 0x80:
            a = ((m.r(0xA4B8 + (b >> 1)) << 1) & 0xFF) ^ 0x80
            m.w(0x8C, a)
            rng = m.r(0x7923 + ch)
            if rng != 0:
                if rng == 0xC2 and not a & 0x80 and a == 0x7E:
                    d = 0x0C00                                  # -> $A3F7 (no shifts)
                elif rng == 0xC2 and a & 0x80:
                    a = (-a) & 0xFF
                    d = a * 0xC0
                    d = (~d) & 0xFFFF
                    lo = ((d & 0xFF) + 1) & 0xFF               # incb (carry stays set from comb)
                    d = (((d >> 8) + 1) & 0xFF) << 8 | lo      # inca
                    d >>= 3
                else:
                    if not a & 0x80:
                        d = ((a << 1) & 0xFF) * rng
                    else:
                        a = (~a) & 0xFF
                        d = ((a << 1) & 0xFF) * rng
                        d = (~d) & 0xFFFF
                    d >>= 4
                if m.r(0x8C) & 0x80:
                    d |= 0xF000
        m.w16(0x89, d)
        m.w16(0x78C3 + 2 * ch, (bc_bias(m, ch) + d) & 0xFFFF)


def bc_bias(m, ch):
    """$A400: breath controller pitch bias (RAM $792B = bias - 50, curve $A485)"""
    bb = m.r(0x792B + ch)
    if bb == 0:
        return 0
    inst = m.r(0x793B + ch)
    a = 0 if inst & 0x80 else m.r(0x7D5B + inst)
    if not inst & 0x80 and a == 0:
        return 0
    if bb in (0x31, 0x32) or not bb & 0x80:
        if bb in (0x31, 0x32) and a >= 0xFE:
            d = 0xC000
        else:
            d = a * m.r(0xA485 + bb)
    else:
        nb = (-bb) & 0xFF
        d = 0xC000 if (nb >= 0x31 and a >= 0xFE) else a * m.r(0xA485 + nb)
        d = (((~d) & 0xFFFF) + 1) & 0xFFFF
        if d == 0:
            return 0
    d >>= 2
    if bb & 0x80:
        d |= 0xC000
    return d


def soft_lfo(m):
    """$AAF7: software LFO per instrument -> $77B6 (only used for performance LFO 'vib'; single mode
    keeps it out of the pitch, but the state is kept the same)"""
    for b in range(8):
        if b != 0 and (m.r(0x7846 + b) & 3) != 3:
            m.w16(0x77B6 + 2 * b, 0)
            continue
        ph = (m.r16(0x7836 + 2 * b) + m.r16(0x781E + 2 * b)) & 0xFFFF
        m.w16(0x7836 + 2 * b, ph)
        a = (ph >> 7) & 0xFF                 # asld -> A
        if ph & 0x8000:
            a = (~a) & 0xFF
        if b == 0:
            m.w(0x776A, a)
        lv = (a + 0x80) & 0xFF
        dep = m.r(0x7D7B + b) + m.r(0x7D73 + b)
        if (dep & 0xFF) == 0:
            m.w16(0x77B6 + 2 * b, 0)
            continue
        dep = 0xFF if dep > 0xFF else dep
        k = hi(dep, m.r(0x77A6 + 2 * b))
        if not lv & 0x80:
            d = lv * k
        else:
            d = (((~(((-lv) & 0xFF) * k)) & 0xFFFF) + 1) & 0xFFFF
        h = (d >> 8) & 0xFF
        d = (0xFF00 if h & 0x80 else 0) | h
        m.w16(0x77B6 + 2 * b, (d << 1) & 0xFFFF)


def pitch_depth(m):
    """$AA1A: controller pitch modulation depth per channel ($7D73), then PM depth of LFO 1 / 2"""
    half = 4 if m.r(0x91) else 0
    m.w(0x92, half)
    for c in range(4):
        ch = c + half
        inst = m.r(0x793B + ch)
        if inst & 0x80:
            continue
        mw = hi(m.r(0x7D3B + ch), m.r(0x7D53 + inst))
        m.w(0x7D73 + ch, mw)
        fc = hi(m.r(0x7D4B + ch), m.r(0x7D63 + inst))
        bc = hi(m.r(0x7D43 + ch), m.r(0x7D5B + inst))
        a = bc + fc
        a = 0xFF if a > 0xFF else sat(a + mw)
        m.w(0x7D73 + ch, a)
    k = 1 if m.r(0x91) else 0
    sel = m.r(0x7850 if k == 0 else 0x7851)
    a = 0
    if not sel & 0x80:
        a = hi(m.r(0x7D7B + sel), m.r(0x77A6 + 2 * sel))
        b = lfo_pick(m, k)
        if not b & 0x80:
            a = sat(a + m.r(0x7D73 + m.r(0x7EF1 + b)))
    m.opz(0x19 if k == 0 else 0x17, (a >> 1) | 0x80)


def keycode(m):
    """$A538: portamento and key code / fraction of channels 7..0"""
    for ch in range(7, -1, -1):
        p = 0x7CF3 + 2 * ch
        glide = False
        inst = m.r(0x793B + ch)
        if m.r(0x7772) & 4 and m.r(0x6E38):
            glide = bool(m.r(0x41) & 0x40)
        else:
            vx = 0x6A67 + (0 if m.r(0x793B + ch) >= 8 else m.r(0x793B + ch)) * 0x6E   # $9A07
            if m.r(vx + 0x3F) and m.r(vx + 0x41):
                glide = True
            elif not inst & 0x80 and m.r(0x41 + inst) & 0x40:
                glide = True
        tgt = m.r16(p)
        cur = m.r16(p + 0x10)
        if not glide:
            cur = tgt
        else:
            diff = (tgt - cur) & 0xFFFF
            rate = m.r(0x77C6 + ch)
            if diff == 0 or rate == 0xFF:
                cur = tgt
            elif not diff & 0x8000:
                step = (((diff >> 8) >> 2) + 1) * rate
                n = (step + cur) & 0xFFFF
                cur = n if n < tgt else tgt
            else:
                step = ((((-(diff >> 8)) & 0xFF) >> 2) + 1) * rate
                n = (cur - step) & 0xFFFF
                cur = n if n >= tgt else tgt
        m.w16(p + 0x10, cur)
        d = (m.r16(0x86) + m.r16(0x78C3 + 2 * ch)) & 0xFFFF
        if ch != 0 or (m.r(0x7846) & 3) == 3:
            x = m.r16(0x77B6 + 2 * ch)
            r = d + x
            sd = (d ^ 0x8000) - 0x8000
            sx = (x ^ 0x8000) - 0x8000
            if not -0x8000 <= sd + sx <= 0x7FFF:
                d = 0x0000 if r > 0xFFFF else 0x7FFF
            else:
                d = r & 0xFFFF
        # addd current, then fold the semitone into 0..95 (signed test uses N ^ V of the add)
        sd = (d ^ 0x8000) - 0x8000
        sc = (cur ^ 0x8000) - 0x8000
        tot = sd + sc
        r = tot & 0xFFFF
        lt = tot < 0 if -0x8000 <= tot <= 0x7FFF else tot >= 0    # N ^ V
        a, b = r >> 8, r & 0xFF
        while lt:
            n = a + 12
            sa = s8(a) + 12
            a = n & 0xFF
            lt = sa < 0 if -128 <= sa <= 127 else sa >= 0
        while a >= 0x60:
            a -= 12
        kc = m.r(0xA2F0 + a)
        m.opz(0x28 + ch, kc)
        m.opz(0x30 + ch, (b & 0xFC) | (m.r(0x7933 + ch) & 1))


def phase_a(m):
    bend(m)
    soft_lfo(m)
    pitch_depth(m)
    keycode(m)


# ---- events: voice load, notes, controllers (single mode: one instrument, 8 voices) ---------------

VOICE = 0x6A67          # voice copy: VCED 0..86 then ACED 0..22 (110 bytes)


def scale(x):
    """$908B: 0..99 -> 0..255"""
    return ((x * 0xA5) << 2 >> 8) & 0xFF


def delay_rate(lfd):
    """$9CF5"""
    a = 99 - lfd
    n = 7 - ((a & 0x70) >> 4)
    d = ((((a & 0x0F) | 0x10) << 1) & 0xFF) << 8
    return d >> n


def power_on(m):
    """RAM the native engine sets once (values of the firmware in single mode)"""
    for ch in range(8):
        m.w(0x7846 + ch, 0x01)       # every channel on LFO 1
        m.w(0x793B + ch, 0x00)       # instrument of the channel
        m.w(0x7933 + ch, 0x07)
    m.m[0x784E:0x7853] = bytes([0x00, 0xFF, 0x00, 0xFF, 0x00])
    m.w(0x7EF1, 0)
    m.w(0x7EF9, 7)
    m.w(0xF6, 8)
    for a in (0x776B, 0x776D, 0x7767, 0x7772):
        m.w(a, 0)
    m.w(0x7772, 0x02)
    reset_controllers(m)
    m.w16(0x86, 0)


def reset_controllers(m, inst=0):
    """$C832"""
    m.w(0x7D83 + inst, 0x80)
    m.w(0x7D53 + inst, 0)
    m.w(0x7D5B + inst, 0)
    m.w(0x7D63 + inst, 0xFE)
    m.w(0x41 + inst, 0x40)
    m.w(0x49 + inst, 0)
    m.w(0x7D6B + inst, 0)


def master_tune(m, mt):
    """$A12D: system master tune 0..127 (64 = centre)"""
    b = ((mt << 1) ^ 0x80) & 0xFF
    d = (0xFF00 if b & 0x80 else 0) | b
    m.w16(0x86, (d << 1) & 0xFFFF)


def load_voice(m, v, a):
    """voice parameters -> RAM (the register writes are model.load_writes)"""
    v, a = list(v), list(a)
    for oi in range(4):                         # the firmware's own clean-up of the stored voice
        v[oi * 13 + 3] = max(1, v[oi * 13 + 3])                 # RR >= 1
        if v[oi * 13 + 11] < 4:
            a[oi * 5 + 2] = min(7, a[oi * 5 + 2])               # fine <= 7 for ratios < 1
    a[19] = 0                                                   # no EG shift on OP1
    m.m[VOICE:VOICE + 87] = bytes(v[:87])
    m.m[VOICE + 87:VOICE + 110] = bytes(a)
    for ch in range(8):
        m.w(0x7913 + ch, 0x80 | (v[53] << 3) | v[52])
        m.w(0x7D3B + ch, scale(v[71]))
        m.w(0x7D43 + ch, scale(v[73]))
        m.w(0x7D4B + ch, scale(a[21]))
        m.w(0x7D7B + ch, scale(v[56]))
        m.w16(0x780E + 2 * ch, delay_rate(v[55]))
        m.w16(0x781E + 2 * ch, m.r16(0x9B6D + 2 * v[54]))
        m.w(0x782E + ch, v[58])
        m.w(0x77C6 + ch, m.r(0xA06D + v[66]))
        m.w(0x7923 + ch, m.r(0xA0E4 + v[64]))
        m.w(0x792B + ch, v[75] - 50)
        m.w(0x7D1B + ch, scale(v[67]))
        m.w(0x7D33 + ch, scale(v[76]))
        m.w(0x791B + ch, v[62] - 24)
        for oi in range(4):
            m.w(0x7853 + oi * 8 + ch, m.r(0x9D89 + v[oi * 13 + 7]))
    m.w(0x7D13, scale(v[57]))
    m.w(0x7D15, scale(v[72]))
    m.w(0x7D17, scale(v[74]))
    m.w(0x7D19, scale(a[22]))
    m.w(0x7852, v[59])
    # voices reset ($90D5)
    for x in range(0x7CE3, 0x7CF3):
        m.w(x, 0)
    for x in range(0x7CF3, 0x7D13, 2):
        m.w16(x, 0x1700)
    for x in range(0x7F29, 0x7F39, 2):
        m.w16(x, 0x1700)
    for x in range(0x7F39, 0x7F89):
        m.w(x, 0)
    for x in range(0xE7, 0xEF):
        m.w(x, 0xFF)
