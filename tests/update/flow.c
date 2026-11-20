/* tests/update/flow.c: the real update callbacks (src/core/update.c) with a fake network and a fake Homebrew Channel
   installer: what is offered, what is installed and how often, as Beta updates is switched. Built by tests/update/run.sh. */
#include "core/update.h"
#include "core/app.h"
#include "core/net.h"
#include "ui/glass.h"
#include "platform/plat.h"
#include "core/hit.h"
#undef main
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_(p) _mkdir(p)
#else
#define mkdir_(p) mkdir(p, 0755)
#endif

#define ID "tv.stremioorigin.app"
#define H1 "1111111111111111111111111111111111111111111111111111111111111111"
#define H2 "2222222222222222222222222222222222222222222222222222222222222222"
#define DL "https://github.com/slvjtr1/webos/releases/download/"
static const char *LIST_BETA_ONLY = "[{\"tag_name\":\"v2.0.0\",\"prerelease\":true,\"draft\":false,\"body\":\"- New\",\"assets\":["
  "{\"name\":\"" ID "_2.0.0_arm.ipk\",\"digest\":\"sha256:" H2 "\",\"browser_download_url\":\"" DL "v2.0.0/" ID "_2.0.0_arm.ipk\"}]}]";
static const char *MAN_CUR = "{\"id\":\"" ID "\",\"version\":\"1.1.1\",\"ipkUrl\":\"" DL "v1.1.1/" ID "_1.1.1_arm.ipk\",\"ipkHash\":{\"sha256\":\"" H1 "\"}}";
static const char *MAN_NEW = "{\"id\":\"" ID "\",\"version\":\"1.2.0\",\"ipkUrl\":\"" DL "v1.2.0/" ID "_1.2.0_arm.ipk\",\"ipkHash\":{\"sha256\":\"" H1 "\"}}";
static const char *MAN_BETA = "{\"id\":\"" ID "\",\"version\":\"2.0.0\",\"channel\":\"beta\",\"ipkUrl\":\"" DL "v2.0.0/" ID "_2.0.0_arm.ipk\",\"ipkHash\":{\"sha256\":\"" H2 "\"}}";

/* ---- the fake network: requests wait until the test answers them ---- */
typedef struct { char url[300]; NetCb cb; void *ud; int done; } Req;
static Req req[64]; static int nreq;
unsigned net_get_timeout(const char *url, int timeoutMs, NetCb cb, void *ud) { (void)timeoutMs; Req *r = &req[nreq++]; snprintf(r->url, sizeof r->url, "%s", url); r->cb = cb; r->ud = ud; r->done = 0; return (unsigned)nreq; }
static void answer(int i, int st, const char *body) { Req *r = &req[i]; r->done = 1; r->cb(st, (const unsigned char *)(body ? body : ""), body ? strlen(body) : 0, r->ud); }
static int open_req(const char *part) { for (int i = nreq - 1; i >= 0; i--) if (!req[i].done && strstr(req[i].url, part)) return i; return -1; }

/* ---- the fake Channel installer ---- */
static int installs; static char instUrl[600], instId[64]; static PlatLunaCb instCb;
unsigned long plat_luna_subscribe(const char *uri, const char *payload, PlatLunaCb cb, void *ud) {
  (void)ud; if (!strstr(uri, "hbchannel.service/install")) return 0;
  installs++; instCb = cb; cJSON *p = cJSON_Parse(payload);
  snprintf(instUrl, sizeof instUrl, "%s", cJSON_GetObjectItem(p, "ipkUrl")->valuestring); snprintf(instId, sizeof instId, "%s", cJSON_GetObjectItem(p, "id")->valuestring);
  cJSON_Delete(p); return 7; }
char *plat_luna_call(const char *uri, const char *payload, int timeoutMs) { (void)uri; (void)payload; (void)timeoutMs; return NULL; }
void plat_luna_cancel(unsigned long h) { (void)h; }
void plat_luna_pump(void) {}
const char *plat_asset_dir(void) { return "build/updflow/assets"; }
const char *plat_data_dir(void) { return "build/updflow"; }

/* ---- the store, words, toasts, clock ---- */
static cJSON *st;
const char *store_get(const char *k, const char *d) { cJSON *v = cJSON_GetObjectItem(st, k); return cJSON_IsString(v) ? v->valuestring : d; }
void store_set(const char *k, const char *v) { cJSON_DeleteItemFromObject(st, k); cJSON_AddStringToObject(st, k, v); }
void store_remove(const char *k) { cJSON_DeleteItemFromObject(st, k); }
const char *T(const char *en) { return en; }
const char *T1(const char *en, const char *k, const char *v) { (void)k; (void)v; return en; }
static char lastToast[200];
void toast(const char *m, int ms, const char *key) { (void)ms; (void)key; snprintf(lastToast, sizeof lastToast, "%s", m); }
static double clk = 100;
double app_now(void) { return clk; }
float app_idle(void) { return 10; }
int app_splash_done = 1;
int player_open(void) { return 0; }
TopNav topNav; Screen *app_overlay; uint32_t ACC2;
/* ---- drawing: not drawn here ---- */
uint32_t acc_a(float a) { (void)a; return 0; }
void app_go(Scr s, const void *p, int n) { (void)s; (void)p; (void)n; }
int app_rtl(void) { return 0; }
void settings_open_about(void) {}
void gfx_circle(float a, float b, float c, uint32_t d) { (void)a; (void)b; (void)c; (void)d; }
void gfx_glare(float a, float b, float c, float d, float e, float f, uint32_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void gfx_opacity_pop(void) {}
void gfx_opacity_push(float k) { (void)k; }
void gfx_ring(float a, float b, float c, float d, float e, float f, uint32_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void gfx_rrect(float a, float b, float c, float d, float e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_rrect_v(float a, float b, float c, float d, float e, uint32_t f, uint32_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void gfx_shadow(float a, float b, float c, float d, float e, float f, uint32_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void hit_act(float a, float b, float c, float d, HitFn f, int x, int y) { (void)a; (void)b; (void)c; (void)d; (void)f; (void)x; (void)y; }
void sglass_bake(SGlass *g, float a, float b, float c, float d) { (void)g; (void)a; (void)b; (void)c; (void)d; }
void sglass_draw(const SGlass *g, float a, float b, float c, float d, float e) { (void)g; (void)a; (void)b; (void)c; (void)d; (void)e; }
void sglass_tick(SGlass *g, float dt) { (void)g; (void)dt; }
float text_draw(const char *u, float a, float b, int c, int d, uint32_t e, float f, float g, int h) { (void)u; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; return 0; }
int text_line_height(int px) { return px; }
float text_width(const char *u, int a, int b, float c) { (void)u; (void)a; (void)b; (void)c; return 10; }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const char *offer(void) { const char *v = update_avail(); return v ? v : "-"; }
static void beta(int on) { store_set("betaUpdates", on ? "true" : "false"); }
static void tick(void) { clk += 0.02; update_tick(0.02f); }

int main(void) {
  mkdir_("build"); mkdir_("build/updflow"); mkdir_("build/updflow/assets");
  { FILE *f = fopen("build/updflow/appinfo.json", "wb"); fputs("{\"id\":\"" ID "\",\"version\": \"1.1.1\"}", f); fclose(f); }
  st = cJSON_CreateObject(); beta(1);
  tick(); /* (the switch's state first seen) */

  printf("a beta offered, then Beta updates switched off\n");
  update_check(1); answer(open_req("api.github.com"), 200, LIST_BETA_ONLY);
  CHECK(!strcmp(offer(), "2.0.0") && update_avail_beta(), "beta 2.0.0 offered (%s)", offer());
  beta(0);
  update_install(); /* pressed before a frame noticed the switch */
  CHECK(installs == 0, "Install with the switch off: nothing sent (%d)", installs);
  CHECK(!strcmp(offer(), "-"), "the beta offer is gone (%s)", offer());
  tick();
  update_install();
  CHECK(installs == 0, "still nothing sent (%d)", installs);
  { int g = open_req("api.github.com"); CHECK(g >= 0, "looked again");
    if (g >= 0) answer(g, 200, LIST_BETA_ONLY); }
  { int m = open_req("/manifest.json"); CHECK(m >= 0 && !strstr(req[m].url, "/beta/"), "stable manifest asked, not the beta one");
    if (m >= 0) answer(m, 200, MAN_CUR); }
  CHECK(!strcmp(offer(), "-") && update_state() == UPD_NONE, "up to date on the stable channel (%s, state %d)", offer(), update_state());
  update_install();
  CHECK(installs == 0, "no install of the beta (%d)", installs);

  printf("Beta updates switched off while the beta manifest is being fetched\n");
  beta(1); tick(); /* (switched on: looks again) */
  { int g = open_req("api.github.com"); answer(g, 0, NULL); } /* GitHub away */
  int bm = open_req("/beta/manifest.json"); CHECK(bm >= 0, "beta manifest asked");
  beta(0); tick();
  int before = nreq;
  if (bm >= 0) answer(bm, 200, MAN_BETA); /* the earlier check's reply comes in late */
  CHECK(nreq == before, "the late reply started nothing (%d new requests)", nreq - before);
  CHECK(!strcmp(offer(), "-"), "the late reply brought no beta back (%s)", offer());
  update_install(); CHECK(installs == 0, "nothing sent (%d)", installs);

  { int g = open_req("api.github.com"); CHECK(g >= 0, "the new check is out"); if (g >= 0) answer(g, 0, NULL); }
  { int m = open_req("/manifest.json"); CHECK(m >= 0 && !strstr(req[m].url, "/beta/"), "only the stable manifest"); if (m >= 0) answer(m, 200, MAN_CUR); }
  CHECK(!strcmp(offer(), "-") && update_state() == UPD_NONE, "up to date (%s)", offer());

  printf("status when the sources say too little\n");
  update_check(1); answer(open_req("api.github.com"), 0, NULL); answer(open_req("/manifest.json"), 200, "{}");
  CHECK(update_state() == UPD_ERROR && !strcmp(lastToast, "Could not check for updates"), "GitHub away, manifest {}: could not check (state %d, %s)", update_state(), lastToast);
  update_check(1); answer(open_req("api.github.com"), 0, NULL); answer(open_req("/manifest.json"), 200, MAN_CUR);
  CHECK(update_state() == UPD_NONE && !strcmp(lastToast, "You have the latest version"), "GitHub away, manifest of the installed version: up to date (state %d)", update_state());
  update_check(1); answer(open_req("api.github.com"), 200, "[]"); answer(open_req("/manifest.json"), 200, "{}");
  CHECK(update_state() == UPD_NONE, "GitHub list read, nothing newer: up to date (state %d)", update_state());
  update_check(1); answer(open_req("api.github.com"), 200, LIST_BETA_ONLY); answer(open_req("/manifest.json"), 404, "Not found");
  CHECK(update_state() == UPD_NONE && !strcmp(offer(), "-"), "GitHub list with only a beta, Beta updates off: up to date (state %d)", update_state());
  update_check(1); answer(open_req("api.github.com"), 403, "{\"message\":\"rate limit\"}"); answer(open_req("/manifest.json"), 0, NULL);
  CHECK(update_state() == UPD_ERROR, "both away: could not check (state %d)", update_state());
  CHECK(installs == 0, "no installs (%d)", installs);

  printf("a beta install that broke off, Beta updates switched off before its retry\n");
  beta(1); tick(); /* switched on: a check */
  { int g = open_req("api.github.com"); CHECK(g >= 0, "looked again"); if (g >= 0) answer(g, 200, LIST_BETA_ONLY); }
  CHECK(!strcmp(offer(), "2.0.0"), "beta offered (%s)", offer());
  clk += 5; update_install();
  CHECK(installs == 1 && strstr(instUrl, "_2.0.0_arm.ipk"), "beta install sent while Beta updates is on (%d)", installs);
  instCb("{\"returnValue\":false,\"errorText\":\"download broke off\"}", NULL); /* a retry planned in 3 s */
  beta(0); tick(); clk += 10; tick(); tick();
  CHECK(installs == 1, "the retry of the beta was not sent (%d new)", installs - 1);
  CHECK(!strcmp(offer(), "-"), "and it is no longer offered (%s)", offer());

  printf("then a stable update is found\n");
  { int g = open_req("api.github.com"); CHECK(g >= 0, "the new check is out"); if (g >= 0) answer(g, 0, NULL); }
  { int m = open_req("/manifest.json"); CHECK(m >= 0 && !strstr(req[m].url, "/beta/"), "stable manifest"); if (m >= 0) answer(m, 200, MAN_NEW); }
  CHECK(!strcmp(offer(), "1.2.0") && !update_avail_beta(), "stable 1.2.0 offered (%s)", offer());
  update_install();
  CHECK(installs == 2 && strstr(instUrl, ID "_1.2.0_arm.ipk") && !strcmp(instId, ID), "one install of 1.2.0 for this app (%d, %s)", installs, instUrl);
  printf("  Beta updates switched on and off during that install: it goes on\n");
  beta(1); tick(); beta(0); tick();
  CHECK(installs == 2 && update_state() == UPD_INSTALLING && !strcmp(offer(), "1.2.0"), "install untouched (state %d)", update_state());

  printf(fails ? "update flow: %d of %d checks FAILED\n" : "update flow: all %d checks passed\n", fails ? fails : checks, checks);
  return fails != 0;
}
