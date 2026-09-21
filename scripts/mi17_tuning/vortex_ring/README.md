# Vortex ring state in FGRotor

`FGRotor` now models the vortex ring state (VRS) of a rotor in a power-on descent at low forward speed, with buffeting. It is
off unless the rotor file has a `<vortexring>` element, so existing aircraft are unchanged.

## Model (`FGRotor::calc_vortex_ring`, `calc_flow_and_thrust`, `CalcRotorState`)

* **Descent ratio** `x = Ww / v_h`. `Ww` is the axial relative air speed at the hub (positive when the air comes up through the
  disc, i.e. in a descent), `v_h = sqrt(W / (2 rho A))` the hover induced velocity from the aircraft weight.
* **Mean effect.** For `0 < x < 2` the empirical induced velocity (Johnson's fit)
  `v_i / v_h = 1 + 1.125 x - 1.372 x^2 + 1.718 x^3 - 0.655 x^4` is compared with momentum theory
  `v_i / v_h = x/2 + sqrt(x^2/4 + 1)`. The relative increase, times `<strength>`, scales the inflow **target** `c0` (not the
  recursion `nu = flow_scale * (...)`, which is applied on every step and would compound). Beyond `x = 1.8` it is tapered away.
* **Forward speed** removes the effect: it fades out between `<mu_start>` and `<mu_end>` (in-plane speed / tip speed).
* **Buffeting.** A depth `0..1` (a bump over about `0.15 < x < 1.6`, faded with forward speed) scales a fluctuating thrust
  (`<buffet_thrust>`, relative) and fluctuating flapping (`<buffet_flap>`, degrees, longitudinal and lateral), which rocks the
  aircraft through the hub moment and the thrust tilt. The signals are sums of sines with incommensurate frequencies around
  `<buffet_hz>`: irregular but repeatable.
* Properties (per engine index of the rotor): `vortex-ring-depth`, `vortex-ring-descent-ratio`, `vortex-ring-inflow-scale`
  (read only), `vortex-ring-strength`, `vortex-ring-buffet-thrust` (settable at run time).

## Mi-17 settings and what they do (`aircraft/mi17/Engines/mi17_main_rotor.xml`)

`strength` is a game-feel knob, not calibrated to flight test data. Hover induced velocity at gross weight is about 33 ft/s.

Thrust at fixed collective (0.16), air rising through the rotor at the sink rate (`thrust_vs_sink.py`), relative to hover:

| sink ft/s | 0 | 8 | 16 | 24 | 32 | 40 | 50 |
|---|---|---|---|---|---|---|---|
| strength 0 (smooth, as before) | 1.00 | 1.05 | 1.09 | 1.13 | 1.17 | 1.21 | 1.26 |
| strength 12 | 1.00 | 1.00 | 0.94 | 0.93 | 0.92 | 0.95 | 1.13 |
| strength 20 | 1.00 | 1.05 | 0.83 | 0.82 | 0.82 | 0.83 | 0.95 |

Forward speed restores the thrust: with a 40 ft/s headwind (24 kt) the loss is gone.

A descent from 590 ft at collective 0.03 (`descent_flight.py`), gross weight:

| strength | sink rate | lowest height after the pull |
|---|---|---|
| 0 | -24 ft/s | 240 ft |
| 2 | -31 | 115 ft |
| **3 (default)** | **-35** | **38 ft** |
| 4 | -38 | 5 ft |
| 6 and up | -44 to -50 (runaway) | on the ground |

Buffeting in the region (buffet_thrust 0.5, buffet_flap 10 degrees, 1.0 Hz; the first values 0.10 / 1 degree / 0.6 Hz were too weak in the game): the roll rate is about +/-29 deg/s, the pitch rate about +/-5.8 deg/s (the game adds a depth-scaled camera shake, since the assist hold masks part of it) (the pitch inertia is 4 times the roll inertia) and the vertical acceleration swings about 0.6 g. Measured at a 30 ft/s sink.

## Building

`JSBSim.vcxproj` (the test executable, `Release/JSBSim.exe`) and `JSBSimForUnreal.vcxproj` (the plugin's `JSBSim.dll`) ask for
toolsets v143 / v142; with only Visual Studio 2026 installed they were built with
`/p:PlatformToolset=v145 /p:WindowsTargetPlatformVersion=10.0.22621.0 /p:SolutionDir=<repo>\`.

## Known limits

* The mean effect is the average induced velocity, not the unsteady wake, so the effect is a smooth loss plus the synthetic
  buffeting; the onset and the depth of the real thing vary.
* The ground effect factor in this rotor is applied to `nu` on every step and compounds: with the Mi-17 numbers (inflow lag
  0.32 s, dt 0.0075 s) the steady inflow is about x0.23 at the rest height, x0.45 at 12 ft, x0.84 at 24 ft and x0.97 at 36 ft,
  much stronger near the ground than the factor itself suggests. Not changed here.
