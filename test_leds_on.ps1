$port = New-Object System.IO.Ports.SerialPort 'COM6', 115200
$port.Open()
Start-Sleep -Milliseconds 500

for ($i = 0; $i -lt 5; $i++) {
    $port.WriteLine("LED:${i}:ON")
    Write-Host "Sent LED:${i}:ON"
    Start-Sleep -Milliseconds 100
}

Write-Host ""
Write-Host "All 5 LEDs should be ON. Press Enter to turn them off..."
Read-Host

for ($i = 0; $i -lt 5; $i++) {
    $port.WriteLine("LED:${i}:OFF")
    Write-Host "Sent LED:${i}:OFF"
    Start-Sleep -Milliseconds 100
}

$port.Close()
Write-Host "Done."
