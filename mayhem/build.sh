#!/usr/bin/env bash
#
# mayhem/build.sh — build libsixel's fuzz harnesses and the behavioral-oracle probes.
# Runs inside the commit image as `mayhem` in /mayhem (see mayhem/Dockerfile). Re-runnable and
# offline: every dependency is the in-tree stb_image / built-in loaders (no libpng/jpeg/curl/gd).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

CONFIGURE_OPTS="--disable-shared --enable-static --disable-python --without-libcurl --without-jpeg --without-png --without-gd --without-gdk-pixbuf2"

# 1) Oracle build (project's NORMAL flags, no sanitizer, no DWARF override), done in a scratch copy of
#    the tracked tree so the sanitized in-tree build below never sees its objects.
#    Produces /mayhem/kat (known-answer probe) and /mayhem/img2sixel-oracle (the real CLI).
ORACLE_DIR="${TMPDIR:-/tmp}/libsixel-oracle"
rm -rf "$ORACLE_DIR"
mkdir -p "$ORACLE_DIR"
git ls-files -z | tar --null -T - -cf - | tar -xf - -C "$ORACLE_DIR"
(
  cd "$ORACLE_DIR"
  env -u CFLAGS -u LDFLAGS ./configure CC="$CC" CFLAGS="-O2 $COVERAGE_FLAGS" LDFLAGS="$COVERAGE_FLAGS" $CONFIGURE_OPTS >/dev/null
  make -j"$MAYHEM_JOBS" -C include >/dev/null
  make -j"$MAYHEM_JOBS" -C src libsixel.la >/dev/null
  make -j"$MAYHEM_JOBS" -C converters img2sixel >/dev/null
  $CC -O2 $COVERAGE_FLAGS -Iinclude "$SRC/mayhem/kat.c" src/.libs/libsixel.a -lm -o /mayhem/kat
  cp converters/img2sixel /mayhem/img2sixel-oracle
)
rm -rf "$ORACLE_DIR"

# 2) Sanitized, SanCov-instrumented library, built in-tree (every generated file is covered by the
#    upstream .gitignore). -fsanitize=fuzzer-no-link gives the library edge coverage.
if [ -f Makefile ]; then make distclean >/dev/null 2>&1 || true; fi
env -u CFLAGS -u LDFLAGS ./configure CC="$CC" \
    CFLAGS="$SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS -O1" \
    LDFLAGS="$SANITIZER_FLAGS" $CONFIGURE_OPTS >/dev/null
make -j"$MAYHEM_JOBS" -C include >/dev/null
make -j"$MAYHEM_JOBS" -C src libsixel.la >/dev/null
LIBSIXEL="$SRC/src/.libs/libsixel.a"

# 3) Harnesses, each twice: libFuzzer binary + standalone run-once reproducer. LeakSanitizer is
#    turned off at build time through mayhem/lsan_off.cc.
LSAN_OBJ="$(mktemp -d)/lsan_off.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$LSAN_OBJ"
for t in fuzz_decode fuzz_load fuzz_encode; do
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE \
      "$SRC/mayhem/$t.c" -I"$SRC/include" "$LIBSIXEL" -lm "$LSAN_OBJ" -o "/mayhem/$t"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS "$STANDALONE_FUZZ_MAIN" \
      "$SRC/mayhem/$t.c" -I"$SRC/include" "$LIBSIXEL" -lm "$LSAN_OBJ" -o "/mayhem/$t-standalone"
done
