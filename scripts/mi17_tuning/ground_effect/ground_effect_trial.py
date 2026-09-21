"""Ground effect: hover equilibrium against height (attitude-hold mode keeps the aircraft upright).
The model's own speed fade of the ground effect is set to 1 so the static effect is measured.
Inflow ratio = vi / (free-air inflow for the same thrust), free-air inflow from vi = k * sqrt(thrust) (momentum theory)."""
import copy
import re
import shutil
import sys
from pathlib import Path

import numpy as np
import yaml

sys.path.insert(0, r"C:\Users\pcpde\Documents\Sim\heli-fdm-tool")
from helifdm.runner import Env, prepare_root, run_many
from helifdm.scenario import build_script, with_mode

spec = with_mode(yaml.safe_load(open(r"C:\Users\pcpde\Documents\Sim\heli-fdm-tool\specs\mi17.yaml")), "legacy")
HIP = Path(spec["paths"]["aircraft_dir"])
WORK = Path(sys.argv[1]).resolve()
R = "propulsion/engine[0]/"


def variant(name, exp=None, shift=None, target=False):
    """A copy of the Hip model with the given ground effect settings on the main rotor and no speed fade in mi17.xml."""
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
    if exp is not None:
        extra = f"<groundeffectexp> {exp} </groundeffectexp>\n  <groundeffectshift unit=\"FT\"> {shift} </groundeffectshift>\n"
        if target:
            extra += "  <groundeffecttarget> 1 </groundeffecttarget>\n"
        text = text.replace("<vortexring>", extra + "  <vortexring>", 1)
    open(f, "w", newline="").write(text)
    g = model / "mi17.xml"
    body = open(g, newline="").read()
    assert body.count("<value> -0.050 </value>") == 1
    open(g, "w", newline="").write(body.replace("<value> -0.050 </value>", "<value> 0.0 </value>"))
    return model


def measure(model, tag, collectives, end=110, window=(90, 110)):
    env = Env(Path(spec["paths"]["jsbsim_exe"]).resolve(), Path(spec["paths"]["data_root"]).resolve(), model, WORK / f"run_{tag}")
    prepare_root(env)
    props = ["position/h-agl-ft", R + "vi-fps", "velocities/h-dot-fps", R + "thrust-lbs", "velocities/vt-fps"]
    scripts = {}
    for i, c in enumerate(collectives):
        d = copy.deepcopy(spec)
        d["scenario"]["step_time"] = 10_000
        d["scenario"]["collective"]["schedule"] = [{"t": 60, "value": c, "tc": 5.0}]
        d["output_rate"] = 10
        scripts[f"c{i}"] = build_script(d, f"c{i}", props, end)
    runs = run_many(env, scripts)
    rows = []
    for i, c in enumerate(collectives):
        r = runs[f"c{i}"]
        m = (r.t >= window[0]) & (r.t <= window[1]) if "Time" in r.data else np.array([], dtype=bool)
        if not m.any():
            rows.append((c, None))
            continue
        rows.append((c, dict(agl=float(r.column("position/h-agl-ft")[m].mean()), vi=float(r.column(R + "vi-fps")[m].mean()),
                             vs=float(r.column("velocities/h-dot-fps")[m].mean()), spread=float(np.ptp(r.column("position/h-agl-ft")[m])),
                             thrust=float(r.column(R + "thrust-lbs")[m].mean()), vt=float(r.column("velocities/vt-fps")[m].mean()))))
    return rows


COLL = [0.05, 0.06, 0.08, 0.10, 0.12, 0.14]
free = measure(variant("free"), "free", [0.12, 0.14, 0.16])
for c, m in free:
    print("free air collective", c, m and {k: round(v, 2) for k, v in m.items()})
ref = [m for c, m in free if m][0]
K = ref["vi"] / np.sqrt(ref["thrust"])
print("free-air inflow constant vi/sqrt(thrust) = %.4f (from collective %.2f)" % (K, free[0][0]))

configs = {
    "old": dict(exp=0.15, shift=12, target=False),
    "new": dict(exp=0.075, shift=9, target=True),
}
for name, kw in configs.items():
    rows = measure(variant(name, **kw), name, COLL)
    print(f"\n{name} {kw}: collective | height ft | climb ft/s | thrust lb | speed ft/s | inflow ratio | formula ge(h)")
    for c, m in rows:
        if not m:
            print(f"  {c:.2f}  no run")
            continue
        formula = 1 - np.exp(-(m["agl"] + kw["shift"]) * kw["exp"])
        ratio = m["vi"] / (K * np.sqrt(m["thrust"]))
        print(f"  {c:.2f}  h {m['agl']:6.1f}  vs {m['vs']:+5.2f}  T {m['thrust']:6.0f}  vt {m['vt']:5.1f}  ratio {ratio:5.2f}  formula {formula:5.2f}")
