// Fundamental-frequency estimation: normalised, taper-corrected autocorrelation per short frame,
// with an octave-error guard (the shortest lag whose peak is close to the best one wins).

#include "AnalysisInternal.h"

#include <algorithm>

namespace eyra::analysis::detail
{

namespace
{
    double percentile (std::vector<double> v, double p)
    {
        if (v.empty())
            return 0.0;
        std::sort (v.begin(), v.end());
        const double pos = p * (double) (v.size() - 1);
        const size_t i = (size_t) pos;
        const double frac = pos - (double) i;
        return i + 1 < v.size() ? v[i] * (1.0 - frac) + v[i + 1] * frac : v[i];
    }
} // namespace

void estimatePitch (Context& ctx)
{
    const double sr = ctx.sampleRate;
    const int numFrames = (int) ctx.shortActive.size();
    ctx.frameF0.assign ((size_t) numFrames, 0.0);
    if (numFrames == 0)
        return;

    PowerSpectrum spectrum (ctx.shortSize);
    std::vector<float> ac ((size_t) ctx.shortSize / 2);

    const int lagMin = std::max (2, (int) std::floor (sr / 1500.0));
    const int lagMax = std::min (ctx.shortSize / 2 - 2, (int) std::ceil (sr / 30.0));

    int analysed = 0;
    std::vector<double> voiced;

    for (int t = 0; t < numFrames; t += 2)
    {
        if (! ctx.shortActive[(size_t) t])
            continue;
        ++analysed;

        if (! spectrum.autocorrelation (ctx.signal.data() + t * ctx.shortHop, ac.data()))
            continue;

        // Candidate peaks must rise clearly out of a preceding dip.
        float runningMin = ac[(size_t) lagMin];
        float best = 0.0f;
        std::vector<int> peaks;
        for (int tau = lagMin + 1; tau < lagMax; ++tau)
        {
            runningMin = std::min (runningMin, ac[(size_t) tau]);
            const float v = ac[(size_t) tau];
            if (v >= ac[(size_t) tau - 1] && v > ac[(size_t) tau + 1] && v >= 0.4f && v - runningMin >= 0.3f)
            {
                peaks.push_back (tau);
                best = std::max (best, v);
            }
        }
        if (peaks.empty())
            continue;

        int chosen = peaks.front();
        for (int tau : peaks)
            if (ac[(size_t) tau] >= 0.88f * best)
            {
                chosen = tau;
                break;
            }

        const double offset = parabolicOffset (ac[(size_t) chosen - 1], ac[(size_t) chosen], ac[(size_t) chosen + 1]);
        const double f0 = sr / ((double) chosen + offset);
        ctx.frameF0[(size_t) t] = f0;
        voiced.push_back (f0);
    }

    auto& p = ctx.result.pitch;
    p.voicedFraction = analysed > 0 ? (double) voiced.size() / (double) analysed : 0.0;
    if (voiced.size() >= 3)
    {
        p.medianHz = percentile (voiced, 0.5);
        p.lowHz = percentile (voiced, 0.1);
        p.highHz = percentile (voiced, 0.9);
        p.spreadCents = 1200.0 * std::log2 (percentile (voiced, 0.75) / std::max (1.0, percentile (voiced, 0.25)));
    }
    p.pitched = p.voicedFraction >= 0.5 && voiced.size() >= 3;
}

} // namespace eyra::analysis::detail
