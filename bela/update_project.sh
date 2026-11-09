#!/bin/sh
# Builds the flat Bela project folder bela/tx81z from the shared sources.
# - all files in one folder (the Bela IDE does not keep subfolders reliably)
# - ymfm copy patched to compile with Bela's default C++11
#   (two constexpr functions without assert, make_unique -> unique_ptr(new ...))
# Run after changing anything in src/ or third_party/ymfm.
set -e
cd "$(dirname "$0")/.."
P=bela/tx81z
mkdir -p $P
rm -rf $P/cpu
cp src/tx81z.cpp src/hd44780.h $P/
sed 's|#include "cpu/hd6303x.h"|#include "hd6303x.h"|' src/tx81z.h > $P/tx81z.h
cp src/cpu/hd6303x.cpp src/cpu/hd6303x.h src/cpu/6800ops.hxx src/cpu/6800ops_list.h src/cpu/6800macros.h src/cpu/6303tables.h $P/
cp third_party/ymfm/src/ymfm.h third_party/ymfm/src/ymfm_opz.cpp $P/
# C++11: constexpr functions may only contain a return statement
sed '/static constexpr uint32_t channel_offset/,/^	}/{/assert(chnum < CHANNELS);/d}; /static constexpr uint32_t operator_offset/,/^	}/{/assert(opnum < OPERATORS);/d}' \
    third_party/ymfm/src/ymfm_opz.h > $P/ymfm_opz.h
cp third_party/ymfm/src/ymfm_fm.h $P/
# C++11: no std::make_unique
sed 's|std::make_unique<fm_channel<RegisterType>>(|std::unique_ptr<fm_channel<RegisterType>>(new fm_channel<RegisterType>(|; s|std::make_unique<fm_operator<RegisterType>>(|std::unique_ptr<fm_operator<RegisterType>>(new fm_operator<RegisterType>(|; s|RegisterType::channel_offset(chnum));|RegisterType::channel_offset(chnum)));|; s|RegisterType::operator_offset(opnum));|RegisterType::operator_offset(opnum)));|' \
    third_party/ymfm/src/ymfm_fm.ipp > $P/ymfm_fm.ipp
cp LICENSE.ymfm LICENSE.mame-derived $P/
echo "updated $P"
