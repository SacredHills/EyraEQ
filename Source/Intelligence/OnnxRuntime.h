#pragma once

#include <juce_core/juce_core.h>

#include <onnxruntime_c_api.h>

#include <mutex>

namespace eyra::intelligence
{

/**
    ONNX Runtime, loaded at run time.

    EyraEQ does not link against ONNX Runtime. The shared library is looked for next to the plug-in
    binary (inside the VST3 bundle / app), or at EYRA_ONNXRUNTIME_LIBRARY. If it is missing or too old,
    isAvailable() is false with a reason, and only instrument recognition is disabled.

    One instance per process (ONNX Runtime recommends a single environment). Thread-safe.
*/
class OnnxRuntime
{
public:
    static OnnxRuntime& getInstance();

    bool isAvailable() const noexcept { return api != nullptr && env != nullptr; }
    juce::String getStatus() const;
    const OrtApi* getApi() const noexcept { return api; }
    OrtEnv* getEnv() const noexcept { return env; }

    /** Candidate library files, in search order. */
    static juce::Array<juce::File> candidateLibraries();

    /** Directory containing the running plug-in or app binary. */
    static juce::File binaryDirectory();

private:
    OnnxRuntime();
    ~OnnxRuntime();

    // Destroyed after the environment is released (members outlive the destructor body). Inference is
    // single-threaded, so no ONNX Runtime threads remain when the library is closed.
    juce::DynamicLibrary library;
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    juce::String status;
};

/** An ONNX model session for float32 tensors. Run() may be called from several threads. */
class OnnxModel
{
public:
    OnnxModel() = default;
    ~OnnxModel();

    bool load (const juce::MemoryBlock& modelData, juce::String& error);
    bool isLoaded() const noexcept { return session != nullptr; }

    /** Runs the model on a float tensor of the given shape; returns the named output (flattened) and its last dimension. */
    bool run (const float* input, const std::vector<int64_t>& shape, const juce::String& inputName,
              const juce::String& outputName, std::vector<float>& output, int64_t& outputColumns, juce::String& error) const;

private:
    const OrtApi* api = nullptr;
    OrtSession* session = nullptr;

    JUCE_DECLARE_NON_COPYABLE (OnnxModel)
};

} // namespace eyra::intelligence
