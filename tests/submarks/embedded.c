/* Exercise the actual cue-ingestion path before the player sees a line. */
#include "../../src/core/mkvsubs.c"
#include <assert.h>

int main(void) {
  static MkvSession session;
  Track track = {0};
  AssHdr header = {0};
  TState state = {.tr = &track, .hdr = &header};
  const char *text = "<font color='#00ff00'>&rlm;مرحبا</font><br>سؤال؟";
  const char *ass = "1,0,Default,,0,0,0,,{\\an8\\b1}&amp;rlm;مرحبا\\Nسؤال؟";
  for (int kind = 1; kind <= 2; kind++) {
    const char *payload = kind == 1 ? text : ass;
    Block block = {.payload = (cu8 *)payload, .plen = strlen(payload), .dur = -1};
    track.kind = kind;
    for (int ar = 0; ar <= 1; ar++) {
      strcpy(track.lang, ar ? "ara" : "eng");
      Line line; char *raw;
      assert(make_line(&session, &state, &block, 5., 2., &line, &raw));
      assert(line.s == 5. && line.e == 7. && !line.hidden);
      if (ar) {
        assert(!strcmp(line.t, "\xe2\x80\x8fمرحبا\nسؤال؟"));
        assert(!line.an && !line.bold && !line.color && line.x == -1 && line.y == -1);
      } else if (kind == 1) assert(line.color && strstr(line.t, "&rlm;"));
      else assert(line.an == 8 && line.bold && strstr(line.t, "&amp;rlm;"));
      free(line.t); free(raw);
    }
  }
  return 0;
}
