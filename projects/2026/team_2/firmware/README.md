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
3. **LiquidCrystal_I2C.h** - by *Martin Kubovčík* and *Frank de Brabander*.

## Pinout (physical build)

| Component | GPIO |
|-----------|------|
| Buzzer (passive module, signal) | 21 |
| RGB LED – Red | 19 |
| RGB LED – Green | 18 |
| RGB LED – Blue | 23 |
| Confirm button (`INPUT_PULLUP`) | 5 |
| Snooze button (`INPUT_PULLUP`) | 16 |
| Display | SDA = 4, SCL = 22 | 

The RGB LED module is common cathode (HIGH = on). Buttons go between the GPIO and GND.

## Configuration

Copy `RecuerdaMed/LocalConfig.example.h` to `RecuerdaMed/LocalConfig.h` and edit the local copy. Git ignores `LocalConfig.h` so WiFi credentials stay on your computer.

- `WIFI_SSID` / `WIFI_PASSWORD`: your 2.4 GHz WiFi network, in `LocalConfig.h`.
- `MQTT_BROKER`: IP of the machine running the backend's Mosquitto, in `LocalConfig.h`. `MQTT_PORT` is set to 1883 in the sketch.
- `DEVICE_ID`: the GUID of the device as registered in the backend (the development seed `ESP32-01` is `e4a3d7ac-d839-40dc-890b-0fa47284511f`).
- `SNOOZE_SEGUNDOS`: snooze duration sent to the backend. Real use: 300 s (5 min, the server default). Shorter values are used for testing.

## How to test

1. Start the backend stack from the root of the project (`docker compose up --build`), which also starts the Mosquitto broker.
2. Set the broker IP and WiFi credentials in `LocalConfig.h`, then upload the sketch.
3. Open the Serial Monitor at 115200 baud. You should see `[WIFI] Conectado`, `[MQTT] Conectado` and `[MQTT] Suscrito a …`.
4. **Alert:** when the backend sends a state with non-empty `alerts`, the device goes to `ALERTANDO` (blinking red LED + melody + displaying the medication and the dosage on the screen).
5. **Confirm:** press the confirm button → the device publishes `taken` → the backend re-publishes the state → the device returns to `IDLE` (green LED).
6. **Snooze:** press the snooze button while in `ALERTANDO` → the device publishes `snoozed` → the backend re-publishes with `snoozed: true` → steady blue LED, no melody.

## Wokwi

`diagram.json` is the diagram used in the simulation phase. Wokwi does not have the RGB LED module used in the physical build, so it uses separate LEDs and different pins. **For the physical wiring, use the pinout above.**

## Technical notes

- Valid events are only `taken` and `snoozed` (with the `medicationId` of the alert). The backend ignores anything else.
- Snooze is **server-side** (5 min, max. 3): the device does not count time locally.
- If WiFi or MQTT drops, the sketch reconnects by itself and, when back, receives the RETAINED state from the broker; nothing manual is needed.

## Bluetooth voice alarms

The firmware uses the ESP32 Bluetooth Classic stack and the ESP-IDF A2DP Source API from Arduino-ESP32 core 3.x. Open **Parlante Bluetooth** in the dashboard, put an A2DP speaker into pairing mode, scan, and select it. The backend stores the address and sends it on the retained `bluetooth/config` MQTT topic. The ESP32 scans and reports devices and connection status on `bluetooth/report`; after restart it receives the selected address again and reconnects.

For each alert, the ESP32 requests a Spanish WAV from `GET /api/devices/{deviceId}/speech`. The API builds the speech from every active medication alert, including dose amount, unit and the optional schedule message. The ESP32 streams the WAV as PCM through A2DP. Without a connected speaker, the normal buzzer alarm is used. Confirming or snoozing stops the voice. The API container runs Piper locally with the `es_AR-daniela-high` voice; its image downloads the model during `docker compose up --build`. No cloud service is needed during alerts. `MQTT_BROKER` in `LocalConfig.h` must point to the host running both Mosquitto and the API. If `API_PORT` differs from 8080, change it in the sketch.

In Arduino IDE select **ESP32 Dev Module**, a 4 MB flash size, and **Partition Scheme → No FS 4MB (2MB APP x2)**. The default 1.2 MB app partition is too small once A2DP is linked. This was compiled with Arduino-ESP32 core 3.3.12, PubSubClient 2.8 and ArduinoJson 7.4.3.

With Arduino CLI, from the project directory and after creating `LocalConfig.h`:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=no_fs --output-dir /tmp/recuerdamed-upload firmware/RecuerdaMed
arduino-cli upload --fqbn esp32:esp32:esp32:PartitionScheme=no_fs --input-dir /tmp/recuerdamed-upload --port /dev/ttyUSB0
```

This requires an original ESP32 with Bluetooth Classic. ESP32-C3, ESP32-S3 and BLE-only speakers cannot be used for A2DP Source. Legacy speakers that require a PIN other than `0000` need additional pairing support.
