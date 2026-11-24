$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$release = Join-Path $project 'dist\LARPlayer'
$executable = Join-Path $release 'LARPlayer.exe'
$archive = Join-Path $project 'dist\LARPlayer-Windows-x64.zip'
if (-not (Test-Path -LiteralPath $archive)) { throw 'The portable ZIP is missing.' }

$help = & $executable help | Out-String
if ($LASTEXITCODE -ne 0 -or $help -notmatch 'playlists') { throw 'The player help command failed.' }
$preview = & $executable preview | Out-String
if ($LASTEXITCODE -ne 0 -or $preview -notmatch 'L A R P L A Y E R') { throw 'The offline preview failed.' }
$version = & (Join-Path $release 'engine\ncspot.exe') --version | Out-String
if ($LASTEXITCODE -ne 0 -or $version -notmatch 'ncspot') { throw 'The optional classic engine is missing.' }

Add-Type -AssemblyName System.IO.Compression
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($name in 'LARPlayer/LARPlayer.exe','LARPlayer/src/deck.hpp','LARPlayer/engine/LICENSE-ncspot.txt','LARPlayer/LICENSE') {
        if (-not $zip.GetEntry($name)) { throw "Missing release entry $name" }
    }
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName -match '(?i)GrooveDial|deck-preview\.png|groove-|profile[/\\]|\.env|soloist\.exe') {
            throw "Unexpected release entry $($entry.FullName)"
        }
    }
} finally {
    $zip.Dispose()
}
Write-Output 'PASS CLI startup, portable ZIP and classic engine'

