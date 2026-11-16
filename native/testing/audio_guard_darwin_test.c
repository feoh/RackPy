// SPDX-License-Identifier: GPL-3.0-or-later
// Positive control for the Darwin DYLD_INSERT_LIBRARIES audio guard.
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*guard_action)(void);
typedef void (*guard_report)(char*, size_t);

int main(void) {
	guard_action enter = (guard_action) dlsym(RTLD_DEFAULT, "audio_guard_enter");
	guard_action leave = (guard_action) dlsym(RTLD_DEFAULT, "audio_guard_leave");
	guard_action reset = (guard_action) dlsym(RTLD_DEFAULT, "audio_guard_reset");
	guard_report report = (guard_report) dlsym(RTLD_DEFAULT, "audio_guard_report");
	if (!enter || !leave || !reset || !report) {
		fputs("audio guard API not injected\n", stderr);
		return 2;
	}
	reset();
	enter();
	void* allocation = malloc(32);
	free(allocation);
	leave();
	char json[256];
	report(json, sizeof(json));
	if (!strstr(json, "\"malloc\": 1") || !strstr(json, "\"free\": 1")) {
		fprintf(stderr, "positive control was not detected: %s\n", json);
		return 1;
	}
	puts(json);
	return 0;
}
