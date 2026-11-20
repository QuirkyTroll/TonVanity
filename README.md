# TonVanity — GPU-генератор TON-адресов

![TonVanity](screenshot.png)

Генератор TON-адресов на CUDA, OpenCL и CPU. Поддерживает seed-фразы из 12 и 24 слов, кошельки W5, V4R2 и TGWallet.

## ⚠️ Безопасность

- Приложение работает полностью офлайн и не делает сетевых запросов.
- Seed-фразы и приватные ключи не покидают ваш ПК.
- Найденные адреса и сид-фразы сохраняются только локально — в `found.txt` рядом с приложением.

## Возможности

- Бэкенды: CUDA (sm_75/86/89/120), OpenCL и CPU.
- Режимы: CPU, CPU+GPU, GPU only.
- Seed-фразы: 12 или 24 слова.
- Кошельки: W5, V4R2, TGWallet.
- Форматы адресов: UQ (non-bounceable) и EQ (bounceable).
- Поиск по окончанию адреса, с учётом регистра.
- Статистика: попытки, время, фраз/с, адресов/с.

## Требования

- Windows 10/11 x64.
- [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0) — для WinUI 3.
- [CUDA Toolkit 12.x](https://developer.nvidia.com/cuda-downloads) — `nvrtc`, `nvcc`/`ptxas`, `cudart64_12.dll`.
- MinGW-w64 ([w64devkit](https://github.com/skeeto/w64devkit)) — `g++` и `windres`.
- Python 3 — для бутстрапа тулчейна.

`build.ps1` скачивает w64devkit и CUDA-компоненты в `.work/toolchain`, а .NET 8 SDK — в `.work/dotnet`, если его нет в системе.

## Сборка

Из корня репозитория:

```powershell
.\build.ps1
```

Первый запуск — с интернетом. Если тулчейн уже скачан:

```powershell
.\build.ps1 -Offline
```

Скрипт собирает `tonvanity.dll`, консольный `TonVanity.exe` и WinUI 3 UI. Результат — в соседней папке `TonVanity/`:

```text
TonVanity/
├── TonVanity.exe        # консоль
├── tonvanity.dll
├── cudart64_12.dll
└── ui/
    └── TonVanity.exe    # WinUI 3
```

## Запуск

Готовая сборка: [TonVanity-win-x64.zip](https://github.com/QuirkyTroll/TonVanity/releases/latest) (Windows x64).

- Консоль: `TonVanity.exe`.
- GUI: `ui\TonVanity.exe`.
- Установка .NET и Windows App SDK не требуется: сборка self-contained.

## Лицензия

MIT — см. [LICENSE](LICENSE).
