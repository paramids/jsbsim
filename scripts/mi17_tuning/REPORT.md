# Mi-17 hover yaw and stability: findings and fix

Date: 2026-09-19. Model: `aircraft/mi17`, branch `mi17-gearbox-config`, commit `08477e5a`.
Written after integrating the model into the Hip Unreal Engine project and investigating a violent hover yaw.
Everything below was measured in standalone JSBSim; where something is an inference, it says so.

## 1. Summary

**The hover yaw (about -4.5 rad/s) and the model's apparent need for a spin were caused by sign errors in the
pitch channel of the autopilot and by a positive-feedback yaw loop. With five small edits the Mi-17 hovers with
about 0.005 rad/s of yaw, holds attitude, and tracks pitch and roll commands.**

1. The game reproduces the model exactly: same -4.2 to -4.5 rad/s as standalone JSBSim and as the model's own
   hover schedule (the comments in `mi17.xml` and `rotor_control.xml` already record it as unresolved).
2. The model's scripts pass because they check only altitude and attitude. In `test_mi17_hover.xml`'s schedule the
   helicopter holds 29.5 ft while spinning at -4.5 rad/s. Nothing asserts on yaw rate.
3. **The pitch attitude hold and the pitch rate damper have the wrong sign** (`afcs.xml`: `ap/pitch-gain = +0.5`,
   pitch SAS `-0.1`). Positive elevator command pitches the nose **down** in this model, so both were positive
   feedback. The fast yaw spin masked this. When the yaw is small the pitch runs away to about 0.64 rad.
4. **The yaw stabiliser has the wrong sign** (`afcs.xml:342`, `-0.05`) and the anti-torque feedforward is set at
   the tail rotor clamp. Together they latched the tail rotor at 0.358 rad and the yaw at -4.5 rad/s.
5. The roll channel signs are correct (flipping them crashes before liftoff).
6. An earlier conclusion of mine that "cyclic has no authority in the low-yaw state" was wrong: it was the sign
   error in item 3 (section 5).

7. Added later (2026-09-20): the rotor rpm governor was tuned (gains and a collective feedforward; rotor within
   about +-4% instead of +9 / -8% on collective steps, section 6.2), and the usable collective range was measured:
   near the ground the two engines carry about 0.6 collective at 20,444 lb, beyond that the rotor droops
   (section 6.3).

The changes are applied to the Hip project's copy of the model and available as patches for this repo
(`mi17_pitch_yaw_fix.patch` and `mi17_governor_fix.patch`, both apply cleanly to `08477e5a`). Verification is in
section 6.

## 2. Symptoms

In the game the Mi-17 started, lifted off at collective 0.12 to 0.19, yawed up to -4.7 rad/s (about 270 deg/s) and
lost control. Standalone with the model's defaults: yaw -4.2 rad/s at collective 0.00 with the tail rotor pinned at
its 0.358 rad ceiling.

## 3. Method

* Built a standalone `JSBSim.exe` from this repo (section 9).
* Game-like hovers: engines started with `starter-cmd` / `cutoff-cmd`, attitude hold on, the bias and collective
  schedule of `test_mi17_hover.xml` (bias 0, ramp to 0.15 at t=120 s, collective 0.092, altitude hold at +30 ft),
  metrics over t = 190..240 s.
* Each trial edits a **private copy** of `aircraft/mi17` (`run_trial.sh`); nothing in `aircraft/` was modified
  by the harness.
* Ground runs are confounded by wheel friction; hover runs have altitude hold on unless stated.

## 4. Findings

### 4.1 The raw airframe needs attitude hold
Every script sets `ap/attitude-hold-on = 1.0`. Without it (SAS only) roll diverges to more than 1 rad in about
55 to 60 s. Attitude hold is a proportional pitch and roll controller (`afcs.xml:91-130`); there is no yaw hold.
Heading hold (`ap/heading-hold-on`) tried on the unmodified model made things worse (yaw up to 18 rad/s).

### 4.2 Tail rotor authority is not the limit
Raising the tail rotor pitch ceiling (`rotor_control.xml:583`, default 0.358) makes yaw **worse**, proportional to
the ceiling:

| ceiling (rad) | 0.358 | 0.45 | 0.60 | 0.80 | 1.00 |
|---|---|---|---|---|---|
| mean yaw (rad/s) | -4.2 | -6.0 | -8.7 | -12.4 | -16.0 |

With the tail pitch pinned (clamp min = max) the tail rotor behaves sensibly. On the ground at default bias:

| pinned pitch (rad) | -0.128 | -0.05 | 0.0 | 0.05 | 0.10 | 0.20 | 0.30 | 0.358 |
|---|---|---|---|---|---|---|---|---|
| yaw (rad/s) | +1.31 | +0.40 | +0.14 | 0.00 | 0.00 | -0.73 | -2.96 | -4.2 |

In hover the slope is steeper, about -20 to -25 rad/s per rad, and balance is near 0.13 to 0.2 rad.

### 4.3 The yaw stabiliser is positive feedback
`afcs.xml:342`: `ap/sas-yaw-cmd = r * -0.05`. With the in-flight tail sensitivity above, the loop gain is about
+1.2, so the tail rotor latches at one of its two clamps. Evidence: on the unmodified model `fcs/adj/pedal-bias`
does nothing from 0 down to -0.35 (yaw -4.53, tail pitch 0.358), then at -0.5 jumps to tail pitch 0.009 with yaw
**+4.09**: two saturated states, nothing between. With the sign flipped the response becomes smooth and monotonic.

### 4.4 The feedforward is calibrated to the clamp
`rotor_control.xml:451`: `fcs/adj/pedal-torque-mix-gain = 0.0000174` with `pedal-col-mix-bias = -0.12` maps
27,500 ft-lb to the 0.358 rad ceiling (per the comment). Balance is at about 0.12 to 0.2 rad. Halving the gain
(0.0000087) puts the feedforward near balance.

### 4.5 The pitch channel sign error
**Cyclic step test** (`cyclic_step.sh`; hover, tail pinned so yaw is small or large; attitude hold off at t=200 s,
pitch cyclic step through the pilot channel, compared with a no-step control):

| | +step (cyclic +0.586 rad) | -step (cyclic -0.586 rad) |
|---|---|---|
| effect on pitch | nose **down**, about -0.45 rad in 0.5 s | nose **up** |

So positive cyclic is nose down, in both yaw states. In the low-yaw hover the attitude hold, seeing a nose-up error
of +0.64 rad, commanded **negative** cyclic (-0.33 rad), which pitches the nose further up. Releasing the attitude
hold made the pitch fall from 0.63 to -0.5 rad. Raising the attitude gain to 2, 8 or 40 saturated the cyclic at
-0.8 rad without reducing the pitch (0.62 to 0.68), which is what positive feedback looks like.

**Flipping the signs fixes it.** Tail pinned at 0.20 (yaw about -1.2 rad/s), hover, mean over t = 190..240 s:

| pitch attitude gain | pitch rate damper | max roll | max pitch | altitude |
|---|---|---|---|---|
| +0.5 (original) | -0.1 (original) | 0.34 | 0.66 | 20.7 ft (not held) |
| -0.5 | -0.1 | 0.67 | 1.08 | slow oscillation |
| -0.5, -1, -2 | +0.1, +0.3, +0.6 | **0.03 to 0.04** | **0.00 to 0.03** | 29.5 ft |

Flipping the **roll** attitude gain instead crashes the run before liftoff, so roll is correct as it was.
Flipping all three SAS signs together (pitch, roll, yaw) also crashed, because roll was wrongly included.

### 4.6 Tail rotor free, pitch channel fixed
With the pitch channel corrected, attitude is steady in every combination (roll 0.06 or less, pitch 0.03 or less)
and the yaw becomes a smooth function of the yaw feedback gain and the feedforward gain:

| yaw feedback \ feedforward | 0.0000174 | 0.0000120 | 0.0000087 |
|---|---|---|---|
| -0.05 (original sign) | -4.53 | -4.53 | -4.53 |
| +0.1 | -3.94 | -2.18 | -1.05 |
| +0.2 | -2.36 | -1.28 | -0.60 |
| +0.4 | -1.29 | -0.69 | -0.32 |
| +0.8 | -0.67 | -0.36 | -0.15 |

(mean yaw rate in rad/s). Adding a yaw-rate integrator (gain 0.2, clipped +-0.3 rad) at feedforward 0.0000087
brings the yaw to 0.004 to 0.005 rad/s regardless of the proportional gain (0.2, 0.4 or 0.8).

### 4.7 Other model defaults worth knowing
* `fcs/adj/collective-bias` default is 0.15 rad and alone gives about 19,000 to 20,000 lb of rotor thrust (the
  scripts zero it during ground settle for this reason, per their own comments). With the default, hover needs
  collective about 0.07 to 0.12.
* Electrical and hydraulic systems default to ON; nothing outside their own files depends on them.
* `ap/stability-aug-on` defaults to 1 and scales all three SAS channels.

## 5. Corrections to earlier conclusions

An earlier version of this report said the longitudinal cyclic "has essentially no authority over pitch" in the
low-yaw state, and listed flapping limits and hub geometry as candidates. That was wrong. The cyclic step test
(4.5) shows it has strong authority; the attitude hold was driving it the wrong way. The earlier grid results that
looked like a "chaotic" model (diverging roll and pitch, gain-insensitive pitch-up) were the same sign error.
A yaw-rate integrator tried before the pitch fix also failed for that reason.

## 6. The fix and its verification

Five edits (all in `mi17_pitch_yaw_fix.patch`):

| file | change |
|---|---|
| `afcs.xml:21` | `ap/pitch-gain` 0.5 to **-0.5** |
| `afcs.xml:322` | pitch SAS `-0.1` to **+0.1** |
| `afcs.xml:342` | yaw SAS `-0.05` to **+0.4** |
| `afcs.xml` (yaw SAS) | add a yaw-rate integrator (gain 0.2, clipped +-0.3 rad) summed into `ap/sas-yaw-cmd` |
| `rotor_control.xml:451` | `pedal-torque-mix-gain` 0.0000174 to **0.0000087** |
| `afcs.xml` (yaw SAS) | **yaw-rate command**: new `ap/yaw-rate-per-pedal` (1.5) and `ap/yaw-rate-error`, so the pedal sets a turn rate (section 6.1) |

**Game-like scenario** (model defaults for bias, engines started, attitude hold on, collective 0.092, no altitude
hold; pitch and roll target steps of 0.10 rad; pedal pulses of +-0.35):

| | original | fixed |
|---|---|---|
| hover yaw (mean) | -4.57 rad/s | **0.006 rad/s** |
| pitch step (target -0.10) | peak -0.025 | **-0.105** |
| roll step (target +0.10) | 0.108 | **0.110** |
| pedal pulses | still -4.6 rad/s | 0.15 rad/s peak, back to 0 |

**The author's own `test_mi17_hover.xml` (3,000 s) on the fixed model:** stable throughout, max roll 0.016 rad, max
pitch 0.034 rad, no divergence. The altitude hold settles at about 38.7 ft instead of 30 ft (original: 29.5 to
30.6 ft); not investigated.

### 6.1 Yaw-rate command (pedal turns the helicopter)
With the stabiliser holding the yaw rate at zero, the pedal only gave a brief nudge (+-0.35 pedal: at most
0.15 rad/s, then back to zero), so the aircraft could not be turned deliberately. Added: the stabiliser tracks a
requested rate instead of zero. New property `ap/yaw-rate-per-pedal` (default 1.5 rad/s per unit of pedal) and
`ap/yaw-rate-error = r + fcs/rudder-cmd-norm * ap/yaw-rate-per-pedal`, used by both the proportional term and the
integrator. A negative `fcs/rudder-cmd-norm` yaws right (the game's plugin negates its rudder input). Centred
pedal reduces to the plain yaw-rate hold, so scripts that never touch the pedal are unaffected.

Hover, collective 0.07, attitude hold on, pedal steps (mean yaw rate in rad/s after settling):

| pedal (fcs/rudder-cmd-norm) | commanded | achieved |
|---|---|---|
| -0.35 (right) | +0.525 | +0.625 |
| +0.35 (left) | -0.525 | -0.439 |
| -1.0 (full right) | +1.50 | +1.50 |
| released | 0 | 0.01 |

Roll reached at most 0.21 rad and pitch 0.09 rad during the full-pedal turn (tail rotor coupling). The 0.35 pedal
steps overshoot or undershoot by about 20% (the direct pedal path adds to the stabiliser); not tuned further.
Set `ap/yaw-rate-per-pedal` to 0 to return to a direct pedal.

### 6.2 Rotor rpm governor (added 2026-09-20)
Flying in the game the rotor rpm was not constant: it sagged when the collective was raised and overshot when it
was lowered. Measured with `governor_trial.sh` (collective steps 0.15 / 0.30 / 0.05 / 0.20, rotor rpm as shaft rpm
divided by 192, after the start-up phase):

| governor | steps up | step down (0.30 to 0.05) | start-up |
|---|---|---|---|
| model default: kp 2.5, ki 0.3, kd 0.1, no feedforward | dips to 0.91 | overshoots to 1.09 | peaks 0.99 |
| **fix: kp 4.0, ki 1.0, kd 0.3, collective feedforward 1.2** | dips to 0.96 | overshoots to 1.03 | peaks 1.00 |

The dip is engine lag: the collective raises the rotor torque at once, but the governor reacts only after the rpm
has fallen and the engine then needs a few seconds to spool up. The feedforward adds `1.2 x collective` to the
governor's throttle output (new property `governor/collective-feedforward`), so the throttle opens with the
collective. Gains alone helped little: raising ki and kp to 2 / 4 still dipped to 0.93 to 0.95, and a feedforward
of 2.0 overshot. The result is about +-4% instead of +9 / -8%. It behaves the same on the unpatched airframe.
`mi17_governor_fix.patch` (applies cleanly to `08477e5a`) makes the change. A step of the full collective in about
1 s still dips a few percent; the engine cannot spool up instantly.

N1 is not held constant, by design: the governor holds the rotor and lets the engine speed follow the load.
(N1 fell from 99% to about 76% to 85% just after start-up, when the governor closed the throttle from full.)

### 6.3 Collective range and available power (added 2026-09-20)
Symptom in the game: at 0.94 collective and 85 to 100 kt the rotor fell from 1.00 to 0.88 with the throttle at 1.00
and N1 at 100%, and the aircraft climbed at over 5,000 ft/min. Measured with `collective_power.sh` (from the
ground, collective stepped at t = 60 s, averaged over t = 64 to 76 s, mass 20,444 lb, the fixed governor):

| collective | rotor avg | rotor min | throttle | power per engine | climb (ft/s) |
|---|---|---|---|---|---|
| 0.10 | 1.00 | 0.99 | 0.75 | 896 hp | 5 |
| 0.30 | 1.01 | 1.00 | 0.86 | 1305 hp | 34 |
| 0.50 | 1.03 | 0.98 | 0.94 | 1641 hp | 50 |
| **0.60** | 1.03 | **0.95** | **0.99** | 1821 hp | 55 |
| 0.70 | 0.99 | 0.92 | 1.00 | 1792 hp | 56 |
| 0.80 | 0.93 | 0.88 | 1.00 | 1723 hp | 56 |
| 0.90 | 0.83 | 0.82 | 1.00 | 1526 hp | 52 |
| 1.00 | 0.68 | 0.67 | 1.00 | 1203 hp | 41 |

* **The limit is about 0.6 collective at this mass, near the ground.** That is where the throttle reaches its stop;
  beyond it the rotor cannot hold 1.00. Hover needs about 0.05 to 0.09.
* **Once the rotor droops the engines deliver less** (1203 hp at 1.0 collective), because the engine power table
  depends on rotor speed. A collective past the limit therefore gets worse quickly rather than levelling off.
* **The engines deliver their rated power.** `mi17_main_engine_1.xml` sets `maxpower` to 2200 hp per side (the
  TV3-117VM rating); the model produces about 1,820 hp each here, at 700 to 800 ft above the start (about 2,300 ft
  elevation) and 100% N1.
* **Altitude matters a lot.** A first, single staircase run climbed to about 10,000 ft above sea level and read only
  about 1,540 hp per engine (about 70% of 2,200, as the pressure there suggests) and a limit near 0.7. That was an
  altitude artefact. Use short runs from the ground, as `collective_power.sh` does.
* **Correction of an earlier remark:** I first said the model's climb rates needed more power than the engines have.
  That used the wrong engine power. With two engines near 1,800 hp, a 50 ft/s climb of 20,444 lb is physically
  plausible, so the rotor's collective-to-thrust calibration was left alone. Whether 50 to 55 ft/s (3,000 to 3,300
  ft/min) matches the real aircraft at this mass was not checked.
* The model's own `performance/power-available-hp` / `power-required-hp` properties report a shortfall even in a
  steady hover, so they were not used.
* **Hip's choice:** the game caps the pilot's collective at 0.6 (`MaxCollective`) rather than changing the model.
  Thinner air or a heavier load lowers the safe value; other masses and altitudes were not tested.

## 7. Remaining issues and limits

* **Yaw-rate command asymmetry.** Small pedal inputs give about +0.63 rad/s right and -0.44 rad/s left for the
  same magnitude (see 6.1). Acceptable for play, not tuned.
* **Altitude hold** in the original script settles about 9 ft higher with the fix (see above).
* **Governor and power limit (6.2, 6.3):** tested only at one mass (20,444 lb) and near the ground, with the collective
  stepped in the tens of seconds. Not tested: other masses, high altitude or temperature, a single engine out,
  forward flight at the power limit. The governor gains were found by a small search, not optimised.
* Only static hover, target steps and pedal pulses were tested. Not tested: forward flight, gusts, other weights
  or CG positions, autorotation, the other scripts in `scripts/test_mi17_*`.
* The values (yaw 0.4, integrator 0.2, feedforward 0.0000087, pitch -0.5 / +0.1) are a working point found by
  search, not an optimum. The yaw was insensitive to the proportional gain once the integrator was in.
* The tail rotor still sits about 60 in above the CG (`mi17.xml`), which couples tail thrust into roll; roll stayed
  within 0.04 rad in these tests.
* The model's comments should be updated: they document -5.2 to -6.7 rad/s as an unresolved "residual" and record
  experiments (raising the clamp, flipping the tail rotor `sense`) that were chasing this sign error.
* Suggested: add a yaw-rate assertion to the scripts (for example fail if |r| exceeds 0.3 rad/s in hover). They
  passed for a long time while spinning at 4.5 rad/s.

## 8. Integration notes (Hip Unreal project)

Separate problems, handled game-side, that affect how the model can be exercised:

* The UE movement component re-applies `EngineCommands[i]` (Starter, Running, CutOff, GeneratorPower, Throttle)
  every frame. Writing `starter-cmd` or the governor's `fcs/throttle-cmd-norm[n]` is overwritten. The game copies
  `governor/throttle-output` into `EngineCommands[0..1].Throttle` and commands starts through the same struct.
* FGTurboshaft starts with Starter + GeneratorPower on and fuel not cut off. Cutting fuel during the start resets
  it. N1 and N2 are not reported for turboshaft engines by the plugin; read `propulsion/engine[n]/n1`.
* The plugin's ground query returns AGL 0.0 when its ray hits nothing, which JSBSim treats as ground at the
  aircraft; flying off the edge of a finite floor drops the aircraft through the world.
* The game engages attitude hold and maps cyclic to `ap/pitch-target-rad` / `ap/roll-target-rad` (stick forward
  = nose down). With the fix these targets are now tracked correctly.

## 9. Reproducing

Build the standalone executable (Visual Studio; adjust the toolset to what is installed):

```
MSBuild.exe JSBSim.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /m
```

This produces `Release/JSBSim.exe` (ignored by `.gitignore`: `*.exe`). Trials, run from the repo root:

```
scripts/mi17_tuning/run_trial.sh baseline                         # unmodified model: yaw -4.53
AP_PITCH=-0.5 SAS_PITCH_VAL=0.1 YAW_K=0.4 YAW_KI=0.2 FF_GAIN=0.0000087 scripts/mi17_tuning/run_trial.sh fixed
PIN=0.20 scripts/mi17_tuning/cyclic_step.sh                       # cyclic step response
git apply scripts/mi17_tuning/mi17_pitch_yaw_fix.patch            # apply the fix to the repo model
git apply scripts/mi17_tuning/mi17_governor_fix.patch             # apply the governor tuning
KP=4 KI=1.0 KD=0.3 FF=1.2 scripts/mi17_tuning/governor_trial.sh gov   # governor: rotor rpm per collective step
scripts/mi17_tuning/collective_power.sh 0.6                       # power and rotor rpm at one collective value
```

`run_trial.sh` edits a private copy by line number and refuses to run if the expected text is not on those lines,
so it targets the **unpatched** model (commit `08477e5a`) and needs its line numbers updated after the patch is
applied.
