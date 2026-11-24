param(
    [Parameter(Mandatory=$true)][string]$Archive,
    [Parameter(Mandatory=$true)][string]$Destination
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$target = [IO.Path]::GetFullPath($Destination)
if (-not [IO.Directory]::Exists($target)) { throw 'The extraction folder is missing.' }
$zip = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Archive))
try {
    if ($zip.Entries.Count -gt 32 -or $zip.Entries.Count -eq 0) { throw 'Unexpected Soloist archive layout.' }
    $total = 0L
    $executables = 0
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $zip.Entries) {
        $name = $entry.FullName
        $parts = $name.TrimEnd('/').Split('/')
        if (-not $name -or $name.StartsWith('/') -or $name.Contains('\') -or $name.Contains(':') -or
            $parts.Where({$_ -eq '' -or $_ -eq '.' -or $_ -eq '..'}).Count -gt 0 -or
            $name -match '[\x00-\x1f]' -or $parts.Count -gt 3 -or -not $seen.Add($name)) {
            throw 'Unsafe Soloist archive entry.'
        }
        # Reject Unix symlinks and Windows reparse points, including directories.
        $unixType = ($entry.ExternalAttributes -shr 16) -band 0xF000
        if ($unixType -eq 0xA000 -or ($entry.ExternalAttributes -band 0x400) -ne 0) {
            throw 'Soloist archive contains a link.'
        }
        $total += $entry.Length
        if ($total -gt 100MB -or $entry.Length -gt 70MB) { throw 'Soloist archive exceeds size limit.' }
        if ($name.EndsWith('/')) { continue }
        $basename = $parts[-1]
        if ($basename -ieq 'soloist.exe') { $executables++ }
        elseif ($basename -notmatch '(?i)^(?:readme|changelog|license|third.party.licenses)(?:\.[a-z0-9]+)?$') {
            throw 'Soloist archive contains an unexpected file.'
        }
    }
    if ($executables -ne 1) { throw 'Soloist archive must contain exactly one executable.' }
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName.EndsWith('/')) { continue }
        $path = [IO.Path]::GetFullPath((Join-Path $target $entry.FullName.Replace('/', '\')))
        if (-not $path.StartsWith(($target.TrimEnd('\') + '\'), [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Soloist archive path escapes the staging folder.'
        }
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
        $stream = $entry.Open()
        try {
            $output = [IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
            try { $stream.CopyTo($output) } finally { $output.Dispose() }
        } finally { $stream.Dispose() }
    }
} finally { $zip.Dispose() }
