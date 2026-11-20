/* Subtitle marks: the brackets and quotation marks some subtitle files put around their lines, taken out while the
   words inside them stay. Only ( ) and the double quotation marks go (" “ ” „ « » and the Arabic ﴾ ﴿); the
   punctuation that reads (. , ، - ! ? ؟ … ' and the rest) stays as it is. Settings > Subtitles > Clean subtitle marks. */
#ifndef SB_SUBMARKS_H
#define SB_SUBMARKS_H
extern volatile int sub_marks_on;     /* the setting, read on any thread (set from the main thread, app_tick) */
extern volatile int sub_tashkeel_on;  /* Remove Arabic diacritics, under it: the vowel marks go too (only while sub_marks_on) */
void sub_marks_clean(char *s);        /* in place; a line left with nothing but spaces becomes empty */
void sub_tashkeel_clean(char *s);     /* in place: the Arabic vowel marks (tashkeel) taken out, the letters stay */
#endif
