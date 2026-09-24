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

### Ryzen AI 1.8.0 / GPT-OSS 20B 16K

Use a separate build directory and the installed 1.8.0 Python runtime. The
Microsoft 0.14.0 archive supplies only headers and the import library;
`onnxruntime-genai.dll` must come from `onnxruntime-genai-directml-ryzenai`
0.14.0 and the other DLLs from its matching `onnxruntime-vitisai` package.
The model directory can hold `hybrid_opt_qmoe_dynamic_experts=4` and
`hybrid_opt_qmoe_num_dynamic_layers=8`; no server-side MoE routing is needed.

```powershell
$site = 'C:\path\to\venv\Lib\site-packages'
$runtime = (New-Item -ItemType Directory -Force 'build-oga14\runtime').FullName
Copy-Item "$site\onnxruntime\capi\*.dll" $runtime
Copy-Item "$site\onnxruntime_genai\onnxruntime-genai.dll" $runtime
Copy-Item "$site\onnxruntime_genai\D3D12Core.dll" $runtime

cmake -S . -B build-oga14 -G 'Visual Studio 17 2022' -A x64 `
  -DRYZENAI_MICROSOFT_OGA=ON '-DRYZENAI_OGA_VERSION=0.14.0' `
  "-DRYZENAI_RUNTIME_DIR=$($runtime.Replace('\', '/'))"
cmake --build build-oga14 --config Release

$env:RYZENAI_VERSION = '1.8.0'
& .\build-oga14\bin\Release\ryzenai-server.exe `
  -m 'C:\path\to\amd-gpt-oss-20B_eager_rai_1.8.0_npu_16K-moe-4x8' `
  --ctx-size 4096 --host 127.0.0.1 --port 8080
```

`--ctx-size` limits the total sequence, including generated tokens. Prompts
longer than the remaining budget are truncated from the beginning. For a
repeatable quality check, send `"do_sample": false` in a chat completion
request. Do not mix 1.7.1 and 1.8.0 runtime DLLs in one executable directory.

### Experimental OGA 0.16.0 build

AMD Ryzen AI 1.8.0 documents OGA 0.14.0 as its compatible release. Microsoft
OGA 0.16.0 can be built and smoke-tested separately with the existing AMD
1.8.0 ONNX Runtime and RyzenAI provider DLLs, but this mixed combination is
not an AMD-supported runtime. Keep `build-oga14` as the known-compatible build.

```powershell
$runtime = (Resolve-Path 'build-oga14\runtime').Path
cmake -S . -B build-oga16 -G 'Visual Studio 17 2022' -A x64 `
  -DRYZENAI_MICROSOFT_OGA=ON '-DRYZENAI_OGA_VERSION=0.16.0' `
  "-DRYZENAI_RUNTIME_DIR=$($runtime.Replace('\', '/'))"
cmake --build build-oga16 --config Release

$native = (New-Item -ItemType Directory -Force 'build-oga16\native-runtime').FullName
Copy-Item 'build-oga16\bin\Release\ryzenai-server.exe' $native
Copy-Item "$runtime\*.dll" $native
Copy-Item 'build-oga16\microsoft-oga\onnxruntime-genai-0.16.0-win-x64\lib\onnxruntime-genai.dll' $native -Force

$env:RYZENAI_VERSION = '1.8.0'
& "$native\ryzenai-server.exe" `
  -m 'C:\path\to\amd-gpt-oss-20B_eager_rai_1.8.0_npu_16K-moe-4x8' `
  --ctx-size 4096 --host 127.0.0.1 --port 8080
```

The regular `build-oga16/bin/Release` directory still contains AMD's OGA
0.14.0 DLL. The `native-runtime` directory replaces only that DLL with the
Microsoft 0.16.0 release; all RyzenAI provider and ONNX Runtime DLLs remain
from AMD 1.8.0. In a local 4K smoke test, both configurations loaded the
model and returned the expected answer through the NPU.

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
