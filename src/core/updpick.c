#include "core/updpick.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int upd_ver_cmp(const char *a, const char *b) {
  for (int i = 0; i < 3; i++) { long x = a ? strtol(a, NULL, 10) : 0, y = b ? strtol(b, NULL, 10) : 0;
    if (x != y) return x > y ? 1 : -1;
    a = a ? strchr(a, '.') : NULL; if (a) a++; b = b ? strchr(b, '.') : NULL; if (b) b++; }
  return 0;
}
static int ws(const char *s) { return *s == ' ' || *s == '\t' || *s == '\v' || *s == '\f' || *s == '\r' ? 1 : !strncmp(s, "\xc2\xa0", 2) ? 2 : 0; }
/* the release's bullet points as plain text, the first five: /^\s*[-*•]\s+(.+)$/, then no **, __, ` and [text](link) -> text */
int upd_notes_from(const char *md, char out[5][256]) {
  int n = 0; if (!md) return 0;
  for (const char *l = md; *l && n < 5;) {
    const char *e = strchr(l, '\n'); size_t len = e ? (size_t)(e - l) : strlen(l); if (len && l[len - 1] == '\r') len--;
    char line[1024]; if (len >= sizeof line) len = sizeof line - 1; memcpy(line, l, len); line[len] = 0;
    l = e ? e + 1 : l + strlen(l);
    char *p = line; int k; while ((k = ws(p))) p += k;
    if (*p == '-' || *p == '*') p++; else if (!strncmp(p, "\xe2\x80\xa2", 3)) p += 3; else continue;
    int nsp = 0; while ((k = ws(p))) { p += k; nsp++; } if (!nsp) continue;
    if (!*p && nsp < 2) continue; /* (.+) needs a character: one of the spaces when nothing else follows */
    char t[1024]; size_t o = 0;
    for (const char *q = p; *q && o + 1 < sizeof t;) {
      if (!strncmp(q, "**", 2) || !strncmp(q, "__", 2)) { q += 2; continue; }
      if (*q == '`') { q++; continue; }
      t[o++] = *q++; }
    t[o] = 0;
    char u[1024]; o = 0;
    for (const char *q = t; *q && o + 1 < sizeof u;) {
      if (*q == '[') { const char *c = q + 1; while (*c && *c != ']') c++;
        if (*c == ']' && c > q + 1 && c[1] == '(') { const char *d = c + 2; while (*d && *d != ')') d++;
          if (*d == ')') { size_t m = (size_t)(c - q - 1); if (o + m >= sizeof u) m = sizeof u - 1 - o; memcpy(u + o, q + 1, m); o += m; q = d + 1; continue; } } }
      u[o++] = *q++; }
    u[o] = 0;
    char *s = u; while ((k = ws(s))) s += k; size_t sl = strlen(s); while (sl && (s[sl - 1] == ' ' || s[sl - 1] == '\t' || s[sl - 1] == '\r')) s[--sl] = 0;
    snprintf(out[n++], 256, "%s", s);
  }
  return n;
}

static int ver_ok(const char *v) { /* ^\d{1,5}\.\d{1,5}\.\d{1,5}$ */
  for (int part = 0; part < 3; part++) { int d = 0; while (*v >= '0' && *v <= '9') { v++; if (++d > 5) return 0; } if (!d) return 0; if (part < 2) { if (*v != '.') return 0; v++; } }
  return !*v;
}
int upd_pkg_ok(const char *appId, const char *fileName, const char *ver, const char *url, const char *sha, char shaOut[72]) {
  if (!appId || !fileName || !ver || !url || !sha || !ver_ok(ver) || strncmp(url, "https://", 8) || strchr(url, '?') || strchr(url, '#') || strlen(url) >= 512) return 0;
  char want[160]; snprintf(want, sizeof want, "%s_%s_arm.ipk", appId, ver);
  if (strcmp(fileName, want)) return 0;
  size_t ul = strlen(url), fl = strlen(fileName); if (ul <= fl || strcmp(url + ul - fl, fileName) || url[ul - fl - 1] != '/') return 0; /* the link fetches that very file */
  if (strlen(sha) != 64) return 0;
  for (int i = 0; i < 64; i++) { char c = sha[i]; if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a'); if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0; if (shaOut) shaOut[i] = c; }
  if (shaOut) shaOut[64] = 0;
  return 1;
}

int upd_from_releases(const char *appId, const char *json, int beta, UpdRel *out) {
  cJSON *list = json ? cJSON_Parse(json) : NULL, *r, *a; int has = 0;
  if (!cJSON_IsArray(list)) { cJSON_Delete(list); return -1; }
  size_t pl = strlen(appId);
  memset(out, 0, sizeof *out);
  cJSON_ArrayForEach(r, list) {
    int isPre = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "prerelease"));
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "draft")) || (isPre && !beta)) continue;
    cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(r, "assets")) {
      cJSON *nm = cJSON_GetObjectItemCaseSensitive(a, "name"), *dg = cJSON_GetObjectItemCaseSensitive(a, "digest"), *du = cJSON_GetObjectItemCaseSensitive(a, "browser_download_url");
      if (!cJSON_IsString(nm) || !cJSON_IsString(dg) || !cJSON_IsString(du) || strncmp(dg->valuestring, "sha256:", 7)) continue;
      const char *n = nm->valuestring; size_t nl = strlen(n);
      if (nl <= pl + 9 || strncmp(n, appId, pl) || n[pl] != '_' || strcmp(n + nl - 8, "_arm.ipk")) continue;
      char ver[24]; if (nl - pl - 9 >= sizeof ver) continue; snprintf(ver, sizeof ver, "%.*s", (int)(nl - pl - 9), n + pl + 1);
      char sha[72]; if (!upd_pkg_ok(appId, n, ver, du->valuestring, dg->valuestring + 7, sha)) continue;
      if (has && upd_ver_cmp(ver, out->ver) <= 0) continue;
      memset(out, 0, sizeof *out); has = 1; out->beta = isPre;
      snprintf(out->ver, sizeof out->ver, "%s", ver); snprintf(out->url, sizeof out->url, "%s", du->valuestring); snprintf(out->sha, sizeof out->sha, "%s", sha);
      cJSON *b = cJSON_GetObjectItemCaseSensitive(r, "body"); out->nnotes = upd_notes_from(cJSON_IsString(b) ? b->valuestring : NULL, out->notes);
    }
  }
  cJSON_Delete(list);
  return has;
}

int upd_from_manifest(const char *appId, const char *json, int betaFile, UpdRel *out) {
  cJSON *m = json ? cJSON_Parse(json) : NULL; int ok = 0;
  memset(out, 0, sizeof *out);
  cJSON *id = cJSON_GetObjectItemCaseSensitive(m, "id"), *ch = cJSON_GetObjectItemCaseSensitive(m, "channel");
  cJSON *v = cJSON_GetObjectItemCaseSensitive(m, "version"), *u = cJSON_GetObjectItemCaseSensitive(m, "ipkUrl"), *h = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(m, "ipkHash"), "sha256");
  int chBeta = cJSON_IsString(ch) && !strcmp(ch->valuestring, "beta");
  if (cJSON_IsObject(m) && cJSON_IsString(v) && cJSON_IsString(u) && cJSON_IsString(h)
      && (!id || (cJSON_IsString(id) && !strcmp(id->valuestring, appId)))   /* (old files all carry it; none is taken without a match) */
      && (betaFile ? chBeta : !chBeta)) {
    const char *url = u->valuestring, *fn = strrchr(url, '/'); fn = fn ? fn + 1 : url;
    char sha[72];
    if (upd_pkg_ok(appId, fn, v->valuestring, url, h->valuestring, sha)) {
      ok = 1; out->beta = betaFile;
      snprintf(out->ver, sizeof out->ver, "%s", v->valuestring); snprintf(out->url, sizeof out->url, "%s", url); snprintf(out->sha, sizeof out->sha, "%s", sha);
      cJSON *ns = cJSON_GetObjectItemCaseSensitive(m, "notes"), *x;
      if (cJSON_IsArray(ns)) { cJSON_ArrayForEach(x, ns) if (out->nnotes < 5 && cJSON_IsString(x)) snprintf(out->notes[out->nnotes++], 256, "%s", x->valuestring); }
      else { cJSON *rn = cJSON_GetObjectItemCaseSensitive(m, "releaseNotes"); out->nnotes = upd_notes_from(cJSON_IsString(rn) ? rn->valuestring : NULL, out->notes); } }
  }
  cJSON_Delete(m);
  return ok;
}

const UpdRel *upd_newer(const UpdRel *a, const UpdRel *b) {
  if (!a) return b; if (!b) return a;
  int c = upd_ver_cmp(a->ver, b->ver); return c > 0 ? a : c < 0 ? b : (a->beta ? b : a); /* (the same version: the stable one) */
}
