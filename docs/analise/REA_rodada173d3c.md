# Round 173d3c (REA): Maximus curve segment modes and Master Mid mode

> **Nature of this document.** Clean-room, behavioural reverse engineering of FL Studio's own Maximus engine
> (`Shared/SG_x64.dll`, sha256 `ad567e65e2aac8314d23f2322f4d17e1b5529a09f64c43d596189582f006f6ac`, the same file as
> rounds 35, 36 and 40). No Image-Line code was copied. Data layouts and behaviours are described in our own words.
> Addresses serve only to trace the evidence. Markers: **[DECOMP]** read in the decompiled or disassembled code,
> **[DATA]** read in FL's factory preset files. Nothing here was checked by running the DLL (**no [EXEC]**): the
> round-36/40 harness was not available. The resulting sound is **not verified against FL**.

Why: 3 of FL's 41 Maximus factory presets still fell back to a generic effect after round 173d3b ("Fancy/Destruction",
"Fancy/Nawak", "Fancy/Mid-triggered gate").

## 1. Curve segment modes [DECOMP `FUN_007885d0`; DATA]

- "Destruction" uses segment mode 1 with tension 0.385 on band 0's curve. "Nawak" uses mode 2 on the curves of bands
  0 and 1 **[DATA]**.
- The Maximus curve evaluator steps through the points in the same way for every mode. Only the shape of the phase
  `s` (0…1) inside a segment depends on the mode. The per-mode shapes are the same as FL's shared envelope editor
  that Fruity WaveShaper uses (REA_rodada51 §2.4):
  - mode 1 (double curve): the mode-0 exponential curve on each half, the second half mirrored:
    `s < ½ → c(2s)/2`, otherwise `1 − c(2(1 − s))/2`. `c` uses the same per-point constants as mode 0. A point without
    tension is marked straight, so the segment is linear.
  - mode 2 (hold): the shape is 0, so the segment keeps the previous point's level until the next point. Mode 2 is not
    in the "straight without tension" mask (modes 0, 1, 7, 8, 9, 11, 12).
- LYNX models modes 0, 1, 2 and 7. Other modes still make the preset fall back. No factory preset uses them.

## 2. Master Mid mode [DECOMP `FUN_0083ef50`, `FUN_008417e0`, `FUN_00843a40`]

- Stored in state byte `0x1FC` (object `+0x324`). Versions below 6 reset it to 0 when loading (`FUN_0083ef50`).
- Effective band modes (`FUN_008417e0`): with the flag set, bands 0 (low) and 2 (high) get mode 2 (muted). This uses
  the mask byte `0x05` at `0x8419E0`, and those two bands skip the solo rule. Bands 1 (mid) and 3 (master) follow the
  usual rules.
- Block flow (`FUN_00843a40`):
  - Crossover: normally the mid band's first high-pass stages run in place on the split signal, so the high band sees
    them. With the flag set, the mid band writes every stage to its own buffer and the split signal stays untouched.
  - The mid band's detector, release stages, static curve and attack stages run on the mid-filtered signal, after the
    band's stereo matrix.
  - Then, for the mid band only, the buffer the gain acts on switches to the full split signal. The band's delay,
    gain (or post level), saturation and mix are applied to that signal in place, and it is what the sum receives.
- Result: the mid band's level drives a gain on the whole signal (a "mid-triggered" compressor or gate). The low and
  high bands are silent, and the master band follows as usual.

## 3. LYNX

- `core/HarmlessFx.cpp`: `Curve` gains modes 1 and 2, and `MaximusPreset::midMode`. The engine leaves the split signal
  alone and applies the mid band's processing to it. The low and high bands are muted.
- Tests: `tests/Maximus173d3cTests.inc` covers acceptance, hold silencing a quiet tone, a straight double curve
  matching a straight segment, Mid mode muting the low band, and FL's three presets (with the FL Data path).
- Open: these renders have no comparison against the DLL. An EXEC run of the three presets would confirm the sound.
