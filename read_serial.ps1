$port = New-Object System.IO.Ports.SerialPort 'COM4', 115200
$port.ReadTimeout = 2000
$port.Open()
Start-Sleep -Milliseconds 1000
Write-Host "Reading from COM4..."
for ($i = 0; $i -lt 8; $i++) {
    try {
        $line = $port.ReadLine()
        Write-Host $line
    } catch {
        Write-Host "timeout"
    }
}
$port.Close()
Write-Host "Done"
