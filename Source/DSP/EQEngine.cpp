#include "EQEngine.h"

#include <algorithm>
#include <cmath>

namespace eyra::dsp
{

namespace
{
    enum class Domain
    {
        LeftRight,
        MidSide
    };

    void toMidSide (double* l, double* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double m = 0.5 * (l[i] + r[i]);
            const double s = 0.5 * (l[i] - r[i]);
            l[i] = m;
            r[i] = s;
        }
    }

    void toLeftRight (double* m, double* s, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double l = m[i] + s[i];
            const double r = m[i] - s[i];
            m[i] = l;
            s[i] = r;
        }
    }

    double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
} // namespace

void EQEngine::prepare (double newSampleRate, int maximumBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    maxBlock = std::max (1, maximumBlockSize);
    numChannelsPrepared = std::clamp (numChannels, 1, 2);

    for (int c = 0; c < 2; ++c)
    {
        work[c].assign ((size_t) maxBlock, 0.0);
        dry[c].assign ((size_t) maxBlock, 0.0);
    }

    for (int i = 0; i < kMaxBands; ++i)
    {
        bands[(size_t) i].setTarget (bandParams[(size_t) i]);
        bands[(size_t) i].prepare (sampleRate);
    }

    inputGain.reset (sampleRate, 0.03);
    outputGain.reset (sampleRate, 0.03);
    wetMix.reset (sampleRate, 0.015);
    soloMix.reset (sampleRate, 0.015);

    inputGain.setCurrentAndTargetValue (inputGain.getTargetValue());
    outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());
    wetMix.setCurrentAndTargetValue (wetMix.getTargetValue());
    soloMix.setCurrentAndTargetValue (soloBand >= 0 ? 1.0 : 0.0);

    reset();
}

void EQEngine::reset() noexcept
{
    for (auto& b : bands)
        b.reset();
    auditionState = {};
}

void EQEngine::setBandParams (int bandIndex, const BandParams& params) noexcept
{
    if (bandIndex < 0 || bandIndex >= kMaxBands)
        return;
    bandParams[(size_t) bandIndex] = params;
    bands[(size_t) bandIndex].setTarget (params);
}

void EQEngine::setInputGainDb (double db) noexcept { inputGain.setTargetValue (dbToGain (db)); }
void EQEngine::setOutputGainDb (double db) noexcept { outputGain.setTargetValue (dbToGain (db)); }
void EQEngine::setMasterBypass (bool shouldBypass) noexcept { wetMix.setTargetValue (shouldBypass ? 0.0 : 1.0); }

void EQEngine::setSoloBand (int bandIndex) noexcept
{
    soloBand = (bandIndex >= 0 && bandIndex < kMaxBands) ? bandIndex : -1;
}

void EQEngine::setTaps (AudioTap* preTap, AudioTap* postTap) noexcept
{
    pre = preTap;
    post = postTap;
}

void EQEngine::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int total = buffer.getNumSamples();
    if (maxBlock == 0 || total == 0 || buffer.getNumChannels() == 0)
        return;

    // Hosts occasionally exceed the announced block size: process in chunks rather than allocate.
    for (int start = 0; start < total; start += maxBlock)
        processChunk (buffer, start, std::min (maxBlock, total - start));
}

void EQEngine::processChunk (juce::AudioBuffer<float>& buffer, int start, int n) noexcept
{
    const int numCh = std::min ({ buffer.getNumChannels(), numChannelsPrepared, 2 });
    const bool stereo = numCh == 2;

    double* w[2] = { work[0].data(), stereo ? work[1].data() : nullptr };
    double* d[2] = { dry[0].data(), stereo ? dry[1].data() : nullptr };

    // --- Input -------------------------------------------------------------------------------
    for (int c = 0; c < numCh; ++c)
    {
        const float* src = buffer.getReadPointer (c, start);
        for (int i = 0; i < n; ++i)
            d[c][i] = (double) src[i];
    }

    for (int i = 0; i < n; ++i)
    {
        const double g = inputGain.getNextValue();
        for (int c = 0; c < numCh; ++c)
            w[c][i] = d[c][i] * g;
    }

    if (pre != nullptr)
        pre->pushSamples (w[0], w[1], n);

    // --- Bands ---------------------------------------------------------------------------------
    Domain domain = Domain::LeftRight;

    for (auto& band : bands)
    {
        if (band.isSilent())
            continue;

        const auto placement = band.currentPlacement();

        if (! stereo)
        {
            // A mono signal has no side component; every other placement processes the channel.
            if (placement != StereoPlacement::Side)
                band.process (w[0], nullptr, n);
            continue;
        }

        const bool wantsMS = placement == StereoPlacement::Mid || placement == StereoPlacement::Side;

        if (wantsMS && domain == Domain::LeftRight)
        {
            toMidSide (w[0], w[1], n);
            domain = Domain::MidSide;
        }
        else if (! wantsMS && domain == Domain::MidSide)
        {
            toLeftRight (w[0], w[1], n);
            domain = Domain::LeftRight;
        }

        switch (placement)
        {
            case StereoPlacement::Stereo: band.process (w[0], w[1], n); break;
            case StereoPlacement::Left:   band.process (w[0], nullptr, n); break;
            case StereoPlacement::Right:  band.process (w[1], nullptr, n); break;
            case StereoPlacement::Mid:    band.process (w[0], nullptr, n); break;
            case StereoPlacement::Side:   band.process (w[1], nullptr, n); break;
        }
    }

    if (domain == Domain::MidSide)
        toLeftRight (w[0], w[1], n);

    // --- Solo audition -------------------------------------------------------------------------
    const bool soloValid = soloBand >= 0 && ! bands[(size_t) soloBand].isSilent();
    soloMix.setTargetValue (soloValid ? 1.0 : 0.0);

    if (soloValid && soloBand != activeSoloBand)
    {
        activeSoloBand = soloBand;
        if (soloMix.getCurrentValue() <= 0.0)
            auditionState = {};
    }

    if (soloMix.isSmoothing() || soloMix.getCurrentValue() > 0.0)
    {
        if (activeSoloBand >= 0)
        {
            const auto& b = bands[(size_t) activeSoloBand];
            auditionCoeffs = designAuditionBandPass (b.currentFrequency(), b.currentQ(), sampleRate);
        }

        const auto& k = auditionCoeffs;
        for (int i = 0; i < n; ++i)
        {
            const double m = soloMix.getNextValue();
            for (int c = 0; c < numCh; ++c)
            {
                auto& z = auditionState[(size_t) c];
                const double x = w[c][i];
                const double y = k.b0 * x + z[0];
                z[0] = k.b1 * x - k.a1 * y + z[1];
                z[1] = k.b2 * x - k.a2 * y;
                w[c][i] = x + m * (y - x);
            }
        }
    }
    else
    {
        activeSoloBand = -1;
    }

    // --- Output gain and master bypass ------------------------------------------------------
    for (int i = 0; i < n; ++i)
    {
        const double g = outputGain.getNextValue();
        const double m = wetMix.getNextValue();
        for (int c = 0; c < numCh; ++c)
        {
            const double wet = w[c][i] * g;
            w[c][i] = d[c][i] + m * (wet - d[c][i]);
        }
    }

    if (post != nullptr)
        post->pushSamples (w[0], w[1], n);

    for (int c = 0; c < numCh; ++c)
    {
        float* dst = buffer.getWritePointer (c, start);
        for (int i = 0; i < n; ++i)
            dst[i] = (float) w[c][i];
    }
}

} // namespace eyra::dsp
