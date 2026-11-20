#!/usr/bin/env python3
"""Servidor de prueba del dashboard multi-nodo de Confort (Etapa 2), sin ESP32.

Sirve web/ y emula /ws y /api/* con el generador sintético para ambos nodos
(Aula y Pasillo) con las 5 magnitudes comparativas de confort ambiental.

Uso:
  python tools/mock_server.py                # http://localhost:8080
  python tools/mock_server.py --rapido       # el tiempo corre 10x más rápido
  python tools/mock_server.py --selftest     # verifica el generador sintético
"""
import argparse
import asyncio
import json
import math
import pathlib
import time
from collections import deque

WEB = pathlib.Path(__file__).resolve().parent.parent / "web"
CABECERA = "uptime_s,epoch,iaq,acc,eco2,bvoc,temp,hum,gas,ldr,sonido"


def simular(t: float, exterior: bool = False) -> dict:
    """Generador sintético idéntico al de firmware/aire_iot/sensor.h."""
    dos_pi = 6.283185307179586
    if not exterior:
        # AULA (INTERIOR)
        p = math.fmod(t, 1200.0)
        pico = 0 if p < 300 else 160 * (p - 300) / 120 if p < 420 else 160 * math.exp(-(p - 420) / 120)
        iaq = 65 + 12 * math.sin(dos_pi * t / 3600) + pico + 2 * math.sin(1.7 * t) * math.sin(0.31 * t)
        iaq = min(max(iaq, 0), 500)
        ldr = 78.0 + 5.0 * math.sin(dos_pi * t / 600)
        ruido_base = 32.0 + 8.0 * math.sin(0.7 * t) * math.sin(1.3 * t)
        ruido_pico = 35.0 * math.sin(0.1 * p) if (250 < p < 450) else 0
        sonido = min(max(ruido_base + max(ruido_pico, 0), 5), 100)
        return {
            "iaq": iaq,
            "eco2": 400 + 5 * max(iaq - 25, 0),
            "bvoc": 0.5 * math.exp(iaq / 100),
            "temp": 23.5 + 1.2 * math.sin(dos_pi * t / 7200) + 0.01 * pico,
            "hum": 48.0 + 4.0 * math.sin(dos_pi * t / 5400) + 0.02 * pico,
            "gas": 140000 * math.exp(-iaq / 150),
            "ldr": ldr,
            "sonido": sonido,
            "pulsos": int(sonido * 14.5),
            "acc": 0 if t < 120 else 1 if t < 300 else 2 if t < 600 else 3,
            "conectado": True,
        }
    else:
        # PASILLO (EXTERIOR)
        p = math.fmod(t + 150.0, 1200.0)
        iaq_ext = 45 + 10 * math.sin(dos_pi * t / 4000) + 4 * math.sin(0.9 * t)
        iaq_ext = min(max(iaq_ext, 10), 500)
        ldr_ext = 55.0 + 12.0 * math.sin(dos_pi * t / 1800)
        transito = 55.0 * math.sin(0.08 * p) if (500 < p < 650) else 0
        sonido_ext = min(max(18.0 + 5.0 * math.sin(0.4 * t) + max(transito, 0), 2), 100)
        return {
            "iaq": iaq_ext,
            "eco2": 410 + 2 * iaq_ext,
            "bvoc": 0.4 * math.exp(iaq_ext / 130),
            "temp": 20.8 + 1.8 * math.sin(dos_pi * (t - 600) / 7200),
            "hum": 52.0 + 6.0 * math.sin(dos_pi * t / 5400),
            "gas": 175000 * math.exp(-iaq_ext / 160),
            "ldr": ldr_ext,
            "sonido": sonido_ext,
            "pulsos": int(sonido_ext * 12.0),
            "acc": 0 if t < 90 else 1 if t < 240 else 3,
            "conectado": True,
        }


class EquipoMock:
    def __init__(self, velocidad: float):
        self.velocidad = velocidad
        self.sin_sensor = False
        self.inicio = time.monotonic()
        self.epoch_arranque = int(time.time())
        self.vivo = deque(maxlen=300)
        self.historial = deque(maxlen=1440)
        self.marcadores = deque(maxlen=64)
        self.proximo_id = 1
        self.ultima = None
        self.clientes = set()

    def ahora(self) -> float:
        return (time.monotonic() - self.inicio) * self.velocidad

    def paso(self) -> dict:
        t = self.ahora()
        if self.sin_sensor:
            return {"t": int(t), "sensor_ok": False}

        aula = simular(t, exterior=False)
        pasillo = simular(t, exterior=True)

        # Sesgo simulado del nodo exterior sin calibrar: el valor mostrado
        # (pasillo) es el corregido, y pasillo_crudo es el que entrega el sensor.
        correccion = {"temp": -2.0, "hum": 4.0, "iaq": -18.0}
        pasillo_crudo = dict(pasillo)
        pasillo_crudo["temp"] = pasillo["temp"] - correccion["temp"]
        pasillo_crudo["hum"] = pasillo["hum"] - correccion["hum"]
        pasillo_crudo["iaq"] = pasillo["iaq"] - correccion["iaq"]

        delta = {
            "iaq": aula["iaq"] - pasillo["iaq"],
            "temp": aula["temp"] - pasillo["temp"],
            "hum": aula["hum"] - pasillo["hum"],
            "ldr": aula["ldr"] - pasillo["ldr"],
            "sonido": aula["sonido"] - pasillo["sonido"],
        }

        delta_crudo = {
            "iaq": aula["iaq"] - pasillo_crudo["iaq"],
            "temp": aula["temp"] - pasillo_crudo["temp"],
            "hum": aula["hum"] - pasillo_crudo["hum"],
            "ldr": aula["ldr"] - pasillo_crudo["ldr"],
            "sonido": aula["sonido"] - pasillo_crudo["sonido"],
        }

        payload = {
            "t": int(t),
            "rol": "aula",
            "aula": aula,
            "pasillo": pasillo,
            "pasillo_crudo": pasillo_crudo,
            "delta": delta,
            "delta_crudo": delta_crudo,
            "calibracion": {
                "temp": correccion["temp"],
                "hum": correccion["hum"],
                "iaq": correccion["iaq"],
                "activa": True,
                "calibrando": False,
            },
            "sensor_ok": True,
            "equipo": {
                "rol": "aula",
                "modo": "STA",
                "ip": "127.0.0.1",
                "rssi": -55,
                "uptime": int(t),
                "heap": 182400,
                "bsec": "simulación (mock)",
                "remoto_ok": True,
            },
        }

        fila_csv = {
            "t": int(t),
            "iaq": aula["iaq"],
            "acc": aula["acc"],
            "eco2": aula["eco2"],
            "bvoc": aula["bvoc"],
            "temp": aula["temp"],
            "hum": aula["hum"],
            "gas": aula["gas"],
            "ldr": aula["ldr"],
            "sonido": aula["sonido"],
        }
        self.vivo.append(fila_csv)
        if not self.historial or int(t) - self.historial[-1]["t"] >= 60:
            self.historial.append(fila_csv)

        self.ultima = payload
        return payload

    def csv(self, filas, paso: int) -> str:
        lineas = [CABECERA]
        for f in filas:
            lineas.append(
                f'{f["t"]},{self.epoch_arranque + f["t"]},{f["iaq"]:.1f},{f["acc"]},'
                f'{round(f["eco2"])},{f["bvoc"]:.2f},{f["temp"]:.2f},{f["hum"]:.2f},'
                f'{round(f["gas"])},{round(f["ldr"])},{round(f["sonido"])}'
            )
        return "\n".join(lineas) + "\n"


def app(eq: EquipoMock):
    from aiohttp import web
    rutas = web.RouteTableDef()

    @rutas.get("/")
    async def index(_):
        return web.FileResponse(WEB / "index.html")

    @rutas.get("/ws")
    async def ws(request):
        sock = web.WebSocketResponse()
        await sock.prepare(request)
        eq.clientes.add(sock)
        try:
            async for _ in sock:
                pass
        finally:
            eq.clientes.discard(sock)
        return sock

    @rutas.get("/api/current")
    async def current(_):
        return web.json_response(eq.ultima or eq.paso())

    @rutas.get("/api/history")
    async def history(request):
        rango = request.query.get("range", "1h")
        if rango == "live":
            return web.Response(text=eq.csv(eq.vivo, 3), content_type="text/csv")
        minutos = {"1h": 60, "6h": 360, "24h": 1440}.get(rango)
        if not minutos:
            return web.json_response({"error": "range debe ser live, 1h, 6h o 24h"}, status=400)
        return web.Response(text=eq.csv(list(eq.historial)[-minutos:], 60), content_type="text/csv")

    @rutas.get("/api/export.csv")
    async def export(_):
        return web.Response(
            text=eq.csv(eq.historial, 60),
            content_type="text/csv",
            headers={"Content-Disposition": 'attachment; filename="confort_interior_exterior.csv"'},
        )

    async def muestreo(_app):
        async def bucle():
            while True:
                payload = eq.paso()
                msg = json.dumps(payload)
                for c in list(eq.clientes):
                    try:
                        await c.send_str(msg)
                    except ConnectionError:
                        eq.clientes.discard(c)
                await asyncio.sleep(3 / eq.velocidad)

        tarea = asyncio.create_task(bucle())
        yield
        tarea.cancel()

    a = web.Application()
    a.add_routes(rutas)
    a.router.add_static("/", WEB)
    a.cleanup_ctx.append(muestreo)
    return a


def precargar(eq: EquipoMock):
    for m in range(1440, 0, -1):
        t = -m * 60
        aula = {**simular(t + 86400, exterior=False), "acc": 3}
        eq.historial.append({
            "t": t,
            "iaq": aula["iaq"],
            "acc": aula["acc"],
            "eco2": aula["eco2"],
            "bvoc": aula["bvoc"],
            "temp": aula["temp"],
            "hum": aula["hum"],
            "gas": aula["gas"],
            "ldr": aula["ldr"],
            "sonido": aula["sonido"],
        })

    for s in range(300, 0, -1):
        t = -s * 3
        aula = simular(t + 86400, exterior=False)
        eq.vivo.append({
            "t": t,
            "iaq": aula["iaq"],
            "acc": 3,
            "eco2": aula["eco2"],
            "bvoc": aula["bvoc"],
            "temp": aula["temp"],
            "hum": aula["hum"],
            "gas": aula["gas"],
            "ldr": aula["ldr"],
            "sonido": aula["sonido"],
        })


def selftest():
    aula = simular(0, exterior=False)
    pasillo = simular(0, exterior=True)
    assert 0 <= aula["iaq"] <= 500
    assert 0 <= pasillo["iaq"] <= 500
    assert 0 <= aula["ldr"] <= 100
    assert 0 <= aula["sonido"] <= 100
    print("OK: Generador sintético de Confort (Etapa 2) validado correctamente.")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--rapido", action="store_true", help="tiempo 10x más rápido")
    ap.add_argument("--puerto", type=int, default=8080)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--sin-sensor", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        selftest()
    else:
        eq = EquipoMock(10 if args.rapido else 1)
        eq.sin_sensor = args.sin_sensor
        precargar(eq)
        from aiohttp import web
        web.run_app(app(eq), port=args.puerto)
