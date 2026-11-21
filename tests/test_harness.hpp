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
    const char* suite;
    const char* name;
    void (*function)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* suite, const char* name, void (*function)()) {
        registry().push_back(Case{suite, name, function});
    }
};

// No arguments runs every case. `--list` prints names. `--suite <name>` runs one file's cases.
// Any other argument is a single test name. A missing name or an empty suite fails the run.
inline int run_all(int argc = 0, char** argv = nullptr) {
    bool list = false;
    std::string suite;
    std::vector<std::string> names;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--list") {
            list = true;
        } else if (argument == "--suite") {
            if (index + 1 >= argc) {
                std::cerr << "missing suite name after --suite\n";
                return 1;
            }
            suite = argv[++index];
        } else if (argument.rfind("--", 0) == 0) {
            std::cerr << "unknown option: " << argument << '\n';
            return 1;
        } else {
            names.push_back(argument);
        }
    }

    if (list) {
        for (const Case& test_case : registry()) {
            std::cout << test_case.suite << ' ' << test_case.name << '\n';
        }
        return 0;
    }

    auto selected = [&](const Case& test_case) {
        if (!suite.empty() && suite != test_case.suite) {
            return false;
        }
        if (names.empty()) {
            return true;
        }
        for (const std::string& name : names) {
            if (name == test_case.name) {
                return true;
            }
        }
        return false;
    };

    if (!suite.empty()) {
        bool found = false;
        for (const Case& test_case : registry()) {
            found = found || suite == test_case.suite;
        }
        if (!found) {
            std::cerr << "unknown suite: " << suite << '\n';
            return 1;
        }
    }
    for (const std::string& name : names) {
        bool found = false;
        for (const Case& test_case : registry()) {
            if (name == test_case.name && (suite.empty() || suite == test_case.suite)) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "unknown test: " << name << '\n';
            return 1;
        }
    }

    int failed = 0;
    int ran = 0;
    for (const Case& test_case : registry()) {
        if (!selected(test_case)) {
            continue;
        }
        ++ran;
        try {
            test_case.function();
            std::cout << "PASSED " << test_case.name << '\n';
        } catch (const std::exception& ex) {
            std::cout << "FAILED " << test_case.name << ": " << ex.what() << '\n';
            ++failed;
        }
    }
    std::cout << (ran - failed) << " passed, " << failed << " failed, " << ran << " total\n";
    return failed;
}

}  // namespace incidentlab_test

#ifndef INCIDENTLAB_TEST_SUITE
#define INCIDENTLAB_TEST_SUITE "unspecified"
#endif

#define INCIDENTLAB_TEST(name)                                                                     \
    static void name();                                                                            \
    static incidentlab_test::Registrar registrar_##name{INCIDENTLAB_TEST_SUITE, #name, &name};     \
    static void name()

#define EXPECT(condition)                                                                          \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            throw incidentlab_test::Failure(std::string(#condition) + " failed at line " +        \
                                            std::to_string(__LINE__));                             \
        }                                                                                          \
    } while (0)
