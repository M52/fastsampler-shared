# Fetch the portable crypto implementation from its official, pinned release.
# Run once before implementing/building the FSB provenance encryption module.
$ErrorActionPreference = 'Stop'
$version = '4.0.2'
$sharedRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stagingRoot = Join-Path $sharedRoot 'build/monocypher-download'
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null

$files = @(
    @{ Source = 'src/monocypher.c'; Name = 'monocypher.c'; MinimumSize = 50000 },
    @{ Source = 'src/monocypher.h'; Name = 'monocypher.h'; MinimumSize = 5000 },
    @{ Source = 'LICENCE.md'; Name = 'LICENCE.md'; MinimumSize = 100 }
)
foreach ($file in $files) {
    $uri = 'https://raw.githubusercontent.com/LoupVaillant/Monocypher/' + $version + '/' + $file.Source
    $staged = Join-Path $stagingRoot $file.Name
    Invoke-WebRequest -UseBasicParsing -Uri $uri -OutFile $staged
    if ((Get-Item -LiteralPath $staged).Length -lt $file.MinimumSize) {
        throw "Downloaded $($file.Name) is unexpectedly short. No dependency files were installed."
    }
    $destination = Join-Path $PSScriptRoot $file.Name
    if (Test-Path -LiteralPath $destination) {
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash) {
            throw "Existing $($file.Name) differs. Preserve or review it before installing this release."
        }
    }
}
foreach ($file in $files) {
    $destination = Join-Path $PSScriptRoot $file.Name
    if (-not (Test-Path -LiteralPath $destination)) {
        Copy-Item -LiteralPath (Join-Path $stagingRoot $file.Name) -Destination $destination
    }
    Get-FileHash -LiteralPath $destination -Algorithm SHA256
}
