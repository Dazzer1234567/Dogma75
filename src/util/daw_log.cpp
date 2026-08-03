#include "daw_log.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

static std::mutex     s_logMutex;
static FILE*          s_logFile = nullptr;
static int64_t        s_startMs = 0;

static void ensureOpen_locked() {
    if (s_logFile) return;
    s_logFile = std::fopen("c:\\0_CODE\\Dogma75\\daw.log", "w");   // truncate each run
    s_startMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
    if (s_logFile) {
        std::fprintf(s_logFile,
            "==== daw.log — new session ====\n");
        std::fflush(s_logFile);
    }
}

void dawLog(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    ensureOpen_locked();
    if (!s_logFile) return;
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
    unsigned tid = (unsigned)std::hash<std::thread::id>{}(std::this_thread::get_id());
    std::fprintf(s_logFile, "%6lld [t%04x] ", (long long)(nowMs - s_startMs), tid & 0xffff);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(s_logFile, fmt, ap);
    va_end(ap);
    std::fputc('\n', s_logFile);
    std::fflush(s_logFile);   // Never buffer — we want the last line before a crash.
}

void dawLogFlush() {
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (s_logFile) std::fflush(s_logFile);
}
