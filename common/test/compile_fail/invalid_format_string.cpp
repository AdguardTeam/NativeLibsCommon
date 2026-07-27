#include "common/format.h"

// A replacement field that is never closed.
void compile_fail() {
    ag::println("unclosed {");
}
