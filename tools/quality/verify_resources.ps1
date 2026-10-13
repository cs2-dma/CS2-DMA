[CmdletBinding()]
param([string]$Binary = 'x64\Release\KevqDMA.exe')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not [IO.Path]::IsPathRooted($Binary)) { $Binary = Join-Path $root $Binary }
$nativeLibrarySearchPath = [Environment]::GetEnvironmentVariable('LIB', 'Process')
try {
[Environment]::SetEnvironmentVariable('LIB', $null, 'Process')
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class KevqResourceReader {
  [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
  [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr FindResource(IntPtr module, string name, IntPtr type);
  [DllImport("kernel32")] public static extern uint SizeofResource(IntPtr module, IntPtr resource);
  [DllImport("kernel32")] public static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
  [DllImport("kernel32")] public static extern IntPtr LockResource(IntPtr resource);
  [DllImport("kernel32")] public static extern bool FreeLibrary(IntPtr module);
}
'@
} finally {
    [Environment]::SetEnvironmentVariable('LIB', $nativeLibrarySearchPath, 'Process')
}
$module = [KevqResourceReader]::LoadLibraryEx($Binary,[IntPtr]::Zero,34)
if ($module -eq [IntPtr]::Zero) { throw "Cannot read PE resources: $Binary" }
$hash = [Security.Cryptography.SHA256]::Create()
try {
    $manifest = Get-Content -Raw (Join-Path $root 'src\Features\WebRadar\Assets\webradar_resources.rc')
    $resources = [regex]::Matches($manifest,'(?m)^\s*(?<Name>"[^"\r\n]+"|[\w/.-]+)\s+RCDATA\s+"(?<Path>[^"\r\n]+)"')
    foreach ($item in $resources) {
        $name = $item.Groups['Name'].Value
        $resource = [KevqResourceReader]::FindResource($module,$name,[IntPtr]10)
        if ($resource -eq [IntPtr]::Zero) { throw "Missing embedded resource: $name" }
        $size = [KevqResourceReader]::SizeofResource($module,$resource)
        $actual = New-Object byte[] $size
        $pointer = [KevqResourceReader]::LockResource([KevqResourceReader]::LoadResource($module,$resource))
        [Runtime.InteropServices.Marshal]::Copy($pointer,$actual,0,$actual.Length)
        $source = [IO.File]::ReadAllBytes((Join-Path $root $item.Groups['Path'].Value))
        if ([BitConverter]::ToString($hash.ComputeHash($actual)) -ne [BitConverter]::ToString($hash.ComputeHash($source))) {
            throw "Outdated embedded resource: $name"
        }
    }
    "PE resources match sources: $($resources.Count) verified; executable was not started."
} finally {
    $hash.Dispose()
    [void][KevqResourceReader]::FreeLibrary($module)
}
