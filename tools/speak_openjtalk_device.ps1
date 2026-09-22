param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string] $Text,

    [string] $Port = 'COM3',

    [ValidateRange(-12.0, 12.0)]
    [double] $Pitch = 3.0,

    [ValidateRange(10, 300)]
    [int] $TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
if ($Text.Contains("`r") -or $Text.Contains("`n")) {
    throw 'Text must be a single line.'
}
if ([System.Text.Encoding]::UTF8.GetByteCount($Text) -ge 1024) {
    throw 'UTF-8 text must be shorter than 1024 bytes.'
}

$serial = $null
try {
    $serial = [System.IO.Ports.SerialPort]::new(
        $Port, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.NewLine = "`n"
    $serial.Encoding = [System.Text.UTF8Encoding]::new($false)
    $serial.ReadTimeout = 500
    $serial.WriteTimeout = 5000
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.Open()

    Start-Sleep -Milliseconds 300
    $pitchText = $Pitch.ToString('0.###', [System.Globalization.CultureInfo]::InvariantCulture)
    $serial.WriteLine("OPENJTALK_TEXT $pitchText")
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $ready = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $serial.ReadLine().TrimEnd("`r")
            Write-Host $line
            if ($line -like '*TEXT_READY*') {
                $ready = $true
                break
            }
            if ($line -like '*SERIAL_ERROR*') {
                throw "K151 reported an error: $line"
            }
        } catch [System.TimeoutException] {
        }
    }
    if (-not $ready) {
        throw "K151 did not accept on-device text analysis on $Port."
    }

    $serial.WriteLine($Text)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $serial.ReadLine().TrimEnd("`r")
            Write-Host $line
            if ($line -like '*RESULT=PASS source=DEVICE*') {
                Write-Host "Spoken successfully with on-device labels: $Text"
                exit 0
            }
            if ($line -like '*RESULT=FAIL source=DEVICE*' -or
                $line -like '*SERIAL_ERROR*') {
                throw "K151 reported an error: $line"
            }
        } catch [System.TimeoutException] {
        }
    }
    throw "Timed out waiting for K151 synthesis on $Port."
} finally {
    if ($serial -ne $null) {
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
}
