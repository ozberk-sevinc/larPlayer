$ErrorActionPreference = 'Stop'

$project = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $project 'dist'
$release = Join-Path $dist 'LARPlayer'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Install Visual Studio with the Desktop development with C++ workload.'
}
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) {
    throw 'The x64 Visual C++ tools are unavailable.'
}
$vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
$source = Join-Path $project 'src\larplayer.cpp'
$object = Join-Path $dist 'larplayer.obj'
$executable = Join-Path $release 'LARPlayer.exe'
if ([IO.Path]::GetFullPath($release) -ne [IO.Path]::GetFullPath((Join-Path $project 'dist\LARPlayer'))) {
    throw 'Unexpected release directory.'
}
if (Test-Path -LiteralPath $release) {
    Remove-Item -LiteralPath $release -Recurse -Force
}
New-Item -ItemType Directory -Path $release -Force | Out-Null

$command = 'call "' + $vcvars + '" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /utf-8 /Fo:"' +
    $object + '" "' + $source + '" /Fe:"' + $executable + '" /link psapi.lib /INCREMENTAL:NO'
Push-Location $project
try {
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "The C++ build failed with exit code $LASTEXITCODE." }
} finally {
    Pop-Location
}

foreach ($folder in 'src','settings','engine','scripts') {
    New-Item -ItemType Directory -Path (Join-Path $release $folder) -Force | Out-Null
}
foreach ($name in 'deck-hotkeys.hpp','deck-wire.hpp','deck.hpp','larplayer.cpp','soloist.hpp') {
    Copy-Item -LiteralPath (Join-Path $project "src\$name") -Destination (Join-Path $release 'src')
}
foreach ($name in 'larplayer-library.toml','larplayer-queue.toml','larplayer-search.toml') {
    Copy-Item -LiteralPath (Join-Path $project "settings\$name") -Destination (Join-Path $release 'settings')
}
Copy-Item -LiteralPath (Join-Path $project 'engine\LICENSE-ncspot.txt') -Destination (Join-Path $release 'engine')
Copy-Item -LiteralPath (Join-Path $project 'scripts\extract-soloist.ps1') -Destination (Join-Path $release 'scripts')
& (Join-Path $project 'scripts\fetch-ncspot.ps1') -Destination (Join-Path $release 'engine\ncspot.exe')
Copy-Item -LiteralPath (Join-Path $project 'README.md'),(Join-Path $project 'LICENSE') -Destination $release
Copy-Item -LiteralPath (Join-Path $project 'docs\preview.svg') -Destination (Join-Path $release 'deck-preview.svg') -Force

$archive = Join-Path $dist 'LARPlayer-Windows-x64.zip'
Compress-Archive -LiteralPath $release -DestinationPath $archive -Force
Write-Output "Built $executable"
Write-Output "Packaged $archive"
$digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$archive.sha256", "$digest  LARPlayer-Windows-x64.zip`n", [Text.UTF8Encoding]::new($false))
Write-Output "SHA-256 $digest"
