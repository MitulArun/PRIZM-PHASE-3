/*
  ==============================================================================

    PRIZM V2 - offline test runner.

  ==============================================================================
*/

#include "TestHarness.h"

int main()
{
    int failedTests = 0;

    for (auto& t : prizmtest::registry())
    {
        const int before = prizmtest::failures();
        prizmtest::info().clear();
        std::cout << "[ RUN  ] " << t.name << "\n";
        t.fn();
        const bool ok = prizmtest::failures() == before;
        failedTests += ok ? 0 : 1;
        std::cout << (ok ? "[  OK  ] " : "[ FAIL ] ") << t.name << "\n";
    }

    std::cout << "\n" << prizmtest::registry().size() - (size_t) failedTests << "/" << prizmtest::registry().size()
              << " test cases passed, " << prizmtest::checks() << " checks, " << prizmtest::failures() << " failures\n";
    return failedTests == 0 ? 0 : 1;
}
