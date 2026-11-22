// Broad spectral balance: DC offset, sub-sonic rumble, regions that stand out from their own spectral
// context ("buildup": mud, boxiness, harshness ...) and overall tilt.
//
// A region is reported because it exceeds the surrounding spectrum of *this* signal, never because of
// where it lies. The frequency range only decides the name. Regions inside the source's own
// fundamental range are not reported (e.g. a tom's 150-250 Hz body).

#include "AnalysisInternal.h"

#include <algorithm>

namespace eyra::analysis::detail
{

namespace
{
    juce::String buildupName (double hz)
    {
        if (hz < 100.0)  return "Low-end buildup";
        if (hz < 300.0)  return "Low-mid buildup (mud)";
        if (hz < 800.0)  return "Boxiness";
        if (hz < 2000.0) return "Honky midrange";
        if (hz < 6000.0) return "Harshness";
        return "Excess high frequencies";
    }

    double baseConfidence (const Context& ctx)
    {
        const auto& s = ctx.settings;
        return juce::jlimit (0.3, 1.0, 0.3 + 0.7 * (ctx.activeSeconds - s.minActiveSeconds) / (s.fullConfidenceSeconds - s.minActiveSeconds));
    }
} // namespace

void analyseTonalBalance (Context& ctx)
{
    auto& r = ctx.result;
    const double binHz = ctx.longBinHz;
    const auto& P = ctx.ltasPower;
    const int maxBin = ctx.maxAnalysisBin;
    const double conf = baseConfidence (ctx);

    // --- DC offset -------------------------------------------------------------------------
    // Only offsets large enough to matter: above -46 dBFS and within 30 dB of the signal level.
    if (std::abs (ctx.dcOffset) >= 0.005 && ctx.rms > 0.0 && 20.0 * std::log10 (std::abs (ctx.dcOffset) / ctx.rms) >= -30.0)
    {
        Finding f;
        f.type = FindingType::DCOffset;
        f.title = "DC offset";
        f.detail = "The signal is offset by " + juce::String (20.0 * std::log10 (std::abs (ctx.dcOffset)), 1)
                 + " dBFS. This wastes headroom and can cause clicks at edits; a gentle high-pass removes it.";
        f.frequency = 0.0;
        f.severity = juce::jlimit (0.2, 1.0, (20.0 * std::log10 (std::abs (ctx.dcOffset) / ctx.rms) + 30.0) / 25.0);
        f.confidence = 0.95;
        r.findings.push_back (f);
    }

    // --- Third-octave band levels ------------------------------------------------------------
    // Measured on the LTAS with non-harmonic narrow peaks replaced by their local baseline, so a
    // resonance is reported once (as a resonance) and does not also inflate a broad region.
    std::vector<double> broad (P);
    for (const auto& c : ctx.resonanceCandidates)
    {
        if (c.harmonicNumber > 0)
            continue;
        const int lo = std::max (1, std::min ((int) std::floor (c.frequency * std::pow (2.0, -1.0 / 24.0) / binHz), c.bin - 3));
        const int hi = std::min (maxBin, std::max ((int) std::ceil (c.frequency * std::pow (2.0, 1.0 / 24.0) / binHz), c.bin + 3));
        for (int k = lo; k <= hi; ++k)
            broad[(size_t) k] = std::min (broad[(size_t) k], std::pow (10.0, ctx.baselineDb[(size_t) k] / 10.0));
    }

    // For a steady pitched source, the spectrum is a comb of harmonics; narrow low bands would alternate
    // between containing a harmonic and containing none. There, band levels follow the harmonic envelope
    // instead: each harmonic's power spread over its harmonic spacing, interpolated (in dB) between harmonics.
    const bool steadyPitch = r.pitch.pitched && r.pitch.spreadCents <= 100.0 && r.pitch.medianHz > 0.0;
    std::vector<std::pair<double, double>> envelope; // (frequency, power density per Hz)
    if (steadyPitch)
    {
        const double f0 = r.pitch.medianHz;
        for (int h = 1; h * f0 < maxBin * binHz && h <= 200; ++h)
        {
            const double fh = h * f0;
            const int lo = std::max (1, (int) std::floor (fh * 0.97 / binHz));
            const int hi = std::min (maxBin - 2, (int) std::ceil (fh * 1.03 / binHz));
            int peak = lo;
            for (int k = lo; k <= hi; ++k)
                if (P[(size_t) k] > P[(size_t) peak])
                    peak = k;
            double power = 0.0;
            for (int k = std::max (1, peak - 2); k <= peak + 2; ++k)
                power += broad[(size_t) k];
            envelope.emplace_back (fh, power / f0);
        }
    }

    auto envelopeDensity = [&envelope] (double f)
    {
        if (f <= envelope.front().first)
            return envelope.front().second;
        for (size_t i = 1; i < envelope.size(); ++i)
            if (f <= envelope[i].first)
            {
                const double t = (f - envelope[i - 1].first) / (envelope[i].first - envelope[i - 1].first);
                return std::pow (10.0, (1.0 - t) * std::log10 (envelope[i - 1].second) + t * std::log10 (envelope[i].second));
            }
        return envelope.back().second;
    };

    ctx.thirdOctaveCentres.clear();
    ctx.thirdOctaveDb.clear();
    const double topHz = std::min (16000.0, 0.42 * ctx.sampleRate);
    for (int i = -16; ; ++i)
    {
        const double c = 1000.0 * std::pow (2.0, i / 3.0);
        if (c > topHz)
            break;
        const double nominalWidth = c * (std::pow (2.0, 1.0 / 6.0) - std::pow (2.0, -1.0 / 6.0));
        double p = 0.0;

        if (envelope.size() >= 3 && c >= 0.75 * r.pitch.medianHz)
        {
            // Mean envelope density across the band.
            constexpr int steps = 8;
            for (int sIdx = 0; sIdx < steps; ++sIdx)
                p += envelopeDensity (c * std::pow (2.0, (sIdx + 0.5) / steps / 3.0 - 1.0 / 6.0));
            p = p / steps * nominalWidth;
        }
        else
        {
            const int lo = std::max (1, (int) std::ceil (c * std::pow (2.0, -1.0 / 6.0) / binHz));
            const int hi = std::min (maxBin, (int) std::floor (c * std::pow (2.0, 1.0 / 6.0) / binHz));
            if (hi >= lo)
                for (int k = lo; k <= hi; ++k)
                    p += broad[(size_t) k];
            else
                p = broad[(size_t) std::clamp ((int) std::lround (c / binHz), 1, maxBin)] * (nominalWidth / binHz);
        }
        ctx.thirdOctaveCentres.push_back (c);
        ctx.thirdOctaveDb.push_back (powerToDb (p));
    }

    const auto& B = ctx.thirdOctaveDb;
    const auto& C = ctx.thirdOctaveCentres;
    const int numBands = (int) B.size();
    if (numBands < 6)
        return;

    const double maxB = *std::max_element (B.begin(), B.end());
    ctx.contentLowBand = 0;
    while (ctx.contentLowBand < numBands - 1 && B[(size_t) ctx.contentLowBand] < maxB - 40.0)
        ++ctx.contentLowBand;
    ctx.contentHighBand = numBands - 1;
    while (ctx.contentHighBand > 0 && B[(size_t) ctx.contentHighBand] < maxB - 40.0)
        --ctx.contentHighBand;
    const int iLo = ctx.contentLowBand, iHi = ctx.contentHighBand;
    r.lowestContentHz = C[(size_t) iLo];
    r.highestContentHz = C[(size_t) iHi];

    // --- Rumble --------------------------------------------------------------------------------
    // Infrasonic energy that is not simply the tail of the musical low end: the octave below the
    // cutoff must be about as strong as the octave above it, and carry a real share of the energy.
    {
        double cutoff = 25.0;
        if (r.pitch.pitched && r.pitch.lowHz > 0.0 && r.pitch.lowHz < 36.0)
            cutoff = std::max (12.0, 0.7 * r.pitch.lowHz);

        double sub = 0.0, octaveBelow = 0.0, octaveAbove = 0.0, total = 0.0;
        for (int k = 1; k <= maxBin; ++k)
        {
            const double f = k * binHz;
            total += P[(size_t) k];
            if (f >= 2.0 && f < cutoff)
                sub += P[(size_t) k];
            if (f >= cutoff * 0.5 && f < cutoff)
                octaveBelow += P[(size_t) k];
            else if (f >= cutoff && f < cutoff * 2.0)
                octaveAbove += P[(size_t) k];
        }

        if (total > 0.0 && sub > 0.0)
        {
            const double shareDb = powerToDb (sub / total);
            const bool notJustATail = octaveBelow >= 0.5 * octaveAbove;
            if (shareDb >= -20.0 && notJustATail)
            {
                Finding f;
                f.type = FindingType::Rumble;
                f.title = "Sub-sonic rumble";
                f.frequency = cutoff * 0.5;
                f.lowFrequency = 10.0;
                f.highFrequency = cutoff;
                f.excessDb = shareDb;
                f.detail = juce::String (100.0 * sub / total, 1) + "% of the energy lies below " + juce::String (juce::roundToInt (cutoff))
                         + " Hz, under the musical content. This is typically handling noise, wind or traffic.";
                f.severity = juce::jlimit (0.2, 1.0, (shareDb + 20.0) / 15.0);
                f.confidence = conf * 0.9;
                r.findings.push_back (f);
            }
        }
    }

    // --- Buildup: regions above their own spectral context -----------------------------------
    std::vector<double> excess ((size_t) numBands, 0.0);
    std::vector<bool> valid ((size_t) numBands, false);
    for (int i = iLo; i <= iHi; ++i)
    {
        double sum = 0.0;
        int count = 0;
        for (int d = 2; d <= 4; ++d)
        {
            if (i - d >= iLo) { sum += B[(size_t) (i - d)]; ++count; }
            if (i + d <= iHi) { sum += B[(size_t) (i + d)]; ++count; }
        }
        if (count >= 2)
        {
            excess[(size_t) i] = B[(size_t) i] - sum / count;
            valid[(size_t) i] = true;
        }
    }

    const double threshold = ctx.settings.buildupThresholdDb;
    for (int i = iLo; i <= iHi;)
    {
        if (! valid[(size_t) i] || excess[(size_t) i] < threshold)
        {
            ++i;
            continue;
        }
        int j = i;
        while (j + 1 <= iHi && valid[(size_t) j + 1] && excess[(size_t) j + 1] >= threshold)
            ++j;

        if (j > i) // at least two adjacent third-octave bands (2/3 octave)
        {
            int peak = i;
            for (int k = i; k <= j; ++k)
                if (excess[(size_t) k] > excess[(size_t) peak])
                    peak = k;

            const double lowHz = C[(size_t) i] * std::pow (2.0, -1.0 / 6.0);
            const double highHz = C[(size_t) j] * std::pow (2.0, 1.0 / 6.0);

            // The source's own fundamental range is natural, not a problem.
            const bool inFundamentalRange = r.pitch.pitched
                                         && highHz > r.pitch.lowHz * std::pow (2.0, -1.0 / 6.0)
                                         && lowHz < r.pitch.highHz * std::pow (2.0, 1.0 / 6.0);

            // At the edges of the source's range the comparison is one-sided; demand more evidence there.
            const bool atEdge = i <= iLo + 1 || j >= iHi - 1;
            if (! inFundamentalRange && (! atEdge || excess[(size_t) peak] >= 6.0))
            {
                Finding f;
                f.type = FindingType::Buildup;
                f.title = buildupName (C[(size_t) peak]);
                f.frequency = C[(size_t) peak];
                f.lowFrequency = lowHz;
                f.highFrequency = highHz;
                f.excessDb = excess[(size_t) peak];
                f.detail = "Around " + describeFrequency (f.frequency) + " (" + describeFrequency (lowHz) + " to " + describeFrequency (highHz)
                         + "), the level is " + juce::String (f.excessDb, 1) + " dB above the surrounding spectrum.";
                if (atEdge)
                    f.detail << " It sits at the edge of the source's range, so this may be its natural shape.";
                f.severity = juce::jlimit (0.0, 1.0, (f.excessDb - 2.0) / 6.0);
                f.confidence = conf * 0.85 * (atEdge ? 0.6 : 1.0);
                r.findings.push_back (f);
            }
        }
        i = j + 1;
    }

    // --- Tilt and centroid (informational) -----------------------------------------------------
    std::vector<std::pair<double, double>> pts;
    for (int i = iLo; i <= iHi; ++i)
        if (C[(size_t) i] >= 100.0 && C[(size_t) i] <= 10000.0)
            pts.emplace_back (std::log2 (C[(size_t) i]), B[(size_t) i]);

    auto fit = [] (const std::vector<std::pair<double, double>>& p, double& a, double& b)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (auto [x, y] : p) { sx += x; sy += y; sxx += x * x; sxy += x * y; }
        const double n = (double) p.size(), d = n * sxx - sx * sx;
        if (p.size() < 3 || std::abs (d) < 1.0e-12)
            return false;
        b = (n * sxy - sx * sy) / d;
        a = (sy - b * sx) / n;
        return true;
    };

    double a = 0.0, b = 0.0;
    if (fit (pts, a, b))
    {
        std::vector<std::pair<double, double>> inliers;
        for (auto [x, y] : pts)
            if (std::abs (y - (a + b * x)) <= 6.0)
                inliers.emplace_back (x, y);
        fit (inliers, a, b);
        r.spectralTiltDbPerOct = b;
    }

    double num = 0.0, den = 0.0;
    for (int k = 1; k <= maxBin; ++k)
    {
        num += k * binHz * P[(size_t) k];
        den += P[(size_t) k];
    }
    r.spectralCentroidHz = den > 0.0 ? num / den : 0.0;
}

} // namespace eyra::analysis::detail
