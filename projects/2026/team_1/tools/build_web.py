#!/usr/bin/env python3
"""Genera firmware/aire_iot/web_assets.h a partir de web/.

Inlinea en index.html cada <link rel="stylesheet" href="x.css"> y <script src="x.js"></script>
locales, comprime con gzip y lo escribe como arreglo PROGMEM.

Uso:
  python3 tools/build_web.py           # regenera el .h
  python3 tools/build_web.py --check   # falla si el .h no está al día con web/
"""
import gzip
import pathlib
import re
import sys

RAIZ = pathlib.Path(__file__).resolve().parent.parent
WEB = RAIZ / "web"
SALIDA = RAIZ / "firmware" / "Confort_Interior" / "web_assets.h"


def inlinear(html: str) -> str:
    html = re.sub(r'<link rel="stylesheet" href="([^"]+)">',
                  lambda m: "<style>" + (WEB / m[1]).read_text(encoding="utf-8") + "</style>", html)
    html = re.sub(r'<script src="([^"]+)"></script>',
                  lambda m: "<script>" + (WEB / m[1]).read_text(encoding="utf-8") + "</script>", html)
    assert 'src="http' not in html and 'href="http' not in html, "La página no puede usar recursos de internet"
    return html


def generar() -> tuple[str, int, int]:
    html = inlinear((WEB / "index.html").read_text(encoding="utf-8")).encode("utf-8")
    gz = gzip.compress(html, compresslevel=9, mtime=0)  # mtime=0: mismo input → mismo .h
    filas = [", ".join(f"0x{b:02x}" for b in gz[i:i + 16]) for i in range(0, len(gz), 16)]
    header = (
        "// GENERADO por tools/build_web.py — no editar a mano.\n"
        "// Editá los archivos de web/ y corré: python3 tools/build_web.py\n"
        "#pragma once\n#include <Arduino.h>\n\n"
        f"const size_t INDEX_HTML_GZ_LEN = {len(gz)};\n"
        "const uint8_t INDEX_HTML_GZ[] PROGMEM = {\n  " + ",\n  ".join(filas) + "\n};\n"
    )
    return header, len(html), len(gz)


if __name__ == "__main__":
    header, crudo, comprimido = generar()
    if "--check" in sys.argv:
        # Se compara la página DESCOMPRIMIDA: zlib de macOS, Linux y Windows puede producir
        # bytes gzip distintos para el mismo HTML, y eso no significa que esté desactualizado.
        actual = SALIDA.read_text(encoding="utf-8") if SALIDA.exists() else ""
        bytes_gz = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-f]{2})", actual.split("PROGMEM", 1)[-1]))
        try:
            embebido = gzip.decompress(bytes_gz).decode("utf-8")
        except (OSError, EOFError, UnicodeDecodeError):
            embebido = None
        esperado = inlinear((WEB / "index.html").read_text(encoding="utf-8"))
        if embebido != esperado:
            sys.exit("web_assets.h está desactualizado: corré python3 tools/build_web.py")
        print("web_assets.h al día")
    else:
        SALIDA.write_text(header, encoding="utf-8")
        print(f"web_assets.h: {crudo} B -> {comprimido} B gzip ({comprimido / 1024:.1f} KB)")
