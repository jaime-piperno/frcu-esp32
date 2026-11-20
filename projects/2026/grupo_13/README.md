# Air Mouse ESP32 — Etapa 2 (Grupo 13)

**Mejora: Modo Precisión con indicación por LED RGB y buzzer**

Trabajo Práctico Integrador — Microcontroladores y RPA
Tecnologías para la Automatización · Ingeniería en Sistemas de Información · UTN FRCU · 2026

- **Proyecto original:** "Air Mouse ESP32" — Grupo 5 (Aldecoa Ludmila, Bertoldi Juliana, Braun Molina Magali, Rey Florencia, Scheffer Catalina).
- **Etapa 2 — Grupo 13:** Barreto Martín, Carmona Julián, Casagrande Joaquín, Flegler Mateo, Graciani Juan Pablo, Joannas Damián.
- **Docentes:** Sander Mauro, Piperno Jaime.

---

## Qué es

Un mouse inalámbrico basado en un ESP32 que se conecta a la PC por Bluetooth (BLE HID), sin drivers ni receptor USB:

- **Joystick** analógico → mueve el cursor.
- **Dos pulsadores** → clic izquierdo y clic derecho.
- **Encoder rotativo** → scroll vertical.
- **LED RGB y buzzer** → indican el estado del sistema.

**Mejora de la Etapa 2:** se agregó un **Modo Precisión**, que baja la sensibilidad del cursor para apuntar con exactitud. Se activa y desactiva **manteniendo apretado el botón derecho 1 segundo**. El modo actual se indica con el color del LED y con el buzzer. La mejora no modifica el hardware ni el cableado.

---

## Contenido del ZIP

```
AirMouse_ESP32_Etapa2_Grupo13/
├── README.md                      ← este archivo (instructivo)
├── src/
│   └── air_mouse_etapa2.ino       ← código con la mejora (comentado; cambios marcados con "// MEJORA:")
├── docs/
│   ├── Informe_Etapa2_Grupo13_AirMouse.pdf   ← informe completo de la Etapa 2
│   ├── diagram.json               ← circuito para Wokwi (sin cambios respecto del original)
│   ├── diagrama_wokwi.png         ← diagrama de conexión
│   └── grafico_sensibilidad.png   ← respuesta del sistema en ambos modos
└── original_grupo5/               ← proyecto tal como fue recibido
    ├── repositorio_Air-Mouse-ESP32/
    ├── Grupo 5 - Air Mouse ESP32 (informe Etapa 1).pdf
    └── VideoAirMouse.mp4
```

---

## Hardware y pines (sin cambios)

| Componente | Señal | GPIO ESP32 |
|---|---|---|
| Joystick KY-023 | VRx / VRy | 32 / 33 (entradas analógicas) |
| Joystick KY-023 | VCC / GND | 3V3 / GND |
| Pulsador derecho (clic derecho + cambio de modo) | — | 25 (INPUT_PULLUP, a GND) |
| Pulsador izquierdo (clic izquierdo) | — | 26 (INPUT_PULLUP, a GND) |
| Encoder KY-040 | CLK / DT | 18 / 19 |
| Encoder KY-040 | + / GND | 3V3 / GND |
| LED RGB KY-009 (cátodo común) | R / G / B / − | 14 / 13 / 27 / GND |
| Buzzer activo KY-012 | Señal / GND | 12 / GND |

> ℹ️ En el proyecto original el LED figuraba con R en el GPIO 13 y G en el 14, pero en el montaje físico esos canales quedaron intercambiados. Se corrigió en el código (`PIN_LED_R = 14`, `PIN_LED_G = 13`) y en `docs/diagram.json`, así que la tabla de arriba refleja la conexión real.

---

## Instalación (Arduino IDE)

### Requisitos

| Elemento | Versión / detalle |
|---|---|
| Arduino IDE | 2.x |
| Soporte de placas | **esp32** de *Espressif Systems*, **versión 3.x** |
| Librería | **HijelHID_BLEMouse** |
| Dependencia | **NimBLE-Arduino ≥ 2.3.8** |
| Placa | ESP32 Dev Module + cable USB **de datos** |
| Driver USB | CP210x o CH340, según el chip USB de la placa |

### Pasos

1. **Soporte de placas:** *Herramientas → Placa → Gestor de placas* → buscar `esp32` → instalar el paquete de **Espressif Systems** (3.x).
2. **Librerías:** *Programa → Incluir librería → Administrar bibliotecas* → instalar `HijelHID_BLEMouse`. Aceptar la instalación de dependencias; si no aparece, instalar también `NimBLE-Arduino`.
3. Abrir `src/air_mouse_etapa2.ino` y verificar que diga:
   ```cpp
   #define MODO_SIMULACION 0
   ```
4. *Herramientas → Placa → esp32 → **ESP32 Dev Module***. *Herramientas → Puerto* → el que aparece al conectar la placa (COMx en Windows, `/dev/ttyUSB0` en Linux).
5. **Subir.** Si se queda en `Connecting....` y falla, mantener apretado el botón **BOOT** de la placa hasta que aparezcan los porcentajes de carga.
6. Abrir el **Monitor Serie a 115200 baudios**: debe aparecer la calibración. **No tocar el joystick mientras el LED está amarillo** (3 s).
7. **Emparejar:** en la PC, *Configuración → Bluetooth → Agregar dispositivo* → `ESP32 Joystick Mouse`.

**Linux:** si al subir aparece `Permission denied` sobre el puerto, ejecutar `sudo usermod -aG dialout $USER` y volver a iniciar sesión.

---

## Uso

| Acción / estado | Resultado |
|---|---|
| Al encender | LED **amarillo**: calibración del joystick (no moverlo) |
| Sin conexión Bluetooth | LED **azul parpadeante** |
| Se conecta a la PC | LED fijo según el modo + 2 beeps |
| Se desconecta | 1 beep largo |
| Pulsación **corta** del botón derecho | Clic derecho (se envía al soltar) |
| **Mantener el botón derecho 1 s** | Cambia de modo |
| Modo **Normal** | LED **verde** · 1 beep · igual al proyecto original |
| Modo **Precisión** | LED **rojo** · 3 beeps · cursor ~3 veces más lento |

| Parámetro | Modo Normal | Modo Precisión |
|---|---|---|
| Sensibilidad (`SPEED`) | 0.010 | 0.003 |
| Desplazamiento máximo por ciclo | 12 | 4 |

Los valores se ajustan en la sección *"2. Parámetros de control"* del código: `SPEED_NORMAL`, `MAX_STEP_NORMAL`, `SPEED_PRECISION`, `MAX_STEP_PRECISION` y `LONG_PRESS_MS`.

---

## Prueba en Wokwi (simulador)

> Wokwi **no puede conectarse por Bluetooth** a una PC, por lo que si el Bluetooth se inicia, la simulación se reinicia en bucle con el error `task_wdt ... btController`. Por eso existe `MODO_SIMULACION`.

1. En [wokwi.com](https://wokwi.com) crear un proyecto nuevo con **ESP32**.
2. Reemplazar el contenido de `diagram.json` por el de `docs/diagram.json`, y el sketch por `src/air_mouse_etapa2.ino`.
3. En la pestaña **Library Manager**, agregar `HijelHID_BLEMouse`.
4. Cambiar a `#define MODO_SIMULACION 1` e iniciar la simulación.
5. Esperar la calibración. El LED pasa a verde y el Monitor Serie muestra `*** MODO SIMULACIÓN ACTIVO ***`.
6. Mover el joystick hasta el tope: el monitor muestra `moveX: 12` en Modo Normal.
7. Mantener apretado el botón derecho más de 1 s: LED rojo, 3 beeps, `-> Modo PRECISIÓN activado`. Con el joystick al tope se ve `moveX: 4`.

**Volver a poner `MODO_SIMULACION 0` antes de cargar el código en la placa.** Si queda en 1, el Bluetooth no se inicia.

---

## Diagnóstico

Con `#define DEBUG_SERIE 1` (en la placa física), el Monitor Serie imprime cada 300 ms:

```
[NORMAL]    rawX: 4095 | rawY: 1920 | centro: (1885, 1915) | moveX: 12 | moveY: 0 | conectado: SI
```

| Lo que se ve | Qué significa |
|---|---|
| `rawX` / `rawY` no cambian al mover el joystick | Joystick mal conectado (VRx→D32, VRy→D33, VCC→3V3, GND) |
| `rawX` cambia pero `moveX` queda en 0 | No sale de la zona muerta; revisar el `centro` (¿se movió el joystick al calibrar?) |
| `moveX` cambia y `conectado: SI`, pero el cursor no se mueve | Problema de emparejamiento: quitar el dispositivo en la PC y volver a emparejar |

**El Bluetooth se conecta y desconecta en bucle:** pasa si la placa quedó emparejada con más de una computadora y ambas intentan reconectarse. Quitar `ESP32 Joystick Mouse` de **todas** las computadoras, reiniciar la placa y emparejar solo una.
