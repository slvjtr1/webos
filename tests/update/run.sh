#!/bin/bash
# tests/update/run.sh : which update is offered (fixed data), then tools/release.sh into a throwaway site folder: a
# stable release, then a beta, which must leave every stable file byte for byte as it was. No network, no live site.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; mkdir -p "$ROOT/build"; cd "$ROOT"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -O1 -g ${SB_ASAN:+-fsanitize=address,undefined} -Isrc -Ithird_party/cjson \
  tests/update/test.c src/core/updpick.c third_party/cjson/cJSON.c -o build/update_test
./build/update_test

T="$ROOT/build/update_site"; rm -rf "$T"; mkdir -p "$T/site" "$T/ipk"
ID=tv.stremioorigin.app
printf 'stable package' > "$T/ipk/${ID}_1.9.0_arm.ipk"; printf 'beta package' > "$T/ipk/${ID}_2.0.0_arm.ipk"; printf 'old beta' > "$T/ipk/${ID}_1.8.0_arm.ipk"
RELEASE_IPK="$T/ipk/${ID}_1.9.0_arm.ipk" bash tools/release.sh 1.9.0 "$T/site" >/dev/null
sums() { (cd "$T/site" && find . -type f ! -path './api/apps/*/beta/*' | sort | xargs sha256sum); }
sums > "$T/before"
RELEASE_IPK="$T/ipk/${ID}_2.0.0_arm.ipk" bash tools/release.sh 2.0.0 "$T/site" --beta >/dev/null
sums > "$T/after"
if diff -q "$T/before" "$T/after" >/dev/null; then echo "release.sh --beta: stable files unchanged ($(wc -l < "$T/before") files)"; else echo "FAIL: release.sh --beta changed stable files"; diff "$T/before" "$T/after"; exit 1; fi
B="$T/site/api/apps/$ID/beta/manifest.json"
[ -f "$B" ] || { echo "FAIL: no beta manifest"; exit 1; }
grep -q "$(sha256sum "$T/ipk/${ID}_2.0.0_arm.ipk" | cut -d' ' -f1)" "$B" || { echo "FAIL: beta manifest without the package's SHA-256"; exit 1; }
# a beta older than the stable release is refused, and writes nothing
if RELEASE_IPK="$T/ipk/${ID}_1.8.0_arm.ipk" bash tools/release.sh 1.8.0 "$T/site" --beta >/dev/null 2>&1; then echo "FAIL: a beta older than the stable release was written"; exit 1; fi
sums > "$T/after2"; diff -q "$T/before" "$T/after2" >/dev/null || { echo "FAIL: the refused beta changed stable files"; exit 1; }
grep -q '"2.0.0"' "$B" || { echo "FAIL: the refused beta replaced the beta manifest"; exit 1; }
echo "release.sh --beta: an older beta refused"
./build/update_test "$T/site/api/apps/$ID/manifest.json" "$B"
