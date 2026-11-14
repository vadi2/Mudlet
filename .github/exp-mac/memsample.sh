#!/bin/bash
# Samples memory every 10 s until killed: free %, swap used, the biggest ld and clang processes.
# usage: memsample.sh <out>
out=$1
echo "# time free_pct swap_used_mb ld_max_rss_mb ld_sum_rss_mb ld_n clang_sum_rss_mb clang_n" > "$out"
while :; do
  free=$(memory_pressure -Q 2>/dev/null | awk -F': ' '/free percentage/ {gsub("%", "", $2); print $2}')
  swap=$(sysctl -n vm.swapusage | awk '{for (i = 1; i <= NF; i++) if ($i == "used") {v = $(i + 2); sub("M", "", v); print v}}')
  ps -A -o rss=,comm= | awk -v t="$(date +%H:%M:%S)" -v f="${free:-?}" -v s="${swap:-?}" '
    { n = split($2, p, "/"); c = p[n] }
    c == "ld" || c == "ld64" || c == "ld-prime" { lm = ($1 > lm ? $1 : lm); ls += $1; ln++ }
    c == "clang" || c == "clang++" { cs += $1; cn++ }
    END { printf "%s %s %s %.0f %.0f %d %.0f %d\n", t, f, s, lm / 1024, ls / 1024, ln, cs / 1024, cn }' >> "$out"
  sleep 10
done
