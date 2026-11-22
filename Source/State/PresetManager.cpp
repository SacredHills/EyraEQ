#include "PresetManager.h"

#include "Parameters/Parameters.h"

#include <EyraBinaryData.h>

namespace eyra::state
{

namespace
{
    const char* const typeTokens[] = { "Bell", "LowShelf", "HighShelf", "HighPass", "LowPass", "Notch", "BandPass", "Tilt" };
    const char* const placementTokens[] = { "Stereo", "Left", "Right", "Mid", "Side" };

    int tokenIndex (const juce::String& token, const char* const* tokens, int count, int fallback)
    {
        for (int i = 0; i < count; ++i)
            if (token.equalsIgnoreCase (tokens[i]))
                return i;
        return fallback;
    }

    int slopeIndexFromDb (int db)
    {
        for (size_t i = 0; i < dsp::kSlopesDbPerOct.size(); ++i)
            if (dsp::kSlopesDbPerOct[i] == db)
                return (int) i;
        return dsp::kDefaultSlopeIndex;
    }

    float realValue (juce::AudioProcessorValueTreeState& s, const juce::String& id)
    {
        auto* p = s.getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    float defaultRealValue (juce::AudioProcessorValueTreeState& s, const juce::String& id)
    {
        auto* p = s.getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getDefaultValue()) : 0.0f;
    }
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, EditHistory& h)
    : state (s), history (h)
{
    refresh();
}

juce::File PresetManager::getUserPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("SacredHills")
        .getChildFile ("EyraEQ")
        .getChildFile ("Presets");
}

void PresetManager::refresh()
{
    entries.clear();

    // Factory presets, ordered by their resource file names (which carry a numeric prefix).
    std::vector<std::pair<juce::String, juce::String>> factory; // original filename, resource name
    for (int i = 0; i < EyraBinaryData::namedResourceListSize; ++i)
        factory.emplace_back (EyraBinaryData::originalFilenames[i], EyraBinaryData::namedResourceList[i]);
    std::sort (factory.begin(), factory.end());

    for (const auto& [filename, resource] : factory)
    {
        int size = 0;
        const char* data = EyraBinaryData::getNamedResource (resource.toRawUTF8(), size);
        if (data == nullptr)
            continue;

        if (auto xml = juce::parseXML (juce::String::fromUTF8 (data, size)))
            if (xml->hasTagName ("EyraEQPreset"))
                entries.push_back ({ xml->getStringAttribute ("name", filename), true, {}, resource });
    }

    auto dir = getUserPresetDirectory();
    if (dir.isDirectory())
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
        files.sort();
        for (const auto& f : files)
            entries.push_back ({ f.getFileNameWithoutExtension(), false, f, {} });
    }

    notify();
}

std::unique_ptr<juce::XmlElement> PresetManager::createPresetXml (const juce::String& name) const
{
    using params::BandField;
    using params::bandParamID;

    auto xml = std::make_unique<juce::XmlElement> ("EyraEQPreset");
    xml->setAttribute ("name", name);
    xml->setAttribute ("version", 1);
    xml->setAttribute ("inputGain", realValue (state, params::id::inputGain));
    xml->setAttribute ("outputGain", realValue (state, params::id::outputGain));

    for (int b = 0; b < params::kNumBands; ++b)
    {
        if (realValue (state, bandParamID (b, BandField::Used)) < 0.5f)
            continue;

        auto* band = xml->createNewChildElement ("Band");
        band->setAttribute ("index", b + 1);
        band->setAttribute ("type", typeTokens[juce::jlimit (0, 7, (int) realValue (state, bandParamID (b, BandField::Type)))]);
        band->setAttribute ("freq", realValue (state, bandParamID (b, BandField::Frequency)));
        band->setAttribute ("gain", realValue (state, bandParamID (b, BandField::Gain)));
        band->setAttribute ("q", realValue (state, bandParamID (b, BandField::Q)));
        band->setAttribute ("slope", dsp::kSlopesDbPerOct[(size_t) juce::jlimit (0, 7, (int) realValue (state, bandParamID (b, BandField::Slope)))]);
        band->setAttribute ("placement", placementTokens[juce::jlimit (0, 4, (int) realValue (state, bandParamID (b, BandField::Placement)))]);
        band->setAttribute ("bypass", realValue (state, bandParamID (b, BandField::Bypass)) >= 0.5f ? 1 : 0);
    }

    return xml;
}

bool PresetManager::applyPresetXml (const juce::XmlElement& xml)
{
    using params::BandField;
    using params::bandParamID;

    if (! xml.hasTagName ("EyraEQPreset"))
        return false;

    // Gather target values first: every band defaults to disabled.
    std::map<juce::String, float> values;

    for (int b = 0; b < params::kNumBands; ++b)
    {
        for (auto f : { BandField::Used, BandField::Bypass, BandField::Type, BandField::Frequency,
                        BandField::Gain, BandField::Q, BandField::Slope, BandField::Placement })
            values[bandParamID (b, f)] = defaultRealValue (state, bandParamID (b, f));
    }

    values[params::id::inputGain] = (float) xml.getDoubleAttribute ("inputGain", 0.0);
    values[params::id::outputGain] = (float) xml.getDoubleAttribute ("outputGain", 0.0);

    for (auto* band : xml.getChildWithTagNameIterator ("Band"))
    {
        const int b = band->getIntAttribute ("index", 0) - 1;
        if (b < 0 || b >= params::kNumBands)
            continue;

        values[bandParamID (b, BandField::Used)] = 1.0f;
        values[bandParamID (b, BandField::Bypass)] = band->getBoolAttribute ("bypass", false) ? 1.0f : 0.0f;
        values[bandParamID (b, BandField::Type)] = (float) tokenIndex (band->getStringAttribute ("type"), typeTokens, 8, 0);
        values[bandParamID (b, BandField::Frequency)] = (float) band->getDoubleAttribute ("freq", 1000.0);
        values[bandParamID (b, BandField::Gain)] = (float) band->getDoubleAttribute ("gain", 0.0);
        values[bandParamID (b, BandField::Q)] = (float) band->getDoubleAttribute ("q", 0.7071);
        values[bandParamID (b, BandField::Slope)] = (float) slopeIndexFromDb (band->getIntAttribute ("slope", 12));
        values[bandParamID (b, BandField::Placement)] = (float) tokenIndex (band->getStringAttribute ("placement"), placementTokens, 5, 0);
    }

    history.beginEdit();
    {
        EditHistory::ScopedSuspend suspend (history);
        for (const auto& [id, v] : values)
            params::setParameterValue (state, id, v);
    }

    setCurrentPresetName (xml.getStringAttribute ("name", "Untitled"));
    return true;
}

bool PresetManager::loadPreset (int index)
{
    if (index < 0 || index >= (int) entries.size())
        return false;

    const auto& e = entries[(size_t) index];
    std::unique_ptr<juce::XmlElement> xml;

    if (e.isFactory)
    {
        int size = 0;
        if (const char* data = EyraBinaryData::getNamedResource (e.resourceName.toRawUTF8(), size))
            xml = juce::parseXML (juce::String::fromUTF8 (data, size));
    }
    else
    {
        xml = juce::parseXML (e.file);
    }

    if (xml == nullptr || ! applyPresetXml (*xml))
        return false;

    setCurrentPresetName (e.name);
    return true;
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;

    auto dir = getUserPresetDirectory();
    if (! dir.createDirectory())
        return false;

    auto xml = createPresetXml (name);
    if (! xml->writeTo (dir.getChildFile (name + fileExtension)))
        return false;

    setCurrentPresetName (name);
    refresh();
    return true;
}

bool PresetManager::deleteUserPreset (int index)
{
    if (index < 0 || index >= (int) entries.size() || entries[(size_t) index].isFactory)
        return false;

    const bool ok = entries[(size_t) index].file.deleteFile();
    refresh();
    return ok;
}

void PresetManager::loadNext()
{
    if (entries.empty())
        return;
    const int i = getCurrentIndex();
    loadPreset (i < 0 ? 0 : (i + 1) % (int) entries.size());
}

void PresetManager::loadPrevious()
{
    if (entries.empty())
        return;
    const int i = getCurrentIndex();
    const int n = (int) entries.size();
    loadPreset (i < 0 ? n - 1 : (i - 1 + n) % n);
}

void PresetManager::resetToDefault()
{
    juce::XmlElement empty ("EyraEQPreset");
    empty.setAttribute ("name", "Default");
    applyPresetXml (empty);
}

juce::String PresetManager::getCurrentPresetName() const
{
    const juce::ScopedLock lock (nameLock);
    return currentName;
}

void PresetManager::setCurrentPresetName (const juce::String& name)
{
    {
        const juce::ScopedLock lock (nameLock);
        currentName = name;
    }
    notify();
}

int PresetManager::getCurrentIndex() const
{
    const auto name = getCurrentPresetName();
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == name)
            return (int) i;
    return -1;
}

void PresetManager::notify()
{
    if (onChange)
        onChange();
}

} // namespace eyra::state
