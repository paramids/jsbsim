"""Liftoff collective from a slow collective ramp (rigged mode as the game runs it): free air, the old (classic) ground effect, the new one."""
import copy
import re
import shutil
import sys
from pathlib import Path

import numpy as np
import yaml

sys.path.insert(0, r"C:\Users\pcpde\Documents\Sim\heli-fdm-tool")
from helifdm.runner import Env, prepare_root, run_many
from helifdm.scenario import build_script

spec = yaml.safe_load(open(r"C:\Users\pcpde\Documents\Sim\heli-fdm-tool\specs\mi17.yaml"))
HIP = Path(spec["paths"]["aircraft_dir"])
WORK = Path(sys.argv[1]).resolve()
R = "propulsion/engine[0]/"


def variant(name, exp=None, shift=None, target=False):
    root = WORK / "models" / name
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    model = root / HIP.name
    shutil.copytree(HIP, model)
    f = model / "Engines" / "mi17_main_rotor.xml"
    text = open(f, newline="").read()
    text = re.sub(r"<groundeffectexp>.*?</groundeffectexp>", "", text)
    text = re.sub(r"<groundeffectshift[^>]*>.*?</groundeffectshift>", "", text)
    text = re.sub(r"<groundeffecttarget>.*?</groundeffecttarget>", "", text)
    if exp is not None:
        extra = f"<groundeffectexp> {exp} </groundeffectexp>\n  <groundeffectshift unit=\"FT\"> {shift} </groundeffectshift>\n"
        if target:
            extra += "  <groundeffecttarget> 1 </groundeffecttarget>\n"
        text = text.replace("<vortexring>", extra + "  <vortexring>", 1)
    open(f, "w", newline="").write(text)
    return model


props = ["position/h-agl-ft", "velocities/h-dot-fps", "fcs/collective-cmd-norm", "propulsion/gearbox[0]/shaft-rpm", R + "thrust-lbs"]
cases = {"free air": variant("free"), "old ground effect (classic 0.15/12)": variant("old", 0.15, 12, False), "new ground effect (target 0.075/9)": variant("new", 0.075, 9, True)}
for label, model in cases.items():
    env = Env(Path(spec["paths"]["jsbsim_exe"]).resolve(), Path(spec["paths"]["data_root"]).resolve(), model, WORK / ("run_" + label.split()[0]))
    prepare_root(env)
    scripts = {}
    for start_rate, tag in ((80.0, "slow"),):
        d = copy.deepcopy(spec)
        d["scenario"]["always"] = []
        d["scenario"]["step_time"] = 10_000
        d["scenario"]["collective"]["schedule"] = [{"t": 62, "value": 0.20, "tc": start_rate}]      # 0 -> 0.20 over 80 s: 0.0025 per second
        d["output_rate"] = 10
        scripts[tag] = build_script(d, tag, props, 150)
    run = run_many(env, scripts)["slow"]
    t = run.t
    agl = run.column("position/h-agl-ft")
    vs = run.column("velocities/h-dot-fps")
    col = run.column("fcs/collective-cmd-norm")
    rpm = run.column("propulsion/gearbox[0]/shaft-rpm") / 192.0
    thrust = run.column(R + "thrust-lbs")
    rest = np.median(agl[(t > 50) & (t < 60)])
    up = np.flatnonzero((t > 62) & (agl > rest + 0.4))
    if len(up):
        i = up[0]
        print(f"{label:38s} lifts off at collective {col[i]:.3f} (t {t[i]:.0f} s, rotor {rpm[i]:.3f}, thrust {thrust[i]:.0f} lb), rest height {rest:.1f} ft")
    else:
        print(f"{label:38s} no liftoff by the end of the ramp (collective {col[-1]:.3f}); rest height {rest:.1f} ft")
    for c in (0.05, 0.08, 0.10, 0.12, 0.14):
        j = np.searchsorted(col, c)
        if j < len(t):
            print(f"      collective {c:.2f}: height {agl[j]:5.1f} ft, climb {vs[j]:+5.2f} ft/s, thrust {thrust[j]:6.0f} lb")
