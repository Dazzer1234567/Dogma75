$port = New-Object System.IO.Ports.SerialPort 'COM6', 115200
$port.ReadTimeout = 1000
$port.Open()
Start-Sleep -Milliseconds 500

Write-Host "=== ENCODER 6 TEST ==="
Write-Host "Turn ONLY encoder 6 now..."
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
