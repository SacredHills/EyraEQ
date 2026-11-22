#include "OnnxRuntime.h"

namespace eyra::intelligence
{

namespace
{
    juce::StringArray libraryNames()
    {
       #if JUCE_WINDOWS
        return { "onnxruntime.dll" };
       #elif JUCE_MAC
        return { "libonnxruntime.1.32.0.dylib", "libonnxruntime.dylib" };
       #else
        return { "libonnxruntime.so.1.32.0", "libonnxruntime.so.1", "libonnxruntime.so" };
       #endif
    }

    /** Converts an OrtStatus into an error string (and releases it). Returns true on success. */
    bool ok (const OrtApi* api, OrtStatus* status, juce::String& error)
    {
        if (status == nullptr)
            return true;
        error = juce::String (api->GetErrorMessage (status));
        api->ReleaseStatus (status);
        return false;
    }
} // namespace

juce::File OnnxRuntime::binaryDirectory()
{
    // For a plug-in this is the plug-in's own binary (JUCE resolves it from the module, not the host).
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
}

juce::Array<juce::File> OnnxRuntime::candidateLibraries()
{
    juce::Array<juce::File> files;

    const auto overridePath = juce::SystemStats::getEnvironmentVariable ("EYRA_ONNXRUNTIME_LIBRARY", {});
    if (overridePath.isNotEmpty())
        files.add (juce::File (overridePath));

    const auto dir = binaryDirectory();
    for (const auto& name : libraryNames())
    {
        files.add (dir.getChildFile (name));
       #if JUCE_MAC
        files.add (dir.getParentDirectory().getChildFile ("Frameworks").getChildFile (name));
       #endif
    }
    return files;
}

OnnxRuntime& OnnxRuntime::getInstance()
{
    static OnnxRuntime instance;
    return instance;
}

OnnxRuntime::OnnxRuntime()
{
    juce::File found;
    for (const auto& f : candidateLibraries())
        if (f.existsAsFile())
        {
            found = f;
            break;
        }

    if (found == juce::File())
    {
        status = "ONNX Runtime library not found next to the plug-in.";
        return;
    }

    if (! library.open (found.getFullPathName()))
    {
        status = "Could not load " + found.getFileName() + ".";
        return;
    }

    using GetApiBase = const OrtApiBase* (ORT_API_CALL*) ();
    auto getApiBase = reinterpret_cast<GetApiBase> (library.getFunction ("OrtGetApiBase"));
    if (getApiBase == nullptr)
    {
        status = found.getFileName() + " is not an ONNX Runtime library.";
        return;
    }

    const OrtApiBase* base = getApiBase();
    const juce::String version (base->GetVersionString());
    api = base->GetApi (ORT_API_VERSION);
    if (api == nullptr)
    {
        status = "ONNX Runtime " + version + " is too old (API version " + juce::String (ORT_API_VERSION) + " needed).";
        return;
    }

    juce::String error;
    if (! ok (api, api->CreateEnv (ORT_LOGGING_LEVEL_WARNING, "EyraEQ", &env), error))
    {
        status = "ONNX Runtime could not start: " + error;
        env = nullptr;
        api = nullptr;
        return;
    }

    status = "ONNX Runtime " + version;
}

OnnxRuntime::~OnnxRuntime()
{
    if (api != nullptr && env != nullptr)
        api->ReleaseEnv (env);
    env = nullptr;
    api = nullptr;
}

juce::String OnnxRuntime::getStatus() const
{
    return status;
}

//==============================================================================
OnnxModel::~OnnxModel()
{
    if (session != nullptr)
        api->ReleaseSession (session);
}

bool OnnxModel::load (const juce::MemoryBlock& data, juce::String& error)
{
    auto& rt = OnnxRuntime::getInstance();
    if (! rt.isAvailable())
    {
        error = rt.getStatus();
        return false;
    }
    api = rt.getApi();

    OrtSessionOptions* options = nullptr;
    if (! ok (api, api->CreateSessionOptions (&options), error))
        return false;

    // Gentle on the host: inference runs on a background thread and must not compete with audio.
    bool success = ok (api, api->SetIntraOpNumThreads (options, 1), error)
                && ok (api, api->SetInterOpNumThreads (options, 1), error)
                && ok (api, api->SetSessionGraphOptimizationLevel (options, ORT_ENABLE_ALL), error)
                && ok (api, api->CreateSessionFromArray (rt.getEnv(), data.getData(), data.getSize(), options, &session), error);
    api->ReleaseSessionOptions (options);

    if (! success)
        session = nullptr;
    return success;
}

bool OnnxModel::run (const float* input, const std::vector<int64_t>& shape, const juce::String& inputName,
                     const juce::String& outputName, std::vector<float>& output, int64_t& outputColumns, juce::String& error) const
{
    if (session == nullptr)
    {
        error = "Model not loaded.";
        return false;
    }

    size_t count = 1;
    for (auto d : shape)
        count *= (size_t) d;

    OrtMemoryInfo* memory = nullptr;
    if (! ok (api, api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &memory), error))
        return false;

    OrtValue* inputTensor = nullptr;
    const bool created = ok (api, api->CreateTensorWithDataAsOrtValue (memory, const_cast<float*> (input), count * sizeof (float),
                                                                        shape.data(), shape.size(),
                                                                        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputTensor), error);
    api->ReleaseMemoryInfo (memory);
    if (! created)
        return false;

    const auto in = inputName.toStdString();
    const auto out = outputName.toStdString();
    const char* inNames[] = { in.c_str() };
    const char* outNames[] = { out.c_str() };
    OrtValue* outputTensor = nullptr;

    bool success = ok (api, api->Run (session, nullptr, inNames, &inputTensor, 1, outNames, 1, &outputTensor), error);
    api->ReleaseValue (inputTensor);

    if (success)
    {
        OrtTensorTypeAndShapeInfo* info = nullptr;
        float* data = nullptr;
        size_t elements = 0, dims = 0;
        success = ok (api, api->GetTensorTypeAndShape (outputTensor, &info), error)
               && ok (api, api->GetTensorShapeElementCount (info, &elements), error)
               && ok (api, api->GetDimensionsCount (info, &dims), error);

        if (success && dims > 0)
        {
            std::vector<int64_t> outShape (dims);
            success = ok (api, api->GetDimensions (info, outShape.data(), dims), error)
                   && ok (api, api->GetTensorMutableData (outputTensor, reinterpret_cast<void**> (&data)), error);
            if (success)
            {
                output.assign (data, data + elements);
                outputColumns = outShape.back();
            }
        }
        if (info != nullptr)
            api->ReleaseTensorTypeAndShapeInfo (info);
        api->ReleaseValue (outputTensor);
    }
    return success;
}

} // namespace eyra::intelligence
