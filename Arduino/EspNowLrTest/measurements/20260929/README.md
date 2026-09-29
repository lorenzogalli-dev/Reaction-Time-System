# 2026-09-29 — EspNowLrTest sessions

Files are renamed from the logger's `HHMMSS_<role>.csv` to say which test they
are. `_A` = start board (Mac), `_B` = finish board (Windows). Any pair runs
through `../../analyse.py <A> <B>`. The numbers are in `HANDOFF.md`, 09-29
sections.

| file | logger name | what |
|---|---|---|
| `01_bench-1m_A.csv` | `121838_A` | bench, boards ~1 m apart, stock ceramic antennas. Includes the bench `ap on` coexistence check. The B side of this session stayed on the Windows laptop |
| `02_walk-ceramic-antenna_{A,B}` | `125103_A`, `125113_B` | first walk, 0-125 m every 25 m, stock ceramic antennas, A on the ground, B on a stick ~1 m, people moving in between. Dead between 75 and 100 m |
| `03_track-walk-31mm-wire_{A,B}` | `151436_A`, `151358_B` | athletics track, **31 mm wire soldered on each board's ceramic antenna** (straight up, with a small horizontal loop). 0-160 m. Holds to ~130 m, dead by 160. The last station was typed as `160160`: it is 160 m |
| `04_110m-crouched-phone-B2m_{A,B}` | `154825_A`, `154615_B` | same boards, phone connected to B's AP (`ap on`) for the whole file. Seven 30 s stations at 110 m: 1-6 with a person **crouched over A** (some with repeated starts, not split in the log); **7 with B raised to ~2 m**. Then one station at 130 m, B still high |
