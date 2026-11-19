#include "core/tvtune.h"
#include "core/store.h"
#include "core/i18n.h"
#ifdef TVTUNE_TEST
char *plat_luna_call(const char *uri, const char *payload, int timeoutMs); /* (tests/tvtune: a fake settings service) */
#else
#include "platform/plat.h"
#endif
#include "cJSON.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#define fsync _commit
#define strtok_r strtok_s
#endif

#define SET "luna://com.webos.settingsservice/setSystemSettings"
#define GET "luna://com.webos.settingsservice/getSystemSettings"
#define EXEC "luna://org.webosbrew.hbchannel.service/exec"
#define LEGACY "tvTuneSaved"         /* 2.0.0 beta kept the record in the store; moved to its own file at the first start */
#define JOURNAL "tvTuneSaved.json"   /* { category: { key: the TV's value before } }: every value changed and not yet put back */
#define MAXW 24

/* How it stays safe:
   - One worker does every call to the TV and every write of the record, one pass at a time (opMu). The main thread
     only says what it wants (a title playing with a level, or nothing) and the worker brings the TV to it.
   - Before a setting is changed, its value before is in the record on disk (written, synced, renamed); a record that
     cannot be written means the setting is not changed. The store is not used for it: it belongs to the main thread
     and its writes are not synced.
   - Putting back is per key: a key put back leaves the record, one the TV refused stays in it and is tried again at
     the next start, stop or app start. A key changed twice keeps its first value before.
   - The app closing waits for the worker's pass, then puts back what the record holds (the same rule). */

typedef struct { const char *cat, *k, *v; int rel, lo, hi; } Want; /* rel: v is a step from the TV's own value, kept within lo..hi; v may list choices "a|b|c", the first the TV takes */
typedef struct { int n, upscale, sAuto, voice, even; char note[96]; Want w[MAXW]; } Job; /* sAuto, voice, even: the sound settings, read on the main thread */

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;   /* what is wanted, the notices */
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t opMu = PTHREAD_MUTEX_INITIALIZER; /* the TV and the record: one pass at a time */
static Job *want; static unsigned wantGen; static int work, busy, quitting, probeReq, notice, avail = -1, soundOut, legacyDone, worker;
static cJSON *legacy;                 /* the store's old record, handed to the worker once */
static pthread_t workerT;
static char noteTxt[96], jpath[1200];
/* the worker's own (opMu) */
static cJSON *pending; static int loaded; static unsigned appliedGen;
static int route = -1; /* how the TV is asked: -1 not found out, 0 the app itself, 1 through the Channel (root), 2 neither */

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
/* the route is found with a read (a refused write says nothing about access: the TV refuses keys it lacks). No
   answer at all leaves it unknown, to be found again at the next call */
static int find_route(void) {
  if (route >= 0) return route;
  static const char *const PROBE = "{\"category\":\"picture\",\"keys\":[\"pictureMode\"]}";
  cJSON *o = direct(GET, PROBE); int found = -1, answered = o != NULL;
  if (ok_of(o)) found = 0;
  else { cJSON *c = via_channel(GET, PROBE); if (ok_of(c)) found = 1; else if (answered || c) found = 2; cJSON_Delete(c); }
  cJSON_Delete(o);
  if (found >= 0) { route = found; fprintf(stderr, "[tvtune] the TV's settings: %s\n", found == 0 ? "the app may change them itself" : found == 1 ? "through the Homebrew Channel (root)" : "not open to the app on this TV"); }
  return found;
}
static cJSON *call(const char *uri, const char *payload) {
  int r = find_route();
  return r == 0 ? direct(uri, payload) : r == 1 ? via_channel(uri, payload) : NULL;
}

static int set_one(const char *cat, const char *k, const char *v) {
  char pl[600]; snprintf(pl, sizeof pl, "{\"category\":\"%s\",\"settings\":{\"%s\":\"%s\"}}", cat, k, v);
  cJSON *o = call(SET, pl); int ok = ok_of(o); cJSON_Delete(o); return ok;
}
static int set_cat(const char *cat, const cJSON *settings) {
  char *js = cJSON_PrintUnformatted(settings); if (!js) return 0;
  char pl[1400]; int n = snprintf(pl, sizeof pl, "{\"category\":\"%s\",\"settings\":%s}", cat, js); free(js);
  if (n >= (int)sizeof pl) return 0; /* (too long: key by key) */
  cJSON *o = call(SET, pl); int ok = ok_of(o); cJSON_Delete(o); return ok;
}
/* a whole category at once; refused, each key alone, and for a key with choices each choice in turn (a key this model
   lacks never stops the others). took: every key the TV took, with the value it took. Returns how many */
static int set_keys(const char *cat, const cJSON *settings, cJSON *took) {
  int n = 0; const cJSON *x;
  if (!cJSON_GetArraySize(settings)) return 0;
  int plain = 1; cJSON_ArrayForEach(x, settings) if (!cJSON_IsString(x) || strchr(x->valuestring, '|')) plain = 0;
  if (plain && cJSON_GetArraySize(settings) > 1 && set_cat(cat, settings)) { cJSON_ArrayForEach(x, settings) { cJSON_AddStringToObject(took, x->string, x->valuestring); n++; } return n; }
  cJSON_ArrayForEach(x, settings) {
    if (!cJSON_IsString(x)) continue;
    char ch[160]; snprintf(ch, sizeof ch, "%s", x->valuestring); int ok = 0; char *save = NULL;
    for (char *c = strtok_r(ch, "|", &save); c && !ok; c = strtok_r(NULL, "|", &save)) if (set_one(cat, x->string, c)) { ok = 1; n++; cJSON_AddStringToObject(took, x->string, c); }
    if (!ok) fprintf(stderr, "[tvtune] %s.%s not taken by this TV\n", cat, x->string); }
  return n;
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

/* ---- the record of values before (opMu) ---- */
static int record_write(void) {
  if (!jpath[0]) return 0;
  if (!pending || !cJSON_GetArraySize(pending)) return remove(jpath) == 0 || errno == ENOENT;
  char *t = cJSON_PrintUnformatted(pending); if (!t) return 0;
  char tmp[1240]; snprintf(tmp, sizeof tmp, "%s.tmp", jpath);
  size_t len = strlen(t); int ok = 0; FILE *f = fopen(tmp, "wb");
  if (f) { ok = fwrite(t, 1, len, f) == len && fflush(f) == 0 && fsync(fileno(f)) == 0; ok &= fclose(f) == 0; }
  free(t);
  if (ok) {
#ifdef _WIN32
    remove(jpath);
#endif
    ok = rename(tmp, jpath) == 0; }
#ifndef _WIN32
  if (ok) { char d[1200]; snprintf(d, sizeof d, "%s", jpath); char *sl = strrchr(d, '/'); if (sl) { *sl = 0; int fd = open(d, O_RDONLY); if (fd >= 0) { fsync(fd); close(fd); } } } /* (the rename itself on disk) */
#endif
  if (!ok) { remove(tmp); fprintf(stderr, "[tvtune] the record of the TV's settings could not be written\n"); }
  return ok;
}
static void record_load(void) {
  if (loaded) return; loaded = 1;
  if (!pending) pending = cJSON_CreateObject();
  FILE *f = jpath[0] ? fopen(jpath, "rb") : NULL; if (!f) return;
  char b[8192]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;
  cJSON *j = cJSON_Parse(b); cJSON *c;
  cJSON_ArrayForEach(c, j) if (cJSON_IsObject(c) && c->string) { cJSON *x; cJSON_ArrayForEach(x, c) if (cJSON_IsString(x) && x->string && clean(x->valuestring)) {
    cJSON *pc = cJSON_GetObjectItemCaseSensitive(pending, c->string); if (!pc) pc = cJSON_AddObjectToObject(pending, c->string);
    if (!cJSON_GetObjectItemCaseSensitive(pc, x->string)) cJSON_AddStringToObject(pc, x->string, x->valuestring); } }
  cJSON_Delete(j);
  if (cJSON_GetArraySize(pending)) fprintf(stderr, "[tvtune] left tuned when the app last closed: putting it back\n");
}
static void record_merge(const cJSON *s) { /* the old store record: kept values before win over nothing, never over the record's own */
  cJSON *c; cJSON_ArrayForEach(c, s) if (cJSON_IsObject(c) && c->string) { cJSON *x; cJSON_ArrayForEach(x, c) if (cJSON_IsString(x) && x->string && clean(x->valuestring)) {
    cJSON *pc = cJSON_GetObjectItemCaseSensitive(pending, c->string); if (!pc) pc = cJSON_AddObjectToObject(pending, c->string);
    if (!cJSON_GetObjectItemCaseSensitive(pc, x->string)) cJSON_AddStringToObject(pc, x->string, x->valuestring); } }
}
static void drop_empty(void) { cJSON *c = pending ? pending->child : NULL; while (c) { cJSON *nx = c->next; if (!cJSON_GetArraySize(c)) cJSON_DeleteItemFromObjectCaseSensitive(pending, c->string); c = nx; } }

/* every value in the record back on the TV: what the TV took leaves the record, the rest stays for the next try */
static int put_back(void) {
  if (!pending || !cJSON_GetArraySize(pending)) return 1;
  int left = 0; cJSON *c;
  cJSON_ArrayForEach(c, pending) {
    cJSON *took = cJSON_CreateObject(); set_keys(c->string, c, took);
    cJSON *x; cJSON_ArrayForEach(x, took) cJSON_DeleteItemFromObjectCaseSensitive(c, x->string);
    left += cJSON_GetArraySize(c); cJSON_Delete(took); }
  drop_empty(); record_write(); /* (not written: the file still holds more than is left, and putting those back again is harmless) */
  fprintf(stderr, left ? "[tvtune] picture and sound: %d setting(s) not put back yet (tried again later)\n" : "[tvtune] picture and sound put back\n", left);
  return !left;
}

static int still(unsigned g) { pthread_mutex_lock(&mu); int s = !quitting && wantGen == g && want; pthread_mutex_unlock(&mu); return s; }
static void wish_sound(Job *j, int out);
/* a title's settings: each value before into the record on disk, then the change; nothing changes when the record
   can't be written. Stops between categories and keys once something else is wanted */
static void apply(Job *j, unsigned g) {
  static const char *const CATS[2] = { "picture", "sound" };
  if (j->sAuto || j->even) wish_sound(j, j->sAuto ? read_sound_out() : 0);
  int reached = 0, upOk = 0, changed = 0;
  for (int c = 0; c < 2 && still(g); c++) { int need = 0; for (int i = 0; i < j->n; i++) need |= !strcmp(j->w[i].cat, CATS[c]); if (!need) continue;
    cJSON *o = get_cat(CATS[c], j, NULL), *have = o ? cJSON_GetObjectItemCaseSensitive(o, "settings") : NULL; if (cJSON_IsObject(have)) reached = 1;
    cJSON *nw = cJSON_CreateObject(), *added = cJSON_CreateObject();
    cJSON *pc = cJSON_GetObjectItemCaseSensitive(pending, CATS[c]);
    for (int i = 0; i < j->n && cJSON_IsObject(have); i++) { const Want *w = &j->w[i]; if (strcmp(w->cat, CATS[c])) continue;
      const cJSON *v = cJSON_GetObjectItemCaseSensitive(have, w->k); if (!cJSON_IsString(v) || !clean(v->valuestring) || cJSON_GetObjectItemCaseSensitive(nw, w->k)) continue;
      char val[160];
      if (w->rel) { char *e; long cur = strtol(v->valuestring, &e, 10); if (e == v->valuestring || *e) continue; long t = cur + atoi(w->v); if (t < w->lo) t = w->lo; if (t > w->hi) t = w->hi; if (t == cur) continue; snprintf(val, sizeof val, "%ld", t); }
      else snprintf(val, sizeof val, "%s", w->v);
      { char first[48]; snprintf(first, sizeof first, "%.*s", (int)strcspn(val, "|"), val); if (!strcmp(v->valuestring, first)) { if (!strcmp(w->k, "superResolution")) upOk = 1; continue; } } /* already so */
      cJSON_AddStringToObject(nw, w->k, val);
      if (!pc || !cJSON_GetObjectItemCaseSensitive(pc, w->k)) cJSON_AddStringToObject(added, w->k, v->valuestring); } /* (a key still in the record keeps its first value before) */
    if (cJSON_GetArraySize(nw)) {
      if (cJSON_GetArraySize(added)) { if (!pc) pc = cJSON_AddObjectToObject(pending, CATS[c]); cJSON *x; cJSON_ArrayForEach(x, added) cJSON_AddStringToObject(pc, x->string, x->valuestring); }
      if (!record_write()) { cJSON *x; cJSON_ArrayForEach(x, added) cJSON_DeleteItemFromObjectCaseSensitive(pc, x->string); drop_empty(); /* not on disk: not changed */ }
      else if (still(g)) {
        cJSON *took = cJSON_CreateObject(); changed += set_keys(CATS[c], nw, took);
        if (cJSON_GetObjectItemCaseSensitive(took, "superResolution")) upOk = 1;
        int drop = 0; cJSON *x; cJSON_ArrayForEach(x, added) if (!cJSON_GetObjectItemCaseSensitive(took, x->string)) { cJSON_DeleteItemFromObjectCaseSensitive(pc, x->string); drop = 1; } /* refused: the TV kept its own */
        if (drop) { drop_empty(); record_write(); }
        cJSON_Delete(took); } }
    cJSON_Delete(added); cJSON_Delete(nw); cJSON_Delete(o); }
  fprintf(stderr, "[tvtune] %s: %d setting(s) changed\n", reached ? "tuned" : "the TV's settings are not open to the app: it stays as it is", changed);
  pthread_mutex_lock(&mu);
  if (reached) avail = 1; else if (avail < 0) avail = 0;
  if (wantGen == g && j->upscale && upOk) { notice = 1; snprintf(noteTxt, sizeof noteTxt, "%s", j->note); }
  pthread_mutex_unlock(&mu);
}

/* one pass: the TV brought to what is wanted. Holding opMu */
static void pass(Job *j, unsigned g, cJSON *old, int probe) {
  record_load();
  if (old) { record_merge(old); if (record_write()) { pthread_mutex_lock(&mu); legacyDone = 1; pthread_mutex_unlock(&mu); } }
  if (!j || g != appliedGen) { put_back(); appliedGen = 0; }
  if (j && g != appliedGen && still(g)) { apply(j, g); appliedGen = g; }
  if (probe) read_sound_out();
}
static void *run(void *ud) { (void)ud;
  for (;;) {
    pthread_mutex_lock(&mu);
    while (!work && !quitting) pthread_cond_wait(&cv, &mu);
    if (quitting) { pthread_mutex_unlock(&mu); return NULL; }
    work = 0; busy = 1;
    Job *j = NULL; if (want) { j = malloc(sizeof *j); if (j) *j = *want; } unsigned g = wantGen;
    cJSON *old = legacy; legacy = NULL; int probe = probeReq; probeReq = 0;
    pthread_mutex_unlock(&mu);
    pthread_mutex_lock(&opMu); pass(j, j ? g : 0, old, probe); pthread_mutex_unlock(&opMu); /* (no memory for the copy: put back, the safe side) */
    free(j); cJSON_Delete(old);
    pthread_mutex_lock(&mu); busy = 0; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mu);
  }
}
/* main thread: the record's place, then the worker (a request is a state, not a queue entry: none is ever lost) */
static void kick(void) {
  pthread_mutex_lock(&mu);
  if (!jpath[0]) { const char *d = store_data_dir(); if (d && *d) snprintf(jpath, sizeof jpath, "%s/%s", d, JOURNAL); }
  if (!worker && !quitting) { if (!pthread_create(&workerT, NULL, run, NULL)) worker = 1; }
  work = 1; pthread_cond_broadcast(&cv);
  pthread_mutex_unlock(&mu);
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

/* main thread: a title starts. Already tuned for one (the player restarting its picture): left as it is */
void tvtune_start(int anime) {
  if (tvtune_state() == 0) return; /* the TV refused the app once this session: not asked again */
  Job j0; memset(&j0, 0, sizeof j0); wish(&j0, anime);
  j0.sAuto = !strcmp(store_get("soundTune", "auto"), "auto"); j0.voice = is_on("voiceClear", "true"); j0.even = is_on("evenVolume", "false");
  if (!j0.n && !j0.sAuto && !j0.even) return;
  Job *j = malloc(sizeof *j); if (!j) return; *j = j0;
  pthread_mutex_lock(&mu); int go = !want; if (go) { want = j; wantGen++; if (!wantGen) wantGen = 1; } pthread_mutex_unlock(&mu);
  if (go) kick(); else free(j);
}
void tvtune_stop(void) {
  pthread_mutex_lock(&mu); Job *j = want; want = NULL; wantGen++; if (!wantGen) wantGen = 1; notice = 0; pthread_mutex_unlock(&mu);
  free(j); kick();
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
static int tickFirst = 1;
void tvtune_tick(void) {
  if (tickFirst) { tickFirst = 0; /* a moment after start: the record put back (the app may have closed while tuned), and where the sound goes */
    cJSON *s = store_node(LEGACY);
    pthread_mutex_lock(&mu); if (cJSON_IsObject(s) && cJSON_GetArraySize(s)) legacy = cJSON_Duplicate(s, 1); else if (s) legacyDone = 1; probeReq = 1; pthread_mutex_unlock(&mu);
    kick(); }
  pthread_mutex_lock(&mu); int d = legacyDone; legacyDone = 0; pthread_mutex_unlock(&mu);
  if (d) { store_remove(LEGACY); store_flush(); } /* (only once the record file holds it) */
}
/* main thread, the app closing: the worker's pass ends (it stops between keys), then the record is put back here */
void tvtune_quit(void) {
  pthread_mutex_lock(&mu); Job *j = want; want = NULL; wantGen++; quitting = 1; pthread_cond_broadcast(&cv);
  if (!jpath[0]) { const char *d = store_data_dir(); if (d && *d) snprintf(jpath, sizeof jpath, "%s/%s", d, JOURNAL); }
  pthread_mutex_unlock(&mu); free(j);
  pthread_mutex_lock(&opMu); record_load(); put_back(); appliedGen = 0; pthread_mutex_unlock(&opMu);
}

#ifdef TVTUNE_TEST
/* tests: wait for the worker to be idle; forget everything held in memory as a process that died would (the record
   file stays), so the next tvtune_tick() behaves as the first after a start */
void tvtune_test_idle(void) { pthread_mutex_lock(&mu); while (work || busy) pthread_cond_wait(&cv, &mu); pthread_mutex_unlock(&mu); }
void tvtune_test_die(void) {
  pthread_mutex_lock(&mu); quitting = 1; pthread_cond_broadcast(&cv); int w = worker; pthread_mutex_unlock(&mu);
  if (w) pthread_join(workerT, NULL);
  pthread_mutex_lock(&mu); free(want); want = NULL; wantGen = 0; work = busy = quitting = probeReq = notice = legacyDone = worker = 0; avail = -1; soundOut = 0; cJSON_Delete(legacy); legacy = NULL; jpath[0] = 0; pthread_mutex_unlock(&mu);
  cJSON_Delete(pending); pending = NULL; loaded = 0; appliedGen = 0; route = -1; tickFirst = 1;
}
#endif
