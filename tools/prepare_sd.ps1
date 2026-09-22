param(
    [Parameter(Mandatory = $true)]
    [string] $DestinationRoot,

    [switch] $IncludeDictionary,

    [switch] $Force
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$destination = [System.IO.Path]::GetFullPath($DestinationRoot)
if (-not (Test-Path -LiteralPath $destination -PathType Container)) {
    throw "DestinationRoot does not exist: $destination"
}

$voiceSource = Join-Path $projectRoot 'assets\mei_normal_16.raw'
$labelsSource = Join-Path $projectRoot 'assets\openjtalk_real_labels.txt'
$dictionarySource = Join-Path $projectRoot 'assets\cache\open_jtalk_dic_utf_8-1.11'
$requiredSources = @($voiceSource, $labelsSource)
if ($IncludeDictionary) {
    $requiredSources += $dictionarySource
}
foreach ($source in $requiredSources) {
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Required OpenJTalk artifact does not exist: $source"
    }
}

$targetDirectory = Join-Path $destination 'openjtalk'
New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null
$voiceTarget = Join-Path $targetDirectory 'mei_normal_16.raw'
$labelsTarget = Join-Path $targetDirectory 'test.lab'
if (-not $Force -and ((Test-Path -LiteralPath $voiceTarget) -or (Test-Path -LiteralPath $labelsTarget))) {
    throw 'Destination files already exist. Use -Force to replace them.'
}

Copy-Item -LiteralPath $voiceSource -Destination $voiceTarget -Force:$Force
Copy-Item -LiteralPath $labelsSource -Destination $labelsTarget -Force:$Force

if ($IncludeDictionary) {
    $dictionaryTarget = Join-Path $targetDirectory 'dic'
    if ((Test-Path -LiteralPath $dictionaryTarget) -and -not $Force) {
        throw 'Dictionary destination already exists. Use -Force to replace its files.'
    }
    New-Item -ItemType Directory -Force -Path $dictionaryTarget | Out-Null
    Get-ChildItem -LiteralPath $dictionarySource -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $dictionaryTarget -Force:$Force
    }
}

Write-Host "Prepared $targetDirectory"
Get-Item -LiteralPath $voiceTarget, $labelsTarget | Select-Object Name, Length, FullName
if ($IncludeDictionary) {
    Get-ChildItem -LiteralPath $dictionaryTarget -File |
        Select-Object Name, Length, FullName
}
