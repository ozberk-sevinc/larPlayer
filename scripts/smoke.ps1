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
    $expected = @(
        'LARPlayer/LARPlayer.exe','LARPlayer/LICENSE','LARPlayer/README.md','LARPlayer/deck-preview.svg',
        'LARPlayer/engine/LICENSE-ncspot.txt','LARPlayer/engine/ncspot.exe',
        'LARPlayer/settings/larplayer-library.toml','LARPlayer/settings/larplayer-queue.toml','LARPlayer/settings/larplayer-search.toml',
        'LARPlayer/src/deck-hotkeys.hpp','LARPlayer/src/deck-wire.hpp','LARPlayer/src/deck.hpp',
        'LARPlayer/src/larplayer.cpp','LARPlayer/src/soloist.hpp','LARPlayer/scripts/extract-soloist.ps1'
    )
    foreach ($name in $expected) {
        if (-not $zip.GetEntry($name)) { throw "Missing release entry $name" }
    }
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName -notin $expected) {
            throw "Unexpected release entry $($entry.FullName)"
        }
        $stream = $entry.Open()
        try {
            $memory = [IO.MemoryStream]::new()
            try { $stream.CopyTo($memory); $bytes = $memory.ToArray() } finally { $memory.Dispose() }
        } finally { $stream.Dispose() }
        $ascii = [Text.Encoding]::ASCII.GetString($bytes)
        if ($ascii -match '-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----' -or
            $ascii -match '(?i)(?:SOLOIST_API_KEY|SPOTIFY_CLIENT_SECRET|SPOTIFY_PASSWORD)\s*[:=]\s*[A-Za-z0-9_-]{12,}') {
            throw "Potential credential content in $($entry.FullName)"
        }
    }
} finally {
    $zip.Dispose()
}
Write-Output "PASS CLI startup, exact portable ZIP manifest and classic engine"
Write-Output "ZIP SHA-256 $((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant())"
