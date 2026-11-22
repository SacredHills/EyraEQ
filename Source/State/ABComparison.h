#pragma once

#include "EditHistory.h"

#include <optional>

namespace eyra::state
{

/**
    Two complete EQ settings ("A" and "B") the user can flip between. The active slot is always the
    live parameter state; the inactive slot is a stored snapshot. Message thread only.
*/
class ABComparison
{
public:
    ABComparison (juce::AudioProcessor& processor, EditHistory& history, juce::StringArray excludedIDs);

    int getActiveSlot() const noexcept { return active; }

    /** Stores the live state in the active slot and recalls the other one. */
    void toggle();

    /** Copies the live state into the inactive slot so both are identical. */
    void copyActiveToInactive();

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);

    std::function<void()> onChange;

private:
    juce::AudioProcessor& processor;
    EditHistory& history;
    juce::StringArray excluded;
    std::array<std::optional<ParameterSnapshot>, 2> slots;
    int active = 0;
};

} // namespace eyra::state
