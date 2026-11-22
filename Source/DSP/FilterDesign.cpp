#include "FilterDesign.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace eyra::dsp
{

namespace
{
    constexpr double pi = std::numbers::pi;
    constexpr double butterworthQ = 0.7071067811865476;

    double square (double x) noexcept { return x * x; }

    bool isFiniteCoeffs (const BiquadCoeffs& c) noexcept
    {
        return std::isfinite (c.b0) && std::isfinite (c.b1) && std::isfinite (c.b2)
            && std::isfinite (c.a1) && std::isfinite (c.a2);
    }

    // phi terms from Vicanek, "Matched Second Order Digital Filters" (2016):
    // |B(e^jw)|^2 = A0 * phi0 + A1 * phi1 + A2 * phi2
    struct Phi
    {
        double p0, p1, p2;
    };

    Phi phiAt (double omega) noexcept
    {
        const double s = std::sin (omega * 0.5);
        const double p1 = s * s;
        const double p0 = 1.0 - p1;
        return { p0, p1, 4.0 * p0 * p1 };
    }

    BiquadCoeffs designMatchedFirstOrder (const AnalogBiquad& p, double w0) noexcept
    {
        // Pole by impulse invariance, numerator matched in magnitude at DC and Nyquist.
        const double poleS = -p.d0 / p.d1;
        const double z = std::exp (poleS * w0);

        BiquadCoeffs c;
        c.a1 = -z;
        c.a2 = 0.0;
        c.b2 = 0.0;

        const double h0 = std::sqrt (p.magnitudeSquared (0.0));
        const double hpi = std::sqrt (p.magnitudeSquared (pi / w0));
        const double sumB = h0 * (1.0 + c.a1);  // b0 + b1
        const double diffB = hpi * (1.0 - c.a1); // b0 - b1
        c.b0 = 0.5 * (sumB + diffB);
        c.b1 = 0.5 * (sumB - diffB);
        return c;
    }

    /** Inverse of a section (swap poles and zeros). Valid when the section is minimum phase. */
    bool invertSection (const BiquadCoeffs& c, BiquadCoeffs& out) noexcept
    {
        if (std::abs (c.b0) < 1.0e-12)
            return false;
        out.b0 = 1.0 / c.b0;
        out.b1 = c.a1 / c.b0;
        out.b2 = c.a2 / c.b0;
        out.a1 = c.b1 / c.b0;
        out.a2 = c.b2 / c.b0;
        return out.isStable();
    }

    // Converts an order-N Butterworth high/low-pass into analogue sections.
    struct PassSections
    {
        std::array<AnalogBiquad, kMaxSections> sections {};
        int count = 0;
    };

    PassSections makePassSections (bool highPass, int order, double userQ) noexcept
    {
        PassSections out;
        const bool odd = (order % 2) != 0;
        const int numBiquads = order / 2;

        if (odd)
        {
            AnalogBiquad a;
            a.firstOrder = true;
            a.n1 = highPass ? 1.0 : 0.0;
            a.n0 = highPass ? 0.0 : 1.0;
            a.d1 = 1.0;
            a.d0 = 1.0;
            out.sections[(size_t) out.count++] = a;
        }

        for (int k = 1; k <= numBiquads; ++k)
        {
            // Butterworth pole Qs for this order.
            const double angle = odd ? (k * pi / order) : ((2.0 * k - 1.0) * pi / (2.0 * order));
            double q = 1.0 / (2.0 * std::cos (angle));

            // The resonance control scales the highest-Q section (the last one). At the default
            // Q of 0.7071 the cascade is a pure Butterworth response.
            if (k == numBiquads)
                q *= userQ / butterworthQ;

            AnalogBiquad a;
            a.n2 = highPass ? 1.0 : 0.0;
            a.n1 = 0.0;
            a.n0 = highPass ? 0.0 : 1.0;
            a.d2 = 1.0;
            a.d1 = 1.0 / q;
            a.d0 = 1.0;
            out.sections[(size_t) out.count++] = a;
        }

        return out;
    }
} // namespace

//==============================================================================
std::complex<double> BiquadCoeffs::response (double omega) const noexcept
{
    const auto z1 = std::polar (1.0, -omega);
    const auto z2 = z1 * z1;
    return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
}

bool BiquadCoeffs::isStable() const noexcept
{
    return isFiniteCoeffs (*this) && std::abs (a2) < 1.0 && std::abs (a1) < 1.0 + a2;
}

std::complex<double> SectionCascade::response (double omega) const noexcept
{
    std::complex<double> h { 1.0, 0.0 };
    for (int i = 0; i < numSections; ++i)
        h *= sections[(size_t) i].response (omega);
    return h;
}

double SectionCascade::magnitudeDb (double frequencyHz, double sampleRate) const noexcept
{
    const double omega = 2.0 * pi * std::min (frequencyHz, 0.5 * sampleRate) / sampleRate;
    const double mag = std::abs (response (omega));
    return 20.0 * std::log10 (std::max (mag, 1.0e-30));
}

//==============================================================================
double AnalogBiquad::magnitudeSquared (double w) const noexcept
{
    if (firstOrder)
        return (square (n0) + square (n1 * w)) / (square (d0) + square (d1 * w));

    const double w2 = w * w;
    return (square (n0 - n2 * w2) + square (n1 * w)) / (square (d0 - d2 * w2) + square (d1 * w));
}

BiquadCoeffs designBilinear (const AnalogBiquad& p, double w0) noexcept
{
    // Bilinear transform with the band frequency pre-warped: s = (1/K) (1 - z^-1) / (1 + z^-1).
    const double k = std::tan (0.5 * w0);
    BiquadCoeffs c;

    if (p.firstOrder)
    {
        const double a0 = p.d1 + p.d0 * k;
        c.b0 = (p.n1 + p.n0 * k) / a0;
        c.b1 = (p.n0 * k - p.n1) / a0;
        c.a1 = (p.d0 * k - p.d1) / a0;
        return c;
    }

    const double k2 = k * k;
    const double a0 = p.d2 + p.d1 * k + p.d0 * k2;
    c.b0 = (p.n2 + p.n1 * k + p.n0 * k2) / a0;
    c.b1 = (2.0 * p.n0 * k2 - 2.0 * p.n2) / a0;
    c.b2 = (p.n2 - p.n1 * k + p.n0 * k2) / a0;
    c.a1 = (2.0 * p.d0 * k2 - 2.0 * p.d2) / a0;
    c.a2 = (p.d2 - p.d1 * k + p.d0 * k2) / a0;
    return c;
}

BiquadCoeffs designMatched (const AnalogBiquad& p, double w0) noexcept
{
    if (p.firstOrder)
    {
        auto c = designMatchedFirstOrder (p, w0);
        return (c.isStable() ? c : designBilinear (p, w0));
    }

    // --- Poles: impulse invariance (z = e^{s w0}) of the analogue poles. -------------------
    BiquadCoeffs c;
    const double disc = p.d1 * p.d1 - 4.0 * p.d2 * p.d0;

    if (disc < 0.0)
    {
        const double sigma = -p.d1 / (2.0 * p.d2) * w0;
        const double omegaP = std::sqrt (-disc) / (2.0 * p.d2) * w0;

        // Poles whose angle would alias past Nyquist cannot be matched: use the bilinear design.
        if (omegaP > 0.95 * pi)
            return designBilinear (p, w0);

        const double r = std::exp (sigma);
        c.a1 = -2.0 * r * std::cos (omegaP);
        c.a2 = r * r;
    }
    else
    {
        const double sq = std::sqrt (disc);
        const double s1 = (-p.d1 + sq) / (2.0 * p.d2) * w0;
        const double s2 = (-p.d1 - sq) / (2.0 * p.d2) * w0;
        const double z1 = std::exp (s1);
        const double z2 = std::exp (s2);
        c.a1 = -(z1 + z2);
        c.a2 = z1 * z2;
    }

    // --- Zeros: match the analogue magnitude at DC, Nyquist and the band frequency. ----------
    const double denA0 = square (1.0 + c.a1 + c.a2);
    const double denA1 = square (1.0 - c.a1 + c.a2);
    const double denA2 = -4.0 * c.a2;

    const double numA0 = p.magnitudeSquared (0.0) * denA0;
    const double numA1 = p.magnitudeSquared (pi / w0) * denA1;

    const auto phi = phiAt (w0);
    const double denAtW0 = denA0 * phi.p0 + denA1 * phi.p1 + denA2 * phi.p2;
    const double numA2 = (p.magnitudeSquared (1.0) * denAtW0 - numA0 * phi.p0 - numA1 * phi.p1) / phi.p2;

    const double sqA0 = std::sqrt (numA0);
    const double sqA1 = std::sqrt (numA1);
    const double w = 0.5 * (sqA0 + sqA1);
    double d = w * w + numA2;

    if (d < 0.0)
    {
        if (d > -1.0e-9 * std::max (w * w, 1.0e-30))
            d = 0.0; // numerically a double zero on the unit circle (e.g. notch)
        else
            return designBilinear (p, w0);
    }

    c.b0 = 0.5 * (w + std::sqrt (d));
    c.b1 = 0.5 * (sqA0 - sqA1);

    if (c.b0 <= 1.0e-300)
        return designBilinear (p, w0);

    c.b2 = -numA2 / (4.0 * c.b0);

    if (! c.isStable())
        return designBilinear (p, w0);

    return c;
}

//==============================================================================
bool typeUsesGain (FilterType t) noexcept
{
    return t == FilterType::Bell || t == FilterType::LowShelf || t == FilterType::HighShelf || t == FilterType::Tilt;
}

bool typeUsesQ (FilterType t, int slopeIndex) noexcept
{
    if (t == FilterType::Tilt)
        return false;
    if ((t == FilterType::HighPass || t == FilterType::LowPass) && kSlopesDbPerOct[(size_t) std::clamp (slopeIndex, 0, 7)] == 6)
        return false;
    return true;
}

bool typeUsesSlope (FilterType t) noexcept
{
    return t == FilterType::HighPass || t == FilterType::LowPass;
}

double maxDesignFrequency (double sampleRate) noexcept
{
    return 0.49 * sampleRate;
}

SectionCascade design (const FilterSpec& spec, double sampleRate) noexcept
{
    SectionCascade out;
    if (sampleRate <= 0.0)
        return out;

    const double f = std::clamp (spec.frequency, kMinFrequency, std::min (kMaxFrequency, maxDesignFrequency (sampleRate)));
    const double q = std::clamp (spec.q, kMinQ, kMaxQ);
    const double g = std::clamp (spec.gainDb, -kMaxGainDb, kMaxGainDb);
    const double w0 = 2.0 * pi * f / sampleRate;

    auto addSection = [&] (const AnalogBiquad& proto)
    {
        out.sections[(size_t) out.numSections++] = designMatched (proto, w0);
    };

    // Bells and shelves are exactly reciprocal for +g / -g. The boost (lightly damped poles) is matched
    // far more accurately, so a cut is designed as the inverse of the corresponding boost.
    const bool reciprocalType = spec.type == FilterType::Bell || spec.type == FilterType::LowShelf || spec.type == FilterType::HighShelf;
    if (reciprocalType && g < 0.0)
    {
        auto boostSpec = spec;
        boostSpec.gainDb = -g;
        const auto boost = design (boostSpec, sampleRate);
        BiquadCoeffs inverted;
        if (boost.numSections == 1 && invertSection (boost.sections[0], inverted))
        {
            out.sections[0] = inverted;
            out.numSections = 1;
            return out;
        }
        // Not minimum phase (should not happen): fall through to the direct design.
    }

    switch (spec.type)
    {
        case FilterType::Bell:
        {
            const double a = std::pow (10.0, g / 40.0);
            AnalogBiquad p;
            p.n2 = 1.0; p.n1 = a / q;         p.n0 = 1.0;
            p.d2 = 1.0; p.d1 = 1.0 / (a * q); p.d0 = 1.0;
            addSection (p);
            break;
        }
        case FilterType::LowShelf:
        {
            const double a = std::pow (10.0, g / 40.0);
            const double sa = std::sqrt (a);
            AnalogBiquad p;
            p.n2 = a; p.n1 = a * sa / q; p.n0 = a * a;
            p.d2 = a; p.d1 = sa / q;     p.d0 = 1.0;
            addSection (p);
            break;
        }
        case FilterType::HighShelf:
        {
            const double a = std::pow (10.0, g / 40.0);
            const double sa = std::sqrt (a);
            AnalogBiquad p;
            p.n2 = a * a; p.n1 = a * sa / q; p.n0 = a;
            p.d2 = 1.0;   p.d1 = sa / q;     p.d0 = a;
            addSection (p);
            break;
        }
        case FilterType::HighPass:
        case FilterType::LowPass:
        {
            const int slopeIndex = std::clamp (spec.slopeIndex, 0, (int) kSlopesDbPerOct.size() - 1);
            const int order = kSlopesDbPerOct[(size_t) slopeIndex] / 6;
            const auto sections = makePassSections (spec.type == FilterType::HighPass, order, q);
            for (int i = 0; i < sections.count; ++i)
                addSection (sections.sections[(size_t) i]);
            break;
        }
        case FilterType::Notch:
        {
            AnalogBiquad p;
            p.n2 = 1.0; p.n1 = 0.0;     p.n0 = 1.0;
            p.d2 = 1.0; p.d1 = 1.0 / q; p.d0 = 1.0;
            addSection (p);
            break;
        }
        case FilterType::BandPass:
        {
            AnalogBiquad p;
            p.n2 = 0.0; p.n1 = 1.0 / q; p.n0 = 0.0;
            p.d2 = 1.0; p.d1 = 1.0 / q; p.d0 = 1.0;
            addSection (p);
            break;
        }
        case FilterType::Tilt:
        {
            // First-order tilt pivoting at the band frequency: -g/2 dB at DC, +g/2 dB at HF.
            const double t = std::pow (10.0, g / 40.0);
            AnalogBiquad p;
            p.firstOrder = true;
            p.n1 = t;   p.n0 = 1.0;
            p.d1 = 1.0; p.d0 = t;
            addSection (p);
            break;
        }
    }

    return out;
}

BiquadCoeffs designAuditionBandPass (double frequency, double q, double sampleRate) noexcept
{
    FilterSpec spec;
    spec.type = FilterType::BandPass;
    spec.frequency = frequency;
    spec.q = std::clamp (q, 0.5, 12.0);
    return design (spec, sampleRate).sections[0];
}

} // namespace eyra::dsp
