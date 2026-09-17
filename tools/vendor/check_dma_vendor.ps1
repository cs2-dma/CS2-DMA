[CmdletBinding()]
param(
    [ValidateSet('Verify', 'Deploy')] [string] $Mode = 'Verify',
    [string] $RuntimeDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$vendorRoot = Join-Path $projectRoot 'src\vendor\DMALibrary'
$manifest = Get-Content -LiteralPath (Join-Path $vendorRoot 'vendor-lock.json') -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or $manifest.platform -ne 'win-x64') {
    throw 'Unsupported DMA vendor manifest.'
}

function Get-VendorHash {
    param([string] $Path)
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Assert-VendorFile {
    param([string] $Path, $Entry)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing vendor file: $Path" }
    if ((Get-VendorHash $Path) -ne $Entry.sha256) {
        throw "DMA vendor hash mismatch: $Path. Restore the pinned bundle; do not mix SDK/runtime versions."
    }
    if ($Entry.runtimeName) {
        $bytes = [IO.File]::ReadAllBytes($Path)
        if ($bytes.Length -lt 64 -or [BitConverter]::ToUInt16($bytes, 0) -ne 0x5A4D) { throw "Invalid PE: $Path" }
        $pe = [BitConverter]::ToInt32($bytes, 60)
        if ($pe -lt 64 -or $pe -gt $bytes.Length - 6 -or
            [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550 -or
            [BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664) {
            throw "Expected x64 DLL: $Path"
        }
        if ((Get-Item -LiteralPath $Path).VersionInfo.FileVersion -ne $Entry.fileVersion) {
            throw "Unexpected DLL file version: $Path"
        }
    }
}

$checked = @()
$expectedPaths = @('libs/vmmdll.h', 'libs/leechcore.h', 'libs/vmm.lib', 'libs/leechcore.lib',
    'runtime/win-x64/vmm.dll', 'runtime/win-x64/leechcore.dll',
    'runtime/win-x64/FTD3XX.dll', 'runtime/win-x64/FTD3XXWU.dll')
$actualPaths = @($manifest.files | ForEach-Object { $_.path })
if ($actualPaths.Count -ne $expectedPaths.Count -or
    @(Compare-Object $expectedPaths $actualPaths).Count -ne 0) {
    throw 'DMA vendor manifest has missing, duplicate or unexpected paths.'
}
foreach ($entry in $manifest.files) {
    if ($entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid SHA-256 in vendor manifest.' }
    $source = [IO.Path]::GetFullPath((Join-Path $vendorRoot $entry.path))
    if (-not $source.StartsWith($vendorRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Vendor manifest path escapes the vendor directory.'
    }
    if ($entry.runtimeName -and $entry.runtimeName -notin @('vmm.dll', 'leechcore.dll', 'FTD3XX.dll', 'FTD3XXWU.dll')) {
        throw 'Unexpected runtime file in vendor manifest.'
    }
    Assert-VendorFile $source $entry
    $checked += [PSCustomObject]@{ Source = $source; Entry = $entry }
}
if ($checked.Count -ne 8 -or @($checked | Where-Object { $_.Entry.runtimeName }).Count -ne 4) {
    throw 'Incomplete DMA vendor manifest.'
}

if ($Mode -eq 'Deploy' -and -not $RuntimeDirectory) { throw 'Deploy requires RuntimeDirectory.' }
if ($RuntimeDirectory) {
    $runtimeRoot = [IO.Path]::GetFullPath($RuntimeDirectory).TrimEnd('\', '/')
    $outputRoot = Join-Path $projectRoot 'x64'
    if (-not $runtimeRoot.StartsWith($outputRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Runtime directory must be an x64 build output within this project.'
    }
    if ($Mode -eq 'Deploy') {
        New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
    }
    foreach ($item in $checked) {
        if (-not $item.Entry.runtimeName) { continue }
        $destination = Join-Path $runtimeRoot $item.Entry.runtimeName
        if ($Mode -eq 'Deploy') {
            $matches = (Test-Path -LiteralPath $destination -PathType Leaf) -and
                (Get-VendorHash $destination) -eq $item.Entry.sha256
            if (-not $matches) {
                Copy-Item -LiteralPath $item.Source -Destination $destination -Force
            }
        }
        Assert-VendorFile $destination $item.Entry
    }
}
Write-Host "DMA vendor OK: MemProcFS $($manifest.memprocfsVersion), LeechCore $($manifest.leechcoreVersion), x64; 8 hashes verified."
