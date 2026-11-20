# TonVanity — GPU TON address generator

![TonVanity](screenshot.png)

TON address generator for CUDA, OpenCL and CPU. Supports 12- and 24-word seed phrases and W5, V4R2 and TGWallet wallets.

## ⚠️ Security

- The application runs fully offline and makes no network requests.
- Seed phrases and private keys never leave your PC.
- Found addresses and seed phrases are stored locally only — in `found.txt` next to the application.

## Features

- Backends: CUDA (sm_75/86/89/120), OpenCL and CPU.
- Modes: CPU, CPU+GPU, GPU only.
- Seed phrases: 12 or 24 words.
- Wallets: W5, V4R2, TGWallet.
- Address formats: UQ (non-bounceable) and EQ (bounceable).
- Suffix search with an optional case-sensitive match.
- Stats: attempts, elapsed time, phrases/s, addresses/s.

## Performance

Measured on an NVIDIA GeForce RTX 5070 Ti Laptop GPU (12 GB) and Intel Core 7 240H, 16 CPU threads. Raw results, no throttling or power-limit correction.

- CUDA, GPU only: **~171,000 phrases/s** (10 s run); GUI peak **198,704 phrases/s**.
- CUDA + CPU: **~143,000 phrases/s**.
- CPU, 16 threads: **~6,200 phrases/s**.
- OpenCL: **~1,030,000 phrases/s** with a suffix filter (1 s run, 24-word seed).

With a suffix filter, CUDA reaches **~417,000 phrases/s**; only matching addresses are kept.

## Run

Windows 10/11 x64. No .NET or Windows App SDK installation required — the build is self-contained.

- Console: [TonVanity.exe](TonVanity.exe)
- GUI: [TonVanity WinUI/TonVanity.exe](TonVanity%20WinUI/TonVanity.exe)

## Build

Sources are in the [source](https://github.com/QuirkyTroll/TonVanity/tree/source) branch.

Requirements:

- [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0) — for WinUI 3.
- [CUDA Toolkit 12.x](https://developer.nvidia.com/cuda-downloads) — `nvrtc`, `nvcc`/`ptxas`, `cudart64_12.dll`.
- MinGW-w64 ([w64devkit](https://github.com/skeeto/w64devkit)) — `g++` and `windres`.
- Python 3 — for toolchain bootstrap.

```powershell
git clone -b source https://github.com/QuirkyTroll/TonVanity.git TonVanity-src
cd TonVanity-src
.\build.ps1
```

The first run requires internet access; rebuild with `.\build.ps1 -Offline`. The script builds `tonvanity.dll`, the console `TonVanity.exe` and the WinUI 3 UI into the sibling `TonVanity/` folder.

## License

MIT — see [LICENSE](LICENSE).
