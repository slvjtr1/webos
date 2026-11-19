#include "core/submarks.h"
#include <string.h>

volatile int sub_marks_on = 1;

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
