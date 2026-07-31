Write-Host "Watching for COM ports... Plug in the Teensy now."
for ($i = 0; $i -lt 30; $i++) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    $ts = (Get-Date).ToString("HH:mm:ss.fff")
    Write-Host "$ts : $($ports -join ', ')"
    Start-Sleep -Milliseconds 500
}
