# How the start detector works

A guide to `Tools/start_detector.py` — what it measures, how, and why it ended
up different from the version it grew out of.

Written for someone who knows the project but not this code. No signal
processing background assumed.

---

## 1. What the tool has to do

An accelerometer is mounted on the starting block. It records continuously.
Somewhere in that recording, the athlete pushes off. The tool has to answer
three questions:

1. **Did the athlete move?**
2. **Exactly when did the movement start?**
3. **Was that legal** — after the go signal, and not so soon after it that they
   must have anticipated it?

Question 2 is the hard one, and almost everything below is about it. A reaction
time is the gap between two instants, so an error of 10 ms in finding the start
of the movement is an error of 10 ms in the result — and the difference between
a legal start and a disqualification is 100 ms.

---

## 2. Where we started

There were two scripts doing overlapping work: `sta_lta_start_detector.py` and
`detect_pushoff.py`. Each got something right that the other did not.

**What `sta_lta_start_detector.py` had right, and what was kept:**

- **The interface.** A sidebar of parameters, a results table, a chart, and a
  button that re-runs everything. Tuning a detector means trying a number,
  looking at the result, and trying another — and a GUI makes that a loop
  instead of an edit-save-rerun cycle. The current tool's interface *is* that
  interface; it was kept deliberately, not rebuilt.
- **Re-arming with hysteresis.** After reporting an event it waited for the
  signal to calm down before it would report another, so a recording could
  contain several. This turned out to matter more than it looks: the other
  script reported exactly one event per file and stopped, which meant a small
  twitch could hide the real push-off behind it. That behaviour was taken from
  here and is now in the merged tool.

**What `detect_pushoff.py` had right:**

- It was written **causally** — processing one sample at a time, never looking
  ahead — so the logic can be ported to C++ on the device without changing it.
- It measured the sample rate from each recording instead of assuming one.
- It did **not** decide on the raw acceleration magnitude (more on this next).

So the current tool is the first one's interface and the second one's
structure, plus three changes that neither had. Each change below is stated
with the measurement that motivated it, so it can be checked rather than taken
on trust.

---

## 3. Change one: what signal the decision is made on

**The obvious choice is wrong, and it is wrong in the direction that matters.**

The intuitive thing is to take the total acceleration — combine the three axes
into one number, `|a| = sqrt(x² + y² + z²)` — and watch for it to jump. That is
what `sta_lta_start_detector.py` did.

The problem is gravity. The sensor always reads about 1 g downward, even at
rest. When the athlete accelerates *horizontally*, that new acceleration is at
right angles to gravity, so the two combine the way the sides of a right
triangle do — and the total barely changes. A measured example from the earlier
sensor evaluation: a movement whose real strength was 196 mg showed up in the
total as **46 mg**. Under-reported by a factor of 4.2.

The drive out of the blocks is mostly horizontal. So the total magnitude hides
exactly the direction we most need to see.

**The fix, first attempt (`detect_pushoff.py`):** find which axis gravity is on,
throw that axis away, use the other two. Better, but it only works if the sensor
is mounted perfectly flat. Tilt it 20° on the block and some gravity leaks into
the two "horizontal" axes while some of the real horizontal drive leaks into the
"vertical" one.

**The fix now:** treat gravity as a *direction in space*, not as an axis. From a
few hundred milliseconds of the athlete at rest we measure which way is down —
as a vector, at whatever angle it happens to be. Then every sample is split into
the part along that direction and the part perpendicular to it:

```
vertical   = how much of the acceleration points along gravity
horizontal = what is left after removing that part
```

No axis is chosen and no mounting angle is assumed. Tilt the sensor however you
like; the split still comes out right. The vertical component drops out for free
as well, which may be useful later.

**This is the signal every decision below is made on.** It is also the middle
panel in the chart — worth remembering, because the raw axes in the top panel
can look completely flat exactly where the detector correctly fires.

---

## 4. Change two: separating *that* it happened from *when* it started

This is the change that improved the numbers the most, and the one most worth
understanding.

### The method both old scripts used

Both used **STA/LTA**, which is standard in earthquake detection and is a good
choice. The idea is simple: keep two running averages of how much the signal is
moving — a short one (about 15 ms, "what is happening right now") and a long one
(about 800 ms, "what has been normal lately") — and compare them. When the short
one is several times the long one, something has changed.

The strength of this is that it adapts. It does not need to know how noisy the
environment is; it compares the present against the recent past. On a windy day
with a vibrating track, the long average rises and the detector automatically
becomes less twitchy.

STA/LTA is kept, unchanged in spirit. It is very good at answering *did
something happen*.

### Where it is weak

It is not good at answering *when did it start*.

To report a time, the old approach reported the moment the signal crossed a
fixed threshold — 20 mg by default. But a push-off does not begin abruptly. It
ramps up. So the reported instant is wherever your threshold happens to
intersect that ramp, and choosing the threshold higher or lower moves the answer.

Measured on a real recording from 2026-09-08:

| threshold | reported reaction time |
|---|---|
| 10 mg | 307.2 ms |
| 20 mg | 315.3 ms |
| 30 mg | 323.4 ms |
| 50 mg | 336.2 ms |

**A 29 ms spread, from a parameter nobody can derive from first principles.**

And you cannot simply set the threshold very low, because the sensor's own
background noise peaks around 7.6 mg. Go below that and you start detecting
noise as movement.

Worse, this bias is not a constant you could subtract once. The ramp rises at
about 1.25 mg per millisecond for one athlete and 3.49 mg/ms for another —
almost three times steeper. The steeper the ramp, the sooner it crosses any
given threshold. **So the explosive athlete is measured as reacting faster than
they did, relative to the slower one, purely as an artefact of the method.**

That is not something better tuning fixes. It is built into using a threshold.

### The fix: a second stage

Earthquake seismology has exactly this problem — a wave emerging gradually from
background noise — and solved it decades ago by splitting the job in two:

1. **STA/LTA decides that an event happened.** Robust, adaptive, approximate
   about timing.
2. **A "picker" then goes back and works out when it started.** Precise, and
   with no threshold at all.

The picker used here is called **AIC**. Section 6 explains it in plain terms.

The result, same recording, same sweep:

| threshold | with the picker | without |
|---|---|---|
| 10 mg | **302.6 ms** | 307.2 ms |
| 20 mg | **302.6 ms** | 315.3 ms |
| 30 mg | **302.6 ms** | 323.4 ms |
| 50 mg | **302.6 ms** | 336.2 ms |

**Spread: 0.0 ms, against 29.0 ms.** It lands on the same sample every time.

The threshold has not become a better number — it has become an *unimportant*
one. It now only has to say "something happened around here". Being wrong about
it by a factor of five no longer moves the measurement.

### It has not flattened everything to one answer

A reasonable worry: if it always gives the same number, is it actually reading
the data? It is. The 0.0 ms is across *parameter changes on one recording*, not
across recordings:

| recording | reaction time | correction applied | ramp steepness |
|---|---|---|---|
| A | 302.6 ms | −12.8 ms | 1.25 mg/ms |
| B | 270.6 ms | −5.8 ms | 3.49 mg/ms |
| C | 329.0 ms | −15.0 ms | 2.17 mg/ms |

Different results, and different corrections. If the picker were applying a
fixed offset, the middle column would be constant. It is not.

There is a neat confirmation in these numbers. The correction *should* be
roughly `threshold ÷ ramp steepness` — the time the ramp takes to climb from
nothing to the threshold, which is exactly the delay the threshold introduced:

- Recording B: 20 ÷ 3.49 = 5.7 ms predicted, **5.8 ms measured**
- Recording A: 20 ÷ 1.25 = 16 ms predicted, **12.8 ms measured**

It removes more from the slow ramp and less from the steep one — which is
precisely the athlete-dependent bias described above.

---

## 5. Change three: the set position is not a false start

Neither old script had a concept of what the athlete is *supposed* to be doing.

At the "set" command an athlete **raises the hips into position**. That is a
real movement, several hundred mg, lasting about a second. A rule that says "any
movement between set and go is a false start" therefore flags, on every single
attempt, the exact movement the "set" command just ordered.

So the first second after "set" is **not judged**. It is still analysed — events
there appear in the results table labelled `rise into set`, so an athlete who
genuinely did start during their rise is visible to a human rather than silently
swallowed.

**A stillness-based alternative was considered and rejected.** The idea was to
wait until the signal goes quiet and only then start judging. It sounds better,
and it has a fatal flaw: an athlete who never goes quiet *because they are
already starting* would never satisfy it, so the detector would never arm — it
would disarm itself in precisely the case it exists to catch. A fixed clock
always runs out.

There is a third possible outcome, `NOT JUDGEABLE`: if the athlete is still
moving when the blanking period ends, the rise had not finished, and nothing
after it can be told apart from its tail. The tool says so rather than guessing
between "clean" and "false".

The firmware's set→go delay was raised from 1–2 s to **2.2–3 s** to match. With
a 1 s blanking period, a 1–2 s gap left almost nothing judged, and the go signal
could fire while the athlete was still settling. **These two numbers are a
pair** — changing one without the other quietly shrinks the judged window.

---

## 6. What the AIC picker actually does

This is the one genuinely new piece of method, so it is worth explaining
properly.

**The question it answers:** given a stretch of recording that we know contains
the start of a movement, which exact sample is the boundary between "nothing was
happening" and "something is happening"?

**How it thinks about it.** Suppose you take that stretch and cut it at some
point. You now have two pieces. If you cut in the right place, the first piece
is all quiet and the second is all movement — two pieces that are each
internally consistent, and very different from each other. If you cut in the
wrong place, at least one piece is a mixture of quiet and movement, and looks
messy.

So the method tries every possible cut point and scores each one by how
well-described the two pieces are. The winning cut is the onset.

The scoring uses each piece's **variance** — how much the values spread out. A
quiet piece has small variance, a moving piece has large variance, and a mixed
piece has large variance made worse by containing two different behaviours. The
formula adds up a "cost" for each side, weighted by how many samples are in it,
and the best cut is the one with the lowest total cost. (The name comes from the
Akaike Information Criterion, a general statistical tool for choosing between
competing descriptions of data. Here it is just: which cut explains this stretch
best?)

**Why this beats a threshold:** it never asks "is the signal above some value".
It asks "where does the character of the signal change" — and it uses every
sample in the window to answer, instead of the single sample that happened to
cross a line. There is nothing to tune, which is why the answer stopped
depending on tuning.

**When it does not work, and what happens then.** The method assumes the window
really does contain a quiet part followed by an active part. If an event fires
in the *middle* of movement already under way — a second push, a stumble —
there is no such boundary, and the method will still dutifully report the
biggest change it can find, which is meaningless.

This was caught on a real recording, where a third event 200 ms into an ongoing
400 mg movement was dragged 83 ms backwards. There is now a check: the start of
the window must be clearly quieter than the end of it, or the picker is not
trusted and the tool falls back to the threshold estimate, saying so explicitly
(`AIC: no clear onset, kept the threshold`).

That is deliberately conservative. Falling back to a number with a known bias is
better than silently substituting one that is simply wrong.

The picker can be switched off entirely — a checkbox in the interface, or
`--no-aic` on the command line — which is how the comparison tables above were
produced. In the chart, the reported onset is a solid line and the old threshold
crossing is a dotted one, so the correction is visible rather than implied.

---

## 7. Delays, and how big each one is

Every number below either has been measured on this hardware or is flagged as
not yet measured. They fall into two very different groups.

### Random errors — these set the precision

| Source | Size | Notes |
|---|---|---|
| Clock resolution | 8 µs | Measured at boot, reported in every recording |
| Sample timing | ~334 µs (1σ) | When each sample really happened vs its timestamp |
| Picker resolution | ~1.2 ms | About one sample at 865 Hz |
| **Total** | **~1–2 ms** | |

### Systematic errors — these shift every result the same way

| Source | Size | Can it be removed? |
|---|---|---|
| Buzzer acoustic latency | **5–20 ms** | **Yes, but not yet done** |
| Threshold bias, picker off | 5–15 ms | Yes — that is what the picker does |
| Threshold bias, picker on | unknown | Needs an independent reference |

**The buzzer is now the largest known error in the system.** The go signal is
timestamped when the firmware drives the buzzer, but sound does not leave the
device instantly — the transducer takes a few milliseconds to start producing
pressure. Every reaction time is therefore too long by that amount.

The good news is that it is a constant. Measure it once — a microphone next to
the buzzer and the firmware's own output pin, recorded on the same time base —
and subtract it from then on. It only needs redoing if the buzzer changes.

Until that measurement exists, **treat absolute reaction times as carrying a
5–20 ms offset in a known direction**. Comparisons between athletes on the same
device are unaffected, because the offset is identical for everyone.

### What was fixed earlier this session

For context, the biggest delay in the system until recently was not in this
list. The go reference used to be a human pressing a key while saying "go" out
loud. On one recording that key press landed **90 ms after** the movement it was
supposed to mark. The firmware now generates the go signal itself and timestamps
it on the same clock as the accelerometer samples, so that error is gone
entirely — replaced by the buzzer's 5–20 ms, which at least is constant.

---

## 8. Limitations — what this does not yet establish

Stated plainly, because they matter more than the results above.

**The thresholds have never been tuned on real block data.** Every number in the
tool is a reasonable starting value. They have been tested against a sensor on a
desk, moved by hand. That validates the *mechanism* — that the timing is sound,
that the markers line up, that the detector fires where it should — but says
nothing about an actual athlete on an actual block.

**Stable is not the same as correct.** The picker's headline result is that its
answer stops depending on how the tool is tuned. That it lands on the *true*
onset is a separate claim, and proving it needs an independent reference — a
high-speed camera, or a force plate. Until then we know the measurement is
reproducible, not that it is accurate.

**Margins are thinner than they look.** On the cleanest recording, the signal
during the judged window peaked at 12.3 mg against a 20 mg threshold — only 1.6
times the margin. That was a still sensor on a desk. An athlete holding the set
position, supporting their own weight, on a track with people around, will
produce more. Whether the margin survives is unknown and is the first thing a
block session should measure.

**The picker is not usable for a live alarm as-is.** It reads about 50 ms of
data *after* the trigger, so it cannot report before then. That is harmless for
recording a reaction time after the fact. For a device that lights a lamp the
instant a false start happens, it would add 50 ms of delay to the alarm — not to
the recorded time, but to the reaction of the machine.

**The hardware side of the start sequence is untested.** The button and buzzer
code compiles and the sequence runs correctly when triggered over the serial
port, but neither component has been physically connected yet.

---

## 9. Summary of what changed and why

| Change | Reason | Evidence |
|---|---|---|
| Decide on horizontal acceleration, gravity removed as a vector | Total magnitude hides horizontal movement; axis-dropping assumes flat mounting | Lateral movement under-reported 4.2× in the sensor evaluation |
| Add an onset picker after STA/LTA | Threshold crossing makes the answer depend on an arbitrary parameter, with a bias that varies per athlete | Reported time moved 29 ms across plausible thresholds; 0.0 ms with the picker |
| Do not judge the first second after "set" | The set position is entered by moving; the old rule flagged it every time | The rise measures several hundred mg, lasting ~1 s |
| Re-arm and report multiple events | One event per recording lets a twitch hide the real push-off | Kept from `sta_lta_start_detector.py`, which had this right |
| Report `NOT JUDGEABLE` | An attempt where the athlete never settled cannot be honestly called clean or false | |

Three recordings from 2026-09-08 gave 302.6 ms, 270.6 ms and 329.0 ms, with a
still-sensor control correctly reporting no movement at all.

The mechanism works. The numbers on real athletes are still ahead of us.
