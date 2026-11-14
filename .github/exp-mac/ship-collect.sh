#!/bin/bash
# After a ship-validation job: memory, link times, and whether the shipped dSYM matches the binary.
# usage: ship-collect.sh <build-dir> <runner-temp> <out-dir>
B=$1 TMP=$2 OUT=$3
H="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$OUT"
pkill -f memsample.sh
cp "$TMP/mem.txt" "$OUT/" 2>/dev/null
awk 'NR > 1 { if ($2 != "?" && (min == "" || $2 + 0 < min + 0)) min = $2; if ($3 + 0 > sw) sw = $3 + 0; if ($4 + 0 > ld) ld = $4 + 0; if ($7 + 0 > cl) cl = $7 + 0 }
     END { printf "memory: min free %s%%, max swap used %.0f MB, max single ld RSS %d MB, max clang sum RSS %d MB\n", min, sw, ld, cl }' "$OUT/mem.txt" | tee "$OUT/mem-summary.txt"
cp "$B/.ninja_log" "$OUT/ninja_log.txt" 2>/dev/null
python3 "$H/ninjalog.py" "$OUT/ninja_log.txt" | tee "$OUT/links.txt"
grep -E "^(CMAKE_BUILD_TYPE|MUDLET_PGO|CMAKE_INTERPROCEDURAL_OPTIMIZATION|CMAKE_EXE_LINKER_FLAGS|EXP_FULL_DEBUG|SENTRY_SEND_DEBUG|BUILD_TESTING)[:=]" "$B/CMakeCache.txt" | tee "$OUT/cache.txt"
cp "$B"/Testing/Temporary/LastTestsFailed*.log "$OUT/" 2>/dev/null
bin=$B/src/mudlet.app/Contents/MacOS/mudlet
dsym=$bin.dSYM
arch=$(uname -m)
{ echo "uuid binary: $(dwarfdump --uuid "$bin")"
  echo "uuid dSYM:   $(dwarfdump --uuid "$dsym")"
  echo "dSYM kB $(du -sk "$dsym" | cut -f1)  binary bytes $(stat -f %z "$bin")"
  ls "$B/src/mudlet-lto" 2>/dev/null | head -3; echo "lto objects kept: $(ls "$B/src/mudlet-lto" 2>/dev/null | wc -l)"
  for sym in TTrigger5match TBuffer19translateToPlainText cTelnet17processSocketData TLuaInterpreter16callEventHandler; do
    a=$(grep -m1 -E "\] __ZN[0-9]*$sym" "$B/src/mudlet.map" | awk '{print $1}')
    [ -n "$a" ] && echo "$sym $a -> $(atos -o "$dsym" -arch "$arch" -l 0x100000000 "$(printf '0x%x' $((a + 16)))")"
  done
  dwarfdump --statistics "$dsym" 2>/dev/null | tr ',' '\n' | grep -E '"#functions"|"#inlined functions"|"#source variables"' | head -4
} 2>&1 | tee "$OUT/dsym-check.txt"
