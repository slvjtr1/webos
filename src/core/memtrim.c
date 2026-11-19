#include "core/memtrim.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#if defined(__linux__) && defined(__GLIBC__) && !defined(SB_MAC)
#include <malloc.h>
#include <pthread.h>
#include <stdlib.h>
#define HAVE_TRIM 1
#endif

#ifdef HAVE_TRIM
static volatile int running;
static Uint32 dueAt, lastAt; static char why[64], whyRun[64];
void mem_tune(void) {
  if (getenv("SB_MALLOC_DEFAULT")) return; /* (development: the C library as it is, to compare) */
  mallopt(M_ARENA_MAX, 4);                 /* four heaps for every thread at once: the freed space is shared, not kept per thread */
  mallopt(M_MMAP_THRESHOLD, 256 * 1024);   /* big blocks (pictures, answers) straight from the system: freed, they go back at once */
  mallopt(M_TRIM_THRESHOLD, 512 * 1024);   /* the top of the main heap given back past half a megabyte free */
}
static void *trim_job(void *a) { (void)a; Uint32 t0 = SDL_GetTicks(); malloc_trim(0); fprintf(stderr, "[mem] trimmed in %u ms (%s)\n", SDL_GetTicks() - t0, whyRun); __atomic_store_n(&running, 0, __ATOMIC_RELEASE); return NULL; }
void mem_trim_soon(double afterSec, const char *w) {
  Uint32 at = SDL_GetTicks() + (Uint32)(afterSec * 1000); if (at < lastAt + 3000) at = lastAt + 3000; /* (spaced: a trim holds a heap's lock while it walks it) */
  if (!dueAt || at < dueAt) { dueAt = at ? at : 1; snprintf(why, sizeof why, "%s", w ? w : ""); }
}
void mem_pump(void) {
  if (!dueAt || SDL_GetTicks() < dueAt || __atomic_load_n(&running, __ATOMIC_ACQUIRE)) return;
  dueAt = 0; lastAt = SDL_GetTicks(); running = 1; memcpy(whyRun, why, sizeof whyRun);
  pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 64 * 1024);
  if (pthread_create(&t, &at, trim_job, NULL)) running = 0; else pthread_detach(t);
  pthread_attr_destroy(&at);
}
#else
void mem_tune(void) {}
void mem_trim_soon(double afterSec, const char *w) { (void)afterSec; (void)w; }
void mem_pump(void) {}
#endif
