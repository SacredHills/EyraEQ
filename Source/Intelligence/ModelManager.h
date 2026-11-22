#pragma once

#include <juce_core/juce_core.h>

namespace eyra::intelligence
{

struct ModelInfo
{
    juce::File directory;
    juce::String name, version;
    juce::var manifest;
    bool valid = false;
    juce::String message; // why it is invalid, or a short description when valid
    bool fromUserFolder = false;
};

/**
    Finds and validates models.

    Search order (the first valid model with the highest version wins; user folder wins ties):
      1. the user model folder   <app data>/SacredHills/EyraEQ/Models/<name>/   (model updates)
      2. the bundled models      <bundle>/Contents/Resources/Models/<name>/  or  <app dir>/Models/<name>/
      3. EYRA_MODEL_DIR          (development and tests)

    A model folder holds manifest.json, the .onnx file and its front-end data. Validation checks the
    manifest fields, that all files exist, the SHA-256 of the model and mel matrix, and the label count.
    The classifier additionally runs the reference vectors before trusting a model (self-test).
*/
class ModelManager
{
public:
    static juce::File userModelsDirectory();
    static juce::Array<juce::File> bundledModelsDirectories();

    /** All directories that may contain "<name>/manifest.json" or "instrument_taxonomy.json". */
    static juce::Array<juce::File> searchRoots();

    static ModelInfo inspect (const juce::File& modelDirectory);
    static ModelInfo findBestModel (const juce::String& name);

    /** The taxonomy file next to the chosen model's root (user folder first). */
    static juce::File findTaxonomyFile();

    /** Compares dotted version strings numerically ("1.10.0" > "1.9.2"). */
    static int compareVersions (const juce::String& a, const juce::String& b);

    static juce::String sha256Of (const juce::File&);

    /** Tests can replace the search roots. Pass an empty array to restore the defaults. */
    static void setSearchRootsForTesting (const juce::Array<juce::File>& roots);
};

} // namespace eyra::intelligence
