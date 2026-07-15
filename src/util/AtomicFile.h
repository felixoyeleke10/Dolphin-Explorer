#pragma once

#include <filesystem>
#include <string_view>
#include <system_error>

namespace dolphin::util {

// Returns a unique candidate path beside `destination`, keeping publication on
// the same filesystem so the final replace can be atomic.
std::filesystem::path siblingTempPath(
    const std::filesystem::path& destination,
    std::string_view tag = "tmp");

// Atomically publishes a completed sibling candidate over `destination`.
// The candidate is moved on success and left in place on failure so the caller
// can inspect or remove it. Existing destinations are replaced on Windows and
// POSIX with one filesystem operation.
bool replaceFileAtomically(const std::filesystem::path& candidate,
                           const std::filesystem::path& destination,
                           std::error_code& error);

} // namespace dolphin::util
