#include "core/submarks.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>

volatile int sub_marks_on = 1;
volatile int sub_tashkeel_on = 0;

/* the length in bytes of an Arabic vowel mark (tashkeel) at p, 0 when it is not one: the fathatan to the sukun
   (U+064B-0652), the superscript alef (U+0670) and the Quranic annotation marks (U+0610-061A, U+06D6-06DC,
   U+06DF-06E4, U+06E7-06E8, U+06EA-06ED). The letters, the hamza forms and the tatweel stay */
static int tashkeel_len(const unsigned char *p) {
  if (p[0] == 0xd9 && ((p[1] >= 0x8b && p[1] <= 0x92) || p[1] == 0xb0)) return 2;
  if (p[0] == 0xd8 && p[1] >= 0x90 && p[1] <= 0x9a) return 2;
  if (p[0] == 0xdb && ((p[1] >= 0x96 && p[1] <= 0x9c) || (p[1] >= 0x9f && p[1] <= 0xa4) || p[1] == 0xa7 || p[1] == 0xa8 || (p[1] >= 0xaa && p[1] <= 0xad))) return 2;
  return 0;
}
void sub_tashkeel_clean(char *s) {
  if (!s) return;
  unsigned char *r = (unsigned char *)s, *w = r;
  while (*r) { int n = tashkeel_len(r); if (n) { r += n; continue; } *w++ = *r++; }
  *w = 0;
}

/* the length in bytes of the mark at p, 0 when it is not one */
static int mark_len(const unsigned char *p) {
  if (p[0] == '(' || p[0] == ')' || p[0] == '"') return 1;
  if (p[0] == 0xc2 && (p[1] == 0xab || p[1] == 0xbb)) return 2;                                   /* « » */
  if (p[0] == 0xe2 && p[1] == 0x80 && (p[2] == 0x9c || p[2] == 0x9d || p[2] == 0x9e || p[2] == 0x9f)) return 3; /* “ ” „ ‟ */
  if (p[0] == 0xef && p[1] == 0xb4 && (p[2] == 0xbe || p[2] == 0xbf)) return 3;                   /* ﴾ ﴿ */
  if (p[0] == 0xef && p[1] == 0xbc && (p[2] == 0x88 || p[2] == 0x89 || p[2] == 0x82)) return 3;   /* （ ） ＂ */
  return 0;
}

void sub_marks_clean(char *s) {
  if (!s) return;
  if (sub_tashkeel_on) sub_tashkeel_clean(s); /* (the option under it: the Arabic vowel marks too) */
  unsigned char *r = (unsigned char *)s, *w = r;
  while (*r) {
    int n = mark_len(r);
    if (!n) { *w++ = *r++; continue; }
    r += n;
    /* the mark stood between a word and the next one ("word(word)", "say"this"): a space keeps them apart */
    if (w > (unsigned char *)s && w[-1] != ' ' && w[-1] != '\n' && *r && *r != ' ' && *r != '\n' && !mark_len(r)) {
      unsigned char c = *r; if (c != '.' && c != ',' && c != '!' && c != '?' && c != ':' && c != ';') *w++ = ' '; }
  }
  *w = 0;
  /* the spaces a mark left: none twice, none at the start or the end of a line, none before . , ! ? : ; */
  r = w = (unsigned char *)s;
  while (*r) {
    if (*r == ' ') {
      while (r[1] == ' ') r++;
      int edge = w == (unsigned char *)s || w[-1] == '\n' || !r[1] || r[1] == '\n' || r[1] == '.' || r[1] == ',' || r[1] == '!' || r[1] == '?' || r[1] == ':' || r[1] == ';';
      r++; if (!edge) *w++ = ' '; continue; }
    *w++ = *r++;
  }
  *w = 0;
  /* lines left empty go, and the text is empty when every line was only marks */
  r = w = (unsigned char *)s;
  while (*r) {
    if (*r == '\n' && (w == (unsigned char *)s || w[-1] == '\n')) { r++; continue; }
    *w++ = *r++;
  }
  while (w > (unsigned char *)s && w[-1] == '\n') w--;
  *w = 0;
}

int sub_lang_arabic(const char *lang) {
  size_t n = strcspn(lang, "-_");
  return (n == 2 && !strncasecmp(lang, "ar", n)) ||
         (n == 3 && !strncasecmp(lang, "ara", n)) ||
         !strcasecmp(lang, "Arabic");
}

/* Handle the entities found in timed text, including double-escaped ampersands.
   Numeric references must be complete Unicode scalars. Unknown entities stay literal. */
static const char *cue_entity(const char *r, unsigned *value) {
  static const struct { const char *name; unsigned value; } names[] = {
    {"rlm;", 0x200f}, {"lrm;", 0x200e}, {"nbsp;", 32}, {"amp;", '&'},
    {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'}, {"apos;", 39},
    {"zwnj;", 0x200c}, {"zwj;", 0x200d}, {"hellip;", 0x2026}
  };
  const char *p = r + 1;
  while (!strncmp(p, "amp;", 4)) p += 4;
  if (*p == '#') {
    const char *d = p + 1; unsigned base = 10, cp = 0;
    if (*d == 'x' || *d == 'X') { base = 16; d++; }
    const char *first = d;
    for (; *d; d++) {
      unsigned v;
      if (*d >= '0' && *d <= '9') v = *d - '0';
      else if (*d >= 'a' && *d <= 'f') v = *d - 'a' + 10;
      else if (*d >= 'A' && *d <= 'F') v = *d - 'A' + 10;
      else break;
      if (v >= base || cp > (0x10ffffu - v) / base) break;
      cp = cp * base + v;
    }
    if (d != first && *d == ';' && cp >= 32 && !(cp >= 0xd800 && cp <= 0xdfff)) {
      *value = cp; return d + 1;
    }
  } else {
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
      size_t n = strlen(names[i].name);
      if (!strncmp(p, names[i].name, n)) { *value = names[i].value; return p + n; }
    }
  }
  if (p > r + 1) { *value = '&'; return p; }
  return NULL;
}

void sub_arabic_embedded_clean(char *text) {
  char *r = text, *w = text;
  while (*r) {
    unsigned cp;
    const char *end = *r == '&' ? cue_entity(r, &cp) : NULL;
    if (!end) { *w++ = *r++; continue; }
    if (cp < 128) *w++ = (char)cp;
    else {
      if (cp >= 0x10000) { *w++ = (char)(0xf0 | cp >> 18); *w++ = (char)(0x80 | ((cp >> 12) & 63)); }
      else if (cp >= 0x800) *w++ = (char)(0xe0 | cp >> 12);
      else *w++ = (char)(0xc0 | cp >> 6);
      if (cp >= 0x800) *w++ = (char)(0x80 | ((cp >> 6) & 63));
      *w++ = (char)(0x80 | (cp & 63));
    }
    r = (char *)end;
  }
  *w = 0;
  r = w = text;
  while (*r) {
    if (*r == '<') {
      char *name = r + 1; if (*name == '/') name++;
      size_t n = strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
      static const char *tags[] = {"br","b","i","u","s","font","span","c","v","ruby","rt"};
      int known = 0;
      for (size_t i = 0; i < sizeof tags / sizeof *tags; i++)
        if (n == strlen(tags[i]) && !strncasecmp(name, tags[i], n)) { known = 1; break; }
      if (known && (name[n] == '>' || name[n] == '/' || name[n] == '.' || isspace((unsigned char)name[n]))) {
        char *end = name + n + strcspn(name + n, "<>\n");
        if (*end == '>') { if (n == 2 && !strncasecmp(name, "br", 2)) *w++ = '\n'; r = end + 1; continue; }
      }
    }
    if (r[0] == '{' && r[1] == '\\') {
      char *end = r + 2 + strcspn(r + 2, "{}\n");
      if (*end == '}') { r = end + 1; continue; }
    }
    if (*r == '\r') { r++; if (*r != '\n') *w++ = '\n'; continue; }
    *w++ = *r++;
  }
  *w = 0;
}
