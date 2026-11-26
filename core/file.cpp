#include "file.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

namespace spools {

static FILE* openUtf8(const std::string& path, const char* mode) {
#ifdef _WIN32
	auto widen = [](const std::string& s) {
		int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
		std::wstring w(n > 0 ? n - 1 : 0, L'\0');
		if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
		return w;
	};
	return _wfopen(widen(path).c_str(), widen(mode).c_str());
#else
	return std::fopen(path.c_str(), mode);
#endif
}

static bool fail(std::string* error, const std::string& what, const std::string& path) {
	if (error) *error = what + " " + path + ": " + std::strerror(errno);
	return false;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string* error) {
	FILE* f = openUtf8(path, "rb");
	if (!f) return fail(error, "Cannot open", path);
	out.clear();
	uint8_t buf[1 << 16];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
	bool ok = !std::ferror(f);
	std::fclose(f);
	return ok || fail(error, "Cannot read", path);
}

bool writeFile(const std::string& path, const uint8_t* data, size_t size, std::string* error) {
	FILE* f = openUtf8(path, "wb");
	if (!f) return fail(error, "Cannot create", path);
	bool ok = std::fwrite(data, 1, size, f) == size;
	ok = (std::fclose(f) == 0) && ok;
	return ok || fail(error, "Cannot write", path);
}

std::string fileExtension(const std::string& path) {
	size_t slash = path.find_last_of("/\\"), dot = path.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
	std::string ext = path.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
	return ext;
}

}
