# Behavioural analysis of FL Studio 2026: round 40 (REA) — Maximus, Soundgoodizer, Vocodex's SG stage and internal carrier

> **Nature of this document.** Continuation of `REA_rodada8.md` §1–2 (Maximus / Soundgoodizer state format),
> `REA_rodada36.md` §2 (the Maximus engine as hosted by `Shared/SG_x64.dll`, only what Harmless's presets use),
> `REA_rodada38.md` §6 (the RMS detector) and `REA_rodada35.md` §7–8 (Vocodex's Soundgoodizer stage and internal Sytrus
> carrier, both left undecoded). *Clean-room* reverse engineering, behavioural only. No Image-Line (nor Intel IPP) code
> or pseudocode was copied: what is here are data layouts, formulas and behaviours described in our own words.
> Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No FLEX pack was opened; no
> Image-Line preset or audio data was copied into the repository (Soundgoodizer's `a…d.dat`, Vocodex's `SG.dat` and the
> Vocodex synth presets are read at import time from the user's own installation).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst`/installation files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-14. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`
  (the work of this round lives under `r38/mx/`).
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Maximus/Maximus_x64.dll` | `acc7affbe4d070ce8b71c13de79f267dd1b257f4207a61a733388a6aec1abdc1` | the mixer Maximus |
  | `Shared/SG_x64.dll` | `ad567e65e2aac8314d23f2322f4d17e1b5529a09f64c43d596189582f006f6ac` (= rounds 35/36/38) | the shared engine build (Harmless/Harmor rack, Vocodex's SG stage) |
  | `Plugins/Fruity/Effects/Soundgoodizer/Soundgoodizer_x64.dll` | `fe3f0c135b1fda9ec3dd933d28300c9d98fd7ba7020c00ca8c6b271465692ac2` | Soundgoodizer: its own, older copy of the engine |
  | `Plugins/Fruity/Effects/Soundgoodizer/Data/Presets/a…d.dat` | `75dd159f…7efb`, `615e9600…9809`, `b86a8043…2605`, `36fb4bcc…3335` | the four presets A–D (Maximus states, version 4, 1333 bytes) |
  | `Plugins/Fruity/Effects/Vocodex/Data/SG.dat` | `f67dd6a1472bdca169208b9ee8dc7a77d6315faac3e59809f83244b55d89ee53` (= round 35) | Vocodex's Soundgoodizer state (Maximus state, version 7, 1342 bytes) |
  | `Plugins/Fruity/Effects/Vocodex/Synthesizer/Sytrus_x64.dll` | `46ea9f910dcc315b0bb8dd3b67dfc8562ff427fc4d291cc5c5c2de07e6f922eb` (= round 35) | Vocodex's internal carrier synth (EXEC only) |

- **EXEC harness:** `r38/mx/run/mx.c` → `mx_<n>.exe` (zig cc, x86_64-windows-gnu), round 35's Vocodex harness extended
  so it hosts any of the four DLLs above: it answers the plugin's data-file requests (dispatcher `0x47` → method 6,
  requests `0x3fb` relative to `Plugins\Fruity\Effects\` and `0x3fc` relative to the plugin folder), the plugin folder
  (`0x1d`), and — new in this round — **the host's file enumeration (method 2, request `0x3fb`)**, so that Vocodex's
  tone list is filled and its internal Sytrus is created (round 35 could not get past an empty list). Same safety
  measures as before (unmodified DLLs, `SetErrorMode`, fault handler, `TerminateProcess`, `timeout`).
- **Drivers (our own code):** `r38/mx/py/mx.py` (runs the harness; test signals `music`, `saw`, `burst`, `loud`,
  `quiet`; error measure), `survey.py` (extracts every Maximus / Soundgoodizer mixer state from the demo projects into
  `r38/mx/data/demo/`, 100 Maximus + 95 Soundgoodizer slots), `t_same.py` (Maximus_x64.dll vs SG_x64.dll on the same
  states), `t_lynx.py` (LYNX's engine vs Maximus_x64.dll), `t_sgz2.py` (LYNX vs Soundgoodizer_x64.dll, 4 presets × 4
  amounts), `sgzc.py` (a byte-identical copy of Soundgoodizer_x64.dll in a folder of our own, with our own states as
  its preset files), `t_vcxsg.py` (Vocodex's SG stage ≡ SG_x64.dll with SG.dat), `t_vcx.py` (LYNX's Vocodex effect vs
  Vocodex_x64.dll); C++ drivers `r38/mx/cpp/mxcmp.cpp` (LYNX's engine on a float file), `vcxcmp.cpp` (LYNX's Vocodex
  with the installation data appended as at import), `bench.cpp` (cost), `teststates.cpp` (the states of
  `tests/tests.cpp`).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels, 44.1 kHz, blocks of 512 unless stated;
"−∞" = bit-identical.

---

## 1. Which build runs which plugin **[DATA + EXEC]**

- **Maximus (mixer)** saves its full state (plugin record `0xD5`); in the demo projects the versions are 10 (33
  slots), 14 (9) and 15 (58). Maximus_x64.dll and the shared `SG_x64.dll` loaded with the same state give the same
  output (`t_same.py`), so LYNX treats them as one build ("shared").
- **Soundgoodizer** saves only 12 bytes: version (3), preset index 0…3 (A…D) and Amount 0…1000. At load it reads
  `Data\Presets\<a…d>.dat` — a Maximus state, version 4 — and sends Amount as **parameter 5** (band mix, round 36
  §2.1) **[DECOMP + EXEC]**. Demo usage: A 74, B 10, C 9, D 2 slots. Its engine is an **older copy** of the Maximus
  code compiled into `Soundgoodizer_x64.dll`; §3 lists where it differs.
- **Vocodex's SG stage** is `SG_x64.dll` (shared build) loaded with `Vocodex\Data\SG.dat`, its parameter 5 = Vocodex's
  SG knob (§4).

---

## 2. Shared engine: what this round added to round 36

Round 36 §2.9 left out everything Harmless's four presets do not use. The demo projects use more of it.

### 2.1 RMS detector (band parameter 12 ≠ 0) **[DECOMP + EXEC]**

Instead of the peak window: per sample `L² + R²`, summed over the last W samples (same W as the peak window) in an
extended-precision running sum that is carried between blocks as a double; the sum is scaled by `1/(2W)`, clamped
below at 0 and square-rooted (IPP threshold + sqrt, float). The release/attack stages that follow are unchanged.

### 2.2 Saturation mode B (band parameter 11 > 0) **[DECOMP + EXEC]**

With ceiling `c = L7(p12)` and threshold `t = (1 − thr/1000)·c` (float), range `r = c − t` (a tiny positive value
when r ≤ 0): samples with `|x| ≤ t` pass; above it `|y| = t + r·(1 − e^{(t − |x|)/r})`, sign of x kept (computed in
extended precision, rounded to float). The output approaches c smoothly. Mode A (thr < 0) is round 36 §2.6.

### 2.3 Older state layouts (versions 3…6) **[DECOMP]**

The loader copies a fixed block of the state raw into the plugin object; its length depends on the version: 0x1FA
bytes for v ≥ 7, 0x1F9 (v6), 0x1F5 (v5), 0x1F1 (v4), 0x1EE (v3). Fields the shorter block does not reach keep the
plugin's reset value (0) — for v4 this includes the oversampling word at 0x1F4. The envelope curves follow the block
as in round 36. Versions below 3 (a piecewise layout) occur in no preset or demo state and are rejected.

### 2.4 Numeric details that matter at −140 dB **[DECOMP of the x87 code + EXEC]**

- **Filter states between blocks:** the end-of-block clean-up of the biquad / all-pass states only zeroes values
  below 2⁻²⁴; the states stay in double (round 36 had them rounded to float — the decompiler shows a conversion the
  instructions do not make).
- **Control smoothers** (release, attack and release-2 one-poles): the state lives in x87 extended precision for the
  whole block, the coefficients are doubles, only the output samples are rounded to float; between blocks the state
  is kept as a double (0 below 2⁻²⁴). LYNX uses `long double` (80-bit on every x86-64 target it is built for).

### 2.5 Menu options stored in the state's UI block **[DECOMP + EXEC]**

- **Solo** (byte 0x1EF) with the editor's selected band (int 0x1E0): when solo is on and a band other than the master
  is selected, every other band 0…2 is muted (effective mode ≥ 2) and the master runs at least as "compression off"
  (effective mode ≥ 1).
- **Linear-phase filters (legacy mode)** (byte 0x1FD): the IIR crossover is replaced by three FIR filters run on the
  (low-cut, oversampled) input, one per band:
  - FIR length `N = round(0.004·band rate) | 1` (at least 5); each split's own windowed-sinc length
    `L = round(K·27 500·(slope + 1)/f) | 1` (at least 5, at most N; K = oversampling factor, f = split frequency).
  - Kernel: ideal low-pass sinc at the normalised frequency, times a 4-term Blackman–Harris window; a high-pass is the
    spectral inversion of the low-pass; the mid band is (low-pass at f_low + high-pass at f_high) inverted, i.e. a
    band-pass; when one outer band is off its split is dropped (mid = low-pass at f_high or high-pass at f_low). Each
    kernel is centred in the N-tap buffer and converted to float. The low and high bands use polarity +1 in this mode.
  - The FIRs add `(N − 1)/2` samples of delay, included in the reported latency.
- **Master Mid mode** (byte 0x1FC) and the v ≥ 14 **linear-phase band split** (flags bit 0, which hands the split to
  Frequency Splitter's `engine_x64.dll`) are **not decoded**; LYNX rejects such states (§6).
  *(Round 173d3c: Master Mid mode is now decoded, see `REA_rodada173d3c.md` §2.)*
- **v ≥ 14 curve-scale flag** (bit 6): round 36 §2.1; now exercised by the demo states.

### 2.6 High band as the first band on **[DECOMP + EXEC]**

The band outputs are accumulated into a sum buffer; the first band that is on *is* the sum buffer. When the low and
mid bands are both off, the high band is the first, and the final add of "band + sum" then **doubles** it. This is the
plugin's behaviour (EXEC: LYNX matches the DLL on such states only with the doubling) and LYNX reproduces it.

### 2.7 Latency **[DECOMP]**

Reported to the host: `max(L₀…₂)/K + L₃`; with the legacy FIRs `(max(L₀…₂) + (N − 1)/2)/K + L₃`. LYNX reports the same to its PDC.

---

## 3. Soundgoodizer_x64.dll's older build **[DECOMP + EXEC]**

Same structure as §2 with these differences:

1. **Loader:** it stops after the fixed block — states newer than version 13 (v14+ options) are not read by this
   build. The shipped presets are v4.
2. **Gain table end:** while building the drawn-curve table the saturated output is compared with the double 0.975
   (not the float), the stored end value is the last output before saturation, and the peak used for saturation A's
   top is divided by the float 0.975.
3. **Saturation A table:** 16-bit — each value is `round(v·32767)` clamped to ±32767 and read back × ceiling/32768.
4. **Crossover:** the low band has no phase-matching all-pass at the high split (only `sL + 1` low-pass stages).
5. **Legacy FIR mode:** the high split's length constant is 110 000 instead of 27 500.

Everything else (detectors, stages, oversampling, band mix, master) is shared.

---

## 4. Vocodex's Soundgoodizer stage **[DECOMP + EXEC]**

- The stage is the shared engine loaded with `Vocodex\Data\SG.dat`, parameter 5 = the SG knob (0 skips the stage), run
  in place on Vocodex's output by the sub-plugin's Eff_Render. `t_vcxsg.py`: Vocodex_x64.dll with SG as stored is
  **bit-identical** to (Vocodex with SG = 0) → SG_x64.dll with SG.dat and parameter 5 = SG, on two demo instances
  (SG = 226 and 546). With SG_x64.dll's default state instead of SG.dat the difference is +3.7…+4.2 dB, so the data file
  is required.
- The latency of the stage adds to Vocodex's own.

---

## 5. Vocodex's internal carrier **[DECOMP + EXEC]**

- With the harness answering the file enumeration, Vocodex lists the 25 presets of
  `Synthesizer\Data\Presets\*.fst` and creates its own Sytrus. The carrier is that Sytrus with the tone's preset (a
  standard FL preset stream for plugin `"Sytrus"`), one voice per latched key: Init/Final pan 0, volume 0.5,
  `Final.Pitch = 100·key − 6000` cents (i.e. the key itself), cut/res 0; the host answers the voice's velocity query
  with 0.78125 and its release velocity with 1.0. The keys stay held for the whole render.
- LYNX runs this on its own exact Sytrus engine (round 31) with the preset read from the user's installation at
  import. Ookay / NewStuff (tone "PWM", keys 51, 63, 66, 73) now vocode against the synth instead of passing the
  modulator through.

---

## 6. What LYNX does with it

- **`EffectKind::maximus`** (`core/MaximusFx`): a mixer effect whose slot state is a small container of our own
  (`"LXMX"`, version, build, band mix, then the Maximus state bytes). A **Maximus** slot keeps FL's own saved state;
  a **Soundgoodizer** slot keeps the preset file read from the user's installation at import plus Amount as the band
  mix, and runs on the Soundgoodizer build. Host blocks longer than 512 frames are split into the engine's calls.
- **Fallbacks:** states the engine does not support (§2.5: Master Mid mode, the v14+ linear-phase split, oversampling
  above ×2), or a Soundgoodizer whose preset file cannot be read, keep the previous approximation; the import report
  lists them.
- **Vocodex:** at import the SG.dat bytes and the carrier's Sytrus state are appended to FL's raw Vocodex state (a
  tagged trailer; FL's bytes are untouched), so a saved `.lynx` project needs no installation to play.

---

## 7. Verification **[EXEC]**

| Check | Cases | Result |
|---|---|---|
| Maximus, every demo state (`t_lynx.py`, music signal) | 100 states | 96 within −139.4 dB (53 bit-identical, median −∞); 4 rejected (all four use the v15 linear-phase split, §2.5) |
| Legacy FIR mode (demo states with byte 0x1FD set) | 2 states × music / noise bursts | −137.3…−139.1 dB |
| Soundgoodizer (`t_sgz2.py`), music, 44.1 kHz / 512 | presets A–D × Amount 0, 128, 600, 1000 | −128.1…−∞ dB (A −128…−137, B −133…−146, C −143…−∞, D −146…−∞) |
| Soundgoodizer, noise bursts, 48 kHz / blocks of 300 | the same 16 cases | −125.9…−∞ dB |
| Vocodex's SG stage ≡ SG_x64.dll + SG.dat (`t_vcxsg.py`) | 2 demo instances | bit-identical |
| LYNX's Vocodex effect vs Vocodex_x64.dll (`t_vcx.py`) | all 10 demo instances, SG stage on | −136.0…−144.9 dB; the two internal-carrier instances (Ookay, NewStuff) −111.8 dB |
| Unit tests (`tests/tests.cpp` `maximus_engine`) | own states vs SG_x64.dll / a Soundgoodizer DLL copy: RMS, saturation B, low cut, solo, legacy FIR, v4 layout on the Soundgoodizer build; containers | goldens within 2·10⁻⁶ |

The −111.8 dB of the internal-carrier instances is the error of LYNX's Sytrus engine on that preset, not of Vocodex
or the SG stage (the external-carrier instances sit at −136 dB and below).

**Cost** (one core, 44.1 kHz, 20 s of noise, `bench.cpp`): a demo Maximus state 1.3 % of real time, the same with the
legacy FIRs 3.4 %, Soundgoodizer preset A 1.9 %.

---

## 8. Open items

- Master Mid mode (UI byte 0x1FC) and the v14+ linear-phase band split through `engine_x64.dll` (4 of the 100 demo
  Maximus states) — not decoded, approximated.
- Oversampling orders above ×2 (no preset or demo state uses them).
- Envelope segment types other than 0 and 7 in the drawn curve (none in the demo states).
- The 512-frame split of long host blocks follows FL's usual block size; FL's own behaviour with longer blocks was not
  observed.
