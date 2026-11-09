// Minimal HD44780 LCD controller (16x2) for the TX81Z emulator.
// Implements the instruction set the firmware needs: 8/4-bit bus, DDRAM,
// CGRAM, entry mode, cursor/display shift, busy flag (never busy).
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

class HD44780
{
public:
	HD44780() { reset(); }

	void reset()
	{
		std::memset(m_ddram, 0x20, sizeof(m_ddram));
		std::memset(m_cgram, 0, sizeof(m_cgram));
		m_ac = 0; m_dir = 1; m_shift_on_write = false; m_disp_shift = 0;
		m_cg_mode = false; m_8bit = true; m_nibble = false; m_rnibble = false;
		m_display_on = false; m_dirty = true;
	}

	void write(int offset, uint8_t data)
	{
		if (!m_8bit)
		{
			if (!m_nibble) { m_latch = data & 0xf0; m_nibble = true; return; }
			data = m_latch | (data >> 4);
			m_nibble = false;
		}
		if (offset & 1) write_data(data); else write_command(data);
	}

	uint8_t read(int offset)
	{
		uint8_t v = (offset & 1) ? read_data_peek() : uint8_t(m_ac & 0x7f); // busy flag always 0
		if (!m_8bit)
		{
			m_rnibble = !m_rnibble;
			if (m_rnibble) return v & 0xf0;
			v = uint8_t(v << 4);
		}
		if (offset & 1) advance();
		return v;
	}

	// 16 visible characters of line 0/1; custom chars shown as digits 0-7,
	// a few Yamaha/HD44780 ROM specials mapped to ASCII
	std::string line(int n) const
	{
		char buf[17];
		line_chars(n, buf);
		return std::string(buf);
	}

	// allocation-free variant (real-time safe): writes 16 chars + NUL
	void line_chars(int n, char *out) const
	{
		for (int i = 0; i < 16; i++)
		{
			int col = (i + m_disp_shift) % 40;
			if (col < 0) col += 40;
			out[i] = printable(m_ddram[n * 0x40 + col]);
		}
		out[16] = 0;
	}

	bool display_on() const { return m_display_on; }
	bool dirty() { bool d = m_dirty; m_dirty = false; return d; }
	bool dirty_peek() const { return m_dirty; }

private:
	static char printable(uint8_t c)
	{
		if (c < 8) return char('0' + c);  // CGRAM characters
		if (c == 0x7e) return '>';        // right arrow in A00 ROM
		if (c == 0x7f) return '<';        // left arrow
		if (c >= 0x20 && c < 0x7e) return char(c);
		return '?';
	}

	void write_command(uint8_t d)
	{
		if (d & 0x80) { m_ac = d & 0x7f; m_cg_mode = false; }
		else if (d & 0x40) { m_ac = d & 0x3f; m_cg_mode = true; }
		else if (d & 0x20) { m_8bit = (d & 0x10) != 0; m_nibble = false; }
		else if (d & 0x10)
		{
			bool display = d & 0x08, right = d & 0x04;
			if (display) { m_disp_shift += right ? -1 : 1; m_dirty = true; }
			else m_ac = uint8_t((m_ac + (right ? 1 : -1)) & 0x7f);
		}
		else if (d & 0x08) { m_display_on = d & 0x04; m_dirty = true; }
		else if (d & 0x04) { m_dir = (d & 0x02) ? 1 : -1; m_shift_on_write = d & 0x01; }
		else if (d & 0x02) { m_ac = 0; m_disp_shift = 0; m_cg_mode = false; m_dirty = true; }
		else if (d & 0x01)
		{
			std::memset(m_ddram, 0x20, sizeof(m_ddram));
			m_ac = 0; m_dir = 1; m_disp_shift = 0; m_cg_mode = false; m_dirty = true;
		}
	}

	void write_data(uint8_t d)
	{
		if (m_cg_mode) m_cgram[m_ac & 0x3f] = d;
		else m_ddram[m_ac & 0x7f] = d;
		m_dirty = true;
		advance();
		if (m_shift_on_write && !m_cg_mode) m_disp_shift += m_dir;
	}

	uint8_t read_data_peek() const
	{
		return m_cg_mode ? m_cgram[m_ac & 0x3f] : m_ddram[m_ac & 0x7f];
	}

	void advance()
	{
		if (m_cg_mode) m_ac = uint8_t((m_ac + m_dir) & 0x3f);
		else
		{
			int a = m_ac + m_dir;
			// 2-line mode: 0x00-0x27 and 0x40-0x67
			if (a == 0x28) a = 0x40;
			else if (a == 0x68) a = 0x00;
			else if (a == 0x3f) a = 0x27;
			else if (a < 0) a = 0x67;
			m_ac = uint8_t(a & 0x7f);
		}
	}

	uint8_t m_ddram[0x80];
	uint8_t m_cgram[0x40];
	uint8_t m_ac;
	int m_dir;
	bool m_shift_on_write;
	int m_disp_shift;
	bool m_cg_mode, m_8bit, m_nibble, m_rnibble, m_display_on, m_dirty;
	uint8_t m_latch = 0;
};
