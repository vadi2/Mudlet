#!/bin/bash
# One benchmark job: unpack every app onto this runner, fetch the svof bundle, run drive.sh.
# usage: bench-job.sh <prefix> <rounds> <arms> <offset>
# env: W (work dir), SOURCE (release | run), ZIPS (the build artifacts' zips), RELEASE_DMG, QT_ROOT_DIR
set -u
H="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
prefix=$1 rounds=$2 arms=$3
export OFFSET=$4
mkdir -p "$W/apps" "$W/out"
sudo mdutil -a -i off > /dev/null 2>&1 || true
defaults write -g NSAppSleepDisabled -bool YES
{ sw_vers; echo "ncpu $(sysctl -n hw.ncpu) memsize $(sysctl -n hw.memsize) $(sysctl -n machdep.cpu.brand_string)"; uptime; } | tee "$W/out/runner-$prefix.txt"

add_offscreen() { # <app>: the offscreen platform plugin, which macdeployqt leaves out
  local plat=$1/Contents/PlugIns/platforms
  [ -f "$plat/libqoffscreen.dylib" ] && return
  cp "$QT_ROOT_DIR/plugins/platforms/libqoffscreen.dylib" "$plat/"
  for r in $(otool -l "$plat/libqoffscreen.dylib" | awk '/cmd LC_RPATH/ {getline; getline; print $2}'); do
    install_name_tool -delete_rpath "$r" "$plat/libqoffscreen.dylib"
  done
  install_name_tool -add_rpath @executable_path/../Frameworks "$plat/libqoffscreen.dylib"
  codesign --force --deep -s - "$1"
}

if [ "$SOURCE" = release ]; then
  hdiutil attach -nobrowse -readonly -mountpoint "$W/dmg" "$RELEASE_DMG" > /dev/null
  mkdir -p "$W/apps/bp"
  ls "$W/dmg"
  src=$(ls -d "$W"/dmg/*.app | head -1)
  ditto "$src" "$W/apps/bp/Mudlet.app"
  hdiutil detach "$W/dmg" > /dev/null
  add_offscreen "$W/apps/bp/Mudlet.app"
  otool -L "$W/apps/bp/Mudlet.app/Contents/Frameworks/QtCore.framework/Versions/A/QtCore" | head -3
else
  for z in "$ZIPS"/*.zip; do
    base=$(basename "$z" .zip)
    v=${base%-L*} l=${base##*-L}
    case "$l" in 1) code=$v ;; 2) code=${v}b ;; 3) code=${v}c ;; *) code=$base ;; esac
    mkdir -p "$W/apps/$code"
    ditto -x -k "$z" "$W/apps/$code"
    echo "$code <- $base: $(cat "$W/apps/$code"/*.app/Contents/Resources/exp-variant.txt 2>/dev/null)"
  done
fi
for a in "$W"/apps/*/*.app; do
  codesign -v "$a" 2>&1 | head -2
  ls "$a/Contents/PlugIns/platforms"
done

url=https://github.com/Mudlet/bench-data/releases/download/svof-1/SvofBench.mpackage
sha=1659af70b73c0e74ea5bb0a3cd53b7f5d6f2392f5acc6b1a0ba41c2fab16c980
curl -fsSL --retry 5 -o "$W/SvofBench.mpackage" "$url" || { echo "cannot download $url"; exit 1; }
[ "$(shasum -a 256 "$W/SvofBench.mpackage" | cut -c1-64)" = "$sha" ] || { echo "SvofBench.mpackage SHA-256 mismatch"; exit 1; }
mkdir -p "$W/svof"
unzip -q "$W/SvofBench.mpackage" -d "$W/svof"
export SVOF_BUNDLE=$W/svof

"$H/drive.sh" "$prefix" "$rounds" "$arms"
