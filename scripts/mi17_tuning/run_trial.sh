#!/bin/bash
# Mi-17 standalone hover trial: one JSBSim run on a private, modified copy of aircraft/mi17.
#
# Reproduces the game-like hover: engines started via starter-cmd/cutoff-cmd, attitude hold ON from t=0,
# the same bias/collective schedule as scripts/test_mi17_hover.xml (bias 0 -> ramp to 0.15, collective 0.092,
# altitude hold at +30 ft), then reports yaw rate, roll/pitch excursion and tail rotor pitch over t = 190..END.
#
# Usage:
#   scripts/mi17_tuning/run_trial.sh NAME            (all knobs via environment variables, see below)
#   YAW_K=0.2 SAS_RP=0.1 scripts/mi17_tuning/run_trial.sh try1
#
# Knobs (defaults reproduce the unmodified model):
#   YAW_K      yaw SAS gain (afcs.xml:342, model default -0.05; the CORRECT-sign value is positive)
#   YAW_KI     yaw-rate integrator gain added to the yaw SAS (default 0 = none)
#   SAS_RP     pitch/roll SAS gain magnitude (afcs.xml:322/332, default 0.1, negative sign kept)
#   AP_GAIN    ap/pitch-gain and ap/roll-gain (afcs.xml:21-22, default 0.5)
#   ATT_KI     integral gain added to attitude hold (default 0 = none)
#   FF_GAIN    fcs/adj/pedal-torque-mix-gain (rotor_control.xml:451, default 0.0000174)
#   PEDAL_BIAS fcs/adj/pedal-bias set at t=0.1 (default 0)
#   PIN        pin tail rotor pitch to a fixed value (clamp min=max=PIN) instead of the model's command chain
#   END        run length in seconds (default 240)
#   JSBSIM     path to the JSBSim executable (default: <repo>/Release/JSBSim.exe, else `JSBSim` on PATH)
#   WORK       scratch directory (default: ${TMPDIR:-/tmp}/mi17_tuning)
#
# NOTE: model edits are made by LINE NUMBER against the model as of the commit named in README.md. The script
# asserts the expected text on each line first and aborts if the model has changed, rather than editing blindly.
set -e
NAME=${1:?usage: run_trial.sh NAME}
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
MODEL="$REPO/aircraft/mi17"
WORK=${WORK:-${TMPDIR:-/tmp}/mi17_tuning}
END=${END:-240}
YAW_K=${YAW_K:--0.05}; YAW_KI=${YAW_KI:-0}; SAS_RP=${SAS_RP:-0.1}; AP_GAIN=${AP_GAIN:-0.5}
AP_PITCH=${AP_PITCH:-$AP_GAIN}; AP_ROLL=${AP_ROLL:-$AP_GAIN}
SAS_PITCH_VAL=${SAS_PITCH_VAL:--$SAS_RP}; SAS_ROLL_VAL=${SAS_ROLL_VAL:--$SAS_RP}
ATT_KI=${ATT_KI:-0}; FF_GAIN=${FF_GAIN:-0.0000174}; PEDAL_BIAS=${PEDAL_BIAS:-0.0}; PIN=${PIN:-}
if [ -z "$JSBSIM" ]; then
  if [ -x "$REPO/Release/JSBSim.exe" ]; then JSBSIM="$REPO/Release/JSBSim.exe"; else JSBSIM=JSBSim; fi
fi

ROOT="$WORK/$NAME"; rm -rf "$ROOT"; mkdir -p "$ROOT/aircraft" "$ROOT/scripts"
cp -r "$MODEL" "$ROOT/aircraft/mi17"
for d in engine systems; do [ -d "$REPO/$d" ] && cp -r "$REPO/$d" "$ROOT/"; done
A="$ROOT/aircraft/mi17/Systems/afcs.xml"; R="$ROOT/aircraft/mi17/Systems/rotor_control.xml"

expect() { # file line pattern
  sed -n "${2}p" "$1" | grep -q -- "$3" || { echo "model changed: $1 line $2 no longer matches '$3'"; exit 2; }
}
expect "$A" 21 'ap/pitch-gain';        expect "$A" 22 'ap/roll-gain'
expect "$A" 318 'ap/sas-pitch-cmd';    expect "$A" 322 '-0.1'
expect "$A" 332 '-0.1';                expect "$A" 338 'ap/sas-yaw-cmd'
expect "$A" 342 '-0.05';               expect "$A" 346 '</fcs_function>'
expect "$A" 353 'ap/sas-pitch-cmd';    expect "$A" 363 'ap/sas-roll-cmd'
expect "$R" 451 'pedal-torque-mix-gain'; expect "$R" 582 '-0.128'; expect "$R" 583 '0.358'

# simple value edits (no line-count change)
sed -i "322s|-0.1|$SAS_PITCH_VAL|; 332s|-0.1|$SAS_ROLL_VAL|; 21s|value=\"0.5\"|value=\"$AP_PITCH\"|; 22s|value=\"0.5\"|value=\"$AP_ROLL\"|" "$A"
sed -i "451s|0.0000174|$FF_GAIN|" "$R"
[ -n "$PIN" ] && sed -i "582s|<min> -0.128 </min>|<min> $PIN </min>|; 583s|<max>  0.358 </max>|<max> $PIN </max>|" "$R"

# insertions, bottom-to-top so line numbers above stay valid
sed -i '363a\      <input> ap/roll-int </input>' "$A"
sed -i '353a\      <input> ap/pitch-int </input>' "$A"
cat > "$ROOT/yaw.blk" <<XML
    <integrator name="ap/sas-yaw-integral">
      <input> velocities/r-rad_sec </input>
      <c1> $YAW_KI </c1>
      <clipto><min> -0.3 </min><max>  0.3 </max></clipto>
    </integrator>
    <fcs_function name="ap/sas-yaw-cmd">
      <function>
        <product>
          <property> ap/stability-aug-on </property>
          <sum>
            <product><property> velocities/r-rad_sec </property><value> $YAW_K </value></product>
            <property> ap/sas-yaw-integral </property>
          </sum>
        </product>
      </function>
    </fcs_function>
XML
{ head -n 337 "$A"; cat "$ROOT/yaw.blk"; tail -n +347 "$A"; } > "$A.n" && mv "$A.n" "$A"
cat > "$ROOT/att.blk" <<XML
    <fcs_function name="ap/pitch-err-on">
      <function><product><property> ap/pitch-error-rad </property><property> ap/attitude-hold-on </property></product></function>
    </fcs_function>
    <fcs_function name="ap/roll-err-on">
      <function><product><property> ap/roll-error-rad </property><property> ap/attitude-hold-on </property></product></function>
    </fcs_function>
    <integrator name="ap/pitch-int">
      <input> ap/pitch-err-on </input>
      <c1> $ATT_KI </c1>
      <clipto><min> -0.5 </min><max>  0.5 </max></clipto>
    </integrator>
    <integrator name="ap/roll-int">
      <input> ap/roll-err-on </input>
      <c1> $ATT_KI </c1>
      <clipto><min> -0.5 </min><max>  0.5 </max></clipto>
    </integrator>
XML
{ head -n 317 "$A"; cat "$ROOT/att.blk"; tail -n +318 "$A"; } > "$A.n" && mv "$A.n" "$A"

cat > "$ROOT/scripts/trial.xml" <<XML
<?xml version="1.0"?>
<runscript name="$NAME">
  <use aircraft="mi17" initialize="reset00"/>
  <output name="trial.csv" type="CSV" rate="4">
    <property> position/h-agl-ft </property>
    <property> fcs/collective-cmd-norm </property>
    <property> propulsion/gearbox[0]/shaft-rpm </property>
    <property> velocities/r-rad_sec </property>
    <property> attitude/roll-rad </property>
    <property> attitude/pitch-rad </property>
    <property> propulsion/engine[2]/antitorque-ctrl-rad </property>
    <property> gear/unit[0]/WOW </property>
    <property> attitude/psi-rad </property>
  </output>
  <run start="0.0" end="$END" dt="0.0075">
    <event name="start" persistent="false">
      <condition> simulation/sim-time-sec ge 0.1 </condition>
      <set name="propulsion/engine[0]/starter-cmd" value="1"/>
      <set name="propulsion/engine[1]/starter-cmd" value="1"/>
      <set name="propulsion/engine[0]/cutoff-cmd" value="0"/>
      <set name="propulsion/engine[1]/cutoff-cmd" value="0"/>
      <set name="propulsion/engine[2]/set-running" value="1"/>
      <set name="ap/attitude-hold-on" value="1.0"/>
      <set name="fcs/collective-cmd-norm" value="0.0"/>
      <set name="fcs/adj/collective-bias" value="0.0"/>
      <set name="fcs/adj/pedal-bias" value="$PEDAL_BIAS"/>
    </event>
    <event name="hold-attitude" persistent="true"><condition> simulation/sim-time-sec ge 0.0 </condition><set name="ap/attitude-hold-on" value="1.0"/></event>
    <event name="settle" persistent="true"><condition> simulation/sim-time-sec le 119.999 </condition><set name="fcs/collective-cmd-norm" value="0.0"/><set name="fcs/adj/collective-bias" value="0.0"/></event>
    <event name="bias ramp" persistent="false"><condition> simulation/sim-time-sec ge 120.0 </condition><set name="fcs/adj/collective-bias" value="0.15" action="FG_RAMP" tc="20.0"/></event>
    <event name="climb and hold" persistent="true" continuous="true"><condition> simulation/sim-time-sec ge 140.0 </condition><set name="ap/altitude-target-ft" value="2313.5"/><set name="ap/altitude-hold-on" value="1.0"/><set name="fcs/collective-cmd-norm" value="0.092" action="FG_RAMP" tc="10.0"/></event>
  </run>
</runscript>
XML

( cd "$ROOT" && timeout 300 "$JSBSIM" --root="$ROOT" --script=scripts/trial.xml > run.log 2>&1 ) || true
awk -F, -v n="$NAME" -v a="yawK=$YAW_K yawKi=$YAW_KI rp=$SAS_RP ap=$AP_GAIN attKi=$ATT_KI ff=$FF_GAIN pedalBias=$PEDAL_BIAS pin=$PIN" '
  NR>1 && $1>=190 { r=$5; s+=r; k++; ar=(r<0?-r:r); if(ar>my)my=ar; ro=($6<0?-$6:$6); if(ro>mro)mro=ro
                    p=($7<0?-$7:$7); if(p>mp)mp=p; sp+=$7; at+=$8; agl=$2 }
  END { if(!k){ printf "%-16s NO DATA (run ended early: see %s/run.log)\n", n, "'"$ROOT"'"; exit }
        printf "%-16s [%s]\n   mean yaw %7.3f rad/s  max|yaw| %5.2f | max|roll| %5.2f  max|pitch| %5.2f  mean pitch %6.3f | tail pitch %6.3f | agl %5.1f ft %s\n",
               n, a, s/k, my, mro, mp, sp/k, at/k, agl, ((mro>0.4||mp>0.4||agl<25)?"<-- attitude/altitude not held":"") }' "$ROOT/trial.csv"
