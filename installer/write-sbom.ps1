# Writes the software bill of materials of the Windows build, in CycloneDX 1.6 JSON: what the
# installer ships, with versions and licences. Run by build-installer.ps1, after `conan install`
# has written the dependency graph and the program has been staged.
#
# The libraries come from Conan's graph: those built for the program (context "host"), not the
# tools that built them nor what only the tests use. Conan does not know of Qt, the bundled font
# and icons or the Visual C++ runtime, so those are added here, Qt with each DLL the stage holds.
param(
    [Parameter(Mandatory)] [string] $Graph,      # build\conan\graph.json
    [Parameter(Mandatory)] [string] $Stage,      # the staged install, as the installer packs it
    [Parameter(Mandatory)] [string] $Version,    # the program's
    [Parameter(Mandatory)] [string] $QtVersion,
    [Parameter(Mandatory)] [string] $Root,       # the repository
    [Parameter(Mandatory)] [string] $Out
)
$ErrorActionPreference = 'Stop'

function New-Licences($ids) {
    # Conan Center gives SPDX identifiers; anything else goes in as a plain name.
    , @($ids | Where-Object { $_ } | ForEach-Object {
        if ($_ -match '^[A-Za-z0-9.+-]+$') { @{ license = @{ id = $_ } } } else { @{ license = @{ name = $_ } } }
    })
}

$nodes = (Get-Content $Graph -Raw | ConvertFrom-Json).graph.nodes.PSObject.Properties
$shipped = @{}  # node id -> node, for the libraries in the program
foreach ($entry in $nodes) {
    $node = $entry.Value
    if ($entry.Name -ne '0' -and $node.context -eq 'host' -and -not $node.test) { $shipped[$entry.Name] = $node }
}
function Get-Ref($node) { "pkg:conan/$($node.name)@$($node.version)" }
# Only the direct ones: the graph lists every dependency under each node, indirect ones too.
function Get-LinkedIds($node) {
    @($node.dependencies.PSObject.Properties |
        Where-Object { $_.Value.direct -and -not $_.Value.build -and $shipped.ContainsKey($_.Name) } |
        ForEach-Object { $_.Name })
}

# A recipe lists the licences of its whole package, without saying which apply together or as
# alternatives. Where that misleads, the licence of what is shipped, as an SPDX expression.
$LicenceOf = @{
    # The package's GPL covers the xz programs; liblzma, all that is linked, is 0BSD since 5.6.
    'xz_utils' = '0BSD'
    # Offered under either.
    'highway'  = 'Apache-2.0 OR BSD-3-Clause'
}

$components = [System.Collections.ArrayList]::new()
$dependencies = [System.Collections.ArrayList]::new()
foreach ($id in $shipped.Keys | Sort-Object { $shipped[$_].name }) {
    $node = $shipped[$id]
    $component = [ordered]@{
        type = 'library'; 'bom-ref' = (Get-Ref $node); name = $node.name; version = [string]$node.version
        description = $node.description; licenses = (New-Licences @($node.license)); purl = (Get-Ref $node)
    }
    if ($LicenceOf.ContainsKey($node.name)) { $component.licenses = @(@{ expression = $LicenceOf[$node.name] }) }
    if ($node.homepage) { $component.externalReferences = @(@{ type = 'website'; url = $node.homepage }) }
    [void]$components.Add($component)
    [void]$dependencies.Add([ordered]@{
        ref = (Get-Ref $node); dependsOn = @(Get-LinkedIds $node | ForEach-Object { Get-Ref $shipped[$_] })
    })
}

$qtModules = @(Get-ChildItem $Stage -Filter 'Qt6*.dll' | Sort-Object Name | ForEach-Object {
    [ordered]@{ type = 'library'; 'bom-ref' = "qt-$($_.BaseName)"; name = $_.BaseName; version = $QtVersion }
})
[void]$components.Add([ordered]@{
    type = 'framework'; 'bom-ref' = 'qt'; supplier = @{ name = 'The Qt Company' }; name = 'Qt'; version = $QtVersion
    description = 'Qt, as its official binaries, linked dynamically'
    licenses = (New-Licences @('LGPL-3.0-only')); cpe = "cpe:2.3:a:qt:qt:${QtVersion}:*:*:*:*:*:*:*"
    externalReferences = @(@{ type = 'website'; url = 'https://www.qt.io' })
    components = $qtModules
})

Add-Type -AssemblyName PresentationCore
$font = New-Object System.Windows.Media.GlyphTypeface((Join-Path $Root 'assets\fonts\InterVariable.ttf'))
$fontVersion = ($font.VersionStrings.Values | Select-Object -First 1) -replace '^Version\s+', '' -replace ';.*$', ''
[void]$components.Add([ordered]@{
    type = 'file'; 'bom-ref' = 'inter'; name = 'Inter'; version = $fontVersion; description = 'The interface font, bundled'
    licenses = (New-Licences @('OFL-1.1')); externalReferences = @(@{ type = 'website'; url = 'https://rsms.me/inter/' })
})
[void]$components.Add([ordered]@{
    type = 'file'; 'bom-ref' = 'tabler-icons'; name = 'Tabler Icons'; description = 'Some of its icons, bundled'
    licenses = (New-Licences @('MIT')); externalReferences = @(@{ type = 'website'; url = 'https://tabler.io/icons' })
})

$runtime = Get-Item (Join-Path $Stage 'msvcp140.dll') -ErrorAction SilentlyContinue
if ($runtime) {
    [void]$components.Add([ordered]@{
        type = 'library'; 'bom-ref' = 'vc-runtime'; supplier = @{ name = 'Microsoft' }
        name = 'Microsoft Visual C++ Redistributable'; version = $runtime.VersionInfo.ProductVersion
        description = 'The Visual C++ runtime DLLs, beside the program'
        licenses = (New-Licences @('Microsoft Visual C++ Redistributable licence'))
    })
}

$rootNode = ($nodes | Where-Object Name -eq '0').Value
$direct = @(Get-LinkedIds $rootNode | ForEach-Object { Get-Ref $shipped[$_] })
$others = @('qt', 'inter', 'tabler-icons') + @(if ($runtime) { 'vc-runtime' })
[void]$dependencies.Insert(0, [ordered]@{ ref = 'imageviewer'; dependsOn = @($direct + $others) })
[void]$dependencies.Add([ordered]@{ ref = 'qt'; dependsOn = @($qtModules | ForEach-Object { $_.'bom-ref' }) })

$bom = [ordered]@{
    bomFormat = 'CycloneDX'; specVersion = '1.6'; serialNumber = "urn:uuid:$([guid]::NewGuid())"; version = 1
    metadata = [ordered]@{
        timestamp = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        tools = @{ components = @(@{ type = 'application'; name = 'installer/write-sbom.ps1' }) }
        component = [ordered]@{
            type = 'application'; 'bom-ref' = 'imageviewer'; name = 'Image Viewer'; version = $Version
            licenses = (New-Licences @('MIT'))
        }
    }
    components = $components
    dependencies = $dependencies
}
# Without a byte order mark, which some readers of JSON refuse.
[IO.File]::WriteAllText($Out, ($bom | ConvertTo-Json -Depth 12), (New-Object Text.UTF8Encoding $false))
