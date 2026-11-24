/* cc -Isrc tests/submarks/test.c src/core/submarks.c && ./a.out */
#include "core/submarks.h"
#include <stdio.h>
#include <string.h>
static int fails;
static void eq(const char *in, const char *want) {
  char b[512]; snprintf(b, sizeof b, "%s", in); sub_marks_clean(b);
  if (strcmp(b, want)) { printf("FAIL: [%s] -> [%s], want [%s]\n", in, b, want); fails++; }
}
int main(void) {
  const char *cues[][2] = {
    {"&rlm;Hello &#x61F;", "\xe2\x80\x8fHello \xd8\x9f"},
    {"&amp;rlm;<font color='red'><i>one</i></font><br/>two", "\xe2\x80\x8fone\ntwo"},
    {"{\\an8}&#8207;A &amp; B", "\xe2\x80\x8f" "A & B"},
    {"&lt;b&gt;3 &lt; 4&lt;/b&gt;", "3 < 4"},
    {"&#x110000; &#0; &unknown; &amp;", "&#x110000; &#0; &unknown; &"},
    {"&#128522;", "\xf0\x9f\x98\x8a"}
  };
  for (size_t i = 0; i < sizeof cues / sizeof *cues; i++) {
    char buf[256]; snprintf(buf, sizeof buf, "%s", cues[i][0]);
    sub_arabic_embedded_clean(buf);
    if (strcmp(buf, cues[i][1])) { puts("embedded Arabic cue mismatch"); fails++; }
  }
  if (!sub_lang_arabic("ar-SA") || !sub_lang_arabic("ARA") || sub_lang_arabic("arc") ||
      sub_lang_arabic("fa") || sub_lang_arabic("en-Arab")) fails++;
  eq("(Laughing) Okay.", "Laughing Okay.");
  eq("\"Hello there!\"", "Hello there!");
  eq("He said \"run\" - now!", "He said run - now!");
  eq("What?! (whispers)", "What?! whispers");
  eq("( )", "");
  eq("\xe2\x80\x9cQuoted\xe2\x80\x9d text", "Quoted text");
  eq("\xc2\xab" "Bonjour" "\xc2\xbb", "Bonjour");
  eq("word(word)", "word word");
  eq("(\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7) \xd9\x83\xd9\x8a\xd9\x81\xd9\x83\xd8\x9f", "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd9\x83\xd9\x8a\xd9\x81\xd9\x83\xd8\x9f"); /* (مرحبا) كيفك؟ */
  eq("\xef\xb4\xbe" "\xd8\xa2\xd9\x8a\xd8\xa9" "\xef\xb4\xbf", "\xd8\xa2\xd9\x8a\xd8\xa9");
  eq("- Don't go!\n- (sighs) Fine...", "- Don't go!\n- sighs Fine...");
  eq("(music)\nLine two", "music\nLine two");
  eq("\"\"\n\"Yes\"", "Yes");
  eq("Wait (really) ?", "Wait really?");
  eq("It's 5 o'clock, isn't it?", "It's 5 o'clock, isn't it?");
  eq("A.B. - 3:00 \xe2\x80\xa6 ok \xd8\x8c \xd8\x9f !", "A.B. - 3:00 \xe2\x80\xa6 ok \xd8\x8c \xd8\x9f!");
  eq("", "");
  /* with Remove Arabic diacritics on: the vowel marks go, the letters, hamza forms, tatweel and punctuation stay */
  eq("\xd9\x85\xd9\x8e\xd8\xb1\xd9\x92\xd8\xad\xd9\x8e\xd8\xa8\xd9\x8b\xd8\xa7", "\xd9\x85\xd9\x8e\xd8\xb1\xd9\x92\xd8\xad\xd9\x8e\xd8\xa8\xd9\x8b\xd8\xa7"); /* (off: مَرْحَبًا unchanged) */
  sub_tashkeel_on = 1;
  eq("\xd9\x85\xd9\x8e\xd8\xb1\xd9\x92\xd8\xad\xd9\x8e\xd8\xa8\xd9\x8b\xd8\xa7", "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7"); /* مَرْحَبًا -> مرحبا */
  eq("\xd8\xb4\xd9\x8f\xd9\x83\xd9\x92\xd8\xb1\xd9\x8b\xd8\xa7 \xd9\x84\xd9\x8e\xd9\x83\xd9\x8e\xd9\x91", "\xd8\xb4\xd9\x83\xd8\xb1\xd8\xa7 \xd9\x84\xd9\x83"); /* شُكْرًا لَكَّ -> شكرا لك (shadda too) */
  eq("\xd9\x87\xd9\xb0\xd8\xb0\xd8\xa7", "\xd9\x87\xd8\xb0\xd8\xa7"); /* هٰذا: the superscript alef */
  eq("(\xd8\xa3\xd9\x8e\xd9\x86\xd9\x92\xd8\xaa\xd9\x8e) \xd8\xa5\xd9\x90\xd9\x84\xd9\x8e\xd9\x89 \xd8\xa3\xd9\x8a\xd9\x86\xd8\x9f", "\xd8\xa3\xd9\x86\xd8\xaa \xd8\xa5\xd9\x84\xd9\x89 \xd8\xa3\xd9\x8a\xd9\x86\xd8\x9f"); /* (أَنْتَ) إِلَى أين؟ -> أنت إلى أين؟: hamza letters stay */
  eq("\xd9\x80\xd9\x80\xd9\x8e", "\xd9\x80\xd9\x80"); /* tatweel stays */
  eq("Caf\xc3\xa9 \xe2\x80\x93 na\xc3\xafve", "Caf\xc3\xa9 \xe2\x80\x93 na\xc3\xafve"); /* other scripts untouched */
  sub_tashkeel_on = 0;
  printf(fails ? "%d failed\n" : "all passed\n", fails);
  return fails != 0;
}
