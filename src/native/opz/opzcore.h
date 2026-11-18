// OpzCore: a YM2414 (OPZ) synthesis loop written for the TX81Z use case, producing exactly the same
// samples as ymfm's ym2414 (checked by txnative-cli --check-ymfm / tools/native/test_ymfm_fork.py).
//
// It keeps ymfm's register file (ymfmx::opz_registers: register mapping, presets, operator cache,
// phase step, LFO and noise) and replaces ymfm's generic per-sample engine (fm_engine_base /
// fm_channel / fm_operator objects) with flat per-operator state and per-channel values cached at
// every register write. Changes that keep the output identical:
//   - operators at their envelope fixed point (reverb state, full attenuation) are not clocked
//   - the LFO PM phase step is memoised per operator
//   - register reads in the sample loop are replaced by values cached when registers change
#pragma once

#include <cstdint>

#include "ymfmx_opz.h"

namespace txn {

class OpzCore
{
public:
	OpzCore();
	void reset();
	void write(uint8_t reg, uint8_t data);   // address + data in one call
	void generate(int32_t *left, int32_t *right);

private:
	using Regs = ymfmx::opz_registers;
	using Cache = ymfmx::opdata_cache;

	struct Op
	{
		uint32_t phase = 0;
		uint16_t att = 0x3ff;   // same type as ymfm (the attack formula depends on it)
		uint8_t state = ymfmx::EG_RELEASE;
		uint8_t key_state = 0;
		uint8_t keyon_live = 0;
		uint8_t am_enable = 0;   // cached op_lfo_am_enable
		int32_t memo_pm = INT32_MIN;
		uint32_t memo_step = 0;
		Cache cache{};
	};
	struct Chan
	{
		int16_t fb[2] = { 0, 0 };
		int16_t fb_in = 0;
		// cached at prepare
		uint8_t feedback = 0, algorithm = 0, out_any = 0, out0 = 0, out1 = 0, am_sens = 0, am_sens2 = 0;
	};

	void prepare();
	void clock_keystate(Op &op, uint32_t keystate);
	void start_attack(Op &op);
	void clock_envelope(Op &op, uint32_t env_counter);
	int32_t compute_volume(const Op &op, uint32_t phase, uint32_t am_offset) const;
	int32_t compute_noise_volume(const Op &op, uint32_t am_offset) const;
	void output_4op(int ch, int32_t *out);

	Regs m_regs;
	Op m_op[32];        // by operator offset (register order)
	Chan m_ch[8];
	uint32_t m_env_counter = 0;
	uint32_t m_modified = 0xff;
	uint32_t m_prepare_count = 0;
	uint32_t m_active = 0;
	uint8_t m_address = 0;
	uint8_t m_noise_on = 0;
};

} // namespace txn
