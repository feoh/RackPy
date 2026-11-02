// SPDX-License-Identifier: GPL-3.0-or-later
// rack::system -> rackpy.raw.system (RP-30B). Paths are str or os.PathLike,
// returned as str. Calls that touch the disk or sleep release the GIL. Python
// never runs on Rack's audio path, so none of these block it.
//
// Rack 2.6.6, guarded here:
// - runProcessDetached is Windows-only and asserts elsewhere: refused off Windows.
// - openBrowser and openDirectory run `xdg-open "<string>"` through the shell
//   (Rack's header warns about injection): ", \, $ and ` are refused, since
//   inside the double quotes they run shell code.
// - unarchiveToDirectory follows ../ entries out of the directory (only absolute
//   paths are refused): documented; rackpy.services.system.unarchive checks first.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include <rack.hpp>

#include "rackpy/runtime/buffers.hpp"
#include "services.hpp"

namespace rackpy {
namespace bindings {
namespace services {

namespace py = pybind11;
namespace sy = rack::system;

std::string pathArg(py::handle value) {
	return py::module_::import("os").attr("fspath")(value).cast<std::string>();
}

namespace {

py::bytes bytesOf(const std::vector<uint8_t>& data) {
	return py::bytes(reinterpret_cast<const char*>(data.data()), data.size());
}

std::vector<uint8_t> dataOf(py::handle value) {
	py::buffer_info info = py::reinterpret_borrow<py::buffer>(value).request();
	if (info.ndim != 1 || info.itemsize != 1)
		throw py::type_error("expected bytes (a 1-D byte buffer)");
	runtime::requireContiguous(info, "the data");
	auto* p = static_cast<const uint8_t*>(info.ptr);
	return std::vector<uint8_t>(p, p + info.size);
}

/** On Linux and macOS Rack runs `xdg-open "..."` or `open "..."` through the shell. Windows' ShellExecuteW runs no
shell, and its paths have backslashes, so nothing is refused there (RP-42: every Windows path was). */
void requireShellSafe(const std::string& s, const char* what) {
#if defined ARCH_WIN
	(void) s;
	(void) what;
#else
	if (s.find_first_of("\"\\$`") != std::string::npos)
		throw py::value_error(std::string(what) + " runs `xdg-open \"...\"` (macOS: `open`) through the shell, where \", \\, $ "
			"and ` run shell code: RackPy refuses them");
#endif
}

/** Flags Rack's setFpuFlags would hand to the CPU and take the process down with (RP-42). On x64, MXCSR bits 16 to 31 are
reserved and writing one raises #GP (SIGSEGV on Linux); a cleared exception mask (bits 7 to 12) makes the thread's own
floating-point arithmetic trap with SIGFPE, and Python runs on this thread. On arm64, FPCR's trap-enable bits (8 to 12
and 15) do the same where the CPU supports trapping. Anything else Rack accepts is passed through. */
void requireSurvivableFpuFlags(uint32_t flags) {
#if defined ARCH_X64
	if (flags & 0xFFFF0000u)
		throw py::value_error("set_fpu_flags: MXCSR bits 16 to 31 are reserved: writing one faults the CPU and kills the "
			"process");
	if ((flags & 0x1F80u) != 0x1F80u)
		throw py::value_error("set_fpu_flags: every MXCSR exception mask (bits 7 to 12) must stay set: an unmasked "
			"floating-point exception kills Python with SIGFPE");
#elif defined ARCH_ARM64
	if (flags & 0x9F00u)
		throw py::value_error("set_fpu_flags: FPCR's trap-enable bits (8 to 12 and 15) must stay clear: a trapped "
			"floating-point exception kills Python with SIGFPE");
#else
	(void) flags;
#endif
}

}  // namespace

void bindSystem(py::module_ m) {
	m.def("join", [](py::object path1, py::object path2, py::args paths) {
		std::string result = sy::join(pathArg(path1), path2.is_none() ? std::string() : pathArg(path2));
		for (py::handle p : paths)
			result = sy::join(result, pathArg(p));
		return result;
	}, py::arg("path1"), py::arg("path2") = py::none(), "Rack's join (and its variadic template): path1/path2/...; an empty "
		"path2 returns path1 with a trailing separator.");
	m.def("get_entries", [](py::object dir, int depth) {
		std::string path = pathArg(dir);
		py::gil_scoped_release release;
		return sy::getEntries(path, depth);
	}, py::arg("dir_path"), py::arg("depth") = 0, "Rack's: the entries' paths (depth -1 recurses all the way).");
	m.def("exists", [](py::object p) { return sy::exists(pathArg(p)); }, py::arg("path"));
	m.def("is_file", [](py::object p) { return sy::isFile(pathArg(p)); }, py::arg("path"));
	m.def("is_directory", [](py::object p) { return sy::isDirectory(pathArg(p)); }, py::arg("path"));
	m.def("get_file_size", [](py::object p) { return sy::getFileSize(pathArg(p)); }, py::arg("path"));
	m.def("rename", [](py::object a, py::object b) {
		std::string src = pathArg(a), dst = pathArg(b);
		py::gil_scoped_release release;
		return sy::rename(src, dst);
	}, py::arg("src_path"), py::arg("dest_path"), "Rack's: whether it renamed (overwriting a file, or an empty directory).");
	m.def("copy", [](py::object a, py::object b) {
		std::string src = pathArg(a), dst = pathArg(b);
		py::gil_scoped_release release;
		return sy::copy(src, dst);
	}, py::arg("src_path"), py::arg("dest_path"), "Rack's: whether it copied (recursively), overwriting.");
	m.def("create_directory", [](py::object p) { return sy::createDirectory(pathArg(p)); }, py::arg("path"));
	m.def("create_directories", [](py::object p) { return sy::createDirectories(pathArg(p)); }, py::arg("path"));
	m.def("create_symbolic_link", [](py::object target, py::object link) {
		return sy::createSymbolicLink(pathArg(target), pathArg(link));
	}, py::arg("target"), py::arg("link"));
	m.def("remove", [](py::object p) { return sy::remove(pathArg(p)); }, py::arg("path"),
		"Rack's: removes a file or an empty directory; whether it did.");
	m.def("remove_recursively", [](py::object p) {
		std::string path = pathArg(p);
		py::gil_scoped_release release;
		return sy::removeRecursively(path);
	}, py::arg("path"), "Rack's: how many files and directories it removed.");
	m.def("get_working_directory", &sy::getWorkingDirectory);
	m.def("set_working_directory", [](py::object p) { sy::setWorkingDirectory(pathArg(p)); }, py::arg("path"),
		"Rack's: the process's working directory (Rack's and every plugin's).");
	m.def("get_temp_directory", &sy::getTempDirectory);
	m.def("get_absolute", [](py::object p) { return sy::getAbsolute(pathArg(p)); }, py::arg("path"));
	m.def("get_canonical", [](py::object p) { return sy::getCanonical(pathArg(p)); }, py::arg("path"),
		"Rack's: the path resolved (it must exist).");
	m.def("get_directory", [](py::object p) { return sy::getDirectory(pathArg(p)); }, py::arg("path"));
	m.def("get_filename", [](py::object p) { return sy::getFilename(pathArg(p)); }, py::arg("path"));
	m.def("get_stem", [](py::object p) { return sy::getStem(pathArg(p)); }, py::arg("path"));
	m.def("get_extension", [](py::object p) { return sy::getExtension(pathArg(p)); }, py::arg("path"));
	m.def("read_file", [](py::object p) {
		std::string path = pathArg(p);
		std::vector<uint8_t> data;
		{
			py::gil_scoped_release release;
			data = sy::readFile(path);  // Rack raises on error
		}
		return bytesOf(data);
	}, py::arg("path"), "Rack's readFile (both overloads): the file's bytes; Rack's Exception on error.");
	m.def("write_file", [](py::object p, py::object data) {
		std::string path = pathArg(p);
		std::vector<uint8_t> bytes = dataOf(data);
		py::gil_scoped_release release;
		sy::writeFile(path, bytes);
	}, py::arg("path"), py::arg("data"), "Rack's: writes the bytes (replacing the file); Rack's Exception on error.");
	m.def("archive_directory", [](py::object archive, py::object dir, int level) {
		std::string a = pathArg(archive), d = pathArg(dir);
		py::gil_scoped_release release;
		sy::archiveDirectory(a, d, level);
	}, py::arg("archive_path"), py::arg("dir_path"), py::arg("compression_level") = 1,
		"Rack's: dir_path's contents as a .tar.zst at archive_path.");
	m.def("archive_directory_to_bytes", [](py::object dir, int level) {
		std::string d = pathArg(dir);
		std::vector<uint8_t> data;
		{
			py::gil_scoped_release release;
			data = sy::archiveDirectory(d, level);
		}
		return bytesOf(data);
	}, py::arg("dir_path"), py::arg("compression_level") = 1, "Rack's archiveDirectory overload that returns the archive.");
	m.def("unarchive_to_directory", [](py::object archive, py::object dir) {
		std::string d = pathArg(dir);
		if (py::isinstance<py::bytes>(archive) || py::isinstance<py::bytearray>(archive) || py::isinstance<py::memoryview>(archive)) {
			std::vector<uint8_t> data = dataOf(archive);
			py::gil_scoped_release release;
			sy::unarchiveToDirectory(data, d);
			return;
		}
		std::string a = pathArg(archive);
		py::gil_scoped_release release;
		sy::unarchiveToDirectory(a, d);
	}, py::arg("archive"), py::arg("dir_path"),
		"Rack's (both overloads): unpacks a .tar.zst (a path or bytes) into dir_path. A zero-byte file deletes the "
		"existing one. Rack 2.6.6 refuses absolute entry paths but follows ../ entries out of dir_path: unpack only "
		"archives you trust (rackpy.services.system.unarchive checks).");
	m.def("get_logical_core_count", &sy::getLogicalCoreCount);
	m.def("set_thread_name", [](const std::string& name) { sy::setThreadName(name); }, py::arg("name"),
		"Rack's: names the calling thread (15 bytes on Linux).");
	m.def("get_stack_trace", &sy::getStackTrace, "Rack's: the calling thread's native stack, as text.");
	m.def("get_time", &sy::getTime, "Rack's monotonic time in seconds, for measuring.");
	m.def("get_unix_time", &sy::getUnixTime);
	m.def("get_thread_time", &sy::getThreadTime, "Rack's: the calling thread's CPU time in seconds.");
	m.def("sleep", [](double time) {
		// Rack sleeps for std::chrono::duration<double>(time): zero or less returns at once, as it does in C++. NaN,
		// infinity and a time past the clock's nanosecond range (about 9.2e9 seconds) overflow that conversion, which is
		// undefined (RP-42).
		if (!std::isfinite(time) || time > 9.0e9)
			throw py::value_error("the time is a finite number of seconds, at most 9e9 (zero or less returns at once)");
		py::gil_scoped_release release;
		sy::sleep(time);
	}, py::arg("time"), "Rack's: sleeps the calling thread (on the main thread, Rack's window stops meanwhile).");
	m.def("get_operating_system_info", &sy::getOperatingSystemInfo);
	m.def("open_browser", [](const std::string& url) {
		requireShellSafe(url, "open_browser");
		sy::openBrowser(url);
	}, py::arg("url"), "Rack's: opens the URL with the desktop's handler, without blocking (blank: nothing).");
	m.def("open_directory", [](py::object p) {
		std::string path = pathArg(p);
		requireShellSafe(path, "open_directory");
		sy::openDirectory(path);
	}, py::arg("path"), "Rack's: opens the directory with the desktop's file manager, without blocking (blank: nothing).");
	m.def("run_process_detached", [](py::object p) {
		std::string path = pathArg(p);
#if defined ARCH_WIN
		sy::runProcessDetached(path);
#else
		(void) path;
		throw py::value_error("Rack implements runProcessDetached on Windows only (on Linux and macOS it asserts)");
#endif
	}, py::arg("path"), "Rack's: starts the program (with elevated rights), detached. Windows only.");
	m.def("get_fpu_flags", &sy::getFpuFlags, "Rack's: the calling thread's MXCSR (x64) or FPCR (ARM64).");
	m.def("set_fpu_flags", [](uint32_t flags) {
		requireSurvivableFpuFlags(flags);
		sy::setFpuFlags(flags);
	}, py::arg("flags"),
		"Rack's: sets the calling thread's floating-point flags (Python's own arithmetic on the thread follows them). "
		"Flags that would kill the process are refused with ValueError: reserved MXCSR bits and unmasked exceptions on x64, "
		"FPCR trap-enable bits on arm64.");
	m.def("reset_fpu_flags", &sy::resetFpuFlags,
		"Rack's: flush-to-zero and denormals-are-zero on the calling thread, as Rack's engine threads run.");
}

}  // namespace services
}  // namespace bindings
}  // namespace rackpy
