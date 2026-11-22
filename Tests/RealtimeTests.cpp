// Verifies that the audio callback performs no heap allocation, by counting calls to the global
// allocation functions while processBlock runs on this thread.

#include "PluginProcessor.h"
#include "TestHelpers.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
thread_local bool countingAllocations = false;
std::atomic<int> allocationCount { 0 };

void* countedAlloc (std::size_t size)
{
    if (countingAllocations)
        allocationCount.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
} // namespace

void* operator new (std::size_t size) { return countedAlloc (size); }
void* operator new[] (std::size_t size) { return countedAlloc (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
class RealtimeTests final : public juce::UnitTest
{
public:
    RealtimeTests() : juce::UnitTest ("Realtime safety", "Plugin") {}

    void runTest() override
    {
        using eyra::params::BandField;
        using eyra::params::bandParamID;

        beginTest ("Allocation counter detects allocations (self-check)");
        {
            allocationCount = 0;
            countingAllocations = true;
            auto probe = std::make_unique<std::vector<int>> (100);
            countingAllocations = false;
            expectGreaterOrEqual (allocationCount.load(), 2);
        }

        beginTest ("processBlock allocates nothing, including during parameter changes");

        EyraEQAudioProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.presets.loadPreset (2);
        p.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        int64_t phase = 0;

        auto* gain = p.apvts.getParameter (bandParamID (1, BandField::Gain));
        auto* type = p.apvts.getParameter (bandParamID (2, BandField::Type));
        auto* used = p.apvts.getParameter (bandParamID (10, BandField::Used));
        auto* place = p.apvts.getParameter (bandParamID (1, BandField::Placement));

        allocationCount = 0;
        for (int blk = 0; blk < 400; ++blk)
        {
            // Parameter changes as a host would deliver them (between blocks).
            gain->setValueNotifyingHost ((float) (blk % 50) / 50.0f);
            if (blk % 37 == 0)
                type->setValueNotifyingHost (type->getValue() > 0.5f ? 0.0f : 1.0f);
            if (blk % 53 == 0)
                used->setValueNotifyingHost (used->getValue() > 0.5f ? 0.0f : 1.0f);
            if (blk % 71 == 0)
                place->setValueNotifyingHost (place->getValue() > 0.5f ? 0.0f : 1.0f);
            p.soloBand = (blk / 100) % 2 == 1 ? 1 : -1;

            eyra::test::fillSine (buf, 220.0, 48000.0, 0.3f, phase);

            countingAllocations = true;
            p.processBlock (buf, midi);
            countingAllocations = false;
        }
        expectEquals (allocationCount.load(), 0);
    }
};

static RealtimeTests realtimeTests;
} // namespace
