// SPDX-License-Identifier: GPL-3.0-or-later
// rack::window resources: Window (Context::window), Font, Image, opaque native
// handles, svgDraw and the unit helpers, in rackpy.raw.window.
//
// Lifetimes. Rack's window caches every Font and Image it loads until the window
// is destroyed, and deletes Image textures in ~Image with the window's NanoVG
// context. Python therefore holds the cached ones weakly, and every Font or
// Image handle goes stale at the next graphics-context destruction (Rack: "do
// not store this reference across screen frames"). An Image Python constructs
// and loads itself is Python's: its texture is deleted on the main thread
// (deferred to the next UI step if Python drops it elsewhere), and at a
// graphics-context destruction, before Rack deletes the context.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <rack.hpp>

#include "drawing.hpp"
#include "../widget/events.hpp"
#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/runtime/python.hpp"

namespace rackpy { namespace drawing {
namespace py = pybind11;
using rack::window::Font;
using rack::window::Image;
using rack::window::Window;

namespace {

/** Bumped at every graphics-context destruction: Font and Image handles from before are stale. */
uint64_t graphicsEpoch = 0;
int fontLimit = 0;

std::shared_ptr<runtime::ContextToken> liveToken(const char* operation) {
	runtime::requireMainThread(operation);
	auto token = runtime::binding().current();
	if (!token || !token->alive() || rack::contextGet() != token->context)
		throw runtime::WrongContext(std::string(operation) + " needs Rack's live context on its main thread");
	return token;
}

Window& liveWindow(const char* operation) {
	auto token = liveToken(operation);
	if (!token->context->window) throw runtime::WrongContext(std::string(operation) + " needs Rack's window (Rack is headless)");
	return *token->context->window;
}

bool windowContext(NVGcontext* vg) {
	auto token = runtime::binding().current();
	if (!token || !token->alive() || !token->context->window) return false;
	auto* window = token->context->window;
	return vg && (vg == window->vg || vg == window->fbVg);
}

// Python-owned Images: their textures must be deleted on the main thread, while the context lives.
std::mutex pendingMutex;
std::vector<Image*> pendingImages;
std::vector<std::weak_ptr<Image>> ownedImages;

void deleteOwnedImage(Image* image) {
	if (image->handle < 0 || runtime::isMainThread()) {
		if (image->handle >= 0 && !windowContext(image->vg)) image->handle = -1;  // its context is gone
		delete image;
		return;
	}
	std::lock_guard<std::mutex> lock(pendingMutex);
	pendingImages.push_back(image);
}

void drainPendingImages() {
	std::vector<Image*> images;
	{
		std::lock_guard<std::mutex> lock(pendingMutex);
		images.swap(pendingImages);
	}
	for (auto* image : images) {
		if (image->handle >= 0 && !windowContext(image->vg)) image->handle = -1;
		delete image;
	}
}

void releaseOwnedTextures() {
	drainPendingImages();
	for (auto& weak : ownedImages) {
		auto image = weak.lock();
		if (image && image->handle >= 0 && windowContext(image->vg)) nvgDeleteImage(image->vg, image->handle);
		if (image) image->handle = -1;
	}
	ownedImages.clear();
}

/** A Font or Image from Python: Rack's cached one (held weakly) or one Python owns. */
template<class T> struct Resource {
	std::shared_ptr<T> owned;
	std::weak_ptr<T> cached;
	std::shared_ptr<runtime::ContextToken> token;
	uint64_t epoch = 0;
	std::shared_ptr<T> lock() const {
		runtime::requireMainThread("using a window resource");
		if (!token || !token->alive()) throw runtime::StaleHandle("the Rack context of this resource has closed");
		runtime::checkContext(*token);
		if (epoch != graphicsEpoch)
			throw runtime::StaleHandle("the graphics context was destroyed since this resource was loaded; load it again");
		auto value = owned ? owned : cached.lock();
		if (!value) throw runtime::StaleHandle("Rack has released this resource");
		return value;
	}
	T& get() const { return *lock(); }
};
using FontRef = Resource<Font>;
using ImageRef = Resource<Image>;

template<class T> py::object cachedObject(const std::shared_ptr<T>& value) {
	if (!value) return py::none();
	auto token = runtime::binding().current();
	return runtime::identity(value.get(), token->generation * 1000003u + graphicsEpoch, [&] {
		Resource<T> ref;
		ref.cached = value;
		ref.token = token;
		ref.epoch = graphicsEpoch;
		return py::cast(std::move(ref));
	});
}

template<class T> Resource<T> ownedResource(std::shared_ptr<T> value) {
	Resource<T> ref;
	ref.owned = std::move(value);
	ref.token = liveToken("constructing a window resource");
	ref.epoch = graphicsEpoch;
	return ref;
}

/** A context's NVGcontext as a NativeHandle: valid while it is still that window's. */
py::object contextHandle(const std::shared_ptr<runtime::ContextToken>& token, NVGcontext* vg, bool fb) {
	if (!vg) return py::none();
	return runtime::identity(vg, token->generation * 1000003u + graphicsEpoch, [&] {
		uint64_t epoch = graphicsEpoch;
		NativeHandle h{"NVGcontext", vg, [token, vg, fb, epoch] {
			auto* window = token->alive() ? token->context->window : nullptr;
			return window && epoch == graphicsEpoch && (fb ? window->fbVg : window->vg) == vg;
		}, nullptr};
		return py::cast(std::move(h));
	});
}

NVGcontext* vgArgument(py::handle value) {
	if (py::isinstance<widgets::Graphics>(value)) {
		auto* vg = value.cast<const widgets::Graphics&>().get();
		if (!windowContext(vg)) throw py::value_error("this GraphicsContext is not one of the window's NanoVG contexts");
		return vg;
	}
	auto* vg = static_cast<NVGcontext*>(value.cast<const NativeHandle&>().get("NVGcontext"));
	return vg;
}

/** Context::window, checked: valid while it is that context's window. */
struct WindowHandle {
	std::shared_ptr<runtime::ContextToken> token;
	Window* window;
	Window& get() const {
		runtime::requireMainThread("using Rack's window");
		if (!token->alive()) throw runtime::StaleHandle("Rack's context has closed");
		runtime::checkContext(*token);
		if (token->context->window != window) throw runtime::StaleHandle("Rack's window has been replaced or deleted");
		return *window;
	}
};

float positiveFinite(float value, const char* what) {
	if (!std::isfinite(value) || value <= 0.f) throw std::invalid_argument(std::string(what) + " must be finite and positive");
	return value;
}

void deprecated(const char* message) {
	if (PyErr_WarnEx(PyExc_DeprecationWarning, message, 1) < 0) throw py::error_already_set();
}

}  // namespace

void noteFont(int handle) {
	if (handle >= fontLimit) fontLimit = handle + 1;
}
bool knownFont(int handle) {
	return handle >= 0 && handle < fontLimit;
}

void bindNativeHandle(py::module_ window) {
	py::class_<NativeHandle>(window, "NativeHandle", py::is_final(),
		"An opaque Rack-owned native object (NVGcontext, GLFWwindow, NSVGimage, patch::Manager, midiloopback::Context): "
		"identity and liveness only. The same object gives the same handle while it lives.")
		.def_readonly("kind", &NativeHandle::kind)
		.def_property_readonly("alive", &NativeHandle::alive)
		.def("__repr__", [](const NativeHandle& h) { return "<NativeHandle " + h.kind + (h.valid && h.valid() ? ">" : " (stale)>"); });
}

py::object windowObject(const std::shared_ptr<runtime::ContextToken>& token) {
	auto* window = token->context->window;
	if (!window) return py::none();
	return runtime::identity(window, token->generation, [&] { return py::cast(WindowHandle{token, window}); });
}

py::object contextFieldHandle(const std::shared_ptr<runtime::ContextToken>& token, const char* field) {
	std::string name = field;
	void* ptr = name == "patch" ? static_cast<void*>(token->context->patch) : static_cast<void*>(token->context->midiLoopbackContext);
	if (!ptr) return py::none();
	const char* kind = name == "patch" ? "patch::Manager" : "midiloopback::Context";
	return runtime::identity(ptr, token->generation, [&] {
		bool patch = name == "patch";
		NativeHandle h{kind, ptr, [token, ptr, patch] {
			if (!token->alive()) return false;
			return (patch ? static_cast<void*>(token->context->patch) : static_cast<void*>(token->context->midiLoopbackContext)) == ptr;
		}, nullptr};
		return py::cast(std::move(h));
	});
}

void bindWindowResources(py::module_ raw) {
	py::module_ m = raw.attr("window").cast<py::module_>();
	m.doc() = "rack::window: the OS window, its NanoVG resources (Font, Image, Svg) and unit helpers.";
	runtime::onGraphicsContextDestroy([] {
		releaseOwnedTextures();
		++graphicsEpoch;
		fontLimit = 0;
	});
	runtime::onUiStep([] { drainPendingImages(); });
	runtime::onContextClosing([] { releaseOwnedTextures(); });

	m.attr("SVG_DPI") = rack::window::SVG_DPI;
	m.attr("MM_PER_IN") = rack::window::MM_PER_IN;
	m.def("in2px", py::overload_cast<float>(&rack::window::in2px), py::arg("inches"), "Inches to Rack pixels (SVG_DPI per inch).");
	m.def("in2px", py::overload_cast<rack::math::Vec>(&rack::window::in2px), py::arg("inches"));
	m.def("mm2px", py::overload_cast<float>(&rack::window::mm2px), py::arg("mm"), "Millimetres to Rack pixels.");
	m.def("mm2px", py::overload_cast<rack::math::Vec>(&rack::window::mm2px), py::arg("mm"));

	py::class_<FontRef>(m, "Font", py::is_final(),
		"A NanoVG font loaded in Rack's window. From Window.load_font(), Rack's cached font: stale after the graphics "
		"context is destroyed, so load it again each frame as Rack advises.")
		.def(py::init([] { return ownedResource(std::make_shared<Font>()); }),  // value-initialized: vg null
			"An unloaded font (handle -1) that Python owns; load_file() loads it. Prefer Window.load_font().")
		.def_property_readonly("alive", [](const FontRef& f) {
			try { f.lock(); return true; } catch (const runtime::StaleHandle&) { return false; }
		})
		.def_property_readonly("handle", [](const FontRef& f) {
			int handle = f.get().handle;
			noteFont(handle);
			return handle;
		},
			"NanoVG's font handle, -1 if unloaded. Read-only: Rack's caches rely on it.")
		.def_property_readonly("vg", [](const FontRef& f) {
			auto& font = f.get();
			auto* window = f.token->context->window;
			return contextHandle(f.token, font.vg, window && font.vg == window->fbVg);
		}, "The window NanoVG context it was loaded in (a NativeHandle), or None.")
		.def("load_file", [](const FontRef& f, const std::string& filename, py::handle vg) {
			if (!f.owned) throw runtime::Ownership("this Font is Rack's cached font; load files into a Font() of your own");
			f.get().loadFile(filename, vgArgument(vg));
		}, py::arg("filename"), py::arg("vg"), "Rack's Font::loadFile: raises rackpy.raw.Exception if the file cannot be loaded.")
		.def_static("load", [](const std::string& filename) {
			deprecated("Font.load mirrors deprecated Font::load; use Window.load_font");
			return cachedObject(liveWindow("loading a font").loadFont(filename));
		}, py::arg("filename"));

	py::class_<ImageRef>(m, "Image", py::is_final(),
		"A NanoVG image loaded in Rack's window. From Window.load_image(), Rack's cached image: stale after the graphics "
		"context is destroyed.")
		.def(py::init([] {
			auto image = std::shared_ptr<Image>(new Image(), deleteOwnedImage);  // value-initialized: vg null
			ownedImages.erase(std::remove_if(ownedImages.begin(), ownedImages.end(),
				[](const std::weak_ptr<Image>& w) { return w.expired(); }), ownedImages.end());
			ownedImages.push_back(image);
			return ownedResource(std::move(image));
		}), "An unloaded image (handle -1) that Python owns; load_file() loads it. Its texture is deleted when Python "
			"drops it, or when the graphics context is destroyed. Prefer Window.load_image().")
		.def_property_readonly("alive", [](const ImageRef& i) {
			try { i.lock(); return true; } catch (const runtime::StaleHandle&) { return false; }
		})
		.def_property_readonly("handle", [](const ImageRef& i) { return i.get().handle; },
			"NanoVG's image handle, -1 if unloaded. Read-only: ~Image deletes the texture by this handle.")
		.def_property_readonly("vg", [](const ImageRef& i) {
			auto& image = i.get();
			auto* window = i.token->context->window;
			return contextHandle(i.token, image.vg, window && image.vg == window->fbVg);
		}, "The window NanoVG context it was loaded in (a NativeHandle), or None.")
		.def("load_file", [](const ImageRef& i, const std::string& filename, py::handle vg) {
			if (!i.owned) throw runtime::Ownership("this Image is Rack's cached image; load files into an Image() of your own");
			i.get().loadFile(filename, vgArgument(vg));
		}, py::arg("filename"), py::arg("vg"), "Rack's Image::loadFile: raises rackpy.raw.Exception if the file cannot be loaded.")
		.def_static("load", [](const std::string& filename) {
			deprecated("Image.load mirrors deprecated Image::load; use Window.load_image");
			return cachedObject(liveWindow("loading an image").loadImage(filename));
		}, py::arg("filename"));

	py::class_<WindowHandle>(m, "Window", py::is_final(), "Rack's OS window (Context.window). Main thread only.")
		.def_property_readonly("alive", [](const WindowHandle& h) {
			return h.token->alive() && h.token->context->window == h.window;
		})
		.def_property_readonly("win", [](const WindowHandle& h) {
			auto& w = h.get();
			auto token = h.token;
			auto* win = w.win;
			return runtime::identity(win, token->generation, [&] {
				NativeHandle n{"GLFWwindow", win, [token, win] {
					return token->alive() && token->context->window && token->context->window->win == win;
				}, nullptr};
				return py::cast(std::move(n));
			});
		}, "The GLFW window, as an opaque NativeHandle.")
		.def_property_readonly("vg", [](const WindowHandle& h) { return contextHandle(h.token, h.get().vg, false); },
			"The window's NanoVG context, as an opaque NativeHandle. Draw through a callback's GraphicsContext.")
		.def_property_readonly("fb_vg", [](const WindowHandle& h) { return contextHandle(h.token, h.get().fbVg, true); },
			"The NanoVG context Rack renders framebuffers with, as an opaque NativeHandle.")
		.def_property("pixel_ratio", [](const WindowHandle& h) { return h.get().pixelRatio; },
			[](const WindowHandle& h, float v) { h.get().pixelRatio = positiveFinite(v, "pixel_ratio"); },
			"UI scaling ratio. Rack recomputes it every frame from its settings.")
		.def_property("window_ratio", [](const WindowHandle& h) { return h.get().windowRatio; },
			[](const WindowHandle& h, float v) { h.get().windowRatio = positiveFinite(v, "window_ratio"); },
			"Framebuffer size / OS window size. Rack recomputes it every frame.")
		.def_property("ui_font", [](const WindowHandle& h) { return cachedObject(h.get().uiFont); },
			[](const WindowHandle& h, const FontRef& font) {
				auto& w = h.get();
				auto value = font.lock();
				if (value->handle < 0 || (value->vg != w.vg && value->vg != w.fbVg))
					throw std::invalid_argument("ui_font must be a font loaded in this window");
				w.uiFont = value;  // Rack's Font destructor deletes nothing: sharing it with Rack is safe
			}, "Rack's UI font. Rack draws with it every frame, so it must be a loaded font (never None).")
		.def("get_size", [](const WindowHandle& h) { return h.get().getSize(); }, "The OS window size, in screen coordinates.")
		.def("set_size", [](const WindowHandle& h, rack::math::Vec size) {
			if (!size.isFinite()) throw std::invalid_argument("window size must be finite");
			h.get().setSize(size);
		}, py::arg("size"), "Requests a window size (at least 640 x 480). The window system applies it asynchronously.")
		.def("close", [](const WindowHandle& h) { h.get().close(); }, "Asks Rack to close its window after this frame: Rack quits.")
		.def("cursor_lock", [](const WindowHandle& h) { h.get().cursorLock(); }, "Hides and locks the cursor (if Rack's settings allow).")
		.def("cursor_unlock", [](const WindowHandle& h) { h.get().cursorUnlock(); })
		.def("is_cursor_locked", [](const WindowHandle& h) { return h.get().isCursorLocked(); })
		.def("get_mods", [](const WindowHandle& h) { return h.get().getMods(); },
			"The keyboard modifiers held now (RACK_MOD_* bits). In an event handler use the event's mods.")
		.def("set_full_screen", [](const WindowHandle& h, bool full) { h.get().setFullScreen(full); }, py::arg("full_screen"))
		.def("is_full_screen", [](const WindowHandle& h) { return h.get().isFullScreen(); })
		.def("get_monitor_refresh_rate", [](const WindowHandle& h) { return h.get().getMonitorRefreshRate(); },
			"The primary monitor's refresh rate in Hz.")
		.def("get_frame_time", [](const WindowHandle& h) { return h.get().getFrameTime(); },
			"When the current frame began rendering (Rack's clock, seconds); NaN before the first frame.")
		.def("get_last_frame_duration", [](const WindowHandle& h) { return h.get().getLastFrameDuration(); },
			"Seconds the last frame took; NaN before one has finished.")
		.def("get_frame_duration_remaining", [](const WindowHandle& h) { return h.get().getFrameDurationRemaining(); },
			"Seconds left until the frame deadline set by Rack's frame rate limit.")
		.def("load_font", [](const WindowHandle& h, const std::string& filename) { return cachedObject(h.get().loadFont(filename)); },
			py::arg("filename"), "Loads and caches a font with Rack's CJK and emoji fallbacks; None if it cannot be loaded.")
		.def("load_font_without_fallbacks", [](const WindowHandle& h, const std::string& filename) {
			return cachedObject(h.get().loadFontWithoutFallbacks(filename));
		}, py::arg("filename"), "Loads and caches a font without fallbacks; None if it cannot be loaded.")
		.def("load_image", [](const WindowHandle& h, const std::string& filename) { return cachedObject(h.get().loadImage(filename)); },
			py::arg("filename"), "Loads and caches an image; None if it cannot be loaded.")
		.def("load_svg", [](const WindowHandle& h, const std::string& filename) { h.get(); return rack::window::Svg::load(filename); },
			py::arg("filename"), "Svg.load(): loads and caches an SVG; None if it cannot be loaded.");
}

} }
