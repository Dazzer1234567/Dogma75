$port = New-Object System.IO.Ports.SerialPort 'COM6', 115200
$port.ReadTimeout = 2000
$port.Open()
Start-Sleep -Milliseconds 500

# Send LED 8 ON
Write-Host "Sending LED:8:ON"
$port.WriteLine("LED:8:ON")
Start-Sleep -Milliseconds 200

# Read response if any
for ($i = 0; $i -lt 3; $i++) {
    try {
        $line = $port.ReadLine()
        Write-Host $line
    } catch {
        break
    }
}

$port.Close()
Write-Host "Done - LED 8 should be ON"
