$port = New-Object System.IO.Ports.SerialPort 'COM6', 115200
$port.ReadTimeout = 2000
$port.DtrEnable = $true
$port.Open()
Start-Sleep -Milliseconds 100
# Toggle DTR to reset Teensy
$port.DtrEnable = $false
Start-Sleep -Milliseconds 100
$port.DtrEnable = $true
Start-Sleep -Milliseconds 1500
Write-Host "Reading from COM4..."
for ($i = 0; $i -lt 5; $i++) {
    try {
        $line = $port.ReadLine()
        Write-Host $line
    } catch {
        Write-Host "timeout"
    }
}
$port.Close()
Write-Host "Done"
