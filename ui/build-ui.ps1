param([switch]$Offline)
$ErrorActionPreference = 'Stop'
$ui = Split-Path -Parent $MyInvocation.MyCommand.Path
$projRoot = Split-Path -Parent $ui
$workspace = Split-Path -Parent $projRoot
$dist = Join-Path $workspace 'TonVanity'
$uiDist = Join-Path $dist 'ui'
$stage = Join-Path $workspace '.work\ui-publish'
$project = Join-Path $ui 'TonVanity.App.csproj'
$nativeDll = Join-Path $dist 'tonvanity.dll'
$cudartDll = Join-Path $dist 'cudart64_12.dll'

if (-not (Test-Path $nativeDll) -or -not (Test-Path $cudartDll)) {
    throw 'Native TonVanity files are missing; run the console build first.'
}

$dotnet = $null
$dotnetCommand = Get-Command dotnet -ErrorAction SilentlyContinue
if ($dotnetCommand) {
    $sdks = & $dotnetCommand.Source --list-sdks 2>$null
    if ($sdks -match '(^|\s)8\.') {
        $dotnet = $dotnetCommand.Source
    }
}

if (-not $dotnet) {
    $sdkDir = Join-Path $workspace '.work\dotnet'
    $sdkExe = Join-Path $sdkDir 'dotnet.exe'
    if (Test-Path $sdkExe) {
        $dotnet = $sdkExe
    } else {
        if ($Offline) {
            throw '.NET 8 SDK not found and -Offline was requested.'
        }
        $python = Get-Command python -ErrorAction SilentlyContinue
        if (-not $python) {
            throw 'Python 3 is required to download the .NET 8 SDK.'
        }
        $zip = Join-Path $workspace '.work\dotnet-sdk-8.0.428-win-x64.zip'
        if (-not (Test-Path $zip)) {
            New-Item -ItemType Directory -Force (Split-Path -Parent $zip) | Out-Null
            & $python.Source -c "import sys,urllib.request;urllib.request.urlretrieve(sys.argv[1],sys.argv[2])" 'https://dotnetcli.azureedge.net/dotnet/Sdk/8.0.428/dotnet-sdk-8.0.428-win-x64.zip' $zip
            if ($LASTEXITCODE -ne 0) { throw '.NET SDK download failed.' }
        }
        New-Item -ItemType Directory -Force $sdkDir | Out-Null
        Expand-Archive -LiteralPath $zip -DestinationPath $sdkDir -Force
        if (-not (Test-Path $sdkExe)) { throw '.NET SDK extraction failed.' }
        $dotnet = $sdkExe
    }
}

$env:DOTNET_ROOT = Split-Path -Parent $dotnet
$env:PATH = (Split-Path -Parent $dotnet) + ';' + $env:PATH
$env:DOTNET_CLI_HOME = Join-Path $workspace '.work\dotnet-home'
$env:NUGET_PACKAGES = Join-Path $workspace '.work\nuget'
$env:APPDATA = Join-Path $workspace '.work\appdata'
$env:LOCALAPPDATA = Join-Path $workspace '.work\localappdata'
$env:TEMP = Join-Path $workspace '.work\temp'
$env:TMP = $env:TEMP
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$env:DOTNET_NOLOGO = '1'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
New-Item -ItemType Directory -Force $env:DOTNET_CLI_HOME, $env:NUGET_PACKAGES, $env:APPDATA, $env:LOCALAPPDATA, $env:TEMP | Out-Null

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    throw 'Python 3 is required for the NuGet package bootstrap.'
}
$localFeed = Join-Path $workspace '.work\nuget-local'
New-Item -ItemType Directory -Force $localFeed | Out-Null
$packages = @(
    'microsoft.windowsappsdk/1.7.260224002',
    'microsoft.windows.sdk.buildtools/10.0.22621.756',
    'microsoft.web.webview2/1.0.2903.40',
    'microsoft.windows.sdk.net.ref/10.0.19041.56',
    'microsoft.netcore.app.runtime.win-x64/8.0.34',
    'microsoft.netcore.app.host.win-x64/8.0.34',
    'microsoft.net.illink.tasks/8.0.34',
    'microsoft.windowsdesktop.app.runtime.win-x64/8.0.34',
    'microsoft.aspnetcore.app.runtime.win-x64/8.0.34'
)
foreach ($spec in $packages) {
    $parts = $spec.Split('/')
    $id = $parts[0]
    $version = $parts[1]
    $file = Join-Path $localFeed "$id.$version.nupkg"
    if (-not (Test-Path $file)) {
        $url = "https://api.nuget.org/v3-flatcontainer/$id/$version/$id.$version.nupkg"
        & $python.Source -c "import sys,urllib.request;urllib.request.urlretrieve(sys.argv[1],sys.argv[2])" $url $file
        if ($LASTEXITCODE -ne 0) { throw "NuGet download failed: $spec" }
    }
}
$nugetConfig = Join-Path $workspace '.work\NuGet.Config'
$configXml = @"
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <clear />
    <add key="local" value="$localFeed" />
  </packageSources>
</configuration>
"@
[System.IO.File]::WriteAllText($nugetConfig, $configXml, (New-Object System.Text.UTF8Encoding($false)))

Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

& $dotnet restore $project -r win-x64 --configfile $nugetConfig
if ($LASTEXITCODE -ne 0) { throw 'dotnet restore failed.' }
$nugetTargets = Join-Path $ui 'obj\TonVanity.App.csproj.nuget.g.targets'
if (Test-Path $nugetTargets) {
    $targetText = [System.IO.File]::ReadAllText($nugetTargets)
    if ($targetText -notmatch 'GetMrtPackagingOutputs') {
        $override = @'
  <Target Name="GetMrtPackagingOutputs" Returns="@(PackagingOutputs)" />
  <Target Name="CopyLocalFilesOutputGroup" Returns="@(CopyLocalFilesOutputGroupOutput)">
    <ItemGroup>
      <CopyLocalFilesOutputGroupOutput Include="@(ReferenceCopyLocalPaths)">
        <TargetPath>%(ReferenceCopyLocalPaths.DestinationSubDirectory)%(ReferenceCopyLocalPaths.Filename)%(ReferenceCopyLocalPaths.Extension)</TargetPath>
      </CopyLocalFilesOutputGroupOutput>
    </ItemGroup>
  </Target>
  <Target Name="_GetDefaultResourceLanguage"><PropertyGroup><DefaultResourceLanguage>en-US</DefaultResourceLanguage></PropertyGroup></Target>
  <Target Name="_GetSdkToolPaths" />
  <Target Name="_GenerateProjectPriFile" />
'@
        $targetText = $targetText.Replace('</Project>', $override + '</Project>')
        [System.IO.File]::WriteAllText($nugetTargets, $targetText, (New-Object System.Text.UTF8Encoding($false)))
    }
}
& $dotnet publish $project -c Release -r win-x64 --self-contained true --no-restore -p:WindowsAppSDKSelfContained=true -p:PublishSingleFile=false -p:DebugType=None -p:DebugSymbols=false -o $stage
if ($LASTEXITCODE -ne 0) { throw 'dotnet publish failed.' }

$publishedExe = Join-Path $stage 'TonVanityUI.exe'
if (Test-Path $publishedExe) {
    Rename-Item -LiteralPath $publishedExe -NewName 'TonVanity.exe'
}

Copy-Item $nativeDll $stage -Force
Copy-Item $cudartDll $stage -Force
$binDir = Join-Path $ui 'bin\Release\net8.0-windows10.0.19041.0\win-x64'
if (Test-Path $binDir) {
    Get-ChildItem $binDir -Filter '*.xbf' | Copy-Item -Destination $stage -Force
}
Get-ChildItem $stage -Recurse -File -Include *.pdb,*.xml,*.lib,*.exp | Remove-Item -Force
Remove-Item (Join-Path $stage 'app.manifest') -Force -ErrorAction SilentlyContinue
if (-not (Test-Path (Join-Path $stage 'TonVanity.exe'))) { throw 'TonVanity.exe was not produced.' }

$makepri = Get-ChildItem (Join-Path $env:NUGET_PACKAGES 'microsoft.windows.sdk.buildtools') -Recurse -Filter 'makepri.exe' |
    Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
if (-not $makepri) { throw 'makepri.exe was not found in the Windows SDK BuildTools package.' }
$priRoot = Join-Path $workspace '.work\pri-root'
$priConfig = Join-Path $workspace '.work\priconfig.xml'
Remove-Item $priRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $priRoot | Out-Null
Get-ChildItem $stage -Filter '*.xbf' | Copy-Item -Destination $priRoot -Force
Get-ChildItem $stage -Filter '*.pri' | Copy-Item -Destination $priRoot -Force
if (-not (Test-Path $priConfig)) {
    & $makepri.FullName createconfig /cf $priConfig /dq en-US /o
    if ($LASTEXITCODE -ne 0) { throw 'makepri createconfig failed.' }
}
& $makepri.FullName new /pr $priRoot /cf $priConfig /of (Join-Path $priRoot 'resources.pri') /o
if ($LASTEXITCODE -ne 0) { throw 'makepri failed.' }
Copy-Item (Join-Path $priRoot 'resources.pri') $stage -Force

$keepLanguages = @('en-us', 'ru-RU')
Get-ChildItem $stage -Directory |
    Where-Object { $_.Name -match '^[a-z]{2,3}(-[A-Za-z]+)*$' -and $_.Name -notin $keepLanguages } |
    Remove-Item -Recurse -Force

$removeFilePatterns = @(
    'Microsoft.WindowsAppRuntime.Bootstrap.dll',
    'Microsoft.WindowsAppRuntime.Bootstrap.Net.dll',
    'Microsoft.WindowsAppRuntime.Release.Net.dll',
    'WebView2Loader.dll',
    'Microsoft.Web.WebView2.Core.dll',
    'Microsoft.Web.WebView2.Core.Projection.dll',
    'Microsoft.UI.Designer.dll',
    'Microsoft.Windows.AI*',
    'Microsoft.Graphics.Imaging*',
    'Microsoft.Windows.Vision*',
    'Microsoft.Windows.Workloads*',
    'Microsoft.Windows.Widgets*',
    'workloads*.json',
    'onnxruntime.dll',
    'DirectML.dll',
    '*.ProxyStub.dll',
    'Microsoft.DiaSymReader.Native.amd64.dll',
    'mscordaccore.dll',
    'mscordaccore_amd64_*.dll',
    'mscordbi.dll',
    'createdump.exe',
    'Microsoft.UI.Xaml.Phone.dll'
)
foreach ($pattern in $removeFilePatterns) {
    Get-ChildItem $stage -File -Filter $pattern -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}
Remove-Item (Join-Path $stage 'Microsoft.UI.Xaml\Assets') -Recurse -Force -ErrorAction SilentlyContinue

Remove-Item $uiDist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $uiDist | Out-Null
Copy-Item (Join-Path $stage '*') $uiDist -Recurse -Force

Get-ChildItem (Join-Path $uiDist 'TonVanity.exe') | Select-Object Name, Length
Remove-Item (Join-Path $ui 'bin') -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $ui 'obj') -Recurse -Force -ErrorAction SilentlyContinue
