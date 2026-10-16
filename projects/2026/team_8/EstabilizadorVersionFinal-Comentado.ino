/*
 * ============================================================================
 *  ESTABILIZADOR DE CELULAR (GIMBAL DE 2 EJES) - ESP32 + MPU6050 + 2 SERVOS
 * ============================================================================
 *
 *  ¿QUÉ HACE ESTE PROGRAMA?
 *  Mantiene nivelado un soporte de celular aunque la base se incline.
 *  Un sensor MPU6050 (acelerómetro + giroscopio) mide la inclinación y dos
 *  servomotores se mueven en sentido contrario para compensarla.
 *
 *  EJES:
 *    - Eje X  -> "roll"  (inclinación lateral, como un balanceo de lado a lado)
 *    - Eje Y  -> "pitch" (inclinación de adelante hacia atrás)
 *
 *  ¿CÓMO FUNCIONA, EN RESUMEN?
 *  1) Al arrancar, se calibra el giroscopio (se mide su error en reposo).
 *  2) En cada vuelta del loop():
 *       a) Se leen el acelerómetro y el giroscopio.
 *       b) Se estima el ángulo actual combinando ambos sensores
 *          (un "filtro complementario" simplificado, ver sección 5).
 *       c) Con ese ángulo, un control integral mueve los servos hasta que
 *          el ángulo estimado vuelve a ser 0 (es decir, la posición está nivelada).
 *       d) Cada 1 segundo se imprimen los datos por el monitor serie.
 *
 *  CUESTIONES A TENER EN CUENTA
 *    - El GIROSCOPIO mide velocidad de giro (°/s). Es muy preciso para
 *      movimientos rápidos, pero al integrarlo (sumar en el tiempo) se va
 *      acumulando un error llamado "deriva" (el ángulo se va corriendo solo).
 *    - El ACELERÓMETRO mide la gravedad, así que da un ángulo absoluto que
 *      no deriva (es decir no tiene error), pero es muy ruidoso cuando el aparato se mueve o vibra.
 *    - Se usa el giroscopio mientras hay movimiento, y el acelerómetro
 *      para corregir la deriva cuando el aparato está quieto.
 *
 *  CONEXIONES:
 *    - MPU6050: SDA -> pin 18, SCL -> pin 19 (bus I2C)
 *    - Servo X (roll)  -> pin 16
 *    - Servo Y (pitch) -> pin 17
 */

// ============================================================================
//  LIBRERÍAS
// ============================================================================
#include <Adafruit_MPU6050.h>  // Manejo del sensor MPU6050
#include <Adafruit_Sensor.h>   // Define el tipo "sensors_event_t" usado para leer datos
#include <Wire.h>              // Comunicación I2C (la usa el MPU6050)
#include <ESP32Servo.h>        // Control de servos en el ESP32

// ============================================================================
//  OBJETOS GLOBALES
// ============================================================================
Adafruit_MPU6050 mpu;  // Representa al sensor
Servo servoX;          // Servo que compensa el roll  (eje X)
Servo servoY;          // Servo que compensa el pitch (eje Y)

// ============================================================================
//  ÁNGULO ESTIMADO (resultado del filtro)
//  Es el "mejor cálculo" de cuánto está inclinado el aparato, en grados.
//  0° significa nivelado.
// ============================================================================
float anguloEstimadoX = 0;
float anguloEstimadoY = 0;

// ============================================================================
//  POSICIÓN CENTRAL DE LOS SERVOS
//  Valores iniciales que usamos para centrar el mpu y que quede nivelado.
//  Ángulo (0-180) al que debe estar cada servo para que el soporte quede
//  nivelado. Son valores propios de CÓMO QUEDÓ ARMADO este montaje físico;
//  si se rearma la estructura habrá que reajustarlos.
// ============================================================================
const float CENTRO_SERVO_X = 104.0;
const float CENTRO_SERVO_Y = 134.0;

// ============================================================================
//  TEMPORIZADORES
//  millis() devuelve los milisegundos transcurridos desde que se encendió
//  la placa. Se guardan marcas de tiempo para medir intervalos.
// ============================================================================
unsigned long tiempoAnterior = 0;  // Instante de la vuelta anterior del loop (para calcular dt)
unsigned long tiempoSerial = 0;    // Última vez que se imprimió por el monitor serie

unsigned long tiempoQuietoX = 0;   // Último momento en que se detectó movimiento en X
unsigned long tiempoQuietoY = 0;   // Último momento en que se detectó movimiento en Y

// ============================================================================
//  PARÁMETROS DEL FILTRO
// ============================================================================
const float UMBRAL_GYRO = 1.0;              // Constante que se usa básicamente para que el dispositivo no se reacomode ante una pequeña turbulencia. Por debajo de esto se considera que NO hay movimiento
const unsigned long TIEMPO_ESPERA = 500;    // milisegundos que debe estar quieto antes de corregir la deriva
const float CORRECCION_DERIVA = 0.02;       // Cuánto "pesa" el acelerómetro en cada corrección (2%).
                                            // Es chico a propósito: la corrección es lenta y suave.

// ============================================================================
//  OFFSETS DEL GIROSCOPIO
//  Un giroscopio nunca marca exactamente 0 en reposo; tiene un pequeño error
//  constante. Se mide al inicio (en función: "calibrarGyro" que está un poco más abajo) y se resta en cada lectura.
// ============================================================================
float offsetGyroX = 0;
float offsetGyroY = 0;
float offsetGyroZ = 0;

// ============================================================================
//  CONTROL DE SERVOS
// ============================================================================
float servoDeltaX = 0;   // Compensación acumulada del servo X (grados respecto al centro)
float servoDeltaY = 0;   // Compensación acumulada del servo Y (grados respecto al centro)

const float KI = 0.15;          // Ganancia integral: qué tan rápido se corrige en cada ciclo.
                                // Más alto = reacciona más rápido, pero puede oscilar.
const float ZONA_MUERTA = 0.5;  // Errores menores a este valor (en grados) se ignoran,
                                // así los servos no tiemblan por ruido mínimo.
const float LIMITE_DELTA = 85;  // Máxima compensación permitida desde el centro (protege el mecanismo).

// Cuando el pitch (eje Y) es muy grande, el cálculo del roll (eje x) con acelerómetro
// se vuelve poco confiable, así que en ese caso no se usa para corregir el roll.
// Por esto definimos la constante LIMITE_PUTCH_CORR como un límite a considerar.
const float LIMITE_PITCH_CORR = 50.0;


// ============================================================================
//  FUNCIÓN: calibrarGyro()
//  Se ejecuta una sola vez al inicio. Con el sensor COMPLETAMENTE QUIETO toma
//  500 lecturas del giroscopio y calcula el promedio de cada eje. Ese promedio
//  es el error de fábrica/temperatura ("offset") que luego se resta.
// ============================================================================
void calibrarGyro() {

  const int muestras = 500;  // Cantidad de lecturas a promediar

  float sumaX = 0;
  float sumaY = 0;
  float sumaZ = 0;

  Serial.println("================================");
  Serial.println("CALIBRANDO GIROSCOPIO");
  Serial.println("NO MUEVAS EL MPU6050");
  Serial.println("================================");

  delay(2000);  // Margen para soltar el aparato y que quede quieto

  // Se acumulan las lecturas
  for (int i = 0; i < muestras; i++) {

    sensors_event_t gyro;
    mpu.getGyroSensor()->getEvent(&gyro);

    // El sensor entrega radianes/segundo; se convierte a grados/segundo
    float gyroX = gyro.gyro.x * 180.0 / PI;
    float gyroY = gyro.gyro.y * 180.0 / PI;
    float gyroZ = gyro.gyro.z * 180.0 / PI;

    sumaX += gyroX;
    sumaY += gyroY;
    sumaZ += gyroZ;

    delay(5);  // Pausa corta entre lecturas (500 muestras x 5 ms = ~2.5 s)
  }

  // Promedio = error constante del giroscopio en reposo
  offsetGyroX = sumaX / muestras;
  offsetGyroY = sumaY / muestras;
  offsetGyroZ = sumaZ / muestras;

  Serial.println("Calibracion terminada.");

  Serial.print("Offset X: ");
  Serial.println(offsetGyroX);

  Serial.print("Offset Y: ");
  Serial.println(offsetGyroY);

  Serial.print("Offset Z: ");
  Serial.println(offsetGyroZ);

  Serial.println("================================");
}

// ============================================================================
//  SETUP: se ejecuta una vez al encender o reiniciar la placa
// ============================================================================
void setup() {
  Serial.begin(115200);  // Inicia el monitor serie (configurarlo a 115200 baudios)

  // Inicia el bus I2C con SDA en el pin 18 y SCL en el pin 19
  Wire.begin(18, 19);

  // Se asigna cada servo a su pin
  servoX.attach(16);
  servoY.attach(17);

  // Los servos arrancan en su posición central (soporte nivelado)
  servoX.write(CENTRO_SERVO_X);
  servoY.write(CENTRO_SERVO_Y);
  delay(2000);  // Tiempo para que dejen el dispositivo recto y quieto para que tome correctamente las lecturas del mpu

  // Se intenta conectar con el MPU6050; si no responde, reintenta cada 1 s
  // (el programa queda "esperando" acá hasta que el sensor esté conectado)
  while (!mpu.begin()) {
    Serial.println("MPU6050 not connected!");
    delay(1000);
  }
  delay(2000);

  Serial.println("MPU6050 ready!, calibrando gyro...");
  delay(1000);

  calibrarGyro();  // Mide el error del giroscopio (el aparato debe estar quieto)

  delay(2000);
  Serial.println("Supuestamente calibrado");

  // Pausa para que el sensor se estabilice antes de empezar
  delay(1000);

  // Se hace una lectura inicial del acelerómetro.
  // (El dato no se guarda ni se usa más adelante; el ángulo estimado
  //  arranca en 0, es decir, se asume que el aparato inicia nivelado.)
  sensors_event_t event;
  mpu.getAccelerometerSensor()->getEvent(&event);

  Serial.println("Posicion inicial guardada.");

  // Se inicializan los temporizadores justo antes de entrar al loop
  tiempoAnterior = millis();
  tiempoQuietoX = millis();
  tiempoQuietoY = millis();
}

// ============================================================================
//  LOOP: se repite constantemente mientras la placa esté encendida
// ============================================================================
void loop() {

  // ==========================================================================
  // 1. LEER SENSORES
  //    "event" guarda los datos del acelerómetro y "gyro" los del giroscopio.
  // ==========================================================================

  sensors_event_t event;
  sensors_event_t gyro;

  mpu.getAccelerometerSensor()->getEvent(&event);
  mpu.getGyroSensor()->getEvent(&gyro);


  // ==========================================================================
  // 2. CALCULAR TIEMPO TRANSCURRIDO (dt)
  //    Para integrar la velocidad de giro hace falta saber cuánto tiempo pasó
  //    desde la vuelta anterior.
  // ==========================================================================

  unsigned long tiempoActual = millis();

  float dt = (tiempoActual - tiempoAnterior) / 1000.0;

  tiempoAnterior = tiempoActual;


  // ==========================================================================
  // 3. GIROSCOPIO
  //    El MPU entrega rad/s; se pasa a grados/s y se le resta el offset
  //    calculado en la calibración para eliminar el error en reposo.
  // ==========================================================================

  float gyroX = gyro.gyro.x * 180.0 / PI;
  float gyroY = gyro.gyro.y * 180.0 / PI;

  gyroX -= offsetGyroX;
  gyroY -= offsetGyroY;


  // ==========================================================================
  // 4. ÁNGULO SEGÚN EL ACELERÓMETRO
  //    Usando la dirección de la gravedad se calcula la inclinación con
  //    trigonometría (atan2). El resultado sale en radianes y se pasa a grados.
  //    Este ángulo es "absoluto" (no deriva) pero es ruidoso si hay vibración.
  // ==========================================================================

  // Roll: rotación alrededor de X (corresponde a gyroX)
  float anguloAcelerometroX = atan2(
    event.acceleration.y,
    event.acceleration.z
  ) * 180.0 / PI;

  // Pitch: rotación alrededor de Y (corresponde a gyroY)
  float anguloAcelerometroY = atan2(
    -event.acceleration.x,
    sqrt(
      event.acceleration.y * event.acceleration.y +
      event.acceleration.z * event.acceleration.z
    )
  ) * 180.0 / PI;


  // ==========================================================================
  // 5. FILTRO (combina giroscopio + acelerómetro)
  //
  //    Para cada eje se distinguen dos situaciones:
  //
  //    A) HAY MOVIMIENTO (el giroscopio supera el umbral):
  //       Se confía en el giroscopio: el ángulo se actualiza sumando
  //       velocidad x tiempo. Además se reinicia el contador de "quietud".
  //
  //    B) ESTÁ QUIETO:
  //       Si lleva quieto más de TIEMPO_ESPERA, se mezcla un poquito del
  //       ángulo del acelerómetro con el ángulo estimado. Así se corrige
  //       lentamente la deriva del giroscopio sin meter ruido del acelerómetro.
  //       La fórmula es un promedio ponderado: 98% estimado + 2% acelerómetro.
  // ==========================================================================

  // ---------- EJE X (roll) ----------

  if (abs(gyroX) > UMBRAL_GYRO) {

    // Estamos moviendo el MPU: integramos la velocidad de giro
    anguloEstimadoX += gyroX * dt;

    // Reiniciamos el contador de quietud
    tiempoQuietoX = millis();

  } else {

    // El MPU está prácticamente quieto en este eje.
    // Se corrige solo si:
    //  - el pitch no es muy grande (con pitch alto, el roll calculado con el
    //    acelerómetro deja de ser confiable), y
    //  - pasaron al menos 500 ms sin movimiento.
    if (fabs(anguloEstimadoY) < LIMITE_PITCH_CORR &&
        (millis() - tiempoQuietoX >= TIEMPO_ESPERA)) {

      // Corrección lenta de la deriva
      anguloEstimadoX =
        anguloEstimadoX * (1.0 - CORRECCION_DERIVA) +
        anguloAcelerometroX * CORRECCION_DERIVA;
    }
  }


  // ---------- EJE Y (pitch) ----------
  // Misma lógica que el eje X, pero sin la restricción del límite de pitch.

  if (abs(gyroY) > UMBRAL_GYRO) {

    anguloEstimadoY += gyroY * dt;

    tiempoQuietoY = millis();

  } else {

    if (millis() - tiempoQuietoY >= TIEMPO_ESPERA) {

      anguloEstimadoY =
        anguloEstimadoY * (1.0 - CORRECCION_DERIVA) +
        anguloAcelerometroY * CORRECCION_DERIVA;
    }
  }


  // ==========================================================================
  // 6. SERVOS (control integral)
  //
  //    El ángulo estimado es el "error": cuánto está inclinado el aparato
  //    respecto a la posición nivelada (0°). En cada ciclo se suma una parte
  //    de ese error (KI * error) a la compensación acumulada del servo.
  //    Mientras haya error, la compensación sigue creciendo; cuando el servo
  //    corrigió lo suficiente, el error llega a 0 y la compensación se frena.
  //    Por eso se llama control "integral".
  // ==========================================================================

  // Solo se acumula si el error supera la zona muerta (evita temblor por ruido)
  if (fabs(anguloEstimadoX) > ZONA_MUERTA) servoDeltaX += KI * anguloEstimadoX;
  if (fabs(anguloEstimadoY) > ZONA_MUERTA) servoDeltaY += KI * anguloEstimadoY;

  // Se limita la compensación para no forzar el mecanismo
  servoDeltaX = constrain(servoDeltaX, -LIMITE_DELTA, LIMITE_DELTA);
  servoDeltaY = constrain(servoDeltaY, -LIMITE_DELTA, LIMITE_DELTA);

  // Posición final de cada servo = centro +/- compensación.
  // En Y se RESTA porque ese servo está montado en sentido invertido.
  int posicionServoX = round(CENTRO_SERVO_X + servoDeltaX);
  int posicionServoY = round(CENTRO_SERVO_Y - servoDeltaY);

  // Los servos solo aceptan ángulos de 0 a 180
  posicionServoX = constrain(posicionServoX, 0, 180);
  posicionServoY = constrain(posicionServoY, 0, 180);

  // Se envía la orden a los servos
  servoX.write(posicionServoX);
  servoY.write(posicionServoY);


  // ==========================================================================
  // 7. MONITOR SERIE (solo para depuración)
  //    Una vez por segundo se imprimen los valores principales, útiles para
  //    verificar el funcionamiento y ajustar parámetros.
  //    (Se hace cada 1 s, y no en cada vuelta, para no ralentizar el control.)
  // ==========================================================================

  if (millis() - tiempoSerial >= 1000) {

    Serial.println("-----------------------------");

    // Ángulos calculados solo con el acelerómetro
    Serial.print("Acelerometro X: ");
    Serial.print(anguloAcelerometroX);
    Serial.print("° | Y: ");
    Serial.print(anguloAcelerometroY);
    Serial.println("°");

    // Velocidad de giro medida (ya con el offset restado)
    Serial.print("Gyro X: ");
    Serial.print(gyroX);
    Serial.print(" °/s | Gyro Y: ");
    Serial.print(gyroY);
    Serial.println(" °/s");

    // Ángulo final estimado por el filtro
    Serial.print("Angulo estimado X: ");
    Serial.print(anguloEstimadoX);
    Serial.print("° | Y: ");
    Serial.print(anguloEstimadoY);
    Serial.println("°");

    // Posición actual enviada a cada servo
    Serial.print("Servo X: ");
    Serial.print(posicionServoX);
    Serial.print("° | Servo Y: ");
    Serial.print(posicionServoY);
    Serial.println("°");

    tiempoSerial = millis();  // Se marca el momento de esta impresión
  }
}
