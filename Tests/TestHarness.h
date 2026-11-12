/*
  ==============================================================================

    PRIZM V2 - minimal dependency-free test harness for the offline tests.

  ==============================================================================
*/

#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace prizmtest
{
    struct TestCase
    {
        std::string name;
        std::function<void()> fn;
    };

    inline std::vector<TestCase>& registry()
    {
        static std::vector<TestCase> tests;
        return tests;
    }

    inline int& failures()     { static int f = 0; return f; }
    inline int& checks()       { static int c = 0; return c; }
    inline std::string& info() { static std::string s; return s; }

    struct Registrar
    {
        Registrar (const char* name, std::function<void()> fn) { registry().push_back ({ name, std::move (fn) }); }
    };

    inline void fail (const char* file, int line, const std::string& what)
    {
        ++failures();
        std::cout << "    FAILED " << file << ":" << line << "  " << what;
        if (! info().empty())
            std::cout << "  [" << info() << "]";
        std::cout << "\n";
    }
}

#define PRIZM_CONCAT2(a, b) a##b
#define PRIZM_CONCAT(a, b) PRIZM_CONCAT2 (a, b)

#define TEST_CASE(name) \
    static void PRIZM_CONCAT (prizmTest_, __LINE__)(); \
    static prizmtest::Registrar PRIZM_CONCAT (prizmReg_, __LINE__) (name, PRIZM_CONCAT (prizmTest_, __LINE__)); \
    static void PRIZM_CONCAT (prizmTest_, __LINE__)()

#define INFO(expr) do { std::ostringstream os_; os_ << expr; prizmtest::info() = os_.str(); } while (0)

#define REPORT(expr) do { std::cout << "    - " << expr << "\n"; } while (0)

#define CHECK(cond) do { ++prizmtest::checks(); if (! (cond)) prizmtest::fail (__FILE__, __LINE__, #cond); } while (0)

#define CHECK_EQ(a, b) do { ++prizmtest::checks(); const auto va_ = (a); const auto vb_ = (b); \
    if (! (va_ == vb_)) { std::ostringstream os_; os_ << #a " == " #b "  (" << va_ << " vs " << vb_ << ")"; \
    prizmtest::fail (__FILE__, __LINE__, os_.str()); } } while (0)

#define CHECK_NEAR(a, b, tol) do { ++prizmtest::checks(); const double va_ = (double) (a), vb_ = (double) (b); \
    if (! (std::abs (va_ - vb_) <= (double) (tol))) { std::ostringstream os_; \
    os_ << #a " ~= " #b "  (" << va_ << " vs " << vb_ << ", tol " << (tol) << ")"; \
    prizmtest::fail (__FILE__, __LINE__, os_.str()); } } while (0)
