$port = New-Object System.IO.Ports.SerialPort 'COM6', 115200
$port.ReadTimeout = 500
$port.Open()
Start-Sleep -Milliseconds 500
Write-Host "Turn all 6 encoders NOW (5 seconds)..."

$startTime = Get-Date
$endTime = $startTime.AddSeconds(5)

while ((Get-Date) -lt $endTime) {
    try {
        $line = $port.ReadLine()
        if ($line -notmatch "heartbeat") {
            Write-Host $line
        }
    } catch { }
}

$port.Close()
Write-Host "Done"
