#include "EditHistory.h"

#include <cmath>

namespace eyra::state
{

namespace
{
    juce::String paramID (juce::AudioProcessorParameter* p)
    {
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            return withID->paramID;
        return {};
    }
} // namespace

ParameterSnapshot captureParameters (juce::AudioProcessor& processor)
{
    ParameterSnapshot s;
    const auto& params = processor.getParameters();
    s.reserve ((size_t) params.size());
    for (auto* p : params)
        s.push_back (p->getValue());
    return s;
}

void applyParameters (juce::AudioProcessor& processor, const ParameterSnapshot& snapshot, const juce::StringArray& skipIDs)
{
    const auto& params = processor.getParameters();
    const int n = juce::jmin (params.size(), (int) snapshot.size());

    for (int i = 0; i < n; ++i)
    {
        auto* p = params[i];
        const float v = snapshot[(size_t) i];
        if (std::isnan (v) || skipIDs.contains (paramID (p)))
            continue;

        if (std::abs (p->getValue() - v) > 1.0e-7f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (v);
            p->endChangeGesture();
        }
    }
}

bool snapshotsEqual (const ParameterSnapshot& a, const ParameterSnapshot& b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs (a[i] - b[i]) > 1.0e-7f)
            return false;
    return true;
}

//==============================================================================
EditHistory::EditHistory (juce::AudioProcessor& p, juce::StringArray excludedIDs)
    : processor (p), excluded (std::move (excludedIDs))
{
    for (auto* param : processor.getParameters())
        param->addListener (this);
}

EditHistory::~EditHistory()
{
    for (auto* param : processor.getParameters())
        param->removeListener (this);
}

void EditHistory::parameterGestureChanged (int parameterIndex, bool gestureIsStarting)
{
    if (! gestureIsStarting || suspendCount > 0)
        return;

    // Only user edits made from the UI count; ignore anything arriving on other threads.
    if (! juce::MessageManager::existsAndIsCurrentThread())
        return;

    if (auto* p = processor.getParameters()[parameterIndex])
        if (excluded.contains (paramID (p)))
            return;

    beginEdit();
}

void EditHistory::beginEdit()
{
    if (suspendCount > 0)
        return;

    auto now = captureParameters (processor);
    if (! undoStack.empty() && snapshotsEqual (undoStack.back(), now))
        return;

    undoStack.push_back (std::move (now));
    if (undoStack.size() > kMaxDepth)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
    notify();
}

bool EditHistory::canUndo() const
{
    // An undo point equal to the current state (a click without a change) is not a real step.
    const auto now = captureParameters (processor);
    for (auto it = undoStack.rbegin(); it != undoStack.rend(); ++it)
        if (! snapshotsEqual (*it, now))
            return true;
    return false;
}

bool EditHistory::canRedo() const
{
    return ! redoStack.empty();
}

void EditHistory::undo()
{
    const auto now = captureParameters (processor);

    while (! undoStack.empty() && snapshotsEqual (undoStack.back(), now))
        undoStack.pop_back();

    if (undoStack.empty())
    {
        notify();
        return;
    }

    auto target = std::move (undoStack.back());
    undoStack.pop_back();
    redoStack.push_back (now);

    {
        ScopedSuspend suspend (*this);
        applyParameters (processor, target, excluded);
    }
    notify();
}

void EditHistory::redo()
{
    if (redoStack.empty())
        return;

    auto target = std::move (redoStack.back());
    redoStack.pop_back();
    undoStack.push_back (captureParameters (processor));

    {
        ScopedSuspend suspend (*this);
        applyParameters (processor, target, excluded);
    }
    notify();
}

void EditHistory::clear()
{
    undoStack.clear();
    redoStack.clear();
    notify();
}

void EditHistory::notify()
{
    if (onChange)
        onChange();
}

} // namespace eyra::state
