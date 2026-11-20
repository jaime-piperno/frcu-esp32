# Air-Mouse-ESP32
Dispositivo de Punteo Inalámbrico (Air Mouse) con ESP32 - UTN FRCU

## Descripción del Proyecto
Dispositivo de puntero aéreo (Air Mouse) basado en el microcontrolador **ESP32**, diseñado para proporcionar control de cursor mediante un joystick analógico, desplazamiento con encoder rotativo y clics físicos independientes, utilizando comunicación inalámbrica de baja energía (**BLE HID**).

---

## Componentes de Hardware (Mapeo de Pines)

| Componente | Modelo / Descripción | Pin ESP32 |
| :--- | :--- | :--- |
| **Joystick** | KY-023 (Eje X) | `GPIO 32` (VRX) |
| **Joystick** | KY-023 (Eje Y) | `GPIO 33` (VRY) |
| **Botón Izquierdo** | Pulsador táctil (Pull-up) | `GPIO 26` |
| **Botón Derecho** | Pulsador táctil (Pull-up) | `GPIO 25` |
| **Encoder Rotativo** | KY-040 (CLK / Scroll) | `GPIO 18` |
| **Encoder Rotativo** | KY-040 (DT) | `GPIO 19` |
| **LED RGB** | KY-009 (Rojo, Verde, Azul) | `GPIO 13`, `14`, `27` |
| **Buzzer Activo** | KY-012 (Alertas sonoras) | `GPIO 12` |

---

## Características Técnicas
* **Conectividad BLE HID:** Emulación de dispositivo de interfaz humana por Bluetooth de bajo consumo utilizando la librería `HijelHID_BLEMouse`.
* **Calibración Dinámica:** Rutina de medición inicial de los centros del joystick al arrancar para evitar desvíos.
* **Señalización de Estados:** 
  * *Calibración:* LED en color Amarillo.
  * *Buscando conexión:* Parpadeo en color Azul.
  * *Conectado:* LED verde fijo con confirmación sonora mediante el buzzer.

---

## Simulación y Ejecución
El proyecto cuenta con simulación mediante la plataforma **Wokwi**, permitiendo validar la lógica de control y el comportamiento de las entradas/salidas digitales y analógicas antes del montaje físico.
