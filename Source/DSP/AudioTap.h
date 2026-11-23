#pragma once

namespace eyra::dsp
{

/** Receives audio for display and analysis (the spectrum analyser). Implementations must be real-time safe. */
struct AudioTap
{
    virtual ~AudioTap() = default;
    virtual void pushSamples (const double* left, const double* right, int numSamples) noexcept = 0;
};

} // namespace eyra::dsp
