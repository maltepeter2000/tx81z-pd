# ymfm fork for the native engine

Copies of `ymfm.h`, `ymfm_fm.h`, `ymfm_fm.ipp`, `ymfm_opz.h`, `ymfm_opz.cpp` from ymfm (BSD-3-Clause,
Aaron Giles; license header in each file), in namespace `ymfmx` so they can sit next to the unmodified
ymfm used by the firmware engine. Changes are marked `[tx81z]`; all of them keep the output bit-identical
(checked by `txnative-cli --check-ymfm` and `tools/native/test_ymfm_fork.py`):

- an operator in the reverb state at full attenuation is a fixed point and is not clocked (only the
  sub-step accumulator of a fixed-frequency operator keeps running)
- with LFO pitch modulation the phase step is memoised per operator and recomputed only when the PM
  value or the cache changes
- the fixed-frequency flag is cached with the operator data

## OpzCore (`opzcore.h/.cpp`)

The engine uses `txn::OpzCore`: ymfm's OPZ register file (`ymfmx::opz_registers`) with a flat
per-sample loop written for the TX81Z (32 operators, 8 four-operator channels) instead of ymfm's generic
engine/channel/operator objects. It contains the two optimisations above and caches per-channel register
values at every register write. Output is bit-identical to the unmodified ymfm (`--check-ymfm`).
Desktop x86: same speed as the optimised fork, about 8-11 % fewer instructions (relevant on the in-order
Cortex-A8 of the Bela).
