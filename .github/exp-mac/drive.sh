#!/bin/bash
# N interleaved rounds of every arm, one arm at a time, the order rotated each round.
# Arm = <code><workload>: workload 46 (bench3.lua), 0 (bench0.lua), svof (the real svof replay).
# ctl46 = bp's own app on 46 a second time, the control. The app for <code> is $W/apps/<code>/*.app.
# usage: drive.sh <prefix> <rounds> <arm,arm,...>    env: W, SVOF_BUNDLE, OFFSET (rotation start), STRIDE
set -u
prefix=$1 rounds=$2
IFS=, read -r -a list <<< "$3"
H="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
O=$W/out/$prefix
mkdir -p "$O"
prog=$O/progress.txt
mark() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" | tee -a "$prog"; }
n=${#list[@]}
stride=${STRIDE:-}; [ -n "$stride" ] || stride=$(( n / rounds )); [ "$stride" -lt 1 ] && stride=1
SVOF="mode = [==[socket]==], bundle = [==[$SVOF_BUNDLE]==], settle = 4.0"
svofcorpus=$SVOF_BUNDLE/$(python3 -c 'import re, sys; print(re.search(r"corpus\s*=\s*\"([^\"]+)\"", open(sys.argv[1]).read()).group(1))' "$SVOF_BUNDLE/manifest.lua")

runarm() { # <arm> <tag>
  local a=$1 code w app
  case "$a" in
    ctl46) code=bp; w=46 ;;
    *svof) code=${a%svof}; w=svof ;;
    *46) code=${a%46}; w=46 ;;
    *0) code=${a%0}; w=0 ;;
    *) echo "UNKNOWN ARM $a"; return ;;
  esac
  app=$(ls -d "$W/apps/$code"/*.app 2>/dev/null | head -1)
  [ -n "$app" ] || { echo "NO APP for $code"; return; }
  case "$w" in
    46) "$H/runarm.sh" "$2" "$app" "$H/bench3.lua" "$H/corpus.log" 10 1 120 BenchRepro 0 ;;
    0) "$H/runarm.sh" "$2" "$app" "$H/bench0.lua" "$H/corpus.log" 10 1 120 BenchRepro 0 ;;
    svof) "$H/runarm.sh" "$2" "$app" "$GITHUB_WORKSPACE/test/svof-replay/bench.lua" "$svofcorpus" 1 0 300 SvofBench 5 "$SVOF" ;;
  esac
}

busy() { top -l 2 -n 0 -s 1 | awk '/CPU usage/ {idle = $(NF - 1)} END {sub("%", "", idle); printf "%.1f", 100 - idle}'; }

mark "start rounds=$rounds arms=$3 stride=$stride offset=${OFFSET:-0} $(sysctl -n hw.ncpu) cpus $(sysctl -n machdep.cpu.brand_string)"
for r in $(seq 1 "$rounds"); do
  k=$(( ((r - 1) * stride + ${OFFSET:-0}) % n ))
  for i in $(seq 0 $((n - 1))); do
    a=${list[$(( (k + i) % n ))]}
    tag=$prefix-$r-$a
    o=$O/$tag.txt
    for try in 1 2; do
      # Start only once the runner is quiet (the previous client's exit, background indexing), up to 30 s
      tq=$(date +%s)
      for _ in $(seq 1 15); do
        cpu=$(busy)
        awk -v c="$cpu" 'BEGIN {exit !(c < 12)}' && break
      done
      ts=$(date +%s)
      out=$(runarm "$a" "$tag" 2>&1)
      { echo "round $r arm $a try $try started $(date -r "$ts" +%H:%M:%S) guest_busy_cpu_pct $cpu quiet_wait_s $((ts - tq))"
        echo "$out"; } > "$o"
      grep -q '^elapsed_s' "$o" && break
      cp "$o" "$o.fail$try"
      mark "$tag no result (try $try)"
    done
    mark "$tag $(grep -E '^(elapsed_s|hb_max_ms|peak_ws_kb|gate|svo_prompts|counts_md5|invocations|stop_reason) ' "$o" | tr '\n' ' ')"
  done
  mark "round $r done"
done
mark "DONE"
touch "$O/DONE"
