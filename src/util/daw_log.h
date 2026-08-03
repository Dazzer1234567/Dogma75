#pragma once

#include <cstdarg>

// Append a printf-formatted line to c:\0_CODE\Dogma75\daw.log with a
// session-relative millisecond stamp and the thread id. Thread-safe.
// Zero-op if the file can't be opened.
void dawLog(const char* fmt, ...);

// Convenience — flush the OS buffer immediately. Called from crash
// handlers so the last few lines survive a subsequent segfault.
void dawLogFlush();
