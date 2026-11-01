// SPDX-License-Identifier: GPL-3.0-or-later
// rackpy::dynlib for POSIX (dlopen) and Windows (LoadLibraryExW). Includes no Rack header.
#include "rackpy/dynlib.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <mach-o/dyld.h>
#else
#include <dlfcn.h>
#include <link.h>
#endif

#include <cstdint>
#include <cstdlib>

namespace rackpy {
namespace dynlib {

#if defined(_WIN32)

namespace {

std::wstring wide(const std::string& utf8) {
	if (utf8.empty())
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int) utf8.size(), nullptr, 0);
	if (n <= 0)
		return std::wstring();
	std::wstring out((size_t) n, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int) utf8.size(), &out[0], n);
	return out;
}

std::string narrow(const wchar_t* text, int length = -1) {
	int n = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
	if (n <= 0)
		return std::string();
	std::string out((size_t) n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, length, &out[0], n, nullptr, nullptr);
	if (length < 0 && !out.empty() && out.back() == '\0')
		out.pop_back();
	return out;
}

std::string lastError() {
	DWORD code = GetLastError();
	wchar_t* text = nullptr;
	DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, code, 0, (wchar_t*) &text, 0, nullptr);
	std::string message = n ? narrow(text, (int) n) : std::string();
	if (text)
		LocalFree(text);
	while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' '))
		message.pop_back();
	return message + " (Windows error " + std::to_string(code) + ")";
}

}  // namespace

void* openPinned(const std::string& path, bool global, std::string& error) {
	(void) global;
	std::wstring file = wide(path);
	if (file.empty()) {
		error = "the path is not valid UTF-8";
		return nullptr;
	}
	// LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR needs a full path with backslashes.
	for (wchar_t& c : file)
		if (c == L'/')
			c = L'\\';
	// Without this, a file that is not a valid DLL makes Windows show a modal "Bad Image" error box, and Rack
	// waits on it (headless, forever); with it, LoadLibraryExW returns error 193 to log instead (RP-41).
	DWORD previousMode = 0;
	SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previousMode);
	HMODULE module = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	DWORD loadError = module ? 0 : GetLastError();
	SetThreadErrorMode(previousMode, nullptr);
	if (!module) {
		SetLastError(loadError);
		error = lastError();
		return nullptr;
	}
	HMODULE pinned = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
			reinterpret_cast<LPCWSTR>(module), &pinned)) {
		error = "loaded, but could not be pinned: " + lastError();
		return nullptr;
	}
	return reinterpret_cast<void*>(module);
}

void* symbol(void* handle, const char* name) {
	return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
}

std::string canonicalPath(const std::string& path) {
	std::wstring file = wide(path);
	if (file.empty())
		return std::string();
	// FILE_FLAG_BACKUP_SEMANTICS opens folders too; no access rights are needed to ask for the final path.
	HANDLE handle = CreateFileW(file.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
		return std::string();
	std::wstring out(MAX_PATH, L'\0');
	DWORD n = GetFinalPathNameByHandleW(handle, &out[0], (DWORD) out.size(), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (n >= out.size()) {
		out.resize(n + 1);
		n = GetFinalPathNameByHandleW(handle, &out[0], (DWORD) out.size(), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	}
	CloseHandle(handle);
	if (n == 0 || n >= out.size())
		return std::string();
	out.resize(n);
	if (out.compare(0, 8, L"\\\\?\\UNC\\") == 0)
		out = L"\\\\" + out.substr(8);  // \\?\UNC\server\share -> \\server\share
	else if (out.compare(0, 4, L"\\\\?\\") == 0)
		out = out.substr(4);  // \\?\C:\... -> C:\...
	std::string result = narrow(out.c_str(), (int) out.size());
	for (char& c : result)
		if (c == '\\')
			c = '/';
	while (result.size() > 3 && result.back() == '/')
		result.pop_back();
	return result;
}

std::vector<std::string> loadedImages() {
	std::vector<std::string> images;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return images;
	MODULEENTRY32W entry;
	entry.dwSize = sizeof(entry);
	for (BOOL more = Module32FirstW(snapshot, &entry); more; more = Module32NextW(snapshot, &entry))
		images.push_back(narrow(entry.szExePath));
	CloseHandle(snapshot);
	return images;
}

#else

void* openPinned(const std::string& path, bool global, std::string& error) {
	void* handle = dlopen(path.c_str(), RTLD_NOW | (global ? RTLD_GLOBAL : RTLD_LOCAL) | RTLD_NODELETE);
	if (!handle) {
		const char* why = dlerror();
		error = why ? why : "dlopen failed";
	}
	return handle;
}

void* symbol(void* handle, const char* name) {
	return dlsym(handle, name);
}

std::string canonicalPath(const std::string& path) {
	char* resolved = realpath(path.c_str(), nullptr);
	if (!resolved)
		return std::string();
	std::string result(resolved);
	std::free(resolved);
	return result;
}

#if defined(__APPLE__)
std::vector<std::string> loadedImages() {
	// dyld's image list, as dl_iterate_phdr's on Linux: also images other plugins opened with RTLD_LOCAL.
	std::vector<std::string> images;
	for (uint32_t i = 0, n = _dyld_image_count(); i < n; i++) {
		const char* name = _dyld_get_image_name(i);
		if (name)
			images.push_back(name);
	}
	return images;
}
#else
namespace {
int collect(struct dl_phdr_info* info, size_t, void* data) {
	if (info->dlpi_name && info->dlpi_name[0])
		static_cast<std::vector<std::string>*>(data)->push_back(info->dlpi_name);
	return 0;
}
}  // namespace

std::vector<std::string> loadedImages() {
	std::vector<std::string> images;
	dl_iterate_phdr(collect, &images);
	return images;
}
#endif

#endif

}  // namespace dynlib
}  // namespace rackpy
