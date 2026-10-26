#!/usr/bin/env bash
#
# mayhem/test.sh — behavioral oracle for libsixel: RUNS the prebuilt known-answer probe and the real
# img2sixel CLI (both built with normal flags by mayhem/build.sh) and compares against golden output.
# libsixel's own src/tests suite is broken at HEAD (needs gdk-pixbuf; dither test fails), so it is not used.
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}


KAT=/mayhem/kat
CLI=/mayhem/img2sixel-oracle
[ -x "$KAT" ] && [ -x "$CLI" ] || { echo "missing $KAT or $CLI: mayhem/build.sh did not produce them" >&2; emit_ctrf "libsixel-kat" 0 1; exit 1; }

pass=0; fail=0
check() { # check <name> <expected> <actual>
  if [ "$2" == "$3" ]; then pass=$((pass+1)); echo "ok   $1"
  else fail=$((fail+1)); echo "FAIL $1" >&2; echo "  expected: $2" >&2; echo "  actual:   $3" >&2; fi
}

T="$(mktemp -d "${TMPDIR:-/tmp}/libsixel-test.XXXXXX")"
trap 'rm -rf "$T"' EXIT
ESC=$'\033'

kat_out="$("$KAT" 2>&1)"
check "decode: raw sixel -> indexed pixels + palette" \
  'DECODE w=4 h=6 ncolors=3 row0=0,0,1,1 row5=0,0,1,1 pal0=ff0000 pal1=00ff00' \
  "$(grep '^DECODE' <<<"$kat_out")"
check "encode: RGB pixels + fixed palette -> sixel bytes" \
  'ENCODE \x1bPq"1;1;4;6#0;2;100;0;0#1;2;0;100;0#0~~#1~~\x1b\' \
  "$(grep '^ENCODE' <<<"$kat_out")"
check "roundtrip: encode then decode keeps the quadrant layout" \
  'ROUNDTRIP w=8 h=12 quadrants_flat=1 distinct=4' \
  "$(grep '^ROUNDTRIP' <<<"$kat_out")"

printf 'P3\n2 2\n255\n255 0 0  0 255 0\n0 0 255  255 255 255\n' > "$T/rgb.ppm"
check "img2sixel: 2x2 P3 ppm" \
  "${ESC}Pq\"1;1;2;2#0;2;97;0;0#1;2;0;97;0#2;2;0;0;97#3;2;97;97;97-#0@#1@\$#2A#3A${ESC}\\" \
  "$("$CLI" -p 4 "$T/rgb.ppm")"

printf 'P1\n4 4\n1 0 0 1\n0 1 1 0\n0 1 1 0\n1 0 0 1\n' > "$T/bw.pbm"
check "img2sixel: 4x4 P1 pbm" \
  "${ESC}Pq\"1;1;4;4#0;2;0;0;0#1;2;97;97;97-#0LFEL\$#1AGHA${ESC}\\" \
  "$("$CLI" "$T/bw.pbm")"

printf 'P6\n2 1\n255\n\x10\x20\x30\xf0\xe0\xd0' > "$T/two.ppm"
check "img2sixel: 2x1 P6 ppm, 2-colour palette" \
  "${ESC}Pq\"1;1;2;1#0;2;6;13;19#1;2;94;88;82-#0@#1@${ESC}\\" \
  "$("$CLI" -p 2 "$T/two.ppm")"

emit_ctrf "libsixel-kat" "$pass" "$fail" 0
