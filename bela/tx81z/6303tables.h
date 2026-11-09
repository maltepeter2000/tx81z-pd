// license:BSD-3-Clause
// copyright-holders:Aaron Giles
// Cycle and dispatch tables extracted verbatim from MAME src/devices/cpu/m6800/m6801.cpp
#define XX 4 // illegal opcode unknown cycle count
const u8 hd6303x::cycles_63701[256] =
{
		/* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
	/*0*/ XX, 1,XX,XX, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	/*1*/  1, 1,XX,XX,XX,XX, 1, 1, 2, 2, 4, 1,XX,XX,XX,XX,
	/*2*/  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	/*3*/  1, 1, 3, 3, 1, 1, 4, 4, 4, 5, 1,10, 5, 7, 9,12,
	/*4*/  1,XX,XX, 1, 1,XX, 1, 1, 1, 1, 1,XX, 1, 1,XX, 1,
	/*5*/  1,XX,XX, 1, 1,XX, 1, 1, 1, 1, 1,XX, 1, 1,XX, 1,
	/*6*/  6, 7, 7, 6, 6, 7, 6, 6, 6, 6, 6, 5, 6, 4, 3, 5,
	/*7*/  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 6, 4, 3, 5,
	/*8*/  2, 2, 2, 3, 2, 2, 2,XX, 2, 2, 2, 2, 3, 5, 3,XX,
	/*9*/  3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 5, 4, 4,
	/*A*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
	/*B*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 5, 5,
	/*C*/  2, 2, 2, 3, 2, 2, 2,XX, 2, 2, 2, 2, 3,XX, 3,XX,
	/*D*/  3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
	/*E*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
	/*F*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5
};

#undef XX

const hd6303x::op_func hd6303x::hd63701_insn[0x100] = {
// 0/8                     1/9                        2/A                        3/B                        4/C                        5/D                        6/E                        7/F
&hd6303x::trap,   &hd6303x::nop,    &hd6303x::trap,   &hd6303x::trap,   &hd6303x::lsrd,   &hd6303x::asld,   &hd6303x::tap,    &hd6303x::tpa,    // 0
&hd6303x::inx,    &hd6303x::dex,    &hd6303x::clv,    &hd6303x::sev,    &hd6303x::clc,    &hd6303x::sec,    &hd6303x::cli,    &hd6303x::sei,
&hd6303x::sba,    &hd6303x::cba,    &hd6303x::undoc1, &hd6303x::undoc2, &hd6303x::trap,   &hd6303x::trap,   &hd6303x::tab,    &hd6303x::tba,    // 1
&hd6303x::xgdx,   &hd6303x::daa,    &hd6303x::slp,    &hd6303x::aba,    &hd6303x::trap,   &hd6303x::trap,   &hd6303x::trap,   &hd6303x::trap,
&hd6303x::bra,    &hd6303x::brn,    &hd6303x::bhi,    &hd6303x::bls,    &hd6303x::bcc,    &hd6303x::bcs,    &hd6303x::bne,    &hd6303x::beq,    // 2
&hd6303x::bvc,    &hd6303x::bvs,    &hd6303x::bpl,    &hd6303x::bmi,    &hd6303x::bge,    &hd6303x::blt,    &hd6303x::bgt,    &hd6303x::ble,
&hd6303x::tsx,    &hd6303x::ins,    &hd6303x::pula,   &hd6303x::pulb,   &hd6303x::des,    &hd6303x::txs,    &hd6303x::psha,   &hd6303x::pshb,   // 3
&hd6303x::pulx,   &hd6303x::rts,    &hd6303x::abx,    &hd6303x::rti,    &hd6303x::pshx,   &hd6303x::mul,    &hd6303x::wai,    &hd6303x::swi,
&hd6303x::nega,   &hd6303x::trap,   &hd6303x::trap,   &hd6303x::coma,   &hd6303x::lsra,   &hd6303x::trap,   &hd6303x::rora,   &hd6303x::asra,   // 4
&hd6303x::asla,   &hd6303x::rola,   &hd6303x::deca,   &hd6303x::trap,   &hd6303x::inca,   &hd6303x::tsta,   &hd6303x::trap,   &hd6303x::clra,
&hd6303x::negb,   &hd6303x::trap,   &hd6303x::trap,   &hd6303x::comb,   &hd6303x::lsrb,   &hd6303x::trap,   &hd6303x::rorb,   &hd6303x::asrb,   // 5
&hd6303x::aslb,   &hd6303x::rolb,   &hd6303x::decb,   &hd6303x::trap,   &hd6303x::incb,   &hd6303x::tstb,   &hd6303x::trap,   &hd6303x::clrb,
&hd6303x::neg_ix, &hd6303x::aim_ix, &hd6303x::oim_ix, &hd6303x::com_ix, &hd6303x::lsr_ix, &hd6303x::eim_ix, &hd6303x::ror_ix, &hd6303x::asr_ix, // 6
&hd6303x::asl_ix, &hd6303x::rol_ix, &hd6303x::dec_ix, &hd6303x::tim_ix, &hd6303x::inc_ix, &hd6303x::tst_ix, &hd6303x::jmp_ix, &hd6303x::clr_ix,
&hd6303x::neg_ex, &hd6303x::aim_di, &hd6303x::oim_di, &hd6303x::com_ex, &hd6303x::lsr_ex, &hd6303x::eim_di, &hd6303x::ror_ex, &hd6303x::asr_ex, // 7
&hd6303x::asl_ex, &hd6303x::rol_ex, &hd6303x::dec_ex, &hd6303x::tim_di, &hd6303x::inc_ex, &hd6303x::tst_ex, &hd6303x::jmp_ex, &hd6303x::clr_ex,
&hd6303x::suba_im,&hd6303x::cmpa_im,&hd6303x::sbca_im,&hd6303x::subd_im,&hd6303x::anda_im,&hd6303x::bita_im,&hd6303x::lda_im, &hd6303x::trap,   // 8
&hd6303x::eora_im,&hd6303x::adca_im,&hd6303x::ora_im, &hd6303x::adda_im,&hd6303x::cpx_im ,&hd6303x::bsr,    &hd6303x::lds_im, &hd6303x::trap,
&hd6303x::suba_di,&hd6303x::cmpa_di,&hd6303x::sbca_di,&hd6303x::subd_di,&hd6303x::anda_di,&hd6303x::bita_di,&hd6303x::lda_di, &hd6303x::sta_di, // 9
&hd6303x::eora_di,&hd6303x::adca_di,&hd6303x::ora_di, &hd6303x::adda_di,&hd6303x::cpx_di ,&hd6303x::jsr_di, &hd6303x::lds_di, &hd6303x::sts_di,
&hd6303x::suba_ix,&hd6303x::cmpa_ix,&hd6303x::sbca_ix,&hd6303x::subd_ix,&hd6303x::anda_ix,&hd6303x::bita_ix,&hd6303x::lda_ix, &hd6303x::sta_ix, // A
&hd6303x::eora_ix,&hd6303x::adca_ix,&hd6303x::ora_ix, &hd6303x::adda_ix,&hd6303x::cpx_ix ,&hd6303x::jsr_ix, &hd6303x::lds_ix, &hd6303x::sts_ix,
&hd6303x::suba_ex,&hd6303x::cmpa_ex,&hd6303x::sbca_ex,&hd6303x::subd_ex,&hd6303x::anda_ex,&hd6303x::bita_ex,&hd6303x::lda_ex, &hd6303x::sta_ex, // B
&hd6303x::eora_ex,&hd6303x::adca_ex,&hd6303x::ora_ex, &hd6303x::adda_ex,&hd6303x::cpx_ex ,&hd6303x::jsr_ex, &hd6303x::lds_ex, &hd6303x::sts_ex,
&hd6303x::subb_im,&hd6303x::cmpb_im,&hd6303x::sbcb_im,&hd6303x::addd_im,&hd6303x::andb_im,&hd6303x::bitb_im,&hd6303x::ldb_im, &hd6303x::trap,   // C
&hd6303x::eorb_im,&hd6303x::adcb_im,&hd6303x::orb_im, &hd6303x::addb_im,&hd6303x::ldd_im, &hd6303x::trap,   &hd6303x::ldx_im, &hd6303x::trap,
&hd6303x::subb_di,&hd6303x::cmpb_di,&hd6303x::sbcb_di,&hd6303x::addd_di,&hd6303x::andb_di,&hd6303x::bitb_di,&hd6303x::ldb_di, &hd6303x::stb_di, // D
&hd6303x::eorb_di,&hd6303x::adcb_di,&hd6303x::orb_di, &hd6303x::addb_di,&hd6303x::ldd_di, &hd6303x::std_di, &hd6303x::ldx_di, &hd6303x::stx_di,
&hd6303x::subb_ix,&hd6303x::cmpb_ix,&hd6303x::sbcb_ix,&hd6303x::addd_ix,&hd6303x::andb_ix,&hd6303x::bitb_ix,&hd6303x::ldb_ix, &hd6303x::stb_ix, // E
&hd6303x::eorb_ix,&hd6303x::adcb_ix,&hd6303x::orb_ix, &hd6303x::addb_ix,&hd6303x::ldd_ix, &hd6303x::std_ix, &hd6303x::ldx_ix, &hd6303x::stx_ix,
&hd6303x::subb_ex,&hd6303x::cmpb_ex,&hd6303x::sbcb_ex,&hd6303x::addd_ex,&hd6303x::andb_ex,&hd6303x::bitb_ex,&hd6303x::ldb_ex, &hd6303x::stb_ex, // F
&hd6303x::eorb_ex,&hd6303x::adcb_ex,&hd6303x::orb_ex, &hd6303x::addb_ex,&hd6303x::ldd_ex, &hd6303x::std_ex, &hd6303x::ldx_ex, &hd6303x::stx_ex
};
