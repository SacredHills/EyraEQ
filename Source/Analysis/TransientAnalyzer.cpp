// Short-time analysis: 1/24-octave band energies over time, spectral flux, onsets,
// percussiveness and per-band decay rates (used to detect ringing).

#include "AnalysisInternal.h"

#include <algorithm>

namespace eyra::analysis::detail
{

namespace
{
    int nextPowerOfTwo (double v)
    {
        int n = 1;
        while (n < (int) v)
            n <<= 1;
        return n;
    }

    double median (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;
        auto mid = v.begin() + (ptrdiff_t) (v.size() / 2);
        std::nth_element (v.begin(), mid, v.end());
        return *mid;
    }

    /** Least-squares slope of y over x = 0..n-1. */
    double slope (const std::vector<double>& y)
    {
        const double n = (double) y.size();
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t i = 0; i < y.size(); ++i)
        {
            const double x = (double) i;
            sx += x; sy += y[i]; sxx += x * x; sxy += x * y[i];
        }
        const double d = n * sxx - sx * sx;
        return std::abs (d) < 1.0e-12 ? 0.0 : (n * sxy - sx * sy) / d;
    }
} // namespace

void computeShortTimeAnalysis (Context& ctx)
{
    const double sr = ctx.sampleRate;
    ctx.shortSize = nextPowerOfTwo (sr * 0.085);
    ctx.shortHop = ctx.shortSize / 8;

    const double topHz = std::min (20000.0, 0.45 * sr);
    ctx.bandCentres.clear();
    for (double f = 20.0; f <= topHz; f *= std::pow (2.0, 1.0 / 24.0))
        ctx.bandCentres.push_back (f);
    const int numBands = (int) ctx.bandCentres.size();

    PowerSpectrum spectrum (ctx.shortSize);
    const int numBins = spectrum.numBins();
    const double binHz = sr / ctx.shortSize;
    std::vector<float> power ((size_t) numBins);

    // Bin ranges per band; bands narrower than a bin interpolate at their centre.
    struct BandMap { int lo, hi; double pos; };
    std::vector<BandMap> map ((size_t) numBands);
    for (int b = 0; b < numBands; ++b)
    {
        const double c = ctx.bandCentres[(size_t) b];
        const double lo = c * std::pow (2.0, -1.0 / 48.0), hi = c * std::pow (2.0, 1.0 / 48.0);
        map[(size_t) b] = { (int) std::ceil (lo / binHz), std::min (numBins - 1, (int) std::floor (hi / binHz)), c / binHz };
    }

    const int n = (int) ctx.signal.size();
    const int numFrames = n >= ctx.shortSize ? 1 + (n - ctx.shortSize) / ctx.shortHop : 0;

    ctx.shortBandPower.assign ((size_t) numFrames, std::vector<float> ((size_t) numBands, 0.0f));
    ctx.shortEnergyDb.assign ((size_t) numFrames, -200.0f);
    ctx.flux.assign ((size_t) numFrames, 0.0f);
    ctx.shortActive.assign ((size_t) numFrames, false);

    std::vector<float> previousDb ((size_t) numBands, -120.0f);

    for (int t = 0; t < numFrames; ++t)
    {
        const int start = t * ctx.shortHop;
        spectrum.compute (ctx.signal.data() + start, power.data());

        double energy = 0.0;
        for (int k = 1; k < numBins; ++k)
            energy += power[(size_t) k];
        ctx.shortEnergyDb[(size_t) t] = (float) powerToDb (energy);
        ctx.shortActive[(size_t) t] = ctx.isActiveAt (start, ctx.shortSize);

        auto& bands = ctx.shortBandPower[(size_t) t];
        float flux = 0.0f;
        for (int b = 0; b < numBands; ++b)
        {
            const auto& m = map[(size_t) b];
            double p = 0.0;
            if (m.hi >= m.lo)
                for (int k = m.lo; k <= m.hi; ++k)
                    p += power[(size_t) k];
            else
            {
                const int k0 = std::clamp ((int) m.pos, 0, numBins - 2);
                const double frac = m.pos - k0;
                p = power[(size_t) k0] * (1.0 - frac) + power[(size_t) k0 + 1] * frac;
            }
            bands[(size_t) b] = (float) p;

            const float db = (float) std::max (-120.0, powerToDb (p));
            flux += std::max (0.0f, db - previousDb[(size_t) b]);
            previousDb[(size_t) b] = db;
        }
        ctx.flux[(size_t) t] = ctx.shortActive[(size_t) t] ? flux / (float) numBands : 0.0f;
    }
}

void detectOnsets (Context& ctx)
{
    const int numFrames = (int) ctx.flux.size();
    ctx.onsets.clear();
    if (numFrames < 8)
        return;

    std::vector<double> active;
    for (int t = 0; t < numFrames; ++t)
        if (ctx.shortActive[(size_t) t])
            active.push_back (ctx.flux[(size_t) t]);
    std::sort (active.begin(), active.end());
    const double p95 = active.empty() ? 0.0 : active[(size_t) (0.95 * (double) (active.size() - 1))];

    const int halfWindow = 15;
    const int minSpacing = std::max (2, (int) std::lround (0.05 / ctx.shortFrameSeconds()));
    std::vector<double> local;

    for (int t = 2; t < numFrames - 2; ++t)
    {
        const double f = ctx.flux[(size_t) t];
        if (! ctx.shortActive[(size_t) t] || f <= 0.0)
            continue;
        if (f < ctx.flux[(size_t) t - 1] || f < ctx.flux[(size_t) t - 2] || f < ctx.flux[(size_t) t + 1] || f < ctx.flux[(size_t) t + 2])
            continue;

        local.clear();
        for (int j = std::max (0, t - halfWindow); j <= std::min (numFrames - 1, t + halfWindow); ++j)
            local.push_back (ctx.flux[(size_t) j]);
        const double threshold = 1.5 * median (local) + 0.15 * p95 + 0.5;
        if (f < threshold)
            continue;

        // The broadband level must actually rise.
        float before = ctx.shortEnergyDb[(size_t) t - 1];
        for (int j = std::max (0, t - 4); j < t; ++j)
            before = std::min (before, ctx.shortEnergyDb[(size_t) j]);
        const float after = std::max (ctx.shortEnergyDb[(size_t) t], ctx.shortEnergyDb[(size_t) t + 1]);
        if (after - before < 4.0f)
            continue;

        if (! ctx.onsets.empty() && t - ctx.onsets.back() < minSpacing)
        {
            if (f > ctx.flux[(size_t) ctx.onsets.back()])
                ctx.onsets.back() = t;
            continue;
        }
        ctx.onsets.push_back (t);
    }

    // Percussiveness: how far the broadband level falls 200 ms after each onset.
    auto& r = ctx.result;
    r.onsetsPerSecond = ctx.activeSeconds > 0.0 ? (double) ctx.onsets.size() / ctx.activeSeconds : 0.0;
    const int laterFrames = (int) std::lround (0.2 / ctx.shortFrameSeconds());
    std::vector<double> drops;
    for (size_t i = 0; i < ctx.onsets.size(); ++i)
    {
        const int o = ctx.onsets[i];
        const int next = i + 1 < ctx.onsets.size() ? ctx.onsets[i + 1] : numFrames;
        // The analysis window spans 8 hops, so the energy peak can arrive up to ~8 frames after the onset.
        int peakIdx = o;
        for (int t = o; t <= std::min ({ o + 10, next - 1, numFrames - 1 }); ++t)
            if (ctx.shortEnergyDb[(size_t) t] > ctx.shortEnergyDb[(size_t) peakIdx])
                peakIdx = t;
        const int later = peakIdx + laterFrames;
        if (later < next && later < numFrames)
            drops.push_back (ctx.shortEnergyDb[(size_t) peakIdx] - ctx.shortEnergyDb[(size_t) later]);
    }
    r.percussive = ctx.onsets.size() >= 3 && r.onsetsPerSecond >= 0.2 && drops.size() >= 3 && median (drops) >= 6.0;
}

bool measureBandDecay (const Context& ctx, int band, double& bandRate, double& neighbourRate, int& onsetsUsed)
{
    const int numFrames = (int) ctx.shortBandPower.size();
    const int numBands = (int) ctx.bandCentres.size();
    onsetsUsed = 0;
    if (band < 0 || band >= numBands || ctx.onsets.size() < 3)
        return false;

    std::vector<int> neighbours;
    for (int d = 2; d <= 8; ++d)
    {
        if (band - d >= 0) neighbours.push_back (band - d);
        if (band + d < numBands) neighbours.push_back (band + d);
    }
    if (neighbours.size() < 3)
        return false;

    auto bandDb = [&] (int t) { return powerToDb (ctx.shortBandPower[(size_t) t][(size_t) band]); };
    auto neighbourDb = [&] (int t)
    {
        double p = 0.0;
        for (int nb : neighbours)
            p += ctx.shortBandPower[(size_t) t][(size_t) nb];
        return powerToDb (p / (double) neighbours.size());
    };

    const double frameSeconds = ctx.shortFrameSeconds();
    const int minGap = (int) std::lround (0.15 / frameSeconds);
    const int maxLength = (int) std::lround (0.4 / frameSeconds);

    auto decayRate = [&] (auto&& series, int start, int end, double& rate)
    {
        int peak = start;
        for (int t = start; t <= std::min (start + 10, end); ++t)
            if (series (t) > series (peak))
                peak = t;
        const double peakDb = series (peak);
        std::vector<double> y;
        for (int t = peak + 1; t <= end; ++t)
        {
            const double v = series (t);
            if (v < peakDb - 35.0)
                break;
            y.push_back (v);
        }
        if (y.size() < 5)
            return false;
        rate = slope (y) / frameSeconds;
        return true;
    };

    std::vector<double> bandRates, neighbourRates;
    for (size_t i = 0; i < ctx.onsets.size(); ++i)
    {
        const int o = ctx.onsets[i];
        const int next = i + 1 < ctx.onsets.size() ? ctx.onsets[i + 1] : numFrames;
        if (next - o < minGap)
            continue;
        const int end = std::min ({ next - 1, o + maxLength, numFrames - 1 });

        double br = 0.0, nr = 0.0;
        if (decayRate (bandDb, o, end, br) && decayRate (neighbourDb, o, end, nr))
        {
            bandRates.push_back (br);
            neighbourRates.push_back (nr);
        }
    }

    onsetsUsed = (int) bandRates.size();
    if (onsetsUsed < 3)
        return false;

    bandRate = median (bandRates);
    neighbourRate = median (neighbourRates);
    return true;
}

} // namespace eyra::analysis::detail
