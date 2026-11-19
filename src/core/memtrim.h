/* The app's memory handed back to the TV once it is no longer needed. The C library keeps what is freed inside its
   heaps; after a big release (a film closed, the TV short of memory) a trim gives it back, on a thread of its own so
   no frame waits for it. At start the heaps are kept few (one per thread otherwise: up to eight on the TV's cores,
   each holding its own freed space). On other platforms this does nothing. */
#ifndef SB_MEMTRIM_H
#define SB_MEMTRIM_H
void mem_tune(void);                                  /* first thing in main, before any thread starts */
void mem_trim_soon(double afterSec, const char *why); /* main thread */
void mem_pump(void);                                  /* main thread, once a frame: starts a trim that is due */
#endif
