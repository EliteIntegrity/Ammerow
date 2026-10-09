# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

[CmdletBinding()]
param(
    [string]$PackageRoot = 'build-package/install',
    [string]$OutputPath = '',
    [string]$RootDirectoryName = '',
    [switch]$FlatRoot
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$versionPath = Join-Path $projectRoot 'version'
if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
    throw "Missing Ammerow version file: $versionPath"
}
$releaseVersion = [System.IO.File]::ReadAllText($versionPath).Trim()
if ($releaseVersion -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$') {
    throw "Invalid Ammerow version: $releaseVersion"
}
$defaultPackageName = "Ammerow-$releaseVersion-windows-x64"
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = "build-package/$defaultPackageName.zip"
}
if ($FlatRoot -and -not [string]::IsNullOrWhiteSpace($RootDirectoryName)) {
    throw 'RootDirectoryName cannot be combined with FlatRoot.'
}
if (-not $FlatRoot -and [string]::IsNullOrWhiteSpace($RootDirectoryName)) {
    $RootDirectoryName = $defaultPackageName
}

function Resolve-ProjectPath {
    param([Parameter(Mandatory)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $projectRoot $Path))
}

if (-not $FlatRoot -and
        $RootDirectoryName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
    throw "Unsafe portable root directory name: $RootDirectoryName"
}

$packagePath = Resolve-ProjectPath $PackageRoot
$outputZip = Resolve-ProjectPath $OutputPath
if (-not (Test-Path -LiteralPath $packagePath -PathType Container)) {
    throw "Release install not found: $packagePath"
}

$packagePrefix = $packagePath.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
    [System.IO.Path]::DirectorySeparatorChar
if ($outputZip.StartsWith($packagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The portable ZIP cannot be written inside the release install tree.'
}

$outputDirectory = Split-Path -Parent $outputZip
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$token = [Guid]::NewGuid().ToString('N')
$temporaryZip = Join-Path $outputDirectory ".$([System.IO.Path]::GetFileName($outputZip)).$token.tmp"
$verifyDirectory = Join-Path $outputDirectory ".portable-zip-verify-$token"

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

try {
    $sourceFiles = @(Get-ChildItem -LiteralPath $packagePath -Recurse -File |
        Sort-Object { $_.FullName.Substring($packagePrefix.Length) })
    if ($sourceFiles.Count -eq 0) {
        throw "Release install is empty: $packagePath"
    }

    $expectedEntries = @($sourceFiles | ForEach-Object {
        $relative = $_.FullName.Substring($packagePrefix.Length).Replace('\', '/')
        if ($FlatRoot) { $relative } else { "$RootDirectoryName/$relative" }
    })
    if ($FlatRoot -and $expectedEntries -notcontains 'ammerow.exe') {
        throw 'A flat Steam depot ZIP must contain ammerow.exe at its root.'
    }

    $archive = [System.IO.Compression.ZipFile]::Open(
        $temporaryZip, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        for ($i = 0; $i -lt $sourceFiles.Count; $i++) {
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive,
                $sourceFiles[$i].FullName,
                $expectedEntries[$i],
                [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally {
        $archive.Dispose()
    }

    $checkArchive = [System.IO.Compression.ZipFile]::OpenRead($temporaryZip)
    try {
        $actualEntries = @($checkArchive.Entries |
            Where-Object { -not $_.FullName.EndsWith('/') } |
            ForEach-Object FullName)
        if ($actualEntries.Count -ne $expectedEntries.Count) {
            throw "ZIP entry count mismatch: expected $($expectedEntries.Count), found $($actualEntries.Count)"
        }
        $differences = @(Compare-Object $expectedEntries $actualEntries)
        if ($differences.Count -ne 0) {
            throw "ZIP entry manifest differs from the release install ($($differences.Count) differences)."
        }
        if (@($actualEntries | Select-Object -Unique).Count -ne $actualEntries.Count) {
            throw 'ZIP contains duplicate entries.'
        }
    } finally {
        $checkArchive.Dispose()
    }

    New-Item -ItemType Directory -Path $verifyDirectory | Out-Null
    [System.IO.Compression.ZipFile]::ExtractToDirectory($temporaryZip, $verifyDirectory)
    $extractedRoot = if ($FlatRoot) {
        $verifyDirectory
    } else {
        Join-Path $verifyDirectory $RootDirectoryName
    }
    if (-not $FlatRoot -and
            -not (Test-Path -LiteralPath $extractedRoot -PathType Container)) {
        throw "ZIP did not extract with the required top-level directory: $RootDirectoryName"
    }

    foreach ($file in $sourceFiles) {
        $relative = $file.FullName.Substring($packagePrefix.Length)
        $extracted = Join-Path $extractedRoot $relative
        if (-not (Test-Path -LiteralPath $extracted -PathType Leaf)) {
            throw "Extracted ZIP is missing: $relative"
        }
        $sourceHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $extractedHash = (Get-FileHash -LiteralPath $extracted -Algorithm SHA256).Hash
        if ($sourceHash -ne $extractedHash) {
            throw "Extracted ZIP hash mismatch: $relative"
        }
    }

    Move-Item -LiteralPath $temporaryZip -Destination $outputZip -Force
    $zipItem = Get-Item -LiteralPath $outputZip
    $zipHash = (Get-FileHash -LiteralPath $outputZip -Algorithm SHA256).Hash.ToLowerInvariant()
    $hashPath = "$outputZip.sha256"
    $hashLine = "$zipHash  $($zipItem.Name)`n"
    [System.IO.File]::WriteAllText($hashPath, $hashLine,
        [System.Text.UTF8Encoding]::new($false))
    $layout = if ($FlatRoot) {
        'flat depot root'
    } else {
        'one top-level directory'
    }
    Write-Host "Release ZIP: $($zipItem.FullName)"
    Write-Host "Validated: $($sourceFiles.Count) files, $layout, extract-and-hash pass"
    Write-Host "Size: $($zipItem.Length) bytes"
    Write-Host "SHA256: $zipHash"
    Write-Host "Checksum: $hashPath"
} finally {
    if (Test-Path -LiteralPath $temporaryZip -PathType Leaf) {
        Remove-Item -LiteralPath $temporaryZip -Force
    }
    if (Test-Path -LiteralPath $verifyDirectory -PathType Container) {
        Remove-Item -LiteralPath $verifyDirectory -Recurse -Force
    }
}
