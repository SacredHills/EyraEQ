#include "ModelManager.h"

#include "OnnxRuntime.h"

#include <juce_cryptography/juce_cryptography.h>

namespace eyra::intelligence
{

namespace
{
    juce::CriticalSection& testRootsLock()
    {
        static juce::CriticalSection lock;
        return lock;
    }

    juce::Array<juce::File>& testRoots()
    {
        static juce::Array<juce::File> roots;
        return roots;
    }
} // namespace

juce::File ModelManager::userModelsDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("SacredHills").getChildFile ("EyraEQ").getChildFile ("Models");
}

juce::Array<juce::File> ModelManager::bundledModelsDirectories()
{
    const auto bin = OnnxRuntime::binaryDirectory();
    // VST3 bundle and macOS app: Contents/<arch or MacOS>/../Resources/Models; Linux / Windows app: <dir>/Models
    return { bin.getParentDirectory().getChildFile ("Resources").getChildFile ("Models"), bin.getChildFile ("Models") };
}

void ModelManager::setSearchRootsForTesting (const juce::Array<juce::File>& roots)
{
    const juce::ScopedLock lock (testRootsLock());
    testRoots() = roots;
}

juce::Array<juce::File> ModelManager::searchRoots()
{
    {
        const juce::ScopedLock lock (testRootsLock());
        if (! testRoots().isEmpty())
            return testRoots();
    }

    juce::Array<juce::File> roots;
    roots.add (userModelsDirectory());
    roots.addArray (bundledModelsDirectories());
    const auto dev = juce::SystemStats::getEnvironmentVariable ("EYRA_MODEL_DIR", {});
    if (dev.isNotEmpty())
        roots.add (juce::File (dev));
    return roots;
}

int ModelManager::compareVersions (const juce::String& a, const juce::String& b)
{
    auto pa = juce::StringArray::fromTokens (a, ".", {});
    auto pb = juce::StringArray::fromTokens (b, ".", {});
    for (int i = 0; i < juce::jmax (pa.size(), pb.size()); ++i)
    {
        const int x = pa[i].getIntValue(), y = pb[i].getIntValue();
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

juce::String ModelManager::sha256Of (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk())
        return {};
    return juce::SHA256 (in).toHexString();
}

ModelInfo ModelManager::inspect (const juce::File& dir)
{
    ModelInfo info;
    info.directory = dir;

    const auto manifestFile = dir.getChildFile ("manifest.json");
    if (! manifestFile.existsAsFile())
    {
        info.message = "no manifest.json";
        return info;
    }

    info.manifest = juce::JSON::parse (manifestFile);
    const auto& m = info.manifest;
    if (! m.isObject())
    {
        info.message = "manifest.json is not valid JSON";
        return info;
    }

    info.name = m.getProperty ("name", "").toString();
    info.version = m.getProperty ("version", "").toString();
    const auto file = dir.getChildFile (m.getProperty ("file", "").toString());
    const auto labels = m.getProperty ("labels", {});
    const auto input = m.getProperty ("input", {});
    const auto outputs = m.getProperty ("outputs", {});
    const auto frontend = m.getProperty ("frontend", {});

    if (info.name.isEmpty() || info.version.isEmpty() || ! labels.isArray() || labels.size() == 0
        || ! input.isObject() || ! outputs.isObject() || ! frontend.isObject()
        || input.getProperty ("name", "").toString().isEmpty() || outputs.getProperty ("scores", "").toString().isEmpty())
    {
        info.message = "manifest is missing required fields";
        return info;
    }

    if (! file.existsAsFile())
    {
        info.message = "model file " + file.getFileName() + " is missing";
        return info;
    }

    if (sha256Of (file) != m.getProperty ("sha256", "").toString())
    {
        info.message = "model file checksum does not match the manifest (damaged or modified)";
        return info;
    }

    const auto mel = dir.getChildFile (frontend.getProperty ("melMatrix", "").toString());
    if (! mel.existsAsFile() || sha256Of (mel) != frontend.getProperty ("melMatrixSha256", "").toString())
    {
        info.message = "front-end data missing or damaged";
        return info;
    }

    info.valid = true;
    info.message = info.name + " " + info.version;
    return info;
}

ModelInfo ModelManager::findBestModel (const juce::String& name)
{
    const auto userDir = userModelsDirectory();
    ModelInfo best;
    juce::String firstProblem;

    for (const auto& root : searchRoots())
    {
        const auto dir = root.getChildFile (name);
        if (! dir.isDirectory())
            continue;

        auto info = inspect (dir);
        info.fromUserFolder = root == userDir;
        if (! info.valid)
        {
            if (firstProblem.isEmpty())
                firstProblem = dir.getFullPathName() + ": " + info.message;
            continue;
        }
        if (! best.valid || compareVersions (info.version, best.version) > 0)
            best = info;
    }

    if (! best.valid)
        best.message = firstProblem.isNotEmpty() ? firstProblem : "no " + name + " model installed";
    return best;
}

juce::File ModelManager::findTaxonomyFile()
{
    for (const auto& root : searchRoots())
    {
        const auto f = root.getChildFile ("instrument_taxonomy.json");
        if (f.existsAsFile())
            return f;
    }
    return {};
}

} // namespace eyra::intelligence
