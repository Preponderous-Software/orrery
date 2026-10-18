// A tiny assertion harness: CHECK records a failure and keeps going, so one
// run reports every broken expectation. main() returns non-zero on failure.
#pragma once

#include <cstdio>
#include <string>

namespace check {
inline int& failures() {
    static int n = 0;
    return n;
}
inline int finish() {
    std::printf("%s (%d failure%s)\n", failures() ? "FAILED" : "ALL PASSED", failures(), failures() == 1 ? "" : "s");
    return failures() ? 1 : 0;
}
}  // namespace check

#define CHECK_MSG(cond, msg)                                                                              \
    do {                                                                                                  \
        if (!(cond)) {                                                                                    \
            std::printf("  FAIL %s:%d: %s -- %s\n", __FILE__, __LINE__, #cond, std::string(msg).c_str()); \
            ++check::failures();                                                                          \
        }                                                                                                 \
    } while (0)
#define CHECK(cond) CHECK_MSG(cond, "")
#define SECTION(name) std::printf("- %s\n", name)
