# Mi-17 hover tuning

Standalone JSBSim investigation of the Mi-17 hover yaw and stability, and the fix it produced.

* `REPORT.md`: findings, evidence tables, the fix and its verification. Start here.
* `mi17_pitch_yaw_fix.patch`: the model changes (`afcs.xml`, `rotor_control.xml`). Apply with
  `git apply scripts/mi17_tuning/mi17_pitch_yaw_fix.patch` (checked cleanly against commit `08477e5a`; not applied
  to this repo).
* `mi17_governor_fix.patch`: the rotor rpm governor tuning (`rpm_governor.xml`, report section 6.2). Applies cleanly
  to the same commit, independent of the other patch.
* `governor_trial.sh`: collective-step test of the governor (knobs `KP KI KD FF`); prints the rotor rpm range per phase.
* `collective_power.sh`: one short run from the ground at a chosen collective; prints rotor rpm, throttle, power per
  engine and climb rate (report section 6.3).
* `run_trial.sh`: one hover trial on a private, modified copy of `aircraft/mi17`. All knobs are environment
  variables, documented at the top of the file. Prints yaw rate, roll/pitch excursion and tail rotor pitch.
* `cyclic_step.sh`: pitch cyclic step response test, compared with a no-step control.

```
scripts/mi17_tuning/run_trial.sh baseline     # unmodified model (expect yaw about -4.53 rad/s)
AP_PITCH=-0.5 SAS_PITCH_VAL=0.1 YAW_K=0.4 YAW_KI=0.2 FF_GAIN=0.0000087 scripts/mi17_tuning/run_trial.sh fixed
PIN=0.20 scripts/mi17_tuning/cyclic_step.sh
```

Needs `Release/JSBSim.exe` (build instructions in `REPORT.md` section 9). Trials are written to
`${TMPDIR:-/tmp}/mi17_tuning/<name>/`. The scripts edit by line number and abort if the expected text is not there,
so they target the unpatched model at commit `08477e5a`.
