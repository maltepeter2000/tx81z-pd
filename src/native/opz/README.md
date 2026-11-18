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
