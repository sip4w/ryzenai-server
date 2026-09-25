# Ryzen AI LLM Server

An OpenAI-compatible Windows server for GPT-OSS-20B on an AMD Ryzen AI NPU.
This fork uses ONNX Runtime GenAI 0.16.0 with the Ryzen AI 1.8.0 runtime and
the `amd-gpt-oss-20B_eager_rai_1.8.0_npu_16K-moe-4x8` model. The server
runs one model per process. Model weights and AMD DLLs are local files and
are not stored in Git.

## Start the local server

The tray executable, server executable, runtime DLLs, and `ryzenai-tray.ini`
are in `build-oga16/native-runtime`. On this host, double-click
`Start RyzenAI Server.lnk` in the repository root, or run:

```powershell
& '.\build-oga16\native-runtime\ryzenai-tray.exe' --start
```

The tray menu provides Start, Stop, Open /health, Open log, and Exit. It does
not add itself to Windows startup. The current configuration binds to
`127.0.0.1:8080`, uses a 4096-token context, and loads the `-moe-4x8` model
from `models/`. Edit `build-oga16/native-runtime/ryzenai-tray.ini` to change
these settings. The local `.lnk` and `.ini` files are ignored by Git.

To run without the tray, launch `build-oga16/native-runtime/ryzenai-server.exe`
with `-m MODEL_DIRECTORY`, `--ctx-size 4096`, `--host 127.0.0.1`, and
`--port 8080`. Do not start a second server on the same port.

## Build

On Windows, install Visual Studio 2022 C++ Build Tools and CMake. The AMD
NPU driver and the model-compatible Ryzen AI 1.8.0 runtime DLLs must also be
available. The existing local `build-oga16/native-runtime` directory holds
the runtime DLLs used by this build; a fresh clone needs those files supplied
separately. See [GPT_OSS_FORK.md](GPT_OSS_FORK.md) for the build command and
runtime layout.

The local build combines Microsoft ONNX Runtime GenAI 0.16.0 with AMD Ryzen
AI 1.8.0 DLLs. This combination works in the current setup but is not a
vendor-supported runtime pairing. Keep the DLL set together when updating
the server.

## API

The server provides:

- `GET /health` - model, execution mode, and runtime status
- `POST /v1/chat/completions` - chat, reasoning content, tool calls, and SSE
- `POST /v1/completions` - text completions and SSE
- `POST /v1/responses` - Responses-style output

For GPT-OSS chat, `message.content` contains the final answer and
`message.reasoning_content` contains analysis when the model emits it. The
generation budget includes reasoning tokens. See [GPT_OSS_FORK.md](GPT_OSS_FORK.md)
for model-specific behavior.

Windows owns the NPU process and model files. A WSL client connects to the
Windows HTTP endpoint; it does not load the model through a Linux NPU driver.
The default `127.0.0.1` bind is local to Windows. To connect from a separate
network namespace, configure a reachable host address and restrict access
appropriately.

## Troubleshooting

- If the tray is open but the API is unavailable, inspect
  `build-oga16/native-runtime/ryzenai-server.log` and the tray configuration.
- If model loading fails, check the model path and keep its ONNX graph,
  tokenizer, and `genai_config.json` together.
- If a DLL fails to load, verify that `build-oga16/native-runtime` contains
  the matching ONNX Runtime GenAI, ONNX Runtime, and Ryzen AI provider DLLs.
- If port 8080 is occupied, stop the other process or change the tray port.

## License

The source is MIT licensed; see [LICENSE](LICENSE). AMD runtime DLLs have
separate terms in [AMD_LICENSE](AMD_LICENSE) and are not committed here.
