#!/usr/bin/env python3
"""
Builds a 64 KB HD6303 test ROM (not Yamaha code) that exercises the emulated
TX81Z hardware the same way the firmware does:

  * reset vector, stack in battery RAM
  * HD44780: 8-bit init, prints "TEST TX81Z OK" on line 1
  * YM2414: timer A + IRQ1 -> counter in internal RAM $80, shown on LCD line 2
  * SCI: external clock, receive interrupt; each MIDI byte received is
    echoed back out of MIDI Out and its hex value written to RAM $6000+
  * YM2414: a simple 4-carrier sine voice keyed on (audio check)
  * P6: bank switching - the bank-1 copy of the reset code differs from bank 0
    (bank 1 is selected by P63 = 1, which the code does, then it checks a
    marker byte to prove the switch worked and prints B1 on the LCD)

Usage: python3 make_test_rom.py out.bin
"""
import sys

class Asm:
    def __init__(self, org):
        self.org = org
        self.code = bytearray()
        self.labels = {}
        self.fixups = []   # (pos, label, kind)

    def pc(self):
        return self.org + len(self.code)

    def label(self, name):
        self.labels[name] = self.pc()

    def b(self, *bs):
        self.code += bytes(bs)

    def w(self, v):
        self.code += bytes([(v >> 8) & 0xff, v & 0xff])

    def ref16(self, name):
        self.fixups.append((len(self.code), name, 16))
        self.w(0)

    def rel(self, name):
        self.fixups.append((len(self.code), name, 8))
        self.b(0)

    def resolve(self):
        for pos, name, kind in self.fixups:
            tgt = self.labels[name]
            if kind == 16:
                self.code[pos] = (tgt >> 8) & 0xff
                self.code[pos + 1] = tgt & 0xff
            else:
                off = tgt - (self.org + pos + 1)
                assert -128 <= off <= 127, name
                self.code[pos] = off & 0xff

    # instruction helpers
    def ldaa_i(self, v): self.b(0x86, v)
    def ldab_i(self, v): self.b(0xC6, v)
    def staa_e(self, a): self.b(0xB7); self.w(a)
    def stab_e(self, a): self.b(0xF7); self.w(a)
    def ldaa_e(self, a): self.b(0xB6); self.w(a)
    def ldaa_d(self, a): self.b(0x96, a)
    def staa_d(self, a): self.b(0x97, a)
    def lcd_cmd(self, v): self.ldaa_i(v); self.staa_e(0x4000)
    def opz(self, reg, val): self.ldaa_i(reg); self.staa_e(0x2000); self.ldaa_i(val); self.staa_e(0x2001)


def build_bank(bank):
    a = Asm(0x8000)

    # --- strings / data ---
    a.label('msg')
    a.code += b'TEST TX81Z OK\0'
    a.label('hex')
    a.code += b'0123456789ABCDEF'
    a.label('marker')
    a.b(0xB0 + bank)               # bank marker: $B0 in bank 0, $B1 in bank 1

    # --- reset ---
    a.label('reset')
    a.b(0x8E); a.w(0x7FFF)          # LDS #$7FFF
    a.ldaa_i(0x00); a.staa_e(0x6100)  # rx write index (in battery RAM)
    a.lcd_cmd(0x38)                 # 8-bit, 2 lines
    a.lcd_cmd(0x0C)                 # display on
    a.lcd_cmd(0x01)                 # clear
    a.lcd_cmd(0x06)                 # increment
    a.b(0xCE); a.ref16('msg')       # LDX #msg
    a.label('print')
    a.b(0xA6, 0x00)                 # LDAA 0,X
    a.b(0x27); a.rel('printed')     # BEQ
    a.staa_e(0x4001)
    a.b(0x08)                       # INX
    a.b(0x20); a.rel('print')       # BRA
    a.label('printed')

    # bank switch: P6 DDR = $FF, P6 = $08 (P63=1 -> bank 1)
    a.ldaa_i(0xFF); a.staa_d(0x16)
    a.ldaa_i(0x08 | 0x10); a.staa_d(0x17)   # + LED1
    a.ldaa_e(0x8000 + 13 + 1 + 16)  # LDAA marker (same address in both banks)
    a.ldab_i(0xC0 + 14); a.stab_e(0x4000)   # cursor line 2, col 14
    a.b(0x81, 0xB1)                 # CMPA #$B1
    a.b(0x26); a.rel('nobank')      # BNE
    a.ldaa_i(ord('B')); a.staa_e(0x4001)
    a.ldaa_i(ord('1')); a.staa_e(0x4001)
    a.label('nobank')

    # OPZ voice on channel 0: algorithm 7 (all carriers), sine
    a.opz(0x20, 0x87)               # pan R, key off, FB=0, ALG=7
    a.opz(0x28, 0x4A)               # KC: octave 4, note A
    for slot in range(4):
        r = slot * 8
        a.opz(0x40 + r, 0x01)       # DT1=0 MUL=1
        a.opz(0x60 + r, 0x08)       # TL
        a.opz(0x80 + r, 0x1F)       # AR max
        a.opz(0xA0 + r, 0x00)       # D1R
        a.opz(0xC0 + r, 0x00)       # D2R
        a.opz(0xE0 + r, 0x0F)       # D1L=0 RR=15
    a.opz(0x08, 0x00)               # select channel 0 for key on/off
    a.opz(0x20, 0xC7)               # OPZ: key on = bit 6 of reg 20-27

    # OPZ timer A: period, load + IRQ enable
    a.opz(0x10, 0xC0)
    a.opz(0x11, 0x00)
    a.opz(0x14, 0x15)

    # RAM control: enable IRQ1 (bit 0) -> OIM #$01,$14
    a.b(0x72, 0x01, 0x14)
    # SCI: external clock (RMCR CC=011), RE + RIE + TE
    a.ldaa_i(0x0C); a.staa_d(0x10)
    a.ldaa_i(0x1A); a.staa_d(0x11)
    a.b(0x0E)                       # CLI

    a.label('main')
    # show IRQ count on line 2, col 0 as two hex digits
    a.ldab_i(0xC0); a.stab_e(0x4000)
    a.ldaa_d(0x80)
    a.b(0x44, 0x44, 0x44, 0x44)     # LSRA x4
    a.b(0xCE); a.ref16('hex')       # LDX #hex
    a.b(0x3A)                       # ABX? no: ABX adds B. use B:
    # (simpler: compute via B)
    a.code = a.code[:-1]
    a.b(0x16)                       # TAB
    a.b(0x3A)                       # ABX
    a.b(0xA6, 0x00); a.staa_e(0x4001)
    a.ldaa_d(0x80); a.b(0x84, 0x0F) # ANDA #$0F
    a.b(0xCE); a.ref16('hex')
    a.b(0x16, 0x3A)
    a.b(0xA6, 0x00); a.staa_e(0x4001)
    a.b(0x3E)                       # WAI
    a.b(0x20); a.rel('main')

    # --- IRQ1: OPZ timer ---
    a.label('irq1')
    a.b(0x7C, 0x00, 0x80)           # INC $0080 (internal RAM)
    a.opz(0x14, 0x15)               # reset timer A flag, keep running
    a.b(0x3B)                       # RTI

    # --- SCI: echo received byte, store in RAM ---
    a.label('sci')
    a.ldaa_d(0x11)                  # read TRCSR (arms flag clear)
    a.b(0x85, 0x80)                 # BITA #$80 (RDRF)
    a.b(0x27); a.rel('sci_done')
    a.ldaa_d(0x12)                  # RDR
    a.b(0xFE); a.w(0x6100)          # LDX $6100 (index in high byte trick: use B)
    a.code = a.code[:-3]
    a.b(0xF6); a.w(0x6100)          # LDAB $6100 (index)
    a.b(0xCE); a.w(0x6000)          # LDX #$6000
    a.b(0x3A)                       # ABX
    a.b(0xA7, 0x00)                 # STAA 0,X
    a.b(0x5C)                       # INCB
    a.stab_e(0x6100)
    a.b(0xD6, 0x11)                 # LDAB TRCSR (arm TDRE clear)
    a.staa_d(0x13)                  # TDR = byte (echo)
    a.label('sci_done')
    a.b(0x3B)                       # RTI

    a.label('unused')
    a.b(0x3B)

    a.resolve()
    img = bytearray([0xFF] * 0x8000)
    img[:len(a.code)] = a.code
    # vectors at $FFE0-$FFFF -> offset $7FE0
    def vec(addr, label):
        v = a.labels[label]
        img[addr - 0x8000] = v >> 8
        img[addr - 0x8000 + 1] = v & 0xff
    for addr in range(0xFFEA, 0xFFFE, 2):
        vec(addr, 'unused')
    vec(0xFFF0, 'sci')
    vec(0xFFF8, 'irq1')
    vec(0xFFFE, 'reset')
    return img, a.labels


def main():
    b0, labels = build_bank(0)
    b1, _ = build_bank(1)
    assert labels['marker'] == 0x8000 + 13 + 1 + 16
    with open(sys.argv[1], 'wb') as f:
        f.write(b0 + b1)
    print('test ROM written:', sys.argv[1], {k: hex(v) for k, v in labels.items()})


if __name__ == '__main__':
    main()
