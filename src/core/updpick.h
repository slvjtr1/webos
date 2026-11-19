/* Which update to offer, from the GitHub release list or the site's manifests: pure (no network, no state), shared
   by both sources so a package is held to one rule wherever it was found, and testable (tests/update). */
#ifndef SB_UPDPICK_H
#define SB_UPDPICK_H
typedef struct { char ver[24], url[512], sha[72]; char notes[5][256]; int nnotes, beta; } UpdRel; /* beta: a pre-release, or from the beta manifest */

int upd_ver_cmp(const char *a, const char *b);
int upd_notes_from(const char *md, char out[5][256]);   /* a release's first five bullet points, as plain text */
/* the one check: version x.y.z, the file exactly <appId>_<ver>_arm.ipk, an https link, a SHA-256 of 64 hex digits
   (out: lower case). 1 when it is this app's package */
int upd_pkg_ok(const char *appId, const char *fileName, const char *ver, const char *url, const char *sha, char shaOut[72]);
/* GitHub's release list: the newest package of this app (pre-releases only with beta). 1 found, 0 the list read but
   nothing in it for us, -1 not a list (GitHub unreachable or refusing) */
int upd_from_releases(const char *appId, const char *json, int beta, UpdRel *out);
/* a site manifest (api/apps/<id>/manifest.json, or beta/manifest.json when betaFile): 1 usable. The stable file never
   yields a beta (one marked "channel": "beta" is refused); one naming another app id, another package or a version
   its file does not carry is refused */
int upd_from_manifest(const char *appId, const char *json, int betaFile, UpdRel *out);
const UpdRel *upd_newer(const UpdRel *a, const UpdRel *b); /* either may be NULL */
#endif
