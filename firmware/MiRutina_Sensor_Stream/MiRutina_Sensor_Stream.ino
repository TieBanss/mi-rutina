#include <ArduinoBLE.h>
#include <LSM6DS3.h>
#include <Wire.h>

/*
 * MI RUTINA - SENSOR XIAO nRF52840 SENSE
 * Firmware de streaming de datos brutos por Bluetooth Low Energy (BLE)
 * Frecuencia: ~40 Hz (cada 25 ms)
 * Payload: 12 bytes (Ax, Ay, Az, Gx, Gy, Gz) en int16_t
 */

LSM6DS3 myIMU(I2C_MODE, 0x6A);

#define SERVICE_UUID           "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHAR_RAW_IMU_UUID      "19b10001-e8f2-537e-4f6c-d104768a1214"

// Estructura binaria compacta (12 bytes)
struct __attribute__((packed)) ImuRawPacket {
  int16_t ax; // Aceleración X en milli-G (ej: 1000 = 1.0G)
  int16_t ay; // Aceleración Y en milli-G
  int16_t az; // Aceleración Z en milli-G
  int16_t gx; // Giroscopio X en grados/s (dps)
  int16_t gy; // Giroscopio Y en grados/s
  int16_t gz; // Giroscopio Z en grados/s
};

BLEService routineService(SERVICE_UUID);
BLECharacteristic rawImuChar(CHAR_RAW_IMU_UUID, BLERead | BLENotify, sizeof(ImuRawPacket));

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL_MS = 25; // 40 Hz

void setup() {
  Serial.begin(115200);

  // Configurar LEDs (lógica invertida en XIAO: LOW = encendido, HIGH = apagado)
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  if (myIMU.begin() != 0) {
    digitalWrite(LED_RED, LOW); // LED Rojo si falla el IMU
    Serial.println("ERROR: No se detecta IMU.");
    while (1);
  }

  if (!BLE.begin()) {
    digitalWrite(LED_RED, LOW); // LED Rojo si falla el BLE
    Serial.println("ERROR: No se pudo iniciar BLE.");
    while (1);
  }

  // Nombre corto para asegurar que quepa en el paquete de anuncio BLE (31 bytes max)
  BLE.setDeviceName("MiRutina");
  BLE.setLocalName("MiRutina");
  BLE.setAdvertisedService(routineService);
  routineService.addCharacteristic(rawImuChar);
  BLE.addService(routineService);

  ImuRawPacket initPacket = {0, 0, 0, 0, 0, 0};
  rawImuChar.writeValue((byte*)&initPacket, sizeof(initPacket));

  BLE.advertise();
  digitalWrite(LED_BLUE, LOW); // LED Azul = Transmitiendo / Esperando conexión

  Serial.println("=========================================");
  Serial.println(">> SENSOR MI RUTINA TRANSMITIENDO <<");
  Serial.println("Nombre Bluetooth: MiRutina");
  Serial.println("=========================================");
}

void loop() {
  // Procesar eventos de radio BLE obligatoriamente
  BLE.poll();

  BLEDevice central = BLE.central();

  if (central && central.connected()) {
    // LED Verde encendido cuando está conectado
    digitalWrite(LED_BLUE, HIGH);
    digitalWrite(LED_GREEN, LOW);

    unsigned long now = millis();
    if (now - lastSend >= SEND_INTERVAL_MS) {
      lastSend = now;

      // Leer acelerómetro y giroscopio
      float ax = myIMU.readFloatAccelX();
      float ay = myIMU.readFloatAccelY();
      float az = myIMU.readFloatAccelZ();
      float gx = myIMU.readFloatGyroX();
      float gy = myIMU.readFloatGyroY();
      float gz = myIMU.readFloatGyroZ();

      // Empaquetar en enteros de 16 bits
      ImuRawPacket pkt;
      pkt.ax = (int16_t)(ax * 1000.0f);
      pkt.ay = (int16_t)(ay * 1000.0f);
      pkt.az = (int16_t)(az * 1000.0f);
      pkt.gx = (int16_t)(gx);
      pkt.gy = (int16_t)(gy);
      pkt.gz = (int16_t)(gz);

      rawImuChar.writeValue((byte*)&pkt, sizeof(pkt));
    }
  } else {
    // Desconectado: LED Azul fijo
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, LOW);
  }
}
