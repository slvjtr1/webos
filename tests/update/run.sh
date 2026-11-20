#!/bin/bash
# tests/update/run.sh : which update is offered (fixed data); the real update callbacks with a fake network and installer
# (flow.c); then tools/release.sh into throwaway site folders: a stable release, then a beta, which must leave every
# stable file byte for byte as it was, and every refused command, which must change nothing at all (site tree,
# pkg/appinfo.json, dist/). No network, no live site, no build.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; mkdir -p "$ROOT/build"; cd "$ROOT"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -O1 -g ${SB_ASAN:+-fsanitize=address,undefined} -Isrc -Ithird_party/cjson \
  tests/update/test.c src/core/updpick.c third_party/cjson/cJSON.c -o build/update_test
./build/update_test

# the update callbacks (src/core/update.c needs the SDL/GLES headers: the system's, or HOSTINC=<dir with SDL2/ GLES2/ KHR/>)
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -O1 -g ${SB_ASAN:+-fsanitize=address,undefined} -Isrc -Ithird_party/cjson \
  ${HOSTINC:+-isystem "$HOSTINC"} tests/update/flow.c src/core/update.c src/core/updpick.c third_party/cjson/cJSON.c -o build/update_flow
./build/update_flow 2>/dev/null

T="$ROOT/build/update_site"; rm -rf "$T"; mkdir -p "$T/site" "$T/ipk"
ID=tv.stremioorigin.app
pk() { printf '%s' "$2" > "$T/ipk/${ID}_$1_arm.ipk"; echo "$T/ipk/${ID}_$1_arm.ipk"; }
rel() { RELEASE_IPK="$1" bash tools/release.sh "${@:2}"; }
RELEASE_IPK="$(pk 1.9.0 'stable package')" bash tools/release.sh 1.9.0 "$T/site" >/dev/null
sums() { (cd "$T/site" && find . -type f ! -path './api/apps/*/beta/*' | sort | xargs sha256sum); }
sums > "$T/before"
rel "$(pk 2.0.0 'beta package')" 2.0.0 "$T/site" --beta >/dev/null
sums > "$T/after"
if diff -q "$T/before" "$T/after" >/dev/null; then echo "release.sh --beta: stable files unchanged ($(wc -l < "$T/before") files)"; else echo "FAIL: release.sh --beta changed stable files"; diff "$T/before" "$T/after"; exit 1; fi
B="$T/site/api/apps/$ID/beta/manifest.json"
[ -f "$B" ] || { echo "FAIL: no beta manifest"; exit 1; }
grep -q "$(sha256sum "$T/ipk/${ID}_2.0.0_arm.ipk" | cut -d' ' -f1)" "$B" || { echo "FAIL: beta manifest without the package's SHA-256"; exit 1; }
./build/update_test "$T/site/api/apps/$ID/manifest.json" "$B"

# ---- refused commands change nothing ----
# everything a release could touch: every file and folder under the site, pkg/appinfo.json, dist/
snap() { { (cd "$1" 2>/dev/null && find . | sort && find . -type f | sort | xargs -r sha256sum) || echo "(no site folder)";
           sha256sum pkg/appinfo.json; (find dist -type f 2>/dev/null | sort | xargs -r sha256sum) || true; } > "$2"; }
refused() { # refused <description> <site> <command...>: must fail, and leave the snapshot as it was
  local what="$1" site="$2"; shift 2
  snap "$site" "$T/s0"
  if "$@" >"$T/out" 2>&1; then echo "FAIL: accepted: $what"; cat "$T/out"; exit 1; fi
  snap "$site" "$T/s1"
  diff -q "$T/s0" "$T/s1" >/dev/null || { echo "FAIL: refused but changed files: $what"; diff "$T/s0" "$T/s1"; exit 1; }
  grep -qiE "usage|unknown|must|newer|older|already|version" "$T/out" || { echo "FAIL: refused without a reason: $what"; cat "$T/out"; exit 1; }
  n=$((n + 1)); }
n=0
S="$T/s2/site"; mkdir -p "$T/s2"
rel "$(pk 1.0.0 'stable 1.0.0')" 1.0.0 "$S" >/dev/null
rel "$(pk 2.0.0 'beta 2.0.0')" 2.0.0 "$S" --beta >/dev/null
pk 1.5.0 'beta 1.5.0' >/dev/null; pk 0.9.0 'old' >/dev/null; pk 1.0.0 'stable 1.0.0' >/dev/null; pk 2.0.0 'beta 2.0.0' >/dev/null
for build in ipk none; do # with RELEASE_IPK, and the normal path (which would set the version and build) without it
  r() { if [ $build = ipk ]; then local v="${1:-}"; [[ "$v" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || v=1.0.0; RELEASE_IPK="$T/ipk/${ID}_${v}_arm.ipk" bash tools/release.sh "$@"; else env -u RELEASE_IPK bash tools/release.sh "$@"; fi; }
  refused "$build: unknown option --btea" "$S" r 2.1.0 "$S" --btea
  refused "$build: unknown option before the version" "$S" r --stable 2.1.0 "$S"
  refused "$build: an extra argument" "$S" r 2.1.0 "$S" extra
  refused "$build: no version" "$S" r
  refused "$build: a beta below the current beta (1.5.0 --beta, beta 2.0.0)" "$S" r 1.5.0 "$S" --beta
  refused "$build: a beta equal to the stable" "$S" r 1.0.0 "$S" --beta
  refused "$build: a stable below the current stable" "$S" r 0.9.0 "$S"
  refused "$build: a version with a leading zero" "$S" r 01.2.0 "$S"
  refused "$build: a version part over 5 digits" "$S" r 100000.0.0 "$S"
  refused "$build: two parts" "$S" r 2.1 "$S"
  refused "$build: four parts" "$S" r 2.1.0.1 "$S"
  refused "$build: a site folder that does not exist yet, unknown option" "$T/nosite" r 2.1.0 "$T/nosite" --btea
done
pk 1.0.0 'another 1.0.0' >/dev/null
refused "the same stable version with another package" "$S" rel "$T/ipk/${ID}_1.0.0_arm.ipk" 1.0.0 "$S"
pk 2.0.0 'another 2.0.0' >/dev/null
refused "the same beta version with another package" "$S" rel "$T/ipk/${ID}_2.0.0_arm.ipk" 2.0.0 "$S" --beta
refused "the same stable version without a package (would rebuild under an old number)" "$S" env -u RELEASE_IPK bash tools/release.sh 1.0.0 "$S"
echo "release.sh: $n refused commands, each changed nothing (site tree, pkg/appinfo.json, dist/)"

# ---- the same release again: nothing to do, nothing changed ----
pk 1.0.0 'stable 1.0.0' >/dev/null; pk 2.0.0 'beta 2.0.0' >/dev/null
snap "$S" "$T/s0"
rel "$T/ipk/${ID}_1.0.0_arm.ipk" 1.0.0 "$S" | grep -q "nothing to do" || { echo "FAIL: identical stable rerun"; exit 1; }
rel "$T/ipk/${ID}_2.0.0_arm.ipk" 2.0.0 "$S" --beta | grep -q "nothing to do" || { echo "FAIL: identical beta rerun"; exit 1; }
snap "$S" "$T/s1"; diff -q "$T/s0" "$T/s1" >/dev/null || { echo "FAIL: an identical rerun changed files"; exit 1; }
echo "release.sh: the same package again: nothing to do, nothing changed"

# ---- a newer beta, then the beta promoted to stable ----
M="$S/api/apps/$ID"
rel "$(pk 2.0.1 'beta 2.0.1')" 2.0.1 "$S" --beta >/dev/null
grep -q '"2.0.1"' "$M/beta/manifest.json" || { echo "FAIL: newer beta not written"; exit 1; }
rel "$(pk 2.0.1 'beta 2.0.1')" 2.0.1 "$S" >/dev/null
[ ! -e "$M/beta/manifest.json" ] || { echo "FAIL: the promoted beta's manifest is still there"; exit 1; }
for f in "$M/manifest.json" "$M/releases/latest.json" "$S/apps.json" "$S/api/apps.json"; do grep -q '"2.0.1"' "$f" || { echo "FAIL: $f not at 2.0.1"; exit 1; }; done
grep -q "$(sha256sum "$T/ipk/${ID}_2.0.1_arm.ipk" | cut -d' ' -f1)" "$M/manifest.json" || { echo "FAIL: promoted stable without the package's SHA-256"; exit 1; }
# a stable above a beta replaces it too; a beta below a newer stable is refused
rel "$(pk 2.1.0 'beta 2.1.0')" 2.1.0 "$S" --beta >/dev/null
rel "$(pk 2.2.0 'stable 2.2.0')" 2.2.0 "$S" >/dev/null
[ ! -e "$M/beta/manifest.json" ] || { echo "FAIL: a beta below the new stable is still offered"; exit 1; }
refused "a beta below the new stable" "$S" rel "$(pk 2.1.5 'beta 2.1.5')" 2.1.5 "$S" --beta
# every version written is one the app reads
for f in $(find "$T" -name '*.json' -path '*api/apps*' ! -name apps.json); do
  v=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$f" | head -1)
  [[ "$v" =~ ^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$ ]] || { echo "FAIL: $f has version '$v'"; exit 1; }; done
echo "release.sh: newer beta, promotion to stable (beta manifest removed), versions all readable"
