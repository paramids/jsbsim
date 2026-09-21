#!/bin/bash
# Mi-17 governor trial: collective steps on a private copy of aircraft/mi17, reports rotor rpm (shaft-rpm / 192)
# per phase. Steps: 0.15 (t=60, 1 s ramp), 0.30 (t=100), 0.05 (t=140), 0.20 (t=180); attitude hold on.
# Usage: KP=4 KI=1 KD=0.3 FF=1.2 scripts/mi17_tuning/governor_trial.sh NAME
#   KP KI KD  governor/gain, integral-gain, derivative-gain (model defaults 2.5 / 0.3 / 0.1)
#   FF        collective feedforward added to the governor output (default 0 = none)
#   MODEL     aircraft/mi17 directory to copy (default: this repo's; use a patched copy to test a fix)
# Works on the unpatched governor (edits its default values); with a patched model pass KP/KI/KD equal to the
# patched values, or leave the sed edits to no-op. Results in ${TMPDIR:-/tmp}/mi17_tuning/<NAME>/.
NAME=${1:?usage: governor_trial.sh NAME}; KP=${KP:-2.5}; KI=${KI:-0.3}; KD=${KD:-0.1}; FF=${FF:-0}
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; REPO="$(cd "$HERE/../.." && pwd)"
MODEL=${MODEL:-$REPO/aircraft/mi17}; JSBSIM=${JSBSIM:-$REPO/Release/JSBSim.exe}
ROOT=${WORK:-${TMPDIR:-/tmp}/mi17_tuning}/$NAME; rm -rf "$ROOT"; mkdir -p "$ROOT/aircraft" "$ROOT/scripts"
cp -r "$MODEL" "$ROOT/aircraft/mi17"; for d in engine systems; do [ -d "$REPO/$d" ] && cp -r "$REPO/$d" "$ROOT/"; done
G="$ROOT/aircraft/mi17/Systems/rpm_governor.xml"
sed -i "s|<property value=\"2.5\"> governor/gain|<property value=\"$KP\"> governor/gain|; s|<property value=\"0.3\"> governor/integral-gain|<property value=\"$KI\"> governor/integral-gain|; s|<property value=\"0.1\"> governor/derivative-gain|<property value=\"$KD\"> governor/derivative-gain|" "$G"
if [ "$FF" != "0" ]; then
sed -i "s|<property> governor/pid-output </property>|<sum><property> governor/pid-output </property><product><property> fcs/collective-cmd-norm </property><value> $FF </value></product></sum>|" "$G"
grep -q "fcs/collective-cmd-norm </property><value> $FF" "$G" || { echo ff edit failed; exit 2; }
fi
cat > "$ROOT/scripts/t.xml" <<XML
<?xml version="1.0"?>
<runscript name="$NAME">
  <use aircraft="mi17" initialize="reset00"/>
  <output name="t.csv" type="CSV" rate="10">
    <property> fcs/collective-cmd-norm </property>
    <property> propulsion/gearbox[0]/shaft-rpm </property>
    <property> governor/throttle-output </property>
    <property> position/h-agl-ft </property>
  </output>
  <run start="0.0" end="260" dt="0.0075">
    <event name="start" persistent="false"><condition> simulation/sim-time-sec ge 0.1 </condition>
      <set name="propulsion/engine[0]/starter-cmd" value="1"/><set name="propulsion/engine[1]/starter-cmd" value="1"/>
      <set name="propulsion/engine[0]/cutoff-cmd" value="0"/><set name="propulsion/engine[1]/cutoff-cmd" value="0"/>
      <set name="propulsion/engine[2]/set-running" value="1"/><set name="ap/attitude-hold-on" value="1.0"/>
      <set name="fcs/collective-cmd-norm" value="0.0"/></event>
    <event name="hold" persistent="true"><condition> simulation/sim-time-sec ge 0.0 </condition><set name="ap/attitude-hold-on" value="1.0"/></event>
    <event name="c1" persistent="true"><condition> simulation/sim-time-sec ge 60 </condition><set name="fcs/collective-cmd-norm" value="0.15" action="FG_RAMP" tc="1.0"/></event>
    <event name="c2" persistent="true"><condition> simulation/sim-time-sec ge 100 </condition><set name="fcs/collective-cmd-norm" value="0.30" action="FG_RAMP" tc="2.0"/></event>
    <event name="c3" persistent="true"><condition> simulation/sim-time-sec ge 140 </condition><set name="fcs/collective-cmd-norm" value="0.05" action="FG_RAMP" tc="2.0"/></event>
    <event name="c4" persistent="true"><condition> simulation/sim-time-sec ge 180 </condition><set name="fcs/collective-cmd-norm" value="0.20" action="FG_RAMP" tc="1.0"/></event>
  </run>
</runscript>
XML
( cd "$ROOT" && timeout 300 "$JSBSIM" --root="$ROOT" --script=scripts/t.xml > run.log 2>&1 ) || true
# per-window rpm stats (rotor = shaft/192)
awk -F, -v n="$NAME" 'NR>1 { t=$1; r=$3/192; w=(t<60)?"a<60":(t<100)?"b60-100":(t<140)?"c100-140":(t<180)?"d140-180":"e180+"; if(t<20) next; if(!(w in mn)||r<mn[w])mn[w]=r; if(!(w in mx)||r>mx[w])mx[w]=r; last[w]=r }
 END{printf "%-14s", n; for(k in mn) printf " %s[%.3f..%.3f end %.3f]", k, mn[k], mx[k], last[k]; printf "\n"}' "$ROOT/t.csv" | tr ' ' '\n' | paste -sd' ' | sed 's/  */ /g'
