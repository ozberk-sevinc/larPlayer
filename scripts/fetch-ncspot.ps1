param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
# Pinned upstream v1.5.1 release archive digest from GitHub's release asset metadata.
$url = 'https://github.com/hrkfdn/ncspot/releases/download/v1.5.1/ncspot-v1.5.1-windows-x86_64.zip'
$expected = '3AF768346B3B7725DA22614E09479DC8889E255439B03698F62827389BE2989C'
$archive = Join-Path ([IO.Path]::GetTempPath()) ("larplayer-ncspot-$PID.zip")
try {
    Invoke-WebRequest -Uri $url -OutFile $archive -MaximumRedirection 5
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
        throw 'The ncspot archive does not match the upstream v1.5.1 release digest.'
    }
    Add-Type -AssemblyName System.IO.Compression
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $matches = @($zip.Entries | Where-Object { $_.FullName -eq 'ncspot.exe' })
        if ($matches.Count -ne 1) { throw 'The verified ncspot archive has an unexpected layout.' }
        $stream = $matches[0].Open()
        try {
            $out = [IO.File]::Open($Destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
            try { $stream.CopyTo($out) } finally { $out.Dispose() }
        } finally { $stream.Dispose() }
    } finally { $zip.Dispose() }
} finally { Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue }
