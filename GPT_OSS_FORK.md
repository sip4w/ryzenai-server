# GPT-OSS / Ryzen AI 1.7.1 fork

This branch adds GPT-OSS chat-template loading from `chat_template.jinja` and
separates the model's `analysis` and `final` channels in both ordinary and
streaming chat completions. It leaves the model directory outside Git.

## Build on Windows

Install Visual Studio 2022 C++ tools and CMake. For the standard build, also
install the Ryzen AI 1.7.1 SDK. Use runtime DLLs matching the model's 16-input
`QMoEBf` graph; the published server 1.7.0 DLLs are incompatible.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DOGA_ROOT="C:/Program Files/RyzenAI/1.7.1"
cmake --build build --config Release
& .\build\bin\Release\ryzenai-server.exe -m "C:\path\to\amd-gpt-oss-20b-onnx-ryzenai-npu" --host 127.0.0.1 --port 8080
```

Keep the model's MoE configuration in its existing `genai_config.json`.
The server reads that configuration through ONNX Runtime GenAI; this fork
does not modify weights or select experts itself. Do not mix 1.7.0 and 1.7.1
runtime DLLs in the executable directory.

### Build without the AMD SDK

The `RYZENAI_MICROSOFT_OGA` option downloads the official Microsoft ONNX
Runtime GenAI 0.11.2 Windows x64 DML archive and verifies its SHA-256. Only
its headers and `onnxruntime-genai.lib` are used for compilation. The AMD
runtime DLLs are still required at run time; point `RYZENAI_RUNTIME_DIR` to
the local model directory that already contains them. CMake copies those DLLs
next to the executable, but does not copy model weights. Neither DLLs nor
weights are committed to Git.

```powershell
cmake -S . -B build-microsoft-oga -G "Visual Studio 17 2022" -A x64 `
  -DRYZENAI_MICROSOFT_OGA=ON `
  -DRYZENAI_RUNTIME_DIR="C:/path/to/amd-gpt-oss-20b-onnx-ryzenai-npu"
cmake --build build-microsoft-oga --config Release
& .\build-microsoft-oga\bin\Release\ryzenai-server.exe -m "C:\path\to\amd-gpt-oss-20b-onnx-ryzenai-npu"
```

Do not replace the model's `onnxruntime-genai.dll` with the DML DLL from the
Microsoft archive. This option removes the SDK requirement for building;
it does not replace AMD's Windows NPU runtime or driver.

## Parser tests without the SDK

```bash
cmake -S . -B build-parser -DRYZENAI_REASONING_TESTS_ONLY=ON
cmake --build build-parser
ctest --test-dir build-parser --output-on-failure
```

For a WSL client, use the Windows server's reachable host address and port.
The model and the server process remain on Windows so the Windows NPU driver
is used. The repo ignores DLLs, executable output, and model weights; do not
force-add proprietary SDK files.
