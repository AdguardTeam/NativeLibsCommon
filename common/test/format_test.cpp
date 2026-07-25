#include <gtest/gtest.h>

#include "common/defs.h"
#include "common/format.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
#define AG_DUP _dup
#define AG_DUP2 _dup2
#define AG_CLOSE _close
#define AG_FILENO _fileno
#else
#define AG_DUP dup
#define AG_DUP2 dup2
#define AG_CLOSE close
#define AG_FILENO fileno
#endif

using FilePtr = ag::UniquePtr<FILE, &std::fclose>;

static std::string read_all(FILE *file) {
    fflush(file);
    if (0 != fseek(file, 0, SEEK_SET)) {
        return "<failed to rewind the temporary file>";
    }
    std::string output;
    char buf[256];
    while (size_t read = fread(buf, 1, sizeof(buf), file)) {
        output.append(buf, read);
    }
    return output;
}

/**
 * Returns everything that `writer` has written into the file passed to it.
 */
template <typename Writer>
static std::string capture_file(Writer &&writer) {
    FilePtr file{tmpfile()};
    if (file == nullptr) {
        return "<failed to create a temporary file>";
    }
    std::forward<Writer>(writer)(file.get());
    return read_all(file.get());
}

/**
 * Returns everything that `writer` has written to `stdout`.
 */
template <typename Writer>
static std::string capture_stdout(Writer &&writer) {
    FilePtr file{tmpfile()};
    if (file == nullptr) {
        return "<failed to create a temporary file>";
    }
    fflush(stdout);
    int saved_stdout = AG_DUP(AG_FILENO(stdout));
    AG_DUP2(AG_FILENO(file.get()), AG_FILENO(stdout));
    std::forward<Writer>(writer)();
    fflush(stdout);
    AG_DUP2(saved_stdout, AG_FILENO(stdout));
    AG_CLOSE(saved_stdout);
    return read_all(file.get());
}

TEST(Format, FormatReturnsFormattedString) {
    ASSERT_EQ("the answer is 42", ag::format("the answer is {}", 42));
    ASSERT_EQ("no args", ag::format("no args"));
    ASSERT_EQ("ff 007 str", ag::format("{:x} {:03} {}", 255, 7, std::string_view("str")));
}

TEST(Format, PrintToFileDoesNotAppendNewline) {
    ASSERT_EQ("value 42", capture_file([](FILE *file) {
        ag::print(file, "value {}", 42);
    }));
}

TEST(Format, PrintlnToFileAppendsNewline) {
    ASSERT_EQ("value 42\n", capture_file([](FILE *file) {
        ag::println(file, "value {}", 42);
    }));
}

TEST(Format, PrintlnToFileWithNoArgs) {
    ASSERT_EQ("just text\n", capture_file([](FILE *file) {
        ag::println(file, "just text");
    }));
}

TEST(Format, PrintlnToFileAppendsNewlineOfItsOwn) {
    ASSERT_EQ("value 42\n\n", capture_file([](FILE *file) {
        ag::println(file, "value {}\n", 42);
    }));
}

TEST(Format, PrintWritesToStdout) {
    ASSERT_EQ("value 42", capture_stdout([] {
        ag::print("value {}", 42);
    }));
}

TEST(Format, PrintlnWritesToStdout) {
    ASSERT_EQ("value 42\n", capture_stdout([] {
        ag::println("value {}", 42);
    }));
}
