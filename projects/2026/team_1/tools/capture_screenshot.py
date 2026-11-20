#!/usr/bin/env python3
"""Captura una imagen real de alta definición del Dashboard de Confort en docs/img/dashboard.png."""
import os
import subprocess
import time
import urllib.request

PORT = 8089
URL = f"http://localhost:{PORT}/"
CHROME_PATH = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
OUT_IMG = os.path.abspath(r"docs\img\dashboard.png")

print(f"Iniciando mock_server en puerto {PORT}...")
proc = subprocess.Popen(
    ["python", "tools/mock_server.py", "--puerto", str(PORT)],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL
)

try:
    # Esperar a que el servidor responda
    for _ in range(20):
        try:
            with urllib.request.urlopen(URL, timeout=1) as resp:
                if resp.status == 200:
                    break
        except Exception:
            time.sleep(0.3)
    else:
        raise RuntimeError("No se pudo conectar con el mock_server")

    print("mock_server listo. Esperando inicialización de telemetría...")
    time.sleep(2)

    print("Ejecutando Google Chrome Headless para captura...")
    cmd = [
        CHROME_PATH,
        "--headless=new",
        "--hide-scrollbars",
        "--window-size=1400,2100",
        f"--screenshot={OUT_IMG}",
        URL
    ]
    res = subprocess.run(cmd, capture_output=True, timeout=15)
    print("Captura finalizada.")

finally:
    proc.terminate()
    proc.wait()

if os.path.exists(OUT_IMG):
    size = os.path.getsize(OUT_IMG)
    print(f"OK: Imagen guardada en {OUT_IMG} ({size} bytes)")
else:
    print("ERROR: No se generó la imagen")

