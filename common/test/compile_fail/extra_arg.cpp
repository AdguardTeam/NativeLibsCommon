#include "common/format.h"

// An extra argument that the format string has no place for. Plain fmt accepts this; rejecting it
// is the whole point of `StrictFormatString`, so this is the case the other ones guard.
void compile_fail() {
    ag::println("value {}", 42, 43);
}
