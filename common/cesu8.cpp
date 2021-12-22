#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "common/cesu8.h"

namespace ag {

size_t cesu8_len(std::string_view utf8) {
    int current_char_len = 0;
    int utf_chars_remaining = 0;
    size_t i = 0;
    for (char c : utf8) {
        if (utf_chars_remaining > 0) {
            if ((c & 0xc0) == 0x80) {
                current_char_len++;
                utf_chars_remaining--;
                if (utf_chars_remaining == 0) {
                    if (current_char_len == 4) {
                        current_char_len = 6;
                    }
                    i += current_char_len;
                }
                continue;
            } else {
                // replacement char
                i += 3;
                utf_chars_remaining = 0;
            }
        }

        if ((c & 0x80) == 0x0) {
            i++;
        } else if ((c & 0xe0) == 0xc0) {
            current_char_len = 1;
            utf_chars_remaining = 1;
        } else if ((c & 0xf0) == 0xe0) {
            current_char_len = 1;
            utf_chars_remaining = 2;
        } else if ((c & 0xf8) == 0xf0) {
            current_char_len = 1;
            utf_chars_remaining = 3;
        } else {
            // replacement char
            i += 3;
            utf_chars_remaining = 0;
        }
    }

    return i;
}

std::string utf8_to_cesu8(std::string_view utf8) {
    int utf_chars_remaining = 0;
    int current_uchar = 0;
    std::string modified_utf;
    modified_utf.reserve(cesu8_len(utf8));
    for (char c : utf8) {
        if (utf_chars_remaining > 0) {
            if ((c & 0xc0) == 0x80) {
                current_uchar <<= 6;
                current_uchar |= c & 0x3f;
                utf_chars_remaining--;
                if (utf_chars_remaining == 0) {
                    if (current_uchar <= 0x7ff) {
                        modified_utf.push_back(0xc0 + ((current_uchar >> 6) & 0x1f));
                        modified_utf.push_back(0x80 + ((current_uchar) & 0x3f));
                    } else if (current_uchar <= 0xffff) {
                        modified_utf.push_back(0xe0 + ((current_uchar >> 12) & 0x0f));
                        modified_utf.push_back(0x80 + ((current_uchar >> 6) & 0x3f));
                        modified_utf.push_back(0x80 + ((current_uchar) & 0x3f));
                    } else {
                        // (current_uchar <= 0x10ffff) is always true
                        // Split into CESU-8 surrogate pair
                        // uchar is 21 bit.
                        // 11101101 1010yyyy 10xxxxxx 11101101 1011xxxx 10xxxxxx
                        // yyyy - top five bits minus one

                        modified_utf.push_back(0xed);
                        modified_utf.push_back(0xa0 + (((current_uchar >> 16) - 1) & 0x0f));
                        modified_utf.push_back(0x80 + ((current_uchar >> 10) & 0x3f));

                        modified_utf.push_back(0xed);
                        modified_utf.push_back(0xb0 + ((current_uchar >> 6) & 0x0f));
                        modified_utf.push_back(0x80 + ((current_uchar >> 0) & 0x3f));
                    }
                }
                continue;
            } else {
                // replacement char
                modified_utf.push_back(0xef);
                modified_utf.push_back(0xbf);
                modified_utf.push_back(0xbd);
                utf_chars_remaining = 0;
            }
        }

        if ((c & 0x80) == 0x0) {
            modified_utf.push_back(c);
        } else if ((c & 0xe0) == 0xc0) {
            current_uchar = c & 0x1f;
            utf_chars_remaining = 1;
        } else if ((c & 0xf0) == 0xe0) {
            current_uchar = c & 0x0f;
            utf_chars_remaining = 2;
        } else if ((c & 0xf8) == 0xf0) {
            current_uchar = c & 0x07;
            utf_chars_remaining = 3;
        } else {
            // replacement char
            modified_utf.push_back(0xef);
            modified_utf.push_back(0xbf);
            modified_utf.push_back(0xbd);
            utf_chars_remaining = 0;
        }
    }

    return modified_utf;
}

} // namespace ag