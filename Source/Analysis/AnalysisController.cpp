#include "AnalysisController.h"

namespace eyra::analysis
{

AnalysisController::AnalysisController() : juce::Thread ("EyraEQ analysis")
{
    fifo.enabled = false;
    pullBuffer.resize ((size_t) fifo.getCapacity());
    setStatus ("Play the track, then press Analyze.");
    startThread (juce::Thread::Priority::low);
}

AnalysisController::~AnalysisController()
{
    fifo.enabled = false;
    stopThread (4000);
}

void AnalysisController::prepare (double newSampleRate)
{
    const double old = sampleRate.exchange (newSampleRate);
    if (! juce::exactlyEqual (old, newSampleRate) && state.load() == State::Listening)
    {
        cancelRequested = true;
        notify();
    }
}

void AnalysisController::startListening()
{
    if (state.load() == State::Listening || state.load() == State::Analysing)
        return;
    startRequested = true;
    notify();
}

bool AnalysisController::canStopAndAnalyse() const noexcept
{
    return state.load() == State::Listening && activeSeconds.load() >= AnalysisSettings().minActiveSeconds;
}

void AnalysisController::stopAndAnalyse()
{
    if (state.load() != State::Listening)
        return;
    stopRequested = true;
    notify();
}

void AnalysisController::cancel()
{
    cancelRequested = true;
    notify();
}

void AnalysisController::clearResult()
{
    if (state.load() == State::Listening || state.load() == State::Analysing)
        return;
    {
        const juce::SpinLock::ScopedLockType lock (resultLock);
        result.reset();
        sourceDescription.clear();
    }
    ++resultVersion;
    state = State::Idle;
    setStatus ("Play the track, then press Analyze.");
}

void AnalysisController::analyseFile (const juce::File& file)
{
    if (state.load() == State::Listening || state.load() == State::Analysing)
        return;
    {
        const juce::ScopedLock lock (fileLock);
        pendingFile = file;
    }
    state = State::Analysing;
    notify();
}

std::shared_ptr<const AnalysisResult> AnalysisController::getResult() const
{
    const juce::SpinLock::ScopedLockType lock (resultLock);
    return result;
}

juce::String AnalysisController::getSourceDescription() const
{
    const juce::SpinLock::ScopedLockType lock (resultLock);
    return sourceDescription;
}

juce::String AnalysisController::getStatusMessage() const
{
    const juce::SpinLock::ScopedLockType lock (resultLock);
    return statusMessage;
}

void AnalysisController::setStatus (const juce::String& s)
{
    const juce::SpinLock::ScopedLockType lock (resultLock);
    statusMessage = s;
}

bool AnalysisController::waitUntilFinished (int timeoutMs) const
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        const auto s = state.load();
        if (s == State::Done || s == State::Idle)
            return true;
        juce::Thread::sleep (5);
    }
    return false;
}

//==============================================================================
void AnalysisController::run()
{
    while (! threadShouldExit())
    {
        wait (10);

        if (cancelRequested.exchange (false))
        {
            fifo.enabled = false;
            capture.clear();
            capture.shrink_to_fit();
            if (state.load() == State::Listening)
            {
                state = getResult() != nullptr ? State::Done : State::Idle;
                setStatus ("Listening cancelled.");
            }
        }

        if (startRequested.exchange (false))
        {
            // Allocate here, on the worker, never on the audio thread.
            captureRate = sampleRate.load();
            capture.assign ((size_t) (captureRate * kMaxCaptureSeconds), 0.0f);
            captureWrite = 0;
            gateEnergy = 0.0;
            gateCount = 0;
            silentChunksInARow = 0;
            capturedSeconds = 0.0;
            activeSeconds = 0.0;
            signalPresent = false;

            // Discard anything left in the FIFO, then open it.
            while (fifo.pull (pullBuffer.data(), (int) pullBuffer.size()) > 0) {}
            fifo.enabled = true;
            state = State::Listening;
            setStatus ("Listening...");
        }

        if (state.load() == State::Listening)
        {
            drainIntoCapture();

            const bool full = captureWrite >= (int) capture.size();
            const bool enough = activeSeconds.load() >= kTargetActiveSeconds;
            const bool stop = stopRequested.exchange (false);

            if (full || enough || stop)
            {
                fifo.enabled = false;
                drainIntoCapture();
                state = State::Analysing;
                setStatus ("Analysing...");
                runAnalysis (capture.data(), captureWrite, captureRate,
                             "Playback, " + juce::String (captureWrite / captureRate, 1) + " s captured");
                capture.clear();
                capture.shrink_to_fit();
            }
        }

        juce::File file;
        {
            const juce::ScopedLock lock (fileLock);
            std::swap (file, pendingFile);
        }
        if (file != juce::File())
            readAndAnalyseFile (file);
    }
}

void AnalysisController::drainIntoCapture()
{
    const int chunk = std::max (64, (int) (captureRate * 0.02));

    for (;;)
    {
        const int n = fifo.pull (pullBuffer.data(), (int) pullBuffer.size());
        if (n <= 0)
            break;

        const int room = (int) capture.size() - captureWrite;
        int copied = 0;

        // Copy while tracking how much real signal has been heard (20 ms chunks above -60 dBFS);
        // stop exactly when the target amount of signal is reached.
        while (copied < n && copied < room && activeSeconds.load() < kTargetActiveSeconds)
        {
            const double v = pullBuffer[(size_t) copied];
            capture[(size_t) (captureWrite + copied)] = (float) v;
            ++copied;
            gateEnergy += v * v;
            if (++gateCount == chunk)
            {
                if (gateEnergy / chunk > 1.0e-6)
                {
                    activeSeconds = activeSeconds.load() + chunk / captureRate;
                    silentChunksInARow = 0;
                }
                else
                    ++silentChunksInARow;
                signalPresent = silentChunksInARow < 25; // heard something in the last ~0.5 s
                gateEnergy = 0.0;
                gateCount = 0;
            }
        }

        captureWrite += copied;
        capturedSeconds = captureWrite / captureRate;
        if (copied < n)
            break; // buffer full or target reached; the rest is discarded
    }
}

void AnalysisController::runAnalysis (const float* data, int numSamples, double rate, const juce::String& source)
{
    auto r = std::make_shared<AnalysisResult> (analyseAudio (data, numSamples, rate));

    {
        const juce::SpinLock::ScopedLockType lock (resultLock);
        result = std::move (r);
        sourceDescription = source;
        statusMessage = result->message;
    }
    ++resultVersion;
    state = State::Done;
}

void AnalysisController::readAndAnalyseFile (const juce::File& file)
{
    state = State::Analysing;
    setStatus ("Reading " + file.getFileName() + "...");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
    {
        state = getResult() != nullptr ? State::Done : State::Idle;
        setStatus ("Could not read " + file.getFileName() + " (unsupported or damaged file).");
        return;
    }

    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * kMaxFileSeconds));
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> buffer (channels, length);
    reader->read (&buffer, 0, length, 0, true, channels > 1);

    std::vector<float> mono ((size_t) length);
    for (int i = 0; i < length; ++i)
    {
        float v = 0.0f;
        for (int c = 0; c < channels; ++c)
            v += buffer.getSample (c, i);
        mono[(size_t) i] = v / (float) channels;
    }

    setStatus ("Analysing " + file.getFileName() + "...");
    juce::String source = file.getFileName();
    if (reader->lengthInSamples > length)
        source << " (first " << juce::String ((int) kMaxFileSeconds / 60) << " min)";
    runAnalysis (mono.data(), length, reader->sampleRate, source);
}

} // namespace eyra::analysis
