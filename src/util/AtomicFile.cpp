#include "util/AtomicFile.h"

#include <atomic>
#include <chrono>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dolphin::util {

std::filesystem::path siblingTempPath(
    const std::filesystem::path& destination,
    std::string_view tag)
{
    static std::atomic<uint64_t> sequence{0};
    const auto tick = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const uint64_t id = sequence.fetch_add(1, std::memory_order_relaxed);

    std::filesystem::path candidate = destination;
    candidate += "." + std::string(tag.empty() ? "tmp" : tag)
               + "." + std::to_string(tick)
               + "." + std::to_string(id);
    return candidate;
}

bool replaceFileAtomically(const std::filesystem::path& candidate,
                           const std::filesystem::path& destination,
                           std::error_code& error)
{
    error.clear();
    if (candidate.empty() || destination.empty() || candidate == destination) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }

#ifdef _WIN32
    if (::MoveFileExW(candidate.c_str(), destination.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    error.assign(static_cast<int>(::GetLastError()), std::system_category());
    return false;
#else
    std::filesystem::rename(candidate, destination, error);
    return !error;
#endif
}

} // namespace dolphin::util
