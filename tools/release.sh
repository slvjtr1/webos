#!/bin/bash
# A release in one step:  tools/release.sh VERSION [site-dir] [--beta]
# --beta: a pre-release. Only api/apps/<id>/beta/manifest.json is written (marked "channel": "beta"); the stable files
#   (manifest.json, releases/latest.json, apps.json, api/apps.json, icon, description) are left exactly as they are,
#   so builds that know no beta channel never see it. Publish it on GitHub as a pre-release (gh release create --prerelease).
# RELEASE_IPK=<file>: use that package as it is (no version change, no build), for checks of the site files.
# Sets the version (pkg/appinfo.json; the What's new card keeps its own: a new card, a new card version), builds the IPK
# (tools/package.sh) and writes the Homebrew Channel files into the site folder, the GitHub Pages
# repository users add to the Homebrew Channel:
#   <id>_<version>_arm.ipk, icon.png, api/apps/<id>/manifest.json (+ releases/latest.json,
#   full_description.html) and the repository list (apps.json + api/apps.json).
# The app's own update check reads the same manifest (src/core/update.c). No git, no push: review the
# site folder, then commit and push it yourself.
#
# Versions (checked before anything is changed: no folder made, no pkg/appinfo.json edit, no build, no site file):
#   x.y.z, each part 0 or 1-99999 without a leading zero (what the app's update check reads, src/core/updpick.c).
#   Stable: above the current stable release. Beta: above the current stable and the current beta.
#   The same version again: nothing to do when it is the very same package (RELEASE_IPK with the same SHA-256);
#   refused otherwise (the app compares versions by number: a new package under an old number never reaches anyone).
#   A stable release at or above the beta's version replaces it: the beta manifest is removed (promoted).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
USAGE="usage: tools/release.sh VERSION [site-dir] [--beta]"
BETA=0; ARGS=()
for a in "$@"; do
  case "$a" in
    --beta) BETA=1 ;;
    -*) echo "unknown option: $a"; echo "$USAGE"; exit 2 ;;
    *) ARGS+=("$a") ;;
  esac
done
[ "${#ARGS[@]}" -ge 1 ] && [ "${#ARGS[@]}" -le 2 ] || { echo "$USAGE"; exit 2; }
VER="${ARGS[0]}"
SITE="${ARGS[1]:-$ROOT/site}"
ID=tv.stremioorigin.app
BASE="https://slvjtr1.github.io/webos"
SRC="https://github.com/slvjtr1/webos"
P='(0|[1-9][0-9]{0,4})'
[[ "$VER" =~ ^$P\.$P\.$P$ ]] || { echo "version must look like 1.0.1 (numbers without leading zeros, at most 5 digits each)"; exit 2; }
sha_of() { (command -v sha256sum >/dev/null && sha256sum "$1" || shasum -a 256 "$1") | cut -d' ' -f1; }
PRESHA=""
if [ -n "${RELEASE_IPK:-}" ]; then
  [ "$(basename "$RELEASE_IPK")" = "${ID}_${VER}_arm.ipk" ] || { echo "RELEASE_IPK must be named ${ID}_${VER}_arm.ipk"; exit 2; }
  [ -f "$RELEASE_IPK" ] || { echo "no $RELEASE_IPK"; exit 2; }
  PRESHA=$(sha_of "$RELEASE_IPK")
fi

# the channel's versions, before anything changes (exit 3: the same package is already released there)
set +e
python3 - "$SITE" "$ID" "$VER" "$BETA" "$PRESHA" <<'EOF'
import json, os, sys
site, pid, ver, beta, sha = sys.argv[1:]
d = os.path.join(site, 'api', 'apps', pid)
def read(p):
    if not os.path.exists(p): return None, None
    try:
        m = json.load(open(p, encoding='utf-8'))
        return tuple(int(x) for x in m['version'].split('.')), m.get('ipkHash', {}).get('sha256')
    except Exception as e:
        sys.exit('cannot read %s (%s): fix it before releasing' % (p, e))
sv, ssha = read(os.path.join(d, 'manifest.json'))
bv, bsha = read(os.path.join(d, 'beta', 'manifest.json'))
v = tuple(int(x) for x in ver.split('.'))
show = lambda t: '.'.join(map(str, t))
def same(cur, cursha, chan):
    if sha and cursha == sha: print('%s %s is already released with this very package: nothing to do' % (chan, ver)); sys.exit(3)
    sys.exit('%s %s is already released%s: the version must go up' % (chan, ver, '' if sha else ' (to repeat it, pass the same package as RELEASE_IPK)'))
if beta == '1':
    if sv and v <= sv: sys.exit('beta %s is not newer than the stable %s' % (ver, show(sv)))
    if bv and v < bv: sys.exit('beta %s is older than the current beta %s' % (ver, show(bv)))
    if bv and v == bv: same(bv, bsha, 'beta')
else:
    if sv and v < sv: sys.exit('stable %s is older than the current stable %s' % (ver, show(sv)))
    if sv and v == sv: same(sv, ssha, 'stable')
EOF
rc=$?
set -e
[ $rc = 3 ] && exit 0
[ $rc = 0 ] || exit 2
mkdir -p "$SITE"

if [ -n "${RELEASE_IPK:-}" ]; then IPK="$RELEASE_IPK"
else
python3 - "$ROOT" "$VER" <<'EOF'
import re, sys
root, ver = sys.argv[1], sys.argv[2]
for f in ('pkg/appinfo.json',): # the version alone, the file as it is written (the What's new card keeps its own version: it is keyed by it, so a fix release with the same card never shows it twice)
    p = root + '/' + f; t = open(p, encoding='utf-8').read(); n = re.subn(r'("version"\s*:\s*")[^"]*(")', r'\g<1>' + ver + r'\g<2>', t, count=1)
    if n[1] != 1: sys.exit('no version in ' + f)
    open(p, 'w', encoding='utf-8').write(n[0])
EOF

"$ROOT/tools/build.sh" webos # always built now: the version just set is the one inside, and its debug build is kept below
rm -f "$ROOT/dist/${ID}_${VER}_arm.ipk"
"$ROOT/tools/package.sh" >/dev/null
IPK="$ROOT/dist/${ID}_${VER}_arm.ipk"
mkdir -p "$ROOT/dist/dbg/$VER" && cp "$ROOT/build/webos/stremioorigin.dbg" "$ROOT/dist/dbg/$VER/" # kept per version: a shared crash report names its code from it (getlogs)
fi
[ -f "$IPK" ] || { echo "no $IPK"; exit 1; }
SHA=$(sha_of "$IPK")

[ "$BETA" = 1 ] || cp "$ROOT/pkg/largeIcon.png" "$SITE/icon.png"
python3 - "$SITE" "$ID" "$VER" "$SHA" "$BASE" "$SRC" "$ROOT/tools/homebrew" "$BETA" <<'EOF'
import json, os, sys
site, pid, ver, sha, base, src, hb, beta = sys.argv[1:]
# the Homebrew Channel listing text, approved as written: tools/homebrew/ (the one place it lives, so it cannot drift)
short = open(os.path.join(hb, 'shortDescription.txt'), encoding='utf-8').read().strip()
full = open(os.path.join(hb, 'full_description.html'), encoding='utf-8').read()
manifest = {
  'id': pid, 'version': ver, 'type': 'native', 'title': 'Stremio Origin',
  'appDescription': 'A native app for Stremio addons, written in C. You bring the account and addons; it ships empty. 28 languages. Unofficial.',
  'iconUri': base + '/icon.png', 'sourceUrl': src, 'rootRequired': False,
  'ipkUrl': '%s/releases/download/v%s/%s_%s_arm.ipk' % (src, ver, pid, ver), 'ipkHash': {'sha256': sha}}
d = os.path.join(site, 'api', 'apps', pid)
if beta == '1':
    # the beta's own file, nothing else: the stable channel stays as the last stable release left it
    manifest['channel'] = 'beta'
    os.makedirs(os.path.join(d, 'beta'), exist_ok=True)
    open(os.path.join(d, 'beta', 'manifest.json'), 'w').write(json.dumps(manifest, indent=2) + '\n')
    print('beta manifest written:', pid, ver, sha, '(stable files untouched)')
    sys.exit(0)
os.makedirs(os.path.join(d, 'releases'), exist_ok=True)
for f in (os.path.join(d, 'manifest.json'), os.path.join(d, 'releases', 'latest.json')):
    open(f, 'w').write(json.dumps(manifest, indent=2) + '\n')
listing = {'paging': {'page': 1, 'count': 1, 'maxPage': 1, 'itemsTotal': 1}, 'packages': [{
  'id': pid, 'title': 'Stremio Origin', 'iconUri': base + '/icon.png', 'pool': 'main',
  'manifestUrl': '%s/api/apps/%s/manifest.json' % (base, pid), 'shortDescription': short,
  'fullDescriptionUrl': 'apps/%s/full_description.html' % pid, 'manifest': manifest}]}
open(os.path.join(site, 'api', 'apps.json'), 'w').write(json.dumps(listing, indent=2) + '\n')
listing['packages'][0]['fullDescriptionUrl'] = 'api/' + listing['packages'][0]['fullDescriptionUrl'] # the same list at the root: paths from there
open(os.path.join(site, 'apps.json'), 'w').write(json.dumps(listing, indent=2) + '\n')
open(os.path.join(d, 'full_description.html'), 'w', encoding='utf-8').write(full)
# promoted: a beta at or below this stable release is replaced by it
bp = os.path.join(d, 'beta', 'manifest.json')
if os.path.exists(bp):
    bv = tuple(int(x) for x in json.load(open(bp, encoding='utf-8'))['version'].split('.'))
    if bv <= tuple(int(x) for x in ver.split('.')): os.remove(bp); print('beta', '.'.join(map(str, bv)), 'replaced by this stable release: beta manifest removed')
print('site updated:', pid, ver, sha)
EOF
if [ "$BETA" = 1 ]; then echo "upload $IPK to the GitHub PRE-release v$VER (gh release create v$VER --prerelease), then commit site/ and push"
else echo "upload $IPK to the GitHub release v$VER, then commit site/ and push (GitHub Pages publishes it)"; fi
