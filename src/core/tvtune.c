#include "core/tvtune.h"
#include "core/store.h"
#include "core/i18n.h"
#include "platform/plat.h"
#include "cJSON.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SET "luna://com.webos.settingsservice/setSystemSettings"
#define GET "luna://com.webos.settingsservice/getSystemSettings"
#define EXEC "luna://org.webosbrew.hbchannel.service/exec"
#define SAVED "tvTuneSaved"      /* { category: { key: value before } } until put back */
#define MAXW 24

typedef struct { const char *cat, *k, *v; int rel, lo, hi; } Want; /* rel: v is a step from the TV's own value, kept within lo..hi; v may list choices "a|b|c", the first the TV takes */
typedef struct { int g, n, upscale, sAuto, voice, even; char note[96]; Want w[MAXW]; } Job; /* sAuto, voice, even: the sound settings, read on the main thread */

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int on, gen, notice, clearSaved, avail = -1, soundOut;
static int route = -1; /* how the TV is asked: -1 not found out, 0 the app itself, 1 through the Channel (root), 2 neither */
static char noteTxt[96];
static cJSON *toSave, *saved;

static int is_on(const char *k, const char *d) { return !strcmp(store_get(k, d), "true"); }
const char *tvtune_level(int anime) {
  const char *v = store_get(anime ? "animeEnhance" : "filmEnhance", anime ? "B" : "off");
  if (!strcmp(v, "true")) return "B"; if (!strcmp(v, "false")) return "off";
  return !strcmp(v, "A") || !strcmp(v, "B") || !strcmp(v, "C") || !strcmp(v, "D") ? v : "off";
}

/* ---- one call to the settings service, by the route that works on this TV ---- */
/* only letters, digits and a few marks go into a shell command: nothing can leave its quotes */
static int clean(const char *s) { for (; *s; s++) { char c = *s; if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("_-.:,{}[]\" ", c))) return 0; } return 1; }
static cJSON *via_channel(const char *uri, const char *payload) {
  if (!clean(payload)) return NULL;
  char cmd[1600]; snprintf(cmd, sizeof cmd, "luna-send -n 1 -f %s '%s'", uri, payload);
  cJSON *p = cJSON_CreateObject(); cJSON_AddStringToObject(p, "command", cmd); char *ps = cJSON_PrintUnformatted(p); cJSON_Delete(p);
  char *r = ps ? plat_luna_call(EXEC, ps, 4000) : NULL; free(ps); if (!r) return NULL;
  cJSON *j = cJSON_Parse(r); free(r); if (!j) return NULL;
  cJSON *rv = cJSON_GetObjectItemCaseSensitive(j, "returnValue"), *out = cJSON_GetObjectItemCaseSensitive(j, "stdoutString");
  cJSON *o = cJSON_IsTrue(rv) && cJSON_IsString(out) ? cJSON_Parse(out->valuestring) : NULL; cJSON_Delete(j); return o;
}
static cJSON *direct(const char *uri, const char *payload) { char *r = plat_luna_call(uri, payload, 3000); if (!r) return NULL; cJSON *j = cJSON_Parse(r); free(r); return j; }
static int ok_of(const cJSON *o) { return o && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "returnValue")); }
/* the first call finds the route: the app's own, else the Channel's (root); answers that refuse access say so */
static cJSON *call(const char *uri, const char *payload) {
  int r; pthread_mutex_lock(&mu); r = route; pthread_mutex_unlock(&mu);
  if (r == 0) return direct(uri, payload);
  if (r == 1) return via_channel(uri, payload);
  if (r == 2) return NULL;
  cJSON *o = direct(uri, payload); int found = -1;
  if (ok_of(o)) found = 0;
  else { cJSON_Delete(o); o = via_channel(uri, payload); found = o ? 1 : 2; }
  pthread_mutex_lock(&mu); route = found; pthread_mutex_unlock(&mu);
  fprintf(stderr, "[tvtune] the TV's settings: %s\n", found == 0 ? "the app may change them itself" : found == 1 ? "through the Homebrew Channel (root)" : "not open to the app on this TV");
  return o;
}

static int set_one(const char *cat, const char *k, const char *v) {
  char pl[600]; snprintf(pl, sizeof pl, "{\"category\":\"%s\",\"settings\":{\"%s\":\"%s\"}}", cat, k, v);
  cJSON *o = call(SET, pl); int ok = ok_of(o); cJSON_Delete(o); return ok;
}
static int set_cat(const char *cat, const cJSON *settings) {
  char *js = cJSON_PrintUnformatted(settings); if (!js) return 0;
  char pl[1400]; snprintf(pl, sizeof pl, "{\"category\":\"%s\",\"settings\":%s}", cat, js); free(js);
  cJSON *o = call(SET, pl); int ok = ok_of(o); cJSON_Delete(o); return ok;
}
/* a whole category at once; refused, each key alone, and for a key with choices each choice in turn (one this model
   does not take never stops the others). out: the keys taken, with the value taken */
static int set_keys(const char *cat, const cJSON *settings, cJSON *out) {
  if (!cJSON_GetArraySize(settings)) return 1;
  int plain = 1; const cJSON *x; cJSON_ArrayForEach(x, settings) if (strchr(x->valuestring, '|')) plain = 0;
  if (plain && set_cat(cat, settings)) { cJSON_ArrayForEach(x, settings) if (out) cJSON_AddStringToObject(out, x->string, x->valuestring); return 1; }
  int any = 0;
  cJSON_ArrayForEach(x, settings) {
    char ch[160]; snprintf(ch, sizeof ch, "%s", x->valuestring); int took = 0;
    for (char *c = strtok(ch, "|"); c && !took; c = strtok(NULL, "|")) if (set_one(cat, x->string, c)) { took = 1; any = 1; if (out) cJSON_AddStringToObject(out, x->string, c); }
    if (!took) fprintf(stderr, "[tvtune] %s.%s not taken by this TV\n", cat, x->string); }
  return any;
}
static cJSON *get_cat(const char *cat, const Job *j, const char *extra) {
  char pl[1000]; int n = snprintf(pl, sizeof pl, "{\"category\":\"%s\",\"keys\":[", cat); int k = 0;
  for (int i = 0; j && i < j->n && n < (int)sizeof pl - 60; i++) if (!strcmp(j->w[i].cat, cat)) n += snprintf(pl + n, sizeof pl - n, "%s\"%s\"", k++ ? "," : "", j->w[i].k);
  if (extra && n < (int)sizeof pl - 60) n += snprintf(pl + n, sizeof pl - n, "%s\"%s\"", k++ ? "," : "", extra);
  snprintf(pl + n, sizeof pl - n, "]}"); return call(GET, pl);
}

/* where the sound goes: read once per title, it decides the sound settings and the DTS converter's channels */
static int read_sound_out(void) {
  cJSON *o = get_cat("sound", NULL, "soundOutput"); const cJSON *s = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(o, "settings"), "soundOutput");
  int r = 0;
  if (cJSON_IsString(s)) { const char *v = s->valuestring;
    r = strstr(v, "speaker") || strstr(v, "headphone") || strstr(v, "bt_") || strstr(v, "blue") || !strcmp(v, "tv_external_speaker") ? 1 : (strstr(v, "arc") || strstr(v, "optical") || strstr(v, "soundbar") || strstr(v, "lineout") || strstr(v, "external")) ? 2 : 0;
    fprintf(stderr, "[tvtune] sound goes to %s (%s)\n", v, r == 1 ? "the TV's speakers" : r == 2 ? "a soundbar or receiver" : "not known"); }
  cJSON_Delete(o);
  pthread_mutex_lock(&mu); soundOut = r; pthread_mutex_unlock(&mu);
  return r;
}

/* ---- the worker ---- */
static void restore(cJSON *s) {
  int ok = 1; cJSON *c; cJSON_ArrayForEach(c, s) if (cJSON_IsObject(c) && c->string) ok &= set_keys(c->string, c, NULL);
  fprintf(stderr, "[tvtune] picture and sound %s\n", ok ? "put back" : "not all put back (tried again at the next start)");
  if (ok) { pthread_mutex_lock(&mu); clearSaved = 1; pthread_mutex_unlock(&mu); }
}
static void *stop_job(void *ud) { cJSON *s = ud; restore(s); cJSON_Delete(s); return NULL; }
static void wish_sound(Job *j, int out);
static void *start_job(void *ud) {
  Job *j = ud; static const char *const CATS[2] = { "picture", "sound" };
  if (j->sAuto || j->even) wish_sound(j, j->sAuto ? read_sound_out() : 0);
  cJSON *old = cJSON_CreateObject(); int reached = 0, upOk = 0, changed = 0;
  for (int c = 0; c < 2; c++) { int need = 0; for (int i = 0; i < j->n; i++) need |= !strcmp(j->w[i].cat, CATS[c]); if (!need) continue;
    pthread_mutex_lock(&mu); int still = j->g == gen; pthread_mutex_unlock(&mu); if (!still) break;
    cJSON *o = get_cat(CATS[c], j, NULL), *have = o ? cJSON_GetObjectItemCaseSensitive(o, "settings") : NULL; if (cJSON_IsObject(have)) reached = 1;
    cJSON *nw = cJSON_CreateObject(), *was = cJSON_CreateObject();
    for (int i = 0; i < j->n && cJSON_IsObject(have); i++) { const Want *w = &j->w[i]; if (strcmp(w->cat, CATS[c])) continue;
      const cJSON *v = cJSON_GetObjectItemCaseSensitive(have, w->k); if (!cJSON_IsString(v) || !clean(v->valuestring) || cJSON_GetObjectItemCaseSensitive(nw, w->k)) continue;
      char val[160];
      if (w->rel) { char *e; long cur = strtol(v->valuestring, &e, 10); if (e == v->valuestring || *e) continue; long t = cur + atoi(w->v); if (t < w->lo) t = w->lo; if (t > w->hi) t = w->hi; if (t == cur) continue; snprintf(val, sizeof val, "%ld", t); }
      else snprintf(val, sizeof val, "%s", w->v);
      { char first[48]; snprintf(first, sizeof first, "%.*s", (int)strcspn(val, "|"), val); if (!strcmp(v->valuestring, first)) { if (!strcmp(w->k, "superResolution")) upOk = 1; continue; } } /* already so */
      cJSON_AddStringToObject(nw, w->k, val); cJSON_AddStringToObject(was, w->k, v->valuestring); }
    cJSON *took = cJSON_CreateObject(); set_keys(CATS[c], nw, took);
    cJSON *kept = cJSON_CreateObject(); const cJSON *x; cJSON_ArrayForEach(x, took) { const cJSON *b = cJSON_GetObjectItemCaseSensitive(was, x->string); if (cJSON_IsString(b)) { cJSON_AddStringToObject(kept, x->string, b->valuestring); changed++; if (!strcmp(x->string, "superResolution")) upOk = 1; } }
    if (cJSON_GetArraySize(kept)) cJSON_AddItemToObject(old, CATS[c], kept); else cJSON_Delete(kept);
    cJSON_Delete(took); cJSON_Delete(nw); cJSON_Delete(was); cJSON_Delete(o); }
  fprintf(stderr, "[tvtune] %s: %d setting(s) changed\n", reached ? "tuned" : "the TV's settings are not open to the app: it stays as it is", changed);
  pthread_mutex_lock(&mu);
  if (reached) avail = 1; else if (avail < 0) avail = 0;
  int stale = j->g != gen; /* the player closed, or another level was chosen, while it worked */
  if (!stale) { if (cJSON_GetArraySize(old)) { cJSON_Delete(saved); saved = old; old = NULL; cJSON_Delete(toSave); toSave = cJSON_Duplicate(saved, 1); on = 1; }
    if (j->upscale && upOk) { notice = 1; snprintf(noteTxt, sizeof noteTxt, "%s", j->note); } }
  pthread_mutex_unlock(&mu);
  if (stale && cJSON_GetArraySize(old)) restore(old); /* its own changes back at once, here: before the next job reads the TV */
  cJSON_Delete(old); free(j);
  return NULL;
}
/* one worker, the jobs in the order asked: what was put back is back before the next title reads it */
static pthread_cond_t qcv = PTHREAD_COND_INITIALIZER; static void *(*qfn[16])(void *); static void *qarg[16]; static int qh, qn, worker;
static void *run(void *ud) { (void)ud;
  for (;;) { pthread_mutex_lock(&mu); while (!qn) pthread_cond_wait(&qcv, &mu); void *(*fn)(void *) = qfn[qh]; void *a = qarg[qh]; qh = (qh + 1) % 16; qn--; pthread_mutex_unlock(&mu); fn(a); } return NULL; }
static void spawn(void *(*fn)(void *), void *arg) {
  pthread_mutex_lock(&mu);
  if (!worker) { pthread_t t; if (!pthread_create(&t, NULL, run, NULL)) { pthread_detach(t); worker = 1; } }
  if (worker && qn < 16) { int i = (qh + qn) % 16; qfn[i] = fn; qarg[i] = arg; qn++; pthread_cond_signal(&qcv); pthread_mutex_unlock(&mu); return; }
  pthread_mutex_unlock(&mu);
  if (fn == stop_job) cJSON_Delete(arg); else free(arg); /* (no thread: nothing done) */
}

/* ---- what is asked ---- */
static void add(Job *j, const char *cat, const char *k, const char *v) { if (j->n < MAXW) j->w[j->n++] = (Want){ cat, k, v, 0, 0, 0 }; }
static void addr(Job *j, const char *cat, const char *k, const char *step, int lo, int hi) { if (j->n < MAXW) j->w[j->n++] = (Want){ cat, k, step, 1, lo, hi }; }
static void wish(Job *j, int anime) {
  const char *lv = tvtune_level(anime); int L = !strcmp(lv, "A") ? 4 : !strcmp(lv, "B") ? 3 : !strcmp(lv, "C") ? 2 : !strcmp(lv, "D") ? 1 : 0;
  if (L) { j->upscale = 1; snprintf(j->note, sizeof j->note, "%s", T1(anime ? "Anime upscale: {l}" : "Upscale: {l}", "l", lv));
    static const char *const SR[5] = { "off", "low", "medium", "medium", "high" };
    add(j, "picture", "superResolution", SR[L]);
    if (anime) { /* drawn pictures: clean lines, flat fills without bands, no smearing of fine line work */
      add(j, "picture", "smoothGradation", L >= 4 ? "high" : L >= 3 ? "medium" : "low");
      if (L >= 2) add(j, "picture", "mpegNoiseReduction", L >= 4 ? "medium" : "low");
      if (L >= 3) add(j, "picture", "noiseReduction", "off");
      if (L >= 3) addr(j, "picture", "sharpness", L >= 4 ? "4" : "2", 0, 50);
      if (L >= 4) add(j, "picture", "edgeEnhancer", "on"); }
    else { /* filmed pictures: detail up, the grain left alone, the film's own motion kept */
      if (L >= 2) addr(j, "picture", "sharpness", L >= 4 ? "8" : L >= 3 ? "5" : "3", 0, 50);
      if (L >= 3) { add(j, "picture", "noiseReduction", "off"); add(j, "picture", "mpegNoiseReduction", "low"); add(j, "picture", "edgeEnhancer", "on"); }
      if (L >= 4) add(j, "picture", "realCinema", "on"); } }
}
/* the sound for where it goes: the TV's speakers get the dialogue lifted and the TV's own sound engine for films; a
   soundbar or receiver gets the film's own sound passed through untouched (its Dolby / Atmos decoded there) */
static void wish_sound(Job *j, int out) {
  if (out == 2) { add(j, "sound", "digitalSoundOutput", "passThrough|auto"); return; }
  if (out == 1) { add(j, "sound", "soundMode", "aiSoundPlus|aiSound|cinema|movie");
    if (j->voice) add(j, "sound", "clearVoice", "level2|on"); }
  if (j->even) add(j, "sound", "autoVolume", "on");
}

void tvtune_start(int anime) {
  if (tvtune_state() == 0) return; /* the TV refused the app once this session: not asked again */
  Job *j = calloc(1, sizeof *j); if (!j) return; wish(j, anime);
  j->sAuto = !strcmp(store_get("soundTune", "auto"), "auto"); j->voice = is_on("voiceClear", "true"); j->even = is_on("evenVolume", "false");
  if (!j->n && !j->sAuto && !j->even) { free(j); return; }
  pthread_mutex_lock(&mu); int go = !on; if (go) j->g = ++gen; pthread_mutex_unlock(&mu); /* (one still working becomes stale: it puts back its own) */
  if (go) spawn(start_job, j); else free(j);
}
void tvtune_stop(void) {
  pthread_mutex_lock(&mu); gen++; notice = 0; cJSON *s = on ? saved : NULL; if (on) { saved = NULL; on = 0; } pthread_mutex_unlock(&mu);
  if (s) spawn(stop_job, s);
}
void tvtune_cycle(int anime) {
  static const char *const NEXT[5][2] = { { "off", "D" }, { "D", "C" }, { "C", "B" }, { "B", "A" }, { "A", "off" } };
  const char *cur = tvtune_level(anime); for (int i = 0; i < 5; i++) if (!strcmp(cur, NEXT[i][0])) { store_set(anime ? "animeEnhance" : "filmEnhance", NEXT[i][1]); break; }
  tvtune_stop();
  tvtune_start(anime);
}
int tvtune_poll(char *msg, size_t cap) { pthread_mutex_lock(&mu); int n = notice; notice = 0; if (n) snprintf(msg, cap, "%s", noteTxt); pthread_mutex_unlock(&mu); return n; }
int tvtune_state(void) { pthread_mutex_lock(&mu); int a = avail; pthread_mutex_unlock(&mu); return a; }
int tvtune_sound_out(void) { pthread_mutex_lock(&mu); int s = soundOut; pthread_mutex_unlock(&mu); return s; }
static void *probe_job(void *ud) { (void)ud; read_sound_out(); return NULL; }
void tvtune_tick(void) {
  static int first = 1; if (first) { first = 0;
    cJSON *s = store_node(SAVED); if (cJSON_IsObject(s) && cJSON_GetArraySize(s)) { fprintf(stderr, "[tvtune] left tuned when the app closed: putting it back\n"); spawn(stop_job, cJSON_Duplicate(s, 1)); }
    spawn(probe_job, NULL); } /* (once, a moment after start: where the sound goes, before the first film) */
  pthread_mutex_lock(&mu); cJSON *s = toSave; toSave = NULL; int c = clearSaved; clearSaved = 0; pthread_mutex_unlock(&mu);
  if (s) { store_set_node(SAVED, s); store_flush(); }
  else if (c && !on) { store_remove(SAVED); store_flush(); }
}
void tvtune_quit(void) {
  pthread_mutex_lock(&mu); cJSON *s = on ? saved : NULL; if (on) { saved = NULL; on = 0; } gen++; pthread_mutex_unlock(&mu);
  if (s) { int ok = 1; cJSON *c; cJSON_ArrayForEach(c, s) if (cJSON_IsObject(c) && c->string) ok &= set_keys(c->string, c, NULL); if (ok) store_remove(SAVED); cJSON_Delete(s); }
}
