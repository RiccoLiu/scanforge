#pragma once

// A 120-line stand-in for Catch2 so the repository builds with ZERO external
// dependencies and no network access. Set -DSCANFORGE_USE_CATCH2=ON once you
// have Catch2 v3 installed: the same SF_TEST / SF_CHECK / SF_NEAR macros map
// straight onto TEST_CASE / REQUIRE, so no test source needs to change.

#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#ifdef SCANFORGE_USE_CATCH2
#include <catch2/catch_test_macros.hpp>
#define SF_TEST(name) TEST_CASE(name)
#define SF_CHECK(expr) REQUIRE(expr)
#define SF_NEAR(a, b, eps) REQUIRE(std::abs((a) - (b)) < (eps))
#else

namespace sf_test {

struct TestCase {
    std::string name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        registry().push_back(TestCase{name, fn});
    }
};

inline void check(bool condition, const char* expression,
                  const char* file, int line) {
    if (!condition) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) +
                                 " check failed: " + expression);
    }
}

inline void checkNear(double actual, double expected, double epsilon,
                      const char* expression, const char* file, int line) {
    if (!(std::abs(actual - expected) < epsilon)) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) +
                                 " near failed: " + expression +
                                 " (got " + std::to_string(actual) +
                                 ", expected " + std::to_string(expected) + ")");
    }
}

inline int runAll() {
    // Flush after every insertion. If a test aborts or hangs (e.g. a CRT
    // assertion), the last "[ RUN ]" line tells you exactly which case died.
    std::cout << std::unitbuf;
    int failed = 0;
    for (const auto& test : registry()) {
        std::cout << "[ RUN  ] " << test.name << '\n';
        try {
            test.fn();
            std::cout << "[ PASS ] " << test.name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << " -> " << e.what() << '\n';
        }
    }
    std::cout << registry().size() << " tests, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

} // namespace sf_test

#define SF_CONCAT_INNER(a, b) a##b
#define SF_CONCAT(a, b) SF_CONCAT_INNER(a, b)

#define SF_TEST(name)                                                            \
    static void SF_CONCAT(sf_test_fn_, __LINE__)();                              \
    static ::sf_test::Registrar SF_CONCAT(sf_test_reg_, __LINE__)(              \
        name, &SF_CONCAT(sf_test_fn_, __LINE__));                                \
    static void SF_CONCAT(sf_test_fn_, __LINE__)()

#define SF_CHECK(expr) ::sf_test::check((expr), #expr, __FILE__, __LINE__)
#define SF_NEAR(a, b, eps) \
    ::sf_test::checkNear((a), (b), (eps), #a " ~ " #b, __FILE__, __LINE__)

#endif // SCANFORGE_USE_CATCH2
