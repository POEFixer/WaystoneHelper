// Text helpers that need no SDK (STL only), so the tests build them: a slider's number format
// from a translated text.
#pragma once

#include <string>
#include <string_view>

namespace WaystoneHelper {

// Marks where a slider's own number goes in a translated text (a TrF argument).
inline constexpr const char* kNumberMark = "\x01";

// The ImGui number format of a slider from a translated text: the text's own '%' are doubled
// (a translation is never a format), the FIRST kNumberMark becomes `conversion`. A later mark
// (a translation naming {value} twice) is written back as "{value}": ImGui passes one number,
// so a second conversion would read an argument that is not there.
inline std::string NumberFormat(std::string_view text, const char* conversion = "%d") {
    std::string out;
    out.reserve(text.size() + 4);
    bool placed = false;
    for (const char c : text) {
        if (c == '%') {
            out += "%%";
        } else if (c == kNumberMark[0]) {
            out += placed ? "{value}" : (conversion ? conversion : "");
            placed = true;
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace WaystoneHelper
