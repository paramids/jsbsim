"""Needs the heli-fdm-tool package (Sim/heli-fdm-tool) on the path and its Python environment; edit the two paths at the top.
Vortex ring checks on a model folder. Usage: vrs2.py <aircraft_dir> <work_dir>
1. thrust coefficient against sink rate at fixed collective (rising air through the rotor), for several strengths,
2. the same with forward speed (the effect must fade),
3. buffeting: thrust and rates in a deep vortex ring."""
import copy
import sys
from pathlib import Path

import numpy as np
import yaml

sys.path.insert(0, r"C:\Users\pcpde\Documents\Sim\heli-fdm-tool")
from helifdm.runner import Env, prepare_root, run_many
from helifdm.scenario import build_script

spec = yaml.safe_load(open(r"C:\Users\pcpde\Documents\Sim\heli-fdm-tool\specs\mi17.yaml"))
env = Env(Path(spec["paths"]["jsbsim_exe"]), Path(spec["paths"]["data_root"]), Path(sys.argv[1]), Path(sys.argv[2]))
prepare_root(env)
R = "propulsion/engine[0]/"
props = [R + "thrust-coefficient", R + "vortex-ring-depth", R + "vortex-ring-descent-ratio", R + "vortex-ring-inflow-scale", "velocities/p-rad_sec",
         "velocities/q-rad_sec", "velocities/h-dot-fps", "propulsion/gearbox[0]/shaft-rpm"]


def scenario(sink, strength, forward_fps=0.0, end=110):
    d = copy.deepcopy(spec)
    d["scenario"]["collective"]["schedule"] = [{"t": 60, "value": 0.16, "tc": 5.0}]
    events = [{"t": 30, "set": [{"name": R + "vortex-ring-strength", "value": strength}]},
              {"t": 100, "set": [{"name": "atmosphere/wind-down-fps", "value": -float(sink)}]}]
    if forward_fps:
        events[1]["set"].append({"name": "atmosphere/wind-north-fps", "value": -float(forward_fps)})   # headwind on the nose
    d["scenario"]["events"] = events
    d["output_rate"] = 20
    return build_script(d, "x", props, end)


sinks = [0, 10, 20, 30, 40, 50]
scripts = {}
for strength in (0.0, 1.0, 3.0, 5.0):
    for v in sinks:
        scripts[f"s{strength}_v{v}"] = scenario(v, strength)
for v in (20, 30):
    for fwd in (10, 20, 40, 60):
        scripts[f"fwd{fwd}_v{v}"] = scenario(v, 3.0, fwd)
runs = run_many(env, scripts)


def ct(run, a=100.5, b=102.5):
    m = (run.t >= a) & (run.t <= b)
    return float(run.column(R + "thrust-coefficient")[m].mean())


print("1. thrust coefficient / hover, rising air through the rotor (= sink rate), no forward speed:")
print("   sink ft/s      " + "  ".join(f"{v:5d}" for v in sinks))
for strength in (0.0, 1.0, 3.0, 5.0):
    base = ct(runs[f"s{strength}_v0"])
    print(f"   strength {strength:3.1f}   " + "  ".join(f"{ct(runs[f's{strength}_v{v}']) / base:5.2f}" for v in sinks))
print("2. strength 3, sink 20 and 30 ft/s with a headwind (forward speed):")
for v in (20, 30):
    base = ct(runs["s3.0_v0"])
    print(f"   sink {v}: " + "  ".join(f"{fwd:2d} ft/s ({fwd * 0.5925:3.0f} kt): {ct(runs[f'fwd{fwd}_v{v}']) / base:5.2f}" for fwd in (10, 20, 40, 60)))
r = runs["s3.0_v30"]
m = (r.t >= 101) & (r.t <= 108)
print("3. buffeting at sink 30 ft/s, strength 3 (t = 101..108 s): depth %.2f, descent ratio %.2f, inflow scale %.2f" % (
    r.column(R + "vortex-ring-depth")[m].mean(), r.column(R + "vortex-ring-descent-ratio")[m].mean(), r.column(R + "vortex-ring-inflow-scale")[m].mean()))
c = r.column(R + "thrust-coefficient")[m]
print("   CT swing (peak to peak) %.1f %% of mean | roll rate +/- %.3f rad/s, pitch rate +/- %.3f rad/s" % (
    100 * np.ptp(c) / c.mean(), np.abs(r.column("velocities/p-rad_sec")[m]).max(), np.abs(r.column("velocities/q-rad_sec")[m]).max()))
