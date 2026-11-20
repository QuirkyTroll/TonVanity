param([switch]$Offline)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj = $root
$dist = Join-Path (Split-Path -Parent $root) 'TonVanity'
$tc = Join-Path (Split-Path -Parent $root) '.work\toolchain'
$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) { throw 'Python 3 is required for the build bootstrap (build-only).' }
$boot = @((Join-Path $proj 'tools\bootstrap.py'))
if ($Offline) { $boot += '--offline' }
& python @boot
if ($LASTEXITCODE -ne 0) { throw 'toolchain bootstrap failed (use .\build.ps1 without -Offline on first run)' }
$gxx = Join-Path $tc 'w64devkit2\w64devkit\bin\g++.exe'
$nvrtc = Join-Path $tc 'cuda\nvidia\cuda_nvrtc\bin'
$helper = Join-Path $tc 'nvrtc_compile.exe'
$ptxas = Join-Path $tc 'cuda\nvidia\cuda_nvcc\bin\ptxas.exe'
$windres = Join-Path $tc 'w64devkit2\w64devkit\bin\windres.exe'
$work = Join-Path (Split-Path -Parent $root) '.work'
$gen = Join-Path $work 'gen'
New-Item -ItemType Directory -Force $dist, $gen | Out-Null
$src = Join-Path $proj 'src'
$native = Join-Path $src 'native'
# The OpenCL program is compiled by the driver at runtime. Embed its source
# in the DLL so the dist folder stays self-contained.
$clsrc = Join-Path $native 'kernels_opencl.cl'
$clhdr = Join-Path $gen 'opencl_src.h'
$cltext = [System.IO.File]::ReadAllText($clsrc)
$cltext = 'static const char OPENCL_SRC[] = R"TNVOPENCL(' + "`r`n" + $cltext + "`r`n" + ')TNVOPENCL";' + "`r`n"
[System.IO.File]::WriteAllText($clhdr, $cltext, (New-Object System.Text.UTF8Encoding($false)))
if (-not (Test-Path $gxx)) { throw "w64devkit not found at $gxx" }
# CUDA blades are expected in $work (already built); build if missing.
$blobs = Join-Path $gen 'blobs.o'
$rc = Join-Path $gen 'blobs.rc'
$cubins = @(
  @{ id=100; file=(Join-Path $work 'kernels_sm75.cubin') },
  @{ id=101; file=(Join-Path $work 'kernels_sm86.cubin') },
  @{ id=102; file=(Join-Path $work 'kernels_sm89.cubin') },
  @{ id=103; file=(Join-Path $work 'kernels_sm120.cubin') },
  @{ id=104; file=(Join-Path $work 'kernels_compute_75.ptx') },
  @{ id=105; file=(Join-Path $work 'kernels_compute_120.ptx') }
)
$need = $false
foreach ($b in $cubins) { if (-not (Test-Path $b.file)) { $need = $true } }
if ($need) {
  $tmp = Join-Path $work 'cuda_compat'
  & $helper $nvrtc (Join-Path $native 'ed25519') (Join-Path $native 'cuda_compat') (Join-Path $native 'kernels.cu') (Join-Path $work 'kernels') compute_75 compute_86 compute_89 compute_120
  if ($LASTEXITCODE -ne 0) { throw 'NVRTC failed' }
  foreach ($a in @(@('75','75'),@('86','86'),@('89','89'),@('120','120'))) {
    & $ptxas "-arch=sm_$($a[1])" -o (Join-Path $work "kernels_sm$($a[1]).cubin") (Join-Path $work "kernels_compute_$($a[0]).ptx")
    if ($LASTEXITCODE -ne 0) { throw "ptxas sm$($a[1]) failed" }
  }
  $lines = foreach ($b in $cubins) { $p = ([System.IO.Path]::GetFullPath($b.file)).Replace('\','\\'); "$($b.id) RCDATA \`"$p\`"" }
  Set-Content -Encoding ASCII $rc ($lines -join "`n")
  & $windres -i $rc -o $blobs -O coff
  if ($LASTEXITCODE -ne 0) { throw 'windres failed' }
}
$common = @('-O3','-std=c++17','-Wno-trigraphs','-static','-static-libgcc','-static-libstdc++','-I', $native, '-I', (Join-Path $native 'ed25519'), '-I', $gen)
$srcs = @(
  (Join-Path $native 'tonvanity.cpp'),
  (Join-Path $native 'ton_crypto.cpp'),
  (Join-Path $native 'ed25519_cpu.cpp'),
  (Join-Path $native 'backend_cpu.cpp'),
  (Join-Path $native 'backend_cuda.cpp'),
  (Join-Path $native 'backend_opencl.cpp')
)
& $gxx @common -shared '-Wl,--export-all-symbols' ("-Wl,--out-implib," + (Join-Path $dist 'libtonvanity.a')) @srcs $blobs -lbcrypt -o (Join-Path $dist 'tonvanity.dll')
if ($LASTEXITCODE -ne 0) { throw 'DLL build failed' }
& $gxx @common (Join-Path $src 'ui\main.cpp') '-L' $dist '-ltonvanity' -o (Join-Path $dist 'TonVanity.exe')
if ($LASTEXITCODE -ne 0) { throw 'EXE build failed' }
Copy-Item (Join-Path $tc 'cuda\nvidia\cuda_runtime\bin\cudart64_12.dll') $dist -Force
Remove-Item (Join-Path $dist 'libtonvanity.a') -ErrorAction SilentlyContinue
& (Join-Path $dist 'TonVanity.exe') --selftest
if ($LASTEXITCODE -ne 0) { throw 'selftest failed' }
& (Join-Path $proj 'ui\build-ui.ps1') -Offline:$Offline
Get-ChildItem $dist | Select-Object Name, Length

