#include "app_paths.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
bool dirExists(const std::string& path) {
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
#endif

std::string resolveRoot() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (n == 0) return ".";
    std::string dir(buf, n);
    size_t slash = dir.find_last_of("\\/");
    if (slash == std::string::npos) return ".";
    dir.resize(slash);

    const std::string exeDir = dir;

    // Walk up looking for the project root. "settings" is the marker: it is
    // present in the repo and is also what a flat deployment would ship
    // beside the exe. Bounded so a missing marker can't walk to the drive
    // root and beyond.
    for (int i = 0; i < 6; i++) {
        if (dirExists(dir + "\\settings")) return dir;
        size_t s = dir.find_last_of("\\/");
        if (s == std::string::npos) break;
        dir.resize(s);
    }
    // No marker found — fall back to the exe's own directory rather than
    // an absolute path that may not exist on this machine.
    return exeDir;
#else
    return ".";
#endif
}

} // namespace

const std::string& appRoot() {
    static const std::string root = resolveRoot();
    return root;
}

std::string appPath(const std::string& relative) {
    std::string r = appRoot();
    if (!r.empty() && r.back() != '\\' && r.back() != '/') r += '\\';
    return r + relative;
}
