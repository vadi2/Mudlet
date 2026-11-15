#!/bin/bash
# exp only: does the dSYM of a ThinLTO mudlet need -object_path_lto when -cache_path_lto is on?
# Relinks the shipped link line four ways into scratch dirs and checks each dSYM.
# usage: collect.sh <build-dir> <out-dir>
set -u
B=$1 OUT=$2
mkdir -p "$OUT"
arch=$(uname -m)
{
  echo "# xcode: $(xcodebuild -version | tr '\n' ' ')  ld: $(ld -v 2>&1 | head -1)"
  grep -E "^(CMAKE_BUILD_TYPE|MUDLET_PGO|CMAKE_INTERPROCEDURAL_OPTIMIZATION|CMAKE_EXE_LINKER_FLAGS|SENTRY_SEND_DEBUG|BUILD_TESTING|CMAKE_CXX_COMPILER_VERSION)[:=]" "$B/CMakeCache.txt"
  echo "# compile lines with -flto=thin: $(grep -c -- '-flto=thin' "$B/compile_commands.json")"
  echo "# compile lines without: $(grep '"command"' "$B/compile_commands.json" | grep -vc -- '-flto=thin')"
} 2>&1 | tee "$OUT/config.txt"
ninja -C "$B" -t commands mudlet_executable | tail -1 > "$OUT/link-full.txt"
echo "# link flags of interest" | tee "$OUT/link-flags.txt"
grep -o -- '-flto=[a-z]*\|-export_dynamic\|-object_path_lto -Xlinker [^ ]*\|-cache_path_lto,[^ ]*' "$OUT/link-full.txt" | sort | uniq -c | tee -a "$OUT/link-flags.txt"
{
  echo "# shipped build: $(ls "$B/src/mudlet-lto" 2>/dev/null | wc -l) objects in mudlet-lto, lto-cache $(du -sk "$B/lto-cache" 2>/dev/null | cut -f1) kB / $(ls "$B/lto-cache" 2>/dev/null | wc -l) files"
  bin=$B/src/mudlet.app/Contents/MacOS/mudlet
  echo "# shipped dSYM: $(du -sk "$bin.dSYM" | cut -f1) kB; uuid bin $(dwarfdump --uuid "$bin" | awk '{print $2}') dSYM $(dwarfdump --uuid "$bin.dSYM" | awk '{print $2}')"
} 2>&1 | tee "$OUT/shipped.txt"

variant() {
  local name=$1 drop=$2 d=$B/exp-$1
  rm -rf "$d"; mkdir -p "$d"
  python3 - "$OUT/link-full.txt" "$d" "$drop" > "$d/link.sh" <<'PY'
import re, sys
cmd = open(sys.argv[1]).read().strip()
d, drop = sys.argv[2], sys.argv[3]
if cmd.startswith(": && "):
    cmd = cmd[5:]
cmd = cmd.split(" && ")[0]
if "obj" in drop:
    cmd, n = re.subn(r"-Xlinker -object_path_lto -Xlinker \S+", "", cmd)
    assert n == 1, "object_path_lto not found"
if "cache" in drop:
    cmd, n = re.subn(r"-Wl,-cache_path_lto,\S+", "", cmd)
    assert n == 1, "cache_path_lto not found"
cmd, n = re.subn(r"(^|\s)-o \S+", r"\1-o " + d + "/mudlet -Wl,-map," + d + "/map.txt", cmd, count=1)
assert n == 1
print(cmd)
PY
  local t0=$(date +%s)
  (cd "$B" && sh "$d/link.sh") > "$d/link.log" 2>&1 || { echo "$name: relink failed"; tail -20 "$d/link.log"; return; }
  local t1=$(date +%s)
  dsymutil "$d/mudlet" -o "$d/mudlet.dSYM" > "$d/dsymutil.log" 2>&1
  dsymutil --dump-debug-map "$d/mudlet" > "$d/debugmap.txt" 2>&1
  {
    echo "== $name (dropped: ${drop:-nothing}) link $((t1 - t0))s"
    echo "-object_path_lto in link: $(grep -c -- '-object_path_lto' "$d/link.sh")  -cache_path_lto in link: $(grep -c -- '-cache_path_lto' "$d/link.sh")"
    echo "dsymutil warnings: $(grep -c warning "$d/dsymutil.log")  dSYM kB $(du -sk "$d/mudlet.dSYM" | cut -f1)"
    grep warning "$d/dsymutil.log" | head -3
    echo "debug map objects by dir:"
    grep -E "^\s*- filename:" "$d/debugmap.txt" | sed -E "s/^ *- filename: *'?//; s/'?\$//; s#\(.*##" | xargs -n1 dirname 2>/dev/null | sed "s#^$B#<B>#" | sort | uniq -c | sort -rn | head -6
    missing=0; total=0
    for f in $(grep -E "^\s*- filename:" "$d/debugmap.txt" | sed -E "s/^ *- filename: *'?//; s/'?\$//; s#\(.*##" | sort -u); do total=$((total+1)); [ -e "$f" ] || missing=$((missing+1)); done
    echo "debug map object files: $total, missing on disk after link: $missing"
    dwarfdump --statistics "$d/mudlet.dSYM" 2>/dev/null | tr ',' '\n' | grep -E '"#functions"|"#source variables"|"#bytes within functions"' | head -3
    for sym in TTrigger5match TBuffer19translateToPlainText cTelnet17processSocketData TLuaInterpreter16callEventHandler; do
      a=$(grep -m1 -E "\] __ZN[0-9]*$sym" "$d/map.txt" | awk '{print $1}')
      [ -n "$a" ] && echo "atos $sym -> $(atos -o "$d/mudlet.dSYM" -arch "$arch" -l 0x100000000 "$(printf '0x%x' $((a + 16)))")"
    done
    echo "exported lua C API symbols: $(nm -gU "$d/mudlet" | grep -cE ' _luaL?_')"
  } 2>&1 | tee "$OUT/variant-$name.txt"
}
variant keep ""
variant noobj "obj"
variant nocache "cache"
variant neither "obj,cache"
