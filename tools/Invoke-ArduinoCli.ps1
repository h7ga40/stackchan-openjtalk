param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $CliArguments
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$localState = if ([string]::IsNullOrWhiteSpace($env:STACKCHAN_ARDUINO_STATE)) {
    Join-Path $projectRoot '.arduino'
} else {
    [System.IO.Path]::GetFullPath($env:STACKCHAN_ARDUINO_STATE)
}
$localTemp = Join-Path (Join-Path $projectRoot '.arduino') 'tmp'

$env:ARDUINO_DIRECTORIES_DATA = Join-Path $localState 'data'
$env:ARDUINO_DIRECTORIES_DOWNLOADS = Join-Path $localState 'downloads'
$env:ARDUINO_DIRECTORIES_USER = Join-Path (Join-Path $projectRoot '.arduino') 'user'
$env:ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS = 'https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json'
$env:TEMP = $localTemp
$env:TMP = $localTemp

New-Item -ItemType Directory -Force -Path $env:ARDUINO_DIRECTORIES_DATA, $env:ARDUINO_DIRECTORIES_DOWNLOADS, $env:ARDUINO_DIRECTORIES_USER, $localTemp | Out-Null

$arduinoCli = Get-Command 'arduino-cli.exe' -ErrorAction SilentlyContinue
if ($null -ne $arduinoCli) {
    $arduinoCliPath = $arduinoCli.Source
}
else {
    $arduinoCliPath = Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
}

if (-not (Test-Path -LiteralPath $arduinoCliPath)) {
    throw 'arduino-cli.exe was not found. Install Arduino IDE 2.x or add arduino-cli to PATH.'
}

& $arduinoCliPath @CliArguments
exit $LASTEXITCODE
