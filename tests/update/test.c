/* tests/update: which update is offered (src/core/updpick.c), from fixed data; no network.
   test                      the cases below
   test <stable> <beta>      the manifests tools/release.sh wrote (tests/update/run.sh): stable stays stable, beta is beta */
#include "core/updpick.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID "tv.stremioorigin.app"
#define H1 "1111111111111111111111111111111111111111111111111111111111111111"
#define H2 "2222222222222222222222222222222222222222222222222222222222222222"
#define DL "https://github.com/slvjtr1/webos/releases/download/"
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* GitHub's list: 2.0.0 a pre-release, 1.1.1 stable */
static const char *LIST_BOTH = "[{\"tag_name\":\"v2.0.0\",\"prerelease\":true,\"draft\":false,\"body\":\"- Upscale\\n- Clean sources\",\"assets\":["
  "{\"name\":\"" ID "_2.0.0_arm.ipk\",\"digest\":\"sha256:" H2 "\",\"browser_download_url\":\"" DL "v2.0.0/" ID "_2.0.0_arm.ipk\"},"
  "{\"name\":\"" ID "_2.0.0_arm.ipk.sha256\",\"digest\":\"sha256:" H1 "\",\"browser_download_url\":\"" DL "v2.0.0/" ID "_2.0.0_arm.ipk.sha256\"}]},"
  "{\"tag_name\":\"v1.1.1\",\"prerelease\":false,\"draft\":false,\"body\":\"- Fixes\",\"assets\":["
  "{\"name\":\"" ID "_1.1.1_arm.ipk\",\"digest\":\"sha256:" H1 "\",\"browser_download_url\":\"" DL "v1.1.1/" ID "_1.1.1_arm.ipk\"}]}]";
static const char *LIST_BETA_ONLY = "[{\"tag_name\":\"v2.0.0\",\"prerelease\":true,\"draft\":false,\"assets\":["
  "{\"name\":\"" ID "_2.0.0_arm.ipk\",\"digest\":\"sha256:" H2 "\",\"browser_download_url\":\"" DL "v2.0.0/" ID "_2.0.0_arm.ipk\"}]}]";
static const char *LIST_ODD = "[{\"prerelease\":false,\"assets\":["
  "{\"name\":\"other.app_3.0.0_arm.ipk\",\"digest\":\"sha256:" H1 "\",\"browser_download_url\":\"" DL "v3/other.app_3.0.0_arm.ipk\"},"
  "{\"name\":\"" ID "_3.0.0_arm.ipk\",\"digest\":\"sha256:" H1 "\",\"browser_download_url\":\"" DL "v3/" ID "_2.9.0_arm.ipk\"},"
  "{\"name\":\"" ID "_3.0.0_arm.ipk\",\"digest\":\"sha256:short\",\"browser_download_url\":\"" DL "v3/" ID "_3.0.0_arm.ipk\"},"
  "{\"name\":\"" ID "_3.0.0-rc1_arm.ipk\",\"digest\":\"sha256:" H1 "\",\"browser_download_url\":\"" DL "v3/" ID "_3.0.0-rc1_arm.ipk\"},"
  "{\"name\":\"" ID "_3.0.0_arm.ipk\",\"digest\":\"sha256:" H1 "\",\"browser_download_url\":\"http://example.com/" ID "_3.0.0_arm.ipk\"}]}]";
/* the stable manifest as tools/release.sh has always written it (no channel field) */
static const char *MAN_OLD = "{\"id\":\"" ID "\",\"version\":\"1.1.1\",\"type\":\"native\",\"title\":\"Stremio Origin\",\"rootRequired\":false,"
  "\"ipkUrl\":\"" DL "v1.1.1/" ID "_1.1.1_arm.ipk\",\"ipkHash\":{\"sha256\":\"" H1 "\"}}";
static const char *MAN_BETA = "{\"id\":\"" ID "\",\"version\":\"2.0.0\",\"channel\":\"beta\",\"ipkUrl\":\"" DL "v2.0.0/" ID "_2.0.0_arm.ipk\",\"ipkHash\":{\"sha256\":\"" H2 "\"}}";

static void man_bad(const char *why, const char *json) { UpdRel r; checks++; if (upd_from_manifest(ID, json, 0, &r)) { fails++; printf("  FAIL: manifest accepted: %s\n", why); } }

static char *slurp(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); char *b = malloc(n + 1); b[fread(b, 1, n, f)] = 0; fclose(f); return b; }

int main(int argc, char **argv) {
  UpdRel r, s;
  if (argc == 3) { /* the files release.sh wrote */
    char *st = slurp(argv[1]), *bt = slurp(argv[2]);
    CHECK(st && upd_from_manifest(ID, st, 0, &r) && !strcmp(r.ver, "1.9.0") && !r.beta, "stable manifest: 1.9.0, stable");
    CHECK(bt && upd_from_manifest(ID, bt, 1, &s) && !strcmp(s.ver, "2.0.0") && s.beta, "beta manifest: 2.0.0, beta");
    CHECK(bt && !upd_from_manifest(ID, bt, 0, &r), "beta manifest never read as the stable one");
    CHECK(upd_newer(NULL, &s) == &s, "beta on, GitHub away: 2.0.0 offered");
    free(st); free(bt);
    printf(fails ? "update (site files): %d of %d FAILED\n" : "update (site files): all %d checks passed\n", fails ? fails : checks, checks); return fails != 0;
  }

  printf("GitHub list, beta off / on\n");
  CHECK(upd_from_releases(ID, LIST_BOTH, 0, &r) == 1 && !strcmp(r.ver, "1.1.1") && !r.beta, "beta off: 1.1.1 (%s)", r.ver);
  CHECK(upd_from_releases(ID, LIST_BOTH, 1, &r) == 1 && !strcmp(r.ver, "2.0.0") && r.beta && !strcmp(r.sha, H2) && r.nnotes == 2, "beta on: 2.0.0 beta with its notes");
  printf("GitHub list with only a beta\n");
  CHECK(upd_from_releases(ID, LIST_BETA_ONLY, 0, &r) == 0, "beta off: nothing (falls back to the stable manifest)");
  CHECK(upd_from_releases(ID, LIST_BETA_ONLY, 1, &r) == 1 && r.beta, "beta on: the beta");
  printf("GitHub away or answering something else\n");
  CHECK(upd_from_releases(ID, NULL, 0, &r) == -1, "no answer");
  CHECK(upd_from_releases(ID, "{\"message\":\"API rate limit exceeded\"}", 1, &r) == -1, "rate limited");
  CHECK(upd_from_releases(ID, "<html>", 0, &r) == -1, "not JSON");
  printf("packages that are not this app's, or not as named\n");
  CHECK(upd_from_releases(ID, LIST_ODD, 1, &r) == 0, "another app, a link to another file, a short hash, an odd version, plain http: none taken (%s)", r.ver);

  printf("manifests\n");
  CHECK(upd_from_manifest(ID, MAN_OLD, 0, &r) && !strcmp(r.ver, "1.1.1") && !r.beta && !strcmp(r.sha, H1), "the old stable manifest still reads");
  CHECK(!upd_from_manifest(ID, MAN_OLD, 1, &r), "a stable file at the beta place is not a beta");
  CHECK(upd_from_manifest(ID, MAN_BETA, 1, &r) && r.beta, "beta manifest");
  man_bad("beta marked file read as stable", MAN_BETA);
  man_bad("another app id", "{\"id\":\"other.app\",\"version\":\"1.1.1\",\"ipkUrl\":\"" DL "v1.1.1/" ID "_1.1.1_arm.ipk\",\"ipkHash\":{\"sha256\":\"" H1 "\"}}");
  man_bad("file of another version", "{\"id\":\"" ID "\",\"version\":\"9.9.9\",\"ipkUrl\":\"" DL "v1.1.1/" ID "_1.1.1_arm.ipk\",\"ipkHash\":{\"sha256\":\"" H1 "\"}}");
  man_bad("another package's file", "{\"id\":\"" ID "\",\"version\":\"1.1.1\",\"ipkUrl\":\"" DL "v1.1.1/other.app_1.1.1_arm.ipk\",\"ipkHash\":{\"sha256\":\"" H1 "\"}}");
  man_bad("hash too short", "{\"id\":\"" ID "\",\"version\":\"1.1.1\",\"ipkUrl\":\"" DL "v1.1.1/" ID "_1.1.1_arm.ipk\",\"ipkHash\":{\"sha256\":\"abc\"}}");
  man_bad("hash not hex", "{\"id\":\"" ID "\",\"version\":\"1.1.1\",\"ipkUrl\":\"" DL "v1.1.1/" ID "_1.1.1_arm.ipk\",\"ipkHash\":{\"sha256\":\"zz11111111111111111111111111111111111111111111111111111111111111\"}}");
  man_bad("plain http", "{\"id\":\"" ID "\",\"version\":\"1.1.1\",\"ipkUrl\":\"http://example.com/" ID "_1.1.1_arm.ipk\",\"ipkHash\":{\"sha256\":\"" H1 "\"}}");
  man_bad("a query after the file", "{\"id\":\"" ID "\",\"version\":\"1.1.1\",\"ipkUrl\":\"" DL "v1.1.1/" ID "_1.1.1_arm.ipk?x=1\",\"ipkHash\":{\"sha256\":\"" H1 "\"}}");
  man_bad("odd version", "{\"id\":\"" ID "\",\"version\":\"1.1\",\"ipkUrl\":\"" DL "v1.1/" ID "_1.1_arm.ipk\",\"ipkHash\":{\"sha256\":\"" H1 "\"}}");
  man_bad("no hash", "{\"id\":\"" ID "\",\"version\":\"1.1.1\",\"ipkUrl\":\"" DL "v1.1.1/" ID "_1.1.1_arm.ipk\"}");
  man_bad("not JSON", "<html>");
  { char up[600]; snprintf(up, sizeof up, "{\"id\":\"" ID "\",\"version\":\"1.1.1\",\"ipkUrl\":\"" DL "v1.1.1/" ID "_1.1.1_arm.ipk\",\"ipkHash\":{\"sha256\":\"%s\"}}", "ABCDEF1111111111111111111111111111111111111111111111111111111111");
    CHECK(upd_from_manifest(ID, up, 0, &r) && !strncmp(r.sha, "abcdef", 6), "an upper-case hash is taken, lower-cased"); }

  printf("the fallback's choice\n");
  upd_from_manifest(ID, MAN_OLD, 0, &r); upd_from_manifest(ID, MAN_BETA, 1, &s);
  CHECK(upd_newer(&r, &s) == &s, "beta on, beta newer: the beta");
  CHECK(upd_newer(&r, NULL) == &r, "beta off: the stable manifest");
  { UpdRel st2 = r; snprintf(st2.ver, sizeof st2.ver, "2.0.1"); CHECK(upd_newer(&st2, &s) == &st2, "a newer stable beats an older beta"); }
  { UpdRel same = s; same.beta = 0; CHECK(upd_newer(&s, &same) == &same, "same version: the stable one"); }
  CHECK(upd_ver_cmp("2.0.0", "1.10.9") > 0 && upd_ver_cmp("1.10.0", "1.9.9") > 0 && !upd_ver_cmp("2.0.0", "2.0.0"), "versions compare by number");

  printf(fails ? "update: %d of %d checks FAILED\n" : "update: all %d checks passed\n", fails ? fails : checks, checks);
  return fails != 0;
}
