for ($i = 0; $i -lt 15; $i++) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports -contains 'COM6') {
        Write-Host "COM6 found!"
        break
    }
    Write-Host "Waiting for COM6... ($ports)"
    Start-Sleep -Seconds 2
}
[System.IO.Ports.SerialPort]::GetPortNames()
