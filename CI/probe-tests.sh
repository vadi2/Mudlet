#!/bin/bash
# THROWAWAY measurement probe - never for upstream.
# Times ctest and the busted suite alone and side by side on the runner that just
# built them, and reports wall time and system-wide CPU utilisation for each.
set -u

ncpu=$(nproc 2>/dev/null || sysctl -n hw.ncpu)
WS="${GITHUB_WORKSPACE}"

case "${RUNNER_OS}" in
  Linux)
    CTEST_DIR="${RUNNER_WORKSPACE}/b/ninja"
    MARKER=/tmp/busted-tests-failed
    CTEST_ENV=(QT_QPA_PLATFORM=offscreen MUDLET_MEDIA_TESTS_REQUIRE_PLAYBACK=1)
    LUA_ENV=(AUTORUN_BUSTED_TESTS=true MUDLET_TEST_MODE=1 MUDLET_TEST_REQUIRE_TTS_MOCK=1
      MUDLET_TEST_REQUIRE_HTTP_FIXTURE=1 MUDLET_TEST_REQUIRE_MMCP_PEER=1 MUDLET_TEST_REQUIRE_TELNET_FIXTURE=1
      MUDLET_TEST_REQUIRE_MEDIA=1 MUDLET_TEST_REQUIRE_WINDOW_RESIZE=1 MUDLET_TEST_REQUIRE_DISCORD=1
      "XDG_RUNTIME_DIR=${MUDLET_TEST_DISCORD_RUNTIME_DIR}" "LD_LIBRARY_PATH=${MUDLET_TEST_DISCORD_LIB_PATH}"
      "DBUS_SESSION_BUS_ADDRESS=disabled:" "TESTS_DIRECTORY=${WS}/src/mudlet-lua/tests" QUIT_MUDLET_AFTER_TESTS=true
      "ASAN_OPTIONS=detect_leaks=1:intercept_tls_get_addr=0"
      "LSAN_OPTIONS=suppressions=${WS}/asan-suppressions.txt:exitcode=1:log_threads=1")
    LUA_CMD=(xvfb-run --auto-servernum "${WS}/src/mudlet" --profile "Mudlet self-test" --mirror --offline)
    PROFILE_DIR="${HOME}/.config/mudlet/profiles/Mudlet self-test"
    ;;
  macOS)
    CTEST_DIR="${RUNNER_WORKSPACE}/b/ninja"
    MARKER=/tmp/busted-tests-failed
    CTEST_ENV=(PROBE=1)
    LUA_ENV=(AUTORUN_BUSTED_TESTS=true MUDLET_TEST_MODE=1 MUDLET_TEST_REQUIRE_TTS_MOCK=1
      MUDLET_TEST_REQUIRE_HTTP_FIXTURE=1 MUDLET_TEST_REQUIRE_MMCP_PEER=1 MUDLET_TEST_REQUIRE_TELNET_FIXTURE=1
      "TESTS_DIRECTORY=${WS}/src/mudlet-lua/tests" QUIT_MUDLET_AFTER_TESTS=true ASAN_OPTIONS=detect_leaks=0)
    LUA_CMD=("${HOME}/Desktop/Mudlet.app/Contents/MacOS/mudlet" --profile "Mudlet self-test" --mirror --offline)
    PROFILE_DIR="${HOME}/.config/mudlet/profiles/Mudlet self-test"
    ;;
  Windows)
    CTEST_DIR="${WS}/build-${MSYSTEM}/test"
    MARKER="${TEMP}/busted-tests-failed"
    CTEST_ENV=(MUDLET_MEDIA_TESTS_REQUIRE_PLAYBACK=1)
    LUA_ENV=(AUTORUN_BUSTED_TESTS=true MUDLET_TEST_MODE=1 MUDLET_TEST_REQUIRE_TTS_MOCK=1
      MUDLET_TEST_REQUIRE_HTTP_FIXTURE=1 QUIT_MUDLET_AFTER_TESTS=true)
    LUA_CMD=("${WS}/build-${MSYSTEM}/release/mudlet.exe" --profile "Mudlet self-test" --offline)
    PROFILE_DIR="$(cygpath -u "${USERPROFILE}")/.config/mudlet/profiles/Mudlet self-test"
    ;;
esac

cpu_snap() {
  if [ -r /proc/stat ]; then
    awk '/^cpu /{print $2+$3+$4+$7+$8, $5+$6}' /proc/stat
  else
    python3 -c 'import psutil;t=psutil.cpu_times();print(t.user+t.system+getattr(t,"nice",0), t.idle)' 2>/dev/null || echo "0 0"
  fi
}
util() { awk -v b0="$1" -v i0="$2" -v b1="$3" -v i1="$4" 'BEGIN{d=(b1-b0)+(i1-i0); if(d>0) printf "%.0f", 100*(b1-b0)/d; else print "?"}'; }

mem_start() {
  rm -f /tmp/probe-mem
  if [ -r /proc/meminfo ] && command -v free >/dev/null; then
    ( while :; do free -m | awk '/^Mem:/{print $3}' >> /tmp/probe-mem; sleep 2; done ) &
    MEMPID=$!
  else
    MEMPID=""
  fi
}
mem_stop() {
  [ -n "${MEMPID}" ] && kill "${MEMPID}" 2>/dev/null
  [ -s /tmp/probe-mem ] && sort -n /tmp/probe-mem | tail -1 || echo "?"
}

run_ctest() { # label j
  local log="/tmp/probe-ctest-$1.log"
  ( cd "${CTEST_DIR}" && env "${CTEST_ENV[@]}" ctest --output-on-failure -j "$2" > "${log}" 2>&1 )
  local rc=$?
  echo "PROBE $1 ctest rc=${rc} $(grep -E 'tests passed' "${log}" | tail -1) $(grep -E 'Total Test time' "${log}" | tail -1)"
  grep -E '\*\*\*Failed|\*\*\*Timeout|Exception|Not Run' "${log}" | head -20
  grep -E 'Test +#[0-9]+: ' "${log}" | sed -E 's/.* ([A-Za-z0-9_]+) \.+.* ([0-9.]+) sec.*/\2 \1/' | sort -rn | head -8 | sed 's/^/PROBE   slow /'
}

run_lua() { # label
  local log="/tmp/probe-lua-$1.log"
  rm -f "${MARKER}"
  rm -rf "${PROFILE_DIR}"
  if [ "${RUNNER_OS}" = "Windows" ]; then
    env "${LUA_ENV[@]}" "TESTS_DIRECTORY=${TESTS_DIRECTORY_WIN}" "${LUA_CMD[@]}" > "${log}" 2>&1
  else
    env "${LUA_ENV[@]}" "${LUA_CMD[@]}" > "${log}" 2>&1
  fi
  local rc=$?
  local failed=no
  [ -e "${MARKER}" ] && failed=yes
  echo "PROBE $1 lua rc=${rc} marker=${failed} $(grep -aE 'successes / ' "${log}" | tail -1 | sed -E 's/.*Mudlet self-test\| //')"
  grep -aE 'byte\(s\) leaked|ERROR: (Address|Leak)Sanitizer' "${log}" | head -5
  if [ "${failed}" = yes ] || [ "${rc}" != 0 ]; then
    grep -aE '(Failure|Error) → |✱|FAILED|Failure ->|Error ->' "${log}" | head -30
  fi
}

measure() { # label, function args...
  local label=$1; shift
  local b0 i0 b1 i1 s e
  read -r b0 i0 < <(cpu_snap)
  mem_start
  s=$(date +%s)
  "$@"
  e=$(date +%s)
  local peak; peak=$(mem_stop)
  read -r b1 i1 < <(cpu_snap)
  echo "PROBE == ${label}: wall=$((e - s))s cpu=$(util "$b0" "$i0" "$b1" "$i1")% peak_mem_used=${peak}MB (ncpu=${ncpu})"
}

both() { # j
  local s; s=$(date +%s)
  ( run_lua "conc-j$1"; echo "PROBE   lua finished after $(( $(date +%s) - s ))s" ) &
  local lpid=$!
  run_ctest "conc-j$1" "$1"
  echo "PROBE   ctest finished after $(( $(date +%s) - s ))s"
  wait "${lpid}"
}

echo "PROBE runner: $(uname -a) ncpu=${ncpu}"
[ -r /proc/meminfo ] && grep MemTotal /proc/meminfo
measure "ctest -j${ncpu} alone" run_ctest "j${ncpu}" "${ncpu}"
measure "ctest -j$((ncpu * 2)) alone" run_ctest "j$((ncpu * 2))" "$((ncpu * 2))"
measure "lua alone" run_lua alone
measure "lua || ctest -j${ncpu}" both "${ncpu}"
measure "lua || ctest -j$((ncpu * 2))" both "$((ncpu * 2))"
measure "ctest -j$((ncpu * 3)) alone" run_ctest "j$((ncpu * 3))" "$((ncpu * 3))"
exit 0
