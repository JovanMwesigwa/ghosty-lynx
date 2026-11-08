# Round 173d3c (REA): Maximus curve segment modes and Master Mid mode

> **Nature of this document.** Clean-room, behavioural reverse engineering of FL Studio's own Maximus engine
> (`Shared/SG_x64.dll`, sha256 `ad567e65e2aac8314d23f2322f4d17e1b5529a09f64c43d596189582f006f6ac`, the same file as
> rounds 35, 36 and 40). No Image-Line code was copied. Data layouts and behaviours are described in our own words.
> Addresses serve only to trace the evidence. Markers: **[DECOMP]** read in the decompiled or disassembled code,
> **[DATA]** read in FL's factory preset files, **[RENDER]** FL's own render (`FL64.exe /R`) compared with LYNX's
> (`LYNX.exe --render`) on the same project (§4). No EXEC harness was used.

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

## 4. Renders against FL [RENDER]

Project: FL's own `Data/System/Render tests/Maximus.flp` (a 3x Osc into Maximus), with the Maximus state replaced by
each preset's state (event 0xD5; the files are in `/mnt/e/lynx-work/audit173d3/maxr/`). FL rendered each project with
`/R /Ewav` through round 180d's `flrender.sh` (no input, minimised launch; FL never took the foreground, no new
input-wall drops). LYNX rendered it with `--render` from the cloud exe of `wip/audit173d3c` (run 42468972029).
Compared over the first 0.5 s, where both renders play the same notes. After about 0.6 s both renders part ways for every
project, the untouched one too: FL's render goes quiet while LYNX keeps playing its pattern. That is the project's
timing, not Maximus. Mono mix, aligned on the lag of least residual:

| Project | Lag (samples) | Residual vs FL | Worst 100 ms | RMS FL / LYNX | Envelope corr (20 ms) | Max 1/3-octave difference | FL preset vs FL original |
|---|---|---|---|---|---|---|---|
| original (v10 state, already supported) | 153 | −89.0 dB | −88.9 dB | −6.23 / −6.23 dB | 1.00000 | 0.002 dB | — |
| Destruction (v2 state, double curve) | 88 | −89.3 dB | −89.2 dB | −4.60 / −4.60 dB | 1.00000 | 0.000 dB | +4.9 dB |
| Nawak (hold segments) | 88 | −84.8 dB | −84.1 dB | −14.79 / −14.79 dB | 1.00000 | 0.001 dB | +1.6 dB |
| Mid-triggered gate (Master Mid mode) | 352 | −86.1 dB | −86.1 dB | −12.84 / −12.84 dB | 1.00000 | 0.002 dB | +2.1 dB |

The last column is the residual of FL's preset render against FL's render of the original project: each preset clearly
changes the sound, and LYNX follows it to the same depth as the already-exact original (about −85 to −89 dB, likely the
limit of FL's 16-bit file). Script: `maxr/cmp.py`; output: `maxr/result.txt`.
