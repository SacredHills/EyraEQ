#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    const juce::String category = argc > 1 ? juce::String (argv[1]) : juce::String();
    if (category.isNotEmpty())
        runner.runTestsInCategory (category);
    else
        runner.runAllTests();

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult (i);
        failures += r->failures;
        passes += r->passes;
        std::cout << (r->failures == 0 ? "[PASS] " : "[FAIL] ") << r->unitTestName << " / " << r->subcategoryName
                  << "  (" << r->passes << " checks, " << r->failures << " failures)\n";
        for (const auto& m : r->messages)
            std::cout << "        " << m << "\n";
    }

    std::cout << "\nTotal: " << passes << " checks passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
