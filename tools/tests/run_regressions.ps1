[CmdletBinding()]
param([string]$PlatformToolset = 'v145', [switch]$Sanitizer, [string]$MsBuild = 'msbuild')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$suites = @('debug_logic','ui_smoke','target_runtime','physics_shapes','primary_keyboard','release_update','dma_runtime','config_runtime','maps','asset_runtime')
if ($Sanitizer) { $suites = @('debug_logic','target_runtime','config_runtime','maps') }
Push-Location $root
try {
    foreach ($suite in $suites) {
        $project = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot $suite) -Filter '*.vcxproj' | Select-Object -First 1
        if (-not $project) { throw "Missing test project: $suite" }
        $arguments = @($project.FullName,'/m:2','/p:Configuration=Release','/p:Platform=x64',"/p:PlatformToolset=$PlatformToolset",'/v:minimal','/nologo')
        if ($Sanitizer) {
            $arguments += "/p:ForceImportAfterCppTargets=$PSScriptRoot\asan.props"
            $arguments += "/p:OutDir=$env:LOCALAPPDATA\KevqDMA\Build\tests\asan\$($project.BaseName)\"
            $arguments += "/p:IntDir=$env:LOCALAPPDATA\KevqDMA\Build\obj\asan\$($project.BaseName)\"
            $toolchain = (& $MsBuild $project.FullName /p:Configuration=Release /p:Platform=x64 "/p:PlatformToolset=$PlatformToolset" /getProperty:VCToolsInstallDir).Trim()
            $runtimeDir = Join-Path $toolchain 'bin\Hostx64\x64'
            if (-not (Test-Path (Join-Path $runtimeDir 'clang_rt.asan_dynamic-x86_64.dll'))) { throw 'MSVC AddressSanitizer runtime is not installed' }
            $env:PATH = "$runtimeDir;$env:PATH"
        }
        & $MsBuild @arguments
        if ($LASTEXITCODE) { throw "Build failed: $suite ($LASTEXITCODE)" }
        $folder = Join-Path $env:LOCALAPPDATA 'KevqDMA\Build\tests\Release'
        if ($Sanitizer) { $folder = Join-Path $env:LOCALAPPDATA "KevqDMA\Build\tests\asan\$($project.BaseName)" }
        if ($suite -eq 'dma_runtime' -and -not $Sanitizer) { $folder = Join-Path $env:LOCALAPPDATA 'KevqDMA\Build\tests\dma_runtime' }
        $binary = Join-Path $folder ($project.BaseName + '.exe')
        if (-not (Test-Path -LiteralPath $binary)) { throw "Missing test binary: $binary" }
        & $binary
        if ($LASTEXITCODE) { throw "Tests failed: $suite ($LASTEXITCODE)" }
    }
    if (-not $Sanitizer) {
        & $MsBuild tools/tests/target_runtime/target_runtime_tests.vcxproj /m:2 /p:Configuration=Release /p:Platform=x64 "/p:PlatformToolset=$PlatformToolset" /p:TargetInputIntegration=true /v:minimal /nologo
        if ($LASTEXITCODE) { throw 'Target/input build failed' }
        & (Join-Path $env:LOCALAPPDATA 'KevqDMA\Build\tests\Release\target_input_tests.exe')
        if ($LASTEXITCODE) { throw 'Target/input tests failed' }
    }
} finally { Pop-Location }
