#include "common/format.h"

// Named arguments, which `StrictFormatStringChecker` rejects outright since it counts positional
// ones.
void compile_fail() {
    ag::println("{name}", 42);
}
