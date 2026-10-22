#pragma once
// Lightweight INI value parsing shared by the Windows loader and portable tests.
// Accept inline "; comment" text without treating it as part of a setting.
#include <cerrno>
#include <climits>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace cr {

inline void StripIniComment(char* value) {
    if (!value) return;
    char* start = value;
    while (*start && std::isspace(static_cast<unsigned char>(*start)))
        ++start;
    if (start != value)
        std::memmove(value, start, std::strlen(start) + 1);

    for (char* p = value; *p; ++p) {
        // Only whitespace-prefixed semicolons count as comments.
        if (*p == ';' && (p == value ||
                          std::isspace(static_cast<unsigned char>(p[-1])))) {
            *p = '\0';
            break;
        }
    }
    char* end = value + std::strlen(value);
    while (end > value &&
           std::isspace(static_cast<unsigned char>(end[-1])))
        *--end = '\0';
}

inline float ParseIniFloat(char* value, float fallback) {
    StripIniComment(value);
    if (!value || !*value) return fallback;
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value, &end);
    return end != value && end && *end == '\0' &&
           errno != ERANGE && std::isfinite(parsed) ? parsed : fallback;
}

inline int ParseIniInt(char* value, int fallback) {
    StripIniComment(value);
    if (!value || !*value) return fallback;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    return end != value && end && *end == '\0' &&
           errno != ERANGE && parsed >= INT_MIN && parsed <= INT_MAX
        ? static_cast<int>(parsed) : fallback;
}

} // namespace cr
