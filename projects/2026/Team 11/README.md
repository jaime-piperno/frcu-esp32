# 📌 UTN FRCU – Tecnologías para la Automatización ESP32 2026

## 👥 Team

- **Team number:** 11 — proyecto base recibido para la Etapa 2.
- **Members:**
  - Ortiz, Facundo.
  - Carrasco, Fabiana.
  - Gomez, Thiago Kehler.
  - Torres, Sofía.
  - Pighetti, Luciano.

---

## 🤖 Project Description

- **Description:**
  Traductor de código Morse a texto mediante un ESP32. El usuario ingresa puntos y rayas con un pulsador; el programa mide la duración de las pulsaciones, reconoce los caracteres y muestra el mensaje en el monitor serie. Un LED rojo y un buzzer acompañan la entrada. La primera pulsación establece el ritmo y también cuenta como un punto del mensaje. Tras cinco segundos de inactividad se presenta el resultado final y se reinicia la calibración.
- **Technology used:**
  ESP32, C++ con Arduino Framework, Arduino IDE, monitor serie y Wokwi para la reproducción del circuito.

El código base ya incluye calibración de ritmo, filtro antirrebote, acumulación de caracteres, espacios entre palabras y cierre por inactividad. Estas funciones pertenecen a la entrega recibida del Team 11; no se presentan como aportes nuevos del Grupo 12.

Las secciones siguientes describen la Etapa 1 recibida. El firmware actual está integrado en `Codigo_traductor_morse`; sus cambios e instrucciones se detallan en **Stage 2 – Improvement**.

---

## 🔩 Components Used

| Component | Quantity | Notes |
|---|---|---|
| Placa de desarrollo ESP32 | 1 | Compatible con la selección ESP32 Dev Module de Arduino IDE; lógica de 3,3 V. |
| Módulo pulsador de tres pines | 1 | Señal a GPIO4, alimentación a 3V3 y masa a GND. Activo en nivel alto. |
| LED rojo | 1 | Indicación visual de los pulsos Morse; controlado desde GPIO2. |
| Resistencia de 220 Ω | 1 | En serie con el LED rojo para limitar su corriente. |
| Buzzer pasivo | 1 | Señal desde GPIO18 y retorno a GND; tono generado de 1000 Hz. |
| Breadboard / Jumper wires | 1 / cantidad necesaria | Montaje sin soldadura y distribución de masa común. |
| Power supply | 1 | USB de la placa; el cable debe permitir datos para programar y usar el monitor serie. |

---

## 🛠️ Usage Instructions

1. **Preparar el entorno.** Instalar Arduino IDE y el paquete **esp32 de Espressif Systems**. Versiones utilizadas: Arduino IDE 2.3.10 y paquete 3.3.12. El firmware base no requiere bibliotecas externas adicionales.
2. **Realizar el cableado con el USB desconectado.**
   - Módulo pulsador: señal `S` a **GPIO4**, alimentación a **3V3** y masa a **GND**.
   - LED rojo: **GPIO2 → resistencia de 220 Ω → ánodo**; cátodo a **GND**.
   - Buzzer pasivo: señal a **GPIO18** y retorno a **GND**.
   - Todos los componentes deben compartir GND. Los rieles negativos separados de la protoboard no están necesariamente unidos: utilizar el mismo riel o vincularlos con un jumper. No aplicar 5 V directamente a los GPIO.
3. **Código de la Etapa 1.** El [sketch recibido](https://github.com/jaime-piperno/frcu-esp32/blob/d142a2fe2c23029703915e054a45fcbc721bccbf/projects/2026/Team%2011/Codigo_traductor_morse/sketch_sep15a_copy_20260917223630_copy_20261008172016.ino) queda disponible en el historial de Git. Para utilizar esa versión, descargarlo como `TraductorMorseOriginal.ino` dentro de una carpeta `TraductorMorseOriginal`, abrirlo en Arduino IDE y cargarlo en **ESP32 Dev Module**. Para el firmware actual, seguir las instrucciones de Etapa 2.
4. **Abrir el monitor serie a 115200 baudios.** Este valor coincide con `Serial.begin(115200)` y corresponde a la comunicación con la PC; no define la duración de los puntos y rayas.
5. **Ingresar el mensaje.** La primera pulsación válida debe ser un punto: fija el ritmo y forma parte del texto. Las siguientes pulsaciones menores que 1,8 veces esa duración se interpretan como punto; las demás, como raya. El código descarta pulsaciones menores de 60 ms y aplica un filtro de cambios de 50 ms.
6. **Separar letras y palabras.** El fin de letra se reconoce a partir de `max(800 ms, 3 × unidad)` de pausa, y el espacio entre palabras a partir de `max(2000 ms, 7 × unidad)`. Tras cinco segundos sin pulsar se muestra `MENSAJE FINAL TRADUCIDO` y se prepara otro mensaje.

**Ejemplo:** con una unidad cercana a 300 ms, ingresar `SOS` como tres pulsaciones cortas, tres largas y tres cortas; usar unos 300/900 ms para punto/raya, pausas de unos 300 ms entre signos y aproximadamente 1,2 s entre letras. Esperar cinco segundos para obtener el mensaje final. El LED rojo externo y el buzzer deben acompañar cada pulsación.

---

## 🧪 Simulation

- **Wokwi project link:** [Reproducción del proyecto base](https://wokwi.com/projects/478614967296076801).
- **Diagram/export files:** la entrega de Etapa 1 incluía el código y el [informe del proyecto](<Trabajo tecnologías para la automatización. G11.pdf>). El circuito actualizado se encuentra en la sección de Etapa 2.
- El enlace Wokwi anterior corresponde a la reproducción realizada por el Grupo 12, no a una simulación publicada originalmente por el Team 11. En ella, un pulsador hacia 3V3 y una resistencia de 10 kΩ a GND representan la entrada y la polarización del módulo original de tres pines.

---

## 📝 Additional Notes

- **Challenges faced / technical decisions made:** medición de pulsos y pausas con `millis()`, umbrales relativos al primer punto y búsqueda en una tabla Morse. Durante la reproducción física se corrigió la conexión del LED rojo y se aseguró una masa común para recuperar el funcionamiento del buzzer.
- **Current limitations of the project:** la primera pulsación siempre cuenta como punto, por lo que el mensaje no puede comenzar directamente con una raya; el ritmo debe ser consistente. La tabla reconoce A–Z y 0–9, sin tildes ni puntuación. Un patrón desconocido se representa con `?`. El resultado se consulta desde la PC; la base no incluye pantalla ni comunicación por Internet.
- **Potential improvements for the future:** traducción inversa, selección física del modo e ingreso remoto de texto. Estas son las ampliaciones abordadas por el Grupo 12 en la sección siguiente.

---

## 🔄 Stage 2 – Improvement

- **Team number:** 12.
- **Members:**
  - Cassânego, Jesica.
  - Geist, Ileana.
  - Perez, Manuel.
  - Tournoud, Marcos.
  - Villaverde, Lara.

### 🚀 Improvement Description

- **What was improved:** se agregó traducción de texto a Morse desde el monitor serie o Telegram, un pulsador para alternar el modo y un LED azul para indicarlo. La primera pulsación Morse ahora solo calibra el ritmo y no forma parte del mensaje.
- **Why:** el proyecto original solo traducía Morse a texto y obligaba a comenzar con un punto. La mejora permite usar ambos sentidos, comenzar con cualquier letra y enviar frases desde el celular.

### 🔩 Component Changes

| Change | Component | Quantity | Notes |
|---|---|---|---|
| Added | Pulsador normalmente abierto | 1 | Entre GPIO25 y GND, con `INPUT_PULLUP`. |
| Added | LED azul | 1 | Indica texto → Morse; controlado desde GPIO26. |
| Added | Resistencia de 220 Ω | 1 | En serie con el LED azul. |
| Added | Cables jumper | -- | Conexiones nuevas y masa común. |

### 🛠️ Updated Usage Instructions

1. Con el USB desconectado, agregar **GPIO25 → pulsador → GND** y **GPIO26 → 220 Ω → ánodo del LED azul**; cátodo a GND.
2. **Crear el bot:** abrir el chat oficial [@BotFather](https://t.me/BotFather) en Telegram, enviar `/newbot` y elegir nombre y nombre de usuario. Guardar el token que entrega; permite controlar el bot y no debe publicarse. [Instrucciones oficiales](https://core.telegram.org/bots/tutorial#obtain-your-bot-token).
3. **Obtener el chat autorizado:** abrir una conversación privada con el bot nuevo desde la cuenta que lo utilizará y enviar `/start`. Con el firmware y cualquier otro consumidor del bot detenidos, consultar desde la PC `https://api.telegram.org/bot<TOKEN>/getUpdates`, reemplazando `<TOKEN>` por el token recibido. En la respuesta, localizar ese mensaje y copiar el número de `message.chat.id`, no `update_id` ni el nombre de usuario. Si `result` está vacío, enviar otro mensaje y repetir. No compartir la URL con el token. [Referencia de getUpdates](https://core.telegram.org/bots/api#getupdates).
4. **Configurar la placa:** copiar [Secretos.example.h](Codigo_traductor_morse/Secretos.example.h) como `Secretos.h`, en la misma carpeta del sketch. Completar los cuatro valores entre comillas:

   ```cpp
   #define MORSE_WIFI_SSID "NOMBRE_DE_LA_RED"
   #define MORSE_WIFI_PASSWORD "CLAVE_DE_LA_RED"
   #define MORSE_TELEGRAM_TOKEN "TOKEN_DEL_BOT"
   #define MORSE_TELEGRAM_CHAT "CHAT_ID_AUTORIZADO"
   ```

   Usar una red Wi-Fi de 2,4 GHz con Internet. Configurar `MORSE_TELEGRAM_CHAT` es lo que autoriza al chat; enviar `/start` por sí solo no lo habilita. Mantener `Secretos.h` y los binarios que lo incluyan fuera de la entrega pública.
5. **Cargar y conectar:** abrir [Codigo_traductor_morse.ino](Codigo_traductor_morse/Codigo_traductor_morse.ino) con sus archivos auxiliares en la misma carpeta y cargar en **ESP32 Dev Module**, paquete Espressif **3.3.12**, sin bibliotecas adicionales. Abrir el monitor serie a **115200 baudios**, final de línea **New Line**, consultar `/red` y esperar `[TG] Listo`. El bot debe estar sin webhook y sin otro consumidor de `getUpdates`; al arrancar se descartan los mensajes anteriores.
6. En **Morse → texto**, LED azul apagado, dar un punto de calibración de unos 300 ms y esperar `CALIBRACION LISTA` antes de ingresar el mensaje. Repetir la calibración para cada mensaje.
7. **Enviar texto desde Telegram:** presionar MODO para **texto → Morse**, LED azul encendido. Enviar una frase desde el chat autorizado. Por ejemplo, `SOS` produce tres puntos, tres rayas y tres puntos mediante el LED rojo y el buzzer; el bot informa inicio y finalización. También puede enviarse texto por el monitor serie. Se admiten hasta 80 caracteres: letras, números y espacios, sin tildes ni signos.
8. `/cancelar` detiene la emisión; el pulsador MODO cambia el sentido y descarta el mensaje en curso. En Telegram, `/estado` consulta el modo y `/ayuda` muestra las instrucciones.

### 🧪 Updated Simulation

- **Wokwi project link:** [versión bidireccional](https://wokwi.com/projects/479246379720787969), anterior a Telegram y a la calibración separada. La versión IoT aún no tiene enlace guardado.
- **Diagram/export files:** [diagram.json](diagram.json) y [código](Codigo_traductor_morse/Codigo_traductor_morse.ino).

Para Wokwi, crear un proyecto ESP32, copiar el contenido de `Codigo_traductor_morse.ino` en `sketch.ino` y agregar `Morse.h`, `Telegram.h`, `Telegram.cpp`, `TelegramPolicy.h` y `diagram.json`. Usar `Wokwi-GUEST` sin contraseña e ingresar `/telegram TOKEN_DEL_BOT|CHAT_ID_AUTORIZADO` en el monitor serie durante la ejecución; repetirlo después de reiniciar. No incluir `Secretos.h` ni guardar el token en el código del simulador. Con el diagrama enfocado, **Q** acciona Morse y **M** cambia de modo.

### 📝 Additional Notes

- **Challenges faced / technical decisions made:** separar calibración y mensaje, compartir la emisión entre serie y Telegram y ejecutar la red en otra tarea. Solo se aceptan mensajes del chat autorizado.
- **Remaining limitations:** frases de hasta 80 caracteres, sin tildes ni puntuación; Telegram requiere Internet.
