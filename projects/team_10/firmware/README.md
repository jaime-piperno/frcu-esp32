# RecuerdaMed — MQTT firmware

ESP32 firmware for RecuerdaMed. It follows a **server-centric** design: the backend decides when to alert and pushes the device state over MQTT. The device never polls and never re-alerts by itself; its state machine (`IDLE`, `ALERTANDO`, `SNOOZE`) is driven only by the `state` message it receives.

## Files

| File | Description |
|------|-------------|
| `RecuerdaMed/RecuerdaMed.ino` | Sketch: WiFi + MQTT + state machine driven by the server state |
| `diagram.json` | Simplified Wokwi diagram (see the note below) |
| `wokwi.toml` | Wokwi configuration for the VS Code extension |
| `libraries.txt` | Libraries used by Wokwi |

## Libraries

In the Arduino IDE go to **Tools → Manage Libraries…** and install (exact names from the Library Manager):

1. **PubSubClient** — by *Nick O'Leary* (knolleary)
2. **ArduinoJson** — by *Benoit Blanchon* (bblanchon). Version 7 is used (`JsonDocument`).

## Pinout (physical build)

| Component | GPIO |
|-----------|------|
| Buzzer (passive module, signal) | 21 |
| RGB LED – Red | 19 |
| RGB LED – Green | 18 |
| RGB LED – Blue | 23 |
| Confirm button (`INPUT_PULLUP`) | 5 |
| Snooze button (`INPUT_PULLUP`) | 16 |

The RGB LED module is common cathode (HIGH = on). Buttons go between the GPIO and GND.

## Configuration

Everything is edited at the top of the sketch:

- `WIFI_SSID` / `WIFI_PASSWORD`: your 2.4 GHz WiFi network.
- `MQTT_BROKER` / `MQTT_PORT`: IP of the machine running the backend's Mosquitto (port 1883). **The IP in the sketch is an example; change it to match your network.**
- `DEVICE_ID`: the GUID of the device as registered in the backend (the development seed `ESP32-01` is `0d3a7287-5d1b-4593-9359-94f7133d86cf`).
- `SNOOZE_SEGUNDOS`: snooze duration sent to the backend. Real use: 300 s (5 min, the server default). Shorter values are used for testing.

## How to test

1. Start the backend stack from the root of the project (`docker compose up --build`), which also starts the Mosquitto broker.
2. Set `MQTT_BROKER` to that machine's IP, set your WiFi credentials, and upload the sketch.
3. Open the Serial Monitor at 115200 baud. You should see `[WIFI] Conectado`, `[MQTT] Conectado` and `[MQTT] Suscrito a …`.
4. **Alert:** when the backend sends a state with non-empty `alerts`, the device goes to `ALERTANDO` (blinking red LED + melody).
5. **Confirm:** press the confirm button → the device publishes `taken` → the backend re-publishes the state → the device returns to `IDLE` (green LED).
6. **Snooze:** press the snooze button while in `ALERTANDO` → the device publishes `snoozed` → the backend re-publishes with `snoozed: true` → steady blue LED, no melody.

## Wokwi

`diagram.json` is the diagram used in the simulation phase. Wokwi does not have the RGB LED module used in the physical build, so it uses separate LEDs and different pins. **For the physical wiring, use the pinout above.**

## Technical notes

- Valid events are only `taken` and `snoozed` (with the `medicationId` of the alert). The backend ignores anything else.
- Snooze is **server-side** (5 min, max. 3): the device does not count time locally.
- If WiFi or MQTT drops, the sketch reconnects by itself and, when back, receives the RETAINED state from the broker; nothing manual is needed.
