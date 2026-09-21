"""Needs the heli-fdm-tool package (Sim/heli-fdm-tool) on the path and its Python environment; edit the two paths at the top.
A pilot's view: hover at altitude, lower the collective to sink, then pull collective. Vortex ring on (strength 12) and off (0)."""
import copy, sys
from pathlib import Path
import numpy as np, yaml
sys.path.insert(0, r"C:\Users\pcpde\Documents\Sim\heli-fdm-tool")
from helifdm.runner import Env, prepare_root, run_many
from helifdm.scenario import build_script
spec = yaml.safe_load(open(r"C:\Users\pcpde\Documents\Sim\heli-fdm-tool\specs\mi17.yaml"))
env = Env(Path(spec["paths"]["jsbsim_exe"]), Path(spec["paths"]["data_root"]), Path(sys.argv[1]), Path(sys.argv[2]))
prepare_root(env)
R = "propulsion/engine[0]/"
props = ["velocities/h-dot-fps", "velocities/vc-kts", "position/h-agl-ft", R + "vortex-ring-depth", R + "vortex-ring-descent-ratio", "attitude/roll-rad", "attitude/pitch-rad"]
scripts = {}
for strength in (0.0, 1.0, 2.0, 3.0, 4.0, 6.0):
    d = copy.deepcopy(spec)
    d["scenario"]["collective"]["schedule"] = [{"t": 60, "value": 0.30, "tc": 5.0}, {"t": 125, "value": 0.16, "tc": 4.0}, {"t": 135, "value": 0.03, "tc": 3.0}, {"t": 150, "value": 0.30, "tc": 3.0}]
    d["scenario"]["events"] = [{"t": 30, "set": [{"name": R + "vortex-ring-strength", "value": strength}]}]
    d["output_rate"] = 10
    scripts[f"s{strength}"] = build_script(d, f"s{strength}", props, 175)
runs = run_many(env, scripts)
for strength in (0.0, 1.0, 2.0, 3.0, 4.0, 6.0):
    r = runs[f"s{strength}"]
    print(f"strength {strength:4.1f}:")
    for a, b, label in ((132, 135, "near hover"), (140, 149, "sinking, collective 0.03"), (150, 156, "collective raised to 0.30"), (160, 170, "later")):
        m = (r.t >= a) & (r.t < b)
        print(f"   {label:<28} t {a:3d}-{b:3d}: climb {r.column('velocities/h-dot-fps')[m].mean():6.1f} ft/s, speed {r.column('velocities/vc-kts')[m].mean():4.1f} kt, "
              f"depth {r.column(R + 'vortex-ring-depth')[m].mean():.2f}, roll +/-{np.degrees(np.abs(r.column('attitude/roll-rad')[m]).max()):4.1f} deg")
    hh = r.column("position/h-agl-ft"); w = (r.t >= 135) & (r.t <= 172)
    print(f"   height at 135 s {hh[(r.t >= 135)][0]:.0f} ft, lowest afterwards {hh[w].min():.0f} ft, fastest sink {r.column('velocities/h-dot-fps')[w].min():.0f} ft/s")
    m = (r.t >= 150) & (r.t <= 172)
    h = r.column('velocities/h-dot-fps')[m]
    t0 = r.t[m][np.argmax(h > 0)] - 150 if (h > 0).any() else float('nan')
    print(f"   after the pull: lowest climb rate {h.min():.1f} ft/s, time until it climbs {t0:.1f} s, height {r.column('position/h-agl-ft')[m][-1]:.0f} ft")
