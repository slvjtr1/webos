/* The release lookup matches only this package's own name for the running app id (<appid>_<x.y.z>_arm.ipk),
   so the native build is never offered another build's package. The install goes through the Homebrew
   Channel (it downloads, checks the SHA-256 and installs as root); before it starts, a small script handed to
   the Channel waits for the new version's appinfo.json and opens the app again, since nothing else in this
   process survives the install. */
#include "core/update.h"
#include "core/updpick.h"
#include <math.h>
#include "ui/glass.h"
#include "core/app.h"
#include "core/net.h"
#include "core/store.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "core/hit.h"
#include "platform/plat.h"
#include "screens/settings.h"
#include "screens/player.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef SB_APP_ID
#define SB_APP_ID "tv.stremioorigin.app"
#endif

#define UPD_GH "https://api.github.com/repos/slvjtr1/webos/releases?per_page=8"
#define UPD_MAN "https://slvjtr1.github.io/webos/api/apps/" SB_APP_ID "/manifest.json"
#define UPD_MAN_BETA "https://slvjtr1.github.io/webos/api/apps/" SB_APP_ID "/beta/manifest.json" /* (its own file: builds that know no channel only ever read the stable one) */
typedef UpdRel Rel; /* (src/core/updpick.c: the one check for both sources) */
static struct {
  char cur[24], appinfo[1200]; int read; Rel avail; int has, state; char msg[240]; unsigned serial;
  int pill, pf; double hideAt, openAt; Tween pa, slide, dot; unsigned long job; int started;
  SGlass glass; unsigned shown, glassFor; double shownAt, goAt; /* the capsule's frosted glass, baked once each time it comes out */
  int kind; struct { char lb[128], badge[48]; void (*ok)(void); int pend; } n; /* kind 1: the other notice's capsule */
} U;
static void changed(void) { U.serial++; }
int update_beta(void) { return !strcmp(store_get("betaUpdates", "false"), "true"); }
int update_avail_beta(void) { return U.has && U.avail.beta; }
#define ver_cmp upd_ver_cmp
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }

/* "1.0.0" from appinfo.json beside the binary (the package), or pkg/ for the Mac preview */
static void read_current(void) {
  if (U.read) return; U.read = 1;
  const char *cands[2] = { "%s/../appinfo.json", "%s/../pkg/appinfo.json" };
  for (int i = 0; i < 2 && !U.cur[0]; i++) {
    char p[1200]; snprintf(p, sizeof p, cands[i], plat_asset_dir());
    FILE *f = fopen(p, "rb"); if (!f) continue; char b[4096]; size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f);
    cJSON *j = cJSON_Parse(b); cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "version");
    if (cJSON_IsString(v) && v->valuestring) { snprintf(U.cur, sizeof U.cur, "%s", v->valuestring); snprintf(U.appinfo, sizeof U.appinfo, "%s", p); }
    cJSON_Delete(j);
  }
  { char w[1200], sh[1200]; snprintf(w, sizeof w, "%s/relaunch.wait", plat_data_dir()); snprintf(sh, sizeof sh, "%s/relaunch.sh", plat_data_dir()); if (access(w, F_OK)) unlink(sh); } /* the script that opened the app again after its update: done once it stops waiting */
  /* a remembered update that is now installed (or older) is forgotten */
  const char *uv = store_get("updVer", "");
  if (*uv && U.cur[0] && ver_cmp(uv, U.cur) <= 0) store_remove("updVer");
}
const char *update_current(void) { read_current(); return U.cur; }
const char *update_avail(void) { return U.has ? U.avail.ver : NULL; }
int update_note_count(void) { return U.has ? U.avail.nnotes : 0; }
const char *update_note(int i) { return U.has && i >= 0 && i < U.avail.nnotes ? U.avail.notes[i] : ""; }
int update_state(void) { return U.state; }
const char *update_msg(void) { return U.msg; }
unsigned update_serial(void) { return U.serial; }

static void pill_show(void);
static double nextAt; static int forceCheck, pillPend; /* the next check while the app stays open; a check that skips the 6-hour rest; a notice that waits for the film to end */
static int reached; /* a source said enough to know (a release list read, or a manifest naming this app's package): finding nothing newer then means up to date */
static int lastSt; /* the list's HTTP answer (0: none), for the log when it could not be read */
/* every check has a number; a reply carries it, and one from an earlier check (Beta updates switched while it was out)
   is dropped: it can never bring back an offer the switch took away */
static unsigned chkGen;
static double retryAt; /* a broken-off install tried again then */
#define UD(manual, stage) ((void *)(((size_t)(chkGen & 0x3fffffffu) << 2) | (size_t)(stage) << 1 | (size_t)(manual)))
#define UD_MANUAL(u) ((int)((size_t)(u) & 1))
#define UD_STAGE(u) ((int)((size_t)(u) >> 1 & 1))
#define UD_STALE(u) (((size_t)(u) >> 2) != (size_t)(chkGen & 0x3fffffffu))
static void pill_hide(void);
static void relaunch_cancel(void);
/* a beta offered while Beta updates is off: taken back, its notice with it (an install already handed to the Channel
   is its own: not touched) */
static int beta_withdraw(void) {
  if (!U.has || !U.avail.beta || update_beta()) return 0;
  fprintf(stderr, "[update] Beta updates is off: the beta %s is no longer offered\n", U.avail.ver);
  U.has = 0; memset(&U.avail, 0, sizeof U.avail); store_remove("updVer"); pillPend = 0; if (!U.job) retryAt = 0;
  if (U.pill && U.kind == 0) { U.pf = 0; pill_hide(); }
  if (U.state == UPD_AVAIL || (U.state == UPD_INSTALLING && !U.job)) U.state = UPD_IDLE;
  U.msg[0] = 0; changed();
  return 1;
}
static void done(const Rel *best, int manual) {
  char at[32]; snprintf(at, sizeof at, "%.0f", now_ms()); store_set("updAt", at);
  if (best && best->beta && !update_beta()) best = NULL; /* (never a beta without Beta updates) */
  if (best && U.cur[0] && ver_cmp(best->ver, U.cur) > 0) {
    U.avail = *best; U.has = 1; U.state = UPD_AVAIL; store_set("updVer", best->ver);
    fprintf(stderr, "[update] %s%s available (running %s)\n", best->ver, best->beta ? " (beta)" : "", U.cur);
    if (!manual) { if (player_open()) pillPend = 1; else pill_show(); } /* (never over a film: after it) */
  } else {
    int none = best || reached;
    U.has = 0; U.state = none ? UPD_NONE : UPD_ERROR; store_remove("updVer");
    if (none) fprintf(stderr, "[update] up to date\n"); else if (lastSt > 0) fprintf(stderr, "[update] could not check (HTTP %d%s)\n", lastSt, lastSt == 200 ? ", not a release list" : ""); else fprintf(stderr, "[update] could not check (no answer: offline, no name lookup or too slow)\n");
    if (manual) toast(T(none ? "You have the latest version" : "Could not check for updates"), 2000, NULL);
    if (!none && U.started) nextAt = app_now() + 600; /* no answer (the network not up yet, the site away): again in 10 minutes */
  }
  changed();
}
/* the fallback when GitHub gave nothing for this app: with Beta updates the beta manifest first (stage 0), then always
   the stable one (stage 1); the newer of the two usable ones is offered */
static Rel fbBeta; static int fbBetaHas;
static void on_manifest(int st, const unsigned char *body, size_t len, void *ud);
static void fetch_manifest(int manual, int stage) {
  const char *mf = getenv(stage ? "SB_UPD_MAN" : "SB_UPD_MAN_BETA"); /* (development: other manifests) */
  net_get_timeout(mf ? mf : stage ? UPD_MAN : UPD_MAN_BETA, 15000, on_manifest, UD(manual, stage));
}
static void on_manifest(int st, const unsigned char *body, size_t len, void *ud) {
  if (UD_STALE(ud)) { fprintf(stderr, "[update] an earlier check's reply: dropped\n"); return; }
  int manual = UD_MANUAL(ud), stage = UD_STAGE(ud); lastSt = st;
  char *txt = NULL; if (st == 200 && body) { txt = malloc(len + 1); if (txt) { memcpy(txt, body, len); txt[len] = 0; } }
  Rel r; int had = txt != NULL, ok = had && upd_from_manifest(SB_APP_ID, txt, !stage, &r); free(txt);
  if (had && !ok) fprintf(stderr, "[update] the %s manifest does not name this app's package: not used\n", stage ? "stable" : "beta");
  if (ok && stage) reached = 1; /* (only a usable stable manifest says what is current: an empty or foreign one says nothing) */
  if (!stage) { fbBetaHas = ok && update_beta(); if (ok) fbBeta = r; fetch_manifest(manual, 1); return; }
  const Rel *best = upd_newer(ok ? &r : NULL, fbBetaHas ? &fbBeta : NULL); fbBetaHas = 0;
  done(best, manual);
}
static void on_releases(int st, const unsigned char *body, size_t len, void *ud) {
  if (UD_STALE(ud)) { fprintf(stderr, "[update] an earlier check's reply: dropped\n"); return; }
  int manual = UD_MANUAL(ud); lastSt = st;
  char *txt = NULL; if (st == 200 && body) { txt = malloc(len + 1); if (txt) { memcpy(txt, body, len); txt[len] = 0; } }
  Rel best; int res = upd_from_releases(SB_APP_ID, txt, update_beta(), &best); free(txt);
  reached = res >= 0;
  if (res == 1) done(&best, manual);
  else fetch_manifest(manual, update_beta() ? 0 : 1);
}
void update_check(int manual) {
  read_current();
  if (U.state == UPD_CHECKING || U.state == UPD_INSTALLING) return;
  const char *gh = getenv("SB_UPD_GH"); /* development: another release list (a local server) */
  if (!manual && !forceCheck && !gh && now_ms() - atof(store_get("updAt", "0")) < 6 * 3600000.0 && !*store_get("updVer", "")) { U.state = UPD_IDLE; return; }
  U.state = UPD_CHECKING; U.msg[0] = 0; changed();
  chkGen++; reached = 0; fbBetaHas = 0;
  net_get_timeout(gh ? gh : UPD_GH, 15000, on_releases, UD(manual, 0));
}

/* ---- install ---- */
static char waitPath[1200], shPath[1200];
static int retries; static int rooted = -1; /* the Channel runs commands as root here (1), not (0), not known (-1) */
static void install_go(void);
static void relaunch_cancel(void) { if (waitPath[0]) unlink(waitPath); if (shPath[0]) unlink(shPath); }
/* the Channel runs this as root, apart from the app: once appinfo.json shows the new version (at most 5 min), the app opens again */
static void relaunch_arm(void) {
#ifdef SB_ANDROIDTV
  return; /* (Android TV: its installer opens the app again; no Channel, no script) */
#endif
  if (!U.appinfo[0]) return;
  char *sh = shPath; snprintf(sh, sizeof shPath, "%s/relaunch.sh", plat_data_dir()); snprintf(waitPath, sizeof waitPath, "%s/relaunch.wait", plat_data_dir());
  FILE *w = fopen(waitPath, "w"); if (w) fclose(w);
  FILE *f = fopen(sh, "w"); if (!f) return;
  fprintf(f, "#!/bin/sh\n# the app opens again once its update is in (written by the app before it asked for the install)\n"
             "M='%s'\ni=0\nwhile [ $i -lt 150 ] && [ -f \"$M\" ]; do\n  sleep 2; i=$((i+1))\n"
             "  if grep -q '\"version\": *\"%s\"' '%s' 2>/dev/null; then rm -f \"$M\"; sleep 2; luna-send -n 1 -f luna://com.webos.applicationManager/launch '{\"id\":\"%s\"}' >/dev/null 2>&1; exit 0; fi\n"
             "done\nrm -f \"$M\"\n", waitPath, U.avail.ver, U.appinfo, SB_APP_ID);
  fclose(f);
  char cmd[1400]; snprintf(cmd, sizeof cmd, "setsid sh '%s' >/dev/null 2>&1 </dev/null &", sh);
  cJSON *p = cJSON_CreateObject(); cJSON_AddStringToObject(p, "command", cmd); char *ps = cJSON_PrintUnformatted(p); cJSON_Delete(p);
  char *r = plat_luna_call("luna://org.webosbrew.hbchannel.service/exec", ps, 3000); free(ps);
  { cJSON *a = r ? cJSON_Parse(r) : NULL; rooted = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "returnValue")) ? 1 : 0; cJSON_Delete(a); } /* (the Channel without root refuses exec: the app is not opened again by itself) */
  fprintf(stderr, "[update] relaunch waiter: %s\n", r ? r : "(no answer)"); free(r);
}
static void on_install(const char *payload, void *ud) {
  (void)ud; cJSON *j = cJSON_Parse(payload); if (!j) return;
  cJSON *rv = cJSON_GetObjectItemCaseSensitive(j, "returnValue"), *et = cJSON_GetObjectItemCaseSensitive(j, "errorText"), *st = cJSON_GetObjectItemCaseSensitive(j, "statusText"), *pg = cJSON_GetObjectItemCaseSensitive(j, "progress");
  if (cJSON_IsFalse(rv) || (cJSON_IsString(et) && et->valuestring)) {
    const char *e = cJSON_IsString(et) ? et->valuestring : "";
    /* no Channel on this TV, or the app may not call it (an app installed in developer mode without the
       Channel): the hub answers at once, and the way that is left is the Channel's own install */
    int noChannel = strstr(e, "does not exist") || strstr(e, "Unknown service") || strstr(e, "Denied") || strstr(e, "not allowed") || strstr(e, "Not permitted");
    int ask = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "permission")); /* (Android TV: the system's leave to install is still to be given; its page is open: not a failure) */
    U.state = UPD_AVAIL; snprintf(U.msg, sizeof U.msg, "%s", ask ? e : noChannel ? T("Install it from the Homebrew Channel") : T1("The update failed: {e}", "e", e));
    if (ask) { fprintf(stderr, "[update] waiting for the leave to install apps (the request stays: given, the download starts by itself)\n"); changed(); cJSON_Delete(j); return; }
    if (U.job) { plat_luna_cancel(U.job); U.job = 0; }
    if (!noChannel && retries < 2) { retries++; retryAt = app_now() + 3 * retries; U.state = UPD_INSTALLING; snprintf(U.msg, sizeof U.msg, "%s", T("Downloading again\xe2\x80\xa6")); fprintf(stderr, "[update] install failed (%s): trying again (%d of 2)\n", e, retries); changed(); cJSON_Delete(j); return; }
    relaunch_cancel();
    fprintf(stderr, "[update] install failed: %s\n", e); toast(noChannel ? T("Install it from the Homebrew Channel") : T("The update failed"), 2500, NULL);
    changed(); cJSON_Delete(j); return;
  }
  if (cJSON_IsString(st) && st->valuestring) { char pc[16] = ""; if (U.state != UPD_INSTALLING) { U.state = UPD_INSTALLING; fprintf(stderr, "[update] installing %s (the leave given)\n", U.avail.ver); } /* (the download that waited for the leave to install) */ if (cJSON_IsNumber(pg)) snprintf(pc, sizeof pc, " %d%%", (int)(pg->valuedouble + 0.5)); snprintf(U.msg, sizeof U.msg, "%s%s", T(st->valuestring), pc); }
  if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "finished"))) { snprintf(U.msg, sizeof U.msg, "%s", rooted == 0 ? T("Installed: open the app again") : T("Installed: the app opens again in a moment")); fprintf(stderr, "[update] installed %s\n", U.avail.ver); }
  changed(); cJSON_Delete(j);
}
void update_install(void) {
  if (app_now() - U.openAt < 0.9) return; /* the OK that opened this page */
  if (U.has && beta_withdraw()) { update_check(1); return; } /* (switched off since the offer came) */
  if (!U.has) { update_check(1); return; }
  if (U.state == UPD_INSTALLING) return;
  U.state = UPD_INSTALLING; snprintf(U.msg, sizeof U.msg, "%s", T("Downloading…")); changed();
  fprintf(stderr, "[update] installing %s\n", U.avail.ver);
  retries = 0; retryAt = 0; relaunch_arm();
  install_go();
}
static void install_go(void) {
  if (!U.has || (U.avail.beta && !update_beta())) { /* (a retry of a beta after Beta updates went off: not sent) */
    fprintf(stderr, "[update] install not sent: the offer is gone\n"); retryAt = 0; relaunch_cancel();
    if (U.state == UPD_INSTALLING) U.state = UPD_IDLE;
    beta_withdraw(); changed(); return; }
  cJSON *p = cJSON_CreateObject(); cJSON_AddStringToObject(p, "ipkUrl", U.avail.url); cJSON_AddStringToObject(p, "ipkHash", U.avail.sha); cJSON_AddStringToObject(p, "id", SB_APP_ID); cJSON_AddTrueToObject(p, "subscribe");
  char *ps = cJSON_PrintUnformatted(p); cJSON_Delete(p);
  U.job = plat_luna_subscribe("luna://org.webosbrew.hbchannel.service/install", ps, on_install, NULL); free(ps);
  if (!U.job) { U.state = UPD_AVAIL; snprintf(U.msg, sizeof U.msg, "%s", T("Install it from the Homebrew Channel")); relaunch_cancel(); changed(); } /* no Channel (or the Mac preview) */
}

/* ---- the notice ---- */
int update_pill_focused(void) { return U.pf; }
float update_dot_hide(void) { return U.dot.v; }
static void pill_show(void) {
  if (!U.has || U.pill) return;
  fprintf(stderr, "[update] notice shown: version %s\n", U.avail.ver);
  U.kind = 0; U.pill = 1; U.shown++; U.shownAt = app_now(); U.goAt = 0; U.hideAt = app_now() + 10; tween_to(&U.pa, 1, 0.3f, EASE_IN_OUT); tween_to(&U.slide, 1, 0.55f, EASE_LINEAR); tween_to(&U.dot, 1, 0.2f, EASE_IN_OUT);
}
static void pill_hide(void) {
  if (U.pf || !U.pill) return;
  U.pill = 0; U.hideAt = 0; tween_to(&U.pa, 0, 0.3f, EASE_IN_OUT); tween_to(&U.slide, 0, 0.45f, EASE_LINEAR); tween_to(&U.dot, 0, 0.2f, EASE_IN_OUT);
}
void update_open(void) {
  U.goAt = app_now(); U.openAt = app_now(); U.pf = 0; U.pill = 0; U.hideAt = 0; tween_to(&U.pa, 0, 0.3f, EASE_IN_OUT); tween_to(&U.slide, 0, 0.45f, EASE_LINEAR); tween_to(&U.dot, 0, 0.2f, EASE_IN_OUT);
  if (app_overlay) { app_overlay->key(K_BACK); app_overlay = NULL; }
  topNav.idx = NAV_SET; app_go(SCR_SET, NULL, 1); settings_open_about();
}
static void notice_ok(void);
int update_notice_key(void) { if (!U.pill || U.pa.v < 0.5f) return 0; if (U.kind == 1) notice_ok(); else update_open(); return 1; } /* the red key while a notice is out: straight to what it is about */
static void notice_ok(void) { void (*ok)(void) = U.n.ok; U.goAt = app_now(); U.pf = 0; pill_hide(); U.n.ok = NULL; if (ok) ok(); }
void update_notice(const char *label, const char *badge, void (*ok)(void)) {
  snprintf(U.n.lb, sizeof U.n.lb, "%s", label); snprintf(U.n.badge, sizeof U.n.badge, "%s", badge); U.n.ok = ok; U.n.pend = 1; /* shown by update_tick once the capsule is free */
}
void update_notice_cancel(void (*ok)(void)) {
  if (U.n.ok != ok) return; U.n.pend = 0; U.n.ok = NULL;
  if (U.kind == 1 && U.pill) { U.pf = 0; pill_hide(); }
}
int update_pill_key(int k, int onSettings) {
  if (U.pf) {
    if (k == K_OK) { if (U.kind == 1) notice_ok(); else update_open(); return 1; }
    U.pf = 0; U.hideAt = app_now() + 4;
    if (k == K_LEFT || k == K_UP) return 1; /* Left: back onto the Settings icon */
    return 0; /* Right and Down go on from the Settings icon */
  }
  if (k == K_RIGHT && U.pill && onSettings) { U.pf = 1; U.hideAt = 0; return 1; }
  return 0;
}
static void pill_click(int a, int b) { (void)a; (void)b; if (U.kind == 1) notice_ok(); else update_open(); }
static const char *pill_lb(void) { return U.kind == 1 ? U.n.lb : T("Update available"); }
static const char *pill_badge(void) { return U.kind == 1 ? U.n.badge : U.avail.ver; }
/* the capsule: the words, for an update its version (plain), then the remote's red key drawn as on the remote: the key that takes you there */
#define KEY_W 46.0f
#define KEY_H 26.0f
static float ver_w(void) { return U.kind == 1 ? 0 : 12 + text_width(pill_badge(), 21, 600, 0.2f); }
static float pill_w(void) { return 92 + text_width(pill_lb(), 23, 600, 0.1f) + ver_w() + 16 + KEY_W + 26; }
/* Search makes room when it sits beyond the Settings icon (the notice's side), with the notice's own motion */
float update_search_shift(float searchX, float setX) {
  if ((U.kind == 0 && !U.has) || U.slide.v <= 0.001f) return 0;
  int beyond = app_rtl() ? searchX < setX : searchX > setX; if (!beyond) return 0;
  return (pill_w() - 76) * ease_apply(EASE_ROW, U.slide.v) * (app_rtl() ? -1 : 1);
}
void update_pill_draw(float setX) {
  float a = U.pa.v; if (a <= 0.001f || (U.kind == 0 && !U.has)) return;
  const char *lb = pill_lb(), *ver = pill_badge(); int rtl = app_rtl(), f = U.pf;
  float tw = text_width(lb, 23, 600, 0.1f), vw = ver_w();
  float W = pill_w(), h = 76, y = 12, dx = -36 * (1 - ease_apply(EASE_ROW, U.slide.v)) * (rtl ? -1 : 1);
  float x0 = (rtl ? setX + 76 - W : setX) + dx;
  if (U.pill && a > 0.5f) hit_act(rtl ? x0 : setX + 76, y, W - 76, h, pill_click, 0, 0); /* beside the icon: the icon keeps its own clicks */
  /* frosted glass, like the sheets: the page behind baked once where the capsule settles, then a light tint over it */
  if (U.glassFor != U.shown) { U.glassFor = U.shown; float xf = rtl ? setX + 76 - W : setX; sglass_bake(&U.glass, xf, y, W, h); }
  gfx_opacity_push(a);
  gfx_shadow(x0, y + 14, W, h, 38, 34, BLACK_A(87));
  gfx_opacity_push(0.78f); sglass_draw(&U.glass, x0, y, W, h, 38); gfx_opacity_pop(); /* glass, the page a little seen through it */
  double now = app_now(); float go = U.goAt > 0 && now - U.goAt < 0.5 ? 1 - (float)((now - U.goAt) / 0.5) : 0; /* OK: it lights up as it takes you there */
  if (f) { gfx_rrect(x0, y, W, h, 38, acc_a(0.5f)); gfx_rrect_v(x0, y, W, h, 38, WHITE_A(51), WHITE_A(15)); }
  else gfx_rrect_v(x0, y, W, h, 38, WHITE_A(34), WHITE_A(6)); /* a sheen from above */
  if (go > 0) gfx_rrect(x0, y, W, h, 38, acc_a(0.75f * go));
  { float ph = (float)fmod(now - U.shownAt, 3.2) / 1.1f; if (ph < 1) gfx_glare(x0, y, W, h, 38, -0.2f + 1.4f * ph * ph * (3 - 2 * ph), WHITE_A(46)); } /* every few seconds a soft light crosses it: there is something to open */
  gfx_ring(x0, y, W, h, 38, 1.5f, go > 0 ? col_alpha(ACC2, 0.4f + 0.6f * go) : WHITE_A(44));
  float tx = rtl ? x0 + W - 92 : x0 + 92, kx = rtl ? x0 + 26 : x0 + W - 26 - KEY_W, ky = 50 - KEY_H / 2;
  text_draw(lb, tx, 50 - text_line_height(23) / 2.0f, 23, 600, f ? WHITE_A(255) : WHITE_A(230), 0.1f, 0, rtl ? ALIGN_RIGHT : ALIGN_LEFT);
  if (vw > 0) text_draw(ver, rtl ? tx - tw - 12 : tx + tw + 12, 50 - text_line_height(21) / 2.0f, 21, 600, f ? WHITE_A(220) : WHITE_A(170), 0.2f, 0, rtl ? ALIGN_RIGHT : ALIGN_LEFT);
  { float br = 0.5f + 0.5f * sinf((float)(now - U.shownAt) * 2.4f); /* the red key, as on the remote: a soft rounded block, two small dots; it glows gently */
    gfx_shadow(kx, ky + 2, KEY_W, KEY_H, 8, 8, BLACK_A(90)); gfx_rrect(kx, ky, KEY_W, KEY_H, 8, RGB(0xef, 0x3b, 0x3b)); gfx_rrect_v(kx, ky, KEY_W, KEY_H, 8, WHITE_A((int)(40 + 30 * br)), WHITE_A(0));
    gfx_circle(kx + KEY_W / 2 - 7, ky + KEY_H / 2, 2.6f, RGBA(90, 10, 10, 170)); gfx_circle(kx + KEY_W / 2 + 7, ky + KEY_H / 2, 2.6f, RGBA(90, 10, 10, 170)); }
  gfx_opacity_pop();
}

void update_tick(float dt) {
  tween_tick(&U.pa, dt); tween_tick(&U.slide, dt); tween_tick(&U.dot, dt); sglass_tick(&U.glass, dt);
  plat_luna_pump();
  if (U.pf && !topNav.active) { U.pf = 0; U.hideAt = app_now() + 4; } /* the highlight left the top bar */
  if (U.pill && U.hideAt && app_now() >= U.hideAt) pill_hide();
  if (U.n.pend && !U.pill && U.pa.v <= 0.001f && !player_open()) { /* the other notice, once the capsule is back in */
    U.n.pend = 0; U.kind = 1; U.pill = 1; U.shown++; U.shownAt = app_now(); U.goAt = 0; U.hideAt = app_now() + 10; tween_to(&U.pa, 1, 0.3f, EASE_IN_OUT); tween_to(&U.slide, 1, 0.55f, EASE_LINEAR); tween_to(&U.dot, 1, 0.2f, EASE_IN_OUT); }
  /* the check, once the start is over and calm: 8 s in, the splash gone, no film, no key for a moment */
  /* every start checks (one small request), then every 6 hours while the app stays open, 10 minutes after a check
     that got no answer; each when nothing plays and nothing is being pressed */
  if (!U.started && app_now() > 8 && app_splash_done && !player_open() && app_idle() > 1) { U.started = 1; nextAt = app_now() + 6 * 3600; forceCheck = 1; update_check(0); forceCheck = 0; }
  else if (U.started && nextAt && app_now() >= nextAt && !player_open() && app_idle() > 1 && U.state != UPD_CHECKING && U.state != UPD_INSTALLING) { nextAt = app_now() + 6 * 3600; forceCheck = 1; update_check(0); forceCheck = 0; }
  if (pillPend && !player_open() && !U.pill && U.pa.v <= 0.001f) { pillPend = 0; pill_show(); }
  if (retryAt && app_now() >= retryAt) { retryAt = 0; if (U.state == UPD_INSTALLING && !U.job) install_go(); } /* (a broken-off download, again) */
  { static int beta = -1; int b = update_beta();
    if (beta >= 0 && b != beta) { /* Beta updates switched: a beta offer goes (off), a check under way is dropped, and it looks again at once */
      if (!b) { beta_withdraw(); if (U.state == UPD_INSTALLING && !U.job) { retryAt = 0; relaunch_cancel(); U.state = UPD_IDLE; changed(); } }
      if (U.state == UPD_CHECKING) { chkGen++; U.state = UPD_IDLE; }
      if (U.state == UPD_INSTALLING) fprintf(stderr, "[update] Beta updates switched while the Channel installs %s: that install goes on\n", U.avail.ver);
      else { forceCheck = 1; update_check(0); forceCheck = 0; } }
    beta = b; }
  { static int autoI = -1; if (autoI < 0) autoI = getenv("SB_UPD_AUTOINSTALL") != NULL; /* development: Install pressed 3 s after the notice (the whole update, untouched) */
    if (autoI && U.has && U.state == UPD_AVAIL && U.shown && app_now() - U.shownAt > 3) { autoI = 0; U.openAt = 0; update_install(); } }
}
