$port = New-Object System.IO.Ports.SerialPort 'COM6', 115200
$port.ReadTimeout = 1000
$port.Open()
Start-Sleep -Milliseconds 500

Write-Host "=== LED + TOUCH INTEGRATION TEST ==="
Write-Host "Watch LED on PCA9685 channel 0"
Write-Host "Touch your aluminium pad to toggle LED"
Write-Host ""

$startTime = Get-Date
$endTime = $startTime.AddSeconds(15)

while ((Get-Date) -lt $endTime) {
    try {
        $line = $port.ReadLine()
        Write-Host $line
    } catch { }
}

$port.Close()
Write-Host ""
Write-Host "=== TEST COMPLETE ==="
