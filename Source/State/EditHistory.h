#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace eyra::state
{

/** All parameter values of a processor, normalised, in parameter-index order. */
using ParameterSnapshot = std::vector<float>;

/** Captures the normalised value of every parameter. */
ParameterSnapshot captureParameters (juce::AudioProcessor& processor);

/** Applies a snapshot on the message thread, notifying the host. Entries that are NaN are skipped. */
void applyParameters (juce::AudioProcessor& processor, const ParameterSnapshot& snapshot, const juce::StringArray& skipIDs = {});

bool snapshotsEqual (const ParameterSnapshot& a, const ParameterSnapshot& b) noexcept;

/**
    Undo / redo over complete parameter snapshots.

    A snapshot of the state *before* each edit is stored when an edit gesture begins. Edit gestures
    are detected automatically from parameter gestures started on the message thread (sliders,
    buttons, dragging nodes), so every UI control is undoable without extra wiring.
    Programmatic changes (undo itself, preset loading) are wrapped in a ScopedSuspend.
*/
class EditHistory : private juce::AudioProcessorParameter::Listener
{
public:
    explicit EditHistory (juce::AudioProcessor& processor, juce::StringArray excludedIDs = {});
    ~EditHistory() override;

    /** Records the current state as an undo point. Call before a programmatic multi-parameter edit. */
    void beginEdit();

    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    void clear();

    struct ScopedSuspend
    {
        explicit ScopedSuspend (EditHistory& h) : history (h) { ++history.suspendCount; }
        ~ScopedSuspend() { --history.suspendCount; }
        EditHistory& history;
    };

    /** Called whenever the undo / redo availability may have changed. Message thread. */
    std::function<void()> onChange;

private:
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    void notify();

    juce::AudioProcessor& processor;
    juce::StringArray excluded;
    std::vector<ParameterSnapshot> undoStack, redoStack;
    int suspendCount = 0;
    static constexpr size_t kMaxDepth = 200;
};

} // namespace eyra::state
