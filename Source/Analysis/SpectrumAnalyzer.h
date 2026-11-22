#pragma once

#include "DSP/EQEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <vector>

namespace eyra::analysis
{

/**
    Single-producer / single-consumer FIFO carrying a mono mix of the signal from the audio thread
    to the UI. The audio thread never blocks: if the reader falls behind, new samples are dropped.
*/
class AnalyzerFifo final : public dsp::AudioTap
{
public:
    static constexpr int kCapacity = 1 << 16;

    explicit AnalyzerFifo (int capacity = kCapacity);

    void pushSamples (const double* left, const double* right, int numSamples) noexcept override;

    /** Reader side: copies up to maxSamples into dest, returns the number read. */
    int pull (float* dest, int maxSamples) noexcept;

    int getNumReady() const noexcept { return fifo.getNumReady(); }

    /** Sample rate of the data, written by the audio thread in prepareToPlay. */
    std::atomic<double> sampleRate { 44100.0 };

private:
    juce::AbstractFifo fifo;
    std::vector<float> buffer;

public:
    /** When false, pushSamples() returns immediately (used by the analysis capture). */
    std::atomic<bool> enabled { true };
    int getCapacity() const noexcept { return (int) buffer.size(); }
    void reset() noexcept { fifo.reset(); }
};

struct AnalyzerSettings
{
    enum class Mode { RMS = 0, Peak };

    int fftOrder = 13;            // 2^11 .. 2^15 points
    int speed = 1;                // 0 slow, 1 normal, 2 fast (FFT overlap 2x / 4x / 8x)
    int averaging = 2;            // 0 off, 1 short, 2 medium, 3 long
    Mode mode = Mode::RMS;
    float decayDbPerSecond = 24.0f; // fall rate in peak mode
    float slopeDbPerOctave = 4.5f;  // display tilt pivoting at 1 kHz
    bool freeze = false;

    static constexpr int kMinOrder = 11, kMaxOrder = 15;
};

/**
    Turns the FIFO contents into a smoothed, log-frequency spectrum for display.
    Runs on the message thread (driven by the editor's timer). Not real-time critical.
*/
class SpectrumAnalyzer
{
public:
    static constexpr int kNumPoints = 512;
    static constexpr double kMinDisplayHz = 10.0;
    static constexpr double kMaxDisplayHz = 30000.0;
    static constexpr float kFloorDb = -140.0f;

    explicit SpectrumAnalyzer (AnalyzerFifo& source);

    void setSettings (const AnalyzerSettings& newSettings);
    const AnalyzerSettings& getSettings() const noexcept { return settings; }

    /** Pulls new audio and updates the spectrum. Returns true if the display data changed. */
    bool update (double elapsedSeconds);

    /** Display levels in dBFS (a full-scale sine reads 0 dB), at getPointFrequency(i). */
    const std::vector<float>& getLevels() const noexcept { return levels; }
    static double getPointFrequency (int index) noexcept;

    /** Clears the history (e.g. after unfreezing or a settings change). */
    void clear();

    /** Exposed for tests: raw single-frame magnitude of the most recent FFT, per bin, in dB. */
    const std::vector<float>& getLastFrameBinsDb() const noexcept { return frameBinsDb; }
    int getFftSize() const noexcept { return fftSize; }

private:
    void configure();
    void computeFrame (double frameSeconds);

    AnalyzerFifo& fifo;
    AnalyzerSettings settings;

    int fftSize = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftBuffer, history, frameBinsDb, levels, powerAverage, pullBuffer;
    int historyWrite = 0;
    int samplesSinceFrame = 0;
    int samplesAvailable = 0;
    float windowGain = 1.0f;
    double lastSampleRate = 0.0;
};

} // namespace eyra::analysis
