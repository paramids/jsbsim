#!/bin/bash
# Mi-17 collective vs power test: from the ground, step the collective to C at t=60 (2 s ramp), average t=64..76
# and report rotor rpm, throttle, power per engine and climb rate. One short run per value keeps the altitude
# (and so the air density) close to the start; a long staircase climbs to ~10,000 ft and loses ~30% power.
# Usage: scripts/mi17_tuning/collective_power.sh 0.6      (run several values in parallel with &)
#   MODEL  aircraft/mi17 directory (default: this repo's)   JSBSIM  executable (default Release/JSBSim.exe)
C=${1:?usage: collective_power.sh COLLECTIVE}
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; REPO="$(cd "$HERE/../.." && pwd)"
MODEL=${MODEL:-$REPO/aircraft/mi17}; JSBSIM=${JSBSIM:-$REPO/Release/JSBSim.exe}
R=${WORK:-${TMPDIR:-/tmp}/mi17_tuning}/power_$C; rm -rf "$R"; mkdir -p "$R/aircraft" "$R/scripts"
cp -r "$MODEL" "$R/aircraft/mi17"; for d in engine systems; do [ -d "$REPO/$d" ] && cp -r "$REPO/$d" "$R/"; done
cat > $R/scripts/p.xml <<XML
<?xml version="1.0"?>
<runscript name="pw$C">
  <use aircraft="mi17" initialize="reset00"/>
  <output name="p.csv" type="CSV" rate="10">
    <property> fcs/collective-cmd-norm </property>
    <property> propulsion/gearbox[0]/shaft-rpm </property>
    <property> governor/throttle-output </property>
    <property> propulsion/engine[0]/power-hp </property>
    <property> velocities/h-dot-fps </property>
    <property> position/h-agl-ft </property>
    <property> velocities/vc-kts </property>
    <property> attitude/pitch-rad </property>
  </output>
  <run start="0.0" end="76" dt="0.0075">
    <event name="start" persistent="false"><condition> simulation/sim-time-sec ge 0.1 </condition>
      <set name="propulsion/engine[0]/starter-cmd" value="1"/><set name="propulsion/engine[1]/starter-cmd" value="1"/>
      <set name="propulsion/engine[0]/cutoff-cmd" value="0"/><set name="propulsion/engine[1]/cutoff-cmd" value="0"/>
      <set name="propulsion/engine[2]/set-running" value="1"/><set name="ap/attitude-hold-on" value="1.0"/>
      <set name="fcs/collective-cmd-norm" value="0.0"/></event>
    <event name="hold" persistent="true"><condition> simulation/sim-time-sec ge 0.0 </condition><set name="ap/attitude-hold-on" value="1.0"/></event>
    <event name="step" persistent="true"><condition> simulation/sim-time-sec ge 60 </condition><set name="fcs/collective-cmd-norm" value="$C" action="FG_RAMP" tc="2.0"/></event>
  </run>
</runscript>
XML
( cd $R && timeout 300 "$JSBSIM" --root=$R --script=scripts/p.xml > run.log 2>&1 )
# average over t = 63..76 (after the 2 s ramp settles)
awk -F, -v c=$C 'NR>1 && $1>=64 { n++; for(i=2;i<=9;i++) s[i]+=$i; if(!mn||$3/192<mn) mn=$3/192; last=$3/192; ha=$7 } END { printf "%5.2f rotor avg %.3f min %.3f end %.3f | thr %.2f | P/engine %5.0f hp | climb %5.1f ft/s | agl end %4.0f | kt %4.0f\n", c, s[3]/n/192, mn, last, s[4]/n, s[5]/n, s[6]/n, ha, s[8]/n }' $R/p.csv
