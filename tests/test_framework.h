#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace testfw {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failure_count() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

inline int run_all() {
    int passed = 0;
    for (const auto& test : registry()) {
        int before = failure_count();
        test.fn();
        if (failure_count() == before) {
            std::printf("PASS  %s\n", test.name.c_str());
            ++passed;
        } else {
            std::printf("FAIL  %s\n", test.name.c_str());
        }
    }
    std::printf("\n%d/%zu tests passed\n", passed, registry().size());
    return failure_count() == 0 ? 0 : 1;
}

}  // namespace testfw

#define TEST(name)                                                  \
    static void test_##name();                                      \
    static testfw::Registrar registrar_##name(#name, test_##name);  \
    static void test_##name()

// CHECK works for any boolean expression (including optional == value).
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++testfw::failure_count();                                    \
            std::printf("  CHECK failed: %s (%s:%d)\n", #cond, __FILE__,  \
                        __LINE__);                                        \
        }                                                                 \
    } while (0)

// CHECK_EQ is for integral values only (prints both sides on failure).
// Both sides are cast to long long so size_t-vs-int comparisons don't
// trigger -Wsign-compare.
#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        long long va = (long long)(a);                                        \
        long long vb = (long long)(b);                                        \
        if (!(va == vb)) {                                                    \
            ++testfw::failure_count();                                        \
            std::printf("  CHECK_EQ failed: %s == %s, got %lld vs %lld "      \
                        "(%s:%d)\n",                                          \
                        #a, #b, (long long)va, (long long)vb, __FILE__,       \
                        __LINE__);                                            \
        }                                                                     \
    } while (0)
