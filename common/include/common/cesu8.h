#pragma once

#include "common/defs.h"
#include <string_view>

namespace ag {

/**
 * Converts UTF-8 string to CESU-8 (or Java modified UTF-8) string
 * @param utf8 UTF-8 string
 * @return CESU-8 string, which is java safe, or empty string if input string is empty
 */
std::string utf8_to_cesu8(std::string_view utf8);

/**
 * Calculate length on UTF-8 string coverted to CESU-8
 * @param utf8 UTF-8 string
 * @return Length of CESU-8 string(0 if input string is empty)
 */
size_t cesu8_len(std::string_view utf8);

}
