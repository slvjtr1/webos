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
  printf(fails ? "%d failed\n" : "all passed\n", fails);
  return fails != 0;
}
