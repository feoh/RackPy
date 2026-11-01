// SPDX-License-Identifier: GPL-3.0-or-later
// A test fixture, never shipped (RP-42). GitHub-hosted macOS runners have no GPU, and Rack's GLFW always asks
// for NSOpenGLPFAAccelerated, so no pixel format matches (GLFW error 65545) and windowed tests cannot run. This
// minimal Rack plugin's init(), which Rack calls before it creates its window, makes -[NSOpenGLPixelFormat
// initWithAttributes:] drop that attribute, so macOS picks its software renderer: Rack renders in software, as
// Linux CI does with Mesa under Xvfb. It changes the test machine's renderer, not RackPy.
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <stdio.h>

typedef id (*InitWithAttributes)(id, SEL, const NSOpenGLPixelFormatAttribute*);
static InitWithAttributes original;

static int takesValue(NSOpenGLPixelFormatAttribute a) {
	switch (a) {
		case NSOpenGLPFAAuxBuffers: case NSOpenGLPFAColorSize: case NSOpenGLPFAAlphaSize: case NSOpenGLPFADepthSize:
		case NSOpenGLPFAStencilSize: case NSOpenGLPFAAccumSize: case NSOpenGLPFASampleBuffers: case NSOpenGLPFASamples:
		case NSOpenGLPFARendererID: case NSOpenGLPFAScreenMask: case NSOpenGLPFAVirtualScreenCount:
		case NSOpenGLPFAOpenGLProfile:
			return 1;
		default:
			return 0;
	}
}

static id softwareInitWithAttributes(id self, SEL cmd, const NSOpenGLPixelFormatAttribute* attribs) {
	NSOpenGLPixelFormatAttribute copy[128];
	int n = 0;
	for (int i = 0; attribs[i] && n < 124;) {
		NSOpenGLPixelFormatAttribute a = attribs[i++];
		if (a == NSOpenGLPFAAccelerated)
			continue;
		copy[n++] = a;
		if (takesValue(a))
			copy[n++] = attribs[i++];
	}
	copy[n] = 0;
	id format = original(self, cmd, copy);
	fprintf(stderr, "RackPy CI software OpenGL: pixel format without NSOpenGLPFAAccelerated: %s\n", format ? "found" : "none");
	return format;
}

__attribute__((visibility("default"))) void init(void* plugin) {
	(void) plugin;
	Method m = class_getInstanceMethod([NSOpenGLPixelFormat class], @selector(initWithAttributes:));
	original = (InitWithAttributes) method_setImplementation(m, (IMP) softwareInitWithAttributes);
	fprintf(stderr, "RackPy CI software OpenGL: installed\n");
}
