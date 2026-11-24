$ErrorActionPreference = 'Stop'

$project = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $project 'dist'
$release = Join-Path $dist 'GrooveDial'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Install Visual Studio with the Desktop development with C++ workload.'
}
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) {
    throw 'The x64 Visual C++ tools are unavailable.'
}
$vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
$source = Join-Path $project 'src\groove.cpp'
$object = Join-Path $dist 'groove.obj'
$executable = Join-Path $release 'GrooveDial.exe'
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

Copy-Item -LiteralPath (Join-Path $project 'src'),(Join-Path $project 'settings'),(Join-Path $project 'engine') -Destination $release -Recurse -Force
Copy-Item -LiteralPath (Join-Path $project 'README.md'),(Join-Path $project 'LICENSE') -Destination $release -Force
Copy-Item -LiteralPath (Join-Path $project 'docs\preview.png') -Destination (Join-Path $release 'deck-preview.png') -Force

$archive = Join-Path $dist 'GrooveDial-Windows-x64.zip'
Compress-Archive -LiteralPath $release -DestinationPath $archive -Force
Write-Output "Built $executable"
Write-Output "Packaged $archive"
