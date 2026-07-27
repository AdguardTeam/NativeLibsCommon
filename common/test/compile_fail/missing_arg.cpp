#include "common/format.h"

// One replacement field too many for the arguments passed.
void compile_fail() {
    ag::println("{} {}", 42);
}
