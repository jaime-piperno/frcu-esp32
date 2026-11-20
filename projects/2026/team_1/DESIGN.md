---
name: Aire IoT
description: Registrador de calidad de aire interior servido desde la flash de un ESP32.
colors:
  papel: "#f6f8fa"
  papel-2: "#ffffff"
  tinta: "#1b2330"
  tinta-2: "#4a5566"
  impresion: "#8795ab"
  regla: "#d4dbe5"
  pluma: "#5a3fd1"
  pluma-evento: "#a4541a"
  foco: "#5a3fd1"
  ok: "#23794a"
  reg: "#9a6a0c"
  mal: "#b3321f"
  b0: "#2e8b57"
  b1: "#6a9a2e"
  b2: "#c29b1d"
  b3: "#d27a24"
  b4: "#c4432d"
  b5: "#97263f"
  b6: "#5c2352"
  reticula: "rgba(62, 88, 128, .30)"
  reticula-fina: "rgba(62, 88, 128, .11)"
typography:
  display:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Helvetica Neue, sans-serif"
    fontSize: "5.5rem"
    fontWeight: 650
    lineHeight: 0.95
    letterSpacing: "-0.04em"
    fontFeature: "tnum"
  headline:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Helvetica Neue, sans-serif"
    fontSize: "1.5rem"
    fontWeight: 650
    lineHeight: 1.2
    letterSpacing: "-0.01em"
  title:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Helvetica Neue, sans-serif"
    fontSize: "1.375rem"
    fontWeight: 600
    letterSpacing: "-0.01em"
    fontFeature: "tnum"
  body:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Helvetica Neue, sans-serif"
    fontSize: "1rem"
    fontWeight: 400
    lineHeight: 1.5
    fontFeature: "tnum"
  body-sm:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Helvetica Neue, sans-serif"
    fontSize: "0.8125rem"
    fontWeight: 400
    lineHeight: 1.5
  label:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Helvetica Neue, sans-serif"
    fontSize: "0.8125rem"
    fontWeight: 600
    letterSpacing: "0.04em"
    fontFeature: "c2sc, smcp"
rounded:
  control: "2px"
  lampara: "50%"
spacing:
  xs: "4px"
  sm: "8px"
  md: "12px"
  lg: "16px"
  xl: "24px"
  xxl: "40px"
components:
  boton:
    backgroundColor: "{colors.papel-2}"
    textColor: "{colors.tinta}"
    rounded: "{rounded.control}"
    padding: "8px 16px"
    height: "44px"
  boton-hover:
    backgroundColor: "{colors.tinta}"
    textColor: "{colors.papel-2}"
  boton-primario:
    backgroundColor: "{colors.mal}"
    textColor: "{colors.papel-2}"
    rounded: "{rounded.control}"
    padding: "8px 16px"
    height: "44px"
  boton-primario-hover:
    backgroundColor: "{colors.tinta}"
    textColor: "{colors.papel-2}"
  segmentado-activo:
    backgroundColor: "{colors.tinta}"
    textColor: "{colors.papel-2}"
    padding: "0 12px"
    height: "40px"
  segmentado-inactivo:
    backgroundColor: "transparent"
    textColor: "{colors.tinta}"
    padding: "0 12px"
    height: "40px"
  input:
    backgroundColor: "{colors.papel-2}"
    textColor: "{colors.tinta}"
    typography: "{typography.body}"
    rounded: "{rounded.control}"
    padding: "8px 10px"
    height: "44px"
  grafico:
    backgroundColor: "{colors.papel-2}"
---

# Design System: Aire IoT

## Overview

**Creative North Star: "El Termohigrógrafo de Tambor"**

La página es un instrumento registrador, no un tablero de tarjetas. Una pluma violeta escribe sobre papel reticulado frío; la retícula está impresa en una sola tinta azul pizarra y las zonas de nivel se imprimen con tono y trama. Todo lo demás (rótulo superior, canales de magnitudes, controles del lazo) se comporta como la caja del instrumento: placas de rótulo en versalitas chicas, reglas de 1 px, esquinas casi rectas y ninguna sombra.

La densidad es de aparato de medición: secciones separadas por reglas horizontales finas, cifras tabulares que no saltan cuando cambian, jerarquía construida solo con tamaño y peso. La lectura principal es enorme (5.5rem, 6rem en escritorio) y el resto baja escalonado sin cromo de tarjetas. El modo nocturno es la misma caja lacada negro-azulada: retícula tenue, tinta luminosa, trama más suave.

Toda la tipografía es la del sistema porque el producto no puede cargar fuentes (la página vive gzipeada en la flash y funciona sin internet); el carácter sale del peso 650, el tracking negativo en cifras y las versalitas de las placas, no de la familia.

**Key Characteristics:**
- Papel frío + retícula de una sola tinta + pluma violeta como único color de dato.
- Niveles siempre con tono y trama o forma, nunca solo con color.
- Bordes de 1 px, radio de 2 px, cero sombras.
- Placas de rótulo en versalitas chicas (`all-small-caps`, 600, +0.04em).
- Cifras tabulares en todo el documento, incluidos los inputs.
- Tema claro y oscuro con los mismos nombres de token; el oscuro se activa por `prefers-color-scheme` o `data-theme="dark"`.

## Colors

Una sola tinta de impresión fría sobre papel, una pluma anilina para el dato y una escala Bosch de siete bandas para el nivel.

### Primary
- **Violeta Anilina** (`pluma`): el trazo de la pluma en el tambor (2 px), las mini trazas de cada canal (1.5 px), el punto de tinta en el último valor, el punto del cursor, el caret de los inputs y la selección de texto. Nocturno: `#b3a1ff`.
- **Anillo de Foco** (`foco`): mismo valor que la pluma; contorno de 2 px con separación de 2 px en todo `:focus-visible`. Nocturno: `#b3a1ff`.

### Secondary
- **Sepia de Eventos** (`pluma-evento`): la segunda pluma. Línea vertical de 1.5 px y rótulo en 600/11px sobre los gráficos, y la hora en la lista de eventos. Nocturno: `#f0a060`.

### Tertiary
- **Escala Bosch de siete bandas** (`b0`…`b6`, verde mar a ciruela): solo para el nivel IAQ. Aparecen en la escala impresa (anchos proporcionales 5/5/5/5/5/10/15 sobre 0–500), en las zonas del tambor (`b0`, `b2`, `b4`) y en los LEDs del lazo (verde `b0`, amarillo `b2`, rojo `b4`). Nocturno: `#3fb37a #8cc04a #e2bf3a #f0963e #ef6a4f #d0506c #a8649a`.
- **Estados de canal** (`ok`, `reg`, `mal`): texto y marcador de estado de cada magnitud y mensajes de formulario. `mal` además es el color de la alarma y del aviso de conexión (borde 1 px + fondo al 7–8% sobre papel). Nocturno: `#5ccc8c #e5b94a #ff7b63`.

### Neutral
- **Papel de Registro** (`papel`): fondo de la página. Nocturno `#0e131a`.
- **Papel Blanco** (`papel-2`): superficie de gráficos, inputs y botones; también el texto sobre fondos llenos. Nocturno `#141b24`.
- **Tinta** (`tinta`): texto principal, regla inferior del rótulo, bordes de botones, escala e índice. Nocturno `#e5eaf1`.
- **Tinta Segunda** (`tinta-2`): placas de rótulo, unidades, ayudas, ejes de los gráficos. Nocturno `#a5b0c0`.
- **Tinta de Impresión** (`impresion`): borde de inputs, lámparas apagadas, líneas de umbral punteadas (4/3) y el brazo de la pluma. Nocturno `#5d6b80`.
- **Regla** (`regla`): separadores entre secciones y canales, borde del tambor, relleno de LEDs apagados. Nocturno `#26303d`.
- **Retícula** (`reticula`, `reticula-fina`): la grilla gruesa con rótulos y la fina sin rótulos del papel (cada 10 IAQ y una subdivisión de tiempo). Nocturno `rgba(150,175,210,.20)` y `.07`.

### Named Rules
**La Regla de la Pluma Única.** El violeta es tinta de dato y de foco. No se usa para decorar, ni para títulos, ni como fondo.

**La Regla de Tono y Trama.** Ningún nivel se comunica solo con color: las bandas 2–5 llevan trama de densidad creciente (rayado a 135°, luego cruzado a 45°), las zonas del tambor van limpias, rayadas y cuadriculadas, y el estado de cada canal cambia de forma (cuadrado lleno, cuadrado rayado, rombo).

**La Regla del Rojo que Actúa.** `mal` solo llena un fondo cuando hay que actuar: el botón de silenciar alarma. En los demás casos va como borde y texto sobre un tinte del 7–8%.

## Typography

**Display Font:** system-ui (con -apple-system, Segoe UI, Roboto, Helvetica Neue, sans-serif)
**Body Font:** la misma pila
**Label/Mono Font:** la misma pila en versalitas chicas

**Character:** una sola familia del sistema, forzada por la restricción sin fuentes. La voz de instrumento sale de las cifras tabulares, el peso 650 con tracking negativo en la lectura y las placas en versalitas.

### Hierarchy
- **Display** (650, 5.5rem → 6rem desde 56rem, 0.95): solo la cifra IAQ principal.
- **Headline** (650, 1.5rem, 1.2, `text-wrap: balance`): la categoría del nivel, precedida por su muestra impresa de 1rem.
- **Title** (600, 1.375rem → 1.75rem en escritorio, −0.01em): el valor de cada canal; la unidad va en 500/0.8125rem en tinta segunda. La recomendación usa 1.125rem/400.
- **Body** (400, 1rem, 1.5): texto corrido e inputs. Notas con ancho máximo de 60–70ch.
- **Body-sm** (400, 0.8125rem): ayudas, lectura del cursor, subtítulo, nota al pie. Las marcas de la escala bajan a 0.6875rem.
- **Label** (600, 0.8125rem, +0.04em, `all-small-caps`, tinta segunda): títulos de sección (a 1rem), rótulos de formulario, términos de la ficha del equipo y el rótulo "Alarma".

### Named Rules
**La Regla de las Cifras Fijas.** `font-variant-numeric: tabular-nums` en el body y en los inputs; ningún número cambia de ancho al actualizarse.

**La Regla de la Jerarquía por Tamaño.** La jerarquía se arma con tamaño y peso sobre una sola familia; no se agregan tarjetas, fondos ni íconos para jerarquizar.

## Layout

Columna única centrada con ancho máximo de 76rem y margen lateral de 1rem, respetando las zonas seguras (`env(safe-area-inset-*)`). Las secciones se apilan con 1.5rem de padding vertical y una regla de 1 px debajo; el rótulo superior cierra con una regla en tinta plena.

Desde 56rem la página se divide 5fr / 7fr con 2.5rem de separación: lectura a la izquierda, tambor en vivo a la derecha estirado a la altura de la lectura. Magnitudes, historial, lazo de control y equipo ocupan el ancho completo. Los canales pasan de filas (nombre/valor arriba, estado/traza abajo) a cinco columnas separadas por reglas verticales; el formulario de umbrales pasa a cinco columnas y la ficha del equipo a seis. Debajo de 26rem se oculta el subtítulo.

Los gráficos toman el ancho del contenedor y una altura de la mitad del ancho, acotada entre 192 y 320 px. El ritmo de espaciado es 4 / 8 / 12 / 16 / 24 / 40 px.

## Elevation & Depth

Plano por completo. No hay `box-shadow` en ningún elemento. La profundidad se da por superficie y regla: el papel blanco (`papel-2`) de los gráficos y los inputs se apoya sobre el papel de registro con un borde de 1 px, y las secciones se separan con reglas finas.

### Named Rules
**La Regla del Papel Plano.** Si algo necesita separarse, recibe una regla de 1 px o cambia a `papel-2`; nunca una sombra.

## Shapes

Bordes de 1 px y esquinas de 2 px en botones, grupos segmentados e inputs; el resto (tambor, escala, avisos, alarma) es recto. Los únicos círculos son indicadores físicos: la lámpara de conexión (0.75rem) y los LEDs del lazo (1.125rem). El índice de la escala es un triángulo de CSS en tinta; los marcadores de estado son cuadrados de 0.5rem que se rotan 45° para el estado malo. El medidor de calibración son tres celdas rectangulares de 0.375 × 0.875rem.

## Components

### Buttons
Placas de instrumento: borde en tinta, fondo blanco y la inversión como respuesta.
- **Shape:** esquinas casi rectas (2px), altura mínima de 44px.
- **Primary (por defecto):** fondo `papel-2`, texto y borde `tinta`, 600 a 0.9375rem, padding 8 × 16px. Los enlaces de descarga usan la misma clase.
- **Hover / Focus:** se invierte a fondo `tinta` con texto `papel-2` (0.15s ease-out). Al presionar baja 1px. Foco con el anillo violeta global. Deshabilitado: 50% de opacidad y cursor de progreso.
- **Alarma:** la única variante llena, en `mal` con texto `papel-2`; en hover pasa a `tinta`.

### Chips
- **Grupo segmentado de rango:** borde de 1px en tinta con divisiones internas de 1px; celdas de 40px de alto y 3.25rem mínimo. La opción activa (`aria-pressed="true"`) se invierte a tinta.

### Inputs / Fields
- **Style:** fondo `papel-2`, borde 1px `impresion`, radio 2px, 44px de alto, caret violeta, cifras tabulares.
- **Focus:** contorno violeta de 2px sin separación y borde en el mismo color.
- **Error:** borde `mal` en `:user-invalid`; el estado del formulario se escribe debajo en `ok` o `mal`.
- **Rótulo:** placa en versalitas arriba, ayuda en 0.8125rem debajo; en el formulario de umbrales la grilla alinea los inputs aunque la ayuda ocupe dos líneas.

### Navigation
- **Rótulo superior:** nombre (700, 1.0625rem), subtítulo, lámpara de conexión (gris apagada, `ok` encendida) y medidor de calibración de tres celdas que se llenan en tinta segunda y pasan a `ok` con precisión 3. Cierra con una regla de 1px en tinta plena.

### El Tambor (componente firma)
Gráfico uPlot sobre `papel-2` con borde `regla`. Retícula en dos pesos, zonas de umbral detrás del trazo (relleno al 8% y trama al 35% en claro, 5% y 16% en oscuro), líneas de umbral punteadas en `impresion`, trazo violeta de 2px sin unir huecos. En el tambor en vivo la pluma es un punto de tinta de 4px con un brazo horizontal hasta el borde derecho; si el último dato es un hueco (sin conexión), la pluma se levanta y no se dibuja. Los eventos se marcan con la pluma sepia.

### Escala Impresa
Siete bandas proporcionales dentro de un borde en tinta, divididas por filetes del color del papel, con trama creciente en las bandas 2–5. Un índice triangular se desliza al valor actual (0.4s, `cubic-bezier(.16, 1, .3, 1)`).

### Canal de Magnitud
Nombre, valor con unidad, estado con marcador de forma y mini traza de pluma de 1.5px con punto final. En móvil son filas separadas por reglas; en escritorio, columnas separadas por reglas verticales.

## Do's and Don'ts

### Do:
- **Do** reservar el violeta `pluma` para trazos de dato, el cursor, el caret, la selección y el foco.
- **Do** acompañar cada color de nivel con trama o forma: trama de densidad creciente en bandas y zonas, cuadrado/rayado/rombo en los estados.
- **Do** separar con reglas de 1px (`regla` entre secciones, `tinta` bajo el rótulo) y apoyar gráficos e inputs en `papel-2`.
- **Do** usar placas en versalitas chicas (600, 0.8125rem, +0.04em, tinta segunda) para títulos de sección, rótulos de campos y términos.
- **Do** mantener `tabular-nums` en cifras e inputs y 44px de alto mínimo en controles.
- **Do** definir cada color nuevo en ambos temas bajo el mismo nombre de token y apagar las transiciones con `prefers-reduced-motion`.

### Don't:
- **Don't** usar `box-shadow` ni esquinas mayores a 2px; los círculos quedan para lámparas y LEDs.
- **Don't** armar tarjetas, tiles redondeados ni anillos con degradé para mostrar métricas.
- **Don't** usar degradés continuos como decoración; `repeating-linear-gradient` existe acá solo como trama impresa.
- **Don't** llenar fondos con `mal` salvo en la acción que silencia la alarma.
- **Don't** cargar fuentes web ni recursos externos; la pila del sistema es obligatoria.
