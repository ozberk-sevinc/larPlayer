$ErrorActionPreference = 'Stop'
$url = 'https://soloist-builds.spotifycdn.com/soloist_release_windows_x86_64.zip'
$stage = Join-Path ([IO.Path]::GetTempPath()) ("larplayer-soloist-$([Guid]::NewGuid().ToString('N'))")
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    $archive = Join-Path $stage 'spotify.zip'
    $unpack = Join-Path $stage 'unpacked'
    New-Item -ItemType Directory -Path $unpack | Out-Null
    Invoke-WebRequest -Uri $url -OutFile $archive -MaximumRedirection 5
    & (Join-Path $PSScriptRoot 'extract-soloist.ps1') -Archive $archive -Destination $unpack
    $exe = @(Get-ChildItem -LiteralPath $unpack -Filter soloist.exe -File -Recurse)
    if ($exe.Count -ne 1) { throw 'Unexpected Spotify executable count.' }
    $signature = Get-AuthenticodeSignature -LiteralPath $exe[0].FullName
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.GetNameInfo('SimpleName',$false) -ne 'Spotify AB') {
        throw 'The downloaded executable does not have a valid Spotify AB signature.'
    }
    $helpText = & $exe[0].FullName --help | Out-String
    if ($LASTEXITCODE -ne 0 -or $helpText -notmatch 'setup-api-key' -or $helpText -notmatch '\-\-ws') {
        throw 'The downloaded Soloist build does not support the required private key and loopback APIs.'
    }
    Write-Output 'PASS official Spotify archive, code signature and stdin key support'
} finally {
    $full = [IO.Path]::GetFullPath($stage)
    if (-not $full.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove a test directory outside TEMP.'
    }
    Remove-Item -LiteralPath $full -Recurse -Force
}
