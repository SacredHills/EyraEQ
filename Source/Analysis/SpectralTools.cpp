#include "AnalysisInternal.h"

#include <algorithm>
#include <numeric>

namespace eyra::analysis::detail
{

namespace
{
    int log2Int (int n)
    {
        int order = 0;
        while ((1 << order) < n)
            ++order;
        return order;
    }
} // namespace

PowerSpectrum::PowerSpectrum (int fftSize)
    : n (fftSize), fft (log2Int (fftSize)), fftDouble (log2Int (fftSize) + 1)
{
    window.resize ((size_t) n);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) n, juce::dsp::WindowingFunction<float>::hann, false);
    const double sum = std::accumulate (window.begin(), window.end(), 0.0);
    gain = (float) (2.0 / sum);

    buffer.assign ((size_t) n * 2, 0.0f);
    buffer2.assign ((size_t) n * 4, 0.0f);

    // Autocorrelation of the window itself, to undo its taper in frame autocorrelations.
    std::copy (window.begin(), window.end(), buffer2.begin());
    std::fill (buffer2.begin() + n, buffer2.end(), 0.0f);
    fftDouble.performRealOnlyForwardTransform (buffer2.data());
    for (int i = 0; i < 2 * n; ++i)
    {
        const float re = buffer2[(size_t) (2 * i)], im = buffer2[(size_t) (2 * i + 1)];
        buffer2[(size_t) (2 * i)] = re * re + im * im;
        buffer2[(size_t) (2 * i + 1)] = 0.0f;
    }
    fftDouble.performRealOnlyInverseTransform (buffer2.data());
    windowAutocorr.resize ((size_t) n / 2);
    for (int t = 0; t < n / 2; ++t)
        windowAutocorr[(size_t) t] = buffer2[(size_t) t] / std::max (buffer2[0], 1.0e-30f);
}

void PowerSpectrum::compute (const float* input, float* out)
{
    for (int i = 0; i < n; ++i)
        buffer[(size_t) i] = input[i] * window[(size_t) i];
    std::fill (buffer.begin() + n, buffer.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (buffer.data(), true);
    for (int k = 0; k < numBins(); ++k)
    {
        const float a = buffer[(size_t) k] * gain;
        out[k] = a * a;
    }
}

bool PowerSpectrum::autocorrelation (const float* input, float* out)
{
    double energy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float v = input[i] * window[(size_t) i];
        buffer2[(size_t) i] = v;
        energy += (double) v * v;
    }
    if (energy < 1.0e-12)
        return false;

    std::fill (buffer2.begin() + n, buffer2.end(), 0.0f);
    fftDouble.performRealOnlyForwardTransform (buffer2.data());
    for (int i = 0; i < 2 * n; ++i)
    {
        const float re = buffer2[(size_t) (2 * i)], im = buffer2[(size_t) (2 * i + 1)];
        buffer2[(size_t) (2 * i)] = re * re + im * im;
        buffer2[(size_t) (2 * i + 1)] = 0.0f;
    }
    fftDouble.performRealOnlyInverseTransform (buffer2.data());

    const float r0 = std::max (buffer2[0], 1.0e-30f);
    for (int t = 0; t < n / 2; ++t)
    {
        const float w = std::max (windowAutocorr[(size_t) t], 0.05f);
        out[t] = (buffer2[(size_t) t] / r0) / w;
    }
    return true;
}

//==============================================================================
std::vector<double> smoothFractionalOctave (const std::vector<double>& power, double binHz, double widthOctaves, int minHalfWidthBins)
{
    const int numBins = (int) power.size();
    std::vector<double> prefix ((size_t) numBins + 1, 0.0);
    for (int k = 0; k < numBins; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + power[(size_t) k];

    const double half = std::pow (2.0, widthOctaves * 0.5);
    std::vector<double> out ((size_t) numBins, 0.0);
    for (int k = 0; k < numBins; ++k)
    {
        const double f = k * binHz;
        int lo = (int) std::floor (f / half / binHz);
        int hi = (int) std::ceil (f * half / binHz);
        lo = std::min (lo, k - minHalfWidthBins);
        hi = std::max (hi, k + minHalfWidthBins);
        lo = std::clamp (lo, 0, numBins - 1);
        hi = std::clamp (hi, 0, numBins - 1);
        out[(size_t) k] = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (double) (hi - lo + 1);
    }
    return out;
}

double medianDbAround (const std::vector<double>& db, int bin, double binHz, double widthOctaves, int minHalfWidthBins, std::vector<double>& scratch)
{
    const int numBins = (int) db.size();
    const double half = std::pow (2.0, widthOctaves * 0.5);
    const double f = bin * binHz;
    int lo = std::min ((int) std::floor (f / half / binHz), bin - minHalfWidthBins);
    int hi = std::max ((int) std::ceil (f * half / binHz), bin + minHalfWidthBins);
    lo = std::clamp (lo, 1, numBins - 1);
    hi = std::clamp (hi, 1, numBins - 1);

    scratch.assign (db.begin() + lo, db.begin() + hi + 1);
    auto mid = scratch.begin() + (ptrdiff_t) (scratch.size() / 2);
    std::nth_element (scratch.begin(), mid, scratch.end());
    return *mid;
}

bool Context::isActiveAt (int sample, int length, double minFraction) const
{
    if (gateBlock <= 0)
        return false;
    const int b0 = std::max (0, sample / gateBlock);
    const int b1 = std::min ((int) activeBlock.size() - 1, (sample + length - 1) / gateBlock);
    int active = 0, total = 0;
    for (int b = b0; b <= b1; ++b, ++total)
        active += activeBlock[(size_t) b] ? 1 : 0;
    return total > 0 && active > 0 && active >= minFraction * total;
}

int Context::bandIndexFor (double hz) const
{
    if (bandCentres.empty())
        return -1;
    const double pos = std::log2 (hz / bandCentres.front()) * 24.0;
    return std::clamp ((int) std::lround (pos), 0, (int) bandCentres.size() - 1);
}

} // namespace eyra::analysis::detail
