#pragma once

#include <cstdint>

// If StdString::CStdStr is already defined via other includes, great.
// If not, include the real header where it lives in your ITGMania slice.
// Try "StdString.h" first; if that doesn't exist in your extern tree,
// replace it with whatever defines StdString::CStdStr.
#include "StdString.h"

// The real engine exposes these as free functions in namespace CrashHandler.
// Keep the signatures aligned with archutils/*/Crash*.{h,cpp} so the linker
// finds our no-op stubs on Windows.
namespace CrashHandler {
void ForceCrash(char const* reason = nullptr);
void ForceDeadlock(StdString::CStdStr<char> reason, std::uint64_t id);
void ForceDeadlock(StdString::CStdStr<char> const& reason, std::uint64_t id);
} // namespace CrashHandler
