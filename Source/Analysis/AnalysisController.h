#pragma once

#include "AudioAnalysis.h"
#include "SpectrumAnalyzer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace eyra::analysis
{

/**
    Runs the "listen, then analyse" workflow without ever touching the audio thread's timing:

      audio thread  --(lock-free FIFO, only while listening)-->  worker thread
      worker thread: buffers up to kMaxCaptureSeconds, tracks how much real signal it has heard,
                     stops at kTargetActiveSeconds of signal (or when asked) and runs analyseAudio().
      message thread: starts / stops / cancels, polls state and the latest result.

    The capture buffer is allocated by the worker when listening starts, never on the audio thread.
    After the frequency analysis, the worker runs instrument recognition on the same audio (the model is
    loaded on first use, shared by all instances).
    The standalone app can also analyse an audio file (also on the worker).
*/
class AnalysisController final : private juce::Thread
{
public:
    enum class State
    {
        Idle,
        Listening,
        Analysing,
        Done
    };

    static constexpr double kTargetActiveSeconds = 15.0;
    static constexpr double kMaxCaptureSeconds = 45.0;
    static constexpr double kMaxFileSeconds = 300.0;

    AnalysisController();
    ~AnalysisController() override;

    /** Feed this from the audio thread (pre-EQ signal). Does nothing unless listening. */
    dsp::AudioTap& getTap() noexcept { return fifo; }

    /** From prepareToPlay. A sample-rate change cancels a capture in progress. */
    void prepare (double sampleRate);

    // --- Message thread ------------------------------------------------------------------------
    void startListening();
    void stopAndAnalyse();
    void cancel();
    void clearResult();
    void analyseFile (const juce::File& file);

    State getState() const noexcept { return state.load(); }
    double getCapturedSeconds() const noexcept { return capturedSeconds.load(); }
    double getActiveSeconds() const noexcept { return activeSeconds.load(); }
    bool isSignalPresent() const noexcept { return signalPresent.load(); }
    bool canStopAndAnalyse() const noexcept;

    std::shared_ptr<const AnalysisResult> getResult() const;
    int getResultVersion() const noexcept { return resultVersion.load(); }
    juce::String getSourceDescription() const;
    juce::String getStatusMessage() const;


    /** For tests: blocks until the worker is idle or done, up to timeoutMs. */
    bool waitUntilFinished (int timeoutMs) const;

private:
    void run() override;
    void drainIntoCapture();
    void runAnalysis (const float* data, int numSamples, double sampleRate, const juce::String& source);
    void readAndAnalyseFile (const juce::File& file);
    void setStatus (const juce::String&);

    AnalyzerFifo fifo { 1 << 18 };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<State> state { State::Idle };
    std::atomic<double> capturedSeconds { 0.0 }, activeSeconds { 0.0 };
    std::atomic<bool> signalPresent { false };

    // Requests from the message thread
    std::atomic<bool> startRequested { false }, stopRequested { false }, cancelRequested { false };
    juce::CriticalSection fileLock;
    juce::File pendingFile;

    // Worker-owned capture
    std::vector<float> capture, pullBuffer;
    int captureWrite = 0;
    double captureRate = 0.0;
    double gateEnergy = 0.0;
    int gateCount = 0;
    int silentChunksInARow = 0;


    mutable juce::SpinLock resultLock;
    std::shared_ptr<const AnalysisResult> result;
    juce::String sourceDescription, statusMessage;
    std::atomic<int> resultVersion { 0 };
};

} // namespace eyra::analysis
