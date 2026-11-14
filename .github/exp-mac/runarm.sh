#!/bin/bash
# One Mudlet socket arm: a fresh private home and profile carrying one bench script, a log server,
# Mudlet on the offscreen platform under /usr/bin/time -l, then the result in summary.lua's format.
# usage: runarm.sh <tag> <app> <bench.lua> <corpus> <repeats> <usego 0|1> <timeout-s> <profile> <exit-wait-s> [extra-lua]
set -u
tag=$1 app=$2 bench=$3 corpus=$4 repeats=$5 usego=$6 timeout=$7 prof=$8 exitwait=$9 extra=${10:-}
H="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R=$W/runs/$tag
exe=$app/Contents/MacOS/mudlet
rm -rf "$R"
cfg=$R/home/.config/mudlet
mkdir -p "$cfg/profiles/$prof/current" "$R/home/Library" "$R/home/.local/share" "$R/home/.cache"
result=$R/result.txt
slog=$R/logserver.log
go=$R/go
now() { perl -MTime::HiRes=time -e 'printf "%.3f\n", time'; }
cpu_ms() { ps -o time= -p "$1" 2>/dev/null | awk '{n = split($1, p, ":"); s = 0; for (i = 1; i <= n; i++) s = s * 60 + p[i]; printf "%d", s * 1000}'; }

goarg=()
[ "$usego" = 1 ] && goarg=(--go "$go")
python3 "$H/logserver.py" "$corpus" "$repeats" "$slog" "$R/port" "${goarg[@]}" &
srv=$!
for _ in $(seq 1 100); do [ -f "$R/port" ] && break; sleep 0.05; done
port=$(cat "$R/port" 2>/dev/null)
[ -n "$port" ] || { echo "NO LOGSERVER"; kill $srv 2>/dev/null; exit 1; }

printf '[General]\ntelnetHandlerAsked=true\ntelnetHandlerEnabled=false\ntelnetHandlerDontAsk=true\n' > "$cfg/Mudlet.ini"
printf '%s' "$port" > "$cfg/profiles/$prof/port"
printf '127.0.0.1' > "$cfg/profiles/$prof/url"
[ -n "$extra" ] && extra=", $extra"
cat > "$cfg/profiles/$prof/current/2030-01-01#00-00-00.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE MudletPackage>
<MudletPackage version="1.001">
	<ScriptPackage>
		<Script isActive="yes" isFolder="no">
			<name>bench</name>
			<packageName></packageName>
			<script>_G.BENCH_CFG = { result = [==[$result]==], label = [==[$tag]==], port = $port, quit = false$extra }
dofile([==[$bench]==])</script>
			<eventHandlerList />
		</Script>
	</ScriptPackage>
</MudletPackage>
EOF

env HOME="$R/home" CFFIXED_USER_HOME="$R/home" XDG_CONFIG_HOME="$R/home/.config" XDG_DATA_HOME="$R/home/.local/share" \
    XDG_CACHE_HOME="$R/home/.cache" QT_QPA_PLATFORM="${BENCH_QPA:-offscreen}" \
    /usr/bin/time -l "$exe" --profile="$prof" > "$R/mudlet.out" 2> "$R/mudlet.err" &
tpid=$!
mpid=
for _ in $(seq 1 50); do mpid=$(pgrep -P $tpid | head -1); [ -n "$mpid" ] && break; sleep 0.1; done
echo "arm=$tag exe=$exe port=$port mudlet_pid=${mpid:-none}"
t0=$(now)
ok=0
for _ in $(seq 1 $((2 * timeout))); do
  grep -q "client connected" "$slog" 2>/dev/null && { ok=1; break; }
  kill -0 $tpid 2>/dev/null || break
  sleep 0.5
done
if [ $ok = 1 ]; then
  echo "connect_s $(echo "$(now) - $t0" | bc)"
  if [ "$usego" = 1 ]; then
    sleep 2.5
    c0=$(cpu_ms "$mpid"); w0=$(now)
    touch "$go"
  else
    w0=$(now)
  fi
  end=$(( $(date +%s) + timeout ))
  while [ "$(date +%s)" -lt $end ]; do
    [ -f "$result" ] && break
    kill -0 $tpid 2>/dev/null || break
    sleep 0.05
  done
  w1=$(now)
  if [ "$usego" = 1 ]; then
    echo "window_ms $(echo "($w1 - $w0) * 1000 / 1" | bc) cpu_proc_ms $(( $(cpu_ms "$mpid") - c0 ))"
  else
    echo "result_after_connect_s $(echo "$w1 - $w0" | bc)"
  fi
else
  echo "NO CONNECTION"
fi
sleep 0.3
for _ in $(seq 1 $((2 * exitwait))); do kill -0 "$mpid" 2>/dev/null || break; sleep 0.5; done
[ -n "$mpid" ] && kill -TERM "$mpid" 2>/dev/null
for _ in $(seq 1 10); do kill -0 $tpid 2>/dev/null || break; sleep 0.3; done
[ -n "$mpid" ] && kill -KILL "$mpid" 2>/dev/null
wait $tpid 2>/dev/null
rss=$(awk '/maximum resident set size/ {print int($1 / 1024)}' "$R/mudlet.err")
foot=$(awk '/peak memory footprint/ {print int($1 / 1024)}' "$R/mudlet.err")
echo "peak_ws_kb ${rss:-0} peak_footprint_kb ${foot:-0}"
grep -E "real|user|sys" "$R/mudlet.err" | tail -1
if [ -f "$result" ]; then
  grep -vE '^(samples_t_lines|gaps_ms|matches|counts) ' "$result"
  c=$(grep -m1 '^counts ' "$result")
  [ -n "$c" ] && echo "counts_md5 $(printf '%s\n' "$c" | md5 -q | cut -c1-8)"
  m=$(grep -m1 '^matches ' "$result")
  [ -n "$m" ] && echo "matches_line ${m#matches }"
else
  echo "NO RESULT"
  tail -8 "$result.dbg" 2>/dev/null
  grep -iE "error|fatal|abort|dyld|qt\.qpa" "$R/mudlet.err" "$R/mudlet.out" 2>/dev/null | head -12
fi
for _ in $(seq 1 12); do kill -0 $srv 2>/dev/null || break; sleep 0.25; done
kill $srv 2>/dev/null
wait $srv 2>/dev/null
grep -E "SENT|connected|closed|CLIENT> " "$slog" 2>/dev/null | head -12
cl=$(grep "CLIENT_LINES" "$slog" 2>/dev/null | tail -1 | awk '{print $NF}')
echo "client_sends ${cl:-unknown}"
if [ -n "${KEEP_FAILED_RUN:-}" ] && [ ! -f "$result" ]; then
  mkdir -p "$KEEP_FAILED_RUN"
  tar -czf "$KEEP_FAILED_RUN/$tag.tgz" -C "$R" --exclude 'home/Library' . 2>/dev/null
fi
rm -rf "$R"
