# Ground effect in FGRotor

`FGRotor` reduces the induced inflow near the ground with a factor `ge(h) = 1 - exp(-(h + shift) * exp) * (rpm / nominal) * scale`
(clamped to 0.5..1.0), `h` being the height of the CG above the ground, `scale` the property
`propulsion/engine/groundeffect-scale-norm` (the Mi-17 fades it with airspeed in `mi17.xml`, 1.0 in a hover, 0.08 at 30 kt).

## The classic form compounds

The factor was applied to the inflow on every step: `nu = ge * ((nu - c0) * exp(-dt / lag) + c0)`. Every step multiplies the inflow by
`ge` again, so the steady inflow is `ge * (1 - e) / (1 - ge * e)` times the free-air value with `e = exp(-dt / lag)`. For the Mi-17 (lag
0.32 s, dt 0.0075 s, `e = 0.977`) a factor of 0.94 gave x0.51 at 11 ft and x0.86 at 22 ft (measured with `ground_effect_trial.py`, inflow
against the free-air inflow for the same thrust), and the result changes with the time step. That is why the ground effect felt too strong
and reached too high.

## `<groundeffecttarget>1</groundeffecttarget>`

With this element the factor is applied to the inflow target once: `c0 *= ge^2; nu = (nu - c0) * exp(-dt / lag) + c0`. In a hover
`nu` follows `sqrt(c0)`, so squaring makes the steady inflow `ge` times the free-air value at fixed thrust. Measured on the ground
(no forward speed) the inflow ratio is 0.69 with a factor of 0.69. It does not depend on the time step or the inflow lag. Without the
element nothing changes (AH-1S and the test aircraft keep their tuning).

## Mi-17 values

`groundeffectexp` 0.075, `groundeffectshift` 9 ft (were 0.15 and 12 ft in the classic form). Fit to the standard rotor ground effect curve
(Cheeseman and Bennett, induced velocity ratio `1 - (R / 4z)^2`, z the hub height, about 9 ft above the CG):

| CG height ft | 6 (rest) | 12 | 25 | 40 | 70 |
|---|---|---|---|---|---|
| ge | 0.69 | 0.82 | 0.93 | 0.97 | 0.99 |

Liftoff collective from a slow collective ramp with the rotor at full speed (rigged mode, `ground_effect_liftoff.py`): 0.133 with the new values, 0.166 in free air and 0.068 with the old classic form. In the game (rotor 0.94-0.95 while pulling) liftoff was at 0.14-0.15. helifdm `check`, `calibrate`, `engines` and `rig`
are unchanged (19/19, 8/8, 10/10, 30/30).

`ground_effect_trial.py <workdir>` measures the equilibrium height and the inflow ratio for the classic and the new form (attitude hold
on, the speed fade of `mi17.xml` set to 1 in the trial copies).

## Deploy

The element needs a JSBSim.dll with this change (the older DLL ignores it and then applies 0.075 / 9 ft in the classic form, which is far
too strong). Deploy the DLL, `FGRotor.h` and the rotor file together.
