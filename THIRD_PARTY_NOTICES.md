# Third-party components

| Component | Used for | Licence | Shipped with the plug-in |
|---|---|---|---|
| [JUCE](https://juce.com) 9.0.6 | Framework (audio, GUI, plug-in wrappers) | AGPLv3 (used under its free licence) | Compiled in |
| Steinberg VST3 SDK (bundled with JUCE) | VST3 format | MIT | Compiled in |
| [ONNX Runtime](https://onnxruntime.ai) 1.32.0, Microsoft | Running the instrument model | MIT ([ThirdParty/onnxruntime/LICENSE](ThirdParty/onnxruntime/LICENSE)) | Shared library inside the bundle; C API headers in `ThirdParty/` |
| YAMNet, Google LLC | Instrument-recognition model weights | Apache License 2.0 ([Models/yamnet/NOTICE](Models/yamnet/NOTICE)) | `Models/yamnet/yamnet.onnx` (converted, network unchanged) |
| AudioSet ontology, Google | Class names in the model manifest | CC BY 4.0 | `Models/yamnet/manifest.json` |
| NSynth dataset, Google Magenta | Validation only (not shipped) | CC BY 4.0 | No. Derived evidence scores are in `Documentation/Validation/` |

EyraEQ's own code is licensed under the GNU Affero General Public License v3.0 (`LICENSE`).
