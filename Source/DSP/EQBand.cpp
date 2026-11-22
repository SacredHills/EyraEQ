#include "EQBand.h"

#include <algorithm>

namespace eyra::dsp
{

namespace
{
    constexpr double kContinuousSmoothingSeconds = 0.04;
    constexpr double kFadeSeconds = 0.012;
} // namespace

void EQBand::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    freqSmoothed.reset (sampleRate, kContinuousSmoothingSeconds);
    qSmoothed.reset (sampleRate, kContinuousSmoothingSeconds);
    gainSmoothed.reset (sampleRate, kContinuousSmoothingSeconds);
    mix.reset (sampleRate, kFadeSeconds);

    current = target;
    freqSmoothed.setCurrentAndTargetValue (std::clamp (target.frequency, kMinFrequency, kMaxFrequency));
    qSmoothed.setCurrentAndTargetValue (std::clamp (target.q, kMinQ, kMaxQ));
    gainSmoothed.setCurrentAndTargetValue (std::clamp (target.gainDb, -kMaxGainDb, kMaxGainDb));
    mix.setCurrentAndTargetValue (activeTarget());

    reset();
}

void EQBand::reset() noexcept
{
    for (auto& channel : state)
        for (auto& s : channel)
            s = {};

    coefficientsValid = false;
    samplesUntilCoeffUpdate = 0;
}

double EQBand::activeTarget() const noexcept
{
    return (target.used && ! target.bypassed) ? 1.0 : 0.0;
}

bool EQBand::discreteConfigDiffers() const noexcept
{
    return target.type != current.type
        || target.slopeIndex != current.slopeIndex
        || target.placement != current.placement;
}

void EQBand::applyPendingDiscreteChange() noexcept
{
    current.type = target.type;
    current.slopeIndex = target.slopeIndex;
    current.placement = target.placement;
    reset();
}

void EQBand::setTarget (const BandParams& p) noexcept
{
    target = p;

    const double f = std::clamp (p.frequency, kMinFrequency, kMaxFrequency);
    const double q = std::clamp (p.q, kMinQ, kMaxQ);
    const double g = std::clamp (p.gainDb, -kMaxGainDb, kMaxGainDb);

    const bool fullyFadedOut = mix.getCurrentValue() <= 0.0 && ! mix.isSmoothing();

    if (fullyFadedOut)
    {
        // Nothing audible from this band: apply everything immediately, starting from clean state.
        if (discreteConfigDiffers())
            applyPendingDiscreteChange();

        freqSmoothed.setCurrentAndTargetValue (f);
        qSmoothed.setCurrentAndTargetValue (q);
        gainSmoothed.setCurrentAndTargetValue (g);

        if (activeTarget() > 0.0)
            reset();

        mix.setTargetValue (activeTarget());
        return;
    }

    freqSmoothed.setTargetValue (f);
    qSmoothed.setTargetValue (q);
    gainSmoothed.setTargetValue (g);

    // A discrete change has to wait until the wet signal has faded out.
    mix.setTargetValue (discreteConfigDiffers() ? 0.0 : activeTarget());
}

bool EQBand::isSilent() const noexcept
{
    return mix.getCurrentValue() <= 0.0 && ! mix.isSmoothing();
}

void EQBand::updateCoefficients() noexcept
{
    FilterSpec spec;
    spec.type = current.type;
    spec.frequency = freqSmoothed.getCurrentValue();
    spec.gainDb = gainSmoothed.getCurrentValue();
    spec.q = qSmoothed.getCurrentValue();
    spec.slopeIndex = current.slopeIndex;
    cascade = design (spec, sampleRate);
    coefficientsValid = true;
}

void EQBand::process (double* ch0, double* ch1, int numSamples) noexcept
{
    int pos = 0;

    while (pos < numSamples)
    {
        if (isSilent())
            return;

        if (! coefficientsValid || samplesUntilCoeffUpdate <= 0)
        {
            updateCoefficients();
            samplesUntilCoeffUpdate = kCoefficientInterval;
        }

        const int chunk = std::min (numSamples - pos, samplesUntilCoeffUpdate);
        const int numSections = cascade.numSections;
        double* channels[2] = { ch0 != nullptr ? ch0 + pos : nullptr, ch1 != nullptr ? ch1 + pos : nullptr };

        const bool mixRamping = mix.isSmoothing();
        const double mixStart = mix.getCurrentValue();
        const double mixEnd = mixRamping ? mix.skip (chunk) : mixStart;
        const double mixStep = (mixEnd - mixStart) / (double) chunk;

        for (int c = 0; c < 2; ++c)
        {
            double* data = channels[c];
            if (data == nullptr)
                continue;

            auto& st = state[(size_t) c];

            for (int i = 0; i < chunk; ++i)
            {
                const double dry = data[i];
                double y = dry;

                for (int s = 0; s < numSections; ++s)
                {
                    const auto& k = cascade.sections[(size_t) s];
                    auto& z = st[(size_t) s];
                    const double x = y;
                    y = k.b0 * x + z.s1;
                    z.s1 = k.b1 * x - k.a1 * y + z.s2;
                    z.s2 = k.b2 * x - k.a2 * y;
                }

                double m = mixStart;
                if (mixRamping)
                {
                    // S-shaped (smoothstep) fade: no slope discontinuity at either end, which keeps
                    // the spectral splatter of a fade far below audibility.
                    const double t = mixStart + mixStep * (double) (i + 1);
                    m = t * t * (3.0 - 2.0 * t);
                }
                data[i] = dry + m * (y - dry);
            }
        }

        freqSmoothed.skip (chunk);
        qSmoothed.skip (chunk);
        gainSmoothed.skip (chunk);

        samplesUntilCoeffUpdate -= chunk;
        pos += chunk;

        // Fade-out finished: perform any pending discrete switch and fade back in.
        if (mix.getCurrentValue() <= 0.0 && ! mix.isSmoothing() && discreteConfigDiffers())
        {
            applyPendingDiscreteChange();
            freqSmoothed.setCurrentAndTargetValue (freqSmoothed.getTargetValue());
            qSmoothed.setCurrentAndTargetValue (qSmoothed.getTargetValue());
            gainSmoothed.setCurrentAndTargetValue (gainSmoothed.getTargetValue());
            mix.setTargetValue (activeTarget());
        }
    }
}

} // namespace eyra::dsp
