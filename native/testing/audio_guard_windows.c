// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only Windows audio-path guard. Load this DLL before running a guard
// binary; it patches imports in modules already loaded in the process.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static __declspec(thread) LONG guard_depth;
static volatile LONG64 counts[9];
static volatile LONG installed;
static volatile LONG hook_failures;
enum { MALLOC, FREE, CALLOC, REALLOC, ALIGNED, LOCK, WRITE, GIL, HOOK_FAILURES };

static void* (__cdecl *next_malloc)(size_t);
static void (__cdecl *next_free)(void*);
static void* (__cdecl *next_calloc)(size_t, size_t);
static void* (__cdecl *next_realloc)(void*, size_t);
static void* (__cdecl *next_aligned_malloc)(size_t, size_t);
static void (__cdecl *next_aligned_free)(void*);
static void (WINAPI *next_enter_critical)(LPCRITICAL_SECTION);
static void (WINAPI *next_srw_exclusive)(PSRWLOCK);
static BOOL (WINAPI *next_write_file)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static int (__cdecl *next_gil_ensure)(void);

#define COUNT(k) do { if (guard_depth) InterlockedIncrement64(&counts[(k)]); } while (0)
static void* __cdecl hook_malloc(size_t n) { COUNT(MALLOC); return next_malloc ? next_malloc(n) : NULL; }
static void __cdecl hook_free(void* p) { COUNT(FREE); if (next_free) next_free(p); }
static void* __cdecl hook_calloc(size_t n, size_t s) { COUNT(CALLOC); return next_calloc ? next_calloc(n, s) : NULL; }
static void* __cdecl hook_realloc(void* p, size_t n) { COUNT(REALLOC); return next_realloc ? next_realloc(p, n) : NULL; }
static void* __cdecl hook_aligned_malloc(size_t n, size_t a) { COUNT(ALIGNED); return next_aligned_malloc ? next_aligned_malloc(n, a) : NULL; }
static void __cdecl hook_aligned_free(void* p) { COUNT(ALIGNED); if (next_aligned_free) next_aligned_free(p); }
static void WINAPI hook_enter_critical(LPCRITICAL_SECTION p) { COUNT(LOCK); if (next_enter_critical) next_enter_critical(p); }
static void WINAPI hook_srw_exclusive(PSRWLOCK p) { COUNT(LOCK); if (next_srw_exclusive) next_srw_exclusive(p); }
static BOOL WINAPI hook_write_file(HANDLE h, LPCVOID b, DWORD n, LPDWORD done, LPOVERLAPPED ov) {
	COUNT(WRITE); return next_write_file ? next_write_file(h, b, n, done, ov) : FALSE;
}
static int __cdecl hook_gil_ensure(void) { COUNT(GIL); return next_gil_ensure ? next_gil_ensure() : 0; }

typedef struct Hook { const char* name; void* replacement; void** original; } Hook;
static Hook hooks[] = {
	{"malloc", hook_malloc, (void**) &next_malloc}, {"free", hook_free, (void**) &next_free},
	{"calloc", hook_calloc, (void**) &next_calloc}, {"realloc", hook_realloc, (void**) &next_realloc},
	{"_aligned_malloc", hook_aligned_malloc, (void**) &next_aligned_malloc},
	{"_aligned_free", hook_aligned_free, (void**) &next_aligned_free},
	{"EnterCriticalSection", hook_enter_critical, (void**) &next_enter_critical},
	{"AcquireSRWLockExclusive", hook_srw_exclusive, (void**) &next_srw_exclusive},
	{"WriteFile", hook_write_file, (void**) &next_write_file},
	{"PyGILState_Ensure", hook_gil_ensure, (void**) &next_gil_ensure}
};

static int patch_module(HMODULE module) {
	unsigned char* base = (unsigned char*) module;
	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*) base;
	if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
	IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*) (base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
	IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress) return 0;
	IMAGE_IMPORT_DESCRIPTOR* desc = (IMAGE_IMPORT_DESCRIPTOR*) (base + dir.VirtualAddress);
	int found = 0;
	for (; desc->Name; ++desc) {
		if (!desc->OriginalFirstThunk || !desc->FirstThunk) continue;
		IMAGE_THUNK_DATA* names = (IMAGE_THUNK_DATA*) (base + desc->OriginalFirstThunk);
		IMAGE_THUNK_DATA* slots = (IMAGE_THUNK_DATA*) (base + desc->FirstThunk);
		for (; names->u1.AddressOfData; ++names, ++slots) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
			IMAGE_IMPORT_BY_NAME* import = (IMAGE_IMPORT_BY_NAME*) (base + names->u1.AddressOfData);
			for (size_t i = 0; i < sizeof hooks / sizeof hooks[0]; ++i) {
				if (strcmp((const char*) import->Name, hooks[i].name)) continue;
				void** slot = (void**) &slots->u1.Function;
				DWORD old_protect;
				if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old_protect)) { InterlockedIncrement(&hook_failures); continue; }
				if (*slot != hooks[i].replacement) {
					if (!*hooks[i].original) *hooks[i].original = *slot;
					InterlockedExchangePointer((PVOID volatile*) slot, hooks[i].replacement);
					found++;
				}
				DWORD ignored;
				if (!VirtualProtect(slot, sizeof *slot, old_protect, &ignored))
					InterlockedIncrement(&hook_failures);
			}
		}
	}
	return found;
}

static int install_hooks(void) {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
	if (snapshot == INVALID_HANDLE_VALUE) return 0;
	MODULEENTRY32 entry; entry.dwSize = sizeof entry;
	int patched = 0;
	if (Module32First(snapshot, &entry)) do { patched += patch_module(entry.hModule); } while (Module32Next(snapshot, &entry));
	CloseHandle(snapshot);
	// Fail closed unless at least one allocation, lock and WriteFile import was hooked.
	int alloc = next_malloc || next_calloc || next_realloc || next_aligned_malloc;
	if (!alloc || (!next_enter_critical && !next_srw_exclusive) || !next_write_file)
		InterlockedIncrement(&hook_failures);
	return patched;
}

__declspec(dllexport) void audio_guard_enter(void) { if (guard_depth != LONG_MAX) ++guard_depth; }
__declspec(dllexport) void audio_guard_leave(void) { if (guard_depth > 0) --guard_depth; }
__declspec(dllexport) void audio_guard_reset(void) {
	for (int i = 0; i < HOOK_FAILURES; ++i) InterlockedExchange64(&counts[i], 0);
	InterlockedExchange64(&counts[HOOK_FAILURES], InterlockedCompareExchange(&hook_failures, 0, 0));
}
__declspec(dllexport) void audio_guard_report(char* out, size_t size) {
	_snprintf_s(out, size, _TRUNCATE,
		"{\"malloc\": %lld, \"free\": %lld, \"calloc\": %lld, \"realloc\": %lld, \"aligned\": %lld, \"mutex_lock\": %lld, \"write\": %lld, \"gil_ensure\": %lld, \"hook_failures\": %lld}",
		counts[MALLOC], counts[FREE], counts[CALLOC], counts[REALLOC], counts[ALIGNED], counts[LOCK], counts[WRITE], counts[GIL], counts[HOOK_FAILURES]);
}

// Call explicitly after loading this test DLL and before starting the guarded
// workload. Import tables are not patched from DllMain: that would run the
// loader walk under the Windows loader lock. Returns nonzero only when the
// required allocation, synchronization and WriteFile hook families are live.
__declspec(dllexport) int audio_guard_install(void) {
	// Rescan after each test DLL is loaded so its imports are covered before it runs.
	if (InterlockedCompareExchange(&installed, 1, 0) == 0)
		InterlockedExchange64(&counts[HOOK_FAILURES], 0);
	install_hooks();
	InterlockedExchange64(&counts[HOOK_FAILURES], hook_failures);
	return InterlockedCompareExchange(&hook_failures, 0, 0) == 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
	(void) reserved;
	if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
	return TRUE;
}
