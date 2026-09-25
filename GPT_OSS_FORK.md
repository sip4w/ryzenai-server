# GPT-OSS on Ryzen AI 1.8

This fork loads the GPT-OSS chat template from `chat_template.jinja` and
returns the model's `analysis` and `final` channels separately for ordinary
and streaming chat completions. The model remains outside Git.

## Runtime layout

The local Windows installation uses:

- `build-oga16/native-runtime/` for `ryzenai-server.exe`, `ryzenai-tray.exe`,
  `ryzenai-tray.ini`, and runtime DLLs.
- `models/amd-gpt-oss-20B_eager_rai_1.8.0_npu_16K-moe-4x8/` for the active
  model, tokenizer, chat template, and `genai_config.json`.
- Microsoft ONNX Runtime GenAI 0.16.0 for the GenAI DLL and import library,
  alongside ONNX Runtime and Ryzen AI provider DLLs from Ryzen AI 1.8.0.

The Microsoft 0.16.0 and AMD 1.8.0 combination is not a vendor-supported
runtime pairing. Keep the staged DLL set together. Model weights, runtime
DLLs, local configuration, and executable output are ignored by Git.

## Windows build

Install Visual Studio 2022 C++ Build Tools, CMake, the AMD NPU driver, and
the Ryzen AI 1.8.0 runtime. Stage model-compatible DLLs in
`build-oga16/native-runtime` before configuring. In particular, that
directory needs `onnxruntime-genai.dll` from Microsoft 0.16.0 and the
matching ONNX Runtime and Ryzen AI provider DLLs from the local AMD runtime.
The CMake option downloads Microsoft's 0.16.0 headers and import library;
it does not install the AMD driver or provider DLLs.

From the repository root in a Visual Studio Developer PowerShell:

```powershell
$runtime = (Resolve-Path '.\build-oga16\native-runtime').Path
cmake -S . -B build-oga16 -G 'Visual Studio 17 2022' -A x64 `
  -DRYZENAI_MICROSOFT_OGA=ON '-DRYZENAI_OGA_VERSION=0.16.0' `
  "-DRYZENAI_RUNTIME_DIR=$($runtime.Replace('\', '/'))"
cmake --build build-oga16 --config Release
```

The executables are written to `build-oga16/bin/Release`. Stop and exit the
tray before replacing files in the active runtime directory:

```powershell
Copy-Item '.\build-oga16\bin\Release\ryzenai-server.exe' $runtime -Force
Copy-Item '.\build-oga16\bin\Release\ryzenai-tray.exe' $runtime -Force
```

For a new local runtime, copy `ryzenai-tray.ini.example` to
`build-oga16/native-runtime/ryzenai-tray.ini` and set `model` to the full
model-directory path. The tray starts the server without a console window.
Its menu provides Start, Stop, Open /health, Open log, and Exit. It does not
install itself in Windows startup. `ryzenai-server.log` is written beside
the tray executable.

## Model configuration and responses

The active model uses `hybrid_opt_qmoe_dynamic_experts=4`,
`hybrid_opt_qmoe_num_dynamic_layers=8`, and
`hybrid_opt_token_backend=npu` in `genai_config.json`. ONNX Runtime GenAI
reads these settings; the server does not route experts or modify weights.
The model is a 16K Token Fusion variant, while the current server session
uses `--ctx-size 4096`. This limits the total sequence, including generated
tokens. The server reserves the requested output budget before truncating
old prompt tokens.

Keep the generation prompt in `chat_template.jinja` ending at
`<|start|>assistant`. Forcing `<|channel|>final<|message|>` there suppresses
the separate analysis channel. Chat completions return analysis in
`reasoning_content` and the final answer in `message.content`. The Responses
API returns answer text. If `max_tokens` is exhausted during reasoning,
the final answer may be empty and the finish reason is `length` or
`incomplete`; request a larger output budget for reasoning-heavy work.
