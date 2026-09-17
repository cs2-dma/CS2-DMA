[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$projectPath = Join-Path $projectRoot 'KevqDMA.vcxproj'
$filtersPath = Join-Path $projectRoot 'KevqDMA.vcxproj.filters'
$solutionPath = Join-Path $projectRoot 'KevqDMA.slnx'
$offsetHeaderPath = Join-Path $projectRoot 'include\Game\Offsets\runtime_offsets.h'
$offsetSourcePath = Join-Path $projectRoot 'src\Game\Offsets\runtime_offsets.cpp'
$remoteFieldsPath = Join-Path $projectRoot 'src\Game\Offsets\runtime_offsets_parts\runtime_offsets_remote_fields.inl'
$configSavePath = Join-Path $projectRoot 'src\app\Config\config_parts\config_save_json_body.inl'
$configLoadPath = Join-Path $projectRoot 'src\app\Config\config_parts\config_apply_json_body.inl'
$resourceManifestPath = Join-Path $projectRoot 'src\Features\WebRadar\Assets\webradar_resources.rc'
$buildInfoPath = Join-Path $projectRoot 'include\app\Core\build_info.h'

function Read-MsBuildItems {
    param(
        [Parameter(Mandatory)] [string] $Path
    )

    [xml] $document = Get-Content -LiteralPath $Path -Raw
    $namespace = New-Object System.Xml.XmlNamespaceManager($document.NameTable)
    $namespace.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')

    $items = @()
    foreach ($itemType in 'ClCompile', 'ClInclude', 'ResourceCompile', 'None') {
        foreach ($node in $document.SelectNodes("//m:$itemType", $namespace)) {
            if ($node.Include) {
                $items += [PSCustomObject]@{
                    Type = $itemType
                    Path = [IO.Path]::GetFullPath((Join-Path $projectRoot $node.Include))
                }
            }
        }
    }
    return $items
}

function Get-RegexCaptures {
    param(
        [Parameter(Mandatory)] [string] $Text,
        [Parameter(Mandatory)] [string] $Pattern
    )

    return [regex]::Matches($Text, $Pattern) |
        ForEach-Object { $_.Groups[1].Value } |
        Sort-Object -Unique
}

$errors = [Collections.Generic.List[string]]::new()
$projectItems = @(Read-MsBuildItems -Path $projectPath)
$filterItems = @(Read-MsBuildItems -Path $filtersPath)

foreach ($item in $projectItems) {
    if (-not (Test-Path -LiteralPath $item.Path -PathType Leaf)) {
        $errors.Add("Project item does not exist: $($item.Path)")
    }
}

$projectItemKeys = @($projectItems | ForEach-Object { "$($_.Type)|$($_.Path)" })
$filterItemKeys = @($filterItems | ForEach-Object { "$($_.Type)|$($_.Path)" })
foreach ($difference in Compare-Object $projectItemKeys $filterItemKeys) {
    $side = if ($difference.SideIndicator -eq '<=') { 'missing from filters' } else { 'stale in filters' }
    $errors.Add("Project/filter mismatch ($side): $($difference.InputObject)")
}

$excludedVendorSources = @(
    'src\vendor\libssh2\src\agent_win.c',
    'src\vendor\libssh2\src\blowfish.c',
    'src\vendor\libssh2\src\libgcrypt.c',
    'src\vendor\libssh2\src\mbedtls.c',
    'src\vendor\libssh2\src\openssl.c',
    'src\vendor\libssh2\src\os400qc3.c',
    'src\vendor\libssh2\src\wincng.c'
) | ForEach-Object { [IO.Path]::GetFullPath((Join-Path $projectRoot $_)) }

$listedSources = @($projectItems |
    Where-Object Type -eq 'ClCompile' |
    ForEach-Object Path)
$actualSources = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'src') -Recurse -File |
    Where-Object Extension -in '.c', '.cpp' |
    ForEach-Object FullName |
    Where-Object { $_ -notin $excludedVendorSources })

foreach ($difference in Compare-Object $listedSources $actualSources) {
    $side = if ($difference.SideIndicator -eq '<=') { 'listed but unexpected' } else { 'not compiled' }
    $errors.Add("Source manifest mismatch ($side): $($difference.InputObject)")
}

# Device-selected movement must never fall back to synthetic local Windows input.
$firstPartyCode = @(
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'include') -Recurse -File
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'src') -Recurse -File |
        Where-Object FullName -NotLike "$(Join-Path $projectRoot 'src\vendor')*"
) | Where-Object Extension -in '.c', '.cpp', '.h', '.hpp', '.inl'
foreach ($file in $firstPartyCode) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    if ($text -match '\b(?:SendInput|mouse_event|SetCursorPos)\s*\(') {
        $errors.Add("Local mouse injection is forbidden; route movement through the selected device: $($file.FullName)")
    }
}

$listedFirstPartyHeaders = @($projectItems |
    Where-Object {
        $_.Type -eq 'ClInclude' -and
        $_.Path -notmatch '[\\/]src[\\/]vendor[\\/]'
    } |
    ForEach-Object Path)
$actualFirstPartyHeaders = @(Get-ChildItem `
        -LiteralPath (Join-Path $projectRoot 'include'), (Join-Path $projectRoot 'src') `
        -Recurse -File |
    Where-Object {
        $_.Extension -in '.h', '.hpp', '.inl' -and
        $_.FullName -notmatch '[\\/]src[\\/]vendor[\\/]'
    } |
    ForEach-Object FullName)
foreach ($difference in Compare-Object $listedFirstPartyHeaders $actualFirstPartyHeaders) {
    $side = if ($difference.SideIndicator -eq '<=') { 'listed but unexpected' } else { 'not listed' }
    $errors.Add("Header manifest mismatch ($side): $($difference.InputObject)")
}

$resourceManifestText = Get-Content -LiteralPath $resourceManifestPath -Raw
$embeddedResourcePaths = @([regex]::Matches(
        $resourceManifestText,
        '(?m)^\S+\s+(?:RCDATA|ICON)\s+"([^"]+)"') |
    ForEach-Object {
        [IO.Path]::GetFullPath((Join-Path $projectRoot $_.Groups[1].Value))
    } |
    Sort-Object -Unique)
$expectedEmbeddedResourcePaths = @(
    Join-Path $projectRoot 'src\assets\app.ico'
    Join-Path $projectRoot 'src\assets\fonts\bootstrap-icons.ttf'
    Join-Path $projectRoot 'src\assets\locales\en.json'
    Join-Path $projectRoot 'src\assets\locales\zh-CN.json'
) + @(Get-ChildItem `
        -LiteralPath (Join-Path $projectRoot 'src\assets\world') `
        -File |
    Where-Object Extension -eq '.json' |
    ForEach-Object FullName
) + @(Get-ChildItem `
        -LiteralPath (Join-Path $projectRoot 'src\assets\weapon_icons') `
        -File |
    Where-Object Extension -in '.png', '.rgba' |
    ForEach-Object FullName
) + @(Get-ChildItem `
        -LiteralPath (Join-Path $projectRoot 'src\Features\WebRadar\Assets') `
        -Recurse -File |
    Where-Object Extension -ne '.rc' |
    ForEach-Object FullName)
$expectedEmbeddedResourcePaths = @($expectedEmbeddedResourcePaths |
    ForEach-Object { [IO.Path]::GetFullPath($_) } |
    Sort-Object -Unique)
foreach ($difference in Compare-Object $embeddedResourcePaths $expectedEmbeddedResourcePaths) {
    $side = if ($difference.SideIndicator -eq '<=') { 'stale manifest entry' } else { 'not embedded' }
    $errors.Add("Embedded resource mismatch ($side): $($difference.InputObject)")
}

$grenadeDatabasePath = Join-Path $projectRoot 'src\assets\world\grenade_helper.json'
$chineseCatalogPath = Join-Path $projectRoot 'src\assets\locales\zh-CN.json'
$grenadeDatabase = Get-Content -LiteralPath $grenadeDatabasePath -Raw | ConvertFrom-Json
$chineseCatalogText = Get-Content -LiteralPath $chineseCatalogPath -Raw
$translatedCallouts = [Collections.Generic.HashSet[string]]::new()
foreach ($match in [regex]::Matches(
        $chineseCatalogText,
        '(?m)^\s*"Grenade callout: ([^"]+)"\s*:')) {
    [void]$translatedCallouts.Add($match.Groups[1].Value)
}
$requiredCallouts = [Collections.Generic.HashSet[string]]::new()
foreach ($map in $grenadeDatabase.maps) {
    foreach ($spot in $map.spots) {
        foreach ($aimPoint in $spot.aim_points) {
            $parts = @($aimPoint.label -split ' From ', 2)
            foreach ($part in $parts) {
                [void]$requiredCallouts.Add($part)
            }
        }
    }
}
foreach ($callout in $requiredCallouts) {
    if (-not $translatedCallouts.Contains($callout)) {
        $errors.Add("Grenade callout translation is missing: $callout")
    }
}
foreach ($callout in $translatedCallouts) {
    if (-not $requiredCallouts.Contains($callout)) {
        $errors.Add("Grenade callout translation is stale: $callout")
    }
}

$buildInfoText = Get-Content -LiteralPath $buildInfoPath -Raw
foreach ($versionDeclaration in @(
        '#define KEVQDMA_VERSION_NUMERIC 1,0,6,0'
        '#define KEVQDMA_VERSION_STRING "1.0.6"'
        '#define KEVQDMA_VERSION_TAG "v" KEVQDMA_VERSION_STRING'
        'inline constexpr std::string_view kVersionTag = KEVQDMA_VERSION_TAG;')) {
    if (-not $buildInfoText.Contains($versionDeclaration)) {
        $errors.Add("Application version declaration is missing: $versionDeclaration")
    }
}
foreach ($resourceVersionUse in @(
        'FILEVERSION KEVQDMA_VERSION_NUMERIC'
        'PRODUCTVERSION KEVQDMA_VERSION_NUMERIC'
        'VALUE "FileVersion", KEVQDMA_VERSION_STRING "\0"'
        'VALUE "ProductVersion", KEVQDMA_VERSION_STRING "\0"')) {
    if (-not $resourceManifestText.Contains($resourceVersionUse)) {
        $errors.Add("Windows version resource is missing: $resourceVersionUse")
    }
}

[xml] $solution = Get-Content -LiteralPath $solutionPath -Raw
$solutionProject = $solution.Solution.Project
if ($null -eq $solutionProject -or $solutionProject.Path -ne 'KevqDMA.vcxproj') {
    $errors.Add('KevqDMA.slnx does not reference KevqDMA.vcxproj.')
}

[xml] $project = Get-Content -LiteralPath $projectPath -Raw
$projectNamespace = New-Object System.Xml.XmlNamespaceManager($project.NameTable)
$projectNamespace.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
$projectGuid = $project.SelectSingleNode('//m:ProjectGuid', $projectNamespace).'#text'
$normalizedSolutionGuid = $solutionProject.Id.Trim().Trim('{', '}').ToLowerInvariant()
$normalizedProjectGuid = $projectGuid.Trim().Trim('{', '}').ToLowerInvariant()
if ($normalizedSolutionGuid -ne $normalizedProjectGuid) {
    $errors.Add("Solution/project GUID mismatch: '$($solutionProject.Id)' != '$projectGuid'.")
}

$valuesText = Get-Content -LiteralPath $offsetHeaderPath -Raw
$offsetSourceText = Get-Content -LiteralPath $offsetSourcePath -Raw
$remoteFieldsText = Get-Content -LiteralPath $remoteFieldsPath -Raw
$valueMembers = @(Get-RegexCaptures -Text $valuesText -Pattern 'std::ptrdiff_t\s+([A-Za-z0-9_]+)\s*=')
$serializedMembers = @(Get-RegexCaptures -Text $offsetSourceText -Pattern '&runtime_offsets::Values::([A-Za-z0-9_]+)')
$remoteMembers = @(Get-RegexCaptures -Text $remoteFieldsText -Pattern '&runtime_offsets::Values::([A-Za-z0-9_]+)')

foreach ($difference in Compare-Object $valueMembers $serializedMembers) {
    $side = if ($difference.SideIndicator -eq '<=') { 'not serialized' } else { 'unknown serialized member' }
    $errors.Add("Offset field mismatch ($side): $($difference.InputObject)")
}
foreach ($difference in Compare-Object $valueMembers $remoteMembers) {
    $side = if ($difference.SideIndicator -eq '<=') { 'not remotely mapped' } else { 'unknown remote member' }
    $errors.Add("Remote offset field mismatch ($side): $($difference.InputObject)")
}

$configGlobalPattern = '(?<![A-Za-z0-9_])g::([A-Za-z0-9_]+)'
$savedConfigGlobals = @(Get-RegexCaptures `
    -Text (Get-Content -LiteralPath $configSavePath -Raw) `
    -Pattern $configGlobalPattern)
$loadedConfigGlobals = @(Get-RegexCaptures `
    -Text (Get-Content -LiteralPath $configLoadPath -Raw) `
    -Pattern $configGlobalPattern)
foreach ($difference in Compare-Object $savedConfigGlobals $loadedConfigGlobals) {
    $side = if ($difference.SideIndicator -eq '<=') { 'saved but not loaded' } else { 'loaded but not saved' }
    $errors.Add("Config field mismatch ($side): $($difference.InputObject)")
}

$vendorVersionChecks = @(
    @{
        Path = 'src\vendor\imgui\imgui.h'
        Pattern = '#define IMGUI_VERSION       "1.92.9"'
        Name = 'Dear ImGui 1.92.9'
    },
    @{
        Path = 'src\vendor\json\json.hpp'
        Pattern = '#define NLOHMANN_JSON_VERSION_MAJOR 3'
        Name = 'nlohmann/json major version 3'
    },
    @{
        Path = 'src\vendor\json\json.hpp'
        Pattern = '#define NLOHMANN_JSON_VERSION_MINOR 12'
        Name = 'nlohmann/json minor version 12'
    },
    @{
        Path = 'src\vendor\json\json.hpp'
        Pattern = '#define NLOHMANN_JSON_VERSION_PATCH 0'
        Name = 'nlohmann/json patch version 0'
    },
    @{
        Path = 'src\vendor\libssh2\include\libssh2.h'
        Pattern = '#define LIBSSH2_VERSION "1.11.1"'
        Name = 'libssh2 1.11.1'
    }
)
foreach ($check in $vendorVersionChecks) {
    $path = Join-Path $projectRoot $check.Path
    $content = Get-Content -LiteralPath $path -Raw
    if (-not $content.Contains($check.Pattern)) {
        $errors.Add("Vendor version check failed: $($check.Name) in $path")
    }
}

$vendorLicensePaths = @(
    'src\vendor\imgui\LICENSE.txt',
    'src\vendor\json\LICENSE.MIT',
    'src\vendor\libssh2\COPYING',
    'src\vendor\qrcodegen\LICENSE.txt'
)
foreach ($relativePath in $vendorLicensePaths) {
    $path = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $errors.Add("Vendor license is missing: $path")
    }
}

$forbiddenDirectories = @(
    'x64\debug_tests',
    'x64\obj',
    'x64\Release_verify',
    'x64\Release\locales'
)
foreach ($relativePath in $forbiddenDirectories) {
    $path = Join-Path $projectRoot $relativePath
    if (Test-Path -LiteralPath $path) {
        $errors.Add("Forbidden build artifact exists: $path")
    }
}

if ($errors.Count -ne 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output (
    'project audit passed: {0} manifest items, {1} compiled sources, {2} first-party headers, {3} embedded resources, {4} offset fields, {5} config fields' -f
    $projectItems.Count,
    $listedSources.Count,
    $listedFirstPartyHeaders.Count,
    $embeddedResourcePaths.Count,
    $valueMembers.Count,
    $savedConfigGlobals.Count
)
