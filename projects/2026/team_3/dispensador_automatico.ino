#include <WiFi.h>
#include <WebServer.h>

// --- Configuración de Pines Físicos ---
const int PIN_TRIG = 19;
const int PIN_ECHO = 18;
const int PIN_RELAY = 14; 
const int PIN_BOTON = 13;

// Pines LED RGB
const int PIN_LED_R = 17;
const int PIN_LED_G = 16; 
const int PIN_LED_B = 4;  

// --- Parámetros de Seguridad ---
const float DISTANCIA_PISO = 17.4; // Medida exacta informada: sensor a la mesa.
// Fondo observado: 16 cm. El umbral queda entre ese fondo y la mesa.
// Es una calibración inicial: confirmar las lecturas reales con la bomba apagada.
const float MARGEN_DETECCION_PISO = 0.4; // Mantiene el límite de presencia en 17 cm.
const float UMBRAL_VACIO = DISTANCIA_PISO - MARGEN_DETECCION_PISO; // 17 cm.
const float MARGEN_SEGURIDAD = 1.5; // Mismo margen de corte que el original.

// Lógica Física: PULLUP forzado. 3.3V en reposo, 0V al presionar.
const int ESTADO_PRESIONADO = LOW; 

// --- Variables de la Máquina de Estados ---
enum EstadoDispensador {
  ESPERANDO_VASO,
  ESPERANDO_RETIRO_TAPA,
  LLENANDO
};

EstadoDispensador estadoActual = ESPERANDO_VASO;
float distanciaBorde = 0.0;
float distanciaFondo = 0.0;
float ultimaDistanciaReportada = 0.0;

// Variables de Filtro de Ruido
int lecturasCorte = 0; 
int lecturasEmergencia = 0;
int lecturasInvalidas = 0;

// ===== CALCULO PORCENTAJE DE LLENADO =====
// Única selección compartida por encoder, web y control de la bomba.
int porcentajeSeleccionado = 100;
float distanciaObjetivo = 0.0;

// ===== ENCODER =====
const int PIN_ENCODER_CLK = 32;
const int PIN_ENCODER_DT = 33;
const int PIN_ENCODER_SW = 25;
const int SENTIDO_ENCODER = 1; // Cambiar a -1 si el módulo gira al revés.
const unsigned long ANTIRREBOTE_MS = 35;

// Las interrupciones conservan los giros incluso durante pulseIn(). La cola
// mantiene su orden, también al alternar sentidos en los límites 10/100%.
portMUX_TYPE muxEncoder = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t estadoEncoderAnterior = 3;
volatile int8_t transicionesEncoder = 0;
volatile int8_t colaEncoder[32];
volatile uint8_t entradaEncoder = 0;
volatile uint8_t salidaEncoder = 0;

struct Pulsador {
  int pin;
  int lecturaAnterior;
  int estadoEstable;
  unsigned long ultimoCambio;
};
Pulsador botonInicio = {PIN_BOTON, HIGH, HIGH, 0};
Pulsador botonEncoder = {PIN_ENCODER_SW, HIGH, HIGH, 0};
bool actualizarPulsador(Pulsador &boton, unsigned long ahora);

// ===== SERVIDOR WEB =====
const char* WIFI_SSID = "Dispensador-ESP32";
const char* WIFI_CLAVE = "AguaESP32";
WebServer servidor(80);
bool servidorWebIniciado = false;
bool consultaEstadoReportada = false;

const char PAGINA_WEB[] PROGMEM = R"HTML(
<!doctype html>
<html lang="es">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Dispensador de Agua</title>
<style>
body{font-family:system-ui,sans-serif;max-width:480px;margin:40px auto;padding:16px;background:#f1f6fa;color:#16324a}
h1{font-size:1.8rem}output{display:block;font-size:3rem;font-weight:bold;margin:20px 0}
#niveles{display:grid;grid-template-columns:repeat(5,1fr);gap:8px}
button{padding:14px 4px;border:1px solid #96abbc;border-radius:8px;background:white;color:#16324a;cursor:pointer}
button[aria-pressed="true"],#confirmar{background:#076ca8;color:white}
#confirmar{width:100%;margin-top:20px}button:disabled{opacity:.6}
#mensaje{min-height:3em}
</style>
<h1>Dispensador de Agua</h1>
<p>Nivel seleccionado</p><output id="actual">--%</output>
<div id="niveles" aria-label="Porcentaje de llenado"></div>
<button id="confirmar" type="button">Confirmar nivel</button>
<p id="mensaje" role="status" aria-live="polite">Conectando...</p>
<p>Para iniciar, colocá el vaso con tapa y usá el pulsador del dispensador.</p>
<script>
const actual=document.querySelector('#actual');
const mensaje=document.querySelector('#mensaje');
const niveles=document.querySelector('#niveles');
// Una sola solicitud a la vez. Las consultas nunca bloquean los botones.
const ESPERA_RESPUESTA_MS=5000;
let peticionActual=null;
let pendiente=null;
let proximoSondeo=null;
let ultimoPorcentaje=null;
let conectado=false;
function bloquearBotones(bloquear){
  document.querySelectorAll('button').forEach(b=>b.disabled=bloquear);
}
function mostrar(datos){
  actual.textContent=datos.porcentaje+'%';
  niveles.querySelectorAll('button').forEach(b=>b.setAttribute('aria-pressed',Number(b.value)===datos.porcentaje));
}
function solicitar(ruta,porcentaje){
  if(peticionActual){
    if(ruta!=='/estado'){
      pendiente={ruta,porcentaje};
      // Dar prioridad a un toque del usuario frente al sondeo pendiente.
      if(peticionActual.ruta==='/estado'){
        peticionActual.cancelada=true;
        peticionActual.xhr.abort();
      }
    }
    return;
  }
  clearTimeout(proximoSondeo);
  const xhr=new XMLHttpRequest();
  const solicitud={xhr,ruta,cancelada:false};
  peticionActual=solicitud;
  const esConsulta=ruta==='/estado';
  bloquearBotones(!esConsulta);
  if(!esConsulta)mensaje.textContent='Enviando al dispensador...';

  function terminar(){
    if(peticionActual!==solicitud)return;
    peticionActual=null;
    bloquearBotones(false);
    if(pendiente){
      const siguiente=pendiente;
      pendiente=null;
      solicitar(siguiente.ruta,siguiente.porcentaje);
    }else{
      // Programar el próximo sondeo después de terminar, sin acumular peticiones.
      proximoSondeo=setTimeout(()=>solicitar('/estado'),1000);
    }
  }
  function fallo(texto){
    conectado=false;
    mensaje.textContent=texto;
  }
  xhr.onload=()=>{
    if(solicitud.cancelada)return;
    try{
      if(xhr.status!==200)throw new Error('Respuesta rechazada');
      const datos=JSON.parse(xhr.responseText);
      if(typeof datos.porcentaje!=='number'||datos.porcentaje<10||datos.porcentaje>100||datos.porcentaje%10!==0){
        throw new Error('Respuesta inválida');
      }
      const cambio=ultimoPorcentaje!==datos.porcentaje;
      mostrar(datos);
      if(ruta==='/confirmar')mensaje.textContent='Nivel confirmado: '+datos.porcentaje+'%';
      else if(ruta==='/nivel')mensaje.textContent='Nivel seleccionado: '+datos.porcentaje+'%';
      else if(cambio||!conectado)mensaje.textContent='Sincronizado con el dispensador';
      ultimoPorcentaje=datos.porcentaje;
      conectado=true;
    }catch(error){fallo('Respuesta inválida o rechazada. Reintentando conexión...');}
  };
  xhr.onerror=()=>fallo('Sin conexión con el ESP32. Reintentando...');
  xhr.ontimeout=()=>fallo('El ESP32 no respondió en 5 segundos. Reintentando...');
  xhr.onloadend=terminar;
  try{
    xhr.open(esConsulta?'GET':'POST',ruta,true);
    xhr.timeout=ESPERA_RESPUESTA_MS; // Incluye la recepción del cuerpo de respuesta.
    xhr.setRequestHeader('Cache-Control','no-cache');
    if(!esConsulta)xhr.setRequestHeader('Content-Type','application/x-www-form-urlencoded');
    xhr.send(esConsulta?null:(porcentaje===undefined?'':'porcentaje='+encodeURIComponent(porcentaje)));
  }catch(error){
    fallo('No se pudo enviar la solicitud. Reintentando...');
    terminar();
  }
}
for(let p=10;p<=100;p+=10){
  const b=document.createElement('button');
  b.type='button';b.value=p;b.textContent=p+'%';b.setAttribute('aria-pressed','false');
  b.onclick=()=>solicitar('/nivel',p);niveles.appendChild(b);
}
document.querySelector('#confirmar').onclick=()=>solicitar('/confirmar');
solicitar('/estado');
</script>
</html>
)HTML";

// ===== ESPERAS SIN BLOQUEO =====
// Se mantienen los tres estados principales y los tiempos originales.
enum FaseEspera {
  SIN_ESPERA, SOLTAR_BOTON, MEDIR_BORDE, HABILITAR_RETIRO,
  MEDIR_FONDO, PARPADEO_ERROR, FIN_LLENADO, VERIFICAR_NIVEL
};
FaseEspera faseEspera = SIN_ESPERA;
unsigned long inicioEspera = 0;
unsigned long ultimoMuestreo = 0;
int cambiosParpadeo = 0;

// ===== MEDICIONES ESTABLES Y VERIFICACION DE LLENADO =====
// Se reutilizan las referencias originales; la mesa solo detecta ausencia.
const int MUESTRAS_REFERENCIA = 5;
const float MAX_VARIACION_REFERENCIA = 0.5; // cm entre mínimo y máximo.
const float TOLERANCIA_VERIFICACION = 0.3;  // Banda de comprobación, no cambia el objetivo.
const unsigned long ASENTAMIENTO_NIVEL_MS = 500;
const int MAX_REANUDACIONES_NIVEL = 3;
float muestrasReferencia[MUESTRAS_REFERENCIA];
int cantidadMuestrasReferencia = 0;
float medicionFiltrada = 0.0;
unsigned long ultimoMuestreoReferencia = 0;
int reanudacionesNivel = 0;
bool diagnosticoSensorActivo = false;
bool esperarLiberacionDiagnostico = false;
unsigned long ultimoDiagnosticoSensor = 0;

void IRAM_ATTR capturarEncoder() {
  portENTER_CRITICAL_ISR(&muxEncoder);
  uint8_t actual = (digitalRead(PIN_ENCODER_CLK) << 1) | digitalRead(PIN_ENCODER_DT);
  uint8_t anterior = estadoEncoderAnterior;
  if ((actual ^ anterior) == 3) {
    transicionesEncoder = 0; // Salto imposible: descartar la secuencia incompleta.
  } else if (actual != anterior) {
    uint8_t transicion = (anterior << 2) | actual;
    bool horario = (transicion == 13 || transicion == 4 ||
                    transicion == 2 || transicion == 11);
    transicionesEncoder += horario ? 1 : -1;
    // Un paso requiere un ciclo completo de cuadratura; los rebotes de ida
    // y vuelta se cancelan antes de volver al reposo CLK=DT=HIGH.
    if (actual == 3) {
      int8_t paso = transicionesEncoder >= 4 ? 1 : (transicionesEncoder <= -4 ? -1 : 0);
      uint8_t siguiente = (entradaEncoder + 1) % 32;
      if (paso != 0 && siguiente != salidaEncoder) {
        colaEncoder[entradaEncoder] = paso;
        entradaEncoder = siguiente;
      }
      transicionesEncoder = 0;
    }
  }
  estadoEncoderAnterior = actual;
  portEXIT_CRITICAL_ISR(&muxEncoder);
}

bool actualizarPulsador(Pulsador &boton, unsigned long ahora) {
  int lectura = digitalRead(boton.pin);
  if (lectura != boton.lecturaAnterior) {
    boton.lecturaAnterior = lectura;
    boton.ultimoCambio = ahora;
  }
  if (lectura != boton.estadoEstable && ahora - boton.ultimoCambio >= ANTIRREBOTE_MS) {
    boton.estadoEstable = lectura;
    return true;
  }
  return false;
}

float calcularDistanciaObjetivo() {
  // 100% coincide exactamente con el corte original: borde + 1,5 cm.
  // El porcentaje representa la altura entre el fondo y ese máximo seguro.
  float distanciaMaximaSegura = distanciaBorde + MARGEN_SEGURIDAD;
  float alturaUtil = distanciaFondo - distanciaMaximaSegura;
  return distanciaFondo - alturaUtil * (porcentajeSeleccionado / 100.0f);
}

void seleccionarPorcentaje(int valor, bool desdeWeb) {
  if (valor < 10 || valor > 100 || valor % 10 != 0) return;
  bool cambio = valor != porcentajeSeleccionado;
  if (!cambio && !desdeWeb) return;
  porcentajeSeleccionado = valor;
  if (cambio && estadoActual == LLENANDO &&
      (faseEspera == SIN_ESPERA || faseEspera == VERIFICAR_NIVEL)) {
    distanciaObjetivo = calcularDistanciaObjetivo();
    lecturasCorte = 0; // No reutilizar confirmaciones de un objetivo anterior.
    if (faseEspera == VERIFICAR_NIVEL) cantidadMuestrasReferencia = 0;
  }
  Serial.print(desdeWeb ? "Nivel seleccionado desde web: " : "Nivel seleccionado: ");
  Serial.print(porcentajeSeleccionado);
  Serial.println("%");
}

void confirmarPorcentaje() {
  // Confirmar informa la selección; no activa la bomba ni cambia el procedimiento.
  Serial.print("Nivel confirmado: ");
  Serial.print(porcentajeSeleccionado);
  Serial.println("%");
}

void atenderEncoder() {
  while (true) {
    portENTER_CRITICAL(&muxEncoder);
    bool hayPaso = salidaEncoder != entradaEncoder;
    int paso = 0;
    if (hayPaso) {
      paso = colaEncoder[salidaEncoder];
      salidaEncoder = (salidaEncoder + 1) % 32;
    }
    portEXIT_CRITICAL(&muxEncoder);
    if (!hayPaso) break;
    int valor = constrain(porcentajeSeleccionado + paso * SENTIDO_ENCODER * 10, 10, 100);
    seleccionarPorcentaje(valor, false);
  }
}

void enviarEstadoWeb() {
  if (servidor.uri() == "/estado" && !consultaEstadoReportada) {
    Serial.println("[WEB] GET /estado recibido: enviando porcentaje.");
    consultaEstadoReportada = true;
  }
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json", String("{\"porcentaje\":") + porcentajeSeleccionado + "}");
}

void iniciarServidorWeb() {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(WIFI_SSID, WIFI_CLAVE)) {
    Serial.println("ERROR: No se pudo crear la red WiFi. Encoder disponible.");
    return;
  }
  servidor.on("/", HTTP_GET, []() {
    Serial.println("[WEB] GET / recibido: enviando página.");
    servidor.sendHeader("Cache-Control", "no-store");
    servidor.send_P(200, "text/html; charset=utf-8", PAGINA_WEB);
  });
  servidor.on("/estado", HTTP_GET, enviarEstadoWeb);
  servidor.on("/nivel", HTTP_POST, []() {
    // Comparación exacta: rechazar texto, decimales y valores fuera de rango.
    if (servidor.hasArg("porcentaje")) {
      String valor = servidor.arg("porcentaje");
      for (int p = 10; p <= 100; p += 10) {
        if (valor == String(p)) {
          seleccionarPorcentaje(p, true);
          enviarEstadoWeb();
          return;
        }
      }
    }
    servidor.send(400, "text/plain; charset=utf-8", "Usar un múltiplo de 10 entre 10 y 100.");
  });
  servidor.on("/confirmar", HTTP_POST, []() {
    confirmarPorcentaje();
    enviarEstadoWeb();
  });
  servidor.onNotFound([]() { servidor.send(404, "text/plain", "Ruta no encontrada"); });
  servidor.begin();
  servidorWebIniciado = true;
  Serial.print("Red WiFi: "); Serial.println(WIFI_SSID);
  Serial.print("Clave WiFi: "); Serial.println(WIFI_CLAVE);
  Serial.print("Abrir http://"); Serial.println(WiFi.softAPIP());
  Serial.println("[WEB] Interfaz con espera máxima de 5 s. Prueba de conexión: /estado");
}

void atenderServidorWeb() {
  if (!servidorWebIniciado) return;
  servidor.handleClient();
  // Comprobar conexiones una vez por segundo, informar solo los cambios.
  static unsigned long ultimaComprobacion = 0;
  static int clientesAnteriores = -1;
  unsigned long ahora = millis();
  if (ahora - ultimaComprobacion < 1000) return;
  ultimaComprobacion = ahora;
  int clientes = WiFi.softAPgetStationNum();
  if (clientes != clientesAnteriores) {
    Serial.print("[WEB] Dispositivos conectados al WiFi: "); Serial.println(clientes);
    clientesAnteriores = clientes;
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_RELAY, OUTPUT);
  
  // Forzamos PULLUP para eliminar la estática y el bloqueo del botón
  pinMode(PIN_BOTON, INPUT_PULLUP); 
  pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
  pinMode(PIN_ENCODER_DT, INPUT_PULLUP);
  pinMode(PIN_ENCODER_SW, INPUT_PULLUP);
  
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  
  digitalWrite(PIN_RELAY, LOW);
  colorLED(HIGH, LOW, LOW);
  estadoEncoderAnterior = (digitalRead(PIN_ENCODER_CLK) << 1) | digitalRead(PIN_ENCODER_DT);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_CLK), capturarEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_DT), capturarEncoder, CHANGE);
  Serial.println("--- Dispensador Físico: Filtros Anti-Ruido Iniciados ---");
  Serial.print("Base del montaje: "); Serial.print(DISTANCIA_PISO); Serial.println(" cm.");
  Serial.print("Límite de presencia del vaso: menos de ");
  Serial.print(UMBRAL_VACIO); Serial.println(" cm.");
  Serial.println("Nivel seleccionado: 100%");
  Serial.println("Diagnóstico seguro del sensor: enviar D por Serial; enviar X para salir.");
  iniciarServidorWeb();
}

void colorLED(int r, int g, int b) {
  digitalWrite(PIN_LED_R, r);
  digitalWrite(PIN_LED_G, g);
  digitalWrite(PIN_LED_B, b);
}

float medirDistancia() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  
  long duracion = pulseIn(PIN_ECHO, HIGH, 30000); 
  if (duracion == 0) return 999.0; 
  return (duracion * 0.0343) / 2.0;
}

bool distanciaValida(float distancia) {
  // Conserva UMBRAL_VACIO y el rechazo de 999 (sin eco). También evita
  // iniciar con mediciones en la zona ciega o geometría incoherente.
  return distancia >= 2.0f && distancia < UMBRAL_VACIO;
}

void reiniciarMuestrasReferencia(unsigned long ahora) {
  cantidadMuestrasReferencia = 0;
  ultimoMuestreoReferencia = ahora;
}

// ===== DIAGNOSTICO SIN BOMBA =====
// Solo se activa al enviar D por Serial. No modifica referencias ni porcentajes.
bool atenderDiagnosticoSensor(unsigned long ahora) {
  while (Serial.available() > 0) {
    char comando = Serial.read();
    if (comando == 'D' || comando == 'd' ||
        ((comando == 'X' || comando == 'x') && diagnosticoSensorActivo)) {
      digitalWrite(PIN_RELAY, LOW);
      estadoActual = ESPERANDO_VASO;
      faseEspera = SIN_ESPERA;
      lecturasCorte = 0;
      lecturasEmergencia = 0;
      lecturasInvalidas = 0;
      reiniciarMuestrasReferencia(ahora);
      colorLED(HIGH, LOW, LOW);
      diagnosticoSensorActivo = (comando == 'D' || comando == 'd');
      esperarLiberacionDiagnostico = true;
      ultimoDiagnosticoSensor = ahora;
      if (diagnosticoSensorActivo) {
        Serial.println("[DIAGNOSTICO] Bomba deshabilitada. Medir sin vaso, con vaso vacío y con tapa. X para salir.");
      } else {
        Serial.println("[DIAGNOSTICO] Finalizado. Bomba apagada; volver a iniciar con vaso vacío, tapa y pulsador.");
      }
    }
  }
  if (!diagnosticoSensorActivo) {
    if (esperarLiberacionDiagnostico) {
      if (botonInicio.estadoEstable != ESTADO_PRESIONADO) esperarLiberacionDiagnostico = false;
      return true; // Exigir soltar el pulsador antes de iniciar otro ciclo.
    }
    return false;
  }
  if (ahora - ultimoDiagnosticoSensor >= 300) {
    ultimoDiagnosticoSensor = ahora;
    float lectura = medirDistancia();
    Serial.print("[DIAGNOSTICO] Sensor: ");
    if (lectura == 999.0f) Serial.println("SIN ECO");
    else { Serial.print(lectura); Serial.println(" cm"); }
  }
  return true; // El pulsador original no puede iniciar el llenado en este modo.
}

// Resultado: 0 esperando; 1 mediana estable; -1 dato inválido; -2 inestable.
// Solo se usa con la bomba apagada, cada 100 ms y sin delay().
int tomarReferenciaEstable(unsigned long ahora) {
  if (ahora - ultimoMuestreoReferencia < 100) return 0;
  ultimoMuestreoReferencia = ahora;
  float lectura = medirDistancia();
  Serial.print("[MUESTRA EN REPOSO] ");
  if (lectura == 999.0f) Serial.println("SIN ECO");
  else { Serial.print(lectura); Serial.println(" cm"); }
  if (!distanciaValida(lectura)) {
    cantidadMuestrasReferencia = 0;
    return -1;
  }
  muestrasReferencia[cantidadMuestrasReferencia++] = lectura;
  if (cantidadMuestrasReferencia < MUESTRAS_REFERENCIA) return 0;

  // Ordenar cinco datos y tomar el central evita basarse en una lectura aislada.
  for (int i = 1; i < MUESTRAS_REFERENCIA; i++) {
    float valor = muestrasReferencia[i];
    int j = i - 1;
    while (j >= 0 && muestrasReferencia[j] > valor) {
      muestrasReferencia[j + 1] = muestrasReferencia[j];
      j--;
    }
    muestrasReferencia[j + 1] = valor;
  }
  cantidadMuestrasReferencia = 0;
  if (muestrasReferencia[MUESTRAS_REFERENCIA - 1] - muestrasReferencia[0] > MAX_VARIACION_REFERENCIA) return -2;
  medicionFiltrada = muestrasReferencia[MUESTRAS_REFERENCIA / 2];
  return 1;
}

void iniciarError(const char* mensaje) {
  digitalWrite(PIN_RELAY, LOW);
  Serial.println(mensaje);
  estadoActual = ESPERANDO_VASO;
  faseEspera = PARPADEO_ERROR;
  inicioEspera = millis();
  cambiosParpadeo = 0;
  colorLED(HIGH, LOW, LOW);
}

void terminarLlenado(const char* mensaje) {
  digitalWrite(PIN_RELAY, LOW);
  Serial.println(mensaje);
  faseEspera = FIN_LLENADO;
  inicioEspera = millis(); // Mantener verde 2 s, con la bomba ya apagada.
}

void iniciarVerificacionNivel() {
  digitalWrite(PIN_RELAY, LOW); // Cortar al cumplir las 3 lecturas originales.
  Serial.println("Corte por nivel detectado. Verificando con la bomba apagada...");
  Serial.print("Borde: "); Serial.print(distanciaBorde);
  Serial.print(" cm; fondo registrado: "); Serial.print(distanciaFondo);
  Serial.print(" cm; selección: "); Serial.print(porcentajeSeleccionado); Serial.print("%");
  Serial.print("; objetivo: "); Serial.print(distanciaObjetivo); Serial.println(" cm.");
  faseEspera = VERIFICAR_NIVEL;
  inicioEspera = millis();
  reiniciarMuestrasReferencia(inicioEspera);
}

void atenderEspera(unsigned long ahora) {
  switch (faseEspera) {
    case SOLTAR_BOTON:
      if (botonInicio.estadoEstable != ESTADO_PRESIONADO) {
        Serial.println("Escaneando nivel de referencia...");
        faseEspera = MEDIR_BORDE;
        inicioEspera = ahora;
        reiniciarMuestrasReferencia(ahora);
      }
      break;

    case MEDIR_BORDE: {
      if (ahora - inicioEspera < 2000) break;
      int resultado = tomarReferenciaEstable(ahora);
      if (resultado == 0) break;
      if (resultado < 0) {
        iniciarError(resultado == -1 ? "ERROR: No hay vaso o medición de tapa inválida."
                                    : "ERROR: Tapa inestable. Mantener vaso y tapa quietos.");
        break;
      }
      distanciaBorde = medicionFiltrada;
      Serial.print("Borde del vaso a: ");
      Serial.print(distanciaBorde);
      Serial.println(" cm.");
      colorLED(LOW, LOW, HIGH); // Azul
      estadoActual = ESPERANDO_RETIRO_TAPA;
      faseEspera = HABILITAR_RETIRO;
      inicioEspera = millis();
      break;
    }

    case HABILITAR_RETIRO:
      if (ahora - inicioEspera >= 1000) faseEspera = SIN_ESPERA;
      break;

    case MEDIR_FONDO: {
      if (ahora - inicioEspera < 3000) break;
      int resultado = tomarReferenciaEstable(ahora);
      if (resultado == 0) break;
      if (resultado < 0) {
        Serial.print("Rango válido del fondo: desde 2 cm hasta menos de ");
        Serial.print(UMBRAL_VACIO);
        Serial.println(" cm.");
        iniciarError(resultado == -1 ? "ERROR CRÍTICO: Vaso retirado o fondo inválido."
                                    : "ERROR: Fondo inestable. Revisar centrado y eco del sensor.");
        break;
      }
      distanciaFondo = medicionFiltrada;
      // Verificar nuevamente el destape tras la estabilización: nunca
      // encender la bomba si la tapa volvió a colocarse o no hay altura útil.
      if (distanciaFondo <= distanciaBorde + 3.0f ||
          distanciaFondo <= distanciaBorde + MARGEN_SEGURIDAD) {
        iniciarError("ERROR: Fondo incoherente con el borde. Revisar vaso/tapa.");
        break;
      }
      ultimaDistanciaReportada = distanciaFondo;
      distanciaObjetivo = calcularDistanciaObjetivo();
      Serial.print("Fondo del vaso a: "); Serial.print(distanciaFondo); Serial.println(" cm.");
      Serial.print("Profundidad interior medida: "); Serial.print(distanciaFondo - distanciaBorde);
      Serial.println(" cm. Debe coincidir con el vaso vacío.");
      Serial.print("Objetivo para "); Serial.print(porcentajeSeleccionado);
      Serial.print("%: "); Serial.print(distanciaObjetivo); Serial.println(" cm.");
      lecturasCorte = 0;
      lecturasEmergencia = 0;
      lecturasInvalidas = 0;
      reanudacionesNivel = 0;
      Serial.println("INICIANDO BOMBEO...");
      colorLED(LOW, HIGH, LOW); // Verde
      digitalWrite(PIN_RELAY, HIGH);
      estadoActual = LLENANDO;
      faseEspera = SIN_ESPERA;
      ultimoMuestreo = millis();
      break;
    }

    case VERIFICAR_NIVEL: {
      // Solo se llega desde el corte normal. Los errores nunca reanudan la bomba.
      if (ahora - inicioEspera < ASENTAMIENTO_NIVEL_MS) break;
      int resultado = tomarReferenciaEstable(ahora);
      if (resultado == 0) break;
      if (resultado < 0) {
        terminarLlenado("ERROR: No se pudo verificar un nivel estable. Bomba apagada.");
        break;
      }
      Serial.print("[VERIFICACION] Agua en reposo: "); Serial.print(medicionFiltrada);
      Serial.print(" cm; distancia al borde: "); Serial.print(medicionFiltrada - distanciaBorde);
      Serial.println(" cm.");
      if (medicionFiltrada < distanciaBorde) {
        terminarLlenado("ERROR: Límite sobrepasado con agua en reposo. Bomba apagada.");
      } else if (medicionFiltrada > distanciaFondo + TOLERANCIA_VERIFICACION) {
        terminarLlenado("ERROR: Lectura en reposo detrás del fondo registrado. Bomba apagada.");
      } else if (medicionFiltrada <= distanciaObjetivo + TOLERANCIA_VERIFICACION) {
        terminarLlenado("Nivel seleccionado alcanzado y verificado. Bombeo finalizado.");
      } else if (reanudacionesNivel >= MAX_REANUDACIONES_NIVEL) {
        terminarLlenado("ERROR: Cortes prematuros repetidos. Revisar vibraciones/eco. Bomba apagada.");
      } else {
        reanudacionesNivel++;
        Serial.println("La lectura en reposo confirma que falta llenar. Continuando el mismo ciclo...");
        lecturasCorte = 0;
        lecturasEmergencia = 0;
        lecturasInvalidas = 0;
        faseEspera = SIN_ESPERA;
        ultimoMuestreo = millis();
        digitalWrite(PIN_RELAY, HIGH);
      }
      break;
    }

    case PARPADEO_ERROR:
      if (ahora - inicioEspera < 150) break;
      inicioEspera = ahora;
      cambiosParpadeo++;
      if (cambiosParpadeo >= 8) {
        faseEspera = SIN_ESPERA;
        colorLED(HIGH, LOW, LOW);
      } else {
        colorLED(cambiosParpadeo % 2 == 0 ? HIGH : LOW, LOW, LOW);
      }
      break;

    case FIN_LLENADO:
      if (ahora - inicioEspera >= 2000) {
        estadoActual = ESPERANDO_VASO;
        faseEspera = SIN_ESPERA;
        colorLED(HIGH, LOW, LOW);
      }
      break;

    case SIN_ESPERA:
      break;
  }
}

void loop() {
  atenderEncoder();
  unsigned long ahora = millis();
  actualizarPulsador(botonInicio, ahora);
  if (actualizarPulsador(botonEncoder, ahora) &&
      botonEncoder.estadoEstable == ESTADO_PRESIONADO) {
    confirmarPorcentaje();
  }
  atenderServidorWeb();
  ahora = millis();

  if (atenderDiagnosticoSensor(ahora)) return;

  if (faseEspera != SIN_ESPERA) {
    atenderEspera(ahora);
    return;
  }
  if (estadoActual == ESPERANDO_VASO) {
    colorLED(HIGH, LOW, LOW); // Rojo
    if (botonInicio.estadoEstable == ESTADO_PRESIONADO) faseEspera = SOLTAR_BOTON;
    return;
  }

  // Sustituye delay(100); conserva el muestreo y deja atender encoder/web.
  if (ahora - ultimoMuestreo < 100) return;
  ultimoMuestreo = ahora;
  float distanciaActual = medirDistancia();

  switch (estadoActual) {
    case ESPERANDO_RETIRO_TAPA:
      if (distanciaActual > distanciaBorde + 3.0f) {
        Serial.println("Movimiento detectado. Esperando estabilización...");
        faseEspera = MEDIR_FONDO;
        inicioEspera = millis();
        reiniciarMuestrasReferencia(inicioEspera);
      }
      break;

    case LLENANDO:
      Serial.print("[EN VIVO] Distancia al sensor: ");
      Serial.print(distanciaActual);
      Serial.println(" cm");

      // 3 lecturas inválidas consecutivas apagan la bomba. El límite de
      // presencia sigue activo aunque haya eco; no aceptar la mesa como agua.
      if (!distanciaValida(distanciaActual)) {
        lecturasCorte = 0;
        lecturasEmergencia = 0;
        if (++lecturasInvalidas >= 3) {
          if (distanciaActual == 999.0f) {
            terminarLlenado("ERROR: Tres lecturas inválidas; última lectura SIN ECO. Bomba apagada.");
          } else {
            Serial.print("Última lectura rechazada: "); Serial.print(distanciaActual);
            Serial.print(" cm; límite del vaso: menos de ");
            Serial.print(UMBRAL_VACIO); Serial.println(" cm.");
            terminarLlenado("ERROR: Tres lecturas inválidas; última fuera del rango del vaso. Revisar calibración/eco. Bomba apagada.");
          }
        }
        break;
      }
      lecturasInvalidas = 0;

      // Filtro 1: se conserva la exigencia de 3 lecturas consecutivas.
      if (distanciaActual <= distanciaObjetivo) lecturasCorte++;
      else lecturasCorte = 0;

      // Filtro 2: mismo límite físico original, independiente del porcentaje.
      if (distanciaActual < distanciaBorde) lecturasEmergencia++;
      else lecturasEmergencia = 0;

      // Priorizar emergencia evita ejecutar dos cortes en la misma vuelta.
      if (lecturasEmergencia >= 3) {
        terminarLlenado("ERROR: Límite sobrepasado. Corte de emergencia.");
      } else if (lecturasCorte >= 3) {
        iniciarVerificacionNivel();
      }
      break;

    case ESPERANDO_VASO:
      break;
  }
}
