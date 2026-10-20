# Copyright (C) 2026 Soni Romanov
# SPDX-License-Identifier: MIT
param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$SourceDirectory
)
$ErrorActionPreference = 'Stop'
if (!$SourceDirectory) { $SourceDirectory = Split-Path -Parent $PSScriptRoot }
$project = Get-Content -Raw -LiteralPath (Join-Path $SourceDirectory 'CMakeLists.txt')
if ($project -notmatch 'project\(MUTHUR VERSION (\d+\.\d+\.\d+)') {
    throw 'Project fallback version is missing.'
}
$version = $Matches[1]
# A source archive has no .git; do not accidentally use a parent repository.
if ((Test-Path -LiteralPath (Join-Path $SourceDirectory '.git')) -and (Get-Command git -ErrorAction SilentlyContinue)) {
    $tag = $null
    try {
        $tag = & git -C $SourceDirectory describe --tags --abbrev=0 --match 'v[0-9]*' --match '[0-9]*' 2>$null
        if ($LASTEXITCODE -ne 0) { $tag = $null }
    } catch { $tag = $null }
    if ($tag -match '^v?(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?)$') {
        $version = $Matches[1]
    }
}
$header = "// Generated at build time. Do not edit.`n#pragma once`n#define MUTHUR_VERSION `"$version`"`n"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$destination = Join-Path $OutputDirectory 'muthur_version.h'
if (!(Test-Path -LiteralPath $destination) -or (Get-Content -Raw -LiteralPath $destination) -cne $header) {
    [System.IO.File]::WriteAllText($destination, $header)
}
exit 0
