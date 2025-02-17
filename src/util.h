// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NINJA_UTIL_H_
#define NINJA_UTIL_H_

#ifdef _WIN32
#include "win32port.h"
#else
#include <stdint.h>
#endif

#include <stdarg.h>

#include <string>
#include <vector>

#ifdef _MSC_VER
#define NORETURN __declspec(noreturn)
#else
#define NORETURN __attribute__((noreturn))
#endif

/// Log a fatal message and exit.
NORETURN void Fatal(const char* msg, ...);

// Have a generic fall-through for different versions of C/C++.
#if defined(__cplusplus) && __cplusplus >= 201703L
#define NINJA_FALLTHROUGH [[fallthrough]]
#elif defined(__cplusplus) && __cplusplus >= 201103L && defined(__clang__)
#define NINJA_FALLTHROUGH [[clang::fallthrough]]
#elif defined(__cplusplus) && __cplusplus >= 201103L && defined(__GNUC__) && \
    __GNUC__ >= 7
#define NINJA_FALLTHROUGH [[gnu::fallthrough]]
#elif defined(__GNUC__) && __GNUC__ >= 7 // gcc 7
#define NINJA_FALLTHROUGH __attribute__ ((fallthrough))
#else // C++11 on gcc 6, and all other cases
#define NINJA_FALLTHROUGH
#endif

/// Log a warning message.
void Warning(const char* msg, ...);
void Warning(const char* msg, va_list ap);

/// Log an error message.
void Error(const char* msg, ...);
void Error(const char* msg, va_list ap);

/// Log an informational message.
void Info(const char* msg, ...);
void Info(const char* msg, va_list ap);

/// Canonicalize a path like "foo/../bar.h" into just "bar.h".
/// |slash_bits| has bits set starting from lowest for a backslash that was
/// normalized to a forward slash. (only used on Windows)
void CanonicalizePath(std::string* path, uint64_t* slash_bits);
void CanonicalizePath(char* path, size_t* len, uint64_t* slash_bits);

/// Appends |input| to |*result|, escaping according to the whims of either
/// Bash, or Win32's CommandLineToArgvW().
/// Appends the string directly to |result| without modification if we can
/// determine that it contains no problematic characters.
void GetShellEscapedString(const std::string& input, std::string* result);
void GetWin32EscapedString(const std::string& input, std::string* result);

/// Read a file to a string (in text mode: with CRLF conversion
/// on Windows).
/// Returns -errno and fills in \a err on error.
int ReadFile(const std::string& path, std::string* contents, std::string* err);

/// Mark a file descriptor to not be inherited on exec()s.
void SetCloseOnExec(int fd);

/// Given a misspelled string and a list of correct spellings, returns
/// the closest match or NULL if there is no close enough match.
const char* SpellcheckStringV(const std::string& text,
                              const std::vector<const char*>& words);

/// Like SpellcheckStringV, but takes a NULL-terminated list.
const char* SpellcheckString(const char* text, ...);

bool islatinalpha(int c);

/// Removes all Ansi escape codes (http://www.termsys.demon.co.uk/vtansi.htm).
std::string StripAnsiEscapeCodes(const std::string& in);

/// @return the number of processors on the machine.  Useful for an initial
/// guess for how many jobs to run in parallel.  @return 0 on error.
int GetProcessorCount();

/// @return the load average of the machine. A negative value is returned
/// on error.
double GetLoadAverage();

/// Elide the given string @a str with '...' in the middle if the length
/// exceeds @a width.
std::string ElideMiddle(const std::string& str, size_t width);

/// Truncates a file to the given size.
bool Truncate(const std::string& path, size_t size, std::string* err);

#ifdef _MSC_VER
#define snprintf _snprintf
#define fileno _fileno
#define unlink _unlink
#define chdir _chdir
#define strtoull _strtoui64
#define getcwd _getcwd
#define PATH_MAX _MAX_PATH
#endif

#ifdef _WIN32
/// Convert the value returned by GetLastError() into a string.
std::string GetLastErrorString();

/// Calls Fatal() with a function name and GetLastErrorString.
NORETURN void Win32Fatal(const char* function, const char* hint = NULL);
#else
const std::string kCleanConsoleSymbol = "\033[0J";
const std::string kCleanLineSymbol = "\033[0K";
#endif

#endif  // NINJA_UTIL_H_
