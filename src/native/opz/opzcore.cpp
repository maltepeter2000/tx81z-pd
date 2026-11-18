#include "opzcore.h"

#include "ymfmx_fm.ipp"   // inline helpers: attenuation_to_volume, attenuation_increment

namespace txn {

using namespace ymfmx;

namespace {

constexpr uint32_t EG_QUIET = 0x380;

// channel operator slots (ymfm opz operator_map): OP index 0..3 -> operator offset
inline int opoffs(int ch, int idx)
{
	static const int add[4] = { 0, 16, 8, 24 };
	return ch + add[idx];
}

// algorithm routing table of ymfm's fm_channel::output_4op
#define ALG(op2in, op3in, op4in, op1out, op2out, op3out) \
	((op2in) | ((op3in) << 1) | ((op4in) << 4) | ((op1out) << 7) | ((op2out) << 8) | ((op3out) << 9))
const uint16_t ALGORITHM_OPS[8] = {
	ALG(1, 2, 3, 0, 0, 0), ALG(0, 5, 3, 0, 0, 0), ALG(0, 2, 6, 0, 0, 0), ALG(1, 0, 7, 0, 0, 0),
	ALG(1, 0, 3, 0, 1, 0), ALG(1, 1, 1, 0, 1, 1), ALG(1, 0, 0, 0, 1, 1), ALG(0, 0, 0, 1, 1, 1),
};
#undef ALG

inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

OpzCore::OpzCore()
{
	reset();
}

// ym2414::reset -> fm_engine_base::reset
void OpzCore::reset()
{
	m_regs.reset();
	write(0x14, 0);   // mode register (as ymfm: written on reset)
	for (auto &c : m_ch) { c.fb[0] = c.fb[1] = 0; c.fb_in = 0; }
	for (auto &o : m_op)
	{
		o.phase = 0;
		o.att = 0x3ff;
		o.state = EG_RELEASE;
		o.key_state = 0;
		o.keyon_live = 0;
	}
}

// fm_engine_base::write (the timers are not emulated, as in ymfm's default interface)
void OpzCore::write(uint8_t reg, uint8_t data)
{
	m_address = reg;
	m_modified = 0xff;
	uint32_t ch, opmask;
	if (m_regs.write(reg, data, ch, opmask) && ch < 8)
	{
		// fm_channel::keyonoff -> fm_operator::keyonoff (KEYON_NORMAL = bit 0); opmask bit i = op index i
		for (int i = 0; i < 4; i++)
		{
			Op &o = m_op[opoffs(int(ch), i)];
			o.keyon_live = uint8_t((o.keyon_live & ~1u) | ((opmask >> i) & 1));
		}
	}
}

void OpzCore::start_attack(Op &o)
{
	if (o.state == EG_ATTACK)
		return;
	o.state = EG_ATTACK;
	o.phase = 0;
	if (o.cache.eg_rate[EG_ATTACK] >= 62)
		o.att = 0;
}

void OpzCore::clock_keystate(Op &o, uint32_t keystate)
{
	if ((keystate ^ o.key_state) == 0)
		return;
	o.key_state = uint8_t(keystate);
	if (keystate)
		start_attack(o);
	else if (o.state < EG_RELEASE)
		o.state = EG_RELEASE;
}

// fm_engine_base::clock, prepare part
void OpzCore::prepare()
{
	m_active = 0;
	m_idle = 0;
	m_noise_on = uint8_t(m_regs.noise_enable());
	for (int ch = 0; ch < 8; ch++)
	{
		Chan &c = m_ch[ch];
		c.feedback = uint8_t(m_regs.ch_feedback(ch));
		c.algorithm = uint8_t(m_regs.ch_algorithm(ch));
		c.out_any = uint8_t(m_regs.ch_output_any(ch));
		c.out0 = uint8_t(m_regs.ch_output_0(ch));
		c.out1 = uint8_t(m_regs.ch_output_1(ch));
		c.am_sens = uint8_t(m_regs.ch_lfo_am_sens(ch));
		c.am_sens2 = uint8_t(m_regs.ch_lfo2_am_sens(ch));
		bool active = false;
		for (int i = 0; i < 4; i++)
		{
			int off = opoffs(ch, i);
			Op &o = m_op[off];
			m_regs.cache_operator_data(ch, off, o.cache);
			o.memo_pm = INT32_MIN;
			o.am_enable = uint8_t(m_regs.op_lfo_am_enable(off));
			clock_keystate(o, o.keyon_live != 0);
			if (o.state != EG_REVERB || o.att < EG_QUIET)
				active = true;
		}
		if (active)
			m_active |= 1u << ch;
		if (channel_idle(ch))
			m_idle |= 1u << ch;
	}
}

// all four operators at the envelope fixed point and none needs its fixed-frequency sub-step: the
// channel only shifts its feedback history until the next key on (which goes through prepare())
bool OpzCore::channel_idle(int ch) const
{
	for (int i = 0; i < 4; i++)
	{
		const Op &o = m_op[opoffs(ch, i)];
		if (o.state != EG_REVERB || o.att != 0x3ff)
			return false;
		if (o.cache.fix_mode && o.cache.phase_step == opdata_cache::PHASE_STEP_DYNAMIC)
			return false;
	}
	return true;
}

// fm_operator::clock_envelope (no SSG-EG on the OPZ)
void OpzCore::clock_envelope(Op &o, uint32_t env_counter)
{
	if (o.state == EG_ATTACK && o.att == 0)
		o.state = EG_DECAY;
	if (o.state == EG_DECAY && o.att >= o.cache.eg_sustain)
		o.state = EG_SUSTAIN;
	uint32_t rate = o.cache.eg_rate[o.state];
	uint32_t rate_shift = rate >> 2;
	env_counter <<= rate_shift;
	if (bitfield(env_counter, 0, 11) != 0)
		return;
	uint32_t relevant_bits = bitfield(env_counter, (rate_shift <= 11) ? 11 : rate_shift, 3);
	uint32_t increment = attenuation_increment(rate, relevant_bits);
	if (o.state == EG_ATTACK)
	{
		if (rate < 62)
			o.att += (~o.att * increment) >> 4;
	}
	else
	{
		o.att += increment;
		if (o.att >= 0x400)
			o.att = 0x3ff;
		if (o.state == EG_RELEASE && o.att >= 0xc0)
			o.state = EG_REVERB;
	}
}

inline int32_t OpzCore::compute_volume(const Op &o, uint32_t phase, uint32_t am_offset) const
{
	if (o.att > EG_QUIET)
		return 0;
	uint32_t sin_att = o.cache.waveform[phase & 0x3ff];
	uint32_t env = (o.att >> o.cache.eg_shift) + (o.am_enable ? am_offset : 0) + o.cache.total_level;
	env = (env < 0x3ff ? env : 0x3ff) << 2;
	int32_t result = int32_t(attenuation_to_volume((sin_att & 0x7fff) + env));
	return (sin_att & 0x8000) ? -result : result;
}

int32_t OpzCore::compute_noise_volume(const Op &o, uint32_t am_offset) const
{
	uint32_t env = (o.att >> o.cache.eg_shift) + (o.am_enable ? am_offset : 0) + o.cache.total_level;
	env = env < 0x3ff ? env : 0x3ff;
	int32_t result = int32_t((env ^ 0x3ff) << 1);
	return (m_regs.noise_state() & 1) ? -result : result;
}

// fm_channel::output_4op with rshift 0, clipmax 32767
void OpzCore::output_4op(int ch, int32_t *out)
{
	Chan &c = m_ch[ch];
	const Op &o1 = m_op[ch], &o2 = m_op[ch + 16], &o3 = m_op[ch + 8], &o4 = m_op[ch + 24];

	uint32_t am = 0;
	if (c.am_sens) am = m_regs.lfo_am_value(0) << (c.am_sens - 1);
	if (c.am_sens2) am += m_regs.lfo_am_value(1) << (c.am_sens2 - 1);

	int32_t opmod = 0;
	if (c.feedback)
		opmod = (c.fb[0] + c.fb[1]) >> (10 - c.feedback);
	int32_t op1 = compute_volume(o1, (o1.phase >> 10) + opmod, am);
	c.fb_in = int16_t(op1);
	if (!c.out_any)
		return;

	const uint32_t alg = ALGORITHM_OPS[c.algorithm];
	int16_t opout[8];
	opout[0] = 0;
	opout[1] = int16_t(op1);
	opmod = opout[alg & 1] >> 1;
	opout[2] = int16_t(compute_volume(o2, (o2.phase >> 10) + opmod, am));
	opout[5] = int16_t(opout[1] + opout[2]);
	opmod = opout[(alg >> 1) & 7] >> 1;
	opout[3] = int16_t(compute_volume(o3, (o3.phase >> 10) + opmod, am));
	opout[6] = int16_t(opout[1] + opout[3]);
	opout[7] = int16_t(opout[2] + opout[3]);
	int32_t result;
	if (m_noise_on && ch == 7)
		result = compute_noise_volume(o4, am);
	else
	{
		opmod = opout[(alg >> 4) & 7] >> 1;
		result = compute_volume(o4, (o4.phase >> 10) + opmod, am);
	}
	if (alg & 0x080) result = clampi(result + opout[1], -32768, 32767);
	if (alg & 0x100) result = clampi(result + opout[2], -32768, 32767);
	if (alg & 0x200) result = clampi(result + opout[3], -32768, 32767);
	if (c.out0) out[0] += result;
	if (c.out1) out[1] += result;
}

// ym2414::generate for one sample
void OpzCore::generate(int32_t *left, int32_t *right)
{
	// fm_engine_base::clock
	if (m_modified != 0 || m_prepare_count++ >= 4096)
	{
		prepare();
		m_modified = m_prepare_count = 0;
	}
	if (bitfield(++m_env_counter, 0, 2) == 3)   // EG_CLOCK_DIVIDER 3
		m_env_counter += 1;
	const int32_t lfo_raw_pm = m_regs.clock_noise_and_lfo();
	const bool eg_tick = bitfield(m_env_counter, 0, 2) == 0;
	const uint32_t eg_count = m_env_counter >> 2;

	for (int ch = 0; ch < 8; ch++)
	{
		Chan &c = m_ch[ch];
		c.fb[0] = c.fb[1];
		c.fb[1] = c.fb_in;
		if (m_idle & (1u << ch))
			continue;
		for (int i = 0; i < 4; i++)
		{
			const int off = opoffs(ch, i);
			Op &o = m_op[off];
			// fixed point (see header): only a fixed-frequency sub-step keeps running
			if (o.state == EG_REVERB && o.att == 0x3ff)
			{
				if (o.cache.fix_mode && o.cache.phase_step == opdata_cache::PHASE_STEP_DYNAMIC)
					m_regs.compute_phase_step(ch, off, o.cache, lfo_raw_pm);
				continue;
			}
			if (eg_tick)
				clock_envelope(o, eg_count);
			uint32_t step = o.cache.phase_step;
			if (step == opdata_cache::PHASE_STEP_DYNAMIC)
			{
				if (o.cache.fix_mode)
					step = m_regs.compute_phase_step(ch, off, o.cache, lfo_raw_pm);
				else
				{
					if (lfo_raw_pm != o.memo_pm)
					{
						o.memo_pm = lfo_raw_pm;
						o.memo_step = m_regs.compute_phase_step(ch, off, o.cache, lfo_raw_pm);
					}
					step = o.memo_step;
				}
			}
			o.phase += step;
		}
		if (channel_idle(ch))
			m_idle |= 1u << ch;
	}

	// fm_engine_base::output over the active channels, then the YM2414 output stage
	int32_t out[2] = { 0, 0 };
	for (int ch = 0; ch < 8; ch++)
		if (m_active & (1u << ch))
			output_4op(ch, out);
	*left = roundtrip_fp(out[0]);
	*right = roundtrip_fp(out[1]);
}

} // namespace txn
