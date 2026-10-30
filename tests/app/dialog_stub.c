// SPDX-License-Identifier: GPL-3.0-or-later
// Linux actual-Rack test only: intercept OS dialogs while retaining Rack's
// own calling code. Environment variables select cancellation or a response.
#define _POSIX_C_SOURCE 200809L
#include <osdialog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void record(const char* kind, int action) {
    const char* path = getenv("RACKPY_DIALOG_LOG");
    if (!path) return;
    FILE* f = fopen(path, "a");
    if (f) {
        fprintf(f, "%s %d\n", kind, action);
        fclose(f);
    }
}

int osdialog_message(osdialog_message_level level, osdialog_message_buttons buttons, const char* message) {
    (void) level;
    (void) message;
    record("message", buttons);
    const char* response = getenv("RACKPY_DIALOG_RESPONSE");
    return response && strcmp(response, "accept") == 0;
}

char* osdialog_file(osdialog_file_action action, const char* path, const char* filename, osdialog_filters* filters) {
    (void) path;
    (void) filename;
    (void) filters;
    record("file", action);
    const char* result = getenv("RACKPY_DIALOG_FILE");
    return result && *result ? strdup(result) : NULL;
}
