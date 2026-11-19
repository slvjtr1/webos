/* tests/tvtune: the TV's picture and sound tuning against a fake settings service (no TV). Build: tests/tvtune/run.sh
   Checks after every case both the fake TV's values and the record (tvTuneSaved.json in the data folder). */
#include "core/tvtune.h"
#include "cJSON.h"
#include <pthread.h>
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

void tvtune_test_idle(void);
void tvtune_test_die(void);

/* ---- the fake TV ---- */
static pthread_mutex_t fm = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t fc = PTHREAD_COND_INITIALIZER;
static cJSON *tv, *initial;
static char refuse[8][48]; static int nrefuse, failBulk, setCalls, gateAt, gateHeld, gateOpen, snapAt;
static char snapRec[8192]; static cJSON *snapTv;
static char dataDir[512] = "build/tvtune_data", recPath[600];

static const char *INIT = "{\"picture\":{\"pictureMode\":\"cinema\",\"superResolution\":\"off\",\"smoothGradation\":\"off\",\"mpegNoiseReduction\":\"off\",\"noiseReduction\":\"medium\",\"sharpness\":\"10\",\"edgeEnhancer\":\"off\",\"realCinema\":\"off\"},"
  "\"sound\":{\"soundOutput\":\"tv_speaker\",\"soundMode\":\"standard\",\"clearVoice\":\"off\",\"autoVolume\":\"off\",\"digitalSoundOutput\":\"pcm\"}}";

static int refused(const char *cat, const char *k) { char ck[64]; snprintf(ck, sizeof ck, "%s.%s", cat, k); for (int i = 0; i < nrefuse; i++) if (!strcmp(refuse[i], ck)) return 1; return 0; }
static char *read_file(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return NULL; static char b[8192]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0; char *r = malloc(n + 1); memcpy(r, b, n + 1); return r; }

char *plat_luna_call(const char *uri, const char *payload, int timeoutMs) {
  (void)timeoutMs;
  if (strstr(uri, "hbchannel")) return strdup("{\"returnValue\":false,\"errorText\":\"no root\"}");
  cJSON *p = cJSON_Parse(payload); const char *cat = cJSON_GetObjectItem(p, "category")->valuestring;
  char *out;
  if (strstr(uri, "getSystemSettings")) {
    pthread_mutex_lock(&fm); cJSON *r = cJSON_CreateObject(), *s = cJSON_AddObjectToObject(r, "settings"), *k; cJSON *c = cJSON_GetObjectItem(tv, cat);
    cJSON_ArrayForEach(k, cJSON_GetObjectItem(p, "keys")) { cJSON *v = cJSON_GetObjectItem(c, k->valuestring); if (v) cJSON_AddStringToObject(s, k->valuestring, v->valuestring); }
    pthread_mutex_unlock(&fm); cJSON_AddTrueToObject(r, "returnValue"); out = cJSON_PrintUnformatted(r); cJSON_Delete(r);
  } else {
    cJSON *set = cJSON_GetObjectItem(p, "settings"), *k; int ok = 1;
    pthread_mutex_lock(&fm);
    int call = ++setCalls;
    if (gateAt && call == gateAt) { gateHeld = 1; pthread_cond_broadcast(&fc); while (!gateOpen) pthread_cond_wait(&fc, &fm); gateHeld = 0; }
    if (failBulk && cJSON_GetArraySize(set) > 1) ok = 0;
    cJSON *c = cJSON_GetObjectItem(tv, cat);
    cJSON_ArrayForEach(k, set) if (refused(cat, k->string) || !cJSON_GetObjectItem(c, k->string)) ok = 0;
    if (ok) cJSON_ArrayForEach(k, set) cJSON_ReplaceItemInObject(c, k->string, cJSON_CreateString(k->valuestring));
    if (ok && snapAt && call == snapAt) { char *r = read_file(recPath); snprintf(snapRec, sizeof snapRec, "%s", r ? r : ""); free(r); cJSON_Delete(snapTv); snapTv = cJSON_Duplicate(tv, 1); }
    pthread_mutex_unlock(&fm);
    out = strdup(ok ? "{\"returnValue\":true}" : "{\"returnValue\":false,\"errorText\":\"refused\"}");
  }
  cJSON_Delete(p); return out;
}

/* ---- the store and words (main thread only, as in the app) ---- */
static cJSON *st;
const char *store_get(const char *k, const char *d) { cJSON *v = cJSON_GetObjectItem(st, k); return cJSON_IsString(v) ? v->valuestring : d; }
void store_set(const char *k, const char *v) { cJSON_DeleteItemFromObject(st, k); cJSON_AddStringToObject(st, k, v); }
cJSON *store_node(const char *k) { return cJSON_GetObjectItem(st, k); }
void store_set_node(const char *k, cJSON *n) { cJSON_DeleteItemFromObject(st, k); cJSON_AddItemToObject(st, k, n); }
void store_remove(const char *k) { cJSON_DeleteItemFromObject(st, k); }
void store_flush(void) {}
const char *store_data_dir(void) { return dataDir; }
const char *T1(const char *en, const char *k, const char *v) { (void)k; static char b[96]; snprintf(b, sizeof b, "%.*s%s", (int)(strstr(en, "{") - en), en, v); return b; }

/* ---- checks ---- */
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const char *tvv(const char *cat, const char *k) { pthread_mutex_lock(&fm); const char *v = cJSON_GetObjectItem(cJSON_GetObjectItem(tv, cat), k)->valuestring; pthread_mutex_unlock(&fm); return v; }
static int tv_is_initial(void) { pthread_mutex_lock(&fm); int e = cJSON_Compare(tv, initial, 1); pthread_mutex_unlock(&fm); return e; }
static cJSON *rec(void) { char *r = read_file(recPath); cJSON *j = r ? cJSON_Parse(r) : NULL; free(r); return j; }
static const char *recv_(cJSON *r, const char *cat, const char *k) { cJSON *v = cJSON_GetObjectItem(cJSON_GetObjectItem(r, cat), k); return cJSON_IsString(v) ? v->valuestring : NULL; }
static int rec_count(cJSON *r) { int n = 0; cJSON *c; cJSON_ArrayForEach(c, r) n += cJSON_GetArraySize(c); return n; }

static void reset(void) {
  tvtune_test_die();
  pthread_mutex_lock(&fm); cJSON_Delete(tv); tv = cJSON_Parse(INIT); nrefuse = failBulk = setCalls = gateAt = gateHeld = gateOpen = snapAt = 0; snapRec[0] = 0; cJSON_Delete(snapTv); snapTv = NULL; pthread_mutex_unlock(&fm);
  cJSON_Delete(st); st = cJSON_Parse("{\"animeEnhance\":\"B\",\"filmEnhance\":\"off\",\"soundTune\":\"auto\",\"voiceClear\":\"true\"}");
  snprintf(dataDir, sizeof dataDir, "build/tvtune_data"); snprintf(recPath, sizeof recPath, "%s/tvTuneSaved.json", dataDir); remove(recPath);
}
static void refuse_add(const char *ck) { pthread_mutex_lock(&fm); snprintf(refuse[nrefuse++], 48, "%s", ck); pthread_mutex_unlock(&fm); }
static void refuse_clear(void) { pthread_mutex_lock(&fm); nrefuse = 0; pthread_mutex_unlock(&fm); }

/* anime B on the fake TV: picture bulk, sound key by key */
static void check_anime_b(void) {
  CHECK(!strcmp(tvv("picture", "superResolution"), "medium"), "superResolution %s", tvv("picture", "superResolution"));
  CHECK(!strcmp(tvv("picture", "mpegNoiseReduction"), "low"), "mpegNR");
  CHECK(!strcmp(tvv("picture", "noiseReduction"), "off"), "NR");
  CHECK(!strcmp(tvv("picture", "sharpness"), "12"), "sharpness %s", tvv("picture", "sharpness"));
  CHECK(!strcmp(tvv("sound", "soundMode"), "aiSoundPlus"), "soundMode %s", tvv("sound", "soundMode"));
  CHECK(!strcmp(tvv("sound", "clearVoice"), "level2"), "clearVoice");
}

int main(void) {
  mkdir_("build"); mkdir_(dataDir); snprintf(recPath, sizeof recPath, "%s/tvTuneSaved.json", dataDir);
  initial = cJSON_Parse(INIT);

  printf("full success\n"); reset();
  tvtune_start(1); tvtune_test_idle(); check_anime_b();
  CHECK(!strcmp(tvv("picture", "smoothGradation"), "medium"), "smoothGradation");
  { cJSON *r = rec(); CHECK(r && rec_count(r) == 7, "record has the 7 changed keys (%d)", r ? rec_count(r) : -1);
    CHECK(recv_(r, "picture", "sharpness") && !strcmp(recv_(r, "picture", "sharpness"), "10"), "sharpness before = 10");
    CHECK(recv_(r, "sound", "soundMode") && !strcmp(recv_(r, "sound", "soundMode"), "standard"), "soundMode before");
    CHECK(!recv_(r, "picture", "edgeEnhancer"), "unchanged key not recorded"); cJSON_Delete(r); }
  { char m[96]; CHECK(tvtune_poll(m, sizeof m) && strstr(m, "B"), "notice after the TV took it"); }
  tvtune_stop(); tvtune_test_idle();
  CHECK(tv_is_initial(), "TV back as it was"); { cJSON *r = rec(); CHECK(!r, "record removed"); cJSON_Delete(r); }

  printf("one key refused when tuning, one when putting back\n"); reset();
  refuse_add("picture.smoothGradation");
  tvtune_start(1); tvtune_test_idle(); check_anime_b();
  CHECK(!strcmp(tvv("picture", "smoothGradation"), "off"), "refused key unchanged");
  { cJSON *r = rec(); CHECK(r && !recv_(r, "picture", "smoothGradation") && rec_count(r) == 6, "refused key not in the record (%d)", r ? rec_count(r) : -1); cJSON_Delete(r); }
  refuse_clear(); refuse_add("picture.sharpness");
  tvtune_stop(); tvtune_test_idle();
  CHECK(!strcmp(tvv("picture", "sharpness"), "12"), "sharpness not put back (refused)");
  CHECK(!strcmp(tvv("picture", "superResolution"), "off") && !strcmp(tvv("sound", "soundMode"), "standard"), "the others put back");
  { cJSON *r = rec(); CHECK(r && rec_count(r) == 1 && recv_(r, "picture", "sharpness") && !strcmp(recv_(r, "picture", "sharpness"), "10"), "record keeps only sharpness = 10 (%d)", r ? rec_count(r) : -1); cJSON_Delete(r); }
  /* still refused at the next title: its first value before is kept, not the tuned one */
  tvtune_start(1); tvtune_test_idle();
  { cJSON *r = rec(); CHECK(recv_(r, "picture", "sharpness") && !strcmp(recv_(r, "picture", "sharpness"), "10"), "sharpness before still 10, not 12 (%s)", recv_(r, "picture", "sharpness") ? recv_(r, "picture", "sharpness") : "-"); cJSON_Delete(r); }
  refuse_clear(); tvtune_stop(); tvtune_test_idle();
  CHECK(tv_is_initial(), "all back once the TV takes it"); { cJSON *r = rec(); CHECK(!r, "record removed"); cJSON_Delete(r); }

  printf("restart after the first change\n"); reset();
  snapAt = 1; tvtune_start(1); tvtune_test_idle();
  { cJSON *s = cJSON_Parse(snapRec); CHECK(s != NULL, "the record was on disk before the first change");
    cJSON *c; cJSON_ArrayForEach(c, snapTv) { cJSON *k; cJSON_ArrayForEach(k, c) { const char *was = cJSON_GetObjectItem(cJSON_GetObjectItem(initial, c->string), k->string)->valuestring;
      if (strcmp(was, k->valuestring)) CHECK(recv_(s, c->string, k->string) && !strcmp(recv_(s, c->string, k->string), was), "%s.%s changed by the first write, its value before on disk", c->string, k->string); } }
    /* the app dies right there: the TV as after that write, the record file as it was then */
    tvtune_test_die();
    pthread_mutex_lock(&fm); cJSON_Delete(tv); tv = cJSON_Duplicate(snapTv, 1); pthread_mutex_unlock(&fm);
    FILE *f = fopen(recPath, "wb"); fputs(snapRec, f); fclose(f); cJSON_Delete(s); }
  CHECK(!tv_is_initial(), "(the TV was left tuned)");
  tvtune_tick(); tvtune_test_idle();
  CHECK(tv_is_initial(), "next start puts the TV back"); { cJSON *r = rec(); CHECK(!r, "record removed"); cJSON_Delete(r); }

  printf("level changed while a change is pending\n"); reset();
  gateAt = 1; tvtune_start(1);
  pthread_mutex_lock(&fm); while (!gateHeld) pthread_cond_wait(&fc, &fm); pthread_mutex_unlock(&fm);
  tvtune_cycle(1); /* B -> A while the worker waits on the TV */
  CHECK(!strcmp(store_get("animeEnhance", ""), "A"), "level now A");
  pthread_mutex_lock(&fm); gateOpen = 1; pthread_cond_broadcast(&fc); pthread_mutex_unlock(&fm);
  tvtune_test_idle();
  CHECK(!strcmp(tvv("picture", "superResolution"), "high") && !strcmp(tvv("picture", "edgeEnhancer"), "on") && !strcmp(tvv("picture", "sharpness"), "14"), "TV at level A (SR %s, sharp %s)", tvv("picture", "superResolution"), tvv("picture", "sharpness"));
  { cJSON *r = rec(); CHECK(recv_(r, "picture", "sharpness") && !strcmp(recv_(r, "picture", "sharpness"), "10") && recv_(r, "picture", "superResolution") && !strcmp(recv_(r, "picture", "superResolution"), "off"), "record holds the real values before, not level B's");
    CHECK(recv_(r, "picture", "edgeEnhancer") && !strcmp(recv_(r, "picture", "edgeEnhancer"), "off"), "edgeEnhancer before"); cJSON_Delete(r); }
  tvtune_stop(); tvtune_test_idle();
  CHECK(tv_is_initial(), "TV back as it was"); { cJSON *r = rec(); CHECK(!r, "record removed"); cJSON_Delete(r); }

  printf("start, stop, start in a row\n"); reset();
  tvtune_start(1); tvtune_stop(); tvtune_start(1); tvtune_test_idle(); check_anime_b();
  { cJSON *r = rec(); CHECK(r && rec_count(r) == 7 && !strcmp(recv_(r, "picture", "sharpness"), "10"), "one record, the first values before"); cJSON_Delete(r); }
  tvtune_stop(); tvtune_test_idle(); CHECK(tv_is_initial(), "back"); { cJSON *r = rec(); CHECK(!r, "record removed"); cJSON_Delete(r); }

  printf("the record cannot be written\n"); reset();
  snprintf(dataDir, sizeof dataDir, "build/tvtune_data/missing/dir");
  tvtune_start(1); tvtune_test_idle();
  CHECK(tv_is_initial(), "nothing changed without a record");
  tvtune_stop(); tvtune_test_idle();

  printf("record from 2.0.0 beta (in the store)\n"); reset();
  pthread_mutex_lock(&fm); cJSON_ReplaceItemInObject(cJSON_GetObjectItem(tv, "picture"), "sharpness", cJSON_CreateString("30")); pthread_mutex_unlock(&fm);
  store_set_node("tvTuneSaved", cJSON_Parse("{\"picture\":{\"sharpness\":\"10\"}}"));
  tvtune_tick(); tvtune_test_idle(); tvtune_tick();
  CHECK(tv_is_initial(), "put back from the old record"); CHECK(!store_node("tvTuneSaved"), "old store key removed");
  { cJSON *r = rec(); CHECK(!r, "record removed"); cJSON_Delete(r); }

  printf("app closes while tuned, one key refused\n"); reset();
  tvtune_start(1); tvtune_test_idle(); refuse_add("sound.clearVoice");
  tvtune_quit();
  CHECK(!strcmp(tvv("sound", "clearVoice"), "level2") && !strcmp(tvv("picture", "sharpness"), "10"), "quit put back all but the refused key");
  { cJSON *r = rec(); CHECK(r && rec_count(r) == 1 && recv_(r, "sound", "clearVoice") && !strcmp(recv_(r, "sound", "clearVoice"), "off"), "record keeps clearVoice = off"); cJSON_Delete(r); }
  tvtune_test_die(); refuse_clear(); tvtune_tick(); tvtune_test_idle();
  CHECK(tv_is_initial(), "next start puts the last key back"); { cJSON *r = rec(); CHECK(!r, "record removed"); cJSON_Delete(r); }

  printf("app closes while tuned, all taken\n"); reset();
  tvtune_start(0 /* film: off */); tvtune_start(1); tvtune_test_idle();
  tvtune_quit(); CHECK(tv_is_initial(), "quit put everything back"); { cJSON *r = rec(); CHECK(!r, "record removed"); cJSON_Delete(r); }

  tvtune_test_die();
  printf(fails ? "tvtune: %d of %d checks FAILED\n" : "tvtune: all %d checks passed\n", fails ? fails : checks, checks);
  return fails != 0;
}
