# Builds build\installer\ImageViewer-<version>-setup.exe from an already built build\release.
# See doc/building-windows.md.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build\release'
$stage = Join-Path $root 'build\stage'
$output = Join-Path $root 'build\installer'

function Get-CacheValue($name) {
    $line = Select-String -Path (Join-Path $build 'CMakeCache.txt') -Pattern "^${name}:[A-Z]+=(.*)$" | Select-Object -First 1
    if (-not $line) { throw "$name is not in CMakeCache.txt. Build the project first." }
    $line.Matches[0].Groups[1].Value
}

function Assert-Success($what) {
    if ($LASTEXITCODE -ne 0) { throw "$what failed with exit code $LASTEXITCODE" }
}

$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $iscc = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) { throw 'Inno Setup 6 was not found.' }

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
& cmake --install $build --prefix $stage
Assert-Success 'cmake --install'

# Licence texts the libraries require to be shipped with the app.
$licences = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force (Join-Path $licences 'image-viewer') | Out-Null
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $licences 'image-viewer')
New-Item -ItemType Directory -Force (Join-Path $licences 'inter') | Out-Null
Copy-Item (Join-Path $root 'assets\fonts\Inter-LICENSE.txt') (Join-Path $licences 'inter')
New-Item -ItemType Directory -Force (Join-Path $licences 'tabler-icons') | Out-Null
Copy-Item (Join-Path $root 'assets\icons\tabler\LICENSE') (Join-Path $licences 'tabler-icons')
$graphFile = Join-Path $root 'build\conan\graph.json'
# --build=missing: a library version without a prebuilt package for this compiler is built here,
# rather than stopping the installer. Nothing is built when the cache has them all.
$graph = & conan install $root -of (Join-Path $root 'build\conan') -s build_type=Release `
    -c tools.cmake.cmaketoolchain:generator=Ninja --build=missing -f json
Assert-Success 'conan install'
Set-Content -Path $graphFile -Value $graph -Encoding UTF8
$nodes = (Get-Content $graphFile -Raw | ConvertFrom-Json).graph.nodes.PSObject.Properties.Value
foreach ($node in $nodes) {
    if ($node.context -ne 'host' -or -not $node.package_folder) { continue }
    $from = Join-Path $node.package_folder 'licenses'
    if (-not (Test-Path $from)) { throw "$($node.ref) has no licence files" }
    $to = Join-Path $licences ($node.ref -replace '/.*$', '')
    New-Item -ItemType Directory -Force $to | Out-Null
    Copy-Item (Join-Path $from '*') $to -Recurse -Force
}

# Qt6_DIR is <version>\<kit>\lib\cmake\Qt6, and the texts are in <version>\Src\LICENSES.
$qtVersionDir = (Get-Item (Get-CacheValue 'Qt6_DIR')).Parent.Parent.Parent.Parent.FullName
$qtLicences = Get-ChildItem (Join-Path $qtVersionDir 'Src\LICENSES') -Filter '*GPL*'
if (-not $qtLicences) { throw 'The Qt licence texts were not found. Install the Qt sources.' }
$to = Join-Path $licences 'qt'
New-Item -ItemType Directory -Force $to | Out-Null
$qtLicences | Copy-Item -Destination $to

$stray = Get-ChildItem $stage -Recurse -Filter '*.pdb'
if ($stray) { throw "Debug symbols would be shipped: $($stray.FullName -join ', ')" }

$version = Get-CacheValue 'CMAKE_PROJECT_VERSION'
& $iscc "/DAppVersion=$version" "/DStageDir=$stage" "/O$output" (Join-Path $PSScriptRoot 'imageviewer.iss')
Assert-Success 'ISCC'

# The debug symbols stay out of the installer but are kept beside it, with the very program they
# belong to, which a debugger needs as well to read a crash dump or a profile of this build.
# tar is part of Windows 10 and later.
$symbols = Join-Path $output "ImageViewer-$version-symbols.tar.zst"
if (-not (Test-Path (Join-Path $build 'imageviewer.pdb'))) { throw 'imageviewer.pdb is missing. Build the project first.' }
& tar --zstd -cf $symbols -C $build imageviewer.exe imageviewer.pdb
Assert-Success 'tar'

# What the installer ships, as a CycloneDX software bill of materials.
& (Join-Path $PSScriptRoot 'write-sbom.ps1') -Graph $graphFile -Stage $stage -Version $version `
    -QtVersion (Split-Path -Leaf $qtVersionDir) -Root $root -Out (Join-Path $output "ImageViewer-$version-sbom.cdx.json")
