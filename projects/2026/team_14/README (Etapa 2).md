# Semáforo de Ruido con alertas por Telegram (ESP32)

Mejora sobre el proyecto **Semáforo de Ruido** del grupo 14: cuando el ruido fuerte se sostiene, el ESP32 envía por Wi-Fi un mensaje a un bot de Telegram, y la alerta llega como notificación al celular de la persona responsable.
## Qué hace el proyecto
El proyecto original es un sistema de lazo cerrado basado en un ESP32 que mide el ruido ambiental y lo muestra como un semáforo (verde, amarillo y rojo), con una alarma sonora en el nivel crítico.
Cómo funciona el sistema original:
1. **Calibración:** al encender (o al presionar la perilla) se miden 15 ventanas en silencio. El máximo más un margen es el "piso de silencio".
2. **Medición:** se muestrea el micrófono en ventanas de 50 ms y se calcula la amplitud pico a pico (máximo menos mínimo). No se mide en decibeles, sino en amplitud del ADC.
3. **Clasificación:** la amplitud se compara con dos umbrales calculados a partir del piso (moderado y fuerte), desplazables con el encoder (sensibilidad de -10 a +10, 2 unidades por paso). Se aplica histéresis para evitar parpadeos en el límite.
4. **Respuesta:** LED RGB, pantalla LCD y buzzer. Mientras suena el buzzer (y 40 ms después) no se mide, para que el micrófono no capte la propia alarma.
## Qué agregamos nosotros
La documentación original planteaba como mejora las "Notificaciones multiplataforma". Implementamos **alertas por Telegram vía Wi-Fi**:
- Cuando el nivel **FUERTE** se mantiene al menos **2 segundos seguidos**, el ESP32 envía un mensaje al bot de Telegram.
- **Por qué:** el buzzer y el LED solo sirven a quien está cerca del dispositivo. Con la notificación, quien es responsable (por ejemplo, el dueño de la casa o el organizador) se entera aunque esté lejos. Telegram resuelve la entrega sin desarrollar una aplicación propia ni un servidor: el ESP32 solo hace una petición HTTPS a la API de bots.
La lógica de medición del programa original **no se modificó**. En el programa principal se agregaron solo tres líneas:

```cpp
#include "TelegramAvisos.h"                              // al inicio

iniciarTelegram();                                       // en setup(), después de iniciarLcd()

manejarTelegram(nivelActual == FUERTE, ultimaAmplitud);  // en loop(), después de imprimirDebug()
```
## Hardware y conexiones

| Componente              | Función                                                               | Pin                    |
| ----------------------- | --------------------------------------------------------------------- | ---------------------- |
| ESP32 (ESP32-WROOM-32)  | Lee sensores, procesa y controla actuadores                           | -                      |
| Micrófono KY-037        | Sensor de sonido (salida analógica)                                   | GPIO34 (ADC1)          |
| Encoder KY-040          | Ajusta sensibilidad (giro) y recalibra (pulsador)                     | CLK 18, DT 19, SW 21   |
| LCD 1602 con módulo I2C | Muestra nivel, amplitud y barra gráfica con umbrales                  | SDA 32, SCL 33         |
| LED RGB cátodo común    | Semáforo: verde, amarillo, rojo; azul = calibrando                    | 26 (R), 25 (G), 27 (B) |
| Buzzer de 3 pines       | Alarma intermitente en nivel fuerte (150 ms on / 150 ms off, 2000 Hz) | GPIO23                 |
El micrófono está en el GPIO34 (ADC1) a propósito: ese ADC funciona sin conflictos con el Wi-Fi.
## Archivos del repositorio

| Archivo                                   | Rol                                                                    |
| ----------------------------------------- | ---------------------------------------------------------------------- |
| `semaforo_de_ruido.ino`                   | Programa original con los tres agregados mínimos                       |
| `TelegramAvisos.h` / `TelegramAvisos.cpp` | Conexión Wi-Fi, sincronización horaria, envío HTTPS y reglas de avisos |
| `credenciales.h`                          | Plantilla de credenciales (sin datos reales) (ver paso 4)              |
> En Arduino IDE, el archivo `.ino` debe estar dentro de una carpeta con el mismo nombre (`semaforo_de_ruido/semaforo_de_ruido.ino`), junto con los demás archivos.
## Puesta en marcha
### Antes de empezar
- Una **red Wi-Fi de 2.4 GHz con internet**, sin portal de inicio de sesión (el ESP32 no soporta 5 GHz).
- Una cuenta de Telegram.
- Arduino IDE con la placa **ESP32 Dev Module**, el core **esp32 de Espressif 3.3.12** y la misma biblioteca **LiquidCrystal I2C** que usa el proyecto original. No se requieren bibliotecas adicionales.
### Paso 1: Crear el bot
1. Abrir [https://t.me/BotFather](https://t.me/BotFather) en Telegram.
2. Enviar `/newbot`.
3. Elegir un nombre y un usuario que termine en `bot`.
4. Copiar el **token** (es la clave privada del bot).
### Paso 2: Iniciar el chat con el bot
Abrir tu bot y enviar `/start`. Sin este paso el bot no puede escribirte.
### Paso 3: Obtener el chat ID
1. Abrir en el navegador: https://api.telegram.org/botTOKEN/getUpdates reemplazando `TOKEN` por el token completo (todo junto, después de `bot`).
2. Buscar `id` dentro de `chat`. **No** uses `update_id` ni `message_id`.
3. Si aparece `"result":[]`, mandá otro mensaje al bot y recargá la página.
### Paso 4: Completar las credenciales
Copiá la plantilla y completala con tus datos, **conservando las comillas**:

```
#pragma once

const char *WIFI_NOMBRE = "TU_WIFI";
const char *WIFI_CLAVE = "TU_PASSWORD";
const char *TELEGRAM_TOKEN = "TOKEN_DE_BOTFATHER";
const char *TELEGRAM_CHAT_ID = "ID_DEL_CHAT";
```

> **Importante:** el token y la clave Wi-Fi son privados. No subas `credenciales.h` completado ni compartas capturas que muestren el token (por ejemplo en la barra del navegador). Si el token se filtra, revocalo con `/revoke` en BotFather.
### Paso 5: Compilar y cargar
1. Abrir `semaforo_de_ruido.ino` en Arduino IDE.
2. Seleccionar la placa **ESP32 Dev Module**.
3. Compilar y cargar.
### Paso 6: Verificar
1. Abrir el Monitor Serie a **115200 baudios**.
2. Debe aparecer **"Wi-Fi conectado"** y luego **"Telegram listo"**.
3. Debe llegar un mensaje de **PRUEBA** al celular.
## Pruebas y diagnóstico
### Probar la alerta
1. Presioná la perilla y hacé silencio para recalibrar.
2. Generá ruido hasta que la pantalla marque **FUERTE** durante al menos 2 segundos.
3. El Monitor Serie debe indicar que el aviso fue enviado, y el mensaje debe llegar al celular.
### Tabla de diagnóstico

| Mensaje en el Monitor Serie          | Causa probable                                                                        |
| ------------------------------------ | ------------------------------------------------------------------------------------- |
| Completar credenciales.h             | Hay campos sin completar; no se inicia la red                                         |
| Wi-Fi intentando conectar            | Revisar nombre, clave y que la red sea de 2.4 GHz                                     |
| Wi-Fi conectado sin "Telegram listo" | Revisar acceso a internet y sincronización horaria (NTP); cada 15 s informa si espera |
| HTTP 401                             | Token incorrecto                                                                      |
| HTTP 400                             | Chat ID incorrecto o falta enviar `/start` al bot                                     |
| HTTP 403                             | El bot está bloqueado                                                                 |
| Código negativo                      | Fallo de red o HTTPS; revisar internet y hora                                         |
| Aviso enviado pero sin sonido        | Notificaciones, chat silenciado, modo silencio o Enfoque                              |
## Decisiones de diseño
- **Tarea separada para el envío.** Las esperas de internet son largas e impredecibles. Si ocurrieran en `loop()`, detendrían el muestreo del micrófono y el control del buzzer. Por eso el envío se hace en una tarea aparte y el semáforo local sigue funcionando aunque no haya red.
- **Ruido sostenido (2 s).** Un pico breve, como un aplauso, no dispara aviso. Es un filtro temporal contra falsas alarmas.
- **Intervalo mínimo entre avisos (30 s).** Evita inundar el chat si el ruido continúa. Si sigue el ruido fuerte, el aviso puede repetirse.
- **HTTPS con certificado validado y sincronización horaria (NTP).** La validación del certificado necesita la hora correcta; por eso Telegram se declara "listo" solo después de sincronizar la hora.
- **Reconexión automática del Wi-Fi y descarte de alertas viejas en la cola.** No se guarda historial de alertas ocurridas sin internet, porque una alerta atrasada ya no sirve.
- **Mensaje de PRUEBA** una vez por arranque, al sincronizar la hora, para verificar el canal sin necesidad de hacer ruido.
### Ajustar los tiempos
Los tiempos se cambian al inicio de `TelegramAvisos.cpp`, en milisegundos:

```cpp
static const uint32_t RUIDO_SOSTENIDO_MS = 2000;
static const uint32_t INTERVALO_AVISOS_MS = 30000;
```

