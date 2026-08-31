#!/usr/bin/env bash
#
# Rebuild the libbluray BD-J jars inside a j2re-image from patched libbluray
# sources, recompiling only the classes our patches touch.
#
# WHY THIS EXISTS
# ---------------
# j2re-image/ ships two prebuilt jars, libbluray-j2se-<ver>.jar and
# libbluray-awt-j2se-<ver>.jar.  They are what the BD-J VM actually loads
# (bdj.c: -Xbootclasspath/p:), and they are NOT produced by our build - the
# workstation gets them from the script.service.jre zip, the Docker builder
# builds them once with ant in a JDK 8 stage from a *pristine* tarball.  So a
# patch under tools/depends/target/libbluray/ that touches bdj/java/** does not
# reach the APK on its own: the stock classes keep being used and the patch is
# silently inert.  That is the same trap documented in FindBluray.cmake for
# libbluray.a, one layer up.
#
# Run this after adding or changing any patch that touches bdj/java/**.
#
#   JAVA8_HOME=/path/to/jdk8 \
#   [JRE_IMAGE_DIR=/path/to/j2re-image] \
#   [PATCH_DIR=/path/to/tools/depends/target/libbluray] \
#     rebuild-bdj-jars.sh [<patched-libbluray-source-root>]
#
# The source root is anything above bdj/java (the extracted tarball, or the
# in-source build dir of the internal cmake libbluray target).  With no argument
# the depends tree next to this script is used.
#
# JDK 8 is required: these classes override java.* packages, so --release is not
# usable, and the bundled JRE is Java 8 so nothing newer may be targeted.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
JRE="${JRE_IMAGE_DIR:-$HERE/j2re-image}"
PATCH_DIR="${PATCH_DIR:-$HERE/../../../depends/target/libbluray}"
ROOT="${1:-}"

find_bdj() {
  # echo the directory that contains bdj/java.  The layout differs between the
  # depends tree (<target>/<platform>/src/libbluray) and the internal cmake
  # ExternalProject, so search rather than assume.
  local base="$1" d
  d=$(find "$base" -maxdepth 8 -type d -path '*/bdj/java' -print -quit 2>/dev/null) || true
  [ -n "$d" ] || return 1
  echo "${d%/bdj/java}"
}

[ -n "$ROOT" ] || ROOT="$HERE/../../../depends/target/libbluray"
SRC="$(find_bdj "$ROOT")" || {
  echo "no bdj/java under $ROOT - pass the patched libbluray source root" >&2; exit 1; }

JAVAC="${JAVA8_HOME:-}/bin/javac"
[ -x "$JAVAC" ] || { echo "set JAVA8_HOME to a JDK 8 (need javac 1.8)" >&2; exit 1; }
JAR="${JAVA8_HOME}/bin/jar"

VER=$(ls "$JRE" 2>/dev/null | sed -n 's/^libbluray-j2se-\(.*\)\.jar$/\1/p' | head -1)
[ -n "$VER" ] || { echo "no libbluray-j2se-*.jar in $JRE" >&2; exit 1; }
MAIN="$JRE/libbluray-j2se-$VER.jar"
AWT="$JRE/libbluray-awt-j2se-$VER.jar"
[ -w "$MAIN" ] && [ -w "$AWT" ] || { echo "$JRE jars are not writable" >&2; exit 1; }

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/out"

# Only recompile the classes our patches actually touch, and splice them into
# the shipped jars.  Compiling the whole bdj tree does not work here: build.xml
# deliberately excludes java/awt/event/FocusEvent and sun/awt/CausedFocusEvent
# from the jars, and the bundled Java 8 rt.jar has no FocusEvent.Cause, so a
# full build fails on classes we are not changing.
grep -h '^+++ b/' "$PATCH_DIR"/*.patch 2>/dev/null \
  | sed 's|^+++ b/||;s|[[:space:]].*$||' \
  | grep -E '^src/libbluray/bdj/java.*\.java$' \
  | sed 's|^src/libbluray/||' \
  | sort -u \
  | while read -r rel; do [ -f "$SRC/$rel" ] && echo "$SRC/$rel"; done > "$TMP/srcs"

if [ ! -s "$TMP/srcs" ]; then
  echo "no patched BD-J java files found - nothing to rebuild"; exit 0
fi
echo "recompiling $(wc -l < "$TMP/srcs") patched BD-J class(es) from $SRC"

# rt.jar from the bundled JRE plus the shipped jars is the right baseline: only
# the patched classes are rebuilt, everything they reference stays as built.
"$JAVAC" -nowarn -encoding UTF-8 \
  -bootclasspath "$JRE/lib/rt.jar:$MAIN:$AWT" \
  -d "$TMP/out" @"$TMP/srcs"

cd "$TMP/out"
# java/awt/** belongs to the awt jar, everything else to the main jar (bdj/build.xml)
AWTC=$(find java/awt -name '*.class' 2>/dev/null || true)
REST=$(find . -name '*.class' ! -path './java/awt/*' | sed 's|^\./||')
[ -n "$AWTC" ] && "$JAR" uf "$AWT"  $AWTC
[ -n "$REST" ] && "$JAR" uf "$MAIN" $REST
echo "rebuilt BD-J jars in $JRE (awt=$(echo $AWTC | wc -w) main=$(echo $REST | wc -w) classes)"
