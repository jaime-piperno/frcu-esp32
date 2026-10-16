# Semáforo de ruido — ESP32 + KY-037

Monitor de nivel sonoro para ambientes cerrados. Mide el ruido de forma continua
y lo señaliza con un LED RGB de tres estados (verde, amarillo y rojo), una alarma
sonora en el nivel crítico y una pantalla LCD con el nivel, la amplitud medida y
una barra gráfica.

Trabajo práctico integrador — Tecnologías para la Automatización, UTN FRCU.

---

## Materiales

| Cantidad | Componente |
|---|---|
| 1 | ESP32 DevKit V1 (30 pines) |
| 1 | Micrófono KY-037 |
| 1 | LED RGB de cátodo común |
| 3 | Resistencias de 220 Ω |
| 1 | Encoder rotativo incremental con pulsador (tipo KY-040) |
| 1 | Buzzer de 3 pines |
| 1 | Pantalla LCD 1602 con módulo I2C (PCF8574) |
| 1 | Protoboard y cables dupont |
| 1 | Cable micro USB |

---

## Conexiones

| Componente | Señal | Pin ESP32 | Alimentación |
|---|---|---|---|
| KY-037 | A0 | GPIO34 | **3V3** |
| | D0 | sin conectar | |
| LED RGB | R (220 Ω) | GPIO26 | cátodo a GND |
| | G (220 Ω) | GPIO25 | |
| | B (220 Ω) | GPIO27 | |
| Buzzer | señal | GPIO23 | 3V3 o 5 V |
| Encoder | CLK | GPIO18 | **3V3** |
| | DT | GPIO19 | |
| | SW | GPIO21 | |
| LCD 1602 I2C | SDA | GPIO32 | **VIN (5 V)** |
| | SCL | GPIO33 | |

Todos los GND van a una misma fila del protoboard, conectada al GND del ESP32.

### Advertencias

**El KY-037 va a 3V3, nunca a 5 V.** Su salida A0 entra a GPIO34, que tolera
como máximo 3,3 V. Alimentado a 5 V el módulo puede entregar hasta esa tensión
y dañar el pin.

**El encoder va a 3V3.** Sus resistencias pull-up dejan CLK, DT y SW en el nivel
de su alimentación, y esos tres pines van directo al ESP32.

---

## Puesta en marcha

1. Instalar el **core ESP32** desde el Gestor de Tarjetas del IDE de Arduino:
   buscar "esp32 by Espressif Systems". Se requiere la versión 2.0.3 o superior,
   o cualquier 3.x.
2. Instalar la librería **LiquidCrystal I2C** (Frank de Brabander) desde el
   Gestor de Librerías. El aviso de que la librería es para arquitectura AVR es
   inofensivo: internamente solo usa `Wire` y funciona en ESP32.
3. En Herramientas seleccionar la placa **DOIT ESP32 DEVKIT V1** y el puerto
   correspondiente.
4. Abrir `semaforo_de_ruido/semaforo_de_ruido.ino`, compilar y cargar.
5. Abrir el Monitor Serie a **115200 baudios**.

---

## Ajuste del micrófono

El KY-037 entrega una señal montada sobre un offset de continua que depende de
la posición de su potenciómetro. Ese offset tiene que quedar centrado dentro del
rango del ADC: si se va contra alguno de los extremos, la señal se recorta y la
amplitud medida deja de ser confiable.

1. Cargar `scripts/test_atenuaciones/test_atenuaciones.ino` y abrir el Monitor Serie.
2. En silencio, girar el potenciómetro del módulo mientras se observa la línea
   de 11 dB. El objetivo es un **DC cercano a 520**, sin que el mínimo llegue a
   0 ni el máximo a 4095.
3. Aplaudir o hablar fuerte para verificar que la amplitud pico a pico aumenta
   de forma clara respecto del silencio.

Que un aplauso muy fuerte sature no es un problema: queda muy por encima del
umbral rojo y no hace falta distinguirlo de un grito. Lo que importa es que el
rango entre silencio, voz y música quede bien resuelto.

El proyecto usa `ADC_11db` (fondo de escala ≈ 2,45 V). Cambiar la atenuación
obliga a rehacer la calibración y a recalcular los márgenes.

---

## Uso

### Calibración

Al encender, el sistema mide el ruido de fondo durante unos segundos y lo toma
como piso de silencio. Durante ese lapso el LED está azul y la pantalla avisa
"Calibrando...". Hay que quedarse en silencio.

El **pulsador del encoder** repite la calibración en cualquier momento. Conviene
usarlo al cambiar de ambiente.

El piso y la sensibilidad se pierden al apagar, y es intencional: el sistema se
reajusta al ambiente en cada encendido. Como el encoder es incremental, la
posición física de la perilla no importa.

### Niveles

| LED | Estado | Significado |
|---|---|---|
| Verde | SILENCIO | Nivel acústico óptimo |
| Amarillo | MODERADO | Nivel moderado |
| Rojo | FUERTE | Nivel crítico, con alarma sonora intermitente |
| Azul | — | Calibrando |

### Pantalla

**Fila superior:** el nivel actual y la amplitud medida, por ejemplo
`MODERADO  A  185`. También muestra avisos temporales al girar el encoder y al
calibrar.

**Fila inferior:** una barra de ruido de 16 celdas con 5 pasos cada una. Una
marca punteada señala el umbral amarillo y una marca llena el umbral rojo. La
escala es fija, así que al girar el encoder se ve cómo se desplazan las marcas.
La barra sube al instante y baja de forma gradual; eso es solo visual y no
afecta a la detección.

### Encoder

Girarlo ajusta la sensibilidad entre −10 y +10. El sentido horario aumenta la
sensibilidad, es decir que baja los dos umbrales y el semáforo reacciona con
menos ruido. Cada detente mueve los umbrales 2 unidades de amplitud.

---

## Ajuste de los umbrales

Los umbrales son relativos al piso de silencio, no valores absolutos:

```
umbral amarillo = piso + MARGEN_AMARILLO_BASE - sensibilidad * 2
umbral rojo     = piso + MARGEN_ROJO_BASE     - sensibilidad * 2
```

Para reajustarlos a un ambiente distinto, con el Monitor Serie abierto hay que
anotar la `Amplitud` en silencio, hablando normal, hablando fuerte y con música
fuerte. Cada margen se elige a mitad de camino entre las dos situaciones que se
quieren separar, y se reemplaza en las constantes correspondientes del `.ino`.

`HISTERESIS` define cuánto hay que bajar respecto del umbral para retroceder de
nivel, y evita que el LED oscile entre dos colores cuando el ruido se queda
justo en el límite. Los dos `static_assert` del código verifican que los tres
valores sean coherentes entre sí y no compilan si no lo son.

---

## Contenido

| Archivo | Descripción |
|---|---|
| `semaforo_de_ruido/semaforo_de_ruido.ino` | Código fuente comentado del proyecto |
| `diagram.json` | Diagrama de conexión, formato Wokwi |
| `diagrama.png` | Captura del diagrama |
| `scripts/test_atenuaciones/` | Compara las cuatro atenuaciones del ADC |

Proyecto en Wokwi: https://wokwi.com/projects/476234030854915073

### Scripts de diagnóstico

**`test_atenuaciones.ino`** mide durante 500 ms con cada una de las cuatro
atenuaciones del ADC e informa el DC, el mínimo, el máximo y la amplitud pico a
pico de cada una, marcando las que saturan. Es la herramienta para ajustar el
potenciómetro del KY-037 y para verificar que el micrófono entrega señal.

