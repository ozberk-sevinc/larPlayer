$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$release = Join-Path $project 'dist\GrooveDial'
$executable = Join-Path $release 'GrooveDial.exe'
$archive = Join-Path $project 'dist\GrooveDial-Windows-x64.zip'
if (-not (Test-Path -LiteralPath $archive)) { throw 'The portable ZIP is missing.' }

$help = & $executable help | Out-String
if ($LASTEXITCODE -ne 0 -or $help -notmatch 'playlists') { throw 'The player help command failed.' }
$preview = & $executable preview | Out-String
if ($LASTEXITCODE -ne 0 -or $preview -notmatch 'G R O O V E') { throw 'The offline preview failed.' }
$version = & (Join-Path $release 'engine\ncspot.exe') --version | Out-String
if ($LASTEXITCODE -ne 0 -or $version -notmatch 'ncspot') { throw 'The optional classic engine is missing.' }

Add-Type -AssemblyName System.IO.Compression
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($name in 'GrooveDial/GrooveDial.exe','GrooveDial/src/deck.hpp','GrooveDial/engine/LICENSE-ncspot.txt','GrooveDial/LICENSE') {
        if (-not $zip.GetEntry($name)) { throw "Missing release entry $name" }
    }
} finally {
    $zip.Dispose()
}
Write-Output 'PASS CLI startup, portable ZIP and classic engine'
