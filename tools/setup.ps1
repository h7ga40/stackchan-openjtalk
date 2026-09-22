$ErrorActionPreference = 'Stop'

$runner = Join-Path $PSScriptRoot 'Invoke-ArduinoCli.ps1'
$projectRoot = Split-Path -Parent $PSScriptRoot

& $runner core update-index
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $runner lib update-index
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $runner core install 'm5stack:esp32@3.2.5'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$libraries = @(
    'M5Unified@0.2.21',
    'M5GFX@0.2.28',
    'IRremoteESP8266@2.9.0',
    'M5Unit-NFC@0.1.1',
    'M5UnitUnified@0.5.5',
    'M5Utility@0.2.0',
    'M5HAL@0.1.2'
)

foreach ($library in $libraries) {
    & $runner lib install $library --no-deps
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$bspProperties = Join-Path $projectRoot 'libraries\StackChan-BSP\library.properties'
if (-not (Test-Path -LiteralPath $bspProperties) -or
    -not (Select-String -LiteralPath $bspProperties -SimpleMatch 'version=1.1.0' -Quiet)) {
    throw 'The vendored and locally patched StackChan-BSP 1.1.0 is missing.'
}

Write-Host 'The isolated StackChan OpenJTalk Arduino environment is ready.'
