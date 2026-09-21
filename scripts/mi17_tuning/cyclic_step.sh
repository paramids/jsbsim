#!/bin/bash
# Pitch-cyclic authority test for the Mi-17 hover.
#
# Runs the standard hover trial (see run_trial.sh) to t=200 s, then switches attitude hold OFF and applies a
# pitch cyclic step through the pilot channel (fcs/elevator-cmd-norm, with fcs/adj/longitudinal-gain raised to 1.0
# so a step of X gives sign(X)*|X|^1.5 rad of cyclic). It compares the pitch response of a step run against a
# no-step control run, so any attitude drift after hold-off cancels out. Use it to compare cyclic authority in
# different hover states (for example tail pitch pinned low vs high, i.e. small vs large yaw rate).
#
# Usage:  PIN=0.20 scripts/mi17_tuning/cyclic_step.sh            (other run_trial.sh knobs pass through)
#         STEP=0.7 scripts/mi17_tuning/cyclic_step.sh           (step size, default 0.7 = 0.586 rad of cyclic)
set -e
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WORK=${WORK:-${TMPDIR:-/tmp}/mi17_tuning}; export WORK
STEP=${STEP:-0.7}
if [ -z "$JSBSIM" ]; then
  if [ -x "$REPO/Release/JSBSim.exe" ]; then JSBSIM="$REPO/Release/JSBSim.exe"; else JSBSIM=JSBSim; fi
fi
TAG=${TAG:-cyc${PIN:+_pin$PIN}}

run() { # name stepvalue
  local name="${TAG}_$1" step=$2
  END=1 "$HERE/run_trial.sh" "$name" > /dev/null 2>&1 || true      # builds the modified model copy + trial.xml
  local R="$WORK/$name"
  sed -i 's|rate="4"|rate="20"|; s|<property> attitude/psi-rad </property>|<property> attitude/psi-rad </property>\n    <property> velocities/q-rad_sec </property>\n    <property> fcs/longitudinal-ctrl-rad </property>|' "$R/scripts/trial.xml"
  sed -i 's|end="1"|end="206"|' "$R/scripts/trial.xml"
  sed -i 's|<condition> simulation/sim-time-sec ge 0.0 </condition><set name="ap/attitude-hold-on" value="1.0"/>|<condition> simulation/sim-time-sec le 199.999 </condition><set name="ap/attitude-hold-on" value="1.0"/>|' "$R/scripts/trial.xml"
  sed -i "s|  </run>|    <event name=\"step\" persistent=\"false\"><condition> simulation/sim-time-sec ge 200.0 </condition><set name=\"ap/attitude-hold-on\" value=\"0.0\"/><set name=\"fcs/adj/longitudinal-gain\" value=\"1.0\"/><set name=\"fcs/elevator-cmd-norm\" value=\"$step\"/></event>\n  </run>|" "$R/scripts/trial.xml"
  ( cd "$R" && timeout 300 "$JSBSIM" --root="$R" --script=scripts/trial.xml > run.log 2>&1 ) || true
}

echo "Pitch cyclic step test  (PIN=${PIN:-none}, step ${STEP} -> $(awk -v s=$STEP 'BEGIN{printf "%.3f", s^1.5}') rad cyclic)"
run ctrl 0; run plus "$STEP"; run minus "-$STEP"
# columns: 1 t, 5 yaw rate, 7 pitch, 11 q, 12 longitudinal-ctrl-rad (column 1 is Time)
pitch_at() { awk -F, -v t="$2" 'NR>1 && $1>=t {print $7; exit}' "$WORK/${TAG}_$1/trial.csv"; }
yaw_at()   { awk -F, -v t="$2" 'NR>1 && $1>=t {print $5; exit}' "$WORK/${TAG}_$1/trial.csv"; }
cyc_at()   { awk -F, -v t="$2" 'NR>1 && $1>=t {print $12; exit}' "$WORK/${TAG}_$1/trial.csv"; }
printf "state at t=199.9: yaw rate %.2f rad/s, pitch %.3f rad\n" "$(yaw_at ctrl 199.9)" "$(pitch_at ctrl 199.9)"
printf "%-8s %-13s %-13s %-13s %-13s\n" "t (s)" "pitch ctrl" "pitch +step" "pitch -step" "(+) - (-)"
for t in 200.5 201 202 203 205; do
  c=$(pitch_at ctrl $t); p=$(pitch_at plus $t); m=$(pitch_at minus $t)
  printf "%-8s %-13.3f %-13.3f %-13.3f %-13.3f\n" $t $c $p $m $(awk -v p=$p -v m=$m 'BEGIN{print p-m}')
done
printf "cyclic actually commanded at t=201: control %.3f, +step %.3f, -step %.3f rad\n" "$(cyc_at ctrl 201)" "$(cyc_at plus 201)" "$(cyc_at minus 201)"
