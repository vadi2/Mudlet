#!/bin/bash
# One variant of the macOS release build: PGO exactly as a tagged release (GENERATE -> pgo-train.sh -> USE),
# then three code layouts of it by relinking with a pad first in __text. Never signs (beyond ad hoc) or uploads.
# usage: build.sh <variant> <out-dir>     env: BUILD_DIR GITHUB_WORKSPACE QT_ROOT_DIR PADS
set -uo pipefail
V=$1
OUT=$2
B=$BUILD_DIR
S=$GITHUB_WORKSPACE
H=$S/.github/exp-mac
read -r -a PADS <<< "${PADS:-0 1040 2208}"
mkdir -p "$OUT/apps"
T=$OUT/timing.txt
stamp() { echo "$(date +%s) $(date -u +%H:%M:%SZ) $*" | tee -a "$T"; }
die() { stamp "FAILED: $*"; exit 1; }
# the job log gets link steps, errors and every 200th line; the full output goes to a file
filtered() { awk '/Linking|error:|FAILED|warning: \(|ld: warning|dsymutil|Creating/ || NR % 200 == 0 { print; fflush() }'; }

case "$V" in
  bp) VFLAGS=() ;;
  tl) VFLAGS=(-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON) ;;
  tk) VFLAGS=(-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-cache_path_lto,$B/lto-cache") ;;
  fl) VFLAGS=(-DCMAKE_C_FLAGS=-flto=full -DCMAKE_CXX_FLAGS=-flto=full -DCMAKE_OBJCXX_FLAGS=-flto=full
              -DCMAKE_EXE_LINKER_FLAGS=-flto=full -DCMAKE_SHARED_LINKER_FLAGS=-flto=full -DCMAKE_MODULE_LINKER_FLAGS=-flto=full) ;;
  *) die "unknown variant $V" ;;
esac
ARCH=$(uname -m)

{ sw_vers; xcodebuild -version; clang --version | head -1; ld -v 2>&1 | head -3; cmake --version | head -1
  echo "ncpu $(sysctl -n hw.ncpu) memsize $(sysctl -n hw.memsize) $(sysctl -n machdep.cpu.brand_string)"
  echo "variant $V flags ${VFLAGS[*]:-none} pads ${PADS[*]}"; } > "$OUT/env.txt" 2>&1
cat "$OUT/env.txt"
"$H/memsample.sh" "$OUT/mem.txt" &
MEMPID=$!
trap 'kill $MEMPID 2>/dev/null' EXIT

# As build-mudlet.yml's "Build Mudlet" step on a tag, minus the secrets: SENTRY_SEND_DEBUG=1 there
# gives -g on both passes, which EXP_FULL_DEBUG reproduces without the upload
export CMAKE_BUILD_TYPE=Release MUDLET_PGO=GENERATE USE_SANITIZER= WITH_SENTRY=ON SENTRY_SEND_DEBUG=0 SENTRY_DSN= WITH_MCPSERVER=ON
export NINJA_STATUS='[%f/%t %o/sec] '
stamp "configure GENERATE"
cmake --preset ci-macos -DEXP_FULL_DEBUG=ON "${VFLAGS[@]}" > "$OUT/configure-generate.log" 2>&1 || { tail -40 "$OUT/configure-generate.log"; die "configure GENERATE"; }
nlog() { # the .ninja_log lines the last build appended (all of it if ninja recompacted the log)
  local n; n=$(wc -l < "$B/.ninja_log")
  if [ "$n" -gt "$1" ]; then tail -n +$(($1 + 1)) "$B/.ninja_log"; else cat "$B/.ninja_log"; fi > "$2"
}
nlines() { wc -l < "$B/.ninja_log" 2>/dev/null || echo 0; }
stamp "build GENERATE"
n0=$(nlines)
cmake --build --preset ci-macos --target PipelineBenchmark MapRenderBenchmark PathfindBenchmark 2>&1 | tee "$OUT/build-generate.log" | filtered
[ "${PIPESTATUS[0]}" = 0 ] || { grep -B2 -A12 "FAILED\|error:" "$OUT/build-generate.log" | head -80; die "build GENERATE"; }
nlog "$n0" "$OUT/ninja_log-generate.txt"
stamp "train"
"$S/CI/pgo-train.sh" "$B" > "$OUT/train.log" 2>&1 || { tail -60 "$OUT/train.log"; die "train"; }
grep '^METRIC' "$OUT/train.log" > "$OUT/train-metrics.txt"
ls -la "$B"/pgo* >> "$OUT/train-metrics.txt" 2>&1
stamp "configure USE"
cmake -B "$B" -DMUDLET_PGO=USE -DBUILD_TESTING=OFF > "$OUT/configure-use.log" 2>&1 || { tail -40 "$OUT/configure-use.log"; die "configure USE"; }
grep -E "^(CMAKE_BUILD_TYPE|MUDLET_PGO|CMAKE_INTERPROCEDURAL_OPTIMIZATION|CMAKE_C_FLAGS|CMAKE_CXX_FLAGS|CMAKE_OBJCXX_FLAGS|CMAKE_EXE_LINKER_FLAGS|EXP_FULL_DEBUG|SENTRY_SEND_DEBUG|WITH_SENTRY|BUILD_TESTING|MUDLET_LAYOUT_PAD|USE_SANITIZER)[:=]" "$B/CMakeCache.txt" > "$OUT/cache.txt"
cat "$OUT/cache.txt"

BIN_REL=src/mudlet.app/Contents/MacOS/mudlet
INST=$B/../installers/osx
if [ ! -d "$INST" ]; then
  git clone -q https://github.com/Mudlet/installers.git "$B/../installers" || die "clone installers"
fi
git -C "$B/../installers" rev-parse HEAD > "$OUT/installers-sha.txt"
# make-installer.sh up to the .dmg, which nothing here needs
sed -e '/^# Generate final .dmg/,$d' -e '/npm install -g appdmg/d' "$INST/make-installer.sh" > "$INST/mk-bundle.sh"
chmod +x "$INST/mk-bundle.sh"

HOT='_mudlet_layout_pad|TTrigger5match|TBuffer19translateToPlainText|cTelnet17processSocketData|TLuaInterpreter16callEventHandler|TTrigger17prescanMayFire|TBuffer6append|TTriggerUnit17processDataStream'
for L in 1 2 3; do
  pad=${PADS[$((L - 1))]}
  if [ "$L" != 1 ]; then
    stamp "relink L$L pad $pad"
    cmake -B "$B" -DMUDLET_LAYOUT_PAD="$pad" > "$OUT/configure-L$L.log" 2>&1 || die "configure L$L"
  else
    stamp "build USE (L1)"
  fi
  n0=$(nlines)
  cmake --build "$B" 2>&1 | tee "$OUT/build-L$L.log" | filtered
  [ "${PIPESTATUS[0]}" = 0 ] || { grep -B2 -A12 "FAILED\|error:" "$OUT/build-L$L.log" | head -80; die "build L$L"; }
  nlog "$n0" "$OUT/ninja_log-L$L.txt"
  stamp "built L$L"
  bin=$B/$BIN_REL
  { echo "md5 $(md5 -q "$bin") size $(stat -f %z "$bin")"; size -m "$bin" | grep -E "Section __text|Segment __TEXT"
    grep -E "$HOT" "$B/src/mudlet.map" | head -40
    echo "# first __text symbols"; awk '/^# Symbols:/ {s = 1; next} s && /^0x/ {print; if (++n == 6) exit}' "$B/src/mudlet.map"; } > "$OUT/map-L$L.txt"
  if [ "$L" = 1 ]; then
    ninja -C "$B" -t commands "$BIN_REL" > "$OUT/commands-L1.txt" 2>/dev/null
    dsym=$bin.dSYM
    { echo "# flags over every command that builds Mudlet"
      grep -o -- '-flto=[a-z]*\|-fno-lto\|-fprofile-use=[^ ]*\|-fprofile-generate[^ ]*\|-gline-tables-only\| -g \|-O[0-3s]\|-object_path_lto,[^ ]*\|-cache_path_lto,[^ ]*\|-order_file,[^ ]*' "$OUT/commands-L1.txt" | sort | uniq -c
      echo "# link command"; tail -1 "$OUT/commands-L1.txt" | tr ' ' '\n' | grep -E "^-|\.a$" | grep -v "^-I\|^-D\|^-W[^l]" | tr '\n' ' '; echo
      echo "# dSYM"
      echo "uuid binary: $(dwarfdump --uuid "$bin")"
      echo "uuid dSYM:   $(dwarfdump --uuid "$dsym")"
      echo "dSYM kB $(du -sk "$dsym" | cut -f1)  binary (stripped) bytes $(stat -f %z "$bin")"
      echo "dsymutil warnings in the L1 build: $(grep -c 'warning: (' "$OUT/build-L1.log")"
      grep 'warning: (' "$OUT/build-L1.log" | head -5
      ls -la "$B/src/mudlet-lto" 2>/dev/null | head -4; ls -la "$B/src/mudlet-lto.o" 2>/dev/null
      echo "# atos through the dSYM (map address + 0x10)"
      for sym in TTrigger5match TBuffer19translateToPlainText cTelnet17processSocketData TLuaInterpreter16callEventHandler; do
        a=$(grep -m1 -E "^0x[0-9A-F]+[[:space:]]+0x[0-9A-F]+[[:space:]]+\[ *[0-9]+\] __ZN[0-9]*$sym" "$B/src/mudlet.map" | awk '{print $1}')
        [ -n "$a" ] && echo "$sym $a -> $(atos -o "$dsym" -arch "$ARCH" -l 0x100000000 "$(printf '0x%x' $((a + 16)))")"
      done
      echo "# dwarfdump --statistics (selected)"
      dwarfdump --statistics "$dsym" 2>/dev/null | tr ',' '\n' | grep -E '"#functions"|"#inlined functions"|"#source variables"|"#bytes within functions"' | head -6
    } > "$OUT/check-L1.txt" 2>&1
    cat "$OUT/check-L1.txt"
  fi
  # package a copy the way make-installer.sh does for a development push (snapshot, unsigned)
  app=exp-L$L.app
  rm -rf "${B:?}/$app"
  cp -R "$B/src/mudlet.app" "$B/$app"
  rm -rf "$B/$app/Contents/MacOS/mudlet.dSYM"
  stamp "package L$L"
  (cd "$INST" && BUILD_FOLDER=$B ./mk-bundle.sh "$app") > "$OUT/package-L$L.log" 2>&1 || { tail -30 "$OUT/package-L$L.log"; die "package L$L"; }
  plat=$B/$app/Contents/PlugIns/platforms
  if [ ! -f "$plat/libqoffscreen.dylib" ]; then
    cp "$QT_ROOT_DIR/plugins/platforms/libqoffscreen.dylib" "$plat/"
    for r in $(otool -l "$plat/libqoffscreen.dylib" | awk '/cmd LC_RPATH/ {getline; getline; print $2}'); do
      install_name_tool -delete_rpath "$r" "$plat/libqoffscreen.dylib"
    done
    install_name_tool -add_rpath @executable_path/../Frameworks "$plat/libqoffscreen.dylib"
    echo "added libqoffscreen.dylib: $(otool -L "$plat/libqoffscreen.dylib" | tr '\n' ' ')" >> "$OUT/package-L$L.log"
  fi
  ls "$plat" >> "$OUT/package-L$L.log"
  echo "$V-L$L pad $pad" > "$B/$app/Contents/Resources/exp-variant.txt"
  codesign --force --deep -s - "$B/$app" >> "$OUT/package-L$L.log" 2>&1 || die "ad hoc sign L$L"
  ditto -c -k --keepParent "$B/$app" "$OUT/apps/$V-L$L.zip" || die "zip L$L"
  rm -rf "${B:?}/$app"
  stamp "packaged L$L $(stat -f %z "$OUT/apps/$V-L$L.zip") bytes"
done

python3 "$H/mapdelta.py" "$OUT"/map-L1.txt "$OUT"/map-L2.txt "$OUT"/map-L3.txt | tee "$OUT/mapdelta.txt"
python3 "$H/ninjalog.py" "$OUT/ninja_log-generate.txt" > "$OUT/links-generate.txt"
python3 "$H/ninjalog.py" "$OUT/ninja_log-L1.txt" > "$OUT/links-use.txt"
python3 "$H/ninjalog.py" "$OUT/ninja_log-L2.txt" > "$OUT/links-L2.txt"
head -8 "$OUT/links-generate.txt" "$OUT/links-use.txt" "$OUT/links-L2.txt"

# Without -object_path_lto, does the dSYM lose the LTO code's debug info? (relinks to a scratch file)
if [ "$V" != bp ]; then
  stamp "relink without object_path_lto"
  tmp=$B/noobj
  rm -rf "$tmp"; mkdir -p "$tmp"
  ninja -C "$B" -t commands "$BIN_REL" | tail -1 > "$tmp/link.sh"
  python3 - "$tmp/link.sh" "$tmp" <<'PY'
import re, sys
path, tmp = sys.argv[1], sys.argv[2]
cmd = open(path).read().strip()
if cmd.startswith(": && "):
    cmd = cmd[5:]
cmd = cmd.split(" && ")[0]
cmd = re.sub(r"-Wl,-object_path_lto,\S+", "", cmd)
cmd = re.sub(r"-Wl,-map,\S+", "-Wl,-map," + tmp + "/map.txt", cmd)
cmd = re.sub(r"(^|\s)-o \S+", r"\1-o " + tmp + "/mudlet", cmd, count=1)
open(path, "w").write(cmd + "\n")
PY
  (cd "$B" && sh "$tmp/link.sh") > "$tmp/link.log" 2>&1 || { echo "relink failed"; tail -20 "$tmp/link.log"; }
  dsymutil "$tmp/mudlet" -o "$tmp/mudlet.dSYM" > "$tmp/dsymutil.log" 2>&1
  { echo "without -object_path_lto: dsymutil warnings $(grep -c 'warning' "$tmp/dsymutil.log"), dSYM kB $(du -sk "$tmp/mudlet.dSYM" | cut -f1)"
    head -3 "$tmp/dsymutil.log"
    a=$(grep -m1 -E "\] __ZN[0-9]*TTrigger5match" "$tmp/map.txt" | awk '{print $1}')
    [ -n "$a" ] && echo "atos TTrigger::match: $(atos -o "$tmp/mudlet.dSYM" -arch "$ARCH" -l 0x100000000 "$(printf '0x%x' $((a + 16)))")"
  } 2>&1 | tee "$OUT/sabotage-object_path_lto.txt"
  rm -rf "$tmp"
fi
stamp "done"
kill $MEMPID 2>/dev/null
awk 'NR > 1 { if ($2 != "?" && (min == "" || $2 + 0 < min + 0)) min = $2; if ($3 + 0 > sw) sw = $3 + 0; if ($4 + 0 > ld) ld = $4 + 0; if ($7 + 0 > cl) cl = $7 + 0 }
     END { printf "memory: min free %s%%, max swap used %.0f MB, max single ld RSS %d MB, max clang sum RSS %d MB\n", min, sw, ld, cl }' "$OUT/mem.txt" | tee "$OUT/mem-summary.txt"
