/* The TV's own picture and sound processors, tuned while a title plays and put back as they were when it stops.
   The TV draws the video and plays its sound itself, so its processors are the ones that can work on them: the app
   does nothing to the frames, costs no CPU for it, and the frame rate never changes.
     Upscale (anime and films apart, each Off / D / C / B / A, D the lightest, A the strongest): the TV's Super
     Resolution, gradation smoothing, noise cleaning, sharpness and edges, set for drawn pictures or for filmed ones.
     Sound for this TV (Auto): what the TV plays through is read (its speakers, headphones, a soundbar or receiver),
     and the TV's dialogue and sound settings are set for it while a title plays.
   Asked of the TV's settings service directly first (no root needed); where the TV refuses an app that, through the
   Homebrew Channel when it has root. Only the settings this TV has are changed. */
#ifndef SB_TVTUNE_H
#define SB_TVTUNE_H
#include <stddef.h>
const char *tvtune_level(int anime);   /* "off", "D", "C", "B", "A" (Settings) */
void tvtune_start(int anime);          /* a title starts (main thread): tuned on a worker */
void tvtune_stop(void);                /* it stops: put back */
void tvtune_cycle(int anime);          /* the next level, applied at once */
int  tvtune_poll(char *msg, size_t cap); /* 1 once with the notice to show (the upscale level the TV took) */
int  tvtune_state(void);               /* -1 not known yet, 0 the TV takes no change from the app, 1 it does */
int  tvtune_sound_out(void);           /* where the sound goes: 0 not known, 1 the TV's speakers / headphones, 2 a soundbar or receiver */
void tvtune_tick(void);                /* per frame (main thread): the store follows the worker */
void tvtune_quit(void);                /* the app closing: put back at once */
#endif
