#include "EqCore.h"

namespace eyra::dsp
{

EqCore::EqCore (juce::AudioProcessor& o, juce::AudioProcessorValueTreeState& state) : owner (o)
{
    for (int b = 0; b < eqBandCount; ++b)
    {
        const char* what[] { "on", "type", "freq", "gain", "q", "slope", "dyn", "range", "thr", "use", "atk", "rel", "arel" };
        for (int k = 0; k < 13; ++k)
            bandParams[(size_t) b][(size_t) k] = state.getParameter (eqId (b, what[k]));
        for (int k : { 0, 1, 6, 9 })
            bandParams[(size_t) b][(size_t) k]->addListener (this);
    }
    outP = state.getParameter ("out");
    for (auto& d : dynNow)
        d.store (0.0f);
}

EqCore::~EqCore()
{
    cancelPendingUpdate();
    for (auto& bp : bandParams)
        for (int k : { 0, 1, 6, 9 })
            bp[(size_t) k]->removeListener (this);
}

EqAll EqCore::settings() const noexcept
{
    EqAll s;
    for (int b = 0; b < eqBandCount; ++b)
    {
        auto& p = bandParams[(size_t) b];
        auto& x = s.b[(size_t) b];
        x.on = value (p[0]); x.type = value (p[1]); x.freq = value (p[2]); x.gain = value (p[3]); x.q = value (p[4]);
        x.slope = value (p[5]); x.dyn = value (p[6]); x.range = value (p[7]); x.thr = value (p[8]); x.use = value (p[9]);
        x.atk = value (p[10]); x.rel = value (p[11]); x.arel = value (p[12]);
    }
    s.out = value (outP);
    return s;
}

void EqCore::updateLatency()
{
    const bool want = settings().anySpectral();
    spectralOn.store (want, std::memory_order_release);
    owner.setLatencySamples (want ? EqSpectral::size : 0);
}

void EqCore::prepare (double sr)
{
    sampleRate = juce::jmax (8000.0, sr);
    for (auto& v : listenIn)
        v.assign ((size_t) 16384, 0.0f);
    for (auto& c : listenFilters)
        for (auto& f : c)
            f.reset();
    listenDesigned = -2;
    for (auto& c : filters)
        for (auto& f : c)
            for (auto& s : f.sec)
                s.reset();
    for (auto& d : detectors)
        d.reset();
    std::fill (envelopes.begin(), envelopes.end(), 0.0f);
    std::fill (applied.begin(), applied.end(), 0.0f);
    for (auto& d : dynNow)
        d.store (0.0f);
    last.out = -1000.0f; // force a redesign
    spectral.reset();
    spectralWasOn = false;
    outGain.reset (sampleRate, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (outP)));
    updateLatency();
}

void EqCore::process (float* L, float* R, int n) noexcept
{
    const auto s = settings();
    const double sr = sampleRate;
    // listening to a band: keep the input (the band region is taken from what comes in)
    const int listen = listenBand.load (std::memory_order_relaxed);
    const bool listening = juce::isPositiveAndBelow (listen, eqBandCount) && s.b[(size_t) listen].isUsed()
                           && (size_t) n <= listenIn[0].size();
    if (listening)
    {
        std::copy (L, L + n, listenIn[0].begin());
        std::copy (R != nullptr ? R : L, (R != nullptr ? R : L) + n, listenIn[1].begin());
    }
    if (! (s == last))
    {
        for (int b = 0; b < eqBandCount; ++b)
        {
            const auto& band = s.b[(size_t) b];
            active[(size_t) b] = band.isOn() && (! eqHasGain (band.typeI()) || std::abs (band.gain) > 0.01f || band.dynamic());
            for (auto& c : filters) c[(size_t) b].design (band, sr, band.gain + (band.dynamic() ? applied[(size_t) b] : 0.0f));
            if (band.dynamic())
            {
                const int t = band.typeI();
                detectors[(size_t) b].set (t == eqLowShelf ? Biquad::lowPass : t == eqHighShelf ? Biquad::highPass : Biquad::bandPass,
                                           sr, band.freq, t == eqBell ? juce::jmax (0.3f, band.q) : 0.7071f);
            }
            else
            {
                applied[(size_t) b] = 0.0f;
                dynNow[(size_t) b].store (0.0f, std::memory_order_relaxed);
            }
        }
        spectral.setWeights (s, sr);
        last = s;
    }
    float* chans[2] = { L, R };
    const int nch = R != nullptr ? 2 : 1;
    constexpr int sub = 32;
    for (int off = 0; off < n; off += sub)
    {
        const int len = juce::jmin (sub, n - off);
        for (int b = 0; b < eqBandCount; ++b)
        {
            const auto& band = s.b[(size_t) b];
            if (! band.dynamic()) continue;
            // level in the band (before the EQ), RMS-like with the attack / release times
            auto& det = detectors[(size_t) b];
            float e = envelopes[(size_t) b];
            const float atk = std::exp (-1.0f / (float) (juce::jmax (0.0005f, band.atk * 0.001f) * sr));
            const float rel = std::exp (-1.0f / (float) (juce::jmax (0.005f, band.rel * 0.001f) * sr));
            // auto release: a small dip in the level (the ripple inside a sustained note) lets go slowly, so the gain does not
            // pump; a deep drop (the gap between two chugs) lets go quickly, so the next hit is not held down
            const bool autoRel = band.arel > 0.5f;
            const float relSlow = std::exp (-1.0f / (float) (0.25 * sr)), relFast = std::exp (-1.0f / (float) (0.025 * sr));
            for (int i = 0; i < len; ++i)
            {
                const float x = R != nullptr ? 0.5f * (L[off + i] + R[off + i]) : L[off + i];
                const float y = det.process (x);
                const float p2 = y * y * 2.0f;   // a sine's peak level
                float rc = rel;
                if (autoRel)
                {
                    const float drop = juce::jlimit (0.0f, 1.0f, (0.5f - p2 / (e + 1.0e-12f)) * (1.0f / 0.375f));   // 0: within 3 dB of the envelope, 1: 9 dB or more below
                    rc = relSlow + (relFast - relSlow) * drop;
                }
                e = p2 > e ? atk * e + (1.0f - atk) * p2 : rc * e + (1.0f - rc) * p2;
            }
            envelopes[(size_t) b] = e;
            const float delta = eqDynamicDelta (10.0f * std::log10 (e + 1.0e-12f) - band.thr, band.range);
            if (std::abs (delta - applied[(size_t) b]) > 0.05f)
            {
                applied[(size_t) b] = delta;
                for (auto& c : filters) c[(size_t) b].design (band, sr, band.gain + delta);
            }
            dynNow[(size_t) b].store (applied[(size_t) b], std::memory_order_relaxed);
        }
        for (int c = 0; c < nch; ++c)
            for (int b = 0; b < eqBandCount; ++b)
                if (active[(size_t) b]) filters[(size_t) c][(size_t) b].process (chans[c] + off, len);
    }

    if (listening)   // output only the part of the sound the band works on (what it adds or takes away)
    {
        const auto& band = s.b[(size_t) listen];
        const int key = listen * 1000 + band.typeI();
        if (key != listenDesigned || std::abs (band.freq - listenFreq) > 0.01f || std::abs (band.q - listenQ) > 0.001f)
        {
            const int t = band.typeI();
            const auto type = t == eqLowShelf || t == eqHighCut ? Biquad::lowPass       // a high cut takes away the highs:
                            : t == eqHighShelf || t == eqLowCut ? Biquad::highPass      //  hear them (and the lows of a low cut)
                            : Biquad::bandPass;
            const auto ty = t == eqHighCut ? Biquad::highPass : t == eqLowCut ? Biquad::lowPass : type;
            const double q = ty == Biquad::bandPass ? juce::jmax (0.3, (double) band.q) : 0.7071;
            for (auto& c : listenFilters)
                for (auto& f : c) f.set (ty, sr, band.freq, q);
            listenDesigned = key;
            listenFreq = band.freq;
            listenQ = band.q;
        }
        float* outs[2] = { L, R };
        for (int c = 0; c < nch; ++c)
        {
            std::copy (listenIn[(size_t) c].begin(), listenIn[(size_t) c].begin() + n, outs[c]);
            for (auto& f : listenFilters[(size_t) c]) f.process (outs[c], n);
        }
    }

    const bool specOn = spectralOn.load (std::memory_order_acquire);
    if (specOn != spectralWasOn) { spectral.reset(); spectralWasOn = specOn; }
    if (specOn) spectral.process (L, R, n, s, sr);

    outGain.setTargetValue (juce::Decibels::decibelsToGain (s.out));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        L[i] *= g;
        if (R != nullptr) R[i] *= g;
    }
}

} // namespace eyra::dsp
