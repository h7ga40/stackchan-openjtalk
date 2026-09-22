param(
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $PSScriptRoot 'Invoke-ArduinoCli.ps1'
$sketchPath = Join-Path $projectRoot 'firmware\StackChanOpenJTalk'
$buildPath = Join-Path $projectRoot 'build\StackChanOpenJTalk'
$outputPath = Join-Path $projectRoot 'out\StackChanOpenJTalk'
$htsRoot = Join-Path $projectRoot 'vendor\hts_engine_raw_API-1.10'
$htsLib = Join-Path $htsRoot 'lib'
$htsInclude = Join-Path $htsRoot 'include'
$frontendProjectRoot = Join-Path $projectRoot 'vendor'
$frontendRoot = Join-Path $frontendProjectRoot 'open_jtalk-1.11'
$frontendIncludePaths = @(
    (Join-Path $frontendProjectRoot 'openjtalk-project-src'),
    (Join-Path $frontendRoot 'mecab\src'),
    (Join-Path $frontendRoot 'njd'),
    (Join-Path $frontendRoot 'jpcommon'),
    (Join-Path $frontendRoot 'text2mecab'),
    (Join-Path $frontendRoot 'mecab2njd'),
    (Join-Path $frontendRoot 'njd_set_pronunciation'),
    (Join-Path $frontendRoot 'njd_set_digit'),
    (Join-Path $frontendRoot 'njd_set_accent_phrase'),
    (Join-Path $frontendRoot 'njd_set_accent_type'),
    (Join-Path $frontendRoot 'njd_set_unvoiced_vowel'),
    (Join-Path $frontendRoot 'njd_set_long_vowel'),
    (Join-Path $frontendRoot 'njd2jpcommon')
)

foreach ($requiredPath in @($htsLib, $htsInclude) + $frontendIncludePaths) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Container)) {
        throw "Vendored OpenJTalk source directory not found: $requiredPath"
    }
}

New-Item -ItemType Directory -Force -Path $buildPath, $outputPath | Out-Null

# Arduino CLI passes these values through response files. Forward slashes keep
# Windows path separators from being interpreted as escape characters.
$htsLibFlag = $htsLib.Replace('\', '/')
$htsIncludeFlag = $htsInclude.Replace('\', '/')
$frontendFlags = $frontendIncludePaths | ForEach-Object { '-I' + $_.Replace('\', '/') }
if ($htsLibFlag -match '\s' -or $htsIncludeFlag -match '\s' -or
    ($frontendFlags | Where-Object { $_ -match '\s' })) {
    throw 'The project path must not contain spaces because Arduino CLI splits compiler.extra_flags.'
}

$sourceFlags = (@(
    '-DFESTIVAL=1', '-DMPL_DEBUG=1', '-DHTS_EMBEDDED=1',
    "-I$htsLibFlag", "-I$htsIncludeFlag"
) + $frontendFlags) -join ' '

$compileArguments = @(
    'compile',
    '--fqbn', 'm5stack:esp32:m5stack_cores3',
    '--libraries', (Join-Path $projectRoot 'libraries'),
    '--build-path', $buildPath,
    '--output-dir', $outputPath,
    '--jobs', '0',
    '--build-property', "compiler.c.extra_flags=$sourceFlags",
    '--build-property', "compiler.cpp.extra_flags=$sourceFlags"
)
if ($Clean) {
    $compileArguments += '--clean'
}
$compileArguments += $sketchPath

& $runner @compileArguments
exit $LASTEXITCODE
