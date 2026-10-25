// SPDX-License-Identifier: GPL-3.0-or-later
// RP-07 audio-path guard, loaded with LD_PRELOAD. While a thread is inside
// audio_guard_enter()/audio_guard_leave(), it counts calls that the production
// audio path must never make: heap allocation and free, mutex locks, write(2)
// and GIL acquisition. Counting itself uses only atomics and thread-locals.
// Promoted from the RP-07 spike for RP-17A: RackPy tests preload it too.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

static __thread int depth;
static atomic_ullong counts[8];
enum { MALLOC, FREE, CALLOC, REALLOC, ALIGNED, MUTEX, WRITE, GIL };

static void* (*real_malloc)(size_t);
static void (*real_free)(void*);
static void* (*real_calloc)(size_t, size_t);
static void* (*real_realloc)(void*, size_t);
static int (*real_posix_memalign)(void**, size_t, size_t);
static void* (*real_aligned_alloc)(size_t, size_t);
static int (*real_mutex_lock)(pthread_mutex_t*);
static ssize_t (*real_write)(int, const void*, size_t);
static int (*real_gil_ensure)(void);

// dlsym() may call calloc before real_calloc is known; serve it from a small static arena.
static char arena[4096];
static size_t arena_used;
static __thread int resolving;

static void resolve(void) {
	if (real_malloc || resolving)
		return;
	resolving = 1;
	real_calloc = dlsym(RTLD_NEXT, "calloc");
	real_malloc = dlsym(RTLD_NEXT, "malloc");
	real_free = dlsym(RTLD_NEXT, "free");
	real_realloc = dlsym(RTLD_NEXT, "realloc");
	real_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
	real_aligned_alloc = dlsym(RTLD_NEXT, "aligned_alloc");
	real_mutex_lock = dlsym(RTLD_NEXT, "pthread_mutex_lock");
	real_write = dlsym(RTLD_NEXT, "write");
	resolving = 0;
}

#define COUNT(kind) \
	do { \
		if (depth) \
			atomic_fetch_add_explicit(&counts[kind], 1, memory_order_relaxed); \
	} while (0)

void* malloc(size_t n) {
	resolve();
	COUNT(MALLOC);
	return real_malloc(n);
}
void free(void* p) {
	if (p >= (void*) arena && p < (void*) (arena + sizeof(arena)))
		return;
	resolve();
	COUNT(FREE);
	if (p)
		real_free(p);
}
void* calloc(size_t a, size_t b) {
	if (!real_calloc) {
		if (resolving) {
			size_t n = (a * b + 15) & ~(size_t) 15;
			if (arena_used + n > sizeof(arena))
				return NULL;
			void* p = arena + arena_used;
			arena_used += n;
			return p;
		}
		resolve();
	}
	COUNT(CALLOC);
	return real_calloc(a, b);
}
void* realloc(void* p, size_t n) {
	resolve();
	COUNT(REALLOC);
	return real_realloc(p, n);
}
int posix_memalign(void** out, size_t align, size_t n) {
	resolve();
	COUNT(ALIGNED);
	return real_posix_memalign(out, align, n);
}
void* aligned_alloc(size_t align, size_t n) {
	resolve();
	COUNT(ALIGNED);
	return real_aligned_alloc(align, n);
}
int pthread_mutex_lock(pthread_mutex_t* m) {
	resolve();
	COUNT(MUTEX);
	return real_mutex_lock(m);
}
ssize_t write(int fd, const void* buf, size_t n) {
	resolve();
	COUNT(WRITE);
	return real_write(fd, buf, n);
}
int PyGILState_Ensure(void) {
	if (!real_gil_ensure)
		real_gil_ensure = dlsym(RTLD_NEXT, "PyGILState_Ensure");
	COUNT(GIL);
	return real_gil_ensure ? real_gil_ensure() : 0;
}

void audio_guard_enter(void) {
	depth++;
}
void audio_guard_leave(void) {
	depth--;
}
void audio_guard_reset(void) {
	for (int i = 0; i < 8; i++)
		atomic_store(&counts[i], 0);
}
void audio_guard_report(char* out, size_t size) {
	snprintf(out, size,
		"{\"malloc\": %llu, \"free\": %llu, \"calloc\": %llu, \"realloc\": %llu, \"aligned\": %llu, "
		"\"mutex_lock\": %llu, \"write\": %llu, \"gil_ensure\": %llu}",
		(unsigned long long) counts[MALLOC], (unsigned long long) counts[FREE], (unsigned long long) counts[CALLOC],
		(unsigned long long) counts[REALLOC], (unsigned long long) counts[ALIGNED], (unsigned long long) counts[MUTEX],
		(unsigned long long) counts[WRITE], (unsigned long long) counts[GIL]);
}
