$port = New-Object System.IO.Ports.SerialPort 'COM6', 115200
$port.ReadTimeout = 2000
$port.Open()
Start-Sleep -Milliseconds 500
Write-Host "Reading from COM6 for 15 seconds... TURN THE ENCODER NOW"
for ($i = 0; $i -lt 15; $i++) {
    try {
        $line = $port.ReadLine()
        Write-Host $line
    } catch {
        Write-Host "timeout"
    }
}
$port.Close()
Write-Host "Done"
