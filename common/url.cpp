#include "common/url.h"

#include <sstream>
#include <utility>
#include <vector>

#include "common/utils.h"

static std::string combine_path(std::string_view l, std::string_view r) {
    if (l.ends_with("/")) {
        l.remove_prefix(1);
    }
    if (r.starts_with("/")) {
        r.remove_prefix(1);
    }
    return AG_FMT("{}/{}", l, r);
}

std::string ag::url::normalize_path(std::string_view path) {
    if (path.empty()) {
        return std::string(path);
    }
    if (path.find("./") == path.npos) {
        return std::string(path);
    }

    size_t start_index = 0;
    size_t protocol_index = path.find("//");
    if (protocol_index != path.npos) {
        size_t idx_to_find = protocol_index + 2 > path.length() ? path.length() : protocol_index + 2;
        size_t segments_start_idx = path.find('/', idx_to_find);
        if (segments_start_idx != path.npos) {
            start_index = segments_start_idx + 1 > path.length() ? path.length() : segments_start_idx + 1;
        }
    }

    std::vector<std::string> output;
    std::ostringstream segment_builder;
    for (size_t index = start_index; index < path.length(); index++) {
        char current_char = path[index];
        if (current_char == '?' || current_char == '#') {
            std::string_view s = path.substr(index);
            output.emplace_back(AG_FMT("{}{}", segment_builder.str(), s));
            segment_builder.str("");
            segment_builder.clear();
            break;
        }
        if (current_char != '/' || index == 0) {
            segment_builder << current_char;
            continue;
        }
        std::string segment = segment_builder.str();
        segment_builder.str("");
        segment_builder.clear();
        segment_builder << current_char;
        if (".." == segment || "." == segment || "/." == segment) {
            continue;
        }
        if (segment.starts_with("/..")) {
            if (!output.empty()) {
                output.erase(std::prev(output.end()));
            }
            continue;
        }
        output.emplace_back(std::move(segment));
    }
    if (segment_builder.tellp() > 0) {
        output.push_back(segment_builder.str());
    }
    return combine_path(path.substr(0, start_index), ag::utils::join(output.begin(), output.end(), ""));
}
