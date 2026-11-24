$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$source = Get-Content -LiteralPath (Join-Path $project 'src\soloist.hpp') -Raw
if ($source -match '(?m)cmd\s*=.*\s-k\s' -or $source -notmatch 'setup-api-key --data-dir' -or
    $source -notmatch 'CreatePipe\(' -or $source -notmatch 'spotify_signed\(exe\)' -or
    $source -notmatch '--ws 127\.0\.0\.1:0') {
    throw 'Key transfer, signature verification, or loopback binding regressed.'
}
Add-Type -AssemblyName System.IO.Compression
$base = Join-Path ([IO.Path]::GetTempPath()) ("larplayer-security-$([Guid]::NewGuid().ToString('N'))")
New-Item -ItemType Directory -Path $base | Out-Null
try {
    function Try-Archive([string]$Name, [string[]]$Entries, [bool]$ShouldPass) {
        $zipPath = Join-Path $base "$Name.zip"
        $outPath = Join-Path $base "$Name-out"
        New-Item -ItemType Directory -Path $outPath | Out-Null
        $zip = [IO.Compression.ZipFile]::Open($zipPath,[IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($entryName in $Entries) {
                $entry = $zip.CreateEntry($entryName)
                $stream = $entry.Open()
                try { $stream.WriteByte(65) } finally { $stream.Dispose() }
            }
        } finally { $zip.Dispose() }
        $legacy = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
        & $legacy -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $project 'scripts\extract-soloist.ps1') -Archive $zipPath -Destination $outPath *> $null
        $failed = $LASTEXITCODE -ne 0
        if ($failed -eq $ShouldPass) { throw "Unexpected archive validation result: $Name" }
        if ($ShouldPass -and -not (Test-Path (Join-Path $outPath 'soloist.exe'))) {
            throw 'The safe archive was not extracted.'
        }
    }
    Try-Archive 'valid' @('soloist.exe','CHANGELOG.md') $true
    Try-Archive 'traversal' @('../outside.exe','soloist.exe') $false
    Try-Archive 'extra-executable' @('soloist.exe','helper.exe') $false
    Try-Archive 'duplicate' @('soloist.exe','soloist.exe') $false
    if (Test-Path (Join-Path $base 'outside.exe')) { throw 'An archive escaped the staging directory.' }
} finally {
    $full = [IO.Path]::GetFullPath($base)
    if (-not $full.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove a test directory outside TEMP.'
    }
    Remove-Item -LiteralPath $full -Recurse -Force
}
Write-Output 'PASS key launch, loopback binding and hostile ZIP rejection'
