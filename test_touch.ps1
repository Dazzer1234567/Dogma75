$port = New-Object System.IO.Ports.SerialPort 'COM6', 115200
$port.ReadTimeout = 1000
$port.Open()
Start-Sleep -Milliseconds 500

Write-Host "=== TOUCH + LED + ENCODER TEST ==="
Write-Host "Touch your aluminium pads..."
Write-Host ""

$startTime = Get-Date
$endTime = $startTime.AddSeconds(30)

while ((Get-Date) -lt $endTime) {
    try {
        $line = $port.ReadLine()
        Write-Host $line
    } catch { }
}

$port.Close()
Write-Host ""
Write-Host "=== TEST COMPLETE ==="
