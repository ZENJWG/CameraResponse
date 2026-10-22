#pragma once
#include <utility>

namespace cr {

// Construct a clean config before parsing. Removed INI overrides revert to
// defaults; the live configuration changes only after successful loading.
template <typename T>
bool ReloadConfigFresh(T& active) {
    T next{};
    next.dir = active.dir;
    next.Load();
    if (next.iniPath.empty())
        return false;
    active = std::move(next);
    return true;
}

} // namespace cr
