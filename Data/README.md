# Data — where the captures came from

All CSVs are written by `capture.py` from the board's serial dump. One file is
one start attempt: the window `[set − 3 s, go + 1 s]`, plus the firmware's own
`on`/`set`/`go` timestamps in the header comments, on the same `micros()` clock
as every sample.

| folder | what it is |
|---|---|
| `block_starts_090926/` | 19 attempts, 2026-09-09. **A real athlete on real starting blocks**, board mounted on the back of the block. |
| `block_starts_110926/` | 8 attempts, 2026-09-11. Same athlete, same mounting, **after the buzzer fix**. |
| `bench_140926/` | 3 attempts, 2026-09-14. **Bench, on a table, not an athlete** - the first runs of the arming-gate firmware, kept as the hardware proof that the gate, the `ARM` marker and the in-CSV verdict work. Do not tune against them. |
| `block_starts_140926/` | 45 attempts, 2026-09-14. **A real athlete on real starting blocks**, first live captures through the finished arm-on-stillness gate (`7927ead`). 40 valid starts (median 147.0 ms, range 111.3-476.5 ms), 5 false starts. |
| `block_starts_150926/` | 11 attempts, 2026-09-15. Same athlete, same gate. All 11 valid starts (median 150.5 ms, range 119.3-213.4 ms). |
| `data_before_080926/` | pre-v4 archive. The `go` marker in these is a human keypress relayed over serial, so it lands up to ~90 ms off. Do not tune anything against them. |

## The difference between the two block-start sets is the stimulus, and it shows

`090926` was recorded through a piezo driven at **3 kHz — off its 4 kHz
resonance, single-ended, barely audible**. `110926` is through the fix: 4 kHz at
resonance, antiphase drive, ~6 dB louder.

Reaction time, valid starts only:

| | median | range |
|---|---|---|
| `090926`, quiet beep | 178 ms | 127 - 299 |
| `110926`, loud beep | **159 ms** | 132 - 169 |

About 19 ms faster, and far tighter: 37 ms of spread against 172 ms. That is
Piéron's law — reaction time falls with stimulus intensity — and it is the
reason `HANDOFF.md` says no number from `090926` may be used to tune anything.
The stimulus behind those numbers is not the one the device now produces.

From `bench_140926/` onward a capture also carries `arm_t_s` (when the board
armed), `arm_peak_mg`, `arm_capped`, and the board's own `board_verdict` /
`board_reaction_ms` / `board_onset_t_us`. `start_detector.py` reads the arming
instant rather than re-deriving it: that decision was taken by the board, and
recomputing it would be second-guessing what actually happened.

Holding the set position reads 7.0 mg at p95 in `090926` and **4.8 mg** in
`110926`; the push-off is 1000-3600 mg in both.
