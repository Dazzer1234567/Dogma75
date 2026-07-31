$port = New-Object System.IO.Ports.SerialPort 'COM6', 115200
$port.ReadTimeout = 500
$port.Open()
Start-Sleep -Milliseconds 500

Write-Host "=== ENCODER 3 ROTATION TEST ==="
Write-Host "Turn encoder 3 (pins 6,7) exactly ONE FULL ROTATION"
Write-Host ""

$total = 0
$startTime = Get-Date
$endTime = $startTime.AddSeconds(10)

while ((Get-Date) -lt $endTime) {
    try {
        $line = $port.ReadLine()
        if ($line -like "E3:*") {
            $delta = [int]($line -replace "E3:", "")
            $total += [Math]::Abs($delta)
        }
    } catch { }
}

$port.Close()
Write-Host ""
Write-Host "=== ENCODER 3 TOTAL PULSES: $total ==="
