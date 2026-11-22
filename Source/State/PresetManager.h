#pragma once

#include "EditHistory.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace eyra::state
{

/**
    Factory presets (embedded at build time from Resources/FactoryPresets) and user presets
    (stored as .eyrapreset XML files in the user's application-data folder).

    Preset files describe bands explicitly, e.g.
        <EyraEQPreset name="..." version="1" inputGain="0" outputGain="0">
          <Band index="1" type="HighPass" freq="80" gain="0" q="0.707" slope="18" placement="Stereo" bypass="0"/>
        </EyraEQPreset>
    Bands that are not listed are disabled. The same format is used for copy / paste.
*/
class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        bool isFactory = false;
        juce::File file;          // user presets
        juce::String resourceName; // factory presets
    };

    PresetManager (juce::AudioProcessorValueTreeState& state, EditHistory& history);

    static juce::File getUserPresetDirectory();
    static constexpr const char* fileExtension = ".eyrapreset";

    void refresh();
    const std::vector<Entry>& getEntries() const noexcept { return entries; }

    bool loadPreset (int index);
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (int index);
    void loadNext();
    void loadPrevious();

    /** Clears all bands and resets the gains (an "init" state). Undoable. */
    void resetToDefault();

    /** Thread-safe: may be called by the host from any thread (e.g. while saving state). */
    juce::String getCurrentPresetName() const;
    void setCurrentPresetName (const juce::String& name);
    int getCurrentIndex() const;

    /** Serialises the live EQ settings in the preset format. */
    std::unique_ptr<juce::XmlElement> createPresetXml (const juce::String& name) const;

    /** Applies a preset document; returns false if it is not a valid preset. Undoable. */
    bool applyPresetXml (const juce::XmlElement& xml);

    std::function<void()> onChange;

private:
    void notify();

    juce::AudioProcessorValueTreeState& state;
    EditHistory& history;
    std::vector<Entry> entries;
    mutable juce::CriticalSection nameLock;
    juce::String currentName { "Default" };
};

} // namespace eyra::state
