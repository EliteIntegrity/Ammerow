# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

# Run after installing into a fresh staging directory and before verifying it.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageRoot,
    [Parameter(Mandatory = $true)][string]$SourceArchive,
    [string]$Python = 'python'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$package = (Resolve-Path -LiteralPath $PackageRoot).Path
$archive = (Resolve-Path -LiteralPath $SourceArchive).Path
$checker = Join-Path $projectRoot 'utils/check-public-source.py'
if (-not (Test-Path -LiteralPath (Join-Path $package 'ammerow.exe') -PathType Leaf)) {
    throw 'The staging directory must contain the release executable.'
}
if (Test-Path -LiteralPath (Join-Path $package 'source')) {
    throw 'Use a fresh staging directory; its source directory already exists.'
}
& $Python $checker --archive $archive
if ($LASTEXITCODE -ne 0) { throw 'Corresponding-source archive validation failed.' }

$destination = Join-Path $package 'source'
New-Item -ItemType Directory -Path $destination | Out-Null
$installed = Join-Path $destination 'Ammerow-source.zip'
Copy-Item -LiteralPath $archive -Destination $installed
$hash = (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText("$installed.sha256", "$hash  Ammerow-source.zip`n",
    [System.Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $projectRoot 'SOURCE-CODE.txt') -Destination $package
& $Python $checker --package $package
if ($LASTEXITCODE -ne 0) { throw 'Installed corresponding-source validation failed.' }
Write-Output "Corresponding source attached: $installed ($hash)"
