# ONNX Runtime C API headers

`include/` holds the C API headers of ONNX Runtime **1.32.0** (`ORT_API_VERSION` 32), Copyright
Microsoft Corporation, MIT licence (see `LICENSE`). They were taken from the conda-forge
`onnxruntime-cpp` 1.32.0 package (a build of microsoft/onnxruntime v1.32.0).

The shared library bundled with the plug-in is Microsoft's official build from the `onnxruntime`
1.32.0 wheel on PyPI (see `CMakeLists.txt`), verified by SHA-256.

EyraEQ does not link against ONNX Runtime. It loads the shared library at run time
(`Source/Intelligence/OnnxRuntime.cpp`) from inside the plug-in bundle, so a missing or incompatible
runtime only disables instrument recognition; the EQ keeps working.
