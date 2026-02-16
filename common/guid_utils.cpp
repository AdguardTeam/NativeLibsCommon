#include "common/guid_utils.h"

#include <cstdint>

#include <openssl/rand.h>

#include "common/utils.h"

/// Placement of dashes in guid string format
constexpr uint8_t GUID_DASH_IDX[] = {8, 13, 18, 23};
/// Size of data fields in guid string format
constexpr uint8_t GUID_FIELD_SIZE[] = {8, 4, 4, 2};
/// Base of numbers used in GUID
constexpr uint8_t GUID_BASE = 16;

std::string ag::guid_to_string(const GUID &guid) {
    return AG_FMT("{{{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}}}", guid.Data1, guid.Data2,
            guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
            guid.Data4[6], guid.Data4[7]);
}

std::optional<GUID> ag::string_to_guid(std::string_view guid_str) {
    if (guid_str.empty()) {
        return std::nullopt;
    }
    // Handle {} and ()
    if (guid_str.size() == 38 && (guid_str[0] == '{' && guid_str[37] == '}')) {
        guid_str.remove_prefix(1);
        guid_str.remove_suffix(1);
    }
    // check dash format
    if (guid_str.size() != 36 || guid_str[GUID_DASH_IDX[0]] != '-' || guid_str[GUID_DASH_IDX[1]] != '-'
            || guid_str[GUID_DASH_IDX[2]] != '-' || guid_str[GUID_DASH_IDX[3]] != '-') {
        return std::nullopt;
    }
    GUID guid{};
    auto res = ag::utils::to_integer<unsigned long>({guid_str.data(), GUID_FIELD_SIZE[0]}, GUID_BASE);
    if (!res.has_value()) {
        return std::nullopt;
    }
    guid.Data1 = res.value();
    guid_str.remove_prefix(GUID_FIELD_SIZE[0] + 1);
    res = ag::utils::to_integer<unsigned short>({guid_str.data(), GUID_FIELD_SIZE[1]}, GUID_BASE);
    if (!res.has_value()) {
        return std::nullopt;
    }
    guid.Data2 = res.value();
    guid_str.remove_prefix(GUID_FIELD_SIZE[1] + 1);
    res = ag::utils::to_integer<unsigned short>({guid_str.data(), GUID_FIELD_SIZE[2]}, GUID_BASE);
    if (!res.has_value()) {
        return std::nullopt;
    }
    guid.Data3 = res.value();
    guid_str.remove_prefix(GUID_FIELD_SIZE[2] + 1);
    for (unsigned char &data_field : guid.Data4) {
        res = ag::utils::to_integer<unsigned char>({guid_str.data(), GUID_FIELD_SIZE[3]}, GUID_BASE);
        if (!res.has_value()) {
            return std::nullopt;
        }
        data_field = res.value();
        guid_str.remove_prefix(GUID_FIELD_SIZE[3]);
        // skip last dash
        if (guid_str[0] == '-') {
            guid_str.remove_prefix(1);
        }
    }
    return guid;
}

GUID ag::random_guid() {
    GUID guid{};
    RAND_bytes((uint8_t *) &guid, sizeof(guid));
    guid.Data3 = 0x40 | (guid.Data3 & 0x0f);
    guid.Data4[0] = 0x80 | (guid.Data4[0] & 0x3f);
    return guid;
}
