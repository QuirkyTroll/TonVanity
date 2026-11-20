#!/usr/bin/env python3
"""Bootstrap the Windows build toolchain for TonVanity.

Downloads (or reuses) w64devkit 2.11.0, the CUDA 12.9 NVRTC/NVCC/runtime
wheels, and builds the tiny NVRTC helper. Python is build-only; the shipped
application has no Python dependency.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_TC = ROOT.parent / ".work" / "toolchain"
W64_URL = "https://github.com/skeeto/w64devkit/releases/download/v2.11.0/w64devkit-x64-2.11.0.7z.exe"
W64_SIZE = 68801240
SEVEN_URL = "https://www.7-zip.org/a/7zr.exe"
SEVEN_SIZE = 602624
WHEELS = [
    ("nvidia-cuda-nvcc-cu12", "12.9.86"),
    ("nvidia-cuda-nvrtc-cu12", "12.9.86"),
    ("nvidia-cuda-runtime-cu12", "12.9.79"),
]
UA = {"User-Agent": "TonVanity-build/1.0"}


def log(msg: str) -> None:
    print(msg, flush=True)


def download(url: str, dest: pathlib.Path, size: int | None = None) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_name(dest.name + ".part")
    log(f"  download {url}")
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=180) as r, open(tmp, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done = 0
        while True:
            chunk = r.read(1024 * 1024)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            if total:
                print(f"\r    {done * 100 // total:3d}%  {done // (1024 * 1024)} MiB", end="", flush=True)
    print()
    actual = tmp.stat().st_size
    if size is not None and actual != size:
        tmp.unlink(missing_ok=True)
        raise RuntimeError(f"size mismatch for {url}: {actual} != {size}")
    tmp.replace(dest)


def wheel_url(name: str, version: str) -> tuple[str, int]:
    url = f"https://pypi.org/pypi/{name}/{version}/json"
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=60) as r:
        data = json.load(r)
    for f in data.get("urls", []):
        if f["filename"].endswith("py3-none-win_amd64.whl"):
            return f["url"], int(f["size"])
    raise RuntimeError(f"no win_amd64 wheel for {name}=={version}")


def requirement_paths(tc: pathlib.Path) -> dict[str, pathlib.Path]:
    return {
        "g++": tc / "w64devkit2" / "w64devkit" / "bin" / "g++.exe",
        "windres": tc / "w64devkit2" / "w64devkit" / "bin" / "windres.exe",
        "nvrtc": tc / "cuda" / "nvidia" / "cuda_nvrtc" / "bin" / "nvrtc64_120_0.dll",
        "ptxas": tc / "cuda" / "nvidia" / "cuda_nvcc" / "bin" / "ptxas.exe",
        "cudart": tc / "cuda" / "nvidia" / "cuda_runtime" / "bin" / "cudart64_12.dll",
        "helper": tc / "nvrtc_compile.exe",
    }


def missing(paths: dict[str, pathlib.Path]) -> list[str]:
    return [k for k, v in paths.items() if not v.is_file()]


def extract_w64(tc: pathlib.Path) -> None:
    seven = tc / "7zr.exe"
    sfx = tc / "w64devkit-x64-2.11.0.7z.exe"
    if not seven.is_file():
        download(SEVEN_URL, seven, SEVEN_SIZE)
    if not sfx.is_file():
        download(W64_URL, sfx, W64_SIZE)
    out = tc / "_w64_extract"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    log("  extract w64devkit")
    subprocess.run([str(seven), "x", "-y", f"-o{out}", str(sfx)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    src = out / "w64devkit"
    if not (src / "bin" / "g++.exe").is_file():
        raise RuntimeError("w64devkit archive did not contain w64devkit/bin/g++.exe")
    dst_root = tc / "w64devkit2"
    shutil.rmtree(dst_root, ignore_errors=True)
    dst_root.mkdir(parents=True, exist_ok=True)
    shutil.move(str(src), str(dst_root / "w64devkit"))
    shutil.rmtree(out, ignore_errors=True)


def ensure_wheels(tc: pathlib.Path, offline: bool) -> None:
    cuda = tc / "cuda"
    cuda.mkdir(parents=True, exist_ok=True)
    for name, version in WHEELS:
        tag = f"{name.replace('-', '_')}-{version}-py3-none-win_amd64.whl"
        wheel = cuda / tag
        target = cuda / "nvidia"
        if name.endswith("nvcc-cu12"):
            target = cuda / "nvidia" / "cuda_nvcc"
        elif name.endswith("nvrtc-cu12"):
            target = cuda / "nvidia" / "cuda_nvrtc"
        elif name.endswith("runtime-cu12"):
            target = cuda / "nvidia" / "cuda_runtime"
        if target.is_dir():
            continue
        if not wheel.is_file():
            if offline:
                raise RuntimeError(f"offline: missing {wheel}")
            url, size = wheel_url(name, version)
            download(url, wheel, size)
        log(f"  extract {wheel.name}")
        with zipfile.ZipFile(wheel) as z:
            z.extractall(cuda)


def build_helper(tc: pathlib.Path) -> None:
    gxx = tc / "w64devkit2" / "w64devkit" / "bin" / "g++.exe"
    src = ROOT / "tools" / "nvrtc_compile.cpp"
    out = tc / "nvrtc_compile.exe"
    if out.is_file():
        return
    log("  build nvrtc_compile.exe")
    subprocess.run([str(gxx), "-O2", "-std=c++17", str(src), "-o", str(out)], check=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--offline", action="store_true", help="only use cached toolchain")
    ap.add_argument("--check", action="store_true", help="print status and exit")
    ap.add_argument("--toolchain", type=pathlib.Path, default=DEFAULT_TC)
    args = ap.parse_args()
    tc = args.toolchain.resolve()
    paths = requirement_paths(tc)
    miss = missing(paths)
    if args.check:
        for k, v in paths.items():
            log(f"{'OK ' if v.is_file() else 'MISS'} {k:7s} {v}")
        return 0 if not miss else 1
    if not miss:
        log(f"toolchain ready: {tc}")
        return 0
    if args.offline:
        log("offline: toolchain is incomplete:")
        for k in miss:
            log(f"  missing {k}: {paths[k]}")
        return 2
    log(f"bootstrapping toolchain in {tc}")
    if not paths["g++"].is_file() or not paths["windres"].is_file():
        extract_w64(tc)
    ensure_wheels(tc, args.offline)
    build_helper(tc)
    miss = missing(paths)
    if miss:
        log("bootstrap failed; still missing:")
        for k in miss:
            log(f"  {k}: {paths[k]}")
        return 3
    log(f"toolchain ready: {tc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
