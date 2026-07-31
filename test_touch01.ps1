$port = New-Object System.IO.Ports.SerialPort 'COM6', 115200
$port.ReadTimeout = 1000
$port.Open()
Start-Sleep -Milliseconds 500

Write-Host "=== TOUCH 0 & 1 TEST ==="
Write-Host "Touch pads 0 and 1 now..."
Write-Host ""

$startTime = Get-Date
$endTime = $startTime.AddSeconds(15)

while ((Get-Date) -lt $endTime) {
    try {
        $line = $port.ReadLine()
        if ($line -match "TOUCH|RELEASE|heartbeat") {
            Write-Host $line
        }
    } catch { }
}

$port.Close()
Write-Host ""
Write-Host "=== TEST COMPLETE ==="
