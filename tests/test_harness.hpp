#pragma once

#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace incidentlab_test {

struct Failure : std::exception {
    explicit Failure(std::string message) : message_(std::move(message)) {}
    const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
};

struct Case {
    const char* name;
    void (*function)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*function)()) { registry().push_back(Case{name, function}); }
};

inline int run_all() {
    int failed = 0;
    for (const Case& test_case : registry()) {
        try {
            test_case.function();
            std::cout << "PASSED " << test_case.name << '\n';
        } catch (const std::exception& ex) {
            std::cout << "FAILED " << test_case.name << ": " << ex.what() << '\n';
            ++failed;
        }
    }
    const int total = static_cast<int>(registry().size());
    std::cout << (total - failed) << " passed, " << failed << " failed, " << total << " total\n";
    return failed;
}

}  // namespace incidentlab_test

#define INCIDENTLAB_TEST(name)                                                                     \
    static void name();                                                                            \
    static incidentlab_test::Registrar registrar_##name{#name, &name};                             \
    static void name()

#define EXPECT(condition)                                                                          \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            throw incidentlab_test::Failure(std::string(#condition) + " failed at line " +        \
                                            std::to_string(__LINE__));                             \
        }                                                                                          \
    } while (0)
