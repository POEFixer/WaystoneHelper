#pragma once
// Small string helpers shared by the data loader, the scanner and the settings UI.
// All of them work on ASCII ids (item paths, Mods.dat ids); display names are
// UTF-8 and only ever compared case-insensitively on their ASCII letters.

#include <string>
#include <string_view>

namespace WaystoneHelper {

inline char AsciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

inline bool AsciiAlnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline std::string ToLowerCopy(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(AsciiLower(c));
    return out;
}

// True when `hay` contains `needleLower` (which must already be lower case),
// ignoring ASCII case. An empty needle matches everything.
inline bool ContainsCI(std::string_view hay, std::string_view needleLower) {
    if (needleLower.empty()) return true;
    if (needleLower.size() > hay.size()) return false;
    for (size_t i = 0; i + needleLower.size() <= hay.size(); ++i) {
        size_t j = 0;
        for (; j < needleLower.size(); ++j)
            if (AsciiLower(hay[i + j]) != needleLower[j]) break;
        if (j == needleLower.size()) return true;
    }
    return false;
}

// Lower-case letters and digits only: "MapMonsterLife_1" -> "mapmonsterlife1".
inline std::string NormalizeIdentifier(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value)
        if (AsciiAlnum(c)) out.push_back(AsciiLower(c));
    return out;
}

// Waystones live under Metadata/Items/Maps/ (the base name is localized, so the
// English word is only a secondary hint).
inline bool LooksLikeWaystone(std::string_view path, std::string_view baseType) {
    return ContainsCI(path, "/maps/") || ContainsCI(path, "waystone") || ContainsCI(baseType, "waystone");
}

// "Metadata/Items/Maps/MapKeyTier15" -> 15; 0 when the path names no tier.
inline int ParseWaystoneTier(std::string_view path) {
    const std::string lower = ToLowerCopy(path);
    size_t pos = lower.find("tier");
    while (pos != std::string::npos) {
        size_t p = pos + 4;
        if (p < lower.size() && lower[p] >= '0' && lower[p] <= '9') {
            int val = 0;
            while (p < lower.size() && lower[p] >= '0' && lower[p] <= '9' && val < 1000) {
                val = val * 10 + (lower[p] - '0');
                ++p;
            }
            return val;
        }
        pos = lower.find("tier", pos + 1);
    }
    return 0;
}

}  // namespace WaystoneHelper
