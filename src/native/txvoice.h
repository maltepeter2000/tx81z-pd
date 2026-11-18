// TX81Z voice records -> VCED / ACED parameter blocks.
//
// The ROM keeps the preset banks A-D and the battery RAM the internal bank as 78-byte records:
// the voice bulk format (VMEM) bytes 0..66 followed by its TX81Z additions, bytes 73..83 (the
// unused DX21 pitch EG bytes 67..72 are left out). Checked against the firmware's own VCED / ACED
// dumps of all 128 programs (tools/native/vmem.py).
#pragma once

#include <cstddef>
#include <cstdint>

namespace txn {

constexpr size_t VOICE_RECORD = 78;
constexpr size_t ROM_PRESETS = 0x51C0;     // ROM file offset: banks A, B, C, D (32 records each)
constexpr size_t NVRAM_INTERNAL = 0x0001;  // battery RAM offset: internal bank (32 records)

inline void unpack_voice(const uint8_t *rec, uint8_t vced[93], uint8_t aced[23])
{
	for (int oi = 0; oi < 4; oi++)   // operator blocks in VCED order
	{
		const uint8_t *o = rec + oi * 10;
		uint8_t *v = vced + oi * 13;
		for (int k = 0; k < 6; k++) v[k] = o[k];          // AR D1R D2R RR D1L LS
		v[8] = (o[6] >> 6) & 1;                            // AME
		v[7] = (o[6] >> 3) & 7;                            // EBS
		v[9] = o[6] & 7;                                   // KVS
		v[10] = o[7];                                      // OUT
		v[11] = o[8];                                      // CRS
		v[6] = (o[9] >> 3) & 3;                            // RS
		v[12] = o[9] & 7;                                  // DET
		const uint8_t *x = rec + 67 + oi * 2;
		uint8_t *a = aced + oi * 5;
		a[4] = (x[0] >> 4) & 3;                            // EG shift
		a[0] = (x[0] >> 3) & 1;                            // FIX
		a[1] = x[0] & 7;                                   // FIX range
		a[3] = (x[1] >> 4) & 7;                            // OSW
		a[2] = x[1] & 15;                                  // FINE
	}
	vced[58] = (rec[40] >> 6) & 1; vced[53] = (rec[40] >> 3) & 7; vced[52] = rec[40] & 7;
	vced[54] = rec[41]; vced[55] = rec[42]; vced[56] = rec[43]; vced[57] = rec[44];
	vced[60] = (rec[45] >> 4) & 7; vced[61] = (rec[45] >> 2) & 3; vced[59] = rec[45] & 3;
	vced[62] = rec[46]; vced[64] = rec[47];
	vced[70] = (rec[48] >> 4) & 1; vced[63] = (rec[48] >> 3) & 1; vced[68] = (rec[48] >> 2) & 1;
	vced[69] = (rec[48] >> 1) & 1; vced[65] = rec[48] & 1;
	vced[66] = rec[49]; vced[67] = rec[50];
	for (int k = 0; k < 6; k++) vced[71 + k] = rec[51 + k];
	for (int k = 0; k < 10; k++) vced[77 + k] = rec[57 + k];
	static const uint8_t PEG[6] = { 99, 99, 99, 50, 50, 50 };   // DX21 pitch EG, unused
	for (int k = 0; k < 6; k++) vced[87 + k] = PEG[k];
	aced[20] = rec[75]; aced[21] = rec[76]; aced[22] = rec[77];
}

} // namespace txn
