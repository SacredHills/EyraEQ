#include "ABComparison.h"

namespace eyra::state
{

namespace
{
    const juce::Identifier abType { "ABComparison" };
    const juce::Identifier slotType { "Slot" };
    const juce::Identifier activeProp { "active" };
    const juce::Identifier indexProp { "index" };
    const juce::Identifier paramType { "P" };
    const juce::Identifier idProp { "id" };
    const juce::Identifier valueProp { "v" };
} // namespace

ABComparison::ABComparison (juce::AudioProcessor& p, EditHistory& h, juce::StringArray excludedIDs)
    : processor (p), history (h), excluded (std::move (excludedIDs))
{
}

void ABComparison::toggle()
{
    const int other = 1 - active;
    slots[(size_t) active] = captureParameters (processor);

    if (slots[(size_t) other].has_value())
    {
        history.beginEdit();
        EditHistory::ScopedSuspend suspend (history);
        applyParameters (processor, *slots[(size_t) other], excluded);
    }
    else
    {
        // First visit to the other slot: it starts as a copy of the current settings.
        slots[(size_t) other] = slots[(size_t) active];
    }

    active = other;
    if (onChange)
        onChange();
}

void ABComparison::copyActiveToInactive()
{
    slots[(size_t) (1 - active)] = captureParameters (processor);
    if (onChange)
        onChange();
}

juce::ValueTree ABComparison::toValueTree() const
{
    juce::ValueTree tree (abType);
    tree.setProperty (activeProp, active, nullptr);

    const auto& params = processor.getParameters();

    for (int s = 0; s < 2; ++s)
    {
        // The active slot is the live parameter state, which the host already saves.
        if (s == active || ! slots[(size_t) s].has_value())
            continue;

        juce::ValueTree slot (slotType);
        slot.setProperty (indexProp, s, nullptr);
        const auto& snap = *slots[(size_t) s];

        for (int i = 0; i < juce::jmin (params.size(), (int) snap.size()); ++i)
        {
            if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]))
            {
                juce::ValueTree pv (paramType);
                pv.setProperty (idProp, withID->paramID, nullptr);
                pv.setProperty (valueProp, snap[(size_t) i], nullptr);
                slot.appendChild (pv, nullptr);
            }
        }
        tree.appendChild (slot, nullptr);
    }

    return tree;
}

void ABComparison::fromValueTree (const juce::ValueTree& tree)
{
    slots = {};
    active = 0;

    if (! tree.hasType (abType))
    {
        if (onChange)
            onChange();
        return;
    }

    active = juce::jlimit (0, 1, (int) tree.getProperty (activeProp, 0));
    const auto& params = processor.getParameters();

    for (const auto& slot : tree)
    {
        if (! slot.hasType (slotType))
            continue;

        const int index = juce::jlimit (0, 1, (int) slot.getProperty (indexProp, 0));
        ParameterSnapshot snap;
        snap.reserve ((size_t) params.size());

        for (auto* p : params)
        {
            float v = p->getDefaultValue();
            if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            {
                auto child = slot.getChildWithProperty (idProp, withID->paramID);
                if (child.isValid())
                    v = (float) child.getProperty (valueProp, v);
            }
            snap.push_back (v);
        }
        slots[(size_t) index] = std::move (snap);
    }

    if (onChange)
        onChange();
}

} // namespace eyra::state
