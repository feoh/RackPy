// SPDX-License-Identifier: GPL-3.0-or-later
// Darwin counterpart to audio_guard.c, injected into test executables with
// DYLD_INSERT_LIBRARIES. __interpose is used instead of symbol interposition
// through a preload loader.
#define _POSIX_C_SOURCE 200112L
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

extern int PyGILState_Ensure(void) __attribute__((weak_import));

// Intercepted APIs: malloc, free, calloc, realloc, posix_memalign,
// aligned_alloc, pthread_mutex_lock, write(2), and PyGILState_Ensure.
// As on Linux, this observes these calls, not every possible allocator or lock.
static __thread int depth;
static __thread int resolving;
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

// dlsym can allocate while resolving interposers. During that recursion,
// allocations are served from static storage; freeing that storage is ignored.
static unsigned char resolver_arena[4096];
static atomic_size_t resolver_arena_used;

static void resolve_symbols(void) {
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
	real_gil_ensure = dlsym(RTLD_NEXT, "PyGILState_Ensure");
	resolving = 0;
}

static void* resolving_alloc(size_t size) {
	if (size > (size_t)-1 - 15)
		return NULL;
	size_t aligned = (size + 15) & ~(size_t) 15;
	size_t used = atomic_load_explicit(&resolver_arena_used, memory_order_relaxed);
	for (;;) {
		if (aligned > sizeof(resolver_arena) - used)
			return NULL;
		if (atomic_compare_exchange_weak_explicit(&resolver_arena_used, &used, used + aligned,
			memory_order_relaxed, memory_order_relaxed))
			return resolver_arena + used;
	}
}

#define COUNT(kind) do { if (depth) atomic_fetch_add_explicit(&counts[kind], 1, memory_order_relaxed); } while (0)

static void* guard_malloc(size_t size) {
	if (!real_malloc && resolving)
		return resolving_alloc(size);
	resolve_symbols();
	COUNT(MALLOC);
	return real_malloc ? real_malloc(size) : NULL;
}
static void guard_free(void* ptr) {
	uintptr_t address = (uintptr_t) ptr;
	uintptr_t arena_start = (uintptr_t) resolver_arena;
	if (address >= arena_start && address < arena_start + sizeof(resolver_arena))
		return;
	resolve_symbols();
	COUNT(FREE);
	if (ptr && real_free)
		real_free(ptr);
}
static void* guard_calloc(size_t count, size_t size) {
	if (!real_calloc && resolving) {
		if (size && count > (size_t)-1 / size)
			return NULL;
		void* result = resolving_alloc(count * size);
		if (result)
			memset(result, 0, count * size);
		return result;
	}
	resolve_symbols();
	COUNT(CALLOC);
	return real_calloc ? real_calloc(count, size) : NULL;
}
static void* guard_realloc(void* ptr, size_t size) {
	if (!real_realloc && resolving)
		return ptr ? NULL : resolving_alloc(size);
	resolve_symbols();
	COUNT(REALLOC);
	return real_realloc ? real_realloc(ptr, size) : NULL;
}
static int guard_posix_memalign(void** out, size_t align, size_t size) {
	resolve_symbols();
	COUNT(ALIGNED);
	return real_posix_memalign ? real_posix_memalign(out, align, size) : ENOMEM;
}
static void* guard_aligned_alloc(size_t align, size_t size) {
	resolve_symbols();
	COUNT(ALIGNED);
	return real_aligned_alloc ? real_aligned_alloc(align, size) : NULL;
}
static int guard_mutex_lock(pthread_mutex_t* mutex) {
	resolve_symbols();
	COUNT(MUTEX);
	return real_mutex_lock ? real_mutex_lock(mutex) : EINVAL;
}
static ssize_t guard_write(int fd, const void* buffer, size_t size) {
	resolve_symbols();
	COUNT(WRITE);
	return real_write ? real_write(fd, buffer, size) : -1;
}
static int guard_gil_ensure(void) {
	resolve_symbols();
	COUNT(GIL);
	return real_gil_ensure ? real_gil_ensure() : 0;
}

#define INTERPOSE(replacement, original) \
	{ (const void*) (replacement), (const void*) (original) }
__attribute__((used)) static const struct { const void* replacement; const void* replacee; } interposers[]
__attribute__((section("__DATA,__interpose"))) = {
	INTERPOSE(guard_malloc, malloc),
	INTERPOSE(guard_free, free),
	INTERPOSE(guard_calloc, calloc),
	INTERPOSE(guard_realloc, realloc),
	INTERPOSE(guard_posix_memalign, posix_memalign),
	INTERPOSE(guard_aligned_alloc, aligned_alloc),
	INTERPOSE(guard_mutex_lock, pthread_mutex_lock),
	INTERPOSE(guard_write, write),
	INTERPOSE(guard_gil_ensure, PyGILState_Ensure),
};

void audio_guard_enter(void) { depth++; }
void audio_guard_leave(void) { if (depth > 0) depth--; }
void audio_guard_reset(void) {
	for (int i = 0; i < 8; i++)
		atomic_store_explicit(&counts[i], 0, memory_order_relaxed);
}
void audio_guard_report(char* out, size_t size) {
	snprintf(out, size,
		"{\"malloc\": %llu, \"free\": %llu, \"calloc\": %llu, \"realloc\": %llu, \"aligned\": %llu, "
		"\"mutex_lock\": %llu, \"write\": %llu, \"gil_ensure\": %llu}",
		(unsigned long long) atomic_load(&counts[MALLOC]), (unsigned long long) atomic_load(&counts[FREE]),
		(unsigned long long) atomic_load(&counts[CALLOC]), (unsigned long long) atomic_load(&counts[REALLOC]),
		(unsigned long long) atomic_load(&counts[ALIGNED]), (unsigned long long) atomic_load(&counts[MUTEX]),
		(unsigned long long) atomic_load(&counts[WRITE]), (unsigned long long) atomic_load(&counts[GIL]));
}
