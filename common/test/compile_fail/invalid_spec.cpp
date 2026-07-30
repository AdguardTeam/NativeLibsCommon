#include "common/format.h"

#include <string_view>

// An integer presentation type applied to a string argument.
void compile_fail() {
    ag::println("{:d}", std::string_view("str"));
}
