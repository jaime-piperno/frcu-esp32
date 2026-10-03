# 📌 UTN FRCU – Tecnologías para la Automatización ESP32 2026

## 👥 Team
- **Team number:** 10
- **Members:**
  - Octavio Orcellet
  - Milton Ibarra
  - Francisco Bournissen
  - Edgardo Del Real
  - Marcos Cettour

---

## 🤖 Project Description
- **Description:**
**RecuerdaMed** is a smart medication reminder for elderly people with chronic treatments. An ESP32-based device signals with a melody and a light when a dose is due, and the user answers with two buttons: **confirm** the dose was taken, or **snooze** it. Every dose is registered in a database, so a relative can check remotely whether the medication was taken or whether a prolonged omission needs assistance.

  The system follows a **server-centric** design: the ESP32 has no clock or scheduler of its own. The backend decides when to alert and pushes the full device state over MQTT; the ESP32 only executes it (as a finite state machine) and publishes the user's button events back. Snooze is handled by the backend (5 minutes, up to 3 times) and a dose left unanswered is marked as missed.

- **Technology used:**
ESP32, Arduino IDE (Arduino-ESP32 core), Wokwi, WiFi + MQTT (Mosquitto, PubSubClient, ArduinoJson). Supporting software: .NET 10 (Minimal API, EF Core, MQTTnet, SignalR), PostgreSQL 17, React + Vite + TypeScript, Docker Compose.

---

## 🔩 Components Used
| Component | Quantity | Notes |
|---|---|---|
| ESP32 DevKit | 1 | NodeMCU-32S / ESP-WROOM-32 |
| Passive buzzer module | 1 | 3-pin (S / VCC / −), GPIO 21. Plays an ascending Do-Mi-Sol-Do melody with `tone()` |
| RGB LED module (CNT1 3_color RGB) | 1 | Common cathode, current-limiting SMD resistors already on the module. R = GPIO 19, G = GPIO 18, B = GPIO 23 |
| Push button (confirm) | 1 | GPIO 5, `INPUT_PULLUP` (active low) |
| Push button (snooze) | 1 | GPIO 16, `INPUT_PULLUP` (active low) |
| Breadboard / Jumper wires | 1 / several | Male-male and female-male jumpers |
| Power supply | 1 | USB (Micro-USB) |

### Pinout (physical build)
| Component | GPIO | Configuration |
|---|---|---|
| Buzzer (signal) | 21 | Digital output / `tone()` |
| RGB LED – Red | 19 | Digital output |
| RGB LED – Green | 18 | Digital output |
| RGB LED – Blue | 23 | Digital output |
| Confirm button | 5 | `INPUT_PULLUP` |
| Snooze button | 16 | `INPUT_PULLUP` |

Buttons are wired between the GPIO and GND, so no external resistors are needed. The buzzer and LED module share the 3.3 V and GND rails of the breadboard.

### LED / buzzer behavior
| State | Meaning | LED | Buzzer |
|---|---|---|---|
| `IDLE` | Nothing pending ("all up to date") | Green | Off |
| `ALERTANDO` | A dose is due | Red, blinking | Ascending melody |
| `SNOOZE` | Alert postponed | Blue | Off |
| After confirming | Dose registered | Green, 3 blinks | Off |

---

## 🛠️ Usage Instructions
1. **Install and configure**
   - Install the [Arduino IDE](https://www.arduino.cc/en/software) and add ESP32 board support.
   - Install the libraries **PubSubClient** (Nick O'Leary) and **ArduinoJson** v7 (Benoit Blanchon).
   - Open `firmware/RecuerdaMed/RecuerdaMed.ino` and edit the constants at the top for your environment: `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_BROKER` and `DEVICE_ID`.
   - ⚠️ `MQTT_BROKER` is the **local IP of the machine running the backend** (Mosquitto). The value in the sketch is only an example: **each user must change it to match their own network**.
2. **Wiring / circuit setup**
   - Wire the components following the pinout above. See [`firmware/README.md`](firmware/README.md) for details.
3. **Start the backend (MQTT broker + API + database)**
   ```bash
   cp .env.example .env
   docker compose up --build
   ```
   The API is available at http://localhost:8080 and the MQTT broker at port 1883. In development the backend creates an example device (`ESP32-01`) with a test medication, whose GUID is the `DEVICE_ID` used in the sketch.
4. **Upload and run the sketch**
   - Select the ESP32 board, upload the sketch and open the Serial Monitor at **115200 baud**.
   - Expected output: `[WIFI] Conectado`, `[MQTT] Conectado` and `[MQTT] Suscrito a …`.
   - When a dose is due the device turns into `ALERTANDO` (blinking red LED + melody). Press the **confirm** button to register the dose (the LED turns green) or **snooze** to postpone it (blue LED, no sound).

*(Screenshots and a demo video of the device are attached to the pull request.)*

---

## 🧪 Simulation
- Wokwi project link: _not available_
- Diagram/export files: [`firmware/diagram.json`](firmware/diagram.json), [`firmware/wokwi.toml`](firmware/wokwi.toml), [`firmware/libraries.txt`](firmware/libraries.txt)
- The system was first validated in Wokwi before moving to hardware. The Wokwi library does not offer the RGB LED module we were given, so the diagram is a **simplified approximation** (separate red/green LEDs, different pins). **The pinout in this README is the authoritative one for the physical build.**

---

## 🧩 Supporting software (backend and dashboard)
Because the ESP32 is governed by the server, the complete system is included so that it can be reproduced end to end:

```
firmware/   ESP32 sketch + Wokwi files          ← main deliverable of the course
backend/    .NET 10 API, domain, MQTT service and tests
frontend/   Web dashboard (React + Vite + TypeScript)
docker-compose.yml, .env.example   Stack: API + PostgreSQL + Mosquitto
```

- **Backend:** decides when to alert, handles snoozes (5 min, max. 3) and missed doses (no answer), persists everything in PostgreSQL, republishes the state over MQTT and notifies the dashboard via SignalR.
- **Dashboard:** shows the patient's adherence in real time so that a relative can follow it remotely.
- **MQTT contract** (per device, identified by its GUID):
  - `recuerdamed/{deviceId}/state` – backend → device. Full state snapshot in JSON (active alerts, snooze, missed doses, next dose). Published **retained** with QoS 1, so the device gets it immediately after a reconnection.
  - `recuerdamed/{deviceId}/events` – device → backend. Button events (`taken`, `snoozed`).

---

## 📝 Additional Notes
- **Technical decisions:**
  - Server-centric architecture: the device never polls and never re-alerts on its own; every transition of its state machine (`IDLE`, `ALERTANDO`, `SNOOZE`) depends on the JSON received.
  - Non-blocking firmware (`millis()` timers instead of `delay()`), software debounce for the buttons and `INPUT_PULLUP` inputs to avoid floating readings.
  - Automatic WiFi/MQTT reconnection (MQTT retry every 3 s) and unique MQTT client ID per boot.
- **Snooze time:** the real value is **5 minutes** (server default). For testing and the class demo the snooze time was shortened through the `SNOOZE_SEGUNDOS` constant in the sketch.
- **Current limitations:**
  - Depends on WiFi and on the backend being reachable; with no connection the status shown to the relative may become outdated, and a power loss stops the local reminders.
  - Audible and visual alerts must be perceivable for people with reduced hearing or sight.
  - The user could press snooze instead of confirm by mistake.
  - The dashboard is basic.
- **Potential improvements (stage 2):**
  - OLED/LCD display showing the medication name and dose time.
  - Bluetooth speaker for distributed audible alerts.
  - NFC/RFID reader (e.g. RC522) to confirm intake by scanning the medication package.
