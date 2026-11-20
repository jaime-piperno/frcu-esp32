# monitor_serial.ps1 - Monitor serie del ESP32 con reconexion automatica.
#
# Uso:
#   .\tools\monitor_serial.ps1              (COM6 a 115200 por defecto)
#   .\tools\monitor_serial.ps1 COM5
#   .\tools\monitor_serial.ps1 COM5 115200

param(
    [string]$Port = "COM6",
    [int]$Baud = 115200
)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

function Open-Port {
    $s = [System.IO.Ports.SerialPort]::new($Port, $Baud)
    $s.NewLine = "`n"
    $s.ReadTimeout = 2000
    # UTF-8 para que el simbolo de grados no salga como "??"
    $s.Encoding = [System.Text.Encoding]::UTF8
    $s.Open()
    # No dejar la placa en reset al abrir el puerto
    $s.DtrEnable = $true
    $s.RtsEnable = $false
    return $s
}

Write-Host "# Escuchando $Port a $Baud baudios - Ctrl+C para salir" -ForegroundColor Cyan
Write-Host "# Si el puerto se cae, reintenta cada 2 s." -ForegroundColor DarkGray

while ($true) {
    $sp = $null
    try {
        $sp = Open-Port
        Write-Host "# Conectado a $Port" -ForegroundColor Green

        while ($true) {
            try {
                $line = $sp.ReadLine()
                if ($line) { Write-Host $line }
            }
            catch [System.TimeoutException] {
                # Sin datos en 2 s: normal, seguimos esperando.
            }
        }
    }
    catch {
        Write-Host "# Puerto no disponible: $($_.Exception.Message)" -ForegroundColor Yellow
    }
    finally {
        if ($sp) {
            try { if ($sp.IsOpen) { $sp.Close() } } catch { }
            try { $sp.Dispose() } catch { }
        }
    }

    Start-Sleep -Seconds 2
}
